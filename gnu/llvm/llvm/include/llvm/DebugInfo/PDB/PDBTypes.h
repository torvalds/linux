//===- PDBTypes.h - Defines enums for various fields contained in PDB ----====//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBTYPES_H
#define LLVM_DEBUGINFO_PDB_PDBTYPES_H

#include "llvm/ADT/APFloat.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/PDB/IPDBEnumChildren.h"
#include "llvm/DebugInfo/PDB/IPDBFrameData.h"
#include "llvm/DebugInfo/PDB/Native/RawTypes.h"
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>

namespace llvm {
namespace pdb {

typedef uint32_t SymIndexId;

class IPDBDataStream;
class IPDBInjectedSource;
class IPDBLineNumber;
class IPDBSectionContrib;
class IPDBSession;
class IPDBSourceFile;
class IPDBTable;
class PDBSymDumper;
class PDBSymbol;
class PDBSymbolExe;
class PDBSymbolCompiland;
class PDBSymbolCompilandDetails;
class PDBSymbolCompilandEnv;
class PDBSymbolFunc;
class PDBSymbolBlock;
class PDBSymbolData;
class PDBSymbolAnnotation;
class PDBSymbolLabel;
class PDBSymbolPublicSymbol;
class PDBSymbolTypeUDT;
class PDBSymbolTypeEnum;
class PDBSymbolTypeFunctionSig;
class PDBSymbolTypePointer;
class PDBSymbolTypeArray;
class PDBSymbolTypeBuiltin;
class PDBSymbolTypeTypedef;
class PDBSymbolTypeBaseClass;
class PDBSymbolTypeFriend;
class PDBSymbolTypeFunctionArg;
class PDBSymbolFuncDebugStart;
class PDBSymbolFuncDebugEnd;
class PDBSymbolUsingNamespace;
class PDBSymbolTypeVTableShape;
class PDBSymbolTypeVTable;
class PDBSymbolCustom;
class PDBSymbolThunk;
class PDBSymbolTypeCustom;
class PDBSymbolTypeManaged;
class PDBSymbolTypeDimension;
class PDBSymbolUnknown;

using IPDBEnumSymbols = IPDBEnumChildren<PDBSymbol>;
using IPDBEnumSourceFiles = IPDBEnumChildren<IPDBSourceFile>;
using IPDBEnumDataStreams = IPDBEnumChildren<IPDBDataStream>;
using IPDBEnumLineNumbers = IPDBEnumChildren<IPDBLineNumber>;
using IPDBEnumTables = IPDBEnumChildren<IPDBTable>;
using IPDBEnumInjectedSources = IPDBEnumChildren<IPDBInjectedSource>;
using IPDBEnumSectionContribs = IPDBEnumChildren<IPDBSectionContrib>;
using IPDBEnumFrameData = IPDBEnumChildren<IPDBFrameData>;

/// Specifies which PDB reader implementation is to be used.  Only a value
/// of PDB_ReaderType::DIA is currently supported, but Native is in the works.
enum class PDB_ReaderType {
  DIA = 0,
  Native = 1,
};

/// An enumeration indicating the type of data contained in this table.
enum class PDB_TableType {
  TableInvalid = 0,
  Symbols,
  SourceFiles,
  LineNumbers,
  SectionContribs,
  Segments,
  InjectedSources,
  FrameData,
  InputAssemblyFiles,
  Dbg
};

/// Defines flags used for enumerating child symbols.  This corresponds to the
/// NameSearchOptions enumeration which is documented here:
/// https://msdn.microsoft.com/en-us/library/yat28ads.aspx
enum PDB_NameSearchFlags {
  NS_Default = 0x0,
  NS_CaseSensitive = 0x1,
  NS_CaseInsensitive = 0x2,
  NS_FileNameExtMatch = 0x4,
  NS_Regex = 0x8,
  NS_UndecoratedName = 0x10,

  // For backward compatibility.
  NS_CaseInFileNameExt = NS_CaseInsensitive | NS_FileNameExtMatch,
  NS_CaseRegex = NS_Regex | NS_CaseSensitive,
  NS_CaseInRex = NS_Regex | NS_CaseInsensitive
};

/// Specifies the hash algorithm that a source file from a PDB was hashed with.
/// This corresponds to the CV_SourceChksum_t enumeration and are documented
/// here: https://msdn.microsoft.com/en-us/library/e96az21x.aspx
enum class PDB_Checksum { None = 0, MD5 = 1, SHA1 = 2, SHA256 = 3 };

/// These values correspond to the CV_CPU_TYPE_e enumeration, and are documented
/// here: https://msdn.microsoft.com/en-us/library/b2fc64ek.aspx
using PDB_Cpu = codeview::CPUType;

enum class PDB_Machine {
  Invalid = 0xffff,
  Unknown = 0x0,
  Am33 = 0x13,
  Amd64 = 0x8664,
  Arm = 0x1C0,
  Arm64 = 0xaa64,
  ArmNT = 0x1C4,
  Ebc = 0xEBC,
  x86 = 0x14C,
  Ia64 = 0x200,
  M32R = 0x9041,
  Mips16 = 0x266,
  MipsFpu = 0x366,
  MipsFpu16 = 0x466,
  PowerPC = 0x1F0,
  PowerPCFP = 0x1F1,
  R4000 = 0x166,
  SH3 = 0x1A2,
  SH3DSP = 0x1A3,
  SH4 = 0x1A6,
  SH5 = 0x1A8,
  Thumb = 0x1C2,
  WceMipsV2 = 0x169
};

// A struct with an inner unnamed enum with explicit underlying type resuls
// in an enum class that can implicitly convert to the underlying type, which
// is convenient for this enum.
struct PDB_SourceCompression {
  enum : uint32_t {
    // No compression. Produced e.g. by `link.exe /natvis:foo.natvis`.
    None,
    // Not known what produces this.
    RunLengthEncoded,
    // Not known what produces this.
    Huffman,
    // Not known what produces this.
    LZ,
    // Produced e.g. by `csc /debug`. The encoded data is its own mini-stream
    // with the following layout (in little endian):
    //   GUID LanguageTypeGuid;
    //   GUID LanguageVendorGuid;
    //   GUID DocumentTypeGuid;
    //   GUID HashFunctionGuid;
    //   uint32_t HashDataSize;
    //   uint32_t CompressedDataSize;
    // Followed by HashDataSize bytes containing a hash checksum,
    // followed by CompressedDataSize bytes containing source contents.
    //
    // CompressedDataSize can be 0, in this case only the hash data is present.
    // (CompressedDataSize is != 0 e.g. if `/embed` is passed to csc.exe.)
    // The compressed data format is:
    //   uint32_t UncompressedDataSize;
    // If UncompressedDataSize is 0, the data is stored uncompressed and
    // CompressedDataSize stores the uncompressed size.
    // If UncompressedDataSize is != 0, then the data is in raw deflate
    // encoding as described in rfc1951.
    //
    // A GUID is 16 bytes, stored in the usual
    //   uint32_t
    //   uint16_t
    //   uint16_t
    //   uint8_t[24]
    // layout.
    //
    // Well-known GUIDs for LanguageTypeGuid are:
    //   63a08714-fc37-11d2-904c-00c04fa302a1 C
    //   3a12d0b7-c26c-11d0-b442-00a0244a1dd2 C++
    //   3f5162f8-07c6-11d3-9053-00c04fa302a1 C#
    //   af046cd1-d0e1-11d2-977c-00a0c9b4d50c Cobol
    //   ab4f38c9-b6e6-43ba-be3b-58080b2ccce3 F#
    //   3a12d0b4-c26c-11d0-b442-00a0244a1dd2 Java
    //   3a12d0b6-c26c-11d0-b442-00a0244a1dd2 JScript
    //   af046cd2-d0e1-11d2-977c-00a0c9b4d50c Pascal
    //   3a12d0b8-c26c-11d0-b442-00a0244a1dd2 Visual Basic
    //
    // Well-known GUIDs for LanguageVendorGuid are:
    //   994b45c4-e6e9-11d2-903f-00c04fa302a1 Microsoft
    //
    // Well-known GUIDs for DocumentTypeGuid are:
    //   5a869d0b-6611-11d3-bd2a-0000f80849bd Text
    //
    // Well-known GUIDs for HashFunctionGuid are:
    //   406ea660-64cf-4c82-b6f0-42d48172a799 MD5    (HashDataSize is 16)
    //   ff1816ec-aa5e-4d10-87f7-6f4963833460 SHA1   (HashDataSize is 20)
    //   8829d00f-11b8-4213-878b-770e8597ac16 SHA256 (HashDataSize is 32)
    DotNet = 101,
  };
};

/// These values correspond to the CV_call_e enumeration, and are documented
/// at the following locations:
///   https://msdn.microsoft.com/en-us/library/b2fc64ek.aspx
///   https://msdn.microsoft.com/en-us/library/windows/desktop/ms680207(v=vs.85).aspx
using PDB_CallingConv = codeview::CallingConvention;

/// These values correspond to the CV_CFL_LANG enumeration, and are documented
/// here: https://msdn.microsoft.com/en-us/library/bw3aekw6.aspx
using PDB_Lang = codeview::SourceLanguage;

/// These values correspond to the DataKind enumeration, and are documented
/// here: https://msdn.microsoft.com/en-us/library/b2x2t313.aspx
enum class PDB_DataKind {
  Unknown,
  Local,
  StaticLocal,
  Param,
  ObjectPtr,
  FileStatic,
  Global,
  Member,
  StaticMember,
  Constant
};

/// These values correspond to the SymTagEnum enumeration, and are documented
/// here: https://msdn.microsoft.com/en-us/library/bkedss5f.aspx
enum class PDB_SymType {
  None,
  Exe,
  Compiland,
  CompilandDetails,
  CompilandEnv,
  Function,
  Block,
  Data,
  Annotation,
  Label,
  PublicSymbol,
  UDT,
  Enum,
  FunctionSig,
  PointerType,
  ArrayType,
  BuiltinType,
  Typedef,
  BaseClass,
  Friend,
  FunctionArg,
  FuncDebugStart,
  FuncDebugEnd,
  UsingNamespace,
  VTableShape,
  VTable,
  Custom,
  Thunk,
  CustomType,
  ManagedType,
  Dimension,
  CallSite,
  InlineSite,
  BaseInterface,
  VectorType,
  MatrixType,
  HLSLType,
  Caller,
  Callee,
  Export,
  HeapAllocationSite,
  CoffGroup,
  Inlinee,
  Max
};

/// These values correspond to the LocationType enumeration, and are documented
/// here: https://msdn.microsoft.com/en-us/library/f57kaez3.aspx
enum class PDB_LocType {
  Null,
  Static,
  TLS,
  RegRel,
  ThisRel,
  Enregistered,
  BitField,
  Slot,
  IlRel,
  MetaData,
  Constant,
  RegRelAliasIndir,
  Max
};

/// These values correspond to the UdtKind enumeration, and are documented
/// here: https://msdn.microsoft.com/en-us/library/wcstk66t.aspx
enum class PDB_UdtType { Struct, Class, Union, Interface };

/// These values correspond to the StackFrameTypeEnum enumeration, and are
/// documented here: https://msdn.microsoft.com/en-us/library/bc5207xw.aspx.
enum class PDB_StackFrameType : uint16_t {
  FPO,
  KernelTrap,
  KernelTSS,
  EBP,
  FrameData,
  Unknown = 0xffff
};

/// These values correspond to the MemoryTypeEnum enumeration, and are
/// documented here: https://msdn.microsoft.com/en-us/library/ms165609.aspx.
enum class PDB_MemoryType : uint16_t {
  Code,
  Data,
  Stack,
  HeapCode,
  Any = 0xffff
};

/// These values correspond to the Basictype enumeration, and are documented
/// here: https://msdn.microsoft.com/en-us/library/4szdtzc3.aspx
enum class PDB_BuiltinType {
  None = 0,
  Void = 1,
  Char = 2,
  WCharT = 3,
  Int = 6,
  UInt = 7,
  Float = 8,
  BCD = 9,
  Bool = 10,
  Long = 13,
  ULong = 14,
  Currency = 25,
  Date = 26,
  Variant = 27,
  Complex = 28,
  Bitfield = 29,
  BSTR = 30,
  HResult = 31,
  Char16 = 32,
  Char32 = 33,
  Char8 = 34,
};

/// These values correspond to the flags that can be combined to control the
/// return of an undecorated name for a C++ decorated name, and are documented
/// here: https://msdn.microsoft.com/en-us/library/kszfk0fs.aspx
enum PDB_UndnameFlags : uint32_t {
  Undname_Complete = 0x0,
  Undname_NoLeadingUnderscores = 0x1,
  Undname_NoMsKeywords = 0x2,
  Undname_NoFuncReturns = 0x4,
  Undname_NoAllocModel = 0x8,
  Undname_NoAllocLang = 0x10,
  Undname_Reserved1 = 0x20,
  Undname_Reserved2 = 0x40,
  Undname_NoThisType = 0x60,
  Undname_NoAccessSpec = 0x80,
  Undname_NoThrowSig = 0x100,
  Undname_NoMemberType = 0x200,
  Undname_NoReturnUDTModel = 0x400,
  Undname_32BitDecode = 0x800,
  Undname_NameOnly = 0x1000,
  Undname_TypeOnly = 0x2000,
  Undname_HaveParams = 0x4000,
  Undname_NoECSU = 0x8000,
  Undname_NoIdentCharCheck = 0x10000,
  Undname_NoPTR64 = 0x20000
};

enum class PDB_MemberAccess { Private = 1, Protected = 2, Public = 3 };

struct VersionInfo {
  uint32_t Major;
  uint32_t Minor;
  uint32_t Build;
  uint32_t QFE;
};

enum PDB_VariantType {
  Empty,
  Unknown,
  Int8,
  Int16,
  Int32,
  Int64,
  Single,
  Double,
  UInt8,
  UInt16,
  UInt32,
  UInt64,
  Bool,
  String
};

struct Variant {
  Variant() = default;

  explicit Variant(bool V) : Type(PDB_VariantType::Bool) { Value.Bool = V; }
  explicit Variant(int8_t V) : Type(PDB_VariantType::Int8) { Value.Int8 = V; }
  explicit Variant(int16_t V) : Type(PDB_VariantType::Int16) {
    Value.Int16 = V;
  }
  explicit Variant(int32_t V) : Type(PDB_VariantType::Int32) {
    Value.Int32 = V;
  }
  explicit Variant(int64_t V) : Type(PDB_VariantType::Int64) {
    Value.Int64 = V;
  }
  explicit Variant(float V) : Type(PDB_VariantType::Single) {
    Value.Single = V;
  }
  explicit Variant(double V) : Type(PDB_VariantType::Double) {
    Value.Double = V;
  }
  explicit Variant(uint8_t V) : Type(PDB_VariantType::UInt8) {
    Value.UInt8 = V;
  }
  explicit Variant(uint16_t V) : Type(PDB_VariantType::UInt16) {
    Value.UInt16 = V;
  }
  explicit Variant(uint32_t V) : Type(PDB_VariantType::UInt32) {
    Value.UInt32 = V;
  }
  explicit Variant(uint64_t V) : Type(PDB_VariantType::UInt64) {
    Value.UInt64 = V;
  }

  Variant(const Variant &Other) {
    *this = Other;
  }

  ~Variant() {
    if (Type == PDB_VariantType::String)
      delete[] Value.String;
  }

  PDB_VariantType Type = PDB_VariantType::Empty;
  union {
    bool Bool;
    int8_t Int8;
    int16_t Int16;
    int32_t Int32;
    int64_t Int64;
    float Single;
    double Double;
    uint8_t UInt8;
    uint16_t UInt16;
    uint32_t UInt32;
    uint64_t UInt64;
    char *String;
  } Value;

  bool isIntegralType() const {
    switch (Type) {
    case Bool:
    case Int8:
    case Int16:
    case Int32:
    case Int64:
    case UInt8:
    case UInt16:
    case UInt32:
    case UInt64:
      return true;
    default:
      return false;
    }
  }

#define VARIANT_WIDTH(Enum, NumBits)                                           \
  case PDB_VariantType::Enum:                                                  \
    return NumBits;

  unsigned getBitWidth() const {
    switch (Type) {
      VARIANT_WIDTH(Bool, 1u)
      VARIANT_WIDTH(Int8, 8u)
      VARIANT_WIDTH(Int16, 16u)
      VARIANT_WIDTH(Int32, 32u)
      VARIANT_WIDTH(Int64, 64u)
      VARIANT_WIDTH(Single, 32u)
      VARIANT_WIDTH(Double, 64u)
      VARIANT_WIDTH(UInt8, 8u)
      VARIANT_WIDTH(UInt16, 16u)
      VARIANT_WIDTH(UInt32, 32u)
      VARIANT_WIDTH(UInt64, 64u)
    default:
      assert(false && "Variant::toAPSInt called on non-numeric type");
      return 0u;
    }
  }

#undef VARIANT_WIDTH

#define VARIANT_APSINT(Enum, NumBits, IsUnsigned)                              \
  case PDB_VariantType::Enum:                                                  \
    return APSInt(APInt(NumBits, Value.Enum), IsUnsigned);

  APSInt toAPSInt() const {
    switch (Type) {
      VARIANT_APSINT(Bool, 1u, true)
      VARIANT_APSINT(Int8, 8u, false)
      VARIANT_APSINT(Int16, 16u, false)
      VARIANT_APSINT(Int32, 32u, false)
      VARIANT_APSINT(Int64, 64u, false)
      VARIANT_APSINT(UInt8, 8u, true)
      VARIANT_APSINT(UInt16, 16u, true)
      VARIANT_APSINT(UInt32, 32u, true)
      VARIANT_APSINT(UInt64, 64u, true)
    default:
      assert(false && "Variant::toAPSInt called on non-integral type");
      return APSInt();
    }
  }

#undef VARIANT_APSINT

  APFloat toAPFloat() const {
    // Float constants may be tagged as integers.
    switch (Type) {
    case PDB_VariantType::Single:
    case PDB_VariantType::UInt32:
    case PDB_VariantType::Int32:
      return APFloat(Value.Single);
    case PDB_VariantType::Double:
    case PDB_VariantType::UInt64:
    case PDB_VariantType::Int64:
      return APFloat(Value.Double);
    default:
      assert(false && "Variant::toAPFloat called on non-floating-point type");
      return APFloat::getZero(APFloat::IEEEsingle());
    }
  }

#define VARIANT_EQUAL_CASE(Enum)                                               \
  case PDB_VariantType::Enum:                                                  \
    return Value.Enum == Other.Value.Enum;

  bool operator==(const Variant &Other) const {
    if (Type != Other.Type)
      return false;
    switch (Type) {
      VARIANT_EQUAL_CASE(Bool)
      VARIANT_EQUAL_CASE(Int8)
      VARIANT_EQUAL_CASE(Int16)
      VARIANT_EQUAL_CASE(Int32)
      VARIANT_EQUAL_CASE(Int64)
      VARIANT_EQUAL_CASE(Single)
      VARIANT_EQUAL_CASE(Double)
      VARIANT_EQUAL_CASE(UInt8)
      VARIANT_EQUAL_CASE(UInt16)
      VARIANT_EQUAL_CASE(UInt32)
      VARIANT_EQUAL_CASE(UInt64)
      VARIANT_EQUAL_CASE(String)
    default:
      return true;
    }
  }

#undef VARIANT_EQUAL_CASE

  bool operator!=(const Variant &Other) const { return !(*this == Other); }
  Variant &operator=(const Variant &Other) {
    if (this == &Other)
      return *this;
    if (Type == PDB_VariantType::String)
      delete[] Value.String;
    Type = Other.Type;
    Value = Other.Value;
    if (Other.Type == PDB_VariantType::String &&
        Other.Value.String != nullptr) {
      Value.String = new char[strlen(Other.Value.String) + 1];
      ::strcpy(Value.String, Other.Value.String);
    }
    return *this;
  }
};

} // end namespace pdb
} // end namespace llvm

namespace std {

template <> struct hash<llvm::pdb::PDB_SymType> {
  using argument_type = llvm::pdb::PDB_SymType;
  using result_type = std::size_t;

  result_type operator()(const argument_type &Arg) const {
    return std::hash<int>()(static_cast<int>(Arg));
  }
};

} // end namespace std

#endif // LLVM_DEBUGINFO_PDB_PDBTYPES_H
