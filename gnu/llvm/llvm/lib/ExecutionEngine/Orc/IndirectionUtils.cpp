//===---- IndirectionUtils.cpp - Utilities for call indirection in Orc ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/IndirectionUtils.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ExecutionEngine/JITLink/x86_64.h"
#include "llvm/ExecutionEngine/Orc/OrcABISupport.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/Support/Format.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include <sstream>

#define DEBUG_TYPE "orc"

using namespace llvm;
using namespace llvm::orc;

namespace {

class CompileCallbackMaterializationUnit : public orc::MaterializationUnit {
public:
  using CompileFunction = JITCompileCallbackManager::CompileFunction;

  CompileCallbackMaterializationUnit(SymbolStringPtr Name,
                                     CompileFunction Compile)
      : MaterializationUnit(Interface(
            SymbolFlagsMap({{Name, JITSymbolFlags::Exported}}), nullptr)),
        Name(std::move(Name)), Compile(std::move(Compile)) {}

  StringRef getName() const override { return "<Compile Callbacks>"; }

private:
  void materialize(std::unique_ptr<MaterializationResponsibility> R) override {
    SymbolMap Result;
    Result[Name] = {Compile(), JITSymbolFlags::Exported};
    // No dependencies, so these calls cannot fail.
    cantFail(R->notifyResolved(Result));
    cantFail(R->notifyEmitted({}));
  }

  void discard(const JITDylib &JD, const SymbolStringPtr &Name) override {
    llvm_unreachable("Discard should never occur on a LMU?");
  }

  SymbolStringPtr Name;
  CompileFunction Compile;
};

} // namespace

namespace llvm {
namespace orc {

TrampolinePool::~TrampolinePool() = default;
void IndirectStubsManager::anchor() {}

Expected<ExecutorAddr>
JITCompileCallbackManager::getCompileCallback(CompileFunction Compile) {
  if (auto TrampolineAddr = TP->getTrampoline()) {
    auto CallbackName =
        ES.intern(std::string("cc") + std::to_string(++NextCallbackId));

    std::lock_guard<std::mutex> Lock(CCMgrMutex);
    AddrToSymbol[*TrampolineAddr] = CallbackName;
    cantFail(
        CallbacksJD.define(std::make_unique<CompileCallbackMaterializationUnit>(
            std::move(CallbackName), std::move(Compile))));
    return *TrampolineAddr;
  } else
    return TrampolineAddr.takeError();
}

ExecutorAddr
JITCompileCallbackManager::executeCompileCallback(ExecutorAddr TrampolineAddr) {
  SymbolStringPtr Name;

  {
    std::unique_lock<std::mutex> Lock(CCMgrMutex);
    auto I = AddrToSymbol.find(TrampolineAddr);

    // If this address is not associated with a compile callback then report an
    // error to the execution session and return ErrorHandlerAddress to the
    // callee.
    if (I == AddrToSymbol.end()) {
      Lock.unlock();
      ES.reportError(
          make_error<StringError>("No compile callback for trampoline at " +
                                      formatv("{0:x}", TrampolineAddr),
                                  inconvertibleErrorCode()));
      return ErrorHandlerAddress;
    } else
      Name = I->second;
  }

  if (auto Sym =
          ES.lookup(makeJITDylibSearchOrder(
                        &CallbacksJD, JITDylibLookupFlags::MatchAllSymbols),
                    Name))
    return Sym->getAddress();
  else {
    llvm::dbgs() << "Didn't find callback.\n";
    // If anything goes wrong materializing Sym then report it to the session
    // and return the ErrorHandlerAddress;
    ES.reportError(Sym.takeError());
    return ErrorHandlerAddress;
  }
}

Expected<std::unique_ptr<JITCompileCallbackManager>>
createLocalCompileCallbackManager(const Triple &T, ExecutionSession &ES,
                                  ExecutorAddr ErrorHandlerAddress) {
  switch (T.getArch()) {
  default:
    return make_error<StringError>(
        std::string("No callback manager available for ") + T.str(),
        inconvertibleErrorCode());
  case Triple::aarch64:
  case Triple::aarch64_32: {
    typedef orc::LocalJITCompileCallbackManager<orc::OrcAArch64> CCMgrT;
    return CCMgrT::Create(ES, ErrorHandlerAddress);
    }

    case Triple::x86: {
      typedef orc::LocalJITCompileCallbackManager<orc::OrcI386> CCMgrT;
      return CCMgrT::Create(ES, ErrorHandlerAddress);
    }

    case Triple::loongarch64: {
      typedef orc::LocalJITCompileCallbackManager<orc::OrcLoongArch64> CCMgrT;
      return CCMgrT::Create(ES, ErrorHandlerAddress);
    }

    case Triple::mips: {
      typedef orc::LocalJITCompileCallbackManager<orc::OrcMips32Be> CCMgrT;
      return CCMgrT::Create(ES, ErrorHandlerAddress);
    }
    case Triple::mipsel: {
      typedef orc::LocalJITCompileCallbackManager<orc::OrcMips32Le> CCMgrT;
      return CCMgrT::Create(ES, ErrorHandlerAddress);
    }

    case Triple::mips64:
    case Triple::mips64el: {
      typedef orc::LocalJITCompileCallbackManager<orc::OrcMips64> CCMgrT;
      return CCMgrT::Create(ES, ErrorHandlerAddress);
    }

    case Triple::riscv64: {
      typedef orc::LocalJITCompileCallbackManager<orc::OrcRiscv64> CCMgrT;
      return CCMgrT::Create(ES, ErrorHandlerAddress);
    }

    case Triple::x86_64: {
      if (T.getOS() == Triple::OSType::Win32) {
        typedef orc::LocalJITCompileCallbackManager<orc::OrcX86_64_Win32> CCMgrT;
        return CCMgrT::Create(ES, ErrorHandlerAddress);
      } else {
        typedef orc::LocalJITCompileCallbackManager<orc::OrcX86_64_SysV> CCMgrT;
        return CCMgrT::Create(ES, ErrorHandlerAddress);
      }
    }

  }
}

std::function<std::unique_ptr<IndirectStubsManager>()>
createLocalIndirectStubsManagerBuilder(const Triple &T) {
  switch (T.getArch()) {
    default:
      return [](){
        return std::make_unique<
                       orc::LocalIndirectStubsManager<orc::OrcGenericABI>>();
      };

    case Triple::aarch64:
    case Triple::aarch64_32:
      return [](){
        return std::make_unique<
                       orc::LocalIndirectStubsManager<orc::OrcAArch64>>();
      };

    case Triple::x86:
      return [](){
        return std::make_unique<
                       orc::LocalIndirectStubsManager<orc::OrcI386>>();
      };

    case Triple::loongarch64:
      return []() {
        return std::make_unique<
            orc::LocalIndirectStubsManager<orc::OrcLoongArch64>>();
      };

    case Triple::mips:
      return [](){
          return std::make_unique<
                      orc::LocalIndirectStubsManager<orc::OrcMips32Be>>();
      };

    case Triple::mipsel:
      return [](){
          return std::make_unique<
                      orc::LocalIndirectStubsManager<orc::OrcMips32Le>>();
      };

    case Triple::mips64:
    case Triple::mips64el:
      return [](){
          return std::make_unique<
                      orc::LocalIndirectStubsManager<orc::OrcMips64>>();
      };

    case Triple::riscv64:
      return []() {
        return std::make_unique<
            orc::LocalIndirectStubsManager<orc::OrcRiscv64>>();
      };

    case Triple::x86_64:
      if (T.getOS() == Triple::OSType::Win32) {
        return [](){
          return std::make_unique<
                     orc::LocalIndirectStubsManager<orc::OrcX86_64_Win32>>();
        };
      } else {
        return [](){
          return std::make_unique<
                     orc::LocalIndirectStubsManager<orc::OrcX86_64_SysV>>();
        };
      }

  }
}

Constant* createIRTypedAddress(FunctionType &FT, ExecutorAddr Addr) {
  Constant *AddrIntVal =
    ConstantInt::get(Type::getInt64Ty(FT.getContext()), Addr.getValue());
  Constant *AddrPtrVal =
    ConstantExpr::getIntToPtr(AddrIntVal, PointerType::get(&FT, 0));
  return AddrPtrVal;
}

GlobalVariable* createImplPointer(PointerType &PT, Module &M,
                                  const Twine &Name, Constant *Initializer) {
  auto IP = new GlobalVariable(M, &PT, false, GlobalValue::ExternalLinkage,
                               Initializer, Name, nullptr,
                               GlobalValue::NotThreadLocal, 0, true);
  IP->setVisibility(GlobalValue::HiddenVisibility);
  return IP;
}

void makeStub(Function &F, Value &ImplPointer) {
  assert(F.isDeclaration() && "Can't turn a definition into a stub.");
  assert(F.getParent() && "Function isn't in a module.");
  Module &M = *F.getParent();
  BasicBlock *EntryBlock = BasicBlock::Create(M.getContext(), "entry", &F);
  IRBuilder<> Builder(EntryBlock);
  LoadInst *ImplAddr = Builder.CreateLoad(F.getType(), &ImplPointer);
  std::vector<Value*> CallArgs;
  for (auto &A : F.args())
    CallArgs.push_back(&A);
  CallInst *Call = Builder.CreateCall(F.getFunctionType(), ImplAddr, CallArgs);
  Call->setTailCall();
  Call->setAttributes(F.getAttributes());
  if (F.getReturnType()->isVoidTy())
    Builder.CreateRetVoid();
  else
    Builder.CreateRet(Call);
}

std::vector<GlobalValue *> SymbolLinkagePromoter::operator()(Module &M) {
  std::vector<GlobalValue *> PromotedGlobals;

  for (auto &GV : M.global_values()) {
    bool Promoted = true;

    // Rename if necessary.
    if (!GV.hasName())
      GV.setName("__orc_anon." + Twine(NextId++));
    else if (GV.getName().starts_with("\01L"))
      GV.setName("__" + GV.getName().substr(1) + "." + Twine(NextId++));
    else if (GV.hasLocalLinkage())
      GV.setName("__orc_lcl." + GV.getName() + "." + Twine(NextId++));
    else
      Promoted = false;

    if (GV.hasLocalLinkage()) {
      GV.setLinkage(GlobalValue::ExternalLinkage);
      GV.setVisibility(GlobalValue::HiddenVisibility);
      Promoted = true;
    }
    GV.setUnnamedAddr(GlobalValue::UnnamedAddr::None);

    if (Promoted)
      PromotedGlobals.push_back(&GV);
  }

  return PromotedGlobals;
}

Function* cloneFunctionDecl(Module &Dst, const Function &F,
                            ValueToValueMapTy *VMap) {
  Function *NewF =
    Function::Create(cast<FunctionType>(F.getValueType()),
                     F.getLinkage(), F.getName(), &Dst);
  NewF->copyAttributesFrom(&F);

  if (VMap) {
    (*VMap)[&F] = NewF;
    auto NewArgI = NewF->arg_begin();
    for (auto ArgI = F.arg_begin(), ArgE = F.arg_end(); ArgI != ArgE;
         ++ArgI, ++NewArgI)
      (*VMap)[&*ArgI] = &*NewArgI;
  }

  return NewF;
}

GlobalVariable* cloneGlobalVariableDecl(Module &Dst, const GlobalVariable &GV,
                                        ValueToValueMapTy *VMap) {
  GlobalVariable *NewGV = new GlobalVariable(
      Dst, GV.getValueType(), GV.isConstant(),
      GV.getLinkage(), nullptr, GV.getName(), nullptr,
      GV.getThreadLocalMode(), GV.getType()->getAddressSpace());
  NewGV->copyAttributesFrom(&GV);
  if (VMap)
    (*VMap)[&GV] = NewGV;
  return NewGV;
}

GlobalAlias* cloneGlobalAliasDecl(Module &Dst, const GlobalAlias &OrigA,
                                  ValueToValueMapTy &VMap) {
  assert(OrigA.getAliasee() && "Original alias doesn't have an aliasee?");
  auto *NewA = GlobalAlias::create(OrigA.getValueType(),
                                   OrigA.getType()->getPointerAddressSpace(),
                                   OrigA.getLinkage(), OrigA.getName(), &Dst);
  NewA->copyAttributesFrom(&OrigA);
  VMap[&OrigA] = NewA;
  return NewA;
}

Error addFunctionPointerRelocationsToCurrentSymbol(jitlink::Symbol &Sym,
                                                   jitlink::LinkGraph &G,
                                                   MCDisassembler &Disassembler,
                                                   MCInstrAnalysis &MIA) {
  // AArch64 appears to already come with the necessary relocations. Among other
  // architectures, only x86_64 is currently implemented here.
  if (G.getTargetTriple().getArch() != Triple::x86_64)
    return Error::success();

  raw_null_ostream CommentStream;
  auto &STI = Disassembler.getSubtargetInfo();

  // Determine the function bounds
  auto &B = Sym.getBlock();
  assert(!B.isZeroFill() && "expected content block");
  auto SymAddress = Sym.getAddress();
  auto SymStartInBlock =
      (const uint8_t *)B.getContent().data() + Sym.getOffset();
  auto SymSize = Sym.getSize() ? Sym.getSize() : B.getSize() - Sym.getOffset();
  auto Content = ArrayRef(SymStartInBlock, SymSize);

  LLVM_DEBUG(dbgs() << "Adding self-relocations to " << Sym.getName() << "\n");

  SmallDenseSet<uintptr_t, 8> ExistingRelocations;
  for (auto &E : B.edges()) {
    if (E.isRelocation())
      ExistingRelocations.insert(E.getOffset());
  }

  size_t I = 0;
  while (I < Content.size()) {
    MCInst Instr;
    uint64_t InstrSize = 0;
    uint64_t InstrStart = SymAddress.getValue() + I;
    auto DecodeStatus = Disassembler.getInstruction(
        Instr, InstrSize, Content.drop_front(I), InstrStart, CommentStream);
    if (DecodeStatus != MCDisassembler::Success) {
      LLVM_DEBUG(dbgs() << "Aborting due to disassembly failure at address "
                        << InstrStart);
      return make_error<StringError>(
          formatv("failed to disassemble at address {0:x16}", InstrStart),
          inconvertibleErrorCode());
    }
    // Advance to the next instruction.
    I += InstrSize;

    // Check for a PC-relative address equal to the symbol itself.
    auto PCRelAddr =
        MIA.evaluateMemoryOperandAddress(Instr, &STI, InstrStart, InstrSize);
    if (!PCRelAddr || *PCRelAddr != SymAddress.getValue())
      continue;

    auto RelocOffInInstr =
        MIA.getMemoryOperandRelocationOffset(Instr, InstrSize);
    if (!RelocOffInInstr || InstrSize - *RelocOffInInstr != 4) {
      LLVM_DEBUG(dbgs() << "Skipping unknown self-relocation at "
                        << InstrStart);
      continue;
    }

    auto RelocOffInBlock = orc::ExecutorAddr(InstrStart) + *RelocOffInInstr -
                           SymAddress + Sym.getOffset();
    if (ExistingRelocations.contains(RelocOffInBlock))
      continue;

    LLVM_DEBUG(dbgs() << "Adding delta32 self-relocation at " << InstrStart);
    B.addEdge(jitlink::x86_64::Delta32, RelocOffInBlock, Sym, /*Addend=*/-4);
  }
  return Error::success();
}

} // End namespace orc.
} // End namespace llvm.
