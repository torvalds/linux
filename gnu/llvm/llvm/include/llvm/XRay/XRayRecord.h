//===- XRayRecord.h - XRay Trace Record -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file replicates the record definition for XRay log entries. This should
// follow the evolution of the log record versions supported in the compiler-rt
// xray project.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_XRAY_XRAYRECORD_H
#define LLVM_XRAY_XRAYRECORD_H

#include <cstdint>
#include <vector>
#include <string>

namespace llvm {
namespace xray {

/// XRay traces all have a header providing some top-matter information useful
/// to help tools determine how to interpret the information available in the
/// trace.
struct XRayFileHeader {
  /// Version of the XRay implementation that produced this file.
  uint16_t Version = 0;

  /// A numeric identifier for the type of file this is. Best used in
  /// combination with Version.
  uint16_t Type = 0;

  /// Whether the CPU that produced the timestamp counters (TSC) move at a
  /// constant rate.
  bool ConstantTSC = false;

  /// Whether the CPU that produced the timestamp counters (TSC) do not stop.
  bool NonstopTSC = false;

  /// The number of cycles per second for the CPU that produced the timestamp
  /// counter (TSC) values. Useful for estimating the amount of time that
  /// elapsed between two TSCs on some platforms.
  uint64_t CycleFrequency = 0;

  // This is different depending on the type of xray record. The naive format
  // stores a Wallclock timespec. FDR logging stores the size of a thread
  // buffer.
  char FreeFormData[16] = {};
};

/// Determines the supported types of records that could be seen in XRay traces.
/// This may or may not correspond to actual record types in the raw trace (as
/// the loader implementation may synthesize this information in the process of
/// of loading).
enum class RecordTypes {
  ENTER,
  EXIT,
  TAIL_EXIT,
  ENTER_ARG,
  CUSTOM_EVENT,
  TYPED_EVENT
};

/// An XRayRecord is the denormalized view of data associated in a trace. These
/// records may not correspond to actual entries in the raw traces, but they are
/// the logical representation of records in a higher-level event log.
struct XRayRecord {
  /// RecordType values are used as "sub-types" which have meaning in the
  /// context of the `Type` below. For function call and custom event records,
  /// the RecordType is always 0, while for typed events we store the type in
  /// the RecordType field.
  uint16_t RecordType;

  /// The CPU where the thread is running. We assume number of CPUs <= 65536.
  uint16_t CPU;

  /// Identifies the type of record.
  RecordTypes Type;

  /// The function ID for the record, if this is a function call record.
  int32_t FuncId;

  /// Get the full 8 bytes of the TSC when we get the log record.
  uint64_t TSC;

  /// The thread ID for the currently running thread.
  uint32_t TId;

  /// The process ID for the currently running process.
  uint32_t PId;

  /// The function call arguments.
  std::vector<uint64_t> CallArgs;

  /// For custom and typed events, we provide the raw data from the trace.
  std::string Data;
};

} // namespace xray
} // namespace llvm

#endif // LLVM_XRAY_XRAYRECORD_H
