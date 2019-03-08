#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/IRPrintingPasses.h"

#include "llvm/IRReader/IRReader.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"

#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"

#include "CFGDump.h"
#include "Common.h"

using namespace llvm;

LLVMContext ctx;

void BasicBlockDFS(BasicBlock *BB, int depth)
{
    std::string blanks(depth * 2, ' ');
    errs() << blanks << depth << BB->getName() << "\n";
    const TerminatorInst *TInst = BB->getTerminator();
    for (unsigned I = 0, NSucc = TInst->getNumSuccessors(); I < NSucc; ++I) {
        BasicBlock *Succ = TInst->getSuccessor(I);
        BasicBlockDFS(Succ, depth + 1);
    // Do stuff with Succ
    }
}

int main(int argc, char **argv) {
    // Module Construction
    std::unique_ptr<Module> M;
    if (argc != 2) {
        errs()  << "Incorrect input format.\n"
                << "Usage ./CFGDump.exe <filename>\n";
        return -1;
    }
    else {
        SMDiagnostic error;
        M = parseIRFile(argv[1], error, ctx);
        if (M == nullptr) {
            errs()  << "Filename error.\n"
                    << "Usage ./CFGDump.exe <filename>\n";
            return -1;
        }
    }

    // M->print(errs(), nullptr);

    // dump the entire CFG of the module
    Function *main_func = M->getFunction("main");
    main_func->print(errs());
    BasicBlock *BB_head = &main_func->getEntryBlock();
    BasicBlockDFS(BB_head, 0);
    BB_head->print(errs());
    BB_head->setName()
    
    errs() << "succ:\n";
    const TerminatorInst *TInst = BB_head->getTerminator();
    for (unsigned I = 0, NSucc = TInst->getNumSuccessors(); I < NSucc; ++I) {
        BasicBlock *Succ = TInst->getSuccessor(I);
        Succ->print(errs());
    // Do stuff with Succ
    }
    
  return 0;
}
