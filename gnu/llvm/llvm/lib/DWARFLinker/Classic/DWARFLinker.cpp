//=== DWARFLinker.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DWARFLinker/Classic/DWARFLinker.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/CodeGen/NonRelocatableStringpool.h"
#include "llvm/DWARFLinker/Classic/DWARFLinkerDeclContext.h"
#include "llvm/DWARFLinker/Classic/DWARFStreamer.h"
#include "llvm/DWARFLinker/Utils.h"
#include "llvm/DebugInfo/DWARF/DWARFAbbreviationDeclaration.h"
#include "llvm/DebugInfo/DWARF/DWARFAcceleratorTable.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDataExtractor.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugLine.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugMacro.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugRangeList.h"
#include "llvm/DebugInfo/DWARF/DWARFDie.h"
#include "llvm/DebugInfo/DWARF/DWARFExpression.h"
#include "llvm/DebugInfo/DWARF/DWARFFormValue.h"
#include "llvm/DebugInfo/DWARF/DWARFSection.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/ThreadPool.h"
#include <vector>

namespace llvm {

using namespace dwarf_linker;
using namespace dwarf_linker::classic;

/// Hold the input and output of the debug info size in bytes.
struct DebugInfoSize {
  uint64_t Input;
  uint64_t Output;
};

/// Compute the total size of the debug info.
static uint64_t getDebugInfoSize(DWARFContext &Dwarf) {
  uint64_t Size = 0;
  for (auto &Unit : Dwarf.compile_units()) {
    Size += Unit->getLength();
  }
  return Size;
}

/// Similar to DWARFUnitSection::getUnitForOffset(), but returning our
/// CompileUnit object instead.
static CompileUnit *getUnitForOffset(const UnitListTy &Units, uint64_t Offset) {
  auto CU = llvm::upper_bound(
      Units, Offset, [](uint64_t LHS, const std::unique_ptr<CompileUnit> &RHS) {
        return LHS < RHS->getOrigUnit().getNextUnitOffset();
      });
  return CU != Units.end() ? CU->get() : nullptr;
}

/// Resolve the DIE attribute reference that has been extracted in \p RefValue.
/// The resulting DIE might be in another CompileUnit which is stored into \p
/// ReferencedCU. \returns null if resolving fails for any reason.
DWARFDie DWARFLinker::resolveDIEReference(const DWARFFile &File,
                                          const UnitListTy &Units,
                                          const DWARFFormValue &RefValue,
                                          const DWARFDie &DIE,
                                          CompileUnit *&RefCU) {
  assert(RefValue.isFormClass(DWARFFormValue::FC_Reference));
  uint64_t RefOffset;
  if (std::optional<uint64_t> Off = RefValue.getAsRelativeReference()) {
    RefOffset = RefValue.getUnit()->getOffset() + *Off;
  } else if (Off = RefValue.getAsDebugInfoReference(); Off) {
    RefOffset = *Off;
  } else {
    reportWarning("Unsupported reference type", File, &DIE);
    return DWARFDie();
  }
  if ((RefCU = getUnitForOffset(Units, RefOffset)))
    if (const auto RefDie = RefCU->getOrigUnit().getDIEForOffset(RefOffset)) {
      // In a file with broken references, an attribute might point to a NULL
      // DIE.
      if (!RefDie.isNULL())
        return RefDie;
    }

  reportWarning("could not find referenced DIE", File, &DIE);
  return DWARFDie();
}

/// \returns whether the passed \a Attr type might contain a DIE reference
/// suitable for ODR uniquing.
static bool isODRAttribute(uint16_t Attr) {
  switch (Attr) {
  default:
    return false;
  case dwarf::DW_AT_type:
  case dwarf::DW_AT_containing_type:
  case dwarf::DW_AT_specification:
  case dwarf::DW_AT_abstract_origin:
  case dwarf::DW_AT_import:
    return true;
  }
  llvm_unreachable("Improper attribute.");
}

static bool isTypeTag(uint16_t Tag) {
  switch (Tag) {
  case dwarf::DW_TAG_array_type:
  case dwarf::DW_TAG_class_type:
  case dwarf::DW_TAG_enumeration_type:
  case dwarf::DW_TAG_pointer_type:
  case dwarf::DW_TAG_reference_type:
  case dwarf::DW_TAG_string_type:
  case dwarf::DW_TAG_structure_type:
  case dwarf::DW_TAG_subroutine_type:
  case dwarf::DW_TAG_template_alias:
  case dwarf::DW_TAG_typedef:
  case dwarf::DW_TAG_union_type:
  case dwarf::DW_TAG_ptr_to_member_type:
  case dwarf::DW_TAG_set_type:
  case dwarf::DW_TAG_subrange_type:
  case dwarf::DW_TAG_base_type:
  case dwarf::DW_TAG_const_type:
  case dwarf::DW_TAG_constant:
  case dwarf::DW_TAG_file_type:
  case dwarf::DW_TAG_namelist:
  case dwarf::DW_TAG_packed_type:
  case dwarf::DW_TAG_volatile_type:
  case dwarf::DW_TAG_restrict_type:
  case dwarf::DW_TAG_atomic_type:
  case dwarf::DW_TAG_interface_type:
  case dwarf::DW_TAG_unspecified_type:
  case dwarf::DW_TAG_shared_type:
  case dwarf::DW_TAG_immutable_type:
    return true;
  default:
    break;
  }
  return false;
}

bool DWARFLinker::DIECloner::getDIENames(const DWARFDie &Die,
                                         AttributesInfo &Info,
                                         OffsetsStringPool &StringPool,
                                         bool StripTemplate) {
  // This function will be called on DIEs having low_pcs and
  // ranges. As getting the name might be more expansive, filter out
  // blocks directly.
  if (Die.getTag() == dwarf::DW_TAG_lexical_block)
    return false;

  if (!Info.MangledName)
    if (const char *MangledName = Die.getLinkageName())
      Info.MangledName = StringPool.getEntry(MangledName);

  if (!Info.Name)
    if (const char *Name = Die.getShortName())
      Info.Name = StringPool.getEntry(Name);

  if (!Info.MangledName)
    Info.MangledName = Info.Name;

  if (StripTemplate && Info.Name && Info.MangledName != Info.Name) {
    StringRef Name = Info.Name.getString();
    if (std::optional<StringRef> StrippedName = StripTemplateParameters(Name))
      Info.NameWithoutTemplate = StringPool.getEntry(*StrippedName);
  }

  return Info.Name || Info.MangledName;
}

/// Resolve the relative path to a build artifact referenced by DWARF by
/// applying DW_AT_comp_dir.
static void resolveRelativeObjectPath(SmallVectorImpl<char> &Buf, DWARFDie CU) {
  sys::path::append(Buf, dwarf::toString(CU.find(dwarf::DW_AT_comp_dir), ""));
}

/// Collect references to parseable Swift interfaces in imported
/// DW_TAG_module blocks.
static void analyzeImportedModule(
    const DWARFDie &DIE, CompileUnit &CU,
    DWARFLinkerBase::SwiftInterfacesMapTy *ParseableSwiftInterfaces,
    std::function<void(const Twine &, const DWARFDie &)> ReportWarning) {
  if (CU.getLanguage() != dwarf::DW_LANG_Swift)
    return;

  if (!ParseableSwiftInterfaces)
    return;

  StringRef Path = dwarf::toStringRef(DIE.find(dwarf::DW_AT_LLVM_include_path));
  if (!Path.ends_with(".swiftinterface"))
    return;
  // Don't track interfaces that are part of the SDK.
  StringRef SysRoot = dwarf::toStringRef(DIE.find(dwarf::DW_AT_LLVM_sysroot));
  if (SysRoot.empty())
    SysRoot = CU.getSysRoot();
  if (!SysRoot.empty() && Path.starts_with(SysRoot))
    return;
  // Don't track interfaces that are part of the toolchain.
  // For example: Swift, _Concurrency, ...
  StringRef DeveloperDir = guessDeveloperDir(SysRoot);
  if (!DeveloperDir.empty() && Path.starts_with(DeveloperDir))
    return;
  if (isInToolchainDir(Path))
    return;
  std::optional<const char *> Name =
      dwarf::toString(DIE.find(dwarf::DW_AT_name));
  if (!Name)
    return;
  auto &Entry = (*ParseableSwiftInterfaces)[*Name];
  // The prepend path is applied later when copying.
  DWARFDie CUDie = CU.getOrigUnit().getUnitDIE();
  SmallString<128> ResolvedPath;
  if (sys::path::is_relative(Path))
    resolveRelativeObjectPath(ResolvedPath, CUDie);
  sys::path::append(ResolvedPath, Path);
  if (!Entry.empty() && Entry != ResolvedPath)
    ReportWarning(Twine("Conflicting parseable interfaces for Swift Module ") +
                      *Name + ": " + Entry + " and " + Path,
                  DIE);
  Entry = std::string(ResolvedPath);
}

/// The distinct types of work performed by the work loop in
/// analyzeContextInfo.
enum class ContextWorklistItemType : uint8_t {
  AnalyzeContextInfo,
  UpdateChildPruning,
  UpdatePruning,
};

/// This class represents an item in the work list. The type defines what kind
/// of work needs to be performed when processing the current item. Everything
/// but the Type and Die fields are optional based on the type.
struct ContextWorklistItem {
  DWARFDie Die;
  unsigned ParentIdx;
  union {
    CompileUnit::DIEInfo *OtherInfo;
    DeclContext *Context;
  };
  ContextWorklistItemType Type;
  bool InImportedModule;

  ContextWorklistItem(DWARFDie Die, ContextWorklistItemType T,
                      CompileUnit::DIEInfo *OtherInfo = nullptr)
      : Die(Die), ParentIdx(0), OtherInfo(OtherInfo), Type(T),
        InImportedModule(false) {}

  ContextWorklistItem(DWARFDie Die, DeclContext *Context, unsigned ParentIdx,
                      bool InImportedModule)
      : Die(Die), ParentIdx(ParentIdx), Context(Context),
        Type(ContextWorklistItemType::AnalyzeContextInfo),
        InImportedModule(InImportedModule) {}
};

static bool updatePruning(const DWARFDie &Die, CompileUnit &CU,
                          uint64_t ModulesEndOffset) {
  CompileUnit::DIEInfo &Info = CU.getInfo(Die);

  // Prune this DIE if it is either a forward declaration inside a
  // DW_TAG_module or a DW_TAG_module that contains nothing but
  // forward declarations.
  Info.Prune &= (Die.getTag() == dwarf::DW_TAG_module) ||
                (isTypeTag(Die.getTag()) &&
                 dwarf::toUnsigned(Die.find(dwarf::DW_AT_declaration), 0));

  // Only prune forward declarations inside a DW_TAG_module for which a
  // definition exists elsewhere.
  if (ModulesEndOffset == 0)
    Info.Prune &= Info.Ctxt && Info.Ctxt->getCanonicalDIEOffset();
  else
    Info.Prune &= Info.Ctxt && Info.Ctxt->getCanonicalDIEOffset() > 0 &&
                  Info.Ctxt->getCanonicalDIEOffset() <= ModulesEndOffset;

  return Info.Prune;
}

static void updateChildPruning(const DWARFDie &Die, CompileUnit &CU,
                               CompileUnit::DIEInfo &ChildInfo) {
  CompileUnit::DIEInfo &Info = CU.getInfo(Die);
  Info.Prune &= ChildInfo.Prune;
}

/// Recursive helper to build the global DeclContext information and
/// gather the child->parent relationships in the original compile unit.
///
/// This function uses the same work list approach as lookForDIEsToKeep.
///
/// \return true when this DIE and all of its children are only
/// forward declarations to types defined in external clang modules
/// (i.e., forward declarations that are children of a DW_TAG_module).
static void analyzeContextInfo(
    const DWARFDie &DIE, unsigned ParentIdx, CompileUnit &CU,
    DeclContext *CurrentDeclContext, DeclContextTree &Contexts,
    uint64_t ModulesEndOffset,
    DWARFLinkerBase::SwiftInterfacesMapTy *ParseableSwiftInterfaces,
    std::function<void(const Twine &, const DWARFDie &)> ReportWarning) {
  // LIFO work list.
  std::vector<ContextWorklistItem> Worklist;
  Worklist.emplace_back(DIE, CurrentDeclContext, ParentIdx, false);

  while (!Worklist.empty()) {
    ContextWorklistItem Current = Worklist.back();
    Worklist.pop_back();

    switch (Current.Type) {
    case ContextWorklistItemType::UpdatePruning:
      updatePruning(Current.Die, CU, ModulesEndOffset);
      continue;
    case ContextWorklistItemType::UpdateChildPruning:
      updateChildPruning(Current.Die, CU, *Current.OtherInfo);
      continue;
    case ContextWorklistItemType::AnalyzeContextInfo:
      break;
    }

    unsigned Idx = CU.getOrigUnit().getDIEIndex(Current.Die);
    CompileUnit::DIEInfo &Info = CU.getInfo(Idx);

    // Clang imposes an ODR on modules(!) regardless of the language:
    //  "The module-id should consist of only a single identifier,
    //   which provides the name of the module being defined. Each
    //   module shall have a single definition."
    //
    // This does not extend to the types inside the modules:
    //  "[I]n C, this implies that if two structs are defined in
    //   different submodules with the same name, those two types are
    //   distinct types (but may be compatible types if their
    //   definitions match)."
    //
    // We treat non-C++ modules like namespaces for this reason.
    if (Current.Die.getTag() == dwarf::DW_TAG_module &&
        Current.ParentIdx == 0 &&
        dwarf::toString(Current.Die.find(dwarf::DW_AT_name), "") !=
            CU.getClangModuleName()) {
      Current.InImportedModule = true;
      analyzeImportedModule(Current.Die, CU, ParseableSwiftInterfaces,
                            ReportWarning);
    }

    Info.ParentIdx = Current.ParentIdx;
    Info.InModuleScope = CU.isClangModule() || Current.InImportedModule;
    if (CU.hasODR() || Info.InModuleScope) {
      if (Current.Context) {
        auto PtrInvalidPair = Contexts.getChildDeclContext(
            *Current.Context, Current.Die, CU, Info.InModuleScope);
        Current.Context = PtrInvalidPair.getPointer();
        Info.Ctxt =
            PtrInvalidPair.getInt() ? nullptr : PtrInvalidPair.getPointer();
        if (Info.Ctxt)
          Info.Ctxt->setDefinedInClangModule(Info.InModuleScope);
      } else
        Info.Ctxt = Current.Context = nullptr;
    }

    Info.Prune = Current.InImportedModule;
    // Add children in reverse order to the worklist to effectively process
    // them in order.
    Worklist.emplace_back(Current.Die, ContextWorklistItemType::UpdatePruning);
    for (auto Child : reverse(Current.Die.children())) {
      CompileUnit::DIEInfo &ChildInfo = CU.getInfo(Child);
      Worklist.emplace_back(
          Current.Die, ContextWorklistItemType::UpdateChildPruning, &ChildInfo);
      Worklist.emplace_back(Child, Current.Context, Idx,
                            Current.InImportedModule);
    }
  }
}

static bool dieNeedsChildrenToBeMeaningful(uint32_t Tag) {
  switch (Tag) {
  default:
    return false;
  case dwarf::DW_TAG_class_type:
  case dwarf::DW_TAG_common_block:
  case dwarf::DW_TAG_lexical_block:
  case dwarf::DW_TAG_structure_type:
  case dwarf::DW_TAG_subprogram:
  case dwarf::DW_TAG_subroutine_type:
  case dwarf::DW_TAG_union_type:
    return true;
  }
  llvm_unreachable("Invalid Tag");
}

void DWARFLinker::cleanupAuxiliarryData(LinkContext &Context) {
  Context.clear();

  for (DIEBlock *I : DIEBlocks)
    I->~DIEBlock();
  for (DIELoc *I : DIELocs)
    I->~DIELoc();

  DIEBlocks.clear();
  DIELocs.clear();
  DIEAlloc.Reset();
}

static bool isTlsAddressCode(uint8_t DW_OP_Code) {
  return DW_OP_Code == dwarf::DW_OP_form_tls_address ||
         DW_OP_Code == dwarf::DW_OP_GNU_push_tls_address;
}

std::pair<bool, std::optional<int64_t>>
DWARFLinker::getVariableRelocAdjustment(AddressesMap &RelocMgr,
                                        const DWARFDie &DIE) {
  assert((DIE.getTag() == dwarf::DW_TAG_variable ||
          DIE.getTag() == dwarf::DW_TAG_constant) &&
         "Wrong type of input die");

  const auto *Abbrev = DIE.getAbbreviationDeclarationPtr();

  // Check if DIE has DW_AT_location attribute.
  DWARFUnit *U = DIE.getDwarfUnit();
  std::optional<uint32_t> LocationIdx =
      Abbrev->findAttributeIndex(dwarf::DW_AT_location);
  if (!LocationIdx)
    return std::make_pair(false, std::nullopt);

  // Get offset to the DW_AT_location attribute.
  uint64_t AttrOffset =
      Abbrev->getAttributeOffsetFromIndex(*LocationIdx, DIE.getOffset(), *U);

  // Get value of the DW_AT_location attribute.
  std::optional<DWARFFormValue> LocationValue =
      Abbrev->getAttributeValueFromOffset(*LocationIdx, AttrOffset, *U);
  if (!LocationValue)
    return std::make_pair(false, std::nullopt);

  // Check that DW_AT_location attribute is of 'exprloc' class.
  // Handling value of location expressions for attributes of 'loclist'
  // class is not implemented yet.
  std::optional<ArrayRef<uint8_t>> Expr = LocationValue->getAsBlock();
  if (!Expr)
    return std::make_pair(false, std::nullopt);

  // Parse 'exprloc' expression.
  DataExtractor Data(toStringRef(*Expr), U->getContext().isLittleEndian(),
                     U->getAddressByteSize());
  DWARFExpression Expression(Data, U->getAddressByteSize(),
                             U->getFormParams().Format);

  bool HasLocationAddress = false;
  uint64_t CurExprOffset = 0;
  for (DWARFExpression::iterator It = Expression.begin();
       It != Expression.end(); ++It) {
    DWARFExpression::iterator NextIt = It;
    ++NextIt;

    const DWARFExpression::Operation &Op = *It;
    switch (Op.getCode()) {
    case dwarf::DW_OP_const2u:
    case dwarf::DW_OP_const4u:
    case dwarf::DW_OP_const8u:
    case dwarf::DW_OP_const2s:
    case dwarf::DW_OP_const4s:
    case dwarf::DW_OP_const8s:
      if (NextIt == Expression.end() || !isTlsAddressCode(NextIt->getCode()))
        break;
      [[fallthrough]];
    case dwarf::DW_OP_addr: {
      HasLocationAddress = true;
      // Check relocation for the address.
      if (std::optional<int64_t> RelocAdjustment =
              RelocMgr.getExprOpAddressRelocAdjustment(
                  *U, Op, AttrOffset + CurExprOffset,
                  AttrOffset + Op.getEndOffset(), Options.Verbose))
        return std::make_pair(HasLocationAddress, *RelocAdjustment);
    } break;
    case dwarf::DW_OP_constx:
    case dwarf::DW_OP_addrx: {
      HasLocationAddress = true;
      if (std::optional<uint64_t> AddressOffset =
              DIE.getDwarfUnit()->getIndexedAddressOffset(
                  Op.getRawOperand(0))) {
        // Check relocation for the address.
        if (std::optional<int64_t> RelocAdjustment =
                RelocMgr.getExprOpAddressRelocAdjustment(
                    *U, Op, *AddressOffset,
                    *AddressOffset + DIE.getDwarfUnit()->getAddressByteSize(),
                    Options.Verbose))
          return std::make_pair(HasLocationAddress, *RelocAdjustment);
      }
    } break;
    default: {
      // Nothing to do.
    } break;
    }
    CurExprOffset = Op.getEndOffset();
  }

  return std::make_pair(HasLocationAddress, std::nullopt);
}

/// Check if a variable describing DIE should be kept.
/// \returns updated TraversalFlags.
unsigned DWARFLinker::shouldKeepVariableDIE(AddressesMap &RelocMgr,
                                            const DWARFDie &DIE,
                                            CompileUnit::DIEInfo &MyInfo,
                                            unsigned Flags) {
  const auto *Abbrev = DIE.getAbbreviationDeclarationPtr();

  // Global variables with constant value can always be kept.
  if (!(Flags & TF_InFunctionScope) &&
      Abbrev->findAttributeIndex(dwarf::DW_AT_const_value)) {
    MyInfo.InDebugMap = true;
    return Flags | TF_Keep;
  }

  // See if there is a relocation to a valid debug map entry inside this
  // variable's location. The order is important here. We want to always check
  // if the variable has a valid relocation, so that the DIEInfo is filled.
  // However, we don't want a static variable in a function to force us to keep
  // the enclosing function, unless requested explicitly.
  std::pair<bool, std::optional<int64_t>> LocExprAddrAndRelocAdjustment =
      getVariableRelocAdjustment(RelocMgr, DIE);

  if (LocExprAddrAndRelocAdjustment.first)
    MyInfo.HasLocationExpressionAddr = true;

  if (!LocExprAddrAndRelocAdjustment.second)
    return Flags;

  MyInfo.AddrAdjust = *LocExprAddrAndRelocAdjustment.second;
  MyInfo.InDebugMap = true;

  if (((Flags & TF_InFunctionScope) &&
       !LLVM_UNLIKELY(Options.KeepFunctionForStatic)))
    return Flags;

  if (Options.Verbose) {
    outs() << "Keeping variable DIE:";
    DIDumpOptions DumpOpts;
    DumpOpts.ChildRecurseDepth = 0;
    DumpOpts.Verbose = Options.Verbose;
    DIE.dump(outs(), 8 /* Indent */, DumpOpts);
  }

  return Flags | TF_Keep;
}

/// Check if a function describing DIE should be kept.
/// \returns updated TraversalFlags.
unsigned DWARFLinker::shouldKeepSubprogramDIE(
    AddressesMap &RelocMgr, const DWARFDie &DIE, const DWARFFile &File,
    CompileUnit &Unit, CompileUnit::DIEInfo &MyInfo, unsigned Flags) {
  Flags |= TF_InFunctionScope;

  auto LowPc = dwarf::toAddress(DIE.find(dwarf::DW_AT_low_pc));
  if (!LowPc)
    return Flags;

  assert(LowPc && "low_pc attribute is not an address.");
  std::optional<int64_t> RelocAdjustment =
      RelocMgr.getSubprogramRelocAdjustment(DIE, Options.Verbose);
  if (!RelocAdjustment)
    return Flags;

  MyInfo.AddrAdjust = *RelocAdjustment;
  MyInfo.InDebugMap = true;

  if (Options.Verbose) {
    outs() << "Keeping subprogram DIE:";
    DIDumpOptions DumpOpts;
    DumpOpts.ChildRecurseDepth = 0;
    DumpOpts.Verbose = Options.Verbose;
    DIE.dump(outs(), 8 /* Indent */, DumpOpts);
  }

  if (DIE.getTag() == dwarf::DW_TAG_label) {
    if (Unit.hasLabelAt(*LowPc))
      return Flags;

    DWARFUnit &OrigUnit = Unit.getOrigUnit();
    // FIXME: dsymutil-classic compat. dsymutil-classic doesn't consider labels
    // that don't fall into the CU's aranges. This is wrong IMO. Debug info
    // generation bugs aside, this is really wrong in the case of labels, where
    // a label marking the end of a function will have a PC == CU's high_pc.
    if (dwarf::toAddress(OrigUnit.getUnitDIE().find(dwarf::DW_AT_high_pc))
            .value_or(UINT64_MAX) <= LowPc)
      return Flags;
    Unit.addLabelLowPc(*LowPc, MyInfo.AddrAdjust);
    return Flags | TF_Keep;
  }

  Flags |= TF_Keep;

  std::optional<uint64_t> HighPc = DIE.getHighPC(*LowPc);
  if (!HighPc) {
    reportWarning("Function without high_pc. Range will be discarded.\n", File,
                  &DIE);
    return Flags;
  }
  if (*LowPc > *HighPc) {
    reportWarning("low_pc greater than high_pc. Range will be discarded.\n",
                  File, &DIE);
    return Flags;
  }

  // Replace the debug map range with a more accurate one.
  Unit.addFunctionRange(*LowPc, *HighPc, MyInfo.AddrAdjust);
  return Flags;
}

/// Check if a DIE should be kept.
/// \returns updated TraversalFlags.
unsigned DWARFLinker::shouldKeepDIE(AddressesMap &RelocMgr, const DWARFDie &DIE,
                                    const DWARFFile &File, CompileUnit &Unit,
                                    CompileUnit::DIEInfo &MyInfo,
                                    unsigned Flags) {
  switch (DIE.getTag()) {
  case dwarf::DW_TAG_constant:
  case dwarf::DW_TAG_variable:
    return shouldKeepVariableDIE(RelocMgr, DIE, MyInfo, Flags);
  case dwarf::DW_TAG_subprogram:
  case dwarf::DW_TAG_label:
    return shouldKeepSubprogramDIE(RelocMgr, DIE, File, Unit, MyInfo, Flags);
  case dwarf::DW_TAG_base_type:
    // DWARF Expressions may reference basic types, but scanning them
    // is expensive. Basic types are tiny, so just keep all of them.
  case dwarf::DW_TAG_imported_module:
  case dwarf::DW_TAG_imported_declaration:
  case dwarf::DW_TAG_imported_unit:
    // We always want to keep these.
    return Flags | TF_Keep;
  default:
    break;
  }

  return Flags;
}

/// Helper that updates the completeness of the current DIE based on the
/// completeness of one of its children. It depends on the incompleteness of
/// the children already being computed.
static void updateChildIncompleteness(const DWARFDie &Die, CompileUnit &CU,
                                      CompileUnit::DIEInfo &ChildInfo) {
  switch (Die.getTag()) {
  case dwarf::DW_TAG_structure_type:
  case dwarf::DW_TAG_class_type:
  case dwarf::DW_TAG_union_type:
    break;
  default:
    return;
  }

  CompileUnit::DIEInfo &MyInfo = CU.getInfo(Die);

  if (ChildInfo.Incomplete || ChildInfo.Prune)
    MyInfo.Incomplete = true;
}

/// Helper that updates the completeness of the current DIE based on the
/// completeness of the DIEs it references. It depends on the incompleteness of
/// the referenced DIE already being computed.
static void updateRefIncompleteness(const DWARFDie &Die, CompileUnit &CU,
                                    CompileUnit::DIEInfo &RefInfo) {
  switch (Die.getTag()) {
  case dwarf::DW_TAG_typedef:
  case dwarf::DW_TAG_member:
  case dwarf::DW_TAG_reference_type:
  case dwarf::DW_TAG_ptr_to_member_type:
  case dwarf::DW_TAG_pointer_type:
    break;
  default:
    return;
  }

  CompileUnit::DIEInfo &MyInfo = CU.getInfo(Die);

  if (MyInfo.Incomplete)
    return;

  if (RefInfo.Incomplete)
    MyInfo.Incomplete = true;
}

/// Look at the children of the given DIE and decide whether they should be
/// kept.
void DWARFLinker::lookForChildDIEsToKeep(
    const DWARFDie &Die, CompileUnit &CU, unsigned Flags,
    SmallVectorImpl<WorklistItem> &Worklist) {
  // The TF_ParentWalk flag tells us that we are currently walking up the
  // parent chain of a required DIE, and we don't want to mark all the children
  // of the parents as kept (consider for example a DW_TAG_namespace node in
  // the parent chain). There are however a set of DIE types for which we want
  // to ignore that directive and still walk their children.
  if (dieNeedsChildrenToBeMeaningful(Die.getTag()))
    Flags &= ~DWARFLinker::TF_ParentWalk;

  // We're finished if this DIE has no children or we're walking the parent
  // chain.
  if (!Die.hasChildren() || (Flags & DWARFLinker::TF_ParentWalk))
    return;

  // Add children in reverse order to the worklist to effectively process them
  // in order.
  for (auto Child : reverse(Die.children())) {
    // Add a worklist item before every child to calculate incompleteness right
    // after the current child is processed.
    CompileUnit::DIEInfo &ChildInfo = CU.getInfo(Child);
    Worklist.emplace_back(Die, CU, WorklistItemType::UpdateChildIncompleteness,
                          &ChildInfo);
    Worklist.emplace_back(Child, CU, Flags);
  }
}

static bool isODRCanonicalCandidate(const DWARFDie &Die, CompileUnit &CU) {
  CompileUnit::DIEInfo &Info = CU.getInfo(Die);

  if (!Info.Ctxt || (Die.getTag() == dwarf::DW_TAG_namespace))
    return false;

  if (!CU.hasODR() && !Info.InModuleScope)
    return false;

  return !Info.Incomplete && Info.Ctxt != CU.getInfo(Info.ParentIdx).Ctxt;
}

void DWARFLinker::markODRCanonicalDie(const DWARFDie &Die, CompileUnit &CU) {
  CompileUnit::DIEInfo &Info = CU.getInfo(Die);

  Info.ODRMarkingDone = true;
  if (Info.Keep && isODRCanonicalCandidate(Die, CU) &&
      !Info.Ctxt->hasCanonicalDIE())
    Info.Ctxt->setHasCanonicalDIE();
}

/// Look at DIEs referenced by the given DIE and decide whether they should be
/// kept. All DIEs referenced though attributes should be kept.
void DWARFLinker::lookForRefDIEsToKeep(
    const DWARFDie &Die, CompileUnit &CU, unsigned Flags,
    const UnitListTy &Units, const DWARFFile &File,
    SmallVectorImpl<WorklistItem> &Worklist) {
  bool UseOdr = (Flags & DWARFLinker::TF_DependencyWalk)
                    ? (Flags & DWARFLinker::TF_ODR)
                    : CU.hasODR();
  DWARFUnit &Unit = CU.getOrigUnit();
  DWARFDataExtractor Data = Unit.getDebugInfoExtractor();
  const auto *Abbrev = Die.getAbbreviationDeclarationPtr();
  uint64_t Offset = Die.getOffset() + getULEB128Size(Abbrev->getCode());

  SmallVector<std::pair<DWARFDie, CompileUnit &>, 4> ReferencedDIEs;
  for (const auto &AttrSpec : Abbrev->attributes()) {
    DWARFFormValue Val(AttrSpec.Form);
    if (!Val.isFormClass(DWARFFormValue::FC_Reference) ||
        AttrSpec.Attr == dwarf::DW_AT_sibling) {
      DWARFFormValue::skipValue(AttrSpec.Form, Data, &Offset,
                                Unit.getFormParams());
      continue;
    }

    Val.extractValue(Data, &Offset, Unit.getFormParams(), &Unit);
    CompileUnit *ReferencedCU;
    if (auto RefDie =
            resolveDIEReference(File, Units, Val, Die, ReferencedCU)) {
      CompileUnit::DIEInfo &Info = ReferencedCU->getInfo(RefDie);
      // If the referenced DIE has a DeclContext that has already been
      // emitted, then do not keep the one in this CU. We'll link to
      // the canonical DIE in cloneDieReferenceAttribute.
      //
      // FIXME: compatibility with dsymutil-classic. UseODR shouldn't
      // be necessary and could be advantageously replaced by
      // ReferencedCU->hasODR() && CU.hasODR().
      //
      // FIXME: compatibility with dsymutil-classic. There is no
      // reason not to unique ref_addr references.
      if (AttrSpec.Form != dwarf::DW_FORM_ref_addr &&
          isODRAttribute(AttrSpec.Attr) && Info.Ctxt &&
          Info.Ctxt->hasCanonicalDIE())
        continue;

      // Keep a module forward declaration if there is no definition.
      if (!(isODRAttribute(AttrSpec.Attr) && Info.Ctxt &&
            Info.Ctxt->hasCanonicalDIE()))
        Info.Prune = false;
      ReferencedDIEs.emplace_back(RefDie, *ReferencedCU);
    }
  }

  unsigned ODRFlag = UseOdr ? DWARFLinker::TF_ODR : 0;

  // Add referenced DIEs in reverse order to the worklist to effectively
  // process them in order.
  for (auto &P : reverse(ReferencedDIEs)) {
    // Add a worklist item before every child to calculate incompleteness right
    // after the current child is processed.
    CompileUnit::DIEInfo &Info = P.second.getInfo(P.first);
    Worklist.emplace_back(Die, CU, WorklistItemType::UpdateRefIncompleteness,
                          &Info);
    Worklist.emplace_back(P.first, P.second,
                          DWARFLinker::TF_Keep |
                              DWARFLinker::TF_DependencyWalk | ODRFlag);
  }
}

/// Look at the parent of the given DIE and decide whether they should be kept.
void DWARFLinker::lookForParentDIEsToKeep(
    unsigned AncestorIdx, CompileUnit &CU, unsigned Flags,
    SmallVectorImpl<WorklistItem> &Worklist) {
  // Stop if we encounter an ancestor that's already marked as kept.
  if (CU.getInfo(AncestorIdx).Keep)
    return;

  DWARFUnit &Unit = CU.getOrigUnit();
  DWARFDie ParentDIE = Unit.getDIEAtIndex(AncestorIdx);
  Worklist.emplace_back(CU.getInfo(AncestorIdx).ParentIdx, CU, Flags);
  Worklist.emplace_back(ParentDIE, CU, Flags);
}

/// Recursively walk the \p DIE tree and look for DIEs to keep. Store that
/// information in \p CU's DIEInfo.
///
/// This function is the entry point of the DIE selection algorithm. It is
/// expected to walk the DIE tree in file order and (though the mediation of
/// its helper) call hasValidRelocation() on each DIE that might be a 'root
/// DIE' (See DwarfLinker class comment).
///
/// While walking the dependencies of root DIEs, this function is also called,
/// but during these dependency walks the file order is not respected. The
/// TF_DependencyWalk flag tells us which kind of traversal we are currently
/// doing.
///
/// The recursive algorithm is implemented iteratively as a work list because
/// very deep recursion could exhaust the stack for large projects. The work
/// list acts as a scheduler for different types of work that need to be
/// performed.
///
/// The recursive nature of the algorithm is simulated by running the "main"
/// algorithm (LookForDIEsToKeep) followed by either looking at more DIEs
/// (LookForChildDIEsToKeep, LookForRefDIEsToKeep, LookForParentDIEsToKeep) or
/// fixing up a computed property (UpdateChildIncompleteness,
/// UpdateRefIncompleteness).
///
/// The return value indicates whether the DIE is incomplete.
void DWARFLinker::lookForDIEsToKeep(AddressesMap &AddressesMap,
                                    const UnitListTy &Units,
                                    const DWARFDie &Die, const DWARFFile &File,
                                    CompileUnit &Cu, unsigned Flags) {
  // LIFO work list.
  SmallVector<WorklistItem, 4> Worklist;
  Worklist.emplace_back(Die, Cu, Flags);

  while (!Worklist.empty()) {
    WorklistItem Current = Worklist.pop_back_val();

    // Look at the worklist type to decide what kind of work to perform.
    switch (Current.Type) {
    case WorklistItemType::UpdateChildIncompleteness:
      updateChildIncompleteness(Current.Die, Current.CU, *Current.OtherInfo);
      continue;
    case WorklistItemType::UpdateRefIncompleteness:
      updateRefIncompleteness(Current.Die, Current.CU, *Current.OtherInfo);
      continue;
    case WorklistItemType::LookForChildDIEsToKeep:
      lookForChildDIEsToKeep(Current.Die, Current.CU, Current.Flags, Worklist);
      continue;
    case WorklistItemType::LookForRefDIEsToKeep:
      lookForRefDIEsToKeep(Current.Die, Current.CU, Current.Flags, Units, File,
                           Worklist);
      continue;
    case WorklistItemType::LookForParentDIEsToKeep:
      lookForParentDIEsToKeep(Current.AncestorIdx, Current.CU, Current.Flags,
                              Worklist);
      continue;
    case WorklistItemType::MarkODRCanonicalDie:
      markODRCanonicalDie(Current.Die, Current.CU);
      continue;
    case WorklistItemType::LookForDIEsToKeep:
      break;
    }

    unsigned Idx = Current.CU.getOrigUnit().getDIEIndex(Current.Die);
    CompileUnit::DIEInfo &MyInfo = Current.CU.getInfo(Idx);

    if (MyInfo.Prune) {
      // We're walking the dependencies of a module forward declaration that was
      // kept because there is no definition.
      if (Current.Flags & TF_DependencyWalk)
        MyInfo.Prune = false;
      else
        continue;
    }

    // If the Keep flag is set, we are marking a required DIE's dependencies.
    // If our target is already marked as kept, we're all set.
    bool AlreadyKept = MyInfo.Keep;
    if ((Current.Flags & TF_DependencyWalk) && AlreadyKept)
      continue;

    if (!(Current.Flags & TF_DependencyWalk))
      Current.Flags = shouldKeepDIE(AddressesMap, Current.Die, File, Current.CU,
                                    MyInfo, Current.Flags);

    // We need to mark context for the canonical die in the end of normal
    // traversing(not TF_DependencyWalk) or after normal traversing if die
    // was not marked as kept.
    if (!(Current.Flags & TF_DependencyWalk) ||
        (MyInfo.ODRMarkingDone && !MyInfo.Keep)) {
      if (Current.CU.hasODR() || MyInfo.InModuleScope)
        Worklist.emplace_back(Current.Die, Current.CU,
                              WorklistItemType::MarkODRCanonicalDie);
    }

    // Finish by looking for child DIEs. Because of the LIFO worklist we need
    // to schedule that work before any subsequent items are added to the
    // worklist.
    Worklist.emplace_back(Current.Die, Current.CU, Current.Flags,
                          WorklistItemType::LookForChildDIEsToKeep);

    if (AlreadyKept || !(Current.Flags & TF_Keep))
      continue;

    // If it is a newly kept DIE mark it as well as all its dependencies as
    // kept.
    MyInfo.Keep = true;

    // We're looking for incomplete types.
    MyInfo.Incomplete =
        Current.Die.getTag() != dwarf::DW_TAG_subprogram &&
        Current.Die.getTag() != dwarf::DW_TAG_member &&
        dwarf::toUnsigned(Current.Die.find(dwarf::DW_AT_declaration), 0);

    // After looking at the parent chain, look for referenced DIEs. Because of
    // the LIFO worklist we need to schedule that work before any subsequent
    // items are added to the worklist.
    Worklist.emplace_back(Current.Die, Current.CU, Current.Flags,
                          WorklistItemType::LookForRefDIEsToKeep);

    bool UseOdr = (Current.Flags & TF_DependencyWalk) ? (Current.Flags & TF_ODR)
                                                      : Current.CU.hasODR();
    unsigned ODRFlag = UseOdr ? TF_ODR : 0;
    unsigned ParFlags = TF_ParentWalk | TF_Keep | TF_DependencyWalk | ODRFlag;

    // Now schedule the parent walk.
    Worklist.emplace_back(MyInfo.ParentIdx, Current.CU, ParFlags);
  }
}

#ifndef NDEBUG
/// A broken link in the keep chain. By recording both the parent and the child
/// we can show only broken links for DIEs with multiple children.
struct BrokenLink {
  BrokenLink(DWARFDie Parent, DWARFDie Child) : Parent(Parent), Child(Child) {}
  DWARFDie Parent;
  DWARFDie Child;
};

/// Verify the keep chain by looking for DIEs that are kept but who's parent
/// isn't.
static void verifyKeepChain(CompileUnit &CU) {
  std::vector<DWARFDie> Worklist;
  Worklist.push_back(CU.getOrigUnit().getUnitDIE());

  // List of broken links.
  std::vector<BrokenLink> BrokenLinks;

  while (!Worklist.empty()) {
    const DWARFDie Current = Worklist.back();
    Worklist.pop_back();

    const bool CurrentDieIsKept = CU.getInfo(Current).Keep;

    for (DWARFDie Child : reverse(Current.children())) {
      Worklist.push_back(Child);

      const bool ChildDieIsKept = CU.getInfo(Child).Keep;
      if (!CurrentDieIsKept && ChildDieIsKept)
        BrokenLinks.emplace_back(Current, Child);
    }
  }

  if (!BrokenLinks.empty()) {
    for (BrokenLink Link : BrokenLinks) {
      WithColor::error() << formatv(
          "Found invalid link in keep chain between {0:x} and {1:x}\n",
          Link.Parent.getOffset(), Link.Child.getOffset());

      errs() << "Parent:";
      Link.Parent.dump(errs(), 0, {});
      CU.getInfo(Link.Parent).dump();

      errs() << "Child:";
      Link.Child.dump(errs(), 2, {});
      CU.getInfo(Link.Child).dump();
    }
    report_fatal_error("invalid keep chain");
  }
}
#endif

/// Assign an abbreviation number to \p Abbrev.
///
/// Our DIEs get freed after every DebugMapObject has been processed,
/// thus the FoldingSet we use to unique DIEAbbrevs cannot refer to
/// the instances hold by the DIEs. When we encounter an abbreviation
/// that we don't know, we create a permanent copy of it.
void DWARFLinker::assignAbbrev(DIEAbbrev &Abbrev) {
  // Check the set for priors.
  FoldingSetNodeID ID;
  Abbrev.Profile(ID);
  void *InsertToken;
  DIEAbbrev *InSet = AbbreviationsSet.FindNodeOrInsertPos(ID, InsertToken);

  // If it's newly added.
  if (InSet) {
    // Assign existing abbreviation number.
    Abbrev.setNumber(InSet->getNumber());
  } else {
    // Add to abbreviation list.
    Abbreviations.push_back(
        std::make_unique<DIEAbbrev>(Abbrev.getTag(), Abbrev.hasChildren()));
    for (const auto &Attr : Abbrev.getData())
      Abbreviations.back()->AddAttribute(Attr);
    AbbreviationsSet.InsertNode(Abbreviations.back().get(), InsertToken);
    // Assign the unique abbreviation number.
    Abbrev.setNumber(Abbreviations.size());
    Abbreviations.back()->setNumber(Abbreviations.size());
  }
}

unsigned DWARFLinker::DIECloner::cloneStringAttribute(DIE &Die,
                                                      AttributeSpec AttrSpec,
                                                      const DWARFFormValue &Val,
                                                      const DWARFUnit &U,
                                                      AttributesInfo &Info) {
  std::optional<const char *> String = dwarf::toString(Val);
  if (!String)
    return 0;
  DwarfStringPoolEntryRef StringEntry;
  if (AttrSpec.Form == dwarf::DW_FORM_line_strp) {
    StringEntry = DebugLineStrPool.getEntry(*String);
  } else {
    StringEntry = DebugStrPool.getEntry(*String);

    if (AttrSpec.Attr == dwarf::DW_AT_APPLE_origin) {
      Info.HasAppleOrigin = true;
      if (std::optional<StringRef> FileName =
              ObjFile.Addresses->getLibraryInstallName()) {
        StringEntry = DebugStrPool.getEntry(*FileName);
      }
    }

    // Update attributes info.
    if (AttrSpec.Attr == dwarf::DW_AT_name)
      Info.Name = StringEntry;
    else if (AttrSpec.Attr == dwarf::DW_AT_MIPS_linkage_name ||
             AttrSpec.Attr == dwarf::DW_AT_linkage_name)
      Info.MangledName = StringEntry;
    if (U.getVersion() >= 5) {
      // Switch everything to DW_FORM_strx strings.
      auto StringOffsetIndex =
          StringOffsetPool.getValueIndex(StringEntry.getOffset());
      return Die
          .addValue(DIEAlloc, dwarf::Attribute(AttrSpec.Attr),
                    dwarf::DW_FORM_strx, DIEInteger(StringOffsetIndex))
          ->sizeOf(U.getFormParams());
    }
    // Switch everything to out of line strings.
    AttrSpec.Form = dwarf::DW_FORM_strp;
  }
  Die.addValue(DIEAlloc, dwarf::Attribute(AttrSpec.Attr), AttrSpec.Form,
               DIEInteger(StringEntry.getOffset()));
  return 4;
}

unsigned DWARFLinker::DIECloner::cloneDieReferenceAttribute(
    DIE &Die, const DWARFDie &InputDIE, AttributeSpec AttrSpec,
    unsigned AttrSize, const DWARFFormValue &Val, const DWARFFile &File,
    CompileUnit &Unit) {
  const DWARFUnit &U = Unit.getOrigUnit();
  uint64_t Ref;
  if (std::optional<uint64_t> Off = Val.getAsRelativeReference())
    Ref = Val.getUnit()->getOffset() + *Off;
  else if (Off = Val.getAsDebugInfoReference(); Off)
    Ref = *Off;
  else
    return 0;

  DIE *NewRefDie = nullptr;
  CompileUnit *RefUnit = nullptr;

  DWARFDie RefDie =
      Linker.resolveDIEReference(File, CompileUnits, Val, InputDIE, RefUnit);

  // If the referenced DIE is not found,  drop the attribute.
  if (!RefDie || AttrSpec.Attr == dwarf::DW_AT_sibling)
    return 0;

  CompileUnit::DIEInfo &RefInfo = RefUnit->getInfo(RefDie);

  // If we already have emitted an equivalent DeclContext, just point
  // at it.
  if (isODRAttribute(AttrSpec.Attr) && RefInfo.Ctxt &&
      RefInfo.Ctxt->getCanonicalDIEOffset()) {
    assert(RefInfo.Ctxt->hasCanonicalDIE() &&
           "Offset to canonical die is set, but context is not marked");
    DIEInteger Attr(RefInfo.Ctxt->getCanonicalDIEOffset());
    Die.addValue(DIEAlloc, dwarf::Attribute(AttrSpec.Attr),
                 dwarf::DW_FORM_ref_addr, Attr);
    return U.getRefAddrByteSize();
  }

  if (!RefInfo.Clone) {
    // We haven't cloned this DIE yet. Just create an empty one and
    // store it. It'll get really cloned when we process it.
    RefInfo.UnclonedReference = true;
    RefInfo.Clone = DIE::get(DIEAlloc, dwarf::Tag(RefDie.getTag()));
  }
  NewRefDie = RefInfo.Clone;

  if (AttrSpec.Form == dwarf::DW_FORM_ref_addr ||
      (Unit.hasODR() && isODRAttribute(AttrSpec.Attr))) {
    // We cannot currently rely on a DIEEntry to emit ref_addr
    // references, because the implementation calls back to DwarfDebug
    // to find the unit offset. (We don't have a DwarfDebug)
    // FIXME: we should be able to design DIEEntry reliance on
    // DwarfDebug away.
    uint64_t Attr;
    if (Ref < InputDIE.getOffset() && !RefInfo.UnclonedReference) {
      // We have already cloned that DIE.
      uint32_t NewRefOffset =
          RefUnit->getStartOffset() + NewRefDie->getOffset();
      Attr = NewRefOffset;
      Die.addValue(DIEAlloc, dwarf::Attribute(AttrSpec.Attr),
                   dwarf::DW_FORM_ref_addr, DIEInteger(Attr));
    } else {
      // A forward reference. Note and fixup later.
      Attr = 0xBADDEF;
      Unit.noteForwardReference(
          NewRefDie, RefUnit, RefInfo.Ctxt,
          Die.addValue(DIEAlloc, dwarf::Attribute(AttrSpec.Attr),
                       dwarf::DW_FORM_ref_addr, DIEInteger(Attr)));
    }
    return U.getRefAddrByteSize();
  }

  Die.addValue(DIEAlloc, dwarf::Attribute(AttrSpec.Attr),
               dwarf::Form(AttrSpec.Form), DIEEntry(*NewRefDie));

  return AttrSize;
}

void DWARFLinker::DIECloner::cloneExpression(
    DataExtractor &Data, DWARFExpression Expression, const DWARFFile &File,
    CompileUnit &Unit, SmallVectorImpl<uint8_t> &OutputBuffer,
    int64_t AddrRelocAdjustment, bool IsLittleEndian) {
  using Encoding = DWARFExpression::Operation::Encoding;

  uint8_t OrigAddressByteSize = Unit.getOrigUnit().getAddressByteSize();

  uint64_t OpOffset = 0;
  for (auto &Op : Expression) {
    auto Desc = Op.getDescription();
    // DW_OP_const_type is variable-length and has 3
    // operands. Thus far we only support 2.
    if ((Desc.Op.size() == 2 && Desc.Op[0] == Encoding::BaseTypeRef) ||
        (Desc.Op.size() == 2 && Desc.Op[1] == Encoding::BaseTypeRef &&
         Desc.Op[0] != Encoding::Size1))
      Linker.reportWarning("Unsupported DW_OP encoding.", File);

    if ((Desc.Op.size() == 1 && Desc.Op[0] == Encoding::BaseTypeRef) ||
        (Desc.Op.size() == 2 && Desc.Op[1] == Encoding::BaseTypeRef &&
         Desc.Op[0] == Encoding::Size1)) {
      // This code assumes that the other non-typeref operand fits into 1 byte.
      assert(OpOffset < Op.getEndOffset());
      uint32_t ULEBsize = Op.getEndOffset() - OpOffset - 1;
      assert(ULEBsize <= 16);

      // Copy over the operation.
      assert(!Op.getSubCode() && "SubOps not yet supported");
      OutputBuffer.push_back(Op.getCode());
      uint64_t RefOffset;
      if (Desc.Op.size() == 1) {
        RefOffset = Op.getRawOperand(0);
      } else {
        OutputBuffer.push_back(Op.getRawOperand(0));
        RefOffset = Op.getRawOperand(1);
      }
      uint32_t Offset = 0;
      // Look up the base type. For DW_OP_convert, the operand may be 0 to
      // instead indicate the generic type. The same holds for
      // DW_OP_reinterpret, which is currently not supported.
      if (RefOffset > 0 || Op.getCode() != dwarf::DW_OP_convert) {
        RefOffset += Unit.getOrigUnit().getOffset();
        auto RefDie = Unit.getOrigUnit().getDIEForOffset(RefOffset);
        CompileUnit::DIEInfo &Info = Unit.getInfo(RefDie);
        if (DIE *Clone = Info.Clone)
          Offset = Clone->getOffset();
        else
          Linker.reportWarning(
              "base type ref doesn't point to DW_TAG_base_type.", File);
      }
      uint8_t ULEB[16];
      unsigned RealSize = encodeULEB128(Offset, ULEB, ULEBsize);
      if (RealSize > ULEBsize) {
        // Emit the generic type as a fallback.
        RealSize = encodeULEB128(0, ULEB, ULEBsize);
        Linker.reportWarning("base type ref doesn't fit.", File);
      }
      assert(RealSize == ULEBsize && "padding failed");
      ArrayRef<uint8_t> ULEBbytes(ULEB, ULEBsize);
      OutputBuffer.append(ULEBbytes.begin(), ULEBbytes.end());
    } else if (!Linker.Options.Update && Op.getCode() == dwarf::DW_OP_addrx) {
      if (std::optional<object::SectionedAddress> SA =
              Unit.getOrigUnit().getAddrOffsetSectionItem(
                  Op.getRawOperand(0))) {
        // DWARFLinker does not use addrx forms since it generates relocated
        // addresses. Replace DW_OP_addrx with DW_OP_addr here.
        // Argument of DW_OP_addrx should be relocated here as it is not
        // processed by applyValidRelocs.
        OutputBuffer.push_back(dwarf::DW_OP_addr);
        uint64_t LinkedAddress = SA->Address + AddrRelocAdjustment;
        if (IsLittleEndian != sys::IsLittleEndianHost)
          sys::swapByteOrder(LinkedAddress);
        ArrayRef<uint8_t> AddressBytes(
            reinterpret_cast<const uint8_t *>(&LinkedAddress),
            OrigAddressByteSize);
        OutputBuffer.append(AddressBytes.begin(), AddressBytes.end());
      } else
        Linker.reportWarning("cannot read DW_OP_addrx operand.", File);
    } else if (!Linker.Options.Update && Op.getCode() == dwarf::DW_OP_constx) {
      if (std::optional<object::SectionedAddress> SA =
              Unit.getOrigUnit().getAddrOffsetSectionItem(
                  Op.getRawOperand(0))) {
        // DWARFLinker does not use constx forms since it generates relocated
        // addresses. Replace DW_OP_constx with DW_OP_const[*]u here.
        // Argument of DW_OP_constx should be relocated here as it is not
        // processed by applyValidRelocs.
        std::optional<uint8_t> OutOperandKind;
        switch (OrigAddressByteSize) {
        case 4:
          OutOperandKind = dwarf::DW_OP_const4u;
          break;
        case 8:
          OutOperandKind = dwarf::DW_OP_const8u;
          break;
        default:
          Linker.reportWarning(
              formatv(("unsupported address size: {0}."), OrigAddressByteSize),
              File);
          break;
        }

        if (OutOperandKind) {
          OutputBuffer.push_back(*OutOperandKind);
          uint64_t LinkedAddress = SA->Address + AddrRelocAdjustment;
          if (IsLittleEndian != sys::IsLittleEndianHost)
            sys::swapByteOrder(LinkedAddress);
          ArrayRef<uint8_t> AddressBytes(
              reinterpret_cast<const uint8_t *>(&LinkedAddress),
              OrigAddressByteSize);
          OutputBuffer.append(AddressBytes.begin(), AddressBytes.end());
        }
      } else
        Linker.reportWarning("cannot read DW_OP_constx operand.", File);
    } else {
      // Copy over everything else unmodified.
      StringRef Bytes = Data.getData().slice(OpOffset, Op.getEndOffset());
      OutputBuffer.append(Bytes.begin(), Bytes.end());
    }
    OpOffset = Op.getEndOffset();
  }
}

unsigned DWARFLinker::DIECloner::cloneBlockAttribute(
    DIE &Die, const DWARFDie &InputDIE, const DWARFFile &File,
    CompileUnit &Unit, AttributeSpec AttrSpec, const DWARFFormValue &Val,
    bool IsLittleEndian) {
  DIEValueList *Attr;
  DIEValue Value;
  DIELoc *Loc = nullptr;
  DIEBlock *Block = nullptr;
  if (AttrSpec.Form == dwarf::DW_FORM_exprloc) {
    Loc = new (DIEAlloc) DIELoc;
    Linker.DIELocs.push_back(Loc);
  } else {
    Block = new (DIEAlloc) DIEBlock;
    Linker.DIEBlocks.push_back(Block);
  }
  Attr = Loc ? static_cast<DIEValueList *>(Loc)
             : static_cast<DIEValueList *>(Block);

  DWARFUnit &OrigUnit = Unit.getOrigUnit();
  // If the block is a DWARF Expression, clone it into the temporary
  // buffer using cloneExpression(), otherwise copy the data directly.
  SmallVector<uint8_t, 32> Buffer;
  ArrayRef<uint8_t> Bytes = *Val.getAsBlock();
  if (DWARFAttribute::mayHaveLocationExpr(AttrSpec.Attr) &&
      (Val.isFormClass(DWARFFormValue::FC_Block) ||
       Val.isFormClass(DWARFFormValue::FC_Exprloc))) {
    DataExtractor Data(StringRef((const char *)Bytes.data(), Bytes.size()),
                       IsLittleEndian, OrigUnit.getAddressByteSize());
    DWARFExpression Expr(Data, OrigUnit.getAddressByteSize(),
                         OrigUnit.getFormParams().Format);
    cloneExpression(Data, Expr, File, Unit, Buffer,
                    Unit.getInfo(InputDIE).AddrAdjust, IsLittleEndian);
    Bytes = Buffer;
  }
  for (auto Byte : Bytes)
    Attr->addValue(DIEAlloc, static_cast<dwarf::Attribute>(0),
                   dwarf::DW_FORM_data1, DIEInteger(Byte));

  // FIXME: If DIEBlock and DIELoc just reuses the Size field of
  // the DIE class, this "if" could be replaced by
  // Attr->setSize(Bytes.size()).
  if (Loc)
    Loc->setSize(Bytes.size());
  else
    Block->setSize(Bytes.size());

  if (Loc)
    Value = DIEValue(dwarf::Attribute(AttrSpec.Attr),
                     dwarf::Form(AttrSpec.Form), Loc);
  else {
    // The expression location data might be updated and exceed the original
    // size. Check whether the new data fits into the original form.
    if ((AttrSpec.Form == dwarf::DW_FORM_block1 &&
         (Bytes.size() > UINT8_MAX)) ||
        (AttrSpec.Form == dwarf::DW_FORM_block2 &&
         (Bytes.size() > UINT16_MAX)) ||
        (AttrSpec.Form == dwarf::DW_FORM_block4 && (Bytes.size() > UINT32_MAX)))
      AttrSpec.Form = dwarf::DW_FORM_block;

    Value = DIEValue(dwarf::Attribute(AttrSpec.Attr),
                     dwarf::Form(AttrSpec.Form), Block);
  }

  return Die.addValue(DIEAlloc, Value)->sizeOf(OrigUnit.getFormParams());
}

unsigned DWARFLinker::DIECloner::cloneAddressAttribute(
    DIE &Die, const DWARFDie &InputDIE, AttributeSpec AttrSpec,
    unsigned AttrSize, const DWARFFormValue &Val, const CompileUnit &Unit,
    AttributesInfo &Info) {
  if (AttrSpec.Attr == dwarf::DW_AT_low_pc)
    Info.HasLowPc = true;

  if (LLVM_UNLIKELY(Linker.Options.Update)) {
    Die.addValue(DIEAlloc, dwarf::Attribute(AttrSpec.Attr),
                 dwarf::Form(AttrSpec.Form), DIEInteger(Val.getRawUValue()));
    return AttrSize;
  }

  // Cloned Die may have address attributes relocated to a
  // totally unrelated value. This can happen:
  //   - If high_pc is an address (Dwarf version == 2), then it might have been
  //     relocated to a totally unrelated value (because the end address in the
  //     object file might be start address of another function which got moved
  //     independently by the linker).
  //   - If address relocated in an inline_subprogram that happens at the
  //     beginning of its inlining function.
  //  To avoid above cases and to not apply relocation twice (in
  //  applyValidRelocs and here), read address attribute from InputDIE and apply
  //  Info.PCOffset here.

  std::optional<DWARFFormValue> AddrAttribute = InputDIE.find(AttrSpec.Attr);
  if (!AddrAttribute)
    llvm_unreachable("Cann't find attribute.");

  std::optional<uint64_t> Addr = AddrAttribute->getAsAddress();
  if (!Addr) {
    Linker.reportWarning("Cann't read address attribute value.", ObjFile);
    return 0;
  }

  if (InputDIE.getTag() == dwarf::DW_TAG_compile_unit &&
      AttrSpec.Attr == dwarf::DW_AT_low_pc) {
    if (std::optional<uint64_t> LowPC = Unit.getLowPc())
      Addr = *LowPC;
    else
      return 0;
  } else if (InputDIE.getTag() == dwarf::DW_TAG_compile_unit &&
             AttrSpec.Attr == dwarf::DW_AT_high_pc) {
    if (uint64_t HighPc = Unit.getHighPc())
      Addr = HighPc;
    else
      return 0;
  } else {
    *Addr += Info.PCOffset;
  }

  if (AttrSpec.Form == dwarf::DW_FORM_addr) {
    Die.addValue(DIEAlloc, static_cast<dwarf::Attribute>(AttrSpec.Attr),
                 AttrSpec.Form, DIEInteger(*Addr));
    return Unit.getOrigUnit().getAddressByteSize();
  }

  auto AddrIndex = AddrPool.getValueIndex(*Addr);

  return Die
      .addValue(DIEAlloc, static_cast<dwarf::Attribute>(AttrSpec.Attr),
                dwarf::Form::DW_FORM_addrx, DIEInteger(AddrIndex))
      ->sizeOf(Unit.getOrigUnit().getFormParams());
}

unsigned DWARFLinker::DIECloner::cloneScalarAttribute(
    DIE &Die, const DWARFDie &InputDIE, const DWARFFile &File,
    CompileUnit &Unit, AttributeSpec AttrSpec, const DWARFFormValue &Val,
    unsigned AttrSize, AttributesInfo &Info) {
  uint64_t Value;

  // Check for the offset to the macro table. If offset is incorrect then we
  // need to remove the attribute.
  if (AttrSpec.Attr == dwarf::DW_AT_macro_info) {
    if (std::optional<uint64_t> Offset = Val.getAsSectionOffset()) {
      const llvm::DWARFDebugMacro *Macro = File.Dwarf->getDebugMacinfo();
      if (Macro == nullptr || !Macro->hasEntryForOffset(*Offset))
        return 0;
    }
  }

  if (AttrSpec.Attr == dwarf::DW_AT_macros) {
    if (std::optional<uint64_t> Offset = Val.getAsSectionOffset()) {
      const llvm::DWARFDebugMacro *Macro = File.Dwarf->getDebugMacro();
      if (Macro == nullptr || !Macro->hasEntryForOffset(*Offset))
        return 0;
    }
  }

  if (AttrSpec.Attr == dwarf::DW_AT_str_offsets_base) {
    // DWARFLinker generates common .debug_str_offsets table used for all
    // compile units. The offset to the common .debug_str_offsets table is 8 on
    // DWARF32.
    Info.AttrStrOffsetBaseSeen = true;
    return Die
        .addValue(DIEAlloc, dwarf::DW_AT_str_offsets_base,
                  dwarf::DW_FORM_sec_offset, DIEInteger(8))
        ->sizeOf(Unit.getOrigUnit().getFormParams());
  }

  if (LLVM_UNLIKELY(Linker.Options.Update)) {
    if (auto OptionalValue = Val.getAsUnsignedConstant())
      Value = *OptionalValue;
    else if (auto OptionalValue = Val.getAsSignedConstant())
      Value = *OptionalValue;
    else if (auto OptionalValue = Val.getAsSectionOffset())
      Value = *OptionalValue;
    else {
      Linker.reportWarning(
          "Unsupported scalar attribute form. Dropping attribute.", File,
          &InputDIE);
      return 0;
    }
    if (AttrSpec.Attr == dwarf::DW_AT_declaration && Value)
      Info.IsDeclaration = true;

    if (AttrSpec.Form == dwarf::DW_FORM_loclistx)
      Die.addValue(DIEAlloc, dwarf::Attribute(AttrSpec.Attr),
                   dwarf::Form(AttrSpec.Form), DIELocList(Value));
    else
      Die.addValue(DIEAlloc, dwarf::Attribute(AttrSpec.Attr),
                   dwarf::Form(AttrSpec.Form), DIEInteger(Value));
    return AttrSize;
  }

  [[maybe_unused]] dwarf::Form OriginalForm = AttrSpec.Form;
  if (AttrSpec.Form == dwarf::DW_FORM_rnglistx) {
    // DWARFLinker does not generate .debug_addr table. Thus we need to change
    // all "addrx" related forms to "addr" version. Change DW_FORM_rnglistx
    // to DW_FORM_sec_offset here.
    std::optional<uint64_t> Index = Val.getAsSectionOffset();
    if (!Index) {
      Linker.reportWarning("Cannot read the attribute. Dropping.", File,
                           &InputDIE);
      return 0;
    }
    std::optional<uint64_t> Offset =
        Unit.getOrigUnit().getRnglistOffset(*Index);
    if (!Offset) {
      Linker.reportWarning("Cannot read the attribute. Dropping.", File,
                           &InputDIE);
      return 0;
    }

    Value = *Offset;
    AttrSpec.Form = dwarf::DW_FORM_sec_offset;
    AttrSize = Unit.getOrigUnit().getFormParams().getDwarfOffsetByteSize();
  } else if (AttrSpec.Form == dwarf::DW_FORM_loclistx) {
    // DWARFLinker does not generate .debug_addr table. Thus we need to change
    // all "addrx" related forms to "addr" version. Change DW_FORM_loclistx
    // to DW_FORM_sec_offset here.
    std::optional<uint64_t> Index = Val.getAsSectionOffset();
    if (!Index) {
      Linker.reportWarning("Cannot read the attribute. Dropping.", File,
                           &InputDIE);
      return 0;
    }
    std::optional<uint64_t> Offset =
        Unit.getOrigUnit().getLoclistOffset(*Index);
    if (!Offset) {
      Linker.reportWarning("Cannot read the attribute. Dropping.", File,
                           &InputDIE);
      return 0;
    }

    Value = *Offset;
    AttrSpec.Form = dwarf::DW_FORM_sec_offset;
    AttrSize = Unit.getOrigUnit().getFormParams().getDwarfOffsetByteSize();
  } else if (AttrSpec.Attr == dwarf::DW_AT_high_pc &&
             Die.getTag() == dwarf::DW_TAG_compile_unit) {
    std::optional<uint64_t> LowPC = Unit.getLowPc();
    if (!LowPC)
      return 0;
    // Dwarf >= 4 high_pc is an size, not an address.
    Value = Unit.getHighPc() - *LowPC;
  } else if (AttrSpec.Form == dwarf::DW_FORM_sec_offset)
    Value = *Val.getAsSectionOffset();
  else if (AttrSpec.Form == dwarf::DW_FORM_sdata)
    Value = *Val.getAsSignedConstant();
  else if (auto OptionalValue = Val.getAsUnsignedConstant())
    Value = *OptionalValue;
  else {
    Linker.reportWarning(
        "Unsupported scalar attribute form. Dropping attribute.", File,
        &InputDIE);
    return 0;
  }

  DIE::value_iterator Patch =
      Die.addValue(DIEAlloc, dwarf::Attribute(AttrSpec.Attr),
                   dwarf::Form(AttrSpec.Form), DIEInteger(Value));
  if (AttrSpec.Attr == dwarf::DW_AT_ranges ||
      AttrSpec.Attr == dwarf::DW_AT_start_scope) {
    Unit.noteRangeAttribute(Die, Patch);
    Info.HasRanges = true;
  } else if (DWARFAttribute::mayHaveLocationList(AttrSpec.Attr) &&
             dwarf::doesFormBelongToClass(AttrSpec.Form,
                                          DWARFFormValue::FC_SectionOffset,
                                          Unit.getOrigUnit().getVersion())) {

    CompileUnit::DIEInfo &LocationDieInfo = Unit.getInfo(InputDIE);
    Unit.noteLocationAttribute({Patch, LocationDieInfo.InDebugMap
                                           ? LocationDieInfo.AddrAdjust
                                           : Info.PCOffset});
  } else if (AttrSpec.Attr == dwarf::DW_AT_declaration && Value)
    Info.IsDeclaration = true;

  // check that all dwarf::DW_FORM_rnglistx are handled previously.
  assert((Info.HasRanges || (OriginalForm != dwarf::DW_FORM_rnglistx)) &&
         "Unhandled DW_FORM_rnglistx attribute");

  return AttrSize;
}

/// Clone \p InputDIE's attribute described by \p AttrSpec with
/// value \p Val, and add it to \p Die.
/// \returns the size of the cloned attribute.
unsigned DWARFLinker::DIECloner::cloneAttribute(
    DIE &Die, const DWARFDie &InputDIE, const DWARFFile &File,
    CompileUnit &Unit, const DWARFFormValue &Val, const AttributeSpec AttrSpec,
    unsigned AttrSize, AttributesInfo &Info, bool IsLittleEndian) {
  const DWARFUnit &U = Unit.getOrigUnit();

  switch (AttrSpec.Form) {
  case dwarf::DW_FORM_strp:
  case dwarf::DW_FORM_line_strp:
  case dwarf::DW_FORM_string:
  case dwarf::DW_FORM_strx:
  case dwarf::DW_FORM_strx1:
  case dwarf::DW_FORM_strx2:
  case dwarf::DW_FORM_strx3:
  case dwarf::DW_FORM_strx4:
    return cloneStringAttribute(Die, AttrSpec, Val, U, Info);
  case dwarf::DW_FORM_ref_addr:
  case dwarf::DW_FORM_ref1:
  case dwarf::DW_FORM_ref2:
  case dwarf::DW_FORM_ref4:
  case dwarf::DW_FORM_ref8:
    return cloneDieReferenceAttribute(Die, InputDIE, AttrSpec, AttrSize, Val,
                                      File, Unit);
  case dwarf::DW_FORM_block:
  case dwarf::DW_FORM_block1:
  case dwarf::DW_FORM_block2:
  case dwarf::DW_FORM_block4:
  case dwarf::DW_FORM_exprloc:
    return cloneBlockAttribute(Die, InputDIE, File, Unit, AttrSpec, Val,
                               IsLittleEndian);
  case dwarf::DW_FORM_addr:
  case dwarf::DW_FORM_addrx:
  case dwarf::DW_FORM_addrx1:
  case dwarf::DW_FORM_addrx2:
  case dwarf::DW_FORM_addrx3:
  case dwarf::DW_FORM_addrx4:
    return cloneAddressAttribute(Die, InputDIE, AttrSpec, AttrSize, Val, Unit,
                                 Info);
  case dwarf::DW_FORM_data1:
  case dwarf::DW_FORM_data2:
  case dwarf::DW_FORM_data4:
  case dwarf::DW_FORM_data8:
  case dwarf::DW_FORM_udata:
  case dwarf::DW_FORM_sdata:
  case dwarf::DW_FORM_sec_offset:
  case dwarf::DW_FORM_flag:
  case dwarf::DW_FORM_flag_present:
  case dwarf::DW_FORM_rnglistx:
  case dwarf::DW_FORM_loclistx:
  case dwarf::DW_FORM_implicit_const:
    return cloneScalarAttribute(Die, InputDIE, File, Unit, AttrSpec, Val,
                                AttrSize, Info);
  default:
    Linker.reportWarning("Unsupported attribute form " +
                             dwarf::FormEncodingString(AttrSpec.Form) +
                             " in cloneAttribute. Dropping.",
                         File, &InputDIE);
  }

  return 0;
}

void DWARFLinker::DIECloner::addObjCAccelerator(CompileUnit &Unit,
                                                const DIE *Die,
                                                DwarfStringPoolEntryRef Name,
                                                OffsetsStringPool &StringPool,
                                                bool SkipPubSection) {
  std::optional<ObjCSelectorNames> Names =
      getObjCNamesIfSelector(Name.getString());
  if (!Names)
    return;
  Unit.addNameAccelerator(Die, StringPool.getEntry(Names->Selector),
                          SkipPubSection);
  Unit.addObjCAccelerator(Die, StringPool.getEntry(Names->ClassName),
                          SkipPubSection);
  if (Names->ClassNameNoCategory)
    Unit.addObjCAccelerator(
        Die, StringPool.getEntry(*Names->ClassNameNoCategory), SkipPubSection);
  if (Names->MethodNameNoCategory)
    Unit.addNameAccelerator(
        Die, StringPool.getEntry(*Names->MethodNameNoCategory), SkipPubSection);
}

static bool
shouldSkipAttribute(bool Update,
                    DWARFAbbreviationDeclaration::AttributeSpec AttrSpec,
                    bool SkipPC) {
  switch (AttrSpec.Attr) {
  default:
    return false;
  case dwarf::DW_AT_low_pc:
  case dwarf::DW_AT_high_pc:
  case dwarf::DW_AT_ranges:
    return !Update && SkipPC;
  case dwarf::DW_AT_rnglists_base:
    // In case !Update the .debug_addr table is not generated/preserved.
    // Thus instead of DW_FORM_rnglistx the DW_FORM_sec_offset is used.
    // Since DW_AT_rnglists_base is used for only DW_FORM_rnglistx the
    // DW_AT_rnglists_base is removed.
    return !Update;
  case dwarf::DW_AT_loclists_base:
    // In case !Update the .debug_addr table is not generated/preserved.
    // Thus instead of DW_FORM_loclistx the DW_FORM_sec_offset is used.
    // Since DW_AT_loclists_base is used for only DW_FORM_loclistx the
    // DW_AT_loclists_base is removed.
    return !Update;
  case dwarf::DW_AT_location:
  case dwarf::DW_AT_frame_base:
    return !Update && SkipPC;
  }
}

struct AttributeLinkedOffsetFixup {
  int64_t LinkedOffsetFixupVal;
  uint64_t InputAttrStartOffset;
  uint64_t InputAttrEndOffset;
};

DIE *DWARFLinker::DIECloner::cloneDIE(const DWARFDie &InputDIE,
                                      const DWARFFile &File, CompileUnit &Unit,
                                      int64_t PCOffset, uint32_t OutOffset,
                                      unsigned Flags, bool IsLittleEndian,
                                      DIE *Die) {
  DWARFUnit &U = Unit.getOrigUnit();
  unsigned Idx = U.getDIEIndex(InputDIE);
  CompileUnit::DIEInfo &Info = Unit.getInfo(Idx);

  // Should the DIE appear in the output?
  if (!Unit.getInfo(Idx).Keep)
    return nullptr;

  uint64_t Offset = InputDIE.getOffset();
  assert(!(Die && Info.Clone) && "Can't supply a DIE and a cloned DIE");
  if (!Die) {
    // The DIE might have been already created by a forward reference
    // (see cloneDieReferenceAttribute()).
    if (!Info.Clone)
      Info.Clone = DIE::get(DIEAlloc, dwarf::Tag(InputDIE.getTag()));
    Die = Info.Clone;
  }

  assert(Die->getTag() == InputDIE.getTag());
  Die->setOffset(OutOffset);
  if (isODRCanonicalCandidate(InputDIE, Unit) && Info.Ctxt &&
      (Info.Ctxt->getCanonicalDIEOffset() == 0)) {
    if (!Info.Ctxt->hasCanonicalDIE())
      Info.Ctxt->setHasCanonicalDIE();
    // We are about to emit a DIE that is the root of its own valid
    // DeclContext tree. Make the current offset the canonical offset
    // for this context.
    Info.Ctxt->setCanonicalDIEOffset(OutOffset + Unit.getStartOffset());
  }

  // Extract and clone every attribute.
  DWARFDataExtractor Data = U.getDebugInfoExtractor();
  // Point to the next DIE (generally there is always at least a NULL
  // entry after the current one). If this is a lone
  // DW_TAG_compile_unit without any children, point to the next unit.
  uint64_t NextOffset = (Idx + 1 < U.getNumDIEs())
                            ? U.getDIEAtIndex(Idx + 1).getOffset()
                            : U.getNextUnitOffset();
  AttributesInfo AttrInfo;

  // We could copy the data only if we need to apply a relocation to it. After
  // testing, it seems there is no performance downside to doing the copy
  // unconditionally, and it makes the code simpler.
  SmallString<40> DIECopy(Data.getData().substr(Offset, NextOffset - Offset));
  Data =
      DWARFDataExtractor(DIECopy, Data.isLittleEndian(), Data.getAddressSize());

  // Modify the copy with relocated addresses.
  ObjFile.Addresses->applyValidRelocs(DIECopy, Offset, Data.isLittleEndian());

  // Reset the Offset to 0 as we will be working on the local copy of
  // the data.
  Offset = 0;

  const auto *Abbrev = InputDIE.getAbbreviationDeclarationPtr();
  Offset += getULEB128Size(Abbrev->getCode());

  // We are entering a subprogram. Get and propagate the PCOffset.
  if (Die->getTag() == dwarf::DW_TAG_subprogram)
    PCOffset = Info.AddrAdjust;
  AttrInfo.PCOffset = PCOffset;

  if (Abbrev->getTag() == dwarf::DW_TAG_subprogram) {
    Flags |= TF_InFunctionScope;
    if (!Info.InDebugMap && LLVM_LIKELY(!Update))
      Flags |= TF_SkipPC;
  } else if (Abbrev->getTag() == dwarf::DW_TAG_variable) {
    // Function-local globals could be in the debug map even when the function
    // is not, e.g., inlined functions.
    if ((Flags & TF_InFunctionScope) && Info.InDebugMap)
      Flags &= ~TF_SkipPC;
    // Location expressions referencing an address which is not in debug map
    // should be deleted.
    else if (!Info.InDebugMap && Info.HasLocationExpressionAddr &&
             LLVM_LIKELY(!Update))
      Flags |= TF_SkipPC;
  }

  std::optional<StringRef> LibraryInstallName =
      ObjFile.Addresses->getLibraryInstallName();
  SmallVector<AttributeLinkedOffsetFixup> AttributesFixups;
  for (const auto &AttrSpec : Abbrev->attributes()) {
    if (shouldSkipAttribute(Update, AttrSpec, Flags & TF_SkipPC)) {
      DWARFFormValue::skipValue(AttrSpec.Form, Data, &Offset,
                                U.getFormParams());
      continue;
    }

    AttributeLinkedOffsetFixup CurAttrFixup;
    CurAttrFixup.InputAttrStartOffset = InputDIE.getOffset() + Offset;
    CurAttrFixup.LinkedOffsetFixupVal =
        Unit.getStartOffset() + OutOffset - CurAttrFixup.InputAttrStartOffset;

    DWARFFormValue Val = AttrSpec.getFormValue();
    uint64_t AttrSize = Offset;
    Val.extractValue(Data, &Offset, U.getFormParams(), &U);
    CurAttrFixup.InputAttrEndOffset = InputDIE.getOffset() + Offset;
    AttrSize = Offset - AttrSize;

    uint64_t FinalAttrSize =
        cloneAttribute(*Die, InputDIE, File, Unit, Val, AttrSpec, AttrSize,
                       AttrInfo, IsLittleEndian);
    if (FinalAttrSize != 0 && ObjFile.Addresses->needToSaveValidRelocs())
      AttributesFixups.push_back(CurAttrFixup);

    OutOffset += FinalAttrSize;
  }

  uint16_t Tag = InputDIE.getTag();
  // Add the DW_AT_APPLE_origin attribute to Compile Unit die if we have
  // an install name and the DWARF doesn't have the attribute yet.
  const bool NeedsAppleOrigin = (Tag == dwarf::DW_TAG_compile_unit) &&
                                LibraryInstallName.has_value() &&
                                !AttrInfo.HasAppleOrigin;
  if (NeedsAppleOrigin) {
    auto StringEntry = DebugStrPool.getEntry(LibraryInstallName.value());
    Die->addValue(DIEAlloc, dwarf::Attribute(dwarf::DW_AT_APPLE_origin),
                  dwarf::DW_FORM_strp, DIEInteger(StringEntry.getOffset()));
    AttrInfo.Name = StringEntry;
    OutOffset += 4;
  }

  // Look for accelerator entries.
  // FIXME: This is slightly wrong. An inline_subroutine without a
  // low_pc, but with AT_ranges might be interesting to get into the
  // accelerator tables too. For now stick with dsymutil's behavior.
  if ((Info.InDebugMap || AttrInfo.HasLowPc || AttrInfo.HasRanges) &&
      Tag != dwarf::DW_TAG_compile_unit &&
      getDIENames(InputDIE, AttrInfo, DebugStrPool,
                  Tag != dwarf::DW_TAG_inlined_subroutine)) {
    if (AttrInfo.MangledName && AttrInfo.MangledName != AttrInfo.Name)
      Unit.addNameAccelerator(Die, AttrInfo.MangledName,
                              Tag == dwarf::DW_TAG_inlined_subroutine);
    if (AttrInfo.Name) {
      if (AttrInfo.NameWithoutTemplate)
        Unit.addNameAccelerator(Die, AttrInfo.NameWithoutTemplate,
                                /* SkipPubSection */ true);
      Unit.addNameAccelerator(Die, AttrInfo.Name,
                              Tag == dwarf::DW_TAG_inlined_subroutine);
    }
    if (AttrInfo.Name)
      addObjCAccelerator(Unit, Die, AttrInfo.Name, DebugStrPool,
                         /* SkipPubSection =*/true);

  } else if (Tag == dwarf::DW_TAG_namespace) {
    if (!AttrInfo.Name)
      AttrInfo.Name = DebugStrPool.getEntry("(anonymous namespace)");
    Unit.addNamespaceAccelerator(Die, AttrInfo.Name);
  } else if (Tag == dwarf::DW_TAG_imported_declaration && AttrInfo.Name) {
    Unit.addNamespaceAccelerator(Die, AttrInfo.Name);
  } else if (isTypeTag(Tag) && !AttrInfo.IsDeclaration &&
             getDIENames(InputDIE, AttrInfo, DebugStrPool) && AttrInfo.Name &&
             AttrInfo.Name.getString()[0]) {
    uint32_t Hash = hashFullyQualifiedName(InputDIE, Unit, File);
    uint64_t RuntimeLang =
        dwarf::toUnsigned(InputDIE.find(dwarf::DW_AT_APPLE_runtime_class))
            .value_or(0);
    bool ObjCClassIsImplementation =
        (RuntimeLang == dwarf::DW_LANG_ObjC ||
         RuntimeLang == dwarf::DW_LANG_ObjC_plus_plus) &&
        dwarf::toUnsigned(InputDIE.find(dwarf::DW_AT_APPLE_objc_complete_type))
            .value_or(0);
    Unit.addTypeAccelerator(Die, AttrInfo.Name, ObjCClassIsImplementation,
                            Hash);
  }

  // Determine whether there are any children that we want to keep.
  bool HasChildren = false;
  for (auto Child : InputDIE.children()) {
    unsigned Idx = U.getDIEIndex(Child);
    if (Unit.getInfo(Idx).Keep) {
      HasChildren = true;
      break;
    }
  }

  if (Unit.getOrigUnit().getVersion() >= 5 && !AttrInfo.AttrStrOffsetBaseSeen &&
      Die->getTag() == dwarf::DW_TAG_compile_unit) {
    // No DW_AT_str_offsets_base seen, add it to the DIE.
    Die->addValue(DIEAlloc, dwarf::DW_AT_str_offsets_base,
                  dwarf::DW_FORM_sec_offset, DIEInteger(8));
    OutOffset += 4;
  }

  DIEAbbrev NewAbbrev = Die->generateAbbrev();
  if (HasChildren)
    NewAbbrev.setChildrenFlag(dwarf::DW_CHILDREN_yes);
  // Assign a permanent abbrev number
  Linker.assignAbbrev(NewAbbrev);
  Die->setAbbrevNumber(NewAbbrev.getNumber());

  uint64_t AbbrevNumberSize = getULEB128Size(Die->getAbbrevNumber());

  // Add the size of the abbreviation number to the output offset.
  OutOffset += AbbrevNumberSize;

  // Update fixups with the size of the abbreviation number
  for (AttributeLinkedOffsetFixup &F : AttributesFixups)
    F.LinkedOffsetFixupVal += AbbrevNumberSize;

  for (AttributeLinkedOffsetFixup &F : AttributesFixups)
    ObjFile.Addresses->updateAndSaveValidRelocs(
        Unit.getOrigUnit().getVersion() >= 5, Unit.getOrigUnit().getOffset(),
        F.LinkedOffsetFixupVal, F.InputAttrStartOffset, F.InputAttrEndOffset);

  if (!HasChildren) {
    // Update our size.
    Die->setSize(OutOffset - Die->getOffset());
    return Die;
  }

  // Recursively clone children.
  for (auto Child : InputDIE.children()) {
    if (DIE *Clone = cloneDIE(Child, File, Unit, PCOffset, OutOffset, Flags,
                              IsLittleEndian)) {
      Die->addChild(Clone);
      OutOffset = Clone->getOffset() + Clone->getSize();
    }
  }

  // Account for the end of children marker.
  OutOffset += sizeof(int8_t);
  // Update our size.
  Die->setSize(OutOffset - Die->getOffset());
  return Die;
}

/// Patch the input object file relevant debug_ranges or debug_rnglists
/// entries and emit them in the output file. Update the relevant attributes
/// to point at the new entries.
void DWARFLinker::generateUnitRanges(CompileUnit &Unit, const DWARFFile &File,
                                     DebugDieValuePool &AddrPool) const {
  if (LLVM_UNLIKELY(Options.Update))
    return;

  const auto &FunctionRanges = Unit.getFunctionRanges();

  // Build set of linked address ranges for unit function ranges.
  AddressRanges LinkedFunctionRanges;
  for (const AddressRangeValuePair &Range : FunctionRanges)
    LinkedFunctionRanges.insert(
        {Range.Range.start() + Range.Value, Range.Range.end() + Range.Value});

  // Emit LinkedFunctionRanges into .debug_aranges
  if (!LinkedFunctionRanges.empty())
    TheDwarfEmitter->emitDwarfDebugArangesTable(Unit, LinkedFunctionRanges);

  RngListAttributesTy AllRngListAttributes = Unit.getRangesAttributes();
  std::optional<PatchLocation> UnitRngListAttribute =
      Unit.getUnitRangesAttribute();

  if (!AllRngListAttributes.empty() || UnitRngListAttribute) {
    std::optional<AddressRangeValuePair> CachedRange;
    MCSymbol *EndLabel = TheDwarfEmitter->emitDwarfDebugRangeListHeader(Unit);

    // Read original address ranges, apply relocation value, emit linked address
    // ranges.
    for (PatchLocation &AttributePatch : AllRngListAttributes) {
      // Get ranges from the source DWARF corresponding to the current
      // attribute.
      AddressRanges LinkedRanges;
      if (Expected<DWARFAddressRangesVector> OriginalRanges =
              Unit.getOrigUnit().findRnglistFromOffset(AttributePatch.get())) {
        // Apply relocation adjustment.
        for (const auto &Range : *OriginalRanges) {
          if (!CachedRange || !CachedRange->Range.contains(Range.LowPC))
            CachedRange = FunctionRanges.getRangeThatContains(Range.LowPC);

          // All range entries should lie in the function range.
          if (!CachedRange) {
            reportWarning("inconsistent range data.", File);
            continue;
          }

          // Store range for emiting.
          LinkedRanges.insert({Range.LowPC + CachedRange->Value,
                               Range.HighPC + CachedRange->Value});
        }
      } else {
        llvm::consumeError(OriginalRanges.takeError());
        reportWarning("invalid range list ignored.", File);
      }

      // Emit linked ranges.
      TheDwarfEmitter->emitDwarfDebugRangeListFragment(
          Unit, LinkedRanges, AttributePatch, AddrPool);
    }

    // Emit ranges for Unit AT_ranges attribute.
    if (UnitRngListAttribute.has_value())
      TheDwarfEmitter->emitDwarfDebugRangeListFragment(
          Unit, LinkedFunctionRanges, *UnitRngListAttribute, AddrPool);

    // Emit ranges footer.
    TheDwarfEmitter->emitDwarfDebugRangeListFooter(Unit, EndLabel);
  }
}

void DWARFLinker::DIECloner::generateUnitLocations(
    CompileUnit &Unit, const DWARFFile &File,
    ExpressionHandlerRef ExprHandler) {
  if (LLVM_UNLIKELY(Linker.Options.Update))
    return;

  const LocListAttributesTy &AllLocListAttributes =
      Unit.getLocationAttributes();

  if (AllLocListAttributes.empty())
    return;

  // Emit locations list table header.
  MCSymbol *EndLabel = Emitter->emitDwarfDebugLocListHeader(Unit);

  for (auto &CurLocAttr : AllLocListAttributes) {
    // Get location expressions vector corresponding to the current attribute
    // from the source DWARF.
    Expected<DWARFLocationExpressionsVector> OriginalLocations =
        Unit.getOrigUnit().findLoclistFromOffset(CurLocAttr.get());

    if (!OriginalLocations) {
      llvm::consumeError(OriginalLocations.takeError());
      Linker.reportWarning("Invalid location attribute ignored.", File);
      continue;
    }

    DWARFLocationExpressionsVector LinkedLocationExpressions;
    for (DWARFLocationExpression &CurExpression : *OriginalLocations) {
      DWARFLocationExpression LinkedExpression;

      if (CurExpression.Range) {
        // Relocate address range.
        LinkedExpression.Range = {
            CurExpression.Range->LowPC + CurLocAttr.RelocAdjustment,
            CurExpression.Range->HighPC + CurLocAttr.RelocAdjustment};
      }

      // Clone expression.
      LinkedExpression.Expr.reserve(CurExpression.Expr.size());
      ExprHandler(CurExpression.Expr, LinkedExpression.Expr,
                  CurLocAttr.RelocAdjustment);

      LinkedLocationExpressions.push_back(LinkedExpression);
    }

    // Emit locations list table fragment corresponding to the CurLocAttr.
    Emitter->emitDwarfDebugLocListFragment(Unit, LinkedLocationExpressions,
                                           CurLocAttr, AddrPool);
  }

  // Emit locations list table footer.
  Emitter->emitDwarfDebugLocListFooter(Unit, EndLabel);
}

static void patchAddrBase(DIE &Die, DIEInteger Offset) {
  for (auto &V : Die.values())
    if (V.getAttribute() == dwarf::DW_AT_addr_base) {
      V = DIEValue(V.getAttribute(), V.getForm(), Offset);
      return;
    }

  llvm_unreachable("Didn't find a DW_AT_addr_base in cloned DIE!");
}

void DWARFLinker::DIECloner::emitDebugAddrSection(
    CompileUnit &Unit, const uint16_t DwarfVersion) const {

  if (LLVM_UNLIKELY(Linker.Options.Update))
    return;

  if (DwarfVersion < 5)
    return;

  if (AddrPool.getValues().empty())
    return;

  MCSymbol *EndLabel = Emitter->emitDwarfDebugAddrsHeader(Unit);
  patchAddrBase(*Unit.getOutputUnitDIE(),
                DIEInteger(Emitter->getDebugAddrSectionSize()));
  Emitter->emitDwarfDebugAddrs(AddrPool.getValues(),
                               Unit.getOrigUnit().getAddressByteSize());
  Emitter->emitDwarfDebugAddrsFooter(Unit, EndLabel);
}

/// Insert the new line info sequence \p Seq into the current
/// set of already linked line info \p Rows.
static void insertLineSequence(std::vector<DWARFDebugLine::Row> &Seq,
                               std::vector<DWARFDebugLine::Row> &Rows) {
  if (Seq.empty())
    return;

  if (!Rows.empty() && Rows.back().Address < Seq.front().Address) {
    llvm::append_range(Rows, Seq);
    Seq.clear();
    return;
  }

  object::SectionedAddress Front = Seq.front().Address;
  auto InsertPoint = partition_point(
      Rows, [=](const DWARFDebugLine::Row &O) { return O.Address < Front; });

  // FIXME: this only removes the unneeded end_sequence if the
  // sequences have been inserted in order. Using a global sort like
  // described in generateLineTableForUnit() and delaying the end_sequene
  // elimination to emitLineTableForUnit() we can get rid of all of them.
  if (InsertPoint != Rows.end() && InsertPoint->Address == Front &&
      InsertPoint->EndSequence) {
    *InsertPoint = Seq.front();
    Rows.insert(InsertPoint + 1, Seq.begin() + 1, Seq.end());
  } else {
    Rows.insert(InsertPoint, Seq.begin(), Seq.end());
  }

  Seq.clear();
}

static void patchStmtList(DIE &Die, DIEInteger Offset) {
  for (auto &V : Die.values())
    if (V.getAttribute() == dwarf::DW_AT_stmt_list) {
      V = DIEValue(V.getAttribute(), V.getForm(), Offset);
      return;
    }

  llvm_unreachable("Didn't find DW_AT_stmt_list in cloned DIE!");
}

void DWARFLinker::DIECloner::rememberUnitForMacroOffset(CompileUnit &Unit) {
  DWARFUnit &OrigUnit = Unit.getOrigUnit();
  DWARFDie OrigUnitDie = OrigUnit.getUnitDIE();

  if (std::optional<uint64_t> MacroAttr =
          dwarf::toSectionOffset(OrigUnitDie.find(dwarf::DW_AT_macros))) {
    UnitMacroMap.insert(std::make_pair(*MacroAttr, &Unit));
    return;
  }

  if (std::optional<uint64_t> MacroAttr =
          dwarf::toSectionOffset(OrigUnitDie.find(dwarf::DW_AT_macro_info))) {
    UnitMacroMap.insert(std::make_pair(*MacroAttr, &Unit));
    return;
  }
}

void DWARFLinker::DIECloner::generateLineTableForUnit(CompileUnit &Unit) {
  if (LLVM_UNLIKELY(Emitter == nullptr))
    return;

  // Check whether DW_AT_stmt_list attribute is presented.
  DWARFDie CUDie = Unit.getOrigUnit().getUnitDIE();
  auto StmtList = dwarf::toSectionOffset(CUDie.find(dwarf::DW_AT_stmt_list));
  if (!StmtList)
    return;

  // Update the cloned DW_AT_stmt_list with the correct debug_line offset.
  if (auto *OutputDIE = Unit.getOutputUnitDIE())
    patchStmtList(*OutputDIE, DIEInteger(Emitter->getLineSectionSize()));

  if (const DWARFDebugLine::LineTable *LT =
          ObjFile.Dwarf->getLineTableForUnit(&Unit.getOrigUnit())) {

    DWARFDebugLine::LineTable LineTable;

    // Set Line Table header.
    LineTable.Prologue = LT->Prologue;

    // Set Line Table Rows.
    if (Linker.Options.Update) {
      LineTable.Rows = LT->Rows;
      // If all the line table contains is a DW_LNE_end_sequence, clear the line
      // table rows, it will be inserted again in the DWARFStreamer.
      if (LineTable.Rows.size() == 1 && LineTable.Rows[0].EndSequence)
        LineTable.Rows.clear();

      LineTable.Sequences = LT->Sequences;
    } else {
      // This vector is the output line table.
      std::vector<DWARFDebugLine::Row> NewRows;
      NewRows.reserve(LT->Rows.size());

      // Current sequence of rows being extracted, before being inserted
      // in NewRows.
      std::vector<DWARFDebugLine::Row> Seq;

      const auto &FunctionRanges = Unit.getFunctionRanges();
      std::optional<AddressRangeValuePair> CurrRange;

      // FIXME: This logic is meant to generate exactly the same output as
      // Darwin's classic dsymutil. There is a nicer way to implement this
      // by simply putting all the relocated line info in NewRows and simply
      // sorting NewRows before passing it to emitLineTableForUnit. This
      // should be correct as sequences for a function should stay
      // together in the sorted output. There are a few corner cases that
      // look suspicious though, and that required to implement the logic
      // this way. Revisit that once initial validation is finished.

      // Iterate over the object file line info and extract the sequences
      // that correspond to linked functions.
      for (DWARFDebugLine::Row Row : LT->Rows) {
        // Check whether we stepped out of the range. The range is
        // half-open, but consider accept the end address of the range if
        // it is marked as end_sequence in the input (because in that
        // case, the relocation offset is accurate and that entry won't
        // serve as the start of another function).
        if (!CurrRange || !CurrRange->Range.contains(Row.Address.Address)) {
          // We just stepped out of a known range. Insert a end_sequence
          // corresponding to the end of the range.
          uint64_t StopAddress =
              CurrRange ? CurrRange->Range.end() + CurrRange->Value : -1ULL;
          CurrRange = FunctionRanges.getRangeThatContains(Row.Address.Address);
          if (StopAddress != -1ULL && !Seq.empty()) {
            // Insert end sequence row with the computed end address, but
            // the same line as the previous one.
            auto NextLine = Seq.back();
            NextLine.Address.Address = StopAddress;
            NextLine.EndSequence = 1;
            NextLine.PrologueEnd = 0;
            NextLine.BasicBlock = 0;
            NextLine.EpilogueBegin = 0;
            Seq.push_back(NextLine);
            insertLineSequence(Seq, NewRows);
          }

          if (!CurrRange)
            continue;
        }

        // Ignore empty sequences.
        if (Row.EndSequence && Seq.empty())
          continue;

        // Relocate row address and add it to the current sequence.
        Row.Address.Address += CurrRange->Value;
        Seq.emplace_back(Row);

        if (Row.EndSequence)
          insertLineSequence(Seq, NewRows);
      }

      LineTable.Rows = std::move(NewRows);
    }

    Emitter->emitLineTableForUnit(LineTable, Unit, DebugStrPool,
                                  DebugLineStrPool);
  } else
    Linker.reportWarning("Cann't load line table.", ObjFile);
}

void DWARFLinker::emitAcceleratorEntriesForUnit(CompileUnit &Unit) {
  for (AccelTableKind AccelTableKind : Options.AccelTables) {
    switch (AccelTableKind) {
    case AccelTableKind::Apple: {
      // Add namespaces.
      for (const auto &Namespace : Unit.getNamespaces())
        AppleNamespaces.addName(Namespace.Name, Namespace.Die->getOffset() +
                                                    Unit.getStartOffset());
      // Add names.
      for (const auto &Pubname : Unit.getPubnames())
        AppleNames.addName(Pubname.Name,
                           Pubname.Die->getOffset() + Unit.getStartOffset());
      // Add types.
      for (const auto &Pubtype : Unit.getPubtypes())
        AppleTypes.addName(
            Pubtype.Name, Pubtype.Die->getOffset() + Unit.getStartOffset(),
            Pubtype.Die->getTag(),
            Pubtype.ObjcClassImplementation ? dwarf::DW_FLAG_type_implementation
                                            : 0,
            Pubtype.QualifiedNameHash);
      // Add ObjC names.
      for (const auto &ObjC : Unit.getObjC())
        AppleObjc.addName(ObjC.Name,
                          ObjC.Die->getOffset() + Unit.getStartOffset());
    } break;
    case AccelTableKind::Pub: {
      TheDwarfEmitter->emitPubNamesForUnit(Unit);
      TheDwarfEmitter->emitPubTypesForUnit(Unit);
    } break;
    case AccelTableKind::DebugNames: {
      for (const auto &Namespace : Unit.getNamespaces())
        DebugNames.addName(
            Namespace.Name, Namespace.Die->getOffset(),
            DWARF5AccelTableData::getDefiningParentDieOffset(*Namespace.Die),
            Namespace.Die->getTag(), Unit.getUniqueID(),
            Unit.getTag() == dwarf::DW_TAG_type_unit);
      for (const auto &Pubname : Unit.getPubnames())
        DebugNames.addName(
            Pubname.Name, Pubname.Die->getOffset(),
            DWARF5AccelTableData::getDefiningParentDieOffset(*Pubname.Die),
            Pubname.Die->getTag(), Unit.getUniqueID(),
            Unit.getTag() == dwarf::DW_TAG_type_unit);
      for (const auto &Pubtype : Unit.getPubtypes())
        DebugNames.addName(
            Pubtype.Name, Pubtype.Die->getOffset(),
            DWARF5AccelTableData::getDefiningParentDieOffset(*Pubtype.Die),
            Pubtype.Die->getTag(), Unit.getUniqueID(),
            Unit.getTag() == dwarf::DW_TAG_type_unit);
    } break;
    }
  }
}

/// Read the frame info stored in the object, and emit the
/// patched frame descriptions for the resulting file.
///
/// This is actually pretty easy as the data of the CIEs and FDEs can
/// be considered as black boxes and moved as is. The only thing to do
/// is to patch the addresses in the headers.
void DWARFLinker::patchFrameInfoForObject(LinkContext &Context) {
  DWARFContext &OrigDwarf = *Context.File.Dwarf;
  unsigned SrcAddrSize = OrigDwarf.getDWARFObj().getAddressSize();

  StringRef FrameData = OrigDwarf.getDWARFObj().getFrameSection().Data;
  if (FrameData.empty())
    return;

  RangesTy AllUnitsRanges;
  for (std::unique_ptr<CompileUnit> &Unit : Context.CompileUnits) {
    for (auto CurRange : Unit->getFunctionRanges())
      AllUnitsRanges.insert(CurRange.Range, CurRange.Value);
  }

  DataExtractor Data(FrameData, OrigDwarf.isLittleEndian(), 0);
  uint64_t InputOffset = 0;

  // Store the data of the CIEs defined in this object, keyed by their
  // offsets.
  DenseMap<uint64_t, StringRef> LocalCIES;

  while (Data.isValidOffset(InputOffset)) {
    uint64_t EntryOffset = InputOffset;
    uint32_t InitialLength = Data.getU32(&InputOffset);
    if (InitialLength == 0xFFFFFFFF)
      return reportWarning("Dwarf64 bits no supported", Context.File);

    uint32_t CIEId = Data.getU32(&InputOffset);
    if (CIEId == 0xFFFFFFFF) {
      // This is a CIE, store it.
      StringRef CIEData = FrameData.substr(EntryOffset, InitialLength + 4);
      LocalCIES[EntryOffset] = CIEData;
      // The -4 is to account for the CIEId we just read.
      InputOffset += InitialLength - 4;
      continue;
    }

    uint64_t Loc = Data.getUnsigned(&InputOffset, SrcAddrSize);

    // Some compilers seem to emit frame info that doesn't start at
    // the function entry point, thus we can't just lookup the address
    // in the debug map. Use the AddressInfo's range map to see if the FDE
    // describes something that we can relocate.
    std::optional<AddressRangeValuePair> Range =
        AllUnitsRanges.getRangeThatContains(Loc);
    if (!Range) {
      // The +4 is to account for the size of the InitialLength field itself.
      InputOffset = EntryOffset + InitialLength + 4;
      continue;
    }

    // This is an FDE, and we have a mapping.
    // Have we already emitted a corresponding CIE?
    StringRef CIEData = LocalCIES[CIEId];
    if (CIEData.empty())
      return reportWarning("Inconsistent debug_frame content. Dropping.",
                           Context.File);

    // Look if we already emitted a CIE that corresponds to the
    // referenced one (the CIE data is the key of that lookup).
    auto IteratorInserted = EmittedCIEs.insert(
        std::make_pair(CIEData, TheDwarfEmitter->getFrameSectionSize()));
    // If there is no CIE yet for this ID, emit it.
    if (IteratorInserted.second) {
      LastCIEOffset = TheDwarfEmitter->getFrameSectionSize();
      IteratorInserted.first->getValue() = LastCIEOffset;
      TheDwarfEmitter->emitCIE(CIEData);
    }

    // Emit the FDE with updated address and CIE pointer.
    // (4 + AddrSize) is the size of the CIEId + initial_location
    // fields that will get reconstructed by emitFDE().
    unsigned FDERemainingBytes = InitialLength - (4 + SrcAddrSize);
    TheDwarfEmitter->emitFDE(IteratorInserted.first->getValue(), SrcAddrSize,
                             Loc + Range->Value,
                             FrameData.substr(InputOffset, FDERemainingBytes));
    InputOffset += FDERemainingBytes;
  }
}

uint32_t DWARFLinker::DIECloner::hashFullyQualifiedName(DWARFDie DIE,
                                                        CompileUnit &U,
                                                        const DWARFFile &File,
                                                        int ChildRecurseDepth) {
  const char *Name = nullptr;
  DWARFUnit *OrigUnit = &U.getOrigUnit();
  CompileUnit *CU = &U;
  std::optional<DWARFFormValue> Ref;

  while (true) {
    if (const char *CurrentName = DIE.getName(DINameKind::ShortName))
      Name = CurrentName;

    if (!(Ref = DIE.find(dwarf::DW_AT_specification)) &&
        !(Ref = DIE.find(dwarf::DW_AT_abstract_origin)))
      break;

    if (!Ref->isFormClass(DWARFFormValue::FC_Reference))
      break;

    CompileUnit *RefCU;
    if (auto RefDIE =
            Linker.resolveDIEReference(File, CompileUnits, *Ref, DIE, RefCU)) {
      CU = RefCU;
      OrigUnit = &RefCU->getOrigUnit();
      DIE = RefDIE;
    }
  }

  unsigned Idx = OrigUnit->getDIEIndex(DIE);
  if (!Name && DIE.getTag() == dwarf::DW_TAG_namespace)
    Name = "(anonymous namespace)";

  if (CU->getInfo(Idx).ParentIdx == 0 ||
      // FIXME: dsymutil-classic compatibility. Ignore modules.
      CU->getOrigUnit().getDIEAtIndex(CU->getInfo(Idx).ParentIdx).getTag() ==
          dwarf::DW_TAG_module)
    return djbHash(Name ? Name : "", djbHash(ChildRecurseDepth ? "" : "::"));

  DWARFDie Die = OrigUnit->getDIEAtIndex(CU->getInfo(Idx).ParentIdx);
  return djbHash(
      (Name ? Name : ""),
      djbHash((Name ? "::" : ""),
              hashFullyQualifiedName(Die, *CU, File, ++ChildRecurseDepth)));
}

static uint64_t getDwoId(const DWARFDie &CUDie) {
  auto DwoId = dwarf::toUnsigned(
      CUDie.find({dwarf::DW_AT_dwo_id, dwarf::DW_AT_GNU_dwo_id}));
  if (DwoId)
    return *DwoId;
  return 0;
}

static std::string
remapPath(StringRef Path,
          const DWARFLinkerBase::ObjectPrefixMapTy &ObjectPrefixMap) {
  if (ObjectPrefixMap.empty())
    return Path.str();

  SmallString<256> p = Path;
  for (const auto &Entry : ObjectPrefixMap)
    if (llvm::sys::path::replace_path_prefix(p, Entry.first, Entry.second))
      break;
  return p.str().str();
}

static std::string
getPCMFile(const DWARFDie &CUDie,
           const DWARFLinkerBase::ObjectPrefixMapTy *ObjectPrefixMap) {
  std::string PCMFile = dwarf::toString(
      CUDie.find({dwarf::DW_AT_dwo_name, dwarf::DW_AT_GNU_dwo_name}), "");

  if (PCMFile.empty())
    return PCMFile;

  if (ObjectPrefixMap)
    PCMFile = remapPath(PCMFile, *ObjectPrefixMap);

  return PCMFile;
}

std::pair<bool, bool> DWARFLinker::isClangModuleRef(const DWARFDie &CUDie,
                                                    std::string &PCMFile,
                                                    LinkContext &Context,
                                                    unsigned Indent,
                                                    bool Quiet) {
  if (PCMFile.empty())
    return std::make_pair(false, false);

  // Clang module DWARF skeleton CUs abuse this for the path to the module.
  uint64_t DwoId = getDwoId(CUDie);

  std::string Name = dwarf::toString(CUDie.find(dwarf::DW_AT_name), "");
  if (Name.empty()) {
    if (!Quiet)
      reportWarning("Anonymous module skeleton CU for " + PCMFile,
                    Context.File);
    return std::make_pair(true, true);
  }

  if (!Quiet && Options.Verbose) {
    outs().indent(Indent);
    outs() << "Found clang module reference " << PCMFile;
  }

  auto Cached = ClangModules.find(PCMFile);
  if (Cached != ClangModules.end()) {
    // FIXME: Until PR27449 (https://llvm.org/bugs/show_bug.cgi?id=27449) is
    // fixed in clang, only warn about DWO_id mismatches in verbose mode.
    // ASTFileSignatures will change randomly when a module is rebuilt.
    if (!Quiet && Options.Verbose && (Cached->second != DwoId))
      reportWarning(Twine("hash mismatch: this object file was built against a "
                          "different version of the module ") +
                        PCMFile,
                    Context.File);
    if (!Quiet && Options.Verbose)
      outs() << " [cached].\n";
    return std::make_pair(true, true);
  }

  return std::make_pair(true, false);
}

bool DWARFLinker::registerModuleReference(const DWARFDie &CUDie,
                                          LinkContext &Context,
                                          ObjFileLoaderTy Loader,
                                          CompileUnitHandlerTy OnCUDieLoaded,
                                          unsigned Indent) {
  std::string PCMFile = getPCMFile(CUDie, Options.ObjectPrefixMap);
  std::pair<bool, bool> IsClangModuleRef =
      isClangModuleRef(CUDie, PCMFile, Context, Indent, false);

  if (!IsClangModuleRef.first)
    return false;

  if (IsClangModuleRef.second)
    return true;

  if (Options.Verbose)
    outs() << " ...\n";

  // Cyclic dependencies are disallowed by Clang, but we still
  // shouldn't run into an infinite loop, so mark it as processed now.
  ClangModules.insert({PCMFile, getDwoId(CUDie)});

  if (Error E = loadClangModule(Loader, CUDie, PCMFile, Context, OnCUDieLoaded,
                                Indent + 2)) {
    consumeError(std::move(E));
    return false;
  }
  return true;
}

Error DWARFLinker::loadClangModule(
    ObjFileLoaderTy Loader, const DWARFDie &CUDie, const std::string &PCMFile,
    LinkContext &Context, CompileUnitHandlerTy OnCUDieLoaded, unsigned Indent) {

  uint64_t DwoId = getDwoId(CUDie);
  std::string ModuleName = dwarf::toString(CUDie.find(dwarf::DW_AT_name), "");

  /// Using a SmallString<0> because loadClangModule() is recursive.
  SmallString<0> Path(Options.PrependPath);
  if (sys::path::is_relative(PCMFile))
    resolveRelativeObjectPath(Path, CUDie);
  sys::path::append(Path, PCMFile);
  // Don't use the cached binary holder because we have no thread-safety
  // guarantee and the lifetime is limited.

  if (Loader == nullptr) {
    reportError("Could not load clang module: loader is not specified.\n",
                Context.File);
    return Error::success();
  }

  auto ErrOrObj = Loader(Context.File.FileName, Path);
  if (!ErrOrObj)
    return Error::success();

  std::unique_ptr<CompileUnit> Unit;
  for (const auto &CU : ErrOrObj->Dwarf->compile_units()) {
    OnCUDieLoaded(*CU);
    // Recursively get all modules imported by this one.
    auto ChildCUDie = CU->getUnitDIE();
    if (!ChildCUDie)
      continue;
    if (!registerModuleReference(ChildCUDie, Context, Loader, OnCUDieLoaded,
                                 Indent)) {
      if (Unit) {
        std::string Err =
            (PCMFile +
             ": Clang modules are expected to have exactly 1 compile unit.\n");
        reportError(Err, Context.File);
        return make_error<StringError>(Err, inconvertibleErrorCode());
      }
      // FIXME: Until PR27449 (https://llvm.org/bugs/show_bug.cgi?id=27449) is
      // fixed in clang, only warn about DWO_id mismatches in verbose mode.
      // ASTFileSignatures will change randomly when a module is rebuilt.
      uint64_t PCMDwoId = getDwoId(ChildCUDie);
      if (PCMDwoId != DwoId) {
        if (Options.Verbose)
          reportWarning(
              Twine("hash mismatch: this object file was built against a "
                    "different version of the module ") +
                  PCMFile,
              Context.File);
        // Update the cache entry with the DwoId of the module loaded from disk.
        ClangModules[PCMFile] = PCMDwoId;
      }

      // Add this module.
      Unit = std::make_unique<CompileUnit>(*CU, UniqueUnitID++, !Options.NoODR,
                                           ModuleName);
    }
  }

  if (Unit)
    Context.ModuleUnits.emplace_back(RefModuleUnit{*ErrOrObj, std::move(Unit)});

  return Error::success();
}

uint64_t DWARFLinker::DIECloner::cloneAllCompileUnits(
    DWARFContext &DwarfContext, const DWARFFile &File, bool IsLittleEndian) {
  uint64_t OutputDebugInfoSize =
      (Emitter == nullptr) ? 0 : Emitter->getDebugInfoSectionSize();
  const uint64_t StartOutputDebugInfoSize = OutputDebugInfoSize;

  for (auto &CurrentUnit : CompileUnits) {
    const uint16_t DwarfVersion = CurrentUnit->getOrigUnit().getVersion();
    const uint32_t UnitHeaderSize = DwarfVersion >= 5 ? 12 : 11;
    auto InputDIE = CurrentUnit->getOrigUnit().getUnitDIE();
    CurrentUnit->setStartOffset(OutputDebugInfoSize);
    if (!InputDIE) {
      OutputDebugInfoSize = CurrentUnit->computeNextUnitOffset(DwarfVersion);
      continue;
    }
    if (CurrentUnit->getInfo(0).Keep) {
      // Clone the InputDIE into your Unit DIE in our compile unit since it
      // already has a DIE inside of it.
      CurrentUnit->createOutputDIE();
      rememberUnitForMacroOffset(*CurrentUnit);
      cloneDIE(InputDIE, File, *CurrentUnit, 0 /* PC offset */, UnitHeaderSize,
               0, IsLittleEndian, CurrentUnit->getOutputUnitDIE());
    }

    OutputDebugInfoSize = CurrentUnit->computeNextUnitOffset(DwarfVersion);

    if (Emitter != nullptr) {

      generateLineTableForUnit(*CurrentUnit);

      Linker.emitAcceleratorEntriesForUnit(*CurrentUnit);

      if (LLVM_UNLIKELY(Linker.Options.Update))
        continue;

      Linker.generateUnitRanges(*CurrentUnit, File, AddrPool);

      auto ProcessExpr = [&](SmallVectorImpl<uint8_t> &SrcBytes,
                             SmallVectorImpl<uint8_t> &OutBytes,
                             int64_t RelocAdjustment) {
        DWARFUnit &OrigUnit = CurrentUnit->getOrigUnit();
        DataExtractor Data(SrcBytes, IsLittleEndian,
                           OrigUnit.getAddressByteSize());
        cloneExpression(Data,
                        DWARFExpression(Data, OrigUnit.getAddressByteSize(),
                                        OrigUnit.getFormParams().Format),
                        File, *CurrentUnit, OutBytes, RelocAdjustment,
                        IsLittleEndian);
      };
      generateUnitLocations(*CurrentUnit, File, ProcessExpr);
      emitDebugAddrSection(*CurrentUnit, DwarfVersion);
    }
    AddrPool.clear();
  }

  if (Emitter != nullptr) {
    assert(Emitter);
    // Emit macro tables.
    Emitter->emitMacroTables(File.Dwarf.get(), UnitMacroMap, DebugStrPool);

    // Emit all the compile unit's debug information.
    for (auto &CurrentUnit : CompileUnits) {
      CurrentUnit->fixupForwardReferences();

      if (!CurrentUnit->getOutputUnitDIE())
        continue;

      unsigned DwarfVersion = CurrentUnit->getOrigUnit().getVersion();

      assert(Emitter->getDebugInfoSectionSize() ==
             CurrentUnit->getStartOffset());
      Emitter->emitCompileUnitHeader(*CurrentUnit, DwarfVersion);
      Emitter->emitDIE(*CurrentUnit->getOutputUnitDIE());
      assert(Emitter->getDebugInfoSectionSize() ==
             CurrentUnit->computeNextUnitOffset(DwarfVersion));
    }
  }

  return OutputDebugInfoSize - StartOutputDebugInfoSize;
}

void DWARFLinker::copyInvariantDebugSection(DWARFContext &Dwarf) {
  TheDwarfEmitter->emitSectionContents(Dwarf.getDWARFObj().getLocSection().Data,
                                       DebugSectionKind::DebugLoc);
  TheDwarfEmitter->emitSectionContents(
      Dwarf.getDWARFObj().getRangesSection().Data,
      DebugSectionKind::DebugRange);
  TheDwarfEmitter->emitSectionContents(
      Dwarf.getDWARFObj().getFrameSection().Data, DebugSectionKind::DebugFrame);
  TheDwarfEmitter->emitSectionContents(Dwarf.getDWARFObj().getArangesSection(),
                                       DebugSectionKind::DebugARanges);
  TheDwarfEmitter->emitSectionContents(
      Dwarf.getDWARFObj().getAddrSection().Data, DebugSectionKind::DebugAddr);
  TheDwarfEmitter->emitSectionContents(
      Dwarf.getDWARFObj().getRnglistsSection().Data,
      DebugSectionKind::DebugRngLists);
  TheDwarfEmitter->emitSectionContents(
      Dwarf.getDWARFObj().getLoclistsSection().Data,
      DebugSectionKind::DebugLocLists);
}

void DWARFLinker::addObjectFile(DWARFFile &File, ObjFileLoaderTy Loader,
                                CompileUnitHandlerTy OnCUDieLoaded) {
  ObjectContexts.emplace_back(LinkContext(File));

  if (ObjectContexts.back().File.Dwarf) {
    for (const std::unique_ptr<DWARFUnit> &CU :
         ObjectContexts.back().File.Dwarf->compile_units()) {
      DWARFDie CUDie = CU->getUnitDIE();

      if (!CUDie)
        continue;

      OnCUDieLoaded(*CU);

      if (!LLVM_UNLIKELY(Options.Update))
        registerModuleReference(CUDie, ObjectContexts.back(), Loader,
                                OnCUDieLoaded);
    }
  }
}

Error DWARFLinker::link() {
  assert((Options.TargetDWARFVersion != 0) &&
         "TargetDWARFVersion should be set");

  // First populate the data structure we need for each iteration of the
  // parallel loop.
  unsigned NumObjects = ObjectContexts.size();

  // This Dwarf string pool which is used for emission. It must be used
  // serially as the order of calling getStringOffset matters for
  // reproducibility.
  OffsetsStringPool DebugStrPool(true);
  OffsetsStringPool DebugLineStrPool(false);
  DebugDieValuePool StringOffsetPool;

  // ODR Contexts for the optimize.
  DeclContextTree ODRContexts;

  for (LinkContext &OptContext : ObjectContexts) {
    if (Options.Verbose)
      outs() << "DEBUG MAP OBJECT: " << OptContext.File.FileName << "\n";

    if (!OptContext.File.Dwarf)
      continue;

    if (Options.VerifyInputDWARF)
      verifyInput(OptContext.File);

    // Look for relocations that correspond to address map entries.

    // there was findvalidrelocations previously ... probably we need to gather
    // info here
    if (LLVM_LIKELY(!Options.Update) &&
        !OptContext.File.Addresses->hasValidRelocs()) {
      if (Options.Verbose)
        outs() << "No valid relocations found. Skipping.\n";

      // Set "Skip" flag as a signal to other loops that we should not
      // process this iteration.
      OptContext.Skip = true;
      continue;
    }

    // Setup access to the debug info.
    if (!OptContext.File.Dwarf)
      continue;

    // Check whether type units are presented.
    if (!OptContext.File.Dwarf->types_section_units().empty()) {
      reportWarning("type units are not currently supported: file will "
                    "be skipped",
                    OptContext.File);
      OptContext.Skip = true;
      continue;
    }

    // Clone all the clang modules with requires extracting the DIE units. We
    // don't need the full debug info until the Analyze phase.
    OptContext.CompileUnits.reserve(
        OptContext.File.Dwarf->getNumCompileUnits());
    for (const auto &CU : OptContext.File.Dwarf->compile_units()) {
      auto CUDie = CU->getUnitDIE(/*ExtractUnitDIEOnly=*/true);
      if (Options.Verbose) {
        outs() << "Input compilation unit:";
        DIDumpOptions DumpOpts;
        DumpOpts.ChildRecurseDepth = 0;
        DumpOpts.Verbose = Options.Verbose;
        CUDie.dump(outs(), 0, DumpOpts);
      }
    }

    for (auto &CU : OptContext.ModuleUnits) {
      if (Error Err = cloneModuleUnit(OptContext, CU, ODRContexts, DebugStrPool,
                                      DebugLineStrPool, StringOffsetPool))
        reportWarning(toString(std::move(Err)), CU.File);
    }
  }

  // At this point we know how much data we have emitted. We use this value to
  // compare canonical DIE offsets in analyzeContextInfo to see if a definition
  // is already emitted, without being affected by canonical die offsets set
  // later. This prevents undeterminism when analyze and clone execute
  // concurrently, as clone set the canonical DIE offset and analyze reads it.
  const uint64_t ModulesEndOffset =
      (TheDwarfEmitter == nullptr) ? 0
                                   : TheDwarfEmitter->getDebugInfoSectionSize();

  // These variables manage the list of processed object files.
  // The mutex and condition variable are to ensure that this is thread safe.
  std::mutex ProcessedFilesMutex;
  std::condition_variable ProcessedFilesConditionVariable;
  BitVector ProcessedFiles(NumObjects, false);

  //  Analyzing the context info is particularly expensive so it is executed in
  //  parallel with emitting the previous compile unit.
  auto AnalyzeLambda = [&](size_t I) {
    auto &Context = ObjectContexts[I];

    if (Context.Skip || !Context.File.Dwarf)
      return;

    for (const auto &CU : Context.File.Dwarf->compile_units()) {
      // Previously we only extracted the unit DIEs. We need the full debug info
      // now.
      auto CUDie = CU->getUnitDIE(/*ExtractUnitDIEOnly=*/false);
      std::string PCMFile = getPCMFile(CUDie, Options.ObjectPrefixMap);

      if (!CUDie || LLVM_UNLIKELY(Options.Update) ||
          !isClangModuleRef(CUDie, PCMFile, Context, 0, true).first) {
        Context.CompileUnits.push_back(std::make_unique<CompileUnit>(
            *CU, UniqueUnitID++, !Options.NoODR && !Options.Update, ""));
      }
    }

    // Now build the DIE parent links that we will use during the next phase.
    for (auto &CurrentUnit : Context.CompileUnits) {
      auto CUDie = CurrentUnit->getOrigUnit().getUnitDIE();
      if (!CUDie)
        continue;
      analyzeContextInfo(CurrentUnit->getOrigUnit().getUnitDIE(), 0,
                         *CurrentUnit, &ODRContexts.getRoot(), ODRContexts,
                         ModulesEndOffset, Options.ParseableSwiftInterfaces,
                         [&](const Twine &Warning, const DWARFDie &DIE) {
                           reportWarning(Warning, Context.File, &DIE);
                         });
    }
  };

  // For each object file map how many bytes were emitted.
  StringMap<DebugInfoSize> SizeByObject;

  // And then the remaining work in serial again.
  // Note, although this loop runs in serial, it can run in parallel with
  // the analyzeContextInfo loop so long as we process files with indices >=
  // than those processed by analyzeContextInfo.
  auto CloneLambda = [&](size_t I) {
    auto &OptContext = ObjectContexts[I];
    if (OptContext.Skip || !OptContext.File.Dwarf)
      return;

    // Then mark all the DIEs that need to be present in the generated output
    // and collect some information about them.
    // Note that this loop can not be merged with the previous one because
    // cross-cu references require the ParentIdx to be setup for every CU in
    // the object file before calling this.
    if (LLVM_UNLIKELY(Options.Update)) {
      for (auto &CurrentUnit : OptContext.CompileUnits)
        CurrentUnit->markEverythingAsKept();
      copyInvariantDebugSection(*OptContext.File.Dwarf);
    } else {
      for (auto &CurrentUnit : OptContext.CompileUnits) {
        lookForDIEsToKeep(*OptContext.File.Addresses, OptContext.CompileUnits,
                          CurrentUnit->getOrigUnit().getUnitDIE(),
                          OptContext.File, *CurrentUnit, 0);
#ifndef NDEBUG
        verifyKeepChain(*CurrentUnit);
#endif
      }
    }

    // The calls to applyValidRelocs inside cloneDIE will walk the reloc
    // array again (in the same way findValidRelocsInDebugInfo() did). We
    // need to reset the NextValidReloc index to the beginning.
    if (OptContext.File.Addresses->hasValidRelocs() ||
        LLVM_UNLIKELY(Options.Update)) {
      SizeByObject[OptContext.File.FileName].Input =
          getDebugInfoSize(*OptContext.File.Dwarf);
      SizeByObject[OptContext.File.FileName].Output =
          DIECloner(*this, TheDwarfEmitter, OptContext.File, DIEAlloc,
                    OptContext.CompileUnits, Options.Update, DebugStrPool,
                    DebugLineStrPool, StringOffsetPool)
              .cloneAllCompileUnits(*OptContext.File.Dwarf, OptContext.File,
                                    OptContext.File.Dwarf->isLittleEndian());
    }
    if ((TheDwarfEmitter != nullptr) && !OptContext.CompileUnits.empty() &&
        LLVM_LIKELY(!Options.Update))
      patchFrameInfoForObject(OptContext);

    // Clean-up before starting working on the next object.
    cleanupAuxiliarryData(OptContext);
  };

  auto EmitLambda = [&]() {
    // Emit everything that's global.
    if (TheDwarfEmitter != nullptr) {
      TheDwarfEmitter->emitAbbrevs(Abbreviations, Options.TargetDWARFVersion);
      TheDwarfEmitter->emitStrings(DebugStrPool);
      TheDwarfEmitter->emitStringOffsets(StringOffsetPool.getValues(),
                                         Options.TargetDWARFVersion);
      TheDwarfEmitter->emitLineStrings(DebugLineStrPool);
      for (AccelTableKind TableKind : Options.AccelTables) {
        switch (TableKind) {
        case AccelTableKind::Apple:
          TheDwarfEmitter->emitAppleNamespaces(AppleNamespaces);
          TheDwarfEmitter->emitAppleNames(AppleNames);
          TheDwarfEmitter->emitAppleTypes(AppleTypes);
          TheDwarfEmitter->emitAppleObjc(AppleObjc);
          break;
        case AccelTableKind::Pub:
          // Already emitted by emitAcceleratorEntriesForUnit.
          // Already emitted by emitAcceleratorEntriesForUnit.
          break;
        case AccelTableKind::DebugNames:
          TheDwarfEmitter->emitDebugNames(DebugNames);
          break;
        }
      }
    }
  };

  auto AnalyzeAll = [&]() {
    for (unsigned I = 0, E = NumObjects; I != E; ++I) {
      AnalyzeLambda(I);

      std::unique_lock<std::mutex> LockGuard(ProcessedFilesMutex);
      ProcessedFiles.set(I);
      ProcessedFilesConditionVariable.notify_one();
    }
  };

  auto CloneAll = [&]() {
    for (unsigned I = 0, E = NumObjects; I != E; ++I) {
      {
        std::unique_lock<std::mutex> LockGuard(ProcessedFilesMutex);
        if (!ProcessedFiles[I]) {
          ProcessedFilesConditionVariable.wait(
              LockGuard, [&]() { return ProcessedFiles[I]; });
        }
      }

      CloneLambda(I);
    }
    EmitLambda();
  };

  // To limit memory usage in the single threaded case, analyze and clone are
  // run sequentially so the OptContext is freed after processing each object
  // in endDebugObject.
  if (Options.Threads == 1) {
    for (unsigned I = 0, E = NumObjects; I != E; ++I) {
      AnalyzeLambda(I);
      CloneLambda(I);
    }
    EmitLambda();
  } else {
    DefaultThreadPool Pool(hardware_concurrency(2));
    Pool.async(AnalyzeAll);
    Pool.async(CloneAll);
    Pool.wait();
  }

  if (Options.Statistics) {
    // Create a vector sorted in descending order by output size.
    std::vector<std::pair<StringRef, DebugInfoSize>> Sorted;
    for (auto &E : SizeByObject)
      Sorted.emplace_back(E.first(), E.second);
    llvm::sort(Sorted, [](auto &LHS, auto &RHS) {
      return LHS.second.Output > RHS.second.Output;
    });

    auto ComputePercentange = [](int64_t Input, int64_t Output) -> float {
      const float Difference = Output - Input;
      const float Sum = Input + Output;
      if (Sum == 0)
        return 0;
      return (Difference / (Sum / 2));
    };

    int64_t InputTotal = 0;
    int64_t OutputTotal = 0;
    const char *FormatStr = "{0,-45} {1,10}b  {2,10}b {3,8:P}\n";

    // Print header.
    outs() << ".debug_info section size (in bytes)\n";
    outs() << "----------------------------------------------------------------"
              "---------------\n";
    outs() << "Filename                                           Object       "
              "  dSYM   Change\n";
    outs() << "----------------------------------------------------------------"
              "---------------\n";

    // Print body.
    for (auto &E : Sorted) {
      InputTotal += E.second.Input;
      OutputTotal += E.second.Output;
      llvm::outs() << formatv(
          FormatStr, sys::path::filename(E.first).take_back(45), E.second.Input,
          E.second.Output, ComputePercentange(E.second.Input, E.second.Output));
    }
    // Print total and footer.
    outs() << "----------------------------------------------------------------"
              "---------------\n";
    llvm::outs() << formatv(FormatStr, "Total", InputTotal, OutputTotal,
                            ComputePercentange(InputTotal, OutputTotal));
    outs() << "----------------------------------------------------------------"
              "---------------\n\n";
  }

  return Error::success();
}

Error DWARFLinker::cloneModuleUnit(LinkContext &Context, RefModuleUnit &Unit,
                                   DeclContextTree &ODRContexts,
                                   OffsetsStringPool &DebugStrPool,
                                   OffsetsStringPool &DebugLineStrPool,
                                   DebugDieValuePool &StringOffsetPool,
                                   unsigned Indent) {
  assert(Unit.Unit.get() != nullptr);

  if (!Unit.Unit->getOrigUnit().getUnitDIE().hasChildren())
    return Error::success();

  if (Options.Verbose) {
    outs().indent(Indent);
    outs() << "cloning .debug_info from " << Unit.File.FileName << "\n";
  }

  // Analyze context for the module.
  analyzeContextInfo(Unit.Unit->getOrigUnit().getUnitDIE(), 0, *(Unit.Unit),
                     &ODRContexts.getRoot(), ODRContexts, 0,
                     Options.ParseableSwiftInterfaces,
                     [&](const Twine &Warning, const DWARFDie &DIE) {
                       reportWarning(Warning, Context.File, &DIE);
                     });
  // Keep everything.
  Unit.Unit->markEverythingAsKept();

  // Clone unit.
  UnitListTy CompileUnits;
  CompileUnits.emplace_back(std::move(Unit.Unit));
  assert(TheDwarfEmitter);
  DIECloner(*this, TheDwarfEmitter, Unit.File, DIEAlloc, CompileUnits,
            Options.Update, DebugStrPool, DebugLineStrPool, StringOffsetPool)
      .cloneAllCompileUnits(*Unit.File.Dwarf, Unit.File,
                            Unit.File.Dwarf->isLittleEndian());
  return Error::success();
}

void DWARFLinker::verifyInput(const DWARFFile &File) {
  assert(File.Dwarf);

  std::string Buffer;
  raw_string_ostream OS(Buffer);
  DIDumpOptions DumpOpts;
  if (!File.Dwarf->verify(OS, DumpOpts.noImplicitRecursion())) {
    if (Options.InputVerificationHandler)
      Options.InputVerificationHandler(File, OS.str());
  }
}

} // namespace llvm
