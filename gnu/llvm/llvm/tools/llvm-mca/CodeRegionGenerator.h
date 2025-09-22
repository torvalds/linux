//===----------------------- CodeRegionGenerator.h --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file declares classes responsible for generating llvm-mca
/// CodeRegions from various types of input. llvm-mca only analyzes CodeRegions,
/// so the classes here provide the input-to-CodeRegions translation.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_MCA_CODEREGION_GENERATOR_H
#define LLVM_TOOLS_LLVM_MCA_CODEREGION_GENERATOR_H

#include "CodeRegion.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/MCA/CustomBehaviour.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/SourceMgr.h"
#include <memory>

namespace llvm {
namespace mca {

class MCACommentConsumer : public AsmCommentConsumer {
protected:
  bool FoundError = false;

public:
  MCACommentConsumer() = default;

  bool hadErr() const { return FoundError; }
};

/// A comment consumer that parses strings.  The only valid tokens are strings.
class AnalysisRegionCommentConsumer : public MCACommentConsumer {
  AnalysisRegions &Regions;

public:
  AnalysisRegionCommentConsumer(AnalysisRegions &R) : Regions(R) {}

  /// Parses a comment. It begins a new region if it is of the form
  /// LLVM-MCA-BEGIN. It ends a region if it is of the form LLVM-MCA-END.
  /// Regions can be optionally named if they are of the form
  /// LLVM-MCA-BEGIN <name> or LLVM-MCA-END <name>. Subregions are
  /// permitted, but a region that begins while another region is active
  /// must be ended before the outer region is ended. If thre is only one
  /// active region, LLVM-MCA-END does not need to provide a name.
  void HandleComment(SMLoc Loc, StringRef CommentText) override;
};

/// A comment consumer that parses strings to create InstrumentRegions.
/// The only valid tokens are strings.
class InstrumentRegionCommentConsumer : public MCACommentConsumer {
  llvm::SourceMgr &SM;

  InstrumentRegions &Regions;

  InstrumentManager &IM;

public:
  InstrumentRegionCommentConsumer(llvm::SourceMgr &SM, InstrumentRegions &R,
                                  InstrumentManager &IM)
      : SM(SM), Regions(R), IM(IM) {}

  /// Parses a comment. It begins a new region if it is of the form
  /// LLVM-MCA-<INSTRUMENTATION_TYPE> <data> where INSTRUMENTATION_TYPE
  /// is a valid InstrumentKind. If there is already an active
  /// region of type INSTRUMENATION_TYPE, then it will end the active
  /// one and begin a new one using the new data.
  void HandleComment(SMLoc Loc, StringRef CommentText) override;

  InstrumentManager &getInstrumentManager() { return IM; }
};

// This class provides the callbacks that occur when parsing input assembly.
class MCStreamerWrapper : public MCStreamer {
protected:
  CodeRegions &Regions;

public:
  MCStreamerWrapper(MCContext &Context, mca::CodeRegions &R)
      : MCStreamer(Context), Regions(R) {}

  // We only want to intercept the emission of new instructions.
  void emitInstruction(const MCInst &Inst,
                       const MCSubtargetInfo & /* unused */) override {
    Regions.addInstruction(Inst);
  }

  bool emitSymbolAttribute(MCSymbol *Symbol, MCSymbolAttr Attribute) override {
    return true;
  }

  void emitCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                        Align ByteAlignment) override {}
  void emitZerofill(MCSection *Section, MCSymbol *Symbol = nullptr,
                    uint64_t Size = 0, Align ByteAlignment = Align(1),
                    SMLoc Loc = SMLoc()) override {}
  void emitGPRel32Value(const MCExpr *Value) override {}
  void beginCOFFSymbolDef(const MCSymbol *Symbol) override {}
  void emitCOFFSymbolStorageClass(int StorageClass) override {}
  void emitCOFFSymbolType(int Type) override {}
  void endCOFFSymbolDef() override {}

  ArrayRef<MCInst> GetInstructionSequence(unsigned Index) const {
    return Regions.getInstructionSequence(Index);
  }
};

class InstrumentMCStreamer : public MCStreamerWrapper {
  InstrumentManager &IM;

public:
  InstrumentMCStreamer(MCContext &Context, mca::InstrumentRegions &R,
                       InstrumentManager &IM)
      : MCStreamerWrapper(Context, R), IM(IM) {}

  void emitInstruction(const MCInst &Inst,
                       const MCSubtargetInfo &MCSI) override {
    MCStreamerWrapper::emitInstruction(Inst, MCSI);

    // We know that Regions is an InstrumentRegions by the constructor.
    for (UniqueInstrument &I : IM.createInstruments(Inst)) {
      StringRef InstrumentKind = I.get()->getDesc();
      // End InstrumentType region if one is open
      if (Regions.isRegionActive(InstrumentKind))
        Regions.endRegion(InstrumentKind, Inst.getLoc());
      // Start new instrumentation region
      Regions.beginRegion(InstrumentKind, Inst.getLoc(), std::move(I));
    }
  }
};

/// This abstract class is responsible for parsing the input given to
/// the llvm-mca driver, and converting that into a CodeRegions instance.
class CodeRegionGenerator {
protected:
  CodeRegionGenerator(const CodeRegionGenerator &) = delete;
  CodeRegionGenerator &operator=(const CodeRegionGenerator &) = delete;
  virtual Expected<const CodeRegions &>
  parseCodeRegions(const std::unique_ptr<MCInstPrinter> &IP,
                   bool SkipFailures) = 0;

public:
  CodeRegionGenerator() {}
  virtual ~CodeRegionGenerator();
};

/// Abastract CodeRegionGenerator with AnalysisRegions member
class AnalysisRegionGenerator : public virtual CodeRegionGenerator {
protected:
  AnalysisRegions Regions;

public:
  AnalysisRegionGenerator(llvm::SourceMgr &SM) : Regions(SM) {}

  virtual Expected<const AnalysisRegions &>
  parseAnalysisRegions(const std::unique_ptr<MCInstPrinter> &IP,
                       bool SkipFailures) = 0;
};

/// Abstract CodeRegionGenerator with InstrumentRegionsRegions member
class InstrumentRegionGenerator : public virtual CodeRegionGenerator {
protected:
  InstrumentRegions Regions;

public:
  InstrumentRegionGenerator(llvm::SourceMgr &SM) : Regions(SM) {}

  virtual Expected<const InstrumentRegions &>
  parseInstrumentRegions(const std::unique_ptr<MCInstPrinter> &IP,
                         bool SkipFailures) = 0;
};

/// This abstract class is responsible for parsing input ASM and
/// generating a CodeRegions instance.
class AsmCodeRegionGenerator : public virtual CodeRegionGenerator {
  const Target &TheTarget;
  const MCAsmInfo &MAI;
  const MCSubtargetInfo &STI;
  const MCInstrInfo &MCII;
  unsigned AssemblerDialect; // This is set during parsing.

protected:
  MCContext &Ctx;

public:
  AsmCodeRegionGenerator(const Target &T, MCContext &C, const MCAsmInfo &A,
                         const MCSubtargetInfo &S, const MCInstrInfo &I)
      : TheTarget(T), MAI(A), STI(S), MCII(I), AssemblerDialect(0), Ctx(C) {}

  virtual MCACommentConsumer *getCommentConsumer() = 0;
  virtual CodeRegions &getRegions() = 0;
  virtual MCStreamerWrapper *getMCStreamer() = 0;

  unsigned getAssemblerDialect() const { return AssemblerDialect; }
  Expected<const CodeRegions &>
  parseCodeRegions(const std::unique_ptr<MCInstPrinter> &IP,
                   bool SkipFailures) override;
};

class AsmAnalysisRegionGenerator final : public AnalysisRegionGenerator,
                                         public AsmCodeRegionGenerator {
  AnalysisRegionCommentConsumer CC;
  MCStreamerWrapper Streamer;

public:
  AsmAnalysisRegionGenerator(const Target &T, llvm::SourceMgr &SM, MCContext &C,
                             const MCAsmInfo &A, const MCSubtargetInfo &S,
                             const MCInstrInfo &I)
      : AnalysisRegionGenerator(SM), AsmCodeRegionGenerator(T, C, A, S, I),
        CC(Regions), Streamer(Ctx, Regions) {}

  MCACommentConsumer *getCommentConsumer() override { return &CC; };
  CodeRegions &getRegions() override { return Regions; };
  MCStreamerWrapper *getMCStreamer() override { return &Streamer; }

  Expected<const AnalysisRegions &>
  parseAnalysisRegions(const std::unique_ptr<MCInstPrinter> &IP,
                       bool SkipFailures) override {
    Expected<const CodeRegions &> RegionsOrErr =
        parseCodeRegions(IP, SkipFailures);
    if (!RegionsOrErr)
      return RegionsOrErr.takeError();
    else
      return static_cast<const AnalysisRegions &>(*RegionsOrErr);
  }

  Expected<const CodeRegions &>
  parseCodeRegions(const std::unique_ptr<MCInstPrinter> &IP,
                   bool SkipFailures) override {
    return AsmCodeRegionGenerator::parseCodeRegions(IP, SkipFailures);
  }
};

class AsmInstrumentRegionGenerator final : public InstrumentRegionGenerator,
                                           public AsmCodeRegionGenerator {
  InstrumentRegionCommentConsumer CC;
  InstrumentMCStreamer Streamer;

public:
  AsmInstrumentRegionGenerator(const Target &T, llvm::SourceMgr &SM,
                               MCContext &C, const MCAsmInfo &A,
                               const MCSubtargetInfo &S, const MCInstrInfo &I,
                               InstrumentManager &IM)
      : InstrumentRegionGenerator(SM), AsmCodeRegionGenerator(T, C, A, S, I),
        CC(SM, Regions, IM), Streamer(Ctx, Regions, IM) {}

  MCACommentConsumer *getCommentConsumer() override { return &CC; };
  CodeRegions &getRegions() override { return Regions; };
  MCStreamerWrapper *getMCStreamer() override { return &Streamer; }

  Expected<const InstrumentRegions &>
  parseInstrumentRegions(const std::unique_ptr<MCInstPrinter> &IP,
                         bool SkipFailures) override {
    Expected<const CodeRegions &> RegionsOrErr =
        parseCodeRegions(IP, SkipFailures);
    if (!RegionsOrErr)
      return RegionsOrErr.takeError();
    else
      return static_cast<const InstrumentRegions &>(*RegionsOrErr);
  }

  Expected<const CodeRegions &>
  parseCodeRegions(const std::unique_ptr<MCInstPrinter> &IP,
                   bool SkipFailures) override {
    return AsmCodeRegionGenerator::parseCodeRegions(IP, SkipFailures);
  }
};

} // namespace mca
} // namespace llvm

#endif // LLVM_TOOLS_LLVM_MCA_CODEREGION_GENERATOR_H
