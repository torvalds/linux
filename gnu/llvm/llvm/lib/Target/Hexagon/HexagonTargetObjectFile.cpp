//===-- HexagonTargetObjectFile.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations of the HexagonTargetAsmInfo properties.
//
//===----------------------------------------------------------------------===//

#include "HexagonTargetObjectFile.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalObject.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/SectionKind.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"

#define DEBUG_TYPE "hexagon-sdata"

using namespace llvm;

static cl::opt<unsigned> SmallDataThreshold("hexagon-small-data-threshold",
  cl::init(8), cl::Hidden,
  cl::desc("The maximum size of an object in the sdata section"));

static cl::opt<bool> NoSmallDataSorting("mno-sort-sda", cl::init(false),
  cl::Hidden, cl::desc("Disable small data sections sorting"));

static cl::opt<bool>
    StaticsInSData("hexagon-statics-in-small-data", cl::Hidden,
                   cl::desc("Allow static variables in .sdata"));

static cl::opt<bool> TraceGVPlacement("trace-gv-placement",
  cl::Hidden, cl::init(false),
  cl::desc("Trace global value placement"));

static cl::opt<bool>
    EmitJtInText("hexagon-emit-jt-text", cl::Hidden, cl::init(false),
                 cl::desc("Emit hexagon jump tables in function section"));

static cl::opt<bool>
    EmitLutInText("hexagon-emit-lut-text", cl::Hidden, cl::init(false),
                 cl::desc("Emit hexagon lookup tables in function section"));

// TraceGVPlacement controls messages for all builds. For builds with assertions
// (debug or release), messages are also controlled by the usual debug flags
// (e.g. -debug and -debug-only=globallayout)
#define TRACE_TO(s, X) s << X
#ifdef NDEBUG
#define TRACE(X)                                                               \
  do {                                                                         \
    if (TraceGVPlacement) {                                                    \
      TRACE_TO(errs(), X);                                                     \
    }                                                                          \
  } while (false)
#else
#define TRACE(X)                                                               \
  do {                                                                         \
    if (TraceGVPlacement) {                                                    \
      TRACE_TO(errs(), X);                                                     \
    } else {                                                                   \
      LLVM_DEBUG(TRACE_TO(dbgs(), X));                                         \
    }                                                                          \
  } while (false)
#endif

// Returns true if the section name is such that the symbol will be put
// in a small data section.
// For instance, global variables with section attributes such as ".sdata"
// ".sdata.*", ".sbss", and ".sbss.*" will go into small data.
static bool isSmallDataSection(StringRef Sec) {
  // sectionName is either ".sdata" or ".sbss". Looking for an exact match
  // obviates the need for checks for section names such as ".sdatafoo".
  if (Sec == ".sdata" || Sec == ".sbss" || Sec == ".scommon")
    return true;
  // If either ".sdata." or ".sbss." is a substring of the section name
  // then put the symbol in small data.
  return Sec.contains(".sdata.") || Sec.contains(".sbss.") ||
         Sec.contains(".scommon.");
}

static const char *getSectionSuffixForSize(unsigned Size) {
  switch (Size) {
  default:
    return "";
  case 1:
    return ".1";
  case 2:
    return ".2";
  case 4:
    return ".4";
  case 8:
    return ".8";
  }
}

void HexagonTargetObjectFile::Initialize(MCContext &Ctx,
      const TargetMachine &TM) {
  TargetLoweringObjectFileELF::Initialize(Ctx, TM);

  SmallDataSection =
    getContext().getELFSection(".sdata", ELF::SHT_PROGBITS,
                               ELF::SHF_WRITE | ELF::SHF_ALLOC |
                               ELF::SHF_HEX_GPREL);
  SmallBSSSection =
    getContext().getELFSection(".sbss", ELF::SHT_NOBITS,
                               ELF::SHF_WRITE | ELF::SHF_ALLOC |
                               ELF::SHF_HEX_GPREL);
}

MCSection *HexagonTargetObjectFile::SelectSectionForGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {
  TRACE("[SelectSectionForGlobal] GO(" << GO->getName() << ") ");
  TRACE("input section(" << GO->getSection() << ") ");

  TRACE((GO->hasPrivateLinkage() ? "private_linkage " : "")
         << (GO->hasLocalLinkage() ? "local_linkage " : "")
         << (GO->hasInternalLinkage() ? "internal " : "")
         << (GO->hasExternalLinkage() ? "external " : "")
         << (GO->hasCommonLinkage() ? "common_linkage " : "")
         << (GO->hasCommonLinkage() ? "common " : "" )
         << (Kind.isCommon() ? "kind_common " : "" )
         << (Kind.isBSS() ? "kind_bss " : "" )
         << (Kind.isBSSLocal() ? "kind_bss_local " : "" ));

  // If the lookup table is used by more than one function, do not place
  // it in text section.
  if (EmitLutInText && GO->getName().starts_with("switch.table")) {
    if (const Function *Fn = getLutUsedFunction(GO))
      return selectSectionForLookupTable(GO, TM, Fn);
  }

  if (isGlobalInSmallSection(GO, TM))
    return selectSmallSectionForGlobal(GO, Kind, TM);

  if (Kind.isCommon()) {
    // This is purely for LTO+Linker Script because commons don't really have a
    // section. However, the BitcodeSectionWriter pass will query for the
    // sections of commons (and the linker expects us to know their section) so
    // we'll return one here.
    return BSSSection;
  }

  TRACE("default_ELF_section\n");
  // Otherwise, we work the same as ELF.
  return TargetLoweringObjectFileELF::SelectSectionForGlobal(GO, Kind, TM);
}

MCSection *HexagonTargetObjectFile::getExplicitSectionGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {
  TRACE("[getExplicitSectionGlobal] GO(" << GO->getName() << ") from("
        << GO->getSection() << ") ");
  TRACE((GO->hasPrivateLinkage() ? "private_linkage " : "")
         << (GO->hasLocalLinkage() ? "local_linkage " : "")
         << (GO->hasInternalLinkage() ? "internal " : "")
         << (GO->hasExternalLinkage() ? "external " : "")
         << (GO->hasCommonLinkage() ? "common_linkage " : "")
         << (GO->hasCommonLinkage() ? "common " : "" )
         << (Kind.isCommon() ? "kind_common " : "" )
         << (Kind.isBSS() ? "kind_bss " : "" )
         << (Kind.isBSSLocal() ? "kind_bss_local " : "" ));

  if (GO->hasSection()) {
    StringRef Section = GO->getSection();
    if (Section.contains(".access.text.group"))
      return getContext().getELFSection(GO->getSection(), ELF::SHT_PROGBITS,
                                        ELF::SHF_ALLOC | ELF::SHF_EXECINSTR);
    if (Section.contains(".access.data.group"))
      return getContext().getELFSection(GO->getSection(), ELF::SHT_PROGBITS,
                                        ELF::SHF_WRITE | ELF::SHF_ALLOC);
  }

  if (isGlobalInSmallSection(GO, TM))
    return selectSmallSectionForGlobal(GO, Kind, TM);

  // Otherwise, we work the same as ELF.
  TRACE("default_ELF_section\n");
  return TargetLoweringObjectFileELF::getExplicitSectionGlobal(GO, Kind, TM);
}

/// Return true if this global value should be placed into small data/bss
/// section.
bool HexagonTargetObjectFile::isGlobalInSmallSection(const GlobalObject *GO,
      const TargetMachine &TM) const {
  bool HaveSData = isSmallDataEnabled(TM);
  if (!HaveSData)
    LLVM_DEBUG(dbgs() << "Small-data allocation is disabled, but symbols "
                         "may have explicit section assignments...\n");
  // Only global variables, not functions.
  LLVM_DEBUG(dbgs() << "Checking if value is in small-data, -G"
                    << SmallDataThreshold << ": \"" << GO->getName() << "\": ");
  const GlobalVariable *GVar = dyn_cast<GlobalVariable>(GO);
  if (!GVar) {
    LLVM_DEBUG(dbgs() << "no, not a global variable\n");
    return false;
  }

  // Globals with external linkage that have an original section set must be
  // emitted to that section, regardless of whether we would put them into
  // small data or not. This is how we can support mixing -G0/-G8 in LTO.
  if (GVar->hasSection()) {
    bool IsSmall = isSmallDataSection(GVar->getSection());
    LLVM_DEBUG(dbgs() << (IsSmall ? "yes" : "no")
                      << ", has section: " << GVar->getSection() << '\n');
    return IsSmall;
  }

  // If sdata is disabled, stop the checks here.
  if (!HaveSData) {
    LLVM_DEBUG(dbgs() << "no, small-data allocation is disabled\n");
    return false;
  }

  if (GVar->isConstant()) {
    LLVM_DEBUG(dbgs() << "no, is a constant\n");
    return false;
  }

  bool IsLocal = GVar->hasLocalLinkage();
  if (!StaticsInSData && IsLocal) {
    LLVM_DEBUG(dbgs() << "no, is static\n");
    return false;
  }

  Type *GType = GVar->getValueType();
  if (isa<ArrayType>(GType)) {
    LLVM_DEBUG(dbgs() << "no, is an array\n");
    return false;
  }

  // If the type is a struct with no body provided, treat is conservatively.
  // There cannot be actual definitions of object of such a type in this CU
  // (only references), so assuming that they are not in sdata is safe. If
  // these objects end up in the sdata, the references will still be valid.
  if (StructType *ST = dyn_cast<StructType>(GType)) {
    if (ST->isOpaque()) {
      LLVM_DEBUG(dbgs() << "no, has opaque type\n");
      return false;
    }
  }

  unsigned Size = GVar->getDataLayout().getTypeAllocSize(GType);
  if (Size == 0) {
    LLVM_DEBUG(dbgs() << "no, has size 0\n");
    return false;
  }
  if (Size > SmallDataThreshold) {
    LLVM_DEBUG(dbgs() << "no, size exceeds sdata threshold: " << Size << '\n');
    return false;
  }

  LLVM_DEBUG(dbgs() << "yes\n");
  return true;
}

bool HexagonTargetObjectFile::isSmallDataEnabled(const TargetMachine &TM)
    const {
  return SmallDataThreshold > 0 && !TM.isPositionIndependent();
}

unsigned HexagonTargetObjectFile::getSmallDataSize() const {
  return SmallDataThreshold;
}

bool HexagonTargetObjectFile::shouldPutJumpTableInFunctionSection(
    bool UsesLabelDifference, const Function &F) const {
  return EmitJtInText;
}

/// Descends any type down to "elementary" components,
/// discovering the smallest addressable one.
/// If zero is returned, declaration will not be modified.
unsigned HexagonTargetObjectFile::getSmallestAddressableSize(const Type *Ty,
      const GlobalValue *GV, const TargetMachine &TM) const {
  // Assign the smallest element access size to the highest
  // value which assembler can handle.
  unsigned SmallestElement = 8;

  if (!Ty)
    return 0;
  switch (Ty->getTypeID()) {
  case Type::StructTyID: {
    const StructType *STy = cast<const StructType>(Ty);
    for (auto &E : STy->elements()) {
      unsigned AtomicSize = getSmallestAddressableSize(E, GV, TM);
      if (AtomicSize < SmallestElement)
        SmallestElement = AtomicSize;
    }
    return (STy->getNumElements() == 0) ? 0 : SmallestElement;
  }
  case Type::ArrayTyID: {
    const ArrayType *ATy = cast<const ArrayType>(Ty);
    return getSmallestAddressableSize(ATy->getElementType(), GV, TM);
  }
  case Type::FixedVectorTyID:
  case Type::ScalableVectorTyID: {
    const VectorType *PTy = cast<const VectorType>(Ty);
    return getSmallestAddressableSize(PTy->getElementType(), GV, TM);
  }
  case Type::PointerTyID:
  case Type::HalfTyID:
  case Type::FloatTyID:
  case Type::DoubleTyID:
  case Type::IntegerTyID: {
    const DataLayout &DL = GV->getDataLayout();
    // It is unfortunate that DL's function take non-const Type*.
    return DL.getTypeAllocSize(const_cast<Type*>(Ty));
  }
  case Type::FunctionTyID:
  case Type::VoidTyID:
  case Type::BFloatTyID:
  case Type::X86_FP80TyID:
  case Type::FP128TyID:
  case Type::PPC_FP128TyID:
  case Type::LabelTyID:
  case Type::MetadataTyID:
  case Type::X86_MMXTyID:
  case Type::X86_AMXTyID:
  case Type::TokenTyID:
  case Type::TypedPointerTyID:
  case Type::TargetExtTyID:
    return 0;
  }

  return 0;
}

MCSection *HexagonTargetObjectFile::selectSmallSectionForGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {
  const Type *GTy = GO->getValueType();
  unsigned Size = getSmallestAddressableSize(GTy, GO, TM);

  // If we have -ffunction-section or -fdata-section then we should emit the
  // global value to a unique section specifically for it... even for sdata.
  bool EmitUniquedSection = TM.getDataSections();

  TRACE("Small data. Size(" << Size << ")");
  // Handle Small Section classification here.
  if (Kind.isBSS() || Kind.isBSSLocal()) {
    // If -mno-sort-sda is not set, find out smallest accessible entity in
    // declaration and add it to the section name string.
    // Note. It does not track the actual usage of the value, only its de-
    // claration. Also, compiler adds explicit pad fields to some struct
    // declarations - they are currently counted towards smallest addres-
    // sable entity.
    if (NoSmallDataSorting) {
      TRACE(" default sbss\n");
      return SmallBSSSection;
    }

    StringRef Prefix(".sbss");
    SmallString<128> Name(Prefix);
    Name.append(getSectionSuffixForSize(Size));

    if (EmitUniquedSection) {
      Name.append(".");
      Name.append(GO->getName());
    }
    TRACE(" unique sbss(" << Name << ")\n");
    return getContext().getELFSection(Name.str(), ELF::SHT_NOBITS,
                ELF::SHF_WRITE | ELF::SHF_ALLOC | ELF::SHF_HEX_GPREL);
  }

  if (Kind.isCommon()) {
    // This is purely for LTO+Linker Script because commons don't really have a
    // section. However, the BitcodeSectionWriter pass will query for the
    // sections of commons (and the linker expects us to know their section) so
    // we'll return one here.
    if (NoSmallDataSorting)
      return BSSSection;

    Twine Name = Twine(".scommon") + getSectionSuffixForSize(Size);
    TRACE(" small COMMON (" << Name << ")\n");

    return getContext().getELFSection(Name.str(), ELF::SHT_NOBITS,
                                      ELF::SHF_WRITE | ELF::SHF_ALLOC |
                                      ELF::SHF_HEX_GPREL);
  }

  // We could have changed sdata object to a constant... in this
  // case the Kind could be wrong for it.
  if (Kind.isMergeableConst()) {
    TRACE(" const_object_as_data ");
    const GlobalVariable *GVar = dyn_cast<GlobalVariable>(GO);
    if (GVar->hasSection() && isSmallDataSection(GVar->getSection()))
      Kind = SectionKind::getData();
  }

  if (Kind.isData()) {
    if (NoSmallDataSorting) {
      TRACE(" default sdata\n");
      return SmallDataSection;
    }

    StringRef Prefix(".sdata");
    SmallString<128> Name(Prefix);
    Name.append(getSectionSuffixForSize(Size));

    if (EmitUniquedSection) {
      Name.append(".");
      Name.append(GO->getName());
    }
    TRACE(" unique sdata(" << Name << ")\n");
    return getContext().getELFSection(Name.str(), ELF::SHT_PROGBITS,
                ELF::SHF_WRITE | ELF::SHF_ALLOC | ELF::SHF_HEX_GPREL);
  }

  TRACE("default ELF section\n");
  // Otherwise, we work the same as ELF.
  return TargetLoweringObjectFileELF::SelectSectionForGlobal(GO, Kind, TM);
}

// Return the function that uses the lookup table. If there are more
// than one live function that uses this look table, bail out and place
// the lookup table in default section.
const Function *
HexagonTargetObjectFile::getLutUsedFunction(const GlobalObject *GO) const {
  const Function *ReturnFn = nullptr;
  for (const auto *U : GO->users()) {
    // validate each instance of user to be a live function.
    auto *I = dyn_cast<Instruction>(U);
    if (!I)
      continue;
    auto *Bb = I->getParent();
    if (!Bb)
      continue;
    auto *UserFn = Bb->getParent();
    if (!ReturnFn)
      ReturnFn = UserFn;
    else if (ReturnFn != UserFn)
      return nullptr;
  }
  return ReturnFn;
}

MCSection *HexagonTargetObjectFile::selectSectionForLookupTable(
    const GlobalObject *GO, const TargetMachine &TM, const Function *Fn) const {

  SectionKind Kind = SectionKind::getText();
  // If the function has explicit section, place the lookup table in this
  // explicit section.
  if (Fn->hasSection())
    return getExplicitSectionGlobal(Fn, Kind, TM);

  const auto *FuncObj = dyn_cast<GlobalObject>(Fn);
  return SelectSectionForGlobal(FuncObj, Kind, TM);
}
