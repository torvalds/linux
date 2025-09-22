//===-- NumericalStabilitySanitizer.cpp -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the instrumentation pass for the numerical sanitizer.
// Conceptually the pass injects shadow computations using higher precision
// types and inserts consistency checks. For details see the paper
// https://arxiv.org/abs/2102.12782.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Instrumentation/NumericalStabilitySanitizer.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Instrumentation.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/EscapeEnumerator.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

#include <cstdint>

using namespace llvm;

#define DEBUG_TYPE "nsan"

STATISTIC(NumInstrumentedFTLoads,
          "Number of instrumented floating-point loads");

STATISTIC(NumInstrumentedFTCalls,
          "Number of instrumented floating-point calls");
STATISTIC(NumInstrumentedFTRets,
          "Number of instrumented floating-point returns");
STATISTIC(NumInstrumentedFTStores,
          "Number of instrumented floating-point stores");
STATISTIC(NumInstrumentedNonFTStores,
          "Number of instrumented non floating-point stores");
STATISTIC(
    NumInstrumentedNonFTMemcpyStores,
    "Number of instrumented non floating-point stores with memcpy semantics");
STATISTIC(NumInstrumentedFCmp, "Number of instrumented fcmps");

// Using smaller shadow types types can help improve speed. For example, `dlq`
// is 3x slower to 5x faster in opt mode and 2-6x faster in dbg mode compared to
// `dqq`.
static cl::opt<std::string> ClShadowMapping(
    "nsan-shadow-type-mapping", cl::init("dqq"),
    cl::desc("One shadow type id for each of `float`, `double`, `long double`. "
             "`d`,`l`,`q`,`e` mean double, x86_fp80, fp128 (quad) and "
             "ppc_fp128 (extended double) respectively. The default is to "
             "shadow `float` as `double`, and `double` and `x86_fp80` as "
             "`fp128`"),
    cl::Hidden);

static cl::opt<bool>
    ClInstrumentFCmp("nsan-instrument-fcmp", cl::init(true),
                     cl::desc("Instrument floating-point comparisons"),
                     cl::Hidden);

static cl::opt<std::string> ClCheckFunctionsFilter(
    "check-functions-filter",
    cl::desc("Only emit checks for arguments of functions "
             "whose names match the given regular expression"),
    cl::value_desc("regex"));

static cl::opt<bool> ClTruncateFCmpEq(
    "nsan-truncate-fcmp-eq", cl::init(true),
    cl::desc(
        "This flag controls the behaviour of fcmp equality comparisons."
        "For equality comparisons such as `x == 0.0f`, we can perform the "
        "shadow check in the shadow (`x_shadow == 0.0) == (x == 0.0f)`) or app "
        " domain (`(trunc(x_shadow) == 0.0f) == (x == 0.0f)`). This helps "
        "catch the case when `x_shadow` is accurate enough (and therefore "
        "close enough to zero) so that `trunc(x_shadow)` is zero even though "
        "both `x` and `x_shadow` are not"),
    cl::Hidden);

// When there is external, uninstrumented code writing to memory, the shadow
// memory can get out of sync with the application memory. Enabling this flag
// emits consistency checks for loads to catch this situation.
// When everything is instrumented, this is not strictly necessary because any
// load should have a corresponding store, but can help debug cases when the
// framework did a bad job at tracking shadow memory modifications by failing on
// load rather than store.
// TODO: provide a way to resume computations from the FT value when the load
// is inconsistent. This ensures that further computations are not polluted.
static cl::opt<bool> ClCheckLoads("nsan-check-loads",
                                  cl::desc("Check floating-point load"),
                                  cl::Hidden);

static cl::opt<bool> ClCheckStores("nsan-check-stores", cl::init(true),
                                   cl::desc("Check floating-point stores"),
                                   cl::Hidden);

static cl::opt<bool> ClCheckRet("nsan-check-ret", cl::init(true),
                                cl::desc("Check floating-point return values"),
                                cl::Hidden);

// LLVM may store constant floats as bitcasted ints.
// It's not really necessary to shadow such stores,
// if the shadow value is unknown the framework will re-extend it on load
// anyway. Moreover, because of size collisions (e.g. bf16 vs f16) it is
// impossible to determine the floating-point type based on the size.
// However, for debugging purposes it can be useful to model such stores.
static cl::opt<bool> ClPropagateNonFTConstStoresAsFT(
    "nsan-propagate-non-ft-const-stores-as-ft",
    cl::desc(
        "Propagate non floating-point const stores as floating point values."
        "For debugging purposes only"),
    cl::Hidden);

constexpr StringLiteral kNsanModuleCtorName("nsan.module_ctor");
constexpr StringLiteral kNsanInitName("__nsan_init");

// The following values must be kept in sync with the runtime.
constexpr int kShadowScale = 2;
constexpr int kMaxVectorWidth = 8;
constexpr int kMaxNumArgs = 128;
constexpr int kMaxShadowTypeSizeBytes = 16; // fp128

namespace {

// Defines the characteristics (type id, type, and floating-point semantics)
// attached for all possible shadow types.
class ShadowTypeConfig {
public:
  static std::unique_ptr<ShadowTypeConfig> fromNsanTypeId(char TypeId);

  // The LLVM Type corresponding to the shadow type.
  virtual Type *getType(LLVMContext &Context) const = 0;

  // The nsan type id of the shadow type (`d`, `l`, `q`, ...).
  virtual char getNsanTypeId() const = 0;

  virtual ~ShadowTypeConfig() = default;
};

template <char NsanTypeId>
class ShadowTypeConfigImpl : public ShadowTypeConfig {
public:
  char getNsanTypeId() const override { return NsanTypeId; }
  static constexpr const char kNsanTypeId = NsanTypeId;
};

// `double` (`d`) shadow type.
class F64ShadowConfig : public ShadowTypeConfigImpl<'d'> {
  Type *getType(LLVMContext &Context) const override {
    return Type::getDoubleTy(Context);
  }
};

// `x86_fp80` (`l`) shadow type: X86 long double.
class F80ShadowConfig : public ShadowTypeConfigImpl<'l'> {
  Type *getType(LLVMContext &Context) const override {
    return Type::getX86_FP80Ty(Context);
  }
};

// `fp128` (`q`) shadow type.
class F128ShadowConfig : public ShadowTypeConfigImpl<'q'> {
  Type *getType(LLVMContext &Context) const override {
    return Type::getFP128Ty(Context);
  }
};

// `ppc_fp128` (`e`) shadow type: IBM extended double with 106 bits of mantissa.
class PPC128ShadowConfig : public ShadowTypeConfigImpl<'e'> {
  Type *getType(LLVMContext &Context) const override {
    return Type::getPPC_FP128Ty(Context);
  }
};

// Creates a ShadowTypeConfig given its type id.
std::unique_ptr<ShadowTypeConfig>
ShadowTypeConfig::fromNsanTypeId(const char TypeId) {
  switch (TypeId) {
  case F64ShadowConfig::kNsanTypeId:
    return std::make_unique<F64ShadowConfig>();
  case F80ShadowConfig::kNsanTypeId:
    return std::make_unique<F80ShadowConfig>();
  case F128ShadowConfig::kNsanTypeId:
    return std::make_unique<F128ShadowConfig>();
  case PPC128ShadowConfig::kNsanTypeId:
    return std::make_unique<PPC128ShadowConfig>();
  }
  report_fatal_error("nsan: invalid shadow type id '" + Twine(TypeId) + "'");
}

// An enum corresponding to shadow value types. Used as indices in arrays, so
// not an `enum class`.
enum FTValueType { kFloat, kDouble, kLongDouble, kNumValueTypes };

// If `FT` corresponds to a primitive FTValueType, return it.
static std::optional<FTValueType> ftValueTypeFromType(Type *FT) {
  if (FT->isFloatTy())
    return kFloat;
  if (FT->isDoubleTy())
    return kDouble;
  if (FT->isX86_FP80Ty())
    return kLongDouble;
  return {};
}

// Returns the LLVM type for an FTValueType.
static Type *typeFromFTValueType(FTValueType VT, LLVMContext &Context) {
  switch (VT) {
  case kFloat:
    return Type::getFloatTy(Context);
  case kDouble:
    return Type::getDoubleTy(Context);
  case kLongDouble:
    return Type::getX86_FP80Ty(Context);
  case kNumValueTypes:
    return nullptr;
  }
  llvm_unreachable("Unhandled FTValueType enum");
}

// Returns the type name for an FTValueType.
static const char *typeNameFromFTValueType(FTValueType VT) {
  switch (VT) {
  case kFloat:
    return "float";
  case kDouble:
    return "double";
  case kLongDouble:
    return "longdouble";
  case kNumValueTypes:
    return nullptr;
  }
  llvm_unreachable("Unhandled FTValueType enum");
}

// A specific mapping configuration of application type to shadow type for nsan
// (see -nsan-shadow-mapping flag).
class MappingConfig {
public:
  explicit MappingConfig(LLVMContext &C) : Context(C) {
    if (ClShadowMapping.size() != 3)
      report_fatal_error("Invalid nsan mapping: " + Twine(ClShadowMapping));
    unsigned ShadowTypeSizeBits[kNumValueTypes];
    for (int VT = 0; VT < kNumValueTypes; ++VT) {
      auto Config = ShadowTypeConfig::fromNsanTypeId(ClShadowMapping[VT]);
      if (!Config)
        report_fatal_error("Failed to get ShadowTypeConfig for " +
                           Twine(ClShadowMapping[VT]));
      const unsigned AppTypeSize =
          typeFromFTValueType(static_cast<FTValueType>(VT), Context)
              ->getScalarSizeInBits();
      const unsigned ShadowTypeSize =
          Config->getType(Context)->getScalarSizeInBits();
      // Check that the shadow type size is at most kShadowScale times the
      // application type size, so that shadow memory compoutations are valid.
      if (ShadowTypeSize > kShadowScale * AppTypeSize)
        report_fatal_error("Invalid nsan mapping f" + Twine(AppTypeSize) +
                           "->f" + Twine(ShadowTypeSize) +
                           ": The shadow type size should be at most " +
                           Twine(kShadowScale) +
                           " times the application type size");
      ShadowTypeSizeBits[VT] = ShadowTypeSize;
      Configs[VT] = std::move(Config);
    }

    // Check that the mapping is monotonous. This is required because if one
    // does an fpextend of `float->long double` in application code, nsan is
    // going to do an fpextend of `shadow(float) -> shadow(long double)` in
    // shadow code. This will fail in `qql` mode, since nsan would be
    // fpextending `f128->long`, which is invalid.
    // TODO: Relax this.
    if (ShadowTypeSizeBits[kFloat] > ShadowTypeSizeBits[kDouble] ||
        ShadowTypeSizeBits[kDouble] > ShadowTypeSizeBits[kLongDouble])
      report_fatal_error("Invalid nsan mapping: { float->f" +
                         Twine(ShadowTypeSizeBits[kFloat]) + "; double->f" +
                         Twine(ShadowTypeSizeBits[kDouble]) +
                         "; long double->f" +
                         Twine(ShadowTypeSizeBits[kLongDouble]) + " }");
  }

  const ShadowTypeConfig &byValueType(FTValueType VT) const {
    assert(VT < FTValueType::kNumValueTypes && "invalid value type");
    return *Configs[VT];
  }

  // Returns the extended shadow type for a given application type.
  Type *getExtendedFPType(Type *FT) const {
    if (const auto VT = ftValueTypeFromType(FT))
      return Configs[*VT]->getType(Context);
    if (FT->isVectorTy()) {
      auto *VecTy = cast<VectorType>(FT);
      // TODO: add support for scalable vector types.
      if (VecTy->isScalableTy())
        return nullptr;
      Type *ExtendedScalar = getExtendedFPType(VecTy->getElementType());
      return ExtendedScalar
                 ? VectorType::get(ExtendedScalar, VecTy->getElementCount())
                 : nullptr;
    }
    return nullptr;
  }

private:
  LLVMContext &Context;
  std::unique_ptr<ShadowTypeConfig> Configs[FTValueType::kNumValueTypes];
};

// The memory extents of a type specifies how many elements of a given
// FTValueType needs to be stored when storing this type.
struct MemoryExtents {
  FTValueType ValueType;
  uint64_t NumElts;
};

static MemoryExtents getMemoryExtentsOrDie(Type *FT) {
  if (const auto VT = ftValueTypeFromType(FT))
    return {*VT, 1};
  if (auto *VecTy = dyn_cast<VectorType>(FT)) {
    const auto ScalarExtents = getMemoryExtentsOrDie(VecTy->getElementType());
    return {ScalarExtents.ValueType,
            ScalarExtents.NumElts * VecTy->getElementCount().getFixedValue()};
  }
  llvm_unreachable("invalid value type");
}

// The location of a check. Passed as parameters to runtime checking functions.
class CheckLoc {
public:
  // Creates a location that references an application memory location.
  static CheckLoc makeStore(Value *Address) {
    CheckLoc Result(kStore);
    Result.Address = Address;
    return Result;
  }
  static CheckLoc makeLoad(Value *Address) {
    CheckLoc Result(kLoad);
    Result.Address = Address;
    return Result;
  }

  // Creates a location that references an argument, given by id.
  static CheckLoc makeArg(int ArgId) {
    CheckLoc Result(kArg);
    Result.ArgId = ArgId;
    return Result;
  }

  // Creates a location that references the return value of a function.
  static CheckLoc makeRet() { return CheckLoc(kRet); }

  // Creates a location that references a vector insert.
  static CheckLoc makeInsert() { return CheckLoc(kInsert); }

  // Returns the CheckType of location this refers to, as an integer-typed LLVM
  // IR value.
  Value *getType(LLVMContext &C) const {
    return ConstantInt::get(Type::getInt32Ty(C), static_cast<int>(CheckTy));
  }

  // Returns a CheckType-specific value representing details of the location
  // (e.g. application address for loads or stores), as an `IntptrTy`-typed LLVM
  // IR value.
  Value *getValue(Type *IntptrTy, IRBuilder<> &Builder) const {
    switch (CheckTy) {
    case kUnknown:
      llvm_unreachable("unknown type");
    case kRet:
    case kInsert:
      return ConstantInt::get(IntptrTy, 0);
    case kArg:
      return ConstantInt::get(IntptrTy, ArgId);
    case kLoad:
    case kStore:
      return Builder.CreatePtrToInt(Address, IntptrTy);
    }
    llvm_unreachable("Unhandled CheckType enum");
  }

private:
  // Must be kept in sync with the runtime,
  // see compiler-rt/lib/nsan/nsan_stats.h
  enum CheckType {
    kUnknown = 0,
    kRet,
    kArg,
    kLoad,
    kStore,
    kInsert,
  };
  explicit CheckLoc(CheckType CheckTy) : CheckTy(CheckTy) {}

  Value *Address = nullptr;
  const CheckType CheckTy;
  int ArgId = -1;
};

// A map of LLVM IR values to shadow LLVM IR values.
class ValueToShadowMap {
public:
  explicit ValueToShadowMap(const MappingConfig &Config) : Config(Config) {}

  ValueToShadowMap(const ValueToShadowMap &) = delete;
  ValueToShadowMap &operator=(const ValueToShadowMap &) = delete;

  // Sets the shadow value for a value. Asserts that the value does not already
  // have a value.
  void setShadow(Value &V, Value &Shadow) {
    [[maybe_unused]] const bool Inserted = Map.try_emplace(&V, &Shadow).second;
    LLVM_DEBUG({
      if (!Inserted) {
        if (auto *I = dyn_cast<Instruction>(&V))
          errs() << I->getFunction()->getName() << ": ";
        errs() << "duplicate shadow (" << &V << "): ";
        V.dump();
      }
    });
    assert(Inserted && "duplicate shadow");
  }

  // Returns true if the value already has a shadow (including if the value is a
  // constant). If true, calling getShadow() is valid.
  bool hasShadow(Value *V) const {
    return isa<Constant>(V) || (Map.find(V) != Map.end());
  }

  // Returns the shadow value for a given value. Asserts that the value has
  // a shadow value. Lazily creates shadows for constant values.
  Value *getShadow(Value *V) const {
    if (Constant *C = dyn_cast<Constant>(V))
      return getShadowConstant(C);
    return Map.find(V)->second;
  }

  bool empty() const { return Map.empty(); }

private:
  // Extends a constant application value to its shadow counterpart.
  APFloat extendConstantFP(APFloat CV, const fltSemantics &To) const {
    bool LosesInfo = false;
    CV.convert(To, APFloatBase::rmTowardZero, &LosesInfo);
    return CV;
  }

  // Returns the shadow constant for the given application constant.
  Constant *getShadowConstant(Constant *C) const {
    if (UndefValue *U = dyn_cast<UndefValue>(C)) {
      return UndefValue::get(Config.getExtendedFPType(U->getType()));
    }
    if (ConstantFP *CFP = dyn_cast<ConstantFP>(C)) {
      // Floating-point constants.
      Type *Ty = Config.getExtendedFPType(CFP->getType());
      return ConstantFP::get(
          Ty, extendConstantFP(CFP->getValueAPF(), Ty->getFltSemantics()));
    }
    // Vector, array, or aggregate constants.
    if (C->getType()->isVectorTy()) {
      SmallVector<Constant *, 8> Elements;
      for (int I = 0, E = cast<VectorType>(C->getType())
                              ->getElementCount()
                              .getFixedValue();
           I < E; ++I)
        Elements.push_back(getShadowConstant(C->getAggregateElement(I)));
      return ConstantVector::get(Elements);
    }
    llvm_unreachable("unimplemented");
  }

  const MappingConfig &Config;
  DenseMap<Value *, Value *> Map;
};

/// Instantiating NumericalStabilitySanitizer inserts the nsan runtime library
/// API function declarations into the module if they don't exist already.
/// Instantiating ensures the __nsan_init function is in the list of global
/// constructors for the module.
class NumericalStabilitySanitizer {
public:
  NumericalStabilitySanitizer(Module &M);
  bool sanitizeFunction(Function &F, const TargetLibraryInfo &TLI);

private:
  bool instrumentMemIntrinsic(MemIntrinsic *MI);
  void maybeAddSuffixForNsanInterface(CallBase *CI);
  bool addrPointsToConstantData(Value *Addr);
  void maybeCreateShadowValue(Instruction &Root, const TargetLibraryInfo &TLI,
                              ValueToShadowMap &Map);
  Value *createShadowValueWithOperandsAvailable(Instruction &Inst,
                                                const TargetLibraryInfo &TLI,
                                                const ValueToShadowMap &Map);
  PHINode *maybeCreateShadowPhi(PHINode &Phi, const TargetLibraryInfo &TLI);
  void createShadowArguments(Function &F, const TargetLibraryInfo &TLI,
                             ValueToShadowMap &Map);

  void populateShadowStack(CallBase &CI, const TargetLibraryInfo &TLI,
                           const ValueToShadowMap &Map);

  void propagateShadowValues(Instruction &Inst, const TargetLibraryInfo &TLI,
                             const ValueToShadowMap &Map);
  Value *emitCheck(Value *V, Value *ShadowV, IRBuilder<> &Builder,
                   CheckLoc Loc);
  Value *emitCheckInternal(Value *V, Value *ShadowV, IRBuilder<> &Builder,
                           CheckLoc Loc);
  void emitFCmpCheck(FCmpInst &FCmp, const ValueToShadowMap &Map);

  // Value creation handlers.
  Value *handleLoad(LoadInst &Load, Type *VT, Type *ExtendedVT);
  Value *handleCallBase(CallBase &Call, Type *VT, Type *ExtendedVT,
                        const TargetLibraryInfo &TLI,
                        const ValueToShadowMap &Map, IRBuilder<> &Builder);
  Value *maybeHandleKnownCallBase(CallBase &Call, Type *VT, Type *ExtendedVT,
                                  const TargetLibraryInfo &TLI,
                                  const ValueToShadowMap &Map,
                                  IRBuilder<> &Builder);
  Value *handleTrunc(const FPTruncInst &Trunc, Type *VT, Type *ExtendedVT,
                     const ValueToShadowMap &Map, IRBuilder<> &Builder);
  Value *handleExt(const FPExtInst &Ext, Type *VT, Type *ExtendedVT,
                   const ValueToShadowMap &Map, IRBuilder<> &Builder);

  // Value propagation handlers.
  void propagateFTStore(StoreInst &Store, Type *VT, Type *ExtendedVT,
                        const ValueToShadowMap &Map);
  void propagateNonFTStore(StoreInst &Store, Type *VT,
                           const ValueToShadowMap &Map);

  const DataLayout &DL;
  LLVMContext &Context;
  MappingConfig Config;
  IntegerType *IntptrTy = nullptr;
  FunctionCallee NsanGetShadowPtrForStore[FTValueType::kNumValueTypes] = {};
  FunctionCallee NsanGetShadowPtrForLoad[FTValueType::kNumValueTypes] = {};
  FunctionCallee NsanCheckValue[FTValueType::kNumValueTypes] = {};
  FunctionCallee NsanFCmpFail[FTValueType::kNumValueTypes] = {};
  FunctionCallee NsanCopyValues;
  FunctionCallee NsanSetValueUnknown;
  FunctionCallee NsanGetRawShadowTypePtr;
  FunctionCallee NsanGetRawShadowPtr;
  GlobalValue *NsanShadowRetTag = nullptr;

  Type *NsanShadowRetType = nullptr;
  GlobalValue *NsanShadowRetPtr = nullptr;

  GlobalValue *NsanShadowArgsTag = nullptr;

  Type *NsanShadowArgsType = nullptr;
  GlobalValue *NsanShadowArgsPtr = nullptr;

  std::optional<Regex> CheckFunctionsFilter;
};
} // end anonymous namespace

PreservedAnalyses
NumericalStabilitySanitizerPass::run(Module &M, ModuleAnalysisManager &MAM) {
  getOrCreateSanitizerCtorAndInitFunctions(
      M, kNsanModuleCtorName, kNsanInitName, /*InitArgTypes=*/{},
      /*InitArgs=*/{},
      // This callback is invoked when the functions are created the first
      // time. Hook them into the global ctors list in that case:
      [&](Function *Ctor, FunctionCallee) { appendToGlobalCtors(M, Ctor, 0); });

  NumericalStabilitySanitizer Nsan(M);
  auto &FAM = MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
  for (Function &F : M)
    Nsan.sanitizeFunction(F, FAM.getResult<TargetLibraryAnalysis>(F));

  return PreservedAnalyses::none();
}

static GlobalValue *createThreadLocalGV(const char *Name, Module &M, Type *Ty) {
  return dyn_cast<GlobalValue>(M.getOrInsertGlobal(Name, Ty, [&M, Ty, Name] {
    return new GlobalVariable(M, Ty, false, GlobalVariable::ExternalLinkage,
                              nullptr, Name, nullptr,
                              GlobalVariable::InitialExecTLSModel);
  }));
}

NumericalStabilitySanitizer::NumericalStabilitySanitizer(Module &M)
    : DL(M.getDataLayout()), Context(M.getContext()), Config(Context) {
  IntptrTy = DL.getIntPtrType(Context);
  Type *PtrTy = PointerType::getUnqual(Context);
  Type *Int32Ty = Type::getInt32Ty(Context);
  Type *Int1Ty = Type::getInt1Ty(Context);
  Type *VoidTy = Type::getVoidTy(Context);

  AttributeList Attr;
  Attr = Attr.addFnAttribute(Context, Attribute::NoUnwind);
  // Initialize the runtime values (functions and global variables).
  for (int I = 0; I < kNumValueTypes; ++I) {
    const FTValueType VT = static_cast<FTValueType>(I);
    const char *VTName = typeNameFromFTValueType(VT);
    Type *VTTy = typeFromFTValueType(VT, Context);

    // Load/store.
    const std::string GetterPrefix =
        std::string("__nsan_get_shadow_ptr_for_") + VTName;
    NsanGetShadowPtrForStore[VT] = M.getOrInsertFunction(
        GetterPrefix + "_store", Attr, PtrTy, PtrTy, IntptrTy);
    NsanGetShadowPtrForLoad[VT] = M.getOrInsertFunction(
        GetterPrefix + "_load", Attr, PtrTy, PtrTy, IntptrTy);

    // Check.
    const auto &ShadowConfig = Config.byValueType(VT);
    Type *ShadowTy = ShadowConfig.getType(Context);
    NsanCheckValue[VT] =
        M.getOrInsertFunction(std::string("__nsan_internal_check_") + VTName +
                                  "_" + ShadowConfig.getNsanTypeId(),
                              Attr, Int32Ty, VTTy, ShadowTy, Int32Ty, IntptrTy);
    NsanFCmpFail[VT] = M.getOrInsertFunction(
        std::string("__nsan_fcmp_fail_") + VTName + "_" +
            ShadowConfig.getNsanTypeId(),
        Attr, VoidTy, VTTy, VTTy, ShadowTy, ShadowTy, Int32Ty, Int1Ty, Int1Ty);
  }

  NsanCopyValues = M.getOrInsertFunction("__nsan_copy_values", Attr, VoidTy,
                                         PtrTy, PtrTy, IntptrTy);
  NsanSetValueUnknown = M.getOrInsertFunction("__nsan_set_value_unknown", Attr,
                                              VoidTy, PtrTy, IntptrTy);

  // TODO: Add attributes nofree, nosync, readnone, readonly,
  NsanGetRawShadowTypePtr = M.getOrInsertFunction(
      "__nsan_internal_get_raw_shadow_type_ptr", Attr, PtrTy, PtrTy);
  NsanGetRawShadowPtr = M.getOrInsertFunction(
      "__nsan_internal_get_raw_shadow_ptr", Attr, PtrTy, PtrTy);

  NsanShadowRetTag = createThreadLocalGV("__nsan_shadow_ret_tag", M, IntptrTy);

  NsanShadowRetType = ArrayType::get(Type::getInt8Ty(Context),
                                     kMaxVectorWidth * kMaxShadowTypeSizeBytes);
  NsanShadowRetPtr =
      createThreadLocalGV("__nsan_shadow_ret_ptr", M, NsanShadowRetType);

  NsanShadowArgsTag =
      createThreadLocalGV("__nsan_shadow_args_tag", M, IntptrTy);

  NsanShadowArgsType =
      ArrayType::get(Type::getInt8Ty(Context),
                     kMaxVectorWidth * kMaxNumArgs * kMaxShadowTypeSizeBytes);

  NsanShadowArgsPtr =
      createThreadLocalGV("__nsan_shadow_args_ptr", M, NsanShadowArgsType);

  if (!ClCheckFunctionsFilter.empty()) {
    Regex R = Regex(ClCheckFunctionsFilter);
    std::string RegexError;
    assert(R.isValid(RegexError));
    CheckFunctionsFilter = std::move(R);
  }
}

// Returns true if the given LLVM Value points to constant data (typically, a
// global variable reference).
bool NumericalStabilitySanitizer::addrPointsToConstantData(Value *Addr) {
  // If this is a GEP, just analyze its pointer operand.
  if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(Addr))
    Addr = GEP->getPointerOperand();

  if (GlobalVariable *GV = dyn_cast<GlobalVariable>(Addr))
    return GV->isConstant();
  return false;
}

// This instruments the function entry to create shadow arguments.
// Pseudocode:
//   if (this_fn_ptr == __nsan_shadow_args_tag) {
//     s(arg0) = LOAD<sizeof(arg0)>(__nsan_shadow_args);
//     s(arg1) = LOAD<sizeof(arg1)>(__nsan_shadow_args + sizeof(arg0));
//     ...
//     __nsan_shadow_args_tag = 0;
//   } else {
//     s(arg0) = fext(arg0);
//     s(arg1) = fext(arg1);
//     ...
//   }
void NumericalStabilitySanitizer::createShadowArguments(
    Function &F, const TargetLibraryInfo &TLI, ValueToShadowMap &Map) {
  assert(!F.getIntrinsicID() && "found a definition of an intrinsic");

  // Do not bother if there are no FP args.
  if (all_of(F.args(), [this](const Argument &Arg) {
        return Config.getExtendedFPType(Arg.getType()) == nullptr;
      }))
    return;

  IRBuilder<> Builder(F.getEntryBlock().getFirstNonPHI());
  // The function has shadow args if the shadow args tag matches the function
  // address.
  Value *HasShadowArgs = Builder.CreateICmpEQ(
      Builder.CreateLoad(IntptrTy, NsanShadowArgsTag, /*isVolatile=*/false),
      Builder.CreatePtrToInt(&F, IntptrTy));

  unsigned ShadowArgsOffsetBytes = 0;
  for (Argument &Arg : F.args()) {
    Type *VT = Arg.getType();
    Type *ExtendedVT = Config.getExtendedFPType(VT);
    if (ExtendedVT == nullptr)
      continue; // Not an FT value.
    Value *L = Builder.CreateAlignedLoad(
        ExtendedVT,
        Builder.CreateConstGEP2_64(NsanShadowArgsType, NsanShadowArgsPtr, 0,
                                   ShadowArgsOffsetBytes),
        Align(1), /*isVolatile=*/false);
    Value *Shadow = Builder.CreateSelect(HasShadowArgs, L,
                                         Builder.CreateFPExt(&Arg, ExtendedVT));
    Map.setShadow(Arg, *Shadow);
    TypeSize SlotSize = DL.getTypeStoreSize(ExtendedVT);
    assert(!SlotSize.isScalable() && "unsupported");
    ShadowArgsOffsetBytes += SlotSize;
  }
  Builder.CreateStore(ConstantInt::get(IntptrTy, 0), NsanShadowArgsTag);
}

// Returns true if the instrumentation should emit code to check arguments
// before a function call.
static bool shouldCheckArgs(CallBase &CI, const TargetLibraryInfo &TLI,
                            const std::optional<Regex> &CheckFunctionsFilter) {

  Function *Fn = CI.getCalledFunction();

  if (CheckFunctionsFilter) {
    // Skip checking args of indirect calls.
    if (Fn == nullptr)
      return false;
    if (CheckFunctionsFilter->match(Fn->getName()))
      return true;
    return false;
  }

  if (Fn == nullptr)
    return true; // Always check args of indirect calls.

  // Never check nsan functions, the user called them for a reason.
  if (Fn->getName().starts_with("__nsan_"))
    return false;

  const auto ID = Fn->getIntrinsicID();
  LibFunc LFunc = LibFunc::NumLibFuncs;
  // Always check args of unknown functions.
  if (ID == Intrinsic::ID() && !TLI.getLibFunc(*Fn, LFunc))
    return true;

  // Do not check args of an `fabs` call that is used for a comparison.
  // This is typically used for `fabs(a-b) < tolerance`, where what matters is
  // the result of the comparison, which is already caught be the fcmp checks.
  if (ID == Intrinsic::fabs || LFunc == LibFunc_fabsf ||
      LFunc == LibFunc_fabs || LFunc == LibFunc_fabsl)
    for (const auto &U : CI.users())
      if (isa<CmpInst>(U))
        return false;

  return true; // Default is check.
}

// Populates the shadow call stack (which contains shadow values for every
// floating-point parameter to the function).
void NumericalStabilitySanitizer::populateShadowStack(
    CallBase &CI, const TargetLibraryInfo &TLI, const ValueToShadowMap &Map) {
  // Do not create a shadow stack for inline asm.
  if (CI.isInlineAsm())
    return;

  // Do not bother if there are no FP args.
  if (all_of(CI.operands(), [this](const Value *Arg) {
        return Config.getExtendedFPType(Arg->getType()) == nullptr;
      }))
    return;

  IRBuilder<> Builder(&CI);
  SmallVector<Value *, 8> ArgShadows;
  const bool ShouldCheckArgs = shouldCheckArgs(CI, TLI, CheckFunctionsFilter);
  for (auto [ArgIdx, Arg] : enumerate(CI.operands())) {
    if (Config.getExtendedFPType(Arg->getType()) == nullptr)
      continue; // Not an FT value.
    Value *ArgShadow = Map.getShadow(Arg);
    ArgShadows.push_back(ShouldCheckArgs ? emitCheck(Arg, ArgShadow, Builder,
                                                     CheckLoc::makeArg(ArgIdx))
                                         : ArgShadow);
  }

  // Do not create shadow stacks for intrinsics/known lib funcs.
  if (Function *Fn = CI.getCalledFunction()) {
    LibFunc LFunc;
    if (Fn->isIntrinsic() || TLI.getLibFunc(*Fn, LFunc))
      return;
  }

  // Set the shadow stack tag.
  Builder.CreateStore(CI.getCalledOperand(), NsanShadowArgsTag);
  TypeSize ShadowArgsOffsetBytes = TypeSize::getFixed(0);

  unsigned ShadowArgId = 0;
  for (const Value *Arg : CI.operands()) {
    Type *VT = Arg->getType();
    Type *ExtendedVT = Config.getExtendedFPType(VT);
    if (ExtendedVT == nullptr)
      continue; // Not an FT value.
    Builder.CreateAlignedStore(
        ArgShadows[ShadowArgId++],
        Builder.CreateConstGEP2_64(NsanShadowArgsType, NsanShadowArgsPtr, 0,
                                   ShadowArgsOffsetBytes),
        Align(1), /*isVolatile=*/false);
    TypeSize SlotSize = DL.getTypeStoreSize(ExtendedVT);
    assert(!SlotSize.isScalable() && "unsupported");
    ShadowArgsOffsetBytes += SlotSize;
  }
}

// Internal part of emitCheck(). Returns a value that indicates whether
// computation should continue with the shadow or resume by re-fextending the
// value.
enum class ContinuationType { // Keep in sync with runtime.
  ContinueWithShadow = 0,
  ResumeFromValue = 1,
};

Value *NumericalStabilitySanitizer::emitCheckInternal(Value *V, Value *ShadowV,
                                                      IRBuilder<> &Builder,
                                                      CheckLoc Loc) {
  // Do not emit checks for constant values, this is redundant.
  if (isa<Constant>(V))
    return ConstantInt::get(
        Builder.getInt32Ty(),
        static_cast<int>(ContinuationType::ContinueWithShadow));

  Type *Ty = V->getType();
  if (const auto VT = ftValueTypeFromType(Ty))
    return Builder.CreateCall(
        NsanCheckValue[*VT],
        {V, ShadowV, Loc.getType(Context), Loc.getValue(IntptrTy, Builder)});

  if (Ty->isVectorTy()) {
    auto *VecTy = cast<VectorType>(Ty);
    // We currently skip scalable vector types in MappingConfig,
    // thus we should not encounter any such types here.
    assert(!VecTy->isScalableTy() &&
           "Scalable vector types are not supported yet");
    Value *CheckResult = nullptr;
    for (int I = 0, E = VecTy->getElementCount().getFixedValue(); I < E; ++I) {
      // We resume if any element resumes. Another option would be to create a
      // vector shuffle with the array of ContinueWithShadow, but that is too
      // complex.
      Value *ExtractV = Builder.CreateExtractElement(V, I);
      Value *ExtractShadowV = Builder.CreateExtractElement(ShadowV, I);
      Value *ComponentCheckResult =
          emitCheckInternal(ExtractV, ExtractShadowV, Builder, Loc);
      CheckResult = CheckResult
                        ? Builder.CreateOr(CheckResult, ComponentCheckResult)
                        : ComponentCheckResult;
    }
    return CheckResult;
  }
  if (Ty->isArrayTy()) {
    Value *CheckResult = nullptr;
    for (auto I : seq(Ty->getArrayNumElements())) {
      Value *ExtractV = Builder.CreateExtractElement(V, I);
      Value *ExtractShadowV = Builder.CreateExtractElement(ShadowV, I);
      Value *ComponentCheckResult =
          emitCheckInternal(ExtractV, ExtractShadowV, Builder, Loc);
      CheckResult = CheckResult
                        ? Builder.CreateOr(CheckResult, ComponentCheckResult)
                        : ComponentCheckResult;
    }
    return CheckResult;
  }
  if (Ty->isStructTy()) {
    Value *CheckResult = nullptr;
    for (auto I : seq(Ty->getStructNumElements())) {
      if (Config.getExtendedFPType(Ty->getStructElementType(I)) == nullptr)
        continue; // Only check FT values.
      Value *ExtractV = Builder.CreateExtractValue(V, I);
      Value *ExtractShadowV = Builder.CreateExtractElement(ShadowV, I);
      Value *ComponentCheckResult =
          emitCheckInternal(ExtractV, ExtractShadowV, Builder, Loc);
      CheckResult = CheckResult
                        ? Builder.CreateOr(CheckResult, ComponentCheckResult)
                        : ComponentCheckResult;
    }
    if (!CheckResult)
      return ConstantInt::get(
          Builder.getInt32Ty(),
          static_cast<int>(ContinuationType::ContinueWithShadow));
    return CheckResult;
  }

  llvm_unreachable("not implemented");
}

// Inserts a runtime check of V against its shadow value ShadowV.
// We check values whenever they escape: on return, call, stores, and
// insertvalue.
// Returns the shadow value that should be used to continue the computations,
// depending on the answer from the runtime.
// TODO: Should we check on select ? phi ?
Value *NumericalStabilitySanitizer::emitCheck(Value *V, Value *ShadowV,
                                              IRBuilder<> &Builder,
                                              CheckLoc Loc) {
  // Do not emit checks for constant values, this is redundant.
  if (isa<Constant>(V))
    return ShadowV;

  if (Instruction *Inst = dyn_cast<Instruction>(V)) {
    Function *F = Inst->getFunction();
    if (CheckFunctionsFilter && !CheckFunctionsFilter->match(F->getName())) {
      return ShadowV;
    }
  }

  Value *CheckResult = emitCheckInternal(V, ShadowV, Builder, Loc);
  Value *ICmpEQ = Builder.CreateICmpEQ(
      CheckResult,
      ConstantInt::get(Builder.getInt32Ty(),
                       static_cast<int>(ContinuationType::ResumeFromValue)));
  return Builder.CreateSelect(
      ICmpEQ, Builder.CreateFPExt(V, Config.getExtendedFPType(V->getType())),
      ShadowV);
}

// Inserts a check that fcmp on shadow values are consistent with that on base
// values.
void NumericalStabilitySanitizer::emitFCmpCheck(FCmpInst &FCmp,
                                                const ValueToShadowMap &Map) {
  if (!ClInstrumentFCmp)
    return;

  Function *F = FCmp.getFunction();
  if (CheckFunctionsFilter && !CheckFunctionsFilter->match(F->getName()))
    return;

  Value *LHS = FCmp.getOperand(0);
  if (Config.getExtendedFPType(LHS->getType()) == nullptr)
    return;
  Value *RHS = FCmp.getOperand(1);

  // Split the basic block. On mismatch, we'll jump to the new basic block with
  // a call to the runtime for error reporting.
  BasicBlock *FCmpBB = FCmp.getParent();
  BasicBlock *NextBB = FCmpBB->splitBasicBlock(FCmp.getNextNode());
  // Remove the newly created terminator unconditional branch.
  FCmpBB->back().eraseFromParent();
  BasicBlock *FailBB =
      BasicBlock::Create(Context, "", FCmpBB->getParent(), NextBB);

  // Create the shadow fcmp and comparison between the fcmps.
  IRBuilder<> FCmpBuilder(FCmpBB);
  FCmpBuilder.SetCurrentDebugLocation(FCmp.getDebugLoc());
  Value *ShadowLHS = Map.getShadow(LHS);
  Value *ShadowRHS = Map.getShadow(RHS);
  // See comment on ClTruncateFCmpEq.
  if (FCmp.isEquality() && ClTruncateFCmpEq) {
    Type *Ty = ShadowLHS->getType();
    ShadowLHS = FCmpBuilder.CreateFPExt(
        FCmpBuilder.CreateFPTrunc(ShadowLHS, LHS->getType()), Ty);
    ShadowRHS = FCmpBuilder.CreateFPExt(
        FCmpBuilder.CreateFPTrunc(ShadowRHS, RHS->getType()), Ty);
  }
  Value *ShadowFCmp =
      FCmpBuilder.CreateFCmp(FCmp.getPredicate(), ShadowLHS, ShadowRHS);
  Value *OriginalAndShadowFcmpMatch =
      FCmpBuilder.CreateICmpEQ(&FCmp, ShadowFCmp);

  if (OriginalAndShadowFcmpMatch->getType()->isVectorTy()) {
    // If we have a vector type, `OriginalAndShadowFcmpMatch` is a vector of i1,
    // where an element is true if the corresponding elements in original and
    // shadow are the same. We want all elements to be 1.
    OriginalAndShadowFcmpMatch =
        FCmpBuilder.CreateAndReduce(OriginalAndShadowFcmpMatch);
  }

  // Use MDBuilder(*C).createLikelyBranchWeights() because "match" is the common
  // case.
  FCmpBuilder.CreateCondBr(OriginalAndShadowFcmpMatch, NextBB, FailBB,
                           MDBuilder(Context).createLikelyBranchWeights());

  // Fill in FailBB.
  IRBuilder<> FailBuilder(FailBB);
  FailBuilder.SetCurrentDebugLocation(FCmp.getDebugLoc());

  const auto EmitFailCall = [this, &FCmp, &FCmpBuilder,
                             &FailBuilder](Value *L, Value *R, Value *ShadowL,
                                           Value *ShadowR, Value *Result,
                                           Value *ShadowResult) {
    Type *FT = L->getType();
    FunctionCallee *Callee = nullptr;
    if (FT->isFloatTy()) {
      Callee = &(NsanFCmpFail[kFloat]);
    } else if (FT->isDoubleTy()) {
      Callee = &(NsanFCmpFail[kDouble]);
    } else if (FT->isX86_FP80Ty()) {
      // TODO: make NsanFCmpFailLongDouble work.
      Callee = &(NsanFCmpFail[kDouble]);
      L = FailBuilder.CreateFPTrunc(L, Type::getDoubleTy(Context));
      R = FailBuilder.CreateFPTrunc(L, Type::getDoubleTy(Context));
    } else {
      llvm_unreachable("not implemented");
    }
    FailBuilder.CreateCall(*Callee, {L, R, ShadowL, ShadowR,
                                     ConstantInt::get(FCmpBuilder.getInt32Ty(),
                                                      FCmp.getPredicate()),
                                     Result, ShadowResult});
  };
  if (LHS->getType()->isVectorTy()) {
    for (int I = 0, E = cast<VectorType>(LHS->getType())
                            ->getElementCount()
                            .getFixedValue();
         I < E; ++I) {
      Value *ExtractLHS = FailBuilder.CreateExtractElement(LHS, I);
      Value *ExtractRHS = FailBuilder.CreateExtractElement(RHS, I);
      Value *ExtractShaodwLHS = FailBuilder.CreateExtractElement(ShadowLHS, I);
      Value *ExtractShaodwRHS = FailBuilder.CreateExtractElement(ShadowRHS, I);
      Value *ExtractFCmp = FailBuilder.CreateExtractElement(&FCmp, I);
      Value *ExtractShadowFCmp =
          FailBuilder.CreateExtractElement(ShadowFCmp, I);
      EmitFailCall(ExtractLHS, ExtractRHS, ExtractShaodwLHS, ExtractShaodwRHS,
                   ExtractFCmp, ExtractShadowFCmp);
    }
  } else {
    EmitFailCall(LHS, RHS, ShadowLHS, ShadowRHS, &FCmp, ShadowFCmp);
  }
  FailBuilder.CreateBr(NextBB);

  ++NumInstrumentedFCmp;
}

// Creates a shadow phi value for any phi that defines a value of FT type.
PHINode *NumericalStabilitySanitizer::maybeCreateShadowPhi(
    PHINode &Phi, const TargetLibraryInfo &TLI) {
  Type *VT = Phi.getType();
  Type *ExtendedVT = Config.getExtendedFPType(VT);
  if (ExtendedVT == nullptr)
    return nullptr; // Not an FT value.
  // The phi operands are shadow values and are not available when the phi is
  // created. They will be populated in a final phase, once all shadow values
  // have been created.
  PHINode *Shadow = PHINode::Create(ExtendedVT, Phi.getNumIncomingValues());
  Shadow->insertAfter(&Phi);
  return Shadow;
}

Value *NumericalStabilitySanitizer::handleLoad(LoadInst &Load, Type *VT,
                                               Type *ExtendedVT) {
  IRBuilder<> Builder(Load.getNextNode());
  Builder.SetCurrentDebugLocation(Load.getDebugLoc());
  if (addrPointsToConstantData(Load.getPointerOperand())) {
    // No need to look into the shadow memory, the value is a constant. Just
    // convert from FT to 2FT.
    return Builder.CreateFPExt(&Load, ExtendedVT);
  }

  // if (%shadowptr == &)
  //    %shadow = fpext %v
  // else
  //    %shadow = load (ptrcast %shadow_ptr))
  // Considered options here:
  //  - Have `NsanGetShadowPtrForLoad` return a fixed address
  //    &__nsan_unknown_value_shadow_address that is valid to load from, and
  //    use a select. This has the advantage that the generated IR is simpler.
  //  - Have `NsanGetShadowPtrForLoad` return nullptr.  Because `select` does
  //    not short-circuit, dereferencing the returned pointer is no longer an
  //    option, have to split and create a separate basic block. This has the
  //    advantage of being easier to debug because it crashes if we ever mess
  //    up.

  const auto Extents = getMemoryExtentsOrDie(VT);
  Value *ShadowPtr = Builder.CreateCall(
      NsanGetShadowPtrForLoad[Extents.ValueType],
      {Load.getPointerOperand(), ConstantInt::get(IntptrTy, Extents.NumElts)});
  ++NumInstrumentedFTLoads;

  // Split the basic block.
  BasicBlock *LoadBB = Load.getParent();
  BasicBlock *NextBB = LoadBB->splitBasicBlock(Builder.GetInsertPoint());
  // Create the two options for creating the shadow value.
  BasicBlock *ShadowLoadBB =
      BasicBlock::Create(Context, "", LoadBB->getParent(), NextBB);
  BasicBlock *FExtBB =
      BasicBlock::Create(Context, "", LoadBB->getParent(), NextBB);

  // Replace the newly created terminator unconditional branch by a conditional
  // branch to one of the options.
  {
    LoadBB->back().eraseFromParent();
    IRBuilder<> LoadBBBuilder(LoadBB); // The old builder has been invalidated.
    LoadBBBuilder.SetCurrentDebugLocation(Load.getDebugLoc());
    LoadBBBuilder.CreateCondBr(LoadBBBuilder.CreateIsNull(ShadowPtr), FExtBB,
                               ShadowLoadBB);
  }

  // Fill in ShadowLoadBB.
  IRBuilder<> ShadowLoadBBBuilder(ShadowLoadBB);
  ShadowLoadBBBuilder.SetCurrentDebugLocation(Load.getDebugLoc());
  Value *ShadowLoad = ShadowLoadBBBuilder.CreateAlignedLoad(
      ExtendedVT, ShadowPtr, Align(1), Load.isVolatile());
  if (ClCheckLoads) {
    ShadowLoad = emitCheck(&Load, ShadowLoad, ShadowLoadBBBuilder,
                           CheckLoc::makeLoad(Load.getPointerOperand()));
  }
  ShadowLoadBBBuilder.CreateBr(NextBB);

  // Fill in FExtBB.
  IRBuilder<> FExtBBBuilder(FExtBB);
  FExtBBBuilder.SetCurrentDebugLocation(Load.getDebugLoc());
  Value *FExt = FExtBBBuilder.CreateFPExt(&Load, ExtendedVT);
  FExtBBBuilder.CreateBr(NextBB);

  // The shadow value come from any of the options.
  IRBuilder<> NextBBBuilder(&*NextBB->begin());
  NextBBBuilder.SetCurrentDebugLocation(Load.getDebugLoc());
  PHINode *ShadowPhi = NextBBBuilder.CreatePHI(ExtendedVT, 2);
  ShadowPhi->addIncoming(ShadowLoad, ShadowLoadBB);
  ShadowPhi->addIncoming(FExt, FExtBB);
  return ShadowPhi;
}

Value *NumericalStabilitySanitizer::handleTrunc(const FPTruncInst &Trunc,
                                                Type *VT, Type *ExtendedVT,
                                                const ValueToShadowMap &Map,
                                                IRBuilder<> &Builder) {
  Value *OrigSource = Trunc.getOperand(0);
  Type *OrigSourceTy = OrigSource->getType();
  Type *ExtendedSourceTy = Config.getExtendedFPType(OrigSourceTy);

  // When truncating:
  //  - (A) If the source has a shadow, we truncate from the shadow, else we
  //    truncate from the original source.
  //  - (B) If the shadow of the source is larger than the shadow of the dest,
  //    we still need a truncate. Else, the shadow of the source is the same
  //    type as the shadow of the dest (because mappings are non-decreasing), so
  //   we don't need to emit a truncate.
  // Examples,
  //   with a mapping of {f32->f64;f64->f80;f80->f128}
  //     fptrunc double   %1 to float     ->  fptrunc x86_fp80 s(%1) to double
  //     fptrunc x86_fp80 %1 to float     ->  fptrunc fp128    s(%1) to double
  //     fptrunc fp128    %1 to float     ->  fptrunc fp128    %1    to double
  //     fptrunc x86_fp80 %1 to double    ->  x86_fp80 s(%1)
  //     fptrunc fp128    %1 to double    ->  fptrunc fp128 %1 to x86_fp80
  //     fptrunc fp128    %1 to x86_fp80  ->  fp128 %1
  //   with a mapping of {f32->f64;f64->f128;f80->f128}
  //     fptrunc double   %1 to float     ->  fptrunc fp128    s(%1) to double
  //     fptrunc x86_fp80 %1 to float     ->  fptrunc fp128    s(%1) to double
  //     fptrunc fp128    %1 to float     ->  fptrunc fp128    %1    to double
  //     fptrunc x86_fp80 %1 to double    ->  fp128 %1
  //     fptrunc fp128    %1 to double    ->  fp128 %1
  //     fptrunc fp128    %1 to x86_fp80  ->  fp128 %1
  //   with a mapping of {f32->f32;f64->f32;f80->f64}
  //     fptrunc double   %1 to float     ->  float s(%1)
  //     fptrunc x86_fp80 %1 to float     ->  fptrunc double    s(%1) to float
  //     fptrunc fp128    %1 to float     ->  fptrunc fp128     %1    to float
  //     fptrunc x86_fp80 %1 to double    ->  fptrunc double    s(%1) to float
  //     fptrunc fp128    %1 to double    ->  fptrunc fp128     %1    to float
  //     fptrunc fp128    %1 to x86_fp80  ->  fptrunc fp128     %1    to double

  // See (A) above.
  Value *Source = ExtendedSourceTy ? Map.getShadow(OrigSource) : OrigSource;
  Type *SourceTy = ExtendedSourceTy ? ExtendedSourceTy : OrigSourceTy;
  // See (B) above.
  if (SourceTy == ExtendedVT)
    return Source;

  return Builder.CreateFPTrunc(Source, ExtendedVT);
}

Value *NumericalStabilitySanitizer::handleExt(const FPExtInst &Ext, Type *VT,
                                              Type *ExtendedVT,
                                              const ValueToShadowMap &Map,
                                              IRBuilder<> &Builder) {
  Value *OrigSource = Ext.getOperand(0);
  Type *OrigSourceTy = OrigSource->getType();
  Type *ExtendedSourceTy = Config.getExtendedFPType(OrigSourceTy);
  // When extending:
  //  - (A) If the source has a shadow, we extend from the shadow, else we
  //    extend from the original source.
  //  - (B) If the shadow of the dest is larger than the shadow of the source,
  //    we still need an extend. Else, the shadow of the source is the same
  //    type as the shadow of the dest (because mappings are non-decreasing), so
  //    we don't need to emit an extend.
  // Examples,
  //   with a mapping of {f32->f64;f64->f80;f80->f128}
  //     fpext half    %1 to float     ->  fpext half     %1    to double
  //     fpext half    %1 to double    ->  fpext half     %1    to x86_fp80
  //     fpext half    %1 to x86_fp80  ->  fpext half     %1    to fp128
  //     fpext float   %1 to double    ->  double s(%1)
  //     fpext float   %1 to x86_fp80  ->  fpext double   s(%1) to fp128
  //     fpext double  %1 to x86_fp80  ->  fpext x86_fp80 s(%1) to fp128
  //   with a mapping of {f32->f64;f64->f128;f80->f128}
  //     fpext half    %1 to float     ->  fpext half     %1    to double
  //     fpext half    %1 to double    ->  fpext half     %1    to fp128
  //     fpext half    %1 to x86_fp80  ->  fpext half     %1    to fp128
  //     fpext float   %1 to double    ->  fpext double   s(%1) to fp128
  //     fpext float   %1 to x86_fp80  ->  fpext double   s(%1) to fp128
  //     fpext double  %1 to x86_fp80  ->  fp128 s(%1)
  //   with a mapping of {f32->f32;f64->f32;f80->f64}
  //     fpext half    %1 to float     ->  fpext half     %1    to float
  //     fpext half    %1 to double    ->  fpext half     %1    to float
  //     fpext half    %1 to x86_fp80  ->  fpext half     %1    to double
  //     fpext float   %1 to double    ->  s(%1)
  //     fpext float   %1 to x86_fp80  ->  fpext float    s(%1) to double
  //     fpext double  %1 to x86_fp80  ->  fpext float    s(%1) to double

  // See (A) above.
  Value *Source = ExtendedSourceTy ? Map.getShadow(OrigSource) : OrigSource;
  Type *SourceTy = ExtendedSourceTy ? ExtendedSourceTy : OrigSourceTy;
  // See (B) above.
  if (SourceTy == ExtendedVT)
    return Source;

  return Builder.CreateFPExt(Source, ExtendedVT);
}

namespace {
// TODO: This should be tablegen-ed.
struct KnownIntrinsic {
  struct WidenedIntrinsic {
    const char *NarrowName;
    Intrinsic::ID ID; // wide id.
    using FnTypeFactory = FunctionType *(*)(LLVMContext &);
    FnTypeFactory MakeFnTy;
  };

  static const char *get(LibFunc LFunc);

  // Given an intrinsic with an `FT` argument, try to find a wider intrinsic
  // that applies the same operation on the shadow argument.
  // Options are:
  //  - pass in the ID and full function type,
  //  - pass in the name, which includes the function type through mangling.
  static const WidenedIntrinsic *widen(StringRef Name);

private:
  struct LFEntry {
    LibFunc LFunc;
    const char *IntrinsicName;
  };
  static const LFEntry kLibfuncIntrinsics[];

  static const WidenedIntrinsic kWidenedIntrinsics[];
};
} // namespace

static FunctionType *makeDoubleDouble(LLVMContext &C) {
  return FunctionType::get(Type::getDoubleTy(C), {Type::getDoubleTy(C)}, false);
}

static FunctionType *makeX86FP80X86FP80(LLVMContext &C) {
  return FunctionType::get(Type::getX86_FP80Ty(C), {Type::getX86_FP80Ty(C)},
                           false);
}

static FunctionType *makeDoubleDoubleI32(LLVMContext &C) {
  return FunctionType::get(Type::getDoubleTy(C),
                           {Type::getDoubleTy(C), Type::getInt32Ty(C)}, false);
}

static FunctionType *makeX86FP80X86FP80I32(LLVMContext &C) {
  return FunctionType::get(Type::getX86_FP80Ty(C),
                           {Type::getX86_FP80Ty(C), Type::getInt32Ty(C)},
                           false);
}

static FunctionType *makeDoubleDoubleDouble(LLVMContext &C) {
  return FunctionType::get(Type::getDoubleTy(C),
                           {Type::getDoubleTy(C), Type::getDoubleTy(C)}, false);
}

static FunctionType *makeX86FP80X86FP80X86FP80(LLVMContext &C) {
  return FunctionType::get(Type::getX86_FP80Ty(C),
                           {Type::getX86_FP80Ty(C), Type::getX86_FP80Ty(C)},
                           false);
}

static FunctionType *makeDoubleDoubleDoubleDouble(LLVMContext &C) {
  return FunctionType::get(
      Type::getDoubleTy(C),
      {Type::getDoubleTy(C), Type::getDoubleTy(C), Type::getDoubleTy(C)},
      false);
}

static FunctionType *makeX86FP80X86FP80X86FP80X86FP80(LLVMContext &C) {
  return FunctionType::get(
      Type::getX86_FP80Ty(C),
      {Type::getX86_FP80Ty(C), Type::getX86_FP80Ty(C), Type::getX86_FP80Ty(C)},
      false);
}

const KnownIntrinsic::WidenedIntrinsic KnownIntrinsic::kWidenedIntrinsics[] = {
    // TODO: Right now we ignore vector intrinsics.
    // This is hard because we have to model the semantics of the intrinsics,
    // e.g. llvm.x86.sse2.min.sd means extract first element, min, insert back.
    // Intrinsics that take any non-vector FT types:
    // NOTE: Right now because of
    // https://github.com/llvm/llvm-project/issues/44744
    // for f128 we need to use makeX86FP80X86FP80 (go to a lower precision and
    // come back).
    {"llvm.sqrt.f32", Intrinsic::sqrt, makeDoubleDouble},
    {"llvm.sqrt.f64", Intrinsic::sqrt, makeX86FP80X86FP80},
    {"llvm.sqrt.f80", Intrinsic::sqrt, makeX86FP80X86FP80},
    {"llvm.powi.f32", Intrinsic::powi, makeDoubleDoubleI32},
    {"llvm.powi.f64", Intrinsic::powi, makeX86FP80X86FP80I32},
    {"llvm.powi.f80", Intrinsic::powi, makeX86FP80X86FP80I32},
    {"llvm.sin.f32", Intrinsic::sin, makeDoubleDouble},
    {"llvm.sin.f64", Intrinsic::sin, makeX86FP80X86FP80},
    {"llvm.sin.f80", Intrinsic::sin, makeX86FP80X86FP80},
    {"llvm.cos.f32", Intrinsic::cos, makeDoubleDouble},
    {"llvm.cos.f64", Intrinsic::cos, makeX86FP80X86FP80},
    {"llvm.cos.f80", Intrinsic::cos, makeX86FP80X86FP80},
    {"llvm.pow.f32", Intrinsic::pow, makeDoubleDoubleDouble},
    {"llvm.pow.f64", Intrinsic::pow, makeX86FP80X86FP80X86FP80},
    {"llvm.pow.f80", Intrinsic::pow, makeX86FP80X86FP80X86FP80},
    {"llvm.exp.f32", Intrinsic::exp, makeDoubleDouble},
    {"llvm.exp.f64", Intrinsic::exp, makeX86FP80X86FP80},
    {"llvm.exp.f80", Intrinsic::exp, makeX86FP80X86FP80},
    {"llvm.exp2.f32", Intrinsic::exp2, makeDoubleDouble},
    {"llvm.exp2.f64", Intrinsic::exp2, makeX86FP80X86FP80},
    {"llvm.exp2.f80", Intrinsic::exp2, makeX86FP80X86FP80},
    {"llvm.log.f32", Intrinsic::log, makeDoubleDouble},
    {"llvm.log.f64", Intrinsic::log, makeX86FP80X86FP80},
    {"llvm.log.f80", Intrinsic::log, makeX86FP80X86FP80},
    {"llvm.log10.f32", Intrinsic::log10, makeDoubleDouble},
    {"llvm.log10.f64", Intrinsic::log10, makeX86FP80X86FP80},
    {"llvm.log10.f80", Intrinsic::log10, makeX86FP80X86FP80},
    {"llvm.log2.f32", Intrinsic::log2, makeDoubleDouble},
    {"llvm.log2.f64", Intrinsic::log2, makeX86FP80X86FP80},
    {"llvm.log2.f80", Intrinsic::log2, makeX86FP80X86FP80},
    {"llvm.fma.f32", Intrinsic::fma, makeDoubleDoubleDoubleDouble},

    {"llvm.fmuladd.f32", Intrinsic::fmuladd, makeDoubleDoubleDoubleDouble},

    {"llvm.fma.f64", Intrinsic::fma, makeX86FP80X86FP80X86FP80X86FP80},

    {"llvm.fmuladd.f64", Intrinsic::fma, makeX86FP80X86FP80X86FP80X86FP80},

    {"llvm.fma.f80", Intrinsic::fma, makeX86FP80X86FP80X86FP80X86FP80},
    {"llvm.fabs.f32", Intrinsic::fabs, makeDoubleDouble},
    {"llvm.fabs.f64", Intrinsic::fabs, makeX86FP80X86FP80},
    {"llvm.fabs.f80", Intrinsic::fabs, makeX86FP80X86FP80},
    {"llvm.minnum.f32", Intrinsic::minnum, makeDoubleDoubleDouble},
    {"llvm.minnum.f64", Intrinsic::minnum, makeX86FP80X86FP80X86FP80},
    {"llvm.minnum.f80", Intrinsic::minnum, makeX86FP80X86FP80X86FP80},
    {"llvm.maxnum.f32", Intrinsic::maxnum, makeDoubleDoubleDouble},
    {"llvm.maxnum.f64", Intrinsic::maxnum, makeX86FP80X86FP80X86FP80},
    {"llvm.maxnum.f80", Intrinsic::maxnum, makeX86FP80X86FP80X86FP80},
    {"llvm.minimum.f32", Intrinsic::minimum, makeDoubleDoubleDouble},
    {"llvm.minimum.f64", Intrinsic::minimum, makeX86FP80X86FP80X86FP80},
    {"llvm.minimum.f80", Intrinsic::minimum, makeX86FP80X86FP80X86FP80},
    {"llvm.maximum.f32", Intrinsic::maximum, makeDoubleDoubleDouble},
    {"llvm.maximum.f64", Intrinsic::maximum, makeX86FP80X86FP80X86FP80},
    {"llvm.maximum.f80", Intrinsic::maximum, makeX86FP80X86FP80X86FP80},
    {"llvm.copysign.f32", Intrinsic::copysign, makeDoubleDoubleDouble},
    {"llvm.copysign.f64", Intrinsic::copysign, makeX86FP80X86FP80X86FP80},
    {"llvm.copysign.f80", Intrinsic::copysign, makeX86FP80X86FP80X86FP80},
    {"llvm.floor.f32", Intrinsic::floor, makeDoubleDouble},
    {"llvm.floor.f64", Intrinsic::floor, makeX86FP80X86FP80},
    {"llvm.floor.f80", Intrinsic::floor, makeX86FP80X86FP80},
    {"llvm.ceil.f32", Intrinsic::ceil, makeDoubleDouble},
    {"llvm.ceil.f64", Intrinsic::ceil, makeX86FP80X86FP80},
    {"llvm.ceil.f80", Intrinsic::ceil, makeX86FP80X86FP80},
    {"llvm.trunc.f32", Intrinsic::trunc, makeDoubleDouble},
    {"llvm.trunc.f64", Intrinsic::trunc, makeX86FP80X86FP80},
    {"llvm.trunc.f80", Intrinsic::trunc, makeX86FP80X86FP80},
    {"llvm.rint.f32", Intrinsic::rint, makeDoubleDouble},
    {"llvm.rint.f64", Intrinsic::rint, makeX86FP80X86FP80},
    {"llvm.rint.f80", Intrinsic::rint, makeX86FP80X86FP80},
    {"llvm.nearbyint.f32", Intrinsic::nearbyint, makeDoubleDouble},
    {"llvm.nearbyint.f64", Intrinsic::nearbyint, makeX86FP80X86FP80},
    {"llvm.nearbyin80f64", Intrinsic::nearbyint, makeX86FP80X86FP80},
    {"llvm.round.f32", Intrinsic::round, makeDoubleDouble},
    {"llvm.round.f64", Intrinsic::round, makeX86FP80X86FP80},
    {"llvm.round.f80", Intrinsic::round, makeX86FP80X86FP80},
    {"llvm.lround.f32", Intrinsic::lround, makeDoubleDouble},
    {"llvm.lround.f64", Intrinsic::lround, makeX86FP80X86FP80},
    {"llvm.lround.f80", Intrinsic::lround, makeX86FP80X86FP80},
    {"llvm.llround.f32", Intrinsic::llround, makeDoubleDouble},
    {"llvm.llround.f64", Intrinsic::llround, makeX86FP80X86FP80},
    {"llvm.llround.f80", Intrinsic::llround, makeX86FP80X86FP80},
    {"llvm.lrint.f32", Intrinsic::lrint, makeDoubleDouble},
    {"llvm.lrint.f64", Intrinsic::lrint, makeX86FP80X86FP80},
    {"llvm.lrint.f80", Intrinsic::lrint, makeX86FP80X86FP80},
    {"llvm.llrint.f32", Intrinsic::llrint, makeDoubleDouble},
    {"llvm.llrint.f64", Intrinsic::llrint, makeX86FP80X86FP80},
    {"llvm.llrint.f80", Intrinsic::llrint, makeX86FP80X86FP80},
};

const KnownIntrinsic::LFEntry KnownIntrinsic::kLibfuncIntrinsics[] = {
    {LibFunc_sqrtf, "llvm.sqrt.f32"},
    {LibFunc_sqrt, "llvm.sqrt.f64"},
    {LibFunc_sqrtl, "llvm.sqrt.f80"},
    {LibFunc_sinf, "llvm.sin.f32"},
    {LibFunc_sin, "llvm.sin.f64"},
    {LibFunc_sinl, "llvm.sin.f80"},
    {LibFunc_cosf, "llvm.cos.f32"},
    {LibFunc_cos, "llvm.cos.f64"},
    {LibFunc_cosl, "llvm.cos.f80"},
    {LibFunc_powf, "llvm.pow.f32"},
    {LibFunc_pow, "llvm.pow.f64"},
    {LibFunc_powl, "llvm.pow.f80"},
    {LibFunc_expf, "llvm.exp.f32"},
    {LibFunc_exp, "llvm.exp.f64"},
    {LibFunc_expl, "llvm.exp.f80"},
    {LibFunc_exp2f, "llvm.exp2.f32"},
    {LibFunc_exp2, "llvm.exp2.f64"},
    {LibFunc_exp2l, "llvm.exp2.f80"},
    {LibFunc_logf, "llvm.log.f32"},
    {LibFunc_log, "llvm.log.f64"},
    {LibFunc_logl, "llvm.log.f80"},
    {LibFunc_log10f, "llvm.log10.f32"},
    {LibFunc_log10, "llvm.log10.f64"},
    {LibFunc_log10l, "llvm.log10.f80"},
    {LibFunc_log2f, "llvm.log2.f32"},
    {LibFunc_log2, "llvm.log2.f64"},
    {LibFunc_log2l, "llvm.log2.f80"},
    {LibFunc_fabsf, "llvm.fabs.f32"},
    {LibFunc_fabs, "llvm.fabs.f64"},
    {LibFunc_fabsl, "llvm.fabs.f80"},
    {LibFunc_copysignf, "llvm.copysign.f32"},
    {LibFunc_copysign, "llvm.copysign.f64"},
    {LibFunc_copysignl, "llvm.copysign.f80"},
    {LibFunc_floorf, "llvm.floor.f32"},
    {LibFunc_floor, "llvm.floor.f64"},
    {LibFunc_floorl, "llvm.floor.f80"},
    {LibFunc_fmaxf, "llvm.maxnum.f32"},
    {LibFunc_fmax, "llvm.maxnum.f64"},
    {LibFunc_fmaxl, "llvm.maxnum.f80"},
    {LibFunc_fminf, "llvm.minnum.f32"},
    {LibFunc_fmin, "llvm.minnum.f64"},
    {LibFunc_fminl, "llvm.minnum.f80"},
    {LibFunc_ceilf, "llvm.ceil.f32"},
    {LibFunc_ceil, "llvm.ceil.f64"},
    {LibFunc_ceill, "llvm.ceil.f80"},
    {LibFunc_truncf, "llvm.trunc.f32"},
    {LibFunc_trunc, "llvm.trunc.f64"},
    {LibFunc_truncl, "llvm.trunc.f80"},
    {LibFunc_rintf, "llvm.rint.f32"},
    {LibFunc_rint, "llvm.rint.f64"},
    {LibFunc_rintl, "llvm.rint.f80"},
    {LibFunc_nearbyintf, "llvm.nearbyint.f32"},
    {LibFunc_nearbyint, "llvm.nearbyint.f64"},
    {LibFunc_nearbyintl, "llvm.nearbyint.f80"},
    {LibFunc_roundf, "llvm.round.f32"},
    {LibFunc_round, "llvm.round.f64"},
    {LibFunc_roundl, "llvm.round.f80"},
};

const char *KnownIntrinsic::get(LibFunc LFunc) {
  for (const auto &E : kLibfuncIntrinsics) {
    if (E.LFunc == LFunc)
      return E.IntrinsicName;
  }
  return nullptr;
}

const KnownIntrinsic::WidenedIntrinsic *KnownIntrinsic::widen(StringRef Name) {
  for (const auto &E : kWidenedIntrinsics) {
    if (E.NarrowName == Name)
      return &E;
  }
  return nullptr;
}

// Returns the name of the LLVM intrinsic corresponding to the given function.
static const char *getIntrinsicFromLibfunc(Function &Fn, Type *VT,
                                           const TargetLibraryInfo &TLI) {
  LibFunc LFunc;
  if (!TLI.getLibFunc(Fn, LFunc))
    return nullptr;

  if (const char *Name = KnownIntrinsic::get(LFunc))
    return Name;

  LLVM_DEBUG(errs() << "TODO: LibFunc: " << TLI.getName(LFunc) << "\n");
  return nullptr;
}

// Try to handle a known function call.
Value *NumericalStabilitySanitizer::maybeHandleKnownCallBase(
    CallBase &Call, Type *VT, Type *ExtendedVT, const TargetLibraryInfo &TLI,
    const ValueToShadowMap &Map, IRBuilder<> &Builder) {
  Function *Fn = Call.getCalledFunction();
  if (Fn == nullptr)
    return nullptr;

  Intrinsic::ID WidenedId = Intrinsic::ID();
  FunctionType *WidenedFnTy = nullptr;
  if (const auto ID = Fn->getIntrinsicID()) {
    const auto *Widened = KnownIntrinsic::widen(Fn->getName());
    if (Widened) {
      WidenedId = Widened->ID;
      WidenedFnTy = Widened->MakeFnTy(Context);
    } else {
      // If we don't know how to widen the intrinsic, we have no choice but to
      // call the non-wide version on a truncated shadow and extend again
      // afterwards.
      WidenedId = ID;
      WidenedFnTy = Fn->getFunctionType();
    }
  } else if (const char *Name = getIntrinsicFromLibfunc(*Fn, VT, TLI)) {
    // We might have a call to a library function that we can replace with a
    // wider Intrinsic.
    const auto *Widened = KnownIntrinsic::widen(Name);
    assert(Widened && "make sure KnownIntrinsic entries are consistent");
    WidenedId = Widened->ID;
    WidenedFnTy = Widened->MakeFnTy(Context);
  } else {
    // This is not a known library function or intrinsic.
    return nullptr;
  }

  // Check that the widened intrinsic is valid.
  SmallVector<Intrinsic::IITDescriptor, 8> Table;
  getIntrinsicInfoTableEntries(WidenedId, Table);
  SmallVector<Type *, 4> ArgTys;
  ArrayRef<Intrinsic::IITDescriptor> TableRef = Table;
  [[maybe_unused]] Intrinsic::MatchIntrinsicTypesResult MatchResult =
      Intrinsic::matchIntrinsicSignature(WidenedFnTy, TableRef, ArgTys);
  assert(MatchResult == Intrinsic::MatchIntrinsicTypes_Match &&
         "invalid widened intrinsic");
  // For known intrinsic functions, we create a second call to the same
  // intrinsic with a different type.
  SmallVector<Value *, 4> Args;
  // The last operand is the intrinsic itself, skip it.
  for (unsigned I = 0, E = Call.getNumOperands() - 1; I < E; ++I) {
    Value *Arg = Call.getOperand(I);
    Type *OrigArgTy = Arg->getType();
    Type *IntrinsicArgTy = WidenedFnTy->getParamType(I);
    if (OrigArgTy == IntrinsicArgTy) {
      Args.push_back(Arg); // The arg is passed as is.
      continue;
    }
    Type *ShadowArgTy = Config.getExtendedFPType(Arg->getType());
    assert(ShadowArgTy &&
           "don't know how to get the shadow value for a non-FT");
    Value *Shadow = Map.getShadow(Arg);
    if (ShadowArgTy == IntrinsicArgTy) {
      // The shadow is the right type for the intrinsic.
      assert(Shadow->getType() == ShadowArgTy);
      Args.push_back(Shadow);
      continue;
    }
    // There is no intrinsic with his level of precision, truncate the shadow.
    Args.push_back(Builder.CreateFPTrunc(Shadow, IntrinsicArgTy));
  }
  Value *IntrinsicCall = Builder.CreateIntrinsic(WidenedId, ArgTys, Args);
  return WidenedFnTy->getReturnType() == ExtendedVT
             ? IntrinsicCall
             : Builder.CreateFPExt(IntrinsicCall, ExtendedVT);
}

// Handle a CallBase, i.e. a function call, an inline asm sequence, or an
// invoke.
Value *NumericalStabilitySanitizer::handleCallBase(CallBase &Call, Type *VT,
                                                   Type *ExtendedVT,
                                                   const TargetLibraryInfo &TLI,
                                                   const ValueToShadowMap &Map,
                                                   IRBuilder<> &Builder) {
  // We cannot look inside inline asm, just expand the result again.
  if (Call.isInlineAsm())
    return Builder.CreateFPExt(&Call, ExtendedVT);

  // Intrinsics and library functions (e.g. sin, exp) are handled
  // specifically, because we know their semantics and can do better than
  // blindly calling them (e.g. compute the sinus in the actual shadow domain).
  if (Value *V =
          maybeHandleKnownCallBase(Call, VT, ExtendedVT, TLI, Map, Builder))
    return V;

  // If the return tag matches that of the called function, read the extended
  // return value from the shadow ret ptr. Else, just extend the return value.
  Value *L =
      Builder.CreateLoad(IntptrTy, NsanShadowRetTag, /*isVolatile=*/false);
  Value *HasShadowRet = Builder.CreateICmpEQ(
      L, Builder.CreatePtrToInt(Call.getCalledOperand(), IntptrTy));

  Value *ShadowRetVal = Builder.CreateLoad(
      ExtendedVT,
      Builder.CreateConstGEP2_64(NsanShadowRetType, NsanShadowRetPtr, 0, 0),
      /*isVolatile=*/false);
  Value *Shadow = Builder.CreateSelect(HasShadowRet, ShadowRetVal,
                                       Builder.CreateFPExt(&Call, ExtendedVT));
  ++NumInstrumentedFTCalls;
  return Shadow;
}

// Creates a shadow value for the given FT value. At that point all operands are
// guaranteed to be available.
Value *NumericalStabilitySanitizer::createShadowValueWithOperandsAvailable(
    Instruction &Inst, const TargetLibraryInfo &TLI,
    const ValueToShadowMap &Map) {
  Type *VT = Inst.getType();
  Type *ExtendedVT = Config.getExtendedFPType(VT);
  assert(ExtendedVT != nullptr && "trying to create a shadow for a non-FT");

  if (auto *Load = dyn_cast<LoadInst>(&Inst))
    return handleLoad(*Load, VT, ExtendedVT);

  if (auto *Call = dyn_cast<CallInst>(&Inst)) {
    // Insert after the call.
    BasicBlock::iterator It(Inst);
    IRBuilder<> Builder(Call->getParent(), ++It);
    Builder.SetCurrentDebugLocation(Call->getDebugLoc());
    return handleCallBase(*Call, VT, ExtendedVT, TLI, Map, Builder);
  }

  if (auto *Invoke = dyn_cast<InvokeInst>(&Inst)) {
    // The Invoke terminates the basic block, create a new basic block in
    // between the successful invoke and the next block.
    BasicBlock *InvokeBB = Invoke->getParent();
    BasicBlock *NextBB = Invoke->getNormalDest();
    BasicBlock *NewBB =
        BasicBlock::Create(Context, "", NextBB->getParent(), NextBB);
    Inst.replaceSuccessorWith(NextBB, NewBB);

    IRBuilder<> Builder(NewBB);
    Builder.SetCurrentDebugLocation(Invoke->getDebugLoc());
    Value *Shadow = handleCallBase(*Invoke, VT, ExtendedVT, TLI, Map, Builder);
    Builder.CreateBr(NextBB);
    NewBB->replaceSuccessorsPhiUsesWith(InvokeBB, NewBB);
    return Shadow;
  }

  IRBuilder<> Builder(Inst.getNextNode());
  Builder.SetCurrentDebugLocation(Inst.getDebugLoc());

  if (auto *Trunc = dyn_cast<FPTruncInst>(&Inst))
    return handleTrunc(*Trunc, VT, ExtendedVT, Map, Builder);
  if (auto *Ext = dyn_cast<FPExtInst>(&Inst))
    return handleExt(*Ext, VT, ExtendedVT, Map, Builder);

  if (auto *UnaryOp = dyn_cast<UnaryOperator>(&Inst))
    return Builder.CreateUnOp(UnaryOp->getOpcode(),
                              Map.getShadow(UnaryOp->getOperand(0)));

  if (auto *BinOp = dyn_cast<BinaryOperator>(&Inst))
    return Builder.CreateBinOp(BinOp->getOpcode(),
                               Map.getShadow(BinOp->getOperand(0)),
                               Map.getShadow(BinOp->getOperand(1)));

  if (isa<UIToFPInst>(&Inst) || isa<SIToFPInst>(&Inst)) {
    auto *Cast = dyn_cast<CastInst>(&Inst);
    return Builder.CreateCast(Cast->getOpcode(), Cast->getOperand(0),
                              ExtendedVT);
  }

  if (auto *S = dyn_cast<SelectInst>(&Inst))
    return Builder.CreateSelect(S->getCondition(),
                                Map.getShadow(S->getTrueValue()),
                                Map.getShadow(S->getFalseValue()));

  if (auto *Extract = dyn_cast<ExtractElementInst>(&Inst))
    return Builder.CreateExtractElement(
        Map.getShadow(Extract->getVectorOperand()), Extract->getIndexOperand());

  if (auto *Insert = dyn_cast<InsertElementInst>(&Inst))
    return Builder.CreateInsertElement(Map.getShadow(Insert->getOperand(0)),
                                       Map.getShadow(Insert->getOperand(1)),
                                       Insert->getOperand(2));

  if (auto *Shuffle = dyn_cast<ShuffleVectorInst>(&Inst))
    return Builder.CreateShuffleVector(Map.getShadow(Shuffle->getOperand(0)),
                                       Map.getShadow(Shuffle->getOperand(1)),
                                       Shuffle->getShuffleMask());
  // TODO: We could make aggregate object first class citizens. For now we
  // just extend the extracted value.
  if (auto *Extract = dyn_cast<ExtractValueInst>(&Inst))
    return Builder.CreateFPExt(Extract, ExtendedVT);

  if (auto *BC = dyn_cast<BitCastInst>(&Inst))
    return Builder.CreateFPExt(BC, ExtendedVT);

  report_fatal_error("Unimplemented support for " +
                     Twine(Inst.getOpcodeName()));
}

// Creates a shadow value for an instruction that defines a value of FT type.
// FT operands that do not already have shadow values are created recursively.
// The DFS is guaranteed to not loop as phis and arguments already have
// shadows.
void NumericalStabilitySanitizer::maybeCreateShadowValue(
    Instruction &Root, const TargetLibraryInfo &TLI, ValueToShadowMap &Map) {
  Type *VT = Root.getType();
  Type *ExtendedVT = Config.getExtendedFPType(VT);
  if (ExtendedVT == nullptr)
    return; // Not an FT value.

  if (Map.hasShadow(&Root))
    return; // Shadow already exists.

  assert(!isa<PHINode>(Root) && "phi nodes should already have shadows");

  std::vector<Instruction *> DfsStack(1, &Root);
  while (!DfsStack.empty()) {
    // Ensure that all operands to the instruction have shadows before
    // proceeding.
    Instruction *I = DfsStack.back();
    // The shadow for the instruction might have been created deeper in the DFS,
    // see `forward_use_with_two_uses` test.
    if (Map.hasShadow(I)) {
      DfsStack.pop_back();
      continue;
    }

    bool MissingShadow = false;
    for (Value *Op : I->operands()) {
      Type *VT = Op->getType();
      if (!Config.getExtendedFPType(VT))
        continue; // Not an FT value.
      if (Map.hasShadow(Op))
        continue; // Shadow is already available.
      MissingShadow = true;
      DfsStack.push_back(cast<Instruction>(Op));
    }
    if (MissingShadow)
      continue; // Process operands and come back to this instruction later.

    // All operands have shadows. Create a shadow for the current value.
    Value *Shadow = createShadowValueWithOperandsAvailable(*I, TLI, Map);
    Map.setShadow(*I, *Shadow);
    DfsStack.pop_back();
  }
}

// A floating-point store needs its value and type written to shadow memory.
void NumericalStabilitySanitizer::propagateFTStore(
    StoreInst &Store, Type *VT, Type *ExtendedVT, const ValueToShadowMap &Map) {
  Value *StoredValue = Store.getValueOperand();
  IRBuilder<> Builder(&Store);
  Builder.SetCurrentDebugLocation(Store.getDebugLoc());
  const auto Extents = getMemoryExtentsOrDie(VT);
  Value *ShadowPtr = Builder.CreateCall(
      NsanGetShadowPtrForStore[Extents.ValueType],
      {Store.getPointerOperand(), ConstantInt::get(IntptrTy, Extents.NumElts)});

  Value *StoredShadow = Map.getShadow(StoredValue);
  if (!Store.getParent()->getParent()->hasOptNone()) {
    // Only check stores when optimizing, because non-optimized code generates
    // too many stores to the stack, creating false positives.
    if (ClCheckStores) {
      StoredShadow = emitCheck(StoredValue, StoredShadow, Builder,
                               CheckLoc::makeStore(Store.getPointerOperand()));
      ++NumInstrumentedFTStores;
    }
  }

  Builder.CreateAlignedStore(StoredShadow, ShadowPtr, Align(1),
                             Store.isVolatile());
}

// A non-ft store needs to invalidate shadow memory. Exceptions are:
//   - memory transfers of floating-point data through other pointer types (llvm
//     optimization passes transform `*(float*)a = *(float*)b` into
//     `*(i32*)a = *(i32*)b` ). These have the same semantics as memcpy.
//   - Writes of FT-sized constants. LLVM likes to do float stores as bitcasted
//     ints. Note that this is not really necessary because if the value is
//     unknown the framework will re-extend it on load anyway. It just felt
//     easier to debug tests with vectors of FTs.
void NumericalStabilitySanitizer::propagateNonFTStore(
    StoreInst &Store, Type *VT, const ValueToShadowMap &Map) {
  Value *PtrOp = Store.getPointerOperand();
  IRBuilder<> Builder(Store.getNextNode());
  Builder.SetCurrentDebugLocation(Store.getDebugLoc());
  Value *Dst = PtrOp;
  TypeSize SlotSize = DL.getTypeStoreSize(VT);
  assert(!SlotSize.isScalable() && "unsupported");
  const auto LoadSizeBytes = SlotSize.getFixedValue();
  Value *ValueSize = Constant::getIntegerValue(
      IntptrTy, APInt(IntptrTy->getPrimitiveSizeInBits(), LoadSizeBytes));

  ++NumInstrumentedNonFTStores;
  Value *StoredValue = Store.getValueOperand();
  if (LoadInst *Load = dyn_cast<LoadInst>(StoredValue)) {
    // TODO: Handle the case when the value is from a phi.
    // This is a memory transfer with memcpy semantics. Copy the type and
    // value from the source. Note that we cannot use __nsan_copy_values()
    // here, because that will not work when there is a write to memory in
    // between the load and the store, e.g. in the case of a swap.
    Type *ShadowTypeIntTy = Type::getIntNTy(Context, 8 * LoadSizeBytes);
    Type *ShadowValueIntTy =
        Type::getIntNTy(Context, 8 * kShadowScale * LoadSizeBytes);
    IRBuilder<> LoadBuilder(Load->getNextNode());
    Builder.SetCurrentDebugLocation(Store.getDebugLoc());
    Value *LoadSrc = Load->getPointerOperand();
    // Read the shadow type and value at load time. The type has the same size
    // as the FT value, the value has twice its size.
    // TODO: cache them to avoid re-creating them when a load is used by
    // several stores. Maybe create them like the FT shadows when a load is
    // encountered.
    Value *RawShadowType = LoadBuilder.CreateAlignedLoad(
        ShadowTypeIntTy,
        LoadBuilder.CreateCall(NsanGetRawShadowTypePtr, {LoadSrc}), Align(1),
        /*isVolatile=*/false);
    Value *RawShadowValue = LoadBuilder.CreateAlignedLoad(
        ShadowValueIntTy,
        LoadBuilder.CreateCall(NsanGetRawShadowPtr, {LoadSrc}), Align(1),
        /*isVolatile=*/false);

    // Write back the shadow type and value at store time.
    Builder.CreateAlignedStore(
        RawShadowType, Builder.CreateCall(NsanGetRawShadowTypePtr, {Dst}),
        Align(1),
        /*isVolatile=*/false);
    Builder.CreateAlignedStore(RawShadowValue,
                               Builder.CreateCall(NsanGetRawShadowPtr, {Dst}),
                               Align(1),
                               /*isVolatile=*/false);

    ++NumInstrumentedNonFTMemcpyStores;
    return;
  }
  // ClPropagateNonFTConstStoresAsFT is by default false.
  if (Constant *C; ClPropagateNonFTConstStoresAsFT &&
                   (C = dyn_cast<Constant>(StoredValue))) {
    // This might be a fp constant stored as an int. Bitcast and store if it has
    // appropriate size.
    Type *BitcastTy = nullptr; // The FT type to bitcast to.
    if (auto *CInt = dyn_cast<ConstantInt>(C)) {
      switch (CInt->getType()->getScalarSizeInBits()) {
      case 32:
        BitcastTy = Type::getFloatTy(Context);
        break;
      case 64:
        BitcastTy = Type::getDoubleTy(Context);
        break;
      case 80:
        BitcastTy = Type::getX86_FP80Ty(Context);
        break;
      default:
        break;
      }
    } else if (auto *CDV = dyn_cast<ConstantDataVector>(C)) {
      const int NumElements =
          cast<VectorType>(CDV->getType())->getElementCount().getFixedValue();
      switch (CDV->getType()->getScalarSizeInBits()) {
      case 32:
        BitcastTy =
            VectorType::get(Type::getFloatTy(Context), NumElements, false);
        break;
      case 64:
        BitcastTy =
            VectorType::get(Type::getDoubleTy(Context), NumElements, false);
        break;
      case 80:
        BitcastTy =
            VectorType::get(Type::getX86_FP80Ty(Context), NumElements, false);
        break;
      default:
        break;
      }
    }
    if (BitcastTy) {
      const MemoryExtents Extents = getMemoryExtentsOrDie(BitcastTy);
      Value *ShadowPtr = Builder.CreateCall(
          NsanGetShadowPtrForStore[Extents.ValueType],
          {PtrOp, ConstantInt::get(IntptrTy, Extents.NumElts)});
      // Bitcast the integer value to the appropriate FT type and extend to 2FT.
      Type *ExtVT = Config.getExtendedFPType(BitcastTy);
      Value *Shadow =
          Builder.CreateFPExt(Builder.CreateBitCast(C, BitcastTy), ExtVT);
      Builder.CreateAlignedStore(Shadow, ShadowPtr, Align(1),
                                 Store.isVolatile());
      return;
    }
  }
  // All other stores just reset the shadow value to unknown.
  Builder.CreateCall(NsanSetValueUnknown, {Dst, ValueSize});
}

void NumericalStabilitySanitizer::propagateShadowValues(
    Instruction &Inst, const TargetLibraryInfo &TLI,
    const ValueToShadowMap &Map) {
  if (auto *Store = dyn_cast<StoreInst>(&Inst)) {
    Value *StoredValue = Store->getValueOperand();
    Type *VT = StoredValue->getType();
    Type *ExtendedVT = Config.getExtendedFPType(VT);
    if (ExtendedVT == nullptr)
      return propagateNonFTStore(*Store, VT, Map);
    return propagateFTStore(*Store, VT, ExtendedVT, Map);
  }

  if (auto *FCmp = dyn_cast<FCmpInst>(&Inst)) {
    emitFCmpCheck(*FCmp, Map);
    return;
  }

  if (auto *CB = dyn_cast<CallBase>(&Inst)) {
    maybeAddSuffixForNsanInterface(CB);
    if (CallInst *CI = dyn_cast<CallInst>(&Inst))
      maybeMarkSanitizerLibraryCallNoBuiltin(CI, &TLI);
    if (MemIntrinsic *MI = dyn_cast<MemIntrinsic>(&Inst)) {
      instrumentMemIntrinsic(MI);
      return;
    }
    populateShadowStack(*CB, TLI, Map);
    return;
  }

  if (auto *RetInst = dyn_cast<ReturnInst>(&Inst)) {
    if (!ClCheckRet)
      return;

    Value *RV = RetInst->getReturnValue();
    if (RV == nullptr)
      return; // This is a `ret void`.
    Type *VT = RV->getType();
    Type *ExtendedVT = Config.getExtendedFPType(VT);
    if (ExtendedVT == nullptr)
      return; // Not an FT ret.
    Value *RVShadow = Map.getShadow(RV);
    IRBuilder<> Builder(RetInst);

    RVShadow = emitCheck(RV, RVShadow, Builder, CheckLoc::makeRet());
    ++NumInstrumentedFTRets;
    // Store tag.
    Value *FnAddr =
        Builder.CreatePtrToInt(Inst.getParent()->getParent(), IntptrTy);
    Builder.CreateStore(FnAddr, NsanShadowRetTag);
    // Store value.
    Value *ShadowRetValPtr =
        Builder.CreateConstGEP2_64(NsanShadowRetType, NsanShadowRetPtr, 0, 0);
    Builder.CreateStore(RVShadow, ShadowRetValPtr);
    return;
  }

  if (InsertValueInst *Insert = dyn_cast<InsertValueInst>(&Inst)) {
    Value *V = Insert->getOperand(1);
    Type *VT = V->getType();
    Type *ExtendedVT = Config.getExtendedFPType(VT);
    if (ExtendedVT == nullptr)
      return;
    IRBuilder<> Builder(Insert);
    emitCheck(V, Map.getShadow(V), Builder, CheckLoc::makeInsert());
    return;
  }
}

// Moves fast math flags from the function to individual instructions, and
// removes the attribute from the function.
// TODO: Make this controllable with a flag.
static void moveFastMathFlags(Function &F,
                              std::vector<Instruction *> &Instructions) {
  FastMathFlags FMF;
#define MOVE_FLAG(attr, setter)                                                \
  if (F.getFnAttribute(attr).getValueAsString() == "true") {                   \
    F.removeFnAttr(attr);                                                      \
    FMF.set##setter();                                                         \
  }
  MOVE_FLAG("unsafe-fp-math", Fast)
  MOVE_FLAG("no-infs-fp-math", NoInfs)
  MOVE_FLAG("no-nans-fp-math", NoNaNs)
  MOVE_FLAG("no-signed-zeros-fp-math", NoSignedZeros)
#undef MOVE_FLAG

  for (Instruction *I : Instructions)
    if (isa<FPMathOperator>(I))
      I->setFastMathFlags(FMF);
}

bool NumericalStabilitySanitizer::sanitizeFunction(
    Function &F, const TargetLibraryInfo &TLI) {
  if (!F.hasFnAttribute(Attribute::SanitizeNumericalStability))
    return false;

  // This is required to prevent instrumenting call to __nsan_init from within
  // the module constructor.
  if (F.getName() == kNsanModuleCtorName)
    return false;
  SmallVector<Instruction *, 8> AllLoadsAndStores;
  SmallVector<Instruction *, 8> LocalLoadsAndStores;

  // The instrumentation maintains:
  //  - for each IR value `v` of floating-point (or vector floating-point) type
  //    FT, a shadow IR value `s(v)` with twice the precision 2FT (e.g.
  //    double for float and f128 for double).
  //  - A shadow memory, which stores `s(v)` for any `v` that has been stored,
  //    along with a shadow memory tag, which stores whether the value in the
  //    corresponding shadow memory is valid. Note that this might be
  //    incorrect if a non-instrumented function stores to memory, or if
  //    memory is stored to through a char pointer.
  //  - A shadow stack, which holds `s(v)` for any floating-point argument `v`
  //    of a call to an instrumented function. This allows
  //    instrumented functions to retrieve the shadow values for their
  //    arguments.
  //    Because instrumented functions can be called from non-instrumented
  //    functions, the stack needs to include a tag so that the instrumented
  //    function knows whether shadow values are available for their
  //    parameters (i.e. whether is was called by an instrumented function).
  //    When shadow arguments are not available, they have to be recreated by
  //    extending the precision of the non-shadow arguments to the non-shadow
  //    value. Non-instrumented functions do not modify (or even know about) the
  //    shadow stack. The shadow stack pointer is __nsan_shadow_args. The shadow
  //    stack tag is __nsan_shadow_args_tag. The tag is any unique identifier
  //    for the function (we use the address of the function). Both variables
  //    are thread local.
  //    Example:
  //     calls                             shadow stack tag      shadow stack
  //     =======================================================================
  //     non_instrumented_1()              0                     0
  //             |
  //             v
  //     instrumented_2(float a)           0                     0
  //             |
  //             v
  //     instrumented_3(float b, double c) &instrumented_3       s(b),s(c)
  //             |
  //             v
  //     instrumented_4(float d)           &instrumented_4       s(d)
  //             |
  //             v
  //     non_instrumented_5(float e)       &non_instrumented_5   s(e)
  //             |
  //             v
  //     instrumented_6(float f)           &non_instrumented_5   s(e)
  //
  //   On entry, instrumented_2 checks whether the tag corresponds to its
  //   function ptr.
  //   Note that functions reset the tag to 0 after reading shadow parameters.
  //   This ensures that the function does not erroneously read invalid data if
  //   called twice in the same stack, once from an instrumented function and
  //   once from an uninstrumented one. For example, in the following example,
  //   resetting the tag in (A) ensures that (B) does not reuse the same the
  //   shadow arguments (which would be incorrect).
  //      instrumented_1(float a)
  //             |
  //             v
  //      instrumented_2(float b)  (A)
  //             |
  //             v
  //      non_instrumented_3()
  //             |
  //             v
  //      instrumented_2(float b)  (B)
  //
  //  - A shadow return slot. Any function that returns a floating-point value
  //    places a shadow return value in __nsan_shadow_ret_val. Again, because
  //    we might be calling non-instrumented functions, this value is guarded
  //    by __nsan_shadow_ret_tag marker indicating which instrumented function
  //    placed the value in __nsan_shadow_ret_val, so that the caller can check
  //    that this corresponds to the callee. Both variables are thread local.
  //
  //    For example, in the following example, the instrumentation in
  //    `instrumented_1` rejects the shadow return value from `instrumented_3`
  //    because is is not tagged as expected (`&instrumented_3` instead of
  //    `non_instrumented_2`):
  //
  //        instrumented_1()
  //            |
  //            v
  //        float non_instrumented_2()
  //            |
  //            v
  //        float instrumented_3()
  //
  // Calls of known math functions (sin, cos, exp, ...) are duplicated to call
  // their overload on the shadow type.

  // Collect all instructions before processing, as creating shadow values
  // creates new instructions inside the function.
  std::vector<Instruction *> OriginalInstructions;
  for (BasicBlock &BB : F)
    for (Instruction &Inst : BB)
      OriginalInstructions.emplace_back(&Inst);

  moveFastMathFlags(F, OriginalInstructions);
  ValueToShadowMap ValueToShadow(Config);

  // In the first pass, we create shadow values for all FT function arguments
  // and all phis. This ensures that the DFS of the next pass does not have
  // any loops.
  std::vector<PHINode *> OriginalPhis;
  createShadowArguments(F, TLI, ValueToShadow);
  for (Instruction *I : OriginalInstructions) {
    if (PHINode *Phi = dyn_cast<PHINode>(I)) {
      if (PHINode *Shadow = maybeCreateShadowPhi(*Phi, TLI)) {
        OriginalPhis.push_back(Phi);
        ValueToShadow.setShadow(*Phi, *Shadow);
      }
    }
  }

  // Create shadow values for all instructions creating FT values.
  for (Instruction *I : OriginalInstructions)
    maybeCreateShadowValue(*I, TLI, ValueToShadow);

  // Propagate shadow values across stores, calls and rets.
  for (Instruction *I : OriginalInstructions)
    propagateShadowValues(*I, TLI, ValueToShadow);

  // The last pass populates shadow phis with shadow values.
  for (PHINode *Phi : OriginalPhis) {
    PHINode *ShadowPhi = dyn_cast<PHINode>(ValueToShadow.getShadow(Phi));
    for (unsigned I : seq(Phi->getNumOperands())) {
      Value *V = Phi->getOperand(I);
      Value *Shadow = ValueToShadow.getShadow(V);
      BasicBlock *IncomingBB = Phi->getIncomingBlock(I);
      // For some instructions (e.g. invoke), we create the shadow in a separate
      // block, different from the block where the original value is created.
      // In that case, the shadow phi might need to refer to this block instead
      // of the original block.
      // Note that this can only happen for instructions as constant shadows are
      // always created in the same block.
      ShadowPhi->addIncoming(Shadow, IncomingBB);
    }
  }

  return !ValueToShadow.empty();
}

// Instrument the memory intrinsics so that they properly modify the shadow
// memory.
bool NumericalStabilitySanitizer::instrumentMemIntrinsic(MemIntrinsic *MI) {
  IRBuilder<> Builder(MI);
  if (auto *M = dyn_cast<MemSetInst>(MI)) {
    Builder.CreateCall(
        NsanSetValueUnknown,
        {/*Address=*/M->getArgOperand(0),
         /*Size=*/Builder.CreateIntCast(M->getArgOperand(2), IntptrTy, false)});
  } else if (auto *M = dyn_cast<MemTransferInst>(MI)) {
    Builder.CreateCall(
        NsanCopyValues,
        {/*Destination=*/M->getArgOperand(0),
         /*Source=*/M->getArgOperand(1),
         /*Size=*/Builder.CreateIntCast(M->getArgOperand(2), IntptrTy, false)});
  }
  return false;
}

void NumericalStabilitySanitizer::maybeAddSuffixForNsanInterface(CallBase *CI) {
  Function *Fn = CI->getCalledFunction();
  if (Fn == nullptr)
    return;

  if (!Fn->getName().starts_with("__nsan_"))
    return;

  if (Fn->getName() == "__nsan_dump_shadow_mem") {
    assert(CI->arg_size() == 4 &&
           "invalid prototype for __nsan_dump_shadow_mem");
    // __nsan_dump_shadow_mem requires an extra parameter with the dynamic
    // configuration:
    // (shadow_type_id_for_long_double << 16) | (shadow_type_id_for_double << 8)
    // | shadow_type_id_for_double
    const uint64_t shadow_value_type_ids =
        (static_cast<size_t>(Config.byValueType(kLongDouble).getNsanTypeId())
         << 16) |
        (static_cast<size_t>(Config.byValueType(kDouble).getNsanTypeId())
         << 8) |
        static_cast<size_t>(Config.byValueType(kFloat).getNsanTypeId());
    CI->setArgOperand(3, ConstantInt::get(IntptrTy, shadow_value_type_ids));
  }
}
