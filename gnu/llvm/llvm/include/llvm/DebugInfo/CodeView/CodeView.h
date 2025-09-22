//===- CodeView.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines constants and basic types describing CodeView debug information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_CODEVIEW_H
#define LLVM_DEBUGINFO_CODEVIEW_CODEVIEW_H

#include <cinttypes>
#include <type_traits>

#include "llvm/ADT/STLForwardCompat.h"
#include "llvm/Support/Endian.h"

namespace llvm {
namespace codeview {

/// Distinguishes individual records in .debug$T or .debug$P section or PDB type
/// stream. The documentation and headers talk about this as the "leaf" type.
enum class TypeRecordKind : uint16_t {
#define TYPE_RECORD(lf_ename, value, name) name = value,
#include "CodeViewTypes.def"
};

/// Duplicate copy of the above enum, but using the official CV names. Useful
/// for reference purposes and when dealing with unknown record types.
enum TypeLeafKind : uint16_t {
#define CV_TYPE(name, val) name = val,
#include "CodeViewTypes.def"
};

/// Distinguishes individual records in the Symbols subsection of a .debug$S
/// section. Equivalent to SYM_ENUM_e in cvinfo.h.
enum class SymbolRecordKind : uint16_t {
#define SYMBOL_RECORD(lf_ename, value, name) name = value,
#include "CodeViewSymbols.def"
};

/// Duplicate copy of the above enum, but using the official CV names. Useful
/// for reference purposes and when dealing with unknown record types.
enum SymbolKind : uint16_t {
#define CV_SYMBOL(name, val) name = val,
#include "CodeViewSymbols.def"
};

#define CV_DEFINE_ENUM_CLASS_FLAGS_OPERATORS(Class)                            \
  inline Class operator|(Class a, Class b) {                                   \
    return static_cast<Class>(llvm::to_underlying(a) |                         \
                              llvm::to_underlying(b));                         \
  }                                                                            \
  inline Class operator&(Class a, Class b) {                                   \
    return static_cast<Class>(llvm::to_underlying(a) &                         \
                              llvm::to_underlying(b));                         \
  }                                                                            \
  inline Class operator~(Class a) {                                            \
    return static_cast<Class>(~llvm::to_underlying(a));                        \
  }                                                                            \
  inline Class &operator|=(Class &a, Class b) {                                \
    a = a | b;                                                                 \
    return a;                                                                  \
  }                                                                            \
  inline Class &operator&=(Class &a, Class b) {                                \
    a = a & b;                                                                 \
    return a;                                                                  \
  }

/// These values correspond to the CV_CPU_TYPE_e enumeration, and are documented
/// here: https://msdn.microsoft.com/en-us/library/b2fc64ek.aspx
enum class CPUType : uint16_t {
  Intel8080 = 0x0,
  Intel8086 = 0x1,
  Intel80286 = 0x2,
  Intel80386 = 0x3,
  Intel80486 = 0x4,
  Pentium = 0x5,
  PentiumPro = 0x6,
  Pentium3 = 0x7,
  MIPS = 0x10,
  MIPS16 = 0x11,
  MIPS32 = 0x12,
  MIPS64 = 0x13,
  MIPSI = 0x14,
  MIPSII = 0x15,
  MIPSIII = 0x16,
  MIPSIV = 0x17,
  MIPSV = 0x18,
  M68000 = 0x20,
  M68010 = 0x21,
  M68020 = 0x22,
  M68030 = 0x23,
  M68040 = 0x24,
  Alpha = 0x30,
  Alpha21164 = 0x31,
  Alpha21164A = 0x32,
  Alpha21264 = 0x33,
  Alpha21364 = 0x34,
  PPC601 = 0x40,
  PPC603 = 0x41,
  PPC604 = 0x42,
  PPC620 = 0x43,
  PPCFP = 0x44,
  PPCBE = 0x45,
  SH3 = 0x50,
  SH3E = 0x51,
  SH3DSP = 0x52,
  SH4 = 0x53,
  SHMedia = 0x54,
  ARM3 = 0x60,
  ARM4 = 0x61,
  ARM4T = 0x62,
  ARM5 = 0x63,
  ARM5T = 0x64,
  ARM6 = 0x65,
  ARM_XMAC = 0x66,
  ARM_WMMX = 0x67,
  ARM7 = 0x68,
  Omni = 0x70,
  Ia64 = 0x80,
  Ia64_2 = 0x81,
  CEE = 0x90,
  AM33 = 0xa0,
  M32R = 0xb0,
  TriCore = 0xc0,
  X64 = 0xd0,
  EBC = 0xe0,
  Thumb = 0xf0,
  ARMNT = 0xf4,
  ARM64 = 0xf6,
  HybridX86ARM64 = 0xf7,
  ARM64EC = 0xf8,
  ARM64X = 0xf9,
  Unknown = 0xff,
  D3D11_Shader = 0x100,
};

/// These values correspond to the CV_CFL_LANG enumeration in the Microsoft
/// Debug Interface Access SDK, and are documented here:
/// https://learn.microsoft.com/en-us/visualstudio/debugger/debug-interface-access/cv-cfl-lang
enum SourceLanguage : uint8_t {
  C = 0x00,
  Cpp = 0x01,
  Fortran = 0x02,
  Masm = 0x03,
  Pascal = 0x04,
  Basic = 0x05,
  Cobol = 0x06,
  Link = 0x07,
  Cvtres = 0x08,
  Cvtpgd = 0x09,
  CSharp = 0x0a,
  VB = 0x0b,
  ILAsm = 0x0c,
  Java = 0x0d,
  JScript = 0x0e,
  MSIL = 0x0f,
  HLSL = 0x10,
  ObjC = 0x11,
  ObjCpp = 0x12,
  Swift = 0x13,
  AliasObj = 0x14,
  Rust = 0x15,
  Go = 0x16,

  /// The DMD compiler emits 'D' for the CV source language. Microsoft does not
  /// have an enumerator for it yet.
  D = 'D',
  /// The Swift compiler used to emit 'S' for the CV source language, but
  /// current versions emit the enumerator defined above.
  OldSwift = 'S',
};

/// These values correspond to the CV_call_e enumeration, and are documented
/// at the following locations:
///   https://msdn.microsoft.com/en-us/library/b2fc64ek.aspx
///   https://msdn.microsoft.com/en-us/library/windows/desktop/ms680207(v=vs.85).aspx
///
enum class CallingConvention : uint8_t {
  NearC = 0x00,       // near right to left push, caller pops stack
  FarC = 0x01,        // far right to left push, caller pops stack
  NearPascal = 0x02,  // near left to right push, callee pops stack
  FarPascal = 0x03,   // far left to right push, callee pops stack
  NearFast = 0x04,    // near left to right push with regs, callee pops stack
  FarFast = 0x05,     // far left to right push with regs, callee pops stack
  NearStdCall = 0x07, // near standard call
  FarStdCall = 0x08,  // far standard call
  NearSysCall = 0x09, // near sys call
  FarSysCall = 0x0a,  // far sys call
  ThisCall = 0x0b,    // this call (this passed in register)
  MipsCall = 0x0c,    // Mips call
  Generic = 0x0d,     // Generic call sequence
  AlphaCall = 0x0e,   // Alpha call
  PpcCall = 0x0f,     // PPC call
  SHCall = 0x10,      // Hitachi SuperH call
  ArmCall = 0x11,     // ARM call
  AM33Call = 0x12,    // AM33 call
  TriCall = 0x13,     // TriCore Call
  SH5Call = 0x14,     // Hitachi SuperH-5 call
  M32RCall = 0x15,    // M32R Call
  ClrCall = 0x16,     // clr call
  Inline =
      0x17, // Marker for routines always inlined and thus lacking a convention
  NearVector = 0x18, // near left to right push with regs, callee pops stack
  Swift = 0x19,      // Swift call
};

enum class ClassOptions : uint16_t {
  None = 0x0000,
  Packed = 0x0001,
  HasConstructorOrDestructor = 0x0002,
  HasOverloadedOperator = 0x0004,
  Nested = 0x0008,
  ContainsNestedClass = 0x0010,
  HasOverloadedAssignmentOperator = 0x0020,
  HasConversionOperator = 0x0040,
  ForwardReference = 0x0080,
  Scoped = 0x0100,
  HasUniqueName = 0x0200,
  Sealed = 0x0400,
  Intrinsic = 0x2000
};
CV_DEFINE_ENUM_CLASS_FLAGS_OPERATORS(ClassOptions)

enum class FrameProcedureOptions : uint32_t {
  None = 0x00000000,
  HasAlloca = 0x00000001,
  HasSetJmp = 0x00000002,
  HasLongJmp = 0x00000004,
  HasInlineAssembly = 0x00000008,
  HasExceptionHandling = 0x00000010,
  MarkedInline = 0x00000020,
  HasStructuredExceptionHandling = 0x00000040,
  Naked = 0x00000080,
  SecurityChecks = 0x00000100,
  AsynchronousExceptionHandling = 0x00000200,
  NoStackOrderingForSecurityChecks = 0x00000400,
  Inlined = 0x00000800,
  StrictSecurityChecks = 0x00001000,
  SafeBuffers = 0x00002000,
  EncodedLocalBasePointerMask = 0x0000C000,
  EncodedParamBasePointerMask = 0x00030000,
  ProfileGuidedOptimization = 0x00040000,
  ValidProfileCounts = 0x00080000,
  OptimizedForSpeed = 0x00100000,
  GuardCfg = 0x00200000,
  GuardCfw = 0x00400000
};
CV_DEFINE_ENUM_CLASS_FLAGS_OPERATORS(FrameProcedureOptions)

enum class FunctionOptions : uint8_t {
  None = 0x00,
  CxxReturnUdt = 0x01,
  Constructor = 0x02,
  ConstructorWithVirtualBases = 0x04
};
CV_DEFINE_ENUM_CLASS_FLAGS_OPERATORS(FunctionOptions)

enum class HfaKind : uint8_t {
  None = 0x00,
  Float = 0x01,
  Double = 0x02,
  Other = 0x03
};

/// Source-level access specifier. (CV_access_e)
enum class MemberAccess : uint8_t {
  None = 0,
  Private = 1,
  Protected = 2,
  Public = 3
};

/// Part of member attribute flags. (CV_methodprop_e)
enum class MethodKind : uint8_t {
  Vanilla = 0x00,
  Virtual = 0x01,
  Static = 0x02,
  Friend = 0x03,
  IntroducingVirtual = 0x04,
  PureVirtual = 0x05,
  PureIntroducingVirtual = 0x06
};

/// Equivalent to CV_fldattr_t bitfield.
enum class MethodOptions : uint16_t {
  None = 0x0000,
  AccessMask = 0x0003,
  MethodKindMask = 0x001c,
  Pseudo = 0x0020,
  NoInherit = 0x0040,
  NoConstruct = 0x0080,
  CompilerGenerated = 0x0100,
  Sealed = 0x0200
};
CV_DEFINE_ENUM_CLASS_FLAGS_OPERATORS(MethodOptions)

/// Equivalent to CV_LABEL_TYPE_e.
enum class LabelType : uint16_t {
  Near = 0x0,
  Far = 0x4,
};

/// Equivalent to CV_modifier_t.
/// TODO: Add flag for _Atomic modifier
enum class ModifierOptions : uint16_t {
  None = 0x0000,
  Const = 0x0001,
  Volatile = 0x0002,
  Unaligned = 0x0004
};
CV_DEFINE_ENUM_CLASS_FLAGS_OPERATORS(ModifierOptions)

// If the subsection kind has this bit set, then the linker should ignore it.
enum : uint32_t { SubsectionIgnoreFlag = 0x80000000 };

enum class DebugSubsectionKind : uint32_t {
  None = 0,
  Symbols = 0xf1,
  Lines = 0xf2,
  StringTable = 0xf3,
  FileChecksums = 0xf4,
  FrameData = 0xf5,
  InlineeLines = 0xf6,
  CrossScopeImports = 0xf7,
  CrossScopeExports = 0xf8,

  // These appear to relate to .Net assembly info.
  ILLines = 0xf9,
  FuncMDTokenMap = 0xfa,
  TypeMDTokenMap = 0xfb,
  MergedAssemblyInput = 0xfc,

  CoffSymbolRVA = 0xfd,

  XfgHashType = 0xff,
  XfgHashVirtual = 0x100,
};

/// Equivalent to CV_ptrtype_e.
enum class PointerKind : uint8_t {
  Near16 = 0x00,                // 16 bit pointer
  Far16 = 0x01,                 // 16:16 far pointer
  Huge16 = 0x02,                // 16:16 huge pointer
  BasedOnSegment = 0x03,        // based on segment
  BasedOnValue = 0x04,          // based on value of base
  BasedOnSegmentValue = 0x05,   // based on segment value of base
  BasedOnAddress = 0x06,        // based on address of base
  BasedOnSegmentAddress = 0x07, // based on segment address of base
  BasedOnType = 0x08,           // based on type
  BasedOnSelf = 0x09,           // based on self
  Near32 = 0x0a,                // 32 bit pointer
  Far32 = 0x0b,                 // 16:32 pointer
  Near64 = 0x0c                 // 64 bit pointer
};

/// Equivalent to CV_ptrmode_e.
enum class PointerMode : uint8_t {
  Pointer = 0x00,                 // "normal" pointer
  LValueReference = 0x01,         // "old" reference
  PointerToDataMember = 0x02,     // pointer to data member
  PointerToMemberFunction = 0x03, // pointer to member function
  RValueReference = 0x04          // r-value reference
};

/// Equivalent to misc lfPointerAttr bitfields.
enum class PointerOptions : uint32_t {
  None = 0x00000000,
  Flat32 = 0x00000100,
  Volatile = 0x00000200,
  Const = 0x00000400,
  Unaligned = 0x00000800,
  Restrict = 0x00001000,
  WinRTSmartPointer = 0x00080000,
  LValueRefThisPointer = 0x00100000,
  RValueRefThisPointer = 0x00200000
};
CV_DEFINE_ENUM_CLASS_FLAGS_OPERATORS(PointerOptions)

/// Equivalent to CV_pmtype_e.
enum class PointerToMemberRepresentation : uint16_t {
  Unknown = 0x00,                     // not specified (pre VC8)
  SingleInheritanceData = 0x01,       // member data, single inheritance
  MultipleInheritanceData = 0x02,     // member data, multiple inheritance
  VirtualInheritanceData = 0x03,      // member data, virtual inheritance
  GeneralData = 0x04,                 // member data, most general
  SingleInheritanceFunction = 0x05,   // member function, single inheritance
  MultipleInheritanceFunction = 0x06, // member function, multiple inheritance
  VirtualInheritanceFunction = 0x07,  // member function, virtual inheritance
  GeneralFunction = 0x08              // member function, most general
};

enum class VFTableSlotKind : uint8_t {
  Near16 = 0x00,
  Far16 = 0x01,
  This = 0x02,
  Outer = 0x03,
  Meta = 0x04,
  Near = 0x05,
  Far = 0x06
};

enum class WindowsRTClassKind : uint8_t {
  None = 0x00,
  RefClass = 0x01,
  ValueClass = 0x02,
  Interface = 0x03
};

/// Corresponds to CV_LVARFLAGS bitfield.
enum class LocalSymFlags : uint16_t {
  None = 0,
  IsParameter = 1 << 0,
  IsAddressTaken = 1 << 1,
  IsCompilerGenerated = 1 << 2,
  IsAggregate = 1 << 3,
  IsAggregated = 1 << 4,
  IsAliased = 1 << 5,
  IsAlias = 1 << 6,
  IsReturnValue = 1 << 7,
  IsOptimizedOut = 1 << 8,
  IsEnregisteredGlobal = 1 << 9,
  IsEnregisteredStatic = 1 << 10,
};
CV_DEFINE_ENUM_CLASS_FLAGS_OPERATORS(LocalSymFlags)

/// Corresponds to the CV_PUBSYMFLAGS bitfield.
enum class PublicSymFlags : uint32_t {
  None = 0,
  Code = 1 << 0,
  Function = 1 << 1,
  Managed = 1 << 2,
  MSIL = 1 << 3,
};
CV_DEFINE_ENUM_CLASS_FLAGS_OPERATORS(PublicSymFlags)

/// Corresponds to the CV_PROCFLAGS bitfield.
enum class ProcSymFlags : uint8_t {
  None = 0,
  HasFP = 1 << 0,
  HasIRET = 1 << 1,
  HasFRET = 1 << 2,
  IsNoReturn = 1 << 3,
  IsUnreachable = 1 << 4,
  HasCustomCallingConv = 1 << 5,
  IsNoInline = 1 << 6,
  HasOptimizedDebugInfo = 1 << 7,
};
CV_DEFINE_ENUM_CLASS_FLAGS_OPERATORS(ProcSymFlags)

/// Corresponds to COMPILESYM2::Flags bitfield.
enum class CompileSym2Flags : uint32_t {
  None = 0,
  SourceLanguageMask = 0xFF,
  EC = 1 << 8,
  NoDbgInfo = 1 << 9,
  LTCG = 1 << 10,
  NoDataAlign = 1 << 11,
  ManagedPresent = 1 << 12,
  SecurityChecks = 1 << 13,
  HotPatch = 1 << 14,
  CVTCIL = 1 << 15,
  MSILModule = 1 << 16,
};
CV_DEFINE_ENUM_CLASS_FLAGS_OPERATORS(CompileSym2Flags)

/// Corresponds to COMPILESYM3::Flags bitfield.
enum class CompileSym3Flags : uint32_t {
  None = 0,
  SourceLanguageMask = 0xFF,
  EC = 1 << 8,
  NoDbgInfo = 1 << 9,
  LTCG = 1 << 10,
  NoDataAlign = 1 << 11,
  ManagedPresent = 1 << 12,
  SecurityChecks = 1 << 13,
  HotPatch = 1 << 14,
  CVTCIL = 1 << 15,
  MSILModule = 1 << 16,
  Sdl = 1 << 17,
  PGO = 1 << 18,
  Exp = 1 << 19,
};
CV_DEFINE_ENUM_CLASS_FLAGS_OPERATORS(CompileSym3Flags)

enum class ExportFlags : uint16_t {
  None = 0,
  IsConstant = 1 << 0,
  IsData = 1 << 1,
  IsPrivate = 1 << 2,
  HasNoName = 1 << 3,
  HasExplicitOrdinal = 1 << 4,
  IsForwarder = 1 << 5
};
CV_DEFINE_ENUM_CLASS_FLAGS_OPERATORS(ExportFlags)

// Corresponds to BinaryAnnotationOpcode enum.
enum class BinaryAnnotationsOpCode : uint32_t {
  Invalid,
  CodeOffset,
  ChangeCodeOffsetBase,
  ChangeCodeOffset,
  ChangeCodeLength,
  ChangeFile,
  ChangeLineOffset,
  ChangeLineEndDelta,
  ChangeRangeKind,
  ChangeColumnStart,
  ChangeColumnEndDelta,
  ChangeCodeOffsetAndLineOffset,
  ChangeCodeLengthAndCodeOffset,
  ChangeColumnEnd,
};

// Corresponds to CV_cookietype_e enum.
enum class FrameCookieKind : uint8_t {
  Copy,
  XorStackPointer,
  XorFramePointer,
  XorR13,
};

// Corresponds to CV_HREG_e enum.
enum class RegisterId : uint16_t {
#define CV_REGISTERS_ALL
#define CV_REGISTER(name, value) name = value,
#include "CodeViewRegisters.def"
#undef CV_REGISTER
#undef CV_REGISTERS_ALL
};

// Register Ids are shared between architectures in CodeView. CPUType is needed
// to map register Id to name.
struct CPURegister {
  CPURegister() = delete;
  CPURegister(CPUType Cpu, codeview::RegisterId Reg) {
    this->Cpu = Cpu;
    this->Reg = Reg;
  }
  CPUType Cpu;
  RegisterId Reg;
};

/// Two-bit value indicating which register is the designated frame pointer
/// register. Appears in the S_FRAMEPROC record flags.
enum class EncodedFramePtrReg : uint8_t {
  None = 0,
  StackPtr = 1,
  FramePtr = 2,
  BasePtr = 3,
};

RegisterId decodeFramePtrReg(EncodedFramePtrReg EncodedReg, CPUType CPU);

EncodedFramePtrReg encodeFramePtrReg(RegisterId Reg, CPUType CPU);

/// These values correspond to the THUNK_ORDINAL enumeration.
enum class ThunkOrdinal : uint8_t {
  Standard,
  ThisAdjustor,
  Vcall,
  Pcode,
  UnknownLoad,
  TrampIncremental,
  BranchIsland
};

enum class TrampolineType : uint16_t { TrampIncremental, BranchIsland };

// These values correspond to the CV_SourceChksum_t enumeration.
enum class FileChecksumKind : uint8_t { None, MD5, SHA1, SHA256 };

enum LineFlags : uint16_t {
  LF_None = 0,
  LF_HaveColumns = 1, // CV_LINES_HAVE_COLUMNS
};

/// Data in the SUBSEC_FRAMEDATA subection.
struct FrameData {
  support::ulittle32_t RvaStart;
  support::ulittle32_t CodeSize;
  support::ulittle32_t LocalSize;
  support::ulittle32_t ParamsSize;
  support::ulittle32_t MaxStackSize;
  support::ulittle32_t FrameFunc;
  support::ulittle16_t PrologSize;
  support::ulittle16_t SavedRegsSize;
  support::ulittle32_t Flags;
  enum : uint32_t {
    HasSEH = 1 << 0,
    HasEH = 1 << 1,
    IsFunctionStart = 1 << 2,
  };
};

// Corresponds to LocalIdAndGlobalIdPair structure.
// This structure information allows cross-referencing between PDBs.  For
// example, when a PDB is being built during compilation it is not yet known
// what other modules may end up in the PDB at link time.  So certain types of
// IDs may clash between the various compile time PDBs.  For each affected
// module, a subsection would be put into the PDB containing a mapping from its
// local IDs to a single ID namespace for all items in the PDB file.
struct CrossModuleExport {
  support::ulittle32_t Local;
  support::ulittle32_t Global;
};

struct CrossModuleImport {
  support::ulittle32_t ModuleNameOffset;
  support::ulittle32_t Count; // Number of elements
  // support::ulittle32_t ids[Count]; // id from referenced module
};

enum class CodeViewContainer { ObjectFile, Pdb };

inline uint32_t alignOf(CodeViewContainer Container) {
  if (Container == CodeViewContainer::ObjectFile)
    return 1;
  return 4;
}

// Corresponds to CV_armswitchtype enum.
// This enum represents the different ways that jump tables entries can be
// encoded to represent the target address to jump to.
// * Pointer: The absolute address to jump to.
// * [U]Int[8|16|32]: A value that is added to some "base" address to get the
//    address to jump to.
// * [U]Int[8|16]ShiftLeft: A value that is shifted left by an implementation
//    specified amount, then added to some "base" address to get the address to
//    jump to.
enum class JumpTableEntrySize : uint16_t {
  Int8 = 0,
  UInt8 = 1,
  Int16 = 2,
  UInt16 = 3,
  Int32 = 4,
  UInt32 = 5,
  Pointer = 6,
  UInt8ShiftLeft = 7,
  UInt16ShiftLeft = 8,
  Int8ShiftLeft = 9,
  Int16ShiftLeft = 10,
};
}
}

#endif
