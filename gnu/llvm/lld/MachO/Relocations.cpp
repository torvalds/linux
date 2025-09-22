//===- Relocations.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Relocations.h"
#include "ConcatOutputSection.h"
#include "Symbols.h"
#include "SyntheticSections.h"
#include "Target.h"

#include "lld/Common/ErrorHandler.h"

using namespace llvm;
using namespace lld;
using namespace lld::macho;

static_assert(sizeof(void *) != 8 || sizeof(Reloc) == 24,
              "Try to minimize Reloc's size; we create many instances");

InputSection *Reloc::getReferentInputSection() const {
  if (const auto *sym = referent.dyn_cast<Symbol *>()) {
    if (const auto *d = dyn_cast<Defined>(sym))
      return d->isec();
    return nullptr;
  } else {
    return referent.get<InputSection *>();
  }
}

bool macho::validateSymbolRelocation(const Symbol *sym,
                                     const InputSection *isec, const Reloc &r) {
  const RelocAttrs &relocAttrs = target->getRelocAttrs(r.type);
  bool valid = true;
  auto message = [&](const Twine &diagnostic) {
    valid = false;
    return (isec->getLocation(r.offset) + ": " + relocAttrs.name +
            " relocation " + diagnostic)
        .str();
  };

  if (relocAttrs.hasAttr(RelocAttrBits::TLV) != sym->isTlv())
    error(message(Twine("requires that symbol ") + sym->getName() + " " +
                  (sym->isTlv() ? "not " : "") + "be thread-local"));

  return valid;
}

// Given an offset in the output buffer, figure out which ConcatInputSection (if
// any) maps to it. At the same time, update the offset such that it is relative
// to the InputSection rather than to the output buffer.
//
// Obtaining the InputSection allows us to have better error diagnostics.
// However, many of our relocation-handling methods do not take the InputSection
// as a parameter. Since we are already passing the buffer offsets to our Target
// methods, this function allows us to emit better errors without threading an
// additional InputSection argument through the call stack.
//
// This is implemented as a slow linear search through OutputSegments,
// OutputSections, and finally the InputSections themselves. However, this
// function should be called only on error paths, so some overhead is fine.
InputSection *macho::offsetToInputSection(uint64_t *off) {
  for (OutputSegment *seg : outputSegments) {
    if (*off < seg->fileOff || *off >= seg->fileOff + seg->fileSize)
      continue;

    const std::vector<OutputSection *> &sections = seg->getSections();
    size_t osecIdx = 0;
    for (; osecIdx < sections.size(); ++osecIdx)
      if (*off < sections[osecIdx]->fileOff)
        break;
    assert(osecIdx > 0);
    // We should be only calling this function on offsets that belong to
    // ConcatOutputSections.
    auto *osec = cast<ConcatOutputSection>(sections[osecIdx - 1]);
    *off -= osec->fileOff;

    size_t isecIdx = 0;
    for (; isecIdx < osec->inputs.size(); ++isecIdx) {
      const ConcatInputSection *isec = osec->inputs[isecIdx];
      if (*off < isec->outSecOff)
        break;
    }
    assert(isecIdx > 0);
    ConcatInputSection *isec = osec->inputs[isecIdx - 1];
    *off -= isec->outSecOff;
    return isec;
  }
  return nullptr;
}

void macho::reportRangeError(void *loc, const Reloc &r, const Twine &v,
                             uint8_t bits, int64_t min, uint64_t max) {
  std::string hint;
  uint64_t off = reinterpret_cast<const uint8_t *>(loc) - in.bufferStart;
  const InputSection *isec = offsetToInputSection(&off);
  std::string locStr = isec ? isec->getLocation(off) : "(invalid location)";
  if (auto *sym = r.referent.dyn_cast<Symbol *>())
    hint = "; references " + toString(*sym);
  error(locStr + ": relocation " + target->getRelocAttrs(r.type).name +
        " is out of range: " + v + " is not in [" + Twine(min) + ", " +
        Twine(max) + "]" + hint);
}

void macho::reportRangeError(void *loc, SymbolDiagnostic d, const Twine &v,
                             uint8_t bits, int64_t min, uint64_t max) {
  // FIXME: should we use `loc` somehow to provide a better error message?
  std::string hint;
  if (d.symbol)
    hint = "; references " + toString(*d.symbol);
  error(d.reason + " is out of range: " + v + " is not in [" + Twine(min) +
        ", " + Twine(max) + "]" + hint);
}

const RelocAttrs macho::invalidRelocAttrs{"INVALID", RelocAttrBits::_0};
