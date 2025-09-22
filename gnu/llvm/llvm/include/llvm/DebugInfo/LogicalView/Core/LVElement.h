//===-- LVElement.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the LVElement class, which is used to describe a debug
// information element.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVELEMENT_H
#define LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVELEMENT_H

#include "llvm/DebugInfo/LogicalView/Core/LVObject.h"
#include "llvm/Support/Casting.h"
#include <map>
#include <set>
#include <vector>

namespace llvm {
namespace logicalview {

// RTTI Subclasses ID.
enum class LVSubclassID : unsigned char {
  LV_ELEMENT,
  LV_LINE_FIRST,
  LV_LINE,
  LV_LINE_DEBUG,
  LV_LINE_ASSEMBLER,
  LV_LINE_LAST,
  lV_SCOPE_FIRST,
  LV_SCOPE,
  LV_SCOPE_AGGREGATE,
  LV_SCOPE_ALIAS,
  LV_SCOPE_ARRAY,
  LV_SCOPE_COMPILE_UNIT,
  LV_SCOPE_ENUMERATION,
  LV_SCOPE_FORMAL_PACK,
  LV_SCOPE_FUNCTION,
  LV_SCOPE_FUNCTION_INLINED,
  LV_SCOPE_FUNCTION_TYPE,
  LV_SCOPE_NAMESPACE,
  LV_SCOPE_ROOT,
  LV_SCOPE_TEMPLATE_PACK,
  LV_SCOPE_LAST,
  LV_SYMBOL_FIRST,
  LV_SYMBOL,
  LV_SYMBOL_LAST,
  LV_TYPE_FIRST,
  LV_TYPE,
  LV_TYPE_DEFINITION,
  LV_TYPE_ENUMERATOR,
  LV_TYPE_IMPORT,
  LV_TYPE_PARAM,
  LV_TYPE_SUBRANGE,
  LV_TYPE_LAST
};

enum class LVElementKind { Discarded, Global, Optimized, LastEntry };
using LVElementKindSet = std::set<LVElementKind>;
using LVElementDispatch = std::map<LVElementKind, LVElementGetFunction>;
using LVElementRequest = std::vector<LVElementGetFunction>;

class LVElement : public LVObject {
  enum class Property {
    IsLine,   // A logical line.
    IsScope,  // A logical scope.
    IsSymbol, // A logical symbol.
    IsType,   // A logical type.
    IsEnumClass,
    IsExternal,
    HasType,
    HasAugmentedName,
    IsTypedefReduced,
    IsArrayResolved,
    IsMemberPointerResolved,
    IsTemplateResolved,
    IsInlined,
    IsInlinedAbstract,
    InvalidFilename,
    HasReference,
    HasReferenceAbstract,
    HasReferenceExtension,
    HasReferenceSpecification,
    QualifiedResolved,
    IncludeInPrint,
    IsStatic,
    TransformName,
    IsScoped,        // CodeView local type.
    IsNested,        // CodeView nested type.
    IsScopedAlready, // CodeView nested type inserted in correct scope.
    IsArtificial,
    IsReferencedType,
    IsSystem,
    OffsetFromTypeIndex,
    IsAnonymous,
    LastEntry
  };
  // Typed bitvector with properties for this element.
  LVProperties<Property> Properties;
  static LVElementDispatch Dispatch;

  /// RTTI.
  const LVSubclassID SubclassID;

  // Indexes in the String Pool.
  size_t NameIndex = 0;
  size_t QualifiedNameIndex = 0;
  size_t FilenameIndex = 0;

  uint16_t AccessibilityCode : 2; // DW_AT_accessibility.
  uint16_t InlineCode : 2;        // DW_AT_inline.
  uint16_t VirtualityCode : 2;    // DW_AT_virtuality.

  // The given Specification points to an element that is connected via the
  // DW_AT_specification, DW_AT_abstract_origin or DW_AT_extension attribute.
  void setFileLine(LVElement *Specification);

  // Get the qualified name that include its parents name.
  void resolveQualifiedName();

protected:
  // Type of this element.
  LVElement *ElementType = nullptr;

  // Print the FileName Index.
  void printFileIndex(raw_ostream &OS, bool Full = true) const override;

public:
  LVElement(LVSubclassID ID)
      : LVObject(), SubclassID(ID), AccessibilityCode(0), InlineCode(0),
        VirtualityCode(0) {}
  LVElement(const LVElement &) = delete;
  LVElement &operator=(const LVElement &) = delete;
  virtual ~LVElement() = default;

  LVSubclassID getSubclassID() const { return SubclassID; }

  PROPERTY(Property, IsLine);
  PROPERTY(Property, IsScope);
  PROPERTY(Property, IsSymbol);
  PROPERTY(Property, IsType);
  PROPERTY(Property, IsEnumClass);
  PROPERTY(Property, IsExternal);
  PROPERTY(Property, HasType);
  PROPERTY(Property, HasAugmentedName);
  PROPERTY(Property, IsTypedefReduced);
  PROPERTY(Property, IsArrayResolved);
  PROPERTY(Property, IsMemberPointerResolved);
  PROPERTY(Property, IsTemplateResolved);
  PROPERTY(Property, IsInlined);
  PROPERTY(Property, IsInlinedAbstract);
  PROPERTY(Property, InvalidFilename);
  PROPERTY(Property, HasReference);
  PROPERTY(Property, HasReferenceAbstract);
  PROPERTY(Property, HasReferenceExtension);
  PROPERTY(Property, HasReferenceSpecification);
  PROPERTY(Property, QualifiedResolved);
  PROPERTY(Property, IncludeInPrint);
  PROPERTY(Property, IsStatic);
  PROPERTY(Property, TransformName);
  PROPERTY(Property, IsScoped);
  PROPERTY(Property, IsNested);
  PROPERTY(Property, IsScopedAlready);
  PROPERTY(Property, IsArtificial);
  PROPERTY(Property, IsReferencedType);
  PROPERTY(Property, IsSystem);
  PROPERTY(Property, OffsetFromTypeIndex);
  PROPERTY(Property, IsAnonymous);

  bool isNamed() const override { return NameIndex != 0; }
  bool isTyped() const override { return ElementType != nullptr; }
  bool isFiled() const override { return FilenameIndex != 0; }

  // The Element class type can point to a Type or Scope.
  bool getIsKindType() const { return ElementType && ElementType->getIsType(); }
  bool getIsKindScope() const {
    return ElementType && ElementType->getIsScope();
  }

  StringRef getName() const override {
    return getStringPool().getString(NameIndex);
  }
  void setName(StringRef ElementName) override;

  // Get pathname associated with the Element.
  StringRef getPathname() const {
    return getStringPool().getString(getFilenameIndex());
  }

  // Set filename associated with the Element.
  void setFilename(StringRef Filename);

  // Set the Element qualified name.
  void setQualifiedName(StringRef Name) {
    QualifiedNameIndex = getStringPool().getIndex(Name);
  }
  StringRef getQualifiedName() const {
    return getStringPool().getString(QualifiedNameIndex);
  }

  size_t getNameIndex() const { return NameIndex; }
  size_t getQualifiedNameIndex() const { return QualifiedNameIndex; }

  void setInnerComponent() { setInnerComponent(getName()); }
  void setInnerComponent(StringRef Name);

  // Element type name.
  StringRef getTypeName() const;

  virtual StringRef getProducer() const { return StringRef(); }
  virtual void setProducer(StringRef ProducerName) {}

  virtual bool isCompileUnit() const { return false; }
  virtual bool isRoot() const { return false; }

  virtual void setReference(LVElement *Element) {}
  virtual void setReference(LVScope *Scope) {}
  virtual void setReference(LVSymbol *Symbol) {}
  virtual void setReference(LVType *Type) {}

  virtual void setLinkageName(StringRef LinkageName) {}
  virtual StringRef getLinkageName() const { return StringRef(); }
  virtual size_t getLinkageNameIndex() const { return 0; }

  virtual uint32_t getCallLineNumber() const { return 0; }
  virtual void setCallLineNumber(uint32_t Number) {}
  virtual size_t getCallFilenameIndex() const { return 0; }
  virtual void setCallFilenameIndex(size_t Index) {}
  size_t getFilenameIndex() const { return FilenameIndex; }
  void setFilenameIndex(size_t Index) { FilenameIndex = Index; }

  // Set the File location for the Element.
  void setFile(LVElement *Reference = nullptr);

  virtual bool isBase() const { return false; }
  virtual bool isTemplateParam() const { return false; }

  virtual uint32_t getBitSize() const { return 0; }
  virtual void setBitSize(uint32_t Size) {}

  virtual int64_t getCount() const { return 0; }
  virtual void setCount(int64_t Value) {}
  virtual int64_t getLowerBound() const { return 0; }
  virtual void setLowerBound(int64_t Value) {}
  virtual int64_t getUpperBound() const { return 0; }
  virtual void setUpperBound(int64_t Value) {}
  virtual std::pair<unsigned, unsigned> getBounds() const { return {}; }
  virtual void setBounds(unsigned Lower, unsigned Upper) {}

  // Access DW_AT_GNU_discriminator attribute.
  virtual uint32_t getDiscriminator() const { return 0; }
  virtual void setDiscriminator(uint32_t Value) {}

  // Process the values for a DW_TAG_enumerator.
  virtual StringRef getValue() const { return {}; }
  virtual void setValue(StringRef Value) {}
  virtual size_t getValueIndex() const { return 0; }

  // DWARF Accessibility Codes.
  uint32_t getAccessibilityCode() const { return AccessibilityCode; }
  void setAccessibilityCode(uint32_t Access) { AccessibilityCode = Access; }
  StringRef
  accessibilityString(uint32_t Access = dwarf::DW_ACCESS_private) const;

  // CodeView Accessibility Codes.
  std::optional<uint32_t> getAccessibilityCode(codeview::MemberAccess Access);
  void setAccessibilityCode(codeview::MemberAccess Access) {
    if (std::optional<uint32_t> Code = getAccessibilityCode(Access))
      AccessibilityCode = Code.value();
  }

  // DWARF Inline Codes.
  uint32_t getInlineCode() const { return InlineCode; }
  void setInlineCode(uint32_t Code) { InlineCode = Code; }
  StringRef inlineCodeString(uint32_t Code) const;

  // DWARF Virtuality Codes.
  uint32_t getVirtualityCode() const { return VirtualityCode; }
  void setVirtualityCode(uint32_t Virtuality) { VirtualityCode = Virtuality; }
  StringRef
  virtualityString(uint32_t Virtuality = dwarf::DW_VIRTUALITY_none) const;

  // CodeView Virtuality Codes.
  std::optional<uint32_t> getVirtualityCode(codeview::MethodKind Virtuality);
  void setVirtualityCode(codeview::MethodKind Virtuality) {
    if (std::optional<uint32_t> Code = getVirtualityCode(Virtuality))
      VirtualityCode = Code.value();
  }

  // DWARF Extern Codes.
  StringRef externalString() const;

  LVElement *getType() const { return ElementType; }
  LVType *getTypeAsType() const;
  LVScope *getTypeAsScope() const;

  void setType(LVElement *Element = nullptr) {
    ElementType = Element;
    if (Element) {
      setHasType();
      Element->setIsReferencedType();
    }
  }

  // Set the type for the element, handling template parameters.
  void setGenericType(LVElement *Element);

  StringRef getTypeQualifiedName() const {
    return ElementType ? ElementType->getQualifiedName() : "";
  }

  StringRef typeAsString() const;
  std::string typeOffsetAsString() const;
  std::string discriminatorAsString() const;

  LVScope *traverseParents(LVScopeGetFunction GetFunction) const;

  LVScope *getFunctionParent() const;
  virtual LVScope *getCompileUnitParent() const;

  // Print any referenced element.
  void printReference(raw_ostream &OS, bool Full, LVElement *Parent) const;

  // Print the linkage name (Symbols and functions).
  void printLinkageName(raw_ostream &OS, bool Full, LVElement *Parent,
                        LVScope *Scope) const;
  void printLinkageName(raw_ostream &OS, bool Full, LVElement *Parent) const;

  // Generate the full name for the Element.
  void resolveFullname(LVElement *BaseType, StringRef Name = emptyString());

  // Generate a name for unnamed elements.
  void generateName(std::string &Prefix) const;
  void generateName();

  virtual bool removeElement(LVElement *Element) { return false; }
  virtual void updateLevel(LVScope *Parent, bool Moved = false);

  // During the parsing of the debug information, the logical elements are
  // created with information extracted from its description entries (DIE).
  // But they are not complete for the logical view concept. A second pass
  // is executed in order to collect their additional information.
  // The following functions 'resolve' some of their properties, such as
  // name, references, parents, extra information based on the element kind.
  virtual void resolve();
  virtual void resolveExtra() {}
  virtual void resolveName();
  virtual void resolveReferences() {}
  void resolveParents();

  bool referenceMatch(const LVElement *Element) const;

  // Returns true if current element is logically equal to the given 'Element'.
  bool equals(const LVElement *Element) const;

  // Report the current element as missing or added during comparison.
  virtual void report(LVComparePass Pass) {}

  static LVElementDispatch &getDispatch() { return Dispatch; }
};

} // end namespace logicalview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVELEMENT_H
