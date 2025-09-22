//===--- RustDemangle.cpp ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a demangler for Rust v0 mangled symbols as specified in
// https://rust-lang.github.io/rfcs/2603-rust-symbol-name-mangling-v0.html
//
//===----------------------------------------------------------------------===//

#include "llvm/Demangle/Demangle.h"
#include "llvm/Demangle/StringViewExtras.h"
#include "llvm/Demangle/Utility.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>

using namespace llvm;

using llvm::itanium_demangle::OutputBuffer;
using llvm::itanium_demangle::ScopedOverride;
using llvm::itanium_demangle::starts_with;

namespace {

struct Identifier {
  std::string_view Name;
  bool Punycode;

  bool empty() const { return Name.empty(); }
};

enum class BasicType {
  Bool,
  Char,
  I8,
  I16,
  I32,
  I64,
  I128,
  ISize,
  U8,
  U16,
  U32,
  U64,
  U128,
  USize,
  F32,
  F64,
  Str,
  Placeholder,
  Unit,
  Variadic,
  Never,
};

enum class IsInType {
  No,
  Yes,
};

enum class LeaveGenericsOpen {
  No,
  Yes,
};

class Demangler {
  // Maximum recursion level. Used to avoid stack overflow.
  size_t MaxRecursionLevel;
  // Current recursion level.
  size_t RecursionLevel;
  size_t BoundLifetimes;
  // Input string that is being demangled with "_R" prefix removed.
  std::string_view Input;
  // Position in the input string.
  size_t Position;
  // When true, print methods append the output to the stream.
  // When false, the output is suppressed.
  bool Print;
  // True if an error occurred.
  bool Error;

public:
  // Demangled output.
  OutputBuffer Output;

  Demangler(size_t MaxRecursionLevel = 500);

  bool demangle(std::string_view MangledName);

private:
  bool demanglePath(IsInType Type,
                    LeaveGenericsOpen LeaveOpen = LeaveGenericsOpen::No);
  void demangleImplPath(IsInType InType);
  void demangleGenericArg();
  void demangleType();
  void demangleFnSig();
  void demangleDynBounds();
  void demangleDynTrait();
  void demangleOptionalBinder();
  void demangleConst();
  void demangleConstInt();
  void demangleConstBool();
  void demangleConstChar();

  template <typename Callable> void demangleBackref(Callable Demangler) {
    uint64_t Backref = parseBase62Number();
    if (Error || Backref >= Position) {
      Error = true;
      return;
    }

    if (!Print)
      return;

    ScopedOverride<size_t> SavePosition(Position, Position);
    Position = Backref;
    Demangler();
  }

  Identifier parseIdentifier();
  uint64_t parseOptionalBase62Number(char Tag);
  uint64_t parseBase62Number();
  uint64_t parseDecimalNumber();
  uint64_t parseHexNumber(std::string_view &HexDigits);

  void print(char C);
  void print(std::string_view S);
  void printDecimalNumber(uint64_t N);
  void printBasicType(BasicType);
  void printLifetime(uint64_t Index);
  void printIdentifier(Identifier Ident);

  char look() const;
  char consume();
  bool consumeIf(char Prefix);

  bool addAssign(uint64_t &A, uint64_t B);
  bool mulAssign(uint64_t &A, uint64_t B);
};

} // namespace

char *llvm::rustDemangle(std::string_view MangledName) {
  // Return early if mangled name doesn't look like a Rust symbol.
  if (MangledName.empty() || !starts_with(MangledName, "_R"))
    return nullptr;

  Demangler D;
  if (!D.demangle(MangledName)) {
    std::free(D.Output.getBuffer());
    return nullptr;
  }

  D.Output += '\0';

  return D.Output.getBuffer();
}

Demangler::Demangler(size_t MaxRecursionLevel)
    : MaxRecursionLevel(MaxRecursionLevel) {}

static inline bool isDigit(const char C) { return '0' <= C && C <= '9'; }

static inline bool isHexDigit(const char C) {
  return ('0' <= C && C <= '9') || ('a' <= C && C <= 'f');
}

static inline bool isLower(const char C) { return 'a' <= C && C <= 'z'; }

static inline bool isUpper(const char C) { return 'A' <= C && C <= 'Z'; }

/// Returns true if C is a valid mangled character: <0-9a-zA-Z_>.
static inline bool isValid(const char C) {
  return isDigit(C) || isLower(C) || isUpper(C) || C == '_';
}

// Demangles Rust v0 mangled symbol. Returns true when successful, and false
// otherwise. The demangled symbol is stored in Output field. It is
// responsibility of the caller to free the memory behind the output stream.
//
// <symbol-name> = "_R" <path> [<instantiating-crate>]
bool Demangler::demangle(std::string_view Mangled) {
  Position = 0;
  Error = false;
  Print = true;
  RecursionLevel = 0;
  BoundLifetimes = 0;

  if (!starts_with(Mangled, "_R")) {
    Error = true;
    return false;
  }
  Mangled.remove_prefix(2);
  size_t Dot = Mangled.find('.');
  Input = Dot == std::string_view::npos ? Mangled : Mangled.substr(0, Dot);

  demanglePath(IsInType::No);

  if (Position != Input.size()) {
    ScopedOverride<bool> SavePrint(Print, false);
    demanglePath(IsInType::No);
  }

  if (Position != Input.size())
    Error = true;

  if (Dot != std::string_view::npos) {
    print(" (");
    print(Mangled.substr(Dot));
    print(")");
  }

  return !Error;
}

// Demangles a path. InType indicates whether a path is inside a type. When
// LeaveOpen is true, a closing `>` after generic arguments is omitted from the
// output. Return value indicates whether generics arguments have been left
// open.
//
// <path> = "C" <identifier>               // crate root
//        | "M" <impl-path> <type>         // <T> (inherent impl)
//        | "X" <impl-path> <type> <path>  // <T as Trait> (trait impl)
//        | "Y" <type> <path>              // <T as Trait> (trait definition)
//        | "N" <ns> <path> <identifier>   // ...::ident (nested path)
//        | "I" <path> {<generic-arg>} "E" // ...<T, U> (generic args)
//        | <backref>
// <identifier> = [<disambiguator>] <undisambiguated-identifier>
// <ns> = "C"      // closure
//      | "S"      // shim
//      | <A-Z>    // other special namespaces
//      | <a-z>    // internal namespaces
bool Demangler::demanglePath(IsInType InType, LeaveGenericsOpen LeaveOpen) {
  if (Error || RecursionLevel >= MaxRecursionLevel) {
    Error = true;
    return false;
  }
  ScopedOverride<size_t> SaveRecursionLevel(RecursionLevel, RecursionLevel + 1);

  switch (consume()) {
  case 'C': {
    parseOptionalBase62Number('s');
    printIdentifier(parseIdentifier());
    break;
  }
  case 'M': {
    demangleImplPath(InType);
    print("<");
    demangleType();
    print(">");
    break;
  }
  case 'X': {
    demangleImplPath(InType);
    print("<");
    demangleType();
    print(" as ");
    demanglePath(IsInType::Yes);
    print(">");
    break;
  }
  case 'Y': {
    print("<");
    demangleType();
    print(" as ");
    demanglePath(IsInType::Yes);
    print(">");
    break;
  }
  case 'N': {
    char NS = consume();
    if (!isLower(NS) && !isUpper(NS)) {
      Error = true;
      break;
    }
    demanglePath(InType);

    uint64_t Disambiguator = parseOptionalBase62Number('s');
    Identifier Ident = parseIdentifier();

    if (isUpper(NS)) {
      // Special namespaces
      print("::{");
      if (NS == 'C')
        print("closure");
      else if (NS == 'S')
        print("shim");
      else
        print(NS);
      if (!Ident.empty()) {
        print(":");
        printIdentifier(Ident);
      }
      print('#');
      printDecimalNumber(Disambiguator);
      print('}');
    } else {
      // Implementation internal namespaces.
      if (!Ident.empty()) {
        print("::");
        printIdentifier(Ident);
      }
    }
    break;
  }
  case 'I': {
    demanglePath(InType);
    // Omit "::" when in a type, where it is optional.
    if (InType == IsInType::No)
      print("::");
    print("<");
    for (size_t I = 0; !Error && !consumeIf('E'); ++I) {
      if (I > 0)
        print(", ");
      demangleGenericArg();
    }
    if (LeaveOpen == LeaveGenericsOpen::Yes)
      return true;
    else
      print(">");
    break;
  }
  case 'B': {
    bool IsOpen = false;
    demangleBackref([&] { IsOpen = demanglePath(InType, LeaveOpen); });
    return IsOpen;
  }
  default:
    Error = true;
    break;
  }

  return false;
}

// <impl-path> = [<disambiguator>] <path>
// <disambiguator> = "s" <base-62-number>
void Demangler::demangleImplPath(IsInType InType) {
  ScopedOverride<bool> SavePrint(Print, false);
  parseOptionalBase62Number('s');
  demanglePath(InType);
}

// <generic-arg> = <lifetime>
//               | <type>
//               | "K" <const>
// <lifetime> = "L" <base-62-number>
void Demangler::demangleGenericArg() {
  if (consumeIf('L'))
    printLifetime(parseBase62Number());
  else if (consumeIf('K'))
    demangleConst();
  else
    demangleType();
}

// <basic-type> = "a"      // i8
//              | "b"      // bool
//              | "c"      // char
//              | "d"      // f64
//              | "e"      // str
//              | "f"      // f32
//              | "h"      // u8
//              | "i"      // isize
//              | "j"      // usize
//              | "l"      // i32
//              | "m"      // u32
//              | "n"      // i128
//              | "o"      // u128
//              | "s"      // i16
//              | "t"      // u16
//              | "u"      // ()
//              | "v"      // ...
//              | "x"      // i64
//              | "y"      // u64
//              | "z"      // !
//              | "p"      // placeholder (e.g. for generic params), shown as _
static bool parseBasicType(char C, BasicType &Type) {
  switch (C) {
  case 'a':
    Type = BasicType::I8;
    return true;
  case 'b':
    Type = BasicType::Bool;
    return true;
  case 'c':
    Type = BasicType::Char;
    return true;
  case 'd':
    Type = BasicType::F64;
    return true;
  case 'e':
    Type = BasicType::Str;
    return true;
  case 'f':
    Type = BasicType::F32;
    return true;
  case 'h':
    Type = BasicType::U8;
    return true;
  case 'i':
    Type = BasicType::ISize;
    return true;
  case 'j':
    Type = BasicType::USize;
    return true;
  case 'l':
    Type = BasicType::I32;
    return true;
  case 'm':
    Type = BasicType::U32;
    return true;
  case 'n':
    Type = BasicType::I128;
    return true;
  case 'o':
    Type = BasicType::U128;
    return true;
  case 'p':
    Type = BasicType::Placeholder;
    return true;
  case 's':
    Type = BasicType::I16;
    return true;
  case 't':
    Type = BasicType::U16;
    return true;
  case 'u':
    Type = BasicType::Unit;
    return true;
  case 'v':
    Type = BasicType::Variadic;
    return true;
  case 'x':
    Type = BasicType::I64;
    return true;
  case 'y':
    Type = BasicType::U64;
    return true;
  case 'z':
    Type = BasicType::Never;
    return true;
  default:
    return false;
  }
}

void Demangler::printBasicType(BasicType Type) {
  switch (Type) {
  case BasicType::Bool:
    print("bool");
    break;
  case BasicType::Char:
    print("char");
    break;
  case BasicType::I8:
    print("i8");
    break;
  case BasicType::I16:
    print("i16");
    break;
  case BasicType::I32:
    print("i32");
    break;
  case BasicType::I64:
    print("i64");
    break;
  case BasicType::I128:
    print("i128");
    break;
  case BasicType::ISize:
    print("isize");
    break;
  case BasicType::U8:
    print("u8");
    break;
  case BasicType::U16:
    print("u16");
    break;
  case BasicType::U32:
    print("u32");
    break;
  case BasicType::U64:
    print("u64");
    break;
  case BasicType::U128:
    print("u128");
    break;
  case BasicType::USize:
    print("usize");
    break;
  case BasicType::F32:
    print("f32");
    break;
  case BasicType::F64:
    print("f64");
    break;
  case BasicType::Str:
    print("str");
    break;
  case BasicType::Placeholder:
    print("_");
    break;
  case BasicType::Unit:
    print("()");
    break;
  case BasicType::Variadic:
    print("...");
    break;
  case BasicType::Never:
    print("!");
    break;
  }
}

// <type> = | <basic-type>
//          | <path>                      // named type
//          | "A" <type> <const>          // [T; N]
//          | "S" <type>                  // [T]
//          | "T" {<type>} "E"            // (T1, T2, T3, ...)
//          | "R" [<lifetime>] <type>     // &T
//          | "Q" [<lifetime>] <type>     // &mut T
//          | "P" <type>                  // *const T
//          | "O" <type>                  // *mut T
//          | "F" <fn-sig>                // fn(...) -> ...
//          | "D" <dyn-bounds> <lifetime> // dyn Trait<Assoc = X> + Send + 'a
//          | <backref>                   // backref
void Demangler::demangleType() {
  if (Error || RecursionLevel >= MaxRecursionLevel) {
    Error = true;
    return;
  }
  ScopedOverride<size_t> SaveRecursionLevel(RecursionLevel, RecursionLevel + 1);

  size_t Start = Position;
  char C = consume();
  BasicType Type;
  if (parseBasicType(C, Type))
    return printBasicType(Type);

  switch (C) {
  case 'A':
    print("[");
    demangleType();
    print("; ");
    demangleConst();
    print("]");
    break;
  case 'S':
    print("[");
    demangleType();
    print("]");
    break;
  case 'T': {
    print("(");
    size_t I = 0;
    for (; !Error && !consumeIf('E'); ++I) {
      if (I > 0)
        print(", ");
      demangleType();
    }
    if (I == 1)
      print(",");
    print(")");
    break;
  }
  case 'R':
  case 'Q':
    print('&');
    if (consumeIf('L')) {
      if (auto Lifetime = parseBase62Number()) {
        printLifetime(Lifetime);
        print(' ');
      }
    }
    if (C == 'Q')
      print("mut ");
    demangleType();
    break;
  case 'P':
    print("*const ");
    demangleType();
    break;
  case 'O':
    print("*mut ");
    demangleType();
    break;
  case 'F':
    demangleFnSig();
    break;
  case 'D':
    demangleDynBounds();
    if (consumeIf('L')) {
      if (auto Lifetime = parseBase62Number()) {
        print(" + ");
        printLifetime(Lifetime);
      }
    } else {
      Error = true;
    }
    break;
  case 'B':
    demangleBackref([&] { demangleType(); });
    break;
  default:
    Position = Start;
    demanglePath(IsInType::Yes);
    break;
  }
}

// <fn-sig> := [<binder>] ["U"] ["K" <abi>] {<type>} "E" <type>
// <abi> = "C"
//       | <undisambiguated-identifier>
void Demangler::demangleFnSig() {
  ScopedOverride<size_t> SaveBoundLifetimes(BoundLifetimes, BoundLifetimes);
  demangleOptionalBinder();

  if (consumeIf('U'))
    print("unsafe ");

  if (consumeIf('K')) {
    print("extern \"");
    if (consumeIf('C')) {
      print("C");
    } else {
      Identifier Ident = parseIdentifier();
      if (Ident.Punycode)
        Error = true;
      for (char C : Ident.Name) {
        // When mangling ABI string, the "-" is replaced with "_".
        if (C == '_')
          C = '-';
        print(C);
      }
    }
    print("\" ");
  }

  print("fn(");
  for (size_t I = 0; !Error && !consumeIf('E'); ++I) {
    if (I > 0)
      print(", ");
    demangleType();
  }
  print(")");

  if (consumeIf('u')) {
    // Skip the unit type from the output.
  } else {
    print(" -> ");
    demangleType();
  }
}

// <dyn-bounds> = [<binder>] {<dyn-trait>} "E"
void Demangler::demangleDynBounds() {
  ScopedOverride<size_t> SaveBoundLifetimes(BoundLifetimes, BoundLifetimes);
  print("dyn ");
  demangleOptionalBinder();
  for (size_t I = 0; !Error && !consumeIf('E'); ++I) {
    if (I > 0)
      print(" + ");
    demangleDynTrait();
  }
}

// <dyn-trait> = <path> {<dyn-trait-assoc-binding>}
// <dyn-trait-assoc-binding> = "p" <undisambiguated-identifier> <type>
void Demangler::demangleDynTrait() {
  bool IsOpen = demanglePath(IsInType::Yes, LeaveGenericsOpen::Yes);
  while (!Error && consumeIf('p')) {
    if (!IsOpen) {
      IsOpen = true;
      print('<');
    } else {
      print(", ");
    }
    print(parseIdentifier().Name);
    print(" = ");
    demangleType();
  }
  if (IsOpen)
    print(">");
}

// Demangles optional binder and updates the number of bound lifetimes.
//
// <binder> = "G" <base-62-number>
void Demangler::demangleOptionalBinder() {
  uint64_t Binder = parseOptionalBase62Number('G');
  if (Error || Binder == 0)
    return;

  // In valid inputs each bound lifetime is referenced later. Referencing a
  // lifetime requires at least one byte of input. Reject inputs that are too
  // short to reference all bound lifetimes. Otherwise demangling of invalid
  // binders could generate excessive amounts of output.
  if (Binder >= Input.size() - BoundLifetimes) {
    Error = true;
    return;
  }

  print("for<");
  for (size_t I = 0; I != Binder; ++I) {
    BoundLifetimes += 1;
    if (I > 0)
      print(", ");
    printLifetime(1);
  }
  print("> ");
}

// <const> = <basic-type> <const-data>
//         | "p"                          // placeholder
//         | <backref>
void Demangler::demangleConst() {
  if (Error || RecursionLevel >= MaxRecursionLevel) {
    Error = true;
    return;
  }
  ScopedOverride<size_t> SaveRecursionLevel(RecursionLevel, RecursionLevel + 1);

  char C = consume();
  BasicType Type;
  if (parseBasicType(C, Type)) {
    switch (Type) {
    case BasicType::I8:
    case BasicType::I16:
    case BasicType::I32:
    case BasicType::I64:
    case BasicType::I128:
    case BasicType::ISize:
    case BasicType::U8:
    case BasicType::U16:
    case BasicType::U32:
    case BasicType::U64:
    case BasicType::U128:
    case BasicType::USize:
      demangleConstInt();
      break;
    case BasicType::Bool:
      demangleConstBool();
      break;
    case BasicType::Char:
      demangleConstChar();
      break;
    case BasicType::Placeholder:
      print('_');
      break;
    default:
      Error = true;
      break;
    }
  } else if (C == 'B') {
    demangleBackref([&] { demangleConst(); });
  } else {
    Error = true;
  }
}

// <const-data> = ["n"] <hex-number>
void Demangler::demangleConstInt() {
  if (consumeIf('n'))
    print('-');

  std::string_view HexDigits;
  uint64_t Value = parseHexNumber(HexDigits);
  if (HexDigits.size() <= 16) {
    printDecimalNumber(Value);
  } else {
    print("0x");
    print(HexDigits);
  }
}

// <const-data> = "0_" // false
//              | "1_" // true
void Demangler::demangleConstBool() {
  std::string_view HexDigits;
  parseHexNumber(HexDigits);
  if (HexDigits == "0")
    print("false");
  else if (HexDigits == "1")
    print("true");
  else
    Error = true;
}

/// Returns true if CodePoint represents a printable ASCII character.
static bool isAsciiPrintable(uint64_t CodePoint) {
  return 0x20 <= CodePoint && CodePoint <= 0x7e;
}

// <const-data> = <hex-number>
void Demangler::demangleConstChar() {
  std::string_view HexDigits;
  uint64_t CodePoint = parseHexNumber(HexDigits);
  if (Error || HexDigits.size() > 6) {
    Error = true;
    return;
  }

  print("'");
  switch (CodePoint) {
  case '\t':
    print(R"(\t)");
    break;
  case '\r':
    print(R"(\r)");
    break;
  case '\n':
    print(R"(\n)");
    break;
  case '\\':
    print(R"(\\)");
    break;
  case '"':
    print(R"(")");
    break;
  case '\'':
    print(R"(\')");
    break;
  default:
    if (isAsciiPrintable(CodePoint)) {
      char C = CodePoint;
      print(C);
    } else {
      print(R"(\u{)");
      print(HexDigits);
      print('}');
    }
    break;
  }
  print('\'');
}

// <undisambiguated-identifier> = ["u"] <decimal-number> ["_"] <bytes>
Identifier Demangler::parseIdentifier() {
  bool Punycode = consumeIf('u');
  uint64_t Bytes = parseDecimalNumber();

  // Underscore resolves the ambiguity when identifier starts with a decimal
  // digit or another underscore.
  consumeIf('_');

  if (Error || Bytes > Input.size() - Position) {
    Error = true;
    return {};
  }
  std::string_view S = Input.substr(Position, Bytes);
  Position += Bytes;

  if (!std::all_of(S.begin(), S.end(), isValid)) {
    Error = true;
    return {};
  }

  return {S, Punycode};
}

// Parses optional base 62 number. The presence of a number is determined using
// Tag. Returns 0 when tag is absent and parsed value + 1 otherwise
//
// This function is indended for parsing disambiguators and binders which when
// not present have their value interpreted as 0, and otherwise as decoded
// value + 1. For example for binders, value for "G_" is 1, for "G0_" value is
// 2. When "G" is absent value is 0.
uint64_t Demangler::parseOptionalBase62Number(char Tag) {
  if (!consumeIf(Tag))
    return 0;

  uint64_t N = parseBase62Number();
  if (Error || !addAssign(N, 1))
    return 0;

  return N;
}

// Parses base 62 number with <0-9a-zA-Z> as digits. Number is terminated by
// "_". All values are offset by 1, so that "_" encodes 0, "0_" encodes 1,
// "1_" encodes 2, etc.
//
// <base-62-number> = {<0-9a-zA-Z>} "_"
uint64_t Demangler::parseBase62Number() {
  if (consumeIf('_'))
    return 0;

  uint64_t Value = 0;

  while (true) {
    uint64_t Digit;
    char C = consume();

    if (C == '_') {
      break;
    } else if (isDigit(C)) {
      Digit = C - '0';
    } else if (isLower(C)) {
      Digit = 10 + (C - 'a');
    } else if (isUpper(C)) {
      Digit = 10 + 26 + (C - 'A');
    } else {
      Error = true;
      return 0;
    }

    if (!mulAssign(Value, 62))
      return 0;

    if (!addAssign(Value, Digit))
      return 0;
  }

  if (!addAssign(Value, 1))
    return 0;

  return Value;
}

// Parses a decimal number that had been encoded without any leading zeros.
//
// <decimal-number> = "0"
//                  | <1-9> {<0-9>}
uint64_t Demangler::parseDecimalNumber() {
  char C = look();
  if (!isDigit(C)) {
    Error = true;
    return 0;
  }

  if (C == '0') {
    consume();
    return 0;
  }

  uint64_t Value = 0;

  while (isDigit(look())) {
    if (!mulAssign(Value, 10)) {
      Error = true;
      return 0;
    }

    uint64_t D = consume() - '0';
    if (!addAssign(Value, D))
      return 0;
  }

  return Value;
}

// Parses a hexadecimal number with <0-9a-f> as a digits. Returns the parsed
// value and stores hex digits in HexDigits. The return value is unspecified if
// HexDigits.size() > 16.
//
// <hex-number> = "0_"
//              | <1-9a-f> {<0-9a-f>} "_"
uint64_t Demangler::parseHexNumber(std::string_view &HexDigits) {
  size_t Start = Position;
  uint64_t Value = 0;

  if (!isHexDigit(look()))
    Error = true;

  if (consumeIf('0')) {
    if (!consumeIf('_'))
      Error = true;
  } else {
    while (!Error && !consumeIf('_')) {
      char C = consume();
      Value *= 16;
      if (isDigit(C))
        Value += C - '0';
      else if ('a' <= C && C <= 'f')
        Value += 10 + (C - 'a');
      else
        Error = true;
    }
  }

  if (Error) {
    HexDigits = std::string_view();
    return 0;
  }

  size_t End = Position - 1;
  assert(Start < End);
  HexDigits = Input.substr(Start, End - Start);
  return Value;
}

void Demangler::print(char C) {
  if (Error || !Print)
    return;

  Output += C;
}

void Demangler::print(std::string_view S) {
  if (Error || !Print)
    return;

  Output += S;
}

void Demangler::printDecimalNumber(uint64_t N) {
  if (Error || !Print)
    return;

  Output << N;
}

// Prints a lifetime. An index 0 always represents an erased lifetime. Indices
// starting from 1, are De Bruijn indices, referring to higher-ranked lifetimes
// bound by one of the enclosing binders.
void Demangler::printLifetime(uint64_t Index) {
  if (Index == 0) {
    print("'_");
    return;
  }

  if (Index - 1 >= BoundLifetimes) {
    Error = true;
    return;
  }

  uint64_t Depth = BoundLifetimes - Index;
  print('\'');
  if (Depth < 26) {
    char C = 'a' + Depth;
    print(C);
  } else {
    print('z');
    printDecimalNumber(Depth - 26 + 1);
  }
}

static inline bool decodePunycodeDigit(char C, size_t &Value) {
  if (isLower(C)) {
    Value = C - 'a';
    return true;
  }

  if (isDigit(C)) {
    Value = 26 + (C - '0');
    return true;
  }

  return false;
}

static void removeNullBytes(OutputBuffer &Output, size_t StartIdx) {
  char *Buffer = Output.getBuffer();
  char *Start = Buffer + StartIdx;
  char *End = Buffer + Output.getCurrentPosition();
  Output.setCurrentPosition(std::remove(Start, End, '\0') - Buffer);
}

// Encodes code point as UTF-8 and stores results in Output. Returns false if
// CodePoint is not a valid unicode scalar value.
static inline bool encodeUTF8(size_t CodePoint, char *Output) {
  if (0xD800 <= CodePoint && CodePoint <= 0xDFFF)
    return false;

  if (CodePoint <= 0x7F) {
    Output[0] = CodePoint;
    return true;
  }

  if (CodePoint <= 0x7FF) {
    Output[0] = 0xC0 | ((CodePoint >> 6) & 0x3F);
    Output[1] = 0x80 | (CodePoint & 0x3F);
    return true;
  }

  if (CodePoint <= 0xFFFF) {
    Output[0] = 0xE0 | (CodePoint >> 12);
    Output[1] = 0x80 | ((CodePoint >> 6) & 0x3F);
    Output[2] = 0x80 | (CodePoint & 0x3F);
    return true;
  }

  if (CodePoint <= 0x10FFFF) {
    Output[0] = 0xF0 | (CodePoint >> 18);
    Output[1] = 0x80 | ((CodePoint >> 12) & 0x3F);
    Output[2] = 0x80 | ((CodePoint >> 6) & 0x3F);
    Output[3] = 0x80 | (CodePoint & 0x3F);
    return true;
  }

  return false;
}

// Decodes string encoded using punycode and appends results to Output.
// Returns true if decoding was successful.
static bool decodePunycode(std::string_view Input, OutputBuffer &Output) {
  size_t OutputSize = Output.getCurrentPosition();
  size_t InputIdx = 0;

  // Rust uses an underscore as a delimiter.
  size_t DelimiterPos = std::string_view::npos;
  for (size_t I = 0; I != Input.size(); ++I)
    if (Input[I] == '_')
      DelimiterPos = I;

  if (DelimiterPos != std::string_view::npos) {
    // Copy basic code points before the last delimiter to the output.
    for (; InputIdx != DelimiterPos; ++InputIdx) {
      char C = Input[InputIdx];
      if (!isValid(C))
        return false;
      // Code points are padded with zeros while decoding is in progress.
      char UTF8[4] = {C};
      Output += std::string_view(UTF8, 4);
    }
    // Skip over the delimiter.
    ++InputIdx;
  }

  size_t Base = 36;
  size_t Skew = 38;
  size_t Bias = 72;
  size_t N = 0x80;
  size_t TMin = 1;
  size_t TMax = 26;
  size_t Damp = 700;

  auto Adapt = [&](size_t Delta, size_t NumPoints) {
    Delta /= Damp;
    Delta += Delta / NumPoints;
    Damp = 2;

    size_t K = 0;
    while (Delta > (Base - TMin) * TMax / 2) {
      Delta /= Base - TMin;
      K += Base;
    }
    return K + (((Base - TMin + 1) * Delta) / (Delta + Skew));
  };

  // Main decoding loop.
  for (size_t I = 0; InputIdx != Input.size(); I += 1) {
    size_t OldI = I;
    size_t W = 1;
    size_t Max = std::numeric_limits<size_t>::max();
    for (size_t K = Base; true; K += Base) {
      if (InputIdx == Input.size())
        return false;
      char C = Input[InputIdx++];
      size_t Digit = 0;
      if (!decodePunycodeDigit(C, Digit))
        return false;

      if (Digit > (Max - I) / W)
        return false;
      I += Digit * W;

      size_t T;
      if (K <= Bias)
        T = TMin;
      else if (K >= Bias + TMax)
        T = TMax;
      else
        T = K - Bias;

      if (Digit < T)
        break;

      if (W > Max / (Base - T))
        return false;
      W *= (Base - T);
    }
    size_t NumPoints = (Output.getCurrentPosition() - OutputSize) / 4 + 1;
    Bias = Adapt(I - OldI, NumPoints);

    if (I / NumPoints > Max - N)
      return false;
    N += I / NumPoints;
    I = I % NumPoints;

    // Insert N at position I in the output.
    char UTF8[4] = {};
    if (!encodeUTF8(N, UTF8))
      return false;
    Output.insert(OutputSize + I * 4, UTF8, 4);
  }

  removeNullBytes(Output, OutputSize);
  return true;
}

void Demangler::printIdentifier(Identifier Ident) {
  if (Error || !Print)
    return;

  if (Ident.Punycode) {
    if (!decodePunycode(Ident.Name, Output))
      Error = true;
  } else {
    print(Ident.Name);
  }
}

char Demangler::look() const {
  if (Error || Position >= Input.size())
    return 0;

  return Input[Position];
}

char Demangler::consume() {
  if (Error || Position >= Input.size()) {
    Error = true;
    return 0;
  }

  return Input[Position++];
}

bool Demangler::consumeIf(char Prefix) {
  if (Error || Position >= Input.size() || Input[Position] != Prefix)
    return false;

  Position += 1;
  return true;
}

/// Computes A + B. When computation wraps around sets the error and returns
/// false. Otherwise assigns the result to A and returns true.
bool Demangler::addAssign(uint64_t &A, uint64_t B) {
  if (A > std::numeric_limits<uint64_t>::max() - B) {
    Error = true;
    return false;
  }

  A += B;
  return true;
}

/// Computes A * B. When computation wraps around sets the error and returns
/// false. Otherwise assigns the result to A and returns true.
bool Demangler::mulAssign(uint64_t &A, uint64_t B) {
  if (B != 0 && A > std::numeric_limits<uint64_t>::max() / B) {
    Error = true;
    return false;
  }

  A *= B;
  return true;
}
