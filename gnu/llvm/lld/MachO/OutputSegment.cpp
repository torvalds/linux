//===- OutputSegment.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "OutputSegment.h"
#include "ConcatOutputSection.h"
#include "InputSection.h"
#include "Symbols.h"
#include "SyntheticSections.h"

#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Memory.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/BinaryFormat/MachO.h"

using namespace llvm;
using namespace llvm::MachO;
using namespace lld;
using namespace lld::macho;

static uint32_t initProt(StringRef name) {
  auto it = find_if(
      config->segmentProtections,
      [&](const SegmentProtection &segprot) { return segprot.name == name; });
  if (it != config->segmentProtections.end())
    return it->initProt;

  if (name == segment_names::text)
    return VM_PROT_READ | VM_PROT_EXECUTE;
  if (name == segment_names::pageZero)
    return 0;
  if (name == segment_names::linkEdit)
    return VM_PROT_READ;
  return VM_PROT_READ | VM_PROT_WRITE;
}

static uint32_t maxProt(StringRef name) {
  assert(config->arch() != AK_i386 &&
         "TODO: i386 has different maxProt requirements");
  return initProt(name);
}

static uint32_t flags(StringRef name) {
  // If we ever implement shared cache output support, SG_READ_ONLY should not
  // be used for dylibs that can be placed in it.
  return name == segment_names::dataConst ? (uint32_t)SG_READ_ONLY : 0;
}

size_t OutputSegment::numNonHiddenSections() const {
  size_t count = 0;
  for (const OutputSection *osec : sections)
    count += (!osec->isHidden() ? 1 : 0);
  return count;
}

void OutputSegment::addOutputSection(OutputSection *osec) {
  inputOrder = std::min(inputOrder, osec->inputOrder);

  osec->parent = this;
  sections.push_back(osec);

  for (const SectionAlign &sectAlign : config->sectionAlignments)
    if (sectAlign.segName == name && sectAlign.sectName == osec->name)
      osec->align = sectAlign.align;
}

template <typename T, typename F> static auto compareByOrder(F ord) {
  return [=](T a, T b) { return ord(a) < ord(b); };
}

static int segmentOrder(OutputSegment *seg) {
  return StringSwitch<int>(seg->name)
      .Case(segment_names::pageZero, -4)
      .Case(segment_names::text, -3)
      .Case(segment_names::dataConst, -2)
      .Case(segment_names::data, -1)
      .Case(segment_names::llvm, std::numeric_limits<int>::max() - 1)
      // Make sure __LINKEDIT is the last segment (i.e. all its hidden
      // sections must be ordered after other sections).
      .Case(segment_names::linkEdit, std::numeric_limits<int>::max())
      .Default(seg->inputOrder);
}

static int sectionOrder(OutputSection *osec) {
  StringRef segname = osec->parent->name;
  // Sections are uniquely identified by their segment + section name.
  if (segname == segment_names::text) {
    return StringSwitch<int>(osec->name)
        .Case(section_names::header, -6)
        .Case(section_names::text, -5)
        .Case(section_names::stubs, -4)
        .Case(section_names::stubHelper, -3)
        .Case(section_names::objcStubs, -2)
        .Case(section_names::initOffsets, -1)
        .Case(section_names::unwindInfo, std::numeric_limits<int>::max() - 1)
        .Case(section_names::ehFrame, std::numeric_limits<int>::max())
        .Default(osec->inputOrder);
  } else if (segname == segment_names::data ||
             segname == segment_names::dataConst) {
    // For each thread spawned, dyld will initialize its TLVs by copying the
    // address range from the start of the first thread-local data section to
    // the end of the last one. We therefore arrange these sections contiguously
    // to minimize the amount of memory used. Additionally, since zerofill
    // sections must be at the end of their segments, and since TLV data
    // sections can be zerofills, we end up putting all TLV data sections at the
    // end of the segment.
    switch (sectionType(osec->flags)) {
    case S_THREAD_LOCAL_VARIABLE_POINTERS:
      return std::numeric_limits<int>::max() - 3;
    case S_THREAD_LOCAL_REGULAR:
      return std::numeric_limits<int>::max() - 2;
    case S_THREAD_LOCAL_ZEROFILL:
      return std::numeric_limits<int>::max() - 1;
    case S_ZEROFILL:
      return std::numeric_limits<int>::max();
    default:
      return StringSwitch<int>(osec->name)
          .Case(section_names::got, -3)
          .Case(section_names::lazySymbolPtr, -2)
          .Case(section_names::const_, -1)
          .Default(osec->inputOrder);
    }
  } else if (segname == segment_names::linkEdit) {
    return StringSwitch<int>(osec->name)
        .Case(section_names::chainFixups, -11)
        .Case(section_names::rebase, -10)
        .Case(section_names::binding, -9)
        .Case(section_names::weakBinding, -8)
        .Case(section_names::lazyBinding, -7)
        .Case(section_names::export_, -6)
        .Case(section_names::functionStarts, -5)
        .Case(section_names::dataInCode, -4)
        .Case(section_names::symbolTable, -3)
        .Case(section_names::indirectSymbolTable, -2)
        .Case(section_names::stringTable, -1)
        .Case(section_names::codeSignature, std::numeric_limits<int>::max())
        .Default(osec->inputOrder);
  }
  // ZeroFill sections must always be the at the end of their segments:
  // dyld checks if a segment's file size is smaller than its in-memory
  // size to detect if a segment has zerofill sections, and if so it maps
  // the missing tail as zerofill.
  if (sectionType(osec->flags) == S_ZEROFILL)
    return std::numeric_limits<int>::max();
  return osec->inputOrder;
}

void OutputSegment::sortOutputSections() {
  // Must be stable_sort() to keep special sections such as
  // S_THREAD_LOCAL_REGULAR in input order.
  llvm::stable_sort(sections, compareByOrder<OutputSection *>(sectionOrder));
}

void OutputSegment::assignAddressesToStartEndSymbols() {
  for (Defined *d : segmentStartSymbols)
    d->value = addr;
  for (Defined *d : segmentEndSymbols)
    d->value = addr + vmSize;
}

void macho::sortOutputSegments() {
  llvm::stable_sort(outputSegments,
                    compareByOrder<OutputSegment *>(segmentOrder));
}

static DenseMap<StringRef, OutputSegment *> nameToOutputSegment;
std::vector<OutputSegment *> macho::outputSegments;

void macho::resetOutputSegments() {
  outputSegments.clear();
  nameToOutputSegment.clear();
}

static StringRef maybeRenameSegment(StringRef name) {
  auto newName = config->segmentRenameMap.find(name);
  if (newName != config->segmentRenameMap.end())
    return newName->second;
  return name;
}

OutputSegment *macho::getOrCreateOutputSegment(StringRef name) {
  name = maybeRenameSegment(name);

  OutputSegment *&segRef = nameToOutputSegment[name];
  if (segRef)
    return segRef;

  segRef = make<OutputSegment>();
  segRef->name = name;
  segRef->maxProt = maxProt(name);
  segRef->initProt = initProt(name);
  segRef->flags = flags(name);

  outputSegments.push_back(segRef);
  return segRef;
}
