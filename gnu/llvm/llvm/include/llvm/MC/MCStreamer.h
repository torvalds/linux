//===- MCStreamer.h - High-level Streaming Machine Code Output --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the MCStreamer class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCSTREAMER_H
#define LLVM_MC_MCSTREAMER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCFragment.h"
#include "llvm/MC/MCLinkerOptimizationHint.h"
#include "llvm/MC/MCPseudoProbe.h"
#include "llvm/MC/MCWinEH.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/TargetParser/ARMTargetParser.h"
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace llvm {

class APInt;
class AssemblerConstantPools;
class MCAsmBackend;
class MCAssembler;
class MCContext;
class MCExpr;
class MCFragment;
class MCInst;
class MCInstPrinter;
class MCRegister;
class MCSection;
class MCStreamer;
class MCSubtargetInfo;
class MCSymbol;
class MCSymbolRefExpr;
class Triple;
class Twine;
class raw_ostream;

namespace codeview {
struct DefRangeRegisterRelHeader;
struct DefRangeSubfieldRegisterHeader;
struct DefRangeRegisterHeader;
struct DefRangeFramePointerRelHeader;
}

using MCSectionSubPair = std::pair<MCSection *, uint32_t>;

/// Target specific streamer interface. This is used so that targets can
/// implement support for target specific assembly directives.
///
/// If target foo wants to use this, it should implement 3 classes:
/// * FooTargetStreamer : public MCTargetStreamer
/// * FooTargetAsmStreamer : public FooTargetStreamer
/// * FooTargetELFStreamer : public FooTargetStreamer
///
/// FooTargetStreamer should have a pure virtual method for each directive. For
/// example, for a ".bar symbol_name" directive, it should have
/// virtual emitBar(const MCSymbol &Symbol) = 0;
///
/// The FooTargetAsmStreamer and FooTargetELFStreamer classes implement the
/// method. The assembly streamer just prints ".bar symbol_name". The object
/// streamer does whatever is needed to implement .bar in the object file.
///
/// In the assembly printer and parser the target streamer can be used by
/// calling getTargetStreamer and casting it to FooTargetStreamer:
///
/// MCTargetStreamer &TS = OutStreamer.getTargetStreamer();
/// FooTargetStreamer &ATS = static_cast<FooTargetStreamer &>(TS);
///
/// The base classes FooTargetAsmStreamer and FooTargetELFStreamer should
/// *never* be treated differently. Callers should always talk to a
/// FooTargetStreamer.
class MCTargetStreamer {
protected:
  MCStreamer &Streamer;

public:
  MCTargetStreamer(MCStreamer &S);
  virtual ~MCTargetStreamer();

  MCStreamer &getStreamer() { return Streamer; }

  // Allow a target to add behavior to the EmitLabel of MCStreamer.
  virtual void emitLabel(MCSymbol *Symbol);
  // Allow a target to add behavior to the emitAssignment of MCStreamer.
  virtual void emitAssignment(MCSymbol *Symbol, const MCExpr *Value);

  virtual void prettyPrintAsm(MCInstPrinter &InstPrinter, uint64_t Address,
                              const MCInst &Inst, const MCSubtargetInfo &STI,
                              raw_ostream &OS);

  virtual void emitDwarfFileDirective(StringRef Directive);

  /// Update streamer for a new active section.
  ///
  /// This is called by popSection and switchSection, if the current
  /// section changes.
  virtual void changeSection(const MCSection *CurSection, MCSection *Section,
                             uint32_t SubSection, raw_ostream &OS);

  virtual void emitValue(const MCExpr *Value);

  /// Emit the bytes in \p Data into the output.
  ///
  /// This is used to emit bytes in \p Data as sequence of .byte directives.
  virtual void emitRawBytes(StringRef Data);

  virtual void emitConstantPools();

  virtual void finish();
};

// FIXME: declared here because it is used from
// lib/CodeGen/AsmPrinter/ARMException.cpp.
class ARMTargetStreamer : public MCTargetStreamer {
public:
  ARMTargetStreamer(MCStreamer &S);
  ~ARMTargetStreamer() override;

  virtual void emitFnStart();
  virtual void emitFnEnd();
  virtual void emitCantUnwind();
  virtual void emitPersonality(const MCSymbol *Personality);
  virtual void emitPersonalityIndex(unsigned Index);
  virtual void emitHandlerData();
  virtual void emitSetFP(unsigned FpReg, unsigned SpReg,
                         int64_t Offset = 0);
  virtual void emitMovSP(unsigned Reg, int64_t Offset = 0);
  virtual void emitPad(int64_t Offset);
  virtual void emitRegSave(const SmallVectorImpl<unsigned> &RegList,
                           bool isVector);
  virtual void emitUnwindRaw(int64_t StackOffset,
                             const SmallVectorImpl<uint8_t> &Opcodes);

  virtual void switchVendor(StringRef Vendor);
  virtual void emitAttribute(unsigned Attribute, unsigned Value);
  virtual void emitTextAttribute(unsigned Attribute, StringRef String);
  virtual void emitIntTextAttribute(unsigned Attribute, unsigned IntValue,
                                    StringRef StringValue = "");
  virtual void emitFPU(ARM::FPUKind FPU);
  virtual void emitArch(ARM::ArchKind Arch);
  virtual void emitArchExtension(uint64_t ArchExt);
  virtual void emitObjectArch(ARM::ArchKind Arch);
  void emitTargetAttributes(const MCSubtargetInfo &STI);
  virtual void finishAttributeSection();
  virtual void emitInst(uint32_t Inst, char Suffix = '\0');

  virtual void annotateTLSDescriptorSequence(const MCSymbolRefExpr *SRE);

  virtual void emitThumbSet(MCSymbol *Symbol, const MCExpr *Value);

  void emitConstantPools() override;

  virtual void emitARMWinCFIAllocStack(unsigned Size, bool Wide);
  virtual void emitARMWinCFISaveRegMask(unsigned Mask, bool Wide);
  virtual void emitARMWinCFISaveSP(unsigned Reg);
  virtual void emitARMWinCFISaveFRegs(unsigned First, unsigned Last);
  virtual void emitARMWinCFISaveLR(unsigned Offset);
  virtual void emitARMWinCFIPrologEnd(bool Fragment);
  virtual void emitARMWinCFINop(bool Wide);
  virtual void emitARMWinCFIEpilogStart(unsigned Condition);
  virtual void emitARMWinCFIEpilogEnd();
  virtual void emitARMWinCFICustom(unsigned Opcode);

  /// Reset any state between object emissions, i.e. the equivalent of
  /// MCStreamer's reset method.
  virtual void reset();

  /// Callback used to implement the ldr= pseudo.
  /// Add a new entry to the constant pool for the current section and return an
  /// MCExpr that can be used to refer to the constant pool location.
  const MCExpr *addConstantPoolEntry(const MCExpr *, SMLoc Loc);

  /// Callback used to implement the .ltorg directive.
  /// Emit contents of constant pool for the current section.
  void emitCurrentConstantPool();

private:
  std::unique_ptr<AssemblerConstantPools> ConstantPools;
};

/// Streaming machine code generation interface.
///
/// This interface is intended to provide a programmatic interface that is very
/// similar to the level that an assembler .s file provides.  It has callbacks
/// to emit bytes, handle directives, etc.  The implementation of this interface
/// retains state to know what the current section is etc.
///
/// There are multiple implementations of this interface: one for writing out
/// a .s file, and implementations that write out .o files of various formats.
///
class MCStreamer {
  MCContext &Context;
  std::unique_ptr<MCTargetStreamer> TargetStreamer;

  std::vector<MCDwarfFrameInfo> DwarfFrameInfos;
  // This is a pair of index into DwarfFrameInfos and the MCSection associated
  // with the frame. Note, we use an index instead of an iterator because they
  // can be invalidated in std::vector.
  SmallVector<std::pair<size_t, MCSection *>, 1> FrameInfoStack;
  MCDwarfFrameInfo *getCurrentDwarfFrameInfo();

  /// Similar to DwarfFrameInfos, but for SEH unwind info. Chained frames may
  /// refer to each other, so use std::unique_ptr to provide pointer stability.
  std::vector<std::unique_ptr<WinEH::FrameInfo>> WinFrameInfos;

  WinEH::FrameInfo *CurrentWinFrameInfo;
  size_t CurrentProcWinFrameInfoStartIndex;

  /// This is stack of current and previous section values saved by
  /// pushSection.
  SmallVector<std::pair<MCSectionSubPair, MCSectionSubPair>, 4> SectionStack;

  /// Pointer to the parser's SMLoc if available. This is used to provide
  /// locations for diagnostics.
  const SMLoc *StartTokLocPtr = nullptr;

  /// The next unique ID to use when creating a WinCFI-related section (.pdata
  /// or .xdata). This ID ensures that we have a one-to-one mapping from
  /// code section to unwind info section, which MSVC's incremental linker
  /// requires.
  unsigned NextWinCFIID = 0;

  bool UseAssemblerInfoForParsing = true;

  /// Is the assembler allowed to insert padding automatically?  For
  /// correctness reasons, we sometimes need to ensure instructions aren't
  /// separated in unexpected ways.  At the moment, this feature is only
  /// useable from an integrated assembler, but assembly syntax is under
  /// discussion for future inclusion.
  bool AllowAutoPadding = false;

protected:
  MCFragment *CurFrag = nullptr;

  MCStreamer(MCContext &Ctx);

  /// This is called by popSection and switchSection, if the current
  /// section changes.
  virtual void changeSection(MCSection *, uint32_t);

  virtual void emitCFIStartProcImpl(MCDwarfFrameInfo &Frame);
  virtual void emitCFIEndProcImpl(MCDwarfFrameInfo &CurFrame);

  WinEH::FrameInfo *getCurrentWinFrameInfo() {
    return CurrentWinFrameInfo;
  }

  virtual void emitWindowsUnwindTables(WinEH::FrameInfo *Frame);

  virtual void emitWindowsUnwindTables();

  virtual void emitRawTextImpl(StringRef String);

  /// Returns true if the .cv_loc directive is in the right section.
  bool checkCVLocSection(unsigned FuncId, unsigned FileNo, SMLoc Loc);

public:
  MCStreamer(const MCStreamer &) = delete;
  MCStreamer &operator=(const MCStreamer &) = delete;
  virtual ~MCStreamer();

  void visitUsedExpr(const MCExpr &Expr);
  virtual void visitUsedSymbol(const MCSymbol &Sym);

  void setTargetStreamer(MCTargetStreamer *TS) {
    TargetStreamer.reset(TS);
  }

  void setStartTokLocPtr(const SMLoc *Loc) { StartTokLocPtr = Loc; }
  SMLoc getStartTokLoc() const {
    return StartTokLocPtr ? *StartTokLocPtr : SMLoc();
  }

  /// State management
  ///
  virtual void reset();

  MCContext &getContext() const { return Context; }

  // MCObjectStreamer has an MCAssembler and allows more expression folding at
  // parse time.
  virtual MCAssembler *getAssemblerPtr() { return nullptr; }

  void setUseAssemblerInfoForParsing(bool v) { UseAssemblerInfoForParsing = v; }
  bool getUseAssemblerInfoForParsing() { return UseAssemblerInfoForParsing; }

  MCTargetStreamer *getTargetStreamer() {
    return TargetStreamer.get();
  }

  void setAllowAutoPadding(bool v) { AllowAutoPadding = v; }
  bool getAllowAutoPadding() const { return AllowAutoPadding; }

  /// When emitting an object file, create and emit a real label. When emitting
  /// textual assembly, this should do nothing to avoid polluting our output.
  virtual MCSymbol *emitCFILabel();

  /// Retrieve the current frame info if one is available and it is not yet
  /// closed. Otherwise, issue an error and return null.
  WinEH::FrameInfo *EnsureValidWinFrameInfo(SMLoc Loc);

  unsigned getNumFrameInfos();
  ArrayRef<MCDwarfFrameInfo> getDwarfFrameInfos() const;

  bool hasUnfinishedDwarfFrameInfo();

  unsigned getNumWinFrameInfos() { return WinFrameInfos.size(); }
  ArrayRef<std::unique_ptr<WinEH::FrameInfo>> getWinFrameInfos() const {
    return WinFrameInfos;
  }

  void generateCompactUnwindEncodings(MCAsmBackend *MAB);

  /// \name Assembly File Formatting.
  /// @{

  /// Return true if this streamer supports verbose assembly and if it is
  /// enabled.
  virtual bool isVerboseAsm() const { return false; }

  /// Return true if this asm streamer supports emitting unformatted text
  /// to the .s file with EmitRawText.
  virtual bool hasRawTextSupport() const { return false; }

  /// Is the integrated assembler required for this streamer to function
  /// correctly?
  virtual bool isIntegratedAssemblerRequired() const { return false; }

  /// Add a textual comment.
  ///
  /// Typically for comments that can be emitted to the generated .s
  /// file if applicable as a QoI issue to make the output of the compiler
  /// more readable.  This only affects the MCAsmStreamer, and only when
  /// verbose assembly output is enabled.
  ///
  /// If the comment includes embedded \n's, they will each get the comment
  /// prefix as appropriate.  The added comment should not end with a \n.
  /// By default, each comment is terminated with an end of line, i.e. the
  /// EOL param is set to true by default. If one prefers not to end the
  /// comment with a new line then the EOL param should be passed
  /// with a false value.
  virtual void AddComment(const Twine &T, bool EOL = true) {}

  /// Return a raw_ostream that comments can be written to. Unlike
  /// AddComment, you are required to terminate comments with \n if you use this
  /// method.
  virtual raw_ostream &getCommentOS();

  /// Print T and prefix it with the comment string (normally #) and
  /// optionally a tab. This prints the comment immediately, not at the end of
  /// the current line. It is basically a safe version of EmitRawText: since it
  /// only prints comments, the object streamer ignores it instead of asserting.
  virtual void emitRawComment(const Twine &T, bool TabPrefix = true);

  /// Add explicit comment T. T is required to be a valid
  /// comment in the output and does not need to be escaped.
  virtual void addExplicitComment(const Twine &T);

  /// Emit added explicit comments.
  virtual void emitExplicitComments();

  /// Emit a blank line to a .s file to pretty it up.
  virtual void addBlankLine() {}

  /// @}

  /// \name Symbol & Section Management
  /// @{

  /// Return the current section that the streamer is emitting code to.
  MCSectionSubPair getCurrentSection() const {
    if (!SectionStack.empty())
      return SectionStack.back().first;
    return MCSectionSubPair();
  }
  MCSection *getCurrentSectionOnly() const {
    return CurFrag->getParent();
  }

  /// Return the previous section that the streamer is emitting code to.
  MCSectionSubPair getPreviousSection() const {
    if (!SectionStack.empty())
      return SectionStack.back().second;
    return MCSectionSubPair();
  }

  MCFragment *getCurrentFragment() const {
    assert(!getCurrentSection().first ||
           CurFrag->getParent() == getCurrentSection().first);
    return CurFrag;
  }

  /// Save the current and previous section on the section stack.
  void pushSection() {
    SectionStack.push_back(
        std::make_pair(getCurrentSection(), getPreviousSection()));
  }

  /// Restore the current and previous section from the section stack.
  /// Calls changeSection as needed.
  ///
  /// Returns false if the stack was empty.
  bool popSection();

  /// Set the current section where code is being emitted to \p Section.  This
  /// is required to update CurSection.
  ///
  /// This corresponds to assembler directives like .section, .text, etc.
  virtual void switchSection(MCSection *Section, uint32_t Subsec = 0);
  bool switchSection(MCSection *Section, const MCExpr *);

  /// Similar to switchSection, but does not print the section directive.
  virtual void switchSectionNoPrint(MCSection *Section);

  /// Create the default sections and set the initial one.
  virtual void initSections(bool NoExecStack, const MCSubtargetInfo &STI);

  MCSymbol *endSection(MCSection *Section);

  /// Returns the mnemonic for \p MI, if the streamer has access to a
  /// instruction printer and returns an empty string otherwise.
  virtual StringRef getMnemonic(MCInst &MI) { return ""; }

  /// Emit a label for \p Symbol into the current section.
  ///
  /// This corresponds to an assembler statement such as:
  ///   foo:
  ///
  /// \param Symbol - The symbol to emit. A given symbol should only be
  /// emitted as a label once, and symbols emitted as a label should never be
  /// used in an assignment.
  // FIXME: These emission are non-const because we mutate the symbol to
  // add the section we're emitting it to later.
  virtual void emitLabel(MCSymbol *Symbol, SMLoc Loc = SMLoc());

  virtual void emitEHSymAttributes(const MCSymbol *Symbol, MCSymbol *EHSymbol);

  /// Note in the output the specified \p Flag.
  virtual void emitAssemblerFlag(MCAssemblerFlag Flag);

  /// Emit the given list \p Options of strings as linker
  /// options into the output.
  virtual void emitLinkerOptions(ArrayRef<std::string> Kind) {}

  /// Note in the output the specified region \p Kind.
  virtual void emitDataRegion(MCDataRegionType Kind) {}

  /// Specify the Mach-O minimum deployment target version.
  virtual void emitVersionMin(MCVersionMinType Type, unsigned Major,
                              unsigned Minor, unsigned Update,
                              VersionTuple SDKVersion) {}

  /// Emit/Specify Mach-O build version command.
  /// \p Platform should be one of MachO::PlatformType.
  virtual void emitBuildVersion(unsigned Platform, unsigned Major,
                                unsigned Minor, unsigned Update,
                                VersionTuple SDKVersion) {}

  virtual void emitDarwinTargetVariantBuildVersion(unsigned Platform,
                                                   unsigned Major,
                                                   unsigned Minor,
                                                   unsigned Update,
                                                   VersionTuple SDKVersion) {}

  void emitVersionForTarget(const Triple &Target,
                            const VersionTuple &SDKVersion,
                            const Triple *DarwinTargetVariantTriple,
                            const VersionTuple &DarwinTargetVariantSDKVersion);

  /// Note in the output that the specified \p Func is a Thumb mode
  /// function (ARM target only).
  virtual void emitThumbFunc(MCSymbol *Func);

  /// Emit an assignment of \p Value to \p Symbol.
  ///
  /// This corresponds to an assembler statement such as:
  ///  symbol = value
  ///
  /// The assignment generates no code, but has the side effect of binding the
  /// value in the current context. For the assembly streamer, this prints the
  /// binding into the .s file.
  ///
  /// \param Symbol - The symbol being assigned to.
  /// \param Value - The value for the symbol.
  virtual void emitAssignment(MCSymbol *Symbol, const MCExpr *Value);

  /// Emit an assignment of \p Value to \p Symbol, but only if \p Value is also
  /// emitted.
  virtual void emitConditionalAssignment(MCSymbol *Symbol, const MCExpr *Value);

  /// Emit an weak reference from \p Alias to \p Symbol.
  ///
  /// This corresponds to an assembler statement such as:
  ///  .weakref alias, symbol
  ///
  /// \param Alias - The alias that is being created.
  /// \param Symbol - The symbol being aliased.
  virtual void emitWeakReference(MCSymbol *Alias, const MCSymbol *Symbol);

  /// Add the given \p Attribute to \p Symbol.
  virtual bool emitSymbolAttribute(MCSymbol *Symbol,
                                   MCSymbolAttr Attribute) = 0;

  /// Set the \p DescValue for the \p Symbol.
  ///
  /// \param Symbol - The symbol to have its n_desc field set.
  /// \param DescValue - The value to set into the n_desc field.
  virtual void emitSymbolDesc(MCSymbol *Symbol, unsigned DescValue);

  /// Start emitting COFF symbol definition
  ///
  /// \param Symbol - The symbol to have its External & Type fields set.
  virtual void beginCOFFSymbolDef(const MCSymbol *Symbol);

  /// Emit the storage class of the symbol.
  ///
  /// \param StorageClass - The storage class the symbol should have.
  virtual void emitCOFFSymbolStorageClass(int StorageClass);

  /// Emit the type of the symbol.
  ///
  /// \param Type - A COFF type identifier (see COFF::SymbolType in X86COFF.h)
  virtual void emitCOFFSymbolType(int Type);

  /// Marks the end of the symbol definition.
  virtual void endCOFFSymbolDef();

  virtual void emitCOFFSafeSEH(MCSymbol const *Symbol);

  /// Emits the symbol table index of a Symbol into the current section.
  virtual void emitCOFFSymbolIndex(MCSymbol const *Symbol);

  /// Emits a COFF section index.
  ///
  /// \param Symbol - Symbol the section number relocation should point to.
  virtual void emitCOFFSectionIndex(MCSymbol const *Symbol);

  /// Emits a COFF section relative relocation.
  ///
  /// \param Symbol - Symbol the section relative relocation should point to.
  virtual void emitCOFFSecRel32(MCSymbol const *Symbol, uint64_t Offset);

  /// Emits a COFF image relative relocation.
  ///
  /// \param Symbol - Symbol the image relative relocation should point to.
  virtual void emitCOFFImgRel32(MCSymbol const *Symbol, int64_t Offset);

  /// Emits an lcomm directive with XCOFF csect information.
  ///
  /// \param LabelSym - Label on the block of storage.
  /// \param Size - The size of the block of storage.
  /// \param CsectSym - Csect name for the block of storage.
  /// \param Alignment - The alignment of the symbol in bytes.
  virtual void emitXCOFFLocalCommonSymbol(MCSymbol *LabelSym, uint64_t Size,
                                          MCSymbol *CsectSym, Align Alignment);

  /// Emit a symbol's linkage and visibility with a linkage directive for XCOFF.
  ///
  /// \param Symbol - The symbol to emit.
  /// \param Linkage - The linkage of the symbol to emit.
  /// \param Visibility - The visibility of the symbol to emit or MCSA_Invalid
  /// if the symbol does not have an explicit visibility.
  virtual void emitXCOFFSymbolLinkageWithVisibility(MCSymbol *Symbol,
                                                    MCSymbolAttr Linkage,
                                                    MCSymbolAttr Visibility);

  /// Emit a XCOFF .rename directive which creates a synonym for an illegal or
  /// undesirable name.
  ///
  /// \param Name - The name used internally in the assembly for references to
  /// the symbol.
  /// \param Rename - The value to which the Name parameter is
  /// changed at the end of assembly.
  virtual void emitXCOFFRenameDirective(const MCSymbol *Name, StringRef Rename);

  /// Emit an XCOFF .except directive which adds information about
  /// a trap instruction to the object file exception section
  ///
  /// \param Symbol - The function containing the trap.
  /// \param Lang - The language code for the exception entry.
  /// \param Reason - The reason code for the exception entry.
  virtual void emitXCOFFExceptDirective(const MCSymbol *Symbol,
                                        const MCSymbol *Trap,
                                        unsigned Lang, unsigned Reason,
                                        unsigned FunctionSize, bool hasDebug);

  /// Emit a XCOFF .ref directive which creates R_REF type entry in the
  /// relocation table for one or more symbols.
  ///
  /// \param Sym - The symbol on the .ref directive.
  virtual void emitXCOFFRefDirective(const MCSymbol *Symbol);

  /// Emit a C_INFO symbol with XCOFF embedded metadata to the .info section.
  ///
  /// \param Name - The embedded metadata name
  /// \param Metadata - The embedded metadata
  virtual void emitXCOFFCInfoSym(StringRef Name, StringRef Metadata);

  /// Emit an ELF .size directive.
  ///
  /// This corresponds to an assembler statement such as:
  ///  .size symbol, expression
  virtual void emitELFSize(MCSymbol *Symbol, const MCExpr *Value);

  /// Emit an ELF .symver directive.
  ///
  /// This corresponds to an assembler statement such as:
  ///  .symver _start, foo@@SOME_VERSION
  virtual void emitELFSymverDirective(const MCSymbol *OriginalSym,
                                      StringRef Name, bool KeepOriginalSym);

  /// Emit a Linker Optimization Hint (LOH) directive.
  /// \param Args - Arguments of the LOH.
  virtual void emitLOHDirective(MCLOHType Kind, const MCLOHArgs &Args) {}

  /// Emit a .gnu_attribute directive.
  virtual void emitGNUAttribute(unsigned Tag, unsigned Value) {}

  /// Emit a common symbol.
  ///
  /// \param Symbol - The common symbol to emit.
  /// \param Size - The size of the common symbol.
  /// \param ByteAlignment - The alignment of the symbol.
  virtual void emitCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                                Align ByteAlignment) = 0;

  /// Emit a local common (.lcomm) symbol.
  ///
  /// \param Symbol - The common symbol to emit.
  /// \param Size - The size of the common symbol.
  /// \param ByteAlignment - The alignment of the common symbol in bytes.
  virtual void emitLocalCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                                     Align ByteAlignment);

  /// Emit the zerofill section and an optional symbol.
  ///
  /// \param Section - The zerofill section to create and or to put the symbol
  /// \param Symbol - The zerofill symbol to emit, if non-NULL.
  /// \param Size - The size of the zerofill symbol.
  /// \param ByteAlignment - The alignment of the zerofill symbol.
  virtual void emitZerofill(MCSection *Section, MCSymbol *Symbol = nullptr,
                            uint64_t Size = 0, Align ByteAlignment = Align(1),
                            SMLoc Loc = SMLoc()) = 0;

  /// Emit a thread local bss (.tbss) symbol.
  ///
  /// \param Section - The thread local common section.
  /// \param Symbol - The thread local common symbol to emit.
  /// \param Size - The size of the symbol.
  /// \param ByteAlignment - The alignment of the thread local common symbol.
  virtual void emitTBSSSymbol(MCSection *Section, MCSymbol *Symbol,
                              uint64_t Size, Align ByteAlignment = Align(1));

  /// @}
  /// \name Generating Data
  /// @{

  /// Emit the bytes in \p Data into the output.
  ///
  /// This is used to implement assembler directives such as .byte, .ascii,
  /// etc.
  virtual void emitBytes(StringRef Data);

  /// Functionally identical to EmitBytes. When emitting textual assembly, this
  /// method uses .byte directives instead of .ascii or .asciz for readability.
  virtual void emitBinaryData(StringRef Data);

  /// Emit the expression \p Value into the output as a native
  /// integer of the given \p Size bytes.
  ///
  /// This is used to implement assembler directives such as .word, .quad,
  /// etc.
  ///
  /// \param Value - The value to emit.
  /// \param Size - The size of the integer (in bytes) to emit. This must
  /// match a native machine width.
  /// \param Loc - The location of the expression for error reporting.
  virtual void emitValueImpl(const MCExpr *Value, unsigned Size,
                             SMLoc Loc = SMLoc());

  void emitValue(const MCExpr *Value, unsigned Size, SMLoc Loc = SMLoc());

  /// Special case of EmitValue that avoids the client having
  /// to pass in a MCExpr for constant integers.
  virtual void emitIntValue(uint64_t Value, unsigned Size);
  virtual void emitIntValue(const APInt &Value);

  /// Special case of EmitValue that avoids the client having to pass
  /// in a MCExpr for constant integers & prints in Hex format for certain
  /// modes.
  virtual void emitIntValueInHex(uint64_t Value, unsigned Size) {
    emitIntValue(Value, Size);
  }

  void emitInt8(uint64_t Value) { emitIntValue(Value, 1); }
  void emitInt16(uint64_t Value) { emitIntValue(Value, 2); }
  void emitInt32(uint64_t Value) { emitIntValue(Value, 4); }
  void emitInt64(uint64_t Value) { emitIntValue(Value, 8); }

  /// Special case of EmitValue that avoids the client having to pass
  /// in a MCExpr for constant integers & prints in Hex format for certain
  /// modes, pads the field with leading zeros to Size width
  virtual void emitIntValueInHexWithPadding(uint64_t Value, unsigned Size) {
    emitIntValue(Value, Size);
  }

  virtual void emitULEB128Value(const MCExpr *Value);

  virtual void emitSLEB128Value(const MCExpr *Value);

  /// Special case of EmitULEB128Value that avoids the client having to
  /// pass in a MCExpr for constant integers.
  unsigned emitULEB128IntValue(uint64_t Value, unsigned PadTo = 0);

  /// Special case of EmitSLEB128Value that avoids the client having to
  /// pass in a MCExpr for constant integers.
  unsigned emitSLEB128IntValue(int64_t Value);

  /// Special case of EmitValue that avoids the client having to pass in
  /// a MCExpr for MCSymbols.
  void emitSymbolValue(const MCSymbol *Sym, unsigned Size,
                       bool IsSectionRelative = false);

  /// Emit the expression \p Value into the output as a dtprel
  /// (64-bit DTP relative) value.
  ///
  /// This is used to implement assembler directives such as .dtpreldword on
  /// targets that support them.
  virtual void emitDTPRel64Value(const MCExpr *Value);

  /// Emit the expression \p Value into the output as a dtprel
  /// (32-bit DTP relative) value.
  ///
  /// This is used to implement assembler directives such as .dtprelword on
  /// targets that support them.
  virtual void emitDTPRel32Value(const MCExpr *Value);

  /// Emit the expression \p Value into the output as a tprel
  /// (64-bit TP relative) value.
  ///
  /// This is used to implement assembler directives such as .tpreldword on
  /// targets that support them.
  virtual void emitTPRel64Value(const MCExpr *Value);

  /// Emit the expression \p Value into the output as a tprel
  /// (32-bit TP relative) value.
  ///
  /// This is used to implement assembler directives such as .tprelword on
  /// targets that support them.
  virtual void emitTPRel32Value(const MCExpr *Value);

  /// Emit the expression \p Value into the output as a gprel64 (64-bit
  /// GP relative) value.
  ///
  /// This is used to implement assembler directives such as .gpdword on
  /// targets that support them.
  virtual void emitGPRel64Value(const MCExpr *Value);

  /// Emit the expression \p Value into the output as a gprel32 (32-bit
  /// GP relative) value.
  ///
  /// This is used to implement assembler directives such as .gprel32 on
  /// targets that support them.
  virtual void emitGPRel32Value(const MCExpr *Value);

  /// Emit NumBytes bytes worth of the value specified by FillValue.
  /// This implements directives such as '.space'.
  void emitFill(uint64_t NumBytes, uint8_t FillValue);

  /// Emit \p Size bytes worth of the value specified by \p FillValue.
  ///
  /// This is used to implement assembler directives such as .space or .skip.
  ///
  /// \param NumBytes - The number of bytes to emit.
  /// \param FillValue - The value to use when filling bytes.
  /// \param Loc - The location of the expression for error reporting.
  virtual void emitFill(const MCExpr &NumBytes, uint64_t FillValue,
                        SMLoc Loc = SMLoc());

  /// Emit \p NumValues copies of \p Size bytes. Each \p Size bytes is
  /// taken from the lowest order 4 bytes of \p Expr expression.
  ///
  /// This is used to implement assembler directives such as .fill.
  ///
  /// \param NumValues - The number of copies of \p Size bytes to emit.
  /// \param Size - The size (in bytes) of each repeated value.
  /// \param Expr - The expression from which \p Size bytes are used.
  virtual void emitFill(const MCExpr &NumValues, int64_t Size, int64_t Expr,
                        SMLoc Loc = SMLoc());

  virtual void emitNops(int64_t NumBytes, int64_t ControlledNopLength,
                        SMLoc Loc, const MCSubtargetInfo& STI);

  /// Emit NumBytes worth of zeros.
  /// This function properly handles data in virtual sections.
  void emitZeros(uint64_t NumBytes);

  /// Emit some number of copies of \p Value until the byte alignment \p
  /// ByteAlignment is reached.
  ///
  /// If the number of bytes need to emit for the alignment is not a multiple
  /// of \p ValueSize, then the contents of the emitted fill bytes is
  /// undefined.
  ///
  /// This used to implement the .align assembler directive.
  ///
  /// \param Alignment - The alignment to reach.
  /// \param Value - The value to use when filling bytes.
  /// \param ValueSize - The size of the integer (in bytes) to emit for
  /// \p Value. This must match a native machine width.
  /// \param MaxBytesToEmit - The maximum numbers of bytes to emit, or 0. If
  /// the alignment cannot be reached in this many bytes, no bytes are
  /// emitted.
  virtual void emitValueToAlignment(Align Alignment, int64_t Value = 0,
                                    unsigned ValueSize = 1,
                                    unsigned MaxBytesToEmit = 0);

  /// Emit nops until the byte alignment \p ByteAlignment is reached.
  ///
  /// This used to align code where the alignment bytes may be executed.  This
  /// can emit different bytes for different sizes to optimize execution.
  ///
  /// \param Alignment - The alignment to reach.
  /// \param STI - The MCSubtargetInfo in operation when padding is emitted.
  /// \param MaxBytesToEmit - The maximum numbers of bytes to emit, or 0. If
  /// the alignment cannot be reached in this many bytes, no bytes are
  /// emitted.
  virtual void emitCodeAlignment(Align Alignment, const MCSubtargetInfo *STI,
                                 unsigned MaxBytesToEmit = 0);

  /// Emit some number of copies of \p Value until the byte offset \p
  /// Offset is reached.
  ///
  /// This is used to implement assembler directives such as .org.
  ///
  /// \param Offset - The offset to reach. This may be an expression, but the
  /// expression must be associated with the current section.
  /// \param Value - The value to use when filling bytes.
  virtual void emitValueToOffset(const MCExpr *Offset, unsigned char Value,
                                 SMLoc Loc);

  /// @}

  /// Switch to a new logical file.  This is used to implement the '.file
  /// "foo.c"' assembler directive.
  virtual void emitFileDirective(StringRef Filename);

  /// Emit ".file assembler diretive with additioal info.
  virtual void emitFileDirective(StringRef Filename, StringRef CompilerVersion,
                                 StringRef TimeStamp, StringRef Description);

  /// Emit the "identifiers" directive.  This implements the
  /// '.ident "version foo"' assembler directive.
  virtual void emitIdent(StringRef IdentString) {}

  /// Associate a filename with a specified logical file number.  This
  /// implements the DWARF2 '.file 4 "foo.c"' assembler directive.
  unsigned emitDwarfFileDirective(
      unsigned FileNo, StringRef Directory, StringRef Filename,
      std::optional<MD5::MD5Result> Checksum = std::nullopt,
      std::optional<StringRef> Source = std::nullopt, unsigned CUID = 0) {
    return cantFail(
        tryEmitDwarfFileDirective(FileNo, Directory, Filename, Checksum,
                                  Source, CUID));
  }

  /// Associate a filename with a specified logical file number.
  /// Also associate a directory, optional checksum, and optional source
  /// text with the logical file.  This implements the DWARF2
  /// '.file 4 "dir/foo.c"' assembler directive, and the DWARF5
  /// '.file 4 "dir/foo.c" md5 "..." source "..."' assembler directive.
  virtual Expected<unsigned> tryEmitDwarfFileDirective(
      unsigned FileNo, StringRef Directory, StringRef Filename,
      std::optional<MD5::MD5Result> Checksum = std::nullopt,
      std::optional<StringRef> Source = std::nullopt, unsigned CUID = 0);

  /// Specify the "root" file of the compilation, using the ".file 0" extension.
  virtual void emitDwarfFile0Directive(StringRef Directory, StringRef Filename,
                                       std::optional<MD5::MD5Result> Checksum,
                                       std::optional<StringRef> Source,
                                       unsigned CUID = 0);

  virtual void emitCFIBKeyFrame();
  virtual void emitCFIMTETaggedFrame();

  /// This implements the DWARF2 '.loc fileno lineno ...' assembler
  /// directive.
  virtual void emitDwarfLocDirective(unsigned FileNo, unsigned Line,
                                     unsigned Column, unsigned Flags,
                                     unsigned Isa, unsigned Discriminator,
                                     StringRef FileName);

  /// Associate a filename with a specified logical file number, and also
  /// specify that file's checksum information.  This implements the '.cv_file 4
  /// "foo.c"' assembler directive. Returns true on success.
  virtual bool emitCVFileDirective(unsigned FileNo, StringRef Filename,
                                   ArrayRef<uint8_t> Checksum,
                                   unsigned ChecksumKind);

  /// Introduces a function id for use with .cv_loc.
  virtual bool emitCVFuncIdDirective(unsigned FunctionId);

  /// Introduces an inline call site id for use with .cv_loc. Includes
  /// extra information for inline line table generation.
  virtual bool emitCVInlineSiteIdDirective(unsigned FunctionId, unsigned IAFunc,
                                           unsigned IAFile, unsigned IALine,
                                           unsigned IACol, SMLoc Loc);

  /// This implements the CodeView '.cv_loc' assembler directive.
  virtual void emitCVLocDirective(unsigned FunctionId, unsigned FileNo,
                                  unsigned Line, unsigned Column,
                                  bool PrologueEnd, bool IsStmt,
                                  StringRef FileName, SMLoc Loc);

  /// This implements the CodeView '.cv_linetable' assembler directive.
  virtual void emitCVLinetableDirective(unsigned FunctionId,
                                        const MCSymbol *FnStart,
                                        const MCSymbol *FnEnd);

  /// This implements the CodeView '.cv_inline_linetable' assembler
  /// directive.
  virtual void emitCVInlineLinetableDirective(unsigned PrimaryFunctionId,
                                              unsigned SourceFileId,
                                              unsigned SourceLineNum,
                                              const MCSymbol *FnStartSym,
                                              const MCSymbol *FnEndSym);

  /// This implements the CodeView '.cv_def_range' assembler
  /// directive.
  virtual void emitCVDefRangeDirective(
      ArrayRef<std::pair<const MCSymbol *, const MCSymbol *>> Ranges,
      StringRef FixedSizePortion);

  virtual void emitCVDefRangeDirective(
      ArrayRef<std::pair<const MCSymbol *, const MCSymbol *>> Ranges,
      codeview::DefRangeRegisterRelHeader DRHdr);

  virtual void emitCVDefRangeDirective(
      ArrayRef<std::pair<const MCSymbol *, const MCSymbol *>> Ranges,
      codeview::DefRangeSubfieldRegisterHeader DRHdr);

  virtual void emitCVDefRangeDirective(
      ArrayRef<std::pair<const MCSymbol *, const MCSymbol *>> Ranges,
      codeview::DefRangeRegisterHeader DRHdr);

  virtual void emitCVDefRangeDirective(
      ArrayRef<std::pair<const MCSymbol *, const MCSymbol *>> Ranges,
      codeview::DefRangeFramePointerRelHeader DRHdr);

  /// This implements the CodeView '.cv_stringtable' assembler directive.
  virtual void emitCVStringTableDirective() {}

  /// This implements the CodeView '.cv_filechecksums' assembler directive.
  virtual void emitCVFileChecksumsDirective() {}

  /// This implements the CodeView '.cv_filechecksumoffset' assembler
  /// directive.
  virtual void emitCVFileChecksumOffsetDirective(unsigned FileNo) {}

  /// This implements the CodeView '.cv_fpo_data' assembler directive.
  virtual void emitCVFPOData(const MCSymbol *ProcSym, SMLoc Loc = {}) {}

  /// Emit the absolute difference between two symbols.
  ///
  /// \pre Offset of \c Hi is greater than the offset \c Lo.
  virtual void emitAbsoluteSymbolDiff(const MCSymbol *Hi, const MCSymbol *Lo,
                                      unsigned Size);

  /// Emit the absolute difference between two symbols encoded with ULEB128.
  virtual void emitAbsoluteSymbolDiffAsULEB128(const MCSymbol *Hi,
                                               const MCSymbol *Lo);

  virtual MCSymbol *getDwarfLineTableSymbol(unsigned CUID);
  virtual void emitCFISections(bool EH, bool Debug);
  void emitCFIStartProc(bool IsSimple, SMLoc Loc = SMLoc());
  void emitCFIEndProc();
  virtual void emitCFIDefCfa(int64_t Register, int64_t Offset, SMLoc Loc = {});
  virtual void emitCFIDefCfaOffset(int64_t Offset, SMLoc Loc = {});
  virtual void emitCFIDefCfaRegister(int64_t Register, SMLoc Loc = {});
  virtual void emitCFILLVMDefAspaceCfa(int64_t Register, int64_t Offset,
                                       int64_t AddressSpace, SMLoc Loc = {});
  virtual void emitCFIOffset(int64_t Register, int64_t Offset, SMLoc Loc = {});
  virtual void emitCFIPersonality(const MCSymbol *Sym, unsigned Encoding);
  virtual void emitCFILsda(const MCSymbol *Sym, unsigned Encoding);
  virtual void emitCFIRememberState(SMLoc Loc);
  virtual void emitCFIRestoreState(SMLoc Loc);
  virtual void emitCFISameValue(int64_t Register, SMLoc Loc = {});
  virtual void emitCFIRestore(int64_t Register, SMLoc Loc = {});
  virtual void emitCFIRelOffset(int64_t Register, int64_t Offset, SMLoc Loc);
  virtual void emitCFIAdjustCfaOffset(int64_t Adjustment, SMLoc Loc = {});
  virtual void emitCFIEscape(StringRef Values, SMLoc Loc = {});
  virtual void emitCFIReturnColumn(int64_t Register);
  virtual void emitCFIGnuArgsSize(int64_t Size, SMLoc Loc = {});
  virtual void emitCFISignalFrame();
  virtual void emitCFIUndefined(int64_t Register, SMLoc Loc = {});
  virtual void emitCFIRegister(int64_t Register1, int64_t Register2,
                               SMLoc Loc = {});
  virtual void emitCFIWindowSave(SMLoc Loc = {});
  virtual void emitCFINegateRAState(SMLoc Loc = {});
  virtual void emitCFILabelDirective(SMLoc Loc, StringRef Name);

  virtual void emitWinCFIStartProc(const MCSymbol *Symbol, SMLoc Loc = SMLoc());
  virtual void emitWinCFIEndProc(SMLoc Loc = SMLoc());
  /// This is used on platforms, such as Windows on ARM64, that require function
  /// or funclet sizes to be emitted in .xdata before the End marker is emitted
  /// for the frame.  We cannot use the End marker, as it is not set at the
  /// point of emitting .xdata, in order to indicate that the frame is active.
  virtual void emitWinCFIFuncletOrFuncEnd(SMLoc Loc = SMLoc());
  virtual void emitWinCFIStartChained(SMLoc Loc = SMLoc());
  virtual void emitWinCFIEndChained(SMLoc Loc = SMLoc());
  virtual void emitWinCFIPushReg(MCRegister Register, SMLoc Loc = SMLoc());
  virtual void emitWinCFISetFrame(MCRegister Register, unsigned Offset,
                                  SMLoc Loc = SMLoc());
  virtual void emitWinCFIAllocStack(unsigned Size, SMLoc Loc = SMLoc());
  virtual void emitWinCFISaveReg(MCRegister Register, unsigned Offset,
                                 SMLoc Loc = SMLoc());
  virtual void emitWinCFISaveXMM(MCRegister Register, unsigned Offset,
                                 SMLoc Loc = SMLoc());
  virtual void emitWinCFIPushFrame(bool Code, SMLoc Loc = SMLoc());
  virtual void emitWinCFIEndProlog(SMLoc Loc = SMLoc());
  virtual void emitWinEHHandler(const MCSymbol *Sym, bool Unwind, bool Except,
                                SMLoc Loc = SMLoc());
  virtual void emitWinEHHandlerData(SMLoc Loc = SMLoc());

  virtual void emitCGProfileEntry(const MCSymbolRefExpr *From,
                                  const MCSymbolRefExpr *To, uint64_t Count);

  /// Get the .pdata section used for the given section. Typically the given
  /// section is either the main .text section or some other COMDAT .text
  /// section, but it may be any section containing code.
  MCSection *getAssociatedPDataSection(const MCSection *TextSec);

  /// Get the .xdata section used for the given section.
  MCSection *getAssociatedXDataSection(const MCSection *TextSec);

  virtual void emitSyntaxDirective();

  /// Record a relocation described by the .reloc directive. Return std::nullopt
  /// if succeeded. Otherwise, return a pair (Name is invalid, error message).
  virtual std::optional<std::pair<bool, std::string>>
  emitRelocDirective(const MCExpr &Offset, StringRef Name, const MCExpr *Expr,
                     SMLoc Loc, const MCSubtargetInfo &STI) {
    return std::nullopt;
  }

  virtual void emitAddrsig() {}
  virtual void emitAddrsigSym(const MCSymbol *Sym) {}

  /// Emit the given \p Instruction into the current section.
  virtual void emitInstruction(const MCInst &Inst, const MCSubtargetInfo &STI);

  /// Emit the a pseudo probe into the current section.
  virtual void emitPseudoProbe(uint64_t Guid, uint64_t Index, uint64_t Type,
                               uint64_t Attr, uint64_t Discriminator,
                               const MCPseudoProbeInlineStack &InlineStack,
                               MCSymbol *FnSym);

  /// Set the bundle alignment mode from now on in the section.
  /// The value 1 means turn the bundle alignment off.
  virtual void emitBundleAlignMode(Align Alignment);

  /// The following instructions are a bundle-locked group.
  ///
  /// \param AlignToEnd - If true, the bundle-locked group will be aligned to
  ///                     the end of a bundle.
  virtual void emitBundleLock(bool AlignToEnd);

  /// Ends a bundle-locked group.
  virtual void emitBundleUnlock();

  /// If this file is backed by a assembly streamer, this dumps the
  /// specified string in the output .s file.  This capability is indicated by
  /// the hasRawTextSupport() predicate.  By default this aborts.
  void emitRawText(const Twine &String);

  /// Streamer specific finalization.
  virtual void finishImpl();
  /// Finish emission of machine code.
  void finish(SMLoc EndLoc = SMLoc());

  virtual bool mayHaveInstructions(MCSection &Sec) const { return true; }

  /// Emit a special value of 0xffffffff if producing 64-bit debugging info.
  void maybeEmitDwarf64Mark();

  /// Emit a unit length field. The actual format, DWARF32 or DWARF64, is chosen
  /// according to the settings.
  virtual void emitDwarfUnitLength(uint64_t Length, const Twine &Comment);

  /// Emit a unit length field. The actual format, DWARF32 or DWARF64, is chosen
  /// according to the settings.
  /// Return the end symbol generated inside, the caller needs to emit it.
  virtual MCSymbol *emitDwarfUnitLength(const Twine &Prefix,
                                        const Twine &Comment);

  /// Emit the debug line start label.
  virtual void emitDwarfLineStartLabel(MCSymbol *StartSym);

  /// Emit the debug line end entry.
  virtual void emitDwarfLineEndEntry(MCSection *Section, MCSymbol *LastLabel) {}

  /// If targets does not support representing debug line section by .loc/.file
  /// directives in assembly output, we need to populate debug line section with
  /// raw debug line contents.
  virtual void emitDwarfAdvanceLineAddr(int64_t LineDelta,
                                        const MCSymbol *LastLabel,
                                        const MCSymbol *Label,
                                        unsigned PointerSize) {}

  /// Do finalization for the streamer at the end of a section.
  virtual void doFinalizationAtSectionEnd(MCSection *Section) {}
};

/// Create a dummy machine code streamer, which does nothing. This is useful for
/// timing the assembler front end.
MCStreamer *createNullStreamer(MCContext &Ctx);

} // end namespace llvm

#endif // LLVM_MC_MCSTREAMER_H
