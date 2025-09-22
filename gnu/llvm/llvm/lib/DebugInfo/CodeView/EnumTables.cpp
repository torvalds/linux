//===- EnumTables.cpp - Enum to string conversion tables ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/CodeView/EnumTables.h"
#include "llvm/Support/ScopedPrinter.h"
#include <type_traits>

using namespace llvm;
using namespace codeview;

#define CV_ENUM_CLASS_ENT(enum_class, enum)                                    \
  { #enum, std::underlying_type_t<enum_class>(enum_class::enum) }

#define CV_ENUM_ENT(ns, enum)                                                  \
  { #enum, ns::enum }

static const EnumEntry<SymbolKind> SymbolTypeNames[] = {
#define CV_SYMBOL(enum, val) {#enum, enum},
#include "llvm/DebugInfo/CodeView/CodeViewSymbols.def"
#undef CV_SYMBOL
};

static const EnumEntry<TypeLeafKind> TypeLeafNames[] = {
#define CV_TYPE(name, val) {#name, name},
#include "llvm/DebugInfo/CodeView/CodeViewTypes.def"
#undef CV_TYPE
};

static const EnumEntry<uint16_t> RegisterNames_X86[] = {
#define CV_REGISTERS_X86
#define CV_REGISTER(name, val) CV_ENUM_CLASS_ENT(RegisterId, name),
#include "llvm/DebugInfo/CodeView/CodeViewRegisters.def"
#undef CV_REGISTER
#undef CV_REGISTERS_X86
};

static const EnumEntry<uint16_t> RegisterNames_ARM[] = {
#define CV_REGISTERS_ARM
#define CV_REGISTER(name, val) CV_ENUM_CLASS_ENT(RegisterId, name),
#include "llvm/DebugInfo/CodeView/CodeViewRegisters.def"
#undef CV_REGISTER
#undef CV_REGISTERS_ARM
};

static const EnumEntry<uint16_t> RegisterNames_ARM64[] = {
#define CV_REGISTERS_ARM64
#define CV_REGISTER(name, val) CV_ENUM_CLASS_ENT(RegisterId, name),
#include "llvm/DebugInfo/CodeView/CodeViewRegisters.def"
#undef CV_REGISTER
#undef CV_REGISTERS_ARM64
};

static const EnumEntry<uint32_t> PublicSymFlagNames[] = {
    CV_ENUM_CLASS_ENT(PublicSymFlags, Code),
    CV_ENUM_CLASS_ENT(PublicSymFlags, Function),
    CV_ENUM_CLASS_ENT(PublicSymFlags, Managed),
    CV_ENUM_CLASS_ENT(PublicSymFlags, MSIL),
};

static const EnumEntry<uint8_t> ProcSymFlagNames[] = {
    CV_ENUM_CLASS_ENT(ProcSymFlags, HasFP),
    CV_ENUM_CLASS_ENT(ProcSymFlags, HasIRET),
    CV_ENUM_CLASS_ENT(ProcSymFlags, HasFRET),
    CV_ENUM_CLASS_ENT(ProcSymFlags, IsNoReturn),
    CV_ENUM_CLASS_ENT(ProcSymFlags, IsUnreachable),
    CV_ENUM_CLASS_ENT(ProcSymFlags, HasCustomCallingConv),
    CV_ENUM_CLASS_ENT(ProcSymFlags, IsNoInline),
    CV_ENUM_CLASS_ENT(ProcSymFlags, HasOptimizedDebugInfo),
};

static const EnumEntry<uint16_t> LocalFlags[] = {
    CV_ENUM_CLASS_ENT(LocalSymFlags, IsParameter),
    CV_ENUM_CLASS_ENT(LocalSymFlags, IsAddressTaken),
    CV_ENUM_CLASS_ENT(LocalSymFlags, IsCompilerGenerated),
    CV_ENUM_CLASS_ENT(LocalSymFlags, IsAggregate),
    CV_ENUM_CLASS_ENT(LocalSymFlags, IsAggregated),
    CV_ENUM_CLASS_ENT(LocalSymFlags, IsAliased),
    CV_ENUM_CLASS_ENT(LocalSymFlags, IsAlias),
    CV_ENUM_CLASS_ENT(LocalSymFlags, IsReturnValue),
    CV_ENUM_CLASS_ENT(LocalSymFlags, IsOptimizedOut),
    CV_ENUM_CLASS_ENT(LocalSymFlags, IsEnregisteredGlobal),
    CV_ENUM_CLASS_ENT(LocalSymFlags, IsEnregisteredStatic),
};

static const EnumEntry<uint8_t> FrameCookieKinds[] = {
    CV_ENUM_CLASS_ENT(FrameCookieKind, Copy),
    CV_ENUM_CLASS_ENT(FrameCookieKind, XorStackPointer),
    CV_ENUM_CLASS_ENT(FrameCookieKind, XorFramePointer),
    CV_ENUM_CLASS_ENT(FrameCookieKind, XorR13),
};

static const EnumEntry<codeview::SourceLanguage> SourceLanguages[] = {
    CV_ENUM_ENT(SourceLanguage, C),        CV_ENUM_ENT(SourceLanguage, Cpp),
    CV_ENUM_ENT(SourceLanguage, Fortran),  CV_ENUM_ENT(SourceLanguage, Masm),
    CV_ENUM_ENT(SourceLanguage, Pascal),   CV_ENUM_ENT(SourceLanguage, Basic),
    CV_ENUM_ENT(SourceLanguage, Cobol),    CV_ENUM_ENT(SourceLanguage, Link),
    CV_ENUM_ENT(SourceLanguage, Cvtres),   CV_ENUM_ENT(SourceLanguage, Cvtpgd),
    CV_ENUM_ENT(SourceLanguage, CSharp),   CV_ENUM_ENT(SourceLanguage, VB),
    CV_ENUM_ENT(SourceLanguage, ILAsm),    CV_ENUM_ENT(SourceLanguage, Java),
    CV_ENUM_ENT(SourceLanguage, JScript),  CV_ENUM_ENT(SourceLanguage, MSIL),
    CV_ENUM_ENT(SourceLanguage, HLSL),     CV_ENUM_ENT(SourceLanguage, D),
    CV_ENUM_ENT(SourceLanguage, Swift),    CV_ENUM_ENT(SourceLanguage, Rust),
    CV_ENUM_ENT(SourceLanguage, ObjC),     CV_ENUM_ENT(SourceLanguage, ObjCpp),
    CV_ENUM_ENT(SourceLanguage, AliasObj), CV_ENUM_ENT(SourceLanguage, Go),
    {"Swift", SourceLanguage::OldSwift},
};

static const EnumEntry<uint32_t> CompileSym2FlagNames[] = {
    CV_ENUM_CLASS_ENT(CompileSym2Flags, EC),
    CV_ENUM_CLASS_ENT(CompileSym2Flags, NoDbgInfo),
    CV_ENUM_CLASS_ENT(CompileSym2Flags, LTCG),
    CV_ENUM_CLASS_ENT(CompileSym2Flags, NoDataAlign),
    CV_ENUM_CLASS_ENT(CompileSym2Flags, ManagedPresent),
    CV_ENUM_CLASS_ENT(CompileSym2Flags, SecurityChecks),
    CV_ENUM_CLASS_ENT(CompileSym2Flags, HotPatch),
    CV_ENUM_CLASS_ENT(CompileSym2Flags, CVTCIL),
    CV_ENUM_CLASS_ENT(CompileSym2Flags, MSILModule),
};

static const EnumEntry<uint32_t> CompileSym3FlagNames[] = {
    CV_ENUM_CLASS_ENT(CompileSym3Flags, EC),
    CV_ENUM_CLASS_ENT(CompileSym3Flags, NoDbgInfo),
    CV_ENUM_CLASS_ENT(CompileSym3Flags, LTCG),
    CV_ENUM_CLASS_ENT(CompileSym3Flags, NoDataAlign),
    CV_ENUM_CLASS_ENT(CompileSym3Flags, ManagedPresent),
    CV_ENUM_CLASS_ENT(CompileSym3Flags, SecurityChecks),
    CV_ENUM_CLASS_ENT(CompileSym3Flags, HotPatch),
    CV_ENUM_CLASS_ENT(CompileSym3Flags, CVTCIL),
    CV_ENUM_CLASS_ENT(CompileSym3Flags, MSILModule),
    CV_ENUM_CLASS_ENT(CompileSym3Flags, Sdl),
    CV_ENUM_CLASS_ENT(CompileSym3Flags, PGO),
    CV_ENUM_CLASS_ENT(CompileSym3Flags, Exp),
};

static const EnumEntry<uint32_t> FileChecksumNames[] = {
    CV_ENUM_CLASS_ENT(FileChecksumKind, None),
    CV_ENUM_CLASS_ENT(FileChecksumKind, MD5),
    CV_ENUM_CLASS_ENT(FileChecksumKind, SHA1),
    CV_ENUM_CLASS_ENT(FileChecksumKind, SHA256),
};

static const EnumEntry<unsigned> CPUTypeNames[] = {
    CV_ENUM_CLASS_ENT(CPUType, Intel8080),
    CV_ENUM_CLASS_ENT(CPUType, Intel8086),
    CV_ENUM_CLASS_ENT(CPUType, Intel80286),
    CV_ENUM_CLASS_ENT(CPUType, Intel80386),
    CV_ENUM_CLASS_ENT(CPUType, Intel80486),
    CV_ENUM_CLASS_ENT(CPUType, Pentium),
    CV_ENUM_CLASS_ENT(CPUType, PentiumPro),
    CV_ENUM_CLASS_ENT(CPUType, Pentium3),
    CV_ENUM_CLASS_ENT(CPUType, MIPS),
    CV_ENUM_CLASS_ENT(CPUType, MIPS16),
    CV_ENUM_CLASS_ENT(CPUType, MIPS32),
    CV_ENUM_CLASS_ENT(CPUType, MIPS64),
    CV_ENUM_CLASS_ENT(CPUType, MIPSI),
    CV_ENUM_CLASS_ENT(CPUType, MIPSII),
    CV_ENUM_CLASS_ENT(CPUType, MIPSIII),
    CV_ENUM_CLASS_ENT(CPUType, MIPSIV),
    CV_ENUM_CLASS_ENT(CPUType, MIPSV),
    CV_ENUM_CLASS_ENT(CPUType, M68000),
    CV_ENUM_CLASS_ENT(CPUType, M68010),
    CV_ENUM_CLASS_ENT(CPUType, M68020),
    CV_ENUM_CLASS_ENT(CPUType, M68030),
    CV_ENUM_CLASS_ENT(CPUType, M68040),
    CV_ENUM_CLASS_ENT(CPUType, Alpha),
    CV_ENUM_CLASS_ENT(CPUType, Alpha21164),
    CV_ENUM_CLASS_ENT(CPUType, Alpha21164A),
    CV_ENUM_CLASS_ENT(CPUType, Alpha21264),
    CV_ENUM_CLASS_ENT(CPUType, Alpha21364),
    CV_ENUM_CLASS_ENT(CPUType, PPC601),
    CV_ENUM_CLASS_ENT(CPUType, PPC603),
    CV_ENUM_CLASS_ENT(CPUType, PPC604),
    CV_ENUM_CLASS_ENT(CPUType, PPC620),
    CV_ENUM_CLASS_ENT(CPUType, PPCFP),
    CV_ENUM_CLASS_ENT(CPUType, PPCBE),
    CV_ENUM_CLASS_ENT(CPUType, SH3),
    CV_ENUM_CLASS_ENT(CPUType, SH3E),
    CV_ENUM_CLASS_ENT(CPUType, SH3DSP),
    CV_ENUM_CLASS_ENT(CPUType, SH4),
    CV_ENUM_CLASS_ENT(CPUType, SHMedia),
    CV_ENUM_CLASS_ENT(CPUType, ARM3),
    CV_ENUM_CLASS_ENT(CPUType, ARM4),
    CV_ENUM_CLASS_ENT(CPUType, ARM4T),
    CV_ENUM_CLASS_ENT(CPUType, ARM5),
    CV_ENUM_CLASS_ENT(CPUType, ARM5T),
    CV_ENUM_CLASS_ENT(CPUType, ARM6),
    CV_ENUM_CLASS_ENT(CPUType, ARM_XMAC),
    CV_ENUM_CLASS_ENT(CPUType, ARM_WMMX),
    CV_ENUM_CLASS_ENT(CPUType, ARM7),
    CV_ENUM_CLASS_ENT(CPUType, Omni),
    CV_ENUM_CLASS_ENT(CPUType, Ia64),
    CV_ENUM_CLASS_ENT(CPUType, Ia64_2),
    CV_ENUM_CLASS_ENT(CPUType, CEE),
    CV_ENUM_CLASS_ENT(CPUType, AM33),
    CV_ENUM_CLASS_ENT(CPUType, M32R),
    CV_ENUM_CLASS_ENT(CPUType, TriCore),
    CV_ENUM_CLASS_ENT(CPUType, X64),
    CV_ENUM_CLASS_ENT(CPUType, EBC),
    CV_ENUM_CLASS_ENT(CPUType, Thumb),
    CV_ENUM_CLASS_ENT(CPUType, ARMNT),
    CV_ENUM_CLASS_ENT(CPUType, ARM64),
    CV_ENUM_CLASS_ENT(CPUType, HybridX86ARM64),
    CV_ENUM_CLASS_ENT(CPUType, ARM64EC),
    CV_ENUM_CLASS_ENT(CPUType, ARM64X),
    CV_ENUM_CLASS_ENT(CPUType, Unknown),
    CV_ENUM_CLASS_ENT(CPUType, D3D11_Shader),
};

static const EnumEntry<uint32_t> FrameProcSymFlagNames[] = {
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, HasAlloca),
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, HasSetJmp),
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, HasLongJmp),
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, HasInlineAssembly),
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, HasExceptionHandling),
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, MarkedInline),
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, HasStructuredExceptionHandling),
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, Naked),
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, SecurityChecks),
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, AsynchronousExceptionHandling),
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, NoStackOrderingForSecurityChecks),
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, Inlined),
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, StrictSecurityChecks),
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, SafeBuffers),
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, EncodedLocalBasePointerMask),
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, EncodedParamBasePointerMask),
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, ProfileGuidedOptimization),
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, ValidProfileCounts),
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, OptimizedForSpeed),
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, GuardCfg),
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, GuardCfw),
};

static const EnumEntry<uint32_t> ModuleSubstreamKindNames[] = {
    CV_ENUM_CLASS_ENT(DebugSubsectionKind, None),
    CV_ENUM_CLASS_ENT(DebugSubsectionKind, Symbols),
    CV_ENUM_CLASS_ENT(DebugSubsectionKind, Lines),
    CV_ENUM_CLASS_ENT(DebugSubsectionKind, StringTable),
    CV_ENUM_CLASS_ENT(DebugSubsectionKind, FileChecksums),
    CV_ENUM_CLASS_ENT(DebugSubsectionKind, FrameData),
    CV_ENUM_CLASS_ENT(DebugSubsectionKind, InlineeLines),
    CV_ENUM_CLASS_ENT(DebugSubsectionKind, CrossScopeImports),
    CV_ENUM_CLASS_ENT(DebugSubsectionKind, CrossScopeExports),
    CV_ENUM_CLASS_ENT(DebugSubsectionKind, ILLines),
    CV_ENUM_CLASS_ENT(DebugSubsectionKind, FuncMDTokenMap),
    CV_ENUM_CLASS_ENT(DebugSubsectionKind, TypeMDTokenMap),
    CV_ENUM_CLASS_ENT(DebugSubsectionKind, MergedAssemblyInput),
    CV_ENUM_CLASS_ENT(DebugSubsectionKind, CoffSymbolRVA),
};

static const EnumEntry<uint16_t> ExportSymFlagNames[] = {
    CV_ENUM_CLASS_ENT(ExportFlags, IsConstant),
    CV_ENUM_CLASS_ENT(ExportFlags, IsData),
    CV_ENUM_CLASS_ENT(ExportFlags, IsPrivate),
    CV_ENUM_CLASS_ENT(ExportFlags, HasNoName),
    CV_ENUM_CLASS_ENT(ExportFlags, HasExplicitOrdinal),
    CV_ENUM_CLASS_ENT(ExportFlags, IsForwarder),
};

static const EnumEntry<uint8_t> ThunkOrdinalNames[] = {
    CV_ENUM_CLASS_ENT(ThunkOrdinal, Standard),
    CV_ENUM_CLASS_ENT(ThunkOrdinal, ThisAdjustor),
    CV_ENUM_CLASS_ENT(ThunkOrdinal, Vcall),
    CV_ENUM_CLASS_ENT(ThunkOrdinal, Pcode),
    CV_ENUM_CLASS_ENT(ThunkOrdinal, UnknownLoad),
    CV_ENUM_CLASS_ENT(ThunkOrdinal, TrampIncremental),
    CV_ENUM_CLASS_ENT(ThunkOrdinal, BranchIsland),
};

static const EnumEntry<uint16_t> TrampolineNames[] = {
    CV_ENUM_CLASS_ENT(TrampolineType, TrampIncremental),
    CV_ENUM_CLASS_ENT(TrampolineType, BranchIsland),
};

static const EnumEntry<COFF::SectionCharacteristics>
    ImageSectionCharacteristicNames[] = {
        CV_ENUM_ENT(COFF, IMAGE_SCN_TYPE_NOLOAD),
        CV_ENUM_ENT(COFF, IMAGE_SCN_TYPE_NO_PAD),
        CV_ENUM_ENT(COFF, IMAGE_SCN_CNT_CODE),
        CV_ENUM_ENT(COFF, IMAGE_SCN_CNT_INITIALIZED_DATA),
        CV_ENUM_ENT(COFF, IMAGE_SCN_CNT_UNINITIALIZED_DATA),
        CV_ENUM_ENT(COFF, IMAGE_SCN_LNK_OTHER),
        CV_ENUM_ENT(COFF, IMAGE_SCN_LNK_INFO),
        CV_ENUM_ENT(COFF, IMAGE_SCN_LNK_REMOVE),
        CV_ENUM_ENT(COFF, IMAGE_SCN_LNK_COMDAT),
        CV_ENUM_ENT(COFF, IMAGE_SCN_GPREL),
        CV_ENUM_ENT(COFF, IMAGE_SCN_MEM_PURGEABLE),
        CV_ENUM_ENT(COFF, IMAGE_SCN_MEM_16BIT),
        CV_ENUM_ENT(COFF, IMAGE_SCN_MEM_LOCKED),
        CV_ENUM_ENT(COFF, IMAGE_SCN_MEM_PRELOAD),
        CV_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_1BYTES),
        CV_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_2BYTES),
        CV_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_4BYTES),
        CV_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_8BYTES),
        CV_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_16BYTES),
        CV_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_32BYTES),
        CV_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_64BYTES),
        CV_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_128BYTES),
        CV_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_256BYTES),
        CV_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_512BYTES),
        CV_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_1024BYTES),
        CV_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_2048BYTES),
        CV_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_4096BYTES),
        CV_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_8192BYTES),
        CV_ENUM_ENT(COFF, IMAGE_SCN_LNK_NRELOC_OVFL),
        CV_ENUM_ENT(COFF, IMAGE_SCN_MEM_DISCARDABLE),
        CV_ENUM_ENT(COFF, IMAGE_SCN_MEM_NOT_CACHED),
        CV_ENUM_ENT(COFF, IMAGE_SCN_MEM_NOT_PAGED),
        CV_ENUM_ENT(COFF, IMAGE_SCN_MEM_SHARED),
        CV_ENUM_ENT(COFF, IMAGE_SCN_MEM_EXECUTE),
        CV_ENUM_ENT(COFF, IMAGE_SCN_MEM_READ),
        CV_ENUM_ENT(COFF, IMAGE_SCN_MEM_WRITE)};

static const EnumEntry<uint16_t> ClassOptionNames[] = {
    CV_ENUM_CLASS_ENT(ClassOptions, Packed),
    CV_ENUM_CLASS_ENT(ClassOptions, HasConstructorOrDestructor),
    CV_ENUM_CLASS_ENT(ClassOptions, HasOverloadedOperator),
    CV_ENUM_CLASS_ENT(ClassOptions, Nested),
    CV_ENUM_CLASS_ENT(ClassOptions, ContainsNestedClass),
    CV_ENUM_CLASS_ENT(ClassOptions, HasOverloadedAssignmentOperator),
    CV_ENUM_CLASS_ENT(ClassOptions, HasConversionOperator),
    CV_ENUM_CLASS_ENT(ClassOptions, ForwardReference),
    CV_ENUM_CLASS_ENT(ClassOptions, Scoped),
    CV_ENUM_CLASS_ENT(ClassOptions, HasUniqueName),
    CV_ENUM_CLASS_ENT(ClassOptions, Sealed),
    CV_ENUM_CLASS_ENT(ClassOptions, Intrinsic),
};

static const EnumEntry<uint8_t> MemberAccessNames[] = {
    CV_ENUM_CLASS_ENT(MemberAccess, None),
    CV_ENUM_CLASS_ENT(MemberAccess, Private),
    CV_ENUM_CLASS_ENT(MemberAccess, Protected),
    CV_ENUM_CLASS_ENT(MemberAccess, Public),
};

static const EnumEntry<uint16_t> MethodOptionNames[] = {
    CV_ENUM_CLASS_ENT(MethodOptions, Pseudo),
    CV_ENUM_CLASS_ENT(MethodOptions, NoInherit),
    CV_ENUM_CLASS_ENT(MethodOptions, NoConstruct),
    CV_ENUM_CLASS_ENT(MethodOptions, CompilerGenerated),
    CV_ENUM_CLASS_ENT(MethodOptions, Sealed),
};

static const EnumEntry<uint16_t> MemberKindNames[] = {
    CV_ENUM_CLASS_ENT(MethodKind, Vanilla),
    CV_ENUM_CLASS_ENT(MethodKind, Virtual),
    CV_ENUM_CLASS_ENT(MethodKind, Static),
    CV_ENUM_CLASS_ENT(MethodKind, Friend),
    CV_ENUM_CLASS_ENT(MethodKind, IntroducingVirtual),
    CV_ENUM_CLASS_ENT(MethodKind, PureVirtual),
    CV_ENUM_CLASS_ENT(MethodKind, PureIntroducingVirtual),
};

static const EnumEntry<uint8_t> PtrKindNames[] = {
    CV_ENUM_CLASS_ENT(PointerKind, Near16),
    CV_ENUM_CLASS_ENT(PointerKind, Far16),
    CV_ENUM_CLASS_ENT(PointerKind, Huge16),
    CV_ENUM_CLASS_ENT(PointerKind, BasedOnSegment),
    CV_ENUM_CLASS_ENT(PointerKind, BasedOnValue),
    CV_ENUM_CLASS_ENT(PointerKind, BasedOnSegmentValue),
    CV_ENUM_CLASS_ENT(PointerKind, BasedOnAddress),
    CV_ENUM_CLASS_ENT(PointerKind, BasedOnSegmentAddress),
    CV_ENUM_CLASS_ENT(PointerKind, BasedOnType),
    CV_ENUM_CLASS_ENT(PointerKind, BasedOnSelf),
    CV_ENUM_CLASS_ENT(PointerKind, Near32),
    CV_ENUM_CLASS_ENT(PointerKind, Far32),
    CV_ENUM_CLASS_ENT(PointerKind, Near64),
};

static const EnumEntry<uint8_t> PtrModeNames[] = {
    CV_ENUM_CLASS_ENT(PointerMode, Pointer),
    CV_ENUM_CLASS_ENT(PointerMode, LValueReference),
    CV_ENUM_CLASS_ENT(PointerMode, PointerToDataMember),
    CV_ENUM_CLASS_ENT(PointerMode, PointerToMemberFunction),
    CV_ENUM_CLASS_ENT(PointerMode, RValueReference),
};

static const EnumEntry<uint16_t> PtrMemberRepNames[] = {
    CV_ENUM_CLASS_ENT(PointerToMemberRepresentation, Unknown),
    CV_ENUM_CLASS_ENT(PointerToMemberRepresentation, SingleInheritanceData),
    CV_ENUM_CLASS_ENT(PointerToMemberRepresentation, MultipleInheritanceData),
    CV_ENUM_CLASS_ENT(PointerToMemberRepresentation, VirtualInheritanceData),
    CV_ENUM_CLASS_ENT(PointerToMemberRepresentation, GeneralData),
    CV_ENUM_CLASS_ENT(PointerToMemberRepresentation, SingleInheritanceFunction),
    CV_ENUM_CLASS_ENT(PointerToMemberRepresentation,
                      MultipleInheritanceFunction),
    CV_ENUM_CLASS_ENT(PointerToMemberRepresentation,
                      VirtualInheritanceFunction),
    CV_ENUM_CLASS_ENT(PointerToMemberRepresentation, GeneralFunction),
};

static const EnumEntry<uint16_t> TypeModifierNames[] = {
    CV_ENUM_CLASS_ENT(ModifierOptions, Const),
    CV_ENUM_CLASS_ENT(ModifierOptions, Volatile),
    CV_ENUM_CLASS_ENT(ModifierOptions, Unaligned),
};

static const EnumEntry<uint8_t> CallingConventions[] = {
    CV_ENUM_CLASS_ENT(CallingConvention, NearC),
    CV_ENUM_CLASS_ENT(CallingConvention, FarC),
    CV_ENUM_CLASS_ENT(CallingConvention, NearPascal),
    CV_ENUM_CLASS_ENT(CallingConvention, FarPascal),
    CV_ENUM_CLASS_ENT(CallingConvention, NearFast),
    CV_ENUM_CLASS_ENT(CallingConvention, FarFast),
    CV_ENUM_CLASS_ENT(CallingConvention, NearStdCall),
    CV_ENUM_CLASS_ENT(CallingConvention, FarStdCall),
    CV_ENUM_CLASS_ENT(CallingConvention, NearSysCall),
    CV_ENUM_CLASS_ENT(CallingConvention, FarSysCall),
    CV_ENUM_CLASS_ENT(CallingConvention, ThisCall),
    CV_ENUM_CLASS_ENT(CallingConvention, MipsCall),
    CV_ENUM_CLASS_ENT(CallingConvention, Generic),
    CV_ENUM_CLASS_ENT(CallingConvention, AlphaCall),
    CV_ENUM_CLASS_ENT(CallingConvention, PpcCall),
    CV_ENUM_CLASS_ENT(CallingConvention, SHCall),
    CV_ENUM_CLASS_ENT(CallingConvention, ArmCall),
    CV_ENUM_CLASS_ENT(CallingConvention, AM33Call),
    CV_ENUM_CLASS_ENT(CallingConvention, TriCall),
    CV_ENUM_CLASS_ENT(CallingConvention, SH5Call),
    CV_ENUM_CLASS_ENT(CallingConvention, M32RCall),
    CV_ENUM_CLASS_ENT(CallingConvention, ClrCall),
    CV_ENUM_CLASS_ENT(CallingConvention, Inline),
    CV_ENUM_CLASS_ENT(CallingConvention, NearVector),
    CV_ENUM_CLASS_ENT(CallingConvention, Swift),
};

static const EnumEntry<uint8_t> FunctionOptionEnum[] = {
    CV_ENUM_CLASS_ENT(FunctionOptions, CxxReturnUdt),
    CV_ENUM_CLASS_ENT(FunctionOptions, Constructor),
    CV_ENUM_CLASS_ENT(FunctionOptions, ConstructorWithVirtualBases),
};

static const EnumEntry<uint16_t> LabelTypeEnum[] = {
    CV_ENUM_CLASS_ENT(LabelType, Near),
    CV_ENUM_CLASS_ENT(LabelType, Far),
};

static const EnumEntry<uint16_t> JumpTableEntrySizeNames[] = {
    CV_ENUM_CLASS_ENT(JumpTableEntrySize, Int8),
    CV_ENUM_CLASS_ENT(JumpTableEntrySize, UInt8),
    CV_ENUM_CLASS_ENT(JumpTableEntrySize, Int16),
    CV_ENUM_CLASS_ENT(JumpTableEntrySize, UInt16),
    CV_ENUM_CLASS_ENT(JumpTableEntrySize, Int32),
    CV_ENUM_CLASS_ENT(JumpTableEntrySize, UInt32),
    CV_ENUM_CLASS_ENT(JumpTableEntrySize, Pointer),
    CV_ENUM_CLASS_ENT(JumpTableEntrySize, UInt8ShiftLeft),
    CV_ENUM_CLASS_ENT(JumpTableEntrySize, UInt16ShiftLeft),
    CV_ENUM_CLASS_ENT(JumpTableEntrySize, Int8ShiftLeft),
    CV_ENUM_CLASS_ENT(JumpTableEntrySize, Int16ShiftLeft),
};

namespace llvm {
namespace codeview {

ArrayRef<EnumEntry<SymbolKind>> getSymbolTypeNames() {
  return ArrayRef(SymbolTypeNames);
}

ArrayRef<EnumEntry<TypeLeafKind>> getTypeLeafNames() {
  return ArrayRef(TypeLeafNames);
}

ArrayRef<EnumEntry<uint16_t>> getRegisterNames(CPUType Cpu) {
  if (Cpu == CPUType::ARMNT) {
    return ArrayRef(RegisterNames_ARM);
  } else if (Cpu == CPUType::ARM64) {
    return ArrayRef(RegisterNames_ARM64);
  }
  return ArrayRef(RegisterNames_X86);
}

ArrayRef<EnumEntry<uint32_t>> getPublicSymFlagNames() {
  return ArrayRef(PublicSymFlagNames);
}

ArrayRef<EnumEntry<uint8_t>> getProcSymFlagNames() {
  return ArrayRef(ProcSymFlagNames);
}

ArrayRef<EnumEntry<uint16_t>> getLocalFlagNames() {
  return ArrayRef(LocalFlags);
}

ArrayRef<EnumEntry<uint8_t>> getFrameCookieKindNames() {
  return ArrayRef(FrameCookieKinds);
}

ArrayRef<EnumEntry<SourceLanguage>> getSourceLanguageNames() {
  return ArrayRef(SourceLanguages);
}

ArrayRef<EnumEntry<uint32_t>> getCompileSym2FlagNames() {
  return ArrayRef(CompileSym2FlagNames);
}

ArrayRef<EnumEntry<uint32_t>> getCompileSym3FlagNames() {
  return ArrayRef(CompileSym3FlagNames);
}

ArrayRef<EnumEntry<uint32_t>> getFileChecksumNames() {
  return ArrayRef(FileChecksumNames);
}

ArrayRef<EnumEntry<unsigned>> getCPUTypeNames() {
  return ArrayRef(CPUTypeNames);
}

ArrayRef<EnumEntry<uint32_t>> getFrameProcSymFlagNames() {
  return ArrayRef(FrameProcSymFlagNames);
}

ArrayRef<EnumEntry<uint16_t>> getExportSymFlagNames() {
  return ArrayRef(ExportSymFlagNames);
}

ArrayRef<EnumEntry<uint32_t>> getModuleSubstreamKindNames() {
  return ArrayRef(ModuleSubstreamKindNames);
}

ArrayRef<EnumEntry<uint8_t>> getThunkOrdinalNames() {
  return ArrayRef(ThunkOrdinalNames);
}

ArrayRef<EnumEntry<uint16_t>> getTrampolineNames() {
  return ArrayRef(TrampolineNames);
}

ArrayRef<EnumEntry<COFF::SectionCharacteristics>>
getImageSectionCharacteristicNames() {
  return ArrayRef(ImageSectionCharacteristicNames);
}

ArrayRef<EnumEntry<uint16_t>> getClassOptionNames() {
  return ArrayRef(ClassOptionNames);
}

ArrayRef<EnumEntry<uint8_t>> getMemberAccessNames() {
  return ArrayRef(MemberAccessNames);
}

ArrayRef<EnumEntry<uint16_t>> getMethodOptionNames() {
  return ArrayRef(MethodOptionNames);
}

ArrayRef<EnumEntry<uint16_t>> getMemberKindNames() {
  return ArrayRef(MemberKindNames);
}

ArrayRef<EnumEntry<uint8_t>> getPtrKindNames() {
  return ArrayRef(PtrKindNames);
}

ArrayRef<EnumEntry<uint8_t>> getPtrModeNames() {
  return ArrayRef(PtrModeNames);
}

ArrayRef<EnumEntry<uint16_t>> getPtrMemberRepNames() {
  return ArrayRef(PtrMemberRepNames);
}

ArrayRef<EnumEntry<uint16_t>> getTypeModifierNames() {
  return ArrayRef(TypeModifierNames);
}

ArrayRef<EnumEntry<uint8_t>> getCallingConventions() {
  return ArrayRef(CallingConventions);
}

ArrayRef<EnumEntry<uint8_t>> getFunctionOptionEnum() {
  return ArrayRef(FunctionOptionEnum);
}

ArrayRef<EnumEntry<uint16_t>> getLabelTypeEnum() {
  return ArrayRef(LabelTypeEnum);
}

ArrayRef<EnumEntry<uint16_t>> getJumpTableEntrySizeNames() {
  return ArrayRef(JumpTableEntrySizeNames);
}

} // end namespace codeview
} // end namespace llvm
