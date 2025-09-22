//===--- PerfSharedStructs.h --- RPC Structs for perf support ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Structs and serialization to share perf-related information
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_SHARED_PERFSHAREDSTRUCTS_H
#define LLVM_EXECUTIONENGINE_ORC_SHARED_PERFSHAREDSTRUCTS_H

#include "llvm/ExecutionEngine/Orc/Shared/SimplePackedSerialization.h"

namespace llvm {

namespace orc {

// The following are POD struct definitions from the perf jit specification

enum class PerfJITRecordType {
  JIT_CODE_LOAD = 0,
  JIT_CODE_MOVE = 1, // not emitted, code isn't moved
  JIT_CODE_DEBUG_INFO = 2,
  JIT_CODE_CLOSE = 3,          // not emitted, unnecessary
  JIT_CODE_UNWINDING_INFO = 4, // not emitted

  JIT_CODE_MAX
};

struct PerfJITRecordPrefix {
  PerfJITRecordType Id; // record type identifier, uint32_t
  uint32_t TotalSize;
};
struct PerfJITCodeLoadRecord {
  PerfJITRecordPrefix Prefix;

  uint32_t Pid;
  uint32_t Tid;
  uint64_t Vma;
  uint64_t CodeAddr;
  uint64_t CodeSize;
  uint64_t CodeIndex;
  std::string Name;
};

struct PerfJITDebugEntry {
  uint64_t Addr;
  uint32_t Lineno;  // source line number starting at 1
  uint32_t Discrim; // column discriminator, 0 is default
  std::string Name;
};

struct PerfJITDebugInfoRecord {
  PerfJITRecordPrefix Prefix;

  uint64_t CodeAddr;
  std::vector<PerfJITDebugEntry> Entries;
};

struct PerfJITCodeUnwindingInfoRecord {
  PerfJITRecordPrefix Prefix;

  uint64_t UnwindDataSize;
  uint64_t EHFrameHdrSize;
  uint64_t MappedSize;
  // Union, one will always be 0/"", the other has data
  uint64_t EHFrameHdrAddr;
  std::string EHFrameHdr;

  uint64_t EHFrameAddr;
  // size is UnwindDataSize - EHFrameHdrSize
};

// Batch vehicle for minimizing RPC calls for perf jit records
struct PerfJITRecordBatch {
  std::vector<PerfJITDebugInfoRecord> DebugInfoRecords;
  std::vector<PerfJITCodeLoadRecord> CodeLoadRecords;
  // only valid if record size > 0
  PerfJITCodeUnwindingInfoRecord UnwindingRecord;
};

// SPS traits for Records

namespace shared {

using SPSPerfJITRecordPrefix = SPSTuple<uint32_t, uint32_t>;

template <>
class SPSSerializationTraits<SPSPerfJITRecordPrefix, PerfJITRecordPrefix> {
public:
  static size_t size(const PerfJITRecordPrefix &Val) {
    return SPSPerfJITRecordPrefix::AsArgList::size(
        static_cast<uint32_t>(Val.Id), Val.TotalSize);
  }
  static bool deserialize(SPSInputBuffer &IB, PerfJITRecordPrefix &Val) {
    uint32_t Id;
    if (!SPSPerfJITRecordPrefix::AsArgList::deserialize(IB, Id, Val.TotalSize))
      return false;
    Val.Id = static_cast<PerfJITRecordType>(Id);
    return true;
  }
  static bool serialize(SPSOutputBuffer &OB, const PerfJITRecordPrefix &Val) {
    return SPSPerfJITRecordPrefix::AsArgList::serialize(
        OB, static_cast<uint32_t>(Val.Id), Val.TotalSize);
  }
};

using SPSPerfJITCodeLoadRecord =
    SPSTuple<SPSPerfJITRecordPrefix, uint32_t, uint32_t, uint64_t, uint64_t,
             uint64_t, uint64_t, SPSString>;

template <>
class SPSSerializationTraits<SPSPerfJITCodeLoadRecord, PerfJITCodeLoadRecord> {
public:
  static size_t size(const PerfJITCodeLoadRecord &Val) {
    return SPSPerfJITCodeLoadRecord::AsArgList::size(
        Val.Prefix, Val.Pid, Val.Tid, Val.Vma, Val.CodeAddr, Val.CodeSize,
        Val.CodeIndex, Val.Name);
  }

  static bool deserialize(SPSInputBuffer &IB, PerfJITCodeLoadRecord &Val) {
    return SPSPerfJITCodeLoadRecord::AsArgList::deserialize(
        IB, Val.Prefix, Val.Pid, Val.Tid, Val.Vma, Val.CodeAddr, Val.CodeSize,
        Val.CodeIndex, Val.Name);
  }

  static bool serialize(SPSOutputBuffer &OB, const PerfJITCodeLoadRecord &Val) {
    return SPSPerfJITCodeLoadRecord::AsArgList::serialize(
        OB, Val.Prefix, Val.Pid, Val.Tid, Val.Vma, Val.CodeAddr, Val.CodeSize,
        Val.CodeIndex, Val.Name);
  }
};

using SPSPerfJITDebugEntry = SPSTuple<uint64_t, uint32_t, uint32_t, SPSString>;

template <>
class SPSSerializationTraits<SPSPerfJITDebugEntry, PerfJITDebugEntry> {
public:
  static size_t size(const PerfJITDebugEntry &Val) {
    return SPSPerfJITDebugEntry::AsArgList::size(Val.Addr, Val.Lineno,
                                                 Val.Discrim, Val.Name);
  }

  static bool deserialize(SPSInputBuffer &IB, PerfJITDebugEntry &Val) {
    return SPSPerfJITDebugEntry::AsArgList::deserialize(
        IB, Val.Addr, Val.Lineno, Val.Discrim, Val.Name);
  }

  static bool serialize(SPSOutputBuffer &OB, const PerfJITDebugEntry &Val) {
    return SPSPerfJITDebugEntry::AsArgList::serialize(OB, Val.Addr, Val.Lineno,
                                                      Val.Discrim, Val.Name);
  }
};

using SPSPerfJITDebugInfoRecord = SPSTuple<SPSPerfJITRecordPrefix, uint64_t,
                                           SPSSequence<SPSPerfJITDebugEntry>>;

template <>
class SPSSerializationTraits<SPSPerfJITDebugInfoRecord,
                             PerfJITDebugInfoRecord> {
public:
  static size_t size(const PerfJITDebugInfoRecord &Val) {
    return SPSPerfJITDebugInfoRecord::AsArgList::size(Val.Prefix, Val.CodeAddr,
                                                      Val.Entries);
  }
  static bool deserialize(SPSInputBuffer &IB, PerfJITDebugInfoRecord &Val) {
    return SPSPerfJITDebugInfoRecord::AsArgList::deserialize(
        IB, Val.Prefix, Val.CodeAddr, Val.Entries);
  }
  static bool serialize(SPSOutputBuffer &OB,
                        const PerfJITDebugInfoRecord &Val) {
    return SPSPerfJITDebugInfoRecord::AsArgList::serialize(
        OB, Val.Prefix, Val.CodeAddr, Val.Entries);
  }
};

using SPSPerfJITCodeUnwindingInfoRecord =
    SPSTuple<SPSPerfJITRecordPrefix, uint64_t, uint64_t, uint64_t, uint64_t,
             SPSString, uint64_t>;
template <>
class SPSSerializationTraits<SPSPerfJITCodeUnwindingInfoRecord,
                             PerfJITCodeUnwindingInfoRecord> {
public:
  static size_t size(const PerfJITCodeUnwindingInfoRecord &Val) {
    return SPSPerfJITCodeUnwindingInfoRecord::AsArgList::size(
        Val.Prefix, Val.UnwindDataSize, Val.EHFrameHdrSize, Val.MappedSize,
        Val.EHFrameHdrAddr, Val.EHFrameHdr, Val.EHFrameAddr);
  }
  static bool deserialize(SPSInputBuffer &IB,
                          PerfJITCodeUnwindingInfoRecord &Val) {
    return SPSPerfJITCodeUnwindingInfoRecord::AsArgList::deserialize(
        IB, Val.Prefix, Val.UnwindDataSize, Val.EHFrameHdrSize, Val.MappedSize,
        Val.EHFrameHdrAddr, Val.EHFrameHdr, Val.EHFrameAddr);
  }
  static bool serialize(SPSOutputBuffer &OB,
                        const PerfJITCodeUnwindingInfoRecord &Val) {
    return SPSPerfJITCodeUnwindingInfoRecord::AsArgList::serialize(
        OB, Val.Prefix, Val.UnwindDataSize, Val.EHFrameHdrSize, Val.MappedSize,
        Val.EHFrameHdrAddr, Val.EHFrameHdr, Val.EHFrameAddr);
  }
};

using SPSPerfJITRecordBatch = SPSTuple<SPSSequence<SPSPerfJITCodeLoadRecord>,
                                       SPSSequence<SPSPerfJITDebugInfoRecord>,
                                       SPSPerfJITCodeUnwindingInfoRecord>;
template <>
class SPSSerializationTraits<SPSPerfJITRecordBatch, PerfJITRecordBatch> {
public:
  static size_t size(const PerfJITRecordBatch &Val) {
    return SPSPerfJITRecordBatch::AsArgList::size(
        Val.CodeLoadRecords, Val.DebugInfoRecords, Val.UnwindingRecord);
  }
  static bool deserialize(SPSInputBuffer &IB, PerfJITRecordBatch &Val) {
    return SPSPerfJITRecordBatch::AsArgList::deserialize(
        IB, Val.CodeLoadRecords, Val.DebugInfoRecords, Val.UnwindingRecord);
  }
  static bool serialize(SPSOutputBuffer &OB, const PerfJITRecordBatch &Val) {
    return SPSPerfJITRecordBatch::AsArgList::serialize(
        OB, Val.CodeLoadRecords, Val.DebugInfoRecords, Val.UnwindingRecord);
  }
};

} // namespace shared

} // namespace orc

} // namespace llvm

#endif