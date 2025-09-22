//===- DebugFrameDataSubsection.h ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_DEBUGFRAMEDATASUBSECTION_H
#define LLVM_DEBUGINFO_CODEVIEW_DEBUGFRAMEDATASUBSECTION_H

#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/DebugSubsection.h"
#include "llvm/Support/BinaryStreamArray.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"

namespace llvm {
class BinaryStreamReader;
class BinaryStreamWriter;

namespace codeview {
class DebugFrameDataSubsectionRef final : public DebugSubsectionRef {
public:
  DebugFrameDataSubsectionRef()
      : DebugSubsectionRef(DebugSubsectionKind::FrameData) {}
  static bool classof(const DebugSubsection *S) {
    return S->kind() == DebugSubsectionKind::FrameData;
  }

  Error initialize(BinaryStreamReader Reader);
  Error initialize(BinaryStreamRef Stream);

  FixedStreamArray<FrameData>::Iterator begin() const { return Frames.begin(); }
  FixedStreamArray<FrameData>::Iterator end() const { return Frames.end(); }

  const support::ulittle32_t *getRelocPtr() const { return RelocPtr; }

private:
  const support::ulittle32_t *RelocPtr = nullptr;
  FixedStreamArray<FrameData> Frames;
};

class DebugFrameDataSubsection final : public DebugSubsection {
public:
  DebugFrameDataSubsection(bool IncludeRelocPtr)
      : DebugSubsection(DebugSubsectionKind::FrameData),
        IncludeRelocPtr(IncludeRelocPtr) {}
  static bool classof(const DebugSubsection *S) {
    return S->kind() == DebugSubsectionKind::FrameData;
  }

  uint32_t calculateSerializedSize() const override;
  Error commit(BinaryStreamWriter &Writer) const override;

  void addFrameData(const FrameData &Frame);
  void setFrames(ArrayRef<FrameData> Frames);

private:
  bool IncludeRelocPtr = false;
  std::vector<FrameData> Frames;
};
}
}

#endif
