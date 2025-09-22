//===-- LVObject.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the LVObject class, which is used to describe a debug
// information object.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVOBJECT_H
#define LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVOBJECT_H

#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/DebugInfo/LogicalView/Core/LVSupport.h"
#include <limits>
#include <list>
#include <string>

namespace llvm {
namespace dwarf {
// Support for CodeView ModifierOptions::Unaligned.
constexpr Tag DW_TAG_unaligned = Tag(dwarf::DW_TAG_hi_user + 1);
} // namespace dwarf
} // namespace llvm

namespace llvm {
namespace logicalview {

using LVSectionIndex = uint64_t;
using LVAddress = uint64_t;
using LVHalf = uint16_t;
using LVLevel = uint32_t;
using LVOffset = uint64_t;
using LVSigned = int64_t;
using LVUnsigned = uint64_t;
using LVSmall = uint8_t;

class LVElement;
class LVLine;
class LVLocation;
class LVLocationSymbol;
class LVObject;
class LVOperation;
class LVScope;
class LVSymbol;
class LVType;

class LVOptions;
class LVPatterns;

StringRef typeNone();
StringRef typeVoid();
StringRef typeInt();
StringRef typeUnknown();
StringRef emptyString();

using LVElementSetFunction = void (LVElement::*)();
using LVElementGetFunction = bool (LVElement::*)() const;
using LVLineSetFunction = void (LVLine::*)();
using LVLineGetFunction = bool (LVLine::*)() const;
using LVObjectSetFunction = void (LVObject::*)();
using LVObjectGetFunction = bool (LVObject::*)() const;
using LVScopeSetFunction = void (LVScope::*)();
using LVScopeGetFunction = bool (LVScope::*)() const;
using LVSymbolSetFunction = void (LVSymbol::*)();
using LVSymbolGetFunction = bool (LVSymbol::*)() const;
using LVTypeSetFunction = void (LVType::*)();
using LVTypeGetFunction = bool (LVType::*)() const;

using LVElements = SmallVector<LVElement *, 8>;
using LVLines = SmallVector<LVLine *, 8>;
using LVLocations = SmallVector<LVLocation *, 8>;
using LVOperations = SmallVector<LVOperation *, 8>;
using LVScopes = SmallVector<LVScope *, 8>;
using LVSymbols = SmallVector<LVSymbol *, 8>;
using LVTypes = SmallVector<LVType *, 8>;

using LVOffsets = SmallVector<LVOffset, 8>;

const LVAddress MaxAddress = std::numeric_limits<uint64_t>::max();

enum class LVBinaryType { NONE, ELF, COFF };
enum class LVComparePass { Missing, Added };

// Validate functions.
using LVValidLocation = bool (LVLocation::*)();

// Keep counters of objects.
struct LVCounter {
  unsigned Lines = 0;
  unsigned Scopes = 0;
  unsigned Symbols = 0;
  unsigned Types = 0;
  void reset() {
    Lines = 0;
    Scopes = 0;
    Symbols = 0;
    Types = 0;
  }
};

class LVObject {
  enum class Property {
    IsLocation,          // Location.
    IsGlobalReference,   // This object is being referenced from another CU.
    IsGeneratedName,     // The Object name was generated.
    IsResolved,          // Object has been resolved.
    IsResolvedName,      // Object name has been resolved.
    IsDiscarded,         // Object has been stripped by the linker.
    IsOptimized,         // Object has been optimized by the compiler.
    IsAdded,             // Object has been 'added'.
    IsMatched,           // Object has been matched to a given pattern.
    IsMissing,           // Object is 'missing'.
    IsMissingLink,       // Object is indirectly 'missing'.
    IsInCompare,         // In 'compare' mode.
    IsFileFromReference, // File ID from specification.
    IsLineFromReference, // Line No from specification.
    HasMoved,            // The object was moved from 'target' to 'reference'.
    HasPattern,          // The object has a pattern.
    IsFinalized,         // CodeView object is finalized.
    IsReferenced,        // CodeView object being referenced.
    HasCodeViewLocation, // CodeView object with debug location.
    LastEntry
  };
  // Typed bitvector with properties for this object.
  LVProperties<Property> Properties;

  LVOffset Offset = 0;
  uint32_t LineNumber = 0;
  LVLevel ScopeLevel = 0;
  union {
    dwarf::Tag Tag;
    dwarf::Attribute Attr;
    LVSmall Opcode;
  } TagAttrOpcode = {dwarf::DW_TAG_null};

  // The parent of this object (nullptr if the root scope). For locations,
  // the parent is a symbol object; otherwise it is a scope object.
  union {
    LVElement *Element;
    LVScope *Scope;
    LVSymbol *Symbol;
  } Parent = {nullptr};

  // We do not support any object duplication, as they are created by parsing
  // the debug information. There is only the case where we need a very basic
  // object, to manipulate its offset, line number and scope level. Allow the
  // copy constructor to create that object; it is used to print a reference
  // to another object and in the case of templates, to print its encoded args.
  LVObject(const LVObject &Object) {
#ifndef NDEBUG
    incID();
#endif
    Properties = Object.Properties;
    Offset = Object.Offset;
    LineNumber = Object.LineNumber;
    ScopeLevel = Object.ScopeLevel;
    TagAttrOpcode = Object.TagAttrOpcode;
    Parent = Object.Parent;
  }

#ifndef NDEBUG
  // This is an internal ID used for debugging logical elements. It is used
  // for cases where an unique offset within the binary input file is not
  // available.
  static uint64_t GID;
  uint64_t ID = 0;

  void incID() {
    ++GID;
    ID = GID;
  }
#endif

protected:
  // Get a string representation for the given number and discriminator.
  std::string lineAsString(uint32_t LineNumber, LVHalf Discriminator,
                           bool ShowZero) const;

  // Get a string representation for the given number.
  std::string referenceAsString(uint32_t LineNumber, bool Spaces) const;

  // Print the Filename or Pathname.
  // Empty implementation for those objects that do not have any user
  // source file references, such as debug locations.
  virtual void printFileIndex(raw_ostream &OS, bool Full = true) const {}

public:
  LVObject() {
#ifndef NDEBUG
    incID();
#endif
  };
  LVObject &operator=(const LVObject &) = delete;
  virtual ~LVObject() = default;

  PROPERTY(Property, IsLocation);
  PROPERTY(Property, IsGlobalReference);
  PROPERTY(Property, IsGeneratedName);
  PROPERTY(Property, IsResolved);
  PROPERTY(Property, IsResolvedName);
  PROPERTY(Property, IsDiscarded);
  PROPERTY(Property, IsOptimized);
  PROPERTY(Property, IsAdded);
  PROPERTY(Property, IsMatched);
  PROPERTY(Property, IsMissing);
  PROPERTY(Property, IsMissingLink);
  PROPERTY(Property, IsInCompare);
  PROPERTY(Property, IsFileFromReference);
  PROPERTY(Property, IsLineFromReference);
  PROPERTY(Property, HasMoved);
  PROPERTY(Property, HasPattern);
  PROPERTY(Property, IsFinalized);
  PROPERTY(Property, IsReferenced);
  PROPERTY(Property, HasCodeViewLocation);

  // True if the scope has been named or typed or with line number.
  virtual bool isNamed() const { return false; }
  virtual bool isTyped() const { return false; }
  virtual bool isFiled() const { return false; }
  bool isLined() const { return LineNumber != 0; }

  // DWARF tag, attribute or expression opcode.
  dwarf::Tag getTag() const { return TagAttrOpcode.Tag; }
  void setTag(dwarf::Tag Tag) { TagAttrOpcode.Tag = Tag; }
  dwarf::Attribute getAttr() const { return TagAttrOpcode.Attr; }
  void setAttr(dwarf::Attribute Attr) { TagAttrOpcode.Attr = Attr; }
  LVSmall getOpcode() const { return TagAttrOpcode.Opcode; }
  void setOpcode(LVSmall Opcode) { TagAttrOpcode.Opcode = Opcode; }

  // DIE offset.
  LVOffset getOffset() const { return Offset; }
  void setOffset(LVOffset DieOffset) { Offset = DieOffset; }

  // Level where this object is located.
  LVLevel getLevel() const { return ScopeLevel; }
  void setLevel(LVLevel Level) { ScopeLevel = Level; }

  virtual StringRef getName() const { return StringRef(); }
  virtual void setName(StringRef ObjectName) {}

  LVElement *getParent() const {
    assert((!Parent.Element || static_cast<LVElement *>(Parent.Element)) &&
           "Invalid element");
    return Parent.Element;
  }
  LVScope *getParentScope() const {
    assert((!Parent.Scope || static_cast<LVScope *>(Parent.Scope)) &&
           "Invalid scope");
    return Parent.Scope;
  }
  LVSymbol *getParentSymbol() const {
    assert((!Parent.Symbol || static_cast<LVSymbol *>(Parent.Symbol)) &&
           "Invalid symbol");
    return Parent.Symbol;
  }
  void setParent(LVScope *Scope);
  void setParent(LVSymbol *Symbol);
  void resetParent() { Parent = {nullptr}; }

  virtual LVAddress getLowerAddress() const { return 0; }
  virtual void setLowerAddress(LVAddress Address) {}
  virtual LVAddress getUpperAddress() const { return 0; }
  virtual void setUpperAddress(LVAddress Address) {}

  uint32_t getLineNumber() const { return LineNumber; }
  void setLineNumber(uint32_t Number) { LineNumber = Number; }

  virtual const char *kind() const { return nullptr; }

  std::string indentAsString() const;
  std::string indentAsString(LVLevel Level) const;

  // String used as padding for printing objects with no line number.
  virtual std::string noLineAsString(bool ShowZero) const;

  // Line number for display; in the case of inlined functions, we use the
  // DW_AT_call_line attribute; otherwise use DW_AT_decl_line attribute.
  virtual std::string lineNumberAsString(bool ShowZero = false) const {
    return lineAsString(getLineNumber(), 0, ShowZero);
  }
  std::string lineNumberAsStringStripped(bool ShowZero = false) const;

  // This function prints the logical view to an output stream.
  // Split: Prints the compilation unit view to a file.
  // Match: Prints the object only if it satisfies the patterns collected
  // from the command line. See the '--select' option.
  // Print: Print the object only if satisfies the conditions specified by
  // the different '--print' options.
  // Full: Prints full information for objects representing debug locations,
  // aggregated scopes, compile unit, functions and namespaces.
  virtual Error doPrint(bool Split, bool Match, bool Print, raw_ostream &OS,
                        bool Full = true) const;
  void printAttributes(raw_ostream &OS, bool Full = true) const;
  void printAttributes(raw_ostream &OS, bool Full, StringRef Name,
                       LVObject *Parent, StringRef Value,
                       bool UseQuotes = false, bool PrintRef = false) const;

  // Mark branch as missing (current element and parents).
  void markBranchAsMissing();

  // Prints the common information for an object (name, type, etc).
  virtual void print(raw_ostream &OS, bool Full = true) const;
  // Prints additional information for an object, depending on its kind
  // (class attributes, debug ranges, files, directories, etc).
  virtual void printExtra(raw_ostream &OS, bool Full = true) const {}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  virtual void dump() const { print(dbgs()); }
#endif

  uint64_t getID() const {
    return
#ifndef NDEBUG
        ID;
#else
        0;
#endif
  }
};

} // end namespace logicalview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVOBJECT_H
