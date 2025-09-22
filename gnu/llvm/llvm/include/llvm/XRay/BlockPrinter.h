//===- BlockPrinter.h - FDR Block Pretty Printer -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// An implementation of the RecordVisitor which formats a block of records for
// easier human consumption.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_XRAY_BLOCKPRINTER_H
#define LLVM_XRAY_BLOCKPRINTER_H

#include "llvm/Support/raw_ostream.h"
#include "llvm/XRay/FDRRecords.h"
#include "llvm/XRay/RecordPrinter.h"

namespace llvm {
namespace xray {

class BlockPrinter : public RecordVisitor {
  enum class State {
    Start,
    Preamble,
    Metadata,
    Function,
    Arg,
    CustomEvent,
    End,
  };

  raw_ostream &OS;
  RecordPrinter &RP;
  State CurrentState = State::Start;

public:
  explicit BlockPrinter(raw_ostream &O, RecordPrinter &P) : OS(O), RP(P) {}

  Error visit(BufferExtents &) override;
  Error visit(WallclockRecord &) override;
  Error visit(NewCPUIDRecord &) override;
  Error visit(TSCWrapRecord &) override;
  Error visit(CustomEventRecord &) override;
  Error visit(CallArgRecord &) override;
  Error visit(PIDRecord &) override;
  Error visit(NewBufferRecord &) override;
  Error visit(EndBufferRecord &) override;
  Error visit(FunctionRecord &) override;
  Error visit(CustomEventRecordV5 &) override;
  Error visit(TypedEventRecord &) override;

  void reset() { CurrentState = State::Start; }
};

} // namespace xray
} // namespace llvm

#endif // LLVM_XRAY_BLOCKPRINTER_H
