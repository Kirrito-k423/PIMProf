//===- AnnotationGeneration.cpp - Generate BB annotator ----------*- C++ -*-===//
//
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/IRPrintingPasses.h"

#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CommandLine.h"

#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"

#include "Common.h"

using namespace llvm;

LLVMContext ctx;

static cl::opt<std::string> OutputFilename(
        "o",
        cl::desc("Specify filename of output for the generator of PIMProfAnnotation."),
        cl::value_desc("outputfile"),
        cl::init("")
    );

// provide definition of function if it has not been defined
// intended behavior:
// ; Function Attrs: noinline nounwind optnone uwtable
// define i64 @Annotation(i64, i64, i64) {
//     %2 = alloca i64, align 4
//     store i64 %0, i64* %2, align 4
//     %3 = load i64, i64* %2, align 4
//     ret i64 %3
// }
void CreateAnnotationFunction(const std::string name, Module &M)
{
    // declare annotator function 
    Function *annotator = dyn_cast<Function>(
        M.getOrInsertFunction(
            name, 
            FunctionType::getInt64Ty(ctx), 
            Type::getInt64Ty(ctx),
            Type::getInt64Ty(ctx),
            Type::getInt64Ty(ctx)
        )
    );

    // add attribute to function
    // which suppresses essentially all optimizations on a function or method
    annotator->addFnAttr(Attribute::NoInline);
    annotator->addFnAttr(Attribute::NoUnwind);
    annotator->addFnAttr(Attribute::OptimizeNone);
    annotator->addFnAttr(Attribute::UWTable);

    // create instructions
    if (annotator->empty()) {
        BasicBlock *temp = BasicBlock::Create(
            ctx, "", annotator, 0);
        auto al = new AllocaInst(Type::getInt64Ty(ctx), 0, "", temp);
        al->setAlignment(4);

        auto it = annotator->arg_begin();
        auto eit = annotator->arg_end();
        for (; it != eit; it++) {
            auto st = new StoreInst(it, al, temp);
            st->setAlignment(4);
        }
        
        auto ld = new LoadInst(al, "", temp);
        ld->setAlignment(4);
        auto rt = ReturnInst::Create(ctx, ld, temp);
        // insert instruction metadata
        MDNode* md = MDNode::get(
            ctx, 
            ConstantAsMetadata::get(
                ConstantInt::get(
                    IntegerType::get(M.getContext(), 64), PIMProfAnnotationBBLID)
            )
        );
        rt->setMetadata(PIMProfBBLIDMetadata, md);
    }
}


int main(int argc, char **argv) {
    cl::ParseCommandLineOptions(argc, argv);
    // Module Construction
    Module *M = new Module("AnnotationGeneration", ctx);
    M->setSourceFileName("AnnotationGeneration");
    M->setDataLayout("e-m:e-i64:64-f80:128-n8:16:32:64-S128");
    M->setTargetTriple("x86_64-pc-linux-gnu");

    CreateAnnotationFunction(PIMProfAnnotationHead, *M);
    CreateAnnotationFunction(PIMProfAnnotationTail, *M);

    std::error_code EC;
    raw_fd_ostream os(OutputFilename, EC,
        static_cast<sys::fs::OpenFlags>(0));
    WriteBitcodeToFile(*M, os);

  return 0;
}