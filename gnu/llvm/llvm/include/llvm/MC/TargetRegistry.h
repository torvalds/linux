//===- MC/TargetRegistry.h - Target Registration ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file exposes the TargetRegistry interface, which tools can use to access
// the appropriate target specific classes (TargetMachine, AsmPrinter, etc.)
// which have been registered.
//
// Target specific class implementations should register themselves using the
// appropriate TargetRegistry interfaces.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_TARGETREGISTRY_H
#define LLVM_MC_TARGETREGISTRY_H

#include "llvm-c/DisassemblerTypes.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/TargetParser/Triple.h"
#include <cassert>
#include <cstddef>
#include <iterator>
#include <memory>
#include <optional>
#include <string>

namespace llvm {

class AsmPrinter;
class MCAsmBackend;
class MCAsmInfo;
class MCAsmParser;
class MCCodeEmitter;
class MCContext;
class MCDisassembler;
class MCInstPrinter;
class MCInstrAnalysis;
class MCInstrInfo;
class MCObjectWriter;
class MCRegisterInfo;
class MCRelocationInfo;
class MCStreamer;
class MCSubtargetInfo;
class MCSymbolizer;
class MCTargetAsmParser;
class MCTargetOptions;
class MCTargetStreamer;
class raw_ostream;
class TargetMachine;
class TargetOptions;
namespace mca {
class CustomBehaviour;
class InstrPostProcess;
class InstrumentManager;
struct SourceMgr;
} // namespace mca

MCStreamer *createNullStreamer(MCContext &Ctx);
// Takes ownership of \p TAB and \p CE.

/// Create a machine code streamer which will print out assembly for the native
/// target, suitable for compiling with a native assembler.
///
/// \param InstPrint - If given, the instruction printer to use. If not given
/// the MCInst representation will be printed.  This method takes ownership of
/// InstPrint.
///
/// \param CE - If given, a code emitter to use to show the instruction
/// encoding inline with the assembly. This method takes ownership of \p CE.
///
/// \param TAB - If given, a target asm backend to use to show the fixup
/// information in conjunction with encoding information. This method takes
/// ownership of \p TAB.
///
/// \param ShowInst - Whether to show the MCInst representation inline with
/// the assembly.
MCStreamer *
createAsmStreamer(MCContext &Ctx, std::unique_ptr<formatted_raw_ostream> OS,
                  MCInstPrinter *InstPrint, std::unique_ptr<MCCodeEmitter> &&CE,
                  std::unique_ptr<MCAsmBackend> &&TAB);

MCStreamer *createELFStreamer(MCContext &Ctx,
                              std::unique_ptr<MCAsmBackend> &&TAB,
                              std::unique_ptr<MCObjectWriter> &&OW,
                              std::unique_ptr<MCCodeEmitter> &&CE);
MCStreamer *createGOFFStreamer(MCContext &Ctx,
                               std::unique_ptr<MCAsmBackend> &&TAB,
                               std::unique_ptr<MCObjectWriter> &&OW,
                               std::unique_ptr<MCCodeEmitter> &&CE);
MCStreamer *createMachOStreamer(MCContext &Ctx,
                                std::unique_ptr<MCAsmBackend> &&TAB,
                                std::unique_ptr<MCObjectWriter> &&OW,
                                std::unique_ptr<MCCodeEmitter> &&CE,
                                bool DWARFMustBeAtTheEnd,
                                bool LabelSections = false);
MCStreamer *createWasmStreamer(MCContext &Ctx,
                               std::unique_ptr<MCAsmBackend> &&TAB,
                               std::unique_ptr<MCObjectWriter> &&OW,
                               std::unique_ptr<MCCodeEmitter> &&CE);
MCStreamer *createSPIRVStreamer(MCContext &Ctx,
                                std::unique_ptr<MCAsmBackend> &&TAB,
                                std::unique_ptr<MCObjectWriter> &&OW,
                                std::unique_ptr<MCCodeEmitter> &&CE);
MCStreamer *createDXContainerStreamer(MCContext &Ctx,
                                      std::unique_ptr<MCAsmBackend> &&TAB,
                                      std::unique_ptr<MCObjectWriter> &&OW,
                                      std::unique_ptr<MCCodeEmitter> &&CE);

MCRelocationInfo *createMCRelocationInfo(const Triple &TT, MCContext &Ctx);

MCSymbolizer *createMCSymbolizer(const Triple &TT, LLVMOpInfoCallback GetOpInfo,
                                 LLVMSymbolLookupCallback SymbolLookUp,
                                 void *DisInfo, MCContext *Ctx,
                                 std::unique_ptr<MCRelocationInfo> &&RelInfo);

mca::CustomBehaviour *createCustomBehaviour(const MCSubtargetInfo &STI,
                                            const mca::SourceMgr &SrcMgr,
                                            const MCInstrInfo &MCII);

mca::InstrPostProcess *createInstrPostProcess(const MCSubtargetInfo &STI,
                                              const MCInstrInfo &MCII);

mca::InstrumentManager *createInstrumentManager(const MCSubtargetInfo &STI,
                                                const MCInstrInfo &MCII);

/// Target - Wrapper for Target specific information.
///
/// For registration purposes, this is a POD type so that targets can be
/// registered without the use of static constructors.
///
/// Targets should implement a single global instance of this class (which
/// will be zero initialized), and pass that instance to the TargetRegistry as
/// part of their initialization.
class Target {
public:
  friend struct TargetRegistry;

  using ArchMatchFnTy = bool (*)(Triple::ArchType Arch);

  using MCAsmInfoCtorFnTy = MCAsmInfo *(*)(const MCRegisterInfo &MRI,
                                           const Triple &TT,
                                           const MCTargetOptions &Options);
  using MCObjectFileInfoCtorFnTy = MCObjectFileInfo *(*)(MCContext &Ctx,
                                                         bool PIC,
                                                         bool LargeCodeModel);
  using MCInstrInfoCtorFnTy = MCInstrInfo *(*)();
  using MCInstrAnalysisCtorFnTy = MCInstrAnalysis *(*)(const MCInstrInfo *Info);
  using MCRegInfoCtorFnTy = MCRegisterInfo *(*)(const Triple &TT);
  using MCSubtargetInfoCtorFnTy = MCSubtargetInfo *(*)(const Triple &TT,
                                                       StringRef CPU,
                                                       StringRef Features);
  using TargetMachineCtorTy = TargetMachine
      *(*)(const Target &T, const Triple &TT, StringRef CPU, StringRef Features,
           const TargetOptions &Options, std::optional<Reloc::Model> RM,
           std::optional<CodeModel::Model> CM, CodeGenOptLevel OL, bool JIT);
  // If it weren't for layering issues (this header is in llvm/Support, but
  // depends on MC?) this should take the Streamer by value rather than rvalue
  // reference.
  using AsmPrinterCtorTy = AsmPrinter *(*)(
      TargetMachine &TM, std::unique_ptr<MCStreamer> &&Streamer);
  using MCAsmBackendCtorTy = MCAsmBackend *(*)(const Target &T,
                                               const MCSubtargetInfo &STI,
                                               const MCRegisterInfo &MRI,
                                               const MCTargetOptions &Options);
  using MCAsmParserCtorTy = MCTargetAsmParser *(*)(
      const MCSubtargetInfo &STI, MCAsmParser &P, const MCInstrInfo &MII,
      const MCTargetOptions &Options);
  using MCDisassemblerCtorTy = MCDisassembler *(*)(const Target &T,
                                                   const MCSubtargetInfo &STI,
                                                   MCContext &Ctx);
  using MCInstPrinterCtorTy = MCInstPrinter *(*)(const Triple &T,
                                                 unsigned SyntaxVariant,
                                                 const MCAsmInfo &MAI,
                                                 const MCInstrInfo &MII,
                                                 const MCRegisterInfo &MRI);
  using MCCodeEmitterCtorTy = MCCodeEmitter *(*)(const MCInstrInfo &II,
                                                 MCContext &Ctx);
  using ELFStreamerCtorTy =
      MCStreamer *(*)(const Triple &T, MCContext &Ctx,
                      std::unique_ptr<MCAsmBackend> &&TAB,
                      std::unique_ptr<MCObjectWriter> &&OW,
                      std::unique_ptr<MCCodeEmitter> &&Emitter);
  using MachOStreamerCtorTy =
      MCStreamer *(*)(MCContext &Ctx, std::unique_ptr<MCAsmBackend> &&TAB,
                      std::unique_ptr<MCObjectWriter> &&OW,
                      std::unique_ptr<MCCodeEmitter> &&Emitter);
  using COFFStreamerCtorTy =
      MCStreamer *(*)(MCContext &Ctx, std::unique_ptr<MCAsmBackend> &&TAB,
                      std::unique_ptr<MCObjectWriter> &&OW,
                      std::unique_ptr<MCCodeEmitter> &&Emitter);
  using XCOFFStreamerCtorTy =
      MCStreamer *(*)(const Triple &T, MCContext &Ctx,
                      std::unique_ptr<MCAsmBackend> &&TAB,
                      std::unique_ptr<MCObjectWriter> &&OW,
                      std::unique_ptr<MCCodeEmitter> &&Emitter);

  using NullTargetStreamerCtorTy = MCTargetStreamer *(*)(MCStreamer &S);
  using AsmTargetStreamerCtorTy =
      MCTargetStreamer *(*)(MCStreamer &S, formatted_raw_ostream &OS,
                            MCInstPrinter *InstPrint);
  using ObjectTargetStreamerCtorTy = MCTargetStreamer *(*)(
      MCStreamer &S, const MCSubtargetInfo &STI);
  using MCRelocationInfoCtorTy = MCRelocationInfo *(*)(const Triple &TT,
                                                       MCContext &Ctx);
  using MCSymbolizerCtorTy = MCSymbolizer *(*)(
      const Triple &TT, LLVMOpInfoCallback GetOpInfo,
      LLVMSymbolLookupCallback SymbolLookUp, void *DisInfo, MCContext *Ctx,
      std::unique_ptr<MCRelocationInfo> &&RelInfo);

  using CustomBehaviourCtorTy =
      mca::CustomBehaviour *(*)(const MCSubtargetInfo &STI,
                                const mca::SourceMgr &SrcMgr,
                                const MCInstrInfo &MCII);

  using InstrPostProcessCtorTy =
      mca::InstrPostProcess *(*)(const MCSubtargetInfo &STI,
                                 const MCInstrInfo &MCII);

  using InstrumentManagerCtorTy =
      mca::InstrumentManager *(*)(const MCSubtargetInfo &STI,
                                  const MCInstrInfo &MCII);

private:
  /// Next - The next registered target in the linked list, maintained by the
  /// TargetRegistry.
  Target *Next;

  /// The target function for checking if an architecture is supported.
  ArchMatchFnTy ArchMatchFn;

  /// Name - The target name.
  const char *Name;

  /// ShortDesc - A short description of the target.
  const char *ShortDesc;

  /// BackendName - The name of the backend implementation. This must match the
  /// name of the 'def X : Target ...' in TableGen.
  const char *BackendName;

  /// HasJIT - Whether this target supports the JIT.
  bool HasJIT;

  /// MCAsmInfoCtorFn - Constructor function for this target's MCAsmInfo, if
  /// registered.
  MCAsmInfoCtorFnTy MCAsmInfoCtorFn;

  /// Constructor function for this target's MCObjectFileInfo, if registered.
  MCObjectFileInfoCtorFnTy MCObjectFileInfoCtorFn;

  /// MCInstrInfoCtorFn - Constructor function for this target's MCInstrInfo,
  /// if registered.
  MCInstrInfoCtorFnTy MCInstrInfoCtorFn;

  /// MCInstrAnalysisCtorFn - Constructor function for this target's
  /// MCInstrAnalysis, if registered.
  MCInstrAnalysisCtorFnTy MCInstrAnalysisCtorFn;

  /// MCRegInfoCtorFn - Constructor function for this target's MCRegisterInfo,
  /// if registered.
  MCRegInfoCtorFnTy MCRegInfoCtorFn;

  /// MCSubtargetInfoCtorFn - Constructor function for this target's
  /// MCSubtargetInfo, if registered.
  MCSubtargetInfoCtorFnTy MCSubtargetInfoCtorFn;

  /// TargetMachineCtorFn - Construction function for this target's
  /// TargetMachine, if registered.
  TargetMachineCtorTy TargetMachineCtorFn;

  /// MCAsmBackendCtorFn - Construction function for this target's
  /// MCAsmBackend, if registered.
  MCAsmBackendCtorTy MCAsmBackendCtorFn;

  /// MCAsmParserCtorFn - Construction function for this target's
  /// MCTargetAsmParser, if registered.
  MCAsmParserCtorTy MCAsmParserCtorFn;

  /// AsmPrinterCtorFn - Construction function for this target's AsmPrinter,
  /// if registered.
  AsmPrinterCtorTy AsmPrinterCtorFn;

  /// MCDisassemblerCtorFn - Construction function for this target's
  /// MCDisassembler, if registered.
  MCDisassemblerCtorTy MCDisassemblerCtorFn;

  /// MCInstPrinterCtorFn - Construction function for this target's
  /// MCInstPrinter, if registered.
  MCInstPrinterCtorTy MCInstPrinterCtorFn;

  /// MCCodeEmitterCtorFn - Construction function for this target's
  /// CodeEmitter, if registered.
  MCCodeEmitterCtorTy MCCodeEmitterCtorFn;

  // Construction functions for the various object formats, if registered.
  COFFStreamerCtorTy COFFStreamerCtorFn = nullptr;
  MachOStreamerCtorTy MachOStreamerCtorFn = nullptr;
  ELFStreamerCtorTy ELFStreamerCtorFn = nullptr;
  XCOFFStreamerCtorTy XCOFFStreamerCtorFn = nullptr;

  /// Construction function for this target's null TargetStreamer, if
  /// registered (default = nullptr).
  NullTargetStreamerCtorTy NullTargetStreamerCtorFn = nullptr;

  /// Construction function for this target's asm TargetStreamer, if
  /// registered (default = nullptr).
  AsmTargetStreamerCtorTy AsmTargetStreamerCtorFn = nullptr;

  /// Construction function for this target's obj TargetStreamer, if
  /// registered (default = nullptr).
  ObjectTargetStreamerCtorTy ObjectTargetStreamerCtorFn = nullptr;

  /// MCRelocationInfoCtorFn - Construction function for this target's
  /// MCRelocationInfo, if registered (default = llvm::createMCRelocationInfo)
  MCRelocationInfoCtorTy MCRelocationInfoCtorFn = nullptr;

  /// MCSymbolizerCtorFn - Construction function for this target's
  /// MCSymbolizer, if registered (default = llvm::createMCSymbolizer)
  MCSymbolizerCtorTy MCSymbolizerCtorFn = nullptr;

  /// CustomBehaviourCtorFn - Construction function for this target's
  /// CustomBehaviour, if registered (default = nullptr).
  CustomBehaviourCtorTy CustomBehaviourCtorFn = nullptr;

  /// InstrPostProcessCtorFn - Construction function for this target's
  /// InstrPostProcess, if registered (default = nullptr).
  InstrPostProcessCtorTy InstrPostProcessCtorFn = nullptr;

  /// InstrumentManagerCtorFn - Construction function for this target's
  /// InstrumentManager, if registered (default = nullptr).
  InstrumentManagerCtorTy InstrumentManagerCtorFn = nullptr;

public:
  Target() = default;

  /// @name Target Information
  /// @{

  // getNext - Return the next registered target.
  const Target *getNext() const { return Next; }

  /// getName - Get the target name.
  const char *getName() const { return Name; }

  /// getShortDescription - Get a short description of the target.
  const char *getShortDescription() const { return ShortDesc; }

  /// getBackendName - Get the backend name.
  const char *getBackendName() const { return BackendName; }

  /// @}
  /// @name Feature Predicates
  /// @{

  /// hasJIT - Check if this targets supports the just-in-time compilation.
  bool hasJIT() const { return HasJIT; }

  /// hasTargetMachine - Check if this target supports code generation.
  bool hasTargetMachine() const { return TargetMachineCtorFn != nullptr; }

  /// hasMCAsmBackend - Check if this target supports .o generation.
  bool hasMCAsmBackend() const { return MCAsmBackendCtorFn != nullptr; }

  /// hasMCAsmParser - Check if this target supports assembly parsing.
  bool hasMCAsmParser() const { return MCAsmParserCtorFn != nullptr; }

  /// @}
  /// @name Feature Constructors
  /// @{

  /// createMCAsmInfo - Create a MCAsmInfo implementation for the specified
  /// target triple.
  ///
  /// \param TheTriple This argument is used to determine the target machine
  /// feature set; it should always be provided. Generally this should be
  /// either the target triple from the module, or the target triple of the
  /// host if that does not exist.
  MCAsmInfo *createMCAsmInfo(const MCRegisterInfo &MRI, StringRef TheTriple,
                             const MCTargetOptions &Options) const {
    if (!MCAsmInfoCtorFn)
      return nullptr;
    return MCAsmInfoCtorFn(MRI, Triple(TheTriple), Options);
  }

  /// Create a MCObjectFileInfo implementation for the specified target
  /// triple.
  ///
  MCObjectFileInfo *createMCObjectFileInfo(MCContext &Ctx, bool PIC,
                                           bool LargeCodeModel = false) const {
    if (!MCObjectFileInfoCtorFn) {
      MCObjectFileInfo *MOFI = new MCObjectFileInfo();
      MOFI->initMCObjectFileInfo(Ctx, PIC, LargeCodeModel);
      return MOFI;
    }
    return MCObjectFileInfoCtorFn(Ctx, PIC, LargeCodeModel);
  }

  /// createMCInstrInfo - Create a MCInstrInfo implementation.
  ///
  MCInstrInfo *createMCInstrInfo() const {
    if (!MCInstrInfoCtorFn)
      return nullptr;
    return MCInstrInfoCtorFn();
  }

  /// createMCInstrAnalysis - Create a MCInstrAnalysis implementation.
  ///
  MCInstrAnalysis *createMCInstrAnalysis(const MCInstrInfo *Info) const {
    if (!MCInstrAnalysisCtorFn)
      return nullptr;
    return MCInstrAnalysisCtorFn(Info);
  }

  /// createMCRegInfo - Create a MCRegisterInfo implementation.
  ///
  MCRegisterInfo *createMCRegInfo(StringRef TT) const {
    if (!MCRegInfoCtorFn)
      return nullptr;
    return MCRegInfoCtorFn(Triple(TT));
  }

  /// createMCSubtargetInfo - Create a MCSubtargetInfo implementation.
  ///
  /// \param TheTriple This argument is used to determine the target machine
  /// feature set; it should always be provided. Generally this should be
  /// either the target triple from the module, or the target triple of the
  /// host if that does not exist.
  /// \param CPU This specifies the name of the target CPU.
  /// \param Features This specifies the string representation of the
  /// additional target features.
  MCSubtargetInfo *createMCSubtargetInfo(StringRef TheTriple, StringRef CPU,
                                         StringRef Features) const {
    if (!MCSubtargetInfoCtorFn)
      return nullptr;
    return MCSubtargetInfoCtorFn(Triple(TheTriple), CPU, Features);
  }

  /// createTargetMachine - Create a target specific machine implementation
  /// for the specified \p Triple.
  ///
  /// \param TT This argument is used to determine the target machine
  /// feature set; it should always be provided. Generally this should be
  /// either the target triple from the module, or the target triple of the
  /// host if that does not exist.
  TargetMachine *createTargetMachine(
      StringRef TT, StringRef CPU, StringRef Features,
      const TargetOptions &Options, std::optional<Reloc::Model> RM,
      std::optional<CodeModel::Model> CM = std::nullopt,
      CodeGenOptLevel OL = CodeGenOptLevel::Default, bool JIT = false) const {
    if (!TargetMachineCtorFn)
      return nullptr;
    return TargetMachineCtorFn(*this, Triple(TT), CPU, Features, Options, RM,
                               CM, OL, JIT);
  }

  /// createMCAsmBackend - Create a target specific assembly parser.
  MCAsmBackend *createMCAsmBackend(const MCSubtargetInfo &STI,
                                   const MCRegisterInfo &MRI,
                                   const MCTargetOptions &Options) const {
    if (!MCAsmBackendCtorFn)
      return nullptr;
    return MCAsmBackendCtorFn(*this, STI, MRI, Options);
  }

  /// createMCAsmParser - Create a target specific assembly parser.
  ///
  /// \param Parser The target independent parser implementation to use for
  /// parsing and lexing.
  MCTargetAsmParser *createMCAsmParser(const MCSubtargetInfo &STI,
                                       MCAsmParser &Parser,
                                       const MCInstrInfo &MII,
                                       const MCTargetOptions &Options) const {
    if (!MCAsmParserCtorFn)
      return nullptr;
    return MCAsmParserCtorFn(STI, Parser, MII, Options);
  }

  /// createAsmPrinter - Create a target specific assembly printer pass.  This
  /// takes ownership of the MCStreamer object.
  AsmPrinter *createAsmPrinter(TargetMachine &TM,
                               std::unique_ptr<MCStreamer> &&Streamer) const {
    if (!AsmPrinterCtorFn)
      return nullptr;
    return AsmPrinterCtorFn(TM, std::move(Streamer));
  }

  MCDisassembler *createMCDisassembler(const MCSubtargetInfo &STI,
                                       MCContext &Ctx) const {
    if (!MCDisassemblerCtorFn)
      return nullptr;
    return MCDisassemblerCtorFn(*this, STI, Ctx);
  }

  MCInstPrinter *createMCInstPrinter(const Triple &T, unsigned SyntaxVariant,
                                     const MCAsmInfo &MAI,
                                     const MCInstrInfo &MII,
                                     const MCRegisterInfo &MRI) const {
    if (!MCInstPrinterCtorFn)
      return nullptr;
    return MCInstPrinterCtorFn(T, SyntaxVariant, MAI, MII, MRI);
  }

  /// createMCCodeEmitter - Create a target specific code emitter.
  MCCodeEmitter *createMCCodeEmitter(const MCInstrInfo &II,
                                     MCContext &Ctx) const {
    if (!MCCodeEmitterCtorFn)
      return nullptr;
    return MCCodeEmitterCtorFn(II, Ctx);
  }

  /// Create a target specific MCStreamer.
  ///
  /// \param T The target triple.
  /// \param Ctx The target context.
  /// \param TAB The target assembler backend object. Takes ownership.
  /// \param OW The stream object.
  /// \param Emitter The target independent assembler object.Takes ownership.
  MCStreamer *createMCObjectStreamer(const Triple &T, MCContext &Ctx,
                                     std::unique_ptr<MCAsmBackend> TAB,
                                     std::unique_ptr<MCObjectWriter> OW,
                                     std::unique_ptr<MCCodeEmitter> Emitter,
                                     const MCSubtargetInfo &STI) const;
  LLVM_DEPRECATED("Use the overload without the 3 trailing bool", "")
  MCStreamer *createMCObjectStreamer(const Triple &T, MCContext &Ctx,
                                     std::unique_ptr<MCAsmBackend> &&TAB,
                                     std::unique_ptr<MCObjectWriter> &&OW,
                                     std::unique_ptr<MCCodeEmitter> &&Emitter,
                                     const MCSubtargetInfo &STI, bool, bool,
                                     bool) const;

  MCStreamer *createAsmStreamer(MCContext &Ctx,
                                std::unique_ptr<formatted_raw_ostream> OS,
                                MCInstPrinter *IP,
                                std::unique_ptr<MCCodeEmitter> CE,
                                std::unique_ptr<MCAsmBackend> TAB) const;
  LLVM_DEPRECATED("Use the overload without the 3 unused bool", "")
  MCStreamer *
  createAsmStreamer(MCContext &Ctx, std::unique_ptr<formatted_raw_ostream> OS,
                    bool IsVerboseAsm, bool UseDwarfDirectory,
                    MCInstPrinter *IP, std::unique_ptr<MCCodeEmitter> &&CE,
                    std::unique_ptr<MCAsmBackend> &&TAB, bool ShowInst) const;

  MCTargetStreamer *createAsmTargetStreamer(MCStreamer &S,
                                            formatted_raw_ostream &OS,
                                            MCInstPrinter *InstPrint) const {
    if (AsmTargetStreamerCtorFn)
      return AsmTargetStreamerCtorFn(S, OS, InstPrint);
    return nullptr;
  }

  MCStreamer *createNullStreamer(MCContext &Ctx) const {
    MCStreamer *S = llvm::createNullStreamer(Ctx);
    createNullTargetStreamer(*S);
    return S;
  }

  MCTargetStreamer *createNullTargetStreamer(MCStreamer &S) const {
    if (NullTargetStreamerCtorFn)
      return NullTargetStreamerCtorFn(S);
    return nullptr;
  }

  /// createMCRelocationInfo - Create a target specific MCRelocationInfo.
  ///
  /// \param TT The target triple.
  /// \param Ctx The target context.
  MCRelocationInfo *createMCRelocationInfo(StringRef TT, MCContext &Ctx) const {
    MCRelocationInfoCtorTy Fn = MCRelocationInfoCtorFn
                                    ? MCRelocationInfoCtorFn
                                    : llvm::createMCRelocationInfo;
    return Fn(Triple(TT), Ctx);
  }

  /// createMCSymbolizer - Create a target specific MCSymbolizer.
  ///
  /// \param TT The target triple.
  /// \param GetOpInfo The function to get the symbolic information for
  /// operands.
  /// \param SymbolLookUp The function to lookup a symbol name.
  /// \param DisInfo The pointer to the block of symbolic information for above
  /// call
  /// back.
  /// \param Ctx The target context.
  /// \param RelInfo The relocation information for this target. Takes
  /// ownership.
  MCSymbolizer *
  createMCSymbolizer(StringRef TT, LLVMOpInfoCallback GetOpInfo,
                     LLVMSymbolLookupCallback SymbolLookUp, void *DisInfo,
                     MCContext *Ctx,
                     std::unique_ptr<MCRelocationInfo> &&RelInfo) const {
    MCSymbolizerCtorTy Fn =
        MCSymbolizerCtorFn ? MCSymbolizerCtorFn : llvm::createMCSymbolizer;
    return Fn(Triple(TT), GetOpInfo, SymbolLookUp, DisInfo, Ctx,
              std::move(RelInfo));
  }

  /// createCustomBehaviour - Create a target specific CustomBehaviour.
  /// This class is used by llvm-mca and requires backend functionality.
  mca::CustomBehaviour *createCustomBehaviour(const MCSubtargetInfo &STI,
                                              const mca::SourceMgr &SrcMgr,
                                              const MCInstrInfo &MCII) const {
    if (CustomBehaviourCtorFn)
      return CustomBehaviourCtorFn(STI, SrcMgr, MCII);
    return nullptr;
  }

  /// createInstrPostProcess - Create a target specific InstrPostProcess.
  /// This class is used by llvm-mca and requires backend functionality.
  mca::InstrPostProcess *createInstrPostProcess(const MCSubtargetInfo &STI,
                                                const MCInstrInfo &MCII) const {
    if (InstrPostProcessCtorFn)
      return InstrPostProcessCtorFn(STI, MCII);
    return nullptr;
  }

  /// createInstrumentManager - Create a target specific
  /// InstrumentManager. This class is used by llvm-mca and requires
  /// backend functionality.
  mca::InstrumentManager *
  createInstrumentManager(const MCSubtargetInfo &STI,
                          const MCInstrInfo &MCII) const {
    if (InstrumentManagerCtorFn)
      return InstrumentManagerCtorFn(STI, MCII);
    return nullptr;
  }

  /// @}
};

/// TargetRegistry - Generic interface to target specific features.
struct TargetRegistry {
  // FIXME: Make this a namespace, probably just move all the Register*
  // functions into Target (currently they all just set members on the Target
  // anyway, and Target friends this class so those functions can...
  // function).
  TargetRegistry() = delete;

  class iterator {
    friend struct TargetRegistry;

    const Target *Current = nullptr;

    explicit iterator(Target *T) : Current(T) {}

  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = Target;
    using difference_type = std::ptrdiff_t;
    using pointer = value_type *;
    using reference = value_type &;

    iterator() = default;

    bool operator==(const iterator &x) const { return Current == x.Current; }
    bool operator!=(const iterator &x) const { return !operator==(x); }

    // Iterator traversal: forward iteration only
    iterator &operator++() { // Preincrement
      assert(Current && "Cannot increment end iterator!");
      Current = Current->getNext();
      return *this;
    }
    iterator operator++(int) { // Postincrement
      iterator tmp = *this;
      ++*this;
      return tmp;
    }

    const Target &operator*() const {
      assert(Current && "Cannot dereference end iterator!");
      return *Current;
    }

    const Target *operator->() const { return &operator*(); }
  };

  /// printRegisteredTargetsForVersion - Print the registered targets
  /// appropriately for inclusion in a tool's version output.
  static void printRegisteredTargetsForVersion(raw_ostream &OS);

  /// @name Registry Access
  /// @{

  static iterator_range<iterator> targets();

  /// lookupTarget - Lookup a target based on a target triple.
  ///
  /// \param Triple - The triple to use for finding a target.
  /// \param Error - On failure, an error string describing why no target was
  /// found.
  static const Target *lookupTarget(StringRef Triple, std::string &Error);

  /// lookupTarget - Lookup a target based on an architecture name
  /// and a target triple.  If the architecture name is non-empty,
  /// then the lookup is done by architecture.  Otherwise, the target
  /// triple is used.
  ///
  /// \param ArchName - The architecture to use for finding a target.
  /// \param TheTriple - The triple to use for finding a target.  The
  /// triple is updated with canonical architecture name if a lookup
  /// by architecture is done.
  /// \param Error - On failure, an error string describing why no target was
  /// found.
  static const Target *lookupTarget(StringRef ArchName, Triple &TheTriple,
                                    std::string &Error);

  /// @}
  /// @name Target Registration
  /// @{

  /// RegisterTarget - Register the given target. Attempts to register a
  /// target which has already been registered will be ignored.
  ///
  /// Clients are responsible for ensuring that registration doesn't occur
  /// while another thread is attempting to access the registry. Typically
  /// this is done by initializing all targets at program startup.
  ///
  /// @param T - The target being registered.
  /// @param Name - The target name. This should be a static string.
  /// @param ShortDesc - A short target description. This should be a static
  /// string.
  /// @param BackendName - The name of the backend. This should be a static
  /// string that is the same for all targets that share a backend
  /// implementation and must match the name used in the 'def X : Target ...' in
  /// TableGen.
  /// @param ArchMatchFn - The arch match checking function for this target.
  /// @param HasJIT - Whether the target supports JIT code
  /// generation.
  static void RegisterTarget(Target &T, const char *Name, const char *ShortDesc,
                             const char *BackendName,
                             Target::ArchMatchFnTy ArchMatchFn,
                             bool HasJIT = false);

  /// RegisterMCAsmInfo - Register a MCAsmInfo implementation for the
  /// given target.
  ///
  /// Clients are responsible for ensuring that registration doesn't occur
  /// while another thread is attempting to access the registry. Typically
  /// this is done by initializing all targets at program startup.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct a MCAsmInfo for the target.
  static void RegisterMCAsmInfo(Target &T, Target::MCAsmInfoCtorFnTy Fn) {
    T.MCAsmInfoCtorFn = Fn;
  }

  /// Register a MCObjectFileInfo implementation for the given target.
  ///
  /// Clients are responsible for ensuring that registration doesn't occur
  /// while another thread is attempting to access the registry. Typically
  /// this is done by initializing all targets at program startup.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct a MCObjectFileInfo for the target.
  static void RegisterMCObjectFileInfo(Target &T,
                                       Target::MCObjectFileInfoCtorFnTy Fn) {
    T.MCObjectFileInfoCtorFn = Fn;
  }

  /// RegisterMCInstrInfo - Register a MCInstrInfo implementation for the
  /// given target.
  ///
  /// Clients are responsible for ensuring that registration doesn't occur
  /// while another thread is attempting to access the registry. Typically
  /// this is done by initializing all targets at program startup.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct a MCInstrInfo for the target.
  static void RegisterMCInstrInfo(Target &T, Target::MCInstrInfoCtorFnTy Fn) {
    T.MCInstrInfoCtorFn = Fn;
  }

  /// RegisterMCInstrAnalysis - Register a MCInstrAnalysis implementation for
  /// the given target.
  static void RegisterMCInstrAnalysis(Target &T,
                                      Target::MCInstrAnalysisCtorFnTy Fn) {
    T.MCInstrAnalysisCtorFn = Fn;
  }

  /// RegisterMCRegInfo - Register a MCRegisterInfo implementation for the
  /// given target.
  ///
  /// Clients are responsible for ensuring that registration doesn't occur
  /// while another thread is attempting to access the registry. Typically
  /// this is done by initializing all targets at program startup.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct a MCRegisterInfo for the target.
  static void RegisterMCRegInfo(Target &T, Target::MCRegInfoCtorFnTy Fn) {
    T.MCRegInfoCtorFn = Fn;
  }

  /// RegisterMCSubtargetInfo - Register a MCSubtargetInfo implementation for
  /// the given target.
  ///
  /// Clients are responsible for ensuring that registration doesn't occur
  /// while another thread is attempting to access the registry. Typically
  /// this is done by initializing all targets at program startup.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct a MCSubtargetInfo for the target.
  static void RegisterMCSubtargetInfo(Target &T,
                                      Target::MCSubtargetInfoCtorFnTy Fn) {
    T.MCSubtargetInfoCtorFn = Fn;
  }

  /// RegisterTargetMachine - Register a TargetMachine implementation for the
  /// given target.
  ///
  /// Clients are responsible for ensuring that registration doesn't occur
  /// while another thread is attempting to access the registry. Typically
  /// this is done by initializing all targets at program startup.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct a TargetMachine for the target.
  static void RegisterTargetMachine(Target &T, Target::TargetMachineCtorTy Fn) {
    T.TargetMachineCtorFn = Fn;
  }

  /// RegisterMCAsmBackend - Register a MCAsmBackend implementation for the
  /// given target.
  ///
  /// Clients are responsible for ensuring that registration doesn't occur
  /// while another thread is attempting to access the registry. Typically
  /// this is done by initializing all targets at program startup.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct an AsmBackend for the target.
  static void RegisterMCAsmBackend(Target &T, Target::MCAsmBackendCtorTy Fn) {
    T.MCAsmBackendCtorFn = Fn;
  }

  /// RegisterMCAsmParser - Register a MCTargetAsmParser implementation for
  /// the given target.
  ///
  /// Clients are responsible for ensuring that registration doesn't occur
  /// while another thread is attempting to access the registry. Typically
  /// this is done by initializing all targets at program startup.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct an MCTargetAsmParser for the target.
  static void RegisterMCAsmParser(Target &T, Target::MCAsmParserCtorTy Fn) {
    T.MCAsmParserCtorFn = Fn;
  }

  /// RegisterAsmPrinter - Register an AsmPrinter implementation for the given
  /// target.
  ///
  /// Clients are responsible for ensuring that registration doesn't occur
  /// while another thread is attempting to access the registry. Typically
  /// this is done by initializing all targets at program startup.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct an AsmPrinter for the target.
  static void RegisterAsmPrinter(Target &T, Target::AsmPrinterCtorTy Fn) {
    T.AsmPrinterCtorFn = Fn;
  }

  /// RegisterMCDisassembler - Register a MCDisassembler implementation for
  /// the given target.
  ///
  /// Clients are responsible for ensuring that registration doesn't occur
  /// while another thread is attempting to access the registry. Typically
  /// this is done by initializing all targets at program startup.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct an MCDisassembler for the target.
  static void RegisterMCDisassembler(Target &T,
                                     Target::MCDisassemblerCtorTy Fn) {
    T.MCDisassemblerCtorFn = Fn;
  }

  /// RegisterMCInstPrinter - Register a MCInstPrinter implementation for the
  /// given target.
  ///
  /// Clients are responsible for ensuring that registration doesn't occur
  /// while another thread is attempting to access the registry. Typically
  /// this is done by initializing all targets at program startup.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct an MCInstPrinter for the target.
  static void RegisterMCInstPrinter(Target &T, Target::MCInstPrinterCtorTy Fn) {
    T.MCInstPrinterCtorFn = Fn;
  }

  /// RegisterMCCodeEmitter - Register a MCCodeEmitter implementation for the
  /// given target.
  ///
  /// Clients are responsible for ensuring that registration doesn't occur
  /// while another thread is attempting to access the registry. Typically
  /// this is done by initializing all targets at program startup.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct an MCCodeEmitter for the target.
  static void RegisterMCCodeEmitter(Target &T, Target::MCCodeEmitterCtorTy Fn) {
    T.MCCodeEmitterCtorFn = Fn;
  }

  static void RegisterCOFFStreamer(Target &T, Target::COFFStreamerCtorTy Fn) {
    T.COFFStreamerCtorFn = Fn;
  }

  static void RegisterMachOStreamer(Target &T, Target::MachOStreamerCtorTy Fn) {
    T.MachOStreamerCtorFn = Fn;
  }

  static void RegisterELFStreamer(Target &T, Target::ELFStreamerCtorTy Fn) {
    T.ELFStreamerCtorFn = Fn;
  }

  static void RegisterXCOFFStreamer(Target &T, Target::XCOFFStreamerCtorTy Fn) {
    T.XCOFFStreamerCtorFn = Fn;
  }

  static void RegisterNullTargetStreamer(Target &T,
                                         Target::NullTargetStreamerCtorTy Fn) {
    T.NullTargetStreamerCtorFn = Fn;
  }

  static void RegisterAsmTargetStreamer(Target &T,
                                        Target::AsmTargetStreamerCtorTy Fn) {
    T.AsmTargetStreamerCtorFn = Fn;
  }

  static void
  RegisterObjectTargetStreamer(Target &T,
                               Target::ObjectTargetStreamerCtorTy Fn) {
    T.ObjectTargetStreamerCtorFn = Fn;
  }

  /// RegisterMCRelocationInfo - Register an MCRelocationInfo
  /// implementation for the given target.
  ///
  /// Clients are responsible for ensuring that registration doesn't occur
  /// while another thread is attempting to access the registry. Typically
  /// this is done by initializing all targets at program startup.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct an MCRelocationInfo for the target.
  static void RegisterMCRelocationInfo(Target &T,
                                       Target::MCRelocationInfoCtorTy Fn) {
    T.MCRelocationInfoCtorFn = Fn;
  }

  /// RegisterMCSymbolizer - Register an MCSymbolizer
  /// implementation for the given target.
  ///
  /// Clients are responsible for ensuring that registration doesn't occur
  /// while another thread is attempting to access the registry. Typically
  /// this is done by initializing all targets at program startup.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct an MCSymbolizer for the target.
  static void RegisterMCSymbolizer(Target &T, Target::MCSymbolizerCtorTy Fn) {
    T.MCSymbolizerCtorFn = Fn;
  }

  /// RegisterCustomBehaviour - Register a CustomBehaviour
  /// implementation for the given target.
  ///
  /// Clients are responsible for ensuring that registration doesn't occur
  /// while another thread is attempting to access the registry. Typically
  /// this is done by initializing all targets at program startup.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct a CustomBehaviour for the target.
  static void RegisterCustomBehaviour(Target &T,
                                      Target::CustomBehaviourCtorTy Fn) {
    T.CustomBehaviourCtorFn = Fn;
  }

  /// RegisterInstrPostProcess - Register an InstrPostProcess
  /// implementation for the given target.
  ///
  /// Clients are responsible for ensuring that registration doesn't occur
  /// while another thread is attempting to access the registry. Typically
  /// this is done by initializing all targets at program startup.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct an InstrPostProcess for the target.
  static void RegisterInstrPostProcess(Target &T,
                                       Target::InstrPostProcessCtorTy Fn) {
    T.InstrPostProcessCtorFn = Fn;
  }

  /// RegisterInstrumentManager - Register an InstrumentManager
  /// implementation for the given target.
  ///
  /// Clients are responsible for ensuring that registration doesn't occur
  /// while another thread is attempting to access the registry. Typically
  /// this is done by initializing all targets at program startup.
  ///
  /// @param T - The target being registered.
  /// @param Fn - A function to construct an InstrumentManager for the
  /// target.
  static void RegisterInstrumentManager(Target &T,
                                        Target::InstrumentManagerCtorTy Fn) {
    T.InstrumentManagerCtorFn = Fn;
  }

  /// @}
};

//===--------------------------------------------------------------------===//

/// RegisterTarget - Helper template for registering a target, for use in the
/// target's initialization function. Usage:
///
///
/// Target &getTheFooTarget() { // The global target instance.
///   static Target TheFooTarget;
///   return TheFooTarget;
/// }
/// extern "C" void LLVMInitializeFooTargetInfo() {
///   RegisterTarget<Triple::foo> X(getTheFooTarget(), "foo", "Foo
///   description", "Foo" /* Backend Name */);
/// }
template <Triple::ArchType TargetArchType = Triple::UnknownArch,
          bool HasJIT = false>
struct RegisterTarget {
  RegisterTarget(Target &T, const char *Name, const char *Desc,
                 const char *BackendName) {
    TargetRegistry::RegisterTarget(T, Name, Desc, BackendName, &getArchMatch,
                                   HasJIT);
  }

  static bool getArchMatch(Triple::ArchType Arch) {
    return Arch == TargetArchType;
  }
};

/// RegisterMCAsmInfo - Helper template for registering a target assembly info
/// implementation.  This invokes the static "Create" method on the class to
/// actually do the construction.  Usage:
///
/// extern "C" void LLVMInitializeFooTarget() {
///   extern Target TheFooTarget;
///   RegisterMCAsmInfo<FooMCAsmInfo> X(TheFooTarget);
/// }
template <class MCAsmInfoImpl> struct RegisterMCAsmInfo {
  RegisterMCAsmInfo(Target &T) {
    TargetRegistry::RegisterMCAsmInfo(T, &Allocator);
  }

private:
  static MCAsmInfo *Allocator(const MCRegisterInfo & /*MRI*/, const Triple &TT,
                              const MCTargetOptions &Options) {
    return new MCAsmInfoImpl(TT, Options);
  }
};

/// RegisterMCAsmInfoFn - Helper template for registering a target assembly info
/// implementation.  This invokes the specified function to do the
/// construction.  Usage:
///
/// extern "C" void LLVMInitializeFooTarget() {
///   extern Target TheFooTarget;
///   RegisterMCAsmInfoFn X(TheFooTarget, TheFunction);
/// }
struct RegisterMCAsmInfoFn {
  RegisterMCAsmInfoFn(Target &T, Target::MCAsmInfoCtorFnTy Fn) {
    TargetRegistry::RegisterMCAsmInfo(T, Fn);
  }
};

/// Helper template for registering a target object file info implementation.
/// This invokes the static "Create" method on the class to actually do the
/// construction.  Usage:
///
/// extern "C" void LLVMInitializeFooTarget() {
///   extern Target TheFooTarget;
///   RegisterMCObjectFileInfo<FooMCObjectFileInfo> X(TheFooTarget);
/// }
template <class MCObjectFileInfoImpl> struct RegisterMCObjectFileInfo {
  RegisterMCObjectFileInfo(Target &T) {
    TargetRegistry::RegisterMCObjectFileInfo(T, &Allocator);
  }

private:
  static MCObjectFileInfo *Allocator(MCContext &Ctx, bool PIC,
                                     bool LargeCodeModel = false) {
    return new MCObjectFileInfoImpl(Ctx, PIC, LargeCodeModel);
  }
};

/// Helper template for registering a target object file info implementation.
/// This invokes the specified function to do the construction.  Usage:
///
/// extern "C" void LLVMInitializeFooTarget() {
///   extern Target TheFooTarget;
///   RegisterMCObjectFileInfoFn X(TheFooTarget, TheFunction);
/// }
struct RegisterMCObjectFileInfoFn {
  RegisterMCObjectFileInfoFn(Target &T, Target::MCObjectFileInfoCtorFnTy Fn) {
    TargetRegistry::RegisterMCObjectFileInfo(T, Fn);
  }
};

/// RegisterMCInstrInfo - Helper template for registering a target instruction
/// info implementation.  This invokes the static "Create" method on the class
/// to actually do the construction.  Usage:
///
/// extern "C" void LLVMInitializeFooTarget() {
///   extern Target TheFooTarget;
///   RegisterMCInstrInfo<FooMCInstrInfo> X(TheFooTarget);
/// }
template <class MCInstrInfoImpl> struct RegisterMCInstrInfo {
  RegisterMCInstrInfo(Target &T) {
    TargetRegistry::RegisterMCInstrInfo(T, &Allocator);
  }

private:
  static MCInstrInfo *Allocator() { return new MCInstrInfoImpl(); }
};

/// RegisterMCInstrInfoFn - Helper template for registering a target
/// instruction info implementation.  This invokes the specified function to
/// do the construction.  Usage:
///
/// extern "C" void LLVMInitializeFooTarget() {
///   extern Target TheFooTarget;
///   RegisterMCInstrInfoFn X(TheFooTarget, TheFunction);
/// }
struct RegisterMCInstrInfoFn {
  RegisterMCInstrInfoFn(Target &T, Target::MCInstrInfoCtorFnTy Fn) {
    TargetRegistry::RegisterMCInstrInfo(T, Fn);
  }
};

/// RegisterMCInstrAnalysis - Helper template for registering a target
/// instruction analyzer implementation.  This invokes the static "Create"
/// method on the class to actually do the construction.  Usage:
///
/// extern "C" void LLVMInitializeFooTarget() {
///   extern Target TheFooTarget;
///   RegisterMCInstrAnalysis<FooMCInstrAnalysis> X(TheFooTarget);
/// }
template <class MCInstrAnalysisImpl> struct RegisterMCInstrAnalysis {
  RegisterMCInstrAnalysis(Target &T) {
    TargetRegistry::RegisterMCInstrAnalysis(T, &Allocator);
  }

private:
  static MCInstrAnalysis *Allocator(const MCInstrInfo *Info) {
    return new MCInstrAnalysisImpl(Info);
  }
};

/// RegisterMCInstrAnalysisFn - Helper template for registering a target
/// instruction analyzer implementation.  This invokes the specified function
/// to do the construction.  Usage:
///
/// extern "C" void LLVMInitializeFooTarget() {
///   extern Target TheFooTarget;
///   RegisterMCInstrAnalysisFn X(TheFooTarget, TheFunction);
/// }
struct RegisterMCInstrAnalysisFn {
  RegisterMCInstrAnalysisFn(Target &T, Target::MCInstrAnalysisCtorFnTy Fn) {
    TargetRegistry::RegisterMCInstrAnalysis(T, Fn);
  }
};

/// RegisterMCRegInfo - Helper template for registering a target register info
/// implementation.  This invokes the static "Create" method on the class to
/// actually do the construction.  Usage:
///
/// extern "C" void LLVMInitializeFooTarget() {
///   extern Target TheFooTarget;
///   RegisterMCRegInfo<FooMCRegInfo> X(TheFooTarget);
/// }
template <class MCRegisterInfoImpl> struct RegisterMCRegInfo {
  RegisterMCRegInfo(Target &T) {
    TargetRegistry::RegisterMCRegInfo(T, &Allocator);
  }

private:
  static MCRegisterInfo *Allocator(const Triple & /*TT*/) {
    return new MCRegisterInfoImpl();
  }
};

/// RegisterMCRegInfoFn - Helper template for registering a target register
/// info implementation.  This invokes the specified function to do the
/// construction.  Usage:
///
/// extern "C" void LLVMInitializeFooTarget() {
///   extern Target TheFooTarget;
///   RegisterMCRegInfoFn X(TheFooTarget, TheFunction);
/// }
struct RegisterMCRegInfoFn {
  RegisterMCRegInfoFn(Target &T, Target::MCRegInfoCtorFnTy Fn) {
    TargetRegistry::RegisterMCRegInfo(T, Fn);
  }
};

/// RegisterMCSubtargetInfo - Helper template for registering a target
/// subtarget info implementation.  This invokes the static "Create" method
/// on the class to actually do the construction.  Usage:
///
/// extern "C" void LLVMInitializeFooTarget() {
///   extern Target TheFooTarget;
///   RegisterMCSubtargetInfo<FooMCSubtargetInfo> X(TheFooTarget);
/// }
template <class MCSubtargetInfoImpl> struct RegisterMCSubtargetInfo {
  RegisterMCSubtargetInfo(Target &T) {
    TargetRegistry::RegisterMCSubtargetInfo(T, &Allocator);
  }

private:
  static MCSubtargetInfo *Allocator(const Triple & /*TT*/, StringRef /*CPU*/,
                                    StringRef /*FS*/) {
    return new MCSubtargetInfoImpl();
  }
};

/// RegisterMCSubtargetInfoFn - Helper template for registering a target
/// subtarget info implementation.  This invokes the specified function to
/// do the construction.  Usage:
///
/// extern "C" void LLVMInitializeFooTarget() {
///   extern Target TheFooTarget;
///   RegisterMCSubtargetInfoFn X(TheFooTarget, TheFunction);
/// }
struct RegisterMCSubtargetInfoFn {
  RegisterMCSubtargetInfoFn(Target &T, Target::MCSubtargetInfoCtorFnTy Fn) {
    TargetRegistry::RegisterMCSubtargetInfo(T, Fn);
  }
};

/// RegisterTargetMachine - Helper template for registering a target machine
/// implementation, for use in the target machine initialization
/// function. Usage:
///
/// extern "C" void LLVMInitializeFooTarget() {
///   extern Target TheFooTarget;
///   RegisterTargetMachine<FooTargetMachine> X(TheFooTarget);
/// }
template <class TargetMachineImpl> struct RegisterTargetMachine {
  RegisterTargetMachine(Target &T) {
    TargetRegistry::RegisterTargetMachine(T, &Allocator);
  }

private:
  static TargetMachine *
  Allocator(const Target &T, const Triple &TT, StringRef CPU, StringRef FS,
            const TargetOptions &Options, std::optional<Reloc::Model> RM,
            std::optional<CodeModel::Model> CM, CodeGenOptLevel OL, bool JIT) {
    return new TargetMachineImpl(T, TT, CPU, FS, Options, RM, CM, OL, JIT);
  }
};

/// RegisterMCAsmBackend - Helper template for registering a target specific
/// assembler backend. Usage:
///
/// extern "C" void LLVMInitializeFooMCAsmBackend() {
///   extern Target TheFooTarget;
///   RegisterMCAsmBackend<FooAsmLexer> X(TheFooTarget);
/// }
template <class MCAsmBackendImpl> struct RegisterMCAsmBackend {
  RegisterMCAsmBackend(Target &T) {
    TargetRegistry::RegisterMCAsmBackend(T, &Allocator);
  }

private:
  static MCAsmBackend *Allocator(const Target &T, const MCSubtargetInfo &STI,
                                 const MCRegisterInfo &MRI,
                                 const MCTargetOptions &Options) {
    return new MCAsmBackendImpl(T, STI, MRI);
  }
};

/// RegisterMCAsmParser - Helper template for registering a target specific
/// assembly parser, for use in the target machine initialization
/// function. Usage:
///
/// extern "C" void LLVMInitializeFooMCAsmParser() {
///   extern Target TheFooTarget;
///   RegisterMCAsmParser<FooAsmParser> X(TheFooTarget);
/// }
template <class MCAsmParserImpl> struct RegisterMCAsmParser {
  RegisterMCAsmParser(Target &T) {
    TargetRegistry::RegisterMCAsmParser(T, &Allocator);
  }

private:
  static MCTargetAsmParser *Allocator(const MCSubtargetInfo &STI,
                                      MCAsmParser &P, const MCInstrInfo &MII,
                                      const MCTargetOptions &Options) {
    return new MCAsmParserImpl(STI, P, MII, Options);
  }
};

/// RegisterAsmPrinter - Helper template for registering a target specific
/// assembly printer, for use in the target machine initialization
/// function. Usage:
///
/// extern "C" void LLVMInitializeFooAsmPrinter() {
///   extern Target TheFooTarget;
///   RegisterAsmPrinter<FooAsmPrinter> X(TheFooTarget);
/// }
template <class AsmPrinterImpl> struct RegisterAsmPrinter {
  RegisterAsmPrinter(Target &T) {
    TargetRegistry::RegisterAsmPrinter(T, &Allocator);
  }

private:
  static AsmPrinter *Allocator(TargetMachine &TM,
                               std::unique_ptr<MCStreamer> &&Streamer) {
    return new AsmPrinterImpl(TM, std::move(Streamer));
  }
};

/// RegisterMCCodeEmitter - Helper template for registering a target specific
/// machine code emitter, for use in the target initialization
/// function. Usage:
///
/// extern "C" void LLVMInitializeFooMCCodeEmitter() {
///   extern Target TheFooTarget;
///   RegisterMCCodeEmitter<FooCodeEmitter> X(TheFooTarget);
/// }
template <class MCCodeEmitterImpl> struct RegisterMCCodeEmitter {
  RegisterMCCodeEmitter(Target &T) {
    TargetRegistry::RegisterMCCodeEmitter(T, &Allocator);
  }

private:
  static MCCodeEmitter *Allocator(const MCInstrInfo & /*II*/,
                                  MCContext & /*Ctx*/) {
    return new MCCodeEmitterImpl();
  }
};

} // end namespace llvm

#endif // LLVM_MC_TARGETREGISTRY_H
