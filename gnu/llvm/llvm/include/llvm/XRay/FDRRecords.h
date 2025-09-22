//===- FDRRecords.h - XRay Flight Data Recorder Mode Records --------------===//
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
#ifndef LLVM_XRAY_FDRRECORDS_H
#define LLVM_XRAY_FDRRECORDS_H

#include <cstdint>
#include <string>

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/Support/Error.h"
#include "llvm/XRay/XRayRecord.h"

namespace llvm {
namespace xray {

class RecordVisitor;
class RecordInitializer;

class Record {
public:
  enum class RecordKind {
    RK_Metadata,
    RK_Metadata_BufferExtents,
    RK_Metadata_WallClockTime,
    RK_Metadata_NewCPUId,
    RK_Metadata_TSCWrap,
    RK_Metadata_CustomEvent,
    RK_Metadata_CustomEventV5,
    RK_Metadata_CallArg,
    RK_Metadata_PIDEntry,
    RK_Metadata_NewBuffer,
    RK_Metadata_EndOfBuffer,
    RK_Metadata_TypedEvent,
    RK_Metadata_LastMetadata,
    RK_Function,
  };

  static StringRef kindToString(RecordKind K);

private:
  const RecordKind T;

public:
  Record(const Record &) = delete;
  Record(Record &&) = delete;
  Record &operator=(const Record &) = delete;
  Record &operator=(Record &&) = delete;
  explicit Record(RecordKind T) : T(T) {}

  RecordKind getRecordType() const { return T; }

  // Each Record should be able to apply an abstract visitor, and choose the
  // appropriate function in the visitor to invoke, given its own type.
  virtual Error apply(RecordVisitor &V) = 0;

  virtual ~Record() = default;
};

class MetadataRecord : public Record {
public:
  enum class MetadataType : unsigned {
    Unknown,
    BufferExtents,
    WallClockTime,
    NewCPUId,
    TSCWrap,
    CustomEvent,
    CallArg,
    PIDEntry,
    NewBuffer,
    EndOfBuffer,
    TypedEvent,
  };

protected:
  static constexpr int kMetadataBodySize = 15;
  friend class RecordInitializer;

private:
  const MetadataType MT;

public:
  explicit MetadataRecord(RecordKind T, MetadataType M) : Record(T), MT(M) {}

  static bool classof(const Record *R) {
    return R->getRecordType() >= RecordKind::RK_Metadata &&
           R->getRecordType() <= RecordKind::RK_Metadata_LastMetadata;
  }

  MetadataType metadataType() const { return MT; }

  virtual ~MetadataRecord() = default;
};

// What follows are specific Metadata record types which encapsulate the
// information associated with specific metadata record types in an FDR mode
// log.
class BufferExtents : public MetadataRecord {
  uint64_t Size = 0;
  friend class RecordInitializer;

public:
  BufferExtents()
      : MetadataRecord(RecordKind::RK_Metadata_BufferExtents,
                       MetadataType::BufferExtents) {}

  explicit BufferExtents(uint64_t S)
      : MetadataRecord(RecordKind::RK_Metadata_BufferExtents,
                       MetadataType::BufferExtents),
        Size(S) {}

  uint64_t size() const { return Size; }

  Error apply(RecordVisitor &V) override;

  static bool classof(const Record *R) {
    return R->getRecordType() == RecordKind::RK_Metadata_BufferExtents;
  }
};

class WallclockRecord : public MetadataRecord {
  uint64_t Seconds = 0;
  uint32_t Nanos = 0;
  friend class RecordInitializer;

public:
  WallclockRecord()
      : MetadataRecord(RecordKind::RK_Metadata_WallClockTime,
                       MetadataType::WallClockTime) {}

  explicit WallclockRecord(uint64_t S, uint32_t N)
      : MetadataRecord(RecordKind::RK_Metadata_WallClockTime,
                       MetadataType::WallClockTime),
        Seconds(S), Nanos(N) {}

  uint64_t seconds() const { return Seconds; }
  uint32_t nanos() const { return Nanos; }

  Error apply(RecordVisitor &V) override;

  static bool classof(const Record *R) {
    return R->getRecordType() == RecordKind::RK_Metadata_WallClockTime;
  }
};

class NewCPUIDRecord : public MetadataRecord {
  uint16_t CPUId = 0;
  uint64_t TSC = 0;
  friend class RecordInitializer;

public:
  NewCPUIDRecord()
      : MetadataRecord(RecordKind::RK_Metadata_NewCPUId,
                       MetadataType::NewCPUId) {}

  NewCPUIDRecord(uint16_t C, uint64_t T)
      : MetadataRecord(RecordKind::RK_Metadata_NewCPUId,
                       MetadataType::NewCPUId),
        CPUId(C), TSC(T) {}

  uint16_t cpuid() const { return CPUId; }

  uint64_t tsc() const { return TSC; }

  Error apply(RecordVisitor &V) override;

  static bool classof(const Record *R) {
    return R->getRecordType() == RecordKind::RK_Metadata_NewCPUId;
  }
};

class TSCWrapRecord : public MetadataRecord {
  uint64_t BaseTSC = 0;
  friend class RecordInitializer;

public:
  TSCWrapRecord()
      : MetadataRecord(RecordKind::RK_Metadata_TSCWrap, MetadataType::TSCWrap) {
  }

  explicit TSCWrapRecord(uint64_t B)
      : MetadataRecord(RecordKind::RK_Metadata_TSCWrap, MetadataType::TSCWrap),
        BaseTSC(B) {}

  uint64_t tsc() const { return BaseTSC; }

  Error apply(RecordVisitor &V) override;

  static bool classof(const Record *R) {
    return R->getRecordType() == RecordKind::RK_Metadata_TSCWrap;
  }
};

class CustomEventRecord : public MetadataRecord {
  int32_t Size = 0;
  uint64_t TSC = 0;
  uint16_t CPU = 0;
  std::string Data{};
  friend class RecordInitializer;

public:
  CustomEventRecord()
      : MetadataRecord(RecordKind::RK_Metadata_CustomEvent,
                       MetadataType::CustomEvent) {}

  explicit CustomEventRecord(uint64_t S, uint64_t T, uint16_t C, std::string D)
      : MetadataRecord(RecordKind::RK_Metadata_CustomEvent,
                       MetadataType::CustomEvent),
        Size(S), TSC(T), CPU(C), Data(std::move(D)) {}

  int32_t size() const { return Size; }
  uint64_t tsc() const { return TSC; }
  uint16_t cpu() const { return CPU; }
  StringRef data() const { return Data; }

  Error apply(RecordVisitor &V) override;

  static bool classof(const Record *R) {
    return R->getRecordType() == RecordKind::RK_Metadata_CustomEvent;
  }
};

class CustomEventRecordV5 : public MetadataRecord {
  int32_t Size = 0;
  int32_t Delta = 0;
  std::string Data{};
  friend class RecordInitializer;

public:
  CustomEventRecordV5()
      : MetadataRecord(RecordKind::RK_Metadata_CustomEventV5,
                       MetadataType::CustomEvent) {}

  explicit CustomEventRecordV5(int32_t S, int32_t D, std::string P)
      : MetadataRecord(RecordKind::RK_Metadata_CustomEventV5,
                       MetadataType::CustomEvent),
        Size(S), Delta(D), Data(std::move(P)) {}

  int32_t size() const { return Size; }
  int32_t delta() const { return Delta; }
  StringRef data() const { return Data; }

  Error apply(RecordVisitor &V) override;

  static bool classof(const Record *R) {
    return R->getRecordType() == RecordKind::RK_Metadata_CustomEventV5;
  }
};

class TypedEventRecord : public MetadataRecord {
  int32_t Size = 0;
  int32_t Delta = 0;
  uint16_t EventType = 0;
  std::string Data{};
  friend class RecordInitializer;

public:
  TypedEventRecord()
      : MetadataRecord(RecordKind::RK_Metadata_TypedEvent,
                       MetadataType::TypedEvent) {}

  explicit TypedEventRecord(int32_t S, int32_t D, uint16_t E, std::string P)
      : MetadataRecord(RecordKind::RK_Metadata_TypedEvent,
                       MetadataType::TypedEvent),
        Size(S), Delta(D), Data(std::move(P)) {}

  int32_t size() const { return Size; }
  int32_t delta() const { return Delta; }
  uint16_t eventType() const { return EventType; }
  StringRef data() const { return Data; }

  Error apply(RecordVisitor &V) override;

  static bool classof(const Record *R) {
    return R->getRecordType() == RecordKind::RK_Metadata_TypedEvent;
  }
};

class CallArgRecord : public MetadataRecord {
  uint64_t Arg = 0;
  friend class RecordInitializer;

public:
  CallArgRecord()
      : MetadataRecord(RecordKind::RK_Metadata_CallArg, MetadataType::CallArg) {
  }

  explicit CallArgRecord(uint64_t A)
      : MetadataRecord(RecordKind::RK_Metadata_CallArg, MetadataType::CallArg),
        Arg(A) {}

  uint64_t arg() const { return Arg; }

  Error apply(RecordVisitor &V) override;

  static bool classof(const Record *R) {
    return R->getRecordType() == RecordKind::RK_Metadata_CallArg;
  }
};

class PIDRecord : public MetadataRecord {
  int32_t PID = 0;
  friend class RecordInitializer;

public:
  PIDRecord()
      : MetadataRecord(RecordKind::RK_Metadata_PIDEntry,
                       MetadataType::PIDEntry) {}

  explicit PIDRecord(int32_t P)
      : MetadataRecord(RecordKind::RK_Metadata_PIDEntry,
                       MetadataType::PIDEntry),
        PID(P) {}

  int32_t pid() const { return PID; }

  Error apply(RecordVisitor &V) override;

  static bool classof(const Record *R) {
    return R->getRecordType() == RecordKind::RK_Metadata_PIDEntry;
  }
};

class NewBufferRecord : public MetadataRecord {
  int32_t TID = 0;
  friend class RecordInitializer;

public:
  NewBufferRecord()
      : MetadataRecord(RecordKind::RK_Metadata_NewBuffer,
                       MetadataType::NewBuffer) {}

  explicit NewBufferRecord(int32_t T)
      : MetadataRecord(RecordKind::RK_Metadata_NewBuffer,
                       MetadataType::NewBuffer),
        TID(T) {}

  int32_t tid() const { return TID; }

  Error apply(RecordVisitor &V) override;

  static bool classof(const Record *R) {
    return R->getRecordType() == RecordKind::RK_Metadata_NewBuffer;
  }
};

class EndBufferRecord : public MetadataRecord {
public:
  EndBufferRecord()
      : MetadataRecord(RecordKind::RK_Metadata_EndOfBuffer,
                       MetadataType::EndOfBuffer) {}

  Error apply(RecordVisitor &V) override;

  static bool classof(const Record *R) {
    return R->getRecordType() == RecordKind::RK_Metadata_EndOfBuffer;
  }
};

class FunctionRecord : public Record {
  RecordTypes Kind;
  int32_t FuncId = 0;
  uint32_t Delta = 0;
  friend class RecordInitializer;

  static constexpr unsigned kFunctionRecordSize = 8;

public:
  FunctionRecord() : Record(RecordKind::RK_Function) {}

  explicit FunctionRecord(RecordTypes K, int32_t F, uint32_t D)
      : Record(RecordKind::RK_Function), Kind(K), FuncId(F), Delta(D) {}

  // A function record is a concrete record type which has a number of common
  // properties.
  RecordTypes recordType() const { return Kind; }
  int32_t functionId() const { return FuncId; }
  uint32_t delta() const { return Delta; }

  Error apply(RecordVisitor &V) override;

  static bool classof(const Record *R) {
    return R->getRecordType() == RecordKind::RK_Function;
  }
};

class RecordVisitor {
public:
  virtual ~RecordVisitor() = default;

  // Support all specific kinds of records:
  virtual Error visit(BufferExtents &) = 0;
  virtual Error visit(WallclockRecord &) = 0;
  virtual Error visit(NewCPUIDRecord &) = 0;
  virtual Error visit(TSCWrapRecord &) = 0;
  virtual Error visit(CustomEventRecord &) = 0;
  virtual Error visit(CallArgRecord &) = 0;
  virtual Error visit(PIDRecord &) = 0;
  virtual Error visit(NewBufferRecord &) = 0;
  virtual Error visit(EndBufferRecord &) = 0;
  virtual Error visit(FunctionRecord &) = 0;
  virtual Error visit(CustomEventRecordV5 &) = 0;
  virtual Error visit(TypedEventRecord &) = 0;
};

class RecordInitializer : public RecordVisitor {
  DataExtractor &E;
  uint64_t &OffsetPtr;
  uint16_t Version;

public:
  static constexpr uint16_t DefaultVersion = 5u;

  explicit RecordInitializer(DataExtractor &DE, uint64_t &OP, uint16_t V)
      : E(DE), OffsetPtr(OP), Version(V) {}

  explicit RecordInitializer(DataExtractor &DE, uint64_t &OP)
      : RecordInitializer(DE, OP, DefaultVersion) {}

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

#endif // LLVM_XRAY_FDRRECORDS_H
