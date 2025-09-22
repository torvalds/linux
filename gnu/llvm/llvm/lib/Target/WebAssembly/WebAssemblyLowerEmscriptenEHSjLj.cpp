//=== WebAssemblyLowerEmscriptenEHSjLj.cpp - Lower exceptions for Emscripten =//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file lowers exception-related instructions and setjmp/longjmp function
/// calls to use Emscripten's library functions. The pass uses JavaScript's try
/// and catch mechanism in case of Emscripten EH/SjLj and Wasm EH intrinsics in
/// case of Emscripten SjLJ.
///
/// * Emscripten exception handling
/// This pass lowers invokes and landingpads into library functions in JS glue
/// code. Invokes are lowered into function wrappers called invoke wrappers that
/// exist in JS side, which wraps the original function call with JS try-catch.
/// If an exception occurred, cxa_throw() function in JS side sets some
/// variables (see below) so we can check whether an exception occurred from
/// wasm code and handle it appropriately.
///
/// * Emscripten setjmp-longjmp handling
/// This pass lowers setjmp to a reasonably-performant approach for emscripten.
/// The idea is that each block with a setjmp is broken up into two parts: the
/// part containing setjmp and the part right after the setjmp. The latter part
/// is either reached from the setjmp, or later from a longjmp. To handle the
/// longjmp, all calls that might longjmp are also called using invoke wrappers
/// and thus JS / try-catch. JS longjmp() function also sets some variables so
/// we can check / whether a longjmp occurred from wasm code. Each block with a
/// function call that might longjmp is also split up after the longjmp call.
/// After the longjmp call, we check whether a longjmp occurred, and if it did,
/// which setjmp it corresponds to, and jump to the right post-setjmp block.
/// We assume setjmp-longjmp handling always run after EH handling, which means
/// we don't expect any exception-related instructions when SjLj runs.
/// FIXME Currently this scheme does not support indirect call of setjmp,
/// because of the limitation of the scheme itself. fastcomp does not support it
/// either.
///
/// In detail, this pass does following things:
///
/// 1) Assumes the existence of global variables: __THREW__, __threwValue
///    __THREW__ and __threwValue are defined in compiler-rt in Emscripten.
///    These variables are used for both exceptions and setjmp/longjmps.
///    __THREW__ indicates whether an exception or a longjmp occurred or not. 0
///    means nothing occurred, 1 means an exception occurred, and other numbers
///    mean a longjmp occurred. In the case of longjmp, __THREW__ variable
///    indicates the corresponding setjmp buffer the longjmp corresponds to.
///    __threwValue is 0 for exceptions, and the argument to longjmp in case of
///    longjmp.
///
/// * Emscripten exception handling
///
/// 2) We assume the existence of setThrew and setTempRet0/getTempRet0 functions
///    at link time. setThrew exists in Emscripten's compiler-rt:
///
///    void setThrew(uintptr_t threw, int value) {
///      if (__THREW__ == 0) {
///        __THREW__ = threw;
///        __threwValue = value;
///      }
///    }
//
///    setTempRet0 is called from __cxa_find_matching_catch() in JS glue code.
///    In exception handling, getTempRet0 indicates the type of an exception
///    caught, and in setjmp/longjmp, it means the second argument to longjmp
///    function.
///
/// 3) Lower
///      invoke @func(arg1, arg2) to label %invoke.cont unwind label %lpad
///    into
///      __THREW__ = 0;
///      call @__invoke_SIG(func, arg1, arg2)
///      %__THREW__.val = __THREW__;
///      __THREW__ = 0;
///      if (%__THREW__.val == 1)
///        goto %lpad
///      else
///         goto %invoke.cont
///    SIG is a mangled string generated based on the LLVM IR-level function
///    signature. After LLVM IR types are lowered to the target wasm types,
///    the names for these wrappers will change based on wasm types as well,
///    as in invoke_vi (function takes an int and returns void). The bodies of
///    these wrappers will be generated in JS glue code, and inside those
///    wrappers we use JS try-catch to generate actual exception effects. It
///    also calls the original callee function. An example wrapper in JS code
///    would look like this:
///      function invoke_vi(index,a1) {
///        try {
///          Module["dynCall_vi"](index,a1); // This calls original callee
///        } catch(e) {
///          if (typeof e !== 'number' && e !== 'longjmp') throw e;
///          _setThrew(1, 0); // setThrew is called here
///        }
///      }
///    If an exception is thrown, __THREW__ will be set to true in a wrapper,
///    so we can jump to the right BB based on this value.
///
/// 4) Lower
///      %val = landingpad catch c1 catch c2 catch c3 ...
///      ... use %val ...
///    into
///      %fmc = call @__cxa_find_matching_catch_N(c1, c2, c3, ...)
///      %val = {%fmc, getTempRet0()}
///      ... use %val ...
///    Here N is a number calculated based on the number of clauses.
///    setTempRet0 is called from __cxa_find_matching_catch() in JS glue code.
///
/// 5) Lower
///      resume {%a, %b}
///    into
///      call @__resumeException(%a)
///    where __resumeException() is a function in JS glue code.
///
/// 6) Lower
///      call @llvm.eh.typeid.for(type) (intrinsic)
///    into
///      call @llvm_eh_typeid_for(type)
///    llvm_eh_typeid_for function will be generated in JS glue code.
///
/// * Emscripten setjmp / longjmp handling
///
/// If there are calls to longjmp()
///
/// 1) Lower
///      longjmp(env, val)
///    into
///      emscripten_longjmp(env, val)
///
/// If there are calls to setjmp()
///
/// 2) In the function entry that calls setjmp, initialize
///    functionInvocationId as follows:
///
///    functionInvocationId = alloca(4)
///
///    Note: the alloca size is not important as this pointer is
///    merely used for pointer comparisions.
///
/// 3) Lower
///      setjmp(env)
///    into
///      __wasm_setjmp(env, label, functionInvocationId)
///
///    __wasm_setjmp records the necessary info (the label and
///    functionInvocationId) to the "env".
///    A BB with setjmp is split into two after setjmp call in order to
///    make the post-setjmp BB the possible destination of longjmp BB.
///
/// 4) Lower every call that might longjmp into
///      __THREW__ = 0;
///      call @__invoke_SIG(func, arg1, arg2)
///      %__THREW__.val = __THREW__;
///      __THREW__ = 0;
///      %__threwValue.val = __threwValue;
///      if (%__THREW__.val != 0 & %__threwValue.val != 0) {
///        %label = __wasm_setjmp_test(%__THREW__.val, functionInvocationId);
///        if (%label == 0)
///          emscripten_longjmp(%__THREW__.val, %__threwValue.val);
///        setTempRet0(%__threwValue.val);
///      } else {
///        %label = -1;
///      }
///      longjmp_result = getTempRet0();
///      switch %label {
///        label 1: goto post-setjmp BB 1
///        label 2: goto post-setjmp BB 2
///        ...
///        default: goto splitted next BB
///      }
///
///    __wasm_setjmp_test examines the jmp buf to see if it was for a matching
///    setjmp call. After calling an invoke wrapper, if a longjmp occurred,
///    __THREW__ will be the address of matching jmp_buf buffer and
///    __threwValue be the second argument to longjmp.
///    __wasm_setjmp_test returns a setjmp label, a unique ID to each setjmp
///    callsite. Label 0 means this longjmp buffer does not correspond to one
///    of the setjmp callsites in this function, so in this case we just chain
///    the longjmp to the caller. Label -1 means no longjmp occurred.
///    Otherwise we jump to the right post-setjmp BB based on the label.
///
/// * Wasm setjmp / longjmp handling
/// This mode still uses some Emscripten library functions but not JavaScript's
/// try-catch mechanism. It instead uses Wasm exception handling intrinsics,
/// which will be lowered to exception handling instructions.
///
/// If there are calls to longjmp()
///
/// 1) Lower
///      longjmp(env, val)
///    into
///      __wasm_longjmp(env, val)
///
/// If there are calls to setjmp()
///
/// 2) and 3): The same as 2) and 3) in Emscripten SjLj.
/// (functionInvocationId initialization + setjmp callsite transformation)
///
/// 4) Create a catchpad with a wasm.catch() intrinsic, which returns the value
/// thrown by __wasm_longjmp function. In the runtime library, we have an
/// equivalent of the following struct:
///
/// struct __WasmLongjmpArgs {
///   void *env;
///   int val;
/// };
///
/// The thrown value here is a pointer to the struct. We use this struct to
/// transfer two values by throwing a single value. Wasm throw and catch
/// instructions are capable of throwing and catching multiple values, but
/// it also requires multivalue support that is currently not very reliable.
/// TODO Switch to throwing and catching two values without using the struct
///
/// All longjmpable function calls will be converted to an invoke that will
/// unwind to this catchpad in case a longjmp occurs. Within the catchpad, we
/// test the thrown values using __wasm_setjmp_test function as we do for
/// Emscripten SjLj. The main difference is, in Emscripten SjLj, we need to
/// transform every longjmpable callsite into a sequence of code including
/// __wasm_setjmp_test() call; in Wasm SjLj we do the testing in only one
/// place, in this catchpad.
///
/// After testing calling __wasm_setjmp_test(), if the longjmp does not
/// correspond to one of the setjmps within the current function, it rethrows
/// the longjmp by calling __wasm_longjmp(). If it corresponds to one of
/// setjmps in the function, we jump to the beginning of the function, which
/// contains a switch to each post-setjmp BB. Again, in Emscripten SjLj, this
/// switch is added for every longjmpable callsite; in Wasm SjLj we do this
/// only once at the top of the function. (after functionInvocationId
/// initialization)
///
/// The below is the pseudocode for what we have described
///
/// entry:
///   Initialize functionInvocationId
///
/// setjmp.dispatch:
///    switch %label {
///      label 1: goto post-setjmp BB 1
///      label 2: goto post-setjmp BB 2
///      ...
///      default: goto splitted next BB
///    }
/// ...
///
/// bb:
///   invoke void @foo() ;; foo is a longjmpable function
///     to label %next unwind label %catch.dispatch.longjmp
/// ...
///
/// catch.dispatch.longjmp:
///   %0 = catchswitch within none [label %catch.longjmp] unwind to caller
///
/// catch.longjmp:
///   %longjmp.args = wasm.catch() ;; struct __WasmLongjmpArgs
///   %env = load 'env' field from __WasmLongjmpArgs
///   %val = load 'val' field from __WasmLongjmpArgs
///   %label = __wasm_setjmp_test(%env, functionInvocationId);
///   if (%label == 0)
///     __wasm_longjmp(%env, %val)
///   catchret to %setjmp.dispatch
///
///===----------------------------------------------------------------------===//

#include "MCTargetDesc/WebAssemblyMCTargetDesc.h"
#include "WebAssembly.h"
#include "WebAssemblyTargetMachine.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/WasmEHFuncInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicsWebAssembly.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/SSAUpdater.h"
#include "llvm/Transforms/Utils/SSAUpdaterBulk.h"
#include <set>

using namespace llvm;

#define DEBUG_TYPE "wasm-lower-em-ehsjlj"

static cl::list<std::string>
    EHAllowlist("emscripten-cxx-exceptions-allowed",
                cl::desc("The list of function names in which Emscripten-style "
                         "exception handling is enabled (see emscripten "
                         "EMSCRIPTEN_CATCHING_ALLOWED options)"),
                cl::CommaSeparated);

namespace {
class WebAssemblyLowerEmscriptenEHSjLj final : public ModulePass {
  bool EnableEmEH;     // Enable Emscripten exception handling
  bool EnableEmSjLj;   // Enable Emscripten setjmp/longjmp handling
  bool EnableWasmSjLj; // Enable Wasm setjmp/longjmp handling
  bool DoSjLj;         // Whether we actually perform setjmp/longjmp handling

  GlobalVariable *ThrewGV = nullptr;      // __THREW__ (Emscripten)
  GlobalVariable *ThrewValueGV = nullptr; // __threwValue (Emscripten)
  Function *GetTempRet0F = nullptr;       // getTempRet0() (Emscripten)
  Function *SetTempRet0F = nullptr;       // setTempRet0() (Emscripten)
  Function *ResumeF = nullptr;            // __resumeException() (Emscripten)
  Function *EHTypeIDF = nullptr;          // llvm.eh.typeid.for() (intrinsic)
  Function *EmLongjmpF = nullptr;         // emscripten_longjmp() (Emscripten)
  Function *WasmSetjmpF = nullptr;        // __wasm_setjmp() (Emscripten)
  Function *WasmSetjmpTestF = nullptr;    // __wasm_setjmp_test() (Emscripten)
  Function *WasmLongjmpF = nullptr;       // __wasm_longjmp() (Emscripten)
  Function *CatchF = nullptr;             // wasm.catch() (intrinsic)

  // type of 'struct __WasmLongjmpArgs' defined in emscripten
  Type *LongjmpArgsTy = nullptr;

  // __cxa_find_matching_catch_N functions.
  // Indexed by the number of clauses in an original landingpad instruction.
  DenseMap<int, Function *> FindMatchingCatches;
  // Map of <function signature string, invoke_ wrappers>
  StringMap<Function *> InvokeWrappers;
  // Set of allowed function names for exception handling
  std::set<std::string> EHAllowlistSet;
  // Functions that contains calls to setjmp
  SmallPtrSet<Function *, 8> SetjmpUsers;

  StringRef getPassName() const override {
    return "WebAssembly Lower Emscripten Exceptions";
  }

  using InstVector = SmallVectorImpl<Instruction *>;
  bool runEHOnFunction(Function &F);
  bool runSjLjOnFunction(Function &F);
  void handleLongjmpableCallsForEmscriptenSjLj(
      Function &F, Instruction *FunctionInvocationId,
      SmallVectorImpl<PHINode *> &SetjmpRetPHIs);
  void
  handleLongjmpableCallsForWasmSjLj(Function &F,
                                    Instruction *FunctionInvocationId,
                                    SmallVectorImpl<PHINode *> &SetjmpRetPHIs);
  Function *getFindMatchingCatch(Module &M, unsigned NumClauses);

  Value *wrapInvoke(CallBase *CI);
  void wrapTestSetjmp(BasicBlock *BB, DebugLoc DL, Value *Threw,
                      Value *FunctionInvocationId, Value *&Label,
                      Value *&LongjmpResult, BasicBlock *&CallEmLongjmpBB,
                      PHINode *&CallEmLongjmpBBThrewPHI,
                      PHINode *&CallEmLongjmpBBThrewValuePHI,
                      BasicBlock *&EndBB);
  Function *getInvokeWrapper(CallBase *CI);

  bool areAllExceptionsAllowed() const { return EHAllowlistSet.empty(); }
  bool supportsException(const Function *F) const {
    return EnableEmEH && (areAllExceptionsAllowed() ||
                          EHAllowlistSet.count(std::string(F->getName())));
  }
  void replaceLongjmpWith(Function *LongjmpF, Function *NewF);

  void rebuildSSA(Function &F);

public:
  static char ID;

  WebAssemblyLowerEmscriptenEHSjLj()
      : ModulePass(ID), EnableEmEH(WebAssembly::WasmEnableEmEH),
        EnableEmSjLj(WebAssembly::WasmEnableEmSjLj),
        EnableWasmSjLj(WebAssembly::WasmEnableSjLj) {
    assert(!(EnableEmSjLj && EnableWasmSjLj) &&
           "Two SjLj modes cannot be turned on at the same time");
    assert(!(EnableEmEH && EnableWasmSjLj) &&
           "Wasm SjLj should be only used with Wasm EH");
    EHAllowlistSet.insert(EHAllowlist.begin(), EHAllowlist.end());
  }
  bool runOnModule(Module &M) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<DominatorTreeWrapperPass>();
  }
};
} // End anonymous namespace

char WebAssemblyLowerEmscriptenEHSjLj::ID = 0;
INITIALIZE_PASS(WebAssemblyLowerEmscriptenEHSjLj, DEBUG_TYPE,
                "WebAssembly Lower Emscripten Exceptions / Setjmp / Longjmp",
                false, false)

ModulePass *llvm::createWebAssemblyLowerEmscriptenEHSjLj() {
  return new WebAssemblyLowerEmscriptenEHSjLj();
}

static bool canThrow(const Value *V) {
  if (const auto *F = dyn_cast<const Function>(V)) {
    // Intrinsics cannot throw
    if (F->isIntrinsic())
      return false;
    StringRef Name = F->getName();
    // leave setjmp and longjmp (mostly) alone, we process them properly later
    if (Name == "setjmp" || Name == "longjmp" || Name == "emscripten_longjmp")
      return false;
    return !F->doesNotThrow();
  }
  // not a function, so an indirect call - can throw, we can't tell
  return true;
}

// Get a thread-local global variable with the given name. If it doesn't exist
// declare it, which will generate an import and assume that it will exist at
// link time.
static GlobalVariable *getGlobalVariable(Module &M, Type *Ty,
                                         WebAssemblyTargetMachine &TM,
                                         const char *Name) {
  auto *GV = dyn_cast<GlobalVariable>(M.getOrInsertGlobal(Name, Ty));
  if (!GV)
    report_fatal_error(Twine("unable to create global: ") + Name);

  // Variables created by this function are thread local. If the target does not
  // support TLS, we depend on CoalesceFeaturesAndStripAtomics to downgrade it
  // to non-thread-local ones, in which case we don't allow this object to be
  // linked with other objects using shared memory.
  GV->setThreadLocalMode(GlobalValue::GeneralDynamicTLSModel);
  return GV;
}

// Simple function name mangler.
// This function simply takes LLVM's string representation of parameter types
// and concatenate them with '_'. There are non-alphanumeric characters but llc
// is ok with it, and we need to postprocess these names after the lowering
// phase anyway.
static std::string getSignature(FunctionType *FTy) {
  std::string Sig;
  raw_string_ostream OS(Sig);
  OS << *FTy->getReturnType();
  for (Type *ParamTy : FTy->params())
    OS << "_" << *ParamTy;
  if (FTy->isVarArg())
    OS << "_...";
  Sig = OS.str();
  erase_if(Sig, isSpace);
  // When s2wasm parses .s file, a comma means the end of an argument. So a
  // mangled function name can contain any character but a comma.
  std::replace(Sig.begin(), Sig.end(), ',', '.');
  return Sig;
}

static Function *getEmscriptenFunction(FunctionType *Ty, const Twine &Name,
                                       Module *M) {
  Function* F = Function::Create(Ty, GlobalValue::ExternalLinkage, Name, M);
  // Tell the linker that this function is expected to be imported from the
  // 'env' module.
  if (!F->hasFnAttribute("wasm-import-module")) {
    llvm::AttrBuilder B(M->getContext());
    B.addAttribute("wasm-import-module", "env");
    F->addFnAttrs(B);
  }
  if (!F->hasFnAttribute("wasm-import-name")) {
    llvm::AttrBuilder B(M->getContext());
    B.addAttribute("wasm-import-name", F->getName());
    F->addFnAttrs(B);
  }
  return F;
}

// Returns an integer type for the target architecture's address space.
// i32 for wasm32 and i64 for wasm64.
static Type *getAddrIntType(Module *M) {
  IRBuilder<> IRB(M->getContext());
  return IRB.getIntNTy(M->getDataLayout().getPointerSizeInBits());
}

// Returns an integer pointer type for the target architecture's address space.
// i32* for wasm32 and i64* for wasm64. With opaque pointers this is just a ptr
// in address space zero.
static Type *getAddrPtrType(Module *M) {
  return PointerType::getUnqual(M->getContext());
}

// Returns an integer whose type is the integer type for the target's address
// space. Returns (i32 C) for wasm32 and (i64 C) for wasm64, when C is the
// integer.
static Value *getAddrSizeInt(Module *M, uint64_t C) {
  IRBuilder<> IRB(M->getContext());
  return IRB.getIntN(M->getDataLayout().getPointerSizeInBits(), C);
}

// Returns __cxa_find_matching_catch_N function, where N = NumClauses + 2.
// This is because a landingpad instruction contains two more arguments, a
// personality function and a cleanup bit, and __cxa_find_matching_catch_N
// functions are named after the number of arguments in the original landingpad
// instruction.
Function *
WebAssemblyLowerEmscriptenEHSjLj::getFindMatchingCatch(Module &M,
                                                       unsigned NumClauses) {
  if (FindMatchingCatches.count(NumClauses))
    return FindMatchingCatches[NumClauses];
  PointerType *Int8PtrTy = PointerType::getUnqual(M.getContext());
  SmallVector<Type *, 16> Args(NumClauses, Int8PtrTy);
  FunctionType *FTy = FunctionType::get(Int8PtrTy, Args, false);
  Function *F = getEmscriptenFunction(
      FTy, "__cxa_find_matching_catch_" + Twine(NumClauses + 2), &M);
  FindMatchingCatches[NumClauses] = F;
  return F;
}

// Generate invoke wrapper seqence with preamble and postamble
// Preamble:
// __THREW__ = 0;
// Postamble:
// %__THREW__.val = __THREW__; __THREW__ = 0;
// Returns %__THREW__.val, which indicates whether an exception is thrown (or
// whether longjmp occurred), for future use.
Value *WebAssemblyLowerEmscriptenEHSjLj::wrapInvoke(CallBase *CI) {
  Module *M = CI->getModule();
  LLVMContext &C = M->getContext();

  IRBuilder<> IRB(C);
  IRB.SetInsertPoint(CI);

  // Pre-invoke
  // __THREW__ = 0;
  IRB.CreateStore(getAddrSizeInt(M, 0), ThrewGV);

  // Invoke function wrapper in JavaScript
  SmallVector<Value *, 16> Args;
  // Put the pointer to the callee as first argument, so it can be called
  // within the invoke wrapper later
  Args.push_back(CI->getCalledOperand());
  Args.append(CI->arg_begin(), CI->arg_end());
  CallInst *NewCall = IRB.CreateCall(getInvokeWrapper(CI), Args);
  NewCall->takeName(CI);
  NewCall->setCallingConv(CallingConv::WASM_EmscriptenInvoke);
  NewCall->setDebugLoc(CI->getDebugLoc());

  // Because we added the pointer to the callee as first argument, all
  // argument attribute indices have to be incremented by one.
  SmallVector<AttributeSet, 8> ArgAttributes;
  const AttributeList &InvokeAL = CI->getAttributes();

  // No attributes for the callee pointer.
  ArgAttributes.push_back(AttributeSet());
  // Copy the argument attributes from the original
  for (unsigned I = 0, E = CI->arg_size(); I < E; ++I)
    ArgAttributes.push_back(InvokeAL.getParamAttrs(I));

  AttrBuilder FnAttrs(CI->getContext(), InvokeAL.getFnAttrs());
  if (auto Args = FnAttrs.getAllocSizeArgs()) {
    // The allocsize attribute (if any) referes to parameters by index and needs
    // to be adjusted.
    auto [SizeArg, NEltArg] = *Args;
    SizeArg += 1;
    if (NEltArg)
      NEltArg = *NEltArg + 1;
    FnAttrs.addAllocSizeAttr(SizeArg, NEltArg);
  }
  // In case the callee has 'noreturn' attribute, We need to remove it, because
  // we expect invoke wrappers to return.
  FnAttrs.removeAttribute(Attribute::NoReturn);

  // Reconstruct the AttributesList based on the vector we constructed.
  AttributeList NewCallAL = AttributeList::get(
      C, AttributeSet::get(C, FnAttrs), InvokeAL.getRetAttrs(), ArgAttributes);
  NewCall->setAttributes(NewCallAL);

  CI->replaceAllUsesWith(NewCall);

  // Post-invoke
  // %__THREW__.val = __THREW__; __THREW__ = 0;
  Value *Threw =
      IRB.CreateLoad(getAddrIntType(M), ThrewGV, ThrewGV->getName() + ".val");
  IRB.CreateStore(getAddrSizeInt(M, 0), ThrewGV);
  return Threw;
}

// Get matching invoke wrapper based on callee signature
Function *WebAssemblyLowerEmscriptenEHSjLj::getInvokeWrapper(CallBase *CI) {
  Module *M = CI->getModule();
  SmallVector<Type *, 16> ArgTys;
  FunctionType *CalleeFTy = CI->getFunctionType();

  std::string Sig = getSignature(CalleeFTy);
  if (InvokeWrappers.contains(Sig))
    return InvokeWrappers[Sig];

  // Put the pointer to the callee as first argument
  ArgTys.push_back(PointerType::getUnqual(CalleeFTy));
  // Add argument types
  ArgTys.append(CalleeFTy->param_begin(), CalleeFTy->param_end());

  FunctionType *FTy = FunctionType::get(CalleeFTy->getReturnType(), ArgTys,
                                        CalleeFTy->isVarArg());
  Function *F = getEmscriptenFunction(FTy, "__invoke_" + Sig, M);
  InvokeWrappers[Sig] = F;
  return F;
}

static bool canLongjmp(const Value *Callee) {
  if (auto *CalleeF = dyn_cast<Function>(Callee))
    if (CalleeF->isIntrinsic())
      return false;

  // Attempting to transform inline assembly will result in something like:
  //     call void @__invoke_void(void ()* asm ...)
  // which is invalid because inline assembly blocks do not have addresses
  // and can't be passed by pointer. The result is a crash with illegal IR.
  if (isa<InlineAsm>(Callee))
    return false;
  StringRef CalleeName = Callee->getName();

  // TODO Include more functions or consider checking with mangled prefixes

  // The reason we include malloc/free here is to exclude the malloc/free
  // calls generated in setjmp prep / cleanup routines.
  if (CalleeName == "setjmp" || CalleeName == "malloc" || CalleeName == "free")
    return false;

  // There are functions in Emscripten's JS glue code or compiler-rt
  if (CalleeName == "__resumeException" || CalleeName == "llvm_eh_typeid_for" ||
      CalleeName == "__wasm_setjmp" || CalleeName == "__wasm_setjmp_test" ||
      CalleeName == "getTempRet0" || CalleeName == "setTempRet0")
    return false;

  // __cxa_find_matching_catch_N functions cannot longjmp
  if (Callee->getName().starts_with("__cxa_find_matching_catch_"))
    return false;

  // Exception-catching related functions
  //
  // We intentionally treat __cxa_end_catch longjmpable in Wasm SjLj even though
  // it surely cannot longjmp, in order to maintain the unwind relationship from
  // all existing catchpads (and calls within them) to catch.dispatch.longjmp.
  //
  // In Wasm EH + Wasm SjLj, we
  // 1. Make all catchswitch and cleanuppad that unwind to caller unwind to
  //    catch.dispatch.longjmp instead
  // 2. Convert all longjmpable calls to invokes that unwind to
  //    catch.dispatch.longjmp
  // But catchswitch BBs are removed in isel, so if an EH catchswitch (generated
  // from an exception)'s catchpad does not contain any calls that are converted
  // into invokes unwinding to catch.dispatch.longjmp, this unwind relationship
  // (EH catchswitch BB -> catch.dispatch.longjmp BB) is lost and
  // catch.dispatch.longjmp BB can be placed before the EH catchswitch BB in
  // CFGSort.
  // int ret = setjmp(buf);
  // try {
  //   foo(); // longjmps
  // } catch (...) {
  // }
  // Then in this code, if 'foo' longjmps, it first unwinds to 'catch (...)'
  // catchswitch, and is not caught by that catchswitch because it is a longjmp,
  // then it should next unwind to catch.dispatch.longjmp BB. But if this 'catch
  // (...)' catchswitch -> catch.dispatch.longjmp unwind relationship is lost,
  // it will not unwind to catch.dispatch.longjmp, producing an incorrect
  // result.
  //
  // Every catchpad generated by Wasm C++ contains __cxa_end_catch, so we
  // intentionally treat it as longjmpable to work around this problem. This is
  // a hacky fix but an easy one.
  //
  // The comment block in findWasmUnwindDestinations() in
  // SelectionDAGBuilder.cpp is addressing a similar problem.
  if (CalleeName == "__cxa_end_catch")
    return WebAssembly::WasmEnableSjLj;
  if (CalleeName == "__cxa_begin_catch" ||
      CalleeName == "__cxa_allocate_exception" || CalleeName == "__cxa_throw" ||
      CalleeName == "__clang_call_terminate")
    return false;

  // std::terminate, which is generated when another exception occurs while
  // handling an exception, cannot longjmp.
  if (CalleeName == "_ZSt9terminatev")
    return false;

  // Otherwise we don't know
  return true;
}

static bool isEmAsmCall(const Value *Callee) {
  StringRef CalleeName = Callee->getName();
  // This is an exhaustive list from Emscripten's <emscripten/em_asm.h>.
  return CalleeName == "emscripten_asm_const_int" ||
         CalleeName == "emscripten_asm_const_double" ||
         CalleeName == "emscripten_asm_const_int_sync_on_main_thread" ||
         CalleeName == "emscripten_asm_const_double_sync_on_main_thread" ||
         CalleeName == "emscripten_asm_const_async_on_main_thread";
}

// Generate __wasm_setjmp_test function call seqence with preamble and
// postamble. The code this generates is equivalent to the following
// JavaScript code:
// %__threwValue.val = __threwValue;
// if (%__THREW__.val != 0 & %__threwValue.val != 0) {
//   %label = __wasm_setjmp_test(%__THREW__.val, functionInvocationId);
//   if (%label == 0)
//     emscripten_longjmp(%__THREW__.val, %__threwValue.val);
//   setTempRet0(%__threwValue.val);
// } else {
//   %label = -1;
// }
// %longjmp_result = getTempRet0();
//
// As output parameters. returns %label, %longjmp_result, and the BB the last
// instruction (%longjmp_result = ...) is in.
void WebAssemblyLowerEmscriptenEHSjLj::wrapTestSetjmp(
    BasicBlock *BB, DebugLoc DL, Value *Threw, Value *FunctionInvocationId,
    Value *&Label, Value *&LongjmpResult, BasicBlock *&CallEmLongjmpBB,
    PHINode *&CallEmLongjmpBBThrewPHI, PHINode *&CallEmLongjmpBBThrewValuePHI,
    BasicBlock *&EndBB) {
  Function *F = BB->getParent();
  Module *M = F->getParent();
  LLVMContext &C = M->getContext();
  IRBuilder<> IRB(C);
  IRB.SetCurrentDebugLocation(DL);

  // if (%__THREW__.val != 0 & %__threwValue.val != 0)
  IRB.SetInsertPoint(BB);
  BasicBlock *ThenBB1 = BasicBlock::Create(C, "if.then1", F);
  BasicBlock *ElseBB1 = BasicBlock::Create(C, "if.else1", F);
  BasicBlock *EndBB1 = BasicBlock::Create(C, "if.end", F);
  Value *ThrewCmp = IRB.CreateICmpNE(Threw, getAddrSizeInt(M, 0));
  Value *ThrewValue = IRB.CreateLoad(IRB.getInt32Ty(), ThrewValueGV,
                                     ThrewValueGV->getName() + ".val");
  Value *ThrewValueCmp = IRB.CreateICmpNE(ThrewValue, IRB.getInt32(0));
  Value *Cmp1 = IRB.CreateAnd(ThrewCmp, ThrewValueCmp, "cmp1");
  IRB.CreateCondBr(Cmp1, ThenBB1, ElseBB1);

  // Generate call.em.longjmp BB once and share it within the function
  if (!CallEmLongjmpBB) {
    // emscripten_longjmp(%__THREW__.val, %__threwValue.val);
    CallEmLongjmpBB = BasicBlock::Create(C, "call.em.longjmp", F);
    IRB.SetInsertPoint(CallEmLongjmpBB);
    CallEmLongjmpBBThrewPHI = IRB.CreatePHI(getAddrIntType(M), 4, "threw.phi");
    CallEmLongjmpBBThrewValuePHI =
        IRB.CreatePHI(IRB.getInt32Ty(), 4, "threwvalue.phi");
    CallEmLongjmpBBThrewPHI->addIncoming(Threw, ThenBB1);
    CallEmLongjmpBBThrewValuePHI->addIncoming(ThrewValue, ThenBB1);
    IRB.CreateCall(EmLongjmpF,
                   {CallEmLongjmpBBThrewPHI, CallEmLongjmpBBThrewValuePHI});
    IRB.CreateUnreachable();
  } else {
    CallEmLongjmpBBThrewPHI->addIncoming(Threw, ThenBB1);
    CallEmLongjmpBBThrewValuePHI->addIncoming(ThrewValue, ThenBB1);
  }

  // %label = __wasm_setjmp_test(%__THREW__.val, functionInvocationId);
  // if (%label == 0)
  IRB.SetInsertPoint(ThenBB1);
  BasicBlock *EndBB2 = BasicBlock::Create(C, "if.end2", F);
  Value *ThrewPtr =
      IRB.CreateIntToPtr(Threw, getAddrPtrType(M), Threw->getName() + ".p");
  Value *ThenLabel = IRB.CreateCall(WasmSetjmpTestF,
                                    {ThrewPtr, FunctionInvocationId}, "label");
  Value *Cmp2 = IRB.CreateICmpEQ(ThenLabel, IRB.getInt32(0));
  IRB.CreateCondBr(Cmp2, CallEmLongjmpBB, EndBB2);

  // setTempRet0(%__threwValue.val);
  IRB.SetInsertPoint(EndBB2);
  IRB.CreateCall(SetTempRet0F, ThrewValue);
  IRB.CreateBr(EndBB1);

  IRB.SetInsertPoint(ElseBB1);
  IRB.CreateBr(EndBB1);

  // longjmp_result = getTempRet0();
  IRB.SetInsertPoint(EndBB1);
  PHINode *LabelPHI = IRB.CreatePHI(IRB.getInt32Ty(), 2, "label");
  LabelPHI->addIncoming(ThenLabel, EndBB2);

  LabelPHI->addIncoming(IRB.getInt32(-1), ElseBB1);

  // Output parameter assignment
  Label = LabelPHI;
  EndBB = EndBB1;
  LongjmpResult = IRB.CreateCall(GetTempRet0F, std::nullopt, "longjmp_result");
}

void WebAssemblyLowerEmscriptenEHSjLj::rebuildSSA(Function &F) {
  DominatorTree &DT = getAnalysis<DominatorTreeWrapperPass>(F).getDomTree();
  DT.recalculate(F); // CFG has been changed

  SSAUpdaterBulk SSA;
  for (BasicBlock &BB : F) {
    for (Instruction &I : BB) {
      unsigned VarID = SSA.AddVariable(I.getName(), I.getType());
      // If a value is defined by an invoke instruction, it is only available in
      // its normal destination and not in its unwind destination.
      if (auto *II = dyn_cast<InvokeInst>(&I))
        SSA.AddAvailableValue(VarID, II->getNormalDest(), II);
      else
        SSA.AddAvailableValue(VarID, &BB, &I);
      for (auto &U : I.uses()) {
        auto *User = cast<Instruction>(U.getUser());
        if (auto *UserPN = dyn_cast<PHINode>(User))
          if (UserPN->getIncomingBlock(U) == &BB)
            continue;
        if (DT.dominates(&I, User))
          continue;
        SSA.AddUse(VarID, &U);
      }
    }
  }
  SSA.RewriteAllUses(&DT);
}

// Replace uses of longjmp with a new longjmp function in Emscripten library.
// In Emscripten SjLj, the new function is
//   void emscripten_longjmp(uintptr_t, i32)
// In Wasm SjLj, the new function is
//   void __wasm_longjmp(i8*, i32)
// Because the original libc longjmp function takes (jmp_buf*, i32), we need a
// ptrtoint/bitcast instruction here to make the type match. jmp_buf* will
// eventually be lowered to i32/i64 in the wasm backend.
void WebAssemblyLowerEmscriptenEHSjLj::replaceLongjmpWith(Function *LongjmpF,
                                                          Function *NewF) {
  assert(NewF == EmLongjmpF || NewF == WasmLongjmpF);
  Module *M = LongjmpF->getParent();
  SmallVector<CallInst *, 8> ToErase;
  LLVMContext &C = LongjmpF->getParent()->getContext();
  IRBuilder<> IRB(C);

  // For calls to longjmp, replace it with emscripten_longjmp/__wasm_longjmp and
  // cast its first argument (jmp_buf*) appropriately
  for (User *U : LongjmpF->users()) {
    auto *CI = dyn_cast<CallInst>(U);
    if (CI && CI->getCalledFunction() == LongjmpF) {
      IRB.SetInsertPoint(CI);
      Value *Env = nullptr;
      if (NewF == EmLongjmpF)
        Env =
            IRB.CreatePtrToInt(CI->getArgOperand(0), getAddrIntType(M), "env");
      else // WasmLongjmpF
        Env = IRB.CreateBitCast(CI->getArgOperand(0), IRB.getPtrTy(), "env");
      IRB.CreateCall(NewF, {Env, CI->getArgOperand(1)});
      ToErase.push_back(CI);
    }
  }
  for (auto *I : ToErase)
    I->eraseFromParent();

  // If we have any remaining uses of longjmp's function pointer, replace it
  // with (void(*)(jmp_buf*, int))emscripten_longjmp / __wasm_longjmp.
  if (!LongjmpF->uses().empty()) {
    Value *NewLongjmp =
        IRB.CreateBitCast(NewF, LongjmpF->getType(), "longjmp.cast");
    LongjmpF->replaceAllUsesWith(NewLongjmp);
  }
}

static bool containsLongjmpableCalls(const Function *F) {
  for (const auto &BB : *F)
    for (const auto &I : BB)
      if (const auto *CB = dyn_cast<CallBase>(&I))
        if (canLongjmp(CB->getCalledOperand()))
          return true;
  return false;
}

// When a function contains a setjmp call but not other calls that can longjmp,
// we don't do setjmp transformation for that setjmp. But we need to convert the
// setjmp calls into "i32 0" so they don't cause link time errors. setjmp always
// returns 0 when called directly.
static void nullifySetjmp(Function *F) {
  Module &M = *F->getParent();
  IRBuilder<> IRB(M.getContext());
  Function *SetjmpF = M.getFunction("setjmp");
  SmallVector<Instruction *, 1> ToErase;

  for (User *U : make_early_inc_range(SetjmpF->users())) {
    auto *CB = cast<CallBase>(U);
    BasicBlock *BB = CB->getParent();
    if (BB->getParent() != F) // in other function
      continue;
    CallInst *CI = nullptr;
    // setjmp cannot throw. So if it is an invoke, lower it to a call
    if (auto *II = dyn_cast<InvokeInst>(CB))
      CI = llvm::changeToCall(II);
    else
      CI = cast<CallInst>(CB);
    ToErase.push_back(CI);
    CI->replaceAllUsesWith(IRB.getInt32(0));
  }
  for (auto *I : ToErase)
    I->eraseFromParent();
}

bool WebAssemblyLowerEmscriptenEHSjLj::runOnModule(Module &M) {
  LLVM_DEBUG(dbgs() << "********** Lower Emscripten EH & SjLj **********\n");

  LLVMContext &C = M.getContext();
  IRBuilder<> IRB(C);

  Function *SetjmpF = M.getFunction("setjmp");
  Function *LongjmpF = M.getFunction("longjmp");

  // In some platforms _setjmp and _longjmp are used instead. Change these to
  // use setjmp/longjmp instead, because we later detect these functions by
  // their names.
  Function *SetjmpF2 = M.getFunction("_setjmp");
  Function *LongjmpF2 = M.getFunction("_longjmp");
  if (SetjmpF2) {
    if (SetjmpF) {
      if (SetjmpF->getFunctionType() != SetjmpF2->getFunctionType())
        report_fatal_error("setjmp and _setjmp have different function types");
    } else {
      SetjmpF = Function::Create(SetjmpF2->getFunctionType(),
                                 GlobalValue::ExternalLinkage, "setjmp", M);
    }
    SetjmpF2->replaceAllUsesWith(SetjmpF);
  }
  if (LongjmpF2) {
    if (LongjmpF) {
      if (LongjmpF->getFunctionType() != LongjmpF2->getFunctionType())
        report_fatal_error(
            "longjmp and _longjmp have different function types");
    } else {
      LongjmpF = Function::Create(LongjmpF2->getFunctionType(),
                                  GlobalValue::ExternalLinkage, "setjmp", M);
    }
    LongjmpF2->replaceAllUsesWith(LongjmpF);
  }

  auto *TPC = getAnalysisIfAvailable<TargetPassConfig>();
  assert(TPC && "Expected a TargetPassConfig");
  auto &TM = TPC->getTM<WebAssemblyTargetMachine>();

  // Declare (or get) global variables __THREW__, __threwValue, and
  // getTempRet0/setTempRet0 function which are used in common for both
  // exception handling and setjmp/longjmp handling
  ThrewGV = getGlobalVariable(M, getAddrIntType(&M), TM, "__THREW__");
  ThrewValueGV = getGlobalVariable(M, IRB.getInt32Ty(), TM, "__threwValue");
  GetTempRet0F = getEmscriptenFunction(
      FunctionType::get(IRB.getInt32Ty(), false), "getTempRet0", &M);
  SetTempRet0F = getEmscriptenFunction(
      FunctionType::get(IRB.getVoidTy(), IRB.getInt32Ty(), false),
      "setTempRet0", &M);
  GetTempRet0F->setDoesNotThrow();
  SetTempRet0F->setDoesNotThrow();

  bool Changed = false;

  // Function registration for exception handling
  if (EnableEmEH) {
    // Register __resumeException function
    FunctionType *ResumeFTy =
        FunctionType::get(IRB.getVoidTy(), IRB.getPtrTy(), false);
    ResumeF = getEmscriptenFunction(ResumeFTy, "__resumeException", &M);
    ResumeF->addFnAttr(Attribute::NoReturn);

    // Register llvm_eh_typeid_for function
    FunctionType *EHTypeIDTy =
        FunctionType::get(IRB.getInt32Ty(), IRB.getPtrTy(), false);
    EHTypeIDF = getEmscriptenFunction(EHTypeIDTy, "llvm_eh_typeid_for", &M);
  }

  // Functions that contains calls to setjmp but don't have other longjmpable
  // calls within them.
  SmallPtrSet<Function *, 4> SetjmpUsersToNullify;

  if ((EnableEmSjLj || EnableWasmSjLj) && SetjmpF) {
    // Precompute setjmp users
    for (User *U : SetjmpF->users()) {
      if (auto *CB = dyn_cast<CallBase>(U)) {
        auto *UserF = CB->getFunction();
        // If a function that calls setjmp does not contain any other calls that
        // can longjmp, we don't need to do any transformation on that function,
        // so can ignore it
        if (containsLongjmpableCalls(UserF))
          SetjmpUsers.insert(UserF);
        else
          SetjmpUsersToNullify.insert(UserF);
      } else {
        std::string S;
        raw_string_ostream SS(S);
        SS << *U;
        report_fatal_error(Twine("Indirect use of setjmp is not supported: ") +
                           SS.str());
      }
    }
  }

  bool SetjmpUsed = SetjmpF && !SetjmpUsers.empty();
  bool LongjmpUsed = LongjmpF && !LongjmpF->use_empty();
  DoSjLj = (EnableEmSjLj | EnableWasmSjLj) && (SetjmpUsed || LongjmpUsed);

  // Function registration and data pre-gathering for setjmp/longjmp handling
  if (DoSjLj) {
    assert(EnableEmSjLj || EnableWasmSjLj);
    if (EnableEmSjLj) {
      // Register emscripten_longjmp function
      FunctionType *FTy = FunctionType::get(
          IRB.getVoidTy(), {getAddrIntType(&M), IRB.getInt32Ty()}, false);
      EmLongjmpF = getEmscriptenFunction(FTy, "emscripten_longjmp", &M);
      EmLongjmpF->addFnAttr(Attribute::NoReturn);
    } else { // EnableWasmSjLj
      Type *Int8PtrTy = IRB.getPtrTy();
      // Register __wasm_longjmp function, which calls __builtin_wasm_longjmp.
      FunctionType *FTy = FunctionType::get(
          IRB.getVoidTy(), {Int8PtrTy, IRB.getInt32Ty()}, false);
      WasmLongjmpF = getEmscriptenFunction(FTy, "__wasm_longjmp", &M);
      WasmLongjmpF->addFnAttr(Attribute::NoReturn);
    }

    if (SetjmpF) {
      Type *Int8PtrTy = IRB.getPtrTy();
      Type *Int32PtrTy = IRB.getPtrTy();
      Type *Int32Ty = IRB.getInt32Ty();

      // Register __wasm_setjmp function
      FunctionType *SetjmpFTy = SetjmpF->getFunctionType();
      FunctionType *FTy = FunctionType::get(
          IRB.getVoidTy(), {SetjmpFTy->getParamType(0), Int32Ty, Int32PtrTy},
          false);
      WasmSetjmpF = getEmscriptenFunction(FTy, "__wasm_setjmp", &M);

      // Register __wasm_setjmp_test function
      FTy = FunctionType::get(Int32Ty, {Int32PtrTy, Int32PtrTy}, false);
      WasmSetjmpTestF = getEmscriptenFunction(FTy, "__wasm_setjmp_test", &M);

      // wasm.catch() will be lowered down to wasm 'catch' instruction in
      // instruction selection.
      CatchF = Intrinsic::getDeclaration(&M, Intrinsic::wasm_catch);
      // Type for struct __WasmLongjmpArgs
      LongjmpArgsTy = StructType::get(Int8PtrTy, // env
                                      Int32Ty    // val
      );
    }
  }

  // Exception handling transformation
  if (EnableEmEH) {
    for (Function &F : M) {
      if (F.isDeclaration())
        continue;
      Changed |= runEHOnFunction(F);
    }
  }

  // Setjmp/longjmp handling transformation
  if (DoSjLj) {
    Changed = true; // We have setjmp or longjmp somewhere
    if (LongjmpF)
      replaceLongjmpWith(LongjmpF, EnableEmSjLj ? EmLongjmpF : WasmLongjmpF);
    // Only traverse functions that uses setjmp in order not to insert
    // unnecessary prep / cleanup code in every function
    if (SetjmpF)
      for (Function *F : SetjmpUsers)
        runSjLjOnFunction(*F);
  }

  // Replace unnecessary setjmp calls with 0
  if ((EnableEmSjLj || EnableWasmSjLj) && !SetjmpUsersToNullify.empty()) {
    Changed = true;
    assert(SetjmpF);
    for (Function *F : SetjmpUsersToNullify)
      nullifySetjmp(F);
  }

  // Delete unused global variables and functions
  for (auto *V : {ThrewGV, ThrewValueGV})
    if (V && V->use_empty())
      V->eraseFromParent();
  for (auto *V : {GetTempRet0F, SetTempRet0F, ResumeF, EHTypeIDF, EmLongjmpF,
                  WasmSetjmpF, WasmSetjmpTestF, WasmLongjmpF, CatchF})
    if (V && V->use_empty())
      V->eraseFromParent();

  return Changed;
}

bool WebAssemblyLowerEmscriptenEHSjLj::runEHOnFunction(Function &F) {
  Module &M = *F.getParent();
  LLVMContext &C = F.getContext();
  IRBuilder<> IRB(C);
  bool Changed = false;
  SmallVector<Instruction *, 64> ToErase;
  SmallPtrSet<LandingPadInst *, 32> LandingPads;

  // rethrow.longjmp BB that will be shared within the function.
  BasicBlock *RethrowLongjmpBB = nullptr;
  // PHI node for the loaded value of __THREW__ global variable in
  // rethrow.longjmp BB
  PHINode *RethrowLongjmpBBThrewPHI = nullptr;

  for (BasicBlock &BB : F) {
    auto *II = dyn_cast<InvokeInst>(BB.getTerminator());
    if (!II)
      continue;
    Changed = true;
    LandingPads.insert(II->getLandingPadInst());
    IRB.SetInsertPoint(II);

    const Value *Callee = II->getCalledOperand();
    bool NeedInvoke = supportsException(&F) && canThrow(Callee);
    if (NeedInvoke) {
      // Wrap invoke with invoke wrapper and generate preamble/postamble
      Value *Threw = wrapInvoke(II);
      ToErase.push_back(II);

      // If setjmp/longjmp handling is enabled, the thrown value can be not an
      // exception but a longjmp. If the current function contains calls to
      // setjmp, it will be appropriately handled in runSjLjOnFunction. But even
      // if the function does not contain setjmp calls, we shouldn't silently
      // ignore longjmps; we should rethrow them so they can be correctly
      // handled in somewhere up the call chain where setjmp is. __THREW__'s
      // value is 0 when nothing happened, 1 when an exception is thrown, and
      // other values when longjmp is thrown.
      //
      // if (%__THREW__.val == 0 || %__THREW__.val == 1)
      //   goto %tail
      // else
      //   goto %longjmp.rethrow
      //
      // rethrow.longjmp: ;; This is longjmp. Rethrow it
      //   %__threwValue.val = __threwValue
      //   emscripten_longjmp(%__THREW__.val, %__threwValue.val);
      //
      // tail: ;; Nothing happened or an exception is thrown
      //   ... Continue exception handling ...
      if (DoSjLj && EnableEmSjLj && !SetjmpUsers.count(&F) &&
          canLongjmp(Callee)) {
        // Create longjmp.rethrow BB once and share it within the function
        if (!RethrowLongjmpBB) {
          RethrowLongjmpBB = BasicBlock::Create(C, "rethrow.longjmp", &F);
          IRB.SetInsertPoint(RethrowLongjmpBB);
          RethrowLongjmpBBThrewPHI =
              IRB.CreatePHI(getAddrIntType(&M), 4, "threw.phi");
          RethrowLongjmpBBThrewPHI->addIncoming(Threw, &BB);
          Value *ThrewValue = IRB.CreateLoad(IRB.getInt32Ty(), ThrewValueGV,
                                             ThrewValueGV->getName() + ".val");
          IRB.CreateCall(EmLongjmpF, {RethrowLongjmpBBThrewPHI, ThrewValue});
          IRB.CreateUnreachable();
        } else {
          RethrowLongjmpBBThrewPHI->addIncoming(Threw, &BB);
        }

        IRB.SetInsertPoint(II); // Restore the insert point back
        BasicBlock *Tail = BasicBlock::Create(C, "tail", &F);
        Value *CmpEqOne =
            IRB.CreateICmpEQ(Threw, getAddrSizeInt(&M, 1), "cmp.eq.one");
        Value *CmpEqZero =
            IRB.CreateICmpEQ(Threw, getAddrSizeInt(&M, 0), "cmp.eq.zero");
        Value *Or = IRB.CreateOr(CmpEqZero, CmpEqOne, "or");
        IRB.CreateCondBr(Or, Tail, RethrowLongjmpBB);
        IRB.SetInsertPoint(Tail);
        BB.replaceSuccessorsPhiUsesWith(&BB, Tail);
      }

      // Insert a branch based on __THREW__ variable
      Value *Cmp = IRB.CreateICmpEQ(Threw, getAddrSizeInt(&M, 1), "cmp");
      IRB.CreateCondBr(Cmp, II->getUnwindDest(), II->getNormalDest());

    } else {
      // This can't throw, and we don't need this invoke, just replace it with a
      // call+branch
      changeToCall(II);
    }
  }

  // Process resume instructions
  for (BasicBlock &BB : F) {
    // Scan the body of the basic block for resumes
    for (Instruction &I : BB) {
      auto *RI = dyn_cast<ResumeInst>(&I);
      if (!RI)
        continue;
      Changed = true;

      // Split the input into legal values
      Value *Input = RI->getValue();
      IRB.SetInsertPoint(RI);
      Value *Low = IRB.CreateExtractValue(Input, 0, "low");
      // Create a call to __resumeException function
      IRB.CreateCall(ResumeF, {Low});
      // Add a terminator to the block
      IRB.CreateUnreachable();
      ToErase.push_back(RI);
    }
  }

  // Process llvm.eh.typeid.for intrinsics
  for (BasicBlock &BB : F) {
    for (Instruction &I : BB) {
      auto *CI = dyn_cast<CallInst>(&I);
      if (!CI)
        continue;
      const Function *Callee = CI->getCalledFunction();
      if (!Callee)
        continue;
      if (Callee->getIntrinsicID() != Intrinsic::eh_typeid_for)
        continue;
      Changed = true;

      IRB.SetInsertPoint(CI);
      CallInst *NewCI =
          IRB.CreateCall(EHTypeIDF, CI->getArgOperand(0), "typeid");
      CI->replaceAllUsesWith(NewCI);
      ToErase.push_back(CI);
    }
  }

  // Look for orphan landingpads, can occur in blocks with no predecessors
  for (BasicBlock &BB : F) {
    Instruction *I = BB.getFirstNonPHI();
    if (auto *LPI = dyn_cast<LandingPadInst>(I))
      LandingPads.insert(LPI);
  }
  Changed |= !LandingPads.empty();

  // Handle all the landingpad for this function together, as multiple invokes
  // may share a single lp
  for (LandingPadInst *LPI : LandingPads) {
    IRB.SetInsertPoint(LPI);
    SmallVector<Value *, 16> FMCArgs;
    for (unsigned I = 0, E = LPI->getNumClauses(); I < E; ++I) {
      Constant *Clause = LPI->getClause(I);
      // TODO Handle filters (= exception specifications).
      // https://github.com/llvm/llvm-project/issues/49740
      if (LPI->isCatch(I))
        FMCArgs.push_back(Clause);
    }

    // Create a call to __cxa_find_matching_catch_N function
    Function *FMCF = getFindMatchingCatch(M, FMCArgs.size());
    CallInst *FMCI = IRB.CreateCall(FMCF, FMCArgs, "fmc");
    Value *Poison = PoisonValue::get(LPI->getType());
    Value *Pair0 = IRB.CreateInsertValue(Poison, FMCI, 0, "pair0");
    Value *TempRet0 = IRB.CreateCall(GetTempRet0F, std::nullopt, "tempret0");
    Value *Pair1 = IRB.CreateInsertValue(Pair0, TempRet0, 1, "pair1");

    LPI->replaceAllUsesWith(Pair1);
    ToErase.push_back(LPI);
  }

  // Erase everything we no longer need in this function
  for (Instruction *I : ToErase)
    I->eraseFromParent();

  return Changed;
}

// This tries to get debug info from the instruction before which a new
// instruction will be inserted, and if there's no debug info in that
// instruction, tries to get the info instead from the previous instruction (if
// any). If none of these has debug info and a DISubprogram is provided, it
// creates a dummy debug info with the first line of the function, because IR
// verifier requires all inlinable callsites should have debug info when both a
// caller and callee have DISubprogram. If none of these conditions are met,
// returns empty info.
static DebugLoc getOrCreateDebugLoc(const Instruction *InsertBefore,
                                    DISubprogram *SP) {
  assert(InsertBefore);
  if (InsertBefore->getDebugLoc())
    return InsertBefore->getDebugLoc();
  const Instruction *Prev = InsertBefore->getPrevNode();
  if (Prev && Prev->getDebugLoc())
    return Prev->getDebugLoc();
  if (SP)
    return DILocation::get(SP->getContext(), SP->getLine(), 1, SP);
  return DebugLoc();
}

bool WebAssemblyLowerEmscriptenEHSjLj::runSjLjOnFunction(Function &F) {
  assert(EnableEmSjLj || EnableWasmSjLj);
  Module &M = *F.getParent();
  LLVMContext &C = F.getContext();
  IRBuilder<> IRB(C);
  SmallVector<Instruction *, 64> ToErase;

  // Setjmp preparation

  BasicBlock *Entry = &F.getEntryBlock();
  DebugLoc FirstDL = getOrCreateDebugLoc(&*Entry->begin(), F.getSubprogram());
  SplitBlock(Entry, &*Entry->getFirstInsertionPt());

  IRB.SetInsertPoint(Entry->getTerminator()->getIterator());
  // This alloca'ed pointer is used by the runtime to identify function
  // invocations. It's just for pointer comparisons. It will never be
  // dereferenced.
  Instruction *FunctionInvocationId =
      IRB.CreateAlloca(IRB.getInt32Ty(), nullptr, "functionInvocationId");
  FunctionInvocationId->setDebugLoc(FirstDL);

  // Setjmp transformation
  SmallVector<PHINode *, 4> SetjmpRetPHIs;
  Function *SetjmpF = M.getFunction("setjmp");
  for (auto *U : make_early_inc_range(SetjmpF->users())) {
    auto *CB = cast<CallBase>(U);
    BasicBlock *BB = CB->getParent();
    if (BB->getParent() != &F) // in other function
      continue;
    if (CB->getOperandBundle(LLVMContext::OB_funclet)) {
      std::string S;
      raw_string_ostream SS(S);
      SS << "In function " + F.getName() +
                ": setjmp within a catch clause is not supported in Wasm EH:\n";
      SS << *CB;
      report_fatal_error(StringRef(SS.str()));
    }

    CallInst *CI = nullptr;
    // setjmp cannot throw. So if it is an invoke, lower it to a call
    if (auto *II = dyn_cast<InvokeInst>(CB))
      CI = llvm::changeToCall(II);
    else
      CI = cast<CallInst>(CB);

    // The tail is everything right after the call, and will be reached once
    // when setjmp is called, and later when longjmp returns to the setjmp
    BasicBlock *Tail = SplitBlock(BB, CI->getNextNode());
    // Add a phi to the tail, which will be the output of setjmp, which
    // indicates if this is the first call or a longjmp back. The phi directly
    // uses the right value based on where we arrive from
    IRB.SetInsertPoint(Tail, Tail->getFirstNonPHIIt());
    PHINode *SetjmpRet = IRB.CreatePHI(IRB.getInt32Ty(), 2, "setjmp.ret");

    // setjmp initial call returns 0
    SetjmpRet->addIncoming(IRB.getInt32(0), BB);
    // The proper output is now this, not the setjmp call itself
    CI->replaceAllUsesWith(SetjmpRet);
    // longjmp returns to the setjmp will add themselves to this phi
    SetjmpRetPHIs.push_back(SetjmpRet);

    // Fix call target
    // Our index in the function is our place in the array + 1 to avoid index
    // 0, because index 0 means the longjmp is not ours to handle.
    IRB.SetInsertPoint(CI);
    Value *Args[] = {CI->getArgOperand(0), IRB.getInt32(SetjmpRetPHIs.size()),
                     FunctionInvocationId};
    IRB.CreateCall(WasmSetjmpF, Args);
    ToErase.push_back(CI);
  }

  // Handle longjmpable calls.
  if (EnableEmSjLj)
    handleLongjmpableCallsForEmscriptenSjLj(F, FunctionInvocationId,
                                            SetjmpRetPHIs);
  else // EnableWasmSjLj
    handleLongjmpableCallsForWasmSjLj(F, FunctionInvocationId, SetjmpRetPHIs);

  // Erase everything we no longer need in this function
  for (Instruction *I : ToErase)
    I->eraseFromParent();

  // Finally, our modifications to the cfg can break dominance of SSA variables.
  // For example, in this code,
  // if (x()) { .. setjmp() .. }
  // if (y()) { .. longjmp() .. }
  // We must split the longjmp block, and it can jump into the block splitted
  // from setjmp one. But that means that when we split the setjmp block, it's
  // first part no longer dominates its second part - there is a theoretically
  // possible control flow path where x() is false, then y() is true and we
  // reach the second part of the setjmp block, without ever reaching the first
  // part. So, we rebuild SSA form here.
  rebuildSSA(F);
  return true;
}

// Update each call that can longjmp so it can return to the corresponding
// setjmp. Refer to 4) of "Emscripten setjmp/longjmp handling" section in the
// comments at top of the file for details.
void WebAssemblyLowerEmscriptenEHSjLj::handleLongjmpableCallsForEmscriptenSjLj(
    Function &F, Instruction *FunctionInvocationId,
    SmallVectorImpl<PHINode *> &SetjmpRetPHIs) {
  Module &M = *F.getParent();
  LLVMContext &C = F.getContext();
  IRBuilder<> IRB(C);
  SmallVector<Instruction *, 64> ToErase;

  // call.em.longjmp BB that will be shared within the function.
  BasicBlock *CallEmLongjmpBB = nullptr;
  // PHI node for the loaded value of __THREW__ global variable in
  // call.em.longjmp BB
  PHINode *CallEmLongjmpBBThrewPHI = nullptr;
  // PHI node for the loaded value of __threwValue global variable in
  // call.em.longjmp BB
  PHINode *CallEmLongjmpBBThrewValuePHI = nullptr;
  // rethrow.exn BB that will be shared within the function.
  BasicBlock *RethrowExnBB = nullptr;

  // Because we are creating new BBs while processing and don't want to make
  // all these newly created BBs candidates again for longjmp processing, we
  // first make the vector of candidate BBs.
  std::vector<BasicBlock *> BBs;
  for (BasicBlock &BB : F)
    BBs.push_back(&BB);

  // BBs.size() will change within the loop, so we query it every time
  for (unsigned I = 0; I < BBs.size(); I++) {
    BasicBlock *BB = BBs[I];
    for (Instruction &I : *BB) {
      if (isa<InvokeInst>(&I)) {
        std::string S;
        raw_string_ostream SS(S);
        SS << "In function " << F.getName()
           << ": When using Wasm EH with Emscripten SjLj, there is a "
              "restriction that `setjmp` function call and exception cannot be "
              "used within the same function:\n";
        SS << I;
        report_fatal_error(StringRef(SS.str()));
      }
      auto *CI = dyn_cast<CallInst>(&I);
      if (!CI)
        continue;

      const Value *Callee = CI->getCalledOperand();
      if (!canLongjmp(Callee))
        continue;
      if (isEmAsmCall(Callee))
        report_fatal_error("Cannot use EM_ASM* alongside setjmp/longjmp in " +
                               F.getName() +
                               ". Please consider using EM_JS, or move the "
                               "EM_ASM into another function.",
                           false);

      Value *Threw = nullptr;
      BasicBlock *Tail;
      if (Callee->getName().starts_with("__invoke_")) {
        // If invoke wrapper has already been generated for this call in
        // previous EH phase, search for the load instruction
        // %__THREW__.val = __THREW__;
        // in postamble after the invoke wrapper call
        LoadInst *ThrewLI = nullptr;
        StoreInst *ThrewResetSI = nullptr;
        for (auto I = std::next(BasicBlock::iterator(CI)), IE = BB->end();
             I != IE; ++I) {
          if (auto *LI = dyn_cast<LoadInst>(I))
            if (auto *GV = dyn_cast<GlobalVariable>(LI->getPointerOperand()))
              if (GV == ThrewGV) {
                Threw = ThrewLI = LI;
                break;
              }
        }
        // Search for the store instruction after the load above
        // __THREW__ = 0;
        for (auto I = std::next(BasicBlock::iterator(ThrewLI)), IE = BB->end();
             I != IE; ++I) {
          if (auto *SI = dyn_cast<StoreInst>(I)) {
            if (auto *GV = dyn_cast<GlobalVariable>(SI->getPointerOperand())) {
              if (GV == ThrewGV &&
                  SI->getValueOperand() == getAddrSizeInt(&M, 0)) {
                ThrewResetSI = SI;
                break;
              }
            }
          }
        }
        assert(Threw && ThrewLI && "Cannot find __THREW__ load after invoke");
        assert(ThrewResetSI && "Cannot find __THREW__ store after invoke");
        Tail = SplitBlock(BB, ThrewResetSI->getNextNode());

      } else {
        // Wrap call with invoke wrapper and generate preamble/postamble
        Threw = wrapInvoke(CI);
        ToErase.push_back(CI);
        Tail = SplitBlock(BB, CI->getNextNode());

        // If exception handling is enabled, the thrown value can be not a
        // longjmp but an exception, in which case we shouldn't silently ignore
        // exceptions; we should rethrow them.
        // __THREW__'s value is 0 when nothing happened, 1 when an exception is
        // thrown, other values when longjmp is thrown.
        //
        // if (%__THREW__.val == 1)
        //   goto %eh.rethrow
        // else
        //   goto %normal
        //
        // eh.rethrow: ;; Rethrow exception
        //   %exn = call @__cxa_find_matching_catch_2() ;; Retrieve thrown ptr
        //   __resumeException(%exn)
        //
        // normal:
        //   <-- Insertion point. Will insert sjlj handling code from here
        //   goto %tail
        //
        // tail:
        //   ...
        if (supportsException(&F) && canThrow(Callee)) {
          // We will add a new conditional branch. So remove the branch created
          // when we split the BB
          ToErase.push_back(BB->getTerminator());

          // Generate rethrow.exn BB once and share it within the function
          if (!RethrowExnBB) {
            RethrowExnBB = BasicBlock::Create(C, "rethrow.exn", &F);
            IRB.SetInsertPoint(RethrowExnBB);
            CallInst *Exn =
                IRB.CreateCall(getFindMatchingCatch(M, 0), {}, "exn");
            IRB.CreateCall(ResumeF, {Exn});
            IRB.CreateUnreachable();
          }

          IRB.SetInsertPoint(CI);
          BasicBlock *NormalBB = BasicBlock::Create(C, "normal", &F);
          Value *CmpEqOne =
              IRB.CreateICmpEQ(Threw, getAddrSizeInt(&M, 1), "cmp.eq.one");
          IRB.CreateCondBr(CmpEqOne, RethrowExnBB, NormalBB);

          IRB.SetInsertPoint(NormalBB);
          IRB.CreateBr(Tail);
          BB = NormalBB; // New insertion point to insert __wasm_setjmp_test()
        }
      }

      // We need to replace the terminator in Tail - SplitBlock makes BB go
      // straight to Tail, we need to check if a longjmp occurred, and go to the
      // right setjmp-tail if so
      ToErase.push_back(BB->getTerminator());

      // Generate a function call to __wasm_setjmp_test function and
      // preamble/postamble code to figure out (1) whether longjmp
      // occurred (2) if longjmp occurred, which setjmp it corresponds to
      Value *Label = nullptr;
      Value *LongjmpResult = nullptr;
      BasicBlock *EndBB = nullptr;
      wrapTestSetjmp(BB, CI->getDebugLoc(), Threw, FunctionInvocationId, Label,
                     LongjmpResult, CallEmLongjmpBB, CallEmLongjmpBBThrewPHI,
                     CallEmLongjmpBBThrewValuePHI, EndBB);
      assert(Label && LongjmpResult && EndBB);

      // Create switch instruction
      IRB.SetInsertPoint(EndBB);
      IRB.SetCurrentDebugLocation(EndBB->back().getDebugLoc());
      SwitchInst *SI = IRB.CreateSwitch(Label, Tail, SetjmpRetPHIs.size());
      // -1 means no longjmp happened, continue normally (will hit the default
      // switch case). 0 means a longjmp that is not ours to handle, needs a
      // rethrow. Otherwise the index is the same as the index in P+1 (to avoid
      // 0).
      for (unsigned I = 0; I < SetjmpRetPHIs.size(); I++) {
        SI->addCase(IRB.getInt32(I + 1), SetjmpRetPHIs[I]->getParent());
        SetjmpRetPHIs[I]->addIncoming(LongjmpResult, EndBB);
      }

      // We are splitting the block here, and must continue to find other calls
      // in the block - which is now split. so continue to traverse in the Tail
      BBs.push_back(Tail);
    }
  }

  for (Instruction *I : ToErase)
    I->eraseFromParent();
}

static BasicBlock *getCleanupRetUnwindDest(const CleanupPadInst *CPI) {
  for (const User *U : CPI->users())
    if (const auto *CRI = dyn_cast<CleanupReturnInst>(U))
      return CRI->getUnwindDest();
  return nullptr;
}

// Create a catchpad in which we catch a longjmp's env and val arguments, test
// if the longjmp corresponds to one of setjmps in the current function, and if
// so, jump to the setjmp dispatch BB from which we go to one of post-setjmp
// BBs. Refer to 4) of "Wasm setjmp/longjmp handling" section in the comments at
// top of the file for details.
void WebAssemblyLowerEmscriptenEHSjLj::handleLongjmpableCallsForWasmSjLj(
    Function &F, Instruction *FunctionInvocationId,
    SmallVectorImpl<PHINode *> &SetjmpRetPHIs) {
  Module &M = *F.getParent();
  LLVMContext &C = F.getContext();
  IRBuilder<> IRB(C);

  // A function with catchswitch/catchpad instruction should have a personality
  // function attached to it. Search for the wasm personality function, and if
  // it exists, use it, and if it doesn't, create a dummy personality function.
  // (SjLj is not going to call it anyway.)
  if (!F.hasPersonalityFn()) {
    StringRef PersName = getEHPersonalityName(EHPersonality::Wasm_CXX);
    FunctionType *PersType =
        FunctionType::get(IRB.getInt32Ty(), /* isVarArg */ true);
    Value *PersF = M.getOrInsertFunction(PersName, PersType).getCallee();
    F.setPersonalityFn(
        cast<Constant>(IRB.CreateBitCast(PersF, IRB.getPtrTy())));
  }

  // Use the entry BB's debugloc as a fallback
  BasicBlock *Entry = &F.getEntryBlock();
  DebugLoc FirstDL = getOrCreateDebugLoc(&*Entry->begin(), F.getSubprogram());
  IRB.SetCurrentDebugLocation(FirstDL);

  // Add setjmp.dispatch BB right after the entry block. Because we have
  // initialized functionInvocationId in the entry block and split the
  // rest into another BB, here 'OrigEntry' is the function's original entry
  // block before the transformation.
  //
  // entry:
  //   functionInvocationId initialization
  // setjmp.dispatch:
  //   switch will be inserted here later
  // entry.split: (OrigEntry)
  //   the original function starts here
  BasicBlock *OrigEntry = Entry->getNextNode();
  BasicBlock *SetjmpDispatchBB =
      BasicBlock::Create(C, "setjmp.dispatch", &F, OrigEntry);
  cast<BranchInst>(Entry->getTerminator())->setSuccessor(0, SetjmpDispatchBB);

  // Create catch.dispatch.longjmp BB and a catchswitch instruction
  BasicBlock *CatchDispatchLongjmpBB =
      BasicBlock::Create(C, "catch.dispatch.longjmp", &F);
  IRB.SetInsertPoint(CatchDispatchLongjmpBB);
  CatchSwitchInst *CatchSwitchLongjmp =
      IRB.CreateCatchSwitch(ConstantTokenNone::get(C), nullptr, 1);

  // Create catch.longjmp BB and a catchpad instruction
  BasicBlock *CatchLongjmpBB = BasicBlock::Create(C, "catch.longjmp", &F);
  CatchSwitchLongjmp->addHandler(CatchLongjmpBB);
  IRB.SetInsertPoint(CatchLongjmpBB);
  CatchPadInst *CatchPad = IRB.CreateCatchPad(CatchSwitchLongjmp, {});

  // Wasm throw and catch instructions can throw and catch multiple values, but
  // that requires multivalue support in the toolchain, which is currently not
  // very reliable. We instead throw and catch a pointer to a struct value of
  // type 'struct __WasmLongjmpArgs', which is defined in Emscripten.
  Instruction *LongjmpArgs =
      IRB.CreateCall(CatchF, {IRB.getInt32(WebAssembly::C_LONGJMP)}, "thrown");
  Value *EnvField =
      IRB.CreateConstGEP2_32(LongjmpArgsTy, LongjmpArgs, 0, 0, "env_gep");
  Value *ValField =
      IRB.CreateConstGEP2_32(LongjmpArgsTy, LongjmpArgs, 0, 1, "val_gep");
  // void *env = __wasm_longjmp_args.env;
  Instruction *Env = IRB.CreateLoad(IRB.getPtrTy(), EnvField, "env");
  // int val = __wasm_longjmp_args.val;
  Instruction *Val = IRB.CreateLoad(IRB.getInt32Ty(), ValField, "val");

  // %label = __wasm_setjmp_test(%env, functionInvocatinoId);
  // if (%label == 0)
  //   __wasm_longjmp(%env, %val)
  // catchret to %setjmp.dispatch
  BasicBlock *ThenBB = BasicBlock::Create(C, "if.then", &F);
  BasicBlock *EndBB = BasicBlock::Create(C, "if.end", &F);
  Value *EnvP = IRB.CreateBitCast(Env, getAddrPtrType(&M), "env.p");
  Value *Label = IRB.CreateCall(WasmSetjmpTestF, {EnvP, FunctionInvocationId},
                                OperandBundleDef("funclet", CatchPad), "label");
  Value *Cmp = IRB.CreateICmpEQ(Label, IRB.getInt32(0));
  IRB.CreateCondBr(Cmp, ThenBB, EndBB);

  IRB.SetInsertPoint(ThenBB);
  CallInst *WasmLongjmpCI = IRB.CreateCall(
      WasmLongjmpF, {Env, Val}, OperandBundleDef("funclet", CatchPad));
  IRB.CreateUnreachable();

  IRB.SetInsertPoint(EndBB);
  // Jump to setjmp.dispatch block
  IRB.CreateCatchRet(CatchPad, SetjmpDispatchBB);

  // Go back to setjmp.dispatch BB
  // setjmp.dispatch:
  //   switch %label {
  //     label 1: goto post-setjmp BB 1
  //     label 2: goto post-setjmp BB 2
  //     ...
  //     default: goto splitted next BB
  //   }
  IRB.SetInsertPoint(SetjmpDispatchBB);
  PHINode *LabelPHI = IRB.CreatePHI(IRB.getInt32Ty(), 2, "label.phi");
  LabelPHI->addIncoming(Label, EndBB);
  LabelPHI->addIncoming(IRB.getInt32(-1), Entry);
  SwitchInst *SI = IRB.CreateSwitch(LabelPHI, OrigEntry, SetjmpRetPHIs.size());
  // -1 means no longjmp happened, continue normally (will hit the default
  // switch case). 0 means a longjmp that is not ours to handle, needs a
  // rethrow. Otherwise the index is the same as the index in P+1 (to avoid
  // 0).
  for (unsigned I = 0; I < SetjmpRetPHIs.size(); I++) {
    SI->addCase(IRB.getInt32(I + 1), SetjmpRetPHIs[I]->getParent());
    SetjmpRetPHIs[I]->addIncoming(Val, SetjmpDispatchBB);
  }

  // Convert all longjmpable call instructions to invokes that unwind to the
  // newly created catch.dispatch.longjmp BB.
  SmallVector<CallInst *, 64> LongjmpableCalls;
  for (auto *BB = &*F.begin(); BB; BB = BB->getNextNode()) {
    for (auto &I : *BB) {
      auto *CI = dyn_cast<CallInst>(&I);
      if (!CI)
        continue;
      const Value *Callee = CI->getCalledOperand();
      if (!canLongjmp(Callee))
        continue;
      if (isEmAsmCall(Callee))
        report_fatal_error("Cannot use EM_ASM* alongside setjmp/longjmp in " +
                               F.getName() +
                               ". Please consider using EM_JS, or move the "
                               "EM_ASM into another function.",
                           false);
      // This is __wasm_longjmp() call we inserted in this function, which
      // rethrows the longjmp when the longjmp does not correspond to one of
      // setjmps in this function. We should not convert this call to an invoke.
      if (CI == WasmLongjmpCI)
        continue;
      LongjmpableCalls.push_back(CI);
    }
  }

  for (auto *CI : LongjmpableCalls) {
    // Even if the callee function has attribute 'nounwind', which is true for
    // all C functions, it can longjmp, which means it can throw a Wasm
    // exception now.
    CI->removeFnAttr(Attribute::NoUnwind);
    if (Function *CalleeF = CI->getCalledFunction())
      CalleeF->removeFnAttr(Attribute::NoUnwind);

    // Change it to an invoke and make it unwind to the catch.dispatch.longjmp
    // BB. If the call is enclosed in another catchpad/cleanuppad scope, unwind
    // to its parent pad's unwind destination instead to preserve the scope
    // structure. It will eventually unwind to the catch.dispatch.longjmp.
    SmallVector<OperandBundleDef, 1> Bundles;
    BasicBlock *UnwindDest = nullptr;
    if (auto Bundle = CI->getOperandBundle(LLVMContext::OB_funclet)) {
      Instruction *FromPad = cast<Instruction>(Bundle->Inputs[0]);
      while (!UnwindDest) {
        if (auto *CPI = dyn_cast<CatchPadInst>(FromPad)) {
          UnwindDest = CPI->getCatchSwitch()->getUnwindDest();
          break;
        }
        if (auto *CPI = dyn_cast<CleanupPadInst>(FromPad)) {
          // getCleanupRetUnwindDest() can return nullptr when
          // 1. This cleanuppad's matching cleanupret uwninds to caller
          // 2. There is no matching cleanupret because it ends with
          //    unreachable.
          // In case of 2, we need to traverse the parent pad chain.
          UnwindDest = getCleanupRetUnwindDest(CPI);
          Value *ParentPad = CPI->getParentPad();
          if (isa<ConstantTokenNone>(ParentPad))
            break;
          FromPad = cast<Instruction>(ParentPad);
        }
      }
    }
    if (!UnwindDest)
      UnwindDest = CatchDispatchLongjmpBB;
    changeToInvokeAndSplitBasicBlock(CI, UnwindDest);
  }

  SmallVector<Instruction *, 16> ToErase;
  for (auto &BB : F) {
    if (auto *CSI = dyn_cast<CatchSwitchInst>(BB.getFirstNonPHI())) {
      if (CSI != CatchSwitchLongjmp && CSI->unwindsToCaller()) {
        IRB.SetInsertPoint(CSI);
        ToErase.push_back(CSI);
        auto *NewCSI = IRB.CreateCatchSwitch(CSI->getParentPad(),
                                             CatchDispatchLongjmpBB, 1);
        NewCSI->addHandler(*CSI->handler_begin());
        NewCSI->takeName(CSI);
        CSI->replaceAllUsesWith(NewCSI);
      }
    }

    if (auto *CRI = dyn_cast<CleanupReturnInst>(BB.getTerminator())) {
      if (CRI->unwindsToCaller()) {
        IRB.SetInsertPoint(CRI);
        ToErase.push_back(CRI);
        IRB.CreateCleanupRet(CRI->getCleanupPad(), CatchDispatchLongjmpBB);
      }
    }
  }

  for (Instruction *I : ToErase)
    I->eraseFromParent();
}
