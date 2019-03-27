//===- PinInstrument.cpp - Utils for instrumentation ------------*- C++ -*-===//
//
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#include <vector>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <string>
#include <unistd.h>


#include "../LLVMAnalysis/Common.h"
#include "PinInstrument.h"

#include "INIReader.h"

using namespace PIMProf;


InstructionLatency::InstructionLatency()
{
    for (UINT32 i = 0; i < MAX_INDEX; i++) {
        latencytable[i] = 1;
    }
    ReadConfig("defaultlatency.ini");
}

InstructionLatency::InstructionLatency(const std::string filename)
{
    InstructionLatency();
    ReadConfig(filename);
}

VOID InstructionLatency::ReadConfig(const std::string filename)
{
    INIReader reader(filename);
    for (UINT32 i = 0; i < MAX_INDEX; i++) {
        std::string opcodestr = OPCODE_StringShort(i);
        if (opcodestr != "LAST") {
            long latency = reader.GetInteger("InstructionLatency", opcodestr, -1);
            if (latency >= 0) {
                latencytable[i] = latency;
            }
        }
    }
}

VOID InstructionLatency::WriteConfig(ostream& out)
{
    out << "[InstructionLatency]" << std::endl
        << "; <Instuction Name> = <Instruction Latency>" << std::endl;
    for (UINT32 i = 0; i < MAX_INDEX; i++)
    {
        std::string opcodestr = OPCODE_StringShort(i);
        if (opcodestr != "LAST") {
            opcodestr = ljstr(opcodestr, 15);
            out << opcodestr << "= " << latencytable[i] << std::endl;
        }
    }
}

VOID InstructionLatency::WriteConfig(const std::string filename)
{
    ofstream out;
    out.open(filename.c_str(), ios_base::out);
    WriteConfig(out);
    out.close();
}

static VOID MemoryLatency::InsRef(ADDRINT addr)
{
    const UINT32 size = 1; // assuming access does not cross cache lines
    const CACHE_BASE::ACCESS_TYPE accessType = CACHE_BASE::ACCESS_TYPE_LOAD;

    // ITLB
    itlb.AccessSingleLine(addr, accessType);

    // first level I-cache
    const BOOL il1Hit = il1.AccessSingleLine(addr, accessType);

    // second level unified Cache
    if ( ! il1Hit) Ul2Access(addr, size, accessType);
}

static VOID MemoryLatency::MemRefMulti(ADDRINT addr, UINT32 size, CACHE_BASE::ACCESS_TYPE accessType)
{
    // DTLB
    dtlb.AccessSingleLine(addr, CACHE_BASE::ACCESS_TYPE_LOAD);

    // first level D-cache
    const BOOL dl1Hit = dl1.Access(addr, size, accessType);

    // second level unified Cache
    if ( ! dl1Hit) Ul2Access(addr, size, accessType);
}

static VOID MemoryLatency::MemRefSingle(ADDRINT addr, UINT32 size, CACHE_BASE::ACCESS_TYPE accessType)
{
    // DTLB
    dtlb.AccessSingleLine(addr, CACHE_BASE::ACCESS_TYPE_LOAD);

    // first level D-cache
    const BOOL dl1Hit = dl1.AccessSingleLine(addr, accessType);

    // second level unified Cache
    if ( ! dl1Hit) Ul2Access(addr, size, accessType);
}

static VOID MemoryLatency::Instruction(INS ins, VOID *v)
{
    // all instruction fetches access I-cache
    INS_InsertCall(
        ins, IPOINT_BEFORE, (AFUNPTR)InsRef,
        IARG_INST_PTR,
        IARG_END);

    if (INS_IsMemoryRead(ins) && INS_IsStandardMemop(ins))
    {
        const UINT32 size = INS_MemoryReadSize(ins);
        const AFUNPTR countFun = (size <= 4 ? (AFUNPTR) MemRefSingle : (AFUNPTR) MemRefMulti);

        // only predicated-on memory instructions access D-cache
        INS_InsertPredicatedCall(
            ins, IPOINT_BEFORE, countFun,
            IARG_MEMORYREAD_EA,
            IARG_MEMORYREAD_SIZE,
            IARG_UINT32, CACHE_BASE::ACCESS_TYPE_LOAD,
            IARG_END);
    }

    if (INS_IsMemoryWrite(ins) && INS_IsStandardMemop(ins))
    {
        const UINT32 size = INS_MemoryWriteSize(ins);
        const AFUNPTR countFun = (size <= 4 ? (AFUNPTR) MemRefSingle : (AFUNPTR) MemRefMulti);

        // only predicated-on memory instructions access D-cache
        INS_InsertPredicatedCall(
            ins, IPOINT_BEFORE, countFun,
            IARG_MEMORYWRITE_EA,
            IARG_MEMORYWRITE_SIZE,
            IARG_UINT32, CACHE_BASE::ACCESS_TYPE_STORE,
            IARG_END);
    }
}

