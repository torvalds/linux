//===-- llvm/Target/TargetLoweringObjectFile.h - Object Info ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements classes used to handle lowerings specific to common
// object file formats.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGET_TARGETLOWERINGOBJECTFILE_H
#define LLVM_TARGET_TARGETLOWERINGOBJECTFILE_H

#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCRegister.h"
#include <cstdint>

namespace llvm {

struct Align;
class Constant;
class DataLayout;
class Function;
class GlobalObject;
class GlobalValue;
class MachineBasicBlock;
class MachineModuleInfo;
class Mangler;
class MCContext;
class MCExpr;
class MCSection;
class MCSymbol;
class MCSymbolRefExpr;
class MCStreamer;
class MCValue;
class Module;
class SectionKind;
class StringRef;
class TargetMachine;
class DSOLocalEquivalent;

class TargetLoweringObjectFile : public MCObjectFileInfo {
  /// Name-mangler for global names.
  Mangler *Mang = nullptr;

protected:
  bool SupportIndirectSymViaGOTPCRel = false;
  bool SupportGOTPCRelWithOffset = true;
  bool SupportDebugThreadLocalLocation = true;
  bool SupportDSOLocalEquivalentLowering = false;

  /// PersonalityEncoding, LSDAEncoding, TTypeEncoding - Some encoding values
  /// for EH.
  unsigned PersonalityEncoding = 0;
  unsigned LSDAEncoding = 0;
  unsigned TTypeEncoding = 0;
  unsigned CallSiteEncoding = 0;

  /// This section contains the static constructor pointer list.
  MCSection *StaticCtorSection = nullptr;

  /// This section contains the static destructor pointer list.
  MCSection *StaticDtorSection = nullptr;

  const TargetMachine *TM = nullptr;

public:
  TargetLoweringObjectFile() = default;
  TargetLoweringObjectFile(const TargetLoweringObjectFile &) = delete;
  TargetLoweringObjectFile &
  operator=(const TargetLoweringObjectFile &) = delete;
  virtual ~TargetLoweringObjectFile();

  Mangler &getMangler() const { return *Mang; }

  /// This method must be called before any actual lowering is done.  This
  /// specifies the current context for codegen, and gives the lowering
  /// implementations a chance to set up their default sections.
  virtual void Initialize(MCContext &ctx, const TargetMachine &TM);

  virtual void emitPersonalityValue(MCStreamer &Streamer, const DataLayout &TM,
                                    const MCSymbol *Sym) const;

  /// Emit the module-level metadata that the platform cares about.
  virtual void emitModuleMetadata(MCStreamer &Streamer, Module &M) const {}

  /// Emit Call Graph Profile metadata.
  void emitCGProfileMetadata(MCStreamer &Streamer, Module &M) const;

  /// Get the module-level metadata that the platform cares about.
  virtual void getModuleMetadata(Module &M) {}

  /// Given a constant with the SectionKind, return a section that it should be
  /// placed in.
  virtual MCSection *getSectionForConstant(const DataLayout &DL,
                                           SectionKind Kind, const Constant *C,
                                           Align &Alignment) const;

  virtual MCSection *
  getSectionForMachineBasicBlock(const Function &F,
                                 const MachineBasicBlock &MBB,
                                 const TargetMachine &TM) const;

  virtual MCSection *
  getUniqueSectionForFunction(const Function &F,
                              const TargetMachine &TM) const;

  /// Classify the specified global variable into a set of target independent
  /// categories embodied in SectionKind.
  static SectionKind getKindForGlobal(const GlobalObject *GO,
                                      const TargetMachine &TM);

  /// This method computes the appropriate section to emit the specified global
  /// variable or function definition. This should not be passed external (or
  /// available externally) globals.
  MCSection *SectionForGlobal(const GlobalObject *GO, SectionKind Kind,
                              const TargetMachine &TM) const;

  /// This method computes the appropriate section to emit the specified global
  /// variable or function definition. This should not be passed external (or
  /// available externally) globals.
  MCSection *SectionForGlobal(const GlobalObject *GO,
                              const TargetMachine &TM) const;

  virtual void getNameWithPrefix(SmallVectorImpl<char> &OutName,
                                 const GlobalValue *GV,
                                 const TargetMachine &TM) const;

  virtual MCSection *getSectionForJumpTable(const Function &F,
                                            const TargetMachine &TM) const;
  virtual MCSection *getSectionForLSDA(const Function &, const MCSymbol &,
                                       const TargetMachine &) const {
    return LSDASection;
  }

  virtual bool shouldPutJumpTableInFunctionSection(bool UsesLabelDifference,
                                                   const Function &F) const;

  /// Targets should implement this method to assign a section to globals with
  /// an explicit section specfied. The implementation of this method can
  /// assume that GO->hasSection() is true.
  virtual MCSection *
  getExplicitSectionGlobal(const GlobalObject *GO, SectionKind Kind,
                           const TargetMachine &TM) const = 0;

  /// Return an MCExpr to use for a reference to the specified global variable
  /// from exception handling information.
  virtual const MCExpr *getTTypeGlobalReference(const GlobalValue *GV,
                                                unsigned Encoding,
                                                const TargetMachine &TM,
                                                MachineModuleInfo *MMI,
                                                MCStreamer &Streamer) const;

  /// Return the MCSymbol for a private symbol with global value name as its
  /// base, with the specified suffix.
  MCSymbol *getSymbolWithGlobalValueBase(const GlobalValue *GV,
                                         StringRef Suffix,
                                         const TargetMachine &TM) const;

  // The symbol that gets passed to .cfi_personality.
  virtual MCSymbol *getCFIPersonalitySymbol(const GlobalValue *GV,
                                            const TargetMachine &TM,
                                            MachineModuleInfo *MMI) const;

  unsigned getPersonalityEncoding() const { return PersonalityEncoding; }
  unsigned getLSDAEncoding() const { return LSDAEncoding; }
  unsigned getTTypeEncoding() const { return TTypeEncoding; }
  unsigned getCallSiteEncoding() const;

  const MCExpr *getTTypeReference(const MCSymbolRefExpr *Sym, unsigned Encoding,
                                  MCStreamer &Streamer) const;

  virtual MCSection *getStaticCtorSection(unsigned Priority,
                                          const MCSymbol *KeySym) const {
    return StaticCtorSection;
  }

  virtual MCSection *getStaticDtorSection(unsigned Priority,
                                          const MCSymbol *KeySym) const {
    return StaticDtorSection;
  }

  /// Create a symbol reference to describe the given TLS variable when
  /// emitting the address in debug info.
  virtual const MCExpr *getDebugThreadLocalSymbol(const MCSymbol *Sym) const;

  virtual const MCExpr *lowerRelativeReference(const GlobalValue *LHS,
                                               const GlobalValue *RHS,
                                               const TargetMachine &TM) const {
    return nullptr;
  }

  /// Target supports a native lowering of a dso_local_equivalent constant
  /// without needing to replace it with equivalent IR.
  bool supportDSOLocalEquivalentLowering() const {
    return SupportDSOLocalEquivalentLowering;
  }

  virtual const MCExpr *lowerDSOLocalEquivalent(const DSOLocalEquivalent *Equiv,
                                                const TargetMachine &TM) const {
    return nullptr;
  }

  /// Target supports replacing a data "PC"-relative access to a symbol
  /// through another symbol, by accessing the later via a GOT entry instead?
  bool supportIndirectSymViaGOTPCRel() const {
    return SupportIndirectSymViaGOTPCRel;
  }

  /// Target GOT "PC"-relative relocation supports encoding an additional
  /// binary expression with an offset?
  bool supportGOTPCRelWithOffset() const {
    return SupportGOTPCRelWithOffset;
  }

  /// Target supports TLS offset relocation in debug section?
  bool supportDebugThreadLocalLocation() const {
    return SupportDebugThreadLocalLocation;
  }

  /// Returns the register used as static base in RWPI variants.
  virtual MCRegister getStaticBase() const { return MCRegister::NoRegister; }

  /// Get the target specific RWPI relocation.
  virtual const MCExpr *getIndirectSymViaRWPI(const MCSymbol *Sym) const {
    return nullptr;
  }

  /// Get the target specific PC relative GOT entry relocation
  virtual const MCExpr *getIndirectSymViaGOTPCRel(const GlobalValue *GV,
                                                  const MCSymbol *Sym,
                                                  const MCValue &MV,
                                                  int64_t Offset,
                                                  MachineModuleInfo *MMI,
                                                  MCStreamer &Streamer) const {
    return nullptr;
  }

  /// If supported, return the section to use for the llvm.commandline
  /// metadata. Otherwise, return nullptr.
  virtual MCSection *getSectionForCommandLines() const {
    return nullptr;
  }

  /// On targets that use separate function descriptor symbols, return a section
  /// for the descriptor given its symbol. Use only with defined functions.
  virtual MCSection *
  getSectionForFunctionDescriptor(const Function *F,
                                  const TargetMachine &TM) const {
    return nullptr;
  }

  /// On targets that support TOC entries, return a section for the entry given
  /// the symbol it refers to.
  /// TODO: Implement this interface for existing ELF targets.
  virtual MCSection *getSectionForTOCEntry(const MCSymbol *S,
                                           const TargetMachine &TM) const {
    return nullptr;
  }

  /// On targets that associate external references with a section, return such
  /// a section for the given external global.
  virtual MCSection *
  getSectionForExternalReference(const GlobalObject *GO,
                                 const TargetMachine &TM) const {
    return nullptr;
  }

  /// Targets that have a special convention for their symbols could use
  /// this hook to return a specialized symbol.
  virtual MCSymbol *getTargetSymbol(const GlobalValue *GV,
                                    const TargetMachine &TM) const {
    return nullptr;
  }

  /// If supported, return the function entry point symbol.
  /// Otherwise, returns nullptr.
  /// Func must be a function or an alias which has a function as base object.
  virtual MCSymbol *getFunctionEntryPointSymbol(const GlobalValue *Func,
                                                const TargetMachine &TM) const {
    return nullptr;
  }

protected:
  virtual MCSection *SelectSectionForGlobal(const GlobalObject *GO,
                                            SectionKind Kind,
                                            const TargetMachine &TM) const = 0;
};

} // end namespace llvm

#endif // LLVM_TARGET_TARGETLOWERINGOBJECTFILE_H
