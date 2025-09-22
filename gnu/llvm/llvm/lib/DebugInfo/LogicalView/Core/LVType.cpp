//===-- LVType.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This implements the LVType class.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/LogicalView/Core/LVType.h"
#include "llvm/DebugInfo/LogicalView/Core/LVCompare.h"
#include "llvm/DebugInfo/LogicalView/Core/LVReader.h"
#include "llvm/DebugInfo/LogicalView/Core/LVScope.h"

using namespace llvm;
using namespace llvm::logicalview;

#define DEBUG_TYPE "Type"

namespace {
const char *const KindBaseType = "BaseType";
const char *const KindConst = "Const";
const char *const KindEnumerator = "Enumerator";
const char *const KindImport = "Import";
const char *const KindPointer = "Pointer";
const char *const KindPointerMember = "PointerMember";
const char *const KindReference = "Reference";
const char *const KindRestrict = "Restrict";
const char *const KindRvalueReference = "RvalueReference";
const char *const KindSubrange = "Subrange";
const char *const KindTemplateTemplate = "TemplateTemplate";
const char *const KindTemplateType = "TemplateType";
const char *const KindTemplateValue = "TemplateValue";
const char *const KindTypeAlias = "TypeAlias";
const char *const KindUndefined = "Undefined";
const char *const KindUnaligned = "Unaligned";
const char *const KindUnspecified = "Unspecified";
const char *const KindVolatile = "Volatile";
} // end anonymous namespace

//===----------------------------------------------------------------------===//
// DWARF Type.
//===----------------------------------------------------------------------===//
// Return a string representation for the type kind.
const char *LVType::kind() const {
  const char *Kind = KindUndefined;
  if (getIsBase())
    Kind = KindBaseType;
  else if (getIsConst())
    Kind = KindConst;
  else if (getIsEnumerator())
    Kind = KindEnumerator;
  else if (getIsImport())
    Kind = KindImport;
  else if (getIsPointerMember())
    Kind = KindPointerMember;
  else if (getIsPointer())
    Kind = KindPointer;
  else if (getIsReference())
    Kind = KindReference;
  else if (getIsRestrict())
    Kind = KindRestrict;
  else if (getIsRvalueReference())
    Kind = KindRvalueReference;
  else if (getIsSubrange())
    Kind = KindSubrange;
  else if (getIsTemplateTypeParam())
    Kind = KindTemplateType;
  else if (getIsTemplateValueParam())
    Kind = KindTemplateValue;
  else if (getIsTemplateTemplateParam())
    Kind = KindTemplateTemplate;
  else if (getIsTypedef())
    Kind = KindTypeAlias;
  else if (getIsUnaligned())
    Kind = KindUnaligned;
  else if (getIsUnspecified())
    Kind = KindUnspecified;
  else if (getIsVolatile())
    Kind = KindVolatile;
  return Kind;
}

LVTypeDispatch LVType::Dispatch = {
    {LVTypeKind::IsBase, &LVType::getIsBase},
    {LVTypeKind::IsConst, &LVType::getIsConst},
    {LVTypeKind::IsEnumerator, &LVType::getIsEnumerator},
    {LVTypeKind::IsImport, &LVType::getIsImport},
    {LVTypeKind::IsImportDeclaration, &LVType::getIsImportDeclaration},
    {LVTypeKind::IsImportModule, &LVType::getIsImportModule},
    {LVTypeKind::IsPointer, &LVType::getIsPointer},
    {LVTypeKind::IsPointerMember, &LVType::getIsPointerMember},
    {LVTypeKind::IsReference, &LVType::getIsReference},
    {LVTypeKind::IsRestrict, &LVType::getIsRestrict},
    {LVTypeKind::IsRvalueReference, &LVType::getIsRvalueReference},
    {LVTypeKind::IsSubrange, &LVType::getIsSubrange},
    {LVTypeKind::IsTemplateParam, &LVType::getIsTemplateParam},
    {LVTypeKind::IsTemplateTemplateParam, &LVType::getIsTemplateTemplateParam},
    {LVTypeKind::IsTemplateTypeParam, &LVType::getIsTemplateTypeParam},
    {LVTypeKind::IsTemplateValueParam, &LVType::getIsTemplateValueParam},
    {LVTypeKind::IsTypedef, &LVType::getIsTypedef},
    {LVTypeKind::IsUnaligned, &LVType::getIsUnaligned},
    {LVTypeKind::IsUnspecified, &LVType::getIsUnspecified},
    {LVTypeKind::IsVolatile, &LVType::getIsVolatile}};

void LVType::resolveReferences() {
  // Some DWARF tags are the representation of types. However, we associate
  // some of them to scopes. The ones associated with types, do not have
  // any reference tags, such as DW_AT_specification, DW_AT_abstract_origin,
  // DW_AT_extension.

  // Set the file/line information using the Debug Information entry.
  setFile(/*Reference=*/nullptr);

  if (LVElement *Element = getType())
    Element->resolve();
}

void LVType::resolveName() {
  if (getIsResolvedName())
    return;
  setIsResolvedName();

  // The templates are recorded as normal DWARF objects relationships;
  // the template parameters are preserved to show the types used during
  // the instantiation; however if a compare have been requested, those
  // parameters needs to be resolved, so no conflicts are generated.
  // The following DWARF illustrates this issue:
  //
  // a) Template Parameters are preserved:
  //      {Class} 'ConstArray<AtomTable>'
  //        {Inherits} -> 'ArrayBase'
  //        {TemplateType} 'taTYPE' -> 'AtomTable'
  //        {Member} 'mData' -> '* taTYPE'
  //
  // b) Template Parameters are resolved:
  //      {Class} 'ConstArray<AtomTable>'
  //        {Inherits} -> 'ArrayBase'
  //        {TemplateType} 'taTYPE' -> 'AtomTable'
  //        {Member} 'mData' -> '* AtomTable'
  //
  // In (b), the {Member} type have been resolved to use the real type.

  LVElement *BaseType = getType();
  if (BaseType && options().getAttributeArgument())
    if (BaseType->isTemplateParam())
      BaseType = BaseType->getType();

  if (BaseType && !BaseType->getIsResolvedName())
    BaseType->resolveName();
  resolveFullname(BaseType, getName());

  // In the case of unnamed types, try to generate a name for it, using
  // the parents name and the line information. Ignore the template parameters.
  if (!isNamed() && !getIsTemplateParam())
    generateName();

  LVElement::resolveName();

  // Resolve any given pattern.
  patterns().resolvePatternMatch(this);
}

StringRef LVType::resolveReferencesChain() {
  // The types do not have a DW_AT_specification or DW_AT_abstract_origin
  // reference. Just return the type name.
  return getName();
}

void LVType::markMissingParents(const LVTypes *References,
                                const LVTypes *Targets) {
  if (!(References && Targets))
    return;

  LLVM_DEBUG({
    dbgs() << "\n[LVType::markMissingParents]\n";
    for (const LVType *Reference : *References)
      dbgs() << "References: "
             << "Kind = " << formattedKind(Reference->kind()) << ", "
             << "Name = " << formattedName(Reference->getName()) << "\n";
    for (const LVType *Target : *Targets)
      dbgs() << "Targets   : "
             << "Kind = " << formattedKind(Target->kind()) << ", "
             << "Name = " << formattedName(Target->getName()) << "\n";
  });

  for (LVType *Reference : *References) {
    LLVM_DEBUG({
      dbgs() << "Search Reference: Name = "
             << formattedName(Reference->getName()) << "\n";
    });
    if (!Reference->findIn(Targets))
      Reference->markBranchAsMissing();
  }
}

LVType *LVType::findIn(const LVTypes *Targets) const {
  if (!Targets)
    return nullptr;

  LLVM_DEBUG({
    dbgs() << "\n[LVType::findIn]\n"
           << "Reference: "
           << "Level = " << getLevel() << ", "
           << "Kind = " << formattedKind(kind()) << ", "
           << "Name = " << formattedName(getName()) << "\n";
    for (const LVType *Target : *Targets)
      dbgs() << "Target   : "
             << "Level = " << Target->getLevel() << ", "
             << "Kind = " << formattedKind(Target->kind()) << ", "
             << "Name = " << formattedName(Target->getName()) << "\n";
  });

  for (LVType *Target : *Targets)
    if (equals(Target))
      return Target;

  return nullptr;
}

// Check for a match on the arguments of a function.
bool LVType::parametersMatch(const LVTypes *References,
                             const LVTypes *Targets) {
  if (!References && !Targets)
    return true;
  if (References && Targets) {
    LVTypes ReferenceTypes;
    LVScopes ReferenceScopes;
    getParameters(References, &ReferenceTypes, &ReferenceScopes);
    LVTypes TargetTypes;
    LVScopes TargetScopes;
    getParameters(Targets, &TargetTypes, &TargetScopes);
    if (!LVType::equals(&ReferenceTypes, &TargetTypes) ||
        !LVScope::equals(&ReferenceScopes, &TargetScopes))
      return false;
    return true;
  }
  return false;
}

// Return the types which are parameters.
void LVType::getParameters(const LVTypes *Types, LVTypes *TypesParam,
                           LVScopes *ScopesParam) {
  if (!Types)
    return;

  // During a compare task, the template parameters are expanded to
  // point to their real types, to avoid compare conflicts.
  for (LVType *Type : *Types) {
    if (!Type->getIsTemplateParam())
      continue;
    if (options().getAttributeArgument()) {
      if (Type->getIsKindType())
        TypesParam->push_back(Type->getTypeAsType());
      else if (Type->getIsKindScope())
        ScopesParam->push_back(Type->getTypeAsScope());
    } else
      TypesParam->push_back(Type);
  }
}

bool LVType::equals(const LVType *Type) const {
  return LVElement::equals(Type);
}

bool LVType::equals(const LVTypes *References, const LVTypes *Targets) {
  if (!References && !Targets)
    return true;
  if (References && Targets && References->size() == Targets->size()) {
    for (const LVType *Reference : *References)
      if (!Reference->findIn(Targets))
        return false;
    return true;
  }
  return false;
}

void LVType::report(LVComparePass Pass) {
  getComparator().printItem(this, Pass);
}

void LVType::print(raw_ostream &OS, bool Full) const {
  if (getIncludeInPrint() &&
      (getIsReference() || getReader().doPrintType(this))) {
    getReaderCompileUnit()->incrementPrintedTypes();
    LVElement::print(OS, Full);
    printExtra(OS, Full);
  }
}

void LVType::printExtra(raw_ostream &OS, bool Full) const {
  OS << formattedKind(kind()) << " " << formattedName(getName()) << "\n";
}

//===----------------------------------------------------------------------===//
// DWARF typedef.
//===----------------------------------------------------------------------===//
// Return the underlying type for a typedef, which can be a type or scope.
LVElement *LVTypeDefinition::getUnderlyingType() {
  LVElement *BaseType = getTypeAsScope();
  if (BaseType)
    // Underlying type is a scope.
    return BaseType;

  LVType *Type = getTypeAsType();
  assert(Type && "Type definition does not have a type.");

  BaseType = Type;
  while (Type->getIsTypedef()) {
    BaseType = Type->getTypeAsScope();
    if (BaseType)
      // Underlying type is a scope.
      return BaseType;

    Type = Type->getTypeAsType();
    if (Type)
      BaseType = Type;
  }

  return BaseType;
}

void LVTypeDefinition::resolveExtra() {
  // In the case of CodeView, the MSVC toolset generates a series of typedefs
  // that refer to internal runtime structures, that we do not process. Those
  // typedefs are marked as 'system'. They have an associated logical type,
  // but the underlying type always is null.
  if (getIsSystem())
    return;

  // Set the reference to the typedef type.
  if (options().getAttributeUnderlying()) {
    setUnderlyingType(getUnderlyingType());
    setIsTypedefReduced();
    if (LVElement *Type = getType()) {
      Type->resolveName();
      resolveFullname(Type);
    }
  }

  // For the case of typedef'd anonymous structures:
  //   typedef struct { ... } Name;
  // Propagate the typedef name to the anonymous structure.
  LVScope *Aggregate = getTypeAsScope();
  if (Aggregate && Aggregate->getIsAnonymous())
    Aggregate->setName(getName());
}

bool LVTypeDefinition::equals(const LVType *Type) const {
  return LVType::equals(Type);
}

void LVTypeDefinition::printExtra(raw_ostream &OS, bool Full) const {
  OS << formattedKind(kind()) << " " << formattedName(getName()) << " -> "
     << typeOffsetAsString()
     << formattedName((getType() ? getType()->getName() : "")) << "\n";
}

//===----------------------------------------------------------------------===//
// DWARF enumerator (DW_TAG_enumerator).
//===----------------------------------------------------------------------===//
bool LVTypeEnumerator::equals(const LVType *Type) const {
  return LVType::equals(Type);
}

void LVTypeEnumerator::printExtra(raw_ostream &OS, bool Full) const {
  OS << formattedKind(kind()) << " '" << getName()
     << "' = " << formattedName(getValue()) << "\n";
}

//===----------------------------------------------------------------------===//
// DWARF import (DW_TAG_imported_module / DW_TAG_imported_declaration).
//===----------------------------------------------------------------------===//
bool LVTypeImport::equals(const LVType *Type) const {
  return LVType::equals(Type);
}

void LVTypeImport::printExtra(raw_ostream &OS, bool Full) const {
  std::string Attributes =
      formatAttributes(virtualityString(), accessibilityString());

  OS << formattedKind(kind()) << " " << typeOffsetAsString() << Attributes
     << formattedName((getType() ? getType()->getName() : "")) << "\n";
}

//===----------------------------------------------------------------------===//
// DWARF Template parameter holder (type or param).
//===----------------------------------------------------------------------===//
LVTypeParam::LVTypeParam() : LVType() {
  options().getAttributeTypename() ? setIncludeInPrint()
                                   : resetIncludeInPrint();
}

// Encode the specific template argument.
void LVTypeParam::encodeTemplateArgument(std::string &Name) const {
  // The incoming type is a template parameter; we have 3 kinds of parameters:
  // - type parameter: resolve the instance (type);
  // - value parameter: resolve the constant value
  // - template parameter: resolve the name of the template.
  // If the parameter type is a template instance (STL sample), we need to
  // expand the type (template template case). For the following variable
  // declarations:
  //   std::type<float> a_float;
  //   std::type<int> a_int;
  // We must generate names like:
  //   "std::type<float,std::less<float>,std::allocator<float>,false>"
  //   "std::type<int,std::less<int>,std::allocator<int>,false>"
  // Instead of the incomplete names:
  //   "type<float,less,allocator,false>"
  //   "type<int,less,allocator,false>"

  if (getIsTemplateTypeParam()) {
    // Get the type instance recorded in the template type; it can be a
    // reference to a type or to a scope.

    if (getIsKindType()) {
      // The argument types always are qualified.
      Name.append(std::string(getTypeQualifiedName()));

      LVType *ArgType = getTypeAsType();
      // For template arguments that are typedefs, use the underlying type,
      // which can be a type or scope.
      if (ArgType->getIsTypedef()) {
        LVObject *BaseType = ArgType->getUnderlyingType();
        Name.append(std::string(BaseType->getName()));
      } else {
        Name.append(std::string(ArgType->getName()));
      }
    } else {
      if (getIsKindScope()) {
        LVScope *ArgScope = getTypeAsScope();
        // If the scope is a template, we have to resolve that template,
        // by recursively traversing its arguments.
        if (ArgScope->getIsTemplate())
          ArgScope->encodeTemplateArguments(Name);
        else {
          // The argument types always are qualified.
          Name.append(std::string(getTypeQualifiedName()));
          Name.append(std::string(ArgScope->getName()));
        }
      }
    }
  } else
    // Template value parameter or template template parameter.
    Name.append(getValue());
}

bool LVTypeParam::equals(const LVType *Type) const {
  if (!LVType::equals(Type))
    return false;

  // Checks the kind of template argument.
  if (getIsTemplateTypeParam() && Type->getIsTemplateTypeParam())
    return getType()->equals(Type->getType());

  if ((getIsTemplateValueParam() && Type->getIsTemplateValueParam()) ||
      (getIsTemplateTemplateParam() && Type->getIsTemplateTemplateParam()))
    return getValueIndex() == Type->getValueIndex();

  return false;
}

void LVTypeParam::printExtra(raw_ostream &OS, bool Full) const {
  OS << formattedKind(kind()) << " " << formattedName(getName()) << " -> "
     << typeOffsetAsString();

  // Depending on the type of parameter, the print includes different
  // information: type, value or reference to a template.
  if (getIsTemplateTypeParam()) {
    OS << formattedNames(getTypeQualifiedName(), getTypeName()) << "\n";
    return;
  }
  if (getIsTemplateValueParam()) {
    OS << formattedName(getValue()) << " " << formattedName(getName()) << "\n";
    return;
  }
  if (getIsTemplateTemplateParam())
    OS << formattedName(getValue()) << "\n";
}

//===----------------------------------------------------------------------===//
// DW_TAG_subrange_type
//===----------------------------------------------------------------------===//
void LVTypeSubrange::resolveExtra() {
  // There are 2 cases to represent the bounds information for an array:
  // 1) DW_TAG_subrange_type
  //      DW_AT_type --> ref_type (type of count)
  //      DW_AT_count --> value (number of elements in subrange)

  // 2) DW_TAG_subrange_type
  //      DW_AT_lower_bound --> value
  //      DW_AT_upper_bound --> value

  // The idea is to represent the bounds as a string, depending on the format:
  // 1) [count]
  // 2) [lower..upper]

  // Subrange information.
  std::string String;

  // Check if we have DW_AT_count subrange style.
  if (getIsSubrangeCount())
    // Get count subrange value. Assume 0 if missing.
    raw_string_ostream(String) << "[" << getCount() << "]";
  else
    raw_string_ostream(String)
        << "[" << getLowerBound() << ".." << getUpperBound() << "]";

  setName(String);
}

bool LVTypeSubrange::equals(const LVType *Type) const {
  if (!LVType::equals(Type))
    return false;

  return getTypeName() == Type->getTypeName() && getName() == Type->getName();
}

void LVTypeSubrange::printExtra(raw_ostream &OS, bool Full) const {
  OS << formattedKind(kind()) << " -> " << typeOffsetAsString()
     << formattedName(getTypeName()) << " " << formattedName(getName()) << "\n";
}
