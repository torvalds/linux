//===- RegionInfoImpl.h - SESE region detection analysis --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Detects single entry single exit regions in the control flow graph.
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_REGIONINFOIMPL_H
#define LLVM_ANALYSIS_REGIONINFOIMPL_H

#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/RegionInfo.h"
#include "llvm/Analysis/RegionIterator.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>
#include <cassert>
#include <iterator>
#include <memory>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

#define DEBUG_TYPE "region"

namespace llvm {
class raw_ostream;

//===----------------------------------------------------------------------===//
/// RegionBase Implementation
template <class Tr>
RegionBase<Tr>::RegionBase(BlockT *Entry, BlockT *Exit,
                           typename Tr::RegionInfoT *RInfo, DomTreeT *dt,
                           RegionT *Parent)
    : RegionNodeBase<Tr>(Parent, Entry, 1), RI(RInfo), DT(dt), exit(Exit) {}

template <class Tr>
RegionBase<Tr>::~RegionBase() {
  // Only clean the cache for this Region. Caches of child Regions will be
  // cleaned when the child Regions are deleted.
  BBNodeMap.clear();
}

template <class Tr>
void RegionBase<Tr>::replaceEntry(BlockT *BB) {
  this->entry.setPointer(BB);
}

template <class Tr>
void RegionBase<Tr>::replaceExit(BlockT *BB) {
  assert(exit && "No exit to replace!");
  exit = BB;
}

template <class Tr>
void RegionBase<Tr>::replaceEntryRecursive(BlockT *NewEntry) {
  std::vector<RegionT *> RegionQueue;
  BlockT *OldEntry = getEntry();

  RegionQueue.push_back(static_cast<RegionT *>(this));
  while (!RegionQueue.empty()) {
    RegionT *R = RegionQueue.back();
    RegionQueue.pop_back();

    R->replaceEntry(NewEntry);
    for (std::unique_ptr<RegionT> &Child : *R) {
      if (Child->getEntry() == OldEntry)
        RegionQueue.push_back(Child.get());
    }
  }
}

template <class Tr>
void RegionBase<Tr>::replaceExitRecursive(BlockT *NewExit) {
  std::vector<RegionT *> RegionQueue;
  BlockT *OldExit = getExit();

  RegionQueue.push_back(static_cast<RegionT *>(this));
  while (!RegionQueue.empty()) {
    RegionT *R = RegionQueue.back();
    RegionQueue.pop_back();

    R->replaceExit(NewExit);
    for (std::unique_ptr<RegionT> &Child : *R) {
      if (Child->getExit() == OldExit)
        RegionQueue.push_back(Child.get());
    }
  }
}

template <class Tr>
bool RegionBase<Tr>::contains(const BlockT *B) const {
  BlockT *BB = const_cast<BlockT *>(B);

  if (!DT->getNode(BB))
    return false;

  BlockT *entry = getEntry(), *exit = getExit();

  // Toplevel region.
  if (!exit)
    return true;

  return (DT->dominates(entry, BB) &&
          !(DT->dominates(exit, BB) && DT->dominates(entry, exit)));
}

template <class Tr>
bool RegionBase<Tr>::contains(const LoopT *L) const {
  // BBs that are not part of any loop are element of the Loop
  // described by the NULL pointer. This loop is not part of any region,
  // except if the region describes the whole function.
  if (!L)
    return getExit() == nullptr;

  if (!contains(L->getHeader()))
    return false;

  SmallVector<BlockT *, 8> ExitingBlocks;
  L->getExitingBlocks(ExitingBlocks);

  for (BlockT *BB : ExitingBlocks) {
    if (!contains(BB))
      return false;
  }

  return true;
}

template <class Tr>
typename Tr::LoopT *RegionBase<Tr>::outermostLoopInRegion(LoopT *L) const {
  if (!contains(L))
    return nullptr;

  while (L && contains(L->getParentLoop())) {
    L = L->getParentLoop();
  }

  return L;
}

template <class Tr>
typename Tr::LoopT *RegionBase<Tr>::outermostLoopInRegion(LoopInfoT *LI,
                                                          BlockT *BB) const {
  assert(LI && BB && "LI and BB cannot be null!");
  LoopT *L = LI->getLoopFor(BB);
  return outermostLoopInRegion(L);
}

template <class Tr>
typename RegionBase<Tr>::BlockT *RegionBase<Tr>::getEnteringBlock() const {
  auto isEnteringBlock = [&](BlockT *Pred, bool AllowRepeats) -> BlockT * {
    assert(!AllowRepeats && "Unexpected parameter value.");
    return DT->getNode(Pred) && !contains(Pred) ? Pred : nullptr;
  };
  return find_singleton<BlockT>(llvm::inverse_children<BlockT *>(getEntry()),
                                isEnteringBlock);
}

template <class Tr>
bool RegionBase<Tr>::getExitingBlocks(
    SmallVectorImpl<BlockT *> &Exitings) const {
  bool CoverAll = true;

  if (!exit)
    return CoverAll;

  for (BlockT *Pred : llvm::inverse_children<BlockT *>(exit)) {
    if (contains(Pred)) {
      Exitings.push_back(Pred);
      continue;
    }

    CoverAll = false;
  }

  return CoverAll;
}

template <class Tr>
typename RegionBase<Tr>::BlockT *RegionBase<Tr>::getExitingBlock() const {
  BlockT *exit = getExit();
  if (!exit)
    return nullptr;

  auto isContained = [&](BlockT *Pred, bool AllowRepeats) -> BlockT * {
    assert(!AllowRepeats && "Unexpected parameter value.");
    return contains(Pred) ? Pred : nullptr;
  };
  return find_singleton<BlockT>(llvm::inverse_children<BlockT *>(exit),
                                isContained);
}

template <class Tr>
bool RegionBase<Tr>::isSimple() const {
  return !isTopLevelRegion() && getEnteringBlock() && getExitingBlock();
}

template <class Tr>
std::string RegionBase<Tr>::getNameStr() const {
  std::string exitName;
  std::string entryName;

  if (getEntry()->getName().empty()) {
    raw_string_ostream OS(entryName);

    getEntry()->printAsOperand(OS, false);
  } else
    entryName = std::string(getEntry()->getName());

  if (getExit()) {
    if (getExit()->getName().empty()) {
      raw_string_ostream OS(exitName);

      getExit()->printAsOperand(OS, false);
    } else
      exitName = std::string(getExit()->getName());
  } else
    exitName = "<Function Return>";

  return entryName + " => " + exitName;
}

template <class Tr>
void RegionBase<Tr>::verifyBBInRegion(BlockT *BB) const {
  if (!contains(BB))
    report_fatal_error("Broken region found: enumerated BB not in region!");

  BlockT *entry = getEntry(), *exit = getExit();

  for (BlockT *Succ : llvm::children<BlockT *>(BB)) {
    if (!contains(Succ) && exit != Succ)
      report_fatal_error("Broken region found: edges leaving the region must go "
                         "to the exit node!");
  }

  if (entry != BB) {
    for (BlockT *Pred : llvm::inverse_children<BlockT *>(BB)) {
      // Allow predecessors that are unreachable, as these are ignored during
      // region analysis.
      if (!contains(Pred) && DT->isReachableFromEntry(Pred))
        report_fatal_error("Broken region found: edges entering the region must "
                           "go to the entry node!");
    }
  }
}

template <class Tr>
void RegionBase<Tr>::verifyWalk(BlockT *BB, std::set<BlockT *> *visited) const {
  BlockT *exit = getExit();

  visited->insert(BB);

  verifyBBInRegion(BB);

  for (BlockT *Succ : llvm::children<BlockT *>(BB)) {
    if (Succ != exit && visited->find(Succ) == visited->end())
      verifyWalk(Succ, visited);
  }
}

template <class Tr>
void RegionBase<Tr>::verifyRegion() const {
  // Only do verification when user wants to, otherwise this expensive check
  // will be invoked by PMDataManager::verifyPreservedAnalysis when
  // a regionpass (marked PreservedAll) finish.
  if (!RegionInfoBase<Tr>::VerifyRegionInfo)
    return;

  std::set<BlockT *> visited;
  verifyWalk(getEntry(), &visited);
}

template <class Tr>
void RegionBase<Tr>::verifyRegionNest() const {
  for (const std::unique_ptr<RegionT> &R : *this)
    R->verifyRegionNest();

  verifyRegion();
}

template <class Tr>
typename RegionBase<Tr>::element_iterator RegionBase<Tr>::element_begin() {
  return GraphTraits<RegionT *>::nodes_begin(static_cast<RegionT *>(this));
}

template <class Tr>
typename RegionBase<Tr>::element_iterator RegionBase<Tr>::element_end() {
  return GraphTraits<RegionT *>::nodes_end(static_cast<RegionT *>(this));
}

template <class Tr>
typename RegionBase<Tr>::const_element_iterator
RegionBase<Tr>::element_begin() const {
  return GraphTraits<const RegionT *>::nodes_begin(
      static_cast<const RegionT *>(this));
}

template <class Tr>
typename RegionBase<Tr>::const_element_iterator
RegionBase<Tr>::element_end() const {
  return GraphTraits<const RegionT *>::nodes_end(
      static_cast<const RegionT *>(this));
}

template <class Tr>
typename Tr::RegionT *RegionBase<Tr>::getSubRegionNode(BlockT *BB) const {
  using RegionT = typename Tr::RegionT;

  RegionT *R = RI->getRegionFor(BB);

  if (!R || R == this)
    return nullptr;

  // If we pass the BB out of this region, that means our code is broken.
  assert(contains(R) && "BB not in current region!");

  while (contains(R->getParent()) && R->getParent() != this)
    R = R->getParent();

  if (R->getEntry() != BB)
    return nullptr;

  return R;
}

template <class Tr>
typename Tr::RegionNodeT *RegionBase<Tr>::getBBNode(BlockT *BB) const {
  assert(contains(BB) && "Can get BB node out of this region!");

  typename BBNodeMapT::const_iterator at = BBNodeMap.find(BB);

  if (at == BBNodeMap.end()) {
    auto Deconst = const_cast<RegionBase<Tr> *>(this);
    typename BBNodeMapT::value_type V = {
        BB,
        std::make_unique<RegionNodeT>(static_cast<RegionT *>(Deconst), BB)};
    at = BBNodeMap.insert(std::move(V)).first;
  }
  return at->second.get();
}

template <class Tr>
typename Tr::RegionNodeT *RegionBase<Tr>::getNode(BlockT *BB) const {
  assert(contains(BB) && "Can get BB node out of this region!");
  if (RegionT *Child = getSubRegionNode(BB))
    return Child->getNode();

  return getBBNode(BB);
}

template <class Tr>
void RegionBase<Tr>::transferChildrenTo(RegionT *To) {
  for (std::unique_ptr<RegionT> &R : *this) {
    R->parent = To;
    To->children.push_back(std::move(R));
  }
  children.clear();
}

template <class Tr>
void RegionBase<Tr>::addSubRegion(RegionT *SubRegion, bool moveChildren) {
  assert(!SubRegion->parent && "SubRegion already has a parent!");
  assert(llvm::none_of(*this,
                       [&](const std::unique_ptr<RegionT> &R) {
                         return R.get() == SubRegion;
                       }) &&
         "Subregion already exists!");

  SubRegion->parent = static_cast<RegionT *>(this);
  children.push_back(std::unique_ptr<RegionT>(SubRegion));

  if (!moveChildren)
    return;

  assert(SubRegion->children.empty() &&
         "SubRegions that contain children are not supported");

  for (RegionNodeT *Element : elements()) {
    if (!Element->isSubRegion()) {
      BlockT *BB = Element->template getNodeAs<BlockT>();

      if (SubRegion->contains(BB))
        RI->setRegionFor(BB, SubRegion);
    }
  }

  std::vector<std::unique_ptr<RegionT>> Keep;
  for (std::unique_ptr<RegionT> &R : *this) {
    if (SubRegion->contains(R.get()) && R.get() != SubRegion) {
      R->parent = SubRegion;
      SubRegion->children.push_back(std::move(R));
    } else
      Keep.push_back(std::move(R));
  }

  children.clear();
  children.insert(
      children.begin(),
      std::move_iterator<typename RegionSet::iterator>(Keep.begin()),
      std::move_iterator<typename RegionSet::iterator>(Keep.end()));
}

template <class Tr>
typename Tr::RegionT *RegionBase<Tr>::removeSubRegion(RegionT *Child) {
  assert(Child->parent == this && "Child is not a child of this region!");
  Child->parent = nullptr;
  typename RegionSet::iterator I =
      llvm::find_if(children, [&](const std::unique_ptr<RegionT> &R) {
        return R.get() == Child;
      });
  assert(I != children.end() && "Region does not exit. Unable to remove.");
  children.erase(children.begin() + (I - begin()));
  return Child;
}

template <class Tr>
unsigned RegionBase<Tr>::getDepth() const {
  unsigned Depth = 0;

  for (RegionT *R = getParent(); R != nullptr; R = R->getParent())
    ++Depth;

  return Depth;
}

template <class Tr>
typename Tr::RegionT *RegionBase<Tr>::getExpandedRegion() const {
  unsigned NumSuccessors = Tr::getNumSuccessors(exit);

  if (NumSuccessors == 0)
    return nullptr;

  RegionT *R = RI->getRegionFor(exit);

  if (R->getEntry() != exit) {
    for (BlockT *Pred : llvm::inverse_children<BlockT *>(getExit()))
      if (!contains(Pred))
        return nullptr;
    if (Tr::getNumSuccessors(exit) == 1)
      return new RegionT(getEntry(), *BlockTraits::child_begin(exit), RI, DT);
    return nullptr;
  }

  while (R->getParent() && R->getParent()->getEntry() == exit)
    R = R->getParent();

  for (BlockT *Pred : llvm::inverse_children<BlockT *>(getExit())) {
    if (!(contains(Pred) || R->contains(Pred)))
      return nullptr;
  }

  return new RegionT(getEntry(), R->getExit(), RI, DT);
}

template <class Tr>
void RegionBase<Tr>::print(raw_ostream &OS, bool print_tree, unsigned level,
                           PrintStyle Style) const {
  if (print_tree)
    OS.indent(level * 2) << '[' << level << "] " << getNameStr();
  else
    OS.indent(level * 2) << getNameStr();

  OS << '\n';

  if (Style != PrintNone) {
    OS.indent(level * 2) << "{\n";
    OS.indent(level * 2 + 2);

    if (Style == PrintBB) {
      for (const auto *BB : blocks())
        OS << BB->getName() << ", "; // TODO: remove the last ","
    } else if (Style == PrintRN) {
      for (const RegionNodeT *Element : elements()) {
        OS << *Element << ", "; // TODO: remove the last ",
      }
    }

    OS << '\n';
  }

  if (print_tree) {
    for (const std::unique_ptr<RegionT> &R : *this)
      R->print(OS, print_tree, level + 1, Style);
  }

  if (Style != PrintNone)
    OS.indent(level * 2) << "} \n";
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
template <class Tr>
void RegionBase<Tr>::dump() const {
  print(dbgs(), true, getDepth(), RegionInfoBase<Tr>::printStyle);
}
#endif

template <class Tr>
void RegionBase<Tr>::clearNodeCache() {
  BBNodeMap.clear();
  for (std::unique_ptr<RegionT> &R : *this)
    R->clearNodeCache();
}

//===----------------------------------------------------------------------===//
// RegionInfoBase implementation
//

template <class Tr>
RegionInfoBase<Tr>::RegionInfoBase() = default;

template <class Tr>
RegionInfoBase<Tr>::~RegionInfoBase() {
  releaseMemory();
}

template <class Tr>
void RegionInfoBase<Tr>::verifyBBMap(const RegionT *R) const {
  assert(R && "Re must be non-null");
  for (const typename Tr::RegionNodeT *Element : R->elements()) {
    if (Element->isSubRegion()) {
      const RegionT *SR = Element->template getNodeAs<RegionT>();
      verifyBBMap(SR);
    } else {
      BlockT *BB = Element->template getNodeAs<BlockT>();
      if (getRegionFor(BB) != R)
        report_fatal_error("BB map does not match region nesting");
    }
  }
}

template <class Tr>
bool RegionInfoBase<Tr>::isCommonDomFrontier(BlockT *BB, BlockT *entry,
                                             BlockT *exit) const {
  for (BlockT *P : llvm::inverse_children<BlockT *>(BB)) {
    if (DT->dominates(entry, P) && !DT->dominates(exit, P))
      return false;
  }

  return true;
}

template <class Tr>
bool RegionInfoBase<Tr>::isRegion(BlockT *entry, BlockT *exit) const {
  assert(entry && exit && "entry and exit must not be null!");

  using DST = typename DomFrontierT::DomSetType;

  DST *entrySuccs = &DF->find(entry)->second;

  // Exit is the header of a loop that contains the entry. In this case,
  // the dominance frontier must only contain the exit.
  if (!DT->dominates(entry, exit)) {
    for (BlockT *successor : *entrySuccs) {
      if (successor != exit && successor != entry)
        return false;
    }

    return true;
  }

  DST *exitSuccs = &DF->find(exit)->second;

  // Do not allow edges leaving the region.
  for (BlockT *Succ : *entrySuccs) {
    if (Succ == exit || Succ == entry)
      continue;
    if (!exitSuccs->contains(Succ))
      return false;
    if (!isCommonDomFrontier(Succ, entry, exit))
      return false;
  }

  // Do not allow edges pointing into the region.
  for (BlockT *Succ : *exitSuccs) {
    if (DT->properlyDominates(entry, Succ) && Succ != exit)
      return false;
  }

  return true;
}

template <class Tr>
void RegionInfoBase<Tr>::insertShortCut(BlockT *entry, BlockT *exit,
                                        BBtoBBMap *ShortCut) const {
  assert(entry && exit && "entry and exit must not be null!");

  typename BBtoBBMap::iterator e = ShortCut->find(exit);

  if (e == ShortCut->end())
    // No further region at exit available.
    (*ShortCut)[entry] = exit;
  else {
    // We found a region e that starts at exit. Therefore (entry, e->second)
    // is also a region, that is larger than (entry, exit). Insert the
    // larger one.
    BlockT *BB = e->second;
    (*ShortCut)[entry] = BB;
  }
}

template <class Tr>
typename Tr::DomTreeNodeT *
RegionInfoBase<Tr>::getNextPostDom(DomTreeNodeT *N, BBtoBBMap *ShortCut) const {
  typename BBtoBBMap::iterator e = ShortCut->find(N->getBlock());

  if (e == ShortCut->end())
    return N->getIDom();

  return PDT->getNode(e->second)->getIDom();
}

template <class Tr>
bool RegionInfoBase<Tr>::isTrivialRegion(BlockT *entry, BlockT *exit) const {
  assert(entry && exit && "entry and exit must not be null!");

  unsigned num_successors =
      BlockTraits::child_end(entry) - BlockTraits::child_begin(entry);

  if (num_successors <= 1 && exit == *(BlockTraits::child_begin(entry)))
    return true;

  return false;
}

template <class Tr>
typename Tr::RegionT *RegionInfoBase<Tr>::createRegion(BlockT *entry,
                                                       BlockT *exit) {
  assert(entry && exit && "entry and exit must not be null!");

  if (isTrivialRegion(entry, exit))
    return nullptr;

  RegionT *region =
      new RegionT(entry, exit, static_cast<RegionInfoT *>(this), DT);
  BBtoRegion.insert({entry, region});

  region->verifyRegion();

  updateStatistics(region);
  return region;
}

template <class Tr>
void RegionInfoBase<Tr>::findRegionsWithEntry(BlockT *entry,
                                              BBtoBBMap *ShortCut) {
  assert(entry);

  DomTreeNodeT *N = PDT->getNode(entry);
  if (!N)
    return;

  RegionT *lastRegion = nullptr;
  BlockT *lastExit = entry;

  // As only a BasicBlock that postdominates entry can finish a region, walk the
  // post dominance tree upwards.
  while ((N = getNextPostDom(N, ShortCut))) {
    BlockT *exit = N->getBlock();

    if (!exit)
      break;

    if (isRegion(entry, exit)) {
      RegionT *newRegion = createRegion(entry, exit);

      if (lastRegion)
        newRegion->addSubRegion(lastRegion);

      lastRegion = newRegion;
      lastExit = exit;
    }

    // This can never be a region, so stop the search.
    if (!DT->dominates(entry, exit))
      break;
  }

  // Tried to create regions from entry to lastExit.  Next time take a
  // shortcut from entry to lastExit.
  if (lastExit != entry)
    insertShortCut(entry, lastExit, ShortCut);
}

template <class Tr>
void RegionInfoBase<Tr>::scanForRegions(FuncT &F, BBtoBBMap *ShortCut) {
  using FuncPtrT = std::add_pointer_t<FuncT>;

  BlockT *entry = GraphTraits<FuncPtrT>::getEntryNode(&F);
  DomTreeNodeT *N = DT->getNode(entry);

  // Iterate over the dominance tree in post order to start with the small
  // regions from the bottom of the dominance tree.  If the small regions are
  // detected first, detection of bigger regions is faster, as we can jump
  // over the small regions.
  for (auto DomNode : post_order(N))
    findRegionsWithEntry(DomNode->getBlock(), ShortCut);
}

template <class Tr>
typename Tr::RegionT *RegionInfoBase<Tr>::getTopMostParent(RegionT *region) {
  while (region->getParent())
    region = region->getParent();

  return region;
}

template <class Tr>
void RegionInfoBase<Tr>::buildRegionsTree(DomTreeNodeT *N, RegionT *region) {
  BlockT *BB = N->getBlock();

  // Passed region exit
  while (BB == region->getExit())
    region = region->getParent();

  typename BBtoRegionMap::iterator it = BBtoRegion.find(BB);

  // This basic block is a start block of a region. It is already in the
  // BBtoRegion relation. Only the child basic blocks have to be updated.
  if (it != BBtoRegion.end()) {
    RegionT *newRegion = it->second;
    region->addSubRegion(getTopMostParent(newRegion));
    region = newRegion;
  } else {
    BBtoRegion[BB] = region;
  }

  for (DomTreeNodeBase<BlockT> *C : *N) {
    buildRegionsTree(C, region);
  }
}

#ifdef EXPENSIVE_CHECKS
template <class Tr>
bool RegionInfoBase<Tr>::VerifyRegionInfo = true;
#else
template <class Tr>
bool RegionInfoBase<Tr>::VerifyRegionInfo = false;
#endif

template <class Tr>
typename Tr::RegionT::PrintStyle RegionInfoBase<Tr>::printStyle =
    RegionBase<Tr>::PrintNone;

template <class Tr>
void RegionInfoBase<Tr>::print(raw_ostream &OS) const {
  OS << "Region tree:\n";
  TopLevelRegion->print(OS, true, 0, printStyle);
  OS << "End region tree\n";
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
template <class Tr>
void RegionInfoBase<Tr>::dump() const { print(dbgs()); }
#endif

template <class Tr> void RegionInfoBase<Tr>::releaseMemory() {
  BBtoRegion.clear();
  if (TopLevelRegion) {
    delete TopLevelRegion;
    TopLevelRegion = nullptr;
  }
}

template <class Tr>
void RegionInfoBase<Tr>::verifyAnalysis() const {
  // Do only verify regions if explicitely activated using EXPENSIVE_CHECKS or
  // -verify-region-info
  if (!RegionInfoBase<Tr>::VerifyRegionInfo)
    return;

  TopLevelRegion->verifyRegionNest();

  verifyBBMap(TopLevelRegion);
}

// Region pass manager support.
template <class Tr>
typename Tr::RegionT *RegionInfoBase<Tr>::getRegionFor(BlockT *BB) const {
  return BBtoRegion.lookup(BB);
}

template <class Tr>
void RegionInfoBase<Tr>::setRegionFor(BlockT *BB, RegionT *R) {
  BBtoRegion[BB] = R;
}

template <class Tr>
typename Tr::RegionT *RegionInfoBase<Tr>::operator[](BlockT *BB) const {
  return getRegionFor(BB);
}

template <class Tr>
typename RegionInfoBase<Tr>::BlockT *
RegionInfoBase<Tr>::getMaxRegionExit(BlockT *BB) const {
  BlockT *Exit = nullptr;

  while (true) {
    // Get largest region that starts at BB.
    RegionT *R = getRegionFor(BB);
    while (R && R->getParent() && R->getParent()->getEntry() == BB)
      R = R->getParent();

    // Get the single exit of BB.
    if (R && R->getEntry() == BB)
      Exit = R->getExit();
    else if (++BlockTraits::child_begin(BB) == BlockTraits::child_end(BB))
      Exit = *BlockTraits::child_begin(BB);
    else // No single exit exists.
      return Exit;

    // Get largest region that starts at Exit.
    RegionT *ExitR = getRegionFor(Exit);
    while (ExitR && ExitR->getParent() &&
           ExitR->getParent()->getEntry() == Exit)
      ExitR = ExitR->getParent();

    for (BlockT *Pred : llvm::inverse_children<BlockT *>(Exit)) {
      if (!R->contains(Pred) && !ExitR->contains(Pred))
        break;
    }

    // This stops infinite cycles.
    if (DT->dominates(Exit, BB))
      break;

    BB = Exit;
  }

  return Exit;
}

template <class Tr>
typename Tr::RegionT *RegionInfoBase<Tr>::getCommonRegion(RegionT *A,
                                                          RegionT *B) const {
  assert(A && B && "One of the Regions is NULL");

  if (A->contains(B))
    return A;

  while (!B->contains(A))
    B = B->getParent();

  return B;
}

template <class Tr>
typename Tr::RegionT *
RegionInfoBase<Tr>::getCommonRegion(SmallVectorImpl<RegionT *> &Regions) const {
  RegionT *ret = Regions.pop_back_val();

  for (RegionT *R : Regions)
    ret = getCommonRegion(ret, R);

  return ret;
}

template <class Tr>
typename Tr::RegionT *
RegionInfoBase<Tr>::getCommonRegion(SmallVectorImpl<BlockT *> &BBs) const {
  RegionT *ret = getRegionFor(BBs.back());
  BBs.pop_back();

  for (BlockT *BB : BBs)
    ret = getCommonRegion(ret, getRegionFor(BB));

  return ret;
}

template <class Tr>
void RegionInfoBase<Tr>::calculate(FuncT &F) {
  using FuncPtrT = std::add_pointer_t<FuncT>;

  // ShortCut a function where for every BB the exit of the largest region
  // starting with BB is stored. These regions can be threated as single BBS.
  // This improves performance on linear CFGs.
  BBtoBBMap ShortCut;

  scanForRegions(F, &ShortCut);
  BlockT *BB = GraphTraits<FuncPtrT>::getEntryNode(&F);
  buildRegionsTree(DT->getNode(BB), TopLevelRegion);
}

} // end namespace llvm

#undef DEBUG_TYPE

#endif // LLVM_ANALYSIS_REGIONINFOIMPL_H
