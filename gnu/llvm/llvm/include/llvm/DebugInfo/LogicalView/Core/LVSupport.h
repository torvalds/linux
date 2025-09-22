//===-- LVSupport.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines support functions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVSUPPORT_H
#define LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVSUPPORT_H

#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/DebugInfo/LogicalView/Core/LVStringPool.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <cctype>
#include <map>
#include <sstream>

namespace llvm {
namespace logicalview {

// Returns the unique string pool instance.
LVStringPool &getStringPool();

using LVStringRefs = std::vector<StringRef>;
using LVLexicalComponent = std::tuple<StringRef, StringRef>;
using LVLexicalIndex =
    std::tuple<LVStringRefs::size_type, LVStringRefs::size_type>;

// Used to record specific characteristics about the objects.
template <typename T> class LVProperties {
  SmallBitVector Bits = SmallBitVector(static_cast<unsigned>(T::LastEntry) + 1);

public:
  LVProperties() = default;

  void set(T Idx) { Bits[static_cast<unsigned>(Idx)] = 1; }
  void reset(T Idx) { Bits[static_cast<unsigned>(Idx)] = 0; }
  bool get(T Idx) const { return Bits[static_cast<unsigned>(Idx)]; }
};

// Generate get, set and reset 'bool' functions for LVProperties instances.
// FAMILY: instance name.
// ENUM: enumeration instance.
// FIELD: enumerator instance.
// F1, F2, F3: optional 'set' functions to be called.
#define BOOL_BIT(FAMILY, ENUM, FIELD)                                          \
  bool get##FIELD() const { return FAMILY.get(ENUM::FIELD); }                  \
  void set##FIELD() { FAMILY.set(ENUM::FIELD); }                               \
  void reset##FIELD() { FAMILY.reset(ENUM::FIELD); }

#define BOOL_BIT_1(FAMILY, ENUM, FIELD, F1)                                    \
  bool get##FIELD() const { return FAMILY.get(ENUM::FIELD); }                  \
  void set##FIELD() {                                                          \
    FAMILY.set(ENUM::FIELD);                                                   \
    set##F1();                                                                 \
  }                                                                            \
  void reset##FIELD() { FAMILY.reset(ENUM::FIELD); }

#define BOOL_BIT_2(FAMILY, ENUM, FIELD, F1, F2)                                \
  bool get##FIELD() const { return FAMILY.get(ENUM::FIELD); }                  \
  void set##FIELD() {                                                          \
    FAMILY.set(ENUM::FIELD);                                                   \
    set##F1();                                                                 \
    set##F2();                                                                 \
  }                                                                            \
  void reset##FIELD() { FAMILY.reset(ENUM::FIELD); }

#define BOOL_BIT_3(FAMILY, ENUM, FIELD, F1, F2, F3)                            \
  bool get##FIELD() const { return FAMILY.get(ENUM::FIELD); }                  \
  void set##FIELD() {                                                          \
    FAMILY.set(ENUM::FIELD);                                                   \
    set##F1();                                                                 \
    set##F2();                                                                 \
    set##F3();                                                                 \
  }                                                                            \
  void reset##FIELD() { FAMILY.reset(ENUM::FIELD); }

// Generate get, set and reset functions for 'properties'.
#define PROPERTY(ENUM, FIELD) BOOL_BIT(Properties, ENUM, FIELD)
#define PROPERTY_1(ENUM, FIELD, F1) BOOL_BIT_1(Properties, ENUM, FIELD, F1)
#define PROPERTY_2(ENUM, FIELD, F1, F2)                                        \
  BOOL_BIT_2(Properties, ENUM, FIELD, F1, F2)
#define PROPERTY_3(ENUM, FIELD, F1, F2, F3)                                    \
  BOOL_BIT_3(Properties, ENUM, FIELD, F1, F2, F3)

// Generate get, set and reset functions for 'kinds'.
#define KIND(ENUM, FIELD) BOOL_BIT(Kinds, ENUM, FIELD)
#define KIND_1(ENUM, FIELD, F1) BOOL_BIT_1(Kinds, ENUM, FIELD, F1)
#define KIND_2(ENUM, FIELD, F1, F2) BOOL_BIT_2(Kinds, ENUM, FIELD, F1, F2)
#define KIND_3(ENUM, FIELD, F1, F2, F3)                                        \
  BOOL_BIT_3(Kinds, ENUM, FIELD, F1, F2, F3)

const int HEX_WIDTH = 12;
inline FormattedNumber hexValue(uint64_t N, unsigned Width = HEX_WIDTH,
                                bool Upper = false) {
  return format_hex(N, Width, Upper);
}

// Output the hexadecimal representation of 'Value' using '[0x%08x]' format.
inline std::string hexString(uint64_t Value, size_t Width = HEX_WIDTH) {
  std::string String;
  raw_string_ostream Stream(String);
  Stream << hexValue(Value, Width, false);
  return String;
}

// Get a hexadecimal string representation for the given value.
inline std::string hexSquareString(uint64_t Value) {
  return (Twine("[") + Twine(hexString(Value)) + Twine("]")).str();
}

// Return a string with the First and Others separated by spaces.
template <typename... Args>
std::string formatAttributes(const StringRef First, Args... Others) {
  const auto List = {First, Others...};
  std::stringstream Stream;
  size_t Size = 0;
  for (const StringRef &Item : List) {
    Stream << (Size ? " " : "") << Item.str();
    Size = Item.size();
  }
  Stream << (Size ? " " : "");
  return Stream.str();
}

// Add an item to a map with second being a small vector.
template <typename MapType, typename KeyType, typename ValueType>
void addItem(MapType *Map, KeyType Key, ValueType Value) {
  (*Map)[Key].push_back(Value);
}

// Double map data structure.
template <typename FirstKeyType, typename SecondKeyType, typename ValueType>
class LVDoubleMap {
  static_assert(std::is_pointer<ValueType>::value,
                "ValueType must be a pointer.");
  using LVSecondMapType = std::map<SecondKeyType, ValueType>;
  using LVFirstMapType =
      std::map<FirstKeyType, std::unique_ptr<LVSecondMapType>>;
  using LVAuxMapType = std::map<SecondKeyType, FirstKeyType>;
  using LVValueTypes = std::vector<ValueType>;
  LVFirstMapType FirstMap;
  LVAuxMapType AuxMap;

public:
  void add(FirstKeyType FirstKey, SecondKeyType SecondKey, ValueType Value) {
    typename LVFirstMapType::iterator FirstIter = FirstMap.find(FirstKey);
    if (FirstIter == FirstMap.end()) {
      auto SecondMapSP = std::make_unique<LVSecondMapType>();
      SecondMapSP->emplace(SecondKey, Value);
      FirstMap.emplace(FirstKey, std::move(SecondMapSP));
    } else {
      LVSecondMapType *SecondMap = FirstIter->second.get();
      if (SecondMap->find(SecondKey) == SecondMap->end())
        SecondMap->emplace(SecondKey, Value);
    }

    typename LVAuxMapType::iterator AuxIter = AuxMap.find(SecondKey);
    if (AuxIter == AuxMap.end()) {
      AuxMap.emplace(SecondKey, FirstKey);
    }
  }

  LVSecondMapType *findMap(FirstKeyType FirstKey) const {
    typename LVFirstMapType::const_iterator FirstIter = FirstMap.find(FirstKey);
    if (FirstIter == FirstMap.end())
      return nullptr;

    return FirstIter->second.get();
  }

  ValueType find(FirstKeyType FirstKey, SecondKeyType SecondKey) const {
    LVSecondMapType *SecondMap = findMap(FirstKey);
    if (!SecondMap)
      return nullptr;

    typename LVSecondMapType::const_iterator SecondIter =
        SecondMap->find(SecondKey);
    return (SecondIter != SecondMap->end()) ? SecondIter->second : nullptr;
  }

  ValueType find(SecondKeyType SecondKey) const {
    typename LVAuxMapType::const_iterator AuxIter = AuxMap.find(SecondKey);
    if (AuxIter == AuxMap.end())
      return nullptr;
    return find(AuxIter->second, SecondKey);
  }

  // Return a vector with all the 'ValueType' values.
  LVValueTypes find() const {
    LVValueTypes Values;
    if (FirstMap.empty())
      return Values;
    for (typename LVFirstMapType::const_reference FirstEntry : FirstMap) {
      LVSecondMapType &SecondMap = *FirstEntry.second;
      for (typename LVSecondMapType::const_reference SecondEntry : SecondMap)
        Values.push_back(SecondEntry.second);
    }
    return Values;
  }
};

// Unified and flattened pathnames.
std::string transformPath(StringRef Path);
std::string flattenedFilePath(StringRef Path);

inline std::string formattedKind(StringRef Kind) {
  return (Twine("{") + Twine(Kind) + Twine("}")).str();
}

inline std::string formattedName(StringRef Name) {
  return (Twine("'") + Twine(Name) + Twine("'")).str();
}

inline std::string formattedNames(StringRef Name1, StringRef Name2) {
  return (Twine("'") + Twine(Name1) + Twine(Name2) + Twine("'")).str();
}

// The given string represents a symbol or type name with optional enclosing
// scopes, such as: name, name<..>, scope::name, scope::..::name, etc.
// The string can have multiple references to template instantiations.
// It returns the inner most component.
LVLexicalComponent getInnerComponent(StringRef Name);
LVStringRefs getAllLexicalComponents(StringRef Name);
std::string getScopedName(const LVStringRefs &Components,
                          StringRef BaseName = {});

// These are the values assigned to the debug location record IDs.
// See DebugInfo/CodeView/CodeViewSymbols.def.
// S_DEFRANGE                               0x113f
// S_DEFRANGE_SUBFIELD                      0x1140
// S_DEFRANGE_REGISTER                      0x1141
// S_DEFRANGE_FRAMEPOINTER_REL              0x1142
// S_DEFRANGE_SUBFIELD_REGISTER             0x1143
// S_DEFRANGE_FRAMEPOINTER_REL_FULL_SCOPE   0x1144
// S_DEFRANGE_REGISTER_REL                  0x1145
// When recording CodeView debug location, the above values are truncated
// to a uint8_t value in order to fit the 'OpCode' used for the logical
// debug location operations.
// Return the original CodeView enum value.
inline uint16_t getCodeViewOperationCode(uint8_t Code) { return 0x1100 | Code; }

} // end namespace logicalview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVSUPPORT_H
