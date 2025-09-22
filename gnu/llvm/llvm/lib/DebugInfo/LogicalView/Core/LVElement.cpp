//===-- LVElement.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This implements the LVElement class.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/LogicalView/Core/LVElement.h"
#include "llvm/DebugInfo/LogicalView/Core/LVReader.h"
#include "llvm/DebugInfo/LogicalView/Core/LVScope.h"
#include "llvm/DebugInfo/LogicalView/Core/LVSymbol.h"
#include "llvm/DebugInfo/LogicalView/Core/LVType.h"

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::logicalview;

#define DEBUG_TYPE "Element"

LVElementDispatch LVElement::Dispatch = {
    {LVElementKind::Discarded, &LVElement::getIsDiscarded},
    {LVElementKind::Global, &LVElement::getIsGlobalReference},
    {LVElementKind::Optimized, &LVElement::getIsOptimized}};

LVType *LVElement::getTypeAsType() const {
  return ElementType && ElementType->getIsType()
             ? static_cast<LVType *>(ElementType)
             : nullptr;
}

LVScope *LVElement::getTypeAsScope() const {
  return ElementType && ElementType->getIsScope()
             ? static_cast<LVScope *>(ElementType)
             : nullptr;
}

// Set the element type.
void LVElement::setGenericType(LVElement *Element) {
  if (!Element->isTemplateParam()) {
    setType(Element);
    return;
  }
  // For template parameters, the instance type can be a type or a scope.
  if (options().getAttributeArgument()) {
    if (Element->getIsKindType())
      setType(Element->getTypeAsType());
    else if (Element->getIsKindScope())
      setType(Element->getTypeAsScope());
  } else
    setType(Element);
}

// Discriminator as string.
std::string LVElement::discriminatorAsString() const {
  uint32_t Discriminator = getDiscriminator();
  std::string String;
  raw_string_ostream Stream(String);
  if (Discriminator && options().getAttributeDiscriminator())
    Stream << "," << Discriminator;
  return String;
}

// Get the type as a string.
StringRef LVElement::typeAsString() const {
  return getHasType() ? getTypeName() : typeVoid();
}

// Get name for element type.
StringRef LVElement::getTypeName() const {
  return ElementType ? ElementType->getName() : StringRef();
}

static size_t getStringIndex(StringRef Name) {
  // Convert the name to Unified format ('\' have been converted into '/').
  std::string Pathname(transformPath(Name));

  // Depending on the --attribute=filename and --attribute=pathname command
  // line options, use the basename or the full pathname as the name.
  if (!options().getAttributePathname()) {
    // Get the basename by ignoring any prefix up to the last slash ('/').
    StringRef Basename = Pathname;
    size_t Pos = Basename.rfind('/');
    if (Pos != std::string::npos)
      Basename = Basename.substr(Pos + 1);
    return getStringPool().getIndex(Basename);
  }

  return getStringPool().getIndex(Pathname);
}

void LVElement::setName(StringRef ElementName) {
  // In the case of Root or Compile Unit, get index for the flatted out name.
  NameIndex = getTransformName() ? getStringIndex(ElementName)
                                 : getStringPool().getIndex(ElementName);
}

void LVElement::setFilename(StringRef Filename) {
  // Get index for the flattened out filename.
  FilenameIndex = getStringIndex(Filename);
}

void LVElement::setInnerComponent(StringRef Name) {
  if (Name.size()) {
    StringRef InnerComponent;
    std::tie(std::ignore, InnerComponent) = getInnerComponent(Name);
    setName(InnerComponent);
  }
}

// Return the string representation of a DIE offset.
std::string LVElement::typeOffsetAsString() const {
  if (options().getAttributeOffset()) {
    LVElement *Element = getType();
    return hexSquareString(Element ? Element->getOffset() : 0);
  }
  return {};
}

StringRef LVElement::accessibilityString(uint32_t Access) const {
  uint32_t Value = getAccessibilityCode();
  switch (Value ? Value : Access) {
  case dwarf::DW_ACCESS_public:
    return "public";
  case dwarf::DW_ACCESS_protected:
    return "protected";
  case dwarf::DW_ACCESS_private:
    return "private";
  default:
    return StringRef();
  }
}

std::optional<uint32_t> LVElement::getAccessibilityCode(MemberAccess Access) {
  switch (Access) {
  case MemberAccess::Private:
    return dwarf::DW_ACCESS_private;
  case MemberAccess::Protected:
    return dwarf::DW_ACCESS_protected;
  case MemberAccess::Public:
    return dwarf::DW_ACCESS_public;
  default:
    return std::nullopt;
  }
}

StringRef LVElement::externalString() const {
  return getIsExternal() ? "extern" : StringRef();
}

StringRef LVElement::inlineCodeString(uint32_t Code) const {
  uint32_t Value = getInlineCode();
  switch (Value ? Value : Code) {
  case dwarf::DW_INL_not_inlined:
    return "not_inlined";
  case dwarf::DW_INL_inlined:
    return "inlined";
  case dwarf::DW_INL_declared_not_inlined:
    return "declared_not_inlined";
  case dwarf::DW_INL_declared_inlined:
    return "declared_inlined";
  default:
    return StringRef();
  }
}

StringRef LVElement::virtualityString(uint32_t Virtuality) const {
  uint32_t Value = getVirtualityCode();
  switch (Value ? Value : Virtuality) {
  case dwarf::DW_VIRTUALITY_none:
    return StringRef();
  case dwarf::DW_VIRTUALITY_virtual:
    return "virtual";
  case dwarf::DW_VIRTUALITY_pure_virtual:
    return "pure virtual";
  default:
    return StringRef();
  }
}

std::optional<uint32_t> LVElement::getVirtualityCode(MethodKind Virtuality) {
  switch (Virtuality) {
  case MethodKind::Virtual:
    return dwarf::DW_VIRTUALITY_virtual;
  case MethodKind::PureVirtual:
    return dwarf::DW_VIRTUALITY_pure_virtual;
  case MethodKind::IntroducingVirtual:
  case MethodKind::PureIntroducingVirtual:
    // No direct equivalents in DWARF. Assume Virtual.
    return dwarf::DW_VIRTUALITY_virtual;
  default:
    return std::nullopt;
  }
}

void LVElement::resolve() {
  if (getIsResolved())
    return;
  setIsResolved();

  resolveReferences();
  resolveParents();
  resolveExtra();
  resolveName();
}

// Set File/Line using the specification element.
void LVElement::setFileLine(LVElement *Specification) {
  // In the case of inlined functions, the correct scope must be associated
  // with the file and line information of the outline version.
  if (!isLined()) {
    setLineNumber(Specification->getLineNumber());
    setIsLineFromReference();
  }
  if (!isFiled()) {
    setFilenameIndex(Specification->getFilenameIndex());
    setIsFileFromReference();
  }
}

void LVElement::resolveName() {
  // Set the qualified name if requested.
  if (options().getAttributeQualified())
    resolveQualifiedName();

  setIsResolvedName();
}

// Resolve any parents.
void LVElement::resolveParents() {
  if (isRoot() || isCompileUnit())
    return;

  LVScope *Parent = getParentScope();
  if (Parent && !Parent->getIsCompileUnit())
    Parent->resolve();
}

// Generate a name for unnamed elements.
void LVElement::generateName(std::string &Prefix) const {
  LVScope *Scope = getParentScope();
  if (!Scope)
    return;

  // Use its parent name and any line information.
  Prefix.append(std::string(Scope->getName()));
  Prefix.append("::");
  Prefix.append(isLined() ? lineNumberAsString(/*ShowZero=*/true) : "?");

  // Remove any whitespaces.
  llvm::erase_if(Prefix, ::isspace);
}

// Generate a name for unnamed elements.
void LVElement::generateName() {
  setIsAnonymous();
  std::string Name;
  generateName(Name);
  setName(Name);
  setIsGeneratedName();
}

void LVElement::updateLevel(LVScope *Parent, bool Moved) {
  setLevel(Parent->getLevel() + 1);
  if (Moved)
    setHasMoved();
}

// Generate the full name for the element, to include special qualifiers.
void LVElement::resolveFullname(LVElement *BaseType, StringRef Name) {
  // For the following sample code,
  //   void *p;
  // some compilers do not generate an attribute for the associated type:
  //      DW_TAG_variable
  //        DW_AT_name 'p'
  //        DW_AT_type $1
  //        ...
  // $1:  DW_TAG_pointer_type
  //      ...
  // For those cases, generate the implicit 'void' type.
  StringRef BaseTypename = BaseType ? BaseType->getName() : emptyString();
  bool GetBaseTypename = false;
  bool UseBaseTypename = true;
  bool UseNameText = true;

  switch (getTag()) {
  case dwarf::DW_TAG_pointer_type: // "*";
    if (!BaseType)
      BaseTypename = typeVoid();
    break;
  case dwarf::DW_TAG_const_type:            // "const"
  case dwarf::DW_TAG_ptr_to_member_type:    // "*"
  case dwarf::DW_TAG_rvalue_reference_type: // "&&"
  case dwarf::DW_TAG_reference_type:        // "&"
  case dwarf::DW_TAG_restrict_type:         // "restrict"
  case dwarf::DW_TAG_volatile_type:         // "volatile"
  case dwarf::DW_TAG_unaligned:             // "unaligned"
    break;
  case dwarf::DW_TAG_base_type:
  case dwarf::DW_TAG_compile_unit:
  case dwarf::DW_TAG_class_type:
  case dwarf::DW_TAG_enumerator:
  case dwarf::DW_TAG_namespace:
  case dwarf::DW_TAG_skeleton_unit:
  case dwarf::DW_TAG_structure_type:
  case dwarf::DW_TAG_union_type:
  case dwarf::DW_TAG_unspecified_type:
  case dwarf::DW_TAG_GNU_template_parameter_pack:
    GetBaseTypename = true;
    break;
  case dwarf::DW_TAG_array_type:
  case dwarf::DW_TAG_call_site:
  case dwarf::DW_TAG_entry_point:
  case dwarf::DW_TAG_enumeration_type:
  case dwarf::DW_TAG_GNU_call_site:
  case dwarf::DW_TAG_imported_module:
  case dwarf::DW_TAG_imported_declaration:
  case dwarf::DW_TAG_inlined_subroutine:
  case dwarf::DW_TAG_label:
  case dwarf::DW_TAG_subprogram:
  case dwarf::DW_TAG_subrange_type:
  case dwarf::DW_TAG_subroutine_type:
  case dwarf::DW_TAG_typedef:
    GetBaseTypename = true;
    UseBaseTypename = false;
    break;
  case dwarf::DW_TAG_template_type_parameter:
  case dwarf::DW_TAG_template_value_parameter:
    UseBaseTypename = false;
    break;
  case dwarf::DW_TAG_GNU_template_template_param:
    break;
  case dwarf::DW_TAG_catch_block:
  case dwarf::DW_TAG_lexical_block:
  case dwarf::DW_TAG_try_block:
    UseNameText = false;
    break;
  default:
    llvm_unreachable("Invalid type.");
    return;
    break;
  }

  // Overwrite if no given value. 'Name' is empty when resolving for scopes
  // and symbols. In the case of types, it represents the type base name.
  if (Name.empty() && GetBaseTypename)
    Name = getName();

  // Concatenate the elements to get the full type name.
  // Type will be: base_parent + pre + base + parent + post.
  std::string Fullname;

  if (UseNameText && Name.size())
    Fullname.append(std::string(Name));
  if (UseBaseTypename && BaseTypename.size()) {
    if (UseNameText && Name.size())
      Fullname.append(" ");
    Fullname.append(std::string(BaseTypename));
  }

  // For a better and consistent layout, check if the generated name
  // contains double space sequences.
  assert((Fullname.find("  ", 0) == std::string::npos) &&
         "Extra double spaces in name.");

  LLVM_DEBUG({ dbgs() << "Fullname = '" << Fullname << "'\n"; });
  setName(Fullname);
}

void LVElement::setFile(LVElement *Reference) {
  if (!options().getAttributeAnySource())
    return;

  // At this point, any existing reference to another element, have been
  // resolved and the file ID extracted from the DI entry.
  if (Reference)
    setFileLine(Reference);

  // The file information is used to show the source file for any element
  // and display any new source file in relation to its parent element.
  // a) Elements that are not inlined.
  //    - We record the DW_AT_decl_line and DW_AT_decl_file.
  // b) Elements that are inlined.
  //    - We record the DW_AT_decl_line and DW_AT_decl_file.
  //    - We record the DW_AT_call_line and DW_AT_call_file.
  // For both cases, we use the DW_AT_decl_file value to detect any changes
  // in the source filename containing the element. Changes on this value
  // indicates that the element being printed is not contained in the
  // previous printed filename.

  // The source files are indexed starting at 0, but DW_AT_decl_file defines
  // that 0 means no file; a value of 1 means the 0th entry.
  size_t Index = 0;

  // An element with no source file information will use the reference
  // attribute (DW_AT_specification, DW_AT_abstract_origin, DW_AT_extension)
  // to update its information.
  if (getIsFileFromReference() && Reference) {
    Index = Reference->getFilenameIndex();
    if (Reference->getInvalidFilename())
      setInvalidFilename();
    setFilenameIndex(Index);
    return;
  }

  // The source files are indexed starting at 0, but DW_AT_decl_file
  // defines that 0 means no file; a value of 1 means the 0th entry.
  Index = getFilenameIndex();
  if (Index) {
    StringRef Filename = getReader().getFilename(this, Index);
    Filename.size() ? setFilename(Filename) : setInvalidFilename();
  }
}

LVScope *LVElement::traverseParents(LVScopeGetFunction GetFunction) const {
  LVScope *Parent = getParentScope();
  while (Parent && !(Parent->*GetFunction)())
    Parent = Parent->getParentScope();
  return Parent;
}

LVScope *LVElement::getFunctionParent() const {
  return traverseParents(&LVScope::getIsFunction);
}

LVScope *LVElement::getCompileUnitParent() const {
  return traverseParents(&LVScope::getIsCompileUnit);
}

// Resolve the qualified name to include the parent hierarchy names.
void LVElement::resolveQualifiedName() {
  if (!getIsReferencedType() || isBase() || getQualifiedResolved() ||
      !getIncludeInPrint())
    return;

  std::string Name;

  // Get the qualified name, excluding the Compile Unit.
  LVScope *Parent = getParentScope();
  if (Parent && !Parent->getIsRoot()) {
    while (Parent && !Parent->getIsCompileUnit()) {
      Name.insert(0, "::");
      if (Parent->isNamed())
        Name.insert(0, std::string(Parent->getName()));
      else {
        std::string Temp;
        Parent->generateName(Temp);
        Name.insert(0, Temp);
      }
      Parent = Parent->getParentScope();
    }
  }

  if (Name.size()) {
    setQualifiedName(Name);
    setQualifiedResolved();
  }
  LLVM_DEBUG({
    dbgs() << "Offset: " << hexSquareString(getOffset())
           << ", Kind: " << formattedKind(kind())
           << ", Name: " << formattedName(getName())
           << ", QualifiedName: " << formattedName(Name) << "\n";
  });
}

bool LVElement::referenceMatch(const LVElement *Element) const {
  return (getHasReference() && Element->getHasReference()) ||
         (!getHasReference() && !Element->getHasReference());
}

bool LVElement::equals(const LVElement *Element) const {
  // The minimum factors that must be the same for an equality are:
  // line number, level, name, qualified name and filename.
  LLVM_DEBUG({
    dbgs() << "\n[Element::equals]\n";
    if (options().getAttributeOffset()) {
      dbgs() << "Reference: " << hexSquareString(getOffset()) << "\n";
      dbgs() << "Target   : " << hexSquareString(Element->getOffset()) << "\n";
    }
    dbgs() << "Reference: "
           << "Kind = " << formattedKind(kind()) << ", "
           << "Name = " << formattedName(getName()) << ", "
           << "Qualified = " << formattedName(getQualifiedName()) << "\n"
           << "Target   : "
           << "Kind = " << formattedKind(Element->kind()) << ", "
           << "Name = " << formattedName(Element->getName()) << ", "
           << "Qualified = " << formattedName(Element->getQualifiedName())
           << "\n"
           << "Reference: "
           << "NameIndex = " << getNameIndex() << ", "
           << "QualifiedNameIndex = " << getQualifiedNameIndex() << ", "
           << "FilenameIndex = " << getFilenameIndex() << "\n"
           << "Target   : "
           << "NameIndex = " << Element->getNameIndex() << ", "
           << "QualifiedNameIndex = " << Element->getQualifiedNameIndex()
           << ", "
           << "FilenameIndex = " << Element->getFilenameIndex() << "\n";
  });
  if ((getLineNumber() != Element->getLineNumber()) ||
      (getLevel() != Element->getLevel()))
    return false;

  if ((getQualifiedNameIndex() != Element->getQualifiedNameIndex()) ||
      (getNameIndex() != Element->getNameIndex()) ||
      (getFilenameIndex() != Element->getFilenameIndex()))
    return false;

  if (!getType() && !Element->getType())
    return true;
  if (getType() && Element->getType())
    return getType()->equals(Element->getType());
  return false;
}

// Print the FileName Index.
void LVElement::printFileIndex(raw_ostream &OS, bool Full) const {
  if (options().getPrintFormatting() && options().getAttributeAnySource() &&
      getFilenameIndex()) {

    // Check if there is a change in the File ID sequence.
    size_t Index = getFilenameIndex();
    if (options().changeFilenameIndex(Index)) {
      // Just to keep a nice layout.
      OS << "\n";
      printAttributes(OS, /*Full=*/false);

      OS << "  {Source} ";
      if (getInvalidFilename())
        OS << format("[0x%08x]\n", Index);
      else
        OS << formattedName(getPathname()) << "\n";
    }
  }
}

void LVElement::printReference(raw_ostream &OS, bool Full,
                               LVElement *Parent) const {
  if (options().getPrintFormatting() && options().getAttributeReference())
    printAttributes(OS, Full, "{Reference} ", Parent,
                    referenceAsString(getLineNumber(), /*Spaces=*/false),
                    /*UseQuotes=*/false, /*PrintRef=*/true);
}

void LVElement::printLinkageName(raw_ostream &OS, bool Full,
                                 LVElement *Parent) const {
  if (options().getPrintFormatting() && options().getAttributeLinkage()) {
    printAttributes(OS, Full, "{Linkage} ", Parent, getLinkageName(),
                    /*UseQuotes=*/true, /*PrintRef=*/false);
  }
}

void LVElement::printLinkageName(raw_ostream &OS, bool Full, LVElement *Parent,
                                 LVScope *Scope) const {
  if (options().getPrintFormatting() && options().getAttributeLinkage()) {
    LVSectionIndex SectionIndex = getReader().getSectionIndex(Scope);
    std::string Text = (Twine(" 0x") + Twine::utohexstr(SectionIndex) +
                        Twine(" '") + Twine(getLinkageName()) + Twine("'"))
                           .str();
    printAttributes(OS, Full, "{Linkage} ", Parent, Text,
                    /*UseQuotes=*/false, /*PrintRef=*/false);
  }
}
