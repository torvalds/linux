//===---- MachineOutliner.cpp - Outline instructions -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Replaces repeated sequences of instructions with function calls.
///
/// This works by placing every instruction from every basic block in a
/// suffix tree, and repeatedly querying that tree for repeated sequences of
/// instructions. If a sequence of instructions appears often, then it ought
/// to be beneficial to pull out into a function.
///
/// The MachineOutliner communicates with a given target using hooks defined in
/// TargetInstrInfo.h. The target supplies the outliner with information on how
/// a specific sequence of instructions should be outlined. This information
/// is used to deduce the number of instructions necessary to
///
/// * Create an outlined function
/// * Call that outlined function
///
/// Targets must implement
///   * getOutliningCandidateInfo
///   * buildOutlinedFrame
///   * insertOutlinedCall
///   * isFunctionSafeToOutlineFrom
///
/// in order to make use of the MachineOutliner.
///
/// This was originally presented at the 2016 LLVM Developers' Meeting in the
/// talk "Reducing Code Size Using Outlining". For a high-level overview of
/// how this pass works, the talk is available on YouTube at
///
/// https://www.youtube.com/watch?v=yorld-WSOeU
///
/// The slides for the talk are available at
///
/// http://www.llvm.org/devmtg/2016-11/Slides/Paquette-Outliner.pdf
///
/// The talk provides an overview of how the outliner finds candidates and
/// ultimately outlines them. It describes how the main data structure for this
/// pass, the suffix tree, is queried and purged for candidates. It also gives
/// a simplified suffix tree construction algorithm for suffix trees based off
/// of the algorithm actually used here, Ukkonen's algorithm.
///
/// For the original RFC for this pass, please see
///
/// http://lists.llvm.org/pipermail/llvm-dev/2016-August/104170.html
///
/// For more information on the suffix tree data structure, please see
/// https://www.cs.helsinki.fi/u/ukkonen/SuffixT1withFigs.pdf
///
//===----------------------------------------------------------------------===//
#include "llvm/CodeGen/MachineOutliner.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineOptimizationRemarkEmitter.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Module.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/SuffixTree.h"
#include "llvm/Support/raw_ostream.h"
#include <functional>
#include <tuple>
#include <vector>

#define DEBUG_TYPE "machine-outliner"

using namespace llvm;
using namespace ore;
using namespace outliner;

// Statistics for outlined functions.
STATISTIC(NumOutlined, "Number of candidates outlined");
STATISTIC(FunctionsCreated, "Number of functions created");

// Statistics for instruction mapping.
STATISTIC(NumLegalInUnsignedVec, "Outlinable instructions mapped");
STATISTIC(NumIllegalInUnsignedVec,
          "Unoutlinable instructions mapped + number of sentinel values");
STATISTIC(NumSentinels, "Sentinel values inserted during mapping");
STATISTIC(NumInvisible,
          "Invisible instructions skipped during mapping");
STATISTIC(UnsignedVecSize,
          "Total number of instructions mapped and saved to mapping vector");

// Set to true if the user wants the outliner to run on linkonceodr linkage
// functions. This is false by default because the linker can dedupe linkonceodr
// functions. Since the outliner is confined to a single module (modulo LTO),
// this is off by default. It should, however, be the default behaviour in
// LTO.
static cl::opt<bool> EnableLinkOnceODROutlining(
    "enable-linkonceodr-outlining", cl::Hidden,
    cl::desc("Enable the machine outliner on linkonceodr functions"),
    cl::init(false));

/// Number of times to re-run the outliner. This is not the total number of runs
/// as the outliner will run at least one time. The default value is set to 0,
/// meaning the outliner will run one time and rerun zero times after that.
static cl::opt<unsigned> OutlinerReruns(
    "machine-outliner-reruns", cl::init(0), cl::Hidden,
    cl::desc(
        "Number of times to rerun the outliner after the initial outline"));

static cl::opt<unsigned> OutlinerBenefitThreshold(
    "outliner-benefit-threshold", cl::init(1), cl::Hidden,
    cl::desc(
        "The minimum size in bytes before an outlining candidate is accepted"));

static cl::opt<bool> OutlinerLeafDescendants(
    "outliner-leaf-descendants", cl::init(true), cl::Hidden,
    cl::desc("Consider all leaf descendants of internal nodes of the suffix "
             "tree as candidates for outlining (if false, only leaf children "
             "are considered)"));

namespace {

/// Maps \p MachineInstrs to unsigned integers and stores the mappings.
struct InstructionMapper {

  /// The next available integer to assign to a \p MachineInstr that
  /// cannot be outlined.
  ///
  /// Set to -3 for compatability with \p DenseMapInfo<unsigned>.
  unsigned IllegalInstrNumber = -3;

  /// The next available integer to assign to a \p MachineInstr that can
  /// be outlined.
  unsigned LegalInstrNumber = 0;

  /// Correspondence from \p MachineInstrs to unsigned integers.
  DenseMap<MachineInstr *, unsigned, MachineInstrExpressionTrait>
      InstructionIntegerMap;

  /// Correspondence between \p MachineBasicBlocks and target-defined flags.
  DenseMap<MachineBasicBlock *, unsigned> MBBFlagsMap;

  /// The vector of unsigned integers that the module is mapped to.
  SmallVector<unsigned> UnsignedVec;

  /// Stores the location of the instruction associated with the integer
  /// at index i in \p UnsignedVec for each index i.
  SmallVector<MachineBasicBlock::iterator> InstrList;

  // Set if we added an illegal number in the previous step.
  // Since each illegal number is unique, we only need one of them between
  // each range of legal numbers. This lets us make sure we don't add more
  // than one illegal number per range.
  bool AddedIllegalLastTime = false;

  /// Maps \p *It to a legal integer.
  ///
  /// Updates \p CanOutlineWithPrevInstr, \p HaveLegalRange, \p InstrListForMBB,
  /// \p UnsignedVecForMBB, \p InstructionIntegerMap, and \p LegalInstrNumber.
  ///
  /// \returns The integer that \p *It was mapped to.
  unsigned mapToLegalUnsigned(
      MachineBasicBlock::iterator &It, bool &CanOutlineWithPrevInstr,
      bool &HaveLegalRange, unsigned &NumLegalInBlock,
      SmallVector<unsigned> &UnsignedVecForMBB,
      SmallVector<MachineBasicBlock::iterator> &InstrListForMBB) {
    // We added something legal, so we should unset the AddedLegalLastTime
    // flag.
    AddedIllegalLastTime = false;

    // If we have at least two adjacent legal instructions (which may have
    // invisible instructions in between), remember that.
    if (CanOutlineWithPrevInstr)
      HaveLegalRange = true;
    CanOutlineWithPrevInstr = true;

    // Keep track of the number of legal instructions we insert.
    NumLegalInBlock++;

    // Get the integer for this instruction or give it the current
    // LegalInstrNumber.
    InstrListForMBB.push_back(It);
    MachineInstr &MI = *It;
    bool WasInserted;
    DenseMap<MachineInstr *, unsigned, MachineInstrExpressionTrait>::iterator
        ResultIt;
    std::tie(ResultIt, WasInserted) =
        InstructionIntegerMap.insert(std::make_pair(&MI, LegalInstrNumber));
    unsigned MINumber = ResultIt->second;

    // There was an insertion.
    if (WasInserted)
      LegalInstrNumber++;

    UnsignedVecForMBB.push_back(MINumber);

    // Make sure we don't overflow or use any integers reserved by the DenseMap.
    if (LegalInstrNumber >= IllegalInstrNumber)
      report_fatal_error("Instruction mapping overflow!");

    assert(LegalInstrNumber != DenseMapInfo<unsigned>::getEmptyKey() &&
           "Tried to assign DenseMap tombstone or empty key to instruction.");
    assert(LegalInstrNumber != DenseMapInfo<unsigned>::getTombstoneKey() &&
           "Tried to assign DenseMap tombstone or empty key to instruction.");

    // Statistics.
    ++NumLegalInUnsignedVec;
    return MINumber;
  }

  /// Maps \p *It to an illegal integer.
  ///
  /// Updates \p InstrListForMBB, \p UnsignedVecForMBB, and \p
  /// IllegalInstrNumber.
  ///
  /// \returns The integer that \p *It was mapped to.
  unsigned mapToIllegalUnsigned(
      MachineBasicBlock::iterator &It, bool &CanOutlineWithPrevInstr,
      SmallVector<unsigned> &UnsignedVecForMBB,
      SmallVector<MachineBasicBlock::iterator> &InstrListForMBB) {
    // Can't outline an illegal instruction. Set the flag.
    CanOutlineWithPrevInstr = false;

    // Only add one illegal number per range of legal numbers.
    if (AddedIllegalLastTime)
      return IllegalInstrNumber;

    // Remember that we added an illegal number last time.
    AddedIllegalLastTime = true;
    unsigned MINumber = IllegalInstrNumber;

    InstrListForMBB.push_back(It);
    UnsignedVecForMBB.push_back(IllegalInstrNumber);
    IllegalInstrNumber--;
    // Statistics.
    ++NumIllegalInUnsignedVec;

    assert(LegalInstrNumber < IllegalInstrNumber &&
           "Instruction mapping overflow!");

    assert(IllegalInstrNumber != DenseMapInfo<unsigned>::getEmptyKey() &&
           "IllegalInstrNumber cannot be DenseMap tombstone or empty key!");

    assert(IllegalInstrNumber != DenseMapInfo<unsigned>::getTombstoneKey() &&
           "IllegalInstrNumber cannot be DenseMap tombstone or empty key!");

    return MINumber;
  }

  /// Transforms a \p MachineBasicBlock into a \p vector of \p unsigneds
  /// and appends it to \p UnsignedVec and \p InstrList.
  ///
  /// Two instructions are assigned the same integer if they are identical.
  /// If an instruction is deemed unsafe to outline, then it will be assigned an
  /// unique integer. The resulting mapping is placed into a suffix tree and
  /// queried for candidates.
  ///
  /// \param MBB The \p MachineBasicBlock to be translated into integers.
  /// \param TII \p TargetInstrInfo for the function.
  void convertToUnsignedVec(MachineBasicBlock &MBB,
                            const TargetInstrInfo &TII) {
    LLVM_DEBUG(dbgs() << "*** Converting MBB '" << MBB.getName()
                      << "' to unsigned vector ***\n");
    unsigned Flags = 0;

    // Don't even map in this case.
    if (!TII.isMBBSafeToOutlineFrom(MBB, Flags))
      return;

    auto OutlinableRanges = TII.getOutlinableRanges(MBB, Flags);
    LLVM_DEBUG(dbgs() << MBB.getName() << ": " << OutlinableRanges.size()
                      << " outlinable range(s)\n");
    if (OutlinableRanges.empty())
      return;

    // Store info for the MBB for later outlining.
    MBBFlagsMap[&MBB] = Flags;

    MachineBasicBlock::iterator It = MBB.begin();

    // The number of instructions in this block that will be considered for
    // outlining.
    unsigned NumLegalInBlock = 0;

    // True if we have at least two legal instructions which aren't separated
    // by an illegal instruction.
    bool HaveLegalRange = false;

    // True if we can perform outlining given the last mapped (non-invisible)
    // instruction. This lets us know if we have a legal range.
    bool CanOutlineWithPrevInstr = false;

    // FIXME: Should this all just be handled in the target, rather than using
    // repeated calls to getOutliningType?
    SmallVector<unsigned> UnsignedVecForMBB;
    SmallVector<MachineBasicBlock::iterator> InstrListForMBB;

    LLVM_DEBUG(dbgs() << "*** Mapping outlinable ranges ***\n");
    for (auto &OutlinableRange : OutlinableRanges) {
      auto OutlinableRangeBegin = OutlinableRange.first;
      auto OutlinableRangeEnd = OutlinableRange.second;
#ifndef NDEBUG
      LLVM_DEBUG(
          dbgs() << "Mapping "
                 << std::distance(OutlinableRangeBegin, OutlinableRangeEnd)
                 << " instruction range\n");
      // Everything outside of an outlinable range is illegal.
      unsigned NumSkippedInRange = 0;
#endif
      for (; It != OutlinableRangeBegin; ++It) {
#ifndef NDEBUG
        ++NumSkippedInRange;
#endif
        mapToIllegalUnsigned(It, CanOutlineWithPrevInstr, UnsignedVecForMBB,
                             InstrListForMBB);
      }
#ifndef NDEBUG
      LLVM_DEBUG(dbgs() << "Skipped " << NumSkippedInRange
                        << " instructions outside outlinable range\n");
#endif
      assert(It != MBB.end() && "Should still have instructions?");
      // `It` is now positioned at the beginning of a range of instructions
      // which may be outlinable. Check if each instruction is known to be safe.
      for (; It != OutlinableRangeEnd; ++It) {
        // Keep track of where this instruction is in the module.
        switch (TII.getOutliningType(It, Flags)) {
        case InstrType::Illegal:
          mapToIllegalUnsigned(It, CanOutlineWithPrevInstr, UnsignedVecForMBB,
                               InstrListForMBB);
          break;

        case InstrType::Legal:
          mapToLegalUnsigned(It, CanOutlineWithPrevInstr, HaveLegalRange,
                             NumLegalInBlock, UnsignedVecForMBB,
                             InstrListForMBB);
          break;

        case InstrType::LegalTerminator:
          mapToLegalUnsigned(It, CanOutlineWithPrevInstr, HaveLegalRange,
                             NumLegalInBlock, UnsignedVecForMBB,
                             InstrListForMBB);
          // The instruction also acts as a terminator, so we have to record
          // that in the string.
          mapToIllegalUnsigned(It, CanOutlineWithPrevInstr, UnsignedVecForMBB,
                               InstrListForMBB);
          break;

        case InstrType::Invisible:
          // Normally this is set by mapTo(Blah)Unsigned, but we just want to
          // skip this instruction. So, unset the flag here.
          ++NumInvisible;
          AddedIllegalLastTime = false;
          break;
        }
      }
    }

    LLVM_DEBUG(dbgs() << "HaveLegalRange = " << HaveLegalRange << "\n");

    // Are there enough legal instructions in the block for outlining to be
    // possible?
    if (HaveLegalRange) {
      // After we're done every insertion, uniquely terminate this part of the
      // "string". This makes sure we won't match across basic block or function
      // boundaries since the "end" is encoded uniquely and thus appears in no
      // repeated substring.
      mapToIllegalUnsigned(It, CanOutlineWithPrevInstr, UnsignedVecForMBB,
                           InstrListForMBB);
      ++NumSentinels;
      append_range(InstrList, InstrListForMBB);
      append_range(UnsignedVec, UnsignedVecForMBB);
    }
  }

  InstructionMapper() {
    // Make sure that the implementation of DenseMapInfo<unsigned> hasn't
    // changed.
    assert(DenseMapInfo<unsigned>::getEmptyKey() == (unsigned)-1 &&
           "DenseMapInfo<unsigned>'s empty key isn't -1!");
    assert(DenseMapInfo<unsigned>::getTombstoneKey() == (unsigned)-2 &&
           "DenseMapInfo<unsigned>'s tombstone key isn't -2!");
  }
};

/// An interprocedural pass which finds repeated sequences of
/// instructions and replaces them with calls to functions.
///
/// Each instruction is mapped to an unsigned integer and placed in a string.
/// The resulting mapping is then placed in a \p SuffixTree. The \p SuffixTree
/// is then repeatedly queried for repeated sequences of instructions. Each
/// non-overlapping repeated sequence is then placed in its own
/// \p MachineFunction and each instance is then replaced with a call to that
/// function.
struct MachineOutliner : public ModulePass {

  static char ID;

  /// Set to true if the outliner should consider functions with
  /// linkonceodr linkage.
  bool OutlineFromLinkOnceODRs = false;

  /// The current repeat number of machine outlining.
  unsigned OutlineRepeatedNum = 0;

  /// Set to true if the outliner should run on all functions in the module
  /// considered safe for outlining.
  /// Set to true by default for compatibility with llc's -run-pass option.
  /// Set when the pass is constructed in TargetPassConfig.
  bool RunOnAllFunctions = true;

  StringRef getPassName() const override { return "Machine Outliner"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineModuleInfoWrapperPass>();
    AU.addPreserved<MachineModuleInfoWrapperPass>();
    AU.setPreservesAll();
    ModulePass::getAnalysisUsage(AU);
  }

  MachineOutliner() : ModulePass(ID) {
    initializeMachineOutlinerPass(*PassRegistry::getPassRegistry());
  }

  /// Remark output explaining that not outlining a set of candidates would be
  /// better than outlining that set.
  void emitNotOutliningCheaperRemark(
      unsigned StringLen, std::vector<Candidate> &CandidatesForRepeatedSeq,
      OutlinedFunction &OF);

  /// Remark output explaining that a function was outlined.
  void emitOutlinedFunctionRemark(OutlinedFunction &OF);

  /// Find all repeated substrings that satisfy the outlining cost model by
  /// constructing a suffix tree.
  ///
  /// If a substring appears at least twice, then it must be represented by
  /// an internal node which appears in at least two suffixes. Each suffix
  /// is represented by a leaf node. To do this, we visit each internal node
  /// in the tree, using the leaf children of each internal node. If an
  /// internal node represents a beneficial substring, then we use each of
  /// its leaf children to find the locations of its substring.
  ///
  /// \param Mapper Contains outlining mapping information.
  /// \param[out] FunctionList Filled with a list of \p OutlinedFunctions
  /// each type of candidate.
  void findCandidates(InstructionMapper &Mapper,
                      std::vector<OutlinedFunction> &FunctionList);

  /// Replace the sequences of instructions represented by \p OutlinedFunctions
  /// with calls to functions.
  ///
  /// \param M The module we are outlining from.
  /// \param FunctionList A list of functions to be inserted into the module.
  /// \param Mapper Contains the instruction mappings for the module.
  bool outline(Module &M, std::vector<OutlinedFunction> &FunctionList,
               InstructionMapper &Mapper, unsigned &OutlinedFunctionNum);

  /// Creates a function for \p OF and inserts it into the module.
  MachineFunction *createOutlinedFunction(Module &M, OutlinedFunction &OF,
                                          InstructionMapper &Mapper,
                                          unsigned Name);

  /// Calls 'doOutline()' 1 + OutlinerReruns times.
  bool runOnModule(Module &M) override;

  /// Construct a suffix tree on the instructions in \p M and outline repeated
  /// strings from that tree.
  bool doOutline(Module &M, unsigned &OutlinedFunctionNum);

  /// Return a DISubprogram for OF if one exists, and null otherwise. Helper
  /// function for remark emission.
  DISubprogram *getSubprogramOrNull(const OutlinedFunction &OF) {
    for (const Candidate &C : OF.Candidates)
      if (MachineFunction *MF = C.getMF())
        if (DISubprogram *SP = MF->getFunction().getSubprogram())
          return SP;
    return nullptr;
  }

  /// Populate and \p InstructionMapper with instruction-to-integer mappings.
  /// These are used to construct a suffix tree.
  void populateMapper(InstructionMapper &Mapper, Module &M,
                      MachineModuleInfo &MMI);

  /// Initialize information necessary to output a size remark.
  /// FIXME: This should be handled by the pass manager, not the outliner.
  /// FIXME: This is nearly identical to the initSizeRemarkInfo in the legacy
  /// pass manager.
  void initSizeRemarkInfo(const Module &M, const MachineModuleInfo &MMI,
                          StringMap<unsigned> &FunctionToInstrCount);

  /// Emit the remark.
  // FIXME: This should be handled by the pass manager, not the outliner.
  void
  emitInstrCountChangedRemark(const Module &M, const MachineModuleInfo &MMI,
                              const StringMap<unsigned> &FunctionToInstrCount);
};
} // Anonymous namespace.

char MachineOutliner::ID = 0;

namespace llvm {
ModulePass *createMachineOutlinerPass(bool RunOnAllFunctions) {
  MachineOutliner *OL = new MachineOutliner();
  OL->RunOnAllFunctions = RunOnAllFunctions;
  return OL;
}

} // namespace llvm

INITIALIZE_PASS(MachineOutliner, DEBUG_TYPE, "Machine Function Outliner", false,
                false)

void MachineOutliner::emitNotOutliningCheaperRemark(
    unsigned StringLen, std::vector<Candidate> &CandidatesForRepeatedSeq,
    OutlinedFunction &OF) {
  // FIXME: Right now, we arbitrarily choose some Candidate from the
  // OutlinedFunction. This isn't necessarily fixed, nor does it have to be.
  // We should probably sort these by function name or something to make sure
  // the remarks are stable.
  Candidate &C = CandidatesForRepeatedSeq.front();
  MachineOptimizationRemarkEmitter MORE(*(C.getMF()), nullptr);
  MORE.emit([&]() {
    MachineOptimizationRemarkMissed R(DEBUG_TYPE, "NotOutliningCheaper",
                                      C.front().getDebugLoc(), C.getMBB());
    R << "Did not outline " << NV("Length", StringLen) << " instructions"
      << " from " << NV("NumOccurrences", CandidatesForRepeatedSeq.size())
      << " locations."
      << " Bytes from outlining all occurrences ("
      << NV("OutliningCost", OF.getOutliningCost()) << ")"
      << " >= Unoutlined instruction bytes ("
      << NV("NotOutliningCost", OF.getNotOutlinedCost()) << ")"
      << " (Also found at: ";

    // Tell the user the other places the candidate was found.
    for (unsigned i = 1, e = CandidatesForRepeatedSeq.size(); i < e; i++) {
      R << NV((Twine("OtherStartLoc") + Twine(i)).str(),
              CandidatesForRepeatedSeq[i].front().getDebugLoc());
      if (i != e - 1)
        R << ", ";
    }

    R << ")";
    return R;
  });
}

void MachineOutliner::emitOutlinedFunctionRemark(OutlinedFunction &OF) {
  MachineBasicBlock *MBB = &*OF.MF->begin();
  MachineOptimizationRemarkEmitter MORE(*OF.MF, nullptr);
  MachineOptimizationRemark R(DEBUG_TYPE, "OutlinedFunction",
                              MBB->findDebugLoc(MBB->begin()), MBB);
  R << "Saved " << NV("OutliningBenefit", OF.getBenefit()) << " bytes by "
    << "outlining " << NV("Length", OF.getNumInstrs()) << " instructions "
    << "from " << NV("NumOccurrences", OF.getOccurrenceCount())
    << " locations. "
    << "(Found at: ";

  // Tell the user the other places the candidate was found.
  for (size_t i = 0, e = OF.Candidates.size(); i < e; i++) {

    R << NV((Twine("StartLoc") + Twine(i)).str(),
            OF.Candidates[i].front().getDebugLoc());
    if (i != e - 1)
      R << ", ";
  }

  R << ")";

  MORE.emit(R);
}

void MachineOutliner::findCandidates(
    InstructionMapper &Mapper, std::vector<OutlinedFunction> &FunctionList) {
  FunctionList.clear();
  SuffixTree ST(Mapper.UnsignedVec, OutlinerLeafDescendants);

  // First, find all of the repeated substrings in the tree of minimum length
  // 2.
  std::vector<Candidate> CandidatesForRepeatedSeq;
  LLVM_DEBUG(dbgs() << "*** Discarding overlapping candidates *** \n");
  LLVM_DEBUG(
      dbgs() << "Searching for overlaps in all repeated sequences...\n");
  for (SuffixTree::RepeatedSubstring &RS : ST) {
    CandidatesForRepeatedSeq.clear();
    unsigned StringLen = RS.Length;
    LLVM_DEBUG(dbgs() << "  Sequence length: " << StringLen << "\n");
    // Debug code to keep track of how many candidates we removed.
#ifndef NDEBUG
    unsigned NumDiscarded = 0;
    unsigned NumKept = 0;
#endif
    // Sort the start indices so that we can efficiently check if candidates
    // overlap with the ones we've already found for this sequence.
    llvm::sort(RS.StartIndices);
    for (const unsigned &StartIdx : RS.StartIndices) {
      // Trick: Discard some candidates that would be incompatible with the
      // ones we've already found for this sequence. This will save us some
      // work in candidate selection.
      //
      // If two candidates overlap, then we can't outline them both. This
      // happens when we have candidates that look like, say
      //
      // AA (where each "A" is an instruction).
      //
      // We might have some portion of the module that looks like this:
      // AAAAAA (6 A's)
      //
      // In this case, there are 5 different copies of "AA" in this range, but
      // at most 3 can be outlined. If only outlining 3 of these is going to
      // be unbeneficial, then we ought to not bother.
      //
      // Note that two things DON'T overlap when they look like this:
      // start1...end1 .... start2...end2
      // That is, one must either
      // * End before the other starts
      // * Start after the other ends
      unsigned EndIdx = StartIdx + StringLen - 1;
      if (!CandidatesForRepeatedSeq.empty() &&
          StartIdx <= CandidatesForRepeatedSeq.back().getEndIdx()) {
#ifndef NDEBUG
        ++NumDiscarded;
        LLVM_DEBUG(dbgs() << "    .. DISCARD candidate @ [" << StartIdx << ", "
                          << EndIdx << "]; overlaps with candidate @ ["
                          << CandidatesForRepeatedSeq.back().getStartIdx()
                          << ", " << CandidatesForRepeatedSeq.back().getEndIdx()
                          << "]\n");
#endif
        continue;
      }
      // It doesn't overlap with anything, so we can outline it.
      // Each sequence is over [StartIt, EndIt].
      // Save the candidate and its location.
#ifndef NDEBUG
      ++NumKept;
#endif
      MachineBasicBlock::iterator StartIt = Mapper.InstrList[StartIdx];
      MachineBasicBlock::iterator EndIt = Mapper.InstrList[EndIdx];
      MachineBasicBlock *MBB = StartIt->getParent();
      CandidatesForRepeatedSeq.emplace_back(StartIdx, StringLen, StartIt, EndIt,
                                            MBB, FunctionList.size(),
                                            Mapper.MBBFlagsMap[MBB]);
    }
#ifndef NDEBUG
    LLVM_DEBUG(dbgs() << "    Candidates discarded: " << NumDiscarded
                      << "\n");
    LLVM_DEBUG(dbgs() << "    Candidates kept: " << NumKept << "\n\n");
#endif

    // We've found something we might want to outline.
    // Create an OutlinedFunction to store it and check if it'd be beneficial
    // to outline.
    if (CandidatesForRepeatedSeq.size() < 2)
      continue;

    // Arbitrarily choose a TII from the first candidate.
    // FIXME: Should getOutliningCandidateInfo move to TargetMachine?
    const TargetInstrInfo *TII =
        CandidatesForRepeatedSeq[0].getMF()->getSubtarget().getInstrInfo();

    std::optional<OutlinedFunction> OF =
        TII->getOutliningCandidateInfo(CandidatesForRepeatedSeq);

    // If we deleted too many candidates, then there's nothing worth outlining.
    // FIXME: This should take target-specified instruction sizes into account.
    if (!OF || OF->Candidates.size() < 2)
      continue;

    // Is it better to outline this candidate than not?
    if (OF->getBenefit() < OutlinerBenefitThreshold) {
      emitNotOutliningCheaperRemark(StringLen, CandidatesForRepeatedSeq, *OF);
      continue;
    }

    FunctionList.push_back(*OF);
  }
}

MachineFunction *MachineOutliner::createOutlinedFunction(
    Module &M, OutlinedFunction &OF, InstructionMapper &Mapper, unsigned Name) {

  // Create the function name. This should be unique.
  // FIXME: We should have a better naming scheme. This should be stable,
  // regardless of changes to the outliner's cost model/traversal order.
  std::string FunctionName = "OUTLINED_FUNCTION_";
  if (OutlineRepeatedNum > 0)
    FunctionName += std::to_string(OutlineRepeatedNum + 1) + "_";
  FunctionName += std::to_string(Name);
  LLVM_DEBUG(dbgs() << "NEW FUNCTION: " << FunctionName << "\n");

  // Create the function using an IR-level function.
  LLVMContext &C = M.getContext();
  Function *F = Function::Create(FunctionType::get(Type::getVoidTy(C), false),
                                 Function::ExternalLinkage, FunctionName, M);

  // NOTE: If this is linkonceodr, then we can take advantage of linker deduping
  // which gives us better results when we outline from linkonceodr functions.
  F->setLinkage(GlobalValue::InternalLinkage);
  F->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);

  // Set optsize/minsize, so we don't insert padding between outlined
  // functions.
  F->addFnAttr(Attribute::OptimizeForSize);
  F->addFnAttr(Attribute::MinSize);

  Candidate &FirstCand = OF.Candidates.front();
  const TargetInstrInfo &TII =
      *FirstCand.getMF()->getSubtarget().getInstrInfo();

  TII.mergeOutliningCandidateAttributes(*F, OF.Candidates);

  // Set uwtable, so we generate eh_frame.
  UWTableKind UW = std::accumulate(
      OF.Candidates.cbegin(), OF.Candidates.cend(), UWTableKind::None,
      [](UWTableKind K, const outliner::Candidate &C) {
        return std::max(K, C.getMF()->getFunction().getUWTableKind());
      });
  F->setUWTableKind(UW);

  BasicBlock *EntryBB = BasicBlock::Create(C, "entry", F);
  IRBuilder<> Builder(EntryBB);
  Builder.CreateRetVoid();

  MachineModuleInfo &MMI = getAnalysis<MachineModuleInfoWrapperPass>().getMMI();
  MachineFunction &MF = MMI.getOrCreateMachineFunction(*F);
  MF.setIsOutlined(true);
  MachineBasicBlock &MBB = *MF.CreateMachineBasicBlock();

  // Insert the new function into the module.
  MF.insert(MF.begin(), &MBB);

  MachineFunction *OriginalMF = FirstCand.front().getMF();
  const std::vector<MCCFIInstruction> &Instrs =
      OriginalMF->getFrameInstructions();
  for (auto &MI : FirstCand) {
    if (MI.isDebugInstr())
      continue;

    // Don't keep debug information for outlined instructions.
    auto DL = DebugLoc();
    if (MI.isCFIInstruction()) {
      unsigned CFIIndex = MI.getOperand(0).getCFIIndex();
      MCCFIInstruction CFI = Instrs[CFIIndex];
      BuildMI(MBB, MBB.end(), DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(MF.addFrameInst(CFI));
    } else {
      MachineInstr *NewMI = MF.CloneMachineInstr(&MI);
      NewMI->dropMemRefs(MF);
      NewMI->setDebugLoc(DL);
      MBB.insert(MBB.end(), NewMI);
    }
  }

  // Set normal properties for a late MachineFunction.
  MF.getProperties().reset(MachineFunctionProperties::Property::IsSSA);
  MF.getProperties().set(MachineFunctionProperties::Property::NoPHIs);
  MF.getProperties().set(MachineFunctionProperties::Property::NoVRegs);
  MF.getProperties().set(MachineFunctionProperties::Property::TracksLiveness);
  MF.getRegInfo().freezeReservedRegs();

  // Compute live-in set for outlined fn
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetRegisterInfo &TRI = *MRI.getTargetRegisterInfo();
  LivePhysRegs LiveIns(TRI);
  for (auto &Cand : OF.Candidates) {
    // Figure out live-ins at the first instruction.
    MachineBasicBlock &OutlineBB = *Cand.front().getParent();
    LivePhysRegs CandLiveIns(TRI);
    CandLiveIns.addLiveOuts(OutlineBB);
    for (const MachineInstr &MI :
         reverse(make_range(Cand.begin(), OutlineBB.end())))
      CandLiveIns.stepBackward(MI);

    // The live-in set for the outlined function is the union of the live-ins
    // from all the outlining points.
    for (MCPhysReg Reg : CandLiveIns)
      LiveIns.addReg(Reg);
  }
  addLiveIns(MBB, LiveIns);

  TII.buildOutlinedFrame(MBB, MF, OF);

  // If there's a DISubprogram associated with this outlined function, then
  // emit debug info for the outlined function.
  if (DISubprogram *SP = getSubprogramOrNull(OF)) {
    // We have a DISubprogram. Get its DICompileUnit.
    DICompileUnit *CU = SP->getUnit();
    DIBuilder DB(M, true, CU);
    DIFile *Unit = SP->getFile();
    Mangler Mg;
    // Get the mangled name of the function for the linkage name.
    std::string Dummy;
    raw_string_ostream MangledNameStream(Dummy);
    Mg.getNameWithPrefix(MangledNameStream, F, false);

    DISubprogram *OutlinedSP = DB.createFunction(
        Unit /* Context */, F->getName(), StringRef(Dummy), Unit /* File */,
        0 /* Line 0 is reserved for compiler-generated code. */,
        DB.createSubroutineType(
            DB.getOrCreateTypeArray(std::nullopt)), /* void type */
        0, /* Line 0 is reserved for compiler-generated code. */
        DINode::DIFlags::FlagArtificial /* Compiler-generated code. */,
        /* Outlined code is optimized code by definition. */
        DISubprogram::SPFlagDefinition | DISubprogram::SPFlagOptimized);

    // Don't add any new variables to the subprogram.
    DB.finalizeSubprogram(OutlinedSP);

    // Attach subprogram to the function.
    F->setSubprogram(OutlinedSP);
    // We're done with the DIBuilder.
    DB.finalize();
  }

  return &MF;
}

bool MachineOutliner::outline(Module &M,
                              std::vector<OutlinedFunction> &FunctionList,
                              InstructionMapper &Mapper,
                              unsigned &OutlinedFunctionNum) {
  LLVM_DEBUG(dbgs() << "*** Outlining ***\n");
  LLVM_DEBUG(dbgs() << "NUMBER OF POTENTIAL FUNCTIONS: " << FunctionList.size()
                    << "\n");
  bool OutlinedSomething = false;

  // Sort by priority where priority := getNotOutlinedCost / getOutliningCost.
  // The function with highest priority should be outlined first.
  stable_sort(FunctionList,
              [](const OutlinedFunction &LHS, const OutlinedFunction &RHS) {
                return LHS.getNotOutlinedCost() * RHS.getOutliningCost() >
                       RHS.getNotOutlinedCost() * LHS.getOutliningCost();
              });

  // Walk over each function, outlining them as we go along. Functions are
  // outlined greedily, based off the sort above.
  auto *UnsignedVecBegin = Mapper.UnsignedVec.begin();
  LLVM_DEBUG(dbgs() << "WALKING FUNCTION LIST\n");
  for (OutlinedFunction &OF : FunctionList) {
#ifndef NDEBUG
    auto NumCandidatesBefore = OF.Candidates.size();
#endif
    // If we outlined something that overlapped with a candidate in a previous
    // step, then we can't outline from it.
    erase_if(OF.Candidates, [&UnsignedVecBegin](Candidate &C) {
      return std::any_of(UnsignedVecBegin + C.getStartIdx(),
                         UnsignedVecBegin + C.getEndIdx() + 1, [](unsigned I) {
                           return I == static_cast<unsigned>(-1);
                         });
    });

#ifndef NDEBUG
    auto NumCandidatesAfter = OF.Candidates.size();
    LLVM_DEBUG(dbgs() << "PRUNED: " << NumCandidatesBefore - NumCandidatesAfter
                      << "/" << NumCandidatesBefore << " candidates\n");
#endif

    // If we made it unbeneficial to outline this function, skip it.
    if (OF.getBenefit() < OutlinerBenefitThreshold) {
      LLVM_DEBUG(dbgs() << "SKIP: Expected benefit (" << OF.getBenefit()
                        << " B) < threshold (" << OutlinerBenefitThreshold
                        << " B)\n");
      continue;
    }

    LLVM_DEBUG(dbgs() << "OUTLINE: Expected benefit (" << OF.getBenefit()
                      << " B) > threshold (" << OutlinerBenefitThreshold
                      << " B)\n");

    // It's beneficial. Create the function and outline its sequence's
    // occurrences.
    OF.MF = createOutlinedFunction(M, OF, Mapper, OutlinedFunctionNum);
    emitOutlinedFunctionRemark(OF);
    FunctionsCreated++;
    OutlinedFunctionNum++; // Created a function, move to the next name.
    MachineFunction *MF = OF.MF;
    const TargetSubtargetInfo &STI = MF->getSubtarget();
    const TargetInstrInfo &TII = *STI.getInstrInfo();

    // Replace occurrences of the sequence with calls to the new function.
    LLVM_DEBUG(dbgs() << "CREATE OUTLINED CALLS\n");
    for (Candidate &C : OF.Candidates) {
      MachineBasicBlock &MBB = *C.getMBB();
      MachineBasicBlock::iterator StartIt = C.begin();
      MachineBasicBlock::iterator EndIt = std::prev(C.end());

      // Insert the call.
      auto CallInst = TII.insertOutlinedCall(M, MBB, StartIt, *MF, C);
// Insert the call.
#ifndef NDEBUG
      auto MBBBeingOutlinedFromName =
          MBB.getName().empty() ? "<unknown>" : MBB.getName().str();
      auto MFBeingOutlinedFromName = MBB.getParent()->getName().empty()
                                         ? "<unknown>"
                                         : MBB.getParent()->getName().str();
      LLVM_DEBUG(dbgs() << "  CALL: " << MF->getName() << " in "
                        << MFBeingOutlinedFromName << ":"
                        << MBBBeingOutlinedFromName << "\n");
      LLVM_DEBUG(dbgs() << "   .. " << *CallInst);
#endif

      // If the caller tracks liveness, then we need to make sure that
      // anything we outline doesn't break liveness assumptions. The outlined
      // functions themselves currently don't track liveness, but we should
      // make sure that the ranges we yank things out of aren't wrong.
      if (MBB.getParent()->getProperties().hasProperty(
              MachineFunctionProperties::Property::TracksLiveness)) {
        // The following code is to add implicit def operands to the call
        // instruction. It also updates call site information for moved
        // code.
        SmallSet<Register, 2> UseRegs, DefRegs;
        // Copy over the defs in the outlined range.
        // First inst in outlined range <-- Anything that's defined in this
        // ...                           .. range has to be added as an
        // implicit Last inst in outlined range  <-- def to the call
        // instruction. Also remove call site information for outlined block
        // of code. The exposed uses need to be copied in the outlined range.
        for (MachineBasicBlock::reverse_iterator
                 Iter = EndIt.getReverse(),
                 Last = std::next(CallInst.getReverse());
             Iter != Last; Iter++) {
          MachineInstr *MI = &*Iter;
          SmallSet<Register, 2> InstrUseRegs;
          for (MachineOperand &MOP : MI->operands()) {
            // Skip over anything that isn't a register.
            if (!MOP.isReg())
              continue;

            if (MOP.isDef()) {
              // Introduce DefRegs set to skip the redundant register.
              DefRegs.insert(MOP.getReg());
              if (UseRegs.count(MOP.getReg()) &&
                  !InstrUseRegs.count(MOP.getReg()))
                // Since the regiester is modeled as defined,
                // it is not necessary to be put in use register set.
                UseRegs.erase(MOP.getReg());
            } else if (!MOP.isUndef()) {
              // Any register which is not undefined should
              // be put in the use register set.
              UseRegs.insert(MOP.getReg());
              InstrUseRegs.insert(MOP.getReg());
            }
          }
          if (MI->isCandidateForCallSiteEntry())
            MI->getMF()->eraseCallSiteInfo(MI);
        }

        for (const Register &I : DefRegs)
          // If it's a def, add it to the call instruction.
          CallInst->addOperand(
              MachineOperand::CreateReg(I, true, /* isDef = true */
                                        true /* isImp = true */));

        for (const Register &I : UseRegs)
          // If it's a exposed use, add it to the call instruction.
          CallInst->addOperand(
              MachineOperand::CreateReg(I, false, /* isDef = false */
                                        true /* isImp = true */));
      }

      // Erase from the point after where the call was inserted up to, and
      // including, the final instruction in the sequence.
      // Erase needs one past the end, so we need std::next there too.
      MBB.erase(std::next(StartIt), std::next(EndIt));

      // Keep track of what we removed by marking them all as -1.
      for (unsigned &I : make_range(UnsignedVecBegin + C.getStartIdx(),
                                    UnsignedVecBegin + C.getEndIdx() + 1))
        I = static_cast<unsigned>(-1);
      OutlinedSomething = true;

      // Statistics.
      NumOutlined++;
    }
  }

  LLVM_DEBUG(dbgs() << "OutlinedSomething = " << OutlinedSomething << "\n";);
  return OutlinedSomething;
}

void MachineOutliner::populateMapper(InstructionMapper &Mapper, Module &M,
                                     MachineModuleInfo &MMI) {
  // Build instruction mappings for each function in the module. Start by
  // iterating over each Function in M.
  LLVM_DEBUG(dbgs() << "*** Populating mapper ***\n");
  for (Function &F : M) {
    LLVM_DEBUG(dbgs() << "MAPPING FUNCTION: " << F.getName() << "\n");

    if (F.hasFnAttribute("nooutline")) {
      LLVM_DEBUG(dbgs() << "SKIP: Function has nooutline attribute\n");
      continue;
    }

    // There's something in F. Check if it has a MachineFunction associated with
    // it.
    MachineFunction *MF = MMI.getMachineFunction(F);

    // If it doesn't, then there's nothing to outline from. Move to the next
    // Function.
    if (!MF) {
      LLVM_DEBUG(dbgs() << "SKIP: Function does not have a MachineFunction\n");
      continue;
    }

    const TargetInstrInfo *TII = MF->getSubtarget().getInstrInfo();
    if (!RunOnAllFunctions && !TII->shouldOutlineFromFunctionByDefault(*MF)) {
      LLVM_DEBUG(dbgs() << "SKIP: Target does not want to outline from "
                           "function by default\n");
      continue;
    }

    // We have a MachineFunction. Ask the target if it's suitable for outlining.
    // If it isn't, then move on to the next Function in the module.
    if (!TII->isFunctionSafeToOutlineFrom(*MF, OutlineFromLinkOnceODRs)) {
      LLVM_DEBUG(dbgs() << "SKIP: " << MF->getName()
                        << ": unsafe to outline from\n");
      continue;
    }

    // We have a function suitable for outlining. Iterate over every
    // MachineBasicBlock in MF and try to map its instructions to a list of
    // unsigned integers.
    const unsigned MinMBBSize = 2;

    for (MachineBasicBlock &MBB : *MF) {
      LLVM_DEBUG(dbgs() << "  MAPPING MBB: '" << MBB.getName() << "'\n");
      // If there isn't anything in MBB, then there's no point in outlining from
      // it.
      // If there are fewer than 2 instructions in the MBB, then it can't ever
      // contain something worth outlining.
      // FIXME: This should be based off of the maximum size in B of an outlined
      // call versus the size in B of the MBB.
      if (MBB.size() < MinMBBSize) {
        LLVM_DEBUG(dbgs() << "    SKIP: MBB size less than minimum size of "
                          << MinMBBSize << "\n");
        continue;
      }

      // Check if MBB could be the target of an indirect branch. If it is, then
      // we don't want to outline from it.
      if (MBB.hasAddressTaken()) {
        LLVM_DEBUG(dbgs() << "    SKIP: MBB's address is taken\n");
        continue;
      }

      // MBB is suitable for outlining. Map it to a list of unsigneds.
      Mapper.convertToUnsignedVec(MBB, *TII);
    }
  }
  // Statistics.
  UnsignedVecSize = Mapper.UnsignedVec.size();
}

void MachineOutliner::initSizeRemarkInfo(
    const Module &M, const MachineModuleInfo &MMI,
    StringMap<unsigned> &FunctionToInstrCount) {
  // Collect instruction counts for every function. We'll use this to emit
  // per-function size remarks later.
  for (const Function &F : M) {
    MachineFunction *MF = MMI.getMachineFunction(F);

    // We only care about MI counts here. If there's no MachineFunction at this
    // point, then there won't be after the outliner runs, so let's move on.
    if (!MF)
      continue;
    FunctionToInstrCount[F.getName().str()] = MF->getInstructionCount();
  }
}

void MachineOutliner::emitInstrCountChangedRemark(
    const Module &M, const MachineModuleInfo &MMI,
    const StringMap<unsigned> &FunctionToInstrCount) {
  // Iterate over each function in the module and emit remarks.
  // Note that we won't miss anything by doing this, because the outliner never
  // deletes functions.
  for (const Function &F : M) {
    MachineFunction *MF = MMI.getMachineFunction(F);

    // The outliner never deletes functions. If we don't have a MF here, then we
    // didn't have one prior to outlining either.
    if (!MF)
      continue;

    std::string Fname = std::string(F.getName());
    unsigned FnCountAfter = MF->getInstructionCount();
    unsigned FnCountBefore = 0;

    // Check if the function was recorded before.
    auto It = FunctionToInstrCount.find(Fname);

    // Did we have a previously-recorded size? If yes, then set FnCountBefore
    // to that.
    if (It != FunctionToInstrCount.end())
      FnCountBefore = It->second;

    // Compute the delta and emit a remark if there was a change.
    int64_t FnDelta = static_cast<int64_t>(FnCountAfter) -
                      static_cast<int64_t>(FnCountBefore);
    if (FnDelta == 0)
      continue;

    MachineOptimizationRemarkEmitter MORE(*MF, nullptr);
    MORE.emit([&]() {
      MachineOptimizationRemarkAnalysis R("size-info", "FunctionMISizeChange",
                                          DiagnosticLocation(), &MF->front());
      R << DiagnosticInfoOptimizationBase::Argument("Pass", "Machine Outliner")
        << ": Function: "
        << DiagnosticInfoOptimizationBase::Argument("Function", F.getName())
        << ": MI instruction count changed from "
        << DiagnosticInfoOptimizationBase::Argument("MIInstrsBefore",
                                                    FnCountBefore)
        << " to "
        << DiagnosticInfoOptimizationBase::Argument("MIInstrsAfter",
                                                    FnCountAfter)
        << "; Delta: "
        << DiagnosticInfoOptimizationBase::Argument("Delta", FnDelta);
      return R;
    });
  }
}

bool MachineOutliner::runOnModule(Module &M) {
  // Check if there's anything in the module. If it's empty, then there's
  // nothing to outline.
  if (M.empty())
    return false;

  // Number to append to the current outlined function.
  unsigned OutlinedFunctionNum = 0;

  OutlineRepeatedNum = 0;
  if (!doOutline(M, OutlinedFunctionNum))
    return false;

  for (unsigned I = 0; I < OutlinerReruns; ++I) {
    OutlinedFunctionNum = 0;
    OutlineRepeatedNum++;
    if (!doOutline(M, OutlinedFunctionNum)) {
      LLVM_DEBUG({
        dbgs() << "Did not outline on iteration " << I + 2 << " out of "
               << OutlinerReruns + 1 << "\n";
      });
      break;
    }
  }

  return true;
}

bool MachineOutliner::doOutline(Module &M, unsigned &OutlinedFunctionNum) {
  MachineModuleInfo &MMI = getAnalysis<MachineModuleInfoWrapperPass>().getMMI();

  // If the user passed -enable-machine-outliner=always or
  // -enable-machine-outliner, the pass will run on all functions in the module.
  // Otherwise, if the target supports default outlining, it will run on all
  // functions deemed by the target to be worth outlining from by default. Tell
  // the user how the outliner is running.
  LLVM_DEBUG({
    dbgs() << "Machine Outliner: Running on ";
    if (RunOnAllFunctions)
      dbgs() << "all functions";
    else
      dbgs() << "target-default functions";
    dbgs() << "\n";
  });

  // If the user specifies that they want to outline from linkonceodrs, set
  // it here.
  OutlineFromLinkOnceODRs = EnableLinkOnceODROutlining;
  InstructionMapper Mapper;

  // Prepare instruction mappings for the suffix tree.
  populateMapper(Mapper, M, MMI);
  std::vector<OutlinedFunction> FunctionList;

  // Find all of the outlining candidates.
  findCandidates(Mapper, FunctionList);

  // If we've requested size remarks, then collect the MI counts of every
  // function before outlining, and the MI counts after outlining.
  // FIXME: This shouldn't be in the outliner at all; it should ultimately be
  // the pass manager's responsibility.
  // This could pretty easily be placed in outline instead, but because we
  // really ultimately *don't* want this here, it's done like this for now
  // instead.

  // Check if we want size remarks.
  bool ShouldEmitSizeRemarks = M.shouldEmitInstrCountChangedRemark();
  StringMap<unsigned> FunctionToInstrCount;
  if (ShouldEmitSizeRemarks)
    initSizeRemarkInfo(M, MMI, FunctionToInstrCount);

  // Outline each of the candidates and return true if something was outlined.
  bool OutlinedSomething =
      outline(M, FunctionList, Mapper, OutlinedFunctionNum);

  // If we outlined something, we definitely changed the MI count of the
  // module. If we've asked for size remarks, then output them.
  // FIXME: This should be in the pass manager.
  if (ShouldEmitSizeRemarks && OutlinedSomething)
    emitInstrCountChangedRemark(M, MMI, FunctionToInstrCount);

  LLVM_DEBUG({
    if (!OutlinedSomething)
      dbgs() << "Stopped outlining at iteration " << OutlineRepeatedNum
             << " because no changes were found.\n";
  });

  return OutlinedSomething;
}
