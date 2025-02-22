//===- CostSolver.cpp - Utils for instrumentation ---------------*- C++ -*-===//
//
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include <cfloat>
#include <climits>

#include "Common.h"
#include "CostSolver.h"

using namespace PIMProf;

/* ===================================================================== */
/* CostSolver */
/* ===================================================================== */
// void CostSolver::BBL2Func(UUIDHashMap<ThreadRunStats *> &bbl, UUIDHashMap<ThreadRunStats *> &func)
// {
//     // func.clear();
//     // for (auto &elem : bbl) {
        
//     // }
// }

// void CostSolver::BBL2Func(BBLIDDataReuse &bbl, FuncDataReuse &func)
// {

// }

// void CostSolver::BBL2Func(SwitchCountList &bbl, SwitchCountList &func)
// {

// }

void CostSolver::initialize(CommandLineParser *parser)
{
    _command_line_parser = parser;
    _batch_threshold = 0;
    _batch_size = 0;

    std::ifstream scaDecision(_command_line_parser->scaDecisionFile());
    std::ifstream decision(_command_line_parser->decisionFile());
    std::ifstream cpustats(_command_line_parser->cpustatsfile());
    std::ifstream pimstats(_command_line_parser->pimstatsfile());
    std::ifstream reuse(_command_line_parser->reusefile());
    assert(cpustats.is_open());
    assert(pimstats.is_open());
    assert(reuse.is_open());
    ParseDecision(decision);
    ParseSCADecision(scaDecision);
    ParseStats(cpustats, _bbl_hash2stats[CPU]);
    ParseStats(pimstats, _bbl_hash2stats[PIM]);
    ParseReuse(reuse, _bbl_data_reuse, _bbl_switch_count);

    // Convert BBLStats to FuncStats
    // BBL2Func(_bbl_hash2stats[CPU], _func_hash2stats[CPU]);
    // BBL2Func(_bbl_hash2stats[PIM], _func_hash2stats[PIM]);
    // BBL2Func(_bbl_data_reuse, _func_data_reuse);
    // BBL2Func(_bbl_switch_count, _func_switch_count);

    // temporarily define flush and fetch cost here
    _flush_cost[CostSite::CPU] = 60;
    _flush_cost[CostSite::PIM] = 30;
    _fetch_cost[CostSite::CPU] = 60;
    _fetch_cost[CostSite::PIM] = 30;
    _switch_cost[CostSite::CPU] = 800;
    _switch_cost[CostSite::PIM] = 800;
    _dataMoveThreshold = _command_line_parser->dataMoveThreshold;
    _mpki_threshold = 5;
    _parallelism_threshold = 15;
    _batch_threshold = 0.001;
    _batch_size = 10;
}

CostSolver::~CostSolver()
{
    for (int i = 0; i < MAX_COST_SITE; i++) {
        for (auto it : _bbl_hash2stats[i]) {
            delete it.second;
        }
    }
}

const std::vector<ThreadRunStats *>* CostSolver::getBBLSortedStats()
{
    if (_dirty) {

        SortStatsMap(_bbl_hash2stats[CPU], _bbl_sorted_stats[CPU]);
        // align CPU with PIM
        for (auto elem : _bbl_sorted_stats[CPU]) {
            UUID bblhash = elem->bblhash;
            BBLID bblid = elem->bblid;
            auto p = _bbl_hash2stats[PIM].find(bblhash);
            if (p != _bbl_hash2stats[PIM].end()) {
                _bbl_sorted_stats[PIM].push_back(p->second);
                p->second->bblid = bblid;
            }
            else {
                // create placeholder
                ThreadRunStats *stats = new ThreadRunStats(0, RunStats(bblid, bblhash));
                _bbl_hash2stats[PIM].insert(std::make_pair(bblhash, stats));
                _bbl_sorted_stats[PIM].push_back(stats);
            }
        }

        assert(_bbl_sorted_stats[CPU].size() == _bbl_sorted_stats[PIM].size());
        for (BBLID i = 0; i < (BBLID)_bbl_sorted_stats[CPU].size(); i++) {
            assert(_bbl_sorted_stats[CPU][i]->bblid == _bbl_sorted_stats[PIM][i]->bblid);
            assert(_bbl_sorted_stats[CPU][i]->bblhash == _bbl_sorted_stats[PIM][i]->bblhash);
        }
        _dirty = false;
    }
    return _bbl_sorted_stats;
}


void CostSolver::ParseDecision(std::istream &ifs)
{
    std::string line, token;

    while(std::getline(ifs, line)) {
        std::stringstream ss(line);
        UUID keyUUID;
        std::string value;
        ss >> std::hex >> keyUUID.first
            >> std::hex >> keyUUID.second
            >> value; 
        if(value=="PIM"){
            ctsDecision[keyUUID]=CostSite::PIM;
        }else if(value=="CPU"){
            ctsDecision[keyUUID]=CostSite::CPU;
        }else{
            assert(false);
        }  
    }
}

void CostSolver::ParseSCADecision(std::istream &ifs)
{
    std::string line, token;

    while(std::getline(ifs, line)) {
        std::stringstream ss(line);
        UUID keyUUID;
        std::string value;
        int curCycles;
        ss >> std::hex >> keyUUID.first
            >> std::hex >> keyUUID.second
            >> value >> std::dec >> curCycles;

        if(value=="PIM"){
            scaDecision[keyUUID]=CostSite::PIM;
        }else if(value=="Follower"){
            scaDecision[keyUUID]=CostSite::Follower;
        }else if(value=="CPU"){
            scaDecision[keyUUID]=CostSite::CPU;
        }else{
            assert(false);
        }  
    }
}

void CostSolver::ParseStats(std::istream &ifs, UUIDHashMap<ThreadRunStats *> &statsmap)
{
    std::string line, token;
    int tid = 0;
    while(std::getline(ifs, line)) {
        if (line.find(HORIZONTAL_LINE) != std::string::npos) { // skip next 2 lines
            std::getline(ifs, line);
            std::stringstream ss(line);
            ss >> token >> tid;
            std::getline(ifs, line);
            continue;
        }
        std::stringstream ss(line);

        RunStats bblstats;
        ss >> bblstats.bblid
           >> bblstats.elapsed_time
           >> bblstats.instruction_count
           >> bblstats.memory_access
           >> std::hex >> bblstats.bblhash.first >> bblstats.bblhash.second;
        assert(bblstats.elapsed_time >= 0);
        auto it = statsmap.find(bblstats.bblhash);
        if (statsmap.find(bblstats.bblhash) == statsmap.end()) {
            ThreadRunStats *p = new ThreadRunStats(tid, bblstats);
            statsmap.insert(std::make_pair(bblstats.bblhash, p));
        }
        else {
            it->second->MergeStats(tid, bblstats);
        }
    }
}

void CostSolver::PrintStats(std::ostream &ofs)
{
    const std::vector<ThreadRunStats *> *sorted = getBBLSortedStats();

    for (int i = 0; i < MAX_COST_SITE; ++i) {
        ofs << HORIZONTAL_LINE << std::endl;
        ofs << std::setw(7) << "BBLID"
            << std::setw(15) << "Time(ns)"
            << std::setw(15) << "Instruction"
            << std::setw(15) << "Memory Access"
            << std::setw(18) << "Hash(hi)"
            << std::setw(18) << "Hash(lo)"
            << std::endl;
        for (auto it = sorted[i].begin(); it != sorted[i].end(); ++it)
        {
            UUID bblhash = (*it)->bblhash;
            ofs << std::setw(7) << (*it)->bblid
                << std::setw(15) << (*it)->elapsed_time
                << std::setw(15) << (*it)->instruction_count
                << std::setw(15) << (*it)->memory_access
                << "  " << std::hex
                << std::setfill('0') << std::setw(16) << bblhash.first
                << "  "
                << std::setfill('0') << std::setw(16) << bblhash.second
                << std::setfill(' ') << std::dec << std::endl;
        }
    }
    
}

void CostSolver::ParseReuse(std::istream &ifs, DataReuse<BBLID> &reuse, SwitchCountList &switchcnt)
{
    std::string line, token;

    // we parses reuse segments and BBL switch counts at the same time
    bool isreusesegment = true; 
    
    while(std::getline(ifs, line)) {
        if (line.find(HORIZONTAL_LINE) != std::string::npos) {
            std::getline(ifs, line);
            std::stringstream ss(line);
            ss >> token;
            if (token == "ReuseSegment") {
                isreusesegment = true;
            }
            else if (token == "BBLSwitchCount") {
                isreusesegment = false;
            }
            else { assert(0); }
            continue;
        }
        if (isreusesegment) {
            std::stringstream ss(line);
            BBLIDDataReuseSegment seg;
            ss >> token >> token >> token; // example: head = 208,
            BBLID head = std::stoi(token.substr(0, token.size() - 1)); //example:  208,
            int64_t count;
            ss >> token >> token >> count; //example: count = 4
            ss >> token;
            BBLID bblid;
            BBLID prebblid = head;
            while (ss >> bblid) {
                seg.insert(bblid);
                interBB_CL_DM[{std::min(bblid,prebblid),std::max(bblid,prebblid)}]+=count;
                prebblid = bblid;
            }
            seg.setHead(head);
            if (count < 0) {
                errormsg("count < 0 for line ``%s''", line.c_str());
                assert(count >= 0);
            }
            seg.setCount(count);
            reuse.UpdateTrie(reuse.getRoot(), &seg);
        }
        else {
            std::stringstream ss(line);
            BBLID fromidx;
            ss >> token >> token >> fromidx >> token;
            std::vector<std::pair<BBLID, uint64_t>> toidxvec;
            while (ss >> token) {
                size_t delim = token.find(':');
                BBLID toidx = stoull(token.substr(0, delim));
                uint64_t count = stoull(token.substr(delim + 1));
                
                interBB_REG_DM[{std::min(fromidx,toidx),std::max(fromidx,toidx)}]+=count;
                toidxvec.push_back(std::make_pair(toidx, count));
            }
            switchcnt.RowInsert(fromidx, toidxvec);
        }
    }
    switchcnt.Sort();

    // std::ofstream ofs("graph.dot", std::ios::out);
    // reuse.PrintDotGraph(ofs, [](BBLID bblid){ return bblid; });

    // reuse.PrintAllSegments(std::cout, [](BBLID bblid){ return bblid; });
}

DECISION CostSolver::PrintSolution(std::ostream &ofs)
{
    DECISION decision;
    DECISION scaPrintDecision;
    DECISION ctsPrintDecision;
    
    if (_command_line_parser->mode() == CommandLineParser::Mode::MPKI) {
        ofs << "CPU only time (ns): " << ElapsedTime(CPU) << std::endl
            << "PIM only time (ns): " << ElapsedTime(PIM) << std::endl;
        decision = PrintMPKIStats(ofs);
    }
    if (_command_line_parser->mode() == CommandLineParser::Mode::REUSE) {
        ofs << "CPU only time (ns): " << ElapsedTime(CPU) << std::endl
            << "PIM only time (ns): " << ElapsedTime(PIM) << std::endl;

        const std::vector<ThreadRunStats *> *sorted = getBBLSortedStats();
        uint64_t instr_cnt = 0;
        for (int i = 0; i < (int)sorted[CPU].size(); i++) {
            instr_cnt += sorted[CPU][i]->instruction_count;
        }
        ofs << "Instruction " << instr_cnt << std::endl;
        PrintMPKIStats(ofs);
        PrintGreedyStats(ofs);
        decision = PrintReuseStats(ofs);
        ctsPrintDecision = PrintCTSStatsFromfile(ctsDecision, ofs);
        PrintSCAStatsFromfile(scaDecision, ofs);
        bestSCAResult minSCAResult(INT_MAX);
        for (int i = 0; i < 100 ; i+=10){
            for (int k = 0; k < 10 ; k+=1){
                for (float j = 0; j < 0.02 ; j+=0.002){
                    minSCAResult = std::min(minSCAResult,  PrintSCAStats(i, k, j));
                }
            }
        }
        minSCAResult.print(ofs);
    }
    if (_command_line_parser->mode() == CommandLineParser::Mode::DEBUG) {
        ofs << "CPU only time (ns): " << ElapsedTime(CPU) << std::endl
            << "PIM only time (ns): " << ElapsedTime(PIM) << std::endl;
        decision = Debug_HierarchicalDecision(ofs);
    }

    PrintDecision(ofs, decision, ctsPrintDecision,false);
    ofs << delayCout.str();

    return decision;
}

// std::ostream & CostSolver::PrintDecision(std::ostream &ofs, const DECISION &decision, bool toscreen)
// {
//     const std::vector<ThreadRunStats *> *sorted = getBBLSortedStats();
//     ofs << HORIZONTAL_LINE << std::endl;
//     if (toscreen == true) {
//         for (uint32_t i = 0; i < decision.size(); i++) {
//             ofs << i << ":"
//                 << getCostSiteString(decision[i])
//                 << " ";
//         }
//         ofs << std::endl;
//     }
//     else {
//         ofs << std::setw(7) << "BBLID"
//             << std::setw(10) << "Decision"
//             << std::setw(14) << "Parallelism"
//             << std::setw(15) << "CPU"
//             << std::setw(15) << "PIM"
//             << std::setw(15) << "Difference"
//             << std::setw(18) << "Hash(hi)"
//             << std::setw(18) << "Hash(lo)"
//             << std::endl;
//         for (uint32_t i = 0; i < sorted[CPU].size(); i++) {
//             auto *cpustats = sorted[CPU][i];
//             auto *pimstats = sorted[PIM][i];
//             COST diff = cpustats->MaxElapsedTime() - pimstats->MaxElapsedTime();
//             ofs << std::setw(7) << i
//                 << std::setw(10) << getCostSiteString(decision[i])
//                 << std::setw(14) << pimstats->parallelism()
//                 << std::setw(15) << cpustats->MaxElapsedTime()
//                 << std::setw(15) << pimstats->MaxElapsedTime()
//                 << std::setw(15) << diff
//                 << "  " << std::hex
//                 << std::setfill('0') << std::setw(16) << cpustats->bblhash.first
//                 << "  "
//                 << std::setfill('0') << std::setw(16) << cpustats->bblhash.second
//                 << std::setfill(' ') << std::dec << std::endl;
//         }
//     }
//     return ofs;
// }

void CostSolver::redecideSCAByCLDM(DECISION &scaPrintDecision){
    auto &ofs = delayCout;
    ofs << HORIZONTAL_LINE << std::endl;
    ofs << "Re-decide SCA decision by cache-line data movement" << std::endl;
    // cluster top 10 Cache line data move, Decide follows static decision if others is Follower, conflict whatever for now
    // 
    TopReuseBBPairs(scaPrintDecision);

}

std::ostream & CostSolver::PrintDecision(std::ostream &ofs, const DECISION &decision, const DECISION &scaPrintDecision, bool toscreen)
{
    const std::vector<ThreadRunStats *> *sorted = getBBLSortedStats();
    ofs << HORIZONTAL_LINE << std::endl;
    if (toscreen == true) {
        for (uint32_t i = 0; i < decision.size(); i++) {
            ofs << i << ":"
                << getCostSiteString(decision[i])
                << " ";
        }
        ofs << std::endl;
    }
    else {
        std::stringstream IncorrectCPUDecision;
        std::stringstream IncorrectPIMDecision;
        const float showThrehold = 0.005;
        std::map<COST, uint32_t> top10PIMProfBB;
        std::map<COST, uint32_t> top10SCABB;
        COST PIMProfCost = Cost(decision, _bbl_data_reuse.getRoot(), _bbl_switch_count);
        COST scaCost = Cost(scaPrintDecision, _bbl_data_reuse.getRoot(), _bbl_switch_count);
        COST threshold = (1e+7);
        COST potential=0;
        std::vector<BBCOUNT> bbcount = bbTimesFromSwitchInfo(decision, _bbl_switch_count);
        ofs << std::setw(7) << "BBLID"
            << std::setw(10) << "Decision"
            << std::setw(12) << "ctsDecision"
            << std::setw(12) << "scaDecision"
            << std::setw(14) << "Parallelism"
            << std::setw(14) << "bbCount"
            << std::setw(15) << "CPU"
            << std::setw(15) << "PIM"
            << std::setw(15) << "Difference"
            << std::setw(21) << "Hash(hi)"
            << std::setw(21) << "Hash(lo)"
            << std::endl;
        IncorrectCPUDecision << std::setw(7) << "BBLID"
            << std::setw(10) << "Decision"
            << std::setw(12) << "ctsDecision"
            << std::setw(12) << "scaDecision"
            << std::setw(14) << "Parallelism"
            << std::setw(14) << "bbCount"
            << std::setw(15) << "CPU"
            << std::setw(15) << "PIM"
            << std::setw(15) << "Difference"
            << std::setw(21) << "Hash(hi)"
            << std::setw(21) << "Hash(lo)"
            << std::endl;
        IncorrectPIMDecision << std::setw(7) << "BBLID"
            << std::setw(10) << "Decision"
            << std::setw(12) << "ctsDecision"
            << std::setw(12) << "scaDecision"
            << std::setw(14) << "Parallelism"
            << std::setw(14) << "bbCount"
            << std::setw(15) << "CPU"
            << std::setw(15) << "PIM"
            << std::setw(15) << "Difference"
            << std::setw(21) << "Hash(hi)"
            << std::setw(21) << "Hash(lo)"
            << std::endl; 
        for (uint32_t i = 0; i < sorted[CPU].size(); i++) {
            auto *cpustats = sorted[CPU][i];
            auto *pimstats = sorted[PIM][i];
            COST diff = cpustats->MaxElapsedTime() - pimstats->MaxElapsedTime();
            ofs << std::setw(7) << std::dec << i
                << std::setw(10) << getCostSiteString(decision[i])
                << std::setw(12) << getCostSiteString(scaPrintDecision[i])
                << std::setw(12) << getCostSiteString(scaDecision[cpustats->bblhash])
                << std::setw(14) << std::dec << pimstats->parallelism()
                << std::setw(14) << std::dec << bbcount[i]
                << std::setw(15) << cpustats->MaxElapsedTime()
                << std::setw(15) << pimstats->MaxElapsedTime()
                << std::setw(15) << diff
                << "  "
                << std::setw(21) << std::hex << cpustats->bblhash.first
                << "  "
                << std::setw(21) << std::hex << cpustats->bblhash.second
                << std::setfill(' ') << std::endl;
            COST PIMProfBBCost = sorted[decision[i]][i]->MaxElapsedTime();
            COST scaBBCost = sorted[scaPrintDecision[i]][i]->MaxElapsedTime();
            if(PIMProfBBCost > showThrehold * PIMProfCost){
                top10PIMProfBB[PIMProfBBCost] = i;
            }
            if(scaBBCost > showThrehold * scaCost){
                top10SCABB[scaBBCost] = i;
            }
            if(diff > threshold && getCostSiteString(scaPrintDecision[i])=="C"){
                IncorrectCPUDecision << std::setw(7) << std::dec << i
                << std::setw(10) << getCostSiteString(decision[i])
                << std::setw(12) << getCostSiteString(scaPrintDecision[i])
                << std::setw(12) << getCostSiteString(scaDecision[cpustats->bblhash])
                << std::setw(14) << std::dec << pimstats->parallelism()
                << std::setw(14) << std::dec << bbcount[i]
                << std::setw(15) << cpustats->MaxElapsedTime()
                << std::setw(15) << pimstats->MaxElapsedTime()
                << std::setw(15) << diff
                << "  "
                << std::setw(21) << std::hex << cpustats->bblhash.first
                << "  "
                << std::setw(21) << std::hex << cpustats->bblhash.second
                << std::setfill(' ') << std::endl;
                potential += std::abs(diff);
            }
            if(diff < -threshold && getCostSiteString(scaPrintDecision[i])=="P"){
                IncorrectPIMDecision << std::setw(7) << std::dec << i
                << std::setw(10) << getCostSiteString(decision[i])
                << std::setw(12) << getCostSiteString(scaPrintDecision[i])
                << std::setw(12) << getCostSiteString(scaDecision[cpustats->bblhash])
                << std::setw(14) << std::dec << pimstats->parallelism()
                << std::setw(14) << std::dec << bbcount[i]
                << std::setw(15) << cpustats->MaxElapsedTime()
                << std::setw(15) << pimstats->MaxElapsedTime()
                << std::setw(15) << diff
                << "  "
                << std::setw(21) << std::hex << cpustats->bblhash.first
                << "  "
                << std::setw(21) << std::hex << cpustats->bblhash.second
                << std::setfill(' ') << std::endl;
                potential += std::abs(diff);
            }
        }
        // half threshold if  IncorrectDecision is empty
        while(potential==0){
            threshold = threshold/10;
            for (uint32_t i = 0; i < sorted[CPU].size(); i++) {
                auto *cpustats = sorted[CPU][i];
                auto *pimstats = sorted[PIM][i];
                COST diff = cpustats->MaxElapsedTime() - pimstats->MaxElapsedTime();
                if(diff > threshold && getCostSiteString(scaPrintDecision[i])=="C"){
                    IncorrectCPUDecision << std::setw(7) << std::dec << i
                    << std::setw(10) << getCostSiteString(decision[i])
                    << std::setw(12) << getCostSiteString(scaPrintDecision[i])
                    << std::setw(12) << getCostSiteString(scaDecision[cpustats->bblhash])
                    << std::setw(14) << std::dec << pimstats->parallelism()
                    << std::setw(14) << std::dec << bbcount[i]
                    << std::setw(15) << cpustats->MaxElapsedTime()
                    << std::setw(15) << pimstats->MaxElapsedTime()
                    << std::setw(15) << diff
                    << "  "
                    << std::setw(21) << std::hex << cpustats->bblhash.first
                    << "  "
                    << std::setw(21) << std::hex << cpustats->bblhash.second
                    << std::setfill(' ') << std::endl;
                    potential += std::abs(diff);
                }
                if(diff < -threshold && getCostSiteString(scaPrintDecision[i])=="P"){
                    IncorrectPIMDecision << std::setw(7) << std::dec <<  i
                    << std::setw(10) << getCostSiteString(decision[i])
                    << std::setw(12) << getCostSiteString(scaPrintDecision[i])
                    << std::setw(12) << getCostSiteString(scaDecision[cpustats->bblhash])
                    << std::setw(14) << std::dec << pimstats->parallelism()
                    << std::setw(14) << std::dec << bbcount[i]
                    << std::setw(15) << cpustats->MaxElapsedTime()
                    << std::setw(15) << pimstats->MaxElapsedTime()
                    << std::setw(15) << diff
                    << "  "
                    << std::setw(21) << std::hex << cpustats->bblhash.first
                    << "  "
                    << std::setw(21) << std::hex << cpustats->bblhash.second
                    << std::setfill(' ') << std::endl;
                    potential += std::abs(diff);
                }
            }
        }
        // Print top10PIMProfBB[PIMProfBBCost] = i;
        ofs << HORIZONTAL_LINE << std::endl;
        ofs << "top10PIMProfBB" << std::endl;
        ofs << std::setw(7) << "BBLID"
            << std::setw(10) << "Decision"
            << std::setw(12) << "ctsDecision"
            // << std::setw(12) << "scaDecision"
            << std::setw(14) << "Parallelism"
            << std::setw(14) << "bbCount"
            << std::setw(15) << "CPU"
            << std::setw(15) << "PIM"
            << std::setw(15) << "Percentage"
            << std::setw(15) << "Difference"
            << std::setw(21) << "Hash(hi)"
            << std::setw(21) << "Hash(lo)"
            << std::endl;
        COST sumofShowBBTime  = 0;
        for(auto it = top10PIMProfBB.begin(); it != top10PIMProfBB.end(); it++){
            auto key = it->first;
            auto i = it->second;
            auto *cpustats = sorted[CPU][i];
            auto *pimstats = sorted[PIM][i];
            COST diff = cpustats->MaxElapsedTime() - pimstats->MaxElapsedTime();
            ofs << std::setw(7) << std::dec << i
                << std::setw(10) << getCostSiteString(decision[i])
                << std::setw(12) << getCostSiteString(scaPrintDecision[i])
                << std::setw(14) << std::dec << pimstats->parallelism()
                << std::setw(14) << std::dec << bbcount[i]
                << std::setw(15) << cpustats->MaxElapsedTime()
                << std::setw(15) << pimstats->MaxElapsedTime()
                << std::setw(15) << key*100/PIMProfCost 
                << std::setw(15) << diff
                << "  "
                << std::setw(21) << std::hex << cpustats->bblhash.first
                << "  "
                << std::setw(21) << std::hex << cpustats->bblhash.second
                << std::setfill(' ') << std::endl;
                sumofShowBBTime += key;
        }
        ofs << "ShowBBTime: " << sumofShowBBTime*100/PIMProfCost << " %" << std::endl;
        // Print top10SCABB[] = i;
        ofs << HORIZONTAL_LINE << std::endl;
        ofs << "top10SCABB" << std::endl;
        ofs << std::setw(7) << "BBLID"
            << std::setw(10) << "Decision"
            << std::setw(12) << "ctsDecision"
            // << std::setw(12) << "scaDecision"
            << std::setw(14) << "Parallelism"
            << std::setw(14) << "bbCount"
            << std::setw(15) << "CPU"
            << std::setw(15) << "PIM"
            << std::setw(15) << "Percentage"
            << std::setw(15) << "Difference"
            << std::setw(21) << "Hash(hi)"
            << std::setw(21) << "Hash(lo)"
            << std::endl;
        sumofShowBBTime  = 0;
        for(auto it = top10SCABB.begin(); it != top10SCABB.end(); it++){
            auto key = it->first;
            auto i = it->second;
            auto *cpustats = sorted[CPU][i];
            auto *pimstats = sorted[PIM][i];
            COST diff = cpustats->MaxElapsedTime() - pimstats->MaxElapsedTime();
            ofs << std::setw(7) << std::dec << i
                << std::setw(10) << getCostSiteString(decision[i])
                << std::setw(12) << getCostSiteString(scaPrintDecision[i])
                << std::setw(14) << std::dec << pimstats->parallelism()
                << std::setw(14) << std::dec << bbcount[i]
                << std::setw(15) << cpustats->MaxElapsedTime()
                << std::setw(15) << pimstats->MaxElapsedTime()
                << std::setw(15) << key*100/scaCost 
                << std::setw(15) << diff
                << "  "
                << std::setw(21) << std::hex << cpustats->bblhash.first
                << "  "
                << std::setw(21) << std::hex << cpustats->bblhash.second
                << std::setfill(' ') << std::endl;
                sumofShowBBTime += key;
        }
        ofs << "ShowBBTime: " << sumofShowBBTime*100/scaCost << " %" << std::endl;
        // Print IncorrectCPUDecision
        ofs << HORIZONTAL_LINE << std::endl;
        ofs << "IncorrectCPUDecision" << std::endl;
        ofs << IncorrectCPUDecision.str();
        // Print IncorrectPIMDecision
        ofs << HORIZONTAL_LINE << std::endl;
        ofs << "IncorrectPIMDecision" << std::endl;
        ofs << IncorrectPIMDecision.str();
        // optimize potential
        auto pimprofTime = ElapsedTime(decision);
        ofs << "Optimize potential " << potential/(pimprofTime.first + pimprofTime.second) << std::endl;
        ReuseCostPrint(scaPrintDecision, _bbl_data_reuse.getRoot(), ofs);
    }
    return ofs;
}

DECISION CostSolver::PrintMPKIStats(std::ostream &ofs)
{
    const std::vector<ThreadRunStats *> *sorted = getBBLSortedStats();
    DECISION decision;
    uint64_t pim_total_instr = 0;
    for (auto it = sorted[PIM].begin(); it != sorted[PIM].end(); ++it) {
        pim_total_instr += (*it)->instruction_count;
    }

    uint64_t instr_threshold = pim_total_instr * 0.01;
    
    for (BBLID i = 0; i < (BBLID)sorted[CPU].size(); ++i) {
        auto *cpustats = sorted[CPU][i];
        auto *pimstats = sorted[PIM][i];

        double instr = pimstats->instruction_count;
        double mem = pimstats->memory_access;
        double mpki = mem / instr * 1000.0;
        int para = pimstats->parallelism();

        // deal with the part that is not inside any BBL
        if (cpustats->bblhash == GLOBAL_BBLHASH) {
            decision.push_back(CostSite::CPU);
            continue;
        }

        if (mpki > _mpki_threshold && para > _parallelism_threshold && instr > instr_threshold) {
            std::cout << para << std::endl;
            decision.push_back(CostSite::PIM);
        }
        else {
            decision.push_back(CostSite::CPU);
        }
    }

    COST reuse_cost = ReuseCost(decision, _bbl_data_reuse.getRoot());
    COST switch_cost = SwitchCost(decision, _bbl_switch_count);
    auto elapsed_time = ElapsedTime(decision);
    COST total_time = reuse_cost + switch_cost + elapsed_time.first + elapsed_time.second;
    assert(total_time == Cost(decision, _bbl_data_reuse.getRoot(), _bbl_switch_count));

    ofs << "MPKI offloading time (ns): " << total_time << " = CPU " << elapsed_time.first << " + PIM " << elapsed_time.second << " + REUSE " << reuse_cost << " + SWITCH " << switch_cost << std::endl;

    return decision;
}

DECISION CostSolver::PrintCTSStatsFromfile(DecisionFromFile decisionFromFile, std::ostream &ofs){
    const std::vector<ThreadRunStats *> *sorted = getBBLSortedStats();
    DECISION decision;
    for (BBLID i = 0; i < (BBLID)sorted[CPU].size(); ++i) {
        auto *cpustats = sorted[CPU][i];
        auto *pimstats = sorted[PIM][i];
        // deal with the part that is not inside any BBL
        if (cpustats->bblhash == MAIN_BBLHASH) {         
            if (cpustats->MaxElapsedTime() <= pimstats->MaxElapsedTime()) {
                decision.push_back(CPU);
            }else {
                decision.push_back(PIM);
            }
        }else if(decisionFromFile.count(cpustats->bblhash)){
            auto tmpdecide = decisionFromFile[cpustats->bblhash];
            decision.push_back(tmpdecide);
        }else{
            decision.push_back(CostSite::CPU);
        }
    }
    COST reuse_cost = ReuseCost(decision, _bbl_data_reuse.getRoot());
    COST switch_cost = SwitchCost(decision, _bbl_switch_count);
    auto elapsed_time = ElapsedTime(decision);
    COST total_time = reuse_cost + switch_cost + elapsed_time.first + elapsed_time.second;
    assert(total_time == Cost(decision, _bbl_data_reuse.getRoot(), _bbl_switch_count));

    ofs << "CTS offloading time (ns): " << total_time << " = CPU " << elapsed_time.first << " + PIM " << elapsed_time.second << " + REUSE " << reuse_cost << " + SWITCH " << switch_cost << std::endl;
    // ofs << "SCA configuration: " << " sca_mpki_threshold: " << sca_mpki_threshold \
    //     << " sca_parallelism_threshold: " << sca_parallelism_threshold \
    //     << " instr_threshold_percentage: " << instr_threshold_percentage \
    //     << std::endl;
    return decision;
}

DECISION CostSolver::PrintSCAStatsFromfile(DecisionFromFile decisionFromFile, std::ostream &ofs){
    const std::vector<ThreadRunStats *> *sorted = getBBLSortedStats();
    DECISION decision;
    CostSite preCostSite = CostSite::PIM;
    for (BBLID i = 0; i < (BBLID)sorted[CPU].size(); ++i) {
        auto *cpustats = sorted[CPU][i];
        auto *pimstats = sorted[PIM][i];
        // deal with the part that is not inside any BBL
        if (cpustats->bblhash == MAIN_BBLHASH) {         
            if (cpustats->MaxElapsedTime() <= pimstats->MaxElapsedTime()) {
                decision.push_back(CPU);
            }else {
                decision.push_back(PIM);
            }
        }else if(decisionFromFile.count(cpustats->bblhash)){
            auto tmpdecide = decisionFromFile[cpustats->bblhash];
            if(tmpdecide==CostSite::Follower)
                decision.push_back(preCostSite);
            else
                decision.push_back(tmpdecide);
        }else{
            decision.push_back(CostSite::CPU);
        }
        preCostSite = *decision.rbegin();
    }
    redecideSCAByCLDM(decision);

    COST reuse_cost = ReuseCost(decision, _bbl_data_reuse.getRoot());
    COST switch_cost = SwitchCost(decision, _bbl_switch_count);
    auto elapsed_time = ElapsedTime(decision);
    COST total_time = reuse_cost + switch_cost + elapsed_time.first + elapsed_time.second;
    assert(total_time == Cost(decision, _bbl_data_reuse.getRoot(), _bbl_switch_count));

    ofs << "SCAFromfile offloading time (ns): " << total_time << " = CPU " << elapsed_time.first << " + PIM " << elapsed_time.second << " + REUSE " << reuse_cost << " + SWITCH " << switch_cost << std::endl;
    // ofs << "SCA configuration: " << " sca_mpki_threshold: " << sca_mpki_threshold \
    //     << " sca_parallelism_threshold: " << sca_parallelism_threshold \
    //     << " instr_threshold_percentage: " << instr_threshold_percentage \
    //     << std::endl;
    return decision;
}

CostSolver::bestSCAResult CostSolver::PrintSCAStats(
                                    int sca_mpki_threshold, \
                                    int sca_parallelism_threshold, 
                                    float instr_threshold_percentage)
{
    const std::vector<ThreadRunStats *> *sorted = getBBLSortedStats();
    DECISION decision;
    uint64_t pim_total_instr = 0;
    for (auto it = sorted[PIM].begin(); it != sorted[PIM].end(); ++it) {
        pim_total_instr += (*it)->instruction_count;
    }

    uint64_t instr_threshold = pim_total_instr * instr_threshold_percentage;
    
    for (BBLID i = 0; i < (BBLID)sorted[CPU].size(); ++i) {
        auto *cpustats = sorted[CPU][i];
        auto *pimstats = sorted[PIM][i];

        double instr = pimstats->instruction_count;
        double mem = pimstats->memory_access;
        double mpki = mem / instr * 1000.0;
        int para = pimstats->parallelism();

        // deal with the part that is not inside any BBL
        if (cpustats->bblhash == GLOBAL_BBLHASH) {
            decision.push_back(CostSite::CPU);
            continue;
        }

        if (mpki >= sca_mpki_threshold && para > sca_parallelism_threshold && instr >= instr_threshold) {
            std::cout << para << std::endl;
            decision.push_back(CostSite::PIM);
        }
        else {
            decision.push_back(CostSite::CPU);
        }
    }

    COST reuse_cost = ReuseCost(decision, _bbl_data_reuse.getRoot());
    COST switch_cost = SwitchCost(decision, _bbl_switch_count);
    auto elapsed_time = ElapsedTime(decision);
    COST total_time = reuse_cost + switch_cost + elapsed_time.first + elapsed_time.second;
    assert(total_time == Cost(decision, _bbl_data_reuse.getRoot(), _bbl_switch_count));

    bestSCAResult result(total_time, elapsed_time, reuse_cost, switch_cost, sca_mpki_threshold, sca_parallelism_threshold, instr_threshold_percentage);
    // ofs << "SCA offloading time (ns): " << total_time << " = CPU " << elapsed_time.first << " + PIM " << elapsed_time.second << " + REUSE " << reuse_cost << " + SWITCH " << switch_cost << std::endl;
    // ofs << "SCA configuration: " << " sca_mpki_threshold: " << sca_mpki_threshold \
    //     << " sca_parallelism_threshold: " << sca_parallelism_threshold \
    //     << " instr_threshold_percentage: " << instr_threshold_percentage \
    //     << std::endl;
    return result;
}

DECISION CostSolver::PrintGreedyStats(std::ostream &ofs)
{
    const std::vector<ThreadRunStats *> *sorted = getBBLSortedStats();
    DECISION decision;
    for (BBLID i = 0; i < (BBLID)sorted[CPU].size(); ++i) {
        auto *cpustats = sorted[CPU][i];
        auto *pimstats = sorted[PIM][i];
        if (cpustats->MaxElapsedTime() <= pimstats->MaxElapsedTime()) {
            decision.push_back(CPU);
        }
        else {
            decision.push_back(PIM);
        }
    }
    COST reuse_cost = ReuseCost(decision, _bbl_data_reuse.getRoot());
    COST switch_cost = SwitchCost(decision, _bbl_switch_count);
    auto elapsed_time = ElapsedTime(decision);
    COST total_time = reuse_cost + switch_cost + elapsed_time.first + elapsed_time.second;
    assert(total_time == Cost(decision, _bbl_data_reuse.getRoot(), _bbl_switch_count));

    ofs << "Greedy offloading time (ns): " << total_time << " = CPU " << elapsed_time.first << " + PIM " << elapsed_time.second << " + REUSE " << reuse_cost << " + SWITCH " << switch_cost << std::endl;

    return decision;
}


// this function does not check whether there is duplicate BBLID in cur_batch
COST CostSolver::PermuteDecision(DECISION &decision, const std::vector<BBLID> &cur_batch, const BBLIDTrieNode *partial_root)
{

    int cur_batch_size = cur_batch.size();
    assert(cur_batch_size < 64);
    COST cur_total = FLT_MAX;
    DECISION temp_decision = decision;
    // find optimal in this batch
    uint64_t permute = (1 << cur_batch_size) - 1;
    
    for (; permute != (uint64_t)(-1); permute--) {
        for (int j = 0; j < cur_batch_size; j++) {
            if ((permute >> j) & 1)
                temp_decision[cur_batch[j]] = PIM;
            else
                temp_decision[cur_batch[j]] = CPU;
        }
        // PrintDecision(std::cout, temp_decision, true);
        COST temp_total = Cost(temp_decision, partial_root, _bbl_switch_count);
        if (temp_total < cur_total) {
            cur_total = temp_total;
            decision = temp_decision; 
        }
    }

    return cur_total;
}

// DECISION CostSolver::PrintReuseStats(std::ostream &ofs)
// {
//     _bbl_data_reuse.SortLeaves();

//     COST reuse_max = SingleSegMaxReuseCost();

//     //initialize all decision to INVALID
//     DECISION decision;
//     decision.resize(_bbl_hash2stats[CPU].size(), INVALID);
//     COST cur_total = FLT_MAX;
//     int seg_count = INT_MAX;

//     BBLIDTrieNode *partial_root = new BBLIDTrieNode();
//     BBLIDDataReuseSegment allidset;
//     int cur_node = 0;
//     int leaves_size = _bbl_data_reuse.getLeaves().size();

//     int batch_cnt = 0;
//     while(cur_node < leaves_size) {
//         std::vector<BBLID> cur_batch;
//         // insert segments until the number of different BBLs exceeds _batch_size or all nodes are added
//         while (cur_node < leaves_size) {
//             BBLIDDataReuseSegment seg;
//             _bbl_data_reuse.ExportSegment(&seg, _bbl_data_reuse.getLeaves()[cur_node]);
//             std::vector<BBLID> diff = seg.diff(allidset);
//             // std::cout << cur_batch.size() << " " << diff.size() << std::endl;
//             cur_node++;
//             // ignore too long segments
//             if (diff.size() > (unsigned)_batch_size) continue;
            
//             allidset.insert(seg);
//             cur_batch.insert(cur_batch.end(), diff.begin(), diff.end());
//             _bbl_data_reuse.UpdateTrie(partial_root, &seg);

//             seg_count = seg.getCount();

//             if (cur_batch.size() + diff.size() > (unsigned)_batch_size) break;
//         }

//         std::cout << "batch = " << batch_cnt << ", size = " << cur_batch.size() << std::endl;

//         cur_total = PermuteDecision(decision, cur_batch, partial_root);

//         for (auto elem : cur_batch) {
//             std::cout << elem << getCostSiteString(decision[elem]) << " ";
//         }

//         std::cout << "seg_count = " << seg_count << ", reuse_max = " << reuse_max << ", cur_total = " << cur_total << std::endl;
//         std::cout << std::endl;
//         if (seg_count * reuse_max < _batch_threshold * cur_total) break;
//         batch_cnt++;
//     }
//     // std::ofstream ofs("temp.dot", std::ofstream::out);
//     // _cost_package->_bbl_data_reuse.print(ofs, partial_root);
//     // ofs.close();

//     _bbl_data_reuse.DeleteTrie(partial_root);

//     const std::vector<ThreadRunStats *> *sorted = getBBLSortedStats();

//     // assign decision for BBLs that did not occur in the reuse chains
//     for (BBLID i = 0; i < (BBLID)sorted[CPU].size(); ++i) {
//         auto *cpustats = sorted[CPU][i];
//         auto *pimstats = sorted[PIM][i];

//         if (decision[i] == INVALID) {
//             if (cpustats->MaxElapsedTime() <= pimstats->MaxElapsedTime()) {
//                 decision[i] = CPU;
//             }
//             else {
//                 decision[i] = PIM;
//             }
//         }
//     }

//     cur_total = Cost(decision, _bbl_data_reuse.getRoot(), _bbl_switch_count);
//     std::cout << "cur_total = " << cur_total << std::endl;
//     // iterate over the remaining BBs 5 times until convergence
//     for (int j = 0; j < 2; ++j) {
//         for (BBLID id = 0; id < (BBLID)sorted[CPU].size(); id++) {
//             // swap decision[id] and check if it reduces overhead
//             decision[id] = (decision[id] == CPU ? PIM : CPU);
//             COST temp_total = Cost(decision, _bbl_data_reuse.getRoot(), _bbl_switch_count);
//             if (temp_total > cur_total) {
//                 decision[id] = (decision[id] == CPU ? PIM : CPU);
//             }
//             else {
//                 cur_total = temp_total;
//             }
//         }
//         std::cout << "cur_total = " << cur_total << std::endl;
//     }

//     COST reuse_cost = ReuseCost(decision, _bbl_data_reuse.getRoot());
//     COST switch_cost = SwitchCost(decision, _bbl_switch_count);
//     auto elapsed_time = ElapsedTime(decision);
//     COST total_time = reuse_cost + switch_cost + elapsed_time.first + elapsed_time.second;
//     assert(total_time == Cost(decision, _bbl_data_reuse.getRoot(), _bbl_switch_count));

//     ofs << "Reuse offloading time (ns): " << total_time << " = CPU " << elapsed_time.first << " + PIM " << elapsed_time.second << " + REUSE " << reuse_cost << " + SWITCH " << switch_cost << std::endl;

//     return decision;
// }

void CostSolver::PrintDisjointSets(std::ostream &ofs)
{
    DisjointSet ds;
    _bbl_data_reuse.SortLeaves();

    COST elapsed_time_min = (ElapsedTime(CPU) < ElapsedTime(PIM) ? ElapsedTime(CPU) : ElapsedTime(PIM));
    COST reuse_max = SingleSegMaxReuseCost();

    for (auto i : _bbl_data_reuse.getLeaves()) {
        BBLIDDataReuseSegment seg;
        _bbl_data_reuse.ExportSegment(&seg, i);
        BBLID first = *seg.begin();
        for (auto elem : seg) {
            ds.Union(first, elem);
        }
        if (seg.getCount() * reuse_max < _batch_threshold * elapsed_time_min) break;
    }

    for (auto it : ds.parent) {
        ofs << it.first << " " << it.second << std::endl;
    }
    
    for (auto it : ds.parent) {
        if (ds.Find(it.first) == it.first) {
            BBLID head = it.first;
            int count = 0;
            for (auto it2 : ds.parent) {
                if (ds.Find(it2.first) == head) {
                    ofs << it2.first << " ";
                     count++;
                }
            }
            ofs << " | Count = " << count << std::endl;
        }
    }
}

DECISION CostSolver::Debug_StartFromUnimportantSegment(std::ostream &ofs)
{
    _bbl_data_reuse.SortLeaves();
    // std::ofstream oo("sortedsegments.out", std::ofstream::out);
    // _bbl_data_reuse.PrintAllSegments(oo, CostSolver::_get_id);
    // oo << std::endl;
    // _bbl_data_reuse.PrintBBLOccurrence(oo, CostSolver::_get_id);


    COST elapsed_time_min = (ElapsedTime(CPU) < ElapsedTime(PIM) ? ElapsedTime(CPU) : ElapsedTime(PIM));
    COST reuse_max = SingleSegMaxReuseCost();

    //initialize all decision to INVALID
    DECISION decision;
    decision.resize(_bbl_hash2stats[CPU].size(), INVALID);
    COST cur_total = FLT_MAX;

    BBLIDTrieNode *partial_root = new BBLIDTrieNode();
    BBLIDDataReuseSegment allidset;
    int cur_node = 0;
    int leaves_size = _bbl_data_reuse.getLeaves().size();

    // find out the node with smallest importance but exceeds the threshold, skip the rest
    while (cur_node < leaves_size) {
        BBLIDDataReuseSegment seg;
        _bbl_data_reuse.ExportSegment(&seg, _bbl_data_reuse.getLeaves()[cur_node]);
        if (seg.getCount() * reuse_max < _batch_threshold * elapsed_time_min) break;
        cur_node++;
    }

    for (; cur_node >= 0; --cur_node) {
        BBLIDDataReuseSegment seg;
        _bbl_data_reuse.ExportSegment(&seg, _bbl_data_reuse.getLeaves()[cur_node]);
        _bbl_data_reuse.UpdateTrie(partial_root, &seg);
        std::vector<BBLID> cur_batch(seg.begin(), seg.end());
        std::cout << "cur_node = " << cur_node << ", size = " << seg.size() << std::endl;

        // ignore too long segments
        if ((int)seg.size() >= _batch_size) continue;

        cur_total = PermuteDecision(decision, cur_batch, partial_root);
        
        for (auto elem : cur_batch) {
            std::cout << elem << getCostSiteString(decision[elem]) << " ";
        }

        std::cout << "seg_count = " << seg.getCount() << ", reuse_max = " << reuse_max << ", cur_total = " << cur_total << std::endl;
        std::cout << std::endl;
    }

    _bbl_data_reuse.DeleteTrie(partial_root);

    const std::vector<ThreadRunStats *> *sorted = getBBLSortedStats();

    // assign decision for BBLs that did not occur in the reuse chains
    for (BBLID i = 0; i < (BBLID)sorted[CPU].size(); ++i) {
        auto *cpustats = sorted[CPU][i];
        auto *pimstats = sorted[PIM][i];

        if (decision[i] == INVALID) {
            if (cpustats->MaxElapsedTime() <= pimstats->MaxElapsedTime()) {
                decision[i] = CPU;
            }
            else {
                decision[i] = PIM;
            }
        }
    }

    cur_total = Cost(decision, _bbl_data_reuse.getRoot(), _bbl_switch_count);
    std::cout << "cur_total = " << cur_total << std::endl;
    // iterate over the remaining BBs until convergence
    for (int j = 0; j < 2; j++) {
        for (BBLID id = 0; id < (BBLID)sorted[CPU].size(); id++) {
            // swap decision[id] and check if it reduces overhead
            decision[id] = (decision[id] == CPU ? PIM : CPU);
            COST temp_total = Cost(decision, _bbl_data_reuse.getRoot(), _bbl_switch_count);
            if (temp_total > cur_total) {
                decision[id] = (decision[id] == CPU ? PIM : CPU);
            }
            else {
                cur_total = temp_total;
            }
        }
        std::cout << "cur_total = " << cur_total << std::endl;
    }

    COST reuse_cost = ReuseCost(decision, _bbl_data_reuse.getRoot());
    COST switch_cost = SwitchCost(decision, _bbl_switch_count);
    auto elapsed_time = ElapsedTime(decision);
    COST total_time = reuse_cost + switch_cost + elapsed_time.first + elapsed_time.second;
    assert(total_time == Cost(decision, _bbl_data_reuse.getRoot(), _bbl_switch_count));

    ofs << "Reuse offloading time (ns): " << total_time << " = CPU " << elapsed_time.first << " + PIM " << elapsed_time.second << " + REUSE " << reuse_cost << " + SWITCH " << switch_cost << std::endl;

    return decision;
}

DECISION CostSolver::PrintReuseStats(std::ostream &ofs)
{
    _bbl_data_reuse.SortLeaves();
    // std::ofstream oo("sortedsegments.out", std::ofstream::out);
    // _bbl_data_reuse.PrintAllSegments(oo, CostSolver::_get_id);
    // oo << std::endl;
    // _bbl_data_reuse.PrintBBLOccurrence(oo, CostSolver::_get_id);


    COST elapsed_time_min = (ElapsedTime(CPU) < ElapsedTime(PIM) ? ElapsedTime(CPU) : ElapsedTime(PIM));
    COST reuse_max = SingleSegMaxReuseCost();

    COST min_total = FLT_MAX;
    DECISION min_decision, decision;
    std::vector<CostSite> init_decisions = {CPU, PIM, INVALID};
    for (auto init_decision : init_decisions) {
        decision.clear();
        decision.resize(_bbl_hash2stats[CPU].size(), init_decision);
        COST cur_total = FLT_MAX;

        BBLIDTrieNode *partial_root = new BBLIDTrieNode();
        BBLIDDataReuseSegment allidset;
        int cur_node = 0;
        int leaves_size = _bbl_data_reuse.getLeaves().size();

        // find out the node with smallest importance but exceeds the threshold, skip the rest
        while (cur_node < leaves_size) {
            BBLIDDataReuseSegment seg;
            _bbl_data_reuse.ExportSegment(&seg, _bbl_data_reuse.getLeaves()[cur_node]);
            if (seg.getCount() * reuse_max < _batch_threshold * elapsed_time_min) break;
            cur_node++;
        }

        for (; cur_node >= 0; --cur_node) {
            BBLIDDataReuseSegment seg;
            _bbl_data_reuse.ExportSegment(&seg, _bbl_data_reuse.getLeaves()[cur_node]);
            _bbl_data_reuse.UpdateTrie(partial_root, &seg);

            // ignore too long segments
            if ((int)seg.size() >= _batch_size) continue;

            // find BBLs with most occurence in all switching points related to BBLs in current segment
            std::unordered_map<BBLID, uint64_t> total_switch_cnt_map;
            for (auto fromidx : seg) {
                SwitchCountList::SwitchCountRow &row = _bbl_switch_count.getRow(fromidx);
                for (auto elem : row) {
                    BBLID toidx = elem.first;
                    uint64_t count = elem.second;
                    auto it = total_switch_cnt_map.find(toidx);
                    if (it != total_switch_cnt_map.end()) {
                        it->second += count;
                    }
                    else {
                        total_switch_cnt_map[toidx] = count;
                    }
                }
            }
            std::vector<std::pair<BBLID, uint64_t>> total_switch_cnt_vec(total_switch_cnt_map.begin(), total_switch_cnt_map.end());
            std::sort(total_switch_cnt_vec.begin(), total_switch_cnt_vec.end(),
                [](auto l, auto r){ return l.second > r.second; });

            for (auto elem : total_switch_cnt_vec) {
                if ((int)seg.size() >= _batch_size) break;
                seg.insert(elem.first);
            }

            std::vector<BBLID> cur_batch(seg.begin(), seg.end());
            std::cout << "cur_node = " << cur_node << ", size = " << seg.size() << std::endl;

            cur_total = PermuteDecision(decision, cur_batch, partial_root);
            
            for (auto elem : cur_batch) {
                std::cout << elem << getCostSiteString(decision[elem]) << " ";
            }
     

            std::cout << "seg_count = " << seg.getCount() << ", reuse_max = " << reuse_max << ", cur_total = " << cur_total << std::endl;
            std::cout << std::endl;
        }

        _bbl_data_reuse.DeleteTrie(partial_root);

        const std::vector<ThreadRunStats *> *sorted = getBBLSortedStats();

        // assign decision for BBLs that did not occur in the reuse chains
        for (BBLID i = 0; i < (BBLID)sorted[CPU].size(); ++i) {
            auto *cpustats = sorted[CPU][i];
            auto *pimstats = sorted[PIM][i];

            if (decision[i] == INVALID) {
                if (cpustats->MaxElapsedTime() <= pimstats->MaxElapsedTime()) {
                    decision[i] = CPU;
                }
                else {
                    decision[i] = PIM;
                }
            }
        }

        cur_total = Cost(decision, _bbl_data_reuse.getRoot(), _bbl_switch_count);
        std::cout << "cur_total = " << cur_total << std::endl;
        // iterate over the remaining BBs until convergence
        for (int j = 0; j < 2; j++) {
            for (BBLID id = 0; id < (BBLID)sorted[CPU].size(); id++) {
                // swap decision[id] and check if it reduces overhead
                decision[id] = (decision[id] == CPU ? PIM : CPU);
                COST temp_total = Cost(decision, _bbl_data_reuse.getRoot(), _bbl_switch_count);
                if (temp_total > cur_total) {
                    decision[id] = (decision[id] == CPU ? PIM : CPU);
                }
                else {
                    cur_total = temp_total;
                }
            }
            std::cout << "cur_total = " << cur_total << std::endl;
        }
        if (min_total > cur_total) {
            min_decision = decision;
            min_total = cur_total;
            std::cout << min_total << std::endl;
        }
    }

    COST reuse_cost = ReuseCost(min_decision, _bbl_data_reuse.getRoot());
    COST switch_cost = SwitchCost(min_decision, _bbl_switch_count);
    auto elapsed_time = ElapsedTime(min_decision);
    COST total_time = reuse_cost + switch_cost + elapsed_time.first + elapsed_time.second;

    ofs << "Reuse offloading time (ns): " << total_time << " = CPU " << elapsed_time.first << " + PIM " << elapsed_time.second << " + REUSE " << reuse_cost << " + SWITCH " << switch_cost << std::endl;


    // std::ofstream oo(
    //     (_command_line_parser->outputfile() + ".debug").c_str(),
    //     std::ofstream::out);
    // _bbl_switch_count.printSwitch(oo, decision, _switch_cost);

    return min_decision;
}

DECISION CostSolver::Debug_HierarchicalDecision(std::ostream &ofs)
{
    _bbl_data_reuse.SortLeaves();
    // std::ofstream oo("sortedsegments.out", std::ofstream::out);
    // _bbl_data_reuse.PrintAllSegments(oo, CostSolver::_get_id);
    // oo << std::endl;
    // _bbl_data_reuse.PrintBBLOccurrence(oo, CostSolver::_get_id);


    COST elapsed_time_min = (ElapsedTime(CPU) < ElapsedTime(PIM) ? ElapsedTime(CPU) : ElapsedTime(PIM));
    COST reuse_max = SingleSegMaxReuseCost();

    //initialize all decision to INVALID
    DECISION decision;
    decision.resize(_bbl_hash2stats[CPU].size(), INVALID);
    COST cur_total = FLT_MAX;

    BBLIDTrieNode *partial_root = new BBLIDTrieNode();
    BBLIDDataReuseSegment allidset;
    int cur_node = 0;
    int leaves_size = _bbl_data_reuse.getLeaves().size();

    // find out the node with smallest importance but exceeds the threshold, skip the rest
    while (cur_node < leaves_size) {
        BBLIDDataReuseSegment seg;
        _bbl_data_reuse.ExportSegment(&seg, _bbl_data_reuse.getLeaves()[cur_node]);
        if (seg.getCount() * reuse_max < _batch_threshold * elapsed_time_min) break;
        cur_node++;
    }

    for (; cur_node >= 0; --cur_node) {
        BBLIDDataReuseSegment seg;
        _bbl_data_reuse.ExportSegment(&seg, _bbl_data_reuse.getLeaves()[cur_node]);
        _bbl_data_reuse.UpdateTrie(partial_root, &seg);

        // ignore too long segments
        if ((int)seg.size() >= _batch_size) continue;

        // find BBLs with most occurence in all switching points related to BBLs in current segment
        std::unordered_map<BBLID, uint64_t> total_switch_cnt_map;
        for (auto fromidx : seg) {
            SwitchCountList::SwitchCountRow &row = _bbl_switch_count.getRow(fromidx);
            for (auto elem : row) {
                BBLID toidx = elem.first;
                uint64_t count = elem.second;
                auto it = total_switch_cnt_map.find(toidx);
                if (it != total_switch_cnt_map.end()) {
                    it->second += count;
                }
                else {
                    total_switch_cnt_map[toidx] = count;
                }
            }
        }
        std::vector<std::pair<BBLID, uint64_t>> total_switch_cnt_vec(total_switch_cnt_map.begin(), total_switch_cnt_map.end());
        std::sort(total_switch_cnt_vec.begin(), total_switch_cnt_vec.end(),
            [](auto l, auto r){ return l.second > r.second; });

        for (auto elem : total_switch_cnt_vec) {
            if ((int)seg.size() >= _batch_size) break;
            seg.insert(elem.first);
        }

        std::vector<BBLID> cur_batch(seg.begin(), seg.end());
        std::cout << "cur_node = " << cur_node << ", size = " << seg.size() << std::endl;

        cur_total = PermuteDecision(decision, cur_batch, partial_root);
        
        for (auto elem : cur_batch) {
            std::cout << elem << getCostSiteString(decision[elem]) << " ";
        }
 

        std::cout << "seg_count = " << seg.getCount() << ", reuse_max = " << reuse_max << ", cur_total = " << cur_total << std::endl;
        std::cout << std::endl;
    }

    _bbl_data_reuse.DeleteTrie(partial_root);

    const std::vector<ThreadRunStats *> *sorted = getBBLSortedStats();

    // assign decision for BBLs that did not occur in the reuse chains
    for (BBLID i = 0; i < (BBLID)sorted[CPU].size(); ++i) {
        auto *cpustats = sorted[CPU][i];
        auto *pimstats = sorted[PIM][i];

        if (decision[i] == INVALID) {
            if (cpustats->MaxElapsedTime() <= pimstats->MaxElapsedTime()) {
                decision[i] = CPU;
            }
            else {
                decision[i] = PIM;
            }
        }
    }

    cur_total = Cost(decision, _bbl_data_reuse.getRoot(), _bbl_switch_count);
    std::cout << "cur_total = " << cur_total << std::endl;
    // iterate over the remaining BBs until convergence
    for (int j = 0; j < 2; j++) {
        for (BBLID id = 0; id < (BBLID)sorted[CPU].size(); id++) {
            // swap decision[id] and check if it reduces overhead
            decision[id] = (decision[id] == CPU ? PIM : CPU);
            COST temp_total = Cost(decision, _bbl_data_reuse.getRoot(), _bbl_switch_count);
            if (temp_total > cur_total) {
                decision[id] = (decision[id] == CPU ? PIM : CPU);
            }
            else {
                cur_total = temp_total;
            }
        }
        std::cout << "cur_total = " << cur_total << std::endl;
    }

    COST reuse_cost = ReuseCost(decision, _bbl_data_reuse.getRoot());
    COST switch_cost = SwitchCost(decision, _bbl_switch_count);
    auto elapsed_time = ElapsedTime(decision);
    COST total_time = reuse_cost + switch_cost + elapsed_time.first + elapsed_time.second;

    ofs << "Reuse offloading time (ns): " << total_time << " = CPU " << elapsed_time.first << " + PIM " << elapsed_time.second << " + REUSE " << reuse_cost << " + SWITCH " << switch_cost << std::endl;


    std::ofstream oo(
        (_command_line_parser->outputfile() + ".debug").c_str(),
        std::ofstream::out);
    _bbl_switch_count.printSwitch(oo, decision, _switch_cost);

    return decision;
}

COST CostSolver::Cost(const DECISION &decision, const BBLIDTrieNode *reusetree, const SwitchCountList &switchcnt)
{
    auto pair = ElapsedTime(decision);
    return (ReuseCost(decision, reusetree) + SwitchCost(decision, switchcnt) + pair.first + pair.second);
}

COST CostSolver::ElapsedTime(CostSite site)
{
    const std::vector<ThreadRunStats *> *sorted = getBBLSortedStats();
    COST elapsed_time = 0;
    for (BBLID i = 0; i < (BBLID)sorted[site].size(); ++i) {
        auto *stats = sorted[site][i];
        elapsed_time += stats->MaxElapsedTime();
    }
    return elapsed_time;
}

std::pair<COST, COST> CostSolver::ElapsedTime(const DECISION &decision)
{
    COST cpu_elapsed_time = 0, pim_elapsed_time = 0;
    const std::vector<ThreadRunStats *> *sorted = getBBLSortedStats();
    for (uint32_t i = 0; i < sorted[CPU].size(); i++) {
        auto *cpustats = sorted[CPU][i];
        auto *pimstats = sorted[PIM][i];
        if (decision[i] == CPU) {
            cpu_elapsed_time += cpustats->MaxElapsedTime();
        }
        else if (decision[i] == PIM) {
            pim_elapsed_time += pimstats->MaxElapsedTime();
        }
        else {
            // do nothing, since decision[i] == INVALID means that node i has not
            // been added to the tree
        }
    }
    return std::make_pair(cpu_elapsed_time, pim_elapsed_time);
}

// decision here can be INVALID
COST CostSolver::SwitchCost(const DECISION &decision, const SwitchCountList &switchcnt)
{
    COST cur_switch_cost = 0;
    for (auto row : switchcnt) {
        cur_switch_cost += row.Cost(decision, _switch_cost);
    }
    
    return cur_switch_cost;
}

std::vector<BBCOUNT> CostSolver::bbTimesFromSwitchInfo(const DECISION &decision, const SwitchCountList &switchcnt)
{
    std::vector<BBCOUNT> bbCount(decision.size(), 0);
    for (auto row : switchcnt) {
        row.bbCountFunc(bbCount);
    } 
    return bbCount;
}

// decision here should not be INVALID
COST CostSolver::ReuseCost(const DECISION &decision, const BBLIDTrieNode *reusetree)
{
    COST cur_reuse_cost = 0;
    for (auto elem : reusetree->_children) {
        TrieBFS(cur_reuse_cost, decision, elem.first, elem.second, false);
    }
    return cur_reuse_cost;
}

struct Compare {
    template<typename T1, typename T2>
    bool operator()(const std::pair<T1, T2>& p1, const std::pair<T1, T2>& p2) const {
        return p1.second > p2.second; // 按照 p1 的值大于 p2 的值进行排序
    }
};

void CostSolver::TopReuseBBPairs(DECISION &decision)
{
    auto &ofs = delayCout;
    //calculate total COST based on interBB_CL_DM and interBB_REG_DM
    std::map<std::pair<BBLID,BBLID>, COST> interBBTotalCost;
    for(auto &datamove: interBB_CL_DM){
        interBBTotalCost[datamove.first] += datamove.second * (_flush_cost[CPU] + _fetch_cost[PIM]);
    }
    for(auto &datamove: interBB_REG_DM){
        interBBTotalCost[datamove.first] += datamove.second * (_switch_cost[CPU]);
    }
    std::vector<std::pair<std::pair<BBLID,BBLID>,COST>> vec(interBBTotalCost.begin(), interBBTotalCost.end());
    sort(vec.begin(),vec.end(),Compare());
    const std::vector<ThreadRunStats *> *sorted = getBBLSortedStats();

    // get the union set
    DisjointSet ds;
    std::set<BBLID> toRedecideBBL;
    COST top1DataCost = vec[0].second;
    for (int i = vec.size()-1; i>=0; i--) {
        auto &pair = vec[i];
        auto &bblIndex1 = pair.first.first;
        auto &bblIndex2 = pair.first.second;
        COST totalCost = pair.second;
        // erase most data cost 
        if(totalCost < _dataMoveThreshold * top1DataCost) continue;
        toRedecideBBL.insert(bblIndex1);
        toRedecideBBL.insert(bblIndex2);
        ds.Union(bblIndex1, bblIndex2);
    }

    // Union CPU & PIM decision count
    std::map<BBLID, PairInt> dsCount;
    // for print different Union
    std::map<BBLID,std::vector<BBLID>> BBLUnion;
    for(auto &bbl : toRedecideBBL){
        dsCount[ds.Find(bbl)] += (scaDecision[sorted[CPU][bbl]->bblhash] == CostSite::PIM)?
                        PIMProf::PairInt::make_pair(1,0):PIMProf::PairInt::make_pair(0,1);
        if(BBLUnion.count(ds.Find(bbl))){
            BBLUnion[ds.Find(bbl)].push_back(bbl);
        }else{
            BBLUnion[ds.Find(bbl)]=std::vector<BBLID> {bbl};
        }
    }

    // print different Union & its sca manual decision
    ofs << "DisjointSet: " << std::endl;
    for(auto &bblSet:BBLUnion ){
        for(auto &bblId: bblSet.second){
            ofs << std::setw(3) << bblId << " ";
        }
        ofs << std::endl;
        for(auto &bblId: bblSet.second){
            ofs << std::setw(3) << getCostSiteString(scaDecision[sorted[CPU][bblId]->bblhash]) << " ";
        }
        ofs << std::endl;   
        ofs << "PIM count in one set: " << dsCount[bblSet.first].first << " " << dsCount[bblSet.first].second << std::endl;  
    }

    //cluster 2 PIM
    for(auto &bbl : toRedecideBBL){
        // divide 2 because consider the second value including half CostSite::Follower and CostSite::CPU
        if(dsCount[ds.Find(bbl)].first >= dsCount[ds.Find(bbl)].second/2){
            decision[bbl] = CostSite::PIM;
        }else{
            // deal with scaDecision == CostSite::Follower
            decision[bbl] = CostSite::CPU;
        }
    }

    for (int i = vec.size()-1; i>=0; i--) {
        auto &pair = vec[i];
        auto &bblIndex1 = pair.first.first;
        auto &bblIndex2 = pair.first.second;
        COST totalCost = pair.second;    
        if(totalCost < _dataMoveThreshold * top1DataCost) continue;
        // if(i<10)
            ofs << "Data move Cost: " << std::setw(7) << pair.second << 
                " id pair : " << bblIndex1 << " <-> "
                << bblIndex2 << 
                " sca manual decison: " 
                << getCostSiteString(scaDecision[sorted[CPU][bblIndex1]->bblhash]) << " <-> "
                << getCostSiteString(scaDecision[sorted[CPU][bblIndex2]->bblhash])
                << std::endl;
            ofs << " cts decision "
                << getCostSiteString(ctsDecision[sorted[CPU][bblIndex1]->bblhash]) << " <-> "
                << getCostSiteString(ctsDecision[sorted[CPU][bblIndex2]->bblhash]);
        if(ctsDecision[sorted[CPU][bblIndex1]->bblhash] != ctsDecision[sorted[CPU][bblIndex2]->bblhash]){
            ofs << " potential";
        }
            ofs << std::endl;
        auto *cpustats1 = sorted[CPU][bblIndex1];
        auto *pimstats1 = sorted[PIM][bblIndex1];
        auto *cpustats2 = sorted[CPU][bblIndex2];
        auto *pimstats2 = sorted[PIM][bblIndex2];
        COST diff1 = cpustats1->MaxElapsedTime() - pimstats1->MaxElapsedTime();
        COST diff2 = cpustats2->MaxElapsedTime() - pimstats2->MaxElapsedTime();
        // COST CL_DM = pair.second * (_flush_cost[CPU] + _fetch_cost[PIM]);
        
        // Only for Union size less than 2:
        // Utilize cost-benefit analysis to replace combinatorial iteration for finding the minimum scenario.
        // if(diff1 >= 0 && diff2 >= 0){
        //     decision[bblIndex1] = CostSite::PIM;
        //     decision[bblIndex2] = CostSite::PIM;
        // }else if(diff1 <= 0 && diff2 <= 0){
        //     decision[bblIndex1] = CostSite::CPU;
        //     decision[bblIndex2] = CostSite::CPU;
        // }else if(std::abs(diff1) > totalCost && std::abs(diff2) > totalCost){
        //     decision[bblIndex1] = (diff1 > 0)?CostSite::PIM:CostSite::CPU;
        //     decision[bblIndex2] = (diff2 > 0)?CostSite::PIM:CostSite::CPU;
        // }else{
        //     bool tmp = (std::abs(diff1)>std::abs(diff2))?(diff1 > 0):(diff2 > 0);
        //     decision[bblIndex1] = (tmp)?CostSite::PIM:CostSite::CPU;
        //     decision[bblIndex2] = (tmp)?CostSite::PIM:CostSite::CPU;
        // }

        // Each pair judge if Cluster to PIM
        // CostSite &scaPreliminary1 = scaDecision[cpustats1->bblhash];
        // CostSite &scaPreliminary2 = scaDecision[cpustats2->bblhash];
        // if(scaPreliminary1 == CostSite::PIM || scaPreliminary2 == CostSite::PIM ){
        //     decision[bblIndex1] = CostSite::PIM;
        //     decision[bblIndex2] = CostSite::PIM;
        // }else{
        //     decision[bblIndex1] = CostSite::CPU;
        //     decision[bblIndex2] = CostSite::CPU;
        // }
        // if(i<10){
            ofs << " re-sca: " << 
                getCostSiteString(decision[bblIndex1]) << " <-> "
                << getCostSiteString(decision[bblIndex2])
                << std::endl;  
            ofs << " Diff(CPU Time - PIM Time) of bbl1: " << std::setw(15) << diff1 << 
                "\n Diff(CPU Time - PIM Time) of bbl2: " << std::setw(15) << diff2 << 
                // "\n abs(df1)+abs(df2): " << std::setw(15) << std::abs(diff1)+std::abs(diff2) << 
                "\n totalCost: " << totalCost << std::endl;
        // }
    }

}

void CostSolver::TrieBFS(COST &cost, const DECISION &decision, BBLID bblid, const BBLIDTrieNode *root, bool isDifferent)
{
    if (root->_isLeaf) {
        // The cost of a segment is zero if and only if the entire segment is in the same place. In other words, if isDifferent, then the cost is non-zero.
        if (isDifferent) {
            // If the initial W is on CPU and there are subsequent R/W on PIM,
            // then this segment contributes to a flush of CPU and data fetch from PIM.
            // We conservatively assume that the fetch will promote data to L1
            assert(bblid == root->_cur);
            if (decision[root->_cur] == CPU) {
                cost += root->_count * (_flush_cost[CPU] + _fetch_cost[PIM]);
            }
            // If the initial W is on PIM and there are subsequent R/W on CPU,
            // then this segment contributes to a flush of PIM and data fetch from CPU
            else {
                cost += root->_count * (_flush_cost[PIM] + _fetch_cost[CPU]);
            }
        }
    }
    else {
        for (auto elem : root->_children) {
            if (isDifferent) {
                TrieBFS(cost, decision, elem.first, elem.second, true);
            }
            else if (decision[bblid] != decision[elem.first]) {
                TrieBFS(cost, decision, elem.first, elem.second, true);
            }
            else {
                TrieBFS(cost, decision, elem.first, elem.second, false);
            }
        }
    }
}

// decision here should not be INVALID
COST CostSolver::ReuseCostPrint(const DECISION &decision, const BBLIDTrieNode *reusetree, std::ostream &ofs)
{
    COST cur_reuse_cost = 0;
    for (auto elem : reusetree->_children) {
        TrieBFS(cur_reuse_cost, decision, elem.first, elem.second, false, {0,0} ,ofs);
    }
    return cur_reuse_cost;
}

void CostSolver::TrieBFS(COST &cost, const DECISION &decision, BBLID bblid, const BBLIDTrieNode *root, bool isDifferent, std::pair<BBLID,BBLID> diffBBLIDs ,std::ostream &ofs)
{
    if (root->_isLeaf) {
        // The cost of a segment is zero if and only if the entire segment is in the same place. In other words, if isDifferent, then the cost is non-zero.
        if (isDifferent) {
            // If the initial W is on CPU and there are subsequent R/W on PIM,
            // then this segment contributes to a flush of CPU and data fetch from PIM.
            // We conservatively assume that the fetch will promote data to L1
            assert(bblid == root->_cur);
            COST delta = 0;
            if (decision[root->_cur] == CPU) {
                delta += root->_count * (_flush_cost[CPU] + _fetch_cost[PIM]);
            }
            // If the initial W is on PIM and there are subsequent R/W on CPU,
            // then this segment contributes to a flush of PIM and data fetch from CPU
            else {
                delta += root->_count * (_flush_cost[PIM] + _fetch_cost[CPU]);
            }
            cost += delta;
            if(delta > 1e+6)
                ofs << "cost delta: " << delta 
                << " diffBBLIDs: " 
                << std::dec << diffBBLIDs.first 
                << " to " << diffBBLIDs.second << std::endl;
        }
    }
    else {
        for (auto elem : root->_children) {
            if (isDifferent) {
                TrieBFS(cost, decision, elem.first, elem.second, true, diffBBLIDs, ofs);
            }
            else if (decision[bblid] != decision[elem.first]) {
                TrieBFS(cost, decision, elem.first, elem.second, true, {bblid, elem.first}, ofs);
            }
            else {
                TrieBFS(cost, decision, elem.first, elem.second, false, diffBBLIDs, ofs);
            }
        }
    }
}
