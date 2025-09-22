//===-- ScopedPrinter.h ----------------------------------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_SCOPEDPRINTER_H
#define LLVM_SUPPORT_SCOPEDPRINTER_H

#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

template <typename T> struct EnumEntry {
  StringRef Name;
  // While Name suffices in most of the cases, in certain cases
  // GNU style and LLVM style of ELFDumper do not
  // display same string for same enum. The AltName if initialized appropriately
  // will hold the string that GNU style emits.
  // Example:
  // "EM_X86_64" string on LLVM style for Elf_Ehdr->e_machine corresponds to
  // "Advanced Micro Devices X86-64" on GNU style
  StringRef AltName;
  T Value;
  constexpr EnumEntry(StringRef N, StringRef A, T V)
      : Name(N), AltName(A), Value(V) {}
  constexpr EnumEntry(StringRef N, T V) : Name(N), AltName(N), Value(V) {}
};

struct HexNumber {
  // To avoid sign-extension we have to explicitly cast to the appropriate
  // unsigned type. The overloads are here so that every type that is implicitly
  // convertible to an integer (including enums and endian helpers) can be used
  // without requiring type traits or call-site changes.
  HexNumber(char Value) : Value(static_cast<unsigned char>(Value)) {}
  HexNumber(signed char Value) : Value(static_cast<unsigned char>(Value)) {}
  HexNumber(signed short Value) : Value(static_cast<unsigned short>(Value)) {}
  HexNumber(signed int Value) : Value(static_cast<unsigned int>(Value)) {}
  HexNumber(signed long Value) : Value(static_cast<unsigned long>(Value)) {}
  HexNumber(signed long long Value)
      : Value(static_cast<unsigned long long>(Value)) {}
  HexNumber(unsigned char Value) : Value(Value) {}
  HexNumber(unsigned short Value) : Value(Value) {}
  HexNumber(unsigned int Value) : Value(Value) {}
  HexNumber(unsigned long Value) : Value(Value) {}
  HexNumber(unsigned long long Value) : Value(Value) {}
  uint64_t Value;
};

struct FlagEntry {
  FlagEntry(StringRef Name, char Value)
      : Name(Name), Value(static_cast<unsigned char>(Value)) {}
  FlagEntry(StringRef Name, signed char Value)
      : Name(Name), Value(static_cast<unsigned char>(Value)) {}
  FlagEntry(StringRef Name, signed short Value)
      : Name(Name), Value(static_cast<unsigned short>(Value)) {}
  FlagEntry(StringRef Name, signed int Value)
      : Name(Name), Value(static_cast<unsigned int>(Value)) {}
  FlagEntry(StringRef Name, signed long Value)
      : Name(Name), Value(static_cast<unsigned long>(Value)) {}
  FlagEntry(StringRef Name, signed long long Value)
      : Name(Name), Value(static_cast<unsigned long long>(Value)) {}
  FlagEntry(StringRef Name, unsigned char Value) : Name(Name), Value(Value) {}
  FlagEntry(StringRef Name, unsigned short Value) : Name(Name), Value(Value) {}
  FlagEntry(StringRef Name, unsigned int Value) : Name(Name), Value(Value) {}
  FlagEntry(StringRef Name, unsigned long Value) : Name(Name), Value(Value) {}
  FlagEntry(StringRef Name, unsigned long long Value)
      : Name(Name), Value(Value) {}
  StringRef Name;
  uint64_t Value;
};

raw_ostream &operator<<(raw_ostream &OS, const HexNumber &Value);

template <class T> std::string to_string(const T &Value) {
  std::string number;
  raw_string_ostream stream(number);
  stream << Value;
  return number;
}

template <typename T, typename TEnum>
std::string enumToString(T Value, ArrayRef<EnumEntry<TEnum>> EnumValues) {
  for (const EnumEntry<TEnum> &EnumItem : EnumValues)
    if (EnumItem.Value == Value)
      return std::string(EnumItem.AltName);
  return utohexstr(Value, true);
}

class ScopedPrinter {
public:
  enum class ScopedPrinterKind {
    Base,
    JSON,
  };

  ScopedPrinter(raw_ostream &OS,
                ScopedPrinterKind Kind = ScopedPrinterKind::Base)
      : OS(OS), Kind(Kind) {}

  ScopedPrinterKind getKind() const { return Kind; }

  static bool classof(const ScopedPrinter *SP) {
    return SP->getKind() == ScopedPrinterKind::Base;
  }

  virtual ~ScopedPrinter() = default;

  void flush() { OS.flush(); }

  void indent(int Levels = 1) { IndentLevel += Levels; }

  void unindent(int Levels = 1) {
    IndentLevel = IndentLevel > Levels ? IndentLevel - Levels : 0;
  }

  void resetIndent() { IndentLevel = 0; }

  int getIndentLevel() { return IndentLevel; }

  void setPrefix(StringRef P) { Prefix = P; }

  void printIndent() {
    OS << Prefix;
    for (int i = 0; i < IndentLevel; ++i)
      OS << "  ";
  }

  template <typename T> HexNumber hex(T Value) { return HexNumber(Value); }

  template <typename T, typename TEnum>
  void printEnum(StringRef Label, T Value,
                 ArrayRef<EnumEntry<TEnum>> EnumValues) {
    StringRef Name;
    bool Found = false;
    for (const auto &EnumItem : EnumValues) {
      if (EnumItem.Value == Value) {
        Name = EnumItem.Name;
        Found = true;
        break;
      }
    }

    if (Found)
      printHex(Label, Name, Value);
    else
      printHex(Label, Value);
  }

  template <typename T, typename TFlag>
  void printFlags(StringRef Label, T Value, ArrayRef<EnumEntry<TFlag>> Flags,
                  TFlag EnumMask1 = {}, TFlag EnumMask2 = {},
                  TFlag EnumMask3 = {}, ArrayRef<FlagEntry> ExtraFlags = {}) {
    SmallVector<FlagEntry, 10> SetFlags(ExtraFlags.begin(), ExtraFlags.end());

    for (const auto &Flag : Flags) {
      if (Flag.Value == 0)
        continue;

      TFlag EnumMask{};
      if (Flag.Value & EnumMask1)
        EnumMask = EnumMask1;
      else if (Flag.Value & EnumMask2)
        EnumMask = EnumMask2;
      else if (Flag.Value & EnumMask3)
        EnumMask = EnumMask3;
      bool IsEnum = (Flag.Value & EnumMask) != 0;
      if ((!IsEnum && (Value & Flag.Value) == Flag.Value) ||
          (IsEnum && (Value & EnumMask) == Flag.Value)) {
        SetFlags.emplace_back(Flag.Name, Flag.Value);
      }
    }

    llvm::sort(SetFlags, &flagName);
    printFlagsImpl(Label, hex(Value), SetFlags);
  }

  template <typename T> void printFlags(StringRef Label, T Value) {
    SmallVector<HexNumber, 10> SetFlags;
    uint64_t Flag = 1;
    uint64_t Curr = Value;
    while (Curr > 0) {
      if (Curr & 1)
        SetFlags.emplace_back(Flag);
      Curr >>= 1;
      Flag <<= 1;
    }
    printFlagsImpl(Label, hex(Value), SetFlags);
  }

  virtual void printNumber(StringRef Label, char Value) {
    startLine() << Label << ": " << static_cast<int>(Value) << "\n";
  }

  virtual void printNumber(StringRef Label, signed char Value) {
    startLine() << Label << ": " << static_cast<int>(Value) << "\n";
  }

  virtual void printNumber(StringRef Label, unsigned char Value) {
    startLine() << Label << ": " << static_cast<unsigned>(Value) << "\n";
  }

  virtual void printNumber(StringRef Label, short Value) {
    startLine() << Label << ": " << Value << "\n";
  }

  virtual void printNumber(StringRef Label, unsigned short Value) {
    startLine() << Label << ": " << Value << "\n";
  }

  virtual void printNumber(StringRef Label, int Value) {
    startLine() << Label << ": " << Value << "\n";
  }

  virtual void printNumber(StringRef Label, unsigned int Value) {
    startLine() << Label << ": " << Value << "\n";
  }

  virtual void printNumber(StringRef Label, long Value) {
    startLine() << Label << ": " << Value << "\n";
  }

  virtual void printNumber(StringRef Label, unsigned long Value) {
    startLine() << Label << ": " << Value << "\n";
  }

  virtual void printNumber(StringRef Label, long long Value) {
    startLine() << Label << ": " << Value << "\n";
  }

  virtual void printNumber(StringRef Label, unsigned long long Value) {
    startLine() << Label << ": " << Value << "\n";
  }

  virtual void printNumber(StringRef Label, const APSInt &Value) {
    startLine() << Label << ": " << Value << "\n";
  }

  virtual void printNumber(StringRef Label, float Value) {
    startLine() << Label << ": " << format("%5.1f", Value) << "\n";
  }

  virtual void printNumber(StringRef Label, double Value) {
    startLine() << Label << ": " << format("%5.1f", Value) << "\n";
  }

  template <typename T>
  void printNumber(StringRef Label, StringRef Str, T Value) {
    printNumberImpl(Label, Str, to_string(Value));
  }

  virtual void printBoolean(StringRef Label, bool Value) {
    startLine() << Label << ": " << (Value ? "Yes" : "No") << '\n';
  }

  template <typename... T> void printVersion(StringRef Label, T... Version) {
    startLine() << Label << ": ";
    printVersionInternal(Version...);
    getOStream() << "\n";
  }

  template <typename T>
  void printList(StringRef Label, const ArrayRef<T> List) {
    SmallVector<std::string, 10> StringList;
    for (const auto &Item : List)
      StringList.emplace_back(to_string(Item));
    printList(Label, StringList);
  }

  virtual void printList(StringRef Label, const ArrayRef<bool> List) {
    printListImpl(Label, List);
  }

  virtual void printList(StringRef Label, const ArrayRef<std::string> List) {
    printListImpl(Label, List);
  }

  virtual void printList(StringRef Label, const ArrayRef<uint64_t> List) {
    printListImpl(Label, List);
  }

  virtual void printList(StringRef Label, const ArrayRef<uint32_t> List) {
    printListImpl(Label, List);
  }

  virtual void printList(StringRef Label, const ArrayRef<uint16_t> List) {
    printListImpl(Label, List);
  }

  virtual void printList(StringRef Label, const ArrayRef<uint8_t> List) {
    SmallVector<unsigned> NumberList;
    for (const uint8_t &Item : List)
      NumberList.emplace_back(Item);
    printListImpl(Label, NumberList);
  }

  virtual void printList(StringRef Label, const ArrayRef<int64_t> List) {
    printListImpl(Label, List);
  }

  virtual void printList(StringRef Label, const ArrayRef<int32_t> List) {
    printListImpl(Label, List);
  }

  virtual void printList(StringRef Label, const ArrayRef<int16_t> List) {
    printListImpl(Label, List);
  }

  virtual void printList(StringRef Label, const ArrayRef<int8_t> List) {
    SmallVector<int> NumberList;
    for (const int8_t &Item : List)
      NumberList.emplace_back(Item);
    printListImpl(Label, NumberList);
  }

  virtual void printList(StringRef Label, const ArrayRef<APSInt> List) {
    printListImpl(Label, List);
  }

  template <typename T, typename U>
  void printList(StringRef Label, const T &List, const U &Printer) {
    startLine() << Label << ": [";
    ListSeparator LS;
    for (const auto &Item : List) {
      OS << LS;
      Printer(OS, Item);
    }
    OS << "]\n";
  }

  template <typename T> void printHexList(StringRef Label, const T &List) {
    SmallVector<HexNumber> HexList;
    for (const auto &Item : List)
      HexList.emplace_back(Item);
    printHexListImpl(Label, HexList);
  }

  template <typename T> void printHex(StringRef Label, T Value) {
    printHexImpl(Label, hex(Value));
  }

  template <typename T> void printHex(StringRef Label, StringRef Str, T Value) {
    printHexImpl(Label, Str, hex(Value));
  }

  template <typename T>
  void printSymbolOffset(StringRef Label, StringRef Symbol, T Value) {
    printSymbolOffsetImpl(Label, Symbol, hex(Value));
  }

  virtual void printString(StringRef Value) { startLine() << Value << "\n"; }

  virtual void printString(StringRef Label, StringRef Value) {
    startLine() << Label << ": " << Value << "\n";
  }

  void printStringEscaped(StringRef Label, StringRef Value) {
    printStringEscapedImpl(Label, Value);
  }

  void printBinary(StringRef Label, StringRef Str, ArrayRef<uint8_t> Value) {
    printBinaryImpl(Label, Str, Value, false);
  }

  void printBinary(StringRef Label, StringRef Str, ArrayRef<char> Value) {
    auto V =
        ArrayRef(reinterpret_cast<const uint8_t *>(Value.data()), Value.size());
    printBinaryImpl(Label, Str, V, false);
  }

  void printBinary(StringRef Label, ArrayRef<uint8_t> Value) {
    printBinaryImpl(Label, StringRef(), Value, false);
  }

  void printBinary(StringRef Label, ArrayRef<char> Value) {
    auto V =
        ArrayRef(reinterpret_cast<const uint8_t *>(Value.data()), Value.size());
    printBinaryImpl(Label, StringRef(), V, false);
  }

  void printBinary(StringRef Label, StringRef Value) {
    auto V =
        ArrayRef(reinterpret_cast<const uint8_t *>(Value.data()), Value.size());
    printBinaryImpl(Label, StringRef(), V, false);
  }

  void printBinaryBlock(StringRef Label, ArrayRef<uint8_t> Value,
                        uint32_t StartOffset) {
    printBinaryImpl(Label, StringRef(), Value, true, StartOffset);
  }

  void printBinaryBlock(StringRef Label, ArrayRef<uint8_t> Value) {
    printBinaryImpl(Label, StringRef(), Value, true);
  }

  void printBinaryBlock(StringRef Label, StringRef Value) {
    auto V =
        ArrayRef(reinterpret_cast<const uint8_t *>(Value.data()), Value.size());
    printBinaryImpl(Label, StringRef(), V, true);
  }

  template <typename T> void printObject(StringRef Label, const T &Value) {
    printString(Label, to_string(Value));
  }

  virtual void objectBegin() { scopedBegin('{'); }

  virtual void objectBegin(StringRef Label) { scopedBegin(Label, '{'); }

  virtual void objectEnd() { scopedEnd('}'); }

  virtual void arrayBegin() { scopedBegin('['); }

  virtual void arrayBegin(StringRef Label) { scopedBegin(Label, '['); }

  virtual void arrayEnd() { scopedEnd(']'); }

  virtual raw_ostream &startLine() {
    printIndent();
    return OS;
  }

  virtual raw_ostream &getOStream() { return OS; }

private:
  template <typename T> void printVersionInternal(T Value) {
    getOStream() << Value;
  }

  template <typename S, typename T, typename... TArgs>
  void printVersionInternal(S Value, T Value2, TArgs... Args) {
    getOStream() << Value << ".";
    printVersionInternal(Value2, Args...);
  }

  static bool flagName(const FlagEntry &LHS, const FlagEntry &RHS) {
    return LHS.Name < RHS.Name;
  }

  virtual void printBinaryImpl(StringRef Label, StringRef Str,
                               ArrayRef<uint8_t> Value, bool Block,
                               uint32_t StartOffset = 0);

  virtual void printFlagsImpl(StringRef Label, HexNumber Value,
                              ArrayRef<FlagEntry> Flags) {
    startLine() << Label << " [ (" << Value << ")\n";
    for (const auto &Flag : Flags)
      startLine() << "  " << Flag.Name << " (" << hex(Flag.Value) << ")\n";
    startLine() << "]\n";
  }

  virtual void printFlagsImpl(StringRef Label, HexNumber Value,
                              ArrayRef<HexNumber> Flags) {
    startLine() << Label << " [ (" << Value << ")\n";
    for (const auto &Flag : Flags)
      startLine() << "  " << Flag << '\n';
    startLine() << "]\n";
  }

  template <typename T> void printListImpl(StringRef Label, const T List) {
    startLine() << Label << ": [";
    ListSeparator LS;
    for (const auto &Item : List)
      OS << LS << Item;
    OS << "]\n";
  }

  virtual void printHexListImpl(StringRef Label,
                                const ArrayRef<HexNumber> List) {
    startLine() << Label << ": [";
    ListSeparator LS;
    for (const auto &Item : List)
      OS << LS << hex(Item);
    OS << "]\n";
  }

  virtual void printHexImpl(StringRef Label, HexNumber Value) {
    startLine() << Label << ": " << Value << "\n";
  }

  virtual void printHexImpl(StringRef Label, StringRef Str, HexNumber Value) {
    startLine() << Label << ": " << Str << " (" << Value << ")\n";
  }

  virtual void printSymbolOffsetImpl(StringRef Label, StringRef Symbol,
                                     HexNumber Value) {
    startLine() << Label << ": " << Symbol << '+' << Value << '\n';
  }

  virtual void printNumberImpl(StringRef Label, StringRef Str,
                               StringRef Value) {
    startLine() << Label << ": " << Str << " (" << Value << ")\n";
  }

  virtual void printStringEscapedImpl(StringRef Label, StringRef Value) {
    startLine() << Label << ": ";
    OS.write_escaped(Value);
    OS << '\n';
  }

  void scopedBegin(char Symbol) {
    startLine() << Symbol << '\n';
    indent();
  }

  void scopedBegin(StringRef Label, char Symbol) {
    startLine() << Label;
    if (!Label.empty())
      OS << ' ';
    OS << Symbol << '\n';
    indent();
  }

  void scopedEnd(char Symbol) {
    unindent();
    startLine() << Symbol << '\n';
  }

  raw_ostream &OS;
  int IndentLevel = 0;
  StringRef Prefix;
  ScopedPrinterKind Kind;
};

template <>
inline void
ScopedPrinter::printHex<support::ulittle16_t>(StringRef Label,
                                              support::ulittle16_t Value) {
  startLine() << Label << ": " << hex(Value) << "\n";
}

struct DelimitedScope;

class JSONScopedPrinter : public ScopedPrinter {
private:
  enum class Scope {
    Array,
    Object,
  };

  enum class ScopeKind {
    NoAttribute,
    Attribute,
    NestedAttribute,
  };

  struct ScopeContext {
    Scope Context;
    ScopeKind Kind;
    ScopeContext(Scope Context, ScopeKind Kind = ScopeKind::NoAttribute)
        : Context(Context), Kind(Kind) {}
  };

  SmallVector<ScopeContext, 8> ScopeHistory;
  json::OStream JOS;
  std::unique_ptr<DelimitedScope> OuterScope;

public:
  JSONScopedPrinter(raw_ostream &OS, bool PrettyPrint = false,
                    std::unique_ptr<DelimitedScope> &&OuterScope =
                        std::unique_ptr<DelimitedScope>{});

  static bool classof(const ScopedPrinter *SP) {
    return SP->getKind() == ScopedPrinter::ScopedPrinterKind::JSON;
  }

  void printNumber(StringRef Label, char Value) override {
    JOS.attribute(Label, Value);
  }

  void printNumber(StringRef Label, signed char Value) override {
    JOS.attribute(Label, Value);
  }

  void printNumber(StringRef Label, unsigned char Value) override {
    JOS.attribute(Label, Value);
  }

  void printNumber(StringRef Label, short Value) override {
    JOS.attribute(Label, Value);
  }

  void printNumber(StringRef Label, unsigned short Value) override {
    JOS.attribute(Label, Value);
  }

  void printNumber(StringRef Label, int Value) override {
    JOS.attribute(Label, Value);
  }

  void printNumber(StringRef Label, unsigned int Value) override {
    JOS.attribute(Label, Value);
  }

  void printNumber(StringRef Label, long Value) override {
    JOS.attribute(Label, Value);
  }

  void printNumber(StringRef Label, unsigned long Value) override {
    JOS.attribute(Label, Value);
  }

  void printNumber(StringRef Label, long long Value) override {
    JOS.attribute(Label, Value);
  }

  void printNumber(StringRef Label, unsigned long long Value) override {
    JOS.attribute(Label, Value);
  }

  void printNumber(StringRef Label, float Value) override {
    JOS.attribute(Label, Value);
  }

  void printNumber(StringRef Label, double Value) override {
    JOS.attribute(Label, Value);
  }

  void printNumber(StringRef Label, const APSInt &Value) override {
    JOS.attributeBegin(Label);
    printAPSInt(Value);
    JOS.attributeEnd();
  }

  void printBoolean(StringRef Label, bool Value) override {
    JOS.attribute(Label, Value);
  }

  void printList(StringRef Label, const ArrayRef<bool> List) override {
    printListImpl(Label, List);
  }

  void printList(StringRef Label, const ArrayRef<std::string> List) override {
    printListImpl(Label, List);
  }

  void printList(StringRef Label, const ArrayRef<uint64_t> List) override {
    printListImpl(Label, List);
  }

  void printList(StringRef Label, const ArrayRef<uint32_t> List) override {
    printListImpl(Label, List);
  }

  void printList(StringRef Label, const ArrayRef<uint16_t> List) override {
    printListImpl(Label, List);
  }

  void printList(StringRef Label, const ArrayRef<uint8_t> List) override {
    printListImpl(Label, List);
  }

  void printList(StringRef Label, const ArrayRef<int64_t> List) override {
    printListImpl(Label, List);
  }

  void printList(StringRef Label, const ArrayRef<int32_t> List) override {
    printListImpl(Label, List);
  }

  void printList(StringRef Label, const ArrayRef<int16_t> List) override {
    printListImpl(Label, List);
  }

  void printList(StringRef Label, const ArrayRef<int8_t> List) override {
    printListImpl(Label, List);
  }

  void printList(StringRef Label, const ArrayRef<APSInt> List) override {
    JOS.attributeArray(Label, [&]() {
      for (const APSInt &Item : List) {
        printAPSInt(Item);
      }
    });
  }

  void printString(StringRef Value) override { JOS.value(Value); }

  void printString(StringRef Label, StringRef Value) override {
    JOS.attribute(Label, Value);
  }

  void objectBegin() override {
    scopedBegin({Scope::Object, ScopeKind::NoAttribute});
  }

  void objectBegin(StringRef Label) override {
    scopedBegin(Label, Scope::Object);
  }

  void objectEnd() override { scopedEnd(); }

  void arrayBegin() override {
    scopedBegin({Scope::Array, ScopeKind::NoAttribute});
  }

  void arrayBegin(StringRef Label) override {
    scopedBegin(Label, Scope::Array);
  }

  void arrayEnd() override { scopedEnd(); }

private:
  // Output HexNumbers as decimals so that they're easier to parse.
  uint64_t hexNumberToInt(HexNumber Hex) { return Hex.Value; }

  void printAPSInt(const APSInt &Value) {
    JOS.rawValueBegin() << Value;
    JOS.rawValueEnd();
  }

  void printFlagsImpl(StringRef Label, HexNumber Value,
                      ArrayRef<FlagEntry> Flags) override {
    JOS.attributeObject(Label, [&]() {
      JOS.attribute("Value", hexNumberToInt(Value));
      JOS.attributeArray("Flags", [&]() {
        for (const FlagEntry &Flag : Flags) {
          JOS.objectBegin();
          JOS.attribute("Name", Flag.Name);
          JOS.attribute("Value", Flag.Value);
          JOS.objectEnd();
        }
      });
    });
  }

  void printFlagsImpl(StringRef Label, HexNumber Value,
                      ArrayRef<HexNumber> Flags) override {
    JOS.attributeObject(Label, [&]() {
      JOS.attribute("Value", hexNumberToInt(Value));
      JOS.attributeArray("Flags", [&]() {
        for (const HexNumber &Flag : Flags) {
          JOS.value(Flag.Value);
        }
      });
    });
  }

  template <typename T> void printListImpl(StringRef Label, const T &List) {
    JOS.attributeArray(Label, [&]() {
      for (const auto &Item : List)
        JOS.value(Item);
    });
  }

  void printHexListImpl(StringRef Label,
                        const ArrayRef<HexNumber> List) override {
    JOS.attributeArray(Label, [&]() {
      for (const HexNumber &Item : List) {
        JOS.value(hexNumberToInt(Item));
      }
    });
  }

  void printHexImpl(StringRef Label, HexNumber Value) override {
    JOS.attribute(Label, hexNumberToInt(Value));
  }

  void printHexImpl(StringRef Label, StringRef Str, HexNumber Value) override {
    JOS.attributeObject(Label, [&]() {
      JOS.attribute("Name", Str);
      JOS.attribute("Value", hexNumberToInt(Value));
    });
  }

  void printSymbolOffsetImpl(StringRef Label, StringRef Symbol,
                             HexNumber Value) override {
    JOS.attributeObject(Label, [&]() {
      JOS.attribute("SymName", Symbol);
      JOS.attribute("Offset", hexNumberToInt(Value));
    });
  }

  void printNumberImpl(StringRef Label, StringRef Str,
                       StringRef Value) override {
    JOS.attributeObject(Label, [&]() {
      JOS.attribute("Name", Str);
      JOS.attributeBegin("Value");
      JOS.rawValueBegin() << Value;
      JOS.rawValueEnd();
      JOS.attributeEnd();
    });
  }

  void printBinaryImpl(StringRef Label, StringRef Str, ArrayRef<uint8_t> Value,
                       bool Block, uint32_t StartOffset = 0) override {
    JOS.attributeObject(Label, [&]() {
      if (!Str.empty())
        JOS.attribute("Value", Str);
      JOS.attribute("Offset", StartOffset);
      JOS.attributeArray("Bytes", [&]() {
        for (uint8_t Val : Value)
          JOS.value(Val);
      });
    });
  }

  void scopedBegin(ScopeContext ScopeCtx) {
    if (ScopeCtx.Context == Scope::Object)
      JOS.objectBegin();
    else if (ScopeCtx.Context == Scope::Array)
      JOS.arrayBegin();
    ScopeHistory.push_back(ScopeCtx);
  }

  void scopedBegin(StringRef Label, Scope Ctx) {
    ScopeKind Kind = ScopeKind::Attribute;
    if (ScopeHistory.empty() || ScopeHistory.back().Context != Scope::Object) {
      JOS.objectBegin();
      Kind = ScopeKind::NestedAttribute;
    }
    JOS.attributeBegin(Label);
    scopedBegin({Ctx, Kind});
  }

  void scopedEnd() {
    ScopeContext ScopeCtx = ScopeHistory.back();
    if (ScopeCtx.Context == Scope::Object)
      JOS.objectEnd();
    else if (ScopeCtx.Context == Scope::Array)
      JOS.arrayEnd();
    if (ScopeCtx.Kind == ScopeKind::Attribute ||
        ScopeCtx.Kind == ScopeKind::NestedAttribute)
      JOS.attributeEnd();
    if (ScopeCtx.Kind == ScopeKind::NestedAttribute)
      JOS.objectEnd();
    ScopeHistory.pop_back();
  }
};

struct DelimitedScope {
  DelimitedScope(ScopedPrinter &W) : W(&W) {}
  DelimitedScope() : W(nullptr) {}
  virtual ~DelimitedScope() = default;
  virtual void setPrinter(ScopedPrinter &W) = 0;
  ScopedPrinter *W;
};

struct DictScope : DelimitedScope {
  explicit DictScope() = default;
  explicit DictScope(ScopedPrinter &W) : DelimitedScope(W) { W.objectBegin(); }

  DictScope(ScopedPrinter &W, StringRef N) : DelimitedScope(W) {
    W.objectBegin(N);
  }

  void setPrinter(ScopedPrinter &W) override {
    this->W = &W;
    W.objectBegin();
  }

  ~DictScope() {
    if (W)
      W->objectEnd();
  }
};

struct ListScope : DelimitedScope {
  explicit ListScope() = default;
  explicit ListScope(ScopedPrinter &W) : DelimitedScope(W) { W.arrayBegin(); }

  ListScope(ScopedPrinter &W, StringRef N) : DelimitedScope(W) {
    W.arrayBegin(N);
  }

  void setPrinter(ScopedPrinter &W) override {
    this->W = &W;
    W.arrayBegin();
  }

  ~ListScope() {
    if (W)
      W->arrayEnd();
  }
};

} // namespace llvm

#endif
