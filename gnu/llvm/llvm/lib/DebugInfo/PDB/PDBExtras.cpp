//===- PDBExtras.cpp - helper functions and classes for PDBs --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/PDBExtras.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm::pdb;

#define CASE_OUTPUT_ENUM_CLASS_STR(Class, Value, Str, Stream)                  \
  case Class::Value:                                                           \
    Stream << Str;                                                             \
    break;

#define CASE_OUTPUT_ENUM_CLASS_NAME(Class, Value, Stream)                      \
  CASE_OUTPUT_ENUM_CLASS_STR(Class, Value, #Value, Stream)

raw_ostream &llvm::pdb::operator<<(raw_ostream &OS,
                                   const PDB_VariantType &Type) {
  switch (Type) {
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_VariantType, Bool, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_VariantType, Single, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_VariantType, Double, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_VariantType, Int8, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_VariantType, Int16, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_VariantType, Int32, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_VariantType, Int64, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_VariantType, UInt8, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_VariantType, UInt16, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_VariantType, UInt32, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_VariantType, UInt64, OS)
    default:
      OS << "Unknown";
  }
  return OS;
}

raw_ostream &llvm::pdb::operator<<(raw_ostream &OS,
                                   const PDB_BuiltinType &Type) {
  switch (Type) {
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_BuiltinType, None, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_BuiltinType, Void, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_BuiltinType, Char, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_BuiltinType, WCharT, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_BuiltinType, Int, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_BuiltinType, UInt, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_BuiltinType, Float, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_BuiltinType, BCD, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_BuiltinType, Bool, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_BuiltinType, Long, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_BuiltinType, ULong, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_BuiltinType, Currency, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_BuiltinType, Date, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_BuiltinType, Variant, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_BuiltinType, Complex, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_BuiltinType, Bitfield, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_BuiltinType, BSTR, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_BuiltinType, HResult, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_BuiltinType, Char16, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_BuiltinType, Char32, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_BuiltinType, Char8, OS)
  }
  return OS;
}

raw_ostream &llvm::pdb::operator<<(raw_ostream &OS,
                                   const PDB_CallingConv &Conv) {
  OS << "__";
  switch (Conv) {
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_CallingConv, NearC      , "cdecl", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_CallingConv, FarC       , "cdecl", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_CallingConv, NearPascal , "pascal", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_CallingConv, FarPascal  , "pascal", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_CallingConv, NearFast   , "fastcall", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_CallingConv, FarFast    , "fastcall", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_CallingConv, NearStdCall, "stdcall", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_CallingConv, FarStdCall , "stdcall", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_CallingConv, NearSysCall, "syscall", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_CallingConv, FarSysCall , "syscall", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_CallingConv, ThisCall   , "thiscall", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_CallingConv, MipsCall   , "mipscall", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_CallingConv, Generic    , "genericcall", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_CallingConv, AlphaCall  , "alphacall", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_CallingConv, PpcCall    , "ppccall", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_CallingConv, SHCall     , "superhcall", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_CallingConv, ArmCall    , "armcall", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_CallingConv, AM33Call   , "am33call", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_CallingConv, TriCall    , "tricall", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_CallingConv, SH5Call    , "sh5call", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_CallingConv, M32RCall   , "m32rcall", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_CallingConv, ClrCall    , "clrcall", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_CallingConv, Inline     , "inlinecall", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_CallingConv, NearVector , "vectorcall", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_CallingConv, Swift, "swiftcall", OS)
  }
  return OS;
}

raw_ostream &llvm::pdb::operator<<(raw_ostream &OS, const PDB_DataKind &Data) {
  switch (Data) {
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_DataKind, Unknown, "unknown", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_DataKind, Local, "local", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_DataKind, StaticLocal, "static local", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_DataKind, Param, "param", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_DataKind, ObjectPtr, "this ptr", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_DataKind, FileStatic, "static global", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_DataKind, Global, "global", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_DataKind, Member, "member", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_DataKind, StaticMember, "static member", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_DataKind, Constant, "const", OS)
  }
  return OS;
}

raw_ostream &llvm::pdb::operator<<(raw_ostream &OS,
                                   const llvm::codeview::CPURegister &CpuReg) {
  if (CpuReg.Cpu == llvm::codeview::CPUType::ARMNT) {
    switch (CpuReg.Reg) {
#define CV_REGISTERS_ARM
#define CV_REGISTER(name, val)                                                 \
  case codeview::RegisterId::name:                                             \
    OS << #name;                                                               \
    return OS;
#include "llvm/DebugInfo/CodeView/CodeViewRegisters.def"
#undef CV_REGISTER
#undef CV_REGISTERS_ARM

    default:
      break;
    }
  } else if (CpuReg.Cpu == llvm::codeview::CPUType::ARM64) {
    switch (CpuReg.Reg) {
#define CV_REGISTERS_ARM64
#define CV_REGISTER(name, val)                                                 \
  case codeview::RegisterId::name:                                             \
    OS << #name;                                                               \
    return OS;
#include "llvm/DebugInfo/CodeView/CodeViewRegisters.def"
#undef CV_REGISTER
#undef CV_REGISTERS_ARM64

    default:
      break;
    }
  } else {
    switch (CpuReg.Reg) {
#define CV_REGISTERS_X86
#define CV_REGISTER(name, val)                                                 \
  case codeview::RegisterId::name:                                             \
    OS << #name;                                                               \
    return OS;
#include "llvm/DebugInfo/CodeView/CodeViewRegisters.def"
#undef CV_REGISTER
#undef CV_REGISTERS_X86

    default:
      break;
    }
  }
  OS << static_cast<int>(CpuReg.Reg);
  return OS;
}

raw_ostream &llvm::pdb::operator<<(raw_ostream &OS, const PDB_LocType &Loc) {
  switch (Loc) {
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_LocType, Static, "static", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_LocType, TLS, "tls", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_LocType, RegRel, "regrel", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_LocType, ThisRel, "thisrel", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_LocType, Enregistered, "register", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_LocType, BitField, "bitfield", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_LocType, Slot, "slot", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_LocType, IlRel, "IL rel", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_LocType, MetaData, "metadata", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_LocType, Constant, "constant", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_LocType, RegRelAliasIndir,
                               "regrelaliasindir", OS)
  default:
    OS << "Unknown";
  }
  return OS;
}

raw_ostream &llvm::pdb::operator<<(raw_ostream &OS,
                                   const codeview::ThunkOrdinal &Thunk) {
  switch (Thunk) {
    CASE_OUTPUT_ENUM_CLASS_NAME(codeview::ThunkOrdinal, BranchIsland, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(codeview::ThunkOrdinal, Pcode, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(codeview::ThunkOrdinal, Standard, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(codeview::ThunkOrdinal, ThisAdjustor, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(codeview::ThunkOrdinal, TrampIncremental, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(codeview::ThunkOrdinal, UnknownLoad, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(codeview::ThunkOrdinal, Vcall, OS)
  }
  return OS;
}

raw_ostream &llvm::pdb::operator<<(raw_ostream &OS,
                                   const PDB_Checksum &Checksum) {
  switch (Checksum) {
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Checksum, None, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Checksum, MD5, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Checksum, SHA1, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Checksum, SHA256, OS)
  }
  return OS;
}

raw_ostream &llvm::pdb::operator<<(raw_ostream &OS, const PDB_Lang &Lang) {
  switch (Lang) {
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Lang, C, OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_Lang, Cpp, "C++", OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Lang, Fortran, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Lang, Masm, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Lang, Pascal, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Lang, Basic, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Lang, Cobol, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Lang, Link, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Lang, Cvtres, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Lang, Cvtpgd, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Lang, CSharp, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Lang, VB, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Lang, ILAsm, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Lang, Java, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Lang, JScript, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Lang, MSIL, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Lang, HLSL, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Lang, D, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Lang, Swift, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Lang, Rust, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Lang, ObjC, OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_Lang, ObjCpp, "ObjC++", OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Lang, AliasObj, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Lang, Go, OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_Lang, OldSwift, "Swift", OS)
  }
  return OS;
}

raw_ostream &llvm::pdb::operator<<(raw_ostream &OS, const PDB_SymType &Tag) {
  switch (Tag) {
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, Exe, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, Compiland, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, CompilandDetails, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, CompilandEnv, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, Function, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, Block, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, Data, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, Annotation, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, Label, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, PublicSymbol, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, UDT, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, Enum, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, FunctionSig, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, PointerType, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, ArrayType, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, BuiltinType, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, Typedef, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, BaseClass, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, Friend, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, FunctionArg, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, FuncDebugStart, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, FuncDebugEnd, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, UsingNamespace, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, VTableShape, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, VTable, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, Custom, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, Thunk, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, CustomType, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, ManagedType, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, Dimension, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, CallSite, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, InlineSite, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, BaseInterface, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, VectorType, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, MatrixType, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, HLSLType, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, Caller, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, Callee, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, Export, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, HeapAllocationSite, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, CoffGroup, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SymType, Inlinee, OS)
  default:
    OS << "Unknown SymTag " << uint32_t(Tag);
  }
  return OS;
}

raw_ostream &llvm::pdb::operator<<(raw_ostream &OS,
                                   const PDB_MemberAccess &Access) {
  switch (Access) {
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_MemberAccess, Public, "public", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_MemberAccess, Protected, "protected", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_MemberAccess, Private, "private", OS)
  }
  return OS;
}

raw_ostream &llvm::pdb::operator<<(raw_ostream &OS, const PDB_UdtType &Type) {
  switch (Type) {
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_UdtType, Class, "class", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_UdtType, Struct, "struct", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_UdtType, Interface, "interface", OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_UdtType, Union, "union", OS)
  }
  return OS;
}

raw_ostream &llvm::pdb::operator<<(raw_ostream &OS,
                                   const PDB_Machine &Machine) {
  switch (Machine) {
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Machine, Am33, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Machine, Amd64, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Machine, Arm, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Machine, ArmNT, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Machine, Ebc, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Machine, x86, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Machine, Ia64, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Machine, M32R, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Machine, Mips16, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Machine, MipsFpu, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Machine, MipsFpu16, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Machine, PowerPC, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Machine, PowerPCFP, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Machine, R4000, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Machine, SH3, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Machine, SH3DSP, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Machine, SH4, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Machine, SH5, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Machine, Thumb, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_Machine, WceMipsV2, OS)
  default:
    OS << "Unknown";
  }
  return OS;
}

raw_ostream &llvm::pdb::dumpPDBSourceCompression(raw_ostream &OS,
                                                 uint32_t Compression) {
  switch (Compression) {
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SourceCompression, None, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SourceCompression, Huffman, OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SourceCompression, LZ, OS)
    CASE_OUTPUT_ENUM_CLASS_STR(PDB_SourceCompression, RunLengthEncoded, "RLE",
                               OS)
    CASE_OUTPUT_ENUM_CLASS_NAME(PDB_SourceCompression, DotNet, OS)
  default:
    OS << "Unknown (" << Compression << ")";
  }
  return OS;
}

raw_ostream &llvm::pdb::operator<<(raw_ostream &OS, const Variant &Value) {
  switch (Value.Type) {
    case PDB_VariantType::Bool:
      OS << (Value.Value.Bool ? "true" : "false");
      break;
    case PDB_VariantType::Double:
      OS << Value.Value.Double;
      break;
    case PDB_VariantType::Int16:
      OS << Value.Value.Int16;
      break;
    case PDB_VariantType::Int32:
      OS << Value.Value.Int32;
      break;
    case PDB_VariantType::Int64:
      OS << Value.Value.Int64;
      break;
    case PDB_VariantType::Int8:
      OS << static_cast<int>(Value.Value.Int8);
      break;
    case PDB_VariantType::Single:
      OS << Value.Value.Single;
      break;
    case PDB_VariantType::UInt16:
      OS << Value.Value.UInt16;
      break;
    case PDB_VariantType::UInt32:
      OS << Value.Value.UInt32;
      break;
    case PDB_VariantType::UInt64:
      OS << Value.Value.UInt64;
      break;
    case PDB_VariantType::UInt8:
      OS << static_cast<unsigned>(Value.Value.UInt8);
      break;
    case PDB_VariantType::String:
      OS << Value.Value.String;
      break;
    default:
      OS << Value.Type;
  }
  return OS;
}

raw_ostream &llvm::pdb::operator<<(raw_ostream &OS,
                                   const VersionInfo &Version) {
  OS << Version.Major << "." << Version.Minor << "." << Version.Build;
  return OS;
}

raw_ostream &llvm::pdb::operator<<(raw_ostream &OS, const TagStats &Stats) {
  for (auto Tag : Stats) {
    OS << Tag.first << ":" << Tag.second << " ";
  }
  return OS;
}
