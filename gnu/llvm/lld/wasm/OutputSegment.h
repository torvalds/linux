//===- OutputSegment.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_WASM_OUTPUT_SEGMENT_H
#define LLD_WASM_OUTPUT_SEGMENT_H

#include "InputChunks.h"
#include "lld/Common/ErrorHandler.h"
#include "llvm/Object/Wasm.h"

namespace lld::wasm {

class InputSegment;

class OutputSegment {
public:
  OutputSegment(StringRef n) : name(n) {}

  void addInputSegment(InputChunk *inSeg);
  void finalizeInputSegments();
  // In most circumstances BSS segments don't need to be written
  // to the output binary.  However if the memory is imported, and
  // we can't use memory.fill during startup (due to lack of bulk
  // memory feature) then we include BSS segments verbatim.
  bool requiredInBinary() const { return !isBss || ctx.emitBssSegments; }

  bool isTLS() const { return name == ".tdata"; }

  StringRef name;
  bool isBss = false;
  uint32_t index = 0;
  uint32_t linkingFlags = 0;
  uint32_t initFlags = 0;
  uint32_t sectionOffset = 0;
  uint32_t alignment = 0;
  uint64_t startVA = 0;
  std::vector<InputChunk *> inputSegments;

  // Sum of the size of the all the input segments
  uint32_t size = 0;

  // Segment header
  std::string header;
};

} // namespace lld::wasm

#endif // LLD_WASM_OUTPUT_SEGMENT_H
