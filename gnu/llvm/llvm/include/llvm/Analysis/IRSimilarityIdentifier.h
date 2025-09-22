//===- IRSimilarityIdentifier.h - Find similarity in a module --------------==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// \file
// Interface file for the IRSimilarityIdentifier for identifying similarities in
// IR including the IRInstructionMapper, which maps an Instruction to unsigned
// integers.
//
// Two sequences of instructions are called "similar" if they perform the same
// series of operations for all inputs.
//
// \code
// %1 = add i32 %a, 10
// %2 = add i32 %a, %1
// %3 = icmp slt icmp %1, %2
// \endcode
//
// and
//
// \code
// %1 = add i32 11, %a
// %2 = sub i32 %a, %1
// %3 = icmp sgt icmp %2, %1
// \endcode
//
// ultimately have the same result, even if the inputs, and structure are
// slightly different.
//
// For instructions, we do not worry about operands that do not have fixed
// semantic meaning to the program.  We consider the opcode that the instruction
// has, the types, parameters, and extra information such as the function name,
// or comparison predicate.  These are used to create a hash to map instructions
// to integers to be used in similarity matching in sequences of instructions
//
// Terminology:
// An IRSimilarityCandidate is a region of IRInstructionData (wrapped
// Instructions), usually used to denote a region of similarity has been found.
//
// A SimilarityGroup is a set of IRSimilarityCandidates that are structurally
// similar to one another.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_IRSIMILARITYIDENTIFIER_H
#define LLVM_ANALYSIS_IRSIMILARITYIDENTIFIER_H

#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/Allocator.h"
#include <optional>

namespace llvm {
class Module;

namespace IRSimilarity {

struct IRInstructionDataList;

/// This represents what is and is not supported when finding similarity in
/// Instructions.
///
/// Legal Instructions are considered when looking at similarity between
/// Instructions.
///
/// Illegal Instructions cannot be considered when looking for similarity
/// between Instructions. They act as boundaries between similarity regions.
///
/// Invisible Instructions are skipped over during analysis.
// TODO: Shared with MachineOutliner
enum InstrType { Legal, Illegal, Invisible };

/// This provides the utilities for hashing an Instruction to an unsigned
/// integer. Two IRInstructionDatas produce the same hash value when their
/// underlying Instructions perform the same operation (even if they don't have
/// the same input operands.)
/// As a more concrete example, consider the following:
///
/// \code
/// %add1 = add i32 %a, %b
/// %add2 = add i32 %c, %d
/// %add3 = add i64 %e, %f
/// \endcode
///
// Then the IRInstructionData wrappers for these Instructions may be hashed like
/// so:
///
/// \code
/// ; These two adds have the same types and operand types, so they hash to the
/// ; same number.
/// %add1 = add i32 %a, %b ; Hash: 1
/// %add2 = add i32 %c, %d ; Hash: 1
/// ; This add produces an i64. This differentiates it from %add1 and %add2. So,
/// ; it hashes to a different number.
/// %add3 = add i64 %e, %f; Hash: 2
/// \endcode
///
///
/// This hashing scheme will be used to represent the program as a very long
/// string. This string can then be placed in a data structure which can be used
/// for similarity queries.
///
/// TODO: Handle types of Instructions which can be equal even with different
/// operands. (E.g. comparisons with swapped predicates.)
/// TODO: Handle CallInsts, which are only checked for function type
/// by \ref isSameOperationAs.
/// TODO: Handle GetElementPtrInsts, as some of the operands have to be the
/// exact same, and some do not.
struct IRInstructionData
    : ilist_node<IRInstructionData, ilist_sentinel_tracking<true>> {

  /// The source Instruction that is being wrapped.
  Instruction *Inst = nullptr;
  /// The values of the operands in the Instruction.
  SmallVector<Value *, 4> OperVals;
  /// The legality of the wrapped instruction. This is informed by InstrType,
  /// and is used when checking when two instructions are considered similar.
  /// If either instruction is not legal, the instructions are automatically not
  /// considered similar.
  bool Legal = false;

  /// This is only relevant if we are wrapping a CmpInst where we needed to
  /// change the predicate of a compare instruction from a greater than form
  /// to a less than form.  It is std::nullopt otherwise.
  std::optional<CmpInst::Predicate> RevisedPredicate;

  /// This is only relevant if we are wrapping a CallInst. If we are requiring
  /// that the function calls have matching names as well as types, and the
  /// call is not an indirect call, this will hold the name of the function.  If
  /// it is an indirect string, it will be the empty string.  However, if this
  /// requirement is not in place it will be the empty string regardless of the
  /// function call type.  The value held here is used to create the hash of the
  /// instruction, and check to make sure two instructions are close to one
  /// another.
  std::optional<std::string> CalleeName;

  /// This structure holds the distances of how far "ahead of" or "behind" the
  /// target blocks of a branch, or the incoming blocks of a phi nodes are.
  /// If the value is negative, it means that the block was registered before
  /// the block of this instruction in terms of blocks in the function.
  /// Code Example:
  /// \code
  /// block_1:
  ///   br i1 %0, label %block_2, label %block_3
  /// block_2:
  ///   br i1 %1, label %block_1, label %block_2
  /// block_3:
  ///   br i1 %2, label %block_2, label %block_1
  /// ; Replacing the labels with relative values, this becomes:
  /// block_1:
  ///   br i1 %0, distance 1, distance 2
  /// block_2:
  ///   br i1 %1, distance -1, distance 0
  /// block_3:
  ///   br i1 %2, distance -1, distance -2
  /// \endcode
  /// Taking block_2 as our example, block_1 is "behind" block_2, and block_2 is
  /// "ahead" of block_2.
  SmallVector<int, 4> RelativeBlockLocations;

  /// Gather the information that is difficult to gather for an Instruction, or
  /// is changed. i.e. the operands of an Instruction and the Types of those
  /// operands. This extra information allows for similarity matching to make
  /// assertions that allow for more flexibility when checking for whether an
  /// Instruction performs the same operation.
  IRInstructionData(Instruction &I, bool Legality, IRInstructionDataList &IDL);
  IRInstructionData(IRInstructionDataList &IDL);

  /// Fills data stuctures for IRInstructionData when it is constructed from a
  // reference or a pointer.
  void initializeInstruction();

  /// Get the predicate that the compare instruction is using for hashing the
  /// instruction. the IRInstructionData must be wrapping a CmpInst.
  CmpInst::Predicate getPredicate() const;

  /// Get the callee name that the call instruction is using for hashing the
  /// instruction. The IRInstructionData must be wrapping a CallInst.
  StringRef getCalleeName() const;

  /// A function that swaps the predicates to their less than form if they are
  /// in a greater than form. Otherwise, the predicate is unchanged.
  ///
  /// \param CI - The comparison operation to find a consistent preidcate for.
  /// \return the consistent comparison predicate. 
  static CmpInst::Predicate predicateForConsistency(CmpInst *CI);

  /// For an IRInstructionData containing a branch, finds the
  /// relative distances from the source basic block to the target by taking
  /// the difference of the number assigned to the current basic block and the
  /// target basic block of the branch.
  ///
  /// \param BasicBlockToInteger - The mapping of basic blocks to their location
  /// in the module.
  void
  setBranchSuccessors(DenseMap<BasicBlock *, unsigned> &BasicBlockToInteger);

  /// For an IRInstructionData containing a CallInst, set the function name
  /// appropriately.  This will be an empty string if it is an indirect call,
  /// or we are not matching by name of the called function.  It will be the
  /// name of the function if \p MatchByName is true and it is not an indirect
  /// call.  We may decide not to match by name in order to expand the
  /// size of the regions we can match.  If a function name has the same type
  /// signature, but the different name, the region of code is still almost the
  /// same.  Since function names can be treated as constants, the name itself
  /// could be extrapolated away.  However, matching by name provides a
  /// specificity and more "identical" code than not matching by name.
  ///
  /// \param MatchByName - A flag to mark whether we are using the called
  /// function name as a differentiating parameter.
  void setCalleeName(bool MatchByName = true);

  /// For an IRInstructionData containing a PHINode, finds the
  /// relative distances from the incoming basic block to the current block by
  /// taking the difference of the number assigned to the current basic block
  /// and the incoming basic block of the branch.
  ///
  /// \param BasicBlockToInteger - The mapping of basic blocks to their location
  /// in the module.
  void
  setPHIPredecessors(DenseMap<BasicBlock *, unsigned> &BasicBlockToInteger);

  /// Get the BasicBlock based operands for PHINodes and BranchInsts.
  ///
  /// \returns A list of relevant BasicBlocks.
  ArrayRef<Value *> getBlockOperVals();

  /// Hashes \p Value based on its opcode, types, and operand types.
  /// Two IRInstructionData instances produce the same hash when they perform
  /// the same operation.
  ///
  /// As a simple example, consider the following instructions.
  ///
  /// \code
  /// %add1 = add i32 %x1, %y1
  /// %add2 = add i32 %x2, %y2
  ///
  /// %sub = sub i32 %x1, %y1
  ///
  /// %add_i64 = add i64 %x2, %y2
  /// \endcode
  ///
  /// Because the first two adds operate the same types, and are performing the
  /// same action, they will be hashed to the same value.
  ///
  /// However, the subtraction instruction is not the same as an addition, and
  /// will be hashed to a different value.
  ///
  /// Finally, the last add has a different type compared to the first two add
  /// instructions, so it will also be hashed to a different value that any of
  /// the previous instructions.
  ///
  /// \param [in] ID - The IRInstructionData instance to be hashed.
  /// \returns A hash_value of the IRInstructionData.
  friend hash_code hash_value(const IRInstructionData &ID) {
    SmallVector<Type *, 4> OperTypes;
    for (Value *V : ID.OperVals)
      OperTypes.push_back(V->getType());

    if (isa<CmpInst>(ID.Inst))
      return llvm::hash_combine(
          llvm::hash_value(ID.Inst->getOpcode()),
          llvm::hash_value(ID.Inst->getType()),
          llvm::hash_value(ID.getPredicate()),
          llvm::hash_combine_range(OperTypes.begin(), OperTypes.end()));

    if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(ID.Inst)) {
      // To hash intrinsics, we use the opcode, and types like the other
      // instructions, but also, the Intrinsic ID, and the Name of the
      // intrinsic.
      Intrinsic::ID IntrinsicID = II->getIntrinsicID();
      return llvm::hash_combine(
          llvm::hash_value(ID.Inst->getOpcode()),
          llvm::hash_value(ID.Inst->getType()), llvm::hash_value(IntrinsicID),
          llvm::hash_value(*ID.CalleeName),
          llvm::hash_combine_range(OperTypes.begin(), OperTypes.end()));
    }

    if (isa<CallInst>(ID.Inst)) {
      std::string FunctionName = *ID.CalleeName;
      return llvm::hash_combine(
          llvm::hash_value(ID.Inst->getOpcode()),
          llvm::hash_value(ID.Inst->getType()),
          llvm::hash_value(ID.Inst->getType()), llvm::hash_value(FunctionName),
          llvm::hash_combine_range(OperTypes.begin(), OperTypes.end()));
    }

    return llvm::hash_combine(
        llvm::hash_value(ID.Inst->getOpcode()),
        llvm::hash_value(ID.Inst->getType()),
        llvm::hash_combine_range(OperTypes.begin(), OperTypes.end()));
  }

  IRInstructionDataList *IDL = nullptr;
};

struct IRInstructionDataList
    : simple_ilist<IRInstructionData, ilist_sentinel_tracking<true>> {};

/// Compare one IRInstructionData class to another IRInstructionData class for
/// whether they are performing a the same operation, and can mapped to the
/// same value. For regular instructions if the hash value is the same, then
/// they will also be close.
///
/// \param A - The first IRInstructionData class to compare
/// \param B - The second IRInstructionData class to compare
/// \returns true if \p A and \p B are similar enough to be mapped to the same
/// value.
bool isClose(const IRInstructionData &A, const IRInstructionData &B);

struct IRInstructionDataTraits : DenseMapInfo<IRInstructionData *> {
  static inline IRInstructionData *getEmptyKey() { return nullptr; }
  static inline IRInstructionData *getTombstoneKey() {
    return reinterpret_cast<IRInstructionData *>(-1);
  }

  static unsigned getHashValue(const IRInstructionData *E) {
    using llvm::hash_value;
    assert(E && "IRInstructionData is a nullptr?");
    return hash_value(*E);
  }

  static bool isEqual(const IRInstructionData *LHS,
                      const IRInstructionData *RHS) {
    if (RHS == getEmptyKey() || RHS == getTombstoneKey() ||
        LHS == getEmptyKey() || LHS == getTombstoneKey())
      return LHS == RHS;

    assert(LHS && RHS && "nullptr should have been caught by getEmptyKey?");
    return isClose(*LHS, *RHS);
  }
};

/// Helper struct for converting the Instructions in a Module into a vector of
/// unsigned integers. This vector of unsigned integers can be thought of as a
/// "numeric string". This numeric string can then be queried by, for example,
/// data structures that find repeated substrings.
///
/// This hashing is done per BasicBlock in the module. To hash Instructions
/// based off of their operations, each Instruction is wrapped in an
/// IRInstructionData struct. The unsigned integer for an IRInstructionData
/// depends on:
/// - The hash provided by the IRInstructionData.
/// - Which member of InstrType the IRInstructionData is classified as.
// See InstrType for more details on the possible classifications, and how they
// manifest in the numeric string.
///
/// The numeric string for an individual BasicBlock is terminated by an unique
/// unsigned integer. This prevents data structures which rely on repetition
/// from matching across BasicBlocks. (For example, the SuffixTree.)
/// As a concrete example, if we have the following two BasicBlocks:
/// \code
/// bb0:
/// %add1 = add i32 %a, %b
/// %add2 = add i32 %c, %d
/// %add3 = add i64 %e, %f
/// bb1:
/// %sub = sub i32 %c, %d
/// \endcode
/// We may hash the Instructions like this (via IRInstructionData):
/// \code
/// bb0:
/// %add1 = add i32 %a, %b ; Hash: 1
/// %add2 = add i32 %c, %d; Hash: 1
/// %add3 = add i64 %e, %f; Hash: 2
/// bb1:
/// %sub = sub i32 %c, %d; Hash: 3
/// %add4 = add i32 %c, %d ; Hash: 1
/// \endcode
/// And produce a "numeric string representation" like so:
/// 1, 1, 2, unique_integer_1, 3, 1, unique_integer_2
///
/// TODO: This is very similar to the MachineOutliner, and should be
/// consolidated into the same interface.
struct IRInstructionMapper {
  /// The starting illegal instruction number to map to.
  ///
  /// Set to -3 for compatibility with DenseMapInfo<unsigned>.
  unsigned IllegalInstrNumber = static_cast<unsigned>(-3);

  /// The next available integer to assign to a legal Instruction to.
  unsigned LegalInstrNumber = 0;

  /// Correspondence from IRInstructionData to unsigned integers.
  DenseMap<IRInstructionData *, unsigned, IRInstructionDataTraits>
      InstructionIntegerMap;

  /// A mapping for a basic block in a module to its assigned number/location
  /// in the module.
  DenseMap<BasicBlock *, unsigned> BasicBlockToInteger;

  /// Set if we added an illegal number in the previous step.
  /// Since each illegal number is unique, we only need one of them between
  /// each range of legal numbers. This lets us make sure we don't add more
  /// than one illegal number per range.
  bool AddedIllegalLastTime = false;

  /// Marks whether we found a illegal instruction in the previous step.
  bool CanCombineWithPrevInstr = false;

  /// Marks whether we have found a set of instructions that is long enough
  /// to be considered for similarity.
  bool HaveLegalRange = false;

  /// Marks whether we should use exact function names, as well as types to
  /// find similarity between calls.
  bool EnableMatchCallsByName = false;

  /// This allocator pointer is in charge of holding on to the IRInstructionData
  /// so it is not deallocated until whatever external tool is using it is done
  /// with the information.
  SpecificBumpPtrAllocator<IRInstructionData> *InstDataAllocator = nullptr;

  /// This allocator pointer is in charge of creating the IRInstructionDataList
  /// so it is not deallocated until whatever external tool is using it is done
  /// with the information.
  SpecificBumpPtrAllocator<IRInstructionDataList> *IDLAllocator = nullptr;

  /// Get an allocated IRInstructionData struct using the InstDataAllocator.
  ///
  /// \param I - The Instruction to wrap with IRInstructionData.
  /// \param Legality - A boolean value that is true if the instruction is to
  /// be considered for similarity, and false if not.
  /// \param IDL - The InstructionDataList that the IRInstructionData is
  /// inserted into.
  /// \returns An allocated IRInstructionData struct.
  IRInstructionData *allocateIRInstructionData(Instruction &I, bool Legality,
                                               IRInstructionDataList &IDL);

  /// Get an empty allocated IRInstructionData struct using the
  /// InstDataAllocator.
  ///
  /// \param IDL - The InstructionDataList that the IRInstructionData is
  /// inserted into.
  /// \returns An allocated IRInstructionData struct.
  IRInstructionData *allocateIRInstructionData(IRInstructionDataList &IDL);

  /// Get an allocated IRInstructionDataList object using the IDLAllocator.
  ///
  /// \returns An allocated IRInstructionDataList object.
  IRInstructionDataList *allocateIRInstructionDataList();

  IRInstructionDataList *IDL = nullptr;

  /// Assigns values to all the basic blocks in function \p F starting from
  /// integer \p BBNumber.
  ///
  /// \param F - The function containing the basic blocks to assign numbers to.
  /// \param BBNumber - The number to start from.
  void initializeForBBs(Function &F, unsigned &BBNumber) {
    for (BasicBlock &BB : F)
      BasicBlockToInteger.insert(std::make_pair(&BB, BBNumber++));
  }

  /// Assigns values to all the basic blocks in Module \p M.
  /// \param M - The module containing the basic blocks to assign numbers to.
  void initializeForBBs(Module &M) {
    unsigned BBNumber = 0;
    for (Function &F : M)
      initializeForBBs(F, BBNumber);
  }

  /// Maps the Instructions in a BasicBlock \p BB to legal or illegal integers
  /// determined by \p InstrType. Two Instructions are mapped to the same value
  /// if they are close as defined by the InstructionData class above.
  ///
  /// \param [in] BB - The BasicBlock to be mapped to integers.
  /// \param [in,out] InstrList - Vector of IRInstructionData to append to.
  /// \param [in,out] IntegerMapping - Vector of unsigned integers to append to.
  void convertToUnsignedVec(BasicBlock &BB,
                            std::vector<IRInstructionData *> &InstrList,
                            std::vector<unsigned> &IntegerMapping);

  /// Maps an Instruction to a legal integer.
  ///
  /// \param [in] It - The Instruction to be mapped to an integer.
  /// \param [in,out] IntegerMappingForBB - Vector of unsigned integers to
  /// append to.
  /// \param [in,out] InstrListForBB - Vector of InstructionData to append to.
  /// \returns The integer \p It was mapped to.
  unsigned mapToLegalUnsigned(BasicBlock::iterator &It,
                              std::vector<unsigned> &IntegerMappingForBB,
                              std::vector<IRInstructionData *> &InstrListForBB);

  /// Maps an Instruction to an illegal integer.
  ///
  /// \param [in] It - The \p Instruction to be mapped to an integer.
  /// \param [in,out] IntegerMappingForBB - Vector of unsigned integers to
  /// append to.
  /// \param [in,out] InstrListForBB - Vector of IRInstructionData to append to.
  /// \param End - true if creating a dummy IRInstructionData at the end of a
  /// basic block.
  /// \returns The integer \p It was mapped to.
  unsigned mapToIllegalUnsigned(
      BasicBlock::iterator &It, std::vector<unsigned> &IntegerMappingForBB,
      std::vector<IRInstructionData *> &InstrListForBB, bool End = false);

  IRInstructionMapper(SpecificBumpPtrAllocator<IRInstructionData> *IDA,
                      SpecificBumpPtrAllocator<IRInstructionDataList> *IDLA)
      : InstDataAllocator(IDA), IDLAllocator(IDLA) {
    // Make sure that the implementation of DenseMapInfo<unsigned> hasn't
    // changed.
    assert(DenseMapInfo<unsigned>::getEmptyKey() == static_cast<unsigned>(-1) &&
           "DenseMapInfo<unsigned>'s empty key isn't -1!");
    assert(DenseMapInfo<unsigned>::getTombstoneKey() ==
               static_cast<unsigned>(-2) &&
           "DenseMapInfo<unsigned>'s tombstone key isn't -2!");

    IDL = new (IDLAllocator->Allocate())
        IRInstructionDataList();
  }

  /// Custom InstVisitor to classify different instructions for whether it can
  /// be analyzed for similarity.
  struct InstructionClassification
      : public InstVisitor<InstructionClassification, InstrType> {
    InstructionClassification() = default;

    // TODO: Determine a scheme to resolve when the label is similar enough.
    InstrType visitBranchInst(BranchInst &BI) {
      if (EnableBranches)
        return Legal;
      return Illegal;
    }
    InstrType visitPHINode(PHINode &PN) { 
      if (EnableBranches)
        return Legal;
      return Illegal;
    }
    // TODO: Handle allocas.
    InstrType visitAllocaInst(AllocaInst &AI) { return Illegal; }
    // We exclude variable argument instructions since variable arguments
    // requires extra checking of the argument list.
    InstrType visitVAArgInst(VAArgInst &VI) { return Illegal; }
    // We exclude all exception handling cases since they are so context
    // dependent.
    InstrType visitLandingPadInst(LandingPadInst &LPI) { return Illegal; }
    InstrType visitFuncletPadInst(FuncletPadInst &FPI) { return Illegal; }
    // DebugInfo should be included in the regions, but should not be
    // analyzed for similarity as it has no bearing on the outcome of the
    // program.
    InstrType visitDbgInfoIntrinsic(DbgInfoIntrinsic &DII) { return Invisible; }
    InstrType visitIntrinsicInst(IntrinsicInst &II) {
      // These are disabled due to complications in the CodeExtractor when
      // outlining these instructions.  For instance, It is unclear what we
      // should do when moving only the start or end lifetime instruction into
      // an outlined function. Also, assume-like intrinsics could be removed
      // from the region, removing arguments, causing discrepencies in the
      // number of inputs between different regions.
      if (II.isAssumeLikeIntrinsic())
        return Illegal;
      return EnableIntrinsics ? Legal : Illegal;
    }
    // We only allow call instructions where the function has a name and
    // is not an indirect call.
    InstrType visitCallInst(CallInst &CI) {
      Function *F = CI.getCalledFunction();
      bool IsIndirectCall = CI.isIndirectCall();
      if (IsIndirectCall && !EnableIndirectCalls)
        return Illegal;
      if (!F && !IsIndirectCall)
        return Illegal;
      // Functions marked with the swifttailcc and tailcc calling conventions
      // require special handling when outlining musttail functions.  The
      // calling convention must be passed down to the outlined function as
      // well. Further, there is special handling for musttail calls as well,
      // requiring a return call directly after.  For now, the outliner does not
      // support this, so we do not handle matching this case either.
      if ((CI.getCallingConv() == CallingConv::SwiftTail ||
           CI.getCallingConv() == CallingConv::Tail) &&
          !EnableMustTailCalls)
        return Illegal;
      if (CI.isMustTailCall() && !EnableMustTailCalls)
        return Illegal;
      return Legal;
    }
    // TODO: We do not current handle similarity that changes the control flow.
    InstrType visitInvokeInst(InvokeInst &II) { return Illegal; }
    // TODO: We do not current handle similarity that changes the control flow.
    InstrType visitCallBrInst(CallBrInst &CBI) { return Illegal; }
    // TODO: Handle interblock similarity.
    InstrType visitTerminator(Instruction &I) { return Illegal; }
    InstrType visitInstruction(Instruction &I) { return Legal; }

    // The flag variable that lets the classifier know whether we should
    // allow branches to be checked for similarity.
    bool EnableBranches = false;

    // The flag variable that lets the classifier know whether we should
    // allow indirect calls to be considered legal instructions.
    bool EnableIndirectCalls = false;

    // Flag that lets the classifier know whether we should allow intrinsics to
    // be checked for similarity.
    bool EnableIntrinsics = false;
  
    // Flag that lets the classifier know whether we should allow tail calls to
    // be checked for similarity.
    bool EnableMustTailCalls = false;
  };

  /// Maps an Instruction to a member of InstrType.
  InstructionClassification InstClassifier;
};

/// This is a class that wraps a range of IRInstructionData from one point to
/// another in the vector of IRInstructionData, which is a region of the
/// program.  It is also responsible for defining the structure within this
/// region of instructions.
///
/// The structure of a region is defined through a value numbering system
/// assigned to each unique value in a region at the creation of the
/// IRSimilarityCandidate.
///
/// For example, for each Instruction we add a mapping for each new
/// value seen in that Instruction.
/// IR:                    Mapping Added:
/// %add1 = add i32 %a, c1    %add1 -> 3, %a -> 1, c1 -> 2
/// %add2 = add i32 %a, %1    %add2 -> 4
/// %add3 = add i32 c2, c1    %add3 -> 6, c2 -> 5
///
/// We can compare IRSimilarityCandidates against one another.
/// The \ref isSimilar function compares each IRInstructionData against one
/// another and if we have the same sequences of IRInstructionData that would
/// create the same hash, we have similar IRSimilarityCandidates.
///
/// We can also compare the structure of IRSimilarityCandidates. If we can
/// create a mapping of registers in the region contained by one
/// IRSimilarityCandidate to the region contained by different
/// IRSimilarityCandidate, they can be considered structurally similar.
///
/// IRSimilarityCandidate1:   IRSimilarityCandidate2:
/// %add1 = add i32 %a, %b    %add1 = add i32 %d, %e
/// %add2 = add i32 %a, %c    %add2 = add i32 %d, %f
/// %add3 = add i32 c1, c2    %add3 = add i32 c3, c4
///
/// Can have the following mapping from candidate to candidate of:
/// %a -> %d, %b -> %e, %c -> %f, c1 -> c3, c2 -> c4
/// and can be considered similar.
///
/// IRSimilarityCandidate1:   IRSimilarityCandidate2:
/// %add1 = add i32 %a, %b    %add1 = add i32 %d, c4
/// %add2 = add i32 %a, %c    %add2 = add i32 %d, %f
/// %add3 = add i32 c1, c2    %add3 = add i32 c3, c4
///
/// We cannot create the same mapping since the use of c4 is not used in the
/// same way as %b or c2.
class IRSimilarityCandidate {
private:
  /// The start index of this IRSimilarityCandidate in the instruction list.
  unsigned StartIdx = 0;

  /// The number of instructions in this IRSimilarityCandidate.
  unsigned Len = 0;

  /// The first instruction in this IRSimilarityCandidate.
  IRInstructionData *FirstInst = nullptr;

  /// The last instruction in this IRSimilarityCandidate.
  IRInstructionData *LastInst = nullptr;

  /// Global Value Numbering structures
  /// @{
  /// Stores the mapping of the value to the number assigned to it in the
  /// IRSimilarityCandidate.
  DenseMap<Value *, unsigned> ValueToNumber;
  /// Stores the mapping of the number to the value assigned this number.
  DenseMap<unsigned, Value *> NumberToValue;
  /// Stores the mapping of a value's number to canonical numbering in the
  /// candidate's respective similarity group.
  DenseMap<unsigned, unsigned> NumberToCanonNum;
  /// Stores the mapping of canonical number in the candidate's respective
  /// similarity group to a value number.
  DenseMap<unsigned, unsigned> CanonNumToNumber;
  /// @}

public:
  /// \param StartIdx - The starting location of the region.
  /// \param Len - The length of the region.
  /// \param FirstInstIt - The starting IRInstructionData of the region.
  /// \param LastInstIt - The ending IRInstructionData of the region.
  IRSimilarityCandidate(unsigned StartIdx, unsigned Len,
                        IRInstructionData *FirstInstIt,
                        IRInstructionData *LastInstIt);

  /// \param A - The first IRInstructionCandidate to compare.
  /// \param B - The second IRInstructionCandidate to compare.
  /// \returns True when every IRInstructionData in \p A is similar to every
  /// IRInstructionData in \p B.
  static bool isSimilar(const IRSimilarityCandidate &A,
                        const IRSimilarityCandidate &B);

  /// \param [in] A - The first IRInstructionCandidate to compare.
  /// \param [in] B - The second IRInstructionCandidate to compare.
  /// \returns True when every IRInstructionData in \p A is structurally similar
  /// to \p B.
  static bool compareStructure(const IRSimilarityCandidate &A,
                               const IRSimilarityCandidate &B);

  /// \param [in] A - The first IRInstructionCandidate to compare.
  /// \param [in] B - The second IRInstructionCandidate to compare.
  /// \param [in,out] ValueNumberMappingA - A mapping of value numbers from
  /// candidate \p A to candidate \B.
  /// \param [in,out] ValueNumberMappingB - A mapping of value numbers from
  /// candidate \p B to candidate \A.
  /// \returns True when every IRInstructionData in \p A is structurally similar
  /// to \p B.
  static bool
  compareStructure(const IRSimilarityCandidate &A,
                   const IRSimilarityCandidate &B,
                   DenseMap<unsigned, DenseSet<unsigned>> &ValueNumberMappingA,
                   DenseMap<unsigned, DenseSet<unsigned>> &ValueNumberMappingB);

  struct OperandMapping {
    /// The IRSimilarityCandidate that holds the instruction the OperVals were
    /// pulled from.
    const IRSimilarityCandidate &IRSC;

    /// The operand values to be analyzed.
    ArrayRef<Value *> &OperVals;

    /// The current mapping of global value numbers from one IRSimilarityCandidate
    /// to another IRSimilarityCandidate.
    DenseMap<unsigned, DenseSet<unsigned>> &ValueNumberMapping;
  };

  /// A helper struct to hold the candidate, for a branch instruction, the
  /// relative location of a label, and the label itself.  This is mostly to
  /// group the values together before passing them as a bundle to a function.
  struct RelativeLocMapping {
    /// The IRSimilarityCandidate that holds the instruction the relative
    /// location was pulled from.
    const IRSimilarityCandidate &IRSC;

    /// The relative location to be analyzed.
    int RelativeLocation;

    /// The corresponding value.
    Value *OperVal;
  };

  /// Compare the operands in \p A and \p B and check that the current mapping
  /// of global value numbers from \p A to \p B and \p B to \A is consistent.
  ///
  /// \param A - The first IRInstructionCandidate, operand values, and current
  /// operand mappings to compare.
  /// \param B - The second IRInstructionCandidate, operand values, and current
  /// operand mappings to compare.
  /// \returns true if the IRSimilarityCandidates operands are compatible.
  static bool compareNonCommutativeOperandMapping(OperandMapping A,
                                                  OperandMapping B);

  /// Compare the operands in \p A and \p B and check that the current mapping
  /// of global value numbers from \p A to \p B and \p B to \A is consistent
  /// given that the operands are commutative.
  ///
  /// \param A - The first IRInstructionCandidate, operand values, and current
  /// operand mappings to compare.
  /// \param B - The second IRInstructionCandidate, operand values, and current
  /// operand mappings to compare.
  /// \returns true if the IRSimilarityCandidates operands are compatible.
  static bool compareCommutativeOperandMapping(OperandMapping A,
                                               OperandMapping B);

  /// Compare the GVN of the assignment value in corresponding instructions in
  /// IRSimilarityCandidates \p A and \p B and check that there exists a mapping
  /// between the values and replaces the mapping with a one-to-one value if
  /// needed.
  ///
  /// \param InstValA - The assignment GVN from the first IRSimilarityCandidate.
  /// \param InstValB - The assignment GVN from the second
  /// IRSimilarityCandidate.
  /// \param [in,out] ValueNumberMappingA - A mapping of value numbers from 
  /// candidate \p A to candidate \B.
  /// \param [in,out] ValueNumberMappingB - A mapping of value numbers from 
  /// candidate \p B to candidate \A.
  /// \returns true if the IRSimilarityCandidates assignments are compatible.
  static bool compareAssignmentMapping(
      const unsigned InstValA, const unsigned &InstValB,
      DenseMap<unsigned, DenseSet<unsigned>> &ValueNumberMappingA,
      DenseMap<unsigned, DenseSet<unsigned>> &ValueNumberMappingB);

  /// Compare the relative locations in \p A and \p B and check that the
  /// distances match if both locations are contained in the region, and that
  /// the branches both point outside the region if they do not.
  /// Example Region:
  /// \code
  /// entry:
  ///   br i1 %0, label %block_1, label %block_3
  /// block_0:
  ///   br i1 %0, label %block_1, label %block_2
  /// block_1:
  ///   br i1 %0, label %block_2, label %block_3
  /// block_2:
  ///   br i1 %1, label %block_1, label %block_4
  /// block_3:
  ///   br i1 %2, label %block_2, label %block_5
  /// \endcode
  /// If we compare the branches in block_0 and block_1 the relative values are
  /// 1 and 2 for both, so we consider this a match.
  ///
  /// If we compare the branches in entry and block_0 the relative values are
  /// 2 and 3, and 1 and 2 respectively.  Since these are not the same we do not
  /// consider them a match.
  ///
  /// If we compare the branches in block_1 and block_2 the relative values are
  /// 1 and 2, and -1 and None respectively.  As a result we do not consider
  /// these to be the same
  ///
  /// If we compare the branches in block_2 and block_3 the relative values are
  /// -1 and None for both.  We do consider these to be a match.
  ///
  /// \param A - The first IRInstructionCandidate, relative location value,
  /// and incoming block.
  /// \param B - The second IRInstructionCandidate, relative location value,
  /// and incoming block.
  /// \returns true if the relative locations match.
  static bool checkRelativeLocations(RelativeLocMapping A,
                                     RelativeLocMapping B);

  /// Create a mapping from the value numbering to a different separate set of
  /// numbers. This will serve as a guide for relating one candidate to another.
  /// The canonical number gives use the ability identify which global value
  /// number in one candidate relates to the global value number in the other.
  ///
  /// \param [in, out] CurrCand - The IRSimilarityCandidate to create a
  /// canonical numbering for.
  static void createCanonicalMappingFor(IRSimilarityCandidate &CurrCand);

  /// Create a mapping for the value numbering of the calling
  /// IRSimilarityCandidate, to a different separate set of numbers, based on
  /// the canonical ordering in \p SourceCand. These are defined based on the
  /// found mappings in \p ToSourceMapping and \p FromSourceMapping.  Both of
  /// these relationships should have the same information, just in opposite
  /// directions.
  ///
  /// \param [in, out] SourceCand - The IRSimilarityCandidate to create a
  /// canonical numbering from.
  /// \param ToSourceMapping - The mapping of value numbers from this candidate
  /// to \p SourceCand.
  /// \param FromSourceMapping - The mapping of value numbers from \p SoureCand
  /// to this candidate.
  void createCanonicalRelationFrom(
      IRSimilarityCandidate &SourceCand,
      DenseMap<unsigned, DenseSet<unsigned>> &ToSourceMapping,
      DenseMap<unsigned, DenseSet<unsigned>> &FromSourceMapping);
  
  /// Create a mapping for the value numbering of the calling
  /// IRSimilarityCandidate, to a different separate set of numbers, based on
  /// the canonical ordering in \p SourceCand. These are defined based on the
  /// found mappings in \p ToSourceMapping and \p FromSourceMapping.  Both of
  /// these relationships should have the same information, just in opposite
  /// directions.  Uses the \p OneToOne mapping from target candidate to \p
  /// SourceCand GVNs to determine the mapping first for values with multiple
  /// mappings.  This mapping is created by the ordering of operands in the
  /// instruction they are first seen in the candidates.
  ///
  /// \param [in, out] SourceCand - The IRSimilarityCandidate to create a
  /// canonical numbering from.
  /// \param [in,out] OneToOne - A mapping of value numbers from candidate
  /// \p A to candidate \B using the structure of the original instructions.
  /// \param ToSourceMapping - The mapping of value numbers from this candidate
  /// to \p SourceCand.
  /// \param FromSourceMapping - The mapping of value numbers from \p SoureCand
  /// to this candidate.
  void createCanonicalRelationFrom(
      IRSimilarityCandidate &SourceCand,
      DenseMap<unsigned, unsigned> &OneToOne,
      DenseMap<unsigned, DenseSet<unsigned>> &ToSourceMapping,
      DenseMap<unsigned, DenseSet<unsigned>> &FromSourceMapping);
  
  /// Create a mapping for the value numbering of the calling
  /// IRSimilarityCandidate, to a different separate set of numbers, based on
  /// the canonical ordering in \p SourceCand. These are defined based on the
  /// canonical mapping defined between \p SoureCandLarge and
  /// \p TargetCandLarge.  These IRSimilarityCandidates are already structurally
  /// similar, and fully encapsulate the IRSimilarityCandidates in question.
  /// These are used as a "bridge" from the \p SourceCand to the target.
  ///
  /// \param [in, out] SourceCand - The IRSimilarityCandidate to create a
  /// canonical numbering from.
  /// \param SoureCandLarge - The IRSimilarityCandidate fully containing
  /// \p SourceCand.
  /// \param TargetCandLarge -  The IRSimilarityCandidate fully containing
  /// this Candidate.
  void createCanonicalRelationFrom(
      IRSimilarityCandidate &SourceCand,
      IRSimilarityCandidate &SourceCandLarge,
      IRSimilarityCandidate &TargetCandLarge);

  /// \param [in,out] BBSet - The set to track the basic blocks.
  void getBasicBlocks(DenseSet<BasicBlock *> &BBSet) const {
    for (IRInstructionData &ID : *this) {
      BasicBlock *BB = ID.Inst->getParent();
      BBSet.insert(BB);
    }
  }

  /// \param [in,out] BBSet - The set to track the basic blocks.
  /// \param [in,out] BBList - A list in order of use to track the basic blocks.
  void getBasicBlocks(DenseSet<BasicBlock *> &BBSet,
                      SmallVector<BasicBlock *> &BBList) const {
    for (IRInstructionData &ID : *this) {
      BasicBlock *BB = ID.Inst->getParent();
      if (BBSet.insert(BB).second)
        BBList.push_back(BB);
    }
  }

  /// Compare the start and end indices of the two IRSimilarityCandidates for
  /// whether they overlap. If the start instruction of one
  /// IRSimilarityCandidate is less than the end instruction of the other, and
  /// the start instruction of one is greater than the start instruction of the
  /// other, they overlap.
  ///
  /// \returns true if the IRSimilarityCandidates do not have overlapping
  /// instructions.
  static bool overlap(const IRSimilarityCandidate &A,
                      const IRSimilarityCandidate &B);

  /// \returns the number of instructions in this Candidate.
  unsigned getLength() const { return Len; }

  /// \returns the start index of this IRSimilarityCandidate.
  unsigned getStartIdx() const { return StartIdx; }

  /// \returns the end index of this IRSimilarityCandidate.
  unsigned getEndIdx() const { return StartIdx + Len - 1; }

  /// \returns The first IRInstructionData.
  IRInstructionData *front() const { return FirstInst; }
  /// \returns The last IRInstructionData.
  IRInstructionData *back() const { return LastInst; }

  /// \returns The first Instruction.
  Instruction *frontInstruction() { return FirstInst->Inst; }
  /// \returns The last Instruction
  Instruction *backInstruction() { return LastInst->Inst; }

  /// \returns The BasicBlock the IRSimilarityCandidate starts in.
  BasicBlock *getStartBB() { return FirstInst->Inst->getParent(); }
  /// \returns The BasicBlock the IRSimilarityCandidate ends in.
  BasicBlock *getEndBB() { return LastInst->Inst->getParent(); }

  /// \returns The Function that the IRSimilarityCandidate is located in.
  Function *getFunction() { return getStartBB()->getParent(); }

  /// Finds the positive number associated with \p V if it has been mapped.
  /// \param [in] V - the Value to find.
  /// \returns The positive number corresponding to the value.
  /// \returns std::nullopt if not present.
  std::optional<unsigned> getGVN(Value *V) {
    assert(V != nullptr && "Value is a nullptr?");
    DenseMap<Value *, unsigned>::iterator VNIt = ValueToNumber.find(V);
    if (VNIt == ValueToNumber.end())
      return std::nullopt;
    return VNIt->second;
  }

  /// Finds the Value associate with \p Num if it exists.
  /// \param [in] Num - the number to find.
  /// \returns The Value associated with the number.
  /// \returns std::nullopt if not present.
  std::optional<Value *> fromGVN(unsigned Num) {
    DenseMap<unsigned, Value *>::iterator VNIt = NumberToValue.find(Num);
    if (VNIt == NumberToValue.end())
      return std::nullopt;
    assert(VNIt->second != nullptr && "Found value is a nullptr!");
    return VNIt->second;
  }

  /// Find the canonical number from the global value number \p N stored in the
  /// candidate.
  ///
  /// \param N - The global value number to find the canonical number for.
  /// \returns An optional containing the value, and std::nullopt if it could
  /// not be found.
  std::optional<unsigned> getCanonicalNum(unsigned N) {
    DenseMap<unsigned, unsigned>::iterator NCIt = NumberToCanonNum.find(N);
    if (NCIt == NumberToCanonNum.end())
      return std::nullopt;
    return NCIt->second;
  }

  /// Find the global value number from the canonical number \p N stored in the
  /// candidate.
  ///
  /// \param N - The canonical number to find the global vlaue number for.
  /// \returns An optional containing the value, and std::nullopt if it could
  /// not be found.
  std::optional<unsigned> fromCanonicalNum(unsigned N) {
    DenseMap<unsigned, unsigned>::iterator CNIt = CanonNumToNumber.find(N);
    if (CNIt == CanonNumToNumber.end())
      return std::nullopt;
    return CNIt->second;
  }

  /// \param RHS -The IRSimilarityCandidate to compare against
  /// \returns true if the IRSimilarityCandidate is occurs after the
  /// IRSimilarityCandidate in the program.
  bool operator<(const IRSimilarityCandidate &RHS) const {
    return getStartIdx() > RHS.getStartIdx();
  }

  using iterator = IRInstructionDataList::iterator;
  iterator begin() const { return iterator(front()); }
  iterator end() const { return std::next(iterator(back())); }
};

typedef DenseMap<IRSimilarityCandidate *,
                 DenseMap<unsigned, DenseSet<unsigned>>>
    CandidateGVNMapping;
typedef std::vector<IRSimilarityCandidate> SimilarityGroup;
typedef std::vector<SimilarityGroup> SimilarityGroupList;

/// This class puts all the pieces of the IRInstructionData,
/// IRInstructionMapper, IRSimilarityCandidate together.
///
/// It first feeds the Module or vector of Modules into the IRInstructionMapper,
/// and puts all the mapped instructions into a single long list of
/// IRInstructionData.
///
/// The list of unsigned integers is given to the Suffix Tree or similar data
/// structure to find repeated subsequences.  We construct an
/// IRSimilarityCandidate for each instance of the subsequence.  We compare them
/// against one another since  These repeated subsequences can have different
/// structure.  For each different kind of structure found, we create a
/// similarity group.
///
/// If we had four IRSimilarityCandidates A, B, C, and D where A, B and D are
/// structurally similar to one another, while C is different we would have two
/// SimilarityGroups:
///
/// SimilarityGroup 1:  SimilarityGroup 2
/// A, B, D             C
///
/// A list of the different similarity groups is then returned after
/// analyzing the module.
class IRSimilarityIdentifier {
public:
  IRSimilarityIdentifier(bool MatchBranches = true,
                         bool MatchIndirectCalls = true,
                         bool MatchCallsWithName = false,
                         bool MatchIntrinsics = true,
                         bool MatchMustTailCalls = true)
      : Mapper(&InstDataAllocator, &InstDataListAllocator),
        EnableBranches(MatchBranches), EnableIndirectCalls(MatchIndirectCalls),
        EnableMatchingCallsByName(MatchCallsWithName),
        EnableIntrinsics(MatchIntrinsics),
        EnableMustTailCalls(MatchMustTailCalls) {}

private:
  /// Map the instructions in the module to unsigned integers, using mapping
  /// already present in the Mapper if possible.
  ///
  /// \param [in] M Module - To map to integers.
  /// \param [in,out] InstrList - The vector to append IRInstructionData to.
  /// \param [in,out] IntegerMapping - The vector to append integers to.
  void populateMapper(Module &M, std::vector<IRInstructionData *> &InstrList,
                      std::vector<unsigned> &IntegerMapping);

  /// Map the instructions in the modules vector to unsigned integers, using
  /// mapping already present in the mapper if possible.
  ///
  /// \param [in] Modules - The list of modules to use to populate the mapper
  /// \param [in,out] InstrList - The vector to append IRInstructionData to.
  /// \param [in,out] IntegerMapping - The vector to append integers to.
  void populateMapper(ArrayRef<std::unique_ptr<Module>> &Modules,
                      std::vector<IRInstructionData *> &InstrList,
                      std::vector<unsigned> &IntegerMapping);

  /// Find the similarity candidates in \p InstrList and corresponding
  /// \p UnsignedVec
  ///
  /// \param [in,out] InstrList - The vector to append IRInstructionData to.
  /// \param [in,out] IntegerMapping - The vector to append integers to.
  /// candidates found in the program.
  void findCandidates(std::vector<IRInstructionData *> &InstrList,
                      std::vector<unsigned> &IntegerMapping);

public:
  // Find the IRSimilarityCandidates in the \p Modules and group by structural
  // similarity in a SimilarityGroup, each group is returned in a
  // SimilarityGroupList.
  //
  // \param [in] Modules - the modules to analyze.
  // \returns The groups of similarity ranges found in the modules.
  SimilarityGroupList &
  findSimilarity(ArrayRef<std::unique_ptr<Module>> Modules);

  // Find the IRSimilarityCandidates in the given Module grouped by structural
  // similarity in a SimilarityGroup, contained inside a SimilarityGroupList.
  //
  // \param [in] M - the module to analyze.
  // \returns The groups of similarity ranges found in the module.
  SimilarityGroupList &findSimilarity(Module &M);

  // Clears \ref SimilarityCandidates if it is already filled by a previous run.
  void resetSimilarityCandidates() {
    // If we've already analyzed a Module or set of Modules, so we must clear
    // the SimilarityCandidates to make sure we do not have only old values
    // hanging around.
    if (SimilarityCandidates)
      SimilarityCandidates->clear();
    else
      SimilarityCandidates = SimilarityGroupList();
  }

  // \returns The groups of similarity ranges found in the most recently passed
  // set of modules.
  std::optional<SimilarityGroupList> &getSimilarity() {
    return SimilarityCandidates;
  }

private:
  /// The allocator for IRInstructionData.
  SpecificBumpPtrAllocator<IRInstructionData> InstDataAllocator;

  /// The allocator for IRInstructionDataLists.
  SpecificBumpPtrAllocator<IRInstructionDataList> InstDataListAllocator;

  /// Map Instructions to unsigned integers and wraps the Instruction in an
  /// instance of IRInstructionData.
  IRInstructionMapper Mapper;

  /// The flag variable that marks whether we should check branches for
  /// similarity, or only look within basic blocks.
  bool EnableBranches = true;

  /// The flag variable that marks whether we allow indirect calls to be checked
  /// for similarity, or exclude them as a legal instruction.
  bool EnableIndirectCalls = true;

  /// The flag variable that marks whether we allow calls to be marked as
  /// similar if they do not have the same name, only the same calling
  /// convention, attributes and type signature.
  bool EnableMatchingCallsByName = true;

  /// The flag variable that marks whether we should check intrinsics for
  /// similarity.
  bool EnableIntrinsics = true;

  // The flag variable that marks whether we should allow tailcalls
  // to be checked for similarity.
  bool EnableMustTailCalls = false;

  /// The SimilarityGroups found with the most recent run of \ref
  /// findSimilarity. std::nullopt if there is no recent run.
  std::optional<SimilarityGroupList> SimilarityCandidates;
};

} // end namespace IRSimilarity

/// An analysis pass based on legacy pass manager that runs and returns
/// IRSimilarityIdentifier run on the Module.
class IRSimilarityIdentifierWrapperPass : public ModulePass {
  std::unique_ptr<IRSimilarity::IRSimilarityIdentifier> IRSI;

public:
  static char ID;
  IRSimilarityIdentifierWrapperPass();

  IRSimilarity::IRSimilarityIdentifier &getIRSI() { return *IRSI; }
  const IRSimilarity::IRSimilarityIdentifier &getIRSI() const { return *IRSI; }

  bool doInitialization(Module &M) override;
  bool doFinalization(Module &M) override;
  bool runOnModule(Module &M) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }
};

/// An analysis pass that runs and returns the IRSimilarityIdentifier run on the
/// Module.
class IRSimilarityAnalysis : public AnalysisInfoMixin<IRSimilarityAnalysis> {
public:
  typedef IRSimilarity::IRSimilarityIdentifier Result;

  Result run(Module &M, ModuleAnalysisManager &);

private:
  friend AnalysisInfoMixin<IRSimilarityAnalysis>;
  static AnalysisKey Key;
};

/// Printer pass that uses \c IRSimilarityAnalysis.
class IRSimilarityAnalysisPrinterPass
    : public PassInfoMixin<IRSimilarityAnalysisPrinterPass> {
  raw_ostream &OS;

public:
  explicit IRSimilarityAnalysisPrinterPass(raw_ostream &OS) : OS(OS) {}
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
  static bool isRequired() { return true; }
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_IRSIMILARITYIDENTIFIER_H
