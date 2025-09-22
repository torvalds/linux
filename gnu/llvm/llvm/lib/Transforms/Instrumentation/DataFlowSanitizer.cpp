//===- DataFlowSanitizer.cpp - dynamic data flow analysis -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file is a part of DataFlowSanitizer, a generalised dynamic data flow
/// analysis.
///
/// Unlike other Sanitizer tools, this tool is not designed to detect a specific
/// class of bugs on its own.  Instead, it provides a generic dynamic data flow
/// analysis framework to be used by clients to help detect application-specific
/// issues within their own code.
///
/// The analysis is based on automatic propagation of data flow labels (also
/// known as taint labels) through a program as it performs computation.
///
/// Argument and return value labels are passed through TLS variables
/// __dfsan_arg_tls and __dfsan_retval_tls.
///
/// Each byte of application memory is backed by a shadow memory byte. The
/// shadow byte can represent up to 8 labels. On Linux/x86_64, memory is then
/// laid out as follows:
///
/// +--------------------+ 0x800000000000 (top of memory)
/// |    application 3   |
/// +--------------------+ 0x700000000000
/// |      invalid       |
/// +--------------------+ 0x610000000000
/// |      origin 1      |
/// +--------------------+ 0x600000000000
/// |    application 2   |
/// +--------------------+ 0x510000000000
/// |      shadow 1      |
/// +--------------------+ 0x500000000000
/// |      invalid       |
/// +--------------------+ 0x400000000000
/// |      origin 3      |
/// +--------------------+ 0x300000000000
/// |      shadow 3      |
/// +--------------------+ 0x200000000000
/// |      origin 2      |
/// +--------------------+ 0x110000000000
/// |      invalid       |
/// +--------------------+ 0x100000000000
/// |      shadow 2      |
/// +--------------------+ 0x010000000000
/// |    application 1   |
/// +--------------------+ 0x000000000000
///
/// MEM_TO_SHADOW(mem) = mem ^ 0x500000000000
/// SHADOW_TO_ORIGIN(shadow) = shadow + 0x100000000000
///
/// For more information, please refer to the design document:
/// http://clang.llvm.org/docs/DataFlowSanitizerDesign.html
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Instrumentation/DataFlowSanitizer.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/iterator.h"
#include "llvm/Analysis/DomTreeUpdater.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/AttributeMask.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/SpecialCaseList.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/Instrumentation.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Local.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

// This must be consistent with ShadowWidthBits.
static const Align ShadowTLSAlignment = Align(2);

static const Align MinOriginAlignment = Align(4);

// The size of TLS variables. These constants must be kept in sync with the ones
// in dfsan.cpp.
static const unsigned ArgTLSSize = 800;
static const unsigned RetvalTLSSize = 800;

// The -dfsan-preserve-alignment flag controls whether this pass assumes that
// alignment requirements provided by the input IR are correct.  For example,
// if the input IR contains a load with alignment 8, this flag will cause
// the shadow load to have alignment 16.  This flag is disabled by default as
// we have unfortunately encountered too much code (including Clang itself;
// see PR14291) which performs misaligned access.
static cl::opt<bool> ClPreserveAlignment(
    "dfsan-preserve-alignment",
    cl::desc("respect alignment requirements provided by input IR"), cl::Hidden,
    cl::init(false));

// The ABI list files control how shadow parameters are passed. The pass treats
// every function labelled "uninstrumented" in the ABI list file as conforming
// to the "native" (i.e. unsanitized) ABI.  Unless the ABI list contains
// additional annotations for those functions, a call to one of those functions
// will produce a warning message, as the labelling behaviour of the function is
// unknown. The other supported annotations for uninstrumented functions are
// "functional" and "discard", which are described below under
// DataFlowSanitizer::WrapperKind.
// Functions will often be labelled with both "uninstrumented" and one of
// "functional" or "discard". This will leave the function unchanged by this
// pass, and create a wrapper function that will call the original.
//
// Instrumented functions can also be annotated as "force_zero_labels", which
// will make all shadow and return values set zero labels.
// Functions should never be labelled with both "force_zero_labels" and
// "uninstrumented" or any of the unistrumented wrapper kinds.
static cl::list<std::string> ClABIListFiles(
    "dfsan-abilist",
    cl::desc("File listing native ABI functions and how the pass treats them"),
    cl::Hidden);

// Controls whether the pass includes or ignores the labels of pointers in load
// instructions.
static cl::opt<bool> ClCombinePointerLabelsOnLoad(
    "dfsan-combine-pointer-labels-on-load",
    cl::desc("Combine the label of the pointer with the label of the data when "
             "loading from memory."),
    cl::Hidden, cl::init(true));

// Controls whether the pass includes or ignores the labels of pointers in
// stores instructions.
static cl::opt<bool> ClCombinePointerLabelsOnStore(
    "dfsan-combine-pointer-labels-on-store",
    cl::desc("Combine the label of the pointer with the label of the data when "
             "storing in memory."),
    cl::Hidden, cl::init(false));

// Controls whether the pass propagates labels of offsets in GEP instructions.
static cl::opt<bool> ClCombineOffsetLabelsOnGEP(
    "dfsan-combine-offset-labels-on-gep",
    cl::desc(
        "Combine the label of the offset with the label of the pointer when "
        "doing pointer arithmetic."),
    cl::Hidden, cl::init(true));

static cl::list<std::string> ClCombineTaintLookupTables(
    "dfsan-combine-taint-lookup-table",
    cl::desc(
        "When dfsan-combine-offset-labels-on-gep and/or "
        "dfsan-combine-pointer-labels-on-load are false, this flag can "
        "be used to re-enable combining offset and/or pointer taint when "
        "loading specific constant global variables (i.e. lookup tables)."),
    cl::Hidden);

static cl::opt<bool> ClDebugNonzeroLabels(
    "dfsan-debug-nonzero-labels",
    cl::desc("Insert calls to __dfsan_nonzero_label on observing a parameter, "
             "load or return with a nonzero label"),
    cl::Hidden);

// Experimental feature that inserts callbacks for certain data events.
// Currently callbacks are only inserted for loads, stores, memory transfers
// (i.e. memcpy and memmove), and comparisons.
//
// If this flag is set to true, the user must provide definitions for the
// following callback functions:
//   void __dfsan_load_callback(dfsan_label Label, void* addr);
//   void __dfsan_store_callback(dfsan_label Label, void* addr);
//   void __dfsan_mem_transfer_callback(dfsan_label *Start, size_t Len);
//   void __dfsan_cmp_callback(dfsan_label CombinedLabel);
static cl::opt<bool> ClEventCallbacks(
    "dfsan-event-callbacks",
    cl::desc("Insert calls to __dfsan_*_callback functions on data events."),
    cl::Hidden, cl::init(false));

// Experimental feature that inserts callbacks for conditionals, including:
// conditional branch, switch, select.
// This must be true for dfsan_set_conditional_callback() to have effect.
static cl::opt<bool> ClConditionalCallbacks(
    "dfsan-conditional-callbacks",
    cl::desc("Insert calls to callback functions on conditionals."), cl::Hidden,
    cl::init(false));

// Experimental feature that inserts callbacks for data reaching a function,
// either via function arguments and loads.
// This must be true for dfsan_set_reaches_function_callback() to have effect.
static cl::opt<bool> ClReachesFunctionCallbacks(
    "dfsan-reaches-function-callbacks",
    cl::desc("Insert calls to callback functions on data reaching a function."),
    cl::Hidden, cl::init(false));

// Controls whether the pass tracks the control flow of select instructions.
static cl::opt<bool> ClTrackSelectControlFlow(
    "dfsan-track-select-control-flow",
    cl::desc("Propagate labels from condition values of select instructions "
             "to results."),
    cl::Hidden, cl::init(true));

// TODO: This default value follows MSan. DFSan may use a different value.
static cl::opt<int> ClInstrumentWithCallThreshold(
    "dfsan-instrument-with-call-threshold",
    cl::desc("If the function being instrumented requires more than "
             "this number of origin stores, use callbacks instead of "
             "inline checks (-1 means never use callbacks)."),
    cl::Hidden, cl::init(3500));

// Controls how to track origins.
// * 0: do not track origins.
// * 1: track origins at memory store operations.
// * 2: track origins at memory load and store operations.
//      TODO: track callsites.
static cl::opt<int> ClTrackOrigins("dfsan-track-origins",
                                   cl::desc("Track origins of labels"),
                                   cl::Hidden, cl::init(0));

static cl::opt<bool> ClIgnorePersonalityRoutine(
    "dfsan-ignore-personality-routine",
    cl::desc("If a personality routine is marked uninstrumented from the ABI "
             "list, do not create a wrapper for it."),
    cl::Hidden, cl::init(false));

static StringRef getGlobalTypeString(const GlobalValue &G) {
  // Types of GlobalVariables are always pointer types.
  Type *GType = G.getValueType();
  // For now we support excluding struct types only.
  if (StructType *SGType = dyn_cast<StructType>(GType)) {
    if (!SGType->isLiteral())
      return SGType->getName();
  }
  return "<unknown type>";
}

namespace {

// Memory map parameters used in application-to-shadow address calculation.
// Offset = (Addr & ~AndMask) ^ XorMask
// Shadow = ShadowBase + Offset
// Origin = (OriginBase + Offset) & ~3ULL
struct MemoryMapParams {
  uint64_t AndMask;
  uint64_t XorMask;
  uint64_t ShadowBase;
  uint64_t OriginBase;
};

} // end anonymous namespace

// NOLINTBEGIN(readability-identifier-naming)
// aarch64 Linux
const MemoryMapParams Linux_AArch64_MemoryMapParams = {
    0,               // AndMask (not used)
    0x0B00000000000, // XorMask
    0,               // ShadowBase (not used)
    0x0200000000000, // OriginBase
};

// x86_64 Linux
const MemoryMapParams Linux_X86_64_MemoryMapParams = {
    0,              // AndMask (not used)
    0x500000000000, // XorMask
    0,              // ShadowBase (not used)
    0x100000000000, // OriginBase
};
// NOLINTEND(readability-identifier-naming)

// loongarch64 Linux
const MemoryMapParams Linux_LoongArch64_MemoryMapParams = {
    0,              // AndMask (not used)
    0x500000000000, // XorMask
    0,              // ShadowBase (not used)
    0x100000000000, // OriginBase
};

namespace {

class DFSanABIList {
  std::unique_ptr<SpecialCaseList> SCL;

public:
  DFSanABIList() = default;

  void set(std::unique_ptr<SpecialCaseList> List) { SCL = std::move(List); }

  /// Returns whether either this function or its source file are listed in the
  /// given category.
  bool isIn(const Function &F, StringRef Category) const {
    return isIn(*F.getParent(), Category) ||
           SCL->inSection("dataflow", "fun", F.getName(), Category);
  }

  /// Returns whether this global alias is listed in the given category.
  ///
  /// If GA aliases a function, the alias's name is matched as a function name
  /// would be.  Similarly, aliases of globals are matched like globals.
  bool isIn(const GlobalAlias &GA, StringRef Category) const {
    if (isIn(*GA.getParent(), Category))
      return true;

    if (isa<FunctionType>(GA.getValueType()))
      return SCL->inSection("dataflow", "fun", GA.getName(), Category);

    return SCL->inSection("dataflow", "global", GA.getName(), Category) ||
           SCL->inSection("dataflow", "type", getGlobalTypeString(GA),
                          Category);
  }

  /// Returns whether this module is listed in the given category.
  bool isIn(const Module &M, StringRef Category) const {
    return SCL->inSection("dataflow", "src", M.getModuleIdentifier(), Category);
  }
};

/// TransformedFunction is used to express the result of transforming one
/// function type into another.  This struct is immutable.  It holds metadata
/// useful for updating calls of the old function to the new type.
struct TransformedFunction {
  TransformedFunction(FunctionType *OriginalType, FunctionType *TransformedType,
                      const std::vector<unsigned> &ArgumentIndexMapping)
      : OriginalType(OriginalType), TransformedType(TransformedType),
        ArgumentIndexMapping(ArgumentIndexMapping) {}

  // Disallow copies.
  TransformedFunction(const TransformedFunction &) = delete;
  TransformedFunction &operator=(const TransformedFunction &) = delete;

  // Allow moves.
  TransformedFunction(TransformedFunction &&) = default;
  TransformedFunction &operator=(TransformedFunction &&) = default;

  /// Type of the function before the transformation.
  FunctionType *OriginalType;

  /// Type of the function after the transformation.
  FunctionType *TransformedType;

  /// Transforming a function may change the position of arguments.  This
  /// member records the mapping from each argument's old position to its new
  /// position.  Argument positions are zero-indexed.  If the transformation
  /// from F to F' made the first argument of F into the third argument of F',
  /// then ArgumentIndexMapping[0] will equal 2.
  std::vector<unsigned> ArgumentIndexMapping;
};

/// Given function attributes from a call site for the original function,
/// return function attributes appropriate for a call to the transformed
/// function.
AttributeList
transformFunctionAttributes(const TransformedFunction &TransformedFunction,
                            LLVMContext &Ctx, AttributeList CallSiteAttrs) {

  // Construct a vector of AttributeSet for each function argument.
  std::vector<llvm::AttributeSet> ArgumentAttributes(
      TransformedFunction.TransformedType->getNumParams());

  // Copy attributes from the parameter of the original function to the
  // transformed version.  'ArgumentIndexMapping' holds the mapping from
  // old argument position to new.
  for (unsigned I = 0, IE = TransformedFunction.ArgumentIndexMapping.size();
       I < IE; ++I) {
    unsigned TransformedIndex = TransformedFunction.ArgumentIndexMapping[I];
    ArgumentAttributes[TransformedIndex] = CallSiteAttrs.getParamAttrs(I);
  }

  // Copy annotations on varargs arguments.
  for (unsigned I = TransformedFunction.OriginalType->getNumParams(),
                IE = CallSiteAttrs.getNumAttrSets();
       I < IE; ++I) {
    ArgumentAttributes.push_back(CallSiteAttrs.getParamAttrs(I));
  }

  return AttributeList::get(Ctx, CallSiteAttrs.getFnAttrs(),
                            CallSiteAttrs.getRetAttrs(),
                            llvm::ArrayRef(ArgumentAttributes));
}

class DataFlowSanitizer {
  friend struct DFSanFunction;
  friend class DFSanVisitor;

  enum { ShadowWidthBits = 8, ShadowWidthBytes = ShadowWidthBits / 8 };

  enum { OriginWidthBits = 32, OriginWidthBytes = OriginWidthBits / 8 };

  /// How should calls to uninstrumented functions be handled?
  enum WrapperKind {
    /// This function is present in an uninstrumented form but we don't know
    /// how it should be handled.  Print a warning and call the function anyway.
    /// Don't label the return value.
    WK_Warning,

    /// This function does not write to (user-accessible) memory, and its return
    /// value is unlabelled.
    WK_Discard,

    /// This function does not write to (user-accessible) memory, and the label
    /// of its return value is the union of the label of its arguments.
    WK_Functional,

    /// Instead of calling the function, a custom wrapper __dfsw_F is called,
    /// where F is the name of the function.  This function may wrap the
    /// original function or provide its own implementation. WK_Custom uses an
    /// extra pointer argument to return the shadow.  This allows the wrapped
    /// form of the function type to be expressed in C.
    WK_Custom
  };

  Module *Mod;
  LLVMContext *Ctx;
  Type *Int8Ptr;
  IntegerType *OriginTy;
  PointerType *OriginPtrTy;
  ConstantInt *ZeroOrigin;
  /// The shadow type for all primitive types and vector types.
  IntegerType *PrimitiveShadowTy;
  PointerType *PrimitiveShadowPtrTy;
  IntegerType *IntptrTy;
  ConstantInt *ZeroPrimitiveShadow;
  Constant *ArgTLS;
  ArrayType *ArgOriginTLSTy;
  Constant *ArgOriginTLS;
  Constant *RetvalTLS;
  Constant *RetvalOriginTLS;
  FunctionType *DFSanUnionLoadFnTy;
  FunctionType *DFSanLoadLabelAndOriginFnTy;
  FunctionType *DFSanUnimplementedFnTy;
  FunctionType *DFSanWrapperExternWeakNullFnTy;
  FunctionType *DFSanSetLabelFnTy;
  FunctionType *DFSanNonzeroLabelFnTy;
  FunctionType *DFSanVarargWrapperFnTy;
  FunctionType *DFSanConditionalCallbackFnTy;
  FunctionType *DFSanConditionalCallbackOriginFnTy;
  FunctionType *DFSanReachesFunctionCallbackFnTy;
  FunctionType *DFSanReachesFunctionCallbackOriginFnTy;
  FunctionType *DFSanCmpCallbackFnTy;
  FunctionType *DFSanLoadStoreCallbackFnTy;
  FunctionType *DFSanMemTransferCallbackFnTy;
  FunctionType *DFSanChainOriginFnTy;
  FunctionType *DFSanChainOriginIfTaintedFnTy;
  FunctionType *DFSanMemOriginTransferFnTy;
  FunctionType *DFSanMemShadowOriginTransferFnTy;
  FunctionType *DFSanMemShadowOriginConditionalExchangeFnTy;
  FunctionType *DFSanMaybeStoreOriginFnTy;
  FunctionCallee DFSanUnionLoadFn;
  FunctionCallee DFSanLoadLabelAndOriginFn;
  FunctionCallee DFSanUnimplementedFn;
  FunctionCallee DFSanWrapperExternWeakNullFn;
  FunctionCallee DFSanSetLabelFn;
  FunctionCallee DFSanNonzeroLabelFn;
  FunctionCallee DFSanVarargWrapperFn;
  FunctionCallee DFSanLoadCallbackFn;
  FunctionCallee DFSanStoreCallbackFn;
  FunctionCallee DFSanMemTransferCallbackFn;
  FunctionCallee DFSanConditionalCallbackFn;
  FunctionCallee DFSanConditionalCallbackOriginFn;
  FunctionCallee DFSanReachesFunctionCallbackFn;
  FunctionCallee DFSanReachesFunctionCallbackOriginFn;
  FunctionCallee DFSanCmpCallbackFn;
  FunctionCallee DFSanChainOriginFn;
  FunctionCallee DFSanChainOriginIfTaintedFn;
  FunctionCallee DFSanMemOriginTransferFn;
  FunctionCallee DFSanMemShadowOriginTransferFn;
  FunctionCallee DFSanMemShadowOriginConditionalExchangeFn;
  FunctionCallee DFSanMaybeStoreOriginFn;
  SmallPtrSet<Value *, 16> DFSanRuntimeFunctions;
  MDNode *ColdCallWeights;
  MDNode *OriginStoreWeights;
  DFSanABIList ABIList;
  DenseMap<Value *, Function *> UnwrappedFnMap;
  AttributeMask ReadOnlyNoneAttrs;
  StringSet<> CombineTaintLookupTableNames;

  /// Memory map parameters used in calculation mapping application addresses
  /// to shadow addresses and origin addresses.
  const MemoryMapParams *MapParams;

  Value *getShadowOffset(Value *Addr, IRBuilder<> &IRB);
  Value *getShadowAddress(Value *Addr, BasicBlock::iterator Pos);
  Value *getShadowAddress(Value *Addr, BasicBlock::iterator Pos,
                          Value *ShadowOffset);
  std::pair<Value *, Value *> getShadowOriginAddress(Value *Addr,
                                                     Align InstAlignment,
                                                     BasicBlock::iterator Pos);
  bool isInstrumented(const Function *F);
  bool isInstrumented(const GlobalAlias *GA);
  bool isForceZeroLabels(const Function *F);
  TransformedFunction getCustomFunctionType(FunctionType *T);
  WrapperKind getWrapperKind(Function *F);
  void addGlobalNameSuffix(GlobalValue *GV);
  void buildExternWeakCheckIfNeeded(IRBuilder<> &IRB, Function *F);
  Function *buildWrapperFunction(Function *F, StringRef NewFName,
                                 GlobalValue::LinkageTypes NewFLink,
                                 FunctionType *NewFT);
  void initializeCallbackFunctions(Module &M);
  void initializeRuntimeFunctions(Module &M);
  bool initializeModule(Module &M);

  /// Advances \p OriginAddr to point to the next 32-bit origin and then loads
  /// from it. Returns the origin's loaded value.
  Value *loadNextOrigin(BasicBlock::iterator Pos, Align OriginAlign,
                        Value **OriginAddr);

  /// Returns whether the given load byte size is amenable to inlined
  /// optimization patterns.
  bool hasLoadSizeForFastPath(uint64_t Size);

  /// Returns whether the pass tracks origins. Supports only TLS ABI mode.
  bool shouldTrackOrigins();

  /// Returns a zero constant with the shadow type of OrigTy.
  ///
  /// getZeroShadow({T1,T2,...}) = {getZeroShadow(T1),getZeroShadow(T2,...}
  /// getZeroShadow([n x T]) = [n x getZeroShadow(T)]
  /// getZeroShadow(other type) = i16(0)
  Constant *getZeroShadow(Type *OrigTy);
  /// Returns a zero constant with the shadow type of V's type.
  Constant *getZeroShadow(Value *V);

  /// Checks if V is a zero shadow.
  bool isZeroShadow(Value *V);

  /// Returns the shadow type of OrigTy.
  ///
  /// getShadowTy({T1,T2,...}) = {getShadowTy(T1),getShadowTy(T2),...}
  /// getShadowTy([n x T]) = [n x getShadowTy(T)]
  /// getShadowTy(other type) = i16
  Type *getShadowTy(Type *OrigTy);
  /// Returns the shadow type of V's type.
  Type *getShadowTy(Value *V);

  const uint64_t NumOfElementsInArgOrgTLS = ArgTLSSize / OriginWidthBytes;

public:
  DataFlowSanitizer(const std::vector<std::string> &ABIListFiles);

  bool runImpl(Module &M,
               llvm::function_ref<TargetLibraryInfo &(Function &)> GetTLI);
};

struct DFSanFunction {
  DataFlowSanitizer &DFS;
  Function *F;
  DominatorTree DT;
  bool IsNativeABI;
  bool IsForceZeroLabels;
  TargetLibraryInfo &TLI;
  AllocaInst *LabelReturnAlloca = nullptr;
  AllocaInst *OriginReturnAlloca = nullptr;
  DenseMap<Value *, Value *> ValShadowMap;
  DenseMap<Value *, Value *> ValOriginMap;
  DenseMap<AllocaInst *, AllocaInst *> AllocaShadowMap;
  DenseMap<AllocaInst *, AllocaInst *> AllocaOriginMap;

  struct PHIFixupElement {
    PHINode *Phi;
    PHINode *ShadowPhi;
    PHINode *OriginPhi;
  };
  std::vector<PHIFixupElement> PHIFixups;

  DenseSet<Instruction *> SkipInsts;
  std::vector<Value *> NonZeroChecks;

  struct CachedShadow {
    BasicBlock *Block; // The block where Shadow is defined.
    Value *Shadow;
  };
  /// Maps a value to its latest shadow value in terms of domination tree.
  DenseMap<std::pair<Value *, Value *>, CachedShadow> CachedShadows;
  /// Maps a value to its latest collapsed shadow value it was converted to in
  /// terms of domination tree. When ClDebugNonzeroLabels is on, this cache is
  /// used at a post process where CFG blocks are split. So it does not cache
  /// BasicBlock like CachedShadows, but uses domination between values.
  DenseMap<Value *, Value *> CachedCollapsedShadows;
  DenseMap<Value *, std::set<Value *>> ShadowElements;

  DFSanFunction(DataFlowSanitizer &DFS, Function *F, bool IsNativeABI,
                bool IsForceZeroLabels, TargetLibraryInfo &TLI)
      : DFS(DFS), F(F), IsNativeABI(IsNativeABI),
        IsForceZeroLabels(IsForceZeroLabels), TLI(TLI) {
    DT.recalculate(*F);
  }

  /// Computes the shadow address for a given function argument.
  ///
  /// Shadow = ArgTLS+ArgOffset.
  Value *getArgTLS(Type *T, unsigned ArgOffset, IRBuilder<> &IRB);

  /// Computes the shadow address for a return value.
  Value *getRetvalTLS(Type *T, IRBuilder<> &IRB);

  /// Computes the origin address for a given function argument.
  ///
  /// Origin = ArgOriginTLS[ArgNo].
  Value *getArgOriginTLS(unsigned ArgNo, IRBuilder<> &IRB);

  /// Computes the origin address for a return value.
  Value *getRetvalOriginTLS();

  Value *getOrigin(Value *V);
  void setOrigin(Instruction *I, Value *Origin);
  /// Generates IR to compute the origin of the last operand with a taint label.
  Value *combineOperandOrigins(Instruction *Inst);
  /// Before the instruction Pos, generates IR to compute the last origin with a
  /// taint label. Labels and origins are from vectors Shadows and Origins
  /// correspondingly. The generated IR is like
  ///   Sn-1 != Zero ? On-1: ... S2 != Zero ? O2: S1 != Zero ? O1: O0
  /// When Zero is nullptr, it uses ZeroPrimitiveShadow. Otherwise it can be
  /// zeros with other bitwidths.
  Value *combineOrigins(const std::vector<Value *> &Shadows,
                        const std::vector<Value *> &Origins,
                        BasicBlock::iterator Pos, ConstantInt *Zero = nullptr);

  Value *getShadow(Value *V);
  void setShadow(Instruction *I, Value *Shadow);
  /// Generates IR to compute the union of the two given shadows, inserting it
  /// before Pos. The combined value is with primitive type.
  Value *combineShadows(Value *V1, Value *V2, BasicBlock::iterator Pos);
  /// Combines the shadow values of V1 and V2, then converts the combined value
  /// with primitive type into a shadow value with the original type T.
  Value *combineShadowsThenConvert(Type *T, Value *V1, Value *V2,
                                   BasicBlock::iterator Pos);
  Value *combineOperandShadows(Instruction *Inst);

  /// Generates IR to load shadow and origin corresponding to bytes [\p
  /// Addr, \p Addr + \p Size), where addr has alignment \p
  /// InstAlignment, and take the union of each of those shadows. The returned
  /// shadow always has primitive type.
  ///
  /// When tracking loads is enabled, the returned origin is a chain at the
  /// current stack if the returned shadow is tainted.
  std::pair<Value *, Value *> loadShadowOrigin(Value *Addr, uint64_t Size,
                                               Align InstAlignment,
                                               BasicBlock::iterator Pos);

  void storePrimitiveShadowOrigin(Value *Addr, uint64_t Size,
                                  Align InstAlignment, Value *PrimitiveShadow,
                                  Value *Origin, BasicBlock::iterator Pos);
  /// Applies PrimitiveShadow to all primitive subtypes of T, returning
  /// the expanded shadow value.
  ///
  /// EFP({T1,T2, ...}, PS) = {EFP(T1,PS),EFP(T2,PS),...}
  /// EFP([n x T], PS) = [n x EFP(T,PS)]
  /// EFP(other types, PS) = PS
  Value *expandFromPrimitiveShadow(Type *T, Value *PrimitiveShadow,
                                   BasicBlock::iterator Pos);
  /// Collapses Shadow into a single primitive shadow value, unioning all
  /// primitive shadow values in the process. Returns the final primitive
  /// shadow value.
  ///
  /// CTP({V1,V2, ...}) = UNION(CFP(V1,PS),CFP(V2,PS),...)
  /// CTP([V1,V2,...]) = UNION(CFP(V1,PS),CFP(V2,PS),...)
  /// CTP(other types, PS) = PS
  Value *collapseToPrimitiveShadow(Value *Shadow, BasicBlock::iterator Pos);

  void storeZeroPrimitiveShadow(Value *Addr, uint64_t Size, Align ShadowAlign,
                                BasicBlock::iterator Pos);

  Align getShadowAlign(Align InstAlignment);

  // If ClConditionalCallbacks is enabled, insert a callback after a given
  // branch instruction using the given conditional expression.
  void addConditionalCallbacksIfEnabled(Instruction &I, Value *Condition);

  // If ClReachesFunctionCallbacks is enabled, insert a callback for each
  // argument and load instruction.
  void addReachesFunctionCallbacksIfEnabled(IRBuilder<> &IRB, Instruction &I,
                                            Value *Data);

  bool isLookupTableConstant(Value *P);

private:
  /// Collapses the shadow with aggregate type into a single primitive shadow
  /// value.
  template <class AggregateType>
  Value *collapseAggregateShadow(AggregateType *AT, Value *Shadow,
                                 IRBuilder<> &IRB);

  Value *collapseToPrimitiveShadow(Value *Shadow, IRBuilder<> &IRB);

  /// Returns the shadow value of an argument A.
  Value *getShadowForTLSArgument(Argument *A);

  /// The fast path of loading shadows.
  std::pair<Value *, Value *>
  loadShadowFast(Value *ShadowAddr, Value *OriginAddr, uint64_t Size,
                 Align ShadowAlign, Align OriginAlign, Value *FirstOrigin,
                 BasicBlock::iterator Pos);

  Align getOriginAlign(Align InstAlignment);

  /// Because 4 contiguous bytes share one 4-byte origin, the most accurate load
  /// is __dfsan_load_label_and_origin. This function returns the union of all
  /// labels and the origin of the first taint label. However this is an
  /// additional call with many instructions. To ensure common cases are fast,
  /// checks if it is possible to load labels and origins without using the
  /// callback function.
  ///
  /// When enabling tracking load instructions, we always use
  /// __dfsan_load_label_and_origin to reduce code size.
  bool useCallbackLoadLabelAndOrigin(uint64_t Size, Align InstAlignment);

  /// Returns a chain at the current stack with previous origin V.
  Value *updateOrigin(Value *V, IRBuilder<> &IRB);

  /// Returns a chain at the current stack with previous origin V if Shadow is
  /// tainted.
  Value *updateOriginIfTainted(Value *Shadow, Value *Origin, IRBuilder<> &IRB);

  /// Creates an Intptr = Origin | Origin << 32 if Intptr's size is 64. Returns
  /// Origin otherwise.
  Value *originToIntptr(IRBuilder<> &IRB, Value *Origin);

  /// Stores Origin into the address range [StoreOriginAddr, StoreOriginAddr +
  /// Size).
  void paintOrigin(IRBuilder<> &IRB, Value *Origin, Value *StoreOriginAddr,
                   uint64_t StoreOriginSize, Align Alignment);

  /// Stores Origin in terms of its Shadow value.
  /// * Do not write origins for zero shadows because we do not trace origins
  ///   for untainted sinks.
  /// * Use __dfsan_maybe_store_origin if there are too many origin store
  ///   instrumentations.
  void storeOrigin(BasicBlock::iterator Pos, Value *Addr, uint64_t Size,
                   Value *Shadow, Value *Origin, Value *StoreOriginAddr,
                   Align InstAlignment);

  /// Convert a scalar value to an i1 by comparing with 0.
  Value *convertToBool(Value *V, IRBuilder<> &IRB, const Twine &Name = "");

  bool shouldInstrumentWithCall();

  /// Generates IR to load shadow and origin corresponding to bytes [\p
  /// Addr, \p Addr + \p Size), where addr has alignment \p
  /// InstAlignment, and take the union of each of those shadows. The returned
  /// shadow always has primitive type.
  std::pair<Value *, Value *>
  loadShadowOriginSansLoadTracking(Value *Addr, uint64_t Size,
                                   Align InstAlignment,
                                   BasicBlock::iterator Pos);
  int NumOriginStores = 0;
};

class DFSanVisitor : public InstVisitor<DFSanVisitor> {
public:
  DFSanFunction &DFSF;

  DFSanVisitor(DFSanFunction &DFSF) : DFSF(DFSF) {}

  const DataLayout &getDataLayout() const {
    return DFSF.F->getDataLayout();
  }

  // Combines shadow values and origins for all of I's operands.
  void visitInstOperands(Instruction &I);

  void visitUnaryOperator(UnaryOperator &UO);
  void visitBinaryOperator(BinaryOperator &BO);
  void visitBitCastInst(BitCastInst &BCI);
  void visitCastInst(CastInst &CI);
  void visitCmpInst(CmpInst &CI);
  void visitLandingPadInst(LandingPadInst &LPI);
  void visitGetElementPtrInst(GetElementPtrInst &GEPI);
  void visitLoadInst(LoadInst &LI);
  void visitStoreInst(StoreInst &SI);
  void visitAtomicRMWInst(AtomicRMWInst &I);
  void visitAtomicCmpXchgInst(AtomicCmpXchgInst &I);
  void visitReturnInst(ReturnInst &RI);
  void visitLibAtomicLoad(CallBase &CB);
  void visitLibAtomicStore(CallBase &CB);
  void visitLibAtomicExchange(CallBase &CB);
  void visitLibAtomicCompareExchange(CallBase &CB);
  void visitCallBase(CallBase &CB);
  void visitPHINode(PHINode &PN);
  void visitExtractElementInst(ExtractElementInst &I);
  void visitInsertElementInst(InsertElementInst &I);
  void visitShuffleVectorInst(ShuffleVectorInst &I);
  void visitExtractValueInst(ExtractValueInst &I);
  void visitInsertValueInst(InsertValueInst &I);
  void visitAllocaInst(AllocaInst &I);
  void visitSelectInst(SelectInst &I);
  void visitMemSetInst(MemSetInst &I);
  void visitMemTransferInst(MemTransferInst &I);
  void visitBranchInst(BranchInst &BR);
  void visitSwitchInst(SwitchInst &SW);

private:
  void visitCASOrRMW(Align InstAlignment, Instruction &I);

  // Returns false when this is an invoke of a custom function.
  bool visitWrappedCallBase(Function &F, CallBase &CB);

  // Combines origins for all of I's operands.
  void visitInstOperandOrigins(Instruction &I);

  void addShadowArguments(Function &F, CallBase &CB, std::vector<Value *> &Args,
                          IRBuilder<> &IRB);

  void addOriginArguments(Function &F, CallBase &CB, std::vector<Value *> &Args,
                          IRBuilder<> &IRB);

  Value *makeAddAcquireOrderingTable(IRBuilder<> &IRB);
  Value *makeAddReleaseOrderingTable(IRBuilder<> &IRB);
};

bool LibAtomicFunction(const Function &F) {
  // This is a bit of a hack because TargetLibraryInfo is a function pass.
  // The DFSan pass would need to be refactored to be function pass oriented
  // (like MSan is) in order to fit together nicely with TargetLibraryInfo.
  // We need this check to prevent them from being instrumented, or wrapped.
  // Match on name and number of arguments.
  if (!F.hasName() || F.isVarArg())
    return false;
  switch (F.arg_size()) {
  case 4:
    return F.getName() == "__atomic_load" || F.getName() == "__atomic_store";
  case 5:
    return F.getName() == "__atomic_exchange";
  case 6:
    return F.getName() == "__atomic_compare_exchange";
  default:
    return false;
  }
}

} // end anonymous namespace

DataFlowSanitizer::DataFlowSanitizer(
    const std::vector<std::string> &ABIListFiles) {
  std::vector<std::string> AllABIListFiles(std::move(ABIListFiles));
  llvm::append_range(AllABIListFiles, ClABIListFiles);
  // FIXME: should we propagate vfs::FileSystem to this constructor?
  ABIList.set(
      SpecialCaseList::createOrDie(AllABIListFiles, *vfs::getRealFileSystem()));

  for (StringRef v : ClCombineTaintLookupTables)
    CombineTaintLookupTableNames.insert(v);
}

TransformedFunction DataFlowSanitizer::getCustomFunctionType(FunctionType *T) {
  SmallVector<Type *, 4> ArgTypes;

  // Some parameters of the custom function being constructed are
  // parameters of T.  Record the mapping from parameters of T to
  // parameters of the custom function, so that parameter attributes
  // at call sites can be updated.
  std::vector<unsigned> ArgumentIndexMapping;
  for (unsigned I = 0, E = T->getNumParams(); I != E; ++I) {
    Type *ParamType = T->getParamType(I);
    ArgumentIndexMapping.push_back(ArgTypes.size());
    ArgTypes.push_back(ParamType);
  }
  for (unsigned I = 0, E = T->getNumParams(); I != E; ++I)
    ArgTypes.push_back(PrimitiveShadowTy);
  if (T->isVarArg())
    ArgTypes.push_back(PrimitiveShadowPtrTy);
  Type *RetType = T->getReturnType();
  if (!RetType->isVoidTy())
    ArgTypes.push_back(PrimitiveShadowPtrTy);

  if (shouldTrackOrigins()) {
    for (unsigned I = 0, E = T->getNumParams(); I != E; ++I)
      ArgTypes.push_back(OriginTy);
    if (T->isVarArg())
      ArgTypes.push_back(OriginPtrTy);
    if (!RetType->isVoidTy())
      ArgTypes.push_back(OriginPtrTy);
  }

  return TransformedFunction(
      T, FunctionType::get(T->getReturnType(), ArgTypes, T->isVarArg()),
      ArgumentIndexMapping);
}

bool DataFlowSanitizer::isZeroShadow(Value *V) {
  Type *T = V->getType();
  if (!isa<ArrayType>(T) && !isa<StructType>(T)) {
    if (const ConstantInt *CI = dyn_cast<ConstantInt>(V))
      return CI->isZero();
    return false;
  }

  return isa<ConstantAggregateZero>(V);
}

bool DataFlowSanitizer::hasLoadSizeForFastPath(uint64_t Size) {
  uint64_t ShadowSize = Size * ShadowWidthBytes;
  return ShadowSize % 8 == 0 || ShadowSize == 4;
}

bool DataFlowSanitizer::shouldTrackOrigins() {
  static const bool ShouldTrackOrigins = ClTrackOrigins;
  return ShouldTrackOrigins;
}

Constant *DataFlowSanitizer::getZeroShadow(Type *OrigTy) {
  if (!isa<ArrayType>(OrigTy) && !isa<StructType>(OrigTy))
    return ZeroPrimitiveShadow;
  Type *ShadowTy = getShadowTy(OrigTy);
  return ConstantAggregateZero::get(ShadowTy);
}

Constant *DataFlowSanitizer::getZeroShadow(Value *V) {
  return getZeroShadow(V->getType());
}

static Value *expandFromPrimitiveShadowRecursive(
    Value *Shadow, SmallVector<unsigned, 4> &Indices, Type *SubShadowTy,
    Value *PrimitiveShadow, IRBuilder<> &IRB) {
  if (!isa<ArrayType>(SubShadowTy) && !isa<StructType>(SubShadowTy))
    return IRB.CreateInsertValue(Shadow, PrimitiveShadow, Indices);

  if (ArrayType *AT = dyn_cast<ArrayType>(SubShadowTy)) {
    for (unsigned Idx = 0; Idx < AT->getNumElements(); Idx++) {
      Indices.push_back(Idx);
      Shadow = expandFromPrimitiveShadowRecursive(
          Shadow, Indices, AT->getElementType(), PrimitiveShadow, IRB);
      Indices.pop_back();
    }
    return Shadow;
  }

  if (StructType *ST = dyn_cast<StructType>(SubShadowTy)) {
    for (unsigned Idx = 0; Idx < ST->getNumElements(); Idx++) {
      Indices.push_back(Idx);
      Shadow = expandFromPrimitiveShadowRecursive(
          Shadow, Indices, ST->getElementType(Idx), PrimitiveShadow, IRB);
      Indices.pop_back();
    }
    return Shadow;
  }
  llvm_unreachable("Unexpected shadow type");
}

bool DFSanFunction::shouldInstrumentWithCall() {
  return ClInstrumentWithCallThreshold >= 0 &&
         NumOriginStores >= ClInstrumentWithCallThreshold;
}

Value *DFSanFunction::expandFromPrimitiveShadow(Type *T, Value *PrimitiveShadow,
                                                BasicBlock::iterator Pos) {
  Type *ShadowTy = DFS.getShadowTy(T);

  if (!isa<ArrayType>(ShadowTy) && !isa<StructType>(ShadowTy))
    return PrimitiveShadow;

  if (DFS.isZeroShadow(PrimitiveShadow))
    return DFS.getZeroShadow(ShadowTy);

  IRBuilder<> IRB(Pos->getParent(), Pos);
  SmallVector<unsigned, 4> Indices;
  Value *Shadow = UndefValue::get(ShadowTy);
  Shadow = expandFromPrimitiveShadowRecursive(Shadow, Indices, ShadowTy,
                                              PrimitiveShadow, IRB);

  // Caches the primitive shadow value that built the shadow value.
  CachedCollapsedShadows[Shadow] = PrimitiveShadow;
  return Shadow;
}

template <class AggregateType>
Value *DFSanFunction::collapseAggregateShadow(AggregateType *AT, Value *Shadow,
                                              IRBuilder<> &IRB) {
  if (!AT->getNumElements())
    return DFS.ZeroPrimitiveShadow;

  Value *FirstItem = IRB.CreateExtractValue(Shadow, 0);
  Value *Aggregator = collapseToPrimitiveShadow(FirstItem, IRB);

  for (unsigned Idx = 1; Idx < AT->getNumElements(); Idx++) {
    Value *ShadowItem = IRB.CreateExtractValue(Shadow, Idx);
    Value *ShadowInner = collapseToPrimitiveShadow(ShadowItem, IRB);
    Aggregator = IRB.CreateOr(Aggregator, ShadowInner);
  }
  return Aggregator;
}

Value *DFSanFunction::collapseToPrimitiveShadow(Value *Shadow,
                                                IRBuilder<> &IRB) {
  Type *ShadowTy = Shadow->getType();
  if (!isa<ArrayType>(ShadowTy) && !isa<StructType>(ShadowTy))
    return Shadow;
  if (ArrayType *AT = dyn_cast<ArrayType>(ShadowTy))
    return collapseAggregateShadow<>(AT, Shadow, IRB);
  if (StructType *ST = dyn_cast<StructType>(ShadowTy))
    return collapseAggregateShadow<>(ST, Shadow, IRB);
  llvm_unreachable("Unexpected shadow type");
}

Value *DFSanFunction::collapseToPrimitiveShadow(Value *Shadow,
                                                BasicBlock::iterator Pos) {
  Type *ShadowTy = Shadow->getType();
  if (!isa<ArrayType>(ShadowTy) && !isa<StructType>(ShadowTy))
    return Shadow;

  // Checks if the cached collapsed shadow value dominates Pos.
  Value *&CS = CachedCollapsedShadows[Shadow];
  if (CS && DT.dominates(CS, Pos))
    return CS;

  IRBuilder<> IRB(Pos->getParent(), Pos);
  Value *PrimitiveShadow = collapseToPrimitiveShadow(Shadow, IRB);
  // Caches the converted primitive shadow value.
  CS = PrimitiveShadow;
  return PrimitiveShadow;
}

void DFSanFunction::addConditionalCallbacksIfEnabled(Instruction &I,
                                                     Value *Condition) {
  if (!ClConditionalCallbacks) {
    return;
  }
  IRBuilder<> IRB(&I);
  Value *CondShadow = getShadow(Condition);
  CallInst *CI;
  if (DFS.shouldTrackOrigins()) {
    Value *CondOrigin = getOrigin(Condition);
    CI = IRB.CreateCall(DFS.DFSanConditionalCallbackOriginFn,
                        {CondShadow, CondOrigin});
  } else {
    CI = IRB.CreateCall(DFS.DFSanConditionalCallbackFn, {CondShadow});
  }
  CI->addParamAttr(0, Attribute::ZExt);
}

void DFSanFunction::addReachesFunctionCallbacksIfEnabled(IRBuilder<> &IRB,
                                                         Instruction &I,
                                                         Value *Data) {
  if (!ClReachesFunctionCallbacks) {
    return;
  }
  const DebugLoc &dbgloc = I.getDebugLoc();
  Value *DataShadow = collapseToPrimitiveShadow(getShadow(Data), IRB);
  ConstantInt *CILine;
  llvm::Value *FilePathPtr;

  if (dbgloc.get() == nullptr) {
    CILine = llvm::ConstantInt::get(I.getContext(), llvm::APInt(32, 0));
    FilePathPtr = IRB.CreateGlobalStringPtr(
        I.getFunction()->getParent()->getSourceFileName());
  } else {
    CILine = llvm::ConstantInt::get(I.getContext(),
                                    llvm::APInt(32, dbgloc.getLine()));
    FilePathPtr =
        IRB.CreateGlobalStringPtr(dbgloc->getFilename());
  }

  llvm::Value *FunctionNamePtr =
      IRB.CreateGlobalStringPtr(I.getFunction()->getName());

  CallInst *CB;
  std::vector<Value *> args;

  if (DFS.shouldTrackOrigins()) {
    Value *DataOrigin = getOrigin(Data);
    args = { DataShadow, DataOrigin, FilePathPtr, CILine, FunctionNamePtr };
    CB = IRB.CreateCall(DFS.DFSanReachesFunctionCallbackOriginFn, args);
  } else {
    args = { DataShadow, FilePathPtr, CILine, FunctionNamePtr };
    CB = IRB.CreateCall(DFS.DFSanReachesFunctionCallbackFn, args);
  }
  CB->addParamAttr(0, Attribute::ZExt);
  CB->setDebugLoc(dbgloc);
}

Type *DataFlowSanitizer::getShadowTy(Type *OrigTy) {
  if (!OrigTy->isSized())
    return PrimitiveShadowTy;
  if (isa<IntegerType>(OrigTy))
    return PrimitiveShadowTy;
  if (isa<VectorType>(OrigTy))
    return PrimitiveShadowTy;
  if (ArrayType *AT = dyn_cast<ArrayType>(OrigTy))
    return ArrayType::get(getShadowTy(AT->getElementType()),
                          AT->getNumElements());
  if (StructType *ST = dyn_cast<StructType>(OrigTy)) {
    SmallVector<Type *, 4> Elements;
    for (unsigned I = 0, N = ST->getNumElements(); I < N; ++I)
      Elements.push_back(getShadowTy(ST->getElementType(I)));
    return StructType::get(*Ctx, Elements);
  }
  return PrimitiveShadowTy;
}

Type *DataFlowSanitizer::getShadowTy(Value *V) {
  return getShadowTy(V->getType());
}

bool DataFlowSanitizer::initializeModule(Module &M) {
  Triple TargetTriple(M.getTargetTriple());
  const DataLayout &DL = M.getDataLayout();

  if (TargetTriple.getOS() != Triple::Linux)
    report_fatal_error("unsupported operating system");
  switch (TargetTriple.getArch()) {
  case Triple::aarch64:
    MapParams = &Linux_AArch64_MemoryMapParams;
    break;
  case Triple::x86_64:
    MapParams = &Linux_X86_64_MemoryMapParams;
    break;
  case Triple::loongarch64:
    MapParams = &Linux_LoongArch64_MemoryMapParams;
    break;
  default:
    report_fatal_error("unsupported architecture");
  }

  Mod = &M;
  Ctx = &M.getContext();
  Int8Ptr = PointerType::getUnqual(*Ctx);
  OriginTy = IntegerType::get(*Ctx, OriginWidthBits);
  OriginPtrTy = PointerType::getUnqual(OriginTy);
  PrimitiveShadowTy = IntegerType::get(*Ctx, ShadowWidthBits);
  PrimitiveShadowPtrTy = PointerType::getUnqual(PrimitiveShadowTy);
  IntptrTy = DL.getIntPtrType(*Ctx);
  ZeroPrimitiveShadow = ConstantInt::getSigned(PrimitiveShadowTy, 0);
  ZeroOrigin = ConstantInt::getSigned(OriginTy, 0);

  Type *DFSanUnionLoadArgs[2] = {PrimitiveShadowPtrTy, IntptrTy};
  DFSanUnionLoadFnTy = FunctionType::get(PrimitiveShadowTy, DFSanUnionLoadArgs,
                                         /*isVarArg=*/false);
  Type *DFSanLoadLabelAndOriginArgs[2] = {Int8Ptr, IntptrTy};
  DFSanLoadLabelAndOriginFnTy =
      FunctionType::get(IntegerType::get(*Ctx, 64), DFSanLoadLabelAndOriginArgs,
                        /*isVarArg=*/false);
  DFSanUnimplementedFnTy = FunctionType::get(
      Type::getVoidTy(*Ctx), PointerType::getUnqual(*Ctx), /*isVarArg=*/false);
  Type *DFSanWrapperExternWeakNullArgs[2] = {Int8Ptr, Int8Ptr};
  DFSanWrapperExternWeakNullFnTy =
      FunctionType::get(Type::getVoidTy(*Ctx), DFSanWrapperExternWeakNullArgs,
                        /*isVarArg=*/false);
  Type *DFSanSetLabelArgs[4] = {PrimitiveShadowTy, OriginTy,
                                PointerType::getUnqual(*Ctx), IntptrTy};
  DFSanSetLabelFnTy = FunctionType::get(Type::getVoidTy(*Ctx),
                                        DFSanSetLabelArgs, /*isVarArg=*/false);
  DFSanNonzeroLabelFnTy = FunctionType::get(Type::getVoidTy(*Ctx), std::nullopt,
                                            /*isVarArg=*/false);
  DFSanVarargWrapperFnTy = FunctionType::get(
      Type::getVoidTy(*Ctx), PointerType::getUnqual(*Ctx), /*isVarArg=*/false);
  DFSanConditionalCallbackFnTy =
      FunctionType::get(Type::getVoidTy(*Ctx), PrimitiveShadowTy,
                        /*isVarArg=*/false);
  Type *DFSanConditionalCallbackOriginArgs[2] = {PrimitiveShadowTy, OriginTy};
  DFSanConditionalCallbackOriginFnTy = FunctionType::get(
      Type::getVoidTy(*Ctx), DFSanConditionalCallbackOriginArgs,
      /*isVarArg=*/false);
  Type *DFSanReachesFunctionCallbackArgs[4] = {PrimitiveShadowTy, Int8Ptr,
                                               OriginTy, Int8Ptr};
  DFSanReachesFunctionCallbackFnTy =
      FunctionType::get(Type::getVoidTy(*Ctx), DFSanReachesFunctionCallbackArgs,
                        /*isVarArg=*/false);
  Type *DFSanReachesFunctionCallbackOriginArgs[5] = {
      PrimitiveShadowTy, OriginTy, Int8Ptr, OriginTy, Int8Ptr};
  DFSanReachesFunctionCallbackOriginFnTy = FunctionType::get(
      Type::getVoidTy(*Ctx), DFSanReachesFunctionCallbackOriginArgs,
      /*isVarArg=*/false);
  DFSanCmpCallbackFnTy =
      FunctionType::get(Type::getVoidTy(*Ctx), PrimitiveShadowTy,
                        /*isVarArg=*/false);
  DFSanChainOriginFnTy =
      FunctionType::get(OriginTy, OriginTy, /*isVarArg=*/false);
  Type *DFSanChainOriginIfTaintedArgs[2] = {PrimitiveShadowTy, OriginTy};
  DFSanChainOriginIfTaintedFnTy = FunctionType::get(
      OriginTy, DFSanChainOriginIfTaintedArgs, /*isVarArg=*/false);
  Type *DFSanMaybeStoreOriginArgs[4] = {IntegerType::get(*Ctx, ShadowWidthBits),
                                        Int8Ptr, IntptrTy, OriginTy};
  DFSanMaybeStoreOriginFnTy = FunctionType::get(
      Type::getVoidTy(*Ctx), DFSanMaybeStoreOriginArgs, /*isVarArg=*/false);
  Type *DFSanMemOriginTransferArgs[3] = {Int8Ptr, Int8Ptr, IntptrTy};
  DFSanMemOriginTransferFnTy = FunctionType::get(
      Type::getVoidTy(*Ctx), DFSanMemOriginTransferArgs, /*isVarArg=*/false);
  Type *DFSanMemShadowOriginTransferArgs[3] = {Int8Ptr, Int8Ptr, IntptrTy};
  DFSanMemShadowOriginTransferFnTy =
      FunctionType::get(Type::getVoidTy(*Ctx), DFSanMemShadowOriginTransferArgs,
                        /*isVarArg=*/false);
  Type *DFSanMemShadowOriginConditionalExchangeArgs[5] = {
      IntegerType::get(*Ctx, 8), Int8Ptr, Int8Ptr, Int8Ptr, IntptrTy};
  DFSanMemShadowOriginConditionalExchangeFnTy = FunctionType::get(
      Type::getVoidTy(*Ctx), DFSanMemShadowOriginConditionalExchangeArgs,
      /*isVarArg=*/false);
  Type *DFSanLoadStoreCallbackArgs[2] = {PrimitiveShadowTy, Int8Ptr};
  DFSanLoadStoreCallbackFnTy =
      FunctionType::get(Type::getVoidTy(*Ctx), DFSanLoadStoreCallbackArgs,
                        /*isVarArg=*/false);
  Type *DFSanMemTransferCallbackArgs[2] = {PrimitiveShadowPtrTy, IntptrTy};
  DFSanMemTransferCallbackFnTy =
      FunctionType::get(Type::getVoidTy(*Ctx), DFSanMemTransferCallbackArgs,
                        /*isVarArg=*/false);

  ColdCallWeights = MDBuilder(*Ctx).createUnlikelyBranchWeights();
  OriginStoreWeights = MDBuilder(*Ctx).createUnlikelyBranchWeights();
  return true;
}

bool DataFlowSanitizer::isInstrumented(const Function *F) {
  return !ABIList.isIn(*F, "uninstrumented");
}

bool DataFlowSanitizer::isInstrumented(const GlobalAlias *GA) {
  return !ABIList.isIn(*GA, "uninstrumented");
}

bool DataFlowSanitizer::isForceZeroLabels(const Function *F) {
  return ABIList.isIn(*F, "force_zero_labels");
}

DataFlowSanitizer::WrapperKind DataFlowSanitizer::getWrapperKind(Function *F) {
  if (ABIList.isIn(*F, "functional"))
    return WK_Functional;
  if (ABIList.isIn(*F, "discard"))
    return WK_Discard;
  if (ABIList.isIn(*F, "custom"))
    return WK_Custom;

  return WK_Warning;
}

void DataFlowSanitizer::addGlobalNameSuffix(GlobalValue *GV) {
  std::string GVName = std::string(GV->getName()), Suffix = ".dfsan";
  GV->setName(GVName + Suffix);

  // Try to change the name of the function in module inline asm.  We only do
  // this for specific asm directives, currently only ".symver", to try to avoid
  // corrupting asm which happens to contain the symbol name as a substring.
  // Note that the substitution for .symver assumes that the versioned symbol
  // also has an instrumented name.
  std::string Asm = GV->getParent()->getModuleInlineAsm();
  std::string SearchStr = ".symver " + GVName + ",";
  size_t Pos = Asm.find(SearchStr);
  if (Pos != std::string::npos) {
    Asm.replace(Pos, SearchStr.size(), ".symver " + GVName + Suffix + ",");
    Pos = Asm.find('@');

    if (Pos == std::string::npos)
      report_fatal_error(Twine("unsupported .symver: ", Asm));

    Asm.replace(Pos, 1, Suffix + "@");
    GV->getParent()->setModuleInlineAsm(Asm);
  }
}

void DataFlowSanitizer::buildExternWeakCheckIfNeeded(IRBuilder<> &IRB,
                                                     Function *F) {
  // If the function we are wrapping was ExternWeak, it may be null.
  // The original code before calling this wrapper may have checked for null,
  // but replacing with a known-to-not-be-null wrapper can break this check.
  // When replacing uses of the extern weak function with the wrapper we try
  // to avoid replacing uses in conditionals, but this is not perfect.
  // In the case where we fail, and accidentally optimize out a null check
  // for a extern weak function, add a check here to help identify the issue.
  if (GlobalValue::isExternalWeakLinkage(F->getLinkage())) {
    std::vector<Value *> Args;
    Args.push_back(F);
    Args.push_back(IRB.CreateGlobalStringPtr(F->getName()));
    IRB.CreateCall(DFSanWrapperExternWeakNullFn, Args);
  }
}

Function *
DataFlowSanitizer::buildWrapperFunction(Function *F, StringRef NewFName,
                                        GlobalValue::LinkageTypes NewFLink,
                                        FunctionType *NewFT) {
  FunctionType *FT = F->getFunctionType();
  Function *NewF = Function::Create(NewFT, NewFLink, F->getAddressSpace(),
                                    NewFName, F->getParent());
  NewF->copyAttributesFrom(F);
  NewF->removeRetAttrs(
      AttributeFuncs::typeIncompatible(NewFT->getReturnType()));

  BasicBlock *BB = BasicBlock::Create(*Ctx, "entry", NewF);
  if (F->isVarArg()) {
    NewF->removeFnAttr("split-stack");
    CallInst::Create(DFSanVarargWrapperFn,
                     IRBuilder<>(BB).CreateGlobalStringPtr(F->getName()), "",
                     BB);
    new UnreachableInst(*Ctx, BB);
  } else {
    auto ArgIt = pointer_iterator<Argument *>(NewF->arg_begin());
    std::vector<Value *> Args(ArgIt, ArgIt + FT->getNumParams());

    CallInst *CI = CallInst::Create(F, Args, "", BB);
    if (FT->getReturnType()->isVoidTy())
      ReturnInst::Create(*Ctx, BB);
    else
      ReturnInst::Create(*Ctx, CI, BB);
  }

  return NewF;
}

// Initialize DataFlowSanitizer runtime functions and declare them in the module
void DataFlowSanitizer::initializeRuntimeFunctions(Module &M) {
  LLVMContext &C = M.getContext();
  {
    AttributeList AL;
    AL = AL.addFnAttribute(C, Attribute::NoUnwind);
    AL = AL.addFnAttribute(
        C, Attribute::getWithMemoryEffects(C, MemoryEffects::readOnly()));
    AL = AL.addRetAttribute(C, Attribute::ZExt);
    DFSanUnionLoadFn =
        Mod->getOrInsertFunction("__dfsan_union_load", DFSanUnionLoadFnTy, AL);
  }
  {
    AttributeList AL;
    AL = AL.addFnAttribute(C, Attribute::NoUnwind);
    AL = AL.addFnAttribute(
        C, Attribute::getWithMemoryEffects(C, MemoryEffects::readOnly()));
    AL = AL.addRetAttribute(C, Attribute::ZExt);
    DFSanLoadLabelAndOriginFn = Mod->getOrInsertFunction(
        "__dfsan_load_label_and_origin", DFSanLoadLabelAndOriginFnTy, AL);
  }
  DFSanUnimplementedFn =
      Mod->getOrInsertFunction("__dfsan_unimplemented", DFSanUnimplementedFnTy);
  DFSanWrapperExternWeakNullFn = Mod->getOrInsertFunction(
      "__dfsan_wrapper_extern_weak_null", DFSanWrapperExternWeakNullFnTy);
  {
    AttributeList AL;
    AL = AL.addParamAttribute(M.getContext(), 0, Attribute::ZExt);
    AL = AL.addParamAttribute(M.getContext(), 1, Attribute::ZExt);
    DFSanSetLabelFn =
        Mod->getOrInsertFunction("__dfsan_set_label", DFSanSetLabelFnTy, AL);
  }
  DFSanNonzeroLabelFn =
      Mod->getOrInsertFunction("__dfsan_nonzero_label", DFSanNonzeroLabelFnTy);
  DFSanVarargWrapperFn = Mod->getOrInsertFunction("__dfsan_vararg_wrapper",
                                                  DFSanVarargWrapperFnTy);
  {
    AttributeList AL;
    AL = AL.addParamAttribute(M.getContext(), 0, Attribute::ZExt);
    AL = AL.addRetAttribute(M.getContext(), Attribute::ZExt);
    DFSanChainOriginFn = Mod->getOrInsertFunction("__dfsan_chain_origin",
                                                  DFSanChainOriginFnTy, AL);
  }
  {
    AttributeList AL;
    AL = AL.addParamAttribute(M.getContext(), 0, Attribute::ZExt);
    AL = AL.addParamAttribute(M.getContext(), 1, Attribute::ZExt);
    AL = AL.addRetAttribute(M.getContext(), Attribute::ZExt);
    DFSanChainOriginIfTaintedFn = Mod->getOrInsertFunction(
        "__dfsan_chain_origin_if_tainted", DFSanChainOriginIfTaintedFnTy, AL);
  }
  DFSanMemOriginTransferFn = Mod->getOrInsertFunction(
      "__dfsan_mem_origin_transfer", DFSanMemOriginTransferFnTy);

  DFSanMemShadowOriginTransferFn = Mod->getOrInsertFunction(
      "__dfsan_mem_shadow_origin_transfer", DFSanMemShadowOriginTransferFnTy);

  DFSanMemShadowOriginConditionalExchangeFn =
      Mod->getOrInsertFunction("__dfsan_mem_shadow_origin_conditional_exchange",
                               DFSanMemShadowOriginConditionalExchangeFnTy);

  {
    AttributeList AL;
    AL = AL.addParamAttribute(M.getContext(), 0, Attribute::ZExt);
    AL = AL.addParamAttribute(M.getContext(), 3, Attribute::ZExt);
    DFSanMaybeStoreOriginFn = Mod->getOrInsertFunction(
        "__dfsan_maybe_store_origin", DFSanMaybeStoreOriginFnTy, AL);
  }

  DFSanRuntimeFunctions.insert(
      DFSanUnionLoadFn.getCallee()->stripPointerCasts());
  DFSanRuntimeFunctions.insert(
      DFSanLoadLabelAndOriginFn.getCallee()->stripPointerCasts());
  DFSanRuntimeFunctions.insert(
      DFSanUnimplementedFn.getCallee()->stripPointerCasts());
  DFSanRuntimeFunctions.insert(
      DFSanWrapperExternWeakNullFn.getCallee()->stripPointerCasts());
  DFSanRuntimeFunctions.insert(
      DFSanSetLabelFn.getCallee()->stripPointerCasts());
  DFSanRuntimeFunctions.insert(
      DFSanNonzeroLabelFn.getCallee()->stripPointerCasts());
  DFSanRuntimeFunctions.insert(
      DFSanVarargWrapperFn.getCallee()->stripPointerCasts());
  DFSanRuntimeFunctions.insert(
      DFSanLoadCallbackFn.getCallee()->stripPointerCasts());
  DFSanRuntimeFunctions.insert(
      DFSanStoreCallbackFn.getCallee()->stripPointerCasts());
  DFSanRuntimeFunctions.insert(
      DFSanMemTransferCallbackFn.getCallee()->stripPointerCasts());
  DFSanRuntimeFunctions.insert(
      DFSanConditionalCallbackFn.getCallee()->stripPointerCasts());
  DFSanRuntimeFunctions.insert(
      DFSanConditionalCallbackOriginFn.getCallee()->stripPointerCasts());
  DFSanRuntimeFunctions.insert(
      DFSanReachesFunctionCallbackFn.getCallee()->stripPointerCasts());
  DFSanRuntimeFunctions.insert(
      DFSanReachesFunctionCallbackOriginFn.getCallee()->stripPointerCasts());
  DFSanRuntimeFunctions.insert(
      DFSanCmpCallbackFn.getCallee()->stripPointerCasts());
  DFSanRuntimeFunctions.insert(
      DFSanChainOriginFn.getCallee()->stripPointerCasts());
  DFSanRuntimeFunctions.insert(
      DFSanChainOriginIfTaintedFn.getCallee()->stripPointerCasts());
  DFSanRuntimeFunctions.insert(
      DFSanMemOriginTransferFn.getCallee()->stripPointerCasts());
  DFSanRuntimeFunctions.insert(
      DFSanMemShadowOriginTransferFn.getCallee()->stripPointerCasts());
  DFSanRuntimeFunctions.insert(
      DFSanMemShadowOriginConditionalExchangeFn.getCallee()
          ->stripPointerCasts());
  DFSanRuntimeFunctions.insert(
      DFSanMaybeStoreOriginFn.getCallee()->stripPointerCasts());
}

// Initializes event callback functions and declare them in the module
void DataFlowSanitizer::initializeCallbackFunctions(Module &M) {
  {
    AttributeList AL;
    AL = AL.addParamAttribute(M.getContext(), 0, Attribute::ZExt);
    DFSanLoadCallbackFn = Mod->getOrInsertFunction(
        "__dfsan_load_callback", DFSanLoadStoreCallbackFnTy, AL);
  }
  {
    AttributeList AL;
    AL = AL.addParamAttribute(M.getContext(), 0, Attribute::ZExt);
    DFSanStoreCallbackFn = Mod->getOrInsertFunction(
        "__dfsan_store_callback", DFSanLoadStoreCallbackFnTy, AL);
  }
  DFSanMemTransferCallbackFn = Mod->getOrInsertFunction(
      "__dfsan_mem_transfer_callback", DFSanMemTransferCallbackFnTy);
  {
    AttributeList AL;
    AL = AL.addParamAttribute(M.getContext(), 0, Attribute::ZExt);
    DFSanCmpCallbackFn = Mod->getOrInsertFunction("__dfsan_cmp_callback",
                                                  DFSanCmpCallbackFnTy, AL);
  }
  {
    AttributeList AL;
    AL = AL.addParamAttribute(M.getContext(), 0, Attribute::ZExt);
    DFSanConditionalCallbackFn = Mod->getOrInsertFunction(
        "__dfsan_conditional_callback", DFSanConditionalCallbackFnTy, AL);
  }
  {
    AttributeList AL;
    AL = AL.addParamAttribute(M.getContext(), 0, Attribute::ZExt);
    DFSanConditionalCallbackOriginFn =
        Mod->getOrInsertFunction("__dfsan_conditional_callback_origin",
                                 DFSanConditionalCallbackOriginFnTy, AL);
  }
  {
    AttributeList AL;
    AL = AL.addParamAttribute(M.getContext(), 0, Attribute::ZExt);
    DFSanReachesFunctionCallbackFn =
        Mod->getOrInsertFunction("__dfsan_reaches_function_callback",
                                 DFSanReachesFunctionCallbackFnTy, AL);
  }
  {
    AttributeList AL;
    AL = AL.addParamAttribute(M.getContext(), 0, Attribute::ZExt);
    DFSanReachesFunctionCallbackOriginFn =
        Mod->getOrInsertFunction("__dfsan_reaches_function_callback_origin",
                                 DFSanReachesFunctionCallbackOriginFnTy, AL);
  }
}

bool DataFlowSanitizer::runImpl(
    Module &M, llvm::function_ref<TargetLibraryInfo &(Function &)> GetTLI) {
  initializeModule(M);

  if (ABIList.isIn(M, "skip"))
    return false;

  const unsigned InitialGlobalSize = M.global_size();
  const unsigned InitialModuleSize = M.size();

  bool Changed = false;

  auto GetOrInsertGlobal = [this, &Changed](StringRef Name,
                                            Type *Ty) -> Constant * {
    Constant *C = Mod->getOrInsertGlobal(Name, Ty);
    if (GlobalVariable *G = dyn_cast<GlobalVariable>(C)) {
      Changed |= G->getThreadLocalMode() != GlobalVariable::InitialExecTLSModel;
      G->setThreadLocalMode(GlobalVariable::InitialExecTLSModel);
    }
    return C;
  };

  // These globals must be kept in sync with the ones in dfsan.cpp.
  ArgTLS =
      GetOrInsertGlobal("__dfsan_arg_tls",
                        ArrayType::get(Type::getInt64Ty(*Ctx), ArgTLSSize / 8));
  RetvalTLS = GetOrInsertGlobal(
      "__dfsan_retval_tls",
      ArrayType::get(Type::getInt64Ty(*Ctx), RetvalTLSSize / 8));
  ArgOriginTLSTy = ArrayType::get(OriginTy, NumOfElementsInArgOrgTLS);
  ArgOriginTLS = GetOrInsertGlobal("__dfsan_arg_origin_tls", ArgOriginTLSTy);
  RetvalOriginTLS = GetOrInsertGlobal("__dfsan_retval_origin_tls", OriginTy);

  (void)Mod->getOrInsertGlobal("__dfsan_track_origins", OriginTy, [&] {
    Changed = true;
    return new GlobalVariable(
        M, OriginTy, true, GlobalValue::WeakODRLinkage,
        ConstantInt::getSigned(OriginTy,
                               shouldTrackOrigins() ? ClTrackOrigins : 0),
        "__dfsan_track_origins");
  });

  initializeCallbackFunctions(M);
  initializeRuntimeFunctions(M);

  std::vector<Function *> FnsToInstrument;
  SmallPtrSet<Function *, 2> FnsWithNativeABI;
  SmallPtrSet<Function *, 2> FnsWithForceZeroLabel;
  SmallPtrSet<Constant *, 1> PersonalityFns;
  for (Function &F : M)
    if (!F.isIntrinsic() && !DFSanRuntimeFunctions.contains(&F) &&
        !LibAtomicFunction(F) &&
        !F.hasFnAttribute(Attribute::DisableSanitizerInstrumentation)) {
      FnsToInstrument.push_back(&F);
      if (F.hasPersonalityFn())
        PersonalityFns.insert(F.getPersonalityFn()->stripPointerCasts());
    }

  if (ClIgnorePersonalityRoutine) {
    for (auto *C : PersonalityFns) {
      assert(isa<Function>(C) && "Personality routine is not a function!");
      Function *F = cast<Function>(C);
      if (!isInstrumented(F))
        llvm::erase(FnsToInstrument, F);
    }
  }

  // Give function aliases prefixes when necessary, and build wrappers where the
  // instrumentedness is inconsistent.
  for (GlobalAlias &GA : llvm::make_early_inc_range(M.aliases())) {
    // Don't stop on weak.  We assume people aren't playing games with the
    // instrumentedness of overridden weak aliases.
    auto *F = dyn_cast<Function>(GA.getAliaseeObject());
    if (!F)
      continue;

    bool GAInst = isInstrumented(&GA), FInst = isInstrumented(F);
    if (GAInst && FInst) {
      addGlobalNameSuffix(&GA);
    } else if (GAInst != FInst) {
      // Non-instrumented alias of an instrumented function, or vice versa.
      // Replace the alias with a native-ABI wrapper of the aliasee.  The pass
      // below will take care of instrumenting it.
      Function *NewF =
          buildWrapperFunction(F, "", GA.getLinkage(), F->getFunctionType());
      GA.replaceAllUsesWith(NewF);
      NewF->takeName(&GA);
      GA.eraseFromParent();
      FnsToInstrument.push_back(NewF);
    }
  }

  // TODO: This could be more precise.
  ReadOnlyNoneAttrs.addAttribute(Attribute::Memory);

  // First, change the ABI of every function in the module.  ABI-listed
  // functions keep their original ABI and get a wrapper function.
  for (std::vector<Function *>::iterator FI = FnsToInstrument.begin(),
                                         FE = FnsToInstrument.end();
       FI != FE; ++FI) {
    Function &F = **FI;
    FunctionType *FT = F.getFunctionType();

    bool IsZeroArgsVoidRet = (FT->getNumParams() == 0 && !FT->isVarArg() &&
                              FT->getReturnType()->isVoidTy());

    if (isInstrumented(&F)) {
      if (isForceZeroLabels(&F))
        FnsWithForceZeroLabel.insert(&F);

      // Instrumented functions get a '.dfsan' suffix.  This allows us to more
      // easily identify cases of mismatching ABIs. This naming scheme is
      // mangling-compatible (see Itanium ABI), using a vendor-specific suffix.
      addGlobalNameSuffix(&F);
    } else if (!IsZeroArgsVoidRet || getWrapperKind(&F) == WK_Custom) {
      // Build a wrapper function for F.  The wrapper simply calls F, and is
      // added to FnsToInstrument so that any instrumentation according to its
      // WrapperKind is done in the second pass below.

      // If the function being wrapped has local linkage, then preserve the
      // function's linkage in the wrapper function.
      GlobalValue::LinkageTypes WrapperLinkage =
          F.hasLocalLinkage() ? F.getLinkage()
                              : GlobalValue::LinkOnceODRLinkage;

      Function *NewF = buildWrapperFunction(
          &F,
          (shouldTrackOrigins() ? std::string("dfso$") : std::string("dfsw$")) +
              std::string(F.getName()),
          WrapperLinkage, FT);
      NewF->removeFnAttrs(ReadOnlyNoneAttrs);

      // Extern weak functions can sometimes be null at execution time.
      // Code will sometimes check if an extern weak function is null.
      // This could look something like:
      //   declare extern_weak i8 @my_func(i8)
      //   br i1 icmp ne (i8 (i8)* @my_func, i8 (i8)* null), label %use_my_func,
      //   label %avoid_my_func
      // The @"dfsw$my_func" wrapper is never null, so if we replace this use
      // in the comparison, the icmp will simplify to false and we have
      // accidentally optimized away a null check that is necessary.
      // This can lead to a crash when the null extern_weak my_func is called.
      //
      // To prevent (the most common pattern of) this problem,
      // do not replace uses in comparisons with the wrapper.
      // We definitely want to replace uses in call instructions.
      // Other uses (e.g. store the function address somewhere) might be
      // called or compared or both - this case may not be handled correctly.
      // We will default to replacing with wrapper in cases we are unsure.
      auto IsNotCmpUse = [](Use &U) -> bool {
        User *Usr = U.getUser();
        if (ConstantExpr *CE = dyn_cast<ConstantExpr>(Usr)) {
          // This is the most common case for icmp ne null
          if (CE->getOpcode() == Instruction::ICmp) {
            return false;
          }
        }
        if (Instruction *I = dyn_cast<Instruction>(Usr)) {
          if (I->getOpcode() == Instruction::ICmp) {
            return false;
          }
        }
        return true;
      };
      F.replaceUsesWithIf(NewF, IsNotCmpUse);

      UnwrappedFnMap[NewF] = &F;
      *FI = NewF;

      if (!F.isDeclaration()) {
        // This function is probably defining an interposition of an
        // uninstrumented function and hence needs to keep the original ABI.
        // But any functions it may call need to use the instrumented ABI, so
        // we instrument it in a mode which preserves the original ABI.
        FnsWithNativeABI.insert(&F);

        // This code needs to rebuild the iterators, as they may be invalidated
        // by the push_back, taking care that the new range does not include
        // any functions added by this code.
        size_t N = FI - FnsToInstrument.begin(),
               Count = FE - FnsToInstrument.begin();
        FnsToInstrument.push_back(&F);
        FI = FnsToInstrument.begin() + N;
        FE = FnsToInstrument.begin() + Count;
      }
      // Hopefully, nobody will try to indirectly call a vararg
      // function... yet.
    } else if (FT->isVarArg()) {
      UnwrappedFnMap[&F] = &F;
      *FI = nullptr;
    }
  }

  for (Function *F : FnsToInstrument) {
    if (!F || F->isDeclaration())
      continue;

    removeUnreachableBlocks(*F);

    DFSanFunction DFSF(*this, F, FnsWithNativeABI.count(F),
                       FnsWithForceZeroLabel.count(F), GetTLI(*F));

    if (ClReachesFunctionCallbacks) {
      // Add callback for arguments reaching this function.
      for (auto &FArg : F->args()) {
        Instruction *Next = &F->getEntryBlock().front();
        Value *FArgShadow = DFSF.getShadow(&FArg);
        if (isZeroShadow(FArgShadow))
          continue;
        if (Instruction *FArgShadowInst = dyn_cast<Instruction>(FArgShadow)) {
          Next = FArgShadowInst->getNextNode();
        }
        if (shouldTrackOrigins()) {
          if (Instruction *Origin =
                  dyn_cast<Instruction>(DFSF.getOrigin(&FArg))) {
            // Ensure IRB insertion point is after loads for shadow and origin.
            Instruction *OriginNext = Origin->getNextNode();
            if (Next->comesBefore(OriginNext)) {
              Next = OriginNext;
            }
          }
        }
        IRBuilder<> IRB(Next);
        DFSF.addReachesFunctionCallbacksIfEnabled(IRB, *Next, &FArg);
      }
    }

    // DFSanVisitor may create new basic blocks, which confuses df_iterator.
    // Build a copy of the list before iterating over it.
    SmallVector<BasicBlock *, 4> BBList(depth_first(&F->getEntryBlock()));

    for (BasicBlock *BB : BBList) {
      Instruction *Inst = &BB->front();
      while (true) {
        // DFSanVisitor may split the current basic block, changing the current
        // instruction's next pointer and moving the next instruction to the
        // tail block from which we should continue.
        Instruction *Next = Inst->getNextNode();
        // DFSanVisitor may delete Inst, so keep track of whether it was a
        // terminator.
        bool IsTerminator = Inst->isTerminator();
        if (!DFSF.SkipInsts.count(Inst))
          DFSanVisitor(DFSF).visit(Inst);
        if (IsTerminator)
          break;
        Inst = Next;
      }
    }

    // We will not necessarily be able to compute the shadow for every phi node
    // until we have visited every block.  Therefore, the code that handles phi
    // nodes adds them to the PHIFixups list so that they can be properly
    // handled here.
    for (DFSanFunction::PHIFixupElement &P : DFSF.PHIFixups) {
      for (unsigned Val = 0, N = P.Phi->getNumIncomingValues(); Val != N;
           ++Val) {
        P.ShadowPhi->setIncomingValue(
            Val, DFSF.getShadow(P.Phi->getIncomingValue(Val)));
        if (P.OriginPhi)
          P.OriginPhi->setIncomingValue(
              Val, DFSF.getOrigin(P.Phi->getIncomingValue(Val)));
      }
    }

    // -dfsan-debug-nonzero-labels will split the CFG in all kinds of crazy
    // places (i.e. instructions in basic blocks we haven't even begun visiting
    // yet).  To make our life easier, do this work in a pass after the main
    // instrumentation.
    if (ClDebugNonzeroLabels) {
      for (Value *V : DFSF.NonZeroChecks) {
        BasicBlock::iterator Pos;
        if (Instruction *I = dyn_cast<Instruction>(V))
          Pos = std::next(I->getIterator());
        else
          Pos = DFSF.F->getEntryBlock().begin();
        while (isa<PHINode>(Pos) || isa<AllocaInst>(Pos))
          Pos = std::next(Pos->getIterator());
        IRBuilder<> IRB(Pos->getParent(), Pos);
        Value *PrimitiveShadow = DFSF.collapseToPrimitiveShadow(V, Pos);
        Value *Ne =
            IRB.CreateICmpNE(PrimitiveShadow, DFSF.DFS.ZeroPrimitiveShadow);
        BranchInst *BI = cast<BranchInst>(SplitBlockAndInsertIfThen(
            Ne, Pos, /*Unreachable=*/false, ColdCallWeights));
        IRBuilder<> ThenIRB(BI);
        ThenIRB.CreateCall(DFSF.DFS.DFSanNonzeroLabelFn, {});
      }
    }
  }

  return Changed || !FnsToInstrument.empty() ||
         M.global_size() != InitialGlobalSize || M.size() != InitialModuleSize;
}

Value *DFSanFunction::getArgTLS(Type *T, unsigned ArgOffset, IRBuilder<> &IRB) {
  Value *Base = IRB.CreatePointerCast(DFS.ArgTLS, DFS.IntptrTy);
  if (ArgOffset)
    Base = IRB.CreateAdd(Base, ConstantInt::get(DFS.IntptrTy, ArgOffset));
  return IRB.CreateIntToPtr(Base, PointerType::get(DFS.getShadowTy(T), 0),
                            "_dfsarg");
}

Value *DFSanFunction::getRetvalTLS(Type *T, IRBuilder<> &IRB) {
  return IRB.CreatePointerCast(
      DFS.RetvalTLS, PointerType::get(DFS.getShadowTy(T), 0), "_dfsret");
}

Value *DFSanFunction::getRetvalOriginTLS() { return DFS.RetvalOriginTLS; }

Value *DFSanFunction::getArgOriginTLS(unsigned ArgNo, IRBuilder<> &IRB) {
  return IRB.CreateConstInBoundsGEP2_64(DFS.ArgOriginTLSTy, DFS.ArgOriginTLS, 0,
                                        ArgNo, "_dfsarg_o");
}

Value *DFSanFunction::getOrigin(Value *V) {
  assert(DFS.shouldTrackOrigins());
  if (!isa<Argument>(V) && !isa<Instruction>(V))
    return DFS.ZeroOrigin;
  Value *&Origin = ValOriginMap[V];
  if (!Origin) {
    if (Argument *A = dyn_cast<Argument>(V)) {
      if (IsNativeABI)
        return DFS.ZeroOrigin;
      if (A->getArgNo() < DFS.NumOfElementsInArgOrgTLS) {
        Instruction *ArgOriginTLSPos = &*F->getEntryBlock().begin();
        IRBuilder<> IRB(ArgOriginTLSPos);
        Value *ArgOriginPtr = getArgOriginTLS(A->getArgNo(), IRB);
        Origin = IRB.CreateLoad(DFS.OriginTy, ArgOriginPtr);
      } else {
        // Overflow
        Origin = DFS.ZeroOrigin;
      }
    } else {
      Origin = DFS.ZeroOrigin;
    }
  }
  return Origin;
}

void DFSanFunction::setOrigin(Instruction *I, Value *Origin) {
  if (!DFS.shouldTrackOrigins())
    return;
  assert(!ValOriginMap.count(I));
  assert(Origin->getType() == DFS.OriginTy);
  ValOriginMap[I] = Origin;
}

Value *DFSanFunction::getShadowForTLSArgument(Argument *A) {
  unsigned ArgOffset = 0;
  const DataLayout &DL = F->getDataLayout();
  for (auto &FArg : F->args()) {
    if (!FArg.getType()->isSized()) {
      if (A == &FArg)
        break;
      continue;
    }

    unsigned Size = DL.getTypeAllocSize(DFS.getShadowTy(&FArg));
    if (A != &FArg) {
      ArgOffset += alignTo(Size, ShadowTLSAlignment);
      if (ArgOffset > ArgTLSSize)
        break; // ArgTLS overflows, uses a zero shadow.
      continue;
    }

    if (ArgOffset + Size > ArgTLSSize)
      break; // ArgTLS overflows, uses a zero shadow.

    Instruction *ArgTLSPos = &*F->getEntryBlock().begin();
    IRBuilder<> IRB(ArgTLSPos);
    Value *ArgShadowPtr = getArgTLS(FArg.getType(), ArgOffset, IRB);
    return IRB.CreateAlignedLoad(DFS.getShadowTy(&FArg), ArgShadowPtr,
                                 ShadowTLSAlignment);
  }

  return DFS.getZeroShadow(A);
}

Value *DFSanFunction::getShadow(Value *V) {
  if (!isa<Argument>(V) && !isa<Instruction>(V))
    return DFS.getZeroShadow(V);
  if (IsForceZeroLabels)
    return DFS.getZeroShadow(V);
  Value *&Shadow = ValShadowMap[V];
  if (!Shadow) {
    if (Argument *A = dyn_cast<Argument>(V)) {
      if (IsNativeABI)
        return DFS.getZeroShadow(V);
      Shadow = getShadowForTLSArgument(A);
      NonZeroChecks.push_back(Shadow);
    } else {
      Shadow = DFS.getZeroShadow(V);
    }
  }
  return Shadow;
}

void DFSanFunction::setShadow(Instruction *I, Value *Shadow) {
  assert(!ValShadowMap.count(I));
  ValShadowMap[I] = Shadow;
}

/// Compute the integer shadow offset that corresponds to a given
/// application address.
///
/// Offset = (Addr & ~AndMask) ^ XorMask
Value *DataFlowSanitizer::getShadowOffset(Value *Addr, IRBuilder<> &IRB) {
  assert(Addr != RetvalTLS && "Reinstrumenting?");
  Value *OffsetLong = IRB.CreatePointerCast(Addr, IntptrTy);

  uint64_t AndMask = MapParams->AndMask;
  if (AndMask)
    OffsetLong =
        IRB.CreateAnd(OffsetLong, ConstantInt::get(IntptrTy, ~AndMask));

  uint64_t XorMask = MapParams->XorMask;
  if (XorMask)
    OffsetLong = IRB.CreateXor(OffsetLong, ConstantInt::get(IntptrTy, XorMask));
  return OffsetLong;
}

std::pair<Value *, Value *>
DataFlowSanitizer::getShadowOriginAddress(Value *Addr, Align InstAlignment,
                                          BasicBlock::iterator Pos) {
  // Returns ((Addr & shadow_mask) + origin_base - shadow_base) & ~4UL
  IRBuilder<> IRB(Pos->getParent(), Pos);
  Value *ShadowOffset = getShadowOffset(Addr, IRB);
  Value *ShadowLong = ShadowOffset;
  uint64_t ShadowBase = MapParams->ShadowBase;
  if (ShadowBase != 0) {
    ShadowLong =
        IRB.CreateAdd(ShadowLong, ConstantInt::get(IntptrTy, ShadowBase));
  }
  IntegerType *ShadowTy = IntegerType::get(*Ctx, ShadowWidthBits);
  Value *ShadowPtr =
      IRB.CreateIntToPtr(ShadowLong, PointerType::get(ShadowTy, 0));
  Value *OriginPtr = nullptr;
  if (shouldTrackOrigins()) {
    Value *OriginLong = ShadowOffset;
    uint64_t OriginBase = MapParams->OriginBase;
    if (OriginBase != 0)
      OriginLong =
          IRB.CreateAdd(OriginLong, ConstantInt::get(IntptrTy, OriginBase));
    const Align Alignment = llvm::assumeAligned(InstAlignment.value());
    // When alignment is >= 4, Addr must be aligned to 4, otherwise it is UB.
    // So Mask is unnecessary.
    if (Alignment < MinOriginAlignment) {
      uint64_t Mask = MinOriginAlignment.value() - 1;
      OriginLong = IRB.CreateAnd(OriginLong, ConstantInt::get(IntptrTy, ~Mask));
    }
    OriginPtr = IRB.CreateIntToPtr(OriginLong, OriginPtrTy);
  }
  return std::make_pair(ShadowPtr, OriginPtr);
}

Value *DataFlowSanitizer::getShadowAddress(Value *Addr,
                                           BasicBlock::iterator Pos,
                                           Value *ShadowOffset) {
  IRBuilder<> IRB(Pos->getParent(), Pos);
  return IRB.CreateIntToPtr(ShadowOffset, PrimitiveShadowPtrTy);
}

Value *DataFlowSanitizer::getShadowAddress(Value *Addr,
                                           BasicBlock::iterator Pos) {
  IRBuilder<> IRB(Pos->getParent(), Pos);
  Value *ShadowOffset = getShadowOffset(Addr, IRB);
  return getShadowAddress(Addr, Pos, ShadowOffset);
}

Value *DFSanFunction::combineShadowsThenConvert(Type *T, Value *V1, Value *V2,
                                                BasicBlock::iterator Pos) {
  Value *PrimitiveValue = combineShadows(V1, V2, Pos);
  return expandFromPrimitiveShadow(T, PrimitiveValue, Pos);
}

// Generates IR to compute the union of the two given shadows, inserting it
// before Pos. The combined value is with primitive type.
Value *DFSanFunction::combineShadows(Value *V1, Value *V2,
                                     BasicBlock::iterator Pos) {
  if (DFS.isZeroShadow(V1))
    return collapseToPrimitiveShadow(V2, Pos);
  if (DFS.isZeroShadow(V2))
    return collapseToPrimitiveShadow(V1, Pos);
  if (V1 == V2)
    return collapseToPrimitiveShadow(V1, Pos);

  auto V1Elems = ShadowElements.find(V1);
  auto V2Elems = ShadowElements.find(V2);
  if (V1Elems != ShadowElements.end() && V2Elems != ShadowElements.end()) {
    if (std::includes(V1Elems->second.begin(), V1Elems->second.end(),
                      V2Elems->second.begin(), V2Elems->second.end())) {
      return collapseToPrimitiveShadow(V1, Pos);
    }
    if (std::includes(V2Elems->second.begin(), V2Elems->second.end(),
                      V1Elems->second.begin(), V1Elems->second.end())) {
      return collapseToPrimitiveShadow(V2, Pos);
    }
  } else if (V1Elems != ShadowElements.end()) {
    if (V1Elems->second.count(V2))
      return collapseToPrimitiveShadow(V1, Pos);
  } else if (V2Elems != ShadowElements.end()) {
    if (V2Elems->second.count(V1))
      return collapseToPrimitiveShadow(V2, Pos);
  }

  auto Key = std::make_pair(V1, V2);
  if (V1 > V2)
    std::swap(Key.first, Key.second);
  CachedShadow &CCS = CachedShadows[Key];
  if (CCS.Block && DT.dominates(CCS.Block, Pos->getParent()))
    return CCS.Shadow;

  // Converts inputs shadows to shadows with primitive types.
  Value *PV1 = collapseToPrimitiveShadow(V1, Pos);
  Value *PV2 = collapseToPrimitiveShadow(V2, Pos);

  IRBuilder<> IRB(Pos->getParent(), Pos);
  CCS.Block = Pos->getParent();
  CCS.Shadow = IRB.CreateOr(PV1, PV2);

  std::set<Value *> UnionElems;
  if (V1Elems != ShadowElements.end()) {
    UnionElems = V1Elems->second;
  } else {
    UnionElems.insert(V1);
  }
  if (V2Elems != ShadowElements.end()) {
    UnionElems.insert(V2Elems->second.begin(), V2Elems->second.end());
  } else {
    UnionElems.insert(V2);
  }
  ShadowElements[CCS.Shadow] = std::move(UnionElems);

  return CCS.Shadow;
}

// A convenience function which folds the shadows of each of the operands
// of the provided instruction Inst, inserting the IR before Inst.  Returns
// the computed union Value.
Value *DFSanFunction::combineOperandShadows(Instruction *Inst) {
  if (Inst->getNumOperands() == 0)
    return DFS.getZeroShadow(Inst);

  Value *Shadow = getShadow(Inst->getOperand(0));
  for (unsigned I = 1, N = Inst->getNumOperands(); I < N; ++I)
    Shadow = combineShadows(Shadow, getShadow(Inst->getOperand(I)),
                            Inst->getIterator());

  return expandFromPrimitiveShadow(Inst->getType(), Shadow,
                                   Inst->getIterator());
}

void DFSanVisitor::visitInstOperands(Instruction &I) {
  Value *CombinedShadow = DFSF.combineOperandShadows(&I);
  DFSF.setShadow(&I, CombinedShadow);
  visitInstOperandOrigins(I);
}

Value *DFSanFunction::combineOrigins(const std::vector<Value *> &Shadows,
                                     const std::vector<Value *> &Origins,
                                     BasicBlock::iterator Pos,
                                     ConstantInt *Zero) {
  assert(Shadows.size() == Origins.size());
  size_t Size = Origins.size();
  if (Size == 0)
    return DFS.ZeroOrigin;
  Value *Origin = nullptr;
  if (!Zero)
    Zero = DFS.ZeroPrimitiveShadow;
  for (size_t I = 0; I != Size; ++I) {
    Value *OpOrigin = Origins[I];
    Constant *ConstOpOrigin = dyn_cast<Constant>(OpOrigin);
    if (ConstOpOrigin && ConstOpOrigin->isNullValue())
      continue;
    if (!Origin) {
      Origin = OpOrigin;
      continue;
    }
    Value *OpShadow = Shadows[I];
    Value *PrimitiveShadow = collapseToPrimitiveShadow(OpShadow, Pos);
    IRBuilder<> IRB(Pos->getParent(), Pos);
    Value *Cond = IRB.CreateICmpNE(PrimitiveShadow, Zero);
    Origin = IRB.CreateSelect(Cond, OpOrigin, Origin);
  }
  return Origin ? Origin : DFS.ZeroOrigin;
}

Value *DFSanFunction::combineOperandOrigins(Instruction *Inst) {
  size_t Size = Inst->getNumOperands();
  std::vector<Value *> Shadows(Size);
  std::vector<Value *> Origins(Size);
  for (unsigned I = 0; I != Size; ++I) {
    Shadows[I] = getShadow(Inst->getOperand(I));
    Origins[I] = getOrigin(Inst->getOperand(I));
  }
  return combineOrigins(Shadows, Origins, Inst->getIterator());
}

void DFSanVisitor::visitInstOperandOrigins(Instruction &I) {
  if (!DFSF.DFS.shouldTrackOrigins())
    return;
  Value *CombinedOrigin = DFSF.combineOperandOrigins(&I);
  DFSF.setOrigin(&I, CombinedOrigin);
}

Align DFSanFunction::getShadowAlign(Align InstAlignment) {
  const Align Alignment = ClPreserveAlignment ? InstAlignment : Align(1);
  return Align(Alignment.value() * DFS.ShadowWidthBytes);
}

Align DFSanFunction::getOriginAlign(Align InstAlignment) {
  const Align Alignment = llvm::assumeAligned(InstAlignment.value());
  return Align(std::max(MinOriginAlignment, Alignment));
}

bool DFSanFunction::isLookupTableConstant(Value *P) {
  if (GlobalVariable *GV = dyn_cast<GlobalVariable>(P->stripPointerCasts()))
    if (GV->isConstant() && GV->hasName())
      return DFS.CombineTaintLookupTableNames.count(GV->getName());

  return false;
}

bool DFSanFunction::useCallbackLoadLabelAndOrigin(uint64_t Size,
                                                  Align InstAlignment) {
  // When enabling tracking load instructions, we always use
  // __dfsan_load_label_and_origin to reduce code size.
  if (ClTrackOrigins == 2)
    return true;

  assert(Size != 0);
  // * if Size == 1, it is sufficient to load its origin aligned at 4.
  // * if Size == 2, we assume most cases Addr % 2 == 0, so it is sufficient to
  //   load its origin aligned at 4. If not, although origins may be lost, it
  //   should not happen very often.
  // * if align >= 4, Addr must be aligned to 4, otherwise it is UB. When
  //   Size % 4 == 0, it is more efficient to load origins without callbacks.
  // * Otherwise we use __dfsan_load_label_and_origin.
  // This should ensure that common cases run efficiently.
  if (Size <= 2)
    return false;

  const Align Alignment = llvm::assumeAligned(InstAlignment.value());
  return Alignment < MinOriginAlignment || !DFS.hasLoadSizeForFastPath(Size);
}

Value *DataFlowSanitizer::loadNextOrigin(BasicBlock::iterator Pos,
                                         Align OriginAlign,
                                         Value **OriginAddr) {
  IRBuilder<> IRB(Pos->getParent(), Pos);
  *OriginAddr =
      IRB.CreateGEP(OriginTy, *OriginAddr, ConstantInt::get(IntptrTy, 1));
  return IRB.CreateAlignedLoad(OriginTy, *OriginAddr, OriginAlign);
}

std::pair<Value *, Value *> DFSanFunction::loadShadowFast(
    Value *ShadowAddr, Value *OriginAddr, uint64_t Size, Align ShadowAlign,
    Align OriginAlign, Value *FirstOrigin, BasicBlock::iterator Pos) {
  const bool ShouldTrackOrigins = DFS.shouldTrackOrigins();
  const uint64_t ShadowSize = Size * DFS.ShadowWidthBytes;

  assert(Size >= 4 && "Not large enough load size for fast path!");

  // Used for origin tracking.
  std::vector<Value *> Shadows;
  std::vector<Value *> Origins;

  // Load instructions in LLVM can have arbitrary byte sizes (e.g., 3, 12, 20)
  // but this function is only used in a subset of cases that make it possible
  // to optimize the instrumentation.
  //
  // Specifically, when the shadow size in bytes (i.e., loaded bytes x shadow
  // per byte) is either:
  // - a multiple of 8  (common)
  // - equal to 4       (only for load32)
  //
  // For the second case, we can fit the wide shadow in a 32-bit integer. In all
  // other cases, we use a 64-bit integer to hold the wide shadow.
  Type *WideShadowTy =
      ShadowSize == 4 ? Type::getInt32Ty(*DFS.Ctx) : Type::getInt64Ty(*DFS.Ctx);

  IRBuilder<> IRB(Pos->getParent(), Pos);
  Value *CombinedWideShadow =
      IRB.CreateAlignedLoad(WideShadowTy, ShadowAddr, ShadowAlign);

  unsigned WideShadowBitWidth = WideShadowTy->getIntegerBitWidth();
  const uint64_t BytesPerWideShadow = WideShadowBitWidth / DFS.ShadowWidthBits;

  auto AppendWideShadowAndOrigin = [&](Value *WideShadow, Value *Origin) {
    if (BytesPerWideShadow > 4) {
      assert(BytesPerWideShadow == 8);
      // The wide shadow relates to two origin pointers: one for the first four
      // application bytes, and one for the latest four. We use a left shift to
      // get just the shadow bytes that correspond to the first origin pointer,
      // and then the entire shadow for the second origin pointer (which will be
      // chosen by combineOrigins() iff the least-significant half of the wide
      // shadow was empty but the other half was not).
      Value *WideShadowLo = IRB.CreateShl(
          WideShadow, ConstantInt::get(WideShadowTy, WideShadowBitWidth / 2));
      Shadows.push_back(WideShadow);
      Origins.push_back(DFS.loadNextOrigin(Pos, OriginAlign, &OriginAddr));

      Shadows.push_back(WideShadowLo);
      Origins.push_back(Origin);
    } else {
      Shadows.push_back(WideShadow);
      Origins.push_back(Origin);
    }
  };

  if (ShouldTrackOrigins)
    AppendWideShadowAndOrigin(CombinedWideShadow, FirstOrigin);

  // First OR all the WideShadows (i.e., 64bit or 32bit shadow chunks) linearly;
  // then OR individual shadows within the combined WideShadow by binary ORing.
  // This is fewer instructions than ORing shadows individually, since it
  // needs logN shift/or instructions (N being the bytes of the combined wide
  // shadow).
  for (uint64_t ByteOfs = BytesPerWideShadow; ByteOfs < Size;
       ByteOfs += BytesPerWideShadow) {
    ShadowAddr = IRB.CreateGEP(WideShadowTy, ShadowAddr,
                               ConstantInt::get(DFS.IntptrTy, 1));
    Value *NextWideShadow =
        IRB.CreateAlignedLoad(WideShadowTy, ShadowAddr, ShadowAlign);
    CombinedWideShadow = IRB.CreateOr(CombinedWideShadow, NextWideShadow);
    if (ShouldTrackOrigins) {
      Value *NextOrigin = DFS.loadNextOrigin(Pos, OriginAlign, &OriginAddr);
      AppendWideShadowAndOrigin(NextWideShadow, NextOrigin);
    }
  }
  for (unsigned Width = WideShadowBitWidth / 2; Width >= DFS.ShadowWidthBits;
       Width >>= 1) {
    Value *ShrShadow = IRB.CreateLShr(CombinedWideShadow, Width);
    CombinedWideShadow = IRB.CreateOr(CombinedWideShadow, ShrShadow);
  }
  return {IRB.CreateTrunc(CombinedWideShadow, DFS.PrimitiveShadowTy),
          ShouldTrackOrigins
              ? combineOrigins(Shadows, Origins, Pos,
                               ConstantInt::getSigned(IRB.getInt64Ty(), 0))
              : DFS.ZeroOrigin};
}

std::pair<Value *, Value *> DFSanFunction::loadShadowOriginSansLoadTracking(
    Value *Addr, uint64_t Size, Align InstAlignment, BasicBlock::iterator Pos) {
  const bool ShouldTrackOrigins = DFS.shouldTrackOrigins();

  // Non-escaped loads.
  if (AllocaInst *AI = dyn_cast<AllocaInst>(Addr)) {
    const auto SI = AllocaShadowMap.find(AI);
    if (SI != AllocaShadowMap.end()) {
      IRBuilder<> IRB(Pos->getParent(), Pos);
      Value *ShadowLI = IRB.CreateLoad(DFS.PrimitiveShadowTy, SI->second);
      const auto OI = AllocaOriginMap.find(AI);
      assert(!ShouldTrackOrigins || OI != AllocaOriginMap.end());
      return {ShadowLI, ShouldTrackOrigins
                            ? IRB.CreateLoad(DFS.OriginTy, OI->second)
                            : nullptr};
    }
  }

  // Load from constant addresses.
  SmallVector<const Value *, 2> Objs;
  getUnderlyingObjects(Addr, Objs);
  bool AllConstants = true;
  for (const Value *Obj : Objs) {
    if (isa<Function>(Obj) || isa<BlockAddress>(Obj))
      continue;
    if (isa<GlobalVariable>(Obj) && cast<GlobalVariable>(Obj)->isConstant())
      continue;

    AllConstants = false;
    break;
  }
  if (AllConstants)
    return {DFS.ZeroPrimitiveShadow,
            ShouldTrackOrigins ? DFS.ZeroOrigin : nullptr};

  if (Size == 0)
    return {DFS.ZeroPrimitiveShadow,
            ShouldTrackOrigins ? DFS.ZeroOrigin : nullptr};

  // Use callback to load if this is not an optimizable case for origin
  // tracking.
  if (ShouldTrackOrigins &&
      useCallbackLoadLabelAndOrigin(Size, InstAlignment)) {
    IRBuilder<> IRB(Pos->getParent(), Pos);
    CallInst *Call =
        IRB.CreateCall(DFS.DFSanLoadLabelAndOriginFn,
                       {Addr, ConstantInt::get(DFS.IntptrTy, Size)});
    Call->addRetAttr(Attribute::ZExt);
    return {IRB.CreateTrunc(IRB.CreateLShr(Call, DFS.OriginWidthBits),
                            DFS.PrimitiveShadowTy),
            IRB.CreateTrunc(Call, DFS.OriginTy)};
  }

  // Other cases that support loading shadows or origins in a fast way.
  Value *ShadowAddr, *OriginAddr;
  std::tie(ShadowAddr, OriginAddr) =
      DFS.getShadowOriginAddress(Addr, InstAlignment, Pos);

  const Align ShadowAlign = getShadowAlign(InstAlignment);
  const Align OriginAlign = getOriginAlign(InstAlignment);
  Value *Origin = nullptr;
  if (ShouldTrackOrigins) {
    IRBuilder<> IRB(Pos->getParent(), Pos);
    Origin = IRB.CreateAlignedLoad(DFS.OriginTy, OriginAddr, OriginAlign);
  }

  // When the byte size is small enough, we can load the shadow directly with
  // just a few instructions.
  switch (Size) {
  case 1: {
    LoadInst *LI = new LoadInst(DFS.PrimitiveShadowTy, ShadowAddr, "", Pos);
    LI->setAlignment(ShadowAlign);
    return {LI, Origin};
  }
  case 2: {
    IRBuilder<> IRB(Pos->getParent(), Pos);
    Value *ShadowAddr1 = IRB.CreateGEP(DFS.PrimitiveShadowTy, ShadowAddr,
                                       ConstantInt::get(DFS.IntptrTy, 1));
    Value *Load =
        IRB.CreateAlignedLoad(DFS.PrimitiveShadowTy, ShadowAddr, ShadowAlign);
    Value *Load1 =
        IRB.CreateAlignedLoad(DFS.PrimitiveShadowTy, ShadowAddr1, ShadowAlign);
    return {combineShadows(Load, Load1, Pos), Origin};
  }
  }
  bool HasSizeForFastPath = DFS.hasLoadSizeForFastPath(Size);

  if (HasSizeForFastPath)
    return loadShadowFast(ShadowAddr, OriginAddr, Size, ShadowAlign,
                          OriginAlign, Origin, Pos);

  IRBuilder<> IRB(Pos->getParent(), Pos);
  CallInst *FallbackCall = IRB.CreateCall(
      DFS.DFSanUnionLoadFn, {ShadowAddr, ConstantInt::get(DFS.IntptrTy, Size)});
  FallbackCall->addRetAttr(Attribute::ZExt);
  return {FallbackCall, Origin};
}

std::pair<Value *, Value *>
DFSanFunction::loadShadowOrigin(Value *Addr, uint64_t Size, Align InstAlignment,
                                BasicBlock::iterator Pos) {
  Value *PrimitiveShadow, *Origin;
  std::tie(PrimitiveShadow, Origin) =
      loadShadowOriginSansLoadTracking(Addr, Size, InstAlignment, Pos);
  if (DFS.shouldTrackOrigins()) {
    if (ClTrackOrigins == 2) {
      IRBuilder<> IRB(Pos->getParent(), Pos);
      auto *ConstantShadow = dyn_cast<Constant>(PrimitiveShadow);
      if (!ConstantShadow || !ConstantShadow->isZeroValue())
        Origin = updateOriginIfTainted(PrimitiveShadow, Origin, IRB);
    }
  }
  return {PrimitiveShadow, Origin};
}

static AtomicOrdering addAcquireOrdering(AtomicOrdering AO) {
  switch (AO) {
  case AtomicOrdering::NotAtomic:
    return AtomicOrdering::NotAtomic;
  case AtomicOrdering::Unordered:
  case AtomicOrdering::Monotonic:
  case AtomicOrdering::Acquire:
    return AtomicOrdering::Acquire;
  case AtomicOrdering::Release:
  case AtomicOrdering::AcquireRelease:
    return AtomicOrdering::AcquireRelease;
  case AtomicOrdering::SequentiallyConsistent:
    return AtomicOrdering::SequentiallyConsistent;
  }
  llvm_unreachable("Unknown ordering");
}

Value *StripPointerGEPsAndCasts(Value *V) {
  if (!V->getType()->isPointerTy())
    return V;

  // DFSan pass should be running on valid IR, but we'll
  // keep a seen set to ensure there are no issues.
  SmallPtrSet<const Value *, 4> Visited;
  Visited.insert(V);
  do {
    if (auto *GEP = dyn_cast<GEPOperator>(V)) {
      V = GEP->getPointerOperand();
    } else if (Operator::getOpcode(V) == Instruction::BitCast) {
      V = cast<Operator>(V)->getOperand(0);
      if (!V->getType()->isPointerTy())
        return V;
    } else if (isa<GlobalAlias>(V)) {
      V = cast<GlobalAlias>(V)->getAliasee();
    }
  } while (Visited.insert(V).second);

  return V;
}

void DFSanVisitor::visitLoadInst(LoadInst &LI) {
  auto &DL = LI.getDataLayout();
  uint64_t Size = DL.getTypeStoreSize(LI.getType());
  if (Size == 0) {
    DFSF.setShadow(&LI, DFSF.DFS.getZeroShadow(&LI));
    DFSF.setOrigin(&LI, DFSF.DFS.ZeroOrigin);
    return;
  }

  // When an application load is atomic, increase atomic ordering between
  // atomic application loads and stores to ensure happen-before order; load
  // shadow data after application data; store zero shadow data before
  // application data. This ensure shadow loads return either labels of the
  // initial application data or zeros.
  if (LI.isAtomic())
    LI.setOrdering(addAcquireOrdering(LI.getOrdering()));

  BasicBlock::iterator AfterLi = std::next(LI.getIterator());
  BasicBlock::iterator Pos = LI.getIterator();
  if (LI.isAtomic())
    Pos = std::next(Pos);

  std::vector<Value *> Shadows;
  std::vector<Value *> Origins;
  Value *PrimitiveShadow, *Origin;
  std::tie(PrimitiveShadow, Origin) =
      DFSF.loadShadowOrigin(LI.getPointerOperand(), Size, LI.getAlign(), Pos);
  const bool ShouldTrackOrigins = DFSF.DFS.shouldTrackOrigins();
  if (ShouldTrackOrigins) {
    Shadows.push_back(PrimitiveShadow);
    Origins.push_back(Origin);
  }
  if (ClCombinePointerLabelsOnLoad ||
      DFSF.isLookupTableConstant(
          StripPointerGEPsAndCasts(LI.getPointerOperand()))) {
    Value *PtrShadow = DFSF.getShadow(LI.getPointerOperand());
    PrimitiveShadow = DFSF.combineShadows(PrimitiveShadow, PtrShadow, Pos);
    if (ShouldTrackOrigins) {
      Shadows.push_back(PtrShadow);
      Origins.push_back(DFSF.getOrigin(LI.getPointerOperand()));
    }
  }
  if (!DFSF.DFS.isZeroShadow(PrimitiveShadow))
    DFSF.NonZeroChecks.push_back(PrimitiveShadow);

  Value *Shadow =
      DFSF.expandFromPrimitiveShadow(LI.getType(), PrimitiveShadow, Pos);
  DFSF.setShadow(&LI, Shadow);

  if (ShouldTrackOrigins) {
    DFSF.setOrigin(&LI, DFSF.combineOrigins(Shadows, Origins, Pos));
  }

  if (ClEventCallbacks) {
    IRBuilder<> IRB(Pos->getParent(), Pos);
    Value *Addr = LI.getPointerOperand();
    CallInst *CI =
        IRB.CreateCall(DFSF.DFS.DFSanLoadCallbackFn, {PrimitiveShadow, Addr});
    CI->addParamAttr(0, Attribute::ZExt);
  }

  IRBuilder<> IRB(AfterLi->getParent(), AfterLi);
  DFSF.addReachesFunctionCallbacksIfEnabled(IRB, LI, &LI);
}

Value *DFSanFunction::updateOriginIfTainted(Value *Shadow, Value *Origin,
                                            IRBuilder<> &IRB) {
  assert(DFS.shouldTrackOrigins());
  return IRB.CreateCall(DFS.DFSanChainOriginIfTaintedFn, {Shadow, Origin});
}

Value *DFSanFunction::updateOrigin(Value *V, IRBuilder<> &IRB) {
  if (!DFS.shouldTrackOrigins())
    return V;
  return IRB.CreateCall(DFS.DFSanChainOriginFn, V);
}

Value *DFSanFunction::originToIntptr(IRBuilder<> &IRB, Value *Origin) {
  const unsigned OriginSize = DataFlowSanitizer::OriginWidthBytes;
  const DataLayout &DL = F->getDataLayout();
  unsigned IntptrSize = DL.getTypeStoreSize(DFS.IntptrTy);
  if (IntptrSize == OriginSize)
    return Origin;
  assert(IntptrSize == OriginSize * 2);
  Origin = IRB.CreateIntCast(Origin, DFS.IntptrTy, /* isSigned */ false);
  return IRB.CreateOr(Origin, IRB.CreateShl(Origin, OriginSize * 8));
}

void DFSanFunction::paintOrigin(IRBuilder<> &IRB, Value *Origin,
                                Value *StoreOriginAddr,
                                uint64_t StoreOriginSize, Align Alignment) {
  const unsigned OriginSize = DataFlowSanitizer::OriginWidthBytes;
  const DataLayout &DL = F->getDataLayout();
  const Align IntptrAlignment = DL.getABITypeAlign(DFS.IntptrTy);
  unsigned IntptrSize = DL.getTypeStoreSize(DFS.IntptrTy);
  assert(IntptrAlignment >= MinOriginAlignment);
  assert(IntptrSize >= OriginSize);

  unsigned Ofs = 0;
  Align CurrentAlignment = Alignment;
  if (Alignment >= IntptrAlignment && IntptrSize > OriginSize) {
    Value *IntptrOrigin = originToIntptr(IRB, Origin);
    Value *IntptrStoreOriginPtr = IRB.CreatePointerCast(
        StoreOriginAddr, PointerType::get(DFS.IntptrTy, 0));
    for (unsigned I = 0; I < StoreOriginSize / IntptrSize; ++I) {
      Value *Ptr =
          I ? IRB.CreateConstGEP1_32(DFS.IntptrTy, IntptrStoreOriginPtr, I)
            : IntptrStoreOriginPtr;
      IRB.CreateAlignedStore(IntptrOrigin, Ptr, CurrentAlignment);
      Ofs += IntptrSize / OriginSize;
      CurrentAlignment = IntptrAlignment;
    }
  }

  for (unsigned I = Ofs; I < (StoreOriginSize + OriginSize - 1) / OriginSize;
       ++I) {
    Value *GEP = I ? IRB.CreateConstGEP1_32(DFS.OriginTy, StoreOriginAddr, I)
                   : StoreOriginAddr;
    IRB.CreateAlignedStore(Origin, GEP, CurrentAlignment);
    CurrentAlignment = MinOriginAlignment;
  }
}

Value *DFSanFunction::convertToBool(Value *V, IRBuilder<> &IRB,
                                    const Twine &Name) {
  Type *VTy = V->getType();
  assert(VTy->isIntegerTy());
  if (VTy->getIntegerBitWidth() == 1)
    // Just converting a bool to a bool, so do nothing.
    return V;
  return IRB.CreateICmpNE(V, ConstantInt::get(VTy, 0), Name);
}

void DFSanFunction::storeOrigin(BasicBlock::iterator Pos, Value *Addr,
                                uint64_t Size, Value *Shadow, Value *Origin,
                                Value *StoreOriginAddr, Align InstAlignment) {
  // Do not write origins for zero shadows because we do not trace origins for
  // untainted sinks.
  const Align OriginAlignment = getOriginAlign(InstAlignment);
  Value *CollapsedShadow = collapseToPrimitiveShadow(Shadow, Pos);
  IRBuilder<> IRB(Pos->getParent(), Pos);
  if (auto *ConstantShadow = dyn_cast<Constant>(CollapsedShadow)) {
    if (!ConstantShadow->isZeroValue())
      paintOrigin(IRB, updateOrigin(Origin, IRB), StoreOriginAddr, Size,
                  OriginAlignment);
    return;
  }

  if (shouldInstrumentWithCall()) {
    IRB.CreateCall(
        DFS.DFSanMaybeStoreOriginFn,
        {CollapsedShadow, Addr, ConstantInt::get(DFS.IntptrTy, Size), Origin});
  } else {
    Value *Cmp = convertToBool(CollapsedShadow, IRB, "_dfscmp");
    DomTreeUpdater DTU(DT, DomTreeUpdater::UpdateStrategy::Lazy);
    Instruction *CheckTerm = SplitBlockAndInsertIfThen(
        Cmp, &*IRB.GetInsertPoint(), false, DFS.OriginStoreWeights, &DTU);
    IRBuilder<> IRBNew(CheckTerm);
    paintOrigin(IRBNew, updateOrigin(Origin, IRBNew), StoreOriginAddr, Size,
                OriginAlignment);
    ++NumOriginStores;
  }
}

void DFSanFunction::storeZeroPrimitiveShadow(Value *Addr, uint64_t Size,
                                             Align ShadowAlign,
                                             BasicBlock::iterator Pos) {
  IRBuilder<> IRB(Pos->getParent(), Pos);
  IntegerType *ShadowTy =
      IntegerType::get(*DFS.Ctx, Size * DFS.ShadowWidthBits);
  Value *ExtZeroShadow = ConstantInt::get(ShadowTy, 0);
  Value *ShadowAddr = DFS.getShadowAddress(Addr, Pos);
  IRB.CreateAlignedStore(ExtZeroShadow, ShadowAddr, ShadowAlign);
  // Do not write origins for 0 shadows because we do not trace origins for
  // untainted sinks.
}

void DFSanFunction::storePrimitiveShadowOrigin(Value *Addr, uint64_t Size,
                                               Align InstAlignment,
                                               Value *PrimitiveShadow,
                                               Value *Origin,
                                               BasicBlock::iterator Pos) {
  const bool ShouldTrackOrigins = DFS.shouldTrackOrigins() && Origin;

  if (AllocaInst *AI = dyn_cast<AllocaInst>(Addr)) {
    const auto SI = AllocaShadowMap.find(AI);
    if (SI != AllocaShadowMap.end()) {
      IRBuilder<> IRB(Pos->getParent(), Pos);
      IRB.CreateStore(PrimitiveShadow, SI->second);

      // Do not write origins for 0 shadows because we do not trace origins for
      // untainted sinks.
      if (ShouldTrackOrigins && !DFS.isZeroShadow(PrimitiveShadow)) {
        const auto OI = AllocaOriginMap.find(AI);
        assert(OI != AllocaOriginMap.end() && Origin);
        IRB.CreateStore(Origin, OI->second);
      }
      return;
    }
  }

  const Align ShadowAlign = getShadowAlign(InstAlignment);
  if (DFS.isZeroShadow(PrimitiveShadow)) {
    storeZeroPrimitiveShadow(Addr, Size, ShadowAlign, Pos);
    return;
  }

  IRBuilder<> IRB(Pos->getParent(), Pos);
  Value *ShadowAddr, *OriginAddr;
  std::tie(ShadowAddr, OriginAddr) =
      DFS.getShadowOriginAddress(Addr, InstAlignment, Pos);

  const unsigned ShadowVecSize = 8;
  assert(ShadowVecSize * DFS.ShadowWidthBits <= 128 &&
         "Shadow vector is too large!");

  uint64_t Offset = 0;
  uint64_t LeftSize = Size;
  if (LeftSize >= ShadowVecSize) {
    auto *ShadowVecTy =
        FixedVectorType::get(DFS.PrimitiveShadowTy, ShadowVecSize);
    Value *ShadowVec = PoisonValue::get(ShadowVecTy);
    for (unsigned I = 0; I != ShadowVecSize; ++I) {
      ShadowVec = IRB.CreateInsertElement(
          ShadowVec, PrimitiveShadow,
          ConstantInt::get(Type::getInt32Ty(*DFS.Ctx), I));
    }
    do {
      Value *CurShadowVecAddr =
          IRB.CreateConstGEP1_32(ShadowVecTy, ShadowAddr, Offset);
      IRB.CreateAlignedStore(ShadowVec, CurShadowVecAddr, ShadowAlign);
      LeftSize -= ShadowVecSize;
      ++Offset;
    } while (LeftSize >= ShadowVecSize);
    Offset *= ShadowVecSize;
  }
  while (LeftSize > 0) {
    Value *CurShadowAddr =
        IRB.CreateConstGEP1_32(DFS.PrimitiveShadowTy, ShadowAddr, Offset);
    IRB.CreateAlignedStore(PrimitiveShadow, CurShadowAddr, ShadowAlign);
    --LeftSize;
    ++Offset;
  }

  if (ShouldTrackOrigins) {
    storeOrigin(Pos, Addr, Size, PrimitiveShadow, Origin, OriginAddr,
                InstAlignment);
  }
}

static AtomicOrdering addReleaseOrdering(AtomicOrdering AO) {
  switch (AO) {
  case AtomicOrdering::NotAtomic:
    return AtomicOrdering::NotAtomic;
  case AtomicOrdering::Unordered:
  case AtomicOrdering::Monotonic:
  case AtomicOrdering::Release:
    return AtomicOrdering::Release;
  case AtomicOrdering::Acquire:
  case AtomicOrdering::AcquireRelease:
    return AtomicOrdering::AcquireRelease;
  case AtomicOrdering::SequentiallyConsistent:
    return AtomicOrdering::SequentiallyConsistent;
  }
  llvm_unreachable("Unknown ordering");
}

void DFSanVisitor::visitStoreInst(StoreInst &SI) {
  auto &DL = SI.getDataLayout();
  Value *Val = SI.getValueOperand();
  uint64_t Size = DL.getTypeStoreSize(Val->getType());
  if (Size == 0)
    return;

  // When an application store is atomic, increase atomic ordering between
  // atomic application loads and stores to ensure happen-before order; load
  // shadow data after application data; store zero shadow data before
  // application data. This ensure shadow loads return either labels of the
  // initial application data or zeros.
  if (SI.isAtomic())
    SI.setOrdering(addReleaseOrdering(SI.getOrdering()));

  const bool ShouldTrackOrigins =
      DFSF.DFS.shouldTrackOrigins() && !SI.isAtomic();
  std::vector<Value *> Shadows;
  std::vector<Value *> Origins;

  Value *Shadow =
      SI.isAtomic() ? DFSF.DFS.getZeroShadow(Val) : DFSF.getShadow(Val);

  if (ShouldTrackOrigins) {
    Shadows.push_back(Shadow);
    Origins.push_back(DFSF.getOrigin(Val));
  }

  Value *PrimitiveShadow;
  if (ClCombinePointerLabelsOnStore) {
    Value *PtrShadow = DFSF.getShadow(SI.getPointerOperand());
    if (ShouldTrackOrigins) {
      Shadows.push_back(PtrShadow);
      Origins.push_back(DFSF.getOrigin(SI.getPointerOperand()));
    }
    PrimitiveShadow = DFSF.combineShadows(Shadow, PtrShadow, SI.getIterator());
  } else {
    PrimitiveShadow = DFSF.collapseToPrimitiveShadow(Shadow, SI.getIterator());
  }
  Value *Origin = nullptr;
  if (ShouldTrackOrigins)
    Origin = DFSF.combineOrigins(Shadows, Origins, SI.getIterator());
  DFSF.storePrimitiveShadowOrigin(SI.getPointerOperand(), Size, SI.getAlign(),
                                  PrimitiveShadow, Origin, SI.getIterator());
  if (ClEventCallbacks) {
    IRBuilder<> IRB(&SI);
    Value *Addr = SI.getPointerOperand();
    CallInst *CI =
        IRB.CreateCall(DFSF.DFS.DFSanStoreCallbackFn, {PrimitiveShadow, Addr});
    CI->addParamAttr(0, Attribute::ZExt);
  }
}

void DFSanVisitor::visitCASOrRMW(Align InstAlignment, Instruction &I) {
  assert(isa<AtomicRMWInst>(I) || isa<AtomicCmpXchgInst>(I));

  Value *Val = I.getOperand(1);
  const auto &DL = I.getDataLayout();
  uint64_t Size = DL.getTypeStoreSize(Val->getType());
  if (Size == 0)
    return;

  // Conservatively set data at stored addresses and return with zero shadow to
  // prevent shadow data races.
  IRBuilder<> IRB(&I);
  Value *Addr = I.getOperand(0);
  const Align ShadowAlign = DFSF.getShadowAlign(InstAlignment);
  DFSF.storeZeroPrimitiveShadow(Addr, Size, ShadowAlign, I.getIterator());
  DFSF.setShadow(&I, DFSF.DFS.getZeroShadow(&I));
  DFSF.setOrigin(&I, DFSF.DFS.ZeroOrigin);
}

void DFSanVisitor::visitAtomicRMWInst(AtomicRMWInst &I) {
  visitCASOrRMW(I.getAlign(), I);
  // TODO: The ordering change follows MSan. It is possible not to change
  // ordering because we always set and use 0 shadows.
  I.setOrdering(addReleaseOrdering(I.getOrdering()));
}

void DFSanVisitor::visitAtomicCmpXchgInst(AtomicCmpXchgInst &I) {
  visitCASOrRMW(I.getAlign(), I);
  // TODO: The ordering change follows MSan. It is possible not to change
  // ordering because we always set and use 0 shadows.
  I.setSuccessOrdering(addReleaseOrdering(I.getSuccessOrdering()));
}

void DFSanVisitor::visitUnaryOperator(UnaryOperator &UO) {
  visitInstOperands(UO);
}

void DFSanVisitor::visitBinaryOperator(BinaryOperator &BO) {
  visitInstOperands(BO);
}

void DFSanVisitor::visitBitCastInst(BitCastInst &BCI) {
  // Special case: if this is the bitcast (there is exactly 1 allowed) between
  // a musttail call and a ret, don't instrument. New instructions are not
  // allowed after a musttail call.
  if (auto *CI = dyn_cast<CallInst>(BCI.getOperand(0)))
    if (CI->isMustTailCall())
      return;
  visitInstOperands(BCI);
}

void DFSanVisitor::visitCastInst(CastInst &CI) { visitInstOperands(CI); }

void DFSanVisitor::visitCmpInst(CmpInst &CI) {
  visitInstOperands(CI);
  if (ClEventCallbacks) {
    IRBuilder<> IRB(&CI);
    Value *CombinedShadow = DFSF.getShadow(&CI);
    CallInst *CallI =
        IRB.CreateCall(DFSF.DFS.DFSanCmpCallbackFn, CombinedShadow);
    CallI->addParamAttr(0, Attribute::ZExt);
  }
}

void DFSanVisitor::visitLandingPadInst(LandingPadInst &LPI) {
  // We do not need to track data through LandingPadInst.
  //
  // For the C++ exceptions, if a value is thrown, this value will be stored
  // in a memory location provided by __cxa_allocate_exception(...) (on the
  // throw side) or  __cxa_begin_catch(...) (on the catch side).
  // This memory will have a shadow, so with the loads and stores we will be
  // able to propagate labels on data thrown through exceptions, without any
  // special handling of the LandingPadInst.
  //
  // The second element in the pair result of the LandingPadInst is a
  // register value, but it is for a type ID and should never be tainted.
  DFSF.setShadow(&LPI, DFSF.DFS.getZeroShadow(&LPI));
  DFSF.setOrigin(&LPI, DFSF.DFS.ZeroOrigin);
}

void DFSanVisitor::visitGetElementPtrInst(GetElementPtrInst &GEPI) {
  if (ClCombineOffsetLabelsOnGEP ||
      DFSF.isLookupTableConstant(
          StripPointerGEPsAndCasts(GEPI.getPointerOperand()))) {
    visitInstOperands(GEPI);
    return;
  }

  // Only propagate shadow/origin of base pointer value but ignore those of
  // offset operands.
  Value *BasePointer = GEPI.getPointerOperand();
  DFSF.setShadow(&GEPI, DFSF.getShadow(BasePointer));
  if (DFSF.DFS.shouldTrackOrigins())
    DFSF.setOrigin(&GEPI, DFSF.getOrigin(BasePointer));
}

void DFSanVisitor::visitExtractElementInst(ExtractElementInst &I) {
  visitInstOperands(I);
}

void DFSanVisitor::visitInsertElementInst(InsertElementInst &I) {
  visitInstOperands(I);
}

void DFSanVisitor::visitShuffleVectorInst(ShuffleVectorInst &I) {
  visitInstOperands(I);
}

void DFSanVisitor::visitExtractValueInst(ExtractValueInst &I) {
  IRBuilder<> IRB(&I);
  Value *Agg = I.getAggregateOperand();
  Value *AggShadow = DFSF.getShadow(Agg);
  Value *ResShadow = IRB.CreateExtractValue(AggShadow, I.getIndices());
  DFSF.setShadow(&I, ResShadow);
  visitInstOperandOrigins(I);
}

void DFSanVisitor::visitInsertValueInst(InsertValueInst &I) {
  IRBuilder<> IRB(&I);
  Value *AggShadow = DFSF.getShadow(I.getAggregateOperand());
  Value *InsShadow = DFSF.getShadow(I.getInsertedValueOperand());
  Value *Res = IRB.CreateInsertValue(AggShadow, InsShadow, I.getIndices());
  DFSF.setShadow(&I, Res);
  visitInstOperandOrigins(I);
}

void DFSanVisitor::visitAllocaInst(AllocaInst &I) {
  bool AllLoadsStores = true;
  for (User *U : I.users()) {
    if (isa<LoadInst>(U))
      continue;

    if (StoreInst *SI = dyn_cast<StoreInst>(U)) {
      if (SI->getPointerOperand() == &I)
        continue;
    }

    AllLoadsStores = false;
    break;
  }
  if (AllLoadsStores) {
    IRBuilder<> IRB(&I);
    DFSF.AllocaShadowMap[&I] = IRB.CreateAlloca(DFSF.DFS.PrimitiveShadowTy);
    if (DFSF.DFS.shouldTrackOrigins()) {
      DFSF.AllocaOriginMap[&I] =
          IRB.CreateAlloca(DFSF.DFS.OriginTy, nullptr, "_dfsa");
    }
  }
  DFSF.setShadow(&I, DFSF.DFS.ZeroPrimitiveShadow);
  DFSF.setOrigin(&I, DFSF.DFS.ZeroOrigin);
}

void DFSanVisitor::visitSelectInst(SelectInst &I) {
  Value *CondShadow = DFSF.getShadow(I.getCondition());
  Value *TrueShadow = DFSF.getShadow(I.getTrueValue());
  Value *FalseShadow = DFSF.getShadow(I.getFalseValue());
  Value *ShadowSel = nullptr;
  const bool ShouldTrackOrigins = DFSF.DFS.shouldTrackOrigins();
  std::vector<Value *> Shadows;
  std::vector<Value *> Origins;
  Value *TrueOrigin =
      ShouldTrackOrigins ? DFSF.getOrigin(I.getTrueValue()) : nullptr;
  Value *FalseOrigin =
      ShouldTrackOrigins ? DFSF.getOrigin(I.getFalseValue()) : nullptr;

  DFSF.addConditionalCallbacksIfEnabled(I, I.getCondition());

  if (isa<VectorType>(I.getCondition()->getType())) {
    ShadowSel = DFSF.combineShadowsThenConvert(I.getType(), TrueShadow,
                                               FalseShadow, I.getIterator());
    if (ShouldTrackOrigins) {
      Shadows.push_back(TrueShadow);
      Shadows.push_back(FalseShadow);
      Origins.push_back(TrueOrigin);
      Origins.push_back(FalseOrigin);
    }
  } else {
    if (TrueShadow == FalseShadow) {
      ShadowSel = TrueShadow;
      if (ShouldTrackOrigins) {
        Shadows.push_back(TrueShadow);
        Origins.push_back(TrueOrigin);
      }
    } else {
      ShadowSel = SelectInst::Create(I.getCondition(), TrueShadow, FalseShadow,
                                     "", I.getIterator());
      if (ShouldTrackOrigins) {
        Shadows.push_back(ShadowSel);
        Origins.push_back(SelectInst::Create(I.getCondition(), TrueOrigin,
                                             FalseOrigin, "", I.getIterator()));
      }
    }
  }
  DFSF.setShadow(&I, ClTrackSelectControlFlow ? DFSF.combineShadowsThenConvert(
                                                    I.getType(), CondShadow,
                                                    ShadowSel, I.getIterator())
                                              : ShadowSel);
  if (ShouldTrackOrigins) {
    if (ClTrackSelectControlFlow) {
      Shadows.push_back(CondShadow);
      Origins.push_back(DFSF.getOrigin(I.getCondition()));
    }
    DFSF.setOrigin(&I, DFSF.combineOrigins(Shadows, Origins, I.getIterator()));
  }
}

void DFSanVisitor::visitMemSetInst(MemSetInst &I) {
  IRBuilder<> IRB(&I);
  Value *ValShadow = DFSF.getShadow(I.getValue());
  Value *ValOrigin = DFSF.DFS.shouldTrackOrigins()
                         ? DFSF.getOrigin(I.getValue())
                         : DFSF.DFS.ZeroOrigin;
  IRB.CreateCall(DFSF.DFS.DFSanSetLabelFn,
                 {ValShadow, ValOrigin, I.getDest(),
                  IRB.CreateZExtOrTrunc(I.getLength(), DFSF.DFS.IntptrTy)});
}

void DFSanVisitor::visitMemTransferInst(MemTransferInst &I) {
  IRBuilder<> IRB(&I);

  // CopyOrMoveOrigin transfers origins by refering to their shadows. So we
  // need to move origins before moving shadows.
  if (DFSF.DFS.shouldTrackOrigins()) {
    IRB.CreateCall(
        DFSF.DFS.DFSanMemOriginTransferFn,
        {I.getArgOperand(0), I.getArgOperand(1),
         IRB.CreateIntCast(I.getArgOperand(2), DFSF.DFS.IntptrTy, false)});
  }

  Value *DestShadow = DFSF.DFS.getShadowAddress(I.getDest(), I.getIterator());
  Value *SrcShadow = DFSF.DFS.getShadowAddress(I.getSource(), I.getIterator());
  Value *LenShadow =
      IRB.CreateMul(I.getLength(), ConstantInt::get(I.getLength()->getType(),
                                                    DFSF.DFS.ShadowWidthBytes));
  auto *MTI = cast<MemTransferInst>(
      IRB.CreateCall(I.getFunctionType(), I.getCalledOperand(),
                     {DestShadow, SrcShadow, LenShadow, I.getVolatileCst()}));
  MTI->setDestAlignment(DFSF.getShadowAlign(I.getDestAlign().valueOrOne()));
  MTI->setSourceAlignment(DFSF.getShadowAlign(I.getSourceAlign().valueOrOne()));
  if (ClEventCallbacks) {
    IRB.CreateCall(
        DFSF.DFS.DFSanMemTransferCallbackFn,
        {DestShadow, IRB.CreateZExtOrTrunc(I.getLength(), DFSF.DFS.IntptrTy)});
  }
}

void DFSanVisitor::visitBranchInst(BranchInst &BR) {
  if (!BR.isConditional())
    return;

  DFSF.addConditionalCallbacksIfEnabled(BR, BR.getCondition());
}

void DFSanVisitor::visitSwitchInst(SwitchInst &SW) {
  DFSF.addConditionalCallbacksIfEnabled(SW, SW.getCondition());
}

static bool isAMustTailRetVal(Value *RetVal) {
  // Tail call may have a bitcast between return.
  if (auto *I = dyn_cast<BitCastInst>(RetVal)) {
    RetVal = I->getOperand(0);
  }
  if (auto *I = dyn_cast<CallInst>(RetVal)) {
    return I->isMustTailCall();
  }
  return false;
}

void DFSanVisitor::visitReturnInst(ReturnInst &RI) {
  if (!DFSF.IsNativeABI && RI.getReturnValue()) {
    // Don't emit the instrumentation for musttail call returns.
    if (isAMustTailRetVal(RI.getReturnValue()))
      return;

    Value *S = DFSF.getShadow(RI.getReturnValue());
    IRBuilder<> IRB(&RI);
    Type *RT = DFSF.F->getFunctionType()->getReturnType();
    unsigned Size = getDataLayout().getTypeAllocSize(DFSF.DFS.getShadowTy(RT));
    if (Size <= RetvalTLSSize) {
      // If the size overflows, stores nothing. At callsite, oversized return
      // shadows are set to zero.
      IRB.CreateAlignedStore(S, DFSF.getRetvalTLS(RT, IRB), ShadowTLSAlignment);
    }
    if (DFSF.DFS.shouldTrackOrigins()) {
      Value *O = DFSF.getOrigin(RI.getReturnValue());
      IRB.CreateStore(O, DFSF.getRetvalOriginTLS());
    }
  }
}

void DFSanVisitor::addShadowArguments(Function &F, CallBase &CB,
                                      std::vector<Value *> &Args,
                                      IRBuilder<> &IRB) {
  FunctionType *FT = F.getFunctionType();

  auto *I = CB.arg_begin();

  // Adds non-variable argument shadows.
  for (unsigned N = FT->getNumParams(); N != 0; ++I, --N)
    Args.push_back(
        DFSF.collapseToPrimitiveShadow(DFSF.getShadow(*I), CB.getIterator()));

  // Adds variable argument shadows.
  if (FT->isVarArg()) {
    auto *LabelVATy = ArrayType::get(DFSF.DFS.PrimitiveShadowTy,
                                     CB.arg_size() - FT->getNumParams());
    auto *LabelVAAlloca =
        new AllocaInst(LabelVATy, getDataLayout().getAllocaAddrSpace(),
                       "labelva", DFSF.F->getEntryBlock().begin());

    for (unsigned N = 0; I != CB.arg_end(); ++I, ++N) {
      auto *LabelVAPtr = IRB.CreateStructGEP(LabelVATy, LabelVAAlloca, N);
      IRB.CreateStore(
          DFSF.collapseToPrimitiveShadow(DFSF.getShadow(*I), CB.getIterator()),
          LabelVAPtr);
    }

    Args.push_back(IRB.CreateStructGEP(LabelVATy, LabelVAAlloca, 0));
  }

  // Adds the return value shadow.
  if (!FT->getReturnType()->isVoidTy()) {
    if (!DFSF.LabelReturnAlloca) {
      DFSF.LabelReturnAlloca = new AllocaInst(
          DFSF.DFS.PrimitiveShadowTy, getDataLayout().getAllocaAddrSpace(),
          "labelreturn", DFSF.F->getEntryBlock().begin());
    }
    Args.push_back(DFSF.LabelReturnAlloca);
  }
}

void DFSanVisitor::addOriginArguments(Function &F, CallBase &CB,
                                      std::vector<Value *> &Args,
                                      IRBuilder<> &IRB) {
  FunctionType *FT = F.getFunctionType();

  auto *I = CB.arg_begin();

  // Add non-variable argument origins.
  for (unsigned N = FT->getNumParams(); N != 0; ++I, --N)
    Args.push_back(DFSF.getOrigin(*I));

  // Add variable argument origins.
  if (FT->isVarArg()) {
    auto *OriginVATy =
        ArrayType::get(DFSF.DFS.OriginTy, CB.arg_size() - FT->getNumParams());
    auto *OriginVAAlloca =
        new AllocaInst(OriginVATy, getDataLayout().getAllocaAddrSpace(),
                       "originva", DFSF.F->getEntryBlock().begin());

    for (unsigned N = 0; I != CB.arg_end(); ++I, ++N) {
      auto *OriginVAPtr = IRB.CreateStructGEP(OriginVATy, OriginVAAlloca, N);
      IRB.CreateStore(DFSF.getOrigin(*I), OriginVAPtr);
    }

    Args.push_back(IRB.CreateStructGEP(OriginVATy, OriginVAAlloca, 0));
  }

  // Add the return value origin.
  if (!FT->getReturnType()->isVoidTy()) {
    if (!DFSF.OriginReturnAlloca) {
      DFSF.OriginReturnAlloca = new AllocaInst(
          DFSF.DFS.OriginTy, getDataLayout().getAllocaAddrSpace(),
          "originreturn", DFSF.F->getEntryBlock().begin());
    }
    Args.push_back(DFSF.OriginReturnAlloca);
  }
}

bool DFSanVisitor::visitWrappedCallBase(Function &F, CallBase &CB) {
  IRBuilder<> IRB(&CB);
  switch (DFSF.DFS.getWrapperKind(&F)) {
  case DataFlowSanitizer::WK_Warning:
    CB.setCalledFunction(&F);
    IRB.CreateCall(DFSF.DFS.DFSanUnimplementedFn,
                   IRB.CreateGlobalStringPtr(F.getName()));
    DFSF.DFS.buildExternWeakCheckIfNeeded(IRB, &F);
    DFSF.setShadow(&CB, DFSF.DFS.getZeroShadow(&CB));
    DFSF.setOrigin(&CB, DFSF.DFS.ZeroOrigin);
    return true;
  case DataFlowSanitizer::WK_Discard:
    CB.setCalledFunction(&F);
    DFSF.DFS.buildExternWeakCheckIfNeeded(IRB, &F);
    DFSF.setShadow(&CB, DFSF.DFS.getZeroShadow(&CB));
    DFSF.setOrigin(&CB, DFSF.DFS.ZeroOrigin);
    return true;
  case DataFlowSanitizer::WK_Functional:
    CB.setCalledFunction(&F);
    DFSF.DFS.buildExternWeakCheckIfNeeded(IRB, &F);
    visitInstOperands(CB);
    return true;
  case DataFlowSanitizer::WK_Custom:
    // Don't try to handle invokes of custom functions, it's too complicated.
    // Instead, invoke the dfsw$ wrapper, which will in turn call the __dfsw_
    // wrapper.
    CallInst *CI = dyn_cast<CallInst>(&CB);
    if (!CI)
      return false;

    const bool ShouldTrackOrigins = DFSF.DFS.shouldTrackOrigins();
    FunctionType *FT = F.getFunctionType();
    TransformedFunction CustomFn = DFSF.DFS.getCustomFunctionType(FT);
    std::string CustomFName = ShouldTrackOrigins ? "__dfso_" : "__dfsw_";
    CustomFName += F.getName();
    FunctionCallee CustomF = DFSF.DFS.Mod->getOrInsertFunction(
        CustomFName, CustomFn.TransformedType);
    if (Function *CustomFn = dyn_cast<Function>(CustomF.getCallee())) {
      CustomFn->copyAttributesFrom(&F);

      // Custom functions returning non-void will write to the return label.
      if (!FT->getReturnType()->isVoidTy()) {
        CustomFn->removeFnAttrs(DFSF.DFS.ReadOnlyNoneAttrs);
      }
    }

    std::vector<Value *> Args;

    // Adds non-variable arguments.
    auto *I = CB.arg_begin();
    for (unsigned N = FT->getNumParams(); N != 0; ++I, --N) {
      Args.push_back(*I);
    }

    // Adds shadow arguments.
    const unsigned ShadowArgStart = Args.size();
    addShadowArguments(F, CB, Args, IRB);

    // Adds origin arguments.
    const unsigned OriginArgStart = Args.size();
    if (ShouldTrackOrigins)
      addOriginArguments(F, CB, Args, IRB);

    // Adds variable arguments.
    append_range(Args, drop_begin(CB.args(), FT->getNumParams()));

    CallInst *CustomCI = IRB.CreateCall(CustomF, Args);
    CustomCI->setCallingConv(CI->getCallingConv());
    CustomCI->setAttributes(transformFunctionAttributes(
        CustomFn, CI->getContext(), CI->getAttributes()));

    // Update the parameter attributes of the custom call instruction to
    // zero extend the shadow parameters. This is required for targets
    // which consider PrimitiveShadowTy an illegal type.
    for (unsigned N = 0; N < FT->getNumParams(); N++) {
      const unsigned ArgNo = ShadowArgStart + N;
      if (CustomCI->getArgOperand(ArgNo)->getType() ==
          DFSF.DFS.PrimitiveShadowTy)
        CustomCI->addParamAttr(ArgNo, Attribute::ZExt);
      if (ShouldTrackOrigins) {
        const unsigned OriginArgNo = OriginArgStart + N;
        if (CustomCI->getArgOperand(OriginArgNo)->getType() ==
            DFSF.DFS.OriginTy)
          CustomCI->addParamAttr(OriginArgNo, Attribute::ZExt);
      }
    }

    // Loads the return value shadow and origin.
    if (!FT->getReturnType()->isVoidTy()) {
      LoadInst *LabelLoad =
          IRB.CreateLoad(DFSF.DFS.PrimitiveShadowTy, DFSF.LabelReturnAlloca);
      DFSF.setShadow(CustomCI,
                     DFSF.expandFromPrimitiveShadow(
                         FT->getReturnType(), LabelLoad, CB.getIterator()));
      if (ShouldTrackOrigins) {
        LoadInst *OriginLoad =
            IRB.CreateLoad(DFSF.DFS.OriginTy, DFSF.OriginReturnAlloca);
        DFSF.setOrigin(CustomCI, OriginLoad);
      }
    }

    CI->replaceAllUsesWith(CustomCI);
    CI->eraseFromParent();
    return true;
  }
  return false;
}

Value *DFSanVisitor::makeAddAcquireOrderingTable(IRBuilder<> &IRB) {
  constexpr int NumOrderings = (int)AtomicOrderingCABI::seq_cst + 1;
  uint32_t OrderingTable[NumOrderings] = {};

  OrderingTable[(int)AtomicOrderingCABI::relaxed] =
      OrderingTable[(int)AtomicOrderingCABI::acquire] =
          OrderingTable[(int)AtomicOrderingCABI::consume] =
              (int)AtomicOrderingCABI::acquire;
  OrderingTable[(int)AtomicOrderingCABI::release] =
      OrderingTable[(int)AtomicOrderingCABI::acq_rel] =
          (int)AtomicOrderingCABI::acq_rel;
  OrderingTable[(int)AtomicOrderingCABI::seq_cst] =
      (int)AtomicOrderingCABI::seq_cst;

  return ConstantDataVector::get(IRB.getContext(), OrderingTable);
}

void DFSanVisitor::visitLibAtomicLoad(CallBase &CB) {
  // Since we use getNextNode here, we can't have CB terminate the BB.
  assert(isa<CallInst>(CB));

  IRBuilder<> IRB(&CB);
  Value *Size = CB.getArgOperand(0);
  Value *SrcPtr = CB.getArgOperand(1);
  Value *DstPtr = CB.getArgOperand(2);
  Value *Ordering = CB.getArgOperand(3);
  // Convert the call to have at least Acquire ordering to make sure
  // the shadow operations aren't reordered before it.
  Value *NewOrdering =
      IRB.CreateExtractElement(makeAddAcquireOrderingTable(IRB), Ordering);
  CB.setArgOperand(3, NewOrdering);

  IRBuilder<> NextIRB(CB.getNextNode());
  NextIRB.SetCurrentDebugLocation(CB.getDebugLoc());

  // TODO: Support ClCombinePointerLabelsOnLoad
  // TODO: Support ClEventCallbacks

  NextIRB.CreateCall(
      DFSF.DFS.DFSanMemShadowOriginTransferFn,
      {DstPtr, SrcPtr, NextIRB.CreateIntCast(Size, DFSF.DFS.IntptrTy, false)});
}

Value *DFSanVisitor::makeAddReleaseOrderingTable(IRBuilder<> &IRB) {
  constexpr int NumOrderings = (int)AtomicOrderingCABI::seq_cst + 1;
  uint32_t OrderingTable[NumOrderings] = {};

  OrderingTable[(int)AtomicOrderingCABI::relaxed] =
      OrderingTable[(int)AtomicOrderingCABI::release] =
          (int)AtomicOrderingCABI::release;
  OrderingTable[(int)AtomicOrderingCABI::consume] =
      OrderingTable[(int)AtomicOrderingCABI::acquire] =
          OrderingTable[(int)AtomicOrderingCABI::acq_rel] =
              (int)AtomicOrderingCABI::acq_rel;
  OrderingTable[(int)AtomicOrderingCABI::seq_cst] =
      (int)AtomicOrderingCABI::seq_cst;

  return ConstantDataVector::get(IRB.getContext(), OrderingTable);
}

void DFSanVisitor::visitLibAtomicStore(CallBase &CB) {
  IRBuilder<> IRB(&CB);
  Value *Size = CB.getArgOperand(0);
  Value *SrcPtr = CB.getArgOperand(1);
  Value *DstPtr = CB.getArgOperand(2);
  Value *Ordering = CB.getArgOperand(3);
  // Convert the call to have at least Release ordering to make sure
  // the shadow operations aren't reordered after it.
  Value *NewOrdering =
      IRB.CreateExtractElement(makeAddReleaseOrderingTable(IRB), Ordering);
  CB.setArgOperand(3, NewOrdering);

  // TODO: Support ClCombinePointerLabelsOnStore
  // TODO: Support ClEventCallbacks

  IRB.CreateCall(
      DFSF.DFS.DFSanMemShadowOriginTransferFn,
      {DstPtr, SrcPtr, IRB.CreateIntCast(Size, DFSF.DFS.IntptrTy, false)});
}

void DFSanVisitor::visitLibAtomicExchange(CallBase &CB) {
  // void __atomic_exchange(size_t size, void *ptr, void *val, void *ret, int
  // ordering)
  IRBuilder<> IRB(&CB);
  Value *Size = CB.getArgOperand(0);
  Value *TargetPtr = CB.getArgOperand(1);
  Value *SrcPtr = CB.getArgOperand(2);
  Value *DstPtr = CB.getArgOperand(3);

  // This operation is not atomic for the shadow and origin memory.
  // This could result in DFSan false positives or false negatives.
  // For now we will assume these operations are rare, and
  // the additional complexity to address this is not warrented.

  // Current Target to Dest
  IRB.CreateCall(
      DFSF.DFS.DFSanMemShadowOriginTransferFn,
      {DstPtr, TargetPtr, IRB.CreateIntCast(Size, DFSF.DFS.IntptrTy, false)});

  // Current Src to Target (overriding)
  IRB.CreateCall(
      DFSF.DFS.DFSanMemShadowOriginTransferFn,
      {TargetPtr, SrcPtr, IRB.CreateIntCast(Size, DFSF.DFS.IntptrTy, false)});
}

void DFSanVisitor::visitLibAtomicCompareExchange(CallBase &CB) {
  // bool __atomic_compare_exchange(size_t size, void *ptr, void *expected, void
  // *desired, int success_order, int failure_order)
  Value *Size = CB.getArgOperand(0);
  Value *TargetPtr = CB.getArgOperand(1);
  Value *ExpectedPtr = CB.getArgOperand(2);
  Value *DesiredPtr = CB.getArgOperand(3);

  // This operation is not atomic for the shadow and origin memory.
  // This could result in DFSan false positives or false negatives.
  // For now we will assume these operations are rare, and
  // the additional complexity to address this is not warrented.

  IRBuilder<> NextIRB(CB.getNextNode());
  NextIRB.SetCurrentDebugLocation(CB.getDebugLoc());

  DFSF.setShadow(&CB, DFSF.DFS.getZeroShadow(&CB));

  // If original call returned true, copy Desired to Target.
  // If original call returned false, copy Target to Expected.
  NextIRB.CreateCall(DFSF.DFS.DFSanMemShadowOriginConditionalExchangeFn,
                     {NextIRB.CreateIntCast(&CB, NextIRB.getInt8Ty(), false),
                      TargetPtr, ExpectedPtr, DesiredPtr,
                      NextIRB.CreateIntCast(Size, DFSF.DFS.IntptrTy, false)});
}

void DFSanVisitor::visitCallBase(CallBase &CB) {
  Function *F = CB.getCalledFunction();
  if ((F && F->isIntrinsic()) || CB.isInlineAsm()) {
    visitInstOperands(CB);
    return;
  }

  // Calls to this function are synthesized in wrappers, and we shouldn't
  // instrument them.
  if (F == DFSF.DFS.DFSanVarargWrapperFn.getCallee()->stripPointerCasts())
    return;

  LibFunc LF;
  if (DFSF.TLI.getLibFunc(CB, LF)) {
    // libatomic.a functions need to have special handling because there isn't
    // a good way to intercept them or compile the library with
    // instrumentation.
    switch (LF) {
    case LibFunc_atomic_load:
      if (!isa<CallInst>(CB)) {
        llvm::errs() << "DFSAN -- cannot instrument invoke of libatomic load. "
                        "Ignoring!\n";
        break;
      }
      visitLibAtomicLoad(CB);
      return;
    case LibFunc_atomic_store:
      visitLibAtomicStore(CB);
      return;
    default:
      break;
    }
  }

  // TODO: These are not supported by TLI? They are not in the enum.
  if (F && F->hasName() && !F->isVarArg()) {
    if (F->getName() == "__atomic_exchange") {
      visitLibAtomicExchange(CB);
      return;
    }
    if (F->getName() == "__atomic_compare_exchange") {
      visitLibAtomicCompareExchange(CB);
      return;
    }
  }

  DenseMap<Value *, Function *>::iterator UnwrappedFnIt =
      DFSF.DFS.UnwrappedFnMap.find(CB.getCalledOperand());
  if (UnwrappedFnIt != DFSF.DFS.UnwrappedFnMap.end())
    if (visitWrappedCallBase(*UnwrappedFnIt->second, CB))
      return;

  IRBuilder<> IRB(&CB);

  const bool ShouldTrackOrigins = DFSF.DFS.shouldTrackOrigins();
  FunctionType *FT = CB.getFunctionType();
  const DataLayout &DL = getDataLayout();

  // Stores argument shadows.
  unsigned ArgOffset = 0;
  for (unsigned I = 0, N = FT->getNumParams(); I != N; ++I) {
    if (ShouldTrackOrigins) {
      // Ignore overflowed origins
      Value *ArgShadow = DFSF.getShadow(CB.getArgOperand(I));
      if (I < DFSF.DFS.NumOfElementsInArgOrgTLS &&
          !DFSF.DFS.isZeroShadow(ArgShadow))
        IRB.CreateStore(DFSF.getOrigin(CB.getArgOperand(I)),
                        DFSF.getArgOriginTLS(I, IRB));
    }

    unsigned Size =
        DL.getTypeAllocSize(DFSF.DFS.getShadowTy(FT->getParamType(I)));
    // Stop storing if arguments' size overflows. Inside a function, arguments
    // after overflow have zero shadow values.
    if (ArgOffset + Size > ArgTLSSize)
      break;
    IRB.CreateAlignedStore(DFSF.getShadow(CB.getArgOperand(I)),
                           DFSF.getArgTLS(FT->getParamType(I), ArgOffset, IRB),
                           ShadowTLSAlignment);
    ArgOffset += alignTo(Size, ShadowTLSAlignment);
  }

  Instruction *Next = nullptr;
  if (!CB.getType()->isVoidTy()) {
    if (InvokeInst *II = dyn_cast<InvokeInst>(&CB)) {
      if (II->getNormalDest()->getSinglePredecessor()) {
        Next = &II->getNormalDest()->front();
      } else {
        BasicBlock *NewBB =
            SplitEdge(II->getParent(), II->getNormalDest(), &DFSF.DT);
        Next = &NewBB->front();
      }
    } else {
      assert(CB.getIterator() != CB.getParent()->end());
      Next = CB.getNextNode();
    }

    // Don't emit the epilogue for musttail call returns.
    if (isa<CallInst>(CB) && cast<CallInst>(CB).isMustTailCall())
      return;

    // Loads the return value shadow.
    IRBuilder<> NextIRB(Next);
    unsigned Size = DL.getTypeAllocSize(DFSF.DFS.getShadowTy(&CB));
    if (Size > RetvalTLSSize) {
      // Set overflowed return shadow to be zero.
      DFSF.setShadow(&CB, DFSF.DFS.getZeroShadow(&CB));
    } else {
      LoadInst *LI = NextIRB.CreateAlignedLoad(
          DFSF.DFS.getShadowTy(&CB), DFSF.getRetvalTLS(CB.getType(), NextIRB),
          ShadowTLSAlignment, "_dfsret");
      DFSF.SkipInsts.insert(LI);
      DFSF.setShadow(&CB, LI);
      DFSF.NonZeroChecks.push_back(LI);
    }

    if (ShouldTrackOrigins) {
      LoadInst *LI = NextIRB.CreateLoad(DFSF.DFS.OriginTy,
                                        DFSF.getRetvalOriginTLS(), "_dfsret_o");
      DFSF.SkipInsts.insert(LI);
      DFSF.setOrigin(&CB, LI);
    }

    DFSF.addReachesFunctionCallbacksIfEnabled(NextIRB, CB, &CB);
  }
}

void DFSanVisitor::visitPHINode(PHINode &PN) {
  Type *ShadowTy = DFSF.DFS.getShadowTy(&PN);
  PHINode *ShadowPN = PHINode::Create(ShadowTy, PN.getNumIncomingValues(), "",
                                      PN.getIterator());

  // Give the shadow phi node valid predecessors to fool SplitEdge into working.
  Value *UndefShadow = UndefValue::get(ShadowTy);
  for (BasicBlock *BB : PN.blocks())
    ShadowPN->addIncoming(UndefShadow, BB);

  DFSF.setShadow(&PN, ShadowPN);

  PHINode *OriginPN = nullptr;
  if (DFSF.DFS.shouldTrackOrigins()) {
    OriginPN = PHINode::Create(DFSF.DFS.OriginTy, PN.getNumIncomingValues(), "",
                               PN.getIterator());
    Value *UndefOrigin = UndefValue::get(DFSF.DFS.OriginTy);
    for (BasicBlock *BB : PN.blocks())
      OriginPN->addIncoming(UndefOrigin, BB);
    DFSF.setOrigin(&PN, OriginPN);
  }

  DFSF.PHIFixups.push_back({&PN, ShadowPN, OriginPN});
}

PreservedAnalyses DataFlowSanitizerPass::run(Module &M,
                                             ModuleAnalysisManager &AM) {
  auto GetTLI = [&](Function &F) -> TargetLibraryInfo & {
    auto &FAM =
        AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
    return FAM.getResult<TargetLibraryAnalysis>(F);
  };
  if (!DataFlowSanitizer(ABIListFiles).runImpl(M, GetTLI))
    return PreservedAnalyses::all();

  PreservedAnalyses PA = PreservedAnalyses::none();
  // GlobalsAA is considered stateless and does not get invalidated unless
  // explicitly invalidated; PreservedAnalyses::none() is not enough. Sanitizers
  // make changes that require GlobalsAA to be invalidated.
  PA.abandon<GlobalsAA>();
  return PA;
}
