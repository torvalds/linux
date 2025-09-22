//===- DAGISelMatcherOpt.cpp - Optimize a DAG Matcher ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the DAG Matcher optimizer.
//
//===----------------------------------------------------------------------===//

#include "Basic/SDNodeProperties.h"
#include "Common/CodeGenDAGPatterns.h"
#include "Common/DAGISelMatcher.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "isel-opt"

/// ContractNodes - Turn multiple matcher node patterns like 'MoveChild+Record'
/// into single compound nodes like RecordChild.
static void ContractNodes(std::unique_ptr<Matcher> &MatcherPtr,
                          const CodeGenDAGPatterns &CGP) {
  // If we reached the end of the chain, we're done.
  Matcher *N = MatcherPtr.get();
  if (!N)
    return;

  // If we have a scope node, walk down all of the children.
  if (ScopeMatcher *Scope = dyn_cast<ScopeMatcher>(N)) {
    for (unsigned i = 0, e = Scope->getNumChildren(); i != e; ++i) {
      std::unique_ptr<Matcher> Child(Scope->takeChild(i));
      ContractNodes(Child, CGP);
      Scope->resetChild(i, Child.release());
    }
    return;
  }

  // If we found a movechild node with a node that comes in a 'foochild' form,
  // transform it.
  if (MoveChildMatcher *MC = dyn_cast<MoveChildMatcher>(N)) {
    Matcher *New = nullptr;
    if (RecordMatcher *RM = dyn_cast<RecordMatcher>(MC->getNext()))
      if (MC->getChildNo() < 8) // Only have RecordChild0...7
        New = new RecordChildMatcher(MC->getChildNo(), RM->getWhatFor(),
                                     RM->getResultNo());

    if (CheckTypeMatcher *CT = dyn_cast<CheckTypeMatcher>(MC->getNext()))
      if (MC->getChildNo() < 8 && // Only have CheckChildType0...7
          CT->getResNo() == 0)    // CheckChildType checks res #0
        New = new CheckChildTypeMatcher(MC->getChildNo(), CT->getType());

    if (CheckSameMatcher *CS = dyn_cast<CheckSameMatcher>(MC->getNext()))
      if (MC->getChildNo() < 4) // Only have CheckChildSame0...3
        New = new CheckChildSameMatcher(MC->getChildNo(), CS->getMatchNumber());

    if (CheckIntegerMatcher *CI = dyn_cast<CheckIntegerMatcher>(MC->getNext()))
      if (MC->getChildNo() < 5) // Only have CheckChildInteger0...4
        New = new CheckChildIntegerMatcher(MC->getChildNo(), CI->getValue());

    if (auto *CCC = dyn_cast<CheckCondCodeMatcher>(MC->getNext()))
      if (MC->getChildNo() == 2) // Only have CheckChild2CondCode
        New = new CheckChild2CondCodeMatcher(CCC->getCondCodeName());

    if (New) {
      // Insert the new node.
      New->setNext(MatcherPtr.release());
      MatcherPtr.reset(New);
      // Remove the old one.
      MC->setNext(MC->getNext()->takeNext());
      return ContractNodes(MatcherPtr, CGP);
    }
  }

  // Zap movechild -> moveparent.
  if (MoveChildMatcher *MC = dyn_cast<MoveChildMatcher>(N))
    if (MoveParentMatcher *MP = dyn_cast<MoveParentMatcher>(MC->getNext())) {
      MatcherPtr.reset(MP->takeNext());
      return ContractNodes(MatcherPtr, CGP);
    }

  // Turn EmitNode->CompleteMatch into MorphNodeTo if we can.
  if (EmitNodeMatcher *EN = dyn_cast<EmitNodeMatcher>(N))
    if (CompleteMatchMatcher *CM =
            dyn_cast<CompleteMatchMatcher>(EN->getNext())) {
      // We can only use MorphNodeTo if the result values match up.
      unsigned RootResultFirst = EN->getFirstResultSlot();
      bool ResultsMatch = true;
      for (unsigned i = 0, e = CM->getNumResults(); i != e; ++i)
        if (CM->getResult(i) != RootResultFirst + i)
          ResultsMatch = false;

      // If the selected node defines a subset of the glue/chain results, we
      // can't use MorphNodeTo.  For example, we can't use MorphNodeTo if the
      // matched pattern has a chain but the root node doesn't.
      const PatternToMatch &Pattern = CM->getPattern();

      if (!EN->hasChain() &&
          Pattern.getSrcPattern().NodeHasProperty(SDNPHasChain, CGP))
        ResultsMatch = false;

      // If the matched node has glue and the output root doesn't, we can't
      // use MorphNodeTo.
      //
      // NOTE: Strictly speaking, we don't have to check for glue here
      // because the code in the pattern generator doesn't handle it right.  We
      // do it anyway for thoroughness.
      if (!EN->hasOutGlue() &&
          Pattern.getSrcPattern().NodeHasProperty(SDNPOutGlue, CGP))
        ResultsMatch = false;

#if 0
      // If the root result node defines more results than the source root node
      // *and* has a chain or glue input, then we can't match it because it
      // would end up replacing the extra result with the chain/glue.
      if ((EN->hasGlue() || EN->hasChain()) &&
          EN->getNumNonChainGlueVTs() > ... need to get no results reliably ...)
        ResultMatch = false;
#endif

      if (ResultsMatch) {
        const SmallVectorImpl<MVT::SimpleValueType> &VTs = EN->getVTList();
        const SmallVectorImpl<unsigned> &Operands = EN->getOperandList();
        MatcherPtr.reset(new MorphNodeToMatcher(
            EN->getInstruction(), VTs, Operands, EN->hasChain(),
            EN->hasInGlue(), EN->hasOutGlue(), EN->hasMemRefs(),
            EN->getNumFixedArityOperands(), Pattern));
        return;
      }

      // FIXME2: Kill off all the SelectionDAG::SelectNodeTo and getMachineNode
      // variants.
    }

  ContractNodes(N->getNextPtr(), CGP);

  // If we have a CheckType/CheckChildType/Record node followed by a
  // CheckOpcode, invert the two nodes.  We prefer to do structural checks
  // before type checks, as this opens opportunities for factoring on targets
  // like X86 where many operations are valid on multiple types.
  if ((isa<CheckTypeMatcher>(N) || isa<CheckChildTypeMatcher>(N) ||
       isa<RecordMatcher>(N)) &&
      isa<CheckOpcodeMatcher>(N->getNext())) {
    // Unlink the two nodes from the list.
    Matcher *CheckType = MatcherPtr.release();
    Matcher *CheckOpcode = CheckType->takeNext();
    Matcher *Tail = CheckOpcode->takeNext();

    // Relink them.
    MatcherPtr.reset(CheckOpcode);
    CheckOpcode->setNext(CheckType);
    CheckType->setNext(Tail);
    return ContractNodes(MatcherPtr, CGP);
  }

  // If we have a MoveParent followed by a MoveChild, we convert it to
  // MoveSibling.
  if (auto *MP = dyn_cast<MoveParentMatcher>(N)) {
    if (auto *MC = dyn_cast<MoveChildMatcher>(MP->getNext())) {
      auto *MS = new MoveSiblingMatcher(MC->getChildNo());
      MS->setNext(MC->takeNext());
      MatcherPtr.reset(MS);
      return ContractNodes(MatcherPtr, CGP);
    }
    if (auto *RC = dyn_cast<RecordChildMatcher>(MP->getNext())) {
      if (auto *MC = dyn_cast<MoveChildMatcher>(RC->getNext())) {
        if (RC->getChildNo() == MC->getChildNo()) {
          auto *MS = new MoveSiblingMatcher(MC->getChildNo());
          auto *RM = new RecordMatcher(RC->getWhatFor(), RC->getResultNo());
          // Insert the new node.
          RM->setNext(MC->takeNext());
          MS->setNext(RM);
          MatcherPtr.reset(MS);
          return ContractNodes(MatcherPtr, CGP);
        }
      }
    }
  }
}

/// FindNodeWithKind - Scan a series of matchers looking for a matcher with a
/// specified kind.  Return null if we didn't find one otherwise return the
/// matcher.
static Matcher *FindNodeWithKind(Matcher *M, Matcher::KindTy Kind) {
  for (; M; M = M->getNext())
    if (M->getKind() == Kind)
      return M;
  return nullptr;
}

/// FactorNodes - Turn matches like this:
///   Scope
///     OPC_CheckType i32
///       ABC
///     OPC_CheckType i32
///       XYZ
/// into:
///   OPC_CheckType i32
///     Scope
///       ABC
///       XYZ
///
static void FactorNodes(std::unique_ptr<Matcher> &InputMatcherPtr) {
  // Look for a push node. Iterates instead of recurses to reduce stack usage.
  ScopeMatcher *Scope = nullptr;
  std::unique_ptr<Matcher> *RebindableMatcherPtr = &InputMatcherPtr;
  while (!Scope) {
    // If we reached the end of the chain, we're done.
    Matcher *N = RebindableMatcherPtr->get();
    if (!N)
      return;

    // If this is not a push node, just scan for one.
    Scope = dyn_cast<ScopeMatcher>(N);
    if (!Scope)
      RebindableMatcherPtr = &(N->getNextPtr());
  }
  std::unique_ptr<Matcher> &MatcherPtr = *RebindableMatcherPtr;

  // Okay, pull together the children of the scope node into a vector so we can
  // inspect it more easily.
  SmallVector<Matcher *, 32> OptionsToMatch;

  for (unsigned i = 0, e = Scope->getNumChildren(); i != e; ++i) {
    // Factor the subexpression.
    std::unique_ptr<Matcher> Child(Scope->takeChild(i));
    FactorNodes(Child);

    // If the child is a ScopeMatcher we can just merge its contents.
    if (auto *SM = dyn_cast<ScopeMatcher>(Child.get())) {
      for (unsigned j = 0, e = SM->getNumChildren(); j != e; ++j)
        OptionsToMatch.push_back(SM->takeChild(j));
    } else {
      OptionsToMatch.push_back(Child.release());
    }
  }

  // Loop over options to match, merging neighboring patterns with identical
  // starting nodes into a shared matcher.
  auto E = OptionsToMatch.end();
  for (auto I = OptionsToMatch.begin(); I != E; ++I) {
    // If there are no other matchers left, there's nothing to merge with.
    auto J = std::next(I);
    if (J == E)
      break;

    // Remember where we started. We'll use this to move non-equal elements.
    auto K = J;

    // Find the set of matchers that start with this node.
    Matcher *Optn = *I;

    // See if the next option starts with the same matcher.  If the two
    // neighbors *do* start with the same matcher, we can factor the matcher out
    // of at least these two patterns.  See what the maximal set we can merge
    // together is.
    SmallVector<Matcher *, 8> EqualMatchers;
    EqualMatchers.push_back(Optn);

    // Factor all of the known-equal matchers after this one into the same
    // group.
    while (J != E && (*J)->isEqual(Optn))
      EqualMatchers.push_back(*J++);

    // If we found a non-equal matcher, see if it is contradictory with the
    // current node.  If so, we know that the ordering relation between the
    // current sets of nodes and this node don't matter.  Look past it to see if
    // we can merge anything else into this matching group.
    while (J != E) {
      Matcher *ScanMatcher = *J;

      // If we found an entry that matches out matcher, merge it into the set to
      // handle.
      if (Optn->isEqual(ScanMatcher)) {
        // It is equal after all, add the option to EqualMatchers.
        EqualMatchers.push_back(ScanMatcher);
        ++J;
        continue;
      }

      // If the option we're checking for contradicts the start of the list,
      // move it earlier in OptionsToMatch for the next iteration of the outer
      // loop. Then continue searching for equal or contradictory matchers.
      if (Optn->isContradictory(ScanMatcher)) {
        *K++ = *J++;
        continue;
      }

      // If we're scanning for a simple node, see if it occurs later in the
      // sequence.  If so, and if we can move it up, it might be contradictory
      // or the same as what we're looking for.  If so, reorder it.
      if (Optn->isSimplePredicateOrRecordNode()) {
        Matcher *M2 = FindNodeWithKind(ScanMatcher, Optn->getKind());
        if (M2 && M2 != ScanMatcher && M2->canMoveBefore(ScanMatcher) &&
            (M2->isEqual(Optn) || M2->isContradictory(Optn))) {
          Matcher *MatcherWithoutM2 = ScanMatcher->unlinkNode(M2);
          M2->setNext(MatcherWithoutM2);
          *J = M2;
          continue;
        }
      }

      // Otherwise, we don't know how to handle this entry, we have to bail.
      break;
    }

    if (J != E &&
        // Don't print if it's obvious nothing extract could be merged anyway.
        std::next(J) != E) {
      LLVM_DEBUG(errs() << "Couldn't merge this:\n"; Optn->print(errs(), 4);
                 errs() << "into this:\n"; (*J)->print(errs(), 4);
                 (*std::next(J))->printOne(errs());
                 if (std::next(J, 2) != E)(*std::next(J, 2))->printOne(errs());
                 errs() << "\n");
    }

    // If we removed any equal matchers, we may need to slide the rest of the
    // elements down for the next iteration of the outer loop.
    if (J != K) {
      while (J != E)
        *K++ = *J++;

      // Update end pointer for outer loop.
      E = K;
    }

    // If we only found one option starting with this matcher, no factoring is
    // possible. Put the Matcher back in OptionsToMatch.
    if (EqualMatchers.size() == 1) {
      *I = EqualMatchers[0];
      continue;
    }

    // Factor these checks by pulling the first node off each entry and
    // discarding it.  Take the first one off the first entry to reuse.
    Matcher *Shared = Optn;
    Optn = Optn->takeNext();
    EqualMatchers[0] = Optn;

    // Remove and delete the first node from the other matchers we're factoring.
    for (unsigned i = 1, e = EqualMatchers.size(); i != e; ++i) {
      Matcher *Tmp = EqualMatchers[i]->takeNext();
      delete EqualMatchers[i];
      EqualMatchers[i] = Tmp;
      assert(!Optn == !Tmp && "Expected all to be null if any are null");
    }

    if (EqualMatchers[0]) {
      Shared->setNext(new ScopeMatcher(std::move(EqualMatchers)));

      // Recursively factor the newly created node.
      FactorNodes(Shared->getNextPtr());
    }

    // Put the new Matcher where we started in OptionsToMatch.
    *I = Shared;
  }

  // Trim the array to match the updated end.
  if (E != OptionsToMatch.end())
    OptionsToMatch.erase(E, OptionsToMatch.end());

  // If we're down to a single pattern to match, then we don't need this scope
  // anymore.
  if (OptionsToMatch.size() == 1) {
    MatcherPtr.reset(OptionsToMatch[0]);
    return;
  }

  if (OptionsToMatch.empty()) {
    MatcherPtr.reset();
    return;
  }

  // If our factoring failed (didn't achieve anything) see if we can simplify in
  // other ways.

  // Check to see if all of the leading entries are now opcode checks.  If so,
  // we can convert this Scope to be a OpcodeSwitch instead.
  bool AllOpcodeChecks = true, AllTypeChecks = true;
  for (unsigned i = 0, e = OptionsToMatch.size(); i != e; ++i) {
    // Check to see if this breaks a series of CheckOpcodeMatchers.
    if (AllOpcodeChecks && !isa<CheckOpcodeMatcher>(OptionsToMatch[i])) {
#if 0
      if (i > 3) {
        errs() << "FAILING OPC #" << i << "\n";
        OptionsToMatch[i]->dump();
      }
#endif
      AllOpcodeChecks = false;
    }

    // Check to see if this breaks a series of CheckTypeMatcher's.
    if (AllTypeChecks) {
      CheckTypeMatcher *CTM = cast_or_null<CheckTypeMatcher>(
          FindNodeWithKind(OptionsToMatch[i], Matcher::CheckType));
      if (!CTM ||
          // iPTR checks could alias any other case without us knowing, don't
          // bother with them.
          CTM->getType() == MVT::iPTR ||
          // SwitchType only works for result #0.
          CTM->getResNo() != 0 ||
          // If the CheckType isn't at the start of the list, see if we can move
          // it there.
          !CTM->canMoveBefore(OptionsToMatch[i])) {
#if 0
        if (i > 3 && AllTypeChecks) {
          errs() << "FAILING TYPE #" << i << "\n";
          OptionsToMatch[i]->dump();
        }
#endif
        AllTypeChecks = false;
      }
    }
  }

  // If all the options are CheckOpcode's, we can form the SwitchOpcode, woot.
  if (AllOpcodeChecks) {
    StringSet<> Opcodes;
    SmallVector<std::pair<const SDNodeInfo *, Matcher *>, 8> Cases;
    for (unsigned i = 0, e = OptionsToMatch.size(); i != e; ++i) {
      CheckOpcodeMatcher *COM = cast<CheckOpcodeMatcher>(OptionsToMatch[i]);
      assert(Opcodes.insert(COM->getOpcode().getEnumName()).second &&
             "Duplicate opcodes not factored?");
      Cases.push_back(std::pair(&COM->getOpcode(), COM->takeNext()));
      delete COM;
    }

    MatcherPtr.reset(new SwitchOpcodeMatcher(std::move(Cases)));
    return;
  }

  // If all the options are CheckType's, we can form the SwitchType, woot.
  if (AllTypeChecks) {
    DenseMap<unsigned, unsigned> TypeEntry;
    SmallVector<std::pair<MVT::SimpleValueType, Matcher *>, 8> Cases;
    for (unsigned i = 0, e = OptionsToMatch.size(); i != e; ++i) {
      Matcher *M = FindNodeWithKind(OptionsToMatch[i], Matcher::CheckType);
      assert(M && isa<CheckTypeMatcher>(M) && "Unknown Matcher type");

      auto *CTM = cast<CheckTypeMatcher>(M);
      Matcher *MatcherWithoutCTM = OptionsToMatch[i]->unlinkNode(CTM);
      MVT::SimpleValueType CTMTy = CTM->getType();
      delete CTM;

      unsigned &Entry = TypeEntry[CTMTy];
      if (Entry != 0) {
        // If we have unfactored duplicate types, then we should factor them.
        Matcher *PrevMatcher = Cases[Entry - 1].second;
        if (ScopeMatcher *SM = dyn_cast<ScopeMatcher>(PrevMatcher)) {
          SM->setNumChildren(SM->getNumChildren() + 1);
          SM->resetChild(SM->getNumChildren() - 1, MatcherWithoutCTM);
          continue;
        }

        SmallVector<Matcher *, 2> Entries = {PrevMatcher, MatcherWithoutCTM};
        Cases[Entry - 1].second = new ScopeMatcher(std::move(Entries));
        continue;
      }

      Entry = Cases.size() + 1;
      Cases.push_back(std::pair(CTMTy, MatcherWithoutCTM));
    }

    // Make sure we recursively factor any scopes we may have created.
    for (auto &M : Cases) {
      if (ScopeMatcher *SM = dyn_cast<ScopeMatcher>(M.second)) {
        std::unique_ptr<Matcher> Scope(SM);
        FactorNodes(Scope);
        M.second = Scope.release();
        assert(M.second && "null matcher");
      }
    }

    if (Cases.size() != 1) {
      MatcherPtr.reset(new SwitchTypeMatcher(std::move(Cases)));
    } else {
      // If we factored and ended up with one case, create it now.
      MatcherPtr.reset(new CheckTypeMatcher(Cases[0].first, 0));
      MatcherPtr->setNext(Cases[0].second);
    }
    return;
  }

  // Reassemble the Scope node with the adjusted children.
  Scope->setNumChildren(OptionsToMatch.size());
  for (unsigned i = 0, e = OptionsToMatch.size(); i != e; ++i)
    Scope->resetChild(i, OptionsToMatch[i]);
}

void llvm::OptimizeMatcher(std::unique_ptr<Matcher> &MatcherPtr,
                           const CodeGenDAGPatterns &CGP) {
  ContractNodes(MatcherPtr, CGP);
  FactorNodes(MatcherPtr);
}
