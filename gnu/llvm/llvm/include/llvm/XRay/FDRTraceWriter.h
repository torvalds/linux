//===- FDRTraceWriter.h - XRay FDR Trace Writer -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Test a utility that can write out XRay FDR Mode formatted trace files.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_XRAY_FDRTRACEWRITER_H
#define LLVM_XRAY_FDRTRACEWRITER_H

#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/XRay/FDRRecords.h"
#include "llvm/XRay/XRayRecord.h"

namespace llvm {
namespace xray {

/// The FDRTraceWriter allows us to hand-craft an XRay Flight Data Recorder
/// (FDR) mode log file. This is used primarily for testing, generating
/// sequences of FDR records that can be read/processed. It can also be used to
/// generate various kinds of execution traces without using the XRay runtime.
/// Note that this writer does not do any validation, but uses the types of
/// records defined in the FDRRecords.h file.
class FDRTraceWriter : public RecordVisitor {
public:
  // Construct an FDRTraceWriter associated with an output stream.
  explicit FDRTraceWriter(raw_ostream &O, const XRayFileHeader &H);
  ~FDRTraceWriter();

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

private:
  support::endian::Writer OS;
};

} // namespace xray
} // namespace llvm

#endif // LLVM_XRAY_FDRTRACEWRITER_H
