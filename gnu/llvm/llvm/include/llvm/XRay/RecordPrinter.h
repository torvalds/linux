//===- RecordPrinter.h - FDR Record Printer -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// An implementation of the RecordVisitor which prints an individual record's
// data in an adhoc format, suitable for human inspection.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_XRAY_RECORDPRINTER_H
#define LLVM_XRAY_RECORDPRINTER_H

#include "llvm/Support/raw_ostream.h"
#include "llvm/XRay/FDRRecords.h"

namespace llvm {
namespace xray {

class RecordPrinter : public RecordVisitor {
  raw_ostream &OS;
  std::string Delim;

public:
  explicit RecordPrinter(raw_ostream &O, std::string D)
      : OS(O), Delim(std::move(D)) {}

  explicit RecordPrinter(raw_ostream &O) : RecordPrinter(O, ""){};

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
};

} // namespace xray
} // namespace llvm

#endif // LLVM_XRAY_RECORDPRINTER_H
