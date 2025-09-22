//===-- xray_records.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of XRay, a dynamic runtime instrumentation system.
//
// This header exposes some record types useful for the XRay in-memory logging
// implementation.
//
//===----------------------------------------------------------------------===//

#ifndef XRAY_XRAY_RECORDS_H
#define XRAY_XRAY_RECORDS_H

#include <cstdint>

namespace __xray {

enum FileTypes {
  NAIVE_LOG = 0,
  FDR_LOG = 1,
};

// FDR mode use of the union field in the XRayFileHeader.
struct alignas(16) FdrAdditionalHeaderData {
  uint64_t ThreadBufferSize;
};

static_assert(sizeof(FdrAdditionalHeaderData) == 16,
              "FdrAdditionalHeaderData != 16 bytes");

// This data structure is used to describe the contents of the file. We use this
// for versioning the supported XRay file formats.
struct alignas(32) XRayFileHeader {
  uint16_t Version = 0;

  // The type of file we're writing out. See the FileTypes enum for more
  // information. This allows different implementations of the XRay logging to
  // have different files for different information being stored.
  uint16_t Type = 0;

  // What follows are a set of flags that indicate useful things for when
  // reading the data in the file.
  bool ConstantTSC : 1;
  bool NonstopTSC : 1;

  // The frequency by which TSC increases per-second.
  alignas(8) uint64_t CycleFrequency = 0;

  union {
    char FreeForm[16];
    // The current civiltime timestamp, as retrieved from 'clock_gettime'. This
    // allows readers of the file to determine when the file was created or
    // written down.
    struct timespec TS;

    struct FdrAdditionalHeaderData FdrData;
  };
} __attribute__((packed));

static_assert(sizeof(XRayFileHeader) == 32, "XRayFileHeader != 32 bytes");

enum RecordTypes {
  NORMAL = 0,
  ARG_PAYLOAD = 1,
};

struct alignas(32) XRayRecord {
  // This is the type of the record being written. We use 16 bits to allow us to
  // treat this as a discriminant, and so that the first 4 bytes get packed
  // properly. See RecordTypes for more supported types.
  uint16_t RecordType = RecordTypes::NORMAL;

  // The CPU where the thread is running. We assume number of CPUs <= 256.
  uint8_t CPU = 0;

  // The type of the event. One of the following:
  //   ENTER = 0
  //   EXIT = 1
  //   TAIL_EXIT = 2
  //   ENTER_ARG = 3
  uint8_t Type = 0;

  // The function ID for the record.
  int32_t FuncId = 0;

  // Get the full 8 bytes of the TSC when we get the log record.
  uint64_t TSC = 0;

  // The thread ID for the currently running thread.
  uint32_t TId = 0;

  // The ID of process that is currently running
  uint32_t PId = 0;
  
  // Use some bytes in the end of the record for buffers.
  char Buffer[8] = {};
} __attribute__((packed));

static_assert(sizeof(XRayRecord) == 32, "XRayRecord != 32 bytes");

struct alignas(32) XRayArgPayload {
  // We use the same 16 bits as a discriminant for the records in the log here
  // too, and so that the first 4 bytes are packed properly.
  uint16_t RecordType = RecordTypes::ARG_PAYLOAD;

  // Add a few bytes to pad.
  uint8_t Padding[2] = {};

  // The function ID for the record.
  int32_t FuncId = 0;

  // The thread ID for the currently running thread.
  uint32_t TId = 0;

  // The ID of process that is currently running
  uint32_t PId = 0;

  // The argument payload.
  uint64_t Arg = 0;

  // The rest of this record ought to be left as padding.
  uint8_t TailPadding[8] = {};
} __attribute__((packed));

static_assert(sizeof(XRayArgPayload) == 32, "XRayArgPayload != 32 bytes");

} // namespace __xray

#endif // XRAY_XRAY_RECORDS_H
