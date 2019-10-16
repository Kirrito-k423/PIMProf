//===- CostPackage.h - Utils for instrumentation ------------*- C++ -*-===//
//
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

/* ===================================================================== */
/* CostPackage */
/* ===================================================================== */
#ifndef __COSTPACKAGE_H__
#define __COSTPACKAGE_H__

#include <stack>
#include <algorithm>
#include <iostream>
#include <unordered_map>

#include "pin.H"
#include "PinUtil.h"
#include "DataReuse.h"
#include "INIReader.h"

namespace PIMProf {
static const UINT32 MAX_INDEX = 4096;
static const UINT32 INDEX_SPECIAL = 3000;
static const UINT32 MAX_MEM_SIZE = 512;


class HashFunc
{
  public:
    // assuming UUID is already murmurhash-ed.
    std::size_t operator()(const UUID &key) const
    {
        size_t result = key.first ^ key.second;
        return result;
    }
};

class CostPackage {
  public:
    // BBLScope information
    BBLScope _bbl_scope;
    std::unordered_map<UUID, UINT32, HashFunc> _bbl_hash;
    BBLID _bbl_size = 0;
    /// whether this region is in openmp
    std::vector<bool> _inOpenMPRegion;
    /// the total instruction cost of each BB
    std::vector<COST> _bbl_instruction_cost[MAX_COST_SITE];
    /// the total memory cost of each BB
    std::vector<COST> _bbl_memory_cost[MAX_COST_SITE];

    long long int _instr_cnt;
    long long int _mem_instr_cnt;
    long long int _nonmem_instr_cnt;

    /// Construction of latency table follows the opcode generation function in
    /// $(PIN_ROOT)/source/tools/SimpleExamples/opcodemix.cpp
    COST _instruction_latency[MAX_COST_SITE][MAX_INDEX];
    /// the cost multiplier of each cost site
    COST _instruction_multiplier[MAX_COST_SITE];

    COST _ilp[MAX_COST_SITE];
    COST _mlp[MAX_COST_SITE];
    UINT32 _core_count[MAX_COST_SITE];


    /// the control latency when switching between sites
    COST _control_latency[MAX_COST_SITE][MAX_COST_SITE];


    DataReuse _data_reuse;

    std::unordered_map<ADDRINT, DataReuseSegment> _tag_seg_map;

  public:
    void initialize();

    inline COST BBLInstructionCost(CostSite site, BBLID bbl) {
        if (_inOpenMPRegion[bbl]) {
            // infomsg() << "wow" << bbl << std::endl;
            return _bbl_instruction_cost[site][bbl] * _instruction_multiplier[site] / _ilp[site] / _core_count[site];
        }
        else {
            return _bbl_instruction_cost[site][bbl] * _instruction_multiplier[site] / _ilp[site];
        }
        
    }
    inline COST BBLMemoryCost(CostSite site, BBLID bbl) {
        if (_inOpenMPRegion[bbl]) {
            return _bbl_memory_cost[site][bbl] / _mlp[site] / _core_count[site];
        }
        else {
            return _bbl_memory_cost[site][bbl] / _mlp[site];
        }
    }
};

} // namespace PIMProf

#endif // __COSTPACKAGE_H__