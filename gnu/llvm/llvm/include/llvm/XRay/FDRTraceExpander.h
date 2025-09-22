//===- FDRTraceExpander.h - XRay FDR Mode Log Expander --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// We define an FDR record visitor which can re-constitute XRayRecord instances
// from a sequence of FDR mode records in arrival order into a collection.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_XRAY_FDRTRACEEXPANDER_H
#define LLVM_XRAY_FDRTRACEEXPANDER_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/XRay/FDRRecords.h"
#include "llvm/XRay/XRayRecord.h"

namespace llvm {
namespace xray {

class TraceExpander : public RecordVisitor {
  // Type-erased callback for handling individual XRayRecord instances.
  function_ref<void(const XRayRecord &)> C;
  int32_t PID = 0;
  int32_t TID = 0;
  uint64_t BaseTSC = 0;
  XRayRecord CurrentRecord{0, 0, RecordTypes::ENTER, 0, 0, 0, 0, {}, {}};
  uint16_t CPUId = 0;
  uint16_t LogVersion = 0;
  bool BuildingRecord = false;
  bool IgnoringRecords = false;

  void resetCurrentRecord();

public:
  explicit TraceExpander(function_ref<void(const XRayRecord &)> F, uint16_t L)
      : C(std::move(F)), LogVersion(L) {}

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

  // Must be called after all the records have been processed, to handle the
  // most recent record generated.
  Error flush();
};

} // namespace xray
} // namespace llvm

#endif // LLVM_XRAY_FDRTRACEEXPANDER_H
