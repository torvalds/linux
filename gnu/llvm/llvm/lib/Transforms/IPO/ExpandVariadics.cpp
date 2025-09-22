//===-- ExpandVariadicsPass.cpp --------------------------------*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is an optimization pass for variadic functions. If called from codegen,
// it can serve as the implementation of variadic functions for a given target.
//
// The strategy is to turn the ... part of a variadic function into a va_list
// and fix up the call sites. The majority of the pass is target independent.
// The exceptions are the va_list type itself and the rules for where to store
// variables in memory such that va_arg can iterate over them given a va_list.
//
// The majority of the plumbing is splitting the variadic function into a
// single basic block that packs the variadic arguments into a va_list and
// a second function that does the work of the original. That packing is
// exactly what is done by va_start. Further, the transform from ... to va_list
// replaced va_start with an operation to copy a va_list from the new argument,
// which is exactly a va_copy. This is useful for reducing target-dependence.
//
// A va_list instance is a forward iterator, where the primary operation va_arg
// is dereference-then-increment. This interface forces significant convergent
// evolution between target specific implementations. The variation in runtime
// data layout is limited to that representable by the iterator, parameterised
// by the type passed to the va_arg instruction.
//
// Therefore the majority of the target specific subtlety is packing arguments
// into a stack allocated buffer such that a va_list can be initialised with it
// and the va_arg expansion for the target will find the arguments at runtime.
//
// The aggregate effect is to unblock other transforms, most critically the
// general purpose inliner. Known calls to variadic functions become zero cost.
//
// Consistency with clang is primarily tested by emitting va_arg using clang
// then expanding the variadic functions using this pass, followed by trying
// to constant fold the functions to no-ops.
//
// Target specific behaviour is tested in IR - mainly checking that values are
// put into positions in call frames that make sense for that particular target.
//
// There is one "clever" invariant in use. va_start intrinsics that are not
// within a varidic functions are an error in the IR verifier. When this
// transform moves blocks from a variadic function into a fixed arity one, it
// moves va_start intrinsics along with everything else. That means that the
// va_start intrinsics that need to be rewritten to use the trailing argument
// are exactly those that are in non-variadic functions so no further state
// is needed to distinguish those that need to be rewritten.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/ExpandVariadics.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

#define DEBUG_TYPE "expand-variadics"

using namespace llvm;

namespace {

cl::opt<ExpandVariadicsMode> ExpandVariadicsModeOption(
    DEBUG_TYPE "-override", cl::desc("Override the behaviour of " DEBUG_TYPE),
    cl::init(ExpandVariadicsMode::Unspecified),
    cl::values(clEnumValN(ExpandVariadicsMode::Unspecified, "unspecified",
                          "Use the implementation defaults"),
               clEnumValN(ExpandVariadicsMode::Disable, "disable",
                          "Disable the pass entirely"),
               clEnumValN(ExpandVariadicsMode::Optimize, "optimize",
                          "Optimise without changing ABI"),
               clEnumValN(ExpandVariadicsMode::Lowering, "lowering",
                          "Change variadic calling convention")));

bool commandLineOverride() {
  return ExpandVariadicsModeOption != ExpandVariadicsMode::Unspecified;
}

// Instances of this class encapsulate the target-dependant behaviour as a
// function of triple. Implementing a new ABI is adding a case to the switch
// in create(llvm::Triple) at the end of this file.
// This class may end up instantiated in TargetMachine instances, keeping it
// here for now until enough targets are implemented for the API to evolve.
class VariadicABIInfo {
protected:
  VariadicABIInfo() = default;

public:
  static std::unique_ptr<VariadicABIInfo> create(const Triple &T);

  // Allow overriding whether the pass runs on a per-target basis
  virtual bool enableForTarget() = 0;

  // Whether a valist instance is passed by value or by address
  // I.e. does it need to be alloca'ed and stored into, or can
  // it be passed directly in a SSA register
  virtual bool vaListPassedInSSARegister() = 0;

  // The type of a va_list iterator object
  virtual Type *vaListType(LLVMContext &Ctx) = 0;

  // The type of a va_list as a function argument as lowered by C
  virtual Type *vaListParameterType(Module &M) = 0;

  // Initialize an allocated va_list object to point to an already
  // initialized contiguous memory region.
  // Return the value to pass as the va_list argument
  virtual Value *initializeVaList(Module &M, LLVMContext &Ctx,
                                  IRBuilder<> &Builder, AllocaInst *VaList,
                                  Value *Buffer) = 0;

  struct VAArgSlotInfo {
    Align DataAlign; // With respect to the call frame
    bool Indirect;   // Passed via a pointer
  };
  virtual VAArgSlotInfo slotInfo(const DataLayout &DL, Type *Parameter) = 0;

  // Targets implemented so far all have the same trivial lowering for these
  bool vaEndIsNop() { return true; }
  bool vaCopyIsMemcpy() { return true; }

  virtual ~VariadicABIInfo() = default;
};

// Module implements getFunction() which returns nullptr on missing declaration
// and getOrInsertFunction which creates one when absent. Intrinsics.h only
// implements getDeclaration which creates one when missing. Checking whether
// an intrinsic exists thus inserts it in the module and it then needs to be
// deleted again to clean up.
// The right name for the two functions on intrinsics would match Module::,
// but doing that in a single change would introduce nullptr dereferences
// where currently there are none. The minimal collateral damage approach
// would split the change over a release to help downstream branches. As it
// is unclear what approach will be preferred, implementing the trivial
// function here in the meantime to decouple from that discussion.
Function *getPreexistingDeclaration(Module *M, Intrinsic::ID Id,
                                    ArrayRef<Type *> Tys = {}) {
  auto *FT = Intrinsic::getType(M->getContext(), Id, Tys);
  return M->getFunction(Tys.empty() ? Intrinsic::getName(Id)
                                    : Intrinsic::getName(Id, Tys, M, FT));
}

class ExpandVariadics : public ModulePass {

  // The pass construction sets the default to optimize when called from middle
  // end and lowering when called from the backend. The command line variable
  // overrides that. This is useful for testing and debugging. It also allows
  // building an applications with variadic functions wholly removed if one
  // has sufficient control over the dependencies, e.g. a statically linked
  // clang that has no variadic function calls remaining in the binary.

public:
  static char ID;
  const ExpandVariadicsMode Mode;
  std::unique_ptr<VariadicABIInfo> ABI;

  ExpandVariadics(ExpandVariadicsMode Mode)
      : ModulePass(ID),
        Mode(commandLineOverride() ? ExpandVariadicsModeOption : Mode) {}

  StringRef getPassName() const override { return "Expand variadic functions"; }

  bool rewriteABI() { return Mode == ExpandVariadicsMode::Lowering; }

  bool runOnModule(Module &M) override;

  bool runOnFunction(Module &M, IRBuilder<> &Builder, Function *F);

  Function *replaceAllUsesWithNewDeclaration(Module &M,
                                             Function *OriginalFunction);

  Function *deriveFixedArityReplacement(Module &M, IRBuilder<> &Builder,
                                        Function *OriginalFunction);

  Function *defineVariadicWrapper(Module &M, IRBuilder<> &Builder,
                                  Function *VariadicWrapper,
                                  Function *FixedArityReplacement);

  bool expandCall(Module &M, IRBuilder<> &Builder, CallBase *CB, FunctionType *,
                  Function *NF);

  // The intrinsic functions va_copy and va_end are removed unconditionally.
  // They correspond to a memcpy and a no-op on all implemented targets.
  // The va_start intrinsic is removed from basic blocks that were not created
  // by this pass, some may remain if needed to maintain the external ABI.

  template <Intrinsic::ID ID, typename InstructionType>
  bool expandIntrinsicUsers(Module &M, IRBuilder<> &Builder,
                            PointerType *IntrinsicArgType) {
    bool Changed = false;
    const DataLayout &DL = M.getDataLayout();
    if (Function *Intrinsic =
            getPreexistingDeclaration(&M, ID, {IntrinsicArgType})) {
      for (User *U : make_early_inc_range(Intrinsic->users()))
        if (auto *I = dyn_cast<InstructionType>(U))
          Changed |= expandVAIntrinsicCall(Builder, DL, I);

      if (Intrinsic->use_empty())
        Intrinsic->eraseFromParent();
    }
    return Changed;
  }

  bool expandVAIntrinsicUsersWithAddrspace(Module &M, IRBuilder<> &Builder,
                                           unsigned Addrspace) {
    auto &Ctx = M.getContext();
    PointerType *IntrinsicArgType = PointerType::get(Ctx, Addrspace);
    bool Changed = false;

    // expand vastart before vacopy as vastart may introduce a vacopy
    Changed |= expandIntrinsicUsers<Intrinsic::vastart, VAStartInst>(
        M, Builder, IntrinsicArgType);
    Changed |= expandIntrinsicUsers<Intrinsic::vaend, VAEndInst>(
        M, Builder, IntrinsicArgType);
    Changed |= expandIntrinsicUsers<Intrinsic::vacopy, VACopyInst>(
        M, Builder, IntrinsicArgType);
    return Changed;
  }

  bool expandVAIntrinsicCall(IRBuilder<> &Builder, const DataLayout &DL,
                             VAStartInst *Inst);

  bool expandVAIntrinsicCall(IRBuilder<> &, const DataLayout &,
                             VAEndInst *Inst);

  bool expandVAIntrinsicCall(IRBuilder<> &Builder, const DataLayout &DL,
                             VACopyInst *Inst);

  FunctionType *inlinableVariadicFunctionType(Module &M, FunctionType *FTy) {
    // The type of "FTy" with the ... removed and a va_list appended
    SmallVector<Type *> ArgTypes(FTy->param_begin(), FTy->param_end());
    ArgTypes.push_back(ABI->vaListParameterType(M));
    return FunctionType::get(FTy->getReturnType(), ArgTypes,
                             /*IsVarArgs=*/false);
  }

  static ConstantInt *sizeOfAlloca(LLVMContext &Ctx, const DataLayout &DL,
                                   AllocaInst *Alloced) {
    std::optional<TypeSize> AllocaTypeSize = Alloced->getAllocationSize(DL);
    uint64_t AsInt = AllocaTypeSize ? AllocaTypeSize->getFixedValue() : 0;
    return ConstantInt::get(Type::getInt64Ty(Ctx), AsInt);
  }

  bool expansionApplicableToFunction(Module &M, Function *F) {
    if (F->isIntrinsic() || !F->isVarArg() ||
        F->hasFnAttribute(Attribute::Naked))
      return false;

    if (F->getCallingConv() != CallingConv::C)
      return false;

    if (rewriteABI())
      return true;

    if (!F->hasExactDefinition())
      return false;

    return true;
  }

  bool expansionApplicableToFunctionCall(CallBase *CB) {
    if (CallInst *CI = dyn_cast<CallInst>(CB)) {
      if (CI->isMustTailCall()) {
        // Cannot expand musttail calls
        return false;
      }

      if (CI->getCallingConv() != CallingConv::C)
        return false;

      return true;
    }

    if (isa<InvokeInst>(CB)) {
      // Invoke not implemented in initial implementation of pass
      return false;
    }

    // Other unimplemented derivative of CallBase
    return false;
  }

  class ExpandedCallFrame {
    // Helper for constructing an alloca instance containing the arguments bound
    // to the variadic ... parameter, rearranged to allow indexing through a
    // va_list iterator
    enum { N = 4 };
    SmallVector<Type *, N> FieldTypes;
    enum Tag { Store, Memcpy, Padding };
    SmallVector<std::tuple<Value *, uint64_t, Tag>, N> Source;

    template <Tag tag> void append(Type *FieldType, Value *V, uint64_t Bytes) {
      FieldTypes.push_back(FieldType);
      Source.push_back({V, Bytes, tag});
    }

  public:
    void store(LLVMContext &Ctx, Type *T, Value *V) { append<Store>(T, V, 0); }

    void memcpy(LLVMContext &Ctx, Type *T, Value *V, uint64_t Bytes) {
      append<Memcpy>(T, V, Bytes);
    }

    void padding(LLVMContext &Ctx, uint64_t By) {
      append<Padding>(ArrayType::get(Type::getInt8Ty(Ctx), By), nullptr, 0);
    }

    size_t size() const { return FieldTypes.size(); }
    bool empty() const { return FieldTypes.empty(); }

    StructType *asStruct(LLVMContext &Ctx, StringRef Name) {
      const bool IsPacked = true;
      return StructType::create(Ctx, FieldTypes,
                                (Twine(Name) + ".vararg").str(), IsPacked);
    }

    void initializeStructAlloca(const DataLayout &DL, IRBuilder<> &Builder,
                                AllocaInst *Alloced) {

      StructType *VarargsTy = cast<StructType>(Alloced->getAllocatedType());

      for (size_t I = 0; I < size(); I++) {

        auto [V, bytes, tag] = Source[I];

        if (tag == Padding) {
          assert(V == nullptr);
          continue;
        }

        auto Dst = Builder.CreateStructGEP(VarargsTy, Alloced, I);

        assert(V != nullptr);

        if (tag == Store)
          Builder.CreateStore(V, Dst);

        if (tag == Memcpy)
          Builder.CreateMemCpy(Dst, {}, V, {}, bytes);
      }
    }
  };
};

bool ExpandVariadics::runOnModule(Module &M) {
  bool Changed = false;
  if (Mode == ExpandVariadicsMode::Disable)
    return Changed;

  Triple TT(M.getTargetTriple());
  ABI = VariadicABIInfo::create(TT);
  if (!ABI)
    return Changed;

  if (!ABI->enableForTarget())
    return Changed;

  auto &Ctx = M.getContext();
  const DataLayout &DL = M.getDataLayout();
  IRBuilder<> Builder(Ctx);

  // Lowering needs to run on all functions exactly once.
  // Optimize could run on functions containing va_start exactly once.
  for (Function &F : make_early_inc_range(M))
    Changed |= runOnFunction(M, Builder, &F);

  // After runOnFunction, all known calls to known variadic functions have been
  // replaced. va_start intrinsics are presently (and invalidly!) only present
  // in functions that used to be variadic and have now been replaced to take a
  // va_list instead. If lowering as opposed to optimising, calls to unknown
  // variadic functions have also been replaced.

  {
    // 0 and AllocaAddrSpace are sufficient for the targets implemented so far
    unsigned Addrspace = 0;
    Changed |= expandVAIntrinsicUsersWithAddrspace(M, Builder, Addrspace);

    Addrspace = DL.getAllocaAddrSpace();
    if (Addrspace != 0)
      Changed |= expandVAIntrinsicUsersWithAddrspace(M, Builder, Addrspace);
  }

  if (Mode != ExpandVariadicsMode::Lowering)
    return Changed;

  for (Function &F : make_early_inc_range(M)) {
    if (F.isDeclaration())
      continue;

    // Now need to track down indirect calls. Can't find those
    // by walking uses of variadic functions, need to crawl the instruction
    // stream. Fortunately this is only necessary for the ABI rewrite case.
    for (BasicBlock &BB : F) {
      for (Instruction &I : make_early_inc_range(BB)) {
        if (CallBase *CB = dyn_cast<CallBase>(&I)) {
          if (CB->isIndirectCall()) {
            FunctionType *FTy = CB->getFunctionType();
            if (FTy->isVarArg())
              Changed |= expandCall(M, Builder, CB, FTy, 0);
          }
        }
      }
    }
  }

  return Changed;
}

bool ExpandVariadics::runOnFunction(Module &M, IRBuilder<> &Builder,
                                    Function *OriginalFunction) {
  bool Changed = false;

  if (!expansionApplicableToFunction(M, OriginalFunction))
    return Changed;

  [[maybe_unused]] const bool OriginalFunctionIsDeclaration =
      OriginalFunction->isDeclaration();
  assert(rewriteABI() || !OriginalFunctionIsDeclaration);

  // Declare a new function and redirect every use to that new function
  Function *VariadicWrapper =
      replaceAllUsesWithNewDeclaration(M, OriginalFunction);
  assert(VariadicWrapper->isDeclaration());
  assert(OriginalFunction->use_empty());

  // Create a new function taking va_list containing the implementation of the
  // original
  Function *FixedArityReplacement =
      deriveFixedArityReplacement(M, Builder, OriginalFunction);
  assert(OriginalFunction->isDeclaration());
  assert(FixedArityReplacement->isDeclaration() ==
         OriginalFunctionIsDeclaration);
  assert(VariadicWrapper->isDeclaration());

  // Create a single block forwarding wrapper that turns a ... into a va_list
  [[maybe_unused]] Function *VariadicWrapperDefine =
      defineVariadicWrapper(M, Builder, VariadicWrapper, FixedArityReplacement);
  assert(VariadicWrapperDefine == VariadicWrapper);
  assert(!VariadicWrapper->isDeclaration());

  // We now have:
  // 1. the original function, now as a declaration with no uses
  // 2. a variadic function that unconditionally calls a fixed arity replacement
  // 3. a fixed arity function equivalent to the original function

  // Replace known calls to the variadic with calls to the va_list equivalent
  for (User *U : make_early_inc_range(VariadicWrapper->users())) {
    if (CallBase *CB = dyn_cast<CallBase>(U)) {
      Value *CalledOperand = CB->getCalledOperand();
      if (VariadicWrapper == CalledOperand)
        Changed |=
            expandCall(M, Builder, CB, VariadicWrapper->getFunctionType(),
                       FixedArityReplacement);
    }
  }

  // The original function will be erased.
  // One of the two new functions will become a replacement for the original.
  // When preserving the ABI, the other is an internal implementation detail.
  // When rewriting the ABI, RAUW then the variadic one.
  Function *const ExternallyAccessible =
      rewriteABI() ? FixedArityReplacement : VariadicWrapper;
  Function *const InternalOnly =
      rewriteABI() ? VariadicWrapper : FixedArityReplacement;

  // The external function is the replacement for the original
  ExternallyAccessible->setLinkage(OriginalFunction->getLinkage());
  ExternallyAccessible->setVisibility(OriginalFunction->getVisibility());
  ExternallyAccessible->setComdat(OriginalFunction->getComdat());
  ExternallyAccessible->takeName(OriginalFunction);

  // Annotate the internal one as internal
  InternalOnly->setVisibility(GlobalValue::DefaultVisibility);
  InternalOnly->setLinkage(GlobalValue::InternalLinkage);

  // The original is unused and obsolete
  OriginalFunction->eraseFromParent();

  InternalOnly->removeDeadConstantUsers();

  if (rewriteABI()) {
    // All known calls to the function have been removed by expandCall
    // Resolve everything else by replaceAllUsesWith
    VariadicWrapper->replaceAllUsesWith(FixedArityReplacement);
    VariadicWrapper->eraseFromParent();
  }

  return Changed;
}

Function *
ExpandVariadics::replaceAllUsesWithNewDeclaration(Module &M,
                                                  Function *OriginalFunction) {
  auto &Ctx = M.getContext();
  Function &F = *OriginalFunction;
  FunctionType *FTy = F.getFunctionType();
  Function *NF = Function::Create(FTy, F.getLinkage(), F.getAddressSpace());

  NF->setName(F.getName() + ".varargs");
  NF->IsNewDbgInfoFormat = F.IsNewDbgInfoFormat;

  F.getParent()->getFunctionList().insert(F.getIterator(), NF);

  AttrBuilder ParamAttrs(Ctx);
  AttributeList Attrs = NF->getAttributes();
  Attrs = Attrs.addParamAttributes(Ctx, FTy->getNumParams(), ParamAttrs);
  NF->setAttributes(Attrs);

  OriginalFunction->replaceAllUsesWith(NF);
  return NF;
}

Function *
ExpandVariadics::deriveFixedArityReplacement(Module &M, IRBuilder<> &Builder,
                                             Function *OriginalFunction) {
  Function &F = *OriginalFunction;
  // The purpose here is split the variadic function F into two functions
  // One is a variadic function that bundles the passed argument into a va_list
  // and passes it to the second function. The second function does whatever
  // the original F does, except that it takes a va_list instead of the ...

  assert(expansionApplicableToFunction(M, &F));

  auto &Ctx = M.getContext();

  // Returned value isDeclaration() is equal to F.isDeclaration()
  // but that property is not invariant throughout this function
  const bool FunctionIsDefinition = !F.isDeclaration();

  FunctionType *FTy = F.getFunctionType();
  SmallVector<Type *> ArgTypes(FTy->param_begin(), FTy->param_end());
  ArgTypes.push_back(ABI->vaListParameterType(M));

  FunctionType *NFTy = inlinableVariadicFunctionType(M, FTy);
  Function *NF = Function::Create(NFTy, F.getLinkage(), F.getAddressSpace());

  // Note - same attribute handling as DeadArgumentElimination
  NF->copyAttributesFrom(&F);
  NF->setComdat(F.getComdat());
  F.getParent()->getFunctionList().insert(F.getIterator(), NF);
  NF->setName(F.getName() + ".valist");
  NF->IsNewDbgInfoFormat = F.IsNewDbgInfoFormat;

  AttrBuilder ParamAttrs(Ctx);

  AttributeList Attrs = NF->getAttributes();
  Attrs = Attrs.addParamAttributes(Ctx, NFTy->getNumParams() - 1, ParamAttrs);
  NF->setAttributes(Attrs);

  // Splice the implementation into the new function with minimal changes
  if (FunctionIsDefinition) {
    NF->splice(NF->begin(), &F);

    auto NewArg = NF->arg_begin();
    for (Argument &Arg : F.args()) {
      Arg.replaceAllUsesWith(NewArg);
      NewArg->setName(Arg.getName()); // takeName without killing the old one
      ++NewArg;
    }
    NewArg->setName("varargs");
  }

  SmallVector<std::pair<unsigned, MDNode *>, 1> MDs;
  F.getAllMetadata(MDs);
  for (auto [KindID, Node] : MDs)
    NF->addMetadata(KindID, *Node);
  F.clearMetadata();

  return NF;
}

Function *
ExpandVariadics::defineVariadicWrapper(Module &M, IRBuilder<> &Builder,
                                       Function *VariadicWrapper,
                                       Function *FixedArityReplacement) {
  auto &Ctx = Builder.getContext();
  const DataLayout &DL = M.getDataLayout();
  assert(VariadicWrapper->isDeclaration());
  Function &F = *VariadicWrapper;

  assert(F.isDeclaration());
  Type *VaListTy = ABI->vaListType(Ctx);

  auto *BB = BasicBlock::Create(Ctx, "entry", &F);
  Builder.SetInsertPoint(BB);

  AllocaInst *VaListInstance =
      Builder.CreateAlloca(VaListTy, nullptr, "va_start");

  Builder.CreateLifetimeStart(VaListInstance,
                              sizeOfAlloca(Ctx, DL, VaListInstance));

  Builder.CreateIntrinsic(Intrinsic::vastart, {DL.getAllocaPtrType(Ctx)},
                          {VaListInstance});

  SmallVector<Value *> Args;
  for (Argument &A : F.args())
    Args.push_back(&A);

  Type *ParameterType = ABI->vaListParameterType(M);
  if (ABI->vaListPassedInSSARegister())
    Args.push_back(Builder.CreateLoad(ParameterType, VaListInstance));
  else
    Args.push_back(Builder.CreateAddrSpaceCast(VaListInstance, ParameterType));

  CallInst *Result = Builder.CreateCall(FixedArityReplacement, Args);

  Builder.CreateIntrinsic(Intrinsic::vaend, {DL.getAllocaPtrType(Ctx)},
                          {VaListInstance});
  Builder.CreateLifetimeEnd(VaListInstance,
                            sizeOfAlloca(Ctx, DL, VaListInstance));

  if (Result->getType()->isVoidTy())
    Builder.CreateRetVoid();
  else
    Builder.CreateRet(Result);

  return VariadicWrapper;
}

bool ExpandVariadics::expandCall(Module &M, IRBuilder<> &Builder, CallBase *CB,
                                 FunctionType *VarargFunctionType,
                                 Function *NF) {
  bool Changed = false;
  const DataLayout &DL = M.getDataLayout();

  if (!expansionApplicableToFunctionCall(CB)) {
    if (rewriteABI())
      report_fatal_error("Cannot lower callbase instruction");
    return Changed;
  }

  // This is tricky. The call instruction's function type might not match
  // the type of the caller. When optimising, can leave it unchanged.
  // Webassembly detects that inconsistency and repairs it.
  FunctionType *FuncType = CB->getFunctionType();
  if (FuncType != VarargFunctionType) {
    if (!rewriteABI())
      return Changed;
    FuncType = VarargFunctionType;
  }

  auto &Ctx = CB->getContext();

  Align MaxFieldAlign(1);

  // The strategy is to allocate a call frame containing the variadic
  // arguments laid out such that a target specific va_list can be initialized
  // with it, such that target specific va_arg instructions will correctly
  // iterate over it. This means getting the alignment right and sometimes
  // embedding a pointer to the value instead of embedding the value itself.

  Function *CBF = CB->getParent()->getParent();

  ExpandedCallFrame Frame;

  uint64_t CurrentOffset = 0;

  for (unsigned I = FuncType->getNumParams(), E = CB->arg_size(); I < E; ++I) {
    Value *ArgVal = CB->getArgOperand(I);
    const bool IsByVal = CB->paramHasAttr(I, Attribute::ByVal);
    const bool IsByRef = CB->paramHasAttr(I, Attribute::ByRef);

    // The type of the value being passed, decoded from byval/byref metadata if
    // required
    Type *const UnderlyingType = IsByVal   ? CB->getParamByValType(I)
                                 : IsByRef ? CB->getParamByRefType(I)
                                           : ArgVal->getType();
    const uint64_t UnderlyingSize =
        DL.getTypeAllocSize(UnderlyingType).getFixedValue();

    // The type to be written into the call frame
    Type *FrameFieldType = UnderlyingType;

    // The value to copy from when initialising the frame alloca
    Value *SourceValue = ArgVal;

    VariadicABIInfo::VAArgSlotInfo SlotInfo = ABI->slotInfo(DL, UnderlyingType);

    if (SlotInfo.Indirect) {
      // The va_arg lowering loads through a pointer. Set up an alloca to aim
      // that pointer at.
      Builder.SetInsertPointPastAllocas(CBF);
      Builder.SetCurrentDebugLocation(CB->getStableDebugLoc());
      Value *CallerCopy =
          Builder.CreateAlloca(UnderlyingType, nullptr, "IndirectAlloca");

      Builder.SetInsertPoint(CB);
      if (IsByVal)
        Builder.CreateMemCpy(CallerCopy, {}, ArgVal, {}, UnderlyingSize);
      else
        Builder.CreateStore(ArgVal, CallerCopy);

      // Indirection now handled, pass the alloca ptr by value
      FrameFieldType = DL.getAllocaPtrType(Ctx);
      SourceValue = CallerCopy;
    }

    // Alignment of the value within the frame
    // This probably needs to be controllable as a function of type
    Align DataAlign = SlotInfo.DataAlign;

    MaxFieldAlign = std::max(MaxFieldAlign, DataAlign);

    uint64_t DataAlignV = DataAlign.value();
    if (uint64_t Rem = CurrentOffset % DataAlignV) {
      // Inject explicit padding to deal with alignment requirements
      uint64_t Padding = DataAlignV - Rem;
      Frame.padding(Ctx, Padding);
      CurrentOffset += Padding;
    }

    if (SlotInfo.Indirect) {
      Frame.store(Ctx, FrameFieldType, SourceValue);
    } else {
      if (IsByVal)
        Frame.memcpy(Ctx, FrameFieldType, SourceValue, UnderlyingSize);
      else
        Frame.store(Ctx, FrameFieldType, SourceValue);
    }

    CurrentOffset += DL.getTypeAllocSize(FrameFieldType).getFixedValue();
  }

  if (Frame.empty()) {
    // Not passing any arguments, hopefully va_arg won't try to read any
    // Creating a single byte frame containing nothing to point the va_list
    // instance as that is less special-casey in the compiler and probably
    // easier to interpret in a debugger.
    Frame.padding(Ctx, 1);
  }

  StructType *VarargsTy = Frame.asStruct(Ctx, CBF->getName());

  // The struct instance needs to be at least MaxFieldAlign for the alignment of
  // the fields to be correct at runtime. Use the native stack alignment instead
  // if that's greater as that tends to give better codegen.
  // This is an awkward way to guess whether there is a known stack alignment
  // without hitting an assert in DL.getStackAlignment, 1024 is an arbitrary
  // number likely to be greater than the natural stack alignment.
  // TODO: DL.getStackAlignment could return a MaybeAlign instead of assert
  Align AllocaAlign = MaxFieldAlign;
  if (DL.exceedsNaturalStackAlignment(Align(1024)))
    AllocaAlign = std::max(AllocaAlign, DL.getStackAlignment());

  // Put the alloca to hold the variadic args in the entry basic block.
  Builder.SetInsertPointPastAllocas(CBF);

  // SetCurrentDebugLocation when the builder SetInsertPoint method does not
  Builder.SetCurrentDebugLocation(CB->getStableDebugLoc());

  // The awkward construction here is to set the alignment on the instance
  AllocaInst *Alloced = Builder.Insert(
      new AllocaInst(VarargsTy, DL.getAllocaAddrSpace(), nullptr, AllocaAlign),
      "vararg_buffer");
  Changed = true;
  assert(Alloced->getAllocatedType() == VarargsTy);

  // Initialize the fields in the struct
  Builder.SetInsertPoint(CB);
  Builder.CreateLifetimeStart(Alloced, sizeOfAlloca(Ctx, DL, Alloced));
  Frame.initializeStructAlloca(DL, Builder, Alloced);

  const unsigned NumArgs = FuncType->getNumParams();
  SmallVector<Value *> Args(CB->arg_begin(), CB->arg_begin() + NumArgs);

  // Initialize a va_list pointing to that struct and pass it as the last
  // argument
  AllocaInst *VaList = nullptr;
  {
    if (!ABI->vaListPassedInSSARegister()) {
      Type *VaListTy = ABI->vaListType(Ctx);
      Builder.SetInsertPointPastAllocas(CBF);
      Builder.SetCurrentDebugLocation(CB->getStableDebugLoc());
      VaList = Builder.CreateAlloca(VaListTy, nullptr, "va_argument");
      Builder.SetInsertPoint(CB);
      Builder.CreateLifetimeStart(VaList, sizeOfAlloca(Ctx, DL, VaList));
    }
    Builder.SetInsertPoint(CB);
    Args.push_back(ABI->initializeVaList(M, Ctx, Builder, VaList, Alloced));
  }

  // Attributes excluding any on the vararg arguments
  AttributeList PAL = CB->getAttributes();
  if (!PAL.isEmpty()) {
    SmallVector<AttributeSet, 8> ArgAttrs;
    for (unsigned ArgNo = 0; ArgNo < NumArgs; ArgNo++)
      ArgAttrs.push_back(PAL.getParamAttrs(ArgNo));
    PAL =
        AttributeList::get(Ctx, PAL.getFnAttrs(), PAL.getRetAttrs(), ArgAttrs);
  }

  SmallVector<OperandBundleDef, 1> OpBundles;
  CB->getOperandBundlesAsDefs(OpBundles);

  CallBase *NewCB = nullptr;

  if (CallInst *CI = dyn_cast<CallInst>(CB)) {
    Value *Dst = NF ? NF : CI->getCalledOperand();
    FunctionType *NFTy = inlinableVariadicFunctionType(M, VarargFunctionType);

    NewCB = CallInst::Create(NFTy, Dst, Args, OpBundles, "", CI);

    CallInst::TailCallKind TCK = CI->getTailCallKind();
    assert(TCK != CallInst::TCK_MustTail);

    // Can't tail call a function that is being passed a pointer to an alloca
    if (TCK == CallInst::TCK_Tail)
      TCK = CallInst::TCK_None;
    CI->setTailCallKind(TCK);

  } else {
    llvm_unreachable("Unreachable when !expansionApplicableToFunctionCall()");
  }

  if (VaList)
    Builder.CreateLifetimeEnd(VaList, sizeOfAlloca(Ctx, DL, VaList));

  Builder.CreateLifetimeEnd(Alloced, sizeOfAlloca(Ctx, DL, Alloced));

  NewCB->setAttributes(PAL);
  NewCB->takeName(CB);
  NewCB->setCallingConv(CB->getCallingConv());
  NewCB->setDebugLoc(DebugLoc());

  // DeadArgElim and ArgPromotion copy exactly this metadata
  NewCB->copyMetadata(*CB, {LLVMContext::MD_prof, LLVMContext::MD_dbg});

  CB->replaceAllUsesWith(NewCB);
  CB->eraseFromParent();
  return Changed;
}

bool ExpandVariadics::expandVAIntrinsicCall(IRBuilder<> &Builder,
                                            const DataLayout &DL,
                                            VAStartInst *Inst) {
  // Only removing va_start instructions that are not in variadic functions.
  // Those would be rejected by the IR verifier before this pass.
  // After splicing basic blocks from a variadic function into a fixed arity
  // one the va_start that used to refer to the ... parameter still exist.
  // There are also variadic functions that this pass did not change and
  // va_start instances in the created single block wrapper functions.
  // Replace exactly the instances in non-variadic functions as those are
  // the ones to be fixed up to use the va_list passed as the final argument.

  Function *ContainingFunction = Inst->getFunction();
  if (ContainingFunction->isVarArg()) {
    return false;
  }

  // The last argument is a vaListParameterType, either a va_list
  // or a pointer to one depending on the target.
  bool PassedByValue = ABI->vaListPassedInSSARegister();
  Argument *PassedVaList =
      ContainingFunction->getArg(ContainingFunction->arg_size() - 1);

  // va_start takes a pointer to a va_list, e.g. one on the stack
  Value *VaStartArg = Inst->getArgList();

  Builder.SetInsertPoint(Inst);

  if (PassedByValue) {
    // The general thing to do is create an alloca, store the va_list argument
    // to it, then create a va_copy. When vaCopyIsMemcpy(), this optimises to a
    // store to the VaStartArg.
    assert(ABI->vaCopyIsMemcpy());
    Builder.CreateStore(PassedVaList, VaStartArg);
  } else {

    // Otherwise emit a vacopy to pick up target-specific handling if any
    auto &Ctx = Builder.getContext();

    Builder.CreateIntrinsic(Intrinsic::vacopy, {DL.getAllocaPtrType(Ctx)},
                            {VaStartArg, PassedVaList});
  }

  Inst->eraseFromParent();
  return true;
}

bool ExpandVariadics::expandVAIntrinsicCall(IRBuilder<> &, const DataLayout &,
                                            VAEndInst *Inst) {
  assert(ABI->vaEndIsNop());
  Inst->eraseFromParent();
  return true;
}

bool ExpandVariadics::expandVAIntrinsicCall(IRBuilder<> &Builder,
                                            const DataLayout &DL,
                                            VACopyInst *Inst) {
  assert(ABI->vaCopyIsMemcpy());
  Builder.SetInsertPoint(Inst);

  auto &Ctx = Builder.getContext();
  Type *VaListTy = ABI->vaListType(Ctx);
  uint64_t Size = DL.getTypeAllocSize(VaListTy).getFixedValue();

  Builder.CreateMemCpy(Inst->getDest(), {}, Inst->getSrc(), {},
                       Builder.getInt32(Size));

  Inst->eraseFromParent();
  return true;
}

struct Amdgpu final : public VariadicABIInfo {

  bool enableForTarget() override { return true; }

  bool vaListPassedInSSARegister() override { return true; }

  Type *vaListType(LLVMContext &Ctx) override {
    return PointerType::getUnqual(Ctx);
  }

  Type *vaListParameterType(Module &M) override {
    return PointerType::getUnqual(M.getContext());
  }

  Value *initializeVaList(Module &M, LLVMContext &Ctx, IRBuilder<> &Builder,
                          AllocaInst * /*va_list*/, Value *Buffer) override {
    // Given Buffer, which is an AllocInst of vararg_buffer
    // need to return something usable as parameter type
    return Builder.CreateAddrSpaceCast(Buffer, vaListParameterType(M));
  }

  VAArgSlotInfo slotInfo(const DataLayout &DL, Type *Parameter) override {
    return {Align(4), false};
  }
};

struct NVPTX final : public VariadicABIInfo {

  bool enableForTarget() override { return true; }

  bool vaListPassedInSSARegister() override { return true; }

  Type *vaListType(LLVMContext &Ctx) override {
    return PointerType::getUnqual(Ctx);
  }

  Type *vaListParameterType(Module &M) override {
    return PointerType::getUnqual(M.getContext());
  }

  Value *initializeVaList(Module &M, LLVMContext &Ctx, IRBuilder<> &Builder,
                          AllocaInst *, Value *Buffer) override {
    return Builder.CreateAddrSpaceCast(Buffer, vaListParameterType(M));
  }

  VAArgSlotInfo slotInfo(const DataLayout &DL, Type *Parameter) override {
    // NVPTX expects natural alignment in all cases. The variadic call ABI will
    // handle promoting types to their appropriate size and alignment.
    Align A = DL.getABITypeAlign(Parameter);
    return {A, false};
  }
};

struct Wasm final : public VariadicABIInfo {

  bool enableForTarget() override {
    // Currently wasm is only used for testing.
    return commandLineOverride();
  }

  bool vaListPassedInSSARegister() override { return true; }

  Type *vaListType(LLVMContext &Ctx) override {
    return PointerType::getUnqual(Ctx);
  }

  Type *vaListParameterType(Module &M) override {
    return PointerType::getUnqual(M.getContext());
  }

  Value *initializeVaList(Module &M, LLVMContext &Ctx, IRBuilder<> &Builder,
                          AllocaInst * /*va_list*/, Value *Buffer) override {
    return Buffer;
  }

  VAArgSlotInfo slotInfo(const DataLayout &DL, Type *Parameter) override {
    LLVMContext &Ctx = Parameter->getContext();
    const unsigned MinAlign = 4;
    Align A = DL.getABITypeAlign(Parameter);
    if (A < MinAlign)
      A = Align(MinAlign);

    if (auto *S = dyn_cast<StructType>(Parameter)) {
      if (S->getNumElements() > 1) {
        return {DL.getABITypeAlign(PointerType::getUnqual(Ctx)), true};
      }
    }

    return {A, false};
  }
};

std::unique_ptr<VariadicABIInfo> VariadicABIInfo::create(const Triple &T) {
  switch (T.getArch()) {
  case Triple::r600:
  case Triple::amdgcn: {
    return std::make_unique<Amdgpu>();
  }

  case Triple::wasm32: {
    return std::make_unique<Wasm>();
  }

  case Triple::nvptx:
  case Triple::nvptx64: {
    return std::make_unique<NVPTX>();
  }

  default:
    return {};
  }
}

} // namespace

char ExpandVariadics::ID = 0;

INITIALIZE_PASS(ExpandVariadics, DEBUG_TYPE, "Expand variadic functions", false,
                false)

ModulePass *llvm::createExpandVariadicsPass(ExpandVariadicsMode M) {
  return new ExpandVariadics(M);
}

PreservedAnalyses ExpandVariadicsPass::run(Module &M, ModuleAnalysisManager &) {
  return ExpandVariadics(Mode).runOnModule(M) ? PreservedAnalyses::none()
                                              : PreservedAnalyses::all();
}

ExpandVariadicsPass::ExpandVariadicsPass(ExpandVariadicsMode M) : Mode(M) {}
