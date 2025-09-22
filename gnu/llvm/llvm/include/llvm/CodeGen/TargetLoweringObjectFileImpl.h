//==- llvm/CodeGen/TargetLoweringObjectFileImpl.h - Object Info --*- C++ -*-==//
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

#ifndef LLVM_CODEGEN_TARGETLOWERINGOBJECTFILEIMPL_H
#define LLVM_CODEGEN_TARGETLOWERINGOBJECTFILEIMPL_H

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/BinaryFormat/XCOFF.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/Target/TargetLoweringObjectFile.h"

namespace llvm {

class GlobalValue;
class MachineModuleInfo;
class MachineFunction;
class MCContext;
class MCExpr;
class MCSection;
class MCSymbol;
class Module;
class TargetMachine;

class TargetLoweringObjectFileELF : public TargetLoweringObjectFile {
  bool UseInitArray = false;
  mutable unsigned NextUniqueID = 1;  // ID 0 is reserved for execute-only sections
  SmallPtrSet<GlobalObject *, 2> Used;

protected:
  MCSymbolRefExpr::VariantKind PLTRelativeVariantKind =
      MCSymbolRefExpr::VK_None;

public:
  TargetLoweringObjectFileELF();
  ~TargetLoweringObjectFileELF() override = default;

  void Initialize(MCContext &Ctx, const TargetMachine &TM) override;

  void getModuleMetadata(Module &M) override;

  /// Emit Obj-C garbage collection and linker options.
  void emitModuleMetadata(MCStreamer &Streamer, Module &M) const override;

  void emitPersonalityValue(MCStreamer &Streamer, const DataLayout &DL,
                            const MCSymbol *Sym) const override;

  /// Given a constant with the SectionKind, return a section that it should be
  /// placed in.
  MCSection *getSectionForConstant(const DataLayout &DL, SectionKind Kind,
                                   const Constant *C,
                                   Align &Alignment) const override;

  MCSection *getExplicitSectionGlobal(const GlobalObject *GO, SectionKind Kind,
                                      const TargetMachine &TM) const override;

  MCSection *SelectSectionForGlobal(const GlobalObject *GO, SectionKind Kind,
                                    const TargetMachine &TM) const override;

  MCSection *getSectionForJumpTable(const Function &F,
                                    const TargetMachine &TM) const override;
  MCSection *getSectionForLSDA(const Function &F, const MCSymbol &FnSym,
                               const TargetMachine &TM) const override;

  MCSection *
  getSectionForMachineBasicBlock(const Function &F,
                                 const MachineBasicBlock &MBB,
                                 const TargetMachine &TM) const override;

  MCSection *
  getUniqueSectionForFunction(const Function &F,
                              const TargetMachine &TM) const override;

  bool shouldPutJumpTableInFunctionSection(bool UsesLabelDifference,
                                           const Function &F) const override;

  /// Return an MCExpr to use for a reference to the specified type info global
  /// variable from exception handling information.
  const MCExpr *getTTypeGlobalReference(const GlobalValue *GV,
                                        unsigned Encoding,
                                        const TargetMachine &TM,
                                        MachineModuleInfo *MMI,
                                        MCStreamer &Streamer) const override;

  // The symbol that gets passed to .cfi_personality.
  MCSymbol *getCFIPersonalitySymbol(const GlobalValue *GV,
                                    const TargetMachine &TM,
                                    MachineModuleInfo *MMI) const override;

  void InitializeELF(bool UseInitArray_);
  MCSection *getStaticCtorSection(unsigned Priority,
                                  const MCSymbol *KeySym) const override;
  MCSection *getStaticDtorSection(unsigned Priority,
                                  const MCSymbol *KeySym) const override;

  const MCExpr *lowerRelativeReference(const GlobalValue *LHS,
                                       const GlobalValue *RHS,
                                       const TargetMachine &TM) const override;

  const MCExpr *lowerDSOLocalEquivalent(const DSOLocalEquivalent *Equiv,
                                        const TargetMachine &TM) const override;

  MCSection *getSectionForCommandLines() const override;
};

class TargetLoweringObjectFileMachO : public TargetLoweringObjectFile {
public:
  TargetLoweringObjectFileMachO();
  ~TargetLoweringObjectFileMachO() override = default;

  void Initialize(MCContext &Ctx, const TargetMachine &TM) override;

  MCSection *getStaticDtorSection(unsigned Priority,
                                  const MCSymbol *KeySym) const override;

  /// Emit the module flags that specify the garbage collection information.
  void emitModuleMetadata(MCStreamer &Streamer, Module &M) const override;

  MCSection *SelectSectionForGlobal(const GlobalObject *GO, SectionKind Kind,
                                    const TargetMachine &TM) const override;

  MCSection *getExplicitSectionGlobal(const GlobalObject *GO, SectionKind Kind,
                                      const TargetMachine &TM) const override;

  MCSection *getSectionForConstant(const DataLayout &DL, SectionKind Kind,
                                   const Constant *C,
                                   Align &Alignment) const override;

  /// The mach-o version of this method defaults to returning a stub reference.
  const MCExpr *getTTypeGlobalReference(const GlobalValue *GV,
                                        unsigned Encoding,
                                        const TargetMachine &TM,
                                        MachineModuleInfo *MMI,
                                        MCStreamer &Streamer) const override;

  // The symbol that gets passed to .cfi_personality.
  MCSymbol *getCFIPersonalitySymbol(const GlobalValue *GV,
                                    const TargetMachine &TM,
                                    MachineModuleInfo *MMI) const override;

  /// Get MachO PC relative GOT entry relocation
  const MCExpr *getIndirectSymViaGOTPCRel(const GlobalValue *GV,
                                          const MCSymbol *Sym,
                                          const MCValue &MV, int64_t Offset,
                                          MachineModuleInfo *MMI,
                                          MCStreamer &Streamer) const override;

  void getNameWithPrefix(SmallVectorImpl<char> &OutName, const GlobalValue *GV,
                         const TargetMachine &TM) const override;

  MCSection *getSectionForCommandLines() const override;
};

class TargetLoweringObjectFileCOFF : public TargetLoweringObjectFile {
  mutable unsigned NextUniqueID = 0;
  const TargetMachine *TM = nullptr;

public:
  ~TargetLoweringObjectFileCOFF() override = default;

  void Initialize(MCContext &Ctx, const TargetMachine &TM) override;
  MCSection *getExplicitSectionGlobal(const GlobalObject *GO, SectionKind Kind,
                                      const TargetMachine &TM) const override;

  MCSection *SelectSectionForGlobal(const GlobalObject *GO, SectionKind Kind,
                                    const TargetMachine &TM) const override;

  void getNameWithPrefix(SmallVectorImpl<char> &OutName, const GlobalValue *GV,
                         const TargetMachine &TM) const override;

  MCSection *getSectionForJumpTable(const Function &F,
                                    const TargetMachine &TM) const override;

  bool shouldPutJumpTableInFunctionSection(bool UsesLabelDifference,
                                           const Function &F) const override;

  /// Emit Obj-C garbage collection and linker options.
  void emitModuleMetadata(MCStreamer &Streamer, Module &M) const override;

  MCSection *getStaticCtorSection(unsigned Priority,
                                  const MCSymbol *KeySym) const override;
  MCSection *getStaticDtorSection(unsigned Priority,
                                  const MCSymbol *KeySym) const override;

  const MCExpr *lowerRelativeReference(const GlobalValue *LHS,
                                       const GlobalValue *RHS,
                                       const TargetMachine &TM) const override;

  /// Given a mergeable constant with the specified size and relocation
  /// information, return a section that it should be placed in.
  MCSection *getSectionForConstant(const DataLayout &DL, SectionKind Kind,
                                   const Constant *C,
                                   Align &Alignment) const override;

private:
  void emitLinkerDirectives(MCStreamer &Streamer, Module &M) const;
};

class TargetLoweringObjectFileWasm : public TargetLoweringObjectFile {
  mutable unsigned NextUniqueID = 0;
  SmallPtrSet<GlobalObject *, 2> Used;

public:
  TargetLoweringObjectFileWasm() = default;
  ~TargetLoweringObjectFileWasm() override = default;

  void getModuleMetadata(Module &M) override;

  MCSection *getExplicitSectionGlobal(const GlobalObject *GO, SectionKind Kind,
                                      const TargetMachine &TM) const override;

  MCSection *SelectSectionForGlobal(const GlobalObject *GO, SectionKind Kind,
                                    const TargetMachine &TM) const override;

  bool shouldPutJumpTableInFunctionSection(bool UsesLabelDifference,
                                           const Function &F) const override;

  void InitializeWasm();
  MCSection *getStaticCtorSection(unsigned Priority,
                                  const MCSymbol *KeySym) const override;
  MCSection *getStaticDtorSection(unsigned Priority,
                                  const MCSymbol *KeySym) const override;

  const MCExpr *lowerRelativeReference(const GlobalValue *LHS,
                                       const GlobalValue *RHS,
                                       const TargetMachine &TM) const override;
};

class TargetLoweringObjectFileXCOFF : public TargetLoweringObjectFile {
public:
  TargetLoweringObjectFileXCOFF() = default;
  ~TargetLoweringObjectFileXCOFF() override = default;

  static bool ShouldEmitEHBlock(const MachineFunction *MF);
  static bool ShouldSetSSPCanaryBitInTB(const MachineFunction *MF);

  static MCSymbol *getEHInfoTableSymbol(const MachineFunction *MF);

  void Initialize(MCContext &Ctx, const TargetMachine &TM) override;

  bool shouldPutJumpTableInFunctionSection(bool UsesLabelDifference,
                                           const Function &F) const override;

  MCSection *getExplicitSectionGlobal(const GlobalObject *GO, SectionKind Kind,
                                      const TargetMachine &TM) const override;

  MCSection *getStaticCtorSection(unsigned Priority,
                                  const MCSymbol *KeySym) const override;
  MCSection *getStaticDtorSection(unsigned Priority,
                                  const MCSymbol *KeySym) const override;

  const MCExpr *lowerRelativeReference(const GlobalValue *LHS,
                                       const GlobalValue *RHS,
                                       const TargetMachine &TM) const override;

  MCSection *SelectSectionForGlobal(const GlobalObject *GO, SectionKind Kind,
                                    const TargetMachine &TM) const override;

  MCSection *getSectionForJumpTable(const Function &F,
                                    const TargetMachine &TM) const override;

  /// Given a constant with the SectionKind, return a section that it should be
  /// placed in.
  MCSection *getSectionForConstant(const DataLayout &DL, SectionKind Kind,
                                   const Constant *C,
                                   Align &Alignment) const override;

  static XCOFF::StorageClass getStorageClassForGlobal(const GlobalValue *GV);

  MCSection *
  getSectionForFunctionDescriptor(const Function *F,
                                  const TargetMachine &TM) const override;
  MCSection *getSectionForTOCEntry(const MCSymbol *Sym,
                                   const TargetMachine &TM) const override;

  /// For external functions, this will always return a function descriptor
  /// csect.
  MCSection *
  getSectionForExternalReference(const GlobalObject *GO,
                                 const TargetMachine &TM) const override;

  /// For functions, this will always return a function descriptor symbol.
  MCSymbol *getTargetSymbol(const GlobalValue *GV,
                            const TargetMachine &TM) const override;

  MCSymbol *getFunctionEntryPointSymbol(const GlobalValue *Func,
                                        const TargetMachine &TM) const override;

  /// For functions, this will return the LSDA section. If option
  /// -ffunction-sections is on, this will return a unique csect with the
  /// function name appended to .gcc_except_table as a suffix of the LSDA
  /// section name.
  MCSection *getSectionForLSDA(const Function &F, const MCSymbol &FnSym,
                               const TargetMachine &TM) const override;
};

class TargetLoweringObjectFileGOFF : public TargetLoweringObjectFile {
public:
  TargetLoweringObjectFileGOFF();
  ~TargetLoweringObjectFileGOFF() override = default;

  MCSection *SelectSectionForGlobal(const GlobalObject *GO, SectionKind Kind,
                                    const TargetMachine &TM) const override;
  MCSection *getExplicitSectionGlobal(const GlobalObject *GO, SectionKind Kind,
                                      const TargetMachine &TM) const override;
  MCSection *getSectionForLSDA(const Function &F, const MCSymbol &FnSym,
                               const TargetMachine &TM) const override;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_TARGETLOWERINGOBJECTFILEIMPL_H
