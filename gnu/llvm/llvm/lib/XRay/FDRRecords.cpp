//===- FDRRecords.cpp -  XRay Flight Data Recorder Mode Records -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Define types and operations on these types that represent the different kinds
// of records we encounter in XRay flight data recorder mode traces.
//
//===----------------------------------------------------------------------===//
#include "llvm/XRay/FDRRecords.h"

namespace llvm {
namespace xray {

Error BufferExtents::apply(RecordVisitor &V) { return V.visit(*this); }
Error WallclockRecord::apply(RecordVisitor &V) { return V.visit(*this); }
Error NewCPUIDRecord::apply(RecordVisitor &V) { return V.visit(*this); }
Error TSCWrapRecord::apply(RecordVisitor &V) { return V.visit(*this); }
Error CustomEventRecord::apply(RecordVisitor &V) { return V.visit(*this); }
Error CallArgRecord::apply(RecordVisitor &V) { return V.visit(*this); }
Error PIDRecord::apply(RecordVisitor &V) { return V.visit(*this); }
Error NewBufferRecord::apply(RecordVisitor &V) { return V.visit(*this); }
Error EndBufferRecord::apply(RecordVisitor &V) { return V.visit(*this); }
Error FunctionRecord::apply(RecordVisitor &V) { return V.visit(*this); }
Error CustomEventRecordV5::apply(RecordVisitor &V) { return V.visit(*this); }
Error TypedEventRecord::apply(RecordVisitor &V) { return V.visit(*this); }

StringRef Record::kindToString(RecordKind K) {
  switch (K) {
  case RecordKind::RK_Metadata:
    return "Metadata";
  case RecordKind::RK_Metadata_BufferExtents:
    return "Metadata:BufferExtents";
  case RecordKind::RK_Metadata_WallClockTime:
    return "Metadata:WallClockTime";
  case RecordKind::RK_Metadata_NewCPUId:
    return "Metadata:NewCPUId";
  case RecordKind::RK_Metadata_TSCWrap:
    return "Metadata:TSCWrap";
  case RecordKind::RK_Metadata_CustomEvent:
    return "Metadata:CustomEvent";
  case RecordKind::RK_Metadata_CustomEventV5:
    return "Metadata:CustomEventV5";
  case RecordKind::RK_Metadata_CallArg:
    return "Metadata:CallArg";
  case RecordKind::RK_Metadata_PIDEntry:
    return "Metadata:PIDEntry";
  case RecordKind::RK_Metadata_NewBuffer:
    return "Metadata:NewBuffer";
  case RecordKind::RK_Metadata_EndOfBuffer:
    return "Metadata:EndOfBuffer";
  case RecordKind::RK_Metadata_TypedEvent:
    return "Metadata:TypedEvent";
  case RecordKind::RK_Metadata_LastMetadata:
    return "Metadata:LastMetadata";
  case RecordKind::RK_Function:
    return "Function";
  }
  return "Unknown";
}

} // namespace xray
} // namespace llvm
