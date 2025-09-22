//===-- xray_fdr_log_records.h  -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of XRay, a function call tracing system.
//
//===----------------------------------------------------------------------===//
#ifndef XRAY_XRAY_FDR_LOG_RECORDS_H
#define XRAY_XRAY_FDR_LOG_RECORDS_H
#include <cstdint>

namespace __xray {

enum class RecordType : uint8_t { Function, Metadata };

// A MetadataRecord encodes the kind of record in its first byte, and have 15
// additional bytes in the end to hold free-form data.
struct alignas(16) MetadataRecord {
  // A MetadataRecord must always have a type of 1.
  /* RecordType */ uint8_t Type : 1;

  // Each kind of record is represented as a 7-bit value (even though we use an
  // unsigned 8-bit enum class to do so).
  enum class RecordKinds : uint8_t {
    NewBuffer,
    EndOfBuffer,
    NewCPUId,
    TSCWrap,
    WalltimeMarker,
    CustomEventMarker,
    CallArgument,
    BufferExtents,
    TypedEventMarker,
    Pid,
  };

  // Use 7 bits to identify this record type.
  /* RecordKinds */ uint8_t RecordKind : 7;
  char Data[15];
} __attribute__((packed));

static_assert(sizeof(MetadataRecord) == 16, "Wrong size for MetadataRecord.");

struct alignas(8) FunctionRecord {
  // A FunctionRecord must always have a type of 0.
  /* RecordType */ uint8_t Type : 1;
  enum class RecordKinds {
    FunctionEnter = 0x00,
    FunctionExit = 0x01,
    FunctionTailExit = 0x02,
  };
  /* RecordKinds */ uint8_t RecordKind : 3;

  // We only use 28 bits of the function ID, so that we can use as few bytes as
  // possible. This means we only support 2^28 (268,435,456) unique function ids
  // in a single binary.
  int FuncId : 28;

  // We use another 4 bytes to hold the delta between the previous entry's TSC.
  // In case we've found that the distance is greater than the allowable 32 bits
  // (either because we are running in a different CPU and the TSC might be
  // different then), we should use a MetadataRecord before this FunctionRecord
  // that will contain the full TSC for that CPU, and keep this to 0.
  uint32_t TSCDelta;
} __attribute__((packed));

static_assert(sizeof(FunctionRecord) == 8, "Wrong size for FunctionRecord.");

} // namespace __xray

#endif // XRAY_XRAY_FDR_LOG_RECORDS_H
