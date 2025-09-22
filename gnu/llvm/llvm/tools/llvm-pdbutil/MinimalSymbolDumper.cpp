//===- MinimalSymbolDumper.cpp -------------------------------- *- C++ --*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MinimalSymbolDumper.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/Formatters.h"
#include "llvm/DebugInfo/CodeView/LazyRandomTypeCollection.h"
#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/PDB/Native/FormatUtil.h"
#include "llvm/DebugInfo/PDB/Native/InputFile.h"
#include "llvm/DebugInfo/PDB/Native/LinePrinter.h"
#include "llvm/DebugInfo/PDB/Native/NativeSession.h"
#include "llvm/DebugInfo/PDB/Native/PDBFile.h"
#include "llvm/DebugInfo/PDB/Native/PDBStringTable.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/FormatVariadic.h"

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::pdb;

static std::string formatLocalSymFlags(uint32_t IndentLevel,
                                       LocalSymFlags Flags) {
  std::vector<std::string> Opts;
  if (Flags == LocalSymFlags::None)
    return "none";

  PUSH_FLAG(LocalSymFlags, IsParameter, Flags, "param");
  PUSH_FLAG(LocalSymFlags, IsAddressTaken, Flags, "address is taken");
  PUSH_FLAG(LocalSymFlags, IsCompilerGenerated, Flags, "compiler generated");
  PUSH_FLAG(LocalSymFlags, IsAggregate, Flags, "aggregate");
  PUSH_FLAG(LocalSymFlags, IsAggregated, Flags, "aggregated");
  PUSH_FLAG(LocalSymFlags, IsAliased, Flags, "aliased");
  PUSH_FLAG(LocalSymFlags, IsAlias, Flags, "alias");
  PUSH_FLAG(LocalSymFlags, IsReturnValue, Flags, "return val");
  PUSH_FLAG(LocalSymFlags, IsOptimizedOut, Flags, "optimized away");
  PUSH_FLAG(LocalSymFlags, IsEnregisteredGlobal, Flags, "enreg global");
  PUSH_FLAG(LocalSymFlags, IsEnregisteredStatic, Flags, "enreg static");
  return typesetItemList(Opts, 4, IndentLevel, " | ");
}

static std::string formatExportFlags(uint32_t IndentLevel, ExportFlags Flags) {
  std::vector<std::string> Opts;
  if (Flags == ExportFlags::None)
    return "none";

  PUSH_FLAG(ExportFlags, IsConstant, Flags, "constant");
  PUSH_FLAG(ExportFlags, IsData, Flags, "data");
  PUSH_FLAG(ExportFlags, IsPrivate, Flags, "private");
  PUSH_FLAG(ExportFlags, HasNoName, Flags, "no name");
  PUSH_FLAG(ExportFlags, HasExplicitOrdinal, Flags, "explicit ord");
  PUSH_FLAG(ExportFlags, IsForwarder, Flags, "forwarder");

  return typesetItemList(Opts, 4, IndentLevel, " | ");
}

static std::string formatCompileSym2Flags(uint32_t IndentLevel,
                                          CompileSym2Flags Flags) {
  std::vector<std::string> Opts;
  Flags &= ~CompileSym2Flags::SourceLanguageMask;
  if (Flags == CompileSym2Flags::None)
    return "none";

  PUSH_FLAG(CompileSym2Flags, EC, Flags, "edit and continue");
  PUSH_FLAG(CompileSym2Flags, NoDbgInfo, Flags, "no dbg info");
  PUSH_FLAG(CompileSym2Flags, LTCG, Flags, "ltcg");
  PUSH_FLAG(CompileSym2Flags, NoDataAlign, Flags, "no data align");
  PUSH_FLAG(CompileSym2Flags, ManagedPresent, Flags, "has managed code");
  PUSH_FLAG(CompileSym2Flags, SecurityChecks, Flags, "security checks");
  PUSH_FLAG(CompileSym2Flags, HotPatch, Flags, "hot patchable");
  PUSH_FLAG(CompileSym2Flags, CVTCIL, Flags, "cvtcil");
  PUSH_FLAG(CompileSym2Flags, MSILModule, Flags, "msil module");
  return typesetItemList(Opts, 4, IndentLevel, " | ");
}

static std::string formatCompileSym3Flags(uint32_t IndentLevel,
                                          CompileSym3Flags Flags) {
  std::vector<std::string> Opts;
  Flags &= ~CompileSym3Flags::SourceLanguageMask;

  if (Flags == CompileSym3Flags::None)
    return "none";

  PUSH_FLAG(CompileSym3Flags, EC, Flags, "edit and continue");
  PUSH_FLAG(CompileSym3Flags, NoDbgInfo, Flags, "no dbg info");
  PUSH_FLAG(CompileSym3Flags, LTCG, Flags, "ltcg");
  PUSH_FLAG(CompileSym3Flags, NoDataAlign, Flags, "no data align");
  PUSH_FLAG(CompileSym3Flags, ManagedPresent, Flags, "has managed code");
  PUSH_FLAG(CompileSym3Flags, SecurityChecks, Flags, "security checks");
  PUSH_FLAG(CompileSym3Flags, HotPatch, Flags, "hot patchable");
  PUSH_FLAG(CompileSym3Flags, CVTCIL, Flags, "cvtcil");
  PUSH_FLAG(CompileSym3Flags, MSILModule, Flags, "msil module");
  PUSH_FLAG(CompileSym3Flags, Sdl, Flags, "sdl");
  PUSH_FLAG(CompileSym3Flags, PGO, Flags, "pgo");
  PUSH_FLAG(CompileSym3Flags, Exp, Flags, "exp");
  return typesetItemList(Opts, 4, IndentLevel, " | ");
}

static std::string formatFrameProcedureOptions(uint32_t IndentLevel,
                                               FrameProcedureOptions FPO) {
  std::vector<std::string> Opts;
  if (FPO == FrameProcedureOptions::None)
    return "none";

  PUSH_FLAG(FrameProcedureOptions, HasAlloca, FPO, "has alloca");
  PUSH_FLAG(FrameProcedureOptions, HasSetJmp, FPO, "has setjmp");
  PUSH_FLAG(FrameProcedureOptions, HasLongJmp, FPO, "has longjmp");
  PUSH_FLAG(FrameProcedureOptions, HasInlineAssembly, FPO, "has inline asm");
  PUSH_FLAG(FrameProcedureOptions, HasExceptionHandling, FPO, "has eh");
  PUSH_FLAG(FrameProcedureOptions, MarkedInline, FPO, "marked inline");
  PUSH_FLAG(FrameProcedureOptions, HasStructuredExceptionHandling, FPO,
            "has seh");
  PUSH_FLAG(FrameProcedureOptions, Naked, FPO, "naked");
  PUSH_FLAG(FrameProcedureOptions, SecurityChecks, FPO, "secure checks");
  PUSH_FLAG(FrameProcedureOptions, AsynchronousExceptionHandling, FPO,
            "has async eh");
  PUSH_FLAG(FrameProcedureOptions, NoStackOrderingForSecurityChecks, FPO,
            "no stack order");
  PUSH_FLAG(FrameProcedureOptions, Inlined, FPO, "inlined");
  PUSH_FLAG(FrameProcedureOptions, StrictSecurityChecks, FPO,
            "strict secure checks");
  PUSH_FLAG(FrameProcedureOptions, SafeBuffers, FPO, "safe buffers");
  PUSH_FLAG(FrameProcedureOptions, ProfileGuidedOptimization, FPO, "pgo");
  PUSH_FLAG(FrameProcedureOptions, ValidProfileCounts, FPO,
            "has profile counts");
  PUSH_FLAG(FrameProcedureOptions, OptimizedForSpeed, FPO, "opt speed");
  PUSH_FLAG(FrameProcedureOptions, GuardCfg, FPO, "guard cfg");
  PUSH_FLAG(FrameProcedureOptions, GuardCfw, FPO, "guard cfw");
  return typesetItemList(Opts, 4, IndentLevel, " | ");
}

static std::string formatPublicSymFlags(uint32_t IndentLevel,
                                        PublicSymFlags Flags) {
  std::vector<std::string> Opts;
  if (Flags == PublicSymFlags::None)
    return "none";

  PUSH_FLAG(PublicSymFlags, Code, Flags, "code");
  PUSH_FLAG(PublicSymFlags, Function, Flags, "function");
  PUSH_FLAG(PublicSymFlags, Managed, Flags, "managed");
  PUSH_FLAG(PublicSymFlags, MSIL, Flags, "msil");
  return typesetItemList(Opts, 4, IndentLevel, " | ");
}

static std::string formatProcSymFlags(uint32_t IndentLevel,
                                      ProcSymFlags Flags) {
  std::vector<std::string> Opts;
  if (Flags == ProcSymFlags::None)
    return "none";

  PUSH_FLAG(ProcSymFlags, HasFP, Flags, "has fp");
  PUSH_FLAG(ProcSymFlags, HasIRET, Flags, "has iret");
  PUSH_FLAG(ProcSymFlags, HasFRET, Flags, "has fret");
  PUSH_FLAG(ProcSymFlags, IsNoReturn, Flags, "noreturn");
  PUSH_FLAG(ProcSymFlags, IsUnreachable, Flags, "unreachable");
  PUSH_FLAG(ProcSymFlags, HasCustomCallingConv, Flags, "custom calling conv");
  PUSH_FLAG(ProcSymFlags, IsNoInline, Flags, "noinline");
  PUSH_FLAG(ProcSymFlags, HasOptimizedDebugInfo, Flags, "opt debuginfo");
  return typesetItemList(Opts, 4, IndentLevel, " | ");
}

static std::string formatThunkOrdinal(ThunkOrdinal Ordinal) {
  switch (Ordinal) {
    RETURN_CASE(ThunkOrdinal, Standard, "thunk");
    RETURN_CASE(ThunkOrdinal, ThisAdjustor, "this adjustor");
    RETURN_CASE(ThunkOrdinal, Vcall, "vcall");
    RETURN_CASE(ThunkOrdinal, Pcode, "pcode");
    RETURN_CASE(ThunkOrdinal, UnknownLoad, "unknown load");
    RETURN_CASE(ThunkOrdinal, TrampIncremental, "tramp incremental");
    RETURN_CASE(ThunkOrdinal, BranchIsland, "branch island");
  }
  return formatUnknownEnum(Ordinal);
}

static std::string formatTrampolineType(TrampolineType Tramp) {
  switch (Tramp) {
    RETURN_CASE(TrampolineType, TrampIncremental, "tramp incremental");
    RETURN_CASE(TrampolineType, BranchIsland, "branch island");
  }
  return formatUnknownEnum(Tramp);
}

static std::string formatSourceLanguage(SourceLanguage Lang) {
  switch (Lang) {
    RETURN_CASE(SourceLanguage, C, "c");
    RETURN_CASE(SourceLanguage, Cpp, "c++");
    RETURN_CASE(SourceLanguage, Fortran, "fortran");
    RETURN_CASE(SourceLanguage, Masm, "masm");
    RETURN_CASE(SourceLanguage, Pascal, "pascal");
    RETURN_CASE(SourceLanguage, Basic, "basic");
    RETURN_CASE(SourceLanguage, Cobol, "cobol");
    RETURN_CASE(SourceLanguage, Link, "link");
    RETURN_CASE(SourceLanguage, VB, "vb");
    RETURN_CASE(SourceLanguage, Cvtres, "cvtres");
    RETURN_CASE(SourceLanguage, Cvtpgd, "cvtpgd");
    RETURN_CASE(SourceLanguage, CSharp, "c#");
    RETURN_CASE(SourceLanguage, ILAsm, "il asm");
    RETURN_CASE(SourceLanguage, Java, "java");
    RETURN_CASE(SourceLanguage, JScript, "javascript");
    RETURN_CASE(SourceLanguage, MSIL, "msil");
    RETURN_CASE(SourceLanguage, HLSL, "hlsl");
    RETURN_CASE(SourceLanguage, D, "d");
    RETURN_CASE(SourceLanguage, Swift, "swift");
    RETURN_CASE(SourceLanguage, Rust, "rust");
    RETURN_CASE(SourceLanguage, ObjC, "objc");
    RETURN_CASE(SourceLanguage, ObjCpp, "objc++");
    RETURN_CASE(SourceLanguage, AliasObj, "aliasobj");
    RETURN_CASE(SourceLanguage, Go, "go");
    RETURN_CASE(SourceLanguage, OldSwift, "swift");
  }
  return formatUnknownEnum(Lang);
}

static std::string formatMachineType(CPUType Cpu) {
  switch (Cpu) {
    RETURN_CASE(CPUType, Intel8080, "intel 8080");
    RETURN_CASE(CPUType, Intel8086, "intel 8086");
    RETURN_CASE(CPUType, Intel80286, "intel 80286");
    RETURN_CASE(CPUType, Intel80386, "intel 80386");
    RETURN_CASE(CPUType, Intel80486, "intel 80486");
    RETURN_CASE(CPUType, Pentium, "intel pentium");
    RETURN_CASE(CPUType, PentiumPro, "intel pentium pro");
    RETURN_CASE(CPUType, Pentium3, "intel pentium 3");
    RETURN_CASE(CPUType, MIPS, "mips");
    RETURN_CASE(CPUType, MIPS16, "mips-16");
    RETURN_CASE(CPUType, MIPS32, "mips-32");
    RETURN_CASE(CPUType, MIPS64, "mips-64");
    RETURN_CASE(CPUType, MIPSI, "mips i");
    RETURN_CASE(CPUType, MIPSII, "mips ii");
    RETURN_CASE(CPUType, MIPSIII, "mips iii");
    RETURN_CASE(CPUType, MIPSIV, "mips iv");
    RETURN_CASE(CPUType, MIPSV, "mips v");
    RETURN_CASE(CPUType, M68000, "motorola 68000");
    RETURN_CASE(CPUType, M68010, "motorola 68010");
    RETURN_CASE(CPUType, M68020, "motorola 68020");
    RETURN_CASE(CPUType, M68030, "motorola 68030");
    RETURN_CASE(CPUType, M68040, "motorola 68040");
    RETURN_CASE(CPUType, Alpha, "alpha");
    RETURN_CASE(CPUType, Alpha21164, "alpha 21164");
    RETURN_CASE(CPUType, Alpha21164A, "alpha 21164a");
    RETURN_CASE(CPUType, Alpha21264, "alpha 21264");
    RETURN_CASE(CPUType, Alpha21364, "alpha 21364");
    RETURN_CASE(CPUType, PPC601, "powerpc 601");
    RETURN_CASE(CPUType, PPC603, "powerpc 603");
    RETURN_CASE(CPUType, PPC604, "powerpc 604");
    RETURN_CASE(CPUType, PPC620, "powerpc 620");
    RETURN_CASE(CPUType, PPCFP, "powerpc fp");
    RETURN_CASE(CPUType, PPCBE, "powerpc be");
    RETURN_CASE(CPUType, SH3, "sh3");
    RETURN_CASE(CPUType, SH3E, "sh3e");
    RETURN_CASE(CPUType, SH3DSP, "sh3 dsp");
    RETURN_CASE(CPUType, SH4, "sh4");
    RETURN_CASE(CPUType, SHMedia, "shmedia");
    RETURN_CASE(CPUType, ARM3, "arm 3");
    RETURN_CASE(CPUType, ARM4, "arm 4");
    RETURN_CASE(CPUType, ARM4T, "arm 4t");
    RETURN_CASE(CPUType, ARM5, "arm 5");
    RETURN_CASE(CPUType, ARM5T, "arm 5t");
    RETURN_CASE(CPUType, ARM6, "arm 6");
    RETURN_CASE(CPUType, ARM_XMAC, "arm xmac");
    RETURN_CASE(CPUType, ARM_WMMX, "arm wmmx");
    RETURN_CASE(CPUType, ARM7, "arm 7");
    RETURN_CASE(CPUType, ARM64, "arm64");
    RETURN_CASE(CPUType, ARM64EC, "arm64ec");
    RETURN_CASE(CPUType, ARM64X, "arm64x");
    RETURN_CASE(CPUType, HybridX86ARM64, "hybrid x86 arm64");
    RETURN_CASE(CPUType, Omni, "omni");
    RETURN_CASE(CPUType, Ia64, "intel itanium ia64");
    RETURN_CASE(CPUType, Ia64_2, "intel itanium ia64 2");
    RETURN_CASE(CPUType, CEE, "cee");
    RETURN_CASE(CPUType, AM33, "am33");
    RETURN_CASE(CPUType, M32R, "m32r");
    RETURN_CASE(CPUType, TriCore, "tri-core");
    RETURN_CASE(CPUType, X64, "intel x86-x64");
    RETURN_CASE(CPUType, EBC, "ebc");
    RETURN_CASE(CPUType, Thumb, "thumb");
    RETURN_CASE(CPUType, ARMNT, "arm nt");
    RETURN_CASE(CPUType, D3D11_Shader, "d3d11 shader");
    RETURN_CASE(CPUType, Unknown, "unknown");
  }
  return formatUnknownEnum(Cpu);
}

static std::string formatCookieKind(FrameCookieKind Kind) {
  switch (Kind) {
    RETURN_CASE(FrameCookieKind, Copy, "copy");
    RETURN_CASE(FrameCookieKind, XorStackPointer, "xor stack ptr");
    RETURN_CASE(FrameCookieKind, XorFramePointer, "xor frame ptr");
    RETURN_CASE(FrameCookieKind, XorR13, "xor rot13");
  }
  return formatUnknownEnum(Kind);
}

static std::string formatRegisterId(RegisterId Id, CPUType Cpu) {
  if (Cpu == CPUType::ARMNT) {
    switch (Id) {
#define CV_REGISTERS_ARM
#define CV_REGISTER(name, val) RETURN_CASE(RegisterId, name, #name)
#include "llvm/DebugInfo/CodeView/CodeViewRegisters.def"
#undef CV_REGISTER
#undef CV_REGISTERS_ARM

    default:
      break;
    }
  } else if (Cpu == CPUType::ARM64) {
    switch (Id) {
#define CV_REGISTERS_ARM64
#define CV_REGISTER(name, val) RETURN_CASE(RegisterId, name, #name)
#include "llvm/DebugInfo/CodeView/CodeViewRegisters.def"
#undef CV_REGISTER
#undef CV_REGISTERS_ARM64

    default:
      break;
    }
  } else {
    switch (Id) {
#define CV_REGISTERS_X86
#define CV_REGISTER(name, val) RETURN_CASE(RegisterId, name, #name)
#include "llvm/DebugInfo/CodeView/CodeViewRegisters.def"
#undef CV_REGISTER
#undef CV_REGISTERS_X86

    default:
      break;
    }
  }
  return formatUnknownEnum(Id);
}

static std::string formatRegisterId(uint16_t Reg16, CPUType Cpu) {
  return formatRegisterId(RegisterId(Reg16), Cpu);
}

static std::string formatRegisterId(ulittle16_t &Reg16, CPUType Cpu) {
  return formatRegisterId(uint16_t(Reg16), Cpu);
}

static std::string formatRange(LocalVariableAddrRange Range) {
  return formatv("[{0},+{1})",
                 formatSegmentOffset(Range.ISectStart, Range.OffsetStart),
                 Range.Range)
      .str();
}

static std::string formatGaps(uint32_t IndentLevel,
                              ArrayRef<LocalVariableAddrGap> Gaps) {
  std::vector<std::string> GapStrs;
  for (const auto &G : Gaps) {
    GapStrs.push_back(formatv("({0},{1})", G.GapStartOffset, G.Range).str());
  }
  return typesetItemList(GapStrs, 7, IndentLevel, ", ");
}

static std::string formatJumpTableEntrySize(JumpTableEntrySize EntrySize) {
  switch (EntrySize) {
    RETURN_CASE(JumpTableEntrySize, Int8, "int8");
    RETURN_CASE(JumpTableEntrySize, UInt8, "uin8");
    RETURN_CASE(JumpTableEntrySize, Int16, "int16");
    RETURN_CASE(JumpTableEntrySize, UInt16, "uint16");
    RETURN_CASE(JumpTableEntrySize, Int32, "int32");
    RETURN_CASE(JumpTableEntrySize, UInt32, "uint32");
    RETURN_CASE(JumpTableEntrySize, Pointer, "pointer");
    RETURN_CASE(JumpTableEntrySize, UInt8ShiftLeft, "uint8shl");
    RETURN_CASE(JumpTableEntrySize, UInt16ShiftLeft, "uint16shl");
    RETURN_CASE(JumpTableEntrySize, Int8ShiftLeft, "int8shl");
    RETURN_CASE(JumpTableEntrySize, Int16ShiftLeft, "int16shl");
  }
  return formatUnknownEnum(EntrySize);
}

Error MinimalSymbolDumper::visitSymbolBegin(codeview::CVSymbol &Record) {
  return visitSymbolBegin(Record, 0);
}

Error MinimalSymbolDumper::visitSymbolBegin(codeview::CVSymbol &Record,
                                            uint32_t Offset) {
  // formatLine puts the newline at the beginning, so we use formatLine here
  // to start a new line, and then individual visit methods use format to
  // append to the existing line.
  P.formatLine("{0} | {1} [size = {2}]",
               fmt_align(Offset, AlignStyle::Right, 6),
               formatSymbolKind(Record.kind()), Record.length());
  P.Indent();
  return Error::success();
}

Error MinimalSymbolDumper::visitSymbolEnd(CVSymbol &Record) {
  if (RecordBytes) {
    AutoIndent Indent(P, 7);
    P.formatBinary("bytes", Record.content(), 0);
  }
  P.Unindent();
  return Error::success();
}

std::string MinimalSymbolDumper::typeOrIdIndex(codeview::TypeIndex TI,
                                               bool IsType) const {
  if (TI.isSimple() || TI.isDecoratedItemId())
    return formatv("{0}", TI).str();
  auto &Container = IsType ? Types : Ids;
  StringRef Name = Container.getTypeName(TI);
  if (Name.size() > 32) {
    Name = Name.take_front(32);
    return std::string(formatv("{0} ({1}...)", TI, Name));
  } else
    return std::string(formatv("{0} ({1})", TI, Name));
}

std::string MinimalSymbolDumper::idIndex(codeview::TypeIndex TI) const {
  return typeOrIdIndex(TI, false);
}

std::string MinimalSymbolDumper::typeIndex(TypeIndex TI) const {
  return typeOrIdIndex(TI, true);
}

Error MinimalSymbolDumper::visitKnownRecord(CVSymbol &CVR, BlockSym &Block) {
  P.format(" `{0}`", Block.Name);
  AutoIndent Indent(P, 7);
  P.formatLine("parent = {0}, end = {1}", Block.Parent, Block.End);
  P.formatLine("code size = {0}, addr = {1}", Block.CodeSize,
               formatSegmentOffset(Block.Segment, Block.CodeOffset));
  return Error::success();
}

Error MinimalSymbolDumper::visitKnownRecord(CVSymbol &CVR, Thunk32Sym &Thunk) {
  P.format(" `{0}`", Thunk.Name);
  AutoIndent Indent(P, 7);
  P.formatLine("parent = {0}, end = {1}, next = {2}", Thunk.Parent, Thunk.End,
               Thunk.Next);
  P.formatLine("kind = {0}, size = {1}, addr = {2}",
               formatThunkOrdinal(Thunk.Thunk), Thunk.Length,
               formatSegmentOffset(Thunk.Segment, Thunk.Offset));

  return Error::success();
}

Error MinimalSymbolDumper::visitKnownRecord(CVSymbol &CVR,
                                            TrampolineSym &Tramp) {
  AutoIndent Indent(P, 7);
  P.formatLine("type = {0}, size = {1}, source = {2}, target = {3}",
               formatTrampolineType(Tramp.Type), Tramp.Size,
               formatSegmentOffset(Tramp.ThunkSection, Tramp.ThunkOffset),
               formatSegmentOffset(Tramp.TargetSection, Tramp.ThunkOffset));

  return Error::success();
}

Error MinimalSymbolDumper::visitKnownRecord(CVSymbol &CVR,
                                            SectionSym &Section) {
  P.format(" `{0}`", Section.Name);
  AutoIndent Indent(P, 7);
  P.formatLine("length = {0}, alignment = {1}, rva = {2}, section # = {3}",
               Section.Length, Section.Alignment, Section.Rva,
               Section.SectionNumber);
  P.printLine("characteristics =");
  AutoIndent Indent2(P, 2);
  P.printLine(formatSectionCharacteristics(P.getIndentLevel(),
                                           Section.Characteristics, 1, "",
                                           CharacteristicStyle::Descriptive));
  return Error::success();
}

Error MinimalSymbolDumper::visitKnownRecord(CVSymbol &CVR, CoffGroupSym &CG) {
  P.format(" `{0}`", CG.Name);
  AutoIndent Indent(P, 7);
  P.formatLine("length = {0}, addr = {1}", CG.Size,
               formatSegmentOffset(CG.Segment, CG.Offset));
  P.printLine("characteristics =");
  AutoIndent Indent2(P, 2);
  P.printLine(formatSectionCharacteristics(P.getIndentLevel(),
                                           CG.Characteristics, 1, "",
                                           CharacteristicStyle::Descriptive));
  return Error::success();
}

Error MinimalSymbolDumper::visitKnownRecord(CVSymbol &CVR,
                                            BPRelativeSym &BPRel) {
  P.format(" `{0}`", BPRel.Name);
  AutoIndent Indent(P, 7);
  P.formatLine("type = {0}, offset = {1}", typeIndex(BPRel.Type), BPRel.Offset);
  return Error::success();
}

Error MinimalSymbolDumper::visitKnownRecord(CVSymbol &CVR,
                                            BuildInfoSym &BuildInfo) {
  P.format(" BuildId = `{0}`", BuildInfo.BuildId);
  return Error::success();
}

Error MinimalSymbolDumper::visitKnownRecord(CVSymbol &CVR,
                                            CallSiteInfoSym &CSI) {
  AutoIndent Indent(P, 7);
  P.formatLine("type = {0}, addr = {1}", typeIndex(CSI.Type),
               formatSegmentOffset(CSI.Segment, CSI.CodeOffset));
  return Error::success();
}

Error MinimalSymbolDumper::visitKnownRecord(CVSymbol &CVR,
                                            EnvBlockSym &EnvBlock) {
  AutoIndent Indent(P, 7);
  for (const auto &Entry : EnvBlock.Fields) {
    P.formatLine("- {0}", Entry);
  }
  return Error::success();
}

Error MinimalSymbolDumper::visitKnownRecord(CVSymbol &CVR, FileStaticSym &FS) {
  P.format(" `{0}`", FS.Name);
  AutoIndent Indent(P, 7);
  if (SymGroup) {
    Expected<StringRef> FileName =
        SymGroup->getNameFromStringTable(FS.ModFilenameOffset);
    if (FileName) {
      P.formatLine("type = {0}, file name = {1} ({2}), flags = {3}",
                   typeIndex(FS.Index), FS.ModFilenameOffset, *FileName,
                   formatLocalSymFlags(P.getIndentLevel() + 9, FS.Flags));
    }
    return Error::success();
  }

  P.formatLine("type = {0}, file name offset = {1}, flags = {2}",
               typeIndex(FS.Index), FS.ModFilenameOffset,
               formatLocalSymFlags(P.getIndentLevel() + 9, FS.Flags));
  return Error::success();
}

Error MinimalSymbolDumper::visitKnownRecord(CVSymbol &CVR, ExportSym &Export) {
  P.format(" `{0}`", Export.Name);
  AutoIndent Indent(P, 7);
  P.formatLine("ordinal = {0}, flags = {1}", Export.Ordinal,
               formatExportFlags(P.getIndentLevel() + 9, Export.Flags));
  return Error::success();
}

Error MinimalSymbolDumper::visitKnownRecord(CVSymbol &CVR,
                                            Compile2Sym &Compile2) {
  AutoIndent Indent(P, 7);
  SourceLanguage Lang = static_cast<SourceLanguage>(
      Compile2.Flags & CompileSym2Flags::SourceLanguageMask);
  CompilationCPU = Compile2.Machine;
  P.formatLine("machine = {0}, ver = {1}, language = {2}",
               formatMachineType(Compile2.Machine), Compile2.Version,
               formatSourceLanguage(Lang));
  P.formatLine("frontend = {0}.{1}.{2}, backend = {3}.{4}.{5}",
               Compile2.VersionFrontendMajor, Compile2.VersionFrontendMinor,
               Compile2.VersionFrontendBuild, Compile2.VersionBackendMajor,
               Compile2.VersionBackendMinor, Compile2.VersionBackendBuild);
  P.formatLine("flags = {0}",
               formatCompileSym2Flags(P.getIndentLevel() + 9, Compile2.Flags));
  P.formatLine(
      "extra strings = {0}",
      typesetStringList(P.getIndentLevel() + 9 + 2, Compile2.ExtraStrings));
  return Error::success();
}

Error MinimalSymbolDumper::visitKnownRecord(CVSymbol &CVR,
                                            Compile3Sym &Compile3) {
  AutoIndent Indent(P, 7);
  SourceLanguage Lang = static_cast<SourceLanguage>(
      Compile3.Flags & CompileSym3Flags::SourceLanguageMask);
  CompilationCPU = Compile3.Machine;
  P.formatLine("machine = {0}, Ver = {1}, language = {2}",
               formatMachineType(Compile3.Machine), Compile3.Version,
               formatSourceLanguage(Lang));
  P.formatLine("frontend = {0}.{1}.{2}.{3}, backend = {4}.{5}.{6}.{7}",
               Compile3.VersionFrontendMajor, Compile3.VersionFrontendMinor,
               Compile3.VersionFrontendBuild, Compile3.VersionFrontendQFE,
               Compile3.VersionBackendMajor, Compile3.VersionBackendMinor,
               Compile3.VersionBackendBuild, Compile3.VersionBackendQFE);
  P.formatLine("flags = {0}",
               formatCompileSym3Flags(P.getIndentLevel() + 9, Compile3.Flags));
  return Error::success();
}

Error MinimalSymbolDumper::visitKnownRecord(CVSymbol &CVR,
                                            ConstantSym &Constant) {
  P.format(" `{0}`", Constant.Name);
  AutoIndent Indent(P, 7);
  P.formatLine("type = {0}, value = {1}", typeIndex(Constant.Type),
               toString(Constant.Value, 10));
  return Error::success();
}

Error MinimalSymbolDumper::visitKnownRecord(CVSymbol &CVR, DataSym &Data) {
  P.format(" `{0}`", Data.Name);
  AutoIndent Indent(P, 7);
  P.formatLine("type = {0}, addr = {1}", typeIndex(Data.Type),
               formatSegmentOffset(Data.Segment, Data.DataOffset));
  return Error::success();
}

Error MinimalSymbolDumper::visitKnownRecord(
    CVSymbol &CVR, DefRangeFramePointerRelFullScopeSym &Def) {
  P.format(" offset = {0}", Def.Offset);
  return Error::success();
}

Error MinimalSymbolDumper::visitKnownRecord(CVSymbol &CVR,
                                            DefRangeFramePointerRelSym &Def) {
  AutoIndent Indent(P, 7);
  P.formatLine("offset = {0}, range = {1}", Def.Hdr.Offset,
               formatRange(Def.Range));
  P.formatLine("gaps = [{0}]", formatGaps(P.getIndentLevel() + 9, Def.Gaps));
  return Error::success();
}

Error MinimalSymbolDumper::visitKnownRecord(CVSymbol &CVR,
                                            DefRangeRegisterRelSym &Def) {
  AutoIndent Indent(P, 7);
  P.formatLine("register = {0}, offset = {1}, offset in parent = {2}, has "
               "spilled udt = {3}",
               formatRegisterId(Def.Hdr.Register, CompilationCPU),
               int32_t(Def.Hdr.BasePointerOffset), Def.offsetInParent(),
               Def.hasSpilledUDTMember());
  P.formatLine("range = {0}, gaps = [{1}]", formatRange(Def.Range),
               formatGaps(P.getIndentLevel() + 9, Def.Gaps));
  return Error::success();
}

Error MinimalSymbolDumper::visitKnownRecord(
    CVSymbol &CVR, DefRangeRegisterSym &DefRangeRegister) {
  AutoIndent Indent(P, 7);
  P.formatLine("register = {0}, may have no name = {1}, range start = "
               "{2}, length = {3}",
               formatRegisterId(DefRangeRegister.Hdr.Register, CompilationCPU),
               bool(DefRangeRegister.Hdr.MayHaveNoName),
               formatSegmentOffset(DefRangeRegister.Range.ISectStart,
                                   DefRangeRegister.Range.OffsetStart),
               DefRangeRegister.Range.Range);
  P.formatLine("gaps = [{0}]",
               formatGaps(P.getIndentLevel() + 9, DefRangeRegister.Gaps));
  return Error::success();
}

Error MinimalSymbolDumper::visitKnownRecord(CVSymbol &CVR,
                                            DefRangeSubfieldRegisterSym &Def) {
  AutoIndent Indent(P, 7);
  bool NoName = !!(Def.Hdr.MayHaveNoName == 0);
  P.formatLine("register = {0}, may have no name = {1}, offset in parent = {2}",
               formatRegisterId(Def.Hdr.Register, CompilationCPU), NoName,
               uint32_t(Def.Hdr.OffsetInParent));
  P.formatLine("range = {0}, gaps = [{1}]", formatRange(Def.Range),
               formatGaps(P.getIndentLevel() + 9, Def.Gaps));
  return Error::success();
}

Error MinimalSymbolDumper::visitKnownRecord(CVSymbol &CVR,
                                            DefRangeSubfieldSym &Def) {
  AutoIndent Indent(P, 7);
  P.formatLine("program = {0}, offset in parent = {1}, range = {2}",
               Def.Program, Def.OffsetInParent, formatRange(Def.Range));
  P.formatLine("gaps = [{0}]", formatGaps(P.getIndentLevel() + 9, Def.Gaps));
  return Error::success();
}

Error MinimalSymbolDumper::visitKnownRecord(CVSymbol &CVR, DefRangeSym &Def) {
  AutoIndent Indent(P, 7);
  P.formatLine("program = {0}, range = {1}", Def.Program,
               formatRange(Def.Range));
  P.formatLine("gaps = [{0}]", formatGaps(P.getIndentLevel() + 9, Def.Gaps));
  return Error::success();
}

Error MinimalSymbolDumper::visitKnownRecord(CVSymbol &CVR, FrameCookieSym &FC) {
  AutoIndent Indent(P, 7);
  P.formatLine("code offset = {0}, Register = {1}, kind = {2}, flags = {3}",
               FC.CodeOffset, formatRegisterId(FC.Register, CompilationCPU),
               formatCookieKind(FC.CookieKind), FC.Flags);
  return Error::success();
}

Error MinimalSymbolDumper::visitKnownRecord(CVSymbol &CVR, FrameProcSym &FP) {
  AutoIndent Indent(P, 7);
  P.formatLine("size = {0}, padding size = {1}, offset to padding = {2}",
               FP.TotalFrameBytes, FP.PaddingFrameBytes, FP.OffsetToPadding);
  P.formatLine("bytes of callee saved registers = {0}, exception handler addr "
               "= {1}",
               FP.BytesOfCalleeSavedRegisters,
               formatSegmentOffset(FP.SectionIdOfExceptionHandler,
                                   FP.OffsetOfExceptionHandler));
  P.formatLine(
      "local fp reg = {0}, param fp reg = {1}",
      formatRegisterId(FP.getLocalFramePtrReg(CompilationCPU), CompilationCPU),
      formatRegisterId(FP.getParamFramePtrReg(CompilationCPU), CompilationCPU));
  P.formatLine("flags = {0}",
               formatFrameProcedureOptions(P.getIndentLevel() + 9, FP.Flags));
  return Error::success();
}

Error MinimalSymbolDumper::visitKnownRecord(CVSymbol &CVR,
                                            HeapAllocationSiteSym &HAS) {
  AutoIndent Indent(P, 7);
  P.formatLine("type = {0}, addr = {1} call size = {2}", typeIndex(HAS.Type),
               formatSegmentOffset(HAS.Segment, HAS.CodeOffset),
               HAS.CallInstructionSize);
  return Error::success();
}

Error MinimalSymbolDumper::visitKnownRecord(CVSymbol &CVR, InlineSiteSym &IS) {
  AutoIndent Indent(P, 7);
  P.formatLine("inlinee = {0}, parent = {1}, end = {2}", idIndex(IS.Inlinee),
               IS.Parent, IS.End);

  // Break down the annotation byte code and calculate code and line offsets.
  // FIXME: It would be helpful if we could look up the initial file and inlinee
  // lines offset using the inlinee index above.
  uint32_t CodeOffset = 0;
  int32_t LineOffset = 0;
  for (auto &Annot : IS.annotations()) {
    P.formatLine("  {0}", fmt_align(toHex(Annot.Bytes), AlignStyle::Left, 9));

    auto formatCodeOffset = [&](uint32_t Delta) {
      CodeOffset += Delta;
      P.format(" code 0x{0} (+0x{1})", utohexstr(CodeOffset), utohexstr(Delta));
    };
    auto formatCodeLength = [&](uint32_t Length) {
      // Notably, changing the code length does not affect the code offset.
      P.format(" code end 0x{0} (+0x{1})", utohexstr(CodeOffset + Length),
               utohexstr(Length));
    };
    auto formatLineOffset = [&](int32_t Delta) {
      LineOffset += Delta;
      char Sign = Delta > 0 ? '+' : '-';
      P.format(" line {0} ({1}{2})", LineOffset, Sign, std::abs(Delta));
    };

    // Use the opcode to interpret the integer values.
    switch (Annot.OpCode) {
    case BinaryAnnotationsOpCode::Invalid:
      break;
    case BinaryAnnotationsOpCode::CodeOffset:
    case BinaryAnnotationsOpCode::ChangeCodeOffset:
      formatCodeOffset(Annot.U1);
      break;
    case BinaryAnnotationsOpCode::ChangeLineOffset:
      formatLineOffset(Annot.S1);
      break;
    case BinaryAnnotationsOpCode::ChangeCodeLength:
      formatCodeLength(Annot.U1);
      // Apparently this annotation updates the code offset. It's hard to make
      // MSVC produce this opcode, but clang uses it, and debuggers seem to use
      // this interpretation.
      CodeOffset += Annot.U1;
      break;
    case BinaryAnnotationsOpCode::ChangeCodeOffsetAndLineOffset:
      formatCodeOffset(Annot.U1);
      formatLineOffset(Annot.S1);
      break;
    case BinaryAnnotationsOpCode::ChangeCodeLengthAndCodeOffset:
      formatCodeOffset(Annot.U2);
      formatCodeLength(Annot.U1);
      break;

    case BinaryAnnotationsOpCode::ChangeFile: {
      uint32_t FileOffset = Annot.U1;
      StringRef Filename = "<unknown>";
      if (SymGroup) {
        if (Expected<StringRef> MaybeFile =
                SymGroup->getNameFromStringTable(FileOffset))
          Filename = *MaybeFile;
        else
          return MaybeFile.takeError();
      }
      P.format(" setfile {0} 0x{1}", utohexstr(FileOffset));
      break;
    }

    // The rest of these are hard to convince MSVC to emit, so they are not as
    // well understood.
    case BinaryAnnotationsOpCode::ChangeCodeOffsetBase:
      formatCodeOffset(Annot.U1);
      break;
    case BinaryAnnotationsOpCode::ChangeLineEndDelta:
    case BinaryAnnotationsOpCode::ChangeRangeKind:
    case BinaryAnnotationsOpCode::ChangeColumnStart:
    case BinaryAnnotationsOpCode::ChangeColumnEnd:
      P.format(" {0} {1}", Annot.Name, Annot.U1);
      break;
    case BinaryAnnotationsOpCode::ChangeColumnEndDelta:
      P.format(" {0} {1}", Annot.Name, Annot.S1);
      break;
    }
  }
  return Error::success();
}

Error MinimalSymbolDumper::visitKnownRecord(CVSymbol &CVR,
                                            RegisterSym &Register) {
  P.format(" `{0}`", Register.Name);
  AutoIndent Indent(P, 7);
  P.formatLine("register = {0}, type = {1}",
               formatRegisterId(Register.Register, CompilationCPU),
               typeIndex(Register.Index));
  return Error::success();
}

Error MinimalSymbolDumper::visitKnownRecord(CVSymbol &CVR,
                                            PublicSym32 &Public) {
  P.format(" `{0}`", Public.Name);
  AutoIndent Indent(P, 7);
  P.formatLine("flags = {0}, addr = {1}",
               formatPublicSymFlags(P.getIndentLevel() + 9, Public.Flags),
               formatSegmentOffset(Public.Segment, Public.Offset));
  return Error::success();
}

Error MinimalSymbolDumper::visitKnownRecord(CVSymbol &CVR, ProcRefSym &PR) {
  P.format(" `{0}`", PR.Name);
  AutoIndent Indent(P, 7);
  P.formatLine("module = {0}, sum name = {1}, offset = {2}", PR.Module,
               PR.SumName, PR.SymOffset);
  return Error::success();
}

Error MinimalSymbolDumper::visitKnownRecord(CVSymbol &CVR, LabelSym &Label) {
  P.format(" `{0}` (addr = {1})", Label.Name,
           formatSegmentOffset(Label.Segment, Label.CodeOffset));
  AutoIndent Indent(P, 7);
  P.formatLine("flags = {0}",
               formatProcSymFlags(P.getIndentLevel() + 9, Label.Flags));
  return Error::success();
}

Error MinimalSymbolDumper::visitKnownRecord(CVSymbol &CVR, LocalSym &Local) {
  P.format(" `{0}`", Local.Name);
  AutoIndent Indent(P, 7);

  std::string FlagStr =
      formatLocalSymFlags(P.getIndentLevel() + 9, Local.Flags);
  P.formatLine("type={0}, flags = {1}", typeIndex(Local.Type), FlagStr);
  return Error::success();
}

Error MinimalSymbolDumper::visitKnownRecord(CVSymbol &CVR,
                                            ObjNameSym &ObjName) {
  P.format(" sig={0}, `{1}`", ObjName.Signature, ObjName.Name);
  return Error::success();
}

Error MinimalSymbolDumper::visitKnownRecord(CVSymbol &CVR, ProcSym &Proc) {
  P.format(" `{0}`", Proc.Name);
  AutoIndent Indent(P, 7);
  P.formatLine("parent = {0}, end = {1}, addr = {2}, code size = {3}",
               Proc.Parent, Proc.End,
               formatSegmentOffset(Proc.Segment, Proc.CodeOffset),
               Proc.CodeSize);
  bool IsType = true;
  switch (Proc.getKind()) {
  case SymbolRecordKind::GlobalProcIdSym:
  case SymbolRecordKind::ProcIdSym:
  case SymbolRecordKind::DPCProcIdSym:
    IsType = false;
    break;
  default:
    break;
  }
  P.formatLine("type = `{0}`, debug start = {1}, debug end = {2}, flags = {3}",
               typeOrIdIndex(Proc.FunctionType, IsType), Proc.DbgStart,
               Proc.DbgEnd,
               formatProcSymFlags(P.getIndentLevel() + 9, Proc.Flags));
  return Error::success();
}

Error MinimalSymbolDumper::visitKnownRecord(CVSymbol &CVR,
                                            ScopeEndSym &ScopeEnd) {
  return Error::success();
}

Error MinimalSymbolDumper::visitKnownRecord(CVSymbol &CVR, CallerSym &Caller) {
  const char *Format;
  switch (CVR.kind()) {
  case S_CALLEES:
    Format = "callee: {0}";
    break;
  case S_CALLERS:
    Format = "caller: {0}";
    break;
  case S_INLINEES:
    Format = "inlinee: {0}";
    break;
  default:
    return llvm::make_error<CodeViewError>(
        "Unknown CV Record type for a CallerSym object!");
  }
  AutoIndent Indent(P, 7);
  for (const auto &I : Caller.Indices) {
    P.formatLine(Format, idIndex(I));
  }
  return Error::success();
}

Error MinimalSymbolDumper::visitKnownRecord(CVSymbol &CVR,
                                            RegRelativeSym &RegRel) {
  P.format(" `{0}`", RegRel.Name);
  AutoIndent Indent(P, 7);
  P.formatLine(
      "type = {0}, register = {1}, offset = {2}", typeIndex(RegRel.Type),
      formatRegisterId(RegRel.Register, CompilationCPU), RegRel.Offset);
  return Error::success();
}

Error MinimalSymbolDumper::visitKnownRecord(CVSymbol &CVR,
                                            ThreadLocalDataSym &Data) {
  P.format(" `{0}`", Data.Name);
  AutoIndent Indent(P, 7);
  P.formatLine("type = {0}, addr = {1}", typeIndex(Data.Type),
               formatSegmentOffset(Data.Segment, Data.DataOffset));
  return Error::success();
}

Error MinimalSymbolDumper::visitKnownRecord(CVSymbol &CVR, UDTSym &UDT) {
  P.format(" `{0}`", UDT.Name);
  AutoIndent Indent(P, 7);
  P.formatLine("original type = {0}", UDT.Type);
  return Error::success();
}

Error MinimalSymbolDumper::visitKnownRecord(CVSymbol &CVR,
                                            UsingNamespaceSym &UN) {
  P.format(" `{0}`", UN.Name);
  return Error::success();
}

Error MinimalSymbolDumper::visitKnownRecord(CVSymbol &CVR,
                                            AnnotationSym &Annot) {
  AutoIndent Indent(P, 7);
  P.formatLine("addr = {0}", formatSegmentOffset(Annot.Segment, Annot.CodeOffset));
  P.formatLine("strings = {0}", typesetStringList(P.getIndentLevel() + 9 + 2,
                                                   Annot.Strings));
  return Error::success();
}

Error MinimalSymbolDumper::visitKnownRecord(CVSymbol &CVR,
                                            JumpTableSym &JumpTable) {
  AutoIndent Indent(P, 7);
  P.formatLine(
      "base = {0}, switchtype = {1}, branch = {2}, table = {3}, entriescount = "
      "{4}",
      formatSegmentOffset(JumpTable.BaseSegment, JumpTable.BaseOffset),
      formatJumpTableEntrySize(JumpTable.SwitchType),
      formatSegmentOffset(JumpTable.BranchSegment, JumpTable.BranchOffset),
      formatSegmentOffset(JumpTable.TableSegment, JumpTable.TableOffset),
      JumpTable.EntriesCount);
  return Error::success();
}
