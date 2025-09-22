//===- SanitizerBinaryMetadata.cpp - binary analysis sanitizers metadata --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of SanitizerBinaryMetadata.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Instrumentation/SanitizerBinaryMetadata.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/CaptureTracking.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/SpecialCaseList.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

#include <array>
#include <cstdint>
#include <memory>

using namespace llvm;

#define DEBUG_TYPE "sanmd"

namespace {

//===--- Constants --------------------------------------------------------===//

constexpr uint32_t kVersionBase = 2;                // occupies lower 16 bits
constexpr uint32_t kVersionPtrSizeRel = (1u << 16); // offsets are pointer-sized
constexpr int kCtorDtorPriority = 2;

// Pairs of names of initialization callback functions and which section
// contains the relevant metadata.
class MetadataInfo {
public:
  const StringRef FunctionPrefix;
  const StringRef SectionSuffix;

  static const MetadataInfo Covered;
  static const MetadataInfo Atomics;

private:
  // Forbid construction elsewhere.
  explicit constexpr MetadataInfo(StringRef FunctionPrefix,
                                  StringRef SectionSuffix)
      : FunctionPrefix(FunctionPrefix), SectionSuffix(SectionSuffix) {}
};
const MetadataInfo MetadataInfo::Covered{
    "__sanitizer_metadata_covered", kSanitizerBinaryMetadataCoveredSection};
const MetadataInfo MetadataInfo::Atomics{
    "__sanitizer_metadata_atomics", kSanitizerBinaryMetadataAtomicsSection};

// The only instances of MetadataInfo are the constants above, so a set of
// them may simply store pointers to them. To deterministically generate code,
// we need to use a set with stable iteration order, such as SetVector.
using MetadataInfoSet = SetVector<const MetadataInfo *>;

//===--- Command-line options ---------------------------------------------===//

cl::opt<bool> ClWeakCallbacks(
    "sanitizer-metadata-weak-callbacks",
    cl::desc("Declare callbacks extern weak, and only call if non-null."),
    cl::Hidden, cl::init(true));
cl::opt<bool>
    ClNoSanitize("sanitizer-metadata-nosanitize-attr",
                 cl::desc("Mark some metadata features uncovered in functions "
                          "with associated no_sanitize attributes."),
                 cl::Hidden, cl::init(true));

cl::opt<bool> ClEmitCovered("sanitizer-metadata-covered",
                            cl::desc("Emit PCs for covered functions."),
                            cl::Hidden, cl::init(false));
cl::opt<bool> ClEmitAtomics("sanitizer-metadata-atomics",
                            cl::desc("Emit PCs for atomic operations."),
                            cl::Hidden, cl::init(false));
cl::opt<bool> ClEmitUAR("sanitizer-metadata-uar",
                        cl::desc("Emit PCs for start of functions that are "
                                 "subject for use-after-return checking"),
                        cl::Hidden, cl::init(false));

//===--- Statistics -------------------------------------------------------===//

STATISTIC(NumMetadataCovered, "Metadata attached to covered functions");
STATISTIC(NumMetadataAtomics, "Metadata attached to atomics");
STATISTIC(NumMetadataUAR, "Metadata attached to UAR functions");

//===----------------------------------------------------------------------===//

// Apply opt overrides.
SanitizerBinaryMetadataOptions &&
transformOptionsFromCl(SanitizerBinaryMetadataOptions &&Opts) {
  Opts.Covered |= ClEmitCovered;
  Opts.Atomics |= ClEmitAtomics;
  Opts.UAR |= ClEmitUAR;
  return std::move(Opts);
}

class SanitizerBinaryMetadata {
public:
  SanitizerBinaryMetadata(Module &M, SanitizerBinaryMetadataOptions Opts,
                          std::unique_ptr<SpecialCaseList> Ignorelist)
      : Mod(M), Options(transformOptionsFromCl(std::move(Opts))),
        Ignorelist(std::move(Ignorelist)), TargetTriple(M.getTargetTriple()),
        VersionStr(utostr(getVersion())), IRB(M.getContext()) {
    // FIXME: Make it work with other formats.
    assert(TargetTriple.isOSBinFormatELF() && "ELF only");
    assert(!(TargetTriple.isNVPTX() || TargetTriple.isAMDGPU()) &&
           "Device targets are not supported");
  }

  bool run();

private:
  uint32_t getVersion() const {
    uint32_t Version = kVersionBase;
    const auto CM = Mod.getCodeModel();
    if (CM.has_value() && (*CM == CodeModel::Medium || *CM == CodeModel::Large))
      Version |= kVersionPtrSizeRel;
    return Version;
  }

  void runOn(Function &F, MetadataInfoSet &MIS);

  // Determines which set of metadata to collect for this instruction.
  //
  // Returns true if covered metadata is required to unambiguously interpret
  // other metadata. For example, if we are interested in atomics metadata, any
  // function with memory operations (atomic or not) requires covered metadata
  // to determine if a memory operation is atomic or not in modules compiled
  // with SanitizerBinaryMetadata.
  bool runOn(Instruction &I, MetadataInfoSet &MIS, MDBuilder &MDB,
             uint64_t &FeatureMask);

  // Get start/end section marker pointer.
  GlobalVariable *getSectionMarker(const Twine &MarkerName, Type *Ty);

  // Returns the target-dependent section name.
  StringRef getSectionName(StringRef SectionSuffix);

  // Returns the section start marker name.
  StringRef getSectionStart(StringRef SectionSuffix);

  // Returns the section end marker name.
  StringRef getSectionEnd(StringRef SectionSuffix);

  // Returns true if the access to the address should be considered "atomic".
  bool pretendAtomicAccess(const Value *Addr);

  Module &Mod;
  const SanitizerBinaryMetadataOptions Options;
  std::unique_ptr<SpecialCaseList> Ignorelist;
  const Triple TargetTriple;
  const std::string VersionStr;
  IRBuilder<> IRB;
  BumpPtrAllocator Alloc;
  UniqueStringSaver StringPool{Alloc};
};

bool SanitizerBinaryMetadata::run() {
  MetadataInfoSet MIS;

  for (Function &F : Mod)
    runOn(F, MIS);

  if (MIS.empty())
    return false;

  //
  // Setup constructors and call all initialization functions for requested
  // metadata features.
  //

  auto *PtrTy = IRB.getPtrTy();
  auto *Int32Ty = IRB.getInt32Ty();
  const std::array<Type *, 3> InitTypes = {Int32Ty, PtrTy, PtrTy};
  auto *Version = ConstantInt::get(Int32Ty, getVersion());

  for (const MetadataInfo *MI : MIS) {
    const std::array<Value *, InitTypes.size()> InitArgs = {
        Version,
        getSectionMarker(getSectionStart(MI->SectionSuffix), PtrTy),
        getSectionMarker(getSectionEnd(MI->SectionSuffix), PtrTy),
    };

    // Calls to the initialization functions with different versions cannot be
    // merged. Give the structors unique names based on the version, which will
    // also be used as the COMDAT key.
    const std::string StructorPrefix = (MI->FunctionPrefix + VersionStr).str();

    // We declare the _add and _del functions as weak, and only call them if
    // there is a valid symbol linked. This allows building binaries with
    // semantic metadata, but without having callbacks. When a tool that wants
    // the metadata is linked which provides the callbacks, they will be called.
    Function *Ctor =
        createSanitizerCtorAndInitFunctions(
            Mod, StructorPrefix + ".module_ctor",
            (MI->FunctionPrefix + "_add").str(), InitTypes, InitArgs,
            /*VersionCheckName=*/StringRef(), /*Weak=*/ClWeakCallbacks)
            .first;
    Function *Dtor =
        createSanitizerCtorAndInitFunctions(
            Mod, StructorPrefix + ".module_dtor",
            (MI->FunctionPrefix + "_del").str(), InitTypes, InitArgs,
            /*VersionCheckName=*/StringRef(), /*Weak=*/ClWeakCallbacks)
            .first;
    Constant *CtorComdatKey = nullptr;
    Constant *DtorComdatKey = nullptr;
    if (TargetTriple.supportsCOMDAT()) {
      // Use COMDAT to deduplicate constructor/destructor function. The COMDAT
      // key needs to be a non-local linkage.
      Ctor->setComdat(Mod.getOrInsertComdat(Ctor->getName()));
      Dtor->setComdat(Mod.getOrInsertComdat(Dtor->getName()));
      Ctor->setLinkage(GlobalValue::ExternalLinkage);
      Dtor->setLinkage(GlobalValue::ExternalLinkage);
      // DSOs should _not_ call another constructor/destructor!
      Ctor->setVisibility(GlobalValue::HiddenVisibility);
      Dtor->setVisibility(GlobalValue::HiddenVisibility);
      CtorComdatKey = Ctor;
      DtorComdatKey = Dtor;
    }
    appendToGlobalCtors(Mod, Ctor, kCtorDtorPriority, CtorComdatKey);
    appendToGlobalDtors(Mod, Dtor, kCtorDtorPriority, DtorComdatKey);
  }

  return true;
}

void SanitizerBinaryMetadata::runOn(Function &F, MetadataInfoSet &MIS) {
  if (F.empty())
    return;
  if (F.hasFnAttribute(Attribute::DisableSanitizerInstrumentation))
    return;
  if (Ignorelist && Ignorelist->inSection("metadata", "fun", F.getName()))
    return;
  // Don't touch available_externally functions, their actual body is elsewhere.
  if (F.getLinkage() == GlobalValue::AvailableExternallyLinkage)
    return;

  MDBuilder MDB(F.getContext());

  // The metadata features enabled for this function, stored along covered
  // metadata (if enabled).
  uint64_t FeatureMask = 0;
  // Don't emit unnecessary covered metadata for all functions to save space.
  bool RequiresCovered = false;

  if (Options.Atomics || Options.UAR) {
    for (BasicBlock &BB : F)
      for (Instruction &I : BB)
        RequiresCovered |= runOn(I, MIS, MDB, FeatureMask);
  }

  if (ClNoSanitize && F.hasFnAttribute("no_sanitize_thread"))
    FeatureMask &= ~kSanitizerBinaryMetadataAtomics;
  if (F.isVarArg())
    FeatureMask &= ~kSanitizerBinaryMetadataUAR;
  if (FeatureMask & kSanitizerBinaryMetadataUAR) {
    RequiresCovered = true;
    NumMetadataUAR++;
  }

  // Covered metadata is always emitted if explicitly requested, otherwise only
  // if some other metadata requires it to unambiguously interpret it for
  // modules compiled with SanitizerBinaryMetadata.
  if (Options.Covered || (FeatureMask && RequiresCovered)) {
    NumMetadataCovered++;
    const auto *MI = &MetadataInfo::Covered;
    MIS.insert(MI);
    const StringRef Section = getSectionName(MI->SectionSuffix);
    // The feature mask will be placed after the function size.
    Constant *CFM = IRB.getInt64(FeatureMask);
    F.setMetadata(LLVMContext::MD_pcsections,
                  MDB.createPCSections({{Section, {CFM}}}));
  }
}

bool isUARSafeCall(CallInst *CI) {
  auto *F = CI->getCalledFunction();
  // There are no intrinsic functions that leak arguments.
  // If the called function does not return, the current function
  // does not return as well, so no possibility of use-after-return.
  // Sanitizer function also don't leak or don't return.
  // It's safe to both pass pointers to local variables to them
  // and to tail-call them.
  return F && (F->isIntrinsic() || F->doesNotReturn() ||
               F->getName().starts_with("__asan_") ||
               F->getName().starts_with("__hwsan_") ||
               F->getName().starts_with("__ubsan_") ||
               F->getName().starts_with("__msan_") ||
               F->getName().starts_with("__tsan_"));
}

bool hasUseAfterReturnUnsafeUses(Value &V) {
  for (User *U : V.users()) {
    if (auto *I = dyn_cast<Instruction>(U)) {
      if (I->isLifetimeStartOrEnd() || I->isDroppable())
        continue;
      if (auto *CI = dyn_cast<CallInst>(U)) {
        if (isUARSafeCall(CI))
          continue;
      }
      if (isa<LoadInst>(U))
        continue;
      if (auto *SI = dyn_cast<StoreInst>(U)) {
        // If storing TO the alloca, then the address isn't taken.
        if (SI->getOperand(1) == &V)
          continue;
      }
      if (auto *GEPI = dyn_cast<GetElementPtrInst>(U)) {
        if (!hasUseAfterReturnUnsafeUses(*GEPI))
          continue;
      } else if (auto *BCI = dyn_cast<BitCastInst>(U)) {
        if (!hasUseAfterReturnUnsafeUses(*BCI))
          continue;
      }
    }
    return true;
  }
  return false;
}

bool useAfterReturnUnsafe(Instruction &I) {
  if (isa<AllocaInst>(I))
    return hasUseAfterReturnUnsafeUses(I);
  // Tail-called functions are not necessary intercepted
  // at runtime because there is no call instruction.
  // So conservatively mark the caller as requiring checking.
  else if (auto *CI = dyn_cast<CallInst>(&I))
    return CI->isTailCall() && !isUARSafeCall(CI);
  return false;
}

bool SanitizerBinaryMetadata::pretendAtomicAccess(const Value *Addr) {
  if (!Addr)
    return false;

  Addr = Addr->stripInBoundsOffsets();
  auto *GV = dyn_cast<GlobalVariable>(Addr);
  if (!GV)
    return false;

  // Some compiler-generated accesses are known racy, to avoid false positives
  // in data-race analysis pretend they're atomic.
  if (GV->hasSection()) {
    const auto OF = Triple(Mod.getTargetTriple()).getObjectFormat();
    const auto ProfSec =
        getInstrProfSectionName(IPSK_cnts, OF, /*AddSegmentInfo=*/false);
    if (GV->getSection().ends_with(ProfSec))
      return true;
  }
  if (GV->getName().starts_with("__llvm_gcov") ||
      GV->getName().starts_with("__llvm_gcda"))
    return true;

  return false;
}

// Returns true if the memory at `Addr` may be shared with other threads.
bool maybeSharedMutable(const Value *Addr) {
  // By default assume memory may be shared.
  if (!Addr)
    return true;

  if (isa<AllocaInst>(getUnderlyingObject(Addr)) &&
      !PointerMayBeCaptured(Addr, true, true))
    return false; // Object is on stack but does not escape.

  Addr = Addr->stripInBoundsOffsets();
  if (auto *GV = dyn_cast<GlobalVariable>(Addr)) {
    if (GV->isConstant())
      return false; // Shared, but not mutable.
  }

  return true;
}

bool SanitizerBinaryMetadata::runOn(Instruction &I, MetadataInfoSet &MIS,
                                    MDBuilder &MDB, uint64_t &FeatureMask) {
  SmallVector<const MetadataInfo *, 1> InstMetadata;
  bool RequiresCovered = false;

  // Only call if at least 1 type of metadata is requested.
  assert(Options.UAR || Options.Atomics);

  if (Options.UAR && !(FeatureMask & kSanitizerBinaryMetadataUAR)) {
    if (useAfterReturnUnsafe(I))
      FeatureMask |= kSanitizerBinaryMetadataUAR;
  }

  if (Options.Atomics) {
    const Value *Addr = nullptr;
    if (auto *SI = dyn_cast<StoreInst>(&I))
      Addr = SI->getPointerOperand();
    else if (auto *LI = dyn_cast<LoadInst>(&I))
      Addr = LI->getPointerOperand();

    if (I.mayReadOrWriteMemory() && maybeSharedMutable(Addr)) {
      auto SSID = getAtomicSyncScopeID(&I);
      if ((SSID.has_value() && *SSID != SyncScope::SingleThread) ||
          pretendAtomicAccess(Addr)) {
        NumMetadataAtomics++;
        InstMetadata.push_back(&MetadataInfo::Atomics);
      }
      FeatureMask |= kSanitizerBinaryMetadataAtomics;
      RequiresCovered = true;
    }
  }

  // Attach MD_pcsections to instruction.
  if (!InstMetadata.empty()) {
    MIS.insert(InstMetadata.begin(), InstMetadata.end());
    SmallVector<MDBuilder::PCSection, 1> Sections;
    for (const auto &MI : InstMetadata)
      Sections.push_back({getSectionName(MI->SectionSuffix), {}});
    I.setMetadata(LLVMContext::MD_pcsections, MDB.createPCSections(Sections));
  }

  return RequiresCovered;
}

GlobalVariable *
SanitizerBinaryMetadata::getSectionMarker(const Twine &MarkerName, Type *Ty) {
  // Use ExternalWeak so that if all sections are discarded due to section
  // garbage collection, the linker will not report undefined symbol errors.
  auto *Marker = new GlobalVariable(Mod, Ty, /*isConstant=*/false,
                                    GlobalVariable::ExternalWeakLinkage,
                                    /*Initializer=*/nullptr, MarkerName);
  Marker->setVisibility(GlobalValue::HiddenVisibility);
  return Marker;
}

StringRef SanitizerBinaryMetadata::getSectionName(StringRef SectionSuffix) {
  // FIXME: Other TargetTriples.
  // Request ULEB128 encoding for all integer constants.
  return StringPool.save(SectionSuffix + VersionStr + "!C");
}

StringRef SanitizerBinaryMetadata::getSectionStart(StringRef SectionSuffix) {
  // Twine only concatenates 2 strings; with >2 strings, concatenating them
  // creates Twine temporaries, and returning the final Twine no longer works
  // because we'd end up with a stack-use-after-return. So here we also use the
  // StringPool to store the new string.
  return StringPool.save("__start_" + SectionSuffix + VersionStr);
}

StringRef SanitizerBinaryMetadata::getSectionEnd(StringRef SectionSuffix) {
  return StringPool.save("__stop_" + SectionSuffix + VersionStr);
}

} // namespace

SanitizerBinaryMetadataPass::SanitizerBinaryMetadataPass(
    SanitizerBinaryMetadataOptions Opts, ArrayRef<std::string> IgnorelistFiles)
    : Options(std::move(Opts)), IgnorelistFiles(std::move(IgnorelistFiles)) {}

PreservedAnalyses
SanitizerBinaryMetadataPass::run(Module &M, AnalysisManager<Module> &AM) {
  std::unique_ptr<SpecialCaseList> Ignorelist;
  if (!IgnorelistFiles.empty()) {
    Ignorelist = SpecialCaseList::createOrDie(IgnorelistFiles,
                                              *vfs::getRealFileSystem());
    if (Ignorelist->inSection("metadata", "src", M.getSourceFileName()))
      return PreservedAnalyses::all();
  }

  SanitizerBinaryMetadata Pass(M, Options, std::move(Ignorelist));
  if (Pass.run())
    return PreservedAnalyses::none();
  return PreservedAnalyses::all();
}
