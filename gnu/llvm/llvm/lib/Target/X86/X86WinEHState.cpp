//===-- X86WinEHState - Insert EH state updates for win32 exceptions ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// All functions using an MSVC EH personality use an explicitly updated state
// number stored in an exception registration stack object. The registration
// object is linked into a thread-local chain of registrations stored at fs:00.
// This pass adds the registration object and EH state updates.
//
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/WinEHFuncInfo.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/EHPersonalities.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsX86.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include <deque>

using namespace llvm;

#define DEBUG_TYPE "winehstate"

namespace {
const int OverdefinedState = INT_MIN;

class WinEHStatePass : public FunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid.

  WinEHStatePass() : FunctionPass(ID) { }

  bool runOnFunction(Function &Fn) override;

  bool doInitialization(Module &M) override;

  bool doFinalization(Module &M) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  StringRef getPassName() const override {
    return "Windows 32-bit x86 EH state insertion";
  }

private:
  void emitExceptionRegistrationRecord(Function *F);

  void linkExceptionRegistration(IRBuilder<> &Builder, Function *Handler);
  void unlinkExceptionRegistration(IRBuilder<> &Builder);
  void addStateStores(Function &F, WinEHFuncInfo &FuncInfo);
  void insertStateNumberStore(Instruction *IP, int State);

  Value *emitEHLSDA(IRBuilder<> &Builder, Function *F);

  Function *generateLSDAInEAXThunk(Function *ParentFunc);

  bool isStateStoreNeeded(EHPersonality Personality, CallBase &Call);
  void rewriteSetJmpCall(IRBuilder<> &Builder, Function &F, CallBase &Call,
                         Value *State);
  int getBaseStateForBB(DenseMap<BasicBlock *, ColorVector> &BlockColors,
                        WinEHFuncInfo &FuncInfo, BasicBlock *BB);
  int getStateForCall(DenseMap<BasicBlock *, ColorVector> &BlockColors,
                      WinEHFuncInfo &FuncInfo, CallBase &Call);

  // Module-level type getters.
  Type *getEHLinkRegistrationType();
  Type *getSEHRegistrationType();
  Type *getCXXEHRegistrationType();

  // Per-module data.
  Module *TheModule = nullptr;
  StructType *EHLinkRegistrationTy = nullptr;
  StructType *CXXEHRegistrationTy = nullptr;
  StructType *SEHRegistrationTy = nullptr;
  FunctionCallee SetJmp3 = nullptr;
  FunctionCallee CxxLongjmpUnwind = nullptr;

  // Per-function state
  EHPersonality Personality = EHPersonality::Unknown;
  Function *PersonalityFn = nullptr;
  bool UseStackGuard = false;
  int ParentBaseState = 0;
  FunctionCallee SehLongjmpUnwind = nullptr;
  Constant *Cookie = nullptr;

  /// The stack allocation containing all EH data, including the link in the
  /// fs:00 chain and the current state.
  AllocaInst *RegNode = nullptr;

  // The allocation containing the EH security guard.
  AllocaInst *EHGuardNode = nullptr;

  /// The index of the state field of RegNode.
  int StateFieldIndex = ~0U;

  /// The linked list node subobject inside of RegNode.
  Value *Link = nullptr;
};
} // namespace

FunctionPass *llvm::createX86WinEHStatePass() { return new WinEHStatePass(); }

char WinEHStatePass::ID = 0;

INITIALIZE_PASS(WinEHStatePass, "x86-winehstate",
                "Insert stores for EH state numbers", false, false)

bool WinEHStatePass::doInitialization(Module &M) {
  TheModule = &M;
  return false;
}

bool WinEHStatePass::doFinalization(Module &M) {
  assert(TheModule == &M);
  TheModule = nullptr;
  EHLinkRegistrationTy = nullptr;
  CXXEHRegistrationTy = nullptr;
  SEHRegistrationTy = nullptr;
  SetJmp3 = nullptr;
  CxxLongjmpUnwind = nullptr;
  SehLongjmpUnwind = nullptr;
  Cookie = nullptr;
  return false;
}

void WinEHStatePass::getAnalysisUsage(AnalysisUsage &AU) const {
  // This pass should only insert a stack allocation, memory accesses, and
  // localrecovers.
  AU.setPreservesCFG();
}

bool WinEHStatePass::runOnFunction(Function &F) {
  // Don't insert state stores or exception handler thunks for
  // available_externally functions. The handler needs to reference the LSDA,
  // which will not be emitted in this case.
  if (F.hasAvailableExternallyLinkage())
    return false;

  // Check the personality. Do nothing if this personality doesn't use funclets.
  if (!F.hasPersonalityFn())
    return false;
  PersonalityFn =
      dyn_cast<Function>(F.getPersonalityFn()->stripPointerCasts());
  if (!PersonalityFn)
    return false;
  Personality = classifyEHPersonality(PersonalityFn);
  if (!isFuncletEHPersonality(Personality))
    return false;

  // Skip this function if there are no EH pads and we aren't using IR-level
  // outlining.
  bool HasPads = false;
  for (BasicBlock &BB : F) {
    if (BB.isEHPad()) {
      HasPads = true;
      break;
    }
  }
  if (!HasPads)
    return false;

  Type *Int8PtrType = PointerType::getUnqual(TheModule->getContext());
  SetJmp3 = TheModule->getOrInsertFunction(
      "_setjmp3", FunctionType::get(
                      Type::getInt32Ty(TheModule->getContext()),
                      {Int8PtrType, Type::getInt32Ty(TheModule->getContext())},
                      /*isVarArg=*/true));

  emitExceptionRegistrationRecord(&F);

  // The state numbers calculated here in IR must agree with what we calculate
  // later on for the MachineFunction. In particular, if an IR pass deletes an
  // unreachable EH pad after this point before machine CFG construction, we
  // will be in trouble. If this assumption is ever broken, we should turn the
  // numbers into an immutable analysis pass.
  WinEHFuncInfo FuncInfo;
  addStateStores(F, FuncInfo);

  // Reset per-function state.
  PersonalityFn = nullptr;
  Personality = EHPersonality::Unknown;
  UseStackGuard = false;
  RegNode = nullptr;
  EHGuardNode = nullptr;

  return true;
}

/// Get the common EH registration subobject:
///   typedef _EXCEPTION_DISPOSITION (*PEXCEPTION_ROUTINE)(
///       _EXCEPTION_RECORD *, void *, _CONTEXT *, void *);
///   struct EHRegistrationNode {
///     EHRegistrationNode *Next;
///     PEXCEPTION_ROUTINE Handler;
///   };
Type *WinEHStatePass::getEHLinkRegistrationType() {
  if (EHLinkRegistrationTy)
    return EHLinkRegistrationTy;
  LLVMContext &Context = TheModule->getContext();
  EHLinkRegistrationTy = StructType::create(Context, "EHRegistrationNode");
  Type *FieldTys[] = {
      PointerType::getUnqual(
          EHLinkRegistrationTy->getContext()), // EHRegistrationNode *Next
      PointerType::getUnqual(Context) // EXCEPTION_DISPOSITION (*Handler)(...)
  };
  EHLinkRegistrationTy->setBody(FieldTys, false);
  return EHLinkRegistrationTy;
}

/// The __CxxFrameHandler3 registration node:
///   struct CXXExceptionRegistration {
///     void *SavedESP;
///     EHRegistrationNode SubRecord;
///     int32_t TryLevel;
///   };
Type *WinEHStatePass::getCXXEHRegistrationType() {
  if (CXXEHRegistrationTy)
    return CXXEHRegistrationTy;
  LLVMContext &Context = TheModule->getContext();
  Type *FieldTys[] = {
      PointerType::getUnqual(Context), // void *SavedESP
      getEHLinkRegistrationType(),     // EHRegistrationNode SubRecord
      Type::getInt32Ty(Context)        // int32_t TryLevel
  };
  CXXEHRegistrationTy =
      StructType::create(FieldTys, "CXXExceptionRegistration");
  return CXXEHRegistrationTy;
}

/// The _except_handler3/4 registration node:
///   struct EH4ExceptionRegistration {
///     void *SavedESP;
///     _EXCEPTION_POINTERS *ExceptionPointers;
///     EHRegistrationNode SubRecord;
///     int32_t EncodedScopeTable;
///     int32_t TryLevel;
///   };
Type *WinEHStatePass::getSEHRegistrationType() {
  if (SEHRegistrationTy)
    return SEHRegistrationTy;
  LLVMContext &Context = TheModule->getContext();
  Type *FieldTys[] = {
      PointerType::getUnqual(Context), // void *SavedESP
      PointerType::getUnqual(Context), // void *ExceptionPointers
      getEHLinkRegistrationType(),     // EHRegistrationNode SubRecord
      Type::getInt32Ty(Context),       // int32_t EncodedScopeTable
      Type::getInt32Ty(Context)        // int32_t TryLevel
  };
  SEHRegistrationTy = StructType::create(FieldTys, "SEHExceptionRegistration");
  return SEHRegistrationTy;
}

// Emit an exception registration record. These are stack allocations with the
// common subobject of two pointers: the previous registration record (the old
// fs:00) and the personality function for the current frame. The data before
// and after that is personality function specific.
void WinEHStatePass::emitExceptionRegistrationRecord(Function *F) {
  assert(Personality == EHPersonality::MSVC_CXX ||
         Personality == EHPersonality::MSVC_X86SEH);

  // Struct type of RegNode. Used for GEPing.
  Type *RegNodeTy;

  IRBuilder<> Builder(&F->getEntryBlock(), F->getEntryBlock().begin());
  Type *Int8PtrType = Builder.getPtrTy();
  Type *Int32Ty = Builder.getInt32Ty();
  Type *VoidTy = Builder.getVoidTy();

  if (Personality == EHPersonality::MSVC_CXX) {
    RegNodeTy = getCXXEHRegistrationType();
    RegNode = Builder.CreateAlloca(RegNodeTy);
    // SavedESP = llvm.stacksave()
    Value *SP = Builder.CreateStackSave();
    Builder.CreateStore(SP, Builder.CreateStructGEP(RegNodeTy, RegNode, 0));
    // TryLevel = -1
    StateFieldIndex = 2;
    ParentBaseState = -1;
    insertStateNumberStore(&*Builder.GetInsertPoint(), ParentBaseState);
    // Handler = __ehhandler$F
    Function *Trampoline = generateLSDAInEAXThunk(F);
    Link = Builder.CreateStructGEP(RegNodeTy, RegNode, 1);
    linkExceptionRegistration(Builder, Trampoline);

    CxxLongjmpUnwind = TheModule->getOrInsertFunction(
        "__CxxLongjmpUnwind",
        FunctionType::get(VoidTy, Int8PtrType, /*isVarArg=*/false));
    cast<Function>(CxxLongjmpUnwind.getCallee()->stripPointerCasts())
        ->setCallingConv(CallingConv::X86_StdCall);
  } else if (Personality == EHPersonality::MSVC_X86SEH) {
    // If _except_handler4 is in use, some additional guard checks and prologue
    // stuff is required.
    StringRef PersonalityName = PersonalityFn->getName();
    UseStackGuard = (PersonalityName == "_except_handler4");

    // Allocate local structures.
    RegNodeTy = getSEHRegistrationType();
    RegNode = Builder.CreateAlloca(RegNodeTy);
    if (UseStackGuard)
      EHGuardNode = Builder.CreateAlloca(Int32Ty);

    // SavedESP = llvm.stacksave()
    Value *SP = Builder.CreateStackSave();
    Builder.CreateStore(SP, Builder.CreateStructGEP(RegNodeTy, RegNode, 0));
    // TryLevel = -2 / -1
    StateFieldIndex = 4;
    ParentBaseState = UseStackGuard ? -2 : -1;
    insertStateNumberStore(&*Builder.GetInsertPoint(), ParentBaseState);
    // ScopeTable = llvm.x86.seh.lsda(F)
    Value *LSDA = emitEHLSDA(Builder, F);
    LSDA = Builder.CreatePtrToInt(LSDA, Int32Ty);
    // If using _except_handler4, xor the address of the table with
    // __security_cookie.
    if (UseStackGuard) {
      Cookie = TheModule->getOrInsertGlobal("__security_cookie", Int32Ty);
      Value *Val = Builder.CreateLoad(Int32Ty, Cookie, "cookie");
      LSDA = Builder.CreateXor(LSDA, Val);
    }
    Builder.CreateStore(LSDA, Builder.CreateStructGEP(RegNodeTy, RegNode, 3));

    // If using _except_handler4, the EHGuard contains: FramePtr xor Cookie.
    if (UseStackGuard) {
      Value *Val = Builder.CreateLoad(Int32Ty, Cookie);
      Value *FrameAddr = Builder.CreateCall(
          Intrinsic::getDeclaration(
              TheModule, Intrinsic::frameaddress,
              Builder.getPtrTy(
                  TheModule->getDataLayout().getAllocaAddrSpace())),
          Builder.getInt32(0), "frameaddr");
      Value *FrameAddrI32 = Builder.CreatePtrToInt(FrameAddr, Int32Ty);
      FrameAddrI32 = Builder.CreateXor(FrameAddrI32, Val);
      Builder.CreateStore(FrameAddrI32, EHGuardNode);
    }

    // Register the exception handler.
    Link = Builder.CreateStructGEP(RegNodeTy, RegNode, 2);
    linkExceptionRegistration(Builder, PersonalityFn);

    SehLongjmpUnwind = TheModule->getOrInsertFunction(
        UseStackGuard ? "_seh_longjmp_unwind4" : "_seh_longjmp_unwind",
        FunctionType::get(Type::getVoidTy(TheModule->getContext()), Int8PtrType,
                          /*isVarArg=*/false));
    cast<Function>(SehLongjmpUnwind.getCallee()->stripPointerCasts())
        ->setCallingConv(CallingConv::X86_StdCall);
  } else {
    llvm_unreachable("unexpected personality function");
  }

  // Insert an unlink before all returns.
  for (BasicBlock &BB : *F) {
    Instruction *T = BB.getTerminator();
    if (!isa<ReturnInst>(T))
      continue;
    Builder.SetInsertPoint(T);
    unlinkExceptionRegistration(Builder);
  }
}

Value *WinEHStatePass::emitEHLSDA(IRBuilder<> &Builder, Function *F) {
  return Builder.CreateCall(
      Intrinsic::getDeclaration(TheModule, Intrinsic::x86_seh_lsda), F);
}

/// Generate a thunk that puts the LSDA of ParentFunc in EAX and then calls
/// PersonalityFn, forwarding the parameters passed to PEXCEPTION_ROUTINE:
///   typedef _EXCEPTION_DISPOSITION (*PEXCEPTION_ROUTINE)(
///       _EXCEPTION_RECORD *, void *, _CONTEXT *, void *);
/// We essentially want this code:
///   movl $lsda, %eax
///   jmpl ___CxxFrameHandler3
Function *WinEHStatePass::generateLSDAInEAXThunk(Function *ParentFunc) {
  LLVMContext &Context = ParentFunc->getContext();
  Type *Int32Ty = Type::getInt32Ty(Context);
  Type *Int8PtrType = PointerType::getUnqual(Context);
  Type *ArgTys[5] = {Int8PtrType, Int8PtrType, Int8PtrType, Int8PtrType,
                     Int8PtrType};
  FunctionType *TrampolineTy =
      FunctionType::get(Int32Ty, ArrayRef(&ArgTys[0], 4),
                        /*isVarArg=*/false);
  FunctionType *TargetFuncTy =
      FunctionType::get(Int32Ty, ArrayRef(&ArgTys[0], 5),
                        /*isVarArg=*/false);
  Function *Trampoline =
      Function::Create(TrampolineTy, GlobalValue::InternalLinkage,
                       Twine("__ehhandler$") + GlobalValue::dropLLVMManglingEscape(
                                                   ParentFunc->getName()),
                       TheModule);
  if (auto *C = ParentFunc->getComdat())
    Trampoline->setComdat(C);
  BasicBlock *EntryBB = BasicBlock::Create(Context, "entry", Trampoline);
  IRBuilder<> Builder(EntryBB);
  Value *LSDA = emitEHLSDA(Builder, ParentFunc);
  auto AI = Trampoline->arg_begin();
  Value *Args[5] = {LSDA, &*AI++, &*AI++, &*AI++, &*AI++};
  CallInst *Call = Builder.CreateCall(TargetFuncTy, PersonalityFn, Args);
  // Can't use musttail due to prototype mismatch, but we can use tail.
  Call->setTailCall(true);
  // Set inreg so we pass it in EAX.
  Call->addParamAttr(0, Attribute::InReg);
  Builder.CreateRet(Call);
  return Trampoline;
}

void WinEHStatePass::linkExceptionRegistration(IRBuilder<> &Builder,
                                               Function *Handler) {
  // Emit the .safeseh directive for this function.
  Handler->addFnAttr("safeseh");

  LLVMContext &C = Builder.getContext();
  Type *LinkTy = getEHLinkRegistrationType();
  // Handler = Handler
  Builder.CreateStore(Handler, Builder.CreateStructGEP(LinkTy, Link, 1));
  // Next = [fs:00]
  Constant *FSZero = Constant::getNullValue(PointerType::get(C, 257));
  Value *Next = Builder.CreateLoad(PointerType::getUnqual(C), FSZero);
  Builder.CreateStore(Next, Builder.CreateStructGEP(LinkTy, Link, 0));
  // [fs:00] = Link
  Builder.CreateStore(Link, FSZero);
}

void WinEHStatePass::unlinkExceptionRegistration(IRBuilder<> &Builder) {
  // Clone Link into the current BB for better address mode folding.
  if (auto *GEP = dyn_cast<GetElementPtrInst>(Link)) {
    GEP = cast<GetElementPtrInst>(GEP->clone());
    Builder.Insert(GEP);
    Link = GEP;
  }

  LLVMContext &C = Builder.getContext();
  Type *LinkTy = getEHLinkRegistrationType();
  // [fs:00] = Link->Next
  Value *Next = Builder.CreateLoad(PointerType::getUnqual(C),
                                   Builder.CreateStructGEP(LinkTy, Link, 0));
  Constant *FSZero = Constant::getNullValue(PointerType::get(C, 257));
  Builder.CreateStore(Next, FSZero);
}

// Calls to setjmp(p) are lowered to _setjmp3(p, 0) by the frontend.
// The idea behind _setjmp3 is that it takes an optional number of personality
// specific parameters to indicate how to restore the personality-specific frame
// state when longjmp is initiated.  Typically, the current TryLevel is saved.
void WinEHStatePass::rewriteSetJmpCall(IRBuilder<> &Builder, Function &F,
                                       CallBase &Call, Value *State) {
  // Don't rewrite calls with a weird number of arguments.
  if (Call.arg_size() != 2)
    return;

  SmallVector<OperandBundleDef, 1> OpBundles;
  Call.getOperandBundlesAsDefs(OpBundles);

  SmallVector<Value *, 3> OptionalArgs;
  if (Personality == EHPersonality::MSVC_CXX) {
    OptionalArgs.push_back(CxxLongjmpUnwind.getCallee());
    OptionalArgs.push_back(State);
    OptionalArgs.push_back(emitEHLSDA(Builder, &F));
  } else if (Personality == EHPersonality::MSVC_X86SEH) {
    OptionalArgs.push_back(SehLongjmpUnwind.getCallee());
    OptionalArgs.push_back(State);
    if (UseStackGuard)
      OptionalArgs.push_back(Cookie);
  } else {
    llvm_unreachable("unhandled personality!");
  }

  SmallVector<Value *, 5> Args;
  Args.push_back(
      Builder.CreateBitCast(Call.getArgOperand(0), Builder.getPtrTy()));
  Args.push_back(Builder.getInt32(OptionalArgs.size()));
  Args.append(OptionalArgs.begin(), OptionalArgs.end());

  CallBase *NewCall;
  if (auto *CI = dyn_cast<CallInst>(&Call)) {
    CallInst *NewCI = Builder.CreateCall(SetJmp3, Args, OpBundles);
    NewCI->setTailCallKind(CI->getTailCallKind());
    NewCall = NewCI;
  } else {
    auto *II = cast<InvokeInst>(&Call);
    NewCall = Builder.CreateInvoke(
        SetJmp3, II->getNormalDest(), II->getUnwindDest(), Args, OpBundles);
  }
  NewCall->setCallingConv(Call.getCallingConv());
  NewCall->setAttributes(Call.getAttributes());
  NewCall->setDebugLoc(Call.getDebugLoc());

  NewCall->takeName(&Call);
  Call.replaceAllUsesWith(NewCall);
  Call.eraseFromParent();
}

// Figure out what state we should assign calls in this block.
int WinEHStatePass::getBaseStateForBB(
    DenseMap<BasicBlock *, ColorVector> &BlockColors, WinEHFuncInfo &FuncInfo,
    BasicBlock *BB) {
  int BaseState = ParentBaseState;
  auto &BBColors = BlockColors[BB];

  assert(BBColors.size() == 1 && "multi-color BB not removed by preparation");
  BasicBlock *FuncletEntryBB = BBColors.front();
  if (auto *FuncletPad =
          dyn_cast<FuncletPadInst>(FuncletEntryBB->getFirstNonPHI())) {
    auto BaseStateI = FuncInfo.FuncletBaseStateMap.find(FuncletPad);
    if (BaseStateI != FuncInfo.FuncletBaseStateMap.end())
      BaseState = BaseStateI->second;
  }

  return BaseState;
}

// Calculate the state a call-site is in.
int WinEHStatePass::getStateForCall(
    DenseMap<BasicBlock *, ColorVector> &BlockColors, WinEHFuncInfo &FuncInfo,
    CallBase &Call) {
  if (auto *II = dyn_cast<InvokeInst>(&Call)) {
    // Look up the state number of the EH pad this unwinds to.
    assert(FuncInfo.InvokeStateMap.count(II) && "invoke has no state!");
    return FuncInfo.InvokeStateMap[II];
  }
  // Possibly throwing call instructions have no actions to take after
  // an unwind. Ensure they are in the -1 state.
  return getBaseStateForBB(BlockColors, FuncInfo, Call.getParent());
}

// Calculate the intersection of all the FinalStates for a BasicBlock's
// predecessors.
static int getPredState(DenseMap<BasicBlock *, int> &FinalStates, Function &F,
                        int ParentBaseState, BasicBlock *BB) {
  // The entry block has no predecessors but we know that the prologue always
  // sets us up with a fixed state.
  if (&F.getEntryBlock() == BB)
    return ParentBaseState;

  // This is an EH Pad, conservatively report this basic block as overdefined.
  if (BB->isEHPad())
    return OverdefinedState;

  int CommonState = OverdefinedState;
  for (BasicBlock *PredBB : predecessors(BB)) {
    // We didn't manage to get a state for one of these predecessors,
    // conservatively report this basic block as overdefined.
    auto PredEndState = FinalStates.find(PredBB);
    if (PredEndState == FinalStates.end())
      return OverdefinedState;

    // This code is reachable via exceptional control flow,
    // conservatively report this basic block as overdefined.
    if (isa<CatchReturnInst>(PredBB->getTerminator()))
      return OverdefinedState;

    int PredState = PredEndState->second;
    assert(PredState != OverdefinedState &&
           "overdefined BBs shouldn't be in FinalStates");
    if (CommonState == OverdefinedState)
      CommonState = PredState;

    // At least two predecessors have different FinalStates,
    // conservatively report this basic block as overdefined.
    if (CommonState != PredState)
      return OverdefinedState;
  }

  return CommonState;
}

// Calculate the intersection of all the InitialStates for a BasicBlock's
// successors.
static int getSuccState(DenseMap<BasicBlock *, int> &InitialStates, Function &F,
                        int ParentBaseState, BasicBlock *BB) {
  // This block rejoins normal control flow,
  // conservatively report this basic block as overdefined.
  if (isa<CatchReturnInst>(BB->getTerminator()))
    return OverdefinedState;

  int CommonState = OverdefinedState;
  for (BasicBlock *SuccBB : successors(BB)) {
    // We didn't manage to get a state for one of these predecessors,
    // conservatively report this basic block as overdefined.
    auto SuccStartState = InitialStates.find(SuccBB);
    if (SuccStartState == InitialStates.end())
      return OverdefinedState;

    // This is an EH Pad, conservatively report this basic block as overdefined.
    if (SuccBB->isEHPad())
      return OverdefinedState;

    int SuccState = SuccStartState->second;
    assert(SuccState != OverdefinedState &&
           "overdefined BBs shouldn't be in FinalStates");
    if (CommonState == OverdefinedState)
      CommonState = SuccState;

    // At least two successors have different InitialStates,
    // conservatively report this basic block as overdefined.
    if (CommonState != SuccState)
      return OverdefinedState;
  }

  return CommonState;
}

bool WinEHStatePass::isStateStoreNeeded(EHPersonality Personality,
                                        CallBase &Call) {
  // If the function touches memory, it needs a state store.
  if (isAsynchronousEHPersonality(Personality))
    return !Call.doesNotAccessMemory();

  // If the function throws, it needs a state store.
  return !Call.doesNotThrow();
}

void WinEHStatePass::addStateStores(Function &F, WinEHFuncInfo &FuncInfo) {
  // Mark the registration node. The backend needs to know which alloca it is so
  // that it can recover the original frame pointer.
  IRBuilder<> Builder(RegNode->getNextNode());
  Value *RegNodeI8 = Builder.CreateBitCast(RegNode, Builder.getPtrTy());
  Builder.CreateCall(
      Intrinsic::getDeclaration(TheModule, Intrinsic::x86_seh_ehregnode),
      {RegNodeI8});

  if (EHGuardNode) {
    IRBuilder<> Builder(EHGuardNode->getNextNode());
    Value *EHGuardNodeI8 =
        Builder.CreateBitCast(EHGuardNode, Builder.getPtrTy());
    Builder.CreateCall(
        Intrinsic::getDeclaration(TheModule, Intrinsic::x86_seh_ehguard),
        {EHGuardNodeI8});
  }

  // Calculate state numbers.
  if (isAsynchronousEHPersonality(Personality))
    calculateSEHStateNumbers(&F, FuncInfo);
  else
    calculateWinCXXEHStateNumbers(&F, FuncInfo);

  // Iterate all the instructions and emit state number stores.
  DenseMap<BasicBlock *, ColorVector> BlockColors = colorEHFunclets(F);
  ReversePostOrderTraversal<Function *> RPOT(&F);

  // InitialStates yields the state of the first call-site for a BasicBlock.
  DenseMap<BasicBlock *, int> InitialStates;
  // FinalStates yields the state of the last call-site for a BasicBlock.
  DenseMap<BasicBlock *, int> FinalStates;
  // Worklist used to revisit BasicBlocks with indeterminate
  // Initial/Final-States.
  std::deque<BasicBlock *> Worklist;
  // Fill in InitialStates and FinalStates for BasicBlocks with call-sites.
  for (BasicBlock *BB : RPOT) {
    int InitialState = OverdefinedState;
    int FinalState;
    if (&F.getEntryBlock() == BB)
      InitialState = FinalState = ParentBaseState;
    for (Instruction &I : *BB) {
      auto *Call = dyn_cast<CallBase>(&I);
      if (!Call || !isStateStoreNeeded(Personality, *Call))
        continue;

      int State = getStateForCall(BlockColors, FuncInfo, *Call);
      if (InitialState == OverdefinedState)
        InitialState = State;
      FinalState = State;
    }
    // No call-sites in this basic block? That's OK, we will come back to these
    // in a later pass.
    if (InitialState == OverdefinedState) {
      Worklist.push_back(BB);
      continue;
    }
    LLVM_DEBUG(dbgs() << "X86WinEHState: " << BB->getName()
                      << " InitialState=" << InitialState << '\n');
    LLVM_DEBUG(dbgs() << "X86WinEHState: " << BB->getName()
                      << " FinalState=" << FinalState << '\n');
    InitialStates.insert({BB, InitialState});
    FinalStates.insert({BB, FinalState});
  }

  // Try to fill-in InitialStates and FinalStates which have no call-sites.
  while (!Worklist.empty()) {
    BasicBlock *BB = Worklist.front();
    Worklist.pop_front();
    // This BasicBlock has already been figured out, nothing more we can do.
    if (InitialStates.count(BB) != 0)
      continue;

    int PredState = getPredState(FinalStates, F, ParentBaseState, BB);
    if (PredState == OverdefinedState)
      continue;

    // We successfully inferred this BasicBlock's state via it's predecessors;
    // enqueue it's successors to see if we can infer their states.
    InitialStates.insert({BB, PredState});
    FinalStates.insert({BB, PredState});
    for (BasicBlock *SuccBB : successors(BB))
      Worklist.push_back(SuccBB);
  }

  // Try to hoist stores from successors.
  for (BasicBlock *BB : RPOT) {
    int SuccState = getSuccState(InitialStates, F, ParentBaseState, BB);
    if (SuccState == OverdefinedState)
      continue;

    // Update our FinalState to reflect the common InitialState of our
    // successors.
    FinalStates.insert({BB, SuccState});
  }

  // Finally, insert state stores before call-sites which transition us to a new
  // state.
  for (BasicBlock *BB : RPOT) {
    auto &BBColors = BlockColors[BB];
    BasicBlock *FuncletEntryBB = BBColors.front();
    if (isa<CleanupPadInst>(FuncletEntryBB->getFirstNonPHI()))
      continue;

    int PrevState = getPredState(FinalStates, F, ParentBaseState, BB);
    LLVM_DEBUG(dbgs() << "X86WinEHState: " << BB->getName()
                      << " PrevState=" << PrevState << '\n');

    for (Instruction &I : *BB) {
      auto *Call = dyn_cast<CallBase>(&I);
      if (!Call || !isStateStoreNeeded(Personality, *Call))
        continue;

      int State = getStateForCall(BlockColors, FuncInfo, *Call);
      if (State != PrevState)
        insertStateNumberStore(&I, State);
      PrevState = State;
    }

    // We might have hoisted a state store into this block, emit it now.
    auto EndState = FinalStates.find(BB);
    if (EndState != FinalStates.end())
      if (EndState->second != PrevState)
        insertStateNumberStore(BB->getTerminator(), EndState->second);
  }

  SmallVector<CallBase *, 1> SetJmp3Calls;
  for (BasicBlock *BB : RPOT) {
    for (Instruction &I : *BB) {
      auto *Call = dyn_cast<CallBase>(&I);
      if (!Call)
        continue;
      if (Call->getCalledOperand()->stripPointerCasts() !=
          SetJmp3.getCallee()->stripPointerCasts())
        continue;

      SetJmp3Calls.push_back(Call);
    }
  }

  for (CallBase *Call : SetJmp3Calls) {
    auto &BBColors = BlockColors[Call->getParent()];
    BasicBlock *FuncletEntryBB = BBColors.front();
    bool InCleanup = isa<CleanupPadInst>(FuncletEntryBB->getFirstNonPHI());

    IRBuilder<> Builder(Call);
    Value *State;
    if (InCleanup) {
      Value *StateField = Builder.CreateStructGEP(RegNode->getAllocatedType(),
                                                  RegNode, StateFieldIndex);
      State = Builder.CreateLoad(Builder.getInt32Ty(), StateField);
    } else {
      State = Builder.getInt32(getStateForCall(BlockColors, FuncInfo, *Call));
    }
    rewriteSetJmpCall(Builder, F, *Call, State);
  }
}

void WinEHStatePass::insertStateNumberStore(Instruction *IP, int State) {
  IRBuilder<> Builder(IP);
  Value *StateField = Builder.CreateStructGEP(RegNode->getAllocatedType(),
                                              RegNode, StateFieldIndex);
  Builder.CreateStore(Builder.getInt32(State), StateField);
}
