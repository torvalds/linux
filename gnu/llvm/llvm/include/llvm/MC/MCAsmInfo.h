//===-- llvm/MC/MCAsmInfo.h - Asm info --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a class to be used as the basis for target specific
// asm writers.  This class primarily takes care of global printing constants,
// which are used in very similar ways across all targets.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCASMINFO_H
#define LLVM_MC_MCASMINFO_H

#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCTargetOptions.h"
#include <vector>

namespace llvm {

class MCContext;
class MCCFIInstruction;
class MCExpr;
class MCSection;
class MCStreamer;
class MCSubtargetInfo;
class MCSymbol;

namespace WinEH {

enum class EncodingType {
  Invalid, /// Invalid
  Alpha,   /// Windows Alpha
  Alpha64, /// Windows AXP64
  ARM,     /// Windows NT (Windows on ARM)
  CE,      /// Windows CE ARM, PowerPC, SH3, SH4
  Itanium, /// Windows x64, Windows Itanium (IA-64)
  X86,     /// Windows x86, uses no CFI, just EH tables
  MIPS = Alpha,
};

} // end namespace WinEH

namespace LCOMM {

enum LCOMMType { NoAlignment, ByteAlignment, Log2Alignment };

} // end namespace LCOMM

/// This class is intended to be used as a base class for asm
/// properties and features specific to the target.
class MCAsmInfo {
public:
  /// Assembly character literal syntax types.
  enum AsmCharLiteralSyntax {
    ACLS_Unknown, /// Unknown; character literals not used by LLVM for this
                  /// target.
    ACLS_SingleQuotePrefix, /// The desired character is prefixed by a single
                            /// quote, e.g., `'A`.
  };

protected:
  //===------------------------------------------------------------------===//
  // Properties to be set by the target writer, used to configure asm printer.
  //

  /// Code pointer size in bytes.  Default is 4.
  unsigned CodePointerSize = 4;

  /// Size of the stack slot reserved for callee-saved registers, in bytes.
  /// Default is same as pointer size.
  unsigned CalleeSaveStackSlotSize = 4;

  /// True if target is little endian.  Default is true.
  bool IsLittleEndian = true;

  /// True if target stack grow up.  Default is false.
  bool StackGrowsUp = false;

  /// True if this target has the MachO .subsections_via_symbols directive.
  /// Default is false.
  bool HasSubsectionsViaSymbols = false;

  /// True if this is a MachO target that supports the macho-specific .zerofill
  /// directive for emitting BSS Symbols.  Default is false.
  bool HasMachoZeroFillDirective = false;

  /// True if this is a MachO target that supports the macho-specific .tbss
  /// directive for emitting thread local BSS Symbols.  Default is false.
  bool HasMachoTBSSDirective = false;

  /// True if this is a non-GNU COFF target. The COFF port of the GNU linker
  /// doesn't handle associative comdats in the way that we would like to use
  /// them.
  bool HasCOFFAssociativeComdats = false;

  /// True if this is a non-GNU COFF target. For GNU targets, we don't generate
  /// constants into comdat sections.
  bool HasCOFFComdatConstants = false;

  /// True if this is an XCOFF target that supports visibility attributes as
  /// part of .global, .weak, .extern, and .comm. Default is false.
  bool HasVisibilityOnlyWithLinkage = false;

  /// This is the maximum possible length of an instruction, which is needed to
  /// compute the size of an inline asm.  Defaults to 4.
  unsigned MaxInstLength = 4;

  /// Every possible instruction length is a multiple of this value.  Factored
  /// out in .debug_frame and .debug_line.  Defaults to 1.
  unsigned MinInstAlignment = 1;

  /// The '$' token, when not referencing an identifier or constant, refers to
  /// the current PC.  Defaults to false.
  bool DollarIsPC = false;

  /// Allow '.' token, when not referencing an identifier or constant, to refer
  /// to the current PC. Defaults to true.
  bool DotIsPC = true;

  /// Whether the '*' token refers to the current PC. This is used for the
  /// HLASM dialect.
  bool StarIsPC = false;

  /// This string, if specified, is used to separate instructions from each
  /// other when on the same line.  Defaults to ';'
  const char *SeparatorString;

  /// This indicates the comment string used by the assembler.  Defaults to
  /// "#"
  StringRef CommentString;

  /// This indicates whether the comment string is only accepted as a comment
  /// at the beginning of statements. Defaults to false.
  bool RestrictCommentStringToStartOfStatement = false;

  /// This indicates whether to allow additional "comment strings" to be lexed
  /// as a comment. Setting this attribute to true, will ensure that C-style
  /// line comments (// ..), C-style block comments (/* .. */), and "#" are
  /// all treated as comments in addition to the string specified by the
  /// CommentString attribute.
  /// Default is true.
  bool AllowAdditionalComments = true;

  /// Should we emit the '\t' as the starting indentation marker for GNU inline
  /// asm statements. Defaults to true.
  bool EmitGNUAsmStartIndentationMarker = true;

  /// This is appended to emitted labels.  Defaults to ":"
  const char *LabelSuffix;

  /// Emit labels in purely upper case. Defaults to false.
  bool EmitLabelsInUpperCase = false;

  // Print the EH begin symbol with an assignment. Defaults to false.
  bool UseAssignmentForEHBegin = false;

  // Do we need to create a local symbol for .size?
  bool NeedsLocalForSize = false;

  /// This prefix is used for globals like constant pool entries that are
  /// completely private to the .s file and should not have names in the .o
  /// file.  Defaults to "L"
  StringRef PrivateGlobalPrefix;

  /// This prefix is used for labels for basic blocks. Defaults to the same as
  /// PrivateGlobalPrefix.
  StringRef PrivateLabelPrefix;

  /// This prefix is used for symbols that should be passed through the
  /// assembler but be removed by the linker.  This is 'l' on Darwin, currently
  /// used for some ObjC metadata.  The default of "" meast that for this system
  /// a plain private symbol should be used.  Defaults to "".
  StringRef LinkerPrivateGlobalPrefix;

  /// If these are nonempty, they contain a directive to emit before and after
  /// an inline assembly statement.  Defaults to "#APP\n", "#NO_APP\n"
  const char *InlineAsmStart;
  const char *InlineAsmEnd;

  /// These are assembly directives that tells the assembler to interpret the
  /// following instructions differently.  Defaults to ".code16", ".code32",
  /// ".code64".
  const char *Code16Directive;
  const char *Code32Directive;
  const char *Code64Directive;

  /// Which dialect of an assembler variant to use.  Defaults to 0
  unsigned AssemblerDialect = 0;

  /// This is true if the assembler allows @ characters in symbol names.
  /// Defaults to false.
  bool AllowAtInName = false;

  /// This is true if the assembler allows the "?" character at the start of
  /// of a string to be lexed as an AsmToken::Identifier.
  /// If the AsmLexer determines that the string can be lexed as a possible
  /// comment, setting this option will have no effect, and the string will
  /// still be lexed as a comment.
  bool AllowQuestionAtStartOfIdentifier = false;

  /// This is true if the assembler allows the "$" character at the start of
  /// of a string to be lexed as an AsmToken::Identifier.
  /// If the AsmLexer determines that the string can be lexed as a possible
  /// comment, setting this option will have no effect, and the string will
  /// still be lexed as a comment.
  bool AllowDollarAtStartOfIdentifier = false;

  /// This is true if the assembler allows the "@" character at the start of
  /// a string to be lexed as an AsmToken::Identifier.
  /// If the AsmLexer determines that the string can be lexed as a possible
  /// comment, setting this option will have no effect, and the string will
  /// still be lexed as a comment.
  bool AllowAtAtStartOfIdentifier = false;

  /// This is true if the assembler allows the "#" character at the start of
  /// a string to be lexed as an AsmToken::Identifier.
  /// If the AsmLexer determines that the string can be lexed as a possible
  /// comment, setting this option will have no effect, and the string will
  /// still be lexed as a comment.
  bool AllowHashAtStartOfIdentifier = false;

  /// If this is true, symbol names with invalid characters will be printed in
  /// quotes.
  bool SupportsQuotedNames = true;

  /// This is true if data region markers should be printed as
  /// ".data_region/.end_data_region" directives. If false, use "$d/$a" labels
  /// instead.
  bool UseDataRegionDirectives = false;

  /// True if .align is to be used for alignment. Only power-of-two
  /// alignment is supported.
  bool UseDotAlignForAlignment = false;

  /// True if the target supports LEB128 directives.
  bool HasLEB128Directives = true;

  /// True if full register names are printed.
  bool PPCUseFullRegisterNames = false;

  //===--- Data Emission Directives -------------------------------------===//

  /// This should be set to the directive used to get some number of zero (and
  /// non-zero if supported by the directive) bytes emitted to the current
  /// section. Common cases are "\t.zero\t" and "\t.space\t". Defaults to
  /// "\t.zero\t"
  const char *ZeroDirective;

  /// This should be set to true if the zero directive supports a value to emit
  /// other than zero. If this is set to false, the Data*bitsDirective's will be
  /// used to emit these bytes. Defaults to true.
  bool ZeroDirectiveSupportsNonZeroValue = true;

  /// This directive allows emission of an ascii string with the standard C
  /// escape characters embedded into it.  If a target doesn't support this, it
  /// can be set to null. Defaults to "\t.ascii\t"
  const char *AsciiDirective;

  /// If not null, this allows for special handling of zero terminated strings
  /// on this target.  This is commonly supported as ".asciz".  If a target
  /// doesn't support this, it can be set to null.  Defaults to "\t.asciz\t"
  const char *AscizDirective;

  /// This directive accepts a comma-separated list of bytes for emission as a
  /// string of bytes.  For targets that do not support this, it shall be set to
  /// null.  Defaults to null.
  const char *ByteListDirective = nullptr;

  /// This directive allows emission of a zero-terminated ascii string without
  /// the standard C escape characters embedded into it.  If a target doesn't
  /// support this, it can be set to null. Defaults to null.
  const char *PlainStringDirective = nullptr;

  /// Form used for character literals in the assembly syntax.  Useful for
  /// producing strings as byte lists.  If a target does not use or support
  /// this, it shall be set to ACLS_Unknown.  Defaults to ACLS_Unknown.
  AsmCharLiteralSyntax CharacterLiteralSyntax = ACLS_Unknown;

  /// These directives are used to output some unit of integer data to the
  /// current section.  If a data directive is set to null, smaller data
  /// directives will be used to emit the large sizes.  Defaults to "\t.byte\t",
  /// "\t.short\t", "\t.long\t", "\t.quad\t"
  const char *Data8bitsDirective;
  const char *Data16bitsDirective;
  const char *Data32bitsDirective;
  const char *Data64bitsDirective;

  /// True if data directives support signed values
  bool SupportsSignedData = true;

  /// If non-null, a directive that is used to emit a word which should be
  /// relocated as a 64-bit GP-relative offset, e.g. .gpdword on Mips.  Defaults
  /// to nullptr.
  const char *GPRel64Directive = nullptr;

  /// If non-null, a directive that is used to emit a word which should be
  /// relocated as a 32-bit GP-relative offset, e.g. .gpword on Mips or .gprel32
  /// on Alpha.  Defaults to nullptr.
  const char *GPRel32Directive = nullptr;

  /// If non-null, directives that are used to emit a word/dword which should
  /// be relocated as a 32/64-bit DTP/TP-relative offset, e.g. .dtprelword/
  /// .dtpreldword/.tprelword/.tpreldword on Mips.
  const char *DTPRel32Directive = nullptr;
  const char *DTPRel64Directive = nullptr;
  const char *TPRel32Directive = nullptr;
  const char *TPRel64Directive = nullptr;

  /// This is true if this target uses "Sun Style" syntax for section switching
  /// ("#alloc,#write" etc) instead of the normal ELF syntax (,"a,w") in
  /// .section directives.  Defaults to false.
  bool SunStyleELFSectionSwitchSyntax = false;

  /// This is true if this target uses ELF '.section' directive before the
  /// '.bss' one. It's used for PPC/Linux which doesn't support the '.bss'
  /// directive only.  Defaults to false.
  bool UsesELFSectionDirectiveForBSS = false;

  bool NeedsDwarfSectionOffsetDirective = false;

  //===--- Alignment Information ----------------------------------------===//

  /// If this is true (the default) then the asmprinter emits ".align N"
  /// directives, where N is the number of bytes to align to.  Otherwise, it
  /// emits ".align log2(N)", e.g. 3 to align to an 8 byte boundary.  Defaults
  /// to true.
  bool AlignmentIsInBytes = true;

  /// If non-zero, this is used to fill the executable space created as the
  /// result of a alignment directive.  Defaults to 0
  unsigned TextAlignFillValue = 0;

  //===--- Global Variable Emission Directives --------------------------===//

  /// This is the directive used to declare a global entity. Defaults to
  /// ".globl".
  const char *GlobalDirective;

  /// True if the expression
  ///   .long f - g
  /// uses a relocation but it can be suppressed by writing
  ///   a = f - g
  ///   .long a
  bool SetDirectiveSuppressesReloc = false;

  /// False if the assembler requires that we use
  /// \code
  ///   Lc = a - b
  ///   .long Lc
  /// \endcode
  //
  /// instead of
  //
  /// \code
  ///   .long a - b
  /// \endcode
  ///
  ///  Defaults to true.
  bool HasAggressiveSymbolFolding = true;

  /// True is .comm's and .lcomms optional alignment is to be specified in bytes
  /// instead of log2(n).  Defaults to true.
  bool COMMDirectiveAlignmentIsInBytes = true;

  /// Describes if the .lcomm directive for the target supports an alignment
  /// argument and how it is interpreted.  Defaults to NoAlignment.
  LCOMM::LCOMMType LCOMMDirectiveAlignmentType = LCOMM::NoAlignment;

  /// True if the target only has basename for .file directive. False if the
  /// target also needs the directory along with the basename. Defaults to true.
  bool HasBasenameOnlyForFileDirective = true;

  /// True if the target represents string constants as mostly raw characters in
  /// paired double quotation with paired double quotation marks as the escape
  /// mechanism to represent a double quotation mark within the string. Defaults
  /// to false.
  bool HasPairedDoubleQuoteStringConstants = false;

  // True if the target allows .align directives on functions. This is true for
  // most targets, so defaults to true.
  bool HasFunctionAlignment = true;

  /// True if the target has .type and .size directives, this is true for most
  /// ELF targets.  Defaults to true.
  bool HasDotTypeDotSizeDirective = true;

  /// True if the target has a single parameter .file directive, this is true
  /// for ELF targets.  Defaults to true.
  bool HasSingleParameterDotFile = true;

  /// True if the target has a four strings .file directive, strings separated
  /// by comma. Defaults to false.
  bool HasFourStringsDotFile = false;

  /// True if the target has a .ident directive, this is true for ELF targets.
  /// Defaults to false.
  bool HasIdentDirective = false;

  /// True if this target supports the MachO .no_dead_strip directive.  Defaults
  /// to false.
  bool HasNoDeadStrip = false;

  /// True if this target supports the MachO .alt_entry directive.  Defaults to
  /// false.
  bool HasAltEntry = false;

  /// Used to declare a global as being a weak symbol. Defaults to ".weak".
  const char *WeakDirective;

  /// This directive, if non-null, is used to declare a global as being a weak
  /// undefined symbol.  Defaults to nullptr.
  const char *WeakRefDirective = nullptr;

  /// True if we have a directive to declare a global as being a weak defined
  /// symbol.  Defaults to false.
  bool HasWeakDefDirective = false;

  /// True if we have a directive to declare a global as being a weak defined
  /// symbol that can be hidden (unexported).  Defaults to false.
  bool HasWeakDefCanBeHiddenDirective = false;

  /// True if we should mark symbols as global instead of weak, for
  /// weak*/linkonce*, if the symbol has a comdat.
  /// Defaults to false.
  bool AvoidWeakIfComdat = false;

  /// This attribute, if not MCSA_Invalid, is used to declare a symbol as having
  /// hidden visibility.  Defaults to MCSA_Hidden.
  MCSymbolAttr HiddenVisibilityAttr = MCSA_Hidden;

  /// This attribute, if not MCSA_Invalid, is used to declare a symbol as having
  /// exported visibility.  Defaults to MCSA_Exported.
  MCSymbolAttr ExportedVisibilityAttr = MCSA_Exported;

  /// This attribute, if not MCSA_Invalid, is used to declare an undefined
  /// symbol as having hidden visibility. Defaults to MCSA_Hidden.
  MCSymbolAttr HiddenDeclarationVisibilityAttr = MCSA_Hidden;

  /// This attribute, if not MCSA_Invalid, is used to declare a symbol as having
  /// protected visibility.  Defaults to MCSA_Protected
  MCSymbolAttr ProtectedVisibilityAttr = MCSA_Protected;

  MCSymbolAttr MemtagAttr = MCSA_Memtag;

  //===--- Dwarf Emission Directives -----------------------------------===//

  /// True if target supports emission of debugging information.  Defaults to
  /// false.
  bool SupportsDebugInformation = false;

  /// Exception handling format for the target.  Defaults to None.
  ExceptionHandling ExceptionsType = ExceptionHandling::None;

  /// True if target uses CFI unwind information for other purposes than EH
  /// (debugging / sanitizers) when `ExceptionsType == ExceptionHandling::None`.
  bool UsesCFIWithoutEH = false;

  /// Windows exception handling data (.pdata) encoding.  Defaults to Invalid.
  WinEH::EncodingType WinEHEncodingType = WinEH::EncodingType::Invalid;

  /// True if Dwarf2 output generally uses relocations for references to other
  /// .debug_* sections.
  bool DwarfUsesRelocationsAcrossSections = true;

  /// True if DWARF FDE symbol reference relocations should be replaced by an
  /// absolute difference.
  bool DwarfFDESymbolsUseAbsDiff = false;

  /// True if the target supports generating the DWARF line table through using
  /// the .loc/.file directives. Defaults to true.
  bool UsesDwarfFileAndLocDirectives = true;

  /// True if DWARF `.file directory' directive syntax is used by
  /// default.
  bool EnableDwarfFileDirectoryDefault = true;

  /// True if the target needs the DWARF section length in the header (if any)
  /// of the DWARF section in the assembly file. Defaults to true.
  bool DwarfSectionSizeRequired = true;

  /// True if dwarf register numbers are printed instead of symbolic register
  /// names in .cfi_* directives.  Defaults to false.
  bool DwarfRegNumForCFI = false;

  /// True if target uses parens to indicate the symbol variant instead of @.
  /// For example, foo(plt) instead of foo@plt.  Defaults to false.
  bool UseParensForSymbolVariant = false;

  /// True if the target uses parens for symbol names starting with
  /// '$' character to distinguish them from absolute names.
  bool UseParensForDollarSignNames = true;

  /// True if the target supports flags in ".loc" directive, false if only
  /// location is allowed.
  bool SupportsExtendedDwarfLocDirective = true;

  //===--- Prologue State ----------------------------------------------===//

  std::vector<MCCFIInstruction> InitialFrameState;

  //===--- Integrated Assembler Information ----------------------------===//

  // Generated object files can use all ELF features supported by GNU ld of
  // this binutils version and later. INT_MAX means all features can be used,
  // regardless of GNU ld support. The default value is referenced by
  // clang/Driver/Options.td.
  std::pair<int, int> BinutilsVersion = {2, 26};

  /// Should we use the integrated assembler?
  /// The integrated assembler should be enabled by default (by the
  /// constructors) when failing to parse a valid piece of assembly (inline
  /// or otherwise) is considered a bug. It may then be overridden after
  /// construction (see LLVMTargetMachine::initAsmInfo()).
  bool UseIntegratedAssembler;

  /// Use AsmParser to parse inlineAsm when UseIntegratedAssembler is not set.
  bool ParseInlineAsmUsingAsmParser;

  /// Preserve Comments in assembly
  bool PreserveAsmComments;

  /// True if the integrated assembler should interpret 'a >> b' constant
  /// expressions as logical rather than arithmetic.
  bool UseLogicalShr = true;

  // If true, then the lexer and expression parser will support %neg(),
  // %hi(), and similar unary operators.
  bool HasMipsExpressions = false;

  // If true, use Motorola-style integers in Assembly (ex. $0ac).
  bool UseMotorolaIntegers = false;

  // If true, emit function descriptor symbol on AIX.
  bool NeedsFunctionDescriptors = false;

public:
  explicit MCAsmInfo();
  virtual ~MCAsmInfo();

  /// Get the code pointer size in bytes.
  unsigned getCodePointerSize() const { return CodePointerSize; }

  /// Get the callee-saved register stack slot
  /// size in bytes.
  unsigned getCalleeSaveStackSlotSize() const {
    return CalleeSaveStackSlotSize;
  }

  /// True if the target is little endian.
  bool isLittleEndian() const { return IsLittleEndian; }

  /// True if target stack grow up.
  bool isStackGrowthDirectionUp() const { return StackGrowsUp; }

  bool hasSubsectionsViaSymbols() const { return HasSubsectionsViaSymbols; }

  // Data directive accessors.

  const char *getData8bitsDirective() const { return Data8bitsDirective; }
  const char *getData16bitsDirective() const { return Data16bitsDirective; }
  const char *getData32bitsDirective() const { return Data32bitsDirective; }
  const char *getData64bitsDirective() const { return Data64bitsDirective; }
  bool supportsSignedData() const { return SupportsSignedData; }
  const char *getGPRel64Directive() const { return GPRel64Directive; }
  const char *getGPRel32Directive() const { return GPRel32Directive; }
  const char *getDTPRel64Directive() const { return DTPRel64Directive; }
  const char *getDTPRel32Directive() const { return DTPRel32Directive; }
  const char *getTPRel64Directive() const { return TPRel64Directive; }
  const char *getTPRel32Directive() const { return TPRel32Directive; }

  /// Targets can implement this method to specify a section to switch to if the
  /// translation unit doesn't have any trampolines that require an executable
  /// stack.
  virtual MCSection *getNonexecutableStackSection(MCContext &Ctx) const {
    return nullptr;
  }

  virtual const MCExpr *getExprForPersonalitySymbol(const MCSymbol *Sym,
                                                    unsigned Encoding,
                                                    MCStreamer &Streamer) const;

  virtual const MCExpr *getExprForFDESymbol(const MCSymbol *Sym,
                                            unsigned Encoding,
                                            MCStreamer &Streamer) const;

  /// Return true if C is an acceptable character inside a symbol name.
  virtual bool isAcceptableChar(char C) const;

  /// Return true if the identifier \p Name does not need quotes to be
  /// syntactically correct.
  virtual bool isValidUnquotedName(StringRef Name) const;

  /// Return true if the .section directive should be omitted when
  /// emitting \p SectionName.  For example:
  ///
  /// shouldOmitSectionDirective(".text")
  ///
  /// returns false => .section .text,#alloc,#execinstr
  /// returns true  => .text
  virtual bool shouldOmitSectionDirective(StringRef SectionName) const;

  bool usesSunStyleELFSectionSwitchSyntax() const {
    return SunStyleELFSectionSwitchSyntax;
  }

  bool usesELFSectionDirectiveForBSS() const {
    return UsesELFSectionDirectiveForBSS;
  }

  bool needsDwarfSectionOffsetDirective() const {
    return NeedsDwarfSectionOffsetDirective;
  }

  // Accessors.

  bool hasMachoZeroFillDirective() const { return HasMachoZeroFillDirective; }
  bool hasMachoTBSSDirective() const { return HasMachoTBSSDirective; }
  bool hasCOFFAssociativeComdats() const { return HasCOFFAssociativeComdats; }
  bool hasCOFFComdatConstants() const { return HasCOFFComdatConstants; }
  bool hasVisibilityOnlyWithLinkage() const {
    return HasVisibilityOnlyWithLinkage;
  }

  /// Returns the maximum possible encoded instruction size in bytes. If \p STI
  /// is null, this should be the maximum size for any subtarget.
  virtual unsigned getMaxInstLength(const MCSubtargetInfo *STI = nullptr) const {
    return MaxInstLength;
  }

  unsigned getMinInstAlignment() const { return MinInstAlignment; }
  bool getDollarIsPC() const { return DollarIsPC; }
  bool getDotIsPC() const { return DotIsPC; }
  bool getStarIsPC() const { return StarIsPC; }
  const char *getSeparatorString() const { return SeparatorString; }

  /// This indicates the column (zero-based) at which asm comments should be
  /// printed.
  unsigned getCommentColumn() const { return 40; }

  StringRef getCommentString() const { return CommentString; }
  bool getRestrictCommentStringToStartOfStatement() const {
    return RestrictCommentStringToStartOfStatement;
  }
  bool shouldAllowAdditionalComments() const { return AllowAdditionalComments; }
  bool getEmitGNUAsmStartIndentationMarker() const {
    return EmitGNUAsmStartIndentationMarker;
  }
  const char *getLabelSuffix() const { return LabelSuffix; }
  bool shouldEmitLabelsInUpperCase() const { return EmitLabelsInUpperCase; }

  bool useAssignmentForEHBegin() const { return UseAssignmentForEHBegin; }
  bool needsLocalForSize() const { return NeedsLocalForSize; }
  StringRef getPrivateGlobalPrefix() const { return PrivateGlobalPrefix; }
  StringRef getPrivateLabelPrefix() const { return PrivateLabelPrefix; }

  bool hasLinkerPrivateGlobalPrefix() const {
    return !LinkerPrivateGlobalPrefix.empty();
  }

  StringRef getLinkerPrivateGlobalPrefix() const {
    if (hasLinkerPrivateGlobalPrefix())
      return LinkerPrivateGlobalPrefix;
    return getPrivateGlobalPrefix();
  }

  const char *getInlineAsmStart() const { return InlineAsmStart; }
  const char *getInlineAsmEnd() const { return InlineAsmEnd; }
  const char *getCode16Directive() const { return Code16Directive; }
  const char *getCode32Directive() const { return Code32Directive; }
  const char *getCode64Directive() const { return Code64Directive; }
  unsigned getAssemblerDialect() const { return AssemblerDialect; }
  bool doesAllowAtInName() const { return AllowAtInName; }
  void setAllowAtInName(bool V) { AllowAtInName = V; }
  bool doesAllowQuestionAtStartOfIdentifier() const {
    return AllowQuestionAtStartOfIdentifier;
  }
  bool doesAllowAtAtStartOfIdentifier() const {
    return AllowAtAtStartOfIdentifier;
  }
  bool doesAllowDollarAtStartOfIdentifier() const {
    return AllowDollarAtStartOfIdentifier;
  }
  bool doesAllowHashAtStartOfIdentifier() const {
    return AllowHashAtStartOfIdentifier;
  }
  bool supportsNameQuoting() const { return SupportsQuotedNames; }

  bool doesSupportDataRegionDirectives() const {
    return UseDataRegionDirectives;
  }

  bool useDotAlignForAlignment() const {
    return UseDotAlignForAlignment;
  }

  bool hasLEB128Directives() const { return HasLEB128Directives; }

  bool useFullRegisterNames() const { return PPCUseFullRegisterNames; }
  void setFullRegisterNames(bool V) { PPCUseFullRegisterNames = V; }

  const char *getZeroDirective() const { return ZeroDirective; }
  bool doesZeroDirectiveSupportNonZeroValue() const {
    return ZeroDirectiveSupportsNonZeroValue;
  }
  const char *getAsciiDirective() const { return AsciiDirective; }
  const char *getAscizDirective() const { return AscizDirective; }
  const char *getByteListDirective() const { return ByteListDirective; }
  const char *getPlainStringDirective() const { return PlainStringDirective; }
  AsmCharLiteralSyntax characterLiteralSyntax() const {
    return CharacterLiteralSyntax;
  }
  bool getAlignmentIsInBytes() const { return AlignmentIsInBytes; }
  unsigned getTextAlignFillValue() const { return TextAlignFillValue; }
  const char *getGlobalDirective() const { return GlobalDirective; }

  bool doesSetDirectiveSuppressReloc() const {
    return SetDirectiveSuppressesReloc;
  }

  bool hasAggressiveSymbolFolding() const { return HasAggressiveSymbolFolding; }

  bool getCOMMDirectiveAlignmentIsInBytes() const {
    return COMMDirectiveAlignmentIsInBytes;
  }

  LCOMM::LCOMMType getLCOMMDirectiveAlignmentType() const {
    return LCOMMDirectiveAlignmentType;
  }

  bool hasBasenameOnlyForFileDirective() const {
    return HasBasenameOnlyForFileDirective;
  }
  bool hasPairedDoubleQuoteStringConstants() const {
    return HasPairedDoubleQuoteStringConstants;
  }
  bool hasFunctionAlignment() const { return HasFunctionAlignment; }
  bool hasDotTypeDotSizeDirective() const { return HasDotTypeDotSizeDirective; }
  bool hasSingleParameterDotFile() const { return HasSingleParameterDotFile; }
  bool hasFourStringsDotFile() const { return HasFourStringsDotFile; }
  bool hasIdentDirective() const { return HasIdentDirective; }
  bool hasNoDeadStrip() const { return HasNoDeadStrip; }
  bool hasAltEntry() const { return HasAltEntry; }
  const char *getWeakDirective() const { return WeakDirective; }
  const char *getWeakRefDirective() const { return WeakRefDirective; }
  bool hasWeakDefDirective() const { return HasWeakDefDirective; }

  bool hasWeakDefCanBeHiddenDirective() const {
    return HasWeakDefCanBeHiddenDirective;
  }

  bool avoidWeakIfComdat() const { return AvoidWeakIfComdat; }

  MCSymbolAttr getHiddenVisibilityAttr() const { return HiddenVisibilityAttr; }

  MCSymbolAttr getExportedVisibilityAttr() const { return ExportedVisibilityAttr; }

  MCSymbolAttr getHiddenDeclarationVisibilityAttr() const {
    return HiddenDeclarationVisibilityAttr;
  }

  MCSymbolAttr getProtectedVisibilityAttr() const {
    return ProtectedVisibilityAttr;
  }

  MCSymbolAttr getMemtagAttr() const { return MemtagAttr; }

  bool doesSupportDebugInformation() const { return SupportsDebugInformation; }

  ExceptionHandling getExceptionHandlingType() const { return ExceptionsType; }
  WinEH::EncodingType getWinEHEncodingType() const { return WinEHEncodingType; }

  void setExceptionsType(ExceptionHandling EH) {
    ExceptionsType = EH;
  }

  bool usesCFIWithoutEH() const {
    return ExceptionsType == ExceptionHandling::None && UsesCFIWithoutEH;
  }

  /// Returns true if the exception handling method for the platform uses call
  /// frame information to unwind.
  bool usesCFIForEH() const {
    return (ExceptionsType == ExceptionHandling::DwarfCFI ||
            ExceptionsType == ExceptionHandling::ARM ||
            ExceptionsType == ExceptionHandling::ZOS || usesWindowsCFI());
  }

  bool usesWindowsCFI() const {
    return ExceptionsType == ExceptionHandling::WinEH &&
           (WinEHEncodingType != WinEH::EncodingType::Invalid &&
            WinEHEncodingType != WinEH::EncodingType::X86);
  }

  bool doesDwarfUseRelocationsAcrossSections() const {
    return DwarfUsesRelocationsAcrossSections;
  }

  bool doDwarfFDESymbolsUseAbsDiff() const { return DwarfFDESymbolsUseAbsDiff; }
  bool useDwarfRegNumForCFI() const { return DwarfRegNumForCFI; }
  bool useParensForSymbolVariant() const { return UseParensForSymbolVariant; }
  bool useParensForDollarSignNames() const {
    return UseParensForDollarSignNames;
  }
  bool supportsExtendedDwarfLocDirective() const {
    return SupportsExtendedDwarfLocDirective;
  }

  bool usesDwarfFileAndLocDirectives() const {
    return UsesDwarfFileAndLocDirectives;
  }

  bool needsDwarfSectionSizeInHeader() const {
    return DwarfSectionSizeRequired;
  }

  bool enableDwarfFileDirectoryDefault() const {
    return EnableDwarfFileDirectoryDefault;
  }

  void addInitialFrameState(const MCCFIInstruction &Inst);

  const std::vector<MCCFIInstruction> &getInitialFrameState() const {
    return InitialFrameState;
  }

  void setBinutilsVersion(std::pair<int, int> Value) {
    BinutilsVersion = Value;
  }

  /// Return true if assembly (inline or otherwise) should be parsed.
  bool useIntegratedAssembler() const { return UseIntegratedAssembler; }

  /// Return true if target want to use AsmParser to parse inlineasm.
  bool parseInlineAsmUsingAsmParser() const {
    return ParseInlineAsmUsingAsmParser;
  }

  bool binutilsIsAtLeast(int Major, int Minor) const {
    return BinutilsVersion >= std::make_pair(Major, Minor);
  }

  /// Set whether assembly (inline or otherwise) should be parsed.
  virtual void setUseIntegratedAssembler(bool Value) {
    UseIntegratedAssembler = Value;
  }

  /// Set whether target want to use AsmParser to parse inlineasm.
  virtual void setParseInlineAsmUsingAsmParser(bool Value) {
    ParseInlineAsmUsingAsmParser = Value;
  }

  /// Return true if assembly (inline or otherwise) should be parsed.
  bool preserveAsmComments() const { return PreserveAsmComments; }

  /// Set whether assembly (inline or otherwise) should be parsed.
  virtual void setPreserveAsmComments(bool Value) {
    PreserveAsmComments = Value;
  }


  bool shouldUseLogicalShr() const { return UseLogicalShr; }

  bool hasMipsExpressions() const { return HasMipsExpressions; }
  bool needsFunctionDescriptors() const { return NeedsFunctionDescriptors; }
  bool shouldUseMotorolaIntegers() const { return UseMotorolaIntegers; }
};

} // end namespace llvm

#endif // LLVM_MC_MCASMINFO_H
