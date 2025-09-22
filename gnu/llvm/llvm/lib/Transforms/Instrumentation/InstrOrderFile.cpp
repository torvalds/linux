//===- InstrOrderFile.cpp ---- Late IR instrumentation for order file ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Instrumentation/InstrOrderFile.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Instrumentation.h"
#include <fstream>
#include <mutex>
#include <sstream>

using namespace llvm;
#define DEBUG_TYPE "instrorderfile"

static cl::opt<std::string> ClOrderFileWriteMapping(
    "orderfile-write-mapping", cl::init(""),
    cl::desc(
        "Dump functions and their MD5 hash to deobfuscate profile data"),
    cl::Hidden);

namespace {

// We need a global bitmap to tell if a function is executed. We also
// need a global variable to save the order of functions. We can use a
// fixed-size buffer that saves the MD5 hash of the function. We need
// a global variable to save the index into the buffer.

std::mutex MappingMutex;

struct InstrOrderFile {
private:
  GlobalVariable *OrderFileBuffer;
  GlobalVariable *BufferIdx;
  GlobalVariable *BitMap;
  ArrayType *BufferTy;
  ArrayType *MapTy;

public:
  InstrOrderFile() = default;

  void createOrderFileData(Module &M) {
    LLVMContext &Ctx = M.getContext();
    int NumFunctions = 0;
    for (Function &F : M) {
      if (!F.isDeclaration())
        NumFunctions++;
    }

    BufferTy =
        ArrayType::get(Type::getInt64Ty(Ctx), INSTR_ORDER_FILE_BUFFER_SIZE);
    Type *IdxTy = Type::getInt32Ty(Ctx);
    MapTy = ArrayType::get(Type::getInt8Ty(Ctx), NumFunctions);

    // Create the global variables.
    std::string SymbolName = INSTR_PROF_ORDERFILE_BUFFER_NAME_STR;
    OrderFileBuffer = new GlobalVariable(M, BufferTy, false, GlobalValue::LinkOnceODRLinkage,
                           Constant::getNullValue(BufferTy), SymbolName);
    Triple TT = Triple(M.getTargetTriple());
    OrderFileBuffer->setSection(
        getInstrProfSectionName(IPSK_orderfile, TT.getObjectFormat()));

    std::string IndexName = INSTR_PROF_ORDERFILE_BUFFER_IDX_NAME_STR;
    BufferIdx = new GlobalVariable(M, IdxTy, false, GlobalValue::LinkOnceODRLinkage,
                           Constant::getNullValue(IdxTy), IndexName);

    std::string BitMapName = "bitmap_0";
    BitMap = new GlobalVariable(M, MapTy, false, GlobalValue::PrivateLinkage,
                                Constant::getNullValue(MapTy), BitMapName);
  }

  // Generate the code sequence in the entry block of each function to
  // update the buffer.
  void generateCodeSequence(Module &M, Function &F, int FuncId) {
    if (!ClOrderFileWriteMapping.empty()) {
      std::lock_guard<std::mutex> LogLock(MappingMutex);
      std::error_code EC;
      llvm::raw_fd_ostream OS(ClOrderFileWriteMapping, EC,
                              llvm::sys::fs::OF_Append);
      if (EC) {
        report_fatal_error(Twine("Failed to open ") + ClOrderFileWriteMapping +
                           " to save mapping file for order file instrumentation\n");
      } else {
        std::stringstream stream;
        stream << std::hex << MD5Hash(F.getName());
        std::string singleLine = "MD5 " + stream.str() + " " +
                                 std::string(F.getName()) + '\n';
        OS << singleLine;
      }
    }

    BasicBlock *OrigEntry = &F.getEntryBlock();

    LLVMContext &Ctx = M.getContext();
    IntegerType *Int32Ty = Type::getInt32Ty(Ctx);
    IntegerType *Int8Ty = Type::getInt8Ty(Ctx);

    // Create a new entry block for instrumentation. We will check the bitmap
    // in this basic block.
    BasicBlock *NewEntry =
        BasicBlock::Create(M.getContext(), "order_file_entry", &F, OrigEntry);
    IRBuilder<> entryB(NewEntry);
    // Create a basic block for updating the circular buffer.
    BasicBlock *UpdateOrderFileBB =
        BasicBlock::Create(M.getContext(), "order_file_set", &F, OrigEntry);
    IRBuilder<> updateB(UpdateOrderFileBB);

    // Check the bitmap, if it is already 1, do nothing.
    // Otherwise, set the bit, grab the index, update the buffer.
    Value *IdxFlags[] = {ConstantInt::get(Int32Ty, 0),
                         ConstantInt::get(Int32Ty, FuncId)};
    Value *MapAddr = entryB.CreateGEP(MapTy, BitMap, IdxFlags, "");
    LoadInst *loadBitMap = entryB.CreateLoad(Int8Ty, MapAddr, "");
    entryB.CreateStore(ConstantInt::get(Int8Ty, 1), MapAddr);
    Value *IsNotExecuted =
        entryB.CreateICmpEQ(loadBitMap, ConstantInt::get(Int8Ty, 0));
    entryB.CreateCondBr(IsNotExecuted, UpdateOrderFileBB, OrigEntry);

    // Fill up UpdateOrderFileBB: grab the index, update the buffer!
    Value *IdxVal = updateB.CreateAtomicRMW(
        AtomicRMWInst::Add, BufferIdx, ConstantInt::get(Int32Ty, 1),
        MaybeAlign(), AtomicOrdering::SequentiallyConsistent);
    // We need to wrap around the index to fit it inside the buffer.
    Value *WrappedIdx = updateB.CreateAnd(
        IdxVal, ConstantInt::get(Int32Ty, INSTR_ORDER_FILE_BUFFER_MASK));
    Value *BufferGEPIdx[] = {ConstantInt::get(Int32Ty, 0), WrappedIdx};
    Value *BufferAddr =
        updateB.CreateGEP(BufferTy, OrderFileBuffer, BufferGEPIdx, "");
    updateB.CreateStore(ConstantInt::get(Type::getInt64Ty(Ctx), MD5Hash(F.getName())),
                        BufferAddr);
    updateB.CreateBr(OrigEntry);
  }

  bool run(Module &M) {
    createOrderFileData(M);

    int FuncId = 0;
    for (Function &F : M) {
      if (F.isDeclaration())
        continue;
      generateCodeSequence(M, F, FuncId);
      ++FuncId;
    }

    return true;
  }

}; // End of InstrOrderFile struct
} // End anonymous namespace

PreservedAnalyses
InstrOrderFilePass::run(Module &M, ModuleAnalysisManager &AM) {
  if (InstrOrderFile().run(M))
    return PreservedAnalyses::none();
  return PreservedAnalyses::all();
}
