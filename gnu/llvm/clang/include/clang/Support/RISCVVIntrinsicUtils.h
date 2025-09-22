//===--- RISCVVIntrinsicUtils.h - RISC-V Vector Intrinsic Utils -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_SUPPORT_RISCVVINTRINSICUTILS_H
#define CLANG_SUPPORT_RISCVVINTRINSICUTILS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace llvm {
class raw_ostream;
} // end namespace llvm

namespace clang {
namespace RISCV {

using VScaleVal = std::optional<unsigned>;

// Modifier for vector type.
enum class VectorTypeModifier : uint8_t {
  NoModifier,
  Widening2XVector,
  Widening4XVector,
  Widening8XVector,
  MaskVector,
  Log2EEW3,
  Log2EEW4,
  Log2EEW5,
  Log2EEW6,
  FixedSEW8,
  FixedSEW16,
  FixedSEW32,
  FixedSEW64,
  LFixedLog2LMULN3,
  LFixedLog2LMULN2,
  LFixedLog2LMULN1,
  LFixedLog2LMUL0,
  LFixedLog2LMUL1,
  LFixedLog2LMUL2,
  LFixedLog2LMUL3,
  SFixedLog2LMULN3,
  SFixedLog2LMULN2,
  SFixedLog2LMULN1,
  SFixedLog2LMUL0,
  SFixedLog2LMUL1,
  SFixedLog2LMUL2,
  SFixedLog2LMUL3,
  SEFixedLog2LMULN3,
  SEFixedLog2LMULN2,
  SEFixedLog2LMULN1,
  SEFixedLog2LMUL0,
  SEFixedLog2LMUL1,
  SEFixedLog2LMUL2,
  SEFixedLog2LMUL3,
  Tuple2,
  Tuple3,
  Tuple4,
  Tuple5,
  Tuple6,
  Tuple7,
  Tuple8,
};

// Similar to basic type but used to describe what's kind of type related to
// basic vector type, used to compute type info of arguments.
enum class BaseTypeModifier : uint8_t {
  Invalid,
  Scalar,
  Vector,
  Void,
  SizeT,
  Ptrdiff,
  UnsignedLong,
  SignedLong,
  Float32
};

// Modifier for type, used for both scalar and vector types.
enum class TypeModifier : uint8_t {
  NoModifier = 0,
  Pointer = 1 << 0,
  Const = 1 << 1,
  Immediate = 1 << 2,
  UnsignedInteger = 1 << 3,
  SignedInteger = 1 << 4,
  Float = 1 << 5,
  BFloat = 1 << 6,
  // LMUL1 should be kind of VectorTypeModifier, but that might come with
  // Widening2XVector for widening reduction.
  // However that might require VectorTypeModifier become bitmask rather than
  // simple enum, so we decide keek LMUL1 in TypeModifier for code size
  // optimization of clang binary size.
  LMUL1 = 1 << 7,
  MaxOffset = 7,
  LLVM_MARK_AS_BITMASK_ENUM(LMUL1),
};

class Policy {
public:
  enum PolicyType {
    Undisturbed,
    Agnostic,
  };

private:
  // The default assumption for an RVV instruction is TAMA, as an undisturbed
  // policy generally will affect the performance of an out-of-order core.
  const PolicyType TailPolicy = Agnostic;
  const PolicyType MaskPolicy = Agnostic;

public:
  Policy() = default;
  Policy(PolicyType TailPolicy) : TailPolicy(TailPolicy) {}
  Policy(PolicyType TailPolicy, PolicyType MaskPolicy)
      : TailPolicy(TailPolicy), MaskPolicy(MaskPolicy) {}

  bool isTAMAPolicy() const {
    return TailPolicy == Agnostic && MaskPolicy == Agnostic;
  }

  bool isTAMUPolicy() const {
    return TailPolicy == Agnostic && MaskPolicy == Undisturbed;
  }

  bool isTUMAPolicy() const {
    return TailPolicy == Undisturbed && MaskPolicy == Agnostic;
  }

  bool isTUMUPolicy() const {
    return TailPolicy == Undisturbed && MaskPolicy == Undisturbed;
  }

  bool isTAPolicy() const { return TailPolicy == Agnostic; }

  bool isTUPolicy() const { return TailPolicy == Undisturbed; }

  bool isMAPolicy() const { return MaskPolicy == Agnostic; }

  bool isMUPolicy() const { return MaskPolicy == Undisturbed; }

  bool operator==(const Policy &Other) const {
    return TailPolicy == Other.TailPolicy && MaskPolicy == Other.MaskPolicy;
  }

  bool operator!=(const Policy &Other) const { return !(*this == Other); }

  bool operator<(const Policy &Other) const {
    // Just for maintain the old order for quick test.
    if (MaskPolicy != Other.MaskPolicy)
      return Other.MaskPolicy < MaskPolicy;
    return TailPolicy < Other.TailPolicy;
  }
};

// PrototypeDescriptor is used to compute type info of arguments or return
// value.
struct PrototypeDescriptor {
  constexpr PrototypeDescriptor() = default;
  constexpr PrototypeDescriptor(
      BaseTypeModifier PT,
      VectorTypeModifier VTM = VectorTypeModifier::NoModifier,
      TypeModifier TM = TypeModifier::NoModifier)
      : PT(static_cast<uint8_t>(PT)), VTM(static_cast<uint8_t>(VTM)),
        TM(static_cast<uint8_t>(TM)) {}
  constexpr PrototypeDescriptor(uint8_t PT, uint8_t VTM, uint8_t TM)
      : PT(PT), VTM(VTM), TM(TM) {}

  uint8_t PT = static_cast<uint8_t>(BaseTypeModifier::Invalid);
  uint8_t VTM = static_cast<uint8_t>(VectorTypeModifier::NoModifier);
  uint8_t TM = static_cast<uint8_t>(TypeModifier::NoModifier);

  bool operator!=(const PrototypeDescriptor &PD) const {
    return !(*this == PD);
  }
  bool operator==(const PrototypeDescriptor &PD) const {
    return PD.PT == PT && PD.VTM == VTM && PD.TM == TM;
  }
  bool operator<(const PrototypeDescriptor &PD) const {
    return std::tie(PT, VTM, TM) < std::tie(PD.PT, PD.VTM, PD.TM);
  }
  static const PrototypeDescriptor Mask;
  static const PrototypeDescriptor Vector;
  static const PrototypeDescriptor VL;
  static std::optional<PrototypeDescriptor>
  parsePrototypeDescriptor(llvm::StringRef PrototypeStr);
};

llvm::SmallVector<PrototypeDescriptor>
parsePrototypes(llvm::StringRef Prototypes);

// Basic type of vector type.
enum class BasicType : uint8_t {
  Unknown = 0,
  Int8 = 1 << 0,
  Int16 = 1 << 1,
  Int32 = 1 << 2,
  Int64 = 1 << 3,
  BFloat16 = 1 << 4,
  Float16 = 1 << 5,
  Float32 = 1 << 6,
  Float64 = 1 << 7,
  MaxOffset = 7,
  LLVM_MARK_AS_BITMASK_ENUM(Float64),
};

// Type of vector type.
enum ScalarTypeKind : uint8_t {
  Void,
  Size_t,
  Ptrdiff_t,
  UnsignedLong,
  SignedLong,
  Boolean,
  SignedInteger,
  UnsignedInteger,
  Float,
  BFloat,
  Invalid,
  Undefined,
};

// Exponential LMUL
struct LMULType {
  int Log2LMUL;
  LMULType(int Log2LMUL);
  // Return the C/C++ string representation of LMUL
  std::string str() const;
  std::optional<unsigned> getScale(unsigned ElementBitwidth) const;
  void MulLog2LMUL(int Log2LMUL);
};

class RVVType;
using RVVTypePtr = RVVType *;
using RVVTypes = std::vector<RVVTypePtr>;
class RVVTypeCache;

// This class is compact representation of a valid and invalid RVVType.
class RVVType {
  friend class RVVTypeCache;

  BasicType BT;
  ScalarTypeKind ScalarType = Undefined;
  LMULType LMUL;
  bool IsPointer = false;
  // IsConstant indices are "int", but have the constant expression.
  bool IsImmediate = false;
  // Const qualifier for pointer to const object or object of const type.
  bool IsConstant = false;
  unsigned ElementBitwidth = 0;
  VScaleVal Scale = 0;
  bool Valid;
  bool IsTuple = false;
  unsigned NF = 0;

  std::string BuiltinStr;
  std::string ClangBuiltinStr;
  std::string Str;
  std::string ShortStr;

  enum class FixedLMULType { LargerThan, SmallerThan, SmallerOrEqual };

  RVVType(BasicType BT, int Log2LMUL, const PrototypeDescriptor &Profile);

public:
  // Return the string representation of a type, which is an encoded string for
  // passing to the BUILTIN() macro in Builtins.def.
  const std::string &getBuiltinStr() const { return BuiltinStr; }

  // Return the clang builtin type for RVV vector type which are used in the
  // riscv_vector.h header file.
  const std::string &getClangBuiltinStr() const { return ClangBuiltinStr; }

  // Return the C/C++ string representation of a type for use in the
  // riscv_vector.h header file.
  const std::string &getTypeStr() const { return Str; }

  // Return the short name of a type for C/C++ name suffix.
  const std::string &getShortStr() {
    // Not all types are used in short name, so compute the short name by
    // demanded.
    if (ShortStr.empty())
      initShortStr();
    return ShortStr;
  }

  bool isValid() const { return Valid; }
  bool isScalar() const { return Scale && *Scale == 0; }
  bool isVector() const { return Scale && *Scale != 0; }
  bool isVector(unsigned Width) const {
    return isVector() && ElementBitwidth == Width;
  }
  bool isFloat() const { return ScalarType == ScalarTypeKind::Float; }
  bool isBFloat() const { return ScalarType == ScalarTypeKind::BFloat; }
  bool isSignedInteger() const {
    return ScalarType == ScalarTypeKind::SignedInteger;
  }
  bool isFloatVector(unsigned Width) const {
    return isVector() && isFloat() && ElementBitwidth == Width;
  }
  bool isFloat(unsigned Width) const {
    return isFloat() && ElementBitwidth == Width;
  }
  bool isConstant() const { return IsConstant; }
  bool isPointer() const { return IsPointer; }
  bool isTuple() const { return IsTuple; }
  unsigned getElementBitwidth() const { return ElementBitwidth; }

  ScalarTypeKind getScalarType() const { return ScalarType; }
  VScaleVal getScale() const { return Scale; }
  unsigned getNF() const {
    assert(NF > 1 && NF <= 8 && "Only legal NF should be fetched");
    return NF;
  }

private:
  // Verify RVV vector type and set Valid.
  bool verifyType() const;

  // Creates a type based on basic types of TypeRange
  void applyBasicType();

  // Applies a prototype modifier to the current type. The result maybe an
  // invalid type.
  void applyModifier(const PrototypeDescriptor &prototype);

  void applyLog2EEW(unsigned Log2EEW);
  void applyFixedSEW(unsigned NewSEW);
  void applyFixedLog2LMUL(int Log2LMUL, enum FixedLMULType Type);

  // Compute and record a string for legal type.
  void initBuiltinStr();
  // Compute and record a builtin RVV vector type string.
  void initClangBuiltinStr();
  // Compute and record a type string for used in the header.
  void initTypeStr();
  // Compute and record a short name of a type for C/C++ name suffix.
  void initShortStr();
};

// This class is used to manage RVVType, RVVType should only created by this
// class, also provided thread-safe cache capability.
class RVVTypeCache {
private:
  std::unordered_map<uint64_t, RVVType> LegalTypes;
  std::set<uint64_t> IllegalTypes;

public:
  /// Compute output and input types by applying different config (basic type
  /// and LMUL with type transformers). It also record result of type in legal
  /// or illegal set to avoid compute the same config again. The result maybe
  /// have illegal RVVType.
  std::optional<RVVTypes>
  computeTypes(BasicType BT, int Log2LMUL, unsigned NF,
               llvm::ArrayRef<PrototypeDescriptor> Prototype);
  std::optional<RVVTypePtr> computeType(BasicType BT, int Log2LMUL,
                                        PrototypeDescriptor Proto);
};

enum PolicyScheme : uint8_t {
  SchemeNone,
  // Passthru operand is at first parameter in C builtin.
  HasPassthruOperand,
  HasPolicyOperand,
};

// TODO refactor RVVIntrinsic class design after support all intrinsic
// combination. This represents an instantiation of an intrinsic with a
// particular type and prototype
class RVVIntrinsic {

private:
  std::string BuiltinName; // Builtin name
  std::string Name;        // C intrinsic name.
  std::string OverloadedName;
  std::string IRName;
  bool IsMasked;
  bool HasMaskedOffOperand;
  bool HasVL;
  PolicyScheme Scheme;
  bool SupportOverloading;
  bool HasBuiltinAlias;
  std::string ManualCodegen;
  RVVTypePtr OutputType; // Builtin output type
  RVVTypes InputTypes;   // Builtin input types
  // The types we use to obtain the specific LLVM intrinsic. They are index of
  // InputTypes. -1 means the return type.
  std::vector<int64_t> IntrinsicTypes;
  unsigned NF = 1;
  Policy PolicyAttrs;

public:
  RVVIntrinsic(llvm::StringRef Name, llvm::StringRef Suffix,
               llvm::StringRef OverloadedName, llvm::StringRef OverloadedSuffix,
               llvm::StringRef IRName, bool IsMasked, bool HasMaskedOffOperand,
               bool HasVL, PolicyScheme Scheme, bool SupportOverloading,
               bool HasBuiltinAlias, llvm::StringRef ManualCodegen,
               const RVVTypes &Types,
               const std::vector<int64_t> &IntrinsicTypes,
               unsigned NF, Policy PolicyAttrs, bool HasFRMRoundModeOp);
  ~RVVIntrinsic() = default;

  RVVTypePtr getOutputType() const { return OutputType; }
  const RVVTypes &getInputTypes() const { return InputTypes; }
  llvm::StringRef getBuiltinName() const { return BuiltinName; }
  bool hasMaskedOffOperand() const { return HasMaskedOffOperand; }
  bool hasVL() const { return HasVL; }
  bool hasPolicy() const { return Scheme != PolicyScheme::SchemeNone; }
  bool hasPassthruOperand() const {
    return Scheme == PolicyScheme::HasPassthruOperand;
  }
  bool hasPolicyOperand() const {
    return Scheme == PolicyScheme::HasPolicyOperand;
  }
  bool supportOverloading() const { return SupportOverloading; }
  bool hasBuiltinAlias() const { return HasBuiltinAlias; }
  bool hasManualCodegen() const { return !ManualCodegen.empty(); }
  bool isMasked() const { return IsMasked; }
  llvm::StringRef getIRName() const { return IRName; }
  llvm::StringRef getManualCodegen() const { return ManualCodegen; }
  PolicyScheme getPolicyScheme() const { return Scheme; }
  unsigned getNF() const { return NF; }
  const std::vector<int64_t> &getIntrinsicTypes() const {
    return IntrinsicTypes;
  }
  Policy getPolicyAttrs() const {
    return PolicyAttrs;
  }
  unsigned getPolicyAttrsBits() const {
    // CGBuiltin.cpp
    // The 0th bit simulates the `vta` of RVV
    // The 1st bit simulates the `vma` of RVV
    // int PolicyAttrs = 0;

    if (PolicyAttrs.isTUMAPolicy())
      return 2;
    if (PolicyAttrs.isTAMAPolicy())
      return 3;
    if (PolicyAttrs.isTUMUPolicy())
      return 0;
    if (PolicyAttrs.isTAMUPolicy())
      return 1;

    llvm_unreachable("unsupport policy");
    return 0;
  }

  // Return the type string for a BUILTIN() macro in Builtins.def.
  std::string getBuiltinTypeStr() const;

  static std::string
  getSuffixStr(RVVTypeCache &TypeCache, BasicType Type, int Log2LMUL,
               llvm::ArrayRef<PrototypeDescriptor> PrototypeDescriptors);

  static llvm::SmallVector<PrototypeDescriptor>
  computeBuiltinTypes(llvm::ArrayRef<PrototypeDescriptor> Prototype,
                      bool IsMasked, bool HasMaskedOffOperand, bool HasVL,
                      unsigned NF, PolicyScheme DefaultScheme,
                      Policy PolicyAttrs, bool IsTuple);

  static llvm::SmallVector<Policy> getSupportedUnMaskedPolicies();
  static llvm::SmallVector<Policy>
      getSupportedMaskedPolicies(bool HasTailPolicy, bool HasMaskPolicy);

  static void updateNamesAndPolicy(bool IsMasked, bool HasPolicy,
                                   std::string &Name, std::string &BuiltinName,
                                   std::string &OverloadedName,
                                   Policy &PolicyAttrs, bool HasFRMRoundModeOp);
};

// RVVRequire should be sync'ed with target features, but only
// required features used in riscv_vector.td.
enum RVVRequire : uint32_t {
  RVV_REQ_None = 0,
  RVV_REQ_RV64 = 1 << 0,
  RVV_REQ_Zvfhmin = 1 << 1,
  RVV_REQ_Xsfvcp = 1 << 2,
  RVV_REQ_Xsfvfnrclipxfqf = 1 << 3,
  RVV_REQ_Xsfvfwmaccqqq = 1 << 4,
  RVV_REQ_Xsfvqmaccdod = 1 << 5,
  RVV_REQ_Xsfvqmaccqoq = 1 << 6,
  RVV_REQ_Zvbb = 1 << 7,
  RVV_REQ_Zvbc = 1 << 8,
  RVV_REQ_Zvkb = 1 << 9,
  RVV_REQ_Zvkg = 1 << 10,
  RVV_REQ_Zvkned = 1 << 11,
  RVV_REQ_Zvknha = 1 << 12,
  RVV_REQ_Zvknhb = 1 << 13,
  RVV_REQ_Zvksed = 1 << 14,
  RVV_REQ_Zvksh = 1 << 15,
  RVV_REQ_Zvfbfwma = 1 << 16,
  RVV_REQ_Zvfbfmin = 1 << 17,
  RVV_REQ_Experimental = 1 << 18,

  LLVM_MARK_AS_BITMASK_ENUM(RVV_REQ_Experimental)
};

// Raw RVV intrinsic info, used to expand later.
// This struct is highly compact for minimized code size.
struct RVVIntrinsicRecord {
  // Intrinsic name, e.g. vadd_vv
  const char *Name;

  // Overloaded intrinsic name, could be empty if it can be computed from Name.
  // e.g. vadd
  const char *OverloadedName;

  // Prototype for this intrinsic, index of RVVSignatureTable.
  uint16_t PrototypeIndex;

  // Suffix of intrinsic name, index of RVVSignatureTable.
  uint16_t SuffixIndex;

  // Suffix of overloaded intrinsic name, index of RVVSignatureTable.
  uint16_t OverloadedSuffixIndex;

  // Length of the prototype.
  uint8_t PrototypeLength;

  // Length of intrinsic name suffix.
  uint8_t SuffixLength;

  // Length of overloaded intrinsic suffix.
  uint8_t OverloadedSuffixSize;

  // Required target features for this intrinsic.
  uint32_t RequiredExtensions;

  // Supported type, mask of BasicType.
  uint8_t TypeRangeMask;

  // Supported LMUL.
  uint8_t Log2LMULMask;

  // Number of fields, greater than 1 if it's segment load/store.
  uint8_t NF;

  bool HasMasked : 1;
  bool HasVL : 1;
  bool HasMaskedOffOperand : 1;
  bool HasTailPolicy : 1;
  bool HasMaskPolicy : 1;
  bool HasFRMRoundModeOp : 1;
  bool IsTuple : 1;
  LLVM_PREFERRED_TYPE(PolicyScheme)
  uint8_t UnMaskedPolicyScheme : 2;
  LLVM_PREFERRED_TYPE(PolicyScheme)
  uint8_t MaskedPolicyScheme : 2;
};

llvm::raw_ostream &operator<<(llvm::raw_ostream &OS,
                              const RVVIntrinsicRecord &RVVInstrRecord);

LLVM_ENABLE_BITMASK_ENUMS_IN_NAMESPACE();
} // end namespace RISCV

} // end namespace clang

#endif // CLANG_SUPPORT_RISCVVINTRINSICUTILS_H
