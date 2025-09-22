//===- lib/MC/ARMELFStreamer.cpp - ELF Object Output for ARM --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file assembles .s files and emits ARM ELF .o object files. Different
// from generic ELF streamer in emitting mapping symbols ($a, $t and $d) to
// delimit regions of data and code.
//
//===----------------------------------------------------------------------===//

#include "ARMMCTargetDesc.h"
#include "ARMUnwindOpAsm.h"
#include "Utils/ARMBaseInfo.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFStreamer.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCFragment.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCSymbolELF.h"
#include "llvm/MC/SectionKind.h"
#include "llvm/Support/ARMBuildAttributes.h"
#include "llvm/Support/ARMEHABI.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <string>

using namespace llvm;

static std::string GetAEABIUnwindPersonalityName(unsigned Index) {
  assert(Index < ARM::EHABI::NUM_PERSONALITY_INDEX &&
         "Invalid personality index");
  return (Twine("__aeabi_unwind_cpp_pr") + Twine(Index)).str();
}

namespace {

class ARMELFStreamer;

class ARMTargetAsmStreamer : public ARMTargetStreamer {
  formatted_raw_ostream &OS;
  MCInstPrinter &InstPrinter;
  bool IsVerboseAsm;

  void emitFnStart() override;
  void emitFnEnd() override;
  void emitCantUnwind() override;
  void emitPersonality(const MCSymbol *Personality) override;
  void emitPersonalityIndex(unsigned Index) override;
  void emitHandlerData() override;
  void emitSetFP(unsigned FpReg, unsigned SpReg, int64_t Offset = 0) override;
  void emitMovSP(unsigned Reg, int64_t Offset = 0) override;
  void emitPad(int64_t Offset) override;
  void emitRegSave(const SmallVectorImpl<unsigned> &RegList,
                   bool isVector) override;
  void emitUnwindRaw(int64_t Offset,
                     const SmallVectorImpl<uint8_t> &Opcodes) override;

  void switchVendor(StringRef Vendor) override;
  void emitAttribute(unsigned Attribute, unsigned Value) override;
  void emitTextAttribute(unsigned Attribute, StringRef String) override;
  void emitIntTextAttribute(unsigned Attribute, unsigned IntValue,
                            StringRef StringValue) override;
  void emitArch(ARM::ArchKind Arch) override;
  void emitArchExtension(uint64_t ArchExt) override;
  void emitObjectArch(ARM::ArchKind Arch) override;
  void emitFPU(ARM::FPUKind FPU) override;
  void emitInst(uint32_t Inst, char Suffix = '\0') override;
  void finishAttributeSection() override;

  void annotateTLSDescriptorSequence(const MCSymbolRefExpr *SRE) override;
  void emitThumbSet(MCSymbol *Symbol, const MCExpr *Value) override;

  void emitARMWinCFIAllocStack(unsigned Size, bool Wide) override;
  void emitARMWinCFISaveRegMask(unsigned Mask, bool Wide) override;
  void emitARMWinCFISaveSP(unsigned Reg) override;
  void emitARMWinCFISaveFRegs(unsigned First, unsigned Last) override;
  void emitARMWinCFISaveLR(unsigned Offset) override;
  void emitARMWinCFIPrologEnd(bool Fragment) override;
  void emitARMWinCFINop(bool Wide) override;
  void emitARMWinCFIEpilogStart(unsigned Condition) override;
  void emitARMWinCFIEpilogEnd() override;
  void emitARMWinCFICustom(unsigned Opcode) override;

public:
  ARMTargetAsmStreamer(MCStreamer &S, formatted_raw_ostream &OS,
                       MCInstPrinter &InstPrinter);
};

ARMTargetAsmStreamer::ARMTargetAsmStreamer(MCStreamer &S,
                                           formatted_raw_ostream &OS,
                                           MCInstPrinter &InstPrinter)
    : ARMTargetStreamer(S), OS(OS), InstPrinter(InstPrinter),
      IsVerboseAsm(S.isVerboseAsm()) {}

void ARMTargetAsmStreamer::emitFnStart() { OS << "\t.fnstart\n"; }
void ARMTargetAsmStreamer::emitFnEnd() { OS << "\t.fnend\n"; }
void ARMTargetAsmStreamer::emitCantUnwind() { OS << "\t.cantunwind\n"; }

void ARMTargetAsmStreamer::emitPersonality(const MCSymbol *Personality) {
  OS << "\t.personality " << Personality->getName() << '\n';
}

void ARMTargetAsmStreamer::emitPersonalityIndex(unsigned Index) {
  OS << "\t.personalityindex " << Index << '\n';
}

void ARMTargetAsmStreamer::emitHandlerData() { OS << "\t.handlerdata\n"; }

void ARMTargetAsmStreamer::emitSetFP(unsigned FpReg, unsigned SpReg,
                                     int64_t Offset) {
  OS << "\t.setfp\t";
  InstPrinter.printRegName(OS, FpReg);
  OS << ", ";
  InstPrinter.printRegName(OS, SpReg);
  if (Offset)
    OS << ", #" << Offset;
  OS << '\n';
}

void ARMTargetAsmStreamer::emitMovSP(unsigned Reg, int64_t Offset) {
  assert((Reg != ARM::SP && Reg != ARM::PC) &&
         "the operand of .movsp cannot be either sp or pc");

  OS << "\t.movsp\t";
  InstPrinter.printRegName(OS, Reg);
  if (Offset)
    OS << ", #" << Offset;
  OS << '\n';
}

void ARMTargetAsmStreamer::emitPad(int64_t Offset) {
  OS << "\t.pad\t#" << Offset << '\n';
}

void ARMTargetAsmStreamer::emitRegSave(const SmallVectorImpl<unsigned> &RegList,
                                       bool isVector) {
  assert(RegList.size() && "RegList should not be empty");
  if (isVector)
    OS << "\t.vsave\t{";
  else
    OS << "\t.save\t{";

  InstPrinter.printRegName(OS, RegList[0]);

  for (unsigned i = 1, e = RegList.size(); i != e; ++i) {
    OS << ", ";
    InstPrinter.printRegName(OS, RegList[i]);
  }

  OS << "}\n";
}

void ARMTargetAsmStreamer::switchVendor(StringRef Vendor) {}

void ARMTargetAsmStreamer::emitAttribute(unsigned Attribute, unsigned Value) {
  OS << "\t.eabi_attribute\t" << Attribute << ", " << Twine(Value);
  if (IsVerboseAsm) {
    StringRef Name = ELFAttrs::attrTypeAsString(
        Attribute, ARMBuildAttrs::getARMAttributeTags());
    if (!Name.empty())
      OS << "\t@ " << Name;
  }
  OS << "\n";
}

void ARMTargetAsmStreamer::emitTextAttribute(unsigned Attribute,
                                             StringRef String) {
  switch (Attribute) {
  case ARMBuildAttrs::CPU_name:
    OS << "\t.cpu\t" << String.lower();
    break;
  default:
    OS << "\t.eabi_attribute\t" << Attribute << ", \"";
    if (Attribute == ARMBuildAttrs::also_compatible_with)
      OS.write_escaped(String);
    else
      OS << String;
    OS << "\"";
    if (IsVerboseAsm) {
      StringRef Name = ELFAttrs::attrTypeAsString(
          Attribute, ARMBuildAttrs::getARMAttributeTags());
      if (!Name.empty())
        OS << "\t@ " << Name;
    }
    break;
  }
  OS << "\n";
}

void ARMTargetAsmStreamer::emitIntTextAttribute(unsigned Attribute,
                                                unsigned IntValue,
                                                StringRef StringValue) {
  switch (Attribute) {
  default: llvm_unreachable("unsupported multi-value attribute in asm mode");
  case ARMBuildAttrs::compatibility:
    OS << "\t.eabi_attribute\t" << Attribute << ", " << IntValue;
    if (!StringValue.empty())
      OS << ", \"" << StringValue << "\"";
    if (IsVerboseAsm)
      OS << "\t@ "
         << ELFAttrs::attrTypeAsString(Attribute,
                                       ARMBuildAttrs::getARMAttributeTags());
    break;
  }
  OS << "\n";
}

void ARMTargetAsmStreamer::emitArch(ARM::ArchKind Arch) {
  OS << "\t.arch\t" << ARM::getArchName(Arch) << "\n";
}

void ARMTargetAsmStreamer::emitArchExtension(uint64_t ArchExt) {
  OS << "\t.arch_extension\t" << ARM::getArchExtName(ArchExt) << "\n";
}

void ARMTargetAsmStreamer::emitObjectArch(ARM::ArchKind Arch) {
  OS << "\t.object_arch\t" << ARM::getArchName(Arch) << '\n';
}

void ARMTargetAsmStreamer::emitFPU(ARM::FPUKind FPU) {
  OS << "\t.fpu\t" << ARM::getFPUName(FPU) << "\n";
}

void ARMTargetAsmStreamer::finishAttributeSection() {}

void ARMTargetAsmStreamer::annotateTLSDescriptorSequence(
    const MCSymbolRefExpr *S) {
  OS << "\t.tlsdescseq\t" << S->getSymbol().getName() << "\n";
}

void ARMTargetAsmStreamer::emitThumbSet(MCSymbol *Symbol, const MCExpr *Value) {
  const MCAsmInfo *MAI = Streamer.getContext().getAsmInfo();

  OS << "\t.thumb_set\t";
  Symbol->print(OS, MAI);
  OS << ", ";
  Value->print(OS, MAI);
  OS << '\n';
}

void ARMTargetAsmStreamer::emitInst(uint32_t Inst, char Suffix) {
  OS << "\t.inst";
  if (Suffix)
    OS << "." << Suffix;
  OS << "\t0x" << Twine::utohexstr(Inst) << "\n";
}

void ARMTargetAsmStreamer::emitUnwindRaw(int64_t Offset,
                                      const SmallVectorImpl<uint8_t> &Opcodes) {
  OS << "\t.unwind_raw " << Offset;
  for (uint8_t Opcode : Opcodes)
    OS << ", 0x" << Twine::utohexstr(Opcode);
  OS << '\n';
}

void ARMTargetAsmStreamer::emitARMWinCFIAllocStack(unsigned Size, bool Wide) {
  if (Wide)
    OS << "\t.seh_stackalloc_w\t" << Size << "\n";
  else
    OS << "\t.seh_stackalloc\t" << Size << "\n";
}

static void printRegs(formatted_raw_ostream &OS, ListSeparator &LS, int First,
                      int Last) {
  if (First != Last)
    OS << LS << "r" << First << "-r" << Last;
  else
    OS << LS << "r" << First;
}

void ARMTargetAsmStreamer::emitARMWinCFISaveRegMask(unsigned Mask, bool Wide) {
  if (Wide)
    OS << "\t.seh_save_regs_w\t";
  else
    OS << "\t.seh_save_regs\t";
  ListSeparator LS;
  int First = -1;
  OS << "{";
  for (int I = 0; I <= 12; I++) {
    if (Mask & (1 << I)) {
      if (First < 0)
        First = I;
    } else {
      if (First >= 0) {
        printRegs(OS, LS, First, I - 1);
        First = -1;
      }
    }
  }
  if (First >= 0)
    printRegs(OS, LS, First, 12);
  if (Mask & (1 << 14))
    OS << LS << "lr";
  OS << "}\n";
}

void ARMTargetAsmStreamer::emitARMWinCFISaveSP(unsigned Reg) {
  OS << "\t.seh_save_sp\tr" << Reg << "\n";
}

void ARMTargetAsmStreamer::emitARMWinCFISaveFRegs(unsigned First,
                                                  unsigned Last) {
  if (First != Last)
    OS << "\t.seh_save_fregs\t{d" << First << "-d" << Last << "}\n";
  else
    OS << "\t.seh_save_fregs\t{d" << First << "}\n";
}

void ARMTargetAsmStreamer::emitARMWinCFISaveLR(unsigned Offset) {
  OS << "\t.seh_save_lr\t" << Offset << "\n";
}

void ARMTargetAsmStreamer::emitARMWinCFIPrologEnd(bool Fragment) {
  if (Fragment)
    OS << "\t.seh_endprologue_fragment\n";
  else
    OS << "\t.seh_endprologue\n";
}

void ARMTargetAsmStreamer::emitARMWinCFINop(bool Wide) {
  if (Wide)
    OS << "\t.seh_nop_w\n";
  else
    OS << "\t.seh_nop\n";
}

void ARMTargetAsmStreamer::emitARMWinCFIEpilogStart(unsigned Condition) {
  if (Condition == ARMCC::AL)
    OS << "\t.seh_startepilogue\n";
  else
    OS << "\t.seh_startepilogue_cond\t"
       << ARMCondCodeToString(static_cast<ARMCC::CondCodes>(Condition)) << "\n";
}

void ARMTargetAsmStreamer::emitARMWinCFIEpilogEnd() {
  OS << "\t.seh_endepilogue\n";
}

void ARMTargetAsmStreamer::emitARMWinCFICustom(unsigned Opcode) {
  int I;
  for (I = 3; I > 0; I--)
    if (Opcode & (0xffu << (8 * I)))
      break;
  ListSeparator LS;
  OS << "\t.seh_custom\t";
  for (; I >= 0; I--)
    OS << LS << ((Opcode >> (8 * I)) & 0xff);
  OS << "\n";
}

class ARMTargetELFStreamer : public ARMTargetStreamer {
private:
  StringRef CurrentVendor;
  ARM::FPUKind FPU = ARM::FK_INVALID;
  ARM::ArchKind Arch = ARM::ArchKind::INVALID;
  ARM::ArchKind EmittedArch = ARM::ArchKind::INVALID;

  MCSection *AttributeSection = nullptr;

  void emitArchDefaultAttributes();
  void emitFPUDefaultAttributes();

  ARMELFStreamer &getStreamer();

  void emitFnStart() override;
  void emitFnEnd() override;
  void emitCantUnwind() override;
  void emitPersonality(const MCSymbol *Personality) override;
  void emitPersonalityIndex(unsigned Index) override;
  void emitHandlerData() override;
  void emitSetFP(unsigned FpReg, unsigned SpReg, int64_t Offset = 0) override;
  void emitMovSP(unsigned Reg, int64_t Offset = 0) override;
  void emitPad(int64_t Offset) override;
  void emitRegSave(const SmallVectorImpl<unsigned> &RegList,
                   bool isVector) override;
  void emitUnwindRaw(int64_t Offset,
                     const SmallVectorImpl<uint8_t> &Opcodes) override;

  void switchVendor(StringRef Vendor) override;
  void emitAttribute(unsigned Attribute, unsigned Value) override;
  void emitTextAttribute(unsigned Attribute, StringRef String) override;
  void emitIntTextAttribute(unsigned Attribute, unsigned IntValue,
                            StringRef StringValue) override;
  void emitArch(ARM::ArchKind Arch) override;
  void emitObjectArch(ARM::ArchKind Arch) override;
  void emitFPU(ARM::FPUKind FPU) override;
  void emitInst(uint32_t Inst, char Suffix = '\0') override;
  void finishAttributeSection() override;
  void emitLabel(MCSymbol *Symbol) override;

  void annotateTLSDescriptorSequence(const MCSymbolRefExpr *SRE) override;
  void emitThumbSet(MCSymbol *Symbol, const MCExpr *Value) override;

  // Reset state between object emissions
  void reset() override;

  void finish() override;

public:
  ARMTargetELFStreamer(MCStreamer &S)
    : ARMTargetStreamer(S), CurrentVendor("aeabi") {}
};

/// Extend the generic ELFStreamer class so that it can emit mapping symbols at
/// the appropriate points in the object files. These symbols are defined in the
/// ARM ELF ABI: infocenter.arm.com/help/topic/com.arm.../IHI0044D_aaelf.pdf.
///
/// In brief: $a, $t or $d should be emitted at the start of each contiguous
/// region of ARM code, Thumb code or data in a section. In practice, this
/// emission does not rely on explicit assembler directives but on inherent
/// properties of the directives doing the emission (e.g. ".byte" is data, "add
/// r0, r0, r0" an instruction).
///
/// As a result this system is orthogonal to the DataRegion infrastructure used
/// by MachO. Beware!
class ARMELFStreamer : public MCELFStreamer {
public:
  friend class ARMTargetELFStreamer;

  ARMELFStreamer(MCContext &Context, std::unique_ptr<MCAsmBackend> TAB,
                 std::unique_ptr<MCObjectWriter> OW,
                 std::unique_ptr<MCCodeEmitter> Emitter, bool IsThumb,
                 bool IsAndroid)
      : MCELFStreamer(Context, std::move(TAB), std::move(OW),
                      std::move(Emitter)),
        IsThumb(IsThumb), IsAndroid(IsAndroid) {
    EHReset();
  }

  ~ARMELFStreamer() override = default;

  // ARM exception handling directives
  void emitFnStart();
  void emitFnEnd();
  void emitCantUnwind();
  void emitPersonality(const MCSymbol *Per);
  void emitPersonalityIndex(unsigned index);
  void emitHandlerData();
  void emitSetFP(unsigned NewFpReg, unsigned NewSpReg, int64_t Offset = 0);
  void emitMovSP(unsigned Reg, int64_t Offset = 0);
  void emitPad(int64_t Offset);
  void emitRegSave(const SmallVectorImpl<unsigned> &RegList, bool isVector);
  void emitUnwindRaw(int64_t Offset, const SmallVectorImpl<uint8_t> &Opcodes);
  void emitFill(const MCExpr &NumBytes, uint64_t FillValue,
                SMLoc Loc) override {
    emitDataMappingSymbol();
    MCObjectStreamer::emitFill(NumBytes, FillValue, Loc);
  }

  void changeSection(MCSection *Section, uint32_t Subsection) override {
    LastMappingSymbols[getCurrentSection().first] = std::move(LastEMSInfo);
    MCELFStreamer::changeSection(Section, Subsection);
    auto LastMappingSymbol = LastMappingSymbols.find(Section);
    if (LastMappingSymbol != LastMappingSymbols.end()) {
      LastEMSInfo = std::move(LastMappingSymbol->second);
      return;
    }
    LastEMSInfo.reset(new ElfMappingSymbolInfo);
  }

  /// This function is the one used to emit instruction data into the ELF
  /// streamer. We override it to add the appropriate mapping symbol if
  /// necessary.
  void emitInstruction(const MCInst &Inst,
                       const MCSubtargetInfo &STI) override {
    if (IsThumb)
      EmitThumbMappingSymbol();
    else
      EmitARMMappingSymbol();

    MCELFStreamer::emitInstruction(Inst, STI);
  }

  void emitInst(uint32_t Inst, char Suffix) {
    unsigned Size;
    char Buffer[4];
    const bool LittleEndian = getContext().getAsmInfo()->isLittleEndian();

    switch (Suffix) {
    case '\0':
      Size = 4;

      assert(!IsThumb);
      EmitARMMappingSymbol();
      for (unsigned II = 0, IE = Size; II != IE; II++) {
        const unsigned I = LittleEndian ? (Size - II - 1) : II;
        Buffer[Size - II - 1] = uint8_t(Inst >> I * CHAR_BIT);
      }

      break;
    case 'n':
    case 'w':
      Size = (Suffix == 'n' ? 2 : 4);

      assert(IsThumb);
      EmitThumbMappingSymbol();
      // Thumb wide instructions are emitted as a pair of 16-bit words of the
      // appropriate endianness.
      for (unsigned II = 0, IE = Size; II != IE; II = II + 2) {
        const unsigned I0 = LittleEndian ? II + 0 : II + 1;
        const unsigned I1 = LittleEndian ? II + 1 : II + 0;
        Buffer[Size - II - 2] = uint8_t(Inst >> I0 * CHAR_BIT);
        Buffer[Size - II - 1] = uint8_t(Inst >> I1 * CHAR_BIT);
      }

      break;
    default:
      llvm_unreachable("Invalid Suffix");
    }

    MCELFStreamer::emitBytes(StringRef(Buffer, Size));
  }

  /// This is one of the functions used to emit data into an ELF section, so the
  /// ARM streamer overrides it to add the appropriate mapping symbol ($d) if
  /// necessary.
  void emitBytes(StringRef Data) override {
    emitDataMappingSymbol();
    MCELFStreamer::emitBytes(Data);
  }

  void FlushPendingMappingSymbol() {
    if (!LastEMSInfo->hasInfo())
      return;
    ElfMappingSymbolInfo *EMS = LastEMSInfo.get();
    emitMappingSymbol("$d", *EMS->F, EMS->Offset);
    EMS->resetInfo();
  }

  /// This is one of the functions used to emit data into an ELF section, so the
  /// ARM streamer overrides it to add the appropriate mapping symbol ($d) if
  /// necessary.
  void emitValueImpl(const MCExpr *Value, unsigned Size, SMLoc Loc) override {
    if (const MCSymbolRefExpr *SRE = dyn_cast_or_null<MCSymbolRefExpr>(Value)) {
      if (SRE->getKind() == MCSymbolRefExpr::VK_ARM_SBREL && !(Size == 4)) {
        getContext().reportError(Loc, "relocated expression must be 32-bit");
        return;
      }
      getOrCreateDataFragment();
    }

    emitDataMappingSymbol();
    MCELFStreamer::emitValueImpl(Value, Size, Loc);
  }

  void emitAssemblerFlag(MCAssemblerFlag Flag) override {
    MCELFStreamer::emitAssemblerFlag(Flag);

    switch (Flag) {
    case MCAF_SyntaxUnified:
      return; // no-op here.
    case MCAF_Code16:
      IsThumb = true;
      return; // Change to Thumb mode
    case MCAF_Code32:
      IsThumb = false;
      return; // Change to ARM mode
    case MCAF_Code64:
      return;
    case MCAF_SubsectionsViaSymbols:
      return;
    }
  }

  /// If a label is defined before the .type directive sets the label's type
  /// then the label can't be recorded as thumb function when the label is
  /// defined. We override emitSymbolAttribute() which is called as part of the
  /// parsing of .type so that if the symbol has already been defined we can
  /// record the label as Thumb. FIXME: there is a corner case where the state
  /// is changed in between the label definition and the .type directive, this
  /// is not expected to occur in practice and handling it would require the
  /// backend to track IsThumb for every label.
  bool emitSymbolAttribute(MCSymbol *Symbol, MCSymbolAttr Attribute) override {
    bool Val = MCELFStreamer::emitSymbolAttribute(Symbol, Attribute);

    if (!IsThumb)
      return Val;

    unsigned Type = cast<MCSymbolELF>(Symbol)->getType();
    if ((Type == ELF::STT_FUNC || Type == ELF::STT_GNU_IFUNC) &&
        Symbol->isDefined())
      getAssembler().setIsThumbFunc(Symbol);

    return Val;
  };

private:
  enum ElfMappingSymbol {
    EMS_None,
    EMS_ARM,
    EMS_Thumb,
    EMS_Data
  };

  struct ElfMappingSymbolInfo {
    void resetInfo() {
      F = nullptr;
      Offset = 0;
    }
    bool hasInfo() { return F != nullptr; }
    MCDataFragment *F = nullptr;
    uint64_t Offset = 0;
    ElfMappingSymbol State = EMS_None;
  };

  void emitDataMappingSymbol() {
    if (LastEMSInfo->State == EMS_Data)
      return;
    else if (LastEMSInfo->State == EMS_None) {
      // This is a tentative symbol, it won't really be emitted until it's
      // actually needed.
      ElfMappingSymbolInfo *EMS = LastEMSInfo.get();
      auto *DF = dyn_cast_or_null<MCDataFragment>(getCurrentFragment());
      if (!DF)
        return;
      EMS->F = DF;
      EMS->Offset = DF->getContents().size();
      LastEMSInfo->State = EMS_Data;
      return;
    }
    EmitMappingSymbol("$d");
    LastEMSInfo->State = EMS_Data;
  }

  void EmitThumbMappingSymbol() {
    if (LastEMSInfo->State == EMS_Thumb)
      return;
    FlushPendingMappingSymbol();
    EmitMappingSymbol("$t");
    LastEMSInfo->State = EMS_Thumb;
  }

  void EmitARMMappingSymbol() {
    if (LastEMSInfo->State == EMS_ARM)
      return;
    FlushPendingMappingSymbol();
    EmitMappingSymbol("$a");
    LastEMSInfo->State = EMS_ARM;
  }

  void EmitMappingSymbol(StringRef Name) {
    auto *Symbol = cast<MCSymbolELF>(getContext().createLocalSymbol(Name));
    emitLabel(Symbol);

    Symbol->setType(ELF::STT_NOTYPE);
    Symbol->setBinding(ELF::STB_LOCAL);
  }

  void emitMappingSymbol(StringRef Name, MCDataFragment &F, uint64_t Offset) {
    auto *Symbol = cast<MCSymbolELF>(getContext().createLocalSymbol(Name));
    emitLabelAtPos(Symbol, SMLoc(), F, Offset);
    Symbol->setType(ELF::STT_NOTYPE);
    Symbol->setBinding(ELF::STB_LOCAL);
  }

  void emitThumbFunc(MCSymbol *Func) override {
    getAssembler().setIsThumbFunc(Func);
    emitSymbolAttribute(Func, MCSA_ELF_TypeFunction);
  }

  // Helper functions for ARM exception handling directives
  void EHReset();

  // Reset state between object emissions
  void reset() override;

  void EmitPersonalityFixup(StringRef Name);
  void FlushPendingOffset();
  void FlushUnwindOpcodes(bool NoHandlerData);

  void SwitchToEHSection(StringRef Prefix, unsigned Type, unsigned Flags,
                         SectionKind Kind, const MCSymbol &Fn);
  void SwitchToExTabSection(const MCSymbol &FnStart);
  void SwitchToExIdxSection(const MCSymbol &FnStart);

  void EmitFixup(const MCExpr *Expr, MCFixupKind Kind);

  bool IsThumb;
  bool IsAndroid;

  DenseMap<const MCSection *, std::unique_ptr<ElfMappingSymbolInfo>>
      LastMappingSymbols;

  std::unique_ptr<ElfMappingSymbolInfo> LastEMSInfo;

  // ARM Exception Handling Frame Information
  MCSymbol *ExTab;
  MCSymbol *FnStart;
  const MCSymbol *Personality;
  unsigned PersonalityIndex;
  unsigned FPReg; // Frame pointer register
  int64_t FPOffset; // Offset: (final frame pointer) - (initial $sp)
  int64_t SPOffset; // Offset: (final $sp) - (initial $sp)
  int64_t PendingOffset; // Offset: (final $sp) - (emitted $sp)
  bool UsedFP;
  bool CantUnwind;
  SmallVector<uint8_t, 64> Opcodes;
  UnwindOpcodeAssembler UnwindOpAsm;
};

} // end anonymous namespace

ARMELFStreamer &ARMTargetELFStreamer::getStreamer() {
  return static_cast<ARMELFStreamer &>(Streamer);
}

void ARMTargetELFStreamer::emitFnStart() { getStreamer().emitFnStart(); }
void ARMTargetELFStreamer::emitFnEnd() { getStreamer().emitFnEnd(); }
void ARMTargetELFStreamer::emitCantUnwind() { getStreamer().emitCantUnwind(); }

void ARMTargetELFStreamer::emitPersonality(const MCSymbol *Personality) {
  getStreamer().emitPersonality(Personality);
}

void ARMTargetELFStreamer::emitPersonalityIndex(unsigned Index) {
  getStreamer().emitPersonalityIndex(Index);
}

void ARMTargetELFStreamer::emitHandlerData() {
  getStreamer().emitHandlerData();
}

void ARMTargetELFStreamer::emitSetFP(unsigned FpReg, unsigned SpReg,
                                     int64_t Offset) {
  getStreamer().emitSetFP(FpReg, SpReg, Offset);
}

void ARMTargetELFStreamer::emitMovSP(unsigned Reg, int64_t Offset) {
  getStreamer().emitMovSP(Reg, Offset);
}

void ARMTargetELFStreamer::emitPad(int64_t Offset) {
  getStreamer().emitPad(Offset);
}

void ARMTargetELFStreamer::emitRegSave(const SmallVectorImpl<unsigned> &RegList,
                                       bool isVector) {
  getStreamer().emitRegSave(RegList, isVector);
}

void ARMTargetELFStreamer::emitUnwindRaw(int64_t Offset,
                                      const SmallVectorImpl<uint8_t> &Opcodes) {
  getStreamer().emitUnwindRaw(Offset, Opcodes);
}

void ARMTargetELFStreamer::switchVendor(StringRef Vendor) {
  assert(!Vendor.empty() && "Vendor cannot be empty.");

  if (CurrentVendor == Vendor)
    return;

  if (!CurrentVendor.empty())
    finishAttributeSection();

  assert(getStreamer().Contents.empty() &&
         ".ARM.attributes should be flushed before changing vendor");
  CurrentVendor = Vendor;

}

void ARMTargetELFStreamer::emitAttribute(unsigned Attribute, unsigned Value) {
  getStreamer().setAttributeItem(Attribute, Value,
                                 /* OverwriteExisting= */ true);
}

void ARMTargetELFStreamer::emitTextAttribute(unsigned Attribute,
                                             StringRef Value) {
  getStreamer().setAttributeItem(Attribute, Value,
                                 /* OverwriteExisting= */ true);
}

void ARMTargetELFStreamer::emitIntTextAttribute(unsigned Attribute,
                                                unsigned IntValue,
                                                StringRef StringValue) {
  getStreamer().setAttributeItems(Attribute, IntValue, StringValue,
                                  /* OverwriteExisting= */ true);
}

void ARMTargetELFStreamer::emitArch(ARM::ArchKind Value) {
  Arch = Value;
}

void ARMTargetELFStreamer::emitObjectArch(ARM::ArchKind Value) {
  EmittedArch = Value;
}

void ARMTargetELFStreamer::emitArchDefaultAttributes() {
  using namespace ARMBuildAttrs;
  ARMELFStreamer &S = getStreamer();

  S.setAttributeItem(CPU_name, ARM::getCPUAttr(Arch), false);

  if (EmittedArch == ARM::ArchKind::INVALID)
    S.setAttributeItem(CPU_arch, ARM::getArchAttr(Arch), false);
  else
    S.setAttributeItem(CPU_arch, ARM::getArchAttr(EmittedArch), false);

  switch (Arch) {
  case ARM::ArchKind::ARMV4:
    S.setAttributeItem(ARM_ISA_use, Allowed, false);
    break;

  case ARM::ArchKind::ARMV4T:
  case ARM::ArchKind::ARMV5T:
  case ARM::ArchKind::XSCALE:
  case ARM::ArchKind::ARMV5TE:
  case ARM::ArchKind::ARMV6:
    S.setAttributeItem(ARM_ISA_use, Allowed, false);
    S.setAttributeItem(THUMB_ISA_use, Allowed, false);
    break;

  case ARM::ArchKind::ARMV6T2:
    S.setAttributeItem(ARM_ISA_use, Allowed, false);
    S.setAttributeItem(THUMB_ISA_use, AllowThumb32, false);
    break;

  case ARM::ArchKind::ARMV6K:
  case ARM::ArchKind::ARMV6KZ:
    S.setAttributeItem(ARM_ISA_use, Allowed, false);
    S.setAttributeItem(THUMB_ISA_use, Allowed, false);
    S.setAttributeItem(Virtualization_use, AllowTZ, false);
    break;

  case ARM::ArchKind::ARMV6M:
    S.setAttributeItem(THUMB_ISA_use, Allowed, false);
    break;

  case ARM::ArchKind::ARMV7A:
    S.setAttributeItem(CPU_arch_profile, ApplicationProfile, false);
    S.setAttributeItem(ARM_ISA_use, Allowed, false);
    S.setAttributeItem(THUMB_ISA_use, AllowThumb32, false);
    break;

  case ARM::ArchKind::ARMV7R:
    S.setAttributeItem(CPU_arch_profile, RealTimeProfile, false);
    S.setAttributeItem(ARM_ISA_use, Allowed, false);
    S.setAttributeItem(THUMB_ISA_use, AllowThumb32, false);
    break;

  case ARM::ArchKind::ARMV7EM:
  case ARM::ArchKind::ARMV7M:
    S.setAttributeItem(CPU_arch_profile, MicroControllerProfile, false);
    S.setAttributeItem(THUMB_ISA_use, AllowThumb32, false);
    break;

  case ARM::ArchKind::ARMV8A:
  case ARM::ArchKind::ARMV8_1A:
  case ARM::ArchKind::ARMV8_2A:
  case ARM::ArchKind::ARMV8_3A:
  case ARM::ArchKind::ARMV8_4A:
  case ARM::ArchKind::ARMV8_5A:
  case ARM::ArchKind::ARMV8_6A:
  case ARM::ArchKind::ARMV8_7A:
  case ARM::ArchKind::ARMV8_8A:
  case ARM::ArchKind::ARMV8_9A:
  case ARM::ArchKind::ARMV9A:
  case ARM::ArchKind::ARMV9_1A:
  case ARM::ArchKind::ARMV9_2A:
  case ARM::ArchKind::ARMV9_3A:
  case ARM::ArchKind::ARMV9_4A:
  case ARM::ArchKind::ARMV9_5A:
    S.setAttributeItem(CPU_arch_profile, ApplicationProfile, false);
    S.setAttributeItem(ARM_ISA_use, Allowed, false);
    S.setAttributeItem(THUMB_ISA_use, AllowThumb32, false);
    S.setAttributeItem(MPextension_use, Allowed, false);
    S.setAttributeItem(Virtualization_use, AllowTZVirtualization, false);
    break;

  case ARM::ArchKind::ARMV8MBaseline:
  case ARM::ArchKind::ARMV8MMainline:
    S.setAttributeItem(THUMB_ISA_use, AllowThumbDerived, false);
    S.setAttributeItem(CPU_arch_profile, MicroControllerProfile, false);
    break;

  case ARM::ArchKind::IWMMXT:
    S.setAttributeItem(ARM_ISA_use, Allowed, false);
    S.setAttributeItem(THUMB_ISA_use, Allowed, false);
    S.setAttributeItem(WMMX_arch, AllowWMMXv1, false);
    break;

  case ARM::ArchKind::IWMMXT2:
    S.setAttributeItem(ARM_ISA_use, Allowed, false);
    S.setAttributeItem(THUMB_ISA_use, Allowed, false);
    S.setAttributeItem(WMMX_arch, AllowWMMXv2, false);
    break;

  default:
    report_fatal_error("Unknown Arch: " + Twine(ARM::getArchName(Arch)));
    break;
  }
}

void ARMTargetELFStreamer::emitFPU(ARM::FPUKind Value) { FPU = Value; }

void ARMTargetELFStreamer::emitFPUDefaultAttributes() {
  ARMELFStreamer &S = getStreamer();

  switch (FPU) {
  case ARM::FK_VFP:
  case ARM::FK_VFPV2:
    S.setAttributeItem(ARMBuildAttrs::FP_arch, ARMBuildAttrs::AllowFPv2,
                       /* OverwriteExisting= */ false);
    break;

  case ARM::FK_VFPV3:
    S.setAttributeItem(ARMBuildAttrs::FP_arch, ARMBuildAttrs::AllowFPv3A,
                       /* OverwriteExisting= */ false);
    break;

  case ARM::FK_VFPV3_FP16:
    S.setAttributeItem(ARMBuildAttrs::FP_arch, ARMBuildAttrs::AllowFPv3A,
                       /* OverwriteExisting= */ false);
    S.setAttributeItem(ARMBuildAttrs::FP_HP_extension, ARMBuildAttrs::AllowHPFP,
                       /* OverwriteExisting= */ false);
    break;

  case ARM::FK_VFPV3_D16:
    S.setAttributeItem(ARMBuildAttrs::FP_arch, ARMBuildAttrs::AllowFPv3B,
                       /* OverwriteExisting= */ false);
    break;

  case ARM::FK_VFPV3_D16_FP16:
    S.setAttributeItem(ARMBuildAttrs::FP_arch, ARMBuildAttrs::AllowFPv3B,
                       /* OverwriteExisting= */ false);
    S.setAttributeItem(ARMBuildAttrs::FP_HP_extension, ARMBuildAttrs::AllowHPFP,
                       /* OverwriteExisting= */ false);
    break;

  case ARM::FK_VFPV3XD:
    S.setAttributeItem(ARMBuildAttrs::FP_arch, ARMBuildAttrs::AllowFPv3B,
                       /* OverwriteExisting= */ false);
    break;
  case ARM::FK_VFPV3XD_FP16:
    S.setAttributeItem(ARMBuildAttrs::FP_arch, ARMBuildAttrs::AllowFPv3B,
                       /* OverwriteExisting= */ false);
    S.setAttributeItem(ARMBuildAttrs::FP_HP_extension, ARMBuildAttrs::AllowHPFP,
                       /* OverwriteExisting= */ false);
    break;

  case ARM::FK_VFPV4:
    S.setAttributeItem(ARMBuildAttrs::FP_arch, ARMBuildAttrs::AllowFPv4A,
                       /* OverwriteExisting= */ false);
    break;

  // ABI_HardFP_use is handled in ARMAsmPrinter, so _SP_D16 is treated the same
  // as _D16 here.
  case ARM::FK_FPV4_SP_D16:
  case ARM::FK_VFPV4_D16:
    S.setAttributeItem(ARMBuildAttrs::FP_arch, ARMBuildAttrs::AllowFPv4B,
                       /* OverwriteExisting= */ false);
    break;

  case ARM::FK_FP_ARMV8:
    S.setAttributeItem(ARMBuildAttrs::FP_arch, ARMBuildAttrs::AllowFPARMv8A,
                       /* OverwriteExisting= */ false);
    break;

  // FPV5_D16 is identical to FP_ARMV8 except for the number of D registers, so
  // uses the FP_ARMV8_D16 build attribute.
  case ARM::FK_FPV5_SP_D16:
  case ARM::FK_FPV5_D16:
    S.setAttributeItem(ARMBuildAttrs::FP_arch, ARMBuildAttrs::AllowFPARMv8B,
                       /* OverwriteExisting= */ false);
    break;

  case ARM::FK_NEON:
    S.setAttributeItem(ARMBuildAttrs::FP_arch, ARMBuildAttrs::AllowFPv3A,
                       /* OverwriteExisting= */ false);
    S.setAttributeItem(ARMBuildAttrs::Advanced_SIMD_arch,
                       ARMBuildAttrs::AllowNeon,
                       /* OverwriteExisting= */ false);
    break;

  case ARM::FK_NEON_FP16:
    S.setAttributeItem(ARMBuildAttrs::FP_arch, ARMBuildAttrs::AllowFPv3A,
                       /* OverwriteExisting= */ false);
    S.setAttributeItem(ARMBuildAttrs::Advanced_SIMD_arch,
                       ARMBuildAttrs::AllowNeon,
                       /* OverwriteExisting= */ false);
    S.setAttributeItem(ARMBuildAttrs::FP_HP_extension, ARMBuildAttrs::AllowHPFP,
                       /* OverwriteExisting= */ false);
    break;

  case ARM::FK_NEON_VFPV4:
    S.setAttributeItem(ARMBuildAttrs::FP_arch, ARMBuildAttrs::AllowFPv4A,
                       /* OverwriteExisting= */ false);
    S.setAttributeItem(ARMBuildAttrs::Advanced_SIMD_arch,
                       ARMBuildAttrs::AllowNeon2,
                       /* OverwriteExisting= */ false);
    break;

  case ARM::FK_NEON_FP_ARMV8:
  case ARM::FK_CRYPTO_NEON_FP_ARMV8:
    S.setAttributeItem(ARMBuildAttrs::FP_arch, ARMBuildAttrs::AllowFPARMv8A,
                       /* OverwriteExisting= */ false);
    // 'Advanced_SIMD_arch' must be emitted not here, but within
    // ARMAsmPrinter::emitAttributes(), depending on hasV8Ops() and hasV8_1a()
    break;

  case ARM::FK_SOFTVFP:
  case ARM::FK_NONE:
    break;

  default:
    report_fatal_error("Unknown FPU: " + Twine(FPU));
    break;
  }
}

void ARMTargetELFStreamer::finishAttributeSection() {
  ARMELFStreamer &S = getStreamer();

  if (FPU != ARM::FK_INVALID)
    emitFPUDefaultAttributes();

  if (Arch != ARM::ArchKind::INVALID)
    emitArchDefaultAttributes();

  if (S.Contents.empty())
    return;

  auto LessTag = [](const MCELFStreamer::AttributeItem &LHS,
                    const MCELFStreamer::AttributeItem &RHS) -> bool {
    // The conformance tag must be emitted first when serialised into an
    // object file. Specifically, the addenda to the ARM ABI states that
    // (2.3.7.4):
    //
    // "To simplify recognition by consumers in the common case of claiming
    // conformity for the whole file, this tag should be emitted first in a
    // file-scope sub-subsection of the first public subsection of the
    // attributes section."
    //
    // So it is special-cased in this comparison predicate when the
    // attributes are sorted in finishAttributeSection().
    return (RHS.Tag != ARMBuildAttrs::conformance) &&
           ((LHS.Tag == ARMBuildAttrs::conformance) || (LHS.Tag < RHS.Tag));
  };
  llvm::sort(S.Contents, LessTag);

  S.emitAttributesSection(CurrentVendor, ".ARM.attributes",
                          ELF::SHT_ARM_ATTRIBUTES, AttributeSection);

  FPU = ARM::FK_INVALID;
}

void ARMTargetELFStreamer::emitLabel(MCSymbol *Symbol) {
  ARMELFStreamer &Streamer = getStreamer();
  if (!Streamer.IsThumb)
    return;

  Streamer.getAssembler().registerSymbol(*Symbol);
  unsigned Type = cast<MCSymbolELF>(Symbol)->getType();
  if (Type == ELF::STT_FUNC || Type == ELF::STT_GNU_IFUNC)
    Streamer.emitThumbFunc(Symbol);
}

void ARMTargetELFStreamer::annotateTLSDescriptorSequence(
    const MCSymbolRefExpr *S) {
  getStreamer().EmitFixup(S, FK_Data_4);
}

void ARMTargetELFStreamer::emitThumbSet(MCSymbol *Symbol, const MCExpr *Value) {
  if (const MCSymbolRefExpr *SRE = dyn_cast<MCSymbolRefExpr>(Value)) {
    const MCSymbol &Sym = SRE->getSymbol();
    if (!Sym.isDefined()) {
      getStreamer().emitAssignment(Symbol, Value);
      return;
    }
  }

  getStreamer().emitThumbFunc(Symbol);
  getStreamer().emitAssignment(Symbol, Value);
}

void ARMTargetELFStreamer::emitInst(uint32_t Inst, char Suffix) {
  getStreamer().emitInst(Inst, Suffix);
}

void ARMTargetELFStreamer::reset() { AttributeSection = nullptr; }

void ARMTargetELFStreamer::finish() {
  ARMTargetStreamer::finish();
  finishAttributeSection();
}

void ARMELFStreamer::reset() {
  MCTargetStreamer &TS = *getTargetStreamer();
  ARMTargetStreamer &ATS = static_cast<ARMTargetStreamer &>(TS);
  ATS.reset();
  MCELFStreamer::reset();
  LastMappingSymbols.clear();
  LastEMSInfo.reset();
  // MCELFStreamer clear's the assembler's e_flags. However, for
  // arm we manually set the ABI version on streamer creation, so
  // do the same here
  getWriter().setELFHeaderEFlags(ELF::EF_ARM_EABI_VER5);
}

inline void ARMELFStreamer::SwitchToEHSection(StringRef Prefix,
                                              unsigned Type,
                                              unsigned Flags,
                                              SectionKind Kind,
                                              const MCSymbol &Fn) {
  const MCSectionELF &FnSection =
    static_cast<const MCSectionELF &>(Fn.getSection());

  // Create the name for new section
  StringRef FnSecName(FnSection.getName());
  SmallString<128> EHSecName(Prefix);
  if (FnSecName != ".text") {
    EHSecName += FnSecName;
  }

  // Get .ARM.extab or .ARM.exidx section
  const MCSymbolELF *Group = FnSection.getGroup();
  if (Group)
    Flags |= ELF::SHF_GROUP;
  MCSectionELF *EHSection = getContext().getELFSection(
      EHSecName, Type, Flags, 0, Group, /*IsComdat=*/true,
      FnSection.getUniqueID(),
      static_cast<const MCSymbolELF *>(FnSection.getBeginSymbol()));

  assert(EHSection && "Failed to get the required EH section");

  // Switch to .ARM.extab or .ARM.exidx section
  switchSection(EHSection);
  emitValueToAlignment(Align(4), 0, 1, 0);
}

inline void ARMELFStreamer::SwitchToExTabSection(const MCSymbol &FnStart) {
  SwitchToEHSection(".ARM.extab", ELF::SHT_PROGBITS, ELF::SHF_ALLOC,
                    SectionKind::getData(), FnStart);
}

inline void ARMELFStreamer::SwitchToExIdxSection(const MCSymbol &FnStart) {
  SwitchToEHSection(".ARM.exidx", ELF::SHT_ARM_EXIDX,
                    ELF::SHF_ALLOC | ELF::SHF_LINK_ORDER,
                    SectionKind::getData(), FnStart);
}

void ARMELFStreamer::EmitFixup(const MCExpr *Expr, MCFixupKind Kind) {
  MCDataFragment *Frag = getOrCreateDataFragment();
  Frag->getFixups().push_back(MCFixup::create(Frag->getContents().size(), Expr,
                                              Kind));
}

void ARMELFStreamer::EHReset() {
  ExTab = nullptr;
  FnStart = nullptr;
  Personality = nullptr;
  PersonalityIndex = ARM::EHABI::NUM_PERSONALITY_INDEX;
  FPReg = ARM::SP;
  FPOffset = 0;
  SPOffset = 0;
  PendingOffset = 0;
  UsedFP = false;
  CantUnwind = false;

  Opcodes.clear();
  UnwindOpAsm.Reset();
}

void ARMELFStreamer::emitFnStart() {
  assert(FnStart == nullptr);
  FnStart = getContext().createTempSymbol();
  emitLabel(FnStart);
}

void ARMELFStreamer::emitFnEnd() {
  assert(FnStart && ".fnstart must precedes .fnend");

  // Emit unwind opcodes if there is no .handlerdata directive
  if (!ExTab && !CantUnwind)
    FlushUnwindOpcodes(true);

  // Emit the exception index table entry
  SwitchToExIdxSection(*FnStart);

  // The EHABI requires a dependency preserving R_ARM_NONE relocation to the
  // personality routine to protect it from an arbitrary platform's static
  // linker garbage collection. We disable this for Android where the unwinder
  // is either dynamically linked or directly references the personality
  // routine.
  if (PersonalityIndex < ARM::EHABI::NUM_PERSONALITY_INDEX && !IsAndroid)
    EmitPersonalityFixup(GetAEABIUnwindPersonalityName(PersonalityIndex));

  const MCSymbolRefExpr *FnStartRef =
    MCSymbolRefExpr::create(FnStart,
                            MCSymbolRefExpr::VK_ARM_PREL31,
                            getContext());

  emitValue(FnStartRef, 4);

  if (CantUnwind) {
    emitInt32(ARM::EHABI::EXIDX_CANTUNWIND);
  } else if (ExTab) {
    // Emit a reference to the unwind opcodes in the ".ARM.extab" section.
    const MCSymbolRefExpr *ExTabEntryRef =
      MCSymbolRefExpr::create(ExTab,
                              MCSymbolRefExpr::VK_ARM_PREL31,
                              getContext());
    emitValue(ExTabEntryRef, 4);
  } else {
    // For the __aeabi_unwind_cpp_pr0, we have to emit the unwind opcodes in
    // the second word of exception index table entry.  The size of the unwind
    // opcodes should always be 4 bytes.
    assert(PersonalityIndex == ARM::EHABI::AEABI_UNWIND_CPP_PR0 &&
           "Compact model must use __aeabi_unwind_cpp_pr0 as personality");
    assert(Opcodes.size() == 4u &&
           "Unwind opcode size for __aeabi_unwind_cpp_pr0 must be equal to 4");
    uint64_t Intval = Opcodes[0] |
                      Opcodes[1] << 8 |
                      Opcodes[2] << 16 |
                      Opcodes[3] << 24;
    emitIntValue(Intval, Opcodes.size());
  }

  // Switch to the section containing FnStart
  switchSection(&FnStart->getSection());

  // Clean exception handling frame information
  EHReset();
}

void ARMELFStreamer::emitCantUnwind() { CantUnwind = true; }

// Add the R_ARM_NONE fixup at the same position
void ARMELFStreamer::EmitPersonalityFixup(StringRef Name) {
  const MCSymbol *PersonalitySym = getContext().getOrCreateSymbol(Name);

  const MCSymbolRefExpr *PersonalityRef = MCSymbolRefExpr::create(
      PersonalitySym, MCSymbolRefExpr::VK_ARM_NONE, getContext());

  visitUsedExpr(*PersonalityRef);
  MCDataFragment *DF = getOrCreateDataFragment();
  DF->getFixups().push_back(MCFixup::create(DF->getContents().size(),
                                            PersonalityRef,
                                            MCFixup::getKindForSize(4, false)));
}

void ARMELFStreamer::FlushPendingOffset() {
  if (PendingOffset != 0) {
    UnwindOpAsm.EmitSPOffset(-PendingOffset);
    PendingOffset = 0;
  }
}

void ARMELFStreamer::FlushUnwindOpcodes(bool NoHandlerData) {
  // Emit the unwind opcode to restore $sp.
  if (UsedFP) {
    const MCRegisterInfo *MRI = getContext().getRegisterInfo();
    int64_t LastRegSaveSPOffset = SPOffset - PendingOffset;
    UnwindOpAsm.EmitSPOffset(LastRegSaveSPOffset - FPOffset);
    UnwindOpAsm.EmitSetSP(MRI->getEncodingValue(FPReg));
  } else {
    FlushPendingOffset();
  }

  // Finalize the unwind opcode sequence
  UnwindOpAsm.Finalize(PersonalityIndex, Opcodes);

  // For compact model 0, we have to emit the unwind opcodes in the .ARM.exidx
  // section.  Thus, we don't have to create an entry in the .ARM.extab
  // section.
  if (NoHandlerData && PersonalityIndex == ARM::EHABI::AEABI_UNWIND_CPP_PR0)
    return;

  // Switch to .ARM.extab section.
  SwitchToExTabSection(*FnStart);

  // Create .ARM.extab label for offset in .ARM.exidx
  assert(!ExTab);
  ExTab = getContext().createTempSymbol();
  emitLabel(ExTab);

  // Emit personality
  if (Personality) {
    const MCSymbolRefExpr *PersonalityRef =
      MCSymbolRefExpr::create(Personality,
                              MCSymbolRefExpr::VK_ARM_PREL31,
                              getContext());

    emitValue(PersonalityRef, 4);
  }

  // Emit unwind opcodes
  assert((Opcodes.size() % 4) == 0 &&
         "Unwind opcode size for __aeabi_cpp_unwind_pr0 must be multiple of 4");
  for (unsigned I = 0; I != Opcodes.size(); I += 4) {
    uint64_t Intval = Opcodes[I] |
                      Opcodes[I + 1] << 8 |
                      Opcodes[I + 2] << 16 |
                      Opcodes[I + 3] << 24;
    emitInt32(Intval);
  }

  // According to ARM EHABI section 9.2, if the __aeabi_unwind_cpp_pr1() or
  // __aeabi_unwind_cpp_pr2() is used, then the handler data must be emitted
  // after the unwind opcodes.  The handler data consists of several 32-bit
  // words, and should be terminated by zero.
  //
  // In case that the .handlerdata directive is not specified by the
  // programmer, we should emit zero to terminate the handler data.
  if (NoHandlerData && !Personality)
    emitInt32(0);
}

void ARMELFStreamer::emitHandlerData() { FlushUnwindOpcodes(false); }

void ARMELFStreamer::emitPersonality(const MCSymbol *Per) {
  Personality = Per;
  UnwindOpAsm.setPersonality(Per);
}

void ARMELFStreamer::emitPersonalityIndex(unsigned Index) {
  assert(Index < ARM::EHABI::NUM_PERSONALITY_INDEX && "invalid index");
  PersonalityIndex = Index;
}

void ARMELFStreamer::emitSetFP(unsigned NewFPReg, unsigned NewSPReg,
                               int64_t Offset) {
  assert((NewSPReg == ARM::SP || NewSPReg == FPReg) &&
         "the operand of .setfp directive should be either $sp or $fp");

  UsedFP = true;
  FPReg = NewFPReg;

  if (NewSPReg == ARM::SP)
    FPOffset = SPOffset + Offset;
  else
    FPOffset += Offset;
}

void ARMELFStreamer::emitMovSP(unsigned Reg, int64_t Offset) {
  assert((Reg != ARM::SP && Reg != ARM::PC) &&
         "the operand of .movsp cannot be either sp or pc");
  assert(FPReg == ARM::SP && "current FP must be SP");

  FlushPendingOffset();

  FPReg = Reg;
  FPOffset = SPOffset + Offset;

  const MCRegisterInfo *MRI = getContext().getRegisterInfo();
  UnwindOpAsm.EmitSetSP(MRI->getEncodingValue(FPReg));
}

void ARMELFStreamer::emitPad(int64_t Offset) {
  // Track the change of the $sp offset
  SPOffset -= Offset;

  // To squash multiple .pad directives, we should delay the unwind opcode
  // until the .save, .vsave, .handlerdata, or .fnend directives.
  PendingOffset -= Offset;
}

static std::pair<unsigned, unsigned>
collectHWRegs(const MCRegisterInfo &MRI, unsigned Idx,
              const SmallVectorImpl<unsigned> &RegList, bool IsVector,
              uint32_t &Mask_) {
  uint32_t Mask = 0;
  unsigned Count = 0;
  while (Idx > 0) {
    unsigned Reg = RegList[Idx - 1];
    if (Reg == ARM::RA_AUTH_CODE)
      break;
    Reg = MRI.getEncodingValue(Reg);
    assert(Reg < (IsVector ? 32U : 16U) && "Register out of range");
    unsigned Bit = (1u << Reg);
    if ((Mask & Bit) == 0) {
      Mask |= Bit;
      ++Count;
    }
    --Idx;
  }

  Mask_ = Mask;
  return {Idx, Count};
}

void ARMELFStreamer::emitRegSave(const SmallVectorImpl<unsigned> &RegList,
                                 bool IsVector) {
  uint32_t Mask;
  unsigned Idx, Count;
  const MCRegisterInfo &MRI = *getContext().getRegisterInfo();

  // Collect the registers in the register list. Issue unwinding instructions in
  // three parts: ordinary hardware registers, return address authentication
  // code pseudo register, the rest of the registers. The RA PAC is kept in an
  // architectural register (usually r12), but we treat it as a special case in
  // order to distinguish between that register containing RA PAC or a general
  // value.
  Idx = RegList.size();
  while (Idx > 0) {
    std::tie(Idx, Count) = collectHWRegs(MRI, Idx, RegList, IsVector, Mask);
    if (Count) {
      // Track the change the $sp offset: For the .save directive, the
      // corresponding push instruction will decrease the $sp by (4 * Count).
      // For the .vsave directive, the corresponding vpush instruction will
      // decrease $sp by (8 * Count).
      SPOffset -= Count * (IsVector ? 8 : 4);

      // Emit the opcode
      FlushPendingOffset();
      if (IsVector)
        UnwindOpAsm.EmitVFPRegSave(Mask);
      else
        UnwindOpAsm.EmitRegSave(Mask);
    } else if (Idx > 0 && RegList[Idx - 1] == ARM::RA_AUTH_CODE) {
      --Idx;
      SPOffset -= 4;
      FlushPendingOffset();
      UnwindOpAsm.EmitRegSave(0);
    }
  }
}

void ARMELFStreamer::emitUnwindRaw(int64_t Offset,
                                   const SmallVectorImpl<uint8_t> &Opcodes) {
  FlushPendingOffset();
  SPOffset = SPOffset - Offset;
  UnwindOpAsm.EmitRaw(Opcodes);
}

namespace llvm {

MCTargetStreamer *createARMTargetAsmStreamer(MCStreamer &S,
                                             formatted_raw_ostream &OS,
                                             MCInstPrinter *InstPrint) {
  return new ARMTargetAsmStreamer(S, OS, *InstPrint);
}

MCTargetStreamer *createARMNullTargetStreamer(MCStreamer &S) {
  return new ARMTargetStreamer(S);
}

MCTargetStreamer *createARMObjectTargetELFStreamer(MCStreamer &S) {
  return new ARMTargetELFStreamer(S);
}

MCELFStreamer *createARMELFStreamer(MCContext &Context,
                                    std::unique_ptr<MCAsmBackend> TAB,
                                    std::unique_ptr<MCObjectWriter> OW,
                                    std::unique_ptr<MCCodeEmitter> Emitter,
                                    bool IsThumb, bool IsAndroid) {
  ARMELFStreamer *S =
      new ARMELFStreamer(Context, std::move(TAB), std::move(OW),
                         std::move(Emitter), IsThumb, IsAndroid);
  // FIXME: This should eventually end up somewhere else where more
  // intelligent flag decisions can be made. For now we are just maintaining
  // the status quo for ARM and setting EF_ARM_EABI_VER5 as the default.
  S->getWriter().setELFHeaderEFlags(ELF::EF_ARM_EABI_VER5);

  return S;
}

} // end namespace llvm
