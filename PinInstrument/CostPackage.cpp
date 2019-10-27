//===- CostPackage.cpp - Utils for instrumentation ------------*- C++ -*-===//
//
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#include "CostPackage.h"

using namespace PIMProf;

/* ===================================================================== */
/* CostPackage */
/* ===================================================================== */

void CostPackage::initialize()
{
    PIN_InitLock(&_thread_count_lock);
}