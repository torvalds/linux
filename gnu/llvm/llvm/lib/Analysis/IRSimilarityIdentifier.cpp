//===- IRSimilarityIdentifier.cpp - Find similarity in a module -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// \file
// Implementation file for the IRSimilarityIdentifier for identifying
// similarities in IR including the IRInstructionMapper.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/IRSimilarityIdentifier.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SetOperations.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/User.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/SuffixTree.h"

using namespace llvm;
using namespace IRSimilarity;

namespace llvm {
cl::opt<bool>
    DisableBranches("no-ir-sim-branch-matching", cl::init(false),
                    cl::ReallyHidden,
                    cl::desc("disable similarity matching, and outlining, "
                             "across branches for debugging purposes."));

cl::opt<bool>
    DisableIndirectCalls("no-ir-sim-indirect-calls", cl::init(false),
                         cl::ReallyHidden,
                         cl::desc("disable outlining indirect calls."));

cl::opt<bool>
    MatchCallsByName("ir-sim-calls-by-name", cl::init(false), cl::ReallyHidden,
                     cl::desc("only allow matching call instructions if the "
                              "name and type signature match."));

cl::opt<bool>
    DisableIntrinsics("no-ir-sim-intrinsics", cl::init(false), cl::ReallyHidden,
                      cl::desc("Don't match or outline intrinsics"));
} // namespace llvm

IRInstructionData::IRInstructionData(Instruction &I, bool Legality,
                                     IRInstructionDataList &IDList)
    : Inst(&I), Legal(Legality), IDL(&IDList) {
  initializeInstruction();
}

void IRInstructionData::initializeInstruction() {
  // We check for whether we have a comparison instruction.  If it is, we
  // find the "less than" version of the predicate for consistency for
  // comparison instructions throught the program.
  if (CmpInst *C = dyn_cast<CmpInst>(Inst)) {
    CmpInst::Predicate Predicate = predicateForConsistency(C);
    if (Predicate != C->getPredicate())
      RevisedPredicate = Predicate;
  }

  // Here we collect the operands and their types for determining whether
  // the structure of the operand use matches between two different candidates.
  for (Use &OI : Inst->operands()) {
    if (isa<CmpInst>(Inst) && RevisedPredicate) {
      // If we have a CmpInst where the predicate is reversed, it means the
      // operands must be reversed as well.
      OperVals.insert(OperVals.begin(), OI.get());
      continue;
    }

    OperVals.push_back(OI.get());
  }

  // We capture the incoming BasicBlocks as values as well as the incoming
  // Values in order to check for structural similarity.
  if (PHINode *PN = dyn_cast<PHINode>(Inst))
    for (BasicBlock *BB : PN->blocks())
      OperVals.push_back(BB);
}

IRInstructionData::IRInstructionData(IRInstructionDataList &IDList)
    : IDL(&IDList) {}

void IRInstructionData::setBranchSuccessors(
    DenseMap<BasicBlock *, unsigned> &BasicBlockToInteger) {
  assert(isa<BranchInst>(Inst) && "Instruction must be branch");

  BranchInst *BI = cast<BranchInst>(Inst);
  DenseMap<BasicBlock *, unsigned>::iterator BBNumIt;

  BBNumIt = BasicBlockToInteger.find(BI->getParent());
  assert(BBNumIt != BasicBlockToInteger.end() &&
         "Could not find location for BasicBlock!");

  int CurrentBlockNumber = static_cast<int>(BBNumIt->second);

  for (Value *V : getBlockOperVals()) {
    BasicBlock *Successor = cast<BasicBlock>(V);
    BBNumIt = BasicBlockToInteger.find(Successor);
    assert(BBNumIt != BasicBlockToInteger.end() &&
           "Could not find number for BasicBlock!");
    int OtherBlockNumber = static_cast<int>(BBNumIt->second);

    int Relative = OtherBlockNumber - CurrentBlockNumber;
    RelativeBlockLocations.push_back(Relative);
  }
}

ArrayRef<Value *> IRInstructionData::getBlockOperVals() {
  assert((isa<BranchInst>(Inst) ||
         isa<PHINode>(Inst)) && "Instruction must be branch or PHINode");
  
  if (BranchInst *BI = dyn_cast<BranchInst>(Inst))
    return ArrayRef<Value *>(
      std::next(OperVals.begin(), BI->isConditional() ? 1 : 0),
      OperVals.end()
    );

  if (PHINode *PN = dyn_cast<PHINode>(Inst))
    return ArrayRef<Value *>(
      std::next(OperVals.begin(), PN->getNumIncomingValues()),
      OperVals.end()
    );

  return ArrayRef<Value *>();
}

void IRInstructionData::setCalleeName(bool MatchByName) {
  CallInst *CI = dyn_cast<CallInst>(Inst);
  assert(CI && "Instruction must be call");

  CalleeName = "";
  if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(Inst)) {
    // To hash intrinsics, we use the opcode, and types like the other
    // instructions, but also, the Intrinsic ID, and the Name of the
    // intrinsic.
    Intrinsic::ID IntrinsicID = II->getIntrinsicID();
    FunctionType *FT = II->getFunctionType();
    // If there is an overloaded name, we have to use the complex version
    // of getName to get the entire string.
    if (Intrinsic::isOverloaded(IntrinsicID))
      CalleeName =
          Intrinsic::getName(IntrinsicID, FT->params(), II->getModule(), FT);
    // If there is not an overloaded name, we only need to use this version.
    else
      CalleeName = Intrinsic::getName(IntrinsicID).str();

    return;
  }

  if (!CI->isIndirectCall() && MatchByName)
    CalleeName = CI->getCalledFunction()->getName().str();
}

void IRInstructionData::setPHIPredecessors(
    DenseMap<BasicBlock *, unsigned> &BasicBlockToInteger) {
  assert(isa<PHINode>(Inst) && "Instruction must be phi node");

  PHINode *PN = cast<PHINode>(Inst);
  DenseMap<BasicBlock *, unsigned>::iterator BBNumIt;

  BBNumIt = BasicBlockToInteger.find(PN->getParent());
  assert(BBNumIt != BasicBlockToInteger.end() &&
         "Could not find location for BasicBlock!");

  int CurrentBlockNumber = static_cast<int>(BBNumIt->second);

  // Convert the incoming blocks of the PHINode to an integer value, based on
  // the relative distances between the current block and the incoming block.
  for (unsigned Idx = 0; Idx < PN->getNumIncomingValues(); Idx++) {
    BasicBlock *Incoming = PN->getIncomingBlock(Idx);
    BBNumIt = BasicBlockToInteger.find(Incoming);
    assert(BBNumIt != BasicBlockToInteger.end() &&
           "Could not find number for BasicBlock!");
    int OtherBlockNumber = static_cast<int>(BBNumIt->second);

    int Relative = OtherBlockNumber - CurrentBlockNumber;
    RelativeBlockLocations.push_back(Relative);
  }
}

CmpInst::Predicate IRInstructionData::predicateForConsistency(CmpInst *CI) {
  switch (CI->getPredicate()) {
  case CmpInst::FCMP_OGT:
  case CmpInst::FCMP_UGT:
  case CmpInst::FCMP_OGE:
  case CmpInst::FCMP_UGE:
  case CmpInst::ICMP_SGT:
  case CmpInst::ICMP_UGT:
  case CmpInst::ICMP_SGE:
  case CmpInst::ICMP_UGE:
    return CI->getSwappedPredicate();
  default:
    return CI->getPredicate();
  }
}

CmpInst::Predicate IRInstructionData::getPredicate() const {
  assert(isa<CmpInst>(Inst) &&
         "Can only get a predicate from a compare instruction");

  if (RevisedPredicate)
    return *RevisedPredicate;

  return cast<CmpInst>(Inst)->getPredicate();
}

StringRef IRInstructionData::getCalleeName() const {
  assert(isa<CallInst>(Inst) &&
         "Can only get a name from a call instruction");

  assert(CalleeName && "CalleeName has not been set");

  return *CalleeName;
}

bool IRSimilarity::isClose(const IRInstructionData &A,
                           const IRInstructionData &B) {

  if (!A.Legal || !B.Legal)
    return false;

  // Check if we are performing the same sort of operation on the same types
  // but not on the same values.
  if (!A.Inst->isSameOperationAs(B.Inst)) {
    // If there is a predicate, this means that either there is a swapped
    // predicate, or that the types are different, we want to make sure that
    // the predicates are equivalent via swapping.
    if (isa<CmpInst>(A.Inst) && isa<CmpInst>(B.Inst)) {

      if (A.getPredicate() != B.getPredicate())
        return false;

      // If the predicates are the same via swap, make sure that the types are
      // still the same.
      auto ZippedTypes = zip(A.OperVals, B.OperVals);

      return all_of(
          ZippedTypes, [](std::tuple<llvm::Value *, llvm::Value *> R) {
            return std::get<0>(R)->getType() == std::get<1>(R)->getType();
          });
    }

    return false;
  }

  // Since any GEP Instruction operands after the first operand cannot be
  // defined by a register, we must make sure that the operands after the first
  // are the same in the two instructions
  if (auto *GEP = dyn_cast<GetElementPtrInst>(A.Inst)) {
    auto *OtherGEP = cast<GetElementPtrInst>(B.Inst);

    // If the instructions do not have the same inbounds restrictions, we do
    // not consider them the same.
    if (GEP->isInBounds() != OtherGEP->isInBounds())
      return false;

    auto ZippedOperands = zip(GEP->indices(), OtherGEP->indices());

    // We increment here since we do not care about the first instruction,
    // we only care about the following operands since they must be the
    // exact same to be considered similar.
    return all_of(drop_begin(ZippedOperands),
                  [](std::tuple<llvm::Use &, llvm::Use &> R) {
                    return std::get<0>(R) == std::get<1>(R);
                  });
  }

  // If the instructions are functions calls, we make sure that the function
  // name is the same.  We already know that the types are since is
  // isSameOperationAs is true.
  if (isa<CallInst>(A.Inst) && isa<CallInst>(B.Inst)) {
    if (A.getCalleeName() != B.getCalleeName())
      return false;
  }

  if (isa<BranchInst>(A.Inst) && isa<BranchInst>(B.Inst) &&
      A.RelativeBlockLocations.size() != B.RelativeBlockLocations.size())
    return false;

  return true;
}

// TODO: This is the same as the MachineOutliner, and should be consolidated
// into the same interface.
void IRInstructionMapper::convertToUnsignedVec(
    BasicBlock &BB, std::vector<IRInstructionData *> &InstrList,
    std::vector<unsigned> &IntegerMapping) {
  BasicBlock::iterator It = BB.begin();

  std::vector<unsigned> IntegerMappingForBB;
  std::vector<IRInstructionData *> InstrListForBB;

  for (BasicBlock::iterator Et = BB.end(); It != Et; ++It) {
    switch (InstClassifier.visit(*It)) {
    case InstrType::Legal:
      mapToLegalUnsigned(It, IntegerMappingForBB, InstrListForBB);
      break;
    case InstrType::Illegal:
      mapToIllegalUnsigned(It, IntegerMappingForBB, InstrListForBB);
      break;
    case InstrType::Invisible:
      AddedIllegalLastTime = false;
      break;
    }
  }

  if (AddedIllegalLastTime)
    mapToIllegalUnsigned(It, IntegerMappingForBB, InstrListForBB, true);
  for (IRInstructionData *ID : InstrListForBB)
    this->IDL->push_back(*ID);
  llvm::append_range(InstrList, InstrListForBB);
  llvm::append_range(IntegerMapping, IntegerMappingForBB);
}

// TODO: This is the same as the MachineOutliner, and should be consolidated
// into the same interface.
unsigned IRInstructionMapper::mapToLegalUnsigned(
    BasicBlock::iterator &It, std::vector<unsigned> &IntegerMappingForBB,
    std::vector<IRInstructionData *> &InstrListForBB) {
  // We added something legal, so we should unset the AddedLegalLastTime
  // flag.
  AddedIllegalLastTime = false;

  // If we have at least two adjacent legal instructions (which may have
  // invisible instructions in between), remember that.
  if (CanCombineWithPrevInstr)
    HaveLegalRange = true;
  CanCombineWithPrevInstr = true;

  // Get the integer for this instruction or give it the current
  // LegalInstrNumber.
  IRInstructionData *ID = allocateIRInstructionData(*It, true, *IDL);
  InstrListForBB.push_back(ID);

  if (isa<BranchInst>(*It))
    ID->setBranchSuccessors(BasicBlockToInteger);

  if (isa<CallInst>(*It))
    ID->setCalleeName(EnableMatchCallsByName);

  if (isa<PHINode>(*It))
    ID->setPHIPredecessors(BasicBlockToInteger);

  // Add to the instruction list
  bool WasInserted;
  DenseMap<IRInstructionData *, unsigned, IRInstructionDataTraits>::iterator
      ResultIt;
  std::tie(ResultIt, WasInserted) =
      InstructionIntegerMap.insert(std::make_pair(ID, LegalInstrNumber));
  unsigned INumber = ResultIt->second;

  // There was an insertion.
  if (WasInserted)
    LegalInstrNumber++;

  IntegerMappingForBB.push_back(INumber);

  // Make sure we don't overflow or use any integers reserved by the DenseMap.
  assert(LegalInstrNumber < IllegalInstrNumber &&
         "Instruction mapping overflow!");

  assert(LegalInstrNumber != DenseMapInfo<unsigned>::getEmptyKey() &&
         "Tried to assign DenseMap tombstone or empty key to instruction.");
  assert(LegalInstrNumber != DenseMapInfo<unsigned>::getTombstoneKey() &&
         "Tried to assign DenseMap tombstone or empty key to instruction.");

  return INumber;
}

IRInstructionData *
IRInstructionMapper::allocateIRInstructionData(Instruction &I, bool Legality,
                                               IRInstructionDataList &IDL) {
  return new (InstDataAllocator->Allocate()) IRInstructionData(I, Legality, IDL);
}

IRInstructionData *
IRInstructionMapper::allocateIRInstructionData(IRInstructionDataList &IDL) {
  return new (InstDataAllocator->Allocate()) IRInstructionData(IDL);
}

IRInstructionDataList *
IRInstructionMapper::allocateIRInstructionDataList() {
  return new (IDLAllocator->Allocate()) IRInstructionDataList();
}

// TODO: This is the same as the MachineOutliner, and should be consolidated
// into the same interface.
unsigned IRInstructionMapper::mapToIllegalUnsigned(
    BasicBlock::iterator &It, std::vector<unsigned> &IntegerMappingForBB,
    std::vector<IRInstructionData *> &InstrListForBB, bool End) {
  // Can't combine an illegal instruction. Set the flag.
  CanCombineWithPrevInstr = false;

  // Only add one illegal number per range of legal numbers.
  if (AddedIllegalLastTime)
    return IllegalInstrNumber;

  IRInstructionData *ID = nullptr;
  if (!End)
    ID = allocateIRInstructionData(*It, false, *IDL);
  else
    ID = allocateIRInstructionData(*IDL);
  InstrListForBB.push_back(ID);

  // Remember that we added an illegal number last time.
  AddedIllegalLastTime = true;
  unsigned INumber = IllegalInstrNumber;
  IntegerMappingForBB.push_back(IllegalInstrNumber--);

  assert(LegalInstrNumber < IllegalInstrNumber &&
         "Instruction mapping overflow!");

  assert(IllegalInstrNumber != DenseMapInfo<unsigned>::getEmptyKey() &&
         "IllegalInstrNumber cannot be DenseMap tombstone or empty key!");

  assert(IllegalInstrNumber != DenseMapInfo<unsigned>::getTombstoneKey() &&
         "IllegalInstrNumber cannot be DenseMap tombstone or empty key!");

  return INumber;
}

IRSimilarityCandidate::IRSimilarityCandidate(unsigned StartIdx, unsigned Len,
                                             IRInstructionData *FirstInstIt,
                                             IRInstructionData *LastInstIt)
    : StartIdx(StartIdx), Len(Len) {

  assert(FirstInstIt != nullptr && "Instruction is nullptr!");
  assert(LastInstIt != nullptr && "Instruction is nullptr!");
  assert(StartIdx + Len > StartIdx &&
         "Overflow for IRSimilarityCandidate range?");
  assert(Len - 1 == static_cast<unsigned>(std::distance(
                        iterator(FirstInstIt), iterator(LastInstIt))) &&
         "Length of the first and last IRInstructionData do not match the "
         "given length");

  // We iterate over the given instructions, and map each unique value
  // to a unique number in the IRSimilarityCandidate ValueToNumber and
  // NumberToValue maps.  A constant get its own value globally, the individual
  // uses of the constants are not considered to be unique.
  //
  // IR:                    Mapping Added:
  // %add1 = add i32 %a, c1    %add1 -> 3, %a -> 1, c1 -> 2
  // %add2 = add i32 %a, %1    %add2 -> 4
  // %add3 = add i32 c2, c1    %add3 -> 6, c2 -> 5
  //
  // when replace with global values, starting from 1, would be
  //
  // 3 = add i32 1, 2
  // 4 = add i32 1, 3
  // 6 = add i32 5, 2
  unsigned LocalValNumber = 1;
  IRInstructionDataList::iterator ID = iterator(*FirstInstIt);
  for (unsigned Loc = StartIdx; Loc < StartIdx + Len; Loc++, ID++) {
    // Map the operand values to an unsigned integer if it does not already
    // have an unsigned integer assigned to it.
    for (Value *Arg : ID->OperVals)
      if (!ValueToNumber.contains(Arg)) {
        ValueToNumber.try_emplace(Arg, LocalValNumber);
        NumberToValue.try_emplace(LocalValNumber, Arg);
        LocalValNumber++;
      }

    // Mapping the instructions to an unsigned integer if it is not already
    // exist in the mapping.
    if (!ValueToNumber.contains(ID->Inst)) {
      ValueToNumber.try_emplace(ID->Inst, LocalValNumber);
      NumberToValue.try_emplace(LocalValNumber, ID->Inst);
      LocalValNumber++;
    }
  }

  // Setting the first and last instruction data pointers for the candidate.  If
  // we got through the entire for loop without hitting an assert, we know
  // that both of these instructions are not nullptrs.
  FirstInst = FirstInstIt;
  LastInst = LastInstIt;

  // Add the basic blocks contained in the set into the global value numbering.
  DenseSet<BasicBlock *> BBSet;
  getBasicBlocks(BBSet);
  for (BasicBlock *BB : BBSet) {
    if (ValueToNumber.contains(BB))
      continue;
    
    ValueToNumber.try_emplace(BB, LocalValNumber);
    NumberToValue.try_emplace(LocalValNumber, BB);
    LocalValNumber++;
  }
}

bool IRSimilarityCandidate::isSimilar(const IRSimilarityCandidate &A,
                                      const IRSimilarityCandidate &B) {
  if (A.getLength() != B.getLength())
    return false;

  auto InstrDataForBoth =
      zip(make_range(A.begin(), A.end()), make_range(B.begin(), B.end()));

  return all_of(InstrDataForBoth,
                [](std::tuple<IRInstructionData &, IRInstructionData &> R) {
                  IRInstructionData &A = std::get<0>(R);
                  IRInstructionData &B = std::get<1>(R);
                  if (!A.Legal || !B.Legal)
                    return false;
                  return isClose(A, B);
                });
}

/// Determine if one or more of the assigned global value numbers for the
/// operands in \p TargetValueNumbers is in the current mapping set for operand
/// numbers in \p SourceOperands.  The set of possible corresponding global
/// value numbers are replaced with the most recent version of compatible
/// values.
///
/// \param [in] SourceValueToNumberMapping - The mapping of a Value to global
/// value number for the source IRInstructionCandidate.
/// \param [in, out] CurrentSrcTgtNumberMapping - The current mapping of source
/// IRSimilarityCandidate global value numbers to a set of possible numbers in
/// the target.
/// \param [in] SourceOperands - The operands in the original
/// IRSimilarityCandidate in the current instruction.
/// \param [in] TargetValueNumbers - The global value numbers of the operands in
/// the corresponding Instruction in the other IRSimilarityCandidate.
/// \returns true if there exists a possible mapping between the source
/// Instruction operands and the target Instruction operands, and false if not.
static bool checkNumberingAndReplaceCommutative(
  const DenseMap<Value *, unsigned> &SourceValueToNumberMapping,
  DenseMap<unsigned, DenseSet<unsigned>> &CurrentSrcTgtNumberMapping,
  ArrayRef<Value *> &SourceOperands,
  DenseSet<unsigned> &TargetValueNumbers){

  DenseMap<unsigned, DenseSet<unsigned>>::iterator ValueMappingIt;

  unsigned ArgVal;
  bool WasInserted;

  // Iterate over the operands in the source IRSimilarityCandidate to determine
  // whether there exists an operand in the other IRSimilarityCandidate that
  // creates a valid mapping of Value to Value between the
  // IRSimilarityCaniddates.
  for (Value *V : SourceOperands) {
    ArgVal = SourceValueToNumberMapping.find(V)->second;

    // Instead of finding a current mapping, we attempt to insert a set.
    std::tie(ValueMappingIt, WasInserted) = CurrentSrcTgtNumberMapping.insert(
        std::make_pair(ArgVal, TargetValueNumbers));

    // We need to iterate over the items in other IRSimilarityCandidate's
    // Instruction to determine whether there is a valid mapping of
    // Value to Value.
    DenseSet<unsigned> NewSet;
    for (unsigned &Curr : ValueMappingIt->second)
      // If we can find the value in the mapping, we add it to the new set.
      if (TargetValueNumbers.contains(Curr))
        NewSet.insert(Curr);

    // If we could not find a Value, return 0.
    if (NewSet.empty())
      return false;
    
    // Otherwise replace the old mapping with the newly constructed one.
    if (NewSet.size() != ValueMappingIt->second.size())
      ValueMappingIt->second.swap(NewSet);

    // We have reached no conclusions about the mapping, and cannot remove
    // any items from the other operands, so we move to check the next operand.
    if (ValueMappingIt->second.size() != 1)
      continue;

    unsigned ValToRemove = *ValueMappingIt->second.begin();
    // When there is only one item left in the mapping for and operand, remove
    // the value from the other operands.  If it results in there being no
    // mapping, return false, it means the mapping is wrong
    for (Value *InnerV : SourceOperands) {
      if (V == InnerV)
        continue;

      unsigned InnerVal = SourceValueToNumberMapping.find(InnerV)->second;
      ValueMappingIt = CurrentSrcTgtNumberMapping.find(InnerVal);
      if (ValueMappingIt == CurrentSrcTgtNumberMapping.end())
        continue;

      ValueMappingIt->second.erase(ValToRemove);
      if (ValueMappingIt->second.empty())
        return false;
    }
  }

  return true;
}

/// Determine if operand number \p TargetArgVal is in the current mapping set
/// for operand number \p SourceArgVal.
///
/// \param [in, out] CurrentSrcTgtNumberMapping current mapping of global
/// value numbers from source IRSimilarityCandidate to target
/// IRSimilarityCandidate.
/// \param [in] SourceArgVal The global value number for an operand in the
/// in the original candidate.
/// \param [in] TargetArgVal The global value number for the corresponding
/// operand in the other candidate.
/// \returns True if there exists a mapping and false if not.
bool checkNumberingAndReplace(
    DenseMap<unsigned, DenseSet<unsigned>> &CurrentSrcTgtNumberMapping,
    unsigned SourceArgVal, unsigned TargetArgVal) {
  // We are given two unsigned integers representing the global values of
  // the operands in different IRSimilarityCandidates and a current mapping
  // between the two.
  //
  // Source Operand GVN: 1
  // Target Operand GVN: 2
  // CurrentMapping: {1: {1, 2}}
  //
  // Since we have mapping, and the target operand is contained in the set, we
  // update it to:
  // CurrentMapping: {1: {2}}
  // and can return true. But, if the mapping was
  // CurrentMapping: {1: {3}}
  // we would return false.

  bool WasInserted;
  DenseMap<unsigned, DenseSet<unsigned>>::iterator Val;

  std::tie(Val, WasInserted) = CurrentSrcTgtNumberMapping.insert(
      std::make_pair(SourceArgVal, DenseSet<unsigned>({TargetArgVal})));

  // If we created a new mapping, then we are done.
  if (WasInserted)
    return true;

  // If there is more than one option in the mapping set, and the target value
  // is included in the mapping set replace that set with one that only includes
  // the target value, as it is the only valid mapping via the non commutative
  // instruction.

  DenseSet<unsigned> &TargetSet = Val->second;
  if (TargetSet.size() > 1 && TargetSet.contains(TargetArgVal)) {
    TargetSet.clear();
    TargetSet.insert(TargetArgVal);
    return true;
  }

  // Return true if we can find the value in the set.
  return TargetSet.contains(TargetArgVal);
}

bool IRSimilarityCandidate::compareNonCommutativeOperandMapping(
    OperandMapping A, OperandMapping B) {
  // Iterators to keep track of where we are in the operands for each
  // Instruction.
  ArrayRef<Value *>::iterator VItA = A.OperVals.begin();
  ArrayRef<Value *>::iterator VItB = B.OperVals.begin();
  unsigned OperandLength = A.OperVals.size();

  // For each operand, get the value numbering and ensure it is consistent.
  for (unsigned Idx = 0; Idx < OperandLength; Idx++, VItA++, VItB++) {
    unsigned OperValA = A.IRSC.ValueToNumber.find(*VItA)->second;
    unsigned OperValB = B.IRSC.ValueToNumber.find(*VItB)->second;

    // Attempt to add a set with only the target value.  If there is no mapping
    // we can create it here.
    //
    // For an instruction like a subtraction:
    // IRSimilarityCandidateA:  IRSimilarityCandidateB:
    // %resultA = sub %a, %b    %resultB = sub %d, %e
    //
    // We map %a -> %d and %b -> %e.
    //
    // And check to see whether their mapping is consistent in
    // checkNumberingAndReplace.

    if (!checkNumberingAndReplace(A.ValueNumberMapping, OperValA, OperValB))
      return false;

    if (!checkNumberingAndReplace(B.ValueNumberMapping, OperValB, OperValA))
      return false;
  }
  return true;
}

bool IRSimilarityCandidate::compareCommutativeOperandMapping(
    OperandMapping A, OperandMapping B) {
  DenseSet<unsigned> ValueNumbersA;      
  DenseSet<unsigned> ValueNumbersB;

  ArrayRef<Value *>::iterator VItA = A.OperVals.begin();
  ArrayRef<Value *>::iterator VItB = B.OperVals.begin();
  unsigned OperandLength = A.OperVals.size();

  // Find the value number sets for the operands.
  for (unsigned Idx = 0; Idx < OperandLength;
       Idx++, VItA++, VItB++) {
    ValueNumbersA.insert(A.IRSC.ValueToNumber.find(*VItA)->second);
    ValueNumbersB.insert(B.IRSC.ValueToNumber.find(*VItB)->second);
  }

  // Iterate over the operands in the first IRSimilarityCandidate and make sure
  // there exists a possible mapping with the operands in the second
  // IRSimilarityCandidate.
  if (!checkNumberingAndReplaceCommutative(A.IRSC.ValueToNumber,
                                           A.ValueNumberMapping, A.OperVals,
                                           ValueNumbersB))
    return false;

  // Iterate over the operands in the second IRSimilarityCandidate and make sure
  // there exists a possible mapping with the operands in the first
  // IRSimilarityCandidate.
  if (!checkNumberingAndReplaceCommutative(B.IRSC.ValueToNumber,
                                           B.ValueNumberMapping, B.OperVals,
                                           ValueNumbersA))
    return false;

  return true;
}

bool IRSimilarityCandidate::compareAssignmentMapping(
    const unsigned InstValA, const unsigned &InstValB,
    DenseMap<unsigned, DenseSet<unsigned>> &ValueNumberMappingA,
    DenseMap<unsigned, DenseSet<unsigned>> &ValueNumberMappingB) {
  DenseMap<unsigned, DenseSet<unsigned>>::iterator ValueMappingIt;
  bool WasInserted;
  std::tie(ValueMappingIt, WasInserted) = ValueNumberMappingA.insert(
      std::make_pair(InstValA, DenseSet<unsigned>({InstValB})));
  if (!WasInserted && !ValueMappingIt->second.contains(InstValB))
    return false;
  else if (ValueMappingIt->second.size() != 1) {
    for (unsigned OtherVal : ValueMappingIt->second) {
      if (OtherVal == InstValB)
        continue;
      if (!ValueNumberMappingA.contains(OtherVal))
        continue;
      if (!ValueNumberMappingA[OtherVal].contains(InstValA))
        continue;
      ValueNumberMappingA[OtherVal].erase(InstValA);
    }
    ValueNumberMappingA.erase(ValueMappingIt);
    std::tie(ValueMappingIt, WasInserted) = ValueNumberMappingA.insert(
      std::make_pair(InstValA, DenseSet<unsigned>({InstValB})));
  }

  return true;
}

bool IRSimilarityCandidate::checkRelativeLocations(RelativeLocMapping A,
                                                   RelativeLocMapping B) {
  // Get the basic blocks the label refers to.
  BasicBlock *ABB = cast<BasicBlock>(A.OperVal);
  BasicBlock *BBB = cast<BasicBlock>(B.OperVal);

  // Get the basic blocks contained in each region.
  DenseSet<BasicBlock *> BasicBlockA;
  DenseSet<BasicBlock *> BasicBlockB;
  A.IRSC.getBasicBlocks(BasicBlockA);
  B.IRSC.getBasicBlocks(BasicBlockB);
  
  // Determine if the block is contained in the region.
  bool AContained = BasicBlockA.contains(ABB);
  bool BContained = BasicBlockB.contains(BBB);

  // Both blocks need to be contained in the region, or both need to be outside
  // the region.
  if (AContained != BContained)
    return false;
  
  // If both are contained, then we need to make sure that the relative
  // distance to the target blocks are the same.
  if (AContained)
    return A.RelativeLocation == B.RelativeLocation;
  return true;
}

bool IRSimilarityCandidate::compareStructure(const IRSimilarityCandidate &A,
                                             const IRSimilarityCandidate &B) {
  DenseMap<unsigned, DenseSet<unsigned>> MappingA;
  DenseMap<unsigned, DenseSet<unsigned>> MappingB;
  return IRSimilarityCandidate::compareStructure(A, B, MappingA, MappingB);
}

typedef detail::zippy<detail::zip_shortest, SmallVector<int, 4> &,
                      SmallVector<int, 4> &, ArrayRef<Value *> &,
                      ArrayRef<Value *> &>
    ZippedRelativeLocationsT;

bool IRSimilarityCandidate::compareStructure(
    const IRSimilarityCandidate &A, const IRSimilarityCandidate &B,
    DenseMap<unsigned, DenseSet<unsigned>> &ValueNumberMappingA,
    DenseMap<unsigned, DenseSet<unsigned>> &ValueNumberMappingB) {
  if (A.getLength() != B.getLength())
    return false;

  if (A.ValueToNumber.size() != B.ValueToNumber.size())
    return false;

  iterator ItA = A.begin();
  iterator ItB = B.begin();

  // These ValueNumber Mapping sets create a create a mapping between the values
  // in one candidate to values in the other candidate.  If we create a set with
  // one element, and that same element maps to the original element in the
  // candidate we have a good mapping.

  // Iterate over the instructions contained in each candidate
  unsigned SectionLength = A.getStartIdx() + A.getLength();
  for (unsigned Loc = A.getStartIdx(); Loc < SectionLength;
       ItA++, ItB++, Loc++) {
    // Make sure the instructions are similar to one another.
    if (!isClose(*ItA, *ItB))
      return false;

    Instruction *IA = ItA->Inst;
    Instruction *IB = ItB->Inst;

    if (!ItA->Legal || !ItB->Legal)
      return false;

    // Get the operand sets for the instructions.
    ArrayRef<Value *> OperValsA = ItA->OperVals;
    ArrayRef<Value *> OperValsB = ItB->OperVals;

    unsigned InstValA = A.ValueToNumber.find(IA)->second;
    unsigned InstValB = B.ValueToNumber.find(IB)->second;

    // Ensure that the mappings for the instructions exists.
    if (!compareAssignmentMapping(InstValA, InstValB, ValueNumberMappingA,
                                  ValueNumberMappingB))
      return false;
    
    if (!compareAssignmentMapping(InstValB, InstValA, ValueNumberMappingB,
                                  ValueNumberMappingA))
      return false;

    // We have different paths for commutative instructions and non-commutative
    // instructions since commutative instructions could allow multiple mappings
    // to certain values.
    if (IA->isCommutative() && !isa<FPMathOperator>(IA) &&
        !isa<IntrinsicInst>(IA)) {
      if (!compareCommutativeOperandMapping(
              {A, OperValsA, ValueNumberMappingA},
              {B, OperValsB, ValueNumberMappingB}))
        return false;
      continue;
    }

    // Handle the non-commutative cases.
    if (!compareNonCommutativeOperandMapping(
            {A, OperValsA, ValueNumberMappingA},
            {B, OperValsB, ValueNumberMappingB}))
      return false;

    // Here we check that between two corresponding instructions,
    // when referring to a basic block in the same region, the
    // relative locations are the same. And, that the instructions refer to
    // basic blocks outside the region in the same corresponding locations.

    // We are able to make the assumption about blocks outside of the region
    // since the target block labels are considered values and will follow the
    // same number matching that we defined for the other instructions in the
    // region.  So, at this point, in each location we target a specific block
    // outside the region, we are targeting a corresponding block in each
    // analagous location in the region we are comparing to.
    if (!(isa<BranchInst>(IA) && isa<BranchInst>(IB)) &&
        !(isa<PHINode>(IA) && isa<PHINode>(IB)))
      continue;

    SmallVector<int, 4> &RelBlockLocsA = ItA->RelativeBlockLocations;
    SmallVector<int, 4> &RelBlockLocsB = ItB->RelativeBlockLocations;
    ArrayRef<Value *> ABL = ItA->getBlockOperVals();
    ArrayRef<Value *> BBL = ItB->getBlockOperVals();

    // Check to make sure that the number of operands, and branching locations
    // between BranchInsts is the same.
    if (RelBlockLocsA.size() != RelBlockLocsB.size() &&
        ABL.size() != BBL.size())
      return false;

    assert(RelBlockLocsA.size() == ABL.size() &&
           "Block information vectors not the same size.");
    assert(RelBlockLocsB.size() == BBL.size() &&
           "Block information vectors not the same size.");

    ZippedRelativeLocationsT ZippedRelativeLocations =
        zip(RelBlockLocsA, RelBlockLocsB, ABL, BBL);
    if (any_of(ZippedRelativeLocations,
               [&A, &B](std::tuple<int, int, Value *, Value *> R) {
                 return !checkRelativeLocations(
                     {A, std::get<0>(R), std::get<2>(R)},
                     {B, std::get<1>(R), std::get<3>(R)});
               }))
      return false;
  }
  return true;
}

bool IRSimilarityCandidate::overlap(const IRSimilarityCandidate &A,
                                    const IRSimilarityCandidate &B) {
  auto DoesOverlap = [](const IRSimilarityCandidate &X,
                        const IRSimilarityCandidate &Y) {
    // Check:
    // XXXXXX        X starts before Y ends
    //      YYYYYYY  Y starts after X starts
    return X.StartIdx <= Y.getEndIdx() && Y.StartIdx >= X.StartIdx;
  };

  return DoesOverlap(A, B) || DoesOverlap(B, A);
}

void IRSimilarityIdentifier::populateMapper(
    Module &M, std::vector<IRInstructionData *> &InstrList,
    std::vector<unsigned> &IntegerMapping) {

  std::vector<IRInstructionData *> InstrListForModule;
  std::vector<unsigned> IntegerMappingForModule;
  // Iterate over the functions in the module to map each Instruction in each
  // BasicBlock to an unsigned integer.
  Mapper.initializeForBBs(M);

  for (Function &F : M) {

    if (F.empty())
      continue;

    for (BasicBlock &BB : F) {

      // BB has potential to have similarity since it has a size greater than 2
      // and can therefore match other regions greater than 2. Map it to a list
      // of unsigned integers.
      Mapper.convertToUnsignedVec(BB, InstrListForModule,
                                  IntegerMappingForModule);
    }

    BasicBlock::iterator It = F.begin()->end();
    Mapper.mapToIllegalUnsigned(It, IntegerMappingForModule, InstrListForModule,
                                true);
    if (InstrListForModule.size() > 0)
      Mapper.IDL->push_back(*InstrListForModule.back());
  }

  // Insert the InstrListForModule at the end of the overall InstrList so that
  // we can have a long InstrList for the entire set of Modules being analyzed.
  llvm::append_range(InstrList, InstrListForModule);
  // Do the same as above, but for IntegerMapping.
  llvm::append_range(IntegerMapping, IntegerMappingForModule);
}

void IRSimilarityIdentifier::populateMapper(
    ArrayRef<std::unique_ptr<Module>> &Modules,
    std::vector<IRInstructionData *> &InstrList,
    std::vector<unsigned> &IntegerMapping) {

  // Iterate over, and map the instructions in each module.
  for (const std::unique_ptr<Module> &M : Modules)
    populateMapper(*M, InstrList, IntegerMapping);
}

/// From a repeated subsequence, find all the different instances of the
/// subsequence from the \p InstrList, and create an IRSimilarityCandidate from
/// the IRInstructionData in subsequence.
///
/// \param [in] Mapper - The instruction mapper for basic correctness checks.
/// \param [in] InstrList - The vector that holds the instruction data.
/// \param [in] IntegerMapping - The vector that holds the mapped integers.
/// \param [out] CandsForRepSubstring - The vector to store the generated
/// IRSimilarityCandidates.
static void createCandidatesFromSuffixTree(
    const IRInstructionMapper& Mapper, std::vector<IRInstructionData *> &InstrList,
    std::vector<unsigned> &IntegerMapping, SuffixTree::RepeatedSubstring &RS,
    std::vector<IRSimilarityCandidate> &CandsForRepSubstring) {

  unsigned StringLen = RS.Length;
  if (StringLen < 2)
    return;

  // Create an IRSimilarityCandidate for instance of this subsequence \p RS.
  for (const unsigned &StartIdx : RS.StartIndices) {
    unsigned EndIdx = StartIdx + StringLen - 1;

    // Check that this subsequence does not contain an illegal instruction.
    bool ContainsIllegal = false;
    for (unsigned CurrIdx = StartIdx; CurrIdx <= EndIdx; CurrIdx++) {
      unsigned Key = IntegerMapping[CurrIdx];
      if (Key > Mapper.IllegalInstrNumber) {
        ContainsIllegal = true;
        break;
      }
    }

    // If we have an illegal instruction, we should not create an
    // IRSimilarityCandidate for this region.
    if (ContainsIllegal)
      continue;

    // We are getting iterators to the instructions in this region of code
    // by advancing the start and end indices from the start of the
    // InstrList.
    std::vector<IRInstructionData *>::iterator StartIt = InstrList.begin();
    std::advance(StartIt, StartIdx);
    std::vector<IRInstructionData *>::iterator EndIt = InstrList.begin();
    std::advance(EndIt, EndIdx);

    CandsForRepSubstring.emplace_back(StartIdx, StringLen, *StartIt, *EndIt);
  }
}

void IRSimilarityCandidate::createCanonicalRelationFrom(
    IRSimilarityCandidate &SourceCand,
    DenseMap<unsigned, DenseSet<unsigned>> &ToSourceMapping,
    DenseMap<unsigned, DenseSet<unsigned>> &FromSourceMapping) {
  assert(SourceCand.CanonNumToNumber.size() != 0 &&
         "Base canonical relationship is empty!");
  assert(SourceCand.NumberToCanonNum.size() != 0 &&
         "Base canonical relationship is empty!");

  assert(CanonNumToNumber.size() == 0 && "Canonical Relationship is non-empty");
  assert(NumberToCanonNum.size() == 0 && "Canonical Relationship is non-empty");

  DenseSet<unsigned> UsedGVNs;
  // Iterate over the mappings provided from this candidate to SourceCand.  We
  // are then able to map the GVN in this candidate to the same canonical number
  // given to the corresponding GVN in SourceCand.
  for (std::pair<unsigned, DenseSet<unsigned>> &GVNMapping : ToSourceMapping) {
    unsigned SourceGVN = GVNMapping.first;

    assert(GVNMapping.second.size() != 0 && "Possible GVNs is 0!");

    unsigned ResultGVN;
    // We need special handling if we have more than one potential value.  This
    // means that there are at least two GVNs that could correspond to this GVN.
    // This could lead to potential swapping later on, so we make a decision
    // here to ensure a one-to-one mapping.
    if (GVNMapping.second.size() > 1) {
      bool Found = false;
      for (unsigned Val : GVNMapping.second) {
        // We make sure the target value number hasn't already been reserved.
        if (UsedGVNs.contains(Val))
          continue;

        // We make sure that the opposite mapping is still consistent.
        DenseMap<unsigned, DenseSet<unsigned>>::iterator It =
            FromSourceMapping.find(Val);

        if (!It->second.contains(SourceGVN))
          continue;

        // We pick the first item that satisfies these conditions.
        Found = true;
        ResultGVN = Val;
        break;
      }

      assert(Found && "Could not find matching value for source GVN");
      (void)Found;

    } else
      ResultGVN = *GVNMapping.second.begin();

    // Whatever GVN is found, we mark it as used.
    UsedGVNs.insert(ResultGVN);

    unsigned CanonNum = *SourceCand.getCanonicalNum(ResultGVN);
    CanonNumToNumber.insert(std::make_pair(CanonNum, SourceGVN));
    NumberToCanonNum.insert(std::make_pair(SourceGVN, CanonNum));
  }

  DenseSet<BasicBlock *> BBSet;
  getBasicBlocks(BBSet);
  // Find canonical numbers for the BasicBlocks in the current candidate.
  // This is done by finding the corresponding value for the first instruction
  // in the block in the current candidate, finding the matching value in the
  // source candidate.  Then by finding the parent of this value, use the
  // canonical number of the block in the source candidate for the canonical
  // number in the current candidate.
  for (BasicBlock *BB : BBSet) {
    unsigned BBGVNForCurrCand = ValueToNumber.find(BB)->second;

    // We can skip the BasicBlock if the canonical numbering has already been
    // found in a separate instruction.
    if (NumberToCanonNum.contains(BBGVNForCurrCand))
      continue;

    // If the basic block is the starting block, then the shared instruction may
    // not be the first instruction in the block, it will be the first
    // instruction in the similarity region.
    Value *FirstOutlineInst = BB == getStartBB()
                                  ? frontInstruction()
                                  : &*BB->instructionsWithoutDebug().begin();

    unsigned FirstInstGVN = *getGVN(FirstOutlineInst);
    unsigned FirstInstCanonNum = *getCanonicalNum(FirstInstGVN);
    unsigned SourceGVN = *SourceCand.fromCanonicalNum(FirstInstCanonNum);
    Value *SourceV = *SourceCand.fromGVN(SourceGVN);
    BasicBlock *SourceBB = cast<Instruction>(SourceV)->getParent();
    unsigned SourceBBGVN = *SourceCand.getGVN(SourceBB);
    unsigned SourceCanonBBGVN = *SourceCand.getCanonicalNum(SourceBBGVN);
    CanonNumToNumber.insert(std::make_pair(SourceCanonBBGVN, BBGVNForCurrCand));
    NumberToCanonNum.insert(std::make_pair(BBGVNForCurrCand, SourceCanonBBGVN));
  }
}

void IRSimilarityCandidate::createCanonicalRelationFrom(
    IRSimilarityCandidate &SourceCand, IRSimilarityCandidate &SourceCandLarge,
    IRSimilarityCandidate &TargetCandLarge) {
  assert(!SourceCand.CanonNumToNumber.empty() &&
         "Canonical Relationship is non-empty");
  assert(!SourceCand.NumberToCanonNum.empty() &&
         "Canonical Relationship is non-empty");

  assert(!SourceCandLarge.CanonNumToNumber.empty() &&
         "Canonical Relationship is non-empty");
  assert(!SourceCandLarge.NumberToCanonNum.empty() &&
         "Canonical Relationship is non-empty");
  
  assert(!TargetCandLarge.CanonNumToNumber.empty() &&
         "Canonical Relationship is non-empty");
  assert(!TargetCandLarge.NumberToCanonNum.empty() &&
         "Canonical Relationship is non-empty");

  assert(CanonNumToNumber.empty() && "Canonical Relationship is non-empty");
  assert(NumberToCanonNum.empty() && "Canonical Relationship is non-empty");

  // We're going to use the larger candidates as a "bridge" to create the
  // canonical number for the target candidate since we have idetified two
  // candidates as subsequences of larger sequences, and therefore must be
  // structurally similar.
  for (std::pair<Value *, unsigned> &ValueNumPair : ValueToNumber) {
    Value *CurrVal = ValueNumPair.first;
    unsigned TargetCandGVN = ValueNumPair.second;

    // Find the numbering in the large candidate that surrounds the 
    // current candidate.
    std::optional<unsigned> OLargeTargetGVN = TargetCandLarge.getGVN(CurrVal);
    assert(OLargeTargetGVN.has_value() && "GVN not found for Value");

    // Get the canonical numbering in the large target candidate.
    std::optional<unsigned> OTargetCandCanon =
        TargetCandLarge.getCanonicalNum(OLargeTargetGVN.value());
    assert(OTargetCandCanon.has_value() &&
           "Canononical Number not found for GVN");
    
    // Get the GVN in the large source candidate from the canonical numbering.
    std::optional<unsigned> OLargeSourceGVN =
        SourceCandLarge.fromCanonicalNum(OTargetCandCanon.value());
    assert(OLargeSourceGVN.has_value() &&
           "GVN Number not found for Canonical Number");
    
    // Get the Value from the GVN in the large source candidate.
    std::optional<Value *> OLargeSourceV =
        SourceCandLarge.fromGVN(OLargeSourceGVN.value());
    assert(OLargeSourceV.has_value() && "Value not found for GVN");

    // Get the GVN number for the Value in the source candidate.
    std::optional<unsigned> OSourceGVN =
        SourceCand.getGVN(OLargeSourceV.value());
    assert(OSourceGVN.has_value() && "GVN Number not found for Value");

    // Get the canonical numbering from the GVN/
    std::optional<unsigned> OSourceCanon =
        SourceCand.getCanonicalNum(OSourceGVN.value());
    assert(OSourceCanon.has_value() && "Canon Number not found for GVN");

    // Insert the canonical numbering and GVN pair into their respective
    // mappings.
    CanonNumToNumber.insert(
        std::make_pair(OSourceCanon.value(), TargetCandGVN));
    NumberToCanonNum.insert(
        std::make_pair(TargetCandGVN, OSourceCanon.value()));
  }
}

void IRSimilarityCandidate::createCanonicalMappingFor(
    IRSimilarityCandidate &CurrCand) {
  assert(CurrCand.CanonNumToNumber.size() == 0 &&
         "Canonical Relationship is non-empty");
  assert(CurrCand.NumberToCanonNum.size() == 0 &&
         "Canonical Relationship is non-empty");

  unsigned CanonNum = 0;
  // Iterate over the value numbers found, the order does not matter in this
  // case.
  for (std::pair<unsigned, Value *> &NumToVal : CurrCand.NumberToValue) {
    CurrCand.NumberToCanonNum.insert(std::make_pair(NumToVal.first, CanonNum));
    CurrCand.CanonNumToNumber.insert(std::make_pair(CanonNum, NumToVal.first));
    CanonNum++;
  }
}

/// Look for larger IRSimilarityCandidates From the previously matched
/// IRSimilarityCandidates that fully contain \p CandA or \p CandB.  If there is
/// an overlap, return a pair of structurally similar, larger
/// IRSimilarityCandidates.
///
/// \param [in] CandA - The first candidate we are trying to determine the
/// structure of.
/// \param [in] CandB - The second candidate we are trying to determine the
/// structure of.
/// \param [in] IndexToIncludedCand - Mapping of index of the an instruction in
/// a circuit to the IRSimilarityCandidates that include this instruction.
/// \param [in] CandToOverallGroup - Mapping of IRSimilarityCandidate to a
/// number representing the structural group assigned to it.
static std::optional<
    std::pair<IRSimilarityCandidate *, IRSimilarityCandidate *>>
CheckLargerCands(
    IRSimilarityCandidate &CandA, IRSimilarityCandidate &CandB,
    DenseMap<unsigned, DenseSet<IRSimilarityCandidate *>> &IndexToIncludedCand,
    DenseMap<IRSimilarityCandidate *, unsigned> &CandToGroup) {
  DenseMap<unsigned, IRSimilarityCandidate *> IncludedGroupAndCandA;
  DenseMap<unsigned, IRSimilarityCandidate *> IncludedGroupAndCandB;
  DenseSet<unsigned> IncludedGroupsA;
  DenseSet<unsigned> IncludedGroupsB;

  // Find the overall similarity group numbers that fully contain the candidate,
  // and record the larger candidate for each group.
  auto IdxToCandidateIt = IndexToIncludedCand.find(CandA.getStartIdx());
  std::optional<std::pair<IRSimilarityCandidate *, IRSimilarityCandidate *>>
      Result;

  unsigned CandAStart = CandA.getStartIdx();
  unsigned CandAEnd = CandA.getEndIdx();
  unsigned CandBStart = CandB.getStartIdx();
  unsigned CandBEnd = CandB.getEndIdx();
  if (IdxToCandidateIt == IndexToIncludedCand.end())
    return Result;
  for (IRSimilarityCandidate *MatchedCand : IdxToCandidateIt->second) {
    if (MatchedCand->getStartIdx() > CandAStart ||
        (MatchedCand->getEndIdx() < CandAEnd))
      continue;
    unsigned GroupNum = CandToGroup.find(MatchedCand)->second;
    IncludedGroupAndCandA.insert(std::make_pair(GroupNum, MatchedCand));
    IncludedGroupsA.insert(GroupNum);
  }

  // Find the overall similarity group numbers that fully contain the next
  // candidate, and record the larger candidate for each group.
  IdxToCandidateIt = IndexToIncludedCand.find(CandBStart);
  if (IdxToCandidateIt == IndexToIncludedCand.end())
    return Result;
  for (IRSimilarityCandidate *MatchedCand : IdxToCandidateIt->second) {
    if (MatchedCand->getStartIdx() > CandBStart ||
        MatchedCand->getEndIdx() < CandBEnd)
      continue;
    unsigned GroupNum = CandToGroup.find(MatchedCand)->second;
    IncludedGroupAndCandB.insert(std::make_pair(GroupNum, MatchedCand));
    IncludedGroupsB.insert(GroupNum);
  }

  // Find the intersection between the two groups, these are the groups where
  // the larger candidates exist.
  set_intersect(IncludedGroupsA, IncludedGroupsB);

  // If there is no intersection between the sets, then we cannot determine
  // whether or not there is a match.
  if (IncludedGroupsA.empty())
    return Result;
  
  // Create a pair that contains the larger candidates.
  auto ItA = IncludedGroupAndCandA.find(*IncludedGroupsA.begin());
  auto ItB = IncludedGroupAndCandB.find(*IncludedGroupsA.begin());
  Result = std::make_pair(ItA->second, ItB->second);
  return Result;
}

/// From the list of IRSimilarityCandidates, perform a comparison between each
/// IRSimilarityCandidate to determine if there are overlapping
/// IRInstructionData, or if they do not have the same structure.
///
/// \param [in] CandsForRepSubstring - The vector containing the
/// IRSimilarityCandidates.
/// \param [out] StructuralGroups - the mapping of unsigned integers to vector
/// of IRSimilarityCandidates where each of the IRSimilarityCandidates in the
/// vector are structurally similar to one another.
/// \param [in] IndexToIncludedCand - Mapping of index of the an instruction in
/// a circuit to the IRSimilarityCandidates that include this instruction.
/// \param [in] CandToOverallGroup - Mapping of IRSimilarityCandidate to a
/// number representing the structural group assigned to it.
static void findCandidateStructures(
    std::vector<IRSimilarityCandidate> &CandsForRepSubstring,
    DenseMap<unsigned, SimilarityGroup> &StructuralGroups,
    DenseMap<unsigned,  DenseSet<IRSimilarityCandidate *>> &IndexToIncludedCand,
    DenseMap<IRSimilarityCandidate *, unsigned> &CandToOverallGroup
    ) {
  std::vector<IRSimilarityCandidate>::iterator CandIt, CandEndIt, InnerCandIt,
      InnerCandEndIt;

  // IRSimilarityCandidates each have a structure for operand use.  It is
  // possible that two instances of the same subsequences have different
  // structure. Each type of structure found is assigned a number.  This
  // DenseMap maps an IRSimilarityCandidate to which type of similarity
  // discovered it fits within.
  DenseMap<IRSimilarityCandidate *, unsigned> CandToGroup;

  // Find the compatibility from each candidate to the others to determine
  // which candidates overlap and which have the same structure by mapping
  // each structure to a different group.
  bool SameStructure;
  bool Inserted;
  unsigned CurrentGroupNum = 0;
  unsigned OuterGroupNum;
  DenseMap<IRSimilarityCandidate *, unsigned>::iterator CandToGroupIt;
  DenseMap<IRSimilarityCandidate *, unsigned>::iterator CandToGroupItInner;
  DenseMap<unsigned, SimilarityGroup>::iterator CurrentGroupPair;

  // Iterate over the candidates to determine its structural and overlapping
  // compatibility with other instructions
  DenseMap<unsigned, DenseSet<unsigned>> ValueNumberMappingA;
  DenseMap<unsigned, DenseSet<unsigned>> ValueNumberMappingB;
  for (CandIt = CandsForRepSubstring.begin(),
      CandEndIt = CandsForRepSubstring.end();
       CandIt != CandEndIt; CandIt++) {

    // Determine if it has an assigned structural group already.
    CandToGroupIt = CandToGroup.find(&*CandIt);
    if (CandToGroupIt == CandToGroup.end()) {
      // If not, we assign it one, and add it to our mapping.
      std::tie(CandToGroupIt, Inserted) =
          CandToGroup.insert(std::make_pair(&*CandIt, CurrentGroupNum++));
    }

    // Get the structural group number from the iterator.
    OuterGroupNum = CandToGroupIt->second;

    // Check if we already have a list of IRSimilarityCandidates for the current
    // structural group.  Create one if one does not exist.
    CurrentGroupPair = StructuralGroups.find(OuterGroupNum);
    if (CurrentGroupPair == StructuralGroups.end()) {
      IRSimilarityCandidate::createCanonicalMappingFor(*CandIt);
      std::tie(CurrentGroupPair, Inserted) = StructuralGroups.insert(
          std::make_pair(OuterGroupNum, SimilarityGroup({*CandIt})));
    }

    // Iterate over the IRSimilarityCandidates following the current
    // IRSimilarityCandidate in the list to determine whether the two
    // IRSimilarityCandidates are compatible.  This is so we do not repeat pairs
    // of IRSimilarityCandidates.
    for (InnerCandIt = std::next(CandIt),
        InnerCandEndIt = CandsForRepSubstring.end();
         InnerCandIt != InnerCandEndIt; InnerCandIt++) {

      // We check if the inner item has a group already, if it does, we skip it.
      CandToGroupItInner = CandToGroup.find(&*InnerCandIt);
      if (CandToGroupItInner != CandToGroup.end())
        continue;

      // Check if we have found structural similarity between two candidates
      // that fully contains the first and second candidates.
      std::optional<std::pair<IRSimilarityCandidate *, IRSimilarityCandidate *>>
          LargerPair = CheckLargerCands(
              *CandIt, *InnerCandIt, IndexToIncludedCand, CandToOverallGroup);

      // If a pair was found, it means that we can assume that these smaller
      // substrings are also structurally similar.  Use the larger candidates to
      // determine the canonical mapping between the two sections.
      if (LargerPair.has_value()) {
        SameStructure = true;
        InnerCandIt->createCanonicalRelationFrom(
            *CandIt, *LargerPair.value().first, *LargerPair.value().second);
        CandToGroup.insert(std::make_pair(&*InnerCandIt, OuterGroupNum));
        CurrentGroupPair->second.push_back(*InnerCandIt);
        continue;
      }

      // Otherwise we determine if they have the same structure and add it to
      // vector if they match.
      ValueNumberMappingA.clear();
      ValueNumberMappingB.clear();
      SameStructure = IRSimilarityCandidate::compareStructure(
          *CandIt, *InnerCandIt, ValueNumberMappingA, ValueNumberMappingB);
      if (!SameStructure)
        continue;

      InnerCandIt->createCanonicalRelationFrom(*CandIt, ValueNumberMappingA,
                                               ValueNumberMappingB);
      CandToGroup.insert(std::make_pair(&*InnerCandIt, OuterGroupNum));
      CurrentGroupPair->second.push_back(*InnerCandIt);
    }
  }
}

void IRSimilarityIdentifier::findCandidates(
    std::vector<IRInstructionData *> &InstrList,
    std::vector<unsigned> &IntegerMapping) {
  SuffixTree ST(IntegerMapping);

  std::vector<IRSimilarityCandidate> CandsForRepSubstring;
  std::vector<SimilarityGroup> NewCandidateGroups;

  DenseMap<unsigned, SimilarityGroup> StructuralGroups;
  DenseMap<unsigned, DenseSet<IRSimilarityCandidate *>> IndexToIncludedCand;
  DenseMap<IRSimilarityCandidate *, unsigned> CandToGroup; 

  // Iterate over the subsequences found by the Suffix Tree to create
  // IRSimilarityCandidates for each repeated subsequence and determine which
  // instances are structurally similar to one another.

  // Sort the suffix tree from longest substring to shortest.
  std::vector<SuffixTree::RepeatedSubstring> RSes;
  for (SuffixTree::RepeatedSubstring &RS : ST)
    RSes.push_back(RS);

  llvm::stable_sort(RSes, [](const SuffixTree::RepeatedSubstring &LHS,
                             const SuffixTree::RepeatedSubstring &RHS) {
    return LHS.Length > RHS.Length;
  });
  for (SuffixTree::RepeatedSubstring &RS : RSes) {
    createCandidatesFromSuffixTree(Mapper, InstrList, IntegerMapping, RS,
                                   CandsForRepSubstring);

    if (CandsForRepSubstring.size() < 2)
      continue;

    findCandidateStructures(CandsForRepSubstring, StructuralGroups,
                            IndexToIncludedCand, CandToGroup);
    for (std::pair<unsigned, SimilarityGroup> &Group : StructuralGroups) {
      // We only add the group if it contains more than one
      // IRSimilarityCandidate.  If there is only one, that means there is no
      // other repeated subsequence with the same structure.
      if (Group.second.size() > 1) {
        SimilarityCandidates->push_back(Group.second);
        // Iterate over each candidate in the group, and add an entry for each
        // instruction included with a mapping to a set of
        // IRSimilarityCandidates that include that instruction.
        for (IRSimilarityCandidate &IRCand : SimilarityCandidates->back()) {
          for (unsigned Idx = IRCand.getStartIdx(), Edx = IRCand.getEndIdx();
               Idx <= Edx; ++Idx) {
            DenseMap<unsigned, DenseSet<IRSimilarityCandidate *>>::iterator
                IdIt;
            IdIt = IndexToIncludedCand.find(Idx);
            bool Inserted = false;
            if (IdIt == IndexToIncludedCand.end())
              std::tie(IdIt, Inserted) = IndexToIncludedCand.insert(
                  std::make_pair(Idx, DenseSet<IRSimilarityCandidate *>()));
            IdIt->second.insert(&IRCand);
          }
          // Add mapping of candidate to the overall similarity group number.
          CandToGroup.insert(
              std::make_pair(&IRCand, SimilarityCandidates->size() - 1));
        }
      }
    }

    CandsForRepSubstring.clear();
    StructuralGroups.clear();
    NewCandidateGroups.clear();
  }
}

SimilarityGroupList &IRSimilarityIdentifier::findSimilarity(
    ArrayRef<std::unique_ptr<Module>> Modules) {
  resetSimilarityCandidates();

  std::vector<IRInstructionData *> InstrList;
  std::vector<unsigned> IntegerMapping;
  Mapper.InstClassifier.EnableBranches = this->EnableBranches;
  Mapper.InstClassifier.EnableIndirectCalls = EnableIndirectCalls;
  Mapper.EnableMatchCallsByName = EnableMatchingCallsByName;
  Mapper.InstClassifier.EnableIntrinsics = EnableIntrinsics;
  Mapper.InstClassifier.EnableMustTailCalls = EnableMustTailCalls;

  populateMapper(Modules, InstrList, IntegerMapping);
  findCandidates(InstrList, IntegerMapping);

  return *SimilarityCandidates;
}

SimilarityGroupList &IRSimilarityIdentifier::findSimilarity(Module &M) {
  resetSimilarityCandidates();
  Mapper.InstClassifier.EnableBranches = this->EnableBranches;
  Mapper.InstClassifier.EnableIndirectCalls = EnableIndirectCalls;
  Mapper.EnableMatchCallsByName = EnableMatchingCallsByName;
  Mapper.InstClassifier.EnableIntrinsics = EnableIntrinsics;
  Mapper.InstClassifier.EnableMustTailCalls = EnableMustTailCalls;

  std::vector<IRInstructionData *> InstrList;
  std::vector<unsigned> IntegerMapping;

  populateMapper(M, InstrList, IntegerMapping);
  findCandidates(InstrList, IntegerMapping);

  return *SimilarityCandidates;
}

INITIALIZE_PASS(IRSimilarityIdentifierWrapperPass, "ir-similarity-identifier",
                "ir-similarity-identifier", false, true)

IRSimilarityIdentifierWrapperPass::IRSimilarityIdentifierWrapperPass()
    : ModulePass(ID) {
  initializeIRSimilarityIdentifierWrapperPassPass(
      *PassRegistry::getPassRegistry());
}

bool IRSimilarityIdentifierWrapperPass::doInitialization(Module &M) {
  IRSI.reset(new IRSimilarityIdentifier(!DisableBranches, !DisableIndirectCalls,
                                        MatchCallsByName, !DisableIntrinsics,
                                        false));
  return false;
}

bool IRSimilarityIdentifierWrapperPass::doFinalization(Module &M) {
  IRSI.reset();
  return false;
}

bool IRSimilarityIdentifierWrapperPass::runOnModule(Module &M) {
  IRSI->findSimilarity(M);
  return false;
}

AnalysisKey IRSimilarityAnalysis::Key;
IRSimilarityIdentifier IRSimilarityAnalysis::run(Module &M,
                                                 ModuleAnalysisManager &) {
  auto IRSI = IRSimilarityIdentifier(!DisableBranches, !DisableIndirectCalls,
                                     MatchCallsByName, !DisableIntrinsics,
                                     false);
  IRSI.findSimilarity(M);
  return IRSI;
}

PreservedAnalyses
IRSimilarityAnalysisPrinterPass::run(Module &M, ModuleAnalysisManager &AM) {
  IRSimilarityIdentifier &IRSI = AM.getResult<IRSimilarityAnalysis>(M);
  std::optional<SimilarityGroupList> &SimilarityCandidatesOpt =
      IRSI.getSimilarity();

  for (std::vector<IRSimilarityCandidate> &CandVec : *SimilarityCandidatesOpt) {
    OS << CandVec.size() << " candidates of length "
       << CandVec.begin()->getLength() << ".  Found in: \n";
    for (IRSimilarityCandidate &Cand : CandVec) {
      OS << "  Function: " << Cand.front()->Inst->getFunction()->getName().str()
         << ", Basic Block: ";
      if (Cand.front()->Inst->getParent()->getName().str() == "")
        OS << "(unnamed)";
      else
        OS << Cand.front()->Inst->getParent()->getName().str();
      OS << "\n    Start Instruction: ";
      Cand.frontInstruction()->print(OS);
      OS << "\n      End Instruction: ";
      Cand.backInstruction()->print(OS);
      OS << "\n";
    }
  }

  return PreservedAnalyses::all();
}

char IRSimilarityIdentifierWrapperPass::ID = 0;
