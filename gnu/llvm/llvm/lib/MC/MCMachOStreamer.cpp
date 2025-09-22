//===- MCMachOStreamer.cpp - MachO Streamer -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCFragment.h"
#include "llvm/MC/MCLinkerOptimizationHint.h"
#include "llvm/MC/MCMachObjectWriter.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCObjectStreamer.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCSectionMachO.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCSymbolMachO.h"
#include "llvm/MC/MCValue.h"
#include "llvm/MC/SectionKind.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <vector>

namespace llvm {
class MCInst;
class MCStreamer;
class MCSubtargetInfo;
class Triple;
} // namespace llvm

using namespace llvm;

namespace {

class MCMachOStreamer : public MCObjectStreamer {
private:
  /// LabelSections - true if each section change should emit a linker local
  /// label for use in relocations for assembler local references. Obviates the
  /// need for local relocations. False by default.
  bool LabelSections;

  /// HasSectionLabel - map of which sections have already had a non-local
  /// label emitted to them. Used so we don't emit extraneous linker local
  /// labels in the middle of the section.
  DenseMap<const MCSection*, bool> HasSectionLabel;

  void emitInstToData(const MCInst &Inst, const MCSubtargetInfo &STI) override;

  void emitDataRegion(MachO::DataRegionType Kind);
  void emitDataRegionEnd();

public:
  MCMachOStreamer(MCContext &Context, std::unique_ptr<MCAsmBackend> MAB,
                  std::unique_ptr<MCObjectWriter> OW,
                  std::unique_ptr<MCCodeEmitter> Emitter, bool label)
      : MCObjectStreamer(Context, std::move(MAB), std::move(OW),
                         std::move(Emitter)),
        LabelSections(label) {}

  /// state management
  void reset() override {
    HasSectionLabel.clear();
    MCObjectStreamer::reset();
  }

  MachObjectWriter &getWriter() {
    return static_cast<MachObjectWriter &>(getAssembler().getWriter());
  }

  /// @name MCStreamer Interface
  /// @{

  void changeSection(MCSection *Sect, uint32_t Subsection = 0) override;
  void emitLabel(MCSymbol *Symbol, SMLoc Loc = SMLoc()) override;
  void emitAssignment(MCSymbol *Symbol, const MCExpr *Value) override;
  void emitEHSymAttributes(const MCSymbol *Symbol, MCSymbol *EHSymbol) override;
  void emitAssemblerFlag(MCAssemblerFlag Flag) override;
  void emitLinkerOptions(ArrayRef<std::string> Options) override;
  void emitDataRegion(MCDataRegionType Kind) override;
  void emitVersionMin(MCVersionMinType Kind, unsigned Major, unsigned Minor,
                      unsigned Update, VersionTuple SDKVersion) override;
  void emitBuildVersion(unsigned Platform, unsigned Major, unsigned Minor,
                        unsigned Update, VersionTuple SDKVersion) override;
  void emitDarwinTargetVariantBuildVersion(unsigned Platform, unsigned Major,
                                           unsigned Minor, unsigned Update,
                                           VersionTuple SDKVersion) override;
  void emitThumbFunc(MCSymbol *Func) override;
  bool emitSymbolAttribute(MCSymbol *Symbol, MCSymbolAttr Attribute) override;
  void emitSymbolDesc(MCSymbol *Symbol, unsigned DescValue) override;
  void emitCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                        Align ByteAlignment) override;

  void emitLocalCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                             Align ByteAlignment) override;
  void emitZerofill(MCSection *Section, MCSymbol *Symbol = nullptr,
                    uint64_t Size = 0, Align ByteAlignment = Align(1),
                    SMLoc Loc = SMLoc()) override;
  void emitTBSSSymbol(MCSection *Section, MCSymbol *Symbol, uint64_t Size,
                      Align ByteAlignment = Align(1)) override;

  void emitIdent(StringRef IdentString) override {
    llvm_unreachable("macho doesn't support this directive");
  }

  void emitLOHDirective(MCLOHType Kind, const MCLOHArgs &Args) override {
    getWriter().getLOHContainer().addDirective(Kind, Args);
  }
  void emitCGProfileEntry(const MCSymbolRefExpr *From,
                          const MCSymbolRefExpr *To, uint64_t Count) override {
    if (!From->getSymbol().isTemporary() && !To->getSymbol().isTemporary())
      getWriter().getCGProfile().push_back({From, To, Count});
  }

  void finishImpl() override;

  void finalizeCGProfileEntry(const MCSymbolRefExpr *&SRE);
  void finalizeCGProfile();
  void createAddrSigSection();
};

} // end anonymous namespace.

void MCMachOStreamer::changeSection(MCSection *Section, uint32_t Subsection) {
  // Change the section normally.
  changeSectionImpl(Section, Subsection);

  // Output a linker-local symbol so we don't need section-relative local
  // relocations. The linker hates us when we do that.
  if (LabelSections && !HasSectionLabel[Section] &&
      !Section->getBeginSymbol()) {
    MCSymbol *Label = getContext().createLinkerPrivateTempSymbol();
    Section->setBeginSymbol(Label);
    HasSectionLabel[Section] = true;
  }
}

void MCMachOStreamer::emitEHSymAttributes(const MCSymbol *Symbol,
                                          MCSymbol *EHSymbol) {
  auto *Sym = cast<MCSymbolMachO>(Symbol);
  getAssembler().registerSymbol(*Symbol);
  if (Symbol->isExternal())
    emitSymbolAttribute(EHSymbol, MCSA_Global);
  if (Sym->isWeakDefinition())
    emitSymbolAttribute(EHSymbol, MCSA_WeakDefinition);
  if (Sym->isPrivateExtern())
    emitSymbolAttribute(EHSymbol, MCSA_PrivateExtern);
}

void MCMachOStreamer::emitLabel(MCSymbol *Symbol, SMLoc Loc) {
  // We have to create a new fragment if this is an atom defining symbol,
  // fragments cannot span atoms.
  if (cast<MCSymbolMachO>(Symbol)->isSymbolLinkerVisible())
    insert(getContext().allocFragment<MCDataFragment>());

  MCObjectStreamer::emitLabel(Symbol, Loc);

  // This causes the reference type flag to be cleared. Darwin 'as' was "trying"
  // to clear the weak reference and weak definition bits too, but the
  // implementation was buggy. For now we just try to match 'as', for
  // diffability.
  //
  // FIXME: Cleanup this code, these bits should be emitted based on semantic
  // properties, not on the order of definition, etc.
  cast<MCSymbolMachO>(Symbol)->clearReferenceType();
}

void MCMachOStreamer::emitAssignment(MCSymbol *Symbol, const MCExpr *Value) {
  MCValue Res;

  if (Value->evaluateAsRelocatable(Res, nullptr, nullptr)) {
    if (const MCSymbolRefExpr *SymAExpr = Res.getSymA()) {
      const MCSymbol &SymA = SymAExpr->getSymbol();
      if (!Res.getSymB() && (SymA.getName() == "" || Res.getConstant() != 0))
        cast<MCSymbolMachO>(Symbol)->setAltEntry();
    }
  }
  MCObjectStreamer::emitAssignment(Symbol, Value);
}

void MCMachOStreamer::emitDataRegion(MachO::DataRegionType Kind) {
  // Create a temporary label to mark the start of the data region.
  MCSymbol *Start = getContext().createTempSymbol();
  emitLabel(Start);
  // Record the region for the object writer to use.
  getWriter().getDataRegions().push_back({Kind, Start, nullptr});
}

void MCMachOStreamer::emitDataRegionEnd() {
  auto &Regions = getWriter().getDataRegions();
  assert(!Regions.empty() && "Mismatched .end_data_region!");
  auto &Data = Regions.back();
  assert(!Data.End && "Mismatched .end_data_region!");
  // Create a temporary label to mark the end of the data region.
  Data.End = getContext().createTempSymbol();
  emitLabel(Data.End);
}

void MCMachOStreamer::emitAssemblerFlag(MCAssemblerFlag Flag) {
  // Let the target do whatever target specific stuff it needs to do.
  getAssembler().getBackend().handleAssemblerFlag(Flag);
  // Do any generic stuff we need to do.
  switch (Flag) {
  case MCAF_SyntaxUnified: return; // no-op here.
  case MCAF_Code16: return; // Change parsing mode; no-op here.
  case MCAF_Code32: return; // Change parsing mode; no-op here.
  case MCAF_Code64: return; // Change parsing mode; no-op here.
  case MCAF_SubsectionsViaSymbols:
    getWriter().setSubsectionsViaSymbols(true);
    return;
  }
}

void MCMachOStreamer::emitLinkerOptions(ArrayRef<std::string> Options) {
  getWriter().getLinkerOptions().push_back(Options);
}

void MCMachOStreamer::emitDataRegion(MCDataRegionType Kind) {
  switch (Kind) {
  case MCDR_DataRegion:
    emitDataRegion(MachO::DataRegionType::DICE_KIND_DATA);
    return;
  case MCDR_DataRegionJT8:
    emitDataRegion(MachO::DataRegionType::DICE_KIND_JUMP_TABLE8);
    return;
  case MCDR_DataRegionJT16:
    emitDataRegion(MachO::DataRegionType::DICE_KIND_JUMP_TABLE16);
    return;
  case MCDR_DataRegionJT32:
    emitDataRegion(MachO::DataRegionType::DICE_KIND_JUMP_TABLE32);
    return;
  case MCDR_DataRegionEnd:
    emitDataRegionEnd();
    return;
  }
}

void MCMachOStreamer::emitVersionMin(MCVersionMinType Kind, unsigned Major,
                                     unsigned Minor, unsigned Update,
                                     VersionTuple SDKVersion) {
  getWriter().setVersionMin(Kind, Major, Minor, Update, SDKVersion);
}

void MCMachOStreamer::emitBuildVersion(unsigned Platform, unsigned Major,
                                       unsigned Minor, unsigned Update,
                                       VersionTuple SDKVersion) {
  getWriter().setBuildVersion((MachO::PlatformType)Platform, Major, Minor,
                              Update, SDKVersion);
}

void MCMachOStreamer::emitDarwinTargetVariantBuildVersion(
    unsigned Platform, unsigned Major, unsigned Minor, unsigned Update,
    VersionTuple SDKVersion) {
  getWriter().setTargetVariantBuildVersion((MachO::PlatformType)Platform, Major,
                                           Minor, Update, SDKVersion);
}

void MCMachOStreamer::emitThumbFunc(MCSymbol *Symbol) {
  // Remember that the function is a thumb function. Fixup and relocation
  // values will need adjusted.
  getAssembler().setIsThumbFunc(Symbol);
  cast<MCSymbolMachO>(Symbol)->setThumbFunc();
}

bool MCMachOStreamer::emitSymbolAttribute(MCSymbol *Sym,
                                          MCSymbolAttr Attribute) {
  MCSymbolMachO *Symbol = cast<MCSymbolMachO>(Sym);

  // Indirect symbols are handled differently, to match how 'as' handles
  // them. This makes writing matching .o files easier.
  if (Attribute == MCSA_IndirectSymbol) {
    // Note that we intentionally cannot use the symbol data here; this is
    // important for matching the string table that 'as' generates.
    getWriter().getIndirectSymbols().push_back(
        {Symbol, getCurrentSectionOnly()});
    return true;
  }

  // Adding a symbol attribute always introduces the symbol, note that an
  // important side effect of calling registerSymbol here is to register
  // the symbol with the assembler.
  getAssembler().registerSymbol(*Symbol);

  // The implementation of symbol attributes is designed to match 'as', but it
  // leaves much to desired. It doesn't really make sense to arbitrarily add and
  // remove flags, but 'as' allows this (in particular, see .desc).
  //
  // In the future it might be worth trying to make these operations more well
  // defined.
  switch (Attribute) {
  case MCSA_Invalid:
  case MCSA_ELF_TypeFunction:
  case MCSA_ELF_TypeIndFunction:
  case MCSA_ELF_TypeObject:
  case MCSA_ELF_TypeTLS:
  case MCSA_ELF_TypeCommon:
  case MCSA_ELF_TypeNoType:
  case MCSA_ELF_TypeGnuUniqueObject:
  case MCSA_Extern:
  case MCSA_Hidden:
  case MCSA_IndirectSymbol:
  case MCSA_Internal:
  case MCSA_Protected:
  case MCSA_Weak:
  case MCSA_Local:
  case MCSA_LGlobal:
  case MCSA_Exported:
  case MCSA_Memtag:
  case MCSA_WeakAntiDep:
    return false;

  case MCSA_Global:
    Symbol->setExternal(true);
    // This effectively clears the undefined lazy bit, in Darwin 'as', although
    // it isn't very consistent because it implements this as part of symbol
    // lookup.
    //
    // FIXME: Cleanup this code, these bits should be emitted based on semantic
    // properties, not on the order of definition, etc.
    Symbol->setReferenceTypeUndefinedLazy(false);
    break;

  case MCSA_LazyReference:
    // FIXME: This requires -dynamic.
    Symbol->setNoDeadStrip();
    if (Symbol->isUndefined())
      Symbol->setReferenceTypeUndefinedLazy(true);
    break;

    // Since .reference sets the no dead strip bit, it is equivalent to
    // .no_dead_strip in practice.
  case MCSA_Reference:
  case MCSA_NoDeadStrip:
    Symbol->setNoDeadStrip();
    break;

  case MCSA_SymbolResolver:
    Symbol->setSymbolResolver();
    break;

  case MCSA_AltEntry:
    Symbol->setAltEntry();
    break;

  case MCSA_PrivateExtern:
    Symbol->setExternal(true);
    Symbol->setPrivateExtern(true);
    break;

  case MCSA_WeakReference:
    // FIXME: This requires -dynamic.
    if (Symbol->isUndefined())
      Symbol->setWeakReference();
    break;

  case MCSA_WeakDefinition:
    // FIXME: 'as' enforces that this is defined and global. The manual claims
    // it has to be in a coalesced section, but this isn't enforced.
    Symbol->setWeakDefinition();
    break;

  case MCSA_WeakDefAutoPrivate:
    Symbol->setWeakDefinition();
    Symbol->setWeakReference();
    break;

  case MCSA_Cold:
    Symbol->setCold();
    break;
  }

  return true;
}

void MCMachOStreamer::emitSymbolDesc(MCSymbol *Symbol, unsigned DescValue) {
  // Encode the 'desc' value into the lowest implementation defined bits.
  getAssembler().registerSymbol(*Symbol);
  cast<MCSymbolMachO>(Symbol)->setDesc(DescValue);
}

void MCMachOStreamer::emitCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                                       Align ByteAlignment) {
  // FIXME: Darwin 'as' does appear to allow redef of a .comm by itself.
  assert(Symbol->isUndefined() && "Cannot define a symbol twice!");

  getAssembler().registerSymbol(*Symbol);
  Symbol->setExternal(true);
  Symbol->setCommon(Size, ByteAlignment);
}

void MCMachOStreamer::emitLocalCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                                            Align ByteAlignment) {
  // '.lcomm' is equivalent to '.zerofill'.
  return emitZerofill(getContext().getObjectFileInfo()->getDataBSSSection(),
                      Symbol, Size, ByteAlignment);
}

void MCMachOStreamer::emitZerofill(MCSection *Section, MCSymbol *Symbol,
                                   uint64_t Size, Align ByteAlignment,
                                   SMLoc Loc) {
  // On darwin all virtual sections have zerofill type. Disallow the usage of
  // .zerofill in non-virtual functions. If something similar is needed, use
  // .space or .zero.
  if (!Section->isVirtualSection()) {
    getContext().reportError(
        Loc, "The usage of .zerofill is restricted to sections of "
             "ZEROFILL type. Use .zero or .space instead.");
    return; // Early returning here shouldn't harm. EmitZeros should work on any
            // section.
  }

  pushSection();
  switchSection(Section);

  // The symbol may not be present, which only creates the section.
  if (Symbol) {
    emitValueToAlignment(ByteAlignment, 0, 1, 0);
    emitLabel(Symbol);
    emitZeros(Size);
  }
  popSection();
}

// This should always be called with the thread local bss section.  Like the
// .zerofill directive this doesn't actually switch sections on us.
void MCMachOStreamer::emitTBSSSymbol(MCSection *Section, MCSymbol *Symbol,
                                     uint64_t Size, Align ByteAlignment) {
  emitZerofill(Section, Symbol, Size, ByteAlignment);
}

void MCMachOStreamer::emitInstToData(const MCInst &Inst,
                                     const MCSubtargetInfo &STI) {
  MCDataFragment *DF = getOrCreateDataFragment();

  SmallVector<MCFixup, 4> Fixups;
  SmallString<256> Code;
  getAssembler().getEmitter().encodeInstruction(Inst, Code, Fixups, STI);

  // Add the fixups and data.
  for (MCFixup &Fixup : Fixups) {
    Fixup.setOffset(Fixup.getOffset() + DF->getContents().size());
    DF->getFixups().push_back(Fixup);
  }
  DF->setHasInstructions(STI);
  DF->getContents().append(Code.begin(), Code.end());
}

void MCMachOStreamer::finishImpl() {
  emitFrames(&getAssembler().getBackend());

  // We have to set the fragment atom associations so we can relax properly for
  // Mach-O.

  // First, scan the symbol table to build a lookup table from fragments to
  // defining symbols.
  DenseMap<const MCFragment *, const MCSymbol *> DefiningSymbolMap;
  for (const MCSymbol &Symbol : getAssembler().symbols()) {
    auto &Sym = cast<MCSymbolMachO>(Symbol);
    if (Sym.isSymbolLinkerVisible() && Sym.isInSection() && !Sym.isVariable() &&
        !Sym.isAltEntry()) {
      // An atom defining symbol should never be internal to a fragment.
      assert(Symbol.getOffset() == 0 &&
             "Invalid offset in atom defining symbol!");
      DefiningSymbolMap[Symbol.getFragment()] = &Symbol;
    }
  }

  // Set the fragment atom associations by tracking the last seen atom defining
  // symbol.
  for (MCSection &Sec : getAssembler()) {
    cast<MCSectionMachO>(Sec).allocAtoms();
    const MCSymbol *CurrentAtom = nullptr;
    size_t I = 0;
    for (MCFragment &Frag : Sec) {
      if (const MCSymbol *Symbol = DefiningSymbolMap.lookup(&Frag))
        CurrentAtom = Symbol;
      cast<MCSectionMachO>(Sec).setAtom(I++, CurrentAtom);
    }
  }

  finalizeCGProfile();

  createAddrSigSection();
  this->MCObjectStreamer::finishImpl();
}

void MCMachOStreamer::finalizeCGProfileEntry(const MCSymbolRefExpr *&SRE) {
  const MCSymbol *S = &SRE->getSymbol();
  if (getAssembler().registerSymbol(*S))
    S->setExternal(true);
}

void MCMachOStreamer::finalizeCGProfile() {
  MCAssembler &Asm = getAssembler();
  MCObjectWriter &W = getWriter();
  if (W.getCGProfile().empty())
    return;
  for (auto &E : W.getCGProfile()) {
    finalizeCGProfileEntry(E.From);
    finalizeCGProfileEntry(E.To);
  }
  // We can't write the section out until symbol indices are finalized which
  // doesn't happen until after section layout. We need to create the section
  // and set its size now so that it's accounted for in layout.
  MCSection *CGProfileSection = Asm.getContext().getMachOSection(
      "__LLVM", "__cg_profile", 0, SectionKind::getMetadata());
  changeSection(CGProfileSection);
  // For each entry, reserve space for 2 32-bit indices and a 64-bit count.
  size_t SectionBytes =
      W.getCGProfile().size() * (2 * sizeof(uint32_t) + sizeof(uint64_t));
  cast<MCDataFragment>(*CGProfileSection->begin())
      .getContents()
      .resize(SectionBytes);
}

MCStreamer *llvm::createMachOStreamer(MCContext &Context,
                                      std::unique_ptr<MCAsmBackend> &&MAB,
                                      std::unique_ptr<MCObjectWriter> &&OW,
                                      std::unique_ptr<MCCodeEmitter> &&CE,
                                      bool DWARFMustBeAtTheEnd,
                                      bool LabelSections) {
  MCMachOStreamer *S = new MCMachOStreamer(
      Context, std::move(MAB), std::move(OW), std::move(CE), LabelSections);
  const Triple &Target = Context.getTargetTriple();
  S->emitVersionForTarget(
      Target, Context.getObjectFileInfo()->getSDKVersion(),
      Context.getObjectFileInfo()->getDarwinTargetVariantTriple(),
      Context.getObjectFileInfo()->getDarwinTargetVariantSDKVersion());
  return S;
}

// The AddrSig section uses a series of relocations to refer to the symbols that
// should be considered address-significant. The only interesting content of
// these relocations is their symbol; the type, length etc will be ignored by
// the linker. The reason we are not referring to the symbol indices directly is
// that those indices will be invalidated by tools that update the symbol table.
// Symbol relocations OTOH will have their indices updated by e.g. llvm-strip.
void MCMachOStreamer::createAddrSigSection() {
  MCAssembler &Asm = getAssembler();
  MCObjectWriter &writer = Asm.getWriter();
  if (!writer.getEmitAddrsigSection())
    return;
  // Create the AddrSig section and first data fragment here as its layout needs
  // to be computed immediately after in order for it to be exported correctly.
  MCSection *AddrSigSection =
      Asm.getContext().getObjectFileInfo()->getAddrSigSection();
  changeSection(AddrSigSection);
  auto *Frag = cast<MCDataFragment>(AddrSigSection->curFragList()->Head);
  // We will generate a series of pointer-sized symbol relocations at offset
  // 0x0. Set the section size to be large enough to contain a single pointer
  // (instead of emitting a zero-sized section) so these relocations are
  // technically valid, even though we don't expect these relocations to
  // actually be applied by the linker.
  Frag->getContents().resize(8);
}
