//=== AMDGPUPrintfRuntimeBinding.cpp - OpenCL printf implementation -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// \file
//
// The pass bind printfs to a kernel arg pointer that will be bound to a buffer
// later by the runtime.
//
// This pass traverses the functions in the module and converts
// each call to printf to a sequence of operations that
// store the following into the printf buffer:
// - format string (passed as a module's metadata unique ID)
// - bitwise copies of printf arguments
// The backend passes will need to store metadata in the kernel
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;

#define DEBUG_TYPE "printfToRuntime"
enum { DWORD_ALIGN = 4 };

namespace {
class AMDGPUPrintfRuntimeBinding final : public ModulePass {

public:
  static char ID;

  explicit AMDGPUPrintfRuntimeBinding();

private:
  bool runOnModule(Module &M) override;
};

class AMDGPUPrintfRuntimeBindingImpl {
public:
  AMDGPUPrintfRuntimeBindingImpl() = default;
  bool run(Module &M);

private:
  void getConversionSpecifiers(SmallVectorImpl<char> &OpConvSpecifiers,
                               StringRef fmt, size_t num_ops) const;

  bool lowerPrintfForGpu(Module &M);

  const DataLayout *TD;
  SmallVector<CallInst *, 32> Printfs;
};
} // namespace

char AMDGPUPrintfRuntimeBinding::ID = 0;

INITIALIZE_PASS_BEGIN(AMDGPUPrintfRuntimeBinding,
                      "amdgpu-printf-runtime-binding", "AMDGPU Printf lowering",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_END(AMDGPUPrintfRuntimeBinding, "amdgpu-printf-runtime-binding",
                    "AMDGPU Printf lowering", false, false)

char &llvm::AMDGPUPrintfRuntimeBindingID = AMDGPUPrintfRuntimeBinding::ID;

namespace llvm {
ModulePass *createAMDGPUPrintfRuntimeBinding() {
  return new AMDGPUPrintfRuntimeBinding();
}
} // namespace llvm

AMDGPUPrintfRuntimeBinding::AMDGPUPrintfRuntimeBinding() : ModulePass(ID) {
  initializeAMDGPUPrintfRuntimeBindingPass(*PassRegistry::getPassRegistry());
}

void AMDGPUPrintfRuntimeBindingImpl::getConversionSpecifiers(
    SmallVectorImpl<char> &OpConvSpecifiers, StringRef Fmt,
    size_t NumOps) const {
  // not all format characters are collected.
  // At this time the format characters of interest
  // are %p and %s, which use to know if we
  // are either storing a literal string or a
  // pointer to the printf buffer.
  static const char ConvSpecifiers[] = "cdieEfgGaosuxXp";
  size_t CurFmtSpecifierIdx = 0;
  size_t PrevFmtSpecifierIdx = 0;

  while ((CurFmtSpecifierIdx = Fmt.find_first_of(
              ConvSpecifiers, CurFmtSpecifierIdx)) != StringRef::npos) {
    bool ArgDump = false;
    StringRef CurFmt = Fmt.substr(PrevFmtSpecifierIdx,
                                  CurFmtSpecifierIdx - PrevFmtSpecifierIdx);
    size_t pTag = CurFmt.find_last_of('%');
    if (pTag != StringRef::npos) {
      ArgDump = true;
      while (pTag && CurFmt[--pTag] == '%') {
        ArgDump = !ArgDump;
      }
    }

    if (ArgDump)
      OpConvSpecifiers.push_back(Fmt[CurFmtSpecifierIdx]);

    PrevFmtSpecifierIdx = ++CurFmtSpecifierIdx;
  }
}

static bool shouldPrintAsStr(char Specifier, Type *OpType) {
  return Specifier == 's' && isa<PointerType>(OpType);
}

constexpr StringLiteral NonLiteralStr("???");
static_assert(NonLiteralStr.size() == 3);

static StringRef getAsConstantStr(Value *V) {
  StringRef S;
  if (!getConstantStringInfo(V, S))
    S = NonLiteralStr;

  return S;
}

static void diagnoseInvalidFormatString(const CallBase *CI) {
  DiagnosticInfoUnsupported UnsupportedFormatStr(
      *CI->getParent()->getParent(),
      "printf format string must be a trivially resolved constant string "
      "global variable",
      CI->getDebugLoc());
  CI->getContext().diagnose(UnsupportedFormatStr);
}

bool AMDGPUPrintfRuntimeBindingImpl::lowerPrintfForGpu(Module &M) {
  LLVMContext &Ctx = M.getContext();
  IRBuilder<> Builder(Ctx);
  Type *I32Ty = Type::getInt32Ty(Ctx);

  // Instead of creating global variables, the printf format strings are
  // extracted and passed as metadata. This avoids polluting llvm's symbol
  // tables in this module. Metadata is going to be extracted by the backend
  // passes and inserted into the OpenCL binary as appropriate.
  NamedMDNode *metaD = M.getOrInsertNamedMetadata("llvm.printf.fmts");
  unsigned UniqID = metaD->getNumOperands();

  for (auto *CI : Printfs) {
    unsigned NumOps = CI->arg_size();

    SmallString<16> OpConvSpecifiers;
    Value *Op = CI->getArgOperand(0);

    StringRef FormatStr;
    if (!getConstantStringInfo(Op, FormatStr)) {
      Value *Stripped = Op->stripPointerCasts();
      if (!isa<UndefValue>(Stripped) && !isa<ConstantPointerNull>(Stripped))
        diagnoseInvalidFormatString(CI);
      continue;
    }

    // We need this call to ascertain that we are printing a string or a
    // pointer. It takes out the specifiers and fills up the first arg.
    getConversionSpecifiers(OpConvSpecifiers, FormatStr, NumOps - 1);

    // Add metadata for the string
    std::string AStreamHolder;
    raw_string_ostream Sizes(AStreamHolder);
    int Sum = DWORD_ALIGN;
    Sizes << CI->arg_size() - 1;
    Sizes << ':';
    for (unsigned ArgCount = 1;
         ArgCount < CI->arg_size() && ArgCount <= OpConvSpecifiers.size();
         ArgCount++) {
      Value *Arg = CI->getArgOperand(ArgCount);
      Type *ArgType = Arg->getType();
      unsigned ArgSize = TD->getTypeAllocSize(ArgType);
      //
      // ArgSize by design should be a multiple of DWORD_ALIGN,
      // expand the arguments that do not follow this rule.
      //
      if (ArgSize % DWORD_ALIGN != 0) {
        Type *ResType = Type::getInt32Ty(Ctx);
        if (auto *VecType = dyn_cast<VectorType>(ArgType))
          ResType = VectorType::get(ResType, VecType->getElementCount());
        Builder.SetInsertPoint(CI);
        Builder.SetCurrentDebugLocation(CI->getDebugLoc());

        if (ArgType->isFloatingPointTy()) {
          Arg = Builder.CreateBitCast(
              Arg,
              IntegerType::getIntNTy(Ctx, ArgType->getPrimitiveSizeInBits()));
        }

        if (OpConvSpecifiers[ArgCount - 1] == 'x' ||
            OpConvSpecifiers[ArgCount - 1] == 'X' ||
            OpConvSpecifiers[ArgCount - 1] == 'u' ||
            OpConvSpecifiers[ArgCount - 1] == 'o')
          Arg = Builder.CreateZExt(Arg, ResType);
        else
          Arg = Builder.CreateSExt(Arg, ResType);
        ArgType = Arg->getType();
        ArgSize = TD->getTypeAllocSize(ArgType);
        CI->setOperand(ArgCount, Arg);
      }
      if (OpConvSpecifiers[ArgCount - 1] == 'f') {
        ConstantFP *FpCons = dyn_cast<ConstantFP>(Arg);
        if (FpCons)
          ArgSize = 4;
        else {
          FPExtInst *FpExt = dyn_cast<FPExtInst>(Arg);
          if (FpExt && FpExt->getType()->isDoubleTy() &&
              FpExt->getOperand(0)->getType()->isFloatTy())
            ArgSize = 4;
        }
      }
      if (shouldPrintAsStr(OpConvSpecifiers[ArgCount - 1], ArgType))
        ArgSize = alignTo(getAsConstantStr(Arg).size() + 1, 4);

      LLVM_DEBUG(dbgs() << "Printf ArgSize (in buffer) = " << ArgSize
                        << " for type: " << *ArgType << '\n');
      Sizes << ArgSize << ':';
      Sum += ArgSize;
    }
    LLVM_DEBUG(dbgs() << "Printf format string in source = " << FormatStr
                      << '\n');
    for (char C : FormatStr) {
      // Rest of the C escape sequences (e.g. \') are handled correctly
      // by the MDParser
      switch (C) {
      case '\a':
        Sizes << "\\a";
        break;
      case '\b':
        Sizes << "\\b";
        break;
      case '\f':
        Sizes << "\\f";
        break;
      case '\n':
        Sizes << "\\n";
        break;
      case '\r':
        Sizes << "\\r";
        break;
      case '\v':
        Sizes << "\\v";
        break;
      case ':':
        // ':' cannot be scanned by Flex, as it is defined as a delimiter
        // Replace it with it's octal representation \72
        Sizes << "\\72";
        break;
      default:
        Sizes << C;
        break;
      }
    }

    // Insert the printf_alloc call
    Builder.SetInsertPoint(CI);
    Builder.SetCurrentDebugLocation(CI->getDebugLoc());

    AttributeList Attr = AttributeList::get(Ctx, AttributeList::FunctionIndex,
                                            Attribute::NoUnwind);

    Type *SizetTy = Type::getInt32Ty(Ctx);

    Type *Tys_alloc[1] = {SizetTy};
    Type *I8Ty = Type::getInt8Ty(Ctx);
    Type *I8Ptr = PointerType::get(I8Ty, 1);
    FunctionType *FTy_alloc = FunctionType::get(I8Ptr, Tys_alloc, false);
    FunctionCallee PrintfAllocFn =
        M.getOrInsertFunction(StringRef("__printf_alloc"), FTy_alloc, Attr);

    LLVM_DEBUG(dbgs() << "Printf metadata = " << Sizes.str() << '\n');
    std::string fmtstr = itostr(++UniqID) + ":" + Sizes.str();
    MDString *fmtStrArray = MDString::get(Ctx, fmtstr);

    MDNode *myMD = MDNode::get(Ctx, fmtStrArray);
    metaD->addOperand(myMD);
    Value *sumC = ConstantInt::get(SizetTy, Sum, false);
    SmallVector<Value *, 1> alloc_args;
    alloc_args.push_back(sumC);
    CallInst *pcall = CallInst::Create(PrintfAllocFn, alloc_args,
                                       "printf_alloc_fn", CI->getIterator());

    //
    // Insert code to split basicblock with a
    // piece of hammock code.
    // basicblock splits after buffer overflow check
    //
    ConstantPointerNull *zeroIntPtr =
        ConstantPointerNull::get(PointerType::get(I8Ty, 1));
    auto *cmp = cast<ICmpInst>(Builder.CreateICmpNE(pcall, zeroIntPtr, ""));
    if (!CI->use_empty()) {
      Value *result =
          Builder.CreateSExt(Builder.CreateNot(cmp), I32Ty, "printf_res");
      CI->replaceAllUsesWith(result);
    }
    SplitBlock(CI->getParent(), cmp);
    Instruction *Brnch =
        SplitBlockAndInsertIfThen(cmp, cmp->getNextNode(), false);
    BasicBlock::iterator BrnchPoint = Brnch->getIterator();

    Builder.SetInsertPoint(Brnch);

    // store unique printf id in the buffer
    //
    GetElementPtrInst *BufferIdx = GetElementPtrInst::Create(
        I8Ty, pcall, ConstantInt::get(Ctx, APInt(32, 0)), "PrintBuffID",
        BrnchPoint);

    Type *idPointer = PointerType::get(I32Ty, AMDGPUAS::GLOBAL_ADDRESS);
    Value *id_gep_cast =
        new BitCastInst(BufferIdx, idPointer, "PrintBuffIdCast", BrnchPoint);

    new StoreInst(ConstantInt::get(I32Ty, UniqID), id_gep_cast, BrnchPoint);

    // 1st 4 bytes hold the printf_id
    // the following GEP is the buffer pointer
    BufferIdx = GetElementPtrInst::Create(I8Ty, pcall,
                                          ConstantInt::get(Ctx, APInt(32, 4)),
                                          "PrintBuffGep", BrnchPoint);

    Type *Int32Ty = Type::getInt32Ty(Ctx);
    for (unsigned ArgCount = 1;
         ArgCount < CI->arg_size() && ArgCount <= OpConvSpecifiers.size();
         ArgCount++) {
      Value *Arg = CI->getArgOperand(ArgCount);
      Type *ArgType = Arg->getType();
      SmallVector<Value *, 32> WhatToStore;
      if (ArgType->isFPOrFPVectorTy() && !isa<VectorType>(ArgType)) {
        if (OpConvSpecifiers[ArgCount - 1] == 'f') {
          if (auto *FpCons = dyn_cast<ConstantFP>(Arg)) {
            APFloat Val(FpCons->getValueAPF());
            bool Lost = false;
            Val.convert(APFloat::IEEEsingle(), APFloat::rmNearestTiesToEven,
                        &Lost);
            Arg = ConstantFP::get(Ctx, Val);
          } else if (auto *FpExt = dyn_cast<FPExtInst>(Arg)) {
            if (FpExt->getType()->isDoubleTy() &&
                FpExt->getOperand(0)->getType()->isFloatTy()) {
              Arg = FpExt->getOperand(0);
            }
          }
        }
        WhatToStore.push_back(Arg);
      } else if (isa<PointerType>(ArgType)) {
        if (shouldPrintAsStr(OpConvSpecifiers[ArgCount - 1], ArgType)) {
          StringRef S = getAsConstantStr(Arg);
          if (!S.empty()) {
            const uint64_t ReadSize = 4;

            DataExtractor Extractor(S, /*IsLittleEndian=*/true, 8);
            DataExtractor::Cursor Offset(0);
            while (Offset && Offset.tell() < S.size()) {
              uint64_t ReadNow = std::min(ReadSize, S.size() - Offset.tell());
              uint64_t ReadBytes = 0;
              switch (ReadNow) {
              default: llvm_unreachable("min(4, X) > 4?");
              case 1:
                ReadBytes = Extractor.getU8(Offset);
                break;
              case 2:
                ReadBytes = Extractor.getU16(Offset);
                break;
              case 3:
                ReadBytes = Extractor.getU24(Offset);
                break;
              case 4:
                ReadBytes = Extractor.getU32(Offset);
                break;
              }

              cantFail(Offset.takeError(),
                       "failed to read bytes from constant array");

              APInt IntVal(8 * ReadSize, ReadBytes);

              // TODO: Should not bothering aligning up.
              if (ReadNow < ReadSize)
                IntVal = IntVal.zext(8 * ReadSize);

              Type *IntTy = Type::getIntNTy(Ctx, IntVal.getBitWidth());
              WhatToStore.push_back(ConstantInt::get(IntTy, IntVal));
            }
          } else {
            // Empty string, give a hint to RT it is no NULL
            Value *ANumV = ConstantInt::get(Int32Ty, 0xFFFFFF00, false);
            WhatToStore.push_back(ANumV);
          }
        } else {
          WhatToStore.push_back(Arg);
        }
      } else {
        WhatToStore.push_back(Arg);
      }
      for (unsigned I = 0, E = WhatToStore.size(); I != E; ++I) {
        Value *TheBtCast = WhatToStore[I];
        unsigned ArgSize = TD->getTypeAllocSize(TheBtCast->getType());
        StoreInst *StBuff = new StoreInst(TheBtCast, BufferIdx, BrnchPoint);
        LLVM_DEBUG(dbgs() << "inserting store to printf buffer:\n"
                          << *StBuff << '\n');
        (void)StBuff;
        if (I + 1 == E && ArgCount + 1 == CI->arg_size())
          break;
        BufferIdx = GetElementPtrInst::Create(
            I8Ty, BufferIdx, {ConstantInt::get(I32Ty, ArgSize)},
            "PrintBuffNextPtr", BrnchPoint);
        LLVM_DEBUG(dbgs() << "inserting gep to the printf buffer:\n"
                          << *BufferIdx << '\n');
      }
    }
  }

  // erase the printf calls
  for (auto *CI : Printfs)
    CI->eraseFromParent();

  Printfs.clear();
  return true;
}

bool AMDGPUPrintfRuntimeBindingImpl::run(Module &M) {
  Triple TT(M.getTargetTriple());
  if (TT.getArch() == Triple::r600)
    return false;

  auto PrintfFunction = M.getFunction("printf");
  if (!PrintfFunction || !PrintfFunction->isDeclaration())
    return false;

  for (auto &U : PrintfFunction->uses()) {
    if (auto *CI = dyn_cast<CallInst>(U.getUser())) {
      if (CI->isCallee(&U) && !CI->isNoBuiltin())
        Printfs.push_back(CI);
    }
  }

  if (Printfs.empty())
    return false;

  TD = &M.getDataLayout();

  return lowerPrintfForGpu(M);
}

bool AMDGPUPrintfRuntimeBinding::runOnModule(Module &M) {
  return AMDGPUPrintfRuntimeBindingImpl().run(M);
}

PreservedAnalyses
AMDGPUPrintfRuntimeBindingPass::run(Module &M, ModuleAnalysisManager &AM) {
  bool Changed = AMDGPUPrintfRuntimeBindingImpl().run(M);
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
