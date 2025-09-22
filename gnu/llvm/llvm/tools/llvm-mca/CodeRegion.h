//===-------------------------- CodeRegion.h -------------------*- C++ -* -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file implements class CodeRegion and CodeRegions, InstrumentRegion,
/// AnalysisRegions, and InstrumentRegions.
///
/// A CodeRegion describes a region of assembly code guarded by special LLVM-MCA
/// comment directives.
///
///   # LLVM-MCA-BEGIN foo
///     ...  ## asm
///   # LLVM-MCA-END
///
/// A comment starting with substring LLVM-MCA-BEGIN marks the beginning of a
/// new region of code.
/// A comment starting with substring LLVM-MCA-END marks the end of the
/// last-seen region of code.
///
/// Code regions are not allowed to overlap. Each region can have a optional
/// description; internally, regions are described by a range of source
/// locations (SMLoc objects).
///
/// An instruction (a MCInst) is added to a CodeRegion R only if its
/// location is in range [R.RangeStart, R.RangeEnd].
///
/// A InstrumentRegion describes a region of assembly code guarded by
/// special LLVM-MCA comment directives.
///
///   # LLVM-MCA-<INSTRUMENTATION_TYPE> <data>
///     ...  ## asm
///
/// where INSTRUMENTATION_TYPE is a type defined in llvm and expects to use
/// data.
///
/// A comment starting with substring LLVM-MCA-<INSTRUMENTATION_TYPE>
/// brings data into scope for llvm-mca to use in its analysis for
/// all following instructions.
///
/// If the same INSTRUMENTATION_TYPE is found later in the instruction list,
/// then the original InstrumentRegion will be automatically ended,
/// and a new InstrumentRegion will begin.
///
/// If there are comments containing the different INSTRUMENTATION_TYPEs,
/// then both data sets remain available. In contrast with a CodeRegion,
/// an InstrumentRegion does not need a comment to end the region.
//
// An instruction (a MCInst) is added to an InstrumentRegion R only
// if its location is in range [R.RangeStart, R.RangeEnd].
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_MCA_CODEREGION_H
#define LLVM_TOOLS_LLVM_MCA_CODEREGION_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MCA/CustomBehaviour.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/Support/SourceMgr.h"
#include <vector>

namespace llvm {
namespace mca {

/// A region of assembly code.
///
/// It identifies a sequence of machine instructions.
class CodeRegion {
  // An optional descriptor for this region.
  llvm::StringRef Description;
  // Instructions that form this region.
  llvm::SmallVector<llvm::MCInst, 16> Instructions;
  // Source location range.
  llvm::SMLoc RangeStart;
  llvm::SMLoc RangeEnd;

  CodeRegion(const CodeRegion &) = delete;
  CodeRegion &operator=(const CodeRegion &) = delete;

public:
  CodeRegion(llvm::StringRef Desc, llvm::SMLoc Start)
      : Description(Desc), RangeStart(Start) {}

  virtual ~CodeRegion() = default;

  void addInstruction(const llvm::MCInst &Instruction) {
    Instructions.emplace_back(Instruction);
  }

  // Remove the given instructions from the set, for unsupported instructions
  // being skipped. Returns an ArrayRef for the updated vector of Instructions.
  [[nodiscard]] llvm::ArrayRef<llvm::MCInst>
  dropInstructions(const llvm::SmallPtrSetImpl<const llvm::MCInst *> &Insts) {
    if (Insts.empty())
      return Instructions;
    llvm::erase_if(Instructions, [&Insts](const llvm::MCInst &Inst) {
      return Insts.contains(&Inst);
    });
    return Instructions;
  }

  llvm::SMLoc startLoc() const { return RangeStart; }
  llvm::SMLoc endLoc() const { return RangeEnd; }

  void setEndLocation(llvm::SMLoc End) { RangeEnd = End; }
  bool empty() const { return Instructions.empty(); }
  bool isLocInRange(llvm::SMLoc Loc) const;

  llvm::ArrayRef<llvm::MCInst> getInstructions() const { return Instructions; }

  llvm::StringRef getDescription() const { return Description; }
};

/// Alias AnalysisRegion with CodeRegion since CodeRegionGenerator
/// is absract and AnalysisRegionGenerator operates on AnalysisRegions
using AnalysisRegion = CodeRegion;

/// A CodeRegion that contains instrumentation that can be used
/// in analysis of the region.
class InstrumentRegion : public CodeRegion {
  /// Instrument for this region.
  UniqueInstrument I;

public:
  InstrumentRegion(llvm::StringRef Desc, llvm::SMLoc Start, UniqueInstrument I)
      : CodeRegion(Desc, Start), I(std::move(I)) {}

public:
  Instrument *getInstrument() const { return I.get(); }
};

class CodeRegionParseError final : public Error {};

class CodeRegions {
  CodeRegions(const CodeRegions &) = delete;
  CodeRegions &operator=(const CodeRegions &) = delete;

protected:
  // A source manager. Used by the tool to generate meaningful warnings.
  llvm::SourceMgr &SM;

  using UniqueCodeRegion = std::unique_ptr<CodeRegion>;
  std::vector<UniqueCodeRegion> Regions;
  llvm::StringMap<unsigned> ActiveRegions;
  bool FoundErrors;

public:
  CodeRegions(llvm::SourceMgr &S) : SM(S), FoundErrors(false) {}
  virtual ~CodeRegions() = default;

  typedef std::vector<UniqueCodeRegion>::iterator iterator;
  typedef std::vector<UniqueCodeRegion>::const_iterator const_iterator;

  iterator begin() { return Regions.begin(); }
  iterator end() { return Regions.end(); }
  const_iterator begin() const { return Regions.cbegin(); }
  const_iterator end() const { return Regions.cend(); }

  void addInstruction(const llvm::MCInst &Instruction);
  llvm::SourceMgr &getSourceMgr() const { return SM; }

  llvm::ArrayRef<llvm::MCInst> getInstructionSequence(unsigned Idx) const {
    return Regions[Idx]->getInstructions();
  }

  bool empty() const {
    return llvm::all_of(Regions, [](const UniqueCodeRegion &Region) {
      return Region->empty();
    });
  }

  bool isValid() const { return !FoundErrors; }

  bool isRegionActive(llvm::StringRef Description) const {
    return ActiveRegions.contains(Description);
  }

  virtual void beginRegion(llvm::StringRef Description, llvm::SMLoc Loc) = 0;
  virtual void beginRegion(llvm::StringRef Description, llvm::SMLoc Loc,
                           UniqueInstrument Instrument) = 0;
  virtual void endRegion(llvm::StringRef Description, llvm::SMLoc Loc) = 0;
};

struct AnalysisRegions : public CodeRegions {
  AnalysisRegions(llvm::SourceMgr &S);

  void beginRegion(llvm::StringRef Description, llvm::SMLoc Loc) override;
  void beginRegion(llvm::StringRef Description, llvm::SMLoc Loc,
                   UniqueInstrument Instrument) override {}
  void endRegion(llvm::StringRef Description, llvm::SMLoc Loc) override;
};

struct InstrumentRegions : public CodeRegions {

  InstrumentRegions(llvm::SourceMgr &S);

  void beginRegion(llvm::StringRef Description, llvm::SMLoc Loc) override{};
  void beginRegion(llvm::StringRef Description, llvm::SMLoc Loc,
                   UniqueInstrument Instrument) override;
  void endRegion(llvm::StringRef Description, llvm::SMLoc Loc) override;

  const SmallVector<Instrument *> getActiveInstruments(llvm::SMLoc Loc) const;
};

} // namespace mca
} // namespace llvm

#endif
