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
void CostSolver::initialize(CommandLineParser *parser)
{
    _command_line_parser = parser;
    _batchthreshold = 0;
    _batchsize = 0;

    std::ifstream cpustats(_command_line_parser->cpustatsfile());
    std::ifstream pimstats(_command_line_parser->pimstatsfile());
    ParseStats(cpustats, _bblhash2stats[CPU]);
    ParseStats(pimstats, _bblhash2stats[PIM]);

    if (_command_line_parser->mode() == CommandLineParser::REUSE) {
        std::ifstream reuse(_command_line_parser->reusefile());
        ParseReuse(reuse, _data_reuse);
    }
    

    // temporarily define flush and fetch cost here
    _flush_cost[CostSite::CPU] = 60;
    _flush_cost[CostSite::PIM] = 30;
    _fetch_cost[CostSite::CPU] = 60;
    _fetch_cost[CostSite::PIM] = 30;
    _batchthreshold = 0.001;
    _batchsize = 10;
}

CostSolver::~CostSolver()
{
    for (int i = 0; i < MAX_COST_SITE; i++) {
        for (auto it = _bblhash2stats[i].begin(); it != _bblhash2stats[i].end(); ++it) {
            delete it->second;
        }
    }
}

void CostSolver::ParseStats(std::istream &ifs, UUIDHashMap<ThreadBBLStats *> &statsmap)
{
    std::string line, token;
    
    while(std::getline(ifs, line)) {
        if (line.find(HORIZONTAL_LINE) != std::string::npos) {
            std::getline(ifs, line);
            std::getline(ifs, line); // skip next 2 lines
            continue;
        }
        std::stringstream ss(line);

        BBLStats bblstats;
        ss >> bblstats.bblid
           >> bblstats.elapsed_time
           >> bblstats.instruction_count
           >> bblstats.memory_access
           >> std::hex >> bblstats.bblhash.first >> bblstats.bblhash.second;
        auto it = statsmap.find(bblstats.bblhash);
        if (statsmap.find(bblstats.bblhash) == statsmap.end()) {
            ThreadBBLStats *p = new ThreadBBLStats(bblstats);
            statsmap.insert(std::make_pair(bblstats.bblhash, p));
        }
        else {
            it->second->MergeStats(bblstats);
        }
    }
}

void CostSolver::ParseReuse(std::istream &ifs, DataReuse<BBLID> &reuse)
{
    std::string line, token;
    
    while(std::getline(ifs, line)) {
        if (line.find(HORIZONTAL_LINE) != std::string::npos) {
            std::getline(ifs, line); // skip next line
            continue;
        }
        std::stringstream ss(line);
        BBLIDDataReuseSegment seg;
        ss >> token >> token >> token;
        BBLID head = std::stoi(token.substr(0, token.size() - 1));
        int count;
        ss >> token >> token >> count;
        ss >> token;
        BBLID bblid;
        while (ss >> bblid) {
            seg.insert(bblid);
        }
        seg.setHead(head);
        seg.setCount(count);
        reuse.UpdateTrie(reuse.getRoot(), &seg);
    }

    reuse.PrintAllSegments(std::cout, [](BBLID bblid){ return bblid; });
}

CostSolver::DECISION CostSolver::PrintSolution(std::ostream &ofs)
{
    DECISION decision;
    
    if (_command_line_parser->mode() == CommandLineParser::Mode::MPKI) {
        decision = PrintMPKISolution(ofs);
    }
    if (_command_line_parser->mode() == CommandLineParser::Mode::REUSE) {
        decision = PrintReuseSolution(ofs);
    }

    return decision;
}

std::ostream & CostSolver::PrintDecision(std::ostream &ofs, const DECISION &decision, bool toscreen)
{
    const std::vector<ThreadBBLStats *> *sorted = getSorted();
    ofs << HORIZONTAL_LINE << std::endl;
    if (toscreen == true) {
        for (uint32_t i = 0; i < decision.size(); i++) {
            ofs << i << ":"
                << (decision[i] == CPU ? "C" : "")
                << (decision[i] == PIM ? "P" : "")
                << (decision[i] == INVALID ? "I" : "")
                << " ";
        }
        ofs << std::endl;
    }
    else {
        ofs << std::setw(7) << "BBLID"
            << std::setw(10) << "Decision"
            << std::setw(15) << "CPU"
            << std::setw(15) << "PIM"
            << std::setw(15) << "difference"
            << std::setw(18) << "Hash(hi)"
            << std::setw(18) << "Hash(lo)"
            << std::endl;
        for (uint32_t i = 0; i < sorted[CPU].size(); i++) {
            auto *cpustats = sorted[CPU][i];
            auto *pimstats = sorted[PIM][i];
            COST diff = cpustats->MaxElapsedTime() - pimstats->MaxElapsedTime();
            ofs << std::setw(7) << i
                << std::setw(10) << (decision[i] == PIM ? "P" : "C")
                << std::setw(15) << cpustats->MaxElapsedTime()
                << std::setw(15) << pimstats->MaxElapsedTime()
                << std::setw(15) << diff
                << "  " << std::hex
                << std::setfill('0') << std::setw(16) << cpustats->bblhash.first
                << "  "
                << std::setfill('0') << std::setw(16) << cpustats->bblhash.second
                << std::setfill(' ') << std::dec << std::endl;
        }
    }
    return ofs;
}

CostSolver::DECISION CostSolver::PrintMPKISolution(std::ostream &ofs)
{
    const std::vector<ThreadBBLStats *> *sorted = getSorted();

    DECISION decision;

    uint64_t pim_total_instr = 0;
    for (auto it = sorted[PIM].begin(); it != sorted[PIM].end(); ++it) {
        pim_total_instr += (*it)->instruction_count;
    }

    uint64_t instr_threshold = pim_total_instr * 0.01;
    uint64_t mpki_threshold = 10;
    COST mpki_time = 0, mpki_cpu_time = 0, mpki_pim_time = 0;
    
    for (BBLID i = 0; i < sorted[CPU].size(); ++i) {
        auto *cpustats = sorted[CPU][i];
        auto *pimstats = sorted[PIM][i];

        double instr = pimstats->instruction_count;
        double mem = pimstats->memory_access;
        double mpki = mem / instr * 1000.0;

        // deal with the part that is not inside any BBL
        if (cpustats->bblhash == GLOBAL_BBLHASH) {
            mpki_time += cpustats->MaxElapsedTime();
            mpki_cpu_time += cpustats->MaxElapsedTime();
            decision.push_back(CostSite::CPU);
            continue;
        }

        if (mpki > mpki_threshold && instr > instr_threshold) {
            mpki_time += pimstats->MaxElapsedTime();
            mpki_pim_time += pimstats->MaxElapsedTime();
            decision.push_back(CostSite::PIM);
        }
        else {
            mpki_time += cpustats->MaxElapsedTime();
            mpki_cpu_time += cpustats->MaxElapsedTime();
            decision.push_back(CostSite::CPU);
        }
    }
    ofs << "CPU only time (ns): " << ElapsedTime(CPU) << std::endl
    << "PIM only time (ns): " << ElapsedTime(PIM) << std::endl
    << "MPKI offloading time (ns): " << mpki_time << " = CPU " << mpki_cpu_time << " + PIM " << mpki_pim_time << std::endl;

    PrintDecision(ofs, decision, false);

    return decision;
}

CostSolver::DECISION CostSolver::PrintReuseSolution(std::ostream &ofs)
{
    _data_reuse.SortLeaves();

    COST reuse_max = 0;
    for (uint32_t i = 0; i < MAX_COST_SITE; i++) {
        reuse_max = std::max(reuse_max, _flush_cost[i]);
        reuse_max = std::max(reuse_max, _fetch_cost[i]);
    }

    COST cur_total = FLT_MAX;
    int seg_count = INT_MAX;
    DECISION decision;

    //initialize all decision to INVALID
    decision.resize(_bblhash2stats[CPU].size(), INVALID);

    BBLIDTrieNode *partial_root = new BBLIDTrieNode();
    BBLIDDataReuseSegment allidset;
    int currentnode = 0;
    int leavessize = _data_reuse.getLeaves().size();

    for (int i = 0; currentnode < leavessize; i++) {
        std::vector<BBLID> idvec;
        // insert segments until the number of different BBLs hit _batchsize
        while (currentnode < leavessize) {
            BBLIDDataReuseSegment seg;
            _data_reuse.ExportSegment(&seg, _data_reuse.getLeaves()[currentnode]);
            std::vector<BBLID> diff = seg.diff(allidset);
            // std::cout << idvec.size() << " " << diff.size() << std::endl;
            if (idvec.size() + diff.size() > (unsigned)_batchsize) break;
            allidset.insert(seg);
            idvec.insert(idvec.end(), diff.begin(), diff.end());
            _data_reuse.UpdateTrie(partial_root, &seg);
            currentnode++;
            seg_count = seg.getCount();
        }

        int idvecsize = idvec.size();
        std::cout << "batch " << i << std::endl;
        for (int j = 0; j < idvecsize; j++) {
            std::cout << idvec[j] << " "; 
        }
        std::cout << std::endl;

        // find optimal in this batch
        assert(idvecsize <= _batchsize);
        uint64_t permute = (1 << idvecsize) - 1;

        // should not compare the cost between batches, so reset cur_total
        cur_total = FLT_MAX;

        DECISION temp_decision = decision;
        for (; permute != (uint64_t)(-1); permute--) {
            for (int j = 0; j < idvecsize; j++) {
                if ((permute >> j) & 1)
                    temp_decision[idvec[j]] = PIM;
                else
                    temp_decision[idvec[j]] = CPU;
            }
            // PrintDecision(std::cout, temp_decision, true);
            COST temp_total = Cost(temp_decision, partial_root);
            if (temp_total < cur_total) {
                cur_total = temp_total;
                decision = temp_decision;
            }
        }
        std::cout << "cur_total = " << cur_total << std::endl;
        std::cout << seg_count << " " << reuse_max << " " << cur_total << std::endl;
        if (seg_count * reuse_max < _batchthreshold * cur_total) break;
    }
    // std::ofstream ofs("temp.dot", std::ofstream::out);
    // _cost_package->_data_reuse.print(ofs, partial_root);
    // ofs.close();

    _data_reuse.DeleteTrie(partial_root);

    const std::vector<ThreadBBLStats *> *sorted = getSorted();

    // assign decision for BBLs that did not occur in the reuse chains
    for (BBLID i = 0; i < sorted[CPU].size(); ++i) {
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

    cur_total = Cost(decision, _data_reuse.getRoot());
    // iterate over the remaining BBs 2 times until convergence
    for (int j = 0; j < 2; j++) {
        for (uint32_t i = 0; i < sorted[CPU].size(); i++) {
            BBLID id = i;

            if (decision[id] == CPU) {
                decision[id] = PIM;
                COST temp_total = Cost(decision, _data_reuse.getRoot());
                if (temp_total > cur_total)
                    decision[id] = CPU;
                else
                    cur_total = temp_total;
            }
            else {
                decision[id] = CPU;
                COST temp_total = Cost(decision, _data_reuse.getRoot());
                if (temp_total > cur_total)
                    decision[id] = PIM;
                else {
                    cur_total = temp_total;
                }
            }
        }
        std::cout << "cur_total = " << cur_total << std::endl;
    }

    ofs << "CPU only time (ns): " << ElapsedTime(CPU) << std::endl
    << "PIM only time (ns): " << ElapsedTime(PIM) << std::endl;

    DECISION greedy_decision;
    for (BBLID i = 0; i < sorted[CPU].size(); ++i) {
        auto *cpustats = sorted[CPU][i];
        auto *pimstats = sorted[PIM][i];
        if (cpustats->MaxElapsedTime() <= pimstats->MaxElapsedTime()) {
            greedy_decision.push_back(CPU);
        }
        else {
            greedy_decision.push_back(PIM);
        }
    }
    COST reuse_cost = ReuseCost(greedy_decision, _data_reuse.getRoot());
    auto elapsed_time = ElapsedTime(greedy_decision);
    COST total_time = reuse_cost + elapsed_time.first + elapsed_time.second;

    ofs << "Greedy offloading time (ns): " << total_time << " = CPU " << elapsed_time.first << " + PIM " << elapsed_time.second << " + REUSE " << reuse_cost << std::endl;

    reuse_cost = ReuseCost(decision, _data_reuse.getRoot());
    elapsed_time = ElapsedTime(decision);
    total_time = reuse_cost + elapsed_time.first + elapsed_time.second;

    ofs << "Reuse offloading time (ns): " << total_time << " = CPU " << elapsed_time.first << " + PIM " << elapsed_time.second << " + REUSE " << reuse_cost << std::endl;

    PrintDecision(ofs, decision, false);

    return decision;
}

COST CostSolver::Cost(const CostSolver::DECISION &decision, BBLIDTrieNode *reusetree)
{
    auto pair = ElapsedTime(decision);
    return (ReuseCost(decision, reusetree) + pair.first + pair.second);
}

COST CostSolver::ElapsedTime(CostSite site)
{
    const std::vector<ThreadBBLStats *> *sorted = getSorted();
    COST elapsed_time = 0;
    for (BBLID i = 0; i < sorted[site].size(); ++i) {
        auto *stats = sorted[site][i];
        elapsed_time += stats->MaxElapsedTime();
    }
    return elapsed_time;
}

std::pair<COST, COST> CostSolver::ElapsedTime(const DECISION &decision)
{
    COST cpu_elapsed_time = 0, pim_elapsed_time = 0;
    const std::vector<ThreadBBLStats *> *sorted = getSorted();
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

COST CostSolver::ReuseCost(const DECISION &decision, BBLIDTrieNode *reusetree)
{
    COST cur_reuse_cost = 0;
    std::map<BBLID, BBLIDTrieNode *>::iterator it = reusetree->_children.begin();
    std::map<BBLID, BBLIDTrieNode *>::iterator eit = reusetree->_children.end();
    for (; it != eit; ++it) {
        TrieBFS(cur_reuse_cost, decision, it->first, it->second, false);
    }
    return cur_reuse_cost;
}

void CostSolver::TrieBFS(COST &cost, const CostSolver::DECISION &decision, BBLID bblid, BBLIDTrieNode *root, bool isDifferent)
{
    if (root->_isLeaf) {
        if (isDifferent) {
            // If the initial W is on CPU and there are subsequent R/W on PIM,
            // then this segment contributes to a flush of CPU and data fetch from PIM.
            // We conservatively assume that the fetch will promote data to L1
            if (decision[bblid] == CPU) {
                // if the initial W can be parallelized, then we assume that
                // the data corresponding to the chain can be flushed/fetched in parallel
                // if (_cost_package->_bbl_parallelizable[bblid])
                //     cost += root->_count * (_flush_cost[CPU] / _cost_package->_core_count[CPU] + _fetch_cost[PIM] / _cost_package->_core_count[PIM]);
                // else
                    cost += root->_count * (_flush_cost[CPU] + _fetch_cost[PIM]);
            }
            // If the initial W is on PIM and there are subsequent R/W on CPU,
            // then this segment contributes to a flush of PIM and data fetch from CPU
            else {
                // if (_cost_package->_bbl_parallelizable[bblid])
                //     cost += root->_count * (_flush_cost[PIM] / _cost_package->_core_count[PIM] + _fetch_cost[CPU] / _cost_package->_core_count[CPU]);
                // else
                    cost += root->_count * (_flush_cost[PIM] + _fetch_cost[CPU]);
            }
        }
    }
    else {
        std::map<BBLID, BBLIDTrieNode *>::iterator it = root->_children.begin();
        std::map<BBLID, BBLIDTrieNode *>::iterator eit = root->_children.end();
        for (; it != eit; ++it) {
            if (isDifferent) {
                TrieBFS(cost, decision, it->first, it->second, true);
            }
            else if (decision[bblid] != decision[it->first]) {
                TrieBFS(cost, decision, it->first, it->second, true);
            }
            else {
                TrieBFS(cost, decision, it->first, it->second, false);
            }
        }
    }
}