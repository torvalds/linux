//===- ConcatOutputSection.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ConcatOutputSection.h"
#include "Config.h"
#include "OutputSegment.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "SyntheticSections.h"
#include "Target.h"
#include "lld/Common/CommonLinkerContext.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/TimeProfiler.h"

using namespace llvm;
using namespace llvm::MachO;
using namespace lld;
using namespace lld::macho;

MapVector<NamePair, ConcatOutputSection *> macho::concatOutputSections;

void ConcatOutputSection::addInput(ConcatInputSection *input) {
  assert(input->parent == this);
  if (inputs.empty()) {
    align = input->align;
    flags = input->getFlags();
  } else {
    align = std::max(align, input->align);
    finalizeFlags(input);
  }
  inputs.push_back(input);
}

// Branch-range extension can be implemented in two ways, either through ...
//
// (1) Branch islands: Single branch instructions (also of limited range),
//     that might be chained in multiple hops to reach the desired
//     destination. On ARM64, as 16 branch islands are needed to hop between
//     opposite ends of a 2 GiB program. LD64 uses branch islands exclusively,
//     even when it needs excessive hops.
//
// (2) Thunks: Instruction(s) to load the destination address into a scratch
//     register, followed by a register-indirect branch. Thunks are
//     constructed to reach any arbitrary address, so need not be
//     chained. Although thunks need not be chained, a program might need
//     multiple thunks to the same destination distributed throughout a large
//     program so that all call sites can have one within range.
//
// The optimal approach is to mix islands for destinations within two hops,
// and use thunks for destinations at greater distance. For now, we only
// implement thunks. TODO: Adding support for branch islands!
//
// Internally -- as expressed in LLD's data structures -- a
// branch-range-extension thunk consists of:
//
// (1) new Defined symbol for the thunk named
//     <FUNCTION>.thunk.<SEQUENCE>, which references ...
// (2) new InputSection, which contains ...
// (3.1) new data for the instructions to load & branch to the far address +
// (3.2) new Relocs on instructions to load the far address, which reference ...
// (4.1) existing Defined symbol for the real function in __text, or
// (4.2) existing DylibSymbol for the real function in a dylib
//
// Nearly-optimal thunk-placement algorithm features:
//
// * Single pass: O(n) on the number of call sites.
//
// * Accounts for the exact space overhead of thunks - no heuristics
//
// * Exploits the full range of call instructions - forward & backward
//
// Data:
//
// * DenseMap<Symbol *, ThunkInfo> thunkMap: Maps the function symbol
//   to its thunk bookkeeper.
//
// * struct ThunkInfo (bookkeeper): Call instructions have limited range, and
//   distant call sites might be unable to reach the same thunk, so multiple
//   thunks are necessary to serve all call sites in a very large program. A
//   thunkInfo stores state for all thunks associated with a particular
//   function:
//     (a) thunk symbol
//     (b) input section containing stub code, and
//     (c) sequence number for the active thunk incarnation.
//   When an old thunk goes out of range, we increment the sequence number and
//   create a new thunk named <FUNCTION>.thunk.<SEQUENCE>.
//
// * A thunk consists of
//     (a) a Defined symbol pointing to
//     (b) an InputSection holding machine code (similar to a MachO stub), and
//     (c) relocs referencing the real function for fixing up the stub code.
//
// * std::vector<InputSection *> MergedInputSection::thunks: A vector parallel
//   to the inputs vector. We store new thunks via cheap vector append, rather
//   than costly insertion into the inputs vector.
//
// Control Flow:
//
// * During address assignment, MergedInputSection::finalize() examines call
//   sites by ascending address and creates thunks.  When a function is beyond
//   the range of a call site, we need a thunk. Place it at the largest
//   available forward address from the call site. Call sites increase
//   monotonically and thunks are always placed as far forward as possible;
//   thus, we place thunks at monotonically increasing addresses. Once a thunk
//   is placed, it and all previous input-section addresses are final.
//
// * ConcatInputSection::finalize() and ConcatInputSection::writeTo() merge
//   the inputs and thunks vectors (both ordered by ascending address), which
//   is simple and cheap.

DenseMap<Symbol *, ThunkInfo> lld::macho::thunkMap;

// Determine whether we need thunks, which depends on the target arch -- RISC
// (i.e., ARM) generally does because it has limited-range branch/call
// instructions, whereas CISC (i.e., x86) generally doesn't. RISC only needs
// thunks for programs so large that branch source & destination addresses
// might differ more than the range of branch instruction(s).
bool TextOutputSection::needsThunks() const {
  if (!target->usesThunks())
    return false;
  uint64_t isecAddr = addr;
  for (ConcatInputSection *isec : inputs)
    isecAddr = alignToPowerOf2(isecAddr, isec->align) + isec->getSize();
  if (isecAddr - addr + in.stubs->getSize() <=
      std::min(target->backwardBranchRange, target->forwardBranchRange))
    return false;
  // Yes, this program is large enough to need thunks.
  for (ConcatInputSection *isec : inputs) {
    for (Reloc &r : isec->relocs) {
      if (!target->hasAttr(r.type, RelocAttrBits::BRANCH))
        continue;
      auto *sym = r.referent.get<Symbol *>();
      // Pre-populate the thunkMap and memoize call site counts for every
      // InputSection and ThunkInfo. We do this for the benefit of
      // estimateStubsInRangeVA().
      ThunkInfo &thunkInfo = thunkMap[sym];
      // Knowing ThunkInfo call site count will help us know whether or not we
      // might need to create more for this referent at the time we are
      // estimating distance to __stubs in estimateStubsInRangeVA().
      ++thunkInfo.callSiteCount;
      // We can avoid work on InputSections that have no BRANCH relocs.
      isec->hasCallSites = true;
    }
  }
  return true;
}

// Since __stubs is placed after __text, we must estimate the address
// beyond which stubs are within range of a simple forward branch.
// This is called exactly once, when the last input section has been finalized.
uint64_t TextOutputSection::estimateStubsInRangeVA(size_t callIdx) const {
  // Tally the functions which still have call sites remaining to process,
  // which yields the maximum number of thunks we might yet place.
  size_t maxPotentialThunks = 0;
  for (auto &tp : thunkMap) {
    ThunkInfo &ti = tp.second;
    // This overcounts: Only sections that are in forward jump range from the
    // currently-active section get finalized, and all input sections are
    // finalized when estimateStubsInRangeVA() is called. So only backward
    // jumps will need thunks, but we count all jumps.
    if (ti.callSitesUsed < ti.callSiteCount)
      maxPotentialThunks += 1;
  }
  // Tally the total size of input sections remaining to process.
  uint64_t isecVA = inputs[callIdx]->getVA();
  uint64_t isecEnd = isecVA;
  for (size_t i = callIdx; i < inputs.size(); i++) {
    InputSection *isec = inputs[i];
    isecEnd = alignToPowerOf2(isecEnd, isec->align) + isec->getSize();
  }
  // Estimate the address after which call sites can safely call stubs
  // directly rather than through intermediary thunks.
  uint64_t forwardBranchRange = target->forwardBranchRange;
  assert(isecEnd > forwardBranchRange &&
         "should not run thunk insertion if all code fits in jump range");
  assert(isecEnd - isecVA <= forwardBranchRange &&
         "should only finalize sections in jump range");
  uint64_t stubsInRangeVA = isecEnd + maxPotentialThunks * target->thunkSize +
                            in.stubs->getSize() - forwardBranchRange;
  log("thunks = " + std::to_string(thunkMap.size()) +
      ", potential = " + std::to_string(maxPotentialThunks) +
      ", stubs = " + std::to_string(in.stubs->getSize()) + ", isecVA = " +
      utohexstr(isecVA) + ", threshold = " + utohexstr(stubsInRangeVA) +
      ", isecEnd = " + utohexstr(isecEnd) +
      ", tail = " + utohexstr(isecEnd - isecVA) +
      ", slop = " + utohexstr(forwardBranchRange - (isecEnd - isecVA)));
  return stubsInRangeVA;
}

void ConcatOutputSection::finalizeOne(ConcatInputSection *isec) {
  size = alignToPowerOf2(size, isec->align);
  fileSize = alignToPowerOf2(fileSize, isec->align);
  isec->outSecOff = size;
  isec->isFinal = true;
  size += isec->getSize();
  fileSize += isec->getFileSize();
}

void ConcatOutputSection::finalizeContents() {
  for (ConcatInputSection *isec : inputs)
    finalizeOne(isec);
}

void TextOutputSection::finalize() {
  if (!needsThunks()) {
    for (ConcatInputSection *isec : inputs)
      finalizeOne(isec);
    return;
  }

  uint64_t forwardBranchRange = target->forwardBranchRange;
  uint64_t backwardBranchRange = target->backwardBranchRange;
  uint64_t stubsInRangeVA = TargetInfo::outOfRangeVA;
  size_t thunkSize = target->thunkSize;
  size_t relocCount = 0;
  size_t callSiteCount = 0;
  size_t thunkCallCount = 0;
  size_t thunkCount = 0;

  // Walk all sections in order. Finalize all sections that are less than
  // forwardBranchRange in front of it.
  // isecVA is the address of the current section.
  // addr + size is the start address of the first non-finalized section.

  // inputs[finalIdx] is for finalization (address-assignment)
  size_t finalIdx = 0;
  // Kick-off by ensuring that the first input section has an address
  for (size_t callIdx = 0, endIdx = inputs.size(); callIdx < endIdx;
       ++callIdx) {
    if (finalIdx == callIdx)
      finalizeOne(inputs[finalIdx++]);
    ConcatInputSection *isec = inputs[callIdx];
    assert(isec->isFinal);
    uint64_t isecVA = isec->getVA();

    // Assign addresses up-to the forward branch-range limit.
    // Every call instruction needs a small number of bytes (on Arm64: 4),
    // and each inserted thunk needs a slightly larger number of bytes
    // (on Arm64: 12). If a section starts with a branch instruction and
    // contains several branch instructions in succession, then the distance
    // from the current position to the position where the thunks are inserted
    // grows. So leave room for a bunch of thunks.
    unsigned slop = 256 * thunkSize;
    while (finalIdx < endIdx) {
      uint64_t expectedNewSize =
          alignToPowerOf2(addr + size, inputs[finalIdx]->align) +
          inputs[finalIdx]->getSize();
      if (expectedNewSize >= isecVA + forwardBranchRange - slop)
        break;
      finalizeOne(inputs[finalIdx++]);
    }

    if (!isec->hasCallSites)
      continue;

    if (finalIdx == endIdx && stubsInRangeVA == TargetInfo::outOfRangeVA) {
      // When we have finalized all input sections, __stubs (destined
      // to follow __text) comes within range of forward branches and
      // we can estimate the threshold address after which we can
      // reach any stub with a forward branch. Note that although it
      // sits in the middle of a loop, this code executes only once.
      // It is in the loop because we need to call it at the proper
      // time: the earliest call site from which the end of __text
      // (and start of __stubs) comes within range of a forward branch.
      stubsInRangeVA = estimateStubsInRangeVA(callIdx);
    }
    // Process relocs by ascending address, i.e., ascending offset within isec
    std::vector<Reloc> &relocs = isec->relocs;
    // FIXME: This property does not hold for object files produced by ld64's
    // `-r` mode.
    assert(is_sorted(relocs,
                     [](Reloc &a, Reloc &b) { return a.offset > b.offset; }));
    for (Reloc &r : reverse(relocs)) {
      ++relocCount;
      if (!target->hasAttr(r.type, RelocAttrBits::BRANCH))
        continue;
      ++callSiteCount;
      // Calculate branch reachability boundaries
      uint64_t callVA = isecVA + r.offset;
      uint64_t lowVA =
          backwardBranchRange < callVA ? callVA - backwardBranchRange : 0;
      uint64_t highVA = callVA + forwardBranchRange;
      // Calculate our call referent address
      auto *funcSym = r.referent.get<Symbol *>();
      ThunkInfo &thunkInfo = thunkMap[funcSym];
      // The referent is not reachable, so we need to use a thunk ...
      if (funcSym->isInStubs() && callVA >= stubsInRangeVA) {
        assert(callVA != TargetInfo::outOfRangeVA);
        // ... Oh, wait! We are close enough to the end that __stubs
        // are now within range of a simple forward branch.
        continue;
      }
      uint64_t funcVA = funcSym->resolveBranchVA();
      ++thunkInfo.callSitesUsed;
      if (lowVA <= funcVA && funcVA <= highVA) {
        // The referent is reachable with a simple call instruction.
        continue;
      }
      ++thunkInfo.thunkCallCount;
      ++thunkCallCount;
      // If an existing thunk is reachable, use it ...
      if (thunkInfo.sym) {
        uint64_t thunkVA = thunkInfo.isec->getVA();
        if (lowVA <= thunkVA && thunkVA <= highVA) {
          r.referent = thunkInfo.sym;
          continue;
        }
      }
      // ... otherwise, create a new thunk.
      if (addr + size > highVA) {
        // There were too many consecutive branch instructions for `slop`
        // above. If you hit this: For the current algorithm, just bumping up
        // slop above and trying again is probably simplest. (See also PR51578
        // comment 5).
        fatal(Twine(__FUNCTION__) + ": FIXME: thunk range overrun");
      }
      thunkInfo.isec =
          makeSyntheticInputSection(isec->getSegName(), isec->getName());
      thunkInfo.isec->parent = this;
      assert(thunkInfo.isec->live);

      StringRef thunkName = saver().save(funcSym->getName() + ".thunk." +
                                         std::to_string(thunkInfo.sequence++));
      if (!isa<Defined>(funcSym) || cast<Defined>(funcSym)->isExternal()) {
        r.referent = thunkInfo.sym = symtab->addDefined(
            thunkName, /*file=*/nullptr, thunkInfo.isec, /*value=*/0, thunkSize,
            /*isWeakDef=*/false, /*isPrivateExtern=*/true,
            /*isReferencedDynamically=*/false, /*noDeadStrip=*/false,
            /*isWeakDefCanBeHidden=*/false);
      } else {
        r.referent = thunkInfo.sym = make<Defined>(
            thunkName, /*file=*/nullptr, thunkInfo.isec, /*value=*/0, thunkSize,
            /*isWeakDef=*/false, /*isExternal=*/false, /*isPrivateExtern=*/true,
            /*includeInSymtab=*/true, /*isReferencedDynamically=*/false,
            /*noDeadStrip=*/false, /*isWeakDefCanBeHidden=*/false);
      }
      thunkInfo.sym->used = true;
      target->populateThunk(thunkInfo.isec, funcSym);
      finalizeOne(thunkInfo.isec);
      thunks.push_back(thunkInfo.isec);
      ++thunkCount;
    }
  }

  log("thunks for " + parent->name + "," + name +
      ": funcs = " + std::to_string(thunkMap.size()) +
      ", relocs = " + std::to_string(relocCount) +
      ", all calls = " + std::to_string(callSiteCount) +
      ", thunk calls = " + std::to_string(thunkCallCount) +
      ", thunks = " + std::to_string(thunkCount));
}

void ConcatOutputSection::writeTo(uint8_t *buf) const {
  for (ConcatInputSection *isec : inputs)
    isec->writeTo(buf + isec->outSecOff);
}

void TextOutputSection::writeTo(uint8_t *buf) const {
  // Merge input sections from thunk & ordinary vectors
  size_t i = 0, ie = inputs.size();
  size_t t = 0, te = thunks.size();
  while (i < ie || t < te) {
    while (i < ie && (t == te || inputs[i]->empty() ||
                      inputs[i]->outSecOff < thunks[t]->outSecOff)) {
      inputs[i]->writeTo(buf + inputs[i]->outSecOff);
      ++i;
    }
    while (t < te && (i == ie || thunks[t]->outSecOff < inputs[i]->outSecOff)) {
      thunks[t]->writeTo(buf + thunks[t]->outSecOff);
      ++t;
    }
  }
}

void ConcatOutputSection::finalizeFlags(InputSection *input) {
  switch (sectionType(input->getFlags())) {
  default /*type-unspec'ed*/:
    // FIXME: Add additional logic here when supporting emitting obj files.
    break;
  case S_4BYTE_LITERALS:
  case S_8BYTE_LITERALS:
  case S_16BYTE_LITERALS:
  case S_CSTRING_LITERALS:
  case S_ZEROFILL:
  case S_LAZY_SYMBOL_POINTERS:
  case S_MOD_TERM_FUNC_POINTERS:
  case S_THREAD_LOCAL_REGULAR:
  case S_THREAD_LOCAL_ZEROFILL:
  case S_THREAD_LOCAL_VARIABLES:
  case S_THREAD_LOCAL_INIT_FUNCTION_POINTERS:
  case S_THREAD_LOCAL_VARIABLE_POINTERS:
  case S_NON_LAZY_SYMBOL_POINTERS:
  case S_SYMBOL_STUBS:
    flags |= input->getFlags();
    break;
  }
}

ConcatOutputSection *
ConcatOutputSection::getOrCreateForInput(const InputSection *isec) {
  NamePair names = maybeRenameSection({isec->getSegName(), isec->getName()});
  ConcatOutputSection *&osec = concatOutputSections[names];
  if (!osec) {
    if (isec->getSegName() == segment_names::text &&
        isec->getName() != section_names::gccExceptTab &&
        isec->getName() != section_names::ehFrame)
      osec = make<TextOutputSection>(names.second);
    else
      osec = make<ConcatOutputSection>(names.second);
  }
  return osec;
}

NamePair macho::maybeRenameSection(NamePair key) {
  auto newNames = config->sectionRenameMap.find(key);
  if (newNames != config->sectionRenameMap.end())
    return newNames->second;
  return key;
}
