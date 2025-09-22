//===- OutputSegment.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MACHO_OUTPUT_SEGMENT_H
#define LLD_MACHO_OUTPUT_SEGMENT_H

#include "OutputSection.h"
#include "Symbols.h"
#include "lld/Common/LLVM.h"
#include "llvm/ADT/TinyPtrVector.h"

#include <limits>
#include <vector>

namespace lld::macho {

namespace segment_names {

constexpr const char dataConst[] = "__DATA_CONST";
constexpr const char dataDirty[] = "__DATA_DIRTY";
constexpr const char data[] = "__DATA";
constexpr const char dwarf[] = "__DWARF";
constexpr const char import[] = "__IMPORT";
constexpr const char ld[] = "__LD"; // output only with -r
constexpr const char linkEdit[] = "__LINKEDIT";
constexpr const char llvm[] = "__LLVM";
constexpr const char pageZero[] = "__PAGEZERO";
constexpr const char textExec[] = "__TEXT_EXEC";
constexpr const char text[] = "__TEXT";

} // namespace segment_names

class OutputSection;
class InputSection;

class OutputSegment {
public:
  void addOutputSection(OutputSection *os);
  void sortOutputSections();
  void assignAddressesToStartEndSymbols();

  const std::vector<OutputSection *> &getSections() const { return sections; }
  size_t numNonHiddenSections() const;

  uint64_t fileOff = 0;
  uint64_t fileSize = 0;
  uint64_t addr = 0;
  uint64_t vmSize = 0;
  int inputOrder = UnspecifiedInputOrder;
  StringRef name;
  uint32_t maxProt = 0;
  uint32_t initProt = 0;
  uint32_t flags = 0;
  uint8_t index;

  llvm::TinyPtrVector<Defined *> segmentStartSymbols;
  llvm::TinyPtrVector<Defined *> segmentEndSymbols;

private:
  std::vector<OutputSection *> sections;
};

extern std::vector<OutputSegment *> outputSegments;

void sortOutputSegments();
void resetOutputSegments();

OutputSegment *getOrCreateOutputSegment(StringRef name);

} // namespace lld::macho

#endif
