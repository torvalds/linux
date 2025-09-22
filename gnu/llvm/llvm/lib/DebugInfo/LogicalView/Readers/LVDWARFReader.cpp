//===-- LVDWARFReader.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This implements the LVDWARFReader class.
// It supports ELF, Mach-O and Wasm binary formats.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/LogicalView/Readers/LVDWARFReader.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugLoc.h"
#include "llvm/DebugInfo/DWARF/DWARFExpression.h"
#include "llvm/DebugInfo/LogicalView/Core/LVLine.h"
#include "llvm/DebugInfo/LogicalView/Core/LVScope.h"
#include "llvm/DebugInfo/LogicalView/Core/LVSymbol.h"
#include "llvm/DebugInfo/LogicalView/Core/LVType.h"
#include "llvm/Object/Error.h"
#include "llvm/Object/MachO.h"
#include "llvm/Support/FormatVariadic.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::logicalview;

#define DEBUG_TYPE "DWARFReader"

LVElement *LVDWARFReader::createElement(dwarf::Tag Tag) {
  CurrentScope = nullptr;
  CurrentSymbol = nullptr;
  CurrentType = nullptr;
  CurrentRanges.clear();

  if (!options().getPrintSymbols()) {
    switch (Tag) {
    // As the command line options did not specify a request to print
    // logical symbols (--print=symbols or --print=all or --print=elements),
    // skip its creation.
    case dwarf::DW_TAG_formal_parameter:
    case dwarf::DW_TAG_unspecified_parameters:
    case dwarf::DW_TAG_member:
    case dwarf::DW_TAG_variable:
    case dwarf::DW_TAG_inheritance:
    case dwarf::DW_TAG_constant:
    case dwarf::DW_TAG_call_site_parameter:
    case dwarf::DW_TAG_GNU_call_site_parameter:
      return nullptr;
    default:
      break;
    }
  }

  switch (Tag) {
  // Types.
  case dwarf::DW_TAG_base_type:
    CurrentType = createType();
    CurrentType->setIsBase();
    if (options().getAttributeBase())
      CurrentType->setIncludeInPrint();
    return CurrentType;
  case dwarf::DW_TAG_const_type:
    CurrentType = createType();
    CurrentType->setIsConst();
    CurrentType->setName("const");
    return CurrentType;
  case dwarf::DW_TAG_enumerator:
    CurrentType = createTypeEnumerator();
    return CurrentType;
  case dwarf::DW_TAG_imported_declaration:
    CurrentType = createTypeImport();
    CurrentType->setIsImportDeclaration();
    return CurrentType;
  case dwarf::DW_TAG_imported_module:
    CurrentType = createTypeImport();
    CurrentType->setIsImportModule();
    return CurrentType;
  case dwarf::DW_TAG_pointer_type:
    CurrentType = createType();
    CurrentType->setIsPointer();
    CurrentType->setName("*");
    return CurrentType;
  case dwarf::DW_TAG_ptr_to_member_type:
    CurrentType = createType();
    CurrentType->setIsPointerMember();
    CurrentType->setName("*");
    return CurrentType;
  case dwarf::DW_TAG_reference_type:
    CurrentType = createType();
    CurrentType->setIsReference();
    CurrentType->setName("&");
    return CurrentType;
  case dwarf::DW_TAG_restrict_type:
    CurrentType = createType();
    CurrentType->setIsRestrict();
    CurrentType->setName("restrict");
    return CurrentType;
  case dwarf::DW_TAG_rvalue_reference_type:
    CurrentType = createType();
    CurrentType->setIsRvalueReference();
    CurrentType->setName("&&");
    return CurrentType;
  case dwarf::DW_TAG_subrange_type:
    CurrentType = createTypeSubrange();
    return CurrentType;
  case dwarf::DW_TAG_template_value_parameter:
    CurrentType = createTypeParam();
    CurrentType->setIsTemplateValueParam();
    return CurrentType;
  case dwarf::DW_TAG_template_type_parameter:
    CurrentType = createTypeParam();
    CurrentType->setIsTemplateTypeParam();
    return CurrentType;
  case dwarf::DW_TAG_GNU_template_template_param:
    CurrentType = createTypeParam();
    CurrentType->setIsTemplateTemplateParam();
    return CurrentType;
  case dwarf::DW_TAG_typedef:
    CurrentType = createTypeDefinition();
    return CurrentType;
  case dwarf::DW_TAG_unspecified_type:
    CurrentType = createType();
    CurrentType->setIsUnspecified();
    return CurrentType;
  case dwarf::DW_TAG_volatile_type:
    CurrentType = createType();
    CurrentType->setIsVolatile();
    CurrentType->setName("volatile");
    return CurrentType;

  // Symbols.
  case dwarf::DW_TAG_formal_parameter:
    CurrentSymbol = createSymbol();
    CurrentSymbol->setIsParameter();
    return CurrentSymbol;
  case dwarf::DW_TAG_unspecified_parameters:
    CurrentSymbol = createSymbol();
    CurrentSymbol->setIsUnspecified();
    CurrentSymbol->setName("...");
    return CurrentSymbol;
  case dwarf::DW_TAG_member:
    CurrentSymbol = createSymbol();
    CurrentSymbol->setIsMember();
    return CurrentSymbol;
  case dwarf::DW_TAG_variable:
    CurrentSymbol = createSymbol();
    CurrentSymbol->setIsVariable();
    return CurrentSymbol;
  case dwarf::DW_TAG_inheritance:
    CurrentSymbol = createSymbol();
    CurrentSymbol->setIsInheritance();
    return CurrentSymbol;
  case dwarf::DW_TAG_call_site_parameter:
  case dwarf::DW_TAG_GNU_call_site_parameter:
    CurrentSymbol = createSymbol();
    CurrentSymbol->setIsCallSiteParameter();
    return CurrentSymbol;
  case dwarf::DW_TAG_constant:
    CurrentSymbol = createSymbol();
    CurrentSymbol->setIsConstant();
    return CurrentSymbol;

  // Scopes.
  case dwarf::DW_TAG_catch_block:
    CurrentScope = createScope();
    CurrentScope->setIsCatchBlock();
    return CurrentScope;
  case dwarf::DW_TAG_lexical_block:
    CurrentScope = createScope();
    CurrentScope->setIsLexicalBlock();
    return CurrentScope;
  case dwarf::DW_TAG_try_block:
    CurrentScope = createScope();
    CurrentScope->setIsTryBlock();
    return CurrentScope;
  case dwarf::DW_TAG_compile_unit:
  case dwarf::DW_TAG_skeleton_unit:
    CurrentScope = createScopeCompileUnit();
    CompileUnit = static_cast<LVScopeCompileUnit *>(CurrentScope);
    return CurrentScope;
  case dwarf::DW_TAG_inlined_subroutine:
    CurrentScope = createScopeFunctionInlined();
    return CurrentScope;
  case dwarf::DW_TAG_namespace:
    CurrentScope = createScopeNamespace();
    return CurrentScope;
  case dwarf::DW_TAG_template_alias:
    CurrentScope = createScopeAlias();
    return CurrentScope;
  case dwarf::DW_TAG_array_type:
    CurrentScope = createScopeArray();
    return CurrentScope;
  case dwarf::DW_TAG_call_site:
  case dwarf::DW_TAG_GNU_call_site:
    CurrentScope = createScopeFunction();
    CurrentScope->setIsCallSite();
    return CurrentScope;
  case dwarf::DW_TAG_entry_point:
    CurrentScope = createScopeFunction();
    CurrentScope->setIsEntryPoint();
    return CurrentScope;
  case dwarf::DW_TAG_subprogram:
    CurrentScope = createScopeFunction();
    CurrentScope->setIsSubprogram();
    return CurrentScope;
  case dwarf::DW_TAG_subroutine_type:
    CurrentScope = createScopeFunctionType();
    return CurrentScope;
  case dwarf::DW_TAG_label:
    CurrentScope = createScopeFunction();
    CurrentScope->setIsLabel();
    return CurrentScope;
  case dwarf::DW_TAG_class_type:
    CurrentScope = createScopeAggregate();
    CurrentScope->setIsClass();
    return CurrentScope;
  case dwarf::DW_TAG_structure_type:
    CurrentScope = createScopeAggregate();
    CurrentScope->setIsStructure();
    return CurrentScope;
  case dwarf::DW_TAG_union_type:
    CurrentScope = createScopeAggregate();
    CurrentScope->setIsUnion();
    return CurrentScope;
  case dwarf::DW_TAG_enumeration_type:
    CurrentScope = createScopeEnumeration();
    return CurrentScope;
  case dwarf::DW_TAG_GNU_formal_parameter_pack:
    CurrentScope = createScopeFormalPack();
    return CurrentScope;
  case dwarf::DW_TAG_GNU_template_parameter_pack:
    CurrentScope = createScopeTemplatePack();
    return CurrentScope;
  default:
    // Collect TAGs not implemented.
    if (options().getInternalTag() && Tag)
      CompileUnit->addDebugTag(Tag, CurrentOffset);
    break;
  }
  return nullptr;
}

void LVDWARFReader::processOneAttribute(const DWARFDie &Die,
                                        LVOffset *OffsetPtr,
                                        const AttributeSpec &AttrSpec) {
  uint64_t OffsetOnEntry = *OffsetPtr;
  DWARFUnit *U = Die.getDwarfUnit();
  const DWARFFormValue &FormValue =
      DWARFFormValue::createFromUnit(AttrSpec.Form, U, OffsetPtr);

  // We are processing .debug_info section, implicit_const attribute
  // values are not really stored here, but in .debug_abbrev section.
  auto GetAsUnsignedConstant = [&]() -> int64_t {
    return AttrSpec.isImplicitConst() ? AttrSpec.getImplicitConstValue()
                                      : *FormValue.getAsUnsignedConstant();
  };

  auto GetFlag = [](const DWARFFormValue &FormValue) -> bool {
    return FormValue.isFormClass(DWARFFormValue::FC_Flag);
  };

  auto GetBoundValue = [](const DWARFFormValue &FormValue) -> int64_t {
    switch (FormValue.getForm()) {
    case dwarf::DW_FORM_ref_addr:
    case dwarf::DW_FORM_ref1:
    case dwarf::DW_FORM_ref2:
    case dwarf::DW_FORM_ref4:
    case dwarf::DW_FORM_ref8:
    case dwarf::DW_FORM_ref_udata:
    case dwarf::DW_FORM_ref_sig8:
      return *FormValue.getAsReferenceUVal();
    case dwarf::DW_FORM_data1:
    case dwarf::DW_FORM_flag:
    case dwarf::DW_FORM_data2:
    case dwarf::DW_FORM_data4:
    case dwarf::DW_FORM_data8:
    case dwarf::DW_FORM_udata:
    case dwarf::DW_FORM_ref_sup4:
    case dwarf::DW_FORM_ref_sup8:
      return *FormValue.getAsUnsignedConstant();
    case dwarf::DW_FORM_sdata:
      return *FormValue.getAsSignedConstant();
    default:
      return 0;
    }
  };

  LLVM_DEBUG({
    dbgs() << "     " << hexValue(OffsetOnEntry)
           << formatv(" {0}", AttrSpec.Attr) << "\n";
  });

  switch (AttrSpec.Attr) {
  case dwarf::DW_AT_accessibility:
    CurrentElement->setAccessibilityCode(*FormValue.getAsUnsignedConstant());
    break;
  case dwarf::DW_AT_artificial:
    CurrentElement->setIsArtificial();
    break;
  case dwarf::DW_AT_bit_size:
    CurrentElement->setBitSize(*FormValue.getAsUnsignedConstant());
    break;
  case dwarf::DW_AT_call_file:
    CurrentElement->setCallFilenameIndex(GetAsUnsignedConstant());
    break;
  case dwarf::DW_AT_call_line:
    CurrentElement->setCallLineNumber(IncrementFileIndex
                                          ? GetAsUnsignedConstant() + 1
                                          : GetAsUnsignedConstant());
    break;
  case dwarf::DW_AT_comp_dir:
    CompileUnit->setCompilationDirectory(dwarf::toStringRef(FormValue));
    break;
  case dwarf::DW_AT_const_value:
    if (FormValue.isFormClass(DWARFFormValue::FC_Block)) {
      ArrayRef<uint8_t> Expr = *FormValue.getAsBlock();
      // Store the expression as a hexadecimal string.
      CurrentElement->setValue(
          llvm::toHex(llvm::toStringRef(Expr), /*LowerCase=*/true));
    } else if (FormValue.isFormClass(DWARFFormValue::FC_Constant)) {
      // In the case of negative values, generate the string representation
      // for a positive value prefixed with the negative sign.
      if (FormValue.getForm() == dwarf::DW_FORM_sdata) {
        std::stringstream Stream;
        int64_t Value = *FormValue.getAsSignedConstant();
        if (Value < 0) {
          Stream << "-";
          Value = std::abs(Value);
        }
        Stream << hexString(Value, 2);
        CurrentElement->setValue(Stream.str());
      } else
        CurrentElement->setValue(
            hexString(*FormValue.getAsUnsignedConstant(), 2));
    } else
      CurrentElement->setValue(dwarf::toStringRef(FormValue));
    break;
  case dwarf::DW_AT_count:
    CurrentElement->setCount(*FormValue.getAsUnsignedConstant());
    break;
  case dwarf::DW_AT_decl_line:
    CurrentElement->setLineNumber(GetAsUnsignedConstant());
    break;
  case dwarf::DW_AT_decl_file:
    CurrentElement->setFilenameIndex(IncrementFileIndex
                                         ? GetAsUnsignedConstant() + 1
                                         : GetAsUnsignedConstant());
    break;
  case dwarf::DW_AT_enum_class:
    if (GetFlag(FormValue))
      CurrentElement->setIsEnumClass();
    break;
  case dwarf::DW_AT_external:
    if (GetFlag(FormValue))
      CurrentElement->setIsExternal();
    break;
  case dwarf::DW_AT_GNU_discriminator:
    CurrentElement->setDiscriminator(*FormValue.getAsUnsignedConstant());
    break;
  case dwarf::DW_AT_inline:
    CurrentElement->setInlineCode(*FormValue.getAsUnsignedConstant());
    break;
  case dwarf::DW_AT_lower_bound:
    CurrentElement->setLowerBound(GetBoundValue(FormValue));
    break;
  case dwarf::DW_AT_name:
    CurrentElement->setName(dwarf::toStringRef(FormValue));
    break;
  case dwarf::DW_AT_linkage_name:
  case dwarf::DW_AT_MIPS_linkage_name:
    CurrentElement->setLinkageName(dwarf::toStringRef(FormValue));
    break;
  case dwarf::DW_AT_producer:
    if (options().getAttributeProducer())
      CurrentElement->setProducer(dwarf::toStringRef(FormValue));
    break;
  case dwarf::DW_AT_upper_bound:
    CurrentElement->setUpperBound(GetBoundValue(FormValue));
    break;
  case dwarf::DW_AT_virtuality:
    CurrentElement->setVirtualityCode(*FormValue.getAsUnsignedConstant());
    break;

  case dwarf::DW_AT_abstract_origin:
  case dwarf::DW_AT_call_origin:
  case dwarf::DW_AT_extension:
  case dwarf::DW_AT_import:
  case dwarf::DW_AT_specification:
  case dwarf::DW_AT_type:
    updateReference(AttrSpec.Attr, FormValue);
    break;

  case dwarf::DW_AT_low_pc:
    if (options().getGeneralCollectRanges()) {
      FoundLowPC = true;
      // For toolchains that support the removal of unused code, the linker
      // marks functions that have been removed, by setting the value for the
      // low_pc to the max address.
      if (std::optional<uint64_t> Value = FormValue.getAsAddress()) {
        CurrentLowPC = *Value;
      } else {
        uint64_t UValue = FormValue.getRawUValue();
        if (U->getAddrOffsetSectionItem(UValue)) {
          CurrentLowPC = *FormValue.getAsAddress();
        } else {
          FoundLowPC = false;
          // We are dealing with an index into the .debug_addr section.
          LLVM_DEBUG({
            dbgs() << format("indexed (%8.8x) address = ", (uint32_t)UValue);
          });
        }
      }
      if (FoundLowPC) {
        if (CurrentLowPC == MaxAddress)
          CurrentElement->setIsDiscarded();
        // Consider the case of WebAssembly.
        CurrentLowPC += WasmCodeSectionOffset;
        if (CurrentElement->isCompileUnit())
          setCUBaseAddress(CurrentLowPC);
      }
    }
    break;

  case dwarf::DW_AT_high_pc:
    if (options().getGeneralCollectRanges()) {
      FoundHighPC = true;
      if (std::optional<uint64_t> Address = FormValue.getAsAddress())
        // High PC is an address.
        CurrentHighPC = *Address;
      if (std::optional<uint64_t> Offset = FormValue.getAsUnsignedConstant())
        // High PC is an offset from LowPC.
        // Don't add the WebAssembly offset if we have seen a DW_AT_low_pc, as
        // the CurrentLowPC has already that offset added. Basically, use the
        // original DW_AT_loc_pc value.
        CurrentHighPC =
            (FoundLowPC ? CurrentLowPC - WasmCodeSectionOffset : CurrentLowPC) +
            *Offset;
      // Store the real upper limit for the address range.
      if (UpdateHighAddress && CurrentHighPC > 0)
        --CurrentHighPC;
      // Consider the case of WebAssembly.
      CurrentHighPC += WasmCodeSectionOffset;
      if (CurrentElement->isCompileUnit())
        setCUHighAddress(CurrentHighPC);
    }
    break;

  case dwarf::DW_AT_ranges:
    if (RangesDataAvailable && options().getGeneralCollectRanges()) {
      auto GetRanges = [](const DWARFFormValue &FormValue,
                          DWARFUnit *U) -> Expected<DWARFAddressRangesVector> {
        if (FormValue.getForm() == dwarf::DW_FORM_rnglistx)
          return U->findRnglistFromIndex(*FormValue.getAsSectionOffset());
        return U->findRnglistFromOffset(*FormValue.getAsSectionOffset());
      };
      Expected<DWARFAddressRangesVector> RangesOrError =
          GetRanges(FormValue, U);
      if (!RangesOrError) {
        LLVM_DEBUG({
          std::string TheError(toString(RangesOrError.takeError()));
          dbgs() << format("error decoding address ranges = ",
                           TheError.c_str());
        });
        consumeError(RangesOrError.takeError());
        break;
      }
      // The address ranges are absolute. There is no need to add any addend.
      DWARFAddressRangesVector Ranges = RangesOrError.get();
      for (DWARFAddressRange &Range : Ranges) {
        // This seems to be a tombstone for empty ranges.
        if (Range.LowPC == Range.HighPC)
          continue;
        // Store the real upper limit for the address range.
        if (UpdateHighAddress && Range.HighPC > 0)
          --Range.HighPC;
        // Consider the case of WebAssembly.
        Range.LowPC += WasmCodeSectionOffset;
        Range.HighPC += WasmCodeSectionOffset;
        // Add the pair of addresses.
        CurrentScope->addObject(Range.LowPC, Range.HighPC);
        // If the scope is the CU, do not update the ranges set.
        if (!CurrentElement->isCompileUnit())
          CurrentRanges.emplace_back(Range.LowPC, Range.HighPC);
      }
    }
    break;

  // Get the location list for the symbol.
  case dwarf::DW_AT_data_member_location:
    if (options().getAttributeAnyLocation())
      processLocationMember(AttrSpec.Attr, FormValue, Die, OffsetOnEntry);
    break;

  // Get the location list for the symbol.
  case dwarf::DW_AT_location:
  case dwarf::DW_AT_string_length:
  case dwarf::DW_AT_use_location:
    if (options().getAttributeAnyLocation() && CurrentSymbol)
      processLocationList(AttrSpec.Attr, FormValue, Die, OffsetOnEntry);
    break;

  case dwarf::DW_AT_call_data_value:
  case dwarf::DW_AT_call_value:
  case dwarf::DW_AT_GNU_call_site_data_value:
  case dwarf::DW_AT_GNU_call_site_value:
    if (options().getAttributeAnyLocation() && CurrentSymbol)
      processLocationList(AttrSpec.Attr, FormValue, Die, OffsetOnEntry,
                          /*CallSiteLocation=*/true);
    break;

  default:
    break;
  }
}

LVScope *LVDWARFReader::processOneDie(const DWARFDie &InputDIE, LVScope *Parent,
                                      DWARFDie &SkeletonDie) {
  // If the input DIE corresponds to the compile unit, it can be:
  // a) Simple DWARF: a standard DIE. Ignore the skeleton DIE (is empty).
  // b) Split DWARF: the DIE for the split DWARF. The skeleton is the DIE
  //    for the skeleton DWARF. Process both DIEs.
  const DWARFDie &DIE = SkeletonDie.isValid() ? SkeletonDie : InputDIE;
  DWARFDataExtractor DebugInfoData =
      DIE.getDwarfUnit()->getDebugInfoExtractor();
  LVOffset Offset = DIE.getOffset();

  // Reset values for the current DIE.
  CurrentLowPC = 0;
  CurrentHighPC = 0;
  CurrentOffset = Offset;
  CurrentEndOffset = 0;
  FoundLowPC = false;
  FoundHighPC = false;

  // Process supported attributes.
  if (DebugInfoData.isValidOffset(Offset)) {

    LLVM_DEBUG({
      dbgs() << "DIE: " << hexValue(Offset) << formatv(" {0}", DIE.getTag())
             << "\n";
    });

    // Create the logical view element for the current DIE.
    dwarf::Tag Tag = DIE.getTag();
    CurrentElement = createElement(Tag);
    if (!CurrentElement)
      return CurrentScope;

    CurrentElement->setTag(Tag);
    CurrentElement->setOffset(Offset);

    if (options().getAttributeAnySource() && CurrentElement->isCompileUnit())
      addCompileUnitOffset(Offset,
                           static_cast<LVScopeCompileUnit *>(CurrentElement));

    // Insert the newly created element into the element symbol table. If the
    // element is in the list, it means there are previously created elements
    // referencing this element.
    if (ElementTable.find(Offset) == ElementTable.end()) {
      // No previous references to this offset.
      ElementTable.emplace(std::piecewise_construct,
                           std::forward_as_tuple(Offset),
                           std::forward_as_tuple(CurrentElement));
    } else {
      // There are previous references to this element. We need to update the
      // element and all the references pointing to this element.
      LVElementEntry &Reference = ElementTable[Offset];
      Reference.Element = CurrentElement;
      // Traverse the element set and update the elements (backtracking).
      for (LVElement *Target : Reference.References)
        Target->setReference(CurrentElement);
      for (LVElement *Target : Reference.Types)
        Target->setType(CurrentElement);
      // Clear the pending elements.
      Reference.References.clear();
      Reference.Types.clear();
    }

    // Add the current element to its parent as there are attributes
    // (locations) that require the scope level.
    if (CurrentScope)
      Parent->addElement(CurrentScope);
    else if (CurrentSymbol)
      Parent->addElement(CurrentSymbol);
    else if (CurrentType)
      Parent->addElement(CurrentType);

    // Process the attributes for the given DIE.
    auto ProcessAttributes = [&](const DWARFDie &TheDIE,
                                 DWARFDataExtractor &DebugData) {
      CurrentEndOffset = Offset;
      uint32_t abbrCode = DebugData.getULEB128(&CurrentEndOffset);
      if (abbrCode) {
        if (const DWARFAbbreviationDeclaration *AbbrevDecl =
                TheDIE.getAbbreviationDeclarationPtr())
          if (AbbrevDecl)
            for (const DWARFAbbreviationDeclaration::AttributeSpec &AttrSpec :
                 AbbrevDecl->attributes())
              processOneAttribute(TheDIE, &CurrentEndOffset, AttrSpec);
      }
    };

    ProcessAttributes(DIE, DebugInfoData);

    // If the input DIE is for a compile unit, process its attributes in
    // the case of split DWARF, to override any common attribute values.
    if (SkeletonDie.isValid()) {
      DWARFDataExtractor DebugInfoData =
          InputDIE.getDwarfUnit()->getDebugInfoExtractor();
      LVOffset Offset = InputDIE.getOffset();
      if (DebugInfoData.isValidOffset(Offset))
        ProcessAttributes(InputDIE, DebugInfoData);
    }
  }

  if (CurrentScope) {
    if (CurrentScope->getCanHaveRanges()) {
      // If the scope has ranges, they are already added to the scope.
      // Add any collected LowPC/HighPC values.
      bool IsCompileUnit = CurrentScope->getIsCompileUnit();
      if (FoundLowPC && FoundHighPC) {
        CurrentScope->addObject(CurrentLowPC, CurrentHighPC);
        if (!IsCompileUnit) {
          // If the scope is a function, add it to the public names.
          if ((options().getAttributePublics() ||
               options().getPrintAnyLine()) &&
              CurrentScope->getIsFunction() &&
              !CurrentScope->getIsInlinedFunction())
            CompileUnit->addPublicName(CurrentScope, CurrentLowPC,
                                       CurrentHighPC);
        }
      }

      // Look for scopes with ranges and no linkage name information that
      // are referencing another scopes via DW_AT_specification. They are
      // possible candidates for a comdat scope.
      if (CurrentScope->getHasRanges() &&
          !CurrentScope->getLinkageNameIndex() &&
          CurrentScope->getHasReferenceSpecification()) {
        // Get the linkage name in order to search for a possible comdat.
        std::optional<DWARFFormValue> LinkageDIE =
            DIE.findRecursively(dwarf::DW_AT_linkage_name);
        if (LinkageDIE.has_value()) {
          StringRef Name(dwarf::toStringRef(LinkageDIE));
          if (!Name.empty())
            CurrentScope->setLinkageName(Name);
        }
      }

      // If the current scope is in the 'LinkageNames' table, update its
      // logical scope. For other scopes, always we will assume the default
      // ".text" section index.
      LVSectionIndex SectionIndex = updateSymbolTable(CurrentScope);
      if (CurrentScope->getIsComdat())
        CompileUnit->setHasComdatScopes();

      // Update section index contained ranges.
      if (SectionIndex) {
        if (!CurrentRanges.empty()) {
          for (LVAddressRange &Range : CurrentRanges)
            addSectionRange(SectionIndex, CurrentScope, Range.first,
                            Range.second);
          CurrentRanges.clear();
        }
        // If the scope is the CU, do not update the ranges set.
        if (FoundLowPC && FoundHighPC && !IsCompileUnit) {
          addSectionRange(SectionIndex, CurrentScope, CurrentLowPC,
                          CurrentHighPC);
        }
      }
    }
    // Mark member functions.
    if (Parent->getIsAggregate())
      CurrentScope->setIsMember();
  }

  // Keep track of symbols with locations.
  if (options().getAttributeAnyLocation() && CurrentSymbol &&
      CurrentSymbol->getHasLocation())
    SymbolsWithLocations.push_back(CurrentSymbol);

  // If we have template parameters, mark the parent as template.
  if (CurrentType && CurrentType->getIsTemplateParam())
    Parent->setIsTemplate();

  return CurrentScope;
}

void LVDWARFReader::traverseDieAndChildren(DWARFDie &DIE, LVScope *Parent,
                                           DWARFDie &SkeletonDie) {
  // Process the current DIE.
  LVScope *Scope = processOneDie(DIE, Parent, SkeletonDie);
  if (Scope) {
    LVOffset Lower = DIE.getOffset();
    LVOffset Upper = CurrentEndOffset;
    DWARFDie DummyDie;
    // Traverse the children chain.
    DWARFDie Child = DIE.getFirstChild();
    while (Child) {
      traverseDieAndChildren(Child, Scope, DummyDie);
      Upper = Child.getOffset();
      Child = Child.getSibling();
    }
    // Calculate contributions to the debug info section.
    if (options().getPrintSizes() && Upper)
      CompileUnit->addSize(Scope, Lower, Upper);
  }
}

void LVDWARFReader::processLocationGaps() {
  if (options().getAttributeAnyLocation())
    for (LVSymbol *Symbol : SymbolsWithLocations)
      Symbol->fillLocationGaps();
}

void LVDWARFReader::createLineAndFileRecords(
    const DWARFDebugLine::LineTable *Lines) {
  if (!Lines)
    return;

  // Get the source filenames.
  if (!Lines->Prologue.FileNames.empty())
    for (const DWARFDebugLine::FileNameEntry &Entry :
         Lines->Prologue.FileNames) {
      std::string Directory;
      if (Lines->getDirectoryForEntry(Entry, Directory))
        Directory = transformPath(Directory);
      if (Directory.empty())
        Directory = std::string(CompileUnit->getCompilationDirectory());
      std::string File = transformPath(dwarf::toStringRef(Entry.Name));
      std::string String;
      raw_string_ostream(String) << Directory << "/" << File;
      CompileUnit->addFilename(String);
    }

  // In DWARF5 the file indexes start at 0;
  bool IncrementIndex = Lines->Prologue.getVersion() >= 5;

  // Get the source lines if requested by command line option.
  if (options().getPrintLines() && Lines->Rows.size())
    for (const DWARFDebugLine::Row &Row : Lines->Rows) {
      // Here we collect logical debug lines in CULines. Later on,
      // the 'processLines()' function will move each created logical line
      // to its enclosing logical scope, using the debug ranges information
      // and they will be released when its scope parent is deleted.
      LVLineDebug *Line = createLineDebug();
      CULines.push_back(Line);
      // Consider the case of WebAssembly.
      Line->setAddress(Row.Address.Address + WasmCodeSectionOffset);
      Line->setFilename(
          CompileUnit->getFilename(IncrementIndex ? Row.File + 1 : Row.File));
      Line->setLineNumber(Row.Line);
      if (Row.Discriminator)
        Line->setDiscriminator(Row.Discriminator);
      if (Row.IsStmt)
        Line->setIsNewStatement();
      if (Row.BasicBlock)
        Line->setIsBasicBlock();
      if (Row.EndSequence)
        Line->setIsEndSequence();
      if (Row.EpilogueBegin)
        Line->setIsEpilogueBegin();
      if (Row.PrologueEnd)
        Line->setIsPrologueEnd();
      LLVM_DEBUG({
        dbgs() << "Address: " << hexValue(Line->getAddress())
               << " Line: " << Line->lineNumberAsString(/*ShowZero=*/true)
               << "\n";
      });
    }
}

std::string LVDWARFReader::getRegisterName(LVSmall Opcode,
                                           ArrayRef<uint64_t> Operands) {
  // The 'prettyPrintRegisterOp' function uses the DWARFUnit to support
  // DW_OP_regval_type. At this point we are operating on a logical view
  // item, with no access to the underlying DWARF data used by LLVM.
  // We do not support DW_OP_regval_type here.
  if (Opcode == dwarf::DW_OP_regval_type)
    return {};

  std::string string;
  raw_string_ostream Stream(string);
  DIDumpOptions DumpOpts;
  auto *MCRegInfo = MRI.get();
  auto GetRegName = [&MCRegInfo](uint64_t DwarfRegNum, bool IsEH) -> StringRef {
    if (!MCRegInfo)
      return {};
    if (std::optional<unsigned> LLVMRegNum =
            MCRegInfo->getLLVMRegNum(DwarfRegNum, IsEH))
      if (const char *RegName = MCRegInfo->getName(*LLVMRegNum))
        return StringRef(RegName);
    return {};
  };
  DumpOpts.GetNameForDWARFReg = GetRegName;
  DWARFExpression::prettyPrintRegisterOp(/*U=*/nullptr, Stream, DumpOpts,
                                         Opcode, Operands);
  return Stream.str();
}

Error LVDWARFReader::createScopes() {
  LLVM_DEBUG({
    W.startLine() << "\n";
    W.printString("File", Obj.getFileName().str());
    W.printString("Format", FileFormatName);
  });

  if (Error Err = LVReader::createScopes())
    return Err;

  // As the DwarfContext object is valid only during the scopes creation,
  // we need to create our own Target information, to be used during the
  // logical view printing, in the case of instructions being requested.
  std::unique_ptr<DWARFContext> DwarfContext = DWARFContext::create(Obj);
  if (!DwarfContext)
    return createStringError(errc::invalid_argument,
                             "Could not create DWARF information: %s",
                             getFilename().str().c_str());

  if (Error Err = loadTargetInfo(Obj))
    return Err;

  // Create a mapping for virtual addresses.
  mapVirtualAddress(Obj);

  // Select the correct compile unit range, depending if we are dealing with
  // a standard or split DWARF object.
  DWARFContext::compile_unit_range CompileUnits =
      DwarfContext->getNumCompileUnits() ? DwarfContext->compile_units()
                                         : DwarfContext->dwo_compile_units();
  for (const std::unique_ptr<DWARFUnit> &CU : CompileUnits) {

    // Deduction of index used for the line records.
    //
    // For the following test case: test.cpp
    //  void foo(void ParamPtr) { }

    // Both GCC and Clang generate DWARF-5 .debug_line layout.

    // * GCC (GNU C++17 11.3.0) - All DW_AT_decl_file use index 1.
    //
    //   .debug_info:
    //     format = DWARF32, version = 0x0005
    //     DW_TAG_compile_unit
    //       DW_AT_name	("test.cpp")
    //       DW_TAG_subprogram ("foo")
    //         DW_AT_decl_file (1)
    //         DW_TAG_formal_parameter ("ParamPtr")
    //           DW_AT_decl_file (1)
    //   .debug_line:
    //     Line table prologue: format (DWARF32), version (5)
    //     include_directories[0] = "..."
    //     file_names[0]: name ("test.cpp"), dir_index (0)
    //     file_names[1]: name ("test.cpp"), dir_index (0)

    // * Clang (14.0.6) - All DW_AT_decl_file use index 0.
    //
    //   .debug_info:
    //     format = DWARF32, version = 0x0005
    //     DW_AT_producer	("clang version 14.0.6")
    //     DW_AT_name	("test.cpp")
    //
    //     DW_TAG_subprogram ("foo")
    //       DW_AT_decl_file (0)
    //       DW_TAG_formal_parameter ("ParamPtr")
    //         DW_AT_decl_file (0)
    //   .debug_line:
    //     Line table prologue: format (DWARF32), version (5)
    //     include_directories[0] = "..."
    //     file_names[0]: name ("test.cpp"), dir_index (0)

    // From DWARFDebugLine::getFileNameByIndex documentation:
    //   In Dwarf 4, the files are 1-indexed.
    //   In Dwarf 5, the files are 0-indexed.
    // Additional discussions here:
    // https://www.mail-archive.com/dwarf-discuss@lists.dwarfstd.org/msg00883.html

    // The DWARF reader is expecting the files are 1-indexed, so using
    // the .debug_line header information decide if the indexed require
    // an internal adjustment.

    // For the case of GCC (DWARF5), if the entries[0] and [1] are the
    // same, do not perform any adjustment.
    auto DeduceIncrementFileIndex = [&]() -> bool {
      if (CU->getVersion() < 5)
        // DWARF-4 or earlier -> Don't increment index.
        return false;

      if (const DWARFDebugLine::LineTable *LT =
              CU->getContext().getLineTableForUnit(CU.get())) {
        // Check if there are at least 2 entries and if they are the same.
        if (LT->hasFileAtIndex(0) && LT->hasFileAtIndex(1)) {
          const DWARFDebugLine::FileNameEntry &EntryZero =
              LT->Prologue.getFileNameEntry(0);
          const DWARFDebugLine::FileNameEntry &EntryOne =
              LT->Prologue.getFileNameEntry(1);
          // Check directory indexes.
          if (EntryZero.DirIdx != EntryOne.DirIdx)
            // DWARF-5 -> Increment index.
            return true;
          // Check filename.
          std::string FileZero;
          std::string FileOne;
          StringRef None;
          LT->getFileNameByIndex(
              0, None, DILineInfoSpecifier::FileLineInfoKind::RawValue,
              FileZero);
          LT->getFileNameByIndex(
              1, None, DILineInfoSpecifier::FileLineInfoKind::RawValue,
              FileOne);
          return FileZero.compare(FileOne);
        }
      }

      // DWARF-5 -> Increment index.
      return true;
    };
    // The DWARF reader expects the indexes as 1-indexed.
    IncrementFileIndex = DeduceIncrementFileIndex();

    DWARFDie UnitDie = CU->getUnitDIE();
    SmallString<16> DWOAlternativeLocation;
    if (UnitDie) {
      std::optional<const char *> DWOFileName =
          CU->getVersion() >= 5
              ? dwarf::toString(UnitDie.find(dwarf::DW_AT_dwo_name))
              : dwarf::toString(UnitDie.find(dwarf::DW_AT_GNU_dwo_name));
      StringRef From(DWOFileName.value_or(""));
      DWOAlternativeLocation = createAlternativePath(From);
    }

    // The current CU can be a normal compile unit (standard) or a skeleton
    // compile unit (split). For both cases, the returned die, will be used
    // to create the logical scopes.
    DWARFDie CUDie = CU->getNonSkeletonUnitDIE(
        /*ExtractUnitDIEOnly=*/false,
        /*DWOAlternativeLocation=*/DWOAlternativeLocation);
    if (!CUDie.isValid())
      continue;

    // The current unit corresponds to the .dwo file. We need to get the
    // skeleton unit and query for any ranges that will enclose any ranges
    // in the non-skeleton unit.
    DWARFDie DummyDie;
    DWARFDie SkeletonDie =
        CUDie.getDwarfUnit()->isDWOUnit() ? CU->getUnitDIE(false) : DummyDie;
    // Disable the ranges processing if we have just a single .dwo object,
    // as any DW_AT_ranges will access not available range information.
    RangesDataAvailable =
        (!CUDie.getDwarfUnit()->isDWOUnit() ||
         (SkeletonDie.isValid() ? !SkeletonDie.getDwarfUnit()->isDWOUnit()
                                : true));

    traverseDieAndChildren(CUDie, Root, SkeletonDie);

    createLineAndFileRecords(DwarfContext->getLineTableForUnit(CU.get()));
    if (Error Err = createInstructions())
      return Err;

    // Process the compilation unit, as there are cases where enclosed
    // functions have the same ranges values. Insert the compilation unit
    // ranges at the end, to allow enclosing ranges to be first in the list.
    LVSectionIndex SectionIndex = getSectionIndex(CompileUnit);
    addSectionRange(SectionIndex, CompileUnit);
    LVRange *ScopesWithRanges = getSectionRanges(SectionIndex);
    ScopesWithRanges->sort();

    processLines(&CULines, SectionIndex);
    processLocationGaps();

    // These are per compile unit.
    ScopesWithRanges->clear();
    SymbolsWithLocations.clear();
    CULines.clear();
  }

  return Error::success();
}

// Get the location information for the associated attribute.
void LVDWARFReader::processLocationList(dwarf::Attribute Attr,
                                        const DWARFFormValue &FormValue,
                                        const DWARFDie &Die,
                                        uint64_t OffsetOnEntry,
                                        bool CallSiteLocation) {

  auto ProcessLocationExpression = [&](const DWARFExpression &Expression) {
    for (const DWARFExpression::Operation &Op : Expression)
      CurrentSymbol->addLocationOperands(Op.getCode(), Op.getRawOperands());
  };

  DWARFUnit *U = Die.getDwarfUnit();
  DWARFContext &DwarfContext = U->getContext();
  bool IsLittleEndian = DwarfContext.isLittleEndian();
  if (FormValue.isFormClass(DWARFFormValue::FC_Block) ||
      (DWARFAttribute::mayHaveLocationExpr(Attr) &&
       FormValue.isFormClass(DWARFFormValue::FC_Exprloc))) {
    ArrayRef<uint8_t> Expr = *FormValue.getAsBlock();
    DataExtractor Data(StringRef((const char *)Expr.data(), Expr.size()),
                       IsLittleEndian, 0);
    DWARFExpression Expression(Data, U->getAddressByteSize(),
                               U->getFormParams().Format);

    // Add location and operation entries.
    CurrentSymbol->addLocation(Attr, /*LowPC=*/0, /*HighPC=*/-1,
                               /*SectionOffset=*/0, OffsetOnEntry,
                               CallSiteLocation);
    ProcessLocationExpression(Expression);
    return;
  }

  if (DWARFAttribute::mayHaveLocationList(Attr) &&
      FormValue.isFormClass(DWARFFormValue::FC_SectionOffset)) {
    uint64_t Offset = *FormValue.getAsSectionOffset();
    if (FormValue.getForm() == dwarf::DW_FORM_loclistx) {
      std::optional<uint64_t> LoclistOffset = U->getLoclistOffset(Offset);
      if (!LoclistOffset)
        return;
      Offset = *LoclistOffset;
    }
    uint64_t BaseAddr = 0;
    if (std::optional<SectionedAddress> BA = U->getBaseAddress())
      BaseAddr = BA->Address;
    LVAddress LowPC = 0;
    LVAddress HighPC = 0;

    auto ProcessLocationEntry = [&](const DWARFLocationEntry &Entry) {
      if (Entry.Kind == dwarf::DW_LLE_base_address) {
        BaseAddr = Entry.Value0;
        return;
      }
      if (Entry.Kind == dwarf::DW_LLE_offset_pair) {
        LowPC = BaseAddr + Entry.Value0;
        HighPC = BaseAddr + Entry.Value1;
        DWARFAddressRange Range{LowPC, HighPC, Entry.SectionIndex};
        if (Range.SectionIndex == SectionedAddress::UndefSection)
          Range.SectionIndex = Entry.SectionIndex;
        DWARFLocationExpression Loc{Range, Entry.Loc};
        DWARFDataExtractor Data(Loc.Expr, IsLittleEndian,
                                U->getAddressByteSize());
        DWARFExpression Expression(Data, U->getAddressByteSize());

        // Store the real upper limit for the address range.
        if (UpdateHighAddress && HighPC > 0)
          --HighPC;
        // Add location and operation entries.
        CurrentSymbol->addLocation(Attr, LowPC, HighPC, Offset, OffsetOnEntry,
                                   CallSiteLocation);
        ProcessLocationExpression(Expression);
      }
    };
    Error E = U->getLocationTable().visitLocationList(
        &Offset, [&](const DWARFLocationEntry &E) {
          ProcessLocationEntry(E);
          return true;
        });
    if (E)
      consumeError(std::move(E));
  }
}

void LVDWARFReader::processLocationMember(dwarf::Attribute Attr,
                                          const DWARFFormValue &FormValue,
                                          const DWARFDie &Die,
                                          uint64_t OffsetOnEntry) {
  // Check if the value is an integer constant.
  if (FormValue.isFormClass(DWARFFormValue::FC_Constant))
    // Add a record to hold a constant as location.
    CurrentSymbol->addLocationConstant(Attr, *FormValue.getAsUnsignedConstant(),
                                       OffsetOnEntry);
  else
    // This is a location description, or a reference to one.
    processLocationList(Attr, FormValue, Die, OffsetOnEntry);
}

// Update the current element with the reference.
void LVDWARFReader::updateReference(dwarf::Attribute Attr,
                                    const DWARFFormValue &FormValue) {
  // FIXME: We are assuming that at most one Reference (DW_AT_specification,
  // DW_AT_abstract_origin, ...) and at most one Type (DW_AT_import, DW_AT_type)
  // appear in any single DIE, but this may not be true.
  uint64_t Offset;
  if (std::optional<uint64_t> Off = FormValue.getAsRelativeReference())
    Offset = FormValue.getUnit()->getOffset() + *Off;
  else if (Off = FormValue.getAsDebugInfoReference(); Off)
    Offset = *Off;
  else
    llvm_unreachable("Unsupported reference type");

  // Get target for the given reference, if already created.
  LVElement *Target = getElementForOffset(
      Offset, CurrentElement,
      /*IsType=*/Attr == dwarf::DW_AT_import || Attr == dwarf::DW_AT_type);
  // Check if we are dealing with cross CU references.
  if (FormValue.getForm() == dwarf::DW_FORM_ref_addr) {
    if (Target) {
      // The global reference is ready. Mark it as global.
      Target->setIsGlobalReference();
      // Remove global reference from the unseen list.
      removeGlobalOffset(Offset);
    } else
      // Record the unseen cross CU reference.
      addGlobalOffset(Offset);
  }

  // At this point, 'Target' can be null, in the case of the target element
  // not being seen. But the correct bit is set, to indicate that the target
  // is being referenced by (abstract_origin, extension, specification) or
  // (import, type).
  // We must differentiate between the kind of reference. This is needed to
  // complete inlined function instances with dropped abstract references,
  // in order to facilitate a logical comparison.
  switch (Attr) {
  case dwarf::DW_AT_abstract_origin:
  case dwarf::DW_AT_call_origin:
    CurrentElement->setReference(Target);
    CurrentElement->setHasReferenceAbstract();
    break;
  case dwarf::DW_AT_extension:
    CurrentElement->setReference(Target);
    CurrentElement->setHasReferenceExtension();
    break;
  case dwarf::DW_AT_specification:
    CurrentElement->setReference(Target);
    CurrentElement->setHasReferenceSpecification();
    break;
  case dwarf::DW_AT_import:
  case dwarf::DW_AT_type:
    CurrentElement->setType(Target);
    break;
  default:
    break;
  }
}

// Get an element given the DIE offset.
LVElement *LVDWARFReader::getElementForOffset(LVOffset Offset,
                                              LVElement *Element, bool IsType) {
  auto Iter = ElementTable.try_emplace(Offset).first;
  // Update the element and all the references pointing to this element.
  LVElementEntry &Entry = Iter->second;
  if (!Entry.Element) {
    if (IsType)
      Entry.Types.insert(Element);
    else
      Entry.References.insert(Element);
  }
  return Entry.Element;
}

Error LVDWARFReader::loadTargetInfo(const ObjectFile &Obj) {
  // Detect the architecture from the object file. We usually don't need OS
  // info to lookup a target and create register info.
  Triple TT;
  TT.setArch(Triple::ArchType(Obj.getArch()));
  TT.setVendor(Triple::UnknownVendor);
  TT.setOS(Triple::UnknownOS);

  // Features to be passed to target/subtarget
  Expected<SubtargetFeatures> Features = Obj.getFeatures();
  SubtargetFeatures FeaturesValue;
  if (!Features) {
    consumeError(Features.takeError());
    FeaturesValue = SubtargetFeatures();
  }
  FeaturesValue = *Features;
  return loadGenericTargetInfo(TT.str(), FeaturesValue.getString());
}

void LVDWARFReader::mapRangeAddress(const ObjectFile &Obj) {
  for (auto Iter = Obj.symbol_begin(); Iter != Obj.symbol_end(); ++Iter) {
    const SymbolRef &Symbol = *Iter;

    Expected<SymbolRef::Type> TypeOrErr = Symbol.getType();
    if (!TypeOrErr) {
      consumeError(TypeOrErr.takeError());
      continue;
    }

    // Process only symbols that represent a function.
    SymbolRef::Type Type = *TypeOrErr;
    if (Type != SymbolRef::ST_Function)
      continue;

    // In the case of a Mach-O STAB symbol, get its section only if
    // the STAB symbol's section field refers to a valid section index.
    // Otherwise the symbol may error trying to load a section that
    // does not exist.
    const MachOObjectFile *MachO = dyn_cast<const MachOObjectFile>(&Obj);
    bool IsSTAB = false;
    if (MachO) {
      DataRefImpl SymDRI = Symbol.getRawDataRefImpl();
      uint8_t NType =
          (MachO->is64Bit() ? MachO->getSymbol64TableEntry(SymDRI).n_type
                            : MachO->getSymbolTableEntry(SymDRI).n_type);
      if (NType & MachO::N_STAB)
        IsSTAB = true;
    }

    Expected<section_iterator> IterOrErr = Symbol.getSection();
    if (!IterOrErr) {
      consumeError(IterOrErr.takeError());
      continue;
    }
    section_iterator Section = IsSTAB ? Obj.section_end() : *IterOrErr;
    if (Section == Obj.section_end())
      continue;

    // Get the symbol value.
    Expected<uint64_t> AddressOrErr = Symbol.getAddress();
    if (!AddressOrErr) {
      consumeError(AddressOrErr.takeError());
      continue;
    }
    uint64_t Address = *AddressOrErr;

    // Get symbol name.
    StringRef Name;
    Expected<StringRef> NameOrErr = Symbol.getName();
    if (!NameOrErr) {
      consumeError(NameOrErr.takeError());
      continue;
    }
    Name = *NameOrErr;

    // Check if the symbol is Comdat.
    Expected<uint32_t> FlagsOrErr = Symbol.getFlags();
    if (!FlagsOrErr) {
      consumeError(FlagsOrErr.takeError());
      continue;
    }
    uint32_t Flags = *FlagsOrErr;

    // Mark the symbol as 'comdat' in any of the following cases:
    // - Symbol has the SF_Weak flag or
    // - Symbol section index different from the DotTextSectionIndex.
    LVSectionIndex SectionIndex = Section->getIndex();
    bool IsComdat =
        (Flags & SymbolRef::SF_Weak) || (SectionIndex != DotTextSectionIndex);

    // Record the symbol name (linkage) and its loading address.
    addToSymbolTable(Name, Address, SectionIndex, IsComdat);
  }
}

void LVDWARFReader::sortScopes() { Root->sort(); }

void LVDWARFReader::print(raw_ostream &OS) const {
  OS << "LVType\n";
  LLVM_DEBUG(dbgs() << "CreateReaders\n");
}
