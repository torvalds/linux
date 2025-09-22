//===- SveEmitter.cpp - Generate arm_sve.h for use with clang -*- C++ -*-===//
//
//  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
//  See https://llvm.org/LICENSE.txt for license information.
//  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This tablegen backend is responsible for emitting arm_sve.h, which includes
// a declaration and definition of each function specified by the ARM C/C++
// Language Extensions (ACLE).
//
// For details, visit:
//  https://developer.arm.com/architectures/system-architectures/software-standards/acle
//
// Each SVE instruction is implemented in terms of 1 or more functions which
// are suffixed with the element type of the input vectors.  Functions may be
// implemented in terms of generic vector operations such as +, *, -, etc. or
// by calling a __builtin_-prefixed function which will be handled by clang's
// CodeGen library.
//
// See also the documentation in include/clang/Basic/arm_sve.td.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include <array>
#include <cctype>
#include <set>
#include <sstream>
#include <string>
#include <tuple>

using namespace llvm;

enum ClassKind {
  ClassNone,
  ClassS,     // signed/unsigned, e.g., "_s8", "_u8" suffix
  ClassG,     // Overloaded name without type suffix
};

enum class ACLEKind { SVE, SME };

using TypeSpec = std::string;

namespace {

class ImmCheck {
  unsigned Arg;
  unsigned Kind;
  unsigned ElementSizeInBits;

public:
  ImmCheck(unsigned Arg, unsigned Kind, unsigned ElementSizeInBits = 0)
      : Arg(Arg), Kind(Kind), ElementSizeInBits(ElementSizeInBits) {}
  ImmCheck(const ImmCheck &Other) = default;
  ~ImmCheck() = default;

  unsigned getArg() const { return Arg; }
  unsigned getKind() const { return Kind; }
  unsigned getElementSizeInBits() const { return ElementSizeInBits; }
};

class SVEType {
  bool Float, Signed, Immediate, Void, Constant, Pointer, BFloat;
  bool DefaultType, IsScalable, Predicate, PredicatePattern, PrefetchOp,
      Svcount;
  unsigned Bitwidth, ElementBitwidth, NumVectors;

public:
  SVEType() : SVEType("", 'v') {}

  SVEType(StringRef TS, char CharMod, unsigned NumVectors = 1)
      : Float(false), Signed(true), Immediate(false), Void(false),
        Constant(false), Pointer(false), BFloat(false), DefaultType(false),
        IsScalable(true), Predicate(false), PredicatePattern(false),
        PrefetchOp(false), Svcount(false), Bitwidth(128), ElementBitwidth(~0U),
        NumVectors(NumVectors) {
    if (!TS.empty())
      applyTypespec(TS);
    applyModifier(CharMod);
  }

  SVEType(const SVEType &Base, unsigned NumV) : SVEType(Base) {
    NumVectors = NumV;
  }

  bool isPointer() const { return Pointer; }
  bool isVoidPointer() const { return Pointer && Void; }
  bool isSigned() const { return Signed; }
  bool isImmediate() const { return Immediate; }
  bool isScalar() const { return NumVectors == 0; }
  bool isVector() const { return NumVectors > 0; }
  bool isScalableVector() const { return isVector() && IsScalable; }
  bool isFixedLengthVector() const { return isVector() && !IsScalable; }
  bool isChar() const { return ElementBitwidth == 8; }
  bool isVoid() const { return Void && !Pointer; }
  bool isDefault() const { return DefaultType; }
  bool isFloat() const { return Float && !BFloat; }
  bool isBFloat() const { return BFloat && !Float; }
  bool isFloatingPoint() const { return Float || BFloat; }
  bool isInteger() const {
    return !isFloatingPoint() && !Predicate && !Svcount;
  }
  bool isScalarPredicate() const {
    return !isFloatingPoint() && Predicate && NumVectors == 0;
  }
  bool isPredicateVector() const { return Predicate; }
  bool isPredicatePattern() const { return PredicatePattern; }
  bool isPrefetchOp() const { return PrefetchOp; }
  bool isSvcount() const { return Svcount; }
  bool isConstant() const { return Constant; }
  unsigned getElementSizeInBits() const { return ElementBitwidth; }
  unsigned getNumVectors() const { return NumVectors; }

  unsigned getNumElements() const {
    assert(ElementBitwidth != ~0U);
    return Bitwidth / ElementBitwidth;
  }
  unsigned getSizeInBits() const {
    return Bitwidth;
  }

  /// Return the string representation of a type, which is an encoded
  /// string for passing to the BUILTIN() macro in Builtins.def.
  std::string builtin_str() const;

  /// Return the C/C++ string representation of a type for use in the
  /// arm_sve.h header file.
  std::string str() const;

private:
  /// Creates the type based on the typespec string in TS.
  void applyTypespec(StringRef TS);

  /// Applies a prototype modifier to the type.
  void applyModifier(char Mod);
};

class SVEEmitter;

/// The main grunt class. This represents an instantiation of an intrinsic with
/// a particular typespec and prototype.
class Intrinsic {
  /// The unmangled name.
  std::string Name;

  /// The name of the corresponding LLVM IR intrinsic.
  std::string LLVMName;

  /// Intrinsic prototype.
  std::string Proto;

  /// The base type spec for this intrinsic.
  TypeSpec BaseTypeSpec;

  /// The base class kind. Most intrinsics use ClassS, which has full type
  /// info for integers (_s32/_u32), or ClassG which is used for overloaded
  /// intrinsics.
  ClassKind Class;

  /// The architectural #ifdef guard.
  std::string SVEGuard, SMEGuard;

  // The merge suffix such as _m, _x or _z.
  std::string MergeSuffix;

  /// The types of return value [0] and parameters [1..].
  std::vector<SVEType> Types;

  /// The "base type", which is VarType('d', BaseTypeSpec).
  SVEType BaseType;

  uint64_t Flags;

  SmallVector<ImmCheck, 2> ImmChecks;

public:
  Intrinsic(StringRef Name, StringRef Proto, uint64_t MergeTy,
            StringRef MergeSuffix, uint64_t MemoryElementTy, StringRef LLVMName,
            uint64_t Flags, ArrayRef<ImmCheck> ImmChecks, TypeSpec BT,
            ClassKind Class, SVEEmitter &Emitter, StringRef SVEGuard,
            StringRef SMEGuard);

  ~Intrinsic()=default;

  std::string getName() const { return Name; }
  std::string getLLVMName() const { return LLVMName; }
  std::string getProto() const { return Proto; }
  TypeSpec getBaseTypeSpec() const { return BaseTypeSpec; }
  SVEType getBaseType() const { return BaseType; }

  StringRef getSVEGuard() const { return SVEGuard; }
  StringRef getSMEGuard() const { return SMEGuard; }
  void printGuard(raw_ostream &OS) const {
    if (!SVEGuard.empty() && SMEGuard.empty())
      OS << SVEGuard;
    else if (SVEGuard.empty() && !SMEGuard.empty())
      OS << SMEGuard;
    else {
      if (SVEGuard.find(",") != std::string::npos ||
          SVEGuard.find("|") != std::string::npos)
        OS << "(" << SVEGuard << ")";
      else
        OS << SVEGuard;
      OS << "|";
      if (SMEGuard.find(",") != std::string::npos ||
          SMEGuard.find("|") != std::string::npos)
        OS << "(" << SMEGuard << ")";
      else
        OS << SMEGuard;
    }
  }
  ClassKind getClassKind() const { return Class; }

  SVEType getReturnType() const { return Types[0]; }
  ArrayRef<SVEType> getTypes() const { return Types; }
  SVEType getParamType(unsigned I) const { return Types[I + 1]; }
  unsigned getNumParams() const {
    return Proto.size() - (2 * llvm::count(Proto, '.')) - 1;
  }

  uint64_t getFlags() const { return Flags; }
  bool isFlagSet(uint64_t Flag) const { return Flags & Flag;}

  ArrayRef<ImmCheck> getImmChecks() const { return ImmChecks; }

  /// Return the type string for a BUILTIN() macro in Builtins.def.
  std::string getBuiltinTypeStr();

  /// Return the name, mangled with type information. The name is mangled for
  /// ClassS, so will add type suffixes such as _u32/_s32.
  std::string getMangledName() const { return mangleName(ClassS); }

  /// As above, but mangles the LLVM name instead.
  std::string getMangledLLVMName() const { return mangleLLVMName(); }

  /// Returns true if the intrinsic is overloaded, in that it should also generate
  /// a short form without the type-specifiers, e.g. 'svld1(..)' instead of
  /// 'svld1_u32(..)'.
  static bool isOverloadedIntrinsic(StringRef Name) {
    auto BrOpen = Name.find('[');
    auto BrClose = Name.find(']');
    return BrOpen != std::string::npos && BrClose != std::string::npos;
  }

  /// Return true if the intrinsic takes a splat operand.
  bool hasSplat() const {
    // These prototype modifiers are described in arm_sve.td.
    return Proto.find_first_of("ajfrKLR@") != std::string::npos;
  }

  /// Return the parameter index of the splat operand.
  unsigned getSplatIdx() const {
    unsigned I = 1, Param = 0;
    for (; I < Proto.size(); ++I, ++Param) {
      if (Proto[I] == 'a' || Proto[I] == 'j' || Proto[I] == 'f' ||
          Proto[I] == 'r' || Proto[I] == 'K' || Proto[I] == 'L' ||
          Proto[I] == 'R' || Proto[I] == '@')
        break;

      // Multivector modifier can be skipped
      if (Proto[I] == '.')
        I += 2;
    }
    assert(I != Proto.size() && "Prototype has no splat operand");
    return Param;
  }

  /// Emits the intrinsic declaration to the ostream.
  void emitIntrinsic(raw_ostream &OS, SVEEmitter &Emitter, ACLEKind Kind) const;

private:
  std::string getMergeSuffix() const { return MergeSuffix; }
  std::string mangleName(ClassKind LocalCK) const;
  std::string mangleLLVMName() const;
  std::string replaceTemplatedArgs(std::string Name, TypeSpec TS,
                                   std::string Proto) const;
};

class SVEEmitter {
private:
  // The reinterpret builtins are generated separately because they
  // need the cross product of all types (121 functions in total),
  // which is inconvenient to specify in the arm_sve.td file or
  // generate in CGBuiltin.cpp.
  struct ReinterpretTypeInfo {
    SVEType BaseType;
    const char *Suffix;
  };

  static const std::array<ReinterpretTypeInfo, 12> Reinterprets;

  RecordKeeper &Records;
  llvm::StringMap<uint64_t> EltTypes;
  llvm::StringMap<uint64_t> MemEltTypes;
  llvm::StringMap<uint64_t> FlagTypes;
  llvm::StringMap<uint64_t> MergeTypes;
  llvm::StringMap<uint64_t> ImmCheckTypes;

public:
  SVEEmitter(RecordKeeper &R) : Records(R) {
    for (auto *RV : Records.getAllDerivedDefinitions("EltType"))
      EltTypes[RV->getNameInitAsString()] = RV->getValueAsInt("Value");
    for (auto *RV : Records.getAllDerivedDefinitions("MemEltType"))
      MemEltTypes[RV->getNameInitAsString()] = RV->getValueAsInt("Value");
    for (auto *RV : Records.getAllDerivedDefinitions("FlagType"))
      FlagTypes[RV->getNameInitAsString()] = RV->getValueAsInt("Value");
    for (auto *RV : Records.getAllDerivedDefinitions("MergeType"))
      MergeTypes[RV->getNameInitAsString()] = RV->getValueAsInt("Value");
    for (auto *RV : Records.getAllDerivedDefinitions("ImmCheckType"))
      ImmCheckTypes[RV->getNameInitAsString()] = RV->getValueAsInt("Value");
  }

  /// Returns the enum value for the immcheck type
  unsigned getEnumValueForImmCheck(StringRef C) const {
    auto It = ImmCheckTypes.find(C);
    if (It != ImmCheckTypes.end())
      return It->getValue();
    llvm_unreachable("Unsupported imm check");
  }

  /// Returns the enum value for the flag type
  uint64_t getEnumValueForFlag(StringRef C) const {
    auto Res = FlagTypes.find(C);
    if (Res != FlagTypes.end())
      return Res->getValue();
    llvm_unreachable("Unsupported flag");
  }

  // Returns the SVETypeFlags for a given value and mask.
  uint64_t encodeFlag(uint64_t V, StringRef MaskName) const {
    auto It = FlagTypes.find(MaskName);
    if (It != FlagTypes.end()) {
      uint64_t Mask = It->getValue();
      unsigned Shift = llvm::countr_zero(Mask);
      assert(Shift < 64 && "Mask value produced an invalid shift value");
      return (V << Shift) & Mask;
    }
    llvm_unreachable("Unsupported flag");
  }

  // Returns the SVETypeFlags for the given element type.
  uint64_t encodeEltType(StringRef EltName) {
    auto It = EltTypes.find(EltName);
    if (It != EltTypes.end())
      return encodeFlag(It->getValue(), "EltTypeMask");
    llvm_unreachable("Unsupported EltType");
  }

  // Returns the SVETypeFlags for the given memory element type.
  uint64_t encodeMemoryElementType(uint64_t MT) {
    return encodeFlag(MT, "MemEltTypeMask");
  }

  // Returns the SVETypeFlags for the given merge type.
  uint64_t encodeMergeType(uint64_t MT) {
    return encodeFlag(MT, "MergeTypeMask");
  }

  // Returns the SVETypeFlags for the given splat operand.
  unsigned encodeSplatOperand(unsigned SplatIdx) {
    assert(SplatIdx < 7 && "SplatIdx out of encodable range");
    return encodeFlag(SplatIdx + 1, "SplatOperandMask");
  }

  // Returns the SVETypeFlags value for the given SVEType.
  uint64_t encodeTypeFlags(const SVEType &T);

  /// Emit arm_sve.h.
  void createHeader(raw_ostream &o);

  // Emits core intrinsics in both arm_sme.h and arm_sve.h
  void createCoreHeaderIntrinsics(raw_ostream &o, SVEEmitter &Emitter,
                                  ACLEKind Kind);

  /// Emit all the __builtin prototypes and code needed by Sema.
  void createBuiltins(raw_ostream &o);

  /// Emit all the information needed to map builtin -> LLVM IR intrinsic.
  void createCodeGenMap(raw_ostream &o);

  /// Emit all the range checks for the immediates.
  void createRangeChecks(raw_ostream &o);

  /// Create the SVETypeFlags used in CGBuiltins
  void createTypeFlags(raw_ostream &o);

  /// Emit arm_sme.h.
  void createSMEHeader(raw_ostream &o);

  /// Emit all the SME __builtin prototypes and code needed by Sema.
  void createSMEBuiltins(raw_ostream &o);

  /// Emit all the information needed to map builtin -> LLVM IR intrinsic.
  void createSMECodeGenMap(raw_ostream &o);

  /// Create a table for a builtin's requirement for PSTATE.SM.
  void createStreamingAttrs(raw_ostream &o, ACLEKind Kind);

  /// Emit all the range checks for the immediates.
  void createSMERangeChecks(raw_ostream &o);

  /// Create a table for a builtin's requirement for PSTATE.ZA.
  void createBuiltinZAState(raw_ostream &OS);

  /// Create intrinsic and add it to \p Out
  void createIntrinsic(Record *R,
                       SmallVectorImpl<std::unique_ptr<Intrinsic>> &Out);
};

const std::array<SVEEmitter::ReinterpretTypeInfo, 12> SVEEmitter::Reinterprets =
    {{{SVEType("c", 'd'), "s8"},
      {SVEType("Uc", 'd'), "u8"},
      {SVEType("s", 'd'), "s16"},
      {SVEType("Us", 'd'), "u16"},
      {SVEType("i", 'd'), "s32"},
      {SVEType("Ui", 'd'), "u32"},
      {SVEType("l", 'd'), "s64"},
      {SVEType("Ul", 'd'), "u64"},
      {SVEType("h", 'd'), "f16"},
      {SVEType("b", 'd'), "bf16"},
      {SVEType("f", 'd'), "f32"},
      {SVEType("d", 'd'), "f64"}}};

} // end anonymous namespace


//===----------------------------------------------------------------------===//
// Type implementation
//===----------------------------------------------------------------------===//

std::string SVEType::builtin_str() const {
  std::string S;
  if (isVoid())
    return "v";

  if (isScalarPredicate())
    return "b";

  if (isSvcount())
    return "Qa";

  if (isVoidPointer())
    S += "v";
  else if (!isFloatingPoint())
    switch (ElementBitwidth) {
    case 1: S += "b"; break;
    case 8: S += "c"; break;
    case 16: S += "s"; break;
    case 32: S += "i"; break;
    case 64: S += "Wi"; break;
    case 128: S += "LLLi"; break;
    default: llvm_unreachable("Unhandled case!");
    }
  else if (isFloat())
    switch (ElementBitwidth) {
    case 16: S += "h"; break;
    case 32: S += "f"; break;
    case 64: S += "d"; break;
    default: llvm_unreachable("Unhandled case!");
    }
  else if (isBFloat()) {
    assert(ElementBitwidth == 16 && "Not a valid BFloat.");
    S += "y";
  }

  if (!isFloatingPoint()) {
    if ((isChar() || isPointer()) && !isVoidPointer()) {
      // Make chars and typed pointers explicitly signed.
      if (Signed)
        S = "S" + S;
      else if (!Signed)
        S = "U" + S;
    } else if (!isVoidPointer() && !Signed) {
      S = "U" + S;
    }
  }

  // Constant indices are "int", but have the "constant expression" modifier.
  if (isImmediate()) {
    assert(!isFloat() && "fp immediates are not supported");
    S = "I" + S;
  }

  if (isScalar()) {
    if (Constant) S += "C";
    if (Pointer) S += "*";
    return S;
  }

  if (isFixedLengthVector())
    return "V" + utostr(getNumElements() * NumVectors) + S;
  return "q" + utostr(getNumElements() * NumVectors) + S;
}

std::string SVEType::str() const {
  if (isPredicatePattern())
    return "enum svpattern";

  if (isPrefetchOp())
    return "enum svprfop";

  std::string S;
  if (Void)
    S += "void";
  else {
    if (isScalableVector() || isSvcount())
      S += "sv";
    if (!Signed && !isFloatingPoint())
      S += "u";

    if (Float)
      S += "float";
    else if (isSvcount())
      S += "count";
    else if (isScalarPredicate() || isPredicateVector())
      S += "bool";
    else if (isBFloat())
      S += "bfloat";
    else
      S += "int";

    if (!isScalarPredicate() && !isPredicateVector() && !isSvcount())
      S += utostr(ElementBitwidth);
    if (isFixedLengthVector())
      S += "x" + utostr(getNumElements());
    if (NumVectors > 1)
      S += "x" + utostr(NumVectors);
    if (!isScalarPredicate())
      S += "_t";
  }

  if (Constant)
    S += " const";
  if (Pointer)
    S += " *";

  return S;
}

void SVEType::applyTypespec(StringRef TS) {
  for (char I : TS) {
    switch (I) {
    case 'Q':
      Svcount = true;
      break;
    case 'P':
      Predicate = true;
      break;
    case 'U':
      Signed = false;
      break;
    case 'c':
      ElementBitwidth = 8;
      break;
    case 's':
      ElementBitwidth = 16;
      break;
    case 'i':
      ElementBitwidth = 32;
      break;
    case 'l':
      ElementBitwidth = 64;
      break;
    case 'q':
      ElementBitwidth = 128;
      break;
    case 'h':
      Float = true;
      ElementBitwidth = 16;
      break;
    case 'f':
      Float = true;
      ElementBitwidth = 32;
      break;
    case 'd':
      Float = true;
      ElementBitwidth = 64;
      break;
    case 'b':
      BFloat = true;
      Float = false;
      ElementBitwidth = 16;
      break;
    default:
      llvm_unreachable("Unhandled type code!");
    }
  }
  assert(ElementBitwidth != ~0U && "Bad element bitwidth!");
}

void SVEType::applyModifier(char Mod) {
  switch (Mod) {
  case 'v':
    Void = true;
    break;
  case 'd':
    DefaultType = true;
    break;
  case 'c':
    Constant = true;
    [[fallthrough]];
  case 'p':
    Pointer = true;
    Bitwidth = ElementBitwidth;
    NumVectors = 0;
    break;
  case 'e':
    Signed = false;
    ElementBitwidth /= 2;
    break;
  case 'h':
    ElementBitwidth /= 2;
    break;
  case 'q':
    ElementBitwidth /= 4;
    break;
  case 'b':
    Signed = false;
    Float = false;
    BFloat = false;
    ElementBitwidth /= 4;
    break;
  case 'o':
    ElementBitwidth *= 4;
    break;
  case 'P':
    Signed = true;
    Float = false;
    BFloat = false;
    Predicate = true;
    Svcount = false;
    Bitwidth = 16;
    ElementBitwidth = 1;
    break;
  case '{':
    IsScalable = false;
    Bitwidth = 128;
    NumVectors = 1;
    break;
  case 's':
  case 'a':
    Bitwidth = ElementBitwidth;
    NumVectors = 0;
    break;
  case 'R':
    ElementBitwidth /= 2;
    NumVectors = 0;
    break;
  case 'r':
    ElementBitwidth /= 4;
    NumVectors = 0;
    break;
  case '@':
    Signed = false;
    Float = false;
    BFloat = false;
    ElementBitwidth /= 4;
    NumVectors = 0;
    break;
  case 'K':
    Signed = true;
    Float = false;
    BFloat = false;
    Bitwidth = ElementBitwidth;
    NumVectors = 0;
    break;
  case 'L':
    Signed = false;
    Float = false;
    BFloat = false;
    Bitwidth = ElementBitwidth;
    NumVectors = 0;
    break;
  case 'u':
    Predicate = false;
    Svcount = false;
    Signed = false;
    Float = false;
    BFloat = false;
    break;
  case 'x':
    Predicate = false;
    Svcount = false;
    Signed = true;
    Float = false;
    BFloat = false;
    break;
  case 'i':
    Predicate = false;
    Svcount = false;
    Float = false;
    BFloat = false;
    ElementBitwidth = Bitwidth = 64;
    NumVectors = 0;
    Signed = false;
    Immediate = true;
    break;
  case 'I':
    Predicate = false;
    Svcount = false;
    Float = false;
    BFloat = false;
    ElementBitwidth = Bitwidth = 32;
    NumVectors = 0;
    Signed = true;
    Immediate = true;
    PredicatePattern = true;
    break;
  case 'J':
    Predicate = false;
    Svcount = false;
    Float = false;
    BFloat = false;
    ElementBitwidth = Bitwidth = 32;
    NumVectors = 0;
    Signed = true;
    Immediate = true;
    PrefetchOp = true;
    break;
  case 'k':
    Predicate = false;
    Svcount = false;
    Signed = true;
    Float = false;
    BFloat = false;
    ElementBitwidth = Bitwidth = 32;
    NumVectors = 0;
    break;
  case 'l':
    Predicate = false;
    Svcount = false;
    Signed = true;
    Float = false;
    BFloat = false;
    ElementBitwidth = Bitwidth = 64;
    NumVectors = 0;
    break;
  case 'm':
    Predicate = false;
    Svcount = false;
    Signed = false;
    Float = false;
    BFloat = false;
    ElementBitwidth = Bitwidth = 32;
    NumVectors = 0;
    break;
  case 'n':
    Predicate = false;
    Svcount = false;
    Signed = false;
    Float = false;
    BFloat = false;
    ElementBitwidth = Bitwidth = 64;
    NumVectors = 0;
    break;
  case 'w':
    ElementBitwidth = 64;
    break;
  case 'j':
    ElementBitwidth = Bitwidth = 64;
    NumVectors = 0;
    break;
  case 'f':
    Signed = false;
    ElementBitwidth = Bitwidth = 64;
    NumVectors = 0;
    break;
  case 'g':
    Signed = false;
    Float = false;
    BFloat = false;
    ElementBitwidth = 64;
    break;
  case '[':
    Signed = false;
    Float = false;
    BFloat = false;
    ElementBitwidth = 8;
    break;
  case 't':
    Signed = true;
    Float = false;
    BFloat = false;
    ElementBitwidth = 32;
    break;
  case 'z':
    Signed = false;
    Float = false;
    BFloat = false;
    ElementBitwidth = 32;
    break;
  case 'O':
    Predicate = false;
    Svcount = false;
    Float = true;
    ElementBitwidth = 16;
    break;
  case 'M':
    Predicate = false;
    Svcount = false;
    Float = true;
    BFloat = false;
    ElementBitwidth = 32;
    break;
  case 'N':
    Predicate = false;
    Svcount = false;
    Float = true;
    ElementBitwidth = 64;
    break;
  case 'Q':
    Constant = true;
    Pointer = true;
    Void = true;
    NumVectors = 0;
    break;
  case 'S':
    Constant = true;
    Pointer = true;
    ElementBitwidth = Bitwidth = 8;
    NumVectors = 0;
    Signed = true;
    break;
  case 'W':
    Constant = true;
    Pointer = true;
    ElementBitwidth = Bitwidth = 8;
    NumVectors = 0;
    Signed = false;
    break;
  case 'T':
    Constant = true;
    Pointer = true;
    ElementBitwidth = Bitwidth = 16;
    NumVectors = 0;
    Signed = true;
    break;
  case 'X':
    Constant = true;
    Pointer = true;
    ElementBitwidth = Bitwidth = 16;
    NumVectors = 0;
    Signed = false;
    break;
  case 'Y':
    Constant = true;
    Pointer = true;
    ElementBitwidth = Bitwidth = 32;
    NumVectors = 0;
    Signed = false;
    break;
  case 'U':
    Constant = true;
    Pointer = true;
    ElementBitwidth = Bitwidth = 32;
    NumVectors = 0;
    Signed = true;
    break;
  case '%':
    Pointer = true;
    Void = true;
    NumVectors = 0;
    break;
  case 'A':
    Pointer = true;
    ElementBitwidth = Bitwidth = 8;
    NumVectors = 0;
    Signed = true;
    break;
  case 'B':
    Pointer = true;
    ElementBitwidth = Bitwidth = 16;
    NumVectors = 0;
    Signed = true;
    break;
  case 'C':
    Pointer = true;
    ElementBitwidth = Bitwidth = 32;
    NumVectors = 0;
    Signed = true;
    break;
  case 'D':
    Pointer = true;
    ElementBitwidth = Bitwidth = 64;
    NumVectors = 0;
    Signed = true;
    break;
  case 'E':
    Pointer = true;
    ElementBitwidth = Bitwidth = 8;
    NumVectors = 0;
    Signed = false;
    break;
  case 'F':
    Pointer = true;
    ElementBitwidth = Bitwidth = 16;
    NumVectors = 0;
    Signed = false;
    break;
  case 'G':
    Pointer = true;
    ElementBitwidth = Bitwidth = 32;
    NumVectors = 0;
    Signed = false;
    break;
  case '$':
    Predicate = false;
    Svcount = false;
    Float = false;
    BFloat = true;
    ElementBitwidth = 16;
    break;
  case '}':
    Predicate = false;
    Signed = true;
    Svcount = true;
    NumVectors = 0;
    Float = false;
    BFloat = false;
    break;
  case '.':
    llvm_unreachable(". is never a type in itself");
    break;
  default:
    llvm_unreachable("Unhandled character!");
  }
}

/// Returns the modifier and number of vectors for the given operand \p Op.
std::pair<char, unsigned> getProtoModifier(StringRef Proto, unsigned Op) {
  for (unsigned P = 0; !Proto.empty(); ++P) {
    unsigned NumVectors = 1;
    unsigned CharsToSkip = 1;
    char Mod = Proto[0];
    if (Mod == '2' || Mod == '3' || Mod == '4') {
      NumVectors = Mod - '0';
      Mod = 'd';
      if (Proto.size() > 1 && Proto[1] == '.') {
        Mod = Proto[2];
        CharsToSkip = 3;
      }
    }

    if (P == Op)
      return {Mod, NumVectors};

    Proto = Proto.drop_front(CharsToSkip);
  }
  llvm_unreachable("Unexpected Op");
}

//===----------------------------------------------------------------------===//
// Intrinsic implementation
//===----------------------------------------------------------------------===//

Intrinsic::Intrinsic(StringRef Name, StringRef Proto, uint64_t MergeTy,
                     StringRef MergeSuffix, uint64_t MemoryElementTy,
                     StringRef LLVMName, uint64_t Flags,
                     ArrayRef<ImmCheck> Checks, TypeSpec BT, ClassKind Class,
                     SVEEmitter &Emitter, StringRef SVEGuard,
                     StringRef SMEGuard)
    : Name(Name.str()), LLVMName(LLVMName), Proto(Proto.str()),
      BaseTypeSpec(BT), Class(Class), SVEGuard(SVEGuard.str()),
      SMEGuard(SMEGuard.str()), MergeSuffix(MergeSuffix.str()),
      BaseType(BT, 'd'), Flags(Flags), ImmChecks(Checks.begin(), Checks.end()) {
  // Types[0] is the return value.
  for (unsigned I = 0; I < (getNumParams() + 1); ++I) {
    char Mod;
    unsigned NumVectors;
    std::tie(Mod, NumVectors) = getProtoModifier(Proto, I);
    SVEType T(BaseTypeSpec, Mod, NumVectors);
    Types.push_back(T);

    // Add range checks for immediates
    if (I > 0) {
      if (T.isPredicatePattern())
        ImmChecks.emplace_back(
            I - 1, Emitter.getEnumValueForImmCheck("ImmCheck0_31"));
      else if (T.isPrefetchOp())
        ImmChecks.emplace_back(
            I - 1, Emitter.getEnumValueForImmCheck("ImmCheck0_13"));
    }
  }

  // Set flags based on properties
  this->Flags |= Emitter.encodeTypeFlags(BaseType);
  this->Flags |= Emitter.encodeMemoryElementType(MemoryElementTy);
  this->Flags |= Emitter.encodeMergeType(MergeTy);
  if (hasSplat())
    this->Flags |= Emitter.encodeSplatOperand(getSplatIdx());
}

std::string Intrinsic::getBuiltinTypeStr() {
  std::string S = getReturnType().builtin_str();
  for (unsigned I = 0; I < getNumParams(); ++I)
    S += getParamType(I).builtin_str();

  return S;
}

std::string Intrinsic::replaceTemplatedArgs(std::string Name, TypeSpec TS,
                                            std::string Proto) const {
  std::string Ret = Name;
  while (Ret.find('{') != std::string::npos) {
    size_t Pos = Ret.find('{');
    size_t End = Ret.find('}');
    unsigned NumChars = End - Pos + 1;
    assert(NumChars == 3 && "Unexpected template argument");

    SVEType T;
    char C = Ret[Pos+1];
    switch(C) {
    default:
      llvm_unreachable("Unknown predication specifier");
    case 'd':
      T = SVEType(TS, 'd');
      break;
    case '0':
    case '1':
    case '2':
    case '3':
      T = SVEType(TS, Proto[C - '0']);
      break;
    }

    // Replace templated arg with the right suffix (e.g. u32)
    std::string TypeCode;
    if (T.isInteger())
      TypeCode = T.isSigned() ? 's' : 'u';
    else if (T.isSvcount())
      TypeCode = 'c';
    else if (T.isPredicateVector())
      TypeCode = 'b';
    else if (T.isBFloat())
      TypeCode = "bf";
    else
      TypeCode = 'f';
    Ret.replace(Pos, NumChars, TypeCode + utostr(T.getElementSizeInBits()));
  }

  return Ret;
}

std::string Intrinsic::mangleLLVMName() const {
  std::string S = getLLVMName();

  // Replace all {d} like expressions with e.g. 'u32'
  return replaceTemplatedArgs(S, getBaseTypeSpec(), getProto());
}

std::string Intrinsic::mangleName(ClassKind LocalCK) const {
  std::string S = getName();

  if (LocalCK == ClassG) {
    // Remove the square brackets and everything in between.
    while (S.find('[') != std::string::npos) {
      auto Start = S.find('[');
      auto End = S.find(']');
      S.erase(Start, (End-Start)+1);
    }
  } else {
    // Remove the square brackets.
    while (S.find('[') != std::string::npos) {
      auto BrPos = S.find('[');
      if (BrPos != std::string::npos)
        S.erase(BrPos, 1);
      BrPos = S.find(']');
      if (BrPos != std::string::npos)
        S.erase(BrPos, 1);
    }
  }

  // Replace all {d} like expressions with e.g. 'u32'
  return replaceTemplatedArgs(S, getBaseTypeSpec(), getProto()) +
         getMergeSuffix();
}

void Intrinsic::emitIntrinsic(raw_ostream &OS, SVEEmitter &Emitter,
                              ACLEKind Kind) const {
  bool IsOverloaded = getClassKind() == ClassG && getProto().size() > 1;

  std::string FullName = mangleName(ClassS);
  std::string ProtoName = mangleName(getClassKind());
  OS << (IsOverloaded ? "__aio " : "__ai ")
     << "__attribute__((__clang_arm_builtin_alias(";

  switch (Kind) {
  case ACLEKind::SME:
    OS << "__builtin_sme_" << FullName << ")";
    break;
  case ACLEKind::SVE:
    OS << "__builtin_sve_" << FullName << ")";
    break;
  }

  OS << "))\n";

  OS << getTypes()[0].str() << " " << ProtoName << "(";
  for (unsigned I = 0; I < getTypes().size() - 1; ++I) {
    if (I != 0)
      OS << ", ";
    OS << getTypes()[I + 1].str();
  }
  OS << ");\n";
}

//===----------------------------------------------------------------------===//
// SVEEmitter implementation
//===----------------------------------------------------------------------===//
uint64_t SVEEmitter::encodeTypeFlags(const SVEType &T) {
  if (T.isFloat()) {
    switch (T.getElementSizeInBits()) {
    case 16:
      return encodeEltType("EltTyFloat16");
    case 32:
      return encodeEltType("EltTyFloat32");
    case 64:
      return encodeEltType("EltTyFloat64");
    default:
      llvm_unreachable("Unhandled float element bitwidth!");
    }
  }

  if (T.isBFloat()) {
    assert(T.getElementSizeInBits() == 16 && "Not a valid BFloat.");
    return encodeEltType("EltTyBFloat16");
  }

  if (T.isPredicateVector() || T.isSvcount()) {
    switch (T.getElementSizeInBits()) {
    case 8:
      return encodeEltType("EltTyBool8");
    case 16:
      return encodeEltType("EltTyBool16");
    case 32:
      return encodeEltType("EltTyBool32");
    case 64:
      return encodeEltType("EltTyBool64");
    default:
      llvm_unreachable("Unhandled predicate element bitwidth!");
    }
  }

  switch (T.getElementSizeInBits()) {
  case 8:
    return encodeEltType("EltTyInt8");
  case 16:
    return encodeEltType("EltTyInt16");
  case 32:
    return encodeEltType("EltTyInt32");
  case 64:
    return encodeEltType("EltTyInt64");
  case 128:
    return encodeEltType("EltTyInt128");
  default:
    llvm_unreachable("Unhandled integer element bitwidth!");
  }
}

void SVEEmitter::createIntrinsic(
    Record *R, SmallVectorImpl<std::unique_ptr<Intrinsic>> &Out) {
  StringRef Name = R->getValueAsString("Name");
  StringRef Proto = R->getValueAsString("Prototype");
  StringRef Types = R->getValueAsString("Types");
  StringRef SVEGuard = R->getValueAsString("SVETargetGuard");
  StringRef SMEGuard = R->getValueAsString("SMETargetGuard");
  StringRef LLVMName = R->getValueAsString("LLVMIntrinsic");
  uint64_t Merge = R->getValueAsInt("Merge");
  StringRef MergeSuffix = R->getValueAsString("MergeSuffix");
  uint64_t MemEltType = R->getValueAsInt("MemEltType");
  std::vector<Record*> FlagsList = R->getValueAsListOfDefs("Flags");
  std::vector<Record*> ImmCheckList = R->getValueAsListOfDefs("ImmChecks");

  int64_t Flags = 0;
  for (auto FlagRec : FlagsList)
    Flags |= FlagRec->getValueAsInt("Value");

  // Create a dummy TypeSpec for non-overloaded builtins.
  if (Types.empty()) {
    assert((Flags & getEnumValueForFlag("IsOverloadNone")) &&
           "Expect TypeSpec for overloaded builtin!");
    Types = "i";
  }

  // Extract type specs from string
  SmallVector<TypeSpec, 8> TypeSpecs;
  TypeSpec Acc;
  for (char I : Types) {
    Acc.push_back(I);
    if (islower(I)) {
      TypeSpecs.push_back(TypeSpec(Acc));
      Acc.clear();
    }
  }

  // Remove duplicate type specs.
  llvm::sort(TypeSpecs);
  TypeSpecs.erase(std::unique(TypeSpecs.begin(), TypeSpecs.end()),
                  TypeSpecs.end());

  // Create an Intrinsic for each type spec.
  for (auto TS : TypeSpecs) {
    // Collate a list of range/option checks for the immediates.
    SmallVector<ImmCheck, 2> ImmChecks;
    for (auto *R : ImmCheckList) {
      int64_t Arg = R->getValueAsInt("Arg");
      int64_t EltSizeArg = R->getValueAsInt("EltSizeArg");
      int64_t Kind = R->getValueAsDef("Kind")->getValueAsInt("Value");
      assert(Arg >= 0 && Kind >= 0 && "Arg and Kind must be nonnegative");

      unsigned ElementSizeInBits = 0;
      char Mod;
      unsigned NumVectors;
      std::tie(Mod, NumVectors) = getProtoModifier(Proto, EltSizeArg + 1);
      if (EltSizeArg >= 0)
        ElementSizeInBits = SVEType(TS, Mod, NumVectors).getElementSizeInBits();
      ImmChecks.push_back(ImmCheck(Arg, Kind, ElementSizeInBits));
    }

    Out.push_back(std::make_unique<Intrinsic>(
        Name, Proto, Merge, MergeSuffix, MemEltType, LLVMName, Flags, ImmChecks,
        TS, ClassS, *this, SVEGuard, SMEGuard));

    // Also generate the short-form (e.g. svadd_m) for the given type-spec.
    if (Intrinsic::isOverloadedIntrinsic(Name))
      Out.push_back(std::make_unique<Intrinsic>(
          Name, Proto, Merge, MergeSuffix, MemEltType, LLVMName, Flags,
          ImmChecks, TS, ClassG, *this, SVEGuard, SMEGuard));
  }
}

void SVEEmitter::createCoreHeaderIntrinsics(raw_ostream &OS,
                                            SVEEmitter &Emitter,
                                            ACLEKind Kind) {
  SmallVector<std::unique_ptr<Intrinsic>, 128> Defs;
  std::vector<Record *> RV = Records.getAllDerivedDefinitions("Inst");
  for (auto *R : RV)
    createIntrinsic(R, Defs);

  // Sort intrinsics in header file by following order/priority:
  // - Architectural guard (i.e. does it require SVE2 or SVE2_AES)
  // - Class (is intrinsic overloaded or not)
  // - Intrinsic name
  std::stable_sort(Defs.begin(), Defs.end(),
                   [](const std::unique_ptr<Intrinsic> &A,
                      const std::unique_ptr<Intrinsic> &B) {
                     auto ToTuple = [](const std::unique_ptr<Intrinsic> &I) {
                       return std::make_tuple(
                           I->getSVEGuard().str() + I->getSMEGuard().str(),
                           (unsigned)I->getClassKind(), I->getName());
                     };
                     return ToTuple(A) < ToTuple(B);
                   });

  // Actually emit the intrinsic declarations.
  for (auto &I : Defs)
    I->emitIntrinsic(OS, Emitter, Kind);
}

void SVEEmitter::createHeader(raw_ostream &OS) {
  OS << "/*===---- arm_sve.h - ARM SVE intrinsics "
        "-----------------------------------===\n"
        " *\n"
        " *\n"
        " * Part of the LLVM Project, under the Apache License v2.0 with LLVM "
        "Exceptions.\n"
        " * See https://llvm.org/LICENSE.txt for license information.\n"
        " * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception\n"
        " *\n"
        " *===-----------------------------------------------------------------"
        "------===\n"
        " */\n\n";

  OS << "#ifndef __ARM_SVE_H\n";
  OS << "#define __ARM_SVE_H\n\n";

  OS << "#if !defined(__LITTLE_ENDIAN__)\n";
  OS << "#error \"Big endian is currently not supported for arm_sve.h\"\n";
  OS << "#endif\n";

  OS << "#include <stdint.h>\n\n";
  OS << "#ifdef  __cplusplus\n";
  OS << "extern \"C\" {\n";
  OS << "#else\n";
  OS << "#include <stdbool.h>\n";
  OS << "#endif\n\n";

  OS << "typedef __fp16 float16_t;\n";
  OS << "typedef float float32_t;\n";
  OS << "typedef double float64_t;\n";

  OS << "typedef __SVInt8_t svint8_t;\n";
  OS << "typedef __SVInt16_t svint16_t;\n";
  OS << "typedef __SVInt32_t svint32_t;\n";
  OS << "typedef __SVInt64_t svint64_t;\n";
  OS << "typedef __SVUint8_t svuint8_t;\n";
  OS << "typedef __SVUint16_t svuint16_t;\n";
  OS << "typedef __SVUint32_t svuint32_t;\n";
  OS << "typedef __SVUint64_t svuint64_t;\n";
  OS << "typedef __SVFloat16_t svfloat16_t;\n\n";

  OS << "typedef __SVBfloat16_t svbfloat16_t;\n";

  OS << "#include <arm_bf16.h>\n";
  OS << "#include <arm_vector_types.h>\n";

  OS << "typedef __SVFloat32_t svfloat32_t;\n";
  OS << "typedef __SVFloat64_t svfloat64_t;\n";
  OS << "typedef __clang_svint8x2_t svint8x2_t;\n";
  OS << "typedef __clang_svint16x2_t svint16x2_t;\n";
  OS << "typedef __clang_svint32x2_t svint32x2_t;\n";
  OS << "typedef __clang_svint64x2_t svint64x2_t;\n";
  OS << "typedef __clang_svuint8x2_t svuint8x2_t;\n";
  OS << "typedef __clang_svuint16x2_t svuint16x2_t;\n";
  OS << "typedef __clang_svuint32x2_t svuint32x2_t;\n";
  OS << "typedef __clang_svuint64x2_t svuint64x2_t;\n";
  OS << "typedef __clang_svfloat16x2_t svfloat16x2_t;\n";
  OS << "typedef __clang_svfloat32x2_t svfloat32x2_t;\n";
  OS << "typedef __clang_svfloat64x2_t svfloat64x2_t;\n";
  OS << "typedef __clang_svint8x3_t svint8x3_t;\n";
  OS << "typedef __clang_svint16x3_t svint16x3_t;\n";
  OS << "typedef __clang_svint32x3_t svint32x3_t;\n";
  OS << "typedef __clang_svint64x3_t svint64x3_t;\n";
  OS << "typedef __clang_svuint8x3_t svuint8x3_t;\n";
  OS << "typedef __clang_svuint16x3_t svuint16x3_t;\n";
  OS << "typedef __clang_svuint32x3_t svuint32x3_t;\n";
  OS << "typedef __clang_svuint64x3_t svuint64x3_t;\n";
  OS << "typedef __clang_svfloat16x3_t svfloat16x3_t;\n";
  OS << "typedef __clang_svfloat32x3_t svfloat32x3_t;\n";
  OS << "typedef __clang_svfloat64x3_t svfloat64x3_t;\n";
  OS << "typedef __clang_svint8x4_t svint8x4_t;\n";
  OS << "typedef __clang_svint16x4_t svint16x4_t;\n";
  OS << "typedef __clang_svint32x4_t svint32x4_t;\n";
  OS << "typedef __clang_svint64x4_t svint64x4_t;\n";
  OS << "typedef __clang_svuint8x4_t svuint8x4_t;\n";
  OS << "typedef __clang_svuint16x4_t svuint16x4_t;\n";
  OS << "typedef __clang_svuint32x4_t svuint32x4_t;\n";
  OS << "typedef __clang_svuint64x4_t svuint64x4_t;\n";
  OS << "typedef __clang_svfloat16x4_t svfloat16x4_t;\n";
  OS << "typedef __clang_svfloat32x4_t svfloat32x4_t;\n";
  OS << "typedef __clang_svfloat64x4_t svfloat64x4_t;\n";
  OS << "typedef __SVBool_t  svbool_t;\n";
  OS << "typedef __clang_svboolx2_t  svboolx2_t;\n";
  OS << "typedef __clang_svboolx4_t  svboolx4_t;\n\n";

  OS << "typedef __clang_svbfloat16x2_t svbfloat16x2_t;\n";
  OS << "typedef __clang_svbfloat16x3_t svbfloat16x3_t;\n";
  OS << "typedef __clang_svbfloat16x4_t svbfloat16x4_t;\n";

  OS << "typedef __SVCount_t svcount_t;\n\n";

  OS << "enum svpattern\n";
  OS << "{\n";
  OS << "  SV_POW2 = 0,\n";
  OS << "  SV_VL1 = 1,\n";
  OS << "  SV_VL2 = 2,\n";
  OS << "  SV_VL3 = 3,\n";
  OS << "  SV_VL4 = 4,\n";
  OS << "  SV_VL5 = 5,\n";
  OS << "  SV_VL6 = 6,\n";
  OS << "  SV_VL7 = 7,\n";
  OS << "  SV_VL8 = 8,\n";
  OS << "  SV_VL16 = 9,\n";
  OS << "  SV_VL32 = 10,\n";
  OS << "  SV_VL64 = 11,\n";
  OS << "  SV_VL128 = 12,\n";
  OS << "  SV_VL256 = 13,\n";
  OS << "  SV_MUL4 = 29,\n";
  OS << "  SV_MUL3 = 30,\n";
  OS << "  SV_ALL = 31\n";
  OS << "};\n\n";

  OS << "enum svprfop\n";
  OS << "{\n";
  OS << "  SV_PLDL1KEEP = 0,\n";
  OS << "  SV_PLDL1STRM = 1,\n";
  OS << "  SV_PLDL2KEEP = 2,\n";
  OS << "  SV_PLDL2STRM = 3,\n";
  OS << "  SV_PLDL3KEEP = 4,\n";
  OS << "  SV_PLDL3STRM = 5,\n";
  OS << "  SV_PSTL1KEEP = 8,\n";
  OS << "  SV_PSTL1STRM = 9,\n";
  OS << "  SV_PSTL2KEEP = 10,\n";
  OS << "  SV_PSTL2STRM = 11,\n";
  OS << "  SV_PSTL3KEEP = 12,\n";
  OS << "  SV_PSTL3STRM = 13\n";
  OS << "};\n\n";

  OS << "/* Function attributes */\n";
  OS << "#define __ai static __inline__ __attribute__((__always_inline__, "
        "__nodebug__))\n\n";
  OS << "#define __aio static __inline__ __attribute__((__always_inline__, "
        "__nodebug__, __overloadable__))\n\n";

  // Add reinterpret functions.
  for (auto [N, Suffix] :
       std::initializer_list<std::pair<unsigned, const char *>>{
           {1, ""}, {2, "_x2"}, {3, "_x3"}, {4, "_x4"}}) {
    for (auto ShortForm : {false, true})
      for (const ReinterpretTypeInfo &To : Reinterprets) {
        SVEType ToV(To.BaseType, N);
        for (const ReinterpretTypeInfo &From : Reinterprets) {
          SVEType FromV(From.BaseType, N);
          OS << "__aio "
                "__attribute__((__clang_arm_builtin_alias(__builtin_sve_"
                "reinterpret_"
             << To.Suffix << "_" << From.Suffix << Suffix << ")))\n"
             << ToV.str() << " svreinterpret_" << To.Suffix;
          if (!ShortForm)
            OS << "_" << From.Suffix << Suffix;
          OS << "(" << FromV.str() << " op);\n";
        }
      }
  }

  createCoreHeaderIntrinsics(OS, *this, ACLEKind::SVE);

  OS << "#define svcvtnt_bf16_x      svcvtnt_bf16_m\n";
  OS << "#define svcvtnt_bf16_f32_x  svcvtnt_bf16_f32_m\n";

  OS << "#define svcvtnt_f16_x      svcvtnt_f16_m\n";
  OS << "#define svcvtnt_f16_f32_x  svcvtnt_f16_f32_m\n";
  OS << "#define svcvtnt_f32_x      svcvtnt_f32_m\n";
  OS << "#define svcvtnt_f32_f64_x  svcvtnt_f32_f64_m\n\n";

  OS << "#define svcvtxnt_f32_x     svcvtxnt_f32_m\n";
  OS << "#define svcvtxnt_f32_f64_x svcvtxnt_f32_f64_m\n\n";

  OS << "#ifdef __cplusplus\n";
  OS << "} // extern \"C\"\n";
  OS << "#endif\n\n";
  OS << "#undef __ai\n\n";
  OS << "#undef __aio\n\n";
  OS << "#endif /* __ARM_SVE_H */\n";
}

void SVEEmitter::createBuiltins(raw_ostream &OS) {
  std::vector<Record *> RV = Records.getAllDerivedDefinitions("Inst");
  SmallVector<std::unique_ptr<Intrinsic>, 128> Defs;
  for (auto *R : RV)
    createIntrinsic(R, Defs);

  // The mappings must be sorted based on BuiltinID.
  llvm::sort(Defs, [](const std::unique_ptr<Intrinsic> &A,
                      const std::unique_ptr<Intrinsic> &B) {
    return A->getMangledName() < B->getMangledName();
  });

  OS << "#ifdef GET_SVE_BUILTINS\n";
  for (auto &Def : Defs) {
    // Only create BUILTINs for non-overloaded intrinsics, as overloaded
    // declarations only live in the header file.
    if (Def->getClassKind() != ClassG) {
      OS << "TARGET_BUILTIN(__builtin_sve_" << Def->getMangledName() << ", \""
         << Def->getBuiltinTypeStr() << "\", \"n\", \"";
      Def->printGuard(OS);
      OS << "\")\n";
    }
  }

  // Add reinterpret functions.
  for (auto [N, Suffix] :
       std::initializer_list<std::pair<unsigned, const char *>>{
           {1, ""}, {2, "_x2"}, {3, "_x3"}, {4, "_x4"}}) {
    for (const ReinterpretTypeInfo &To : Reinterprets) {
      SVEType ToV(To.BaseType, N);
      for (const ReinterpretTypeInfo &From : Reinterprets) {
        SVEType FromV(From.BaseType, N);
        OS << "TARGET_BUILTIN(__builtin_sve_reinterpret_" << To.Suffix << "_"
           << From.Suffix << Suffix << +", \"" << ToV.builtin_str()
           << FromV.builtin_str() << "\", \"n\", \"sme|sve\")\n";
      }
    }
  }

  OS << "#endif\n\n";
}

void SVEEmitter::createCodeGenMap(raw_ostream &OS) {
  std::vector<Record *> RV = Records.getAllDerivedDefinitions("Inst");
  SmallVector<std::unique_ptr<Intrinsic>, 128> Defs;
  for (auto *R : RV)
    createIntrinsic(R, Defs);

  // The mappings must be sorted based on BuiltinID.
  llvm::sort(Defs, [](const std::unique_ptr<Intrinsic> &A,
                      const std::unique_ptr<Intrinsic> &B) {
    return A->getMangledName() < B->getMangledName();
  });

  OS << "#ifdef GET_SVE_LLVM_INTRINSIC_MAP\n";
  for (auto &Def : Defs) {
    // Builtins only exist for non-overloaded intrinsics, overloaded
    // declarations only live in the header file.
    if (Def->getClassKind() == ClassG)
      continue;

    uint64_t Flags = Def->getFlags();
    auto FlagString = std::to_string(Flags);

    std::string LLVMName = Def->getMangledLLVMName();
    std::string Builtin = Def->getMangledName();
    if (!LLVMName.empty())
      OS << "SVEMAP1(" << Builtin << ", " << LLVMName << ", " << FlagString
         << "),\n";
    else
      OS << "SVEMAP2(" << Builtin << ", " << FlagString << "),\n";
  }
  OS << "#endif\n\n";
}

void SVEEmitter::createRangeChecks(raw_ostream &OS) {
  std::vector<Record *> RV = Records.getAllDerivedDefinitions("Inst");
  SmallVector<std::unique_ptr<Intrinsic>, 128> Defs;
  for (auto *R : RV)
    createIntrinsic(R, Defs);

  // The mappings must be sorted based on BuiltinID.
  llvm::sort(Defs, [](const std::unique_ptr<Intrinsic> &A,
                      const std::unique_ptr<Intrinsic> &B) {
    return A->getMangledName() < B->getMangledName();
  });


  OS << "#ifdef GET_SVE_IMMEDIATE_CHECK\n";

  // Ensure these are only emitted once.
  std::set<std::string> Emitted;

  for (auto &Def : Defs) {
    if (Emitted.find(Def->getMangledName()) != Emitted.end() ||
        Def->getImmChecks().empty())
      continue;

    OS << "case SVE::BI__builtin_sve_" << Def->getMangledName() << ":\n";
    for (auto &Check : Def->getImmChecks())
      OS << "ImmChecks.push_back(std::make_tuple(" << Check.getArg() << ", "
         << Check.getKind() << ", " << Check.getElementSizeInBits() << "));\n";
    OS << "  break;\n";

    Emitted.insert(Def->getMangledName());
  }

  OS << "#endif\n\n";
}

/// Create the SVETypeFlags used in CGBuiltins
void SVEEmitter::createTypeFlags(raw_ostream &OS) {
  OS << "#ifdef LLVM_GET_SVE_TYPEFLAGS\n";
  for (auto &KV : FlagTypes)
    OS << "const uint64_t " << KV.getKey() << " = " << KV.getValue() << ";\n";
  OS << "#endif\n\n";

  OS << "#ifdef LLVM_GET_SVE_ELTTYPES\n";
  for (auto &KV : EltTypes)
    OS << "  " << KV.getKey() << " = " << KV.getValue() << ",\n";
  OS << "#endif\n\n";

  OS << "#ifdef LLVM_GET_SVE_MEMELTTYPES\n";
  for (auto &KV : MemEltTypes)
    OS << "  " << KV.getKey() << " = " << KV.getValue() << ",\n";
  OS << "#endif\n\n";

  OS << "#ifdef LLVM_GET_SVE_MERGETYPES\n";
  for (auto &KV : MergeTypes)
    OS << "  " << KV.getKey() << " = " << KV.getValue() << ",\n";
  OS << "#endif\n\n";

  OS << "#ifdef LLVM_GET_SVE_IMMCHECKTYPES\n";
  for (auto &KV : ImmCheckTypes)
    OS << "  " << KV.getKey() << " = " << KV.getValue() << ",\n";
  OS << "#endif\n\n";
}

void SVEEmitter::createSMEHeader(raw_ostream &OS) {
  OS << "/*===---- arm_sme.h - ARM SME intrinsics "
        "------===\n"
        " *\n"
        " *\n"
        " * Part of the LLVM Project, under the Apache License v2.0 with LLVM "
        "Exceptions.\n"
        " * See https://llvm.org/LICENSE.txt for license information.\n"
        " * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception\n"
        " *\n"
        " *===-----------------------------------------------------------------"
        "------===\n"
        " */\n\n";

  OS << "#ifndef __ARM_SME_H\n";
  OS << "#define __ARM_SME_H\n\n";

  OS << "#if !defined(__LITTLE_ENDIAN__)\n";
  OS << "#error \"Big endian is currently not supported for arm_sme.h\"\n";
  OS << "#endif\n";

  OS << "#include <arm_sve.h>\n\n";
  OS << "#include <stddef.h>\n\n";

  OS << "/* Function attributes */\n";
  OS << "#define __ai static __inline__ __attribute__((__always_inline__, "
        "__nodebug__))\n\n";
  OS << "#define __aio static __inline__ __attribute__((__always_inline__, "
        "__nodebug__, __overloadable__))\n\n";

  OS << "#ifdef  __cplusplus\n";
  OS << "extern \"C\" {\n";
  OS << "#endif\n\n";

  OS << "void __arm_za_disable(void) __arm_streaming_compatible;\n\n";

  OS << "__ai bool __arm_has_sme(void) __arm_streaming_compatible {\n";
  OS << "  uint64_t x0, x1;\n";
  OS << "  __builtin_arm_get_sme_state(&x0, &x1);\n";
  OS << "  return x0 & (1ULL << 63);\n";
  OS << "}\n\n";

  OS << "__ai bool __arm_in_streaming_mode(void) __arm_streaming_compatible "
        "{\n";
  OS << "  uint64_t x0, x1;\n";
  OS << "  __builtin_arm_get_sme_state(&x0, &x1);\n";
  OS << "  return x0 & 1;\n";
  OS << "}\n\n";

  OS << "void *__arm_sc_memcpy(void *dest, const void *src, size_t n) __arm_streaming_compatible;\n";
  OS << "void *__arm_sc_memmove(void *dest, const void *src, size_t n) __arm_streaming_compatible;\n";
  OS << "void *__arm_sc_memset(void *s, int c, size_t n) __arm_streaming_compatible;\n";
  OS << "void *__arm_sc_memchr(void *s, int c, size_t n) __arm_streaming_compatible;\n\n";

  OS << "__ai __attribute__((target(\"sme\"))) void svundef_za(void) "
        "__arm_streaming_compatible __arm_out(\"za\") "
        "{ }\n\n";

  createCoreHeaderIntrinsics(OS, *this, ACLEKind::SME);

  OS << "#ifdef __cplusplus\n";
  OS << "} // extern \"C\"\n";
  OS << "#endif\n\n";
  OS << "#undef __ai\n\n";
  OS << "#endif /* __ARM_SME_H */\n";
}

void SVEEmitter::createSMEBuiltins(raw_ostream &OS) {
  std::vector<Record *> RV = Records.getAllDerivedDefinitions("Inst");
  SmallVector<std::unique_ptr<Intrinsic>, 128> Defs;
  for (auto *R : RV) {
    createIntrinsic(R, Defs);
  }

  // The mappings must be sorted based on BuiltinID.
  llvm::sort(Defs, [](const std::unique_ptr<Intrinsic> &A,
                      const std::unique_ptr<Intrinsic> &B) {
    return A->getMangledName() < B->getMangledName();
  });

  OS << "#ifdef GET_SME_BUILTINS\n";
  for (auto &Def : Defs) {
    // Only create BUILTINs for non-overloaded intrinsics, as overloaded
    // declarations only live in the header file.
    if (Def->getClassKind() != ClassG) {
      OS << "TARGET_BUILTIN(__builtin_sme_" << Def->getMangledName() << ", \""
         << Def->getBuiltinTypeStr() << "\", \"n\", \"";
      Def->printGuard(OS);
      OS << "\")\n";
    }
  }

  OS << "#endif\n\n";
}

void SVEEmitter::createSMECodeGenMap(raw_ostream &OS) {
  std::vector<Record *> RV = Records.getAllDerivedDefinitions("Inst");
  SmallVector<std::unique_ptr<Intrinsic>, 128> Defs;
  for (auto *R : RV) {
    createIntrinsic(R, Defs);
  }

  // The mappings must be sorted based on BuiltinID.
  llvm::sort(Defs, [](const std::unique_ptr<Intrinsic> &A,
                      const std::unique_ptr<Intrinsic> &B) {
    return A->getMangledName() < B->getMangledName();
  });

  OS << "#ifdef GET_SME_LLVM_INTRINSIC_MAP\n";
  for (auto &Def : Defs) {
    // Builtins only exist for non-overloaded intrinsics, overloaded
    // declarations only live in the header file.
    if (Def->getClassKind() == ClassG)
      continue;

    uint64_t Flags = Def->getFlags();
    auto FlagString = std::to_string(Flags);

    std::string LLVMName = Def->getLLVMName();
    std::string Builtin = Def->getMangledName();
    if (!LLVMName.empty())
      OS << "SMEMAP1(" << Builtin << ", " << LLVMName << ", " << FlagString
         << "),\n";
    else
      OS << "SMEMAP2(" << Builtin << ", " << FlagString << "),\n";
  }
  OS << "#endif\n\n";
}

void SVEEmitter::createSMERangeChecks(raw_ostream &OS) {
  std::vector<Record *> RV = Records.getAllDerivedDefinitions("Inst");
  SmallVector<std::unique_ptr<Intrinsic>, 128> Defs;
  for (auto *R : RV) {
    createIntrinsic(R, Defs);
  }

  // The mappings must be sorted based on BuiltinID.
  llvm::sort(Defs, [](const std::unique_ptr<Intrinsic> &A,
                      const std::unique_ptr<Intrinsic> &B) {
    return A->getMangledName() < B->getMangledName();
  });


  OS << "#ifdef GET_SME_IMMEDIATE_CHECK\n";

  // Ensure these are only emitted once.
  std::set<std::string> Emitted;

  for (auto &Def : Defs) {
    if (Emitted.find(Def->getMangledName()) != Emitted.end() ||
        Def->getImmChecks().empty())
      continue;

    OS << "case SME::BI__builtin_sme_" << Def->getMangledName() << ":\n";
    for (auto &Check : Def->getImmChecks())
      OS << "ImmChecks.push_back(std::make_tuple(" << Check.getArg() << ", "
         << Check.getKind() << ", " << Check.getElementSizeInBits() << "));\n";
    OS << "  break;\n";

    Emitted.insert(Def->getMangledName());
  }

  OS << "#endif\n\n";
}

void SVEEmitter::createBuiltinZAState(raw_ostream &OS) {
  std::vector<Record *> RV = Records.getAllDerivedDefinitions("Inst");
  SmallVector<std::unique_ptr<Intrinsic>, 128> Defs;
  for (auto *R : RV)
    createIntrinsic(R, Defs);

  std::map<std::string, std::set<std::string>> IntrinsicsPerState;
  for (auto &Def : Defs) {
    std::string Key;
    auto AddToKey = [&Key](const std::string &S) -> void {
      Key = Key.empty() ? S : (Key + " | " + S);
    };

    if (Def->isFlagSet(getEnumValueForFlag("IsInZA")))
      AddToKey("ArmInZA");
    else if (Def->isFlagSet(getEnumValueForFlag("IsOutZA")))
      AddToKey("ArmOutZA");
    else if (Def->isFlagSet(getEnumValueForFlag("IsInOutZA")))
      AddToKey("ArmInOutZA");

    if (Def->isFlagSet(getEnumValueForFlag("IsInZT0")))
      AddToKey("ArmInZT0");
    else if (Def->isFlagSet(getEnumValueForFlag("IsOutZT0")))
      AddToKey("ArmOutZT0");
    else if (Def->isFlagSet(getEnumValueForFlag("IsInOutZT0")))
      AddToKey("ArmInOutZT0");

    if (!Key.empty())
      IntrinsicsPerState[Key].insert(Def->getMangledName());
  }

  OS << "#ifdef GET_SME_BUILTIN_GET_STATE\n";
  for (auto &KV : IntrinsicsPerState) {
    for (StringRef Name : KV.second)
      OS << "case SME::BI__builtin_sme_" << Name << ":\n";
    OS << "  return " << KV.first << ";\n";
  }
  OS << "#endif\n\n";
}

void SVEEmitter::createStreamingAttrs(raw_ostream &OS, ACLEKind Kind) {
  std::vector<Record *> RV = Records.getAllDerivedDefinitions("Inst");
  SmallVector<std::unique_ptr<Intrinsic>, 128> Defs;
  for (auto *R : RV)
    createIntrinsic(R, Defs);

  StringRef ExtensionKind;
  switch (Kind) {
  case ACLEKind::SME:
    ExtensionKind = "SME";
    break;
  case ACLEKind::SVE:
    ExtensionKind = "SVE";
    break;
  }

  OS << "#ifdef GET_" << ExtensionKind << "_STREAMING_ATTRS\n";

  llvm::StringMap<std::set<std::string>> StreamingMap;

  uint64_t IsStreamingFlag = getEnumValueForFlag("IsStreaming");
  uint64_t VerifyRuntimeMode = getEnumValueForFlag("VerifyRuntimeMode");
  uint64_t IsStreamingCompatibleFlag =
      getEnumValueForFlag("IsStreamingCompatible");

  for (auto &Def : Defs) {
    if (!Def->isFlagSet(VerifyRuntimeMode) && !Def->getSVEGuard().empty() &&
        !Def->getSMEGuard().empty())
      report_fatal_error("Missing VerifyRuntimeMode flag");

    if (Def->isFlagSet(IsStreamingFlag))
      StreamingMap["ArmStreaming"].insert(Def->getMangledName());
    else if (Def->isFlagSet(VerifyRuntimeMode))
      StreamingMap["VerifyRuntimeMode"].insert(Def->getMangledName());
    else if (Def->isFlagSet(IsStreamingCompatibleFlag))
      StreamingMap["ArmStreamingCompatible"].insert(Def->getMangledName());
    else
      StreamingMap["ArmNonStreaming"].insert(Def->getMangledName());
  }

  for (auto BuiltinType : StreamingMap.keys()) {
    for (auto Name : StreamingMap[BuiltinType]) {
      OS << "case " << ExtensionKind << "::BI__builtin_"
         << ExtensionKind.lower() << "_";
      OS << Name << ":\n";
    }
    OS << "  BuiltinType = " << BuiltinType << ";\n";
    OS << "  break;\n";
  }

  OS << "#endif\n\n";
}

namespace clang {
void EmitSveHeader(RecordKeeper &Records, raw_ostream &OS) {
  SVEEmitter(Records).createHeader(OS);
}

void EmitSveBuiltins(RecordKeeper &Records, raw_ostream &OS) {
  SVEEmitter(Records).createBuiltins(OS);
}

void EmitSveBuiltinCG(RecordKeeper &Records, raw_ostream &OS) {
  SVEEmitter(Records).createCodeGenMap(OS);
}

void EmitSveRangeChecks(RecordKeeper &Records, raw_ostream &OS) {
  SVEEmitter(Records).createRangeChecks(OS);
}

void EmitSveTypeFlags(RecordKeeper &Records, raw_ostream &OS) {
  SVEEmitter(Records).createTypeFlags(OS);
}

void EmitSveStreamingAttrs(RecordKeeper &Records, raw_ostream &OS) {
  SVEEmitter(Records).createStreamingAttrs(OS, ACLEKind::SVE);
}

void EmitSmeHeader(RecordKeeper &Records, raw_ostream &OS) {
  SVEEmitter(Records).createSMEHeader(OS);
}

void EmitSmeBuiltins(RecordKeeper &Records, raw_ostream &OS) {
  SVEEmitter(Records).createSMEBuiltins(OS);
}

void EmitSmeBuiltinCG(RecordKeeper &Records, raw_ostream &OS) {
  SVEEmitter(Records).createSMECodeGenMap(OS);
}

void EmitSmeRangeChecks(RecordKeeper &Records, raw_ostream &OS) {
  SVEEmitter(Records).createSMERangeChecks(OS);
}

void EmitSmeStreamingAttrs(RecordKeeper &Records, raw_ostream &OS) {
  SVEEmitter(Records).createStreamingAttrs(OS, ACLEKind::SME);
}

void EmitSmeBuiltinZAState(RecordKeeper &Records, raw_ostream &OS) {
  SVEEmitter(Records).createBuiltinZAState(OS);
}
} // End namespace clang
