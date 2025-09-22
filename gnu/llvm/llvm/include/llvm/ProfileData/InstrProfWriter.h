//===- InstrProfWriter.h - Instrumented profiling writer --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains support for writing profiling data for instrumentation
// based PGO and coverage.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_PROFILEDATA_INSTRPROFWRITER_H
#define LLVM_PROFILEDATA_INSTRPROFWRITER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/Object/BuildID.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/ProfileData/MemProf.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <memory>
#include <random>

namespace llvm {

/// Writer for instrumentation based profile data.
class InstrProfRecordWriterTrait;
class ProfOStream;
class MemoryBuffer;
class raw_fd_ostream;

class InstrProfWriter {
public:
  using ProfilingData = SmallDenseMap<uint64_t, InstrProfRecord>;

private:
  bool Sparse;
  StringMap<ProfilingData> FunctionData;
  /// The maximum length of a single temporal profile trace.
  uint64_t MaxTemporalProfTraceLength;
  /// The maximum number of stored temporal profile traces.
  uint64_t TemporalProfTraceReservoirSize;
  /// The total number of temporal profile traces seen.
  uint64_t TemporalProfTraceStreamSize = 0;
  /// The list of temporal profile traces.
  SmallVector<TemporalProfTraceTy> TemporalProfTraces;
  std::mt19937 RNG;

  // The MemProf data.
  memprof::IndexedMemProfData MemProfData;

  // List of binary ids.
  std::vector<llvm::object::BuildID> BinaryIds;

  // Read the vtable names from raw instr profile reader.
  StringSet<> VTableNames;

  // An enum describing the attributes of the profile.
  InstrProfKind ProfileKind = InstrProfKind::Unknown;
  // Use raw pointer here for the incomplete type object.
  InstrProfRecordWriterTrait *InfoObj;

  // Temporary support for writing the previous version of the format, to enable
  // some forward compatibility. Currently this suppresses the writing of the
  // new vtable names section and header fields.
  // TODO: Consider enabling this with future version changes as well, to ease
  // deployment of newer versions of llvm-profdata.
  bool WritePrevVersion = false;

  // The MemProf version we should write.
  memprof::IndexedVersion MemProfVersionRequested;

  // Whether to serialize the full schema.
  bool MemProfFullSchema;

public:
  InstrProfWriter(
      bool Sparse = false, uint64_t TemporalProfTraceReservoirSize = 0,
      uint64_t MaxTemporalProfTraceLength = 0, bool WritePrevVersion = false,
      memprof::IndexedVersion MemProfVersionRequested = memprof::Version0,
      bool MemProfFullSchema = false);
  ~InstrProfWriter();

  StringMap<ProfilingData> &getProfileData() { return FunctionData; }

  /// Add function counts for the given function. If there are already counts
  /// for this function and the hash and number of counts match, each counter is
  /// summed. Optionally scale counts by \p Weight.
  void addRecord(NamedInstrProfRecord &&I, uint64_t Weight,
                 function_ref<void(Error)> Warn);
  void addRecord(NamedInstrProfRecord &&I, function_ref<void(Error)> Warn) {
    addRecord(std::move(I), 1, Warn);
  }
  void addVTableName(StringRef VTableName) { VTableNames.insert(VTableName); }

  /// Add \p SrcTraces using reservoir sampling where \p SrcStreamSize is the
  /// total number of temporal profiling traces the source has seen.
  void addTemporalProfileTraces(SmallVectorImpl<TemporalProfTraceTy> &SrcTraces,
                                uint64_t SrcStreamSize);

  /// Add a memprof record for a function identified by its \p Id.
  void addMemProfRecord(const GlobalValue::GUID Id,
                        const memprof::IndexedMemProfRecord &Record);

  /// Add a memprof frame identified by the hash of the contents of the frame in
  /// \p FrameId.
  bool addMemProfFrame(const memprof::FrameId, const memprof::Frame &F,
                       function_ref<void(Error)> Warn);

  /// Add a call stack identified by the hash of the contents of the call stack
  /// in \p CallStack.
  bool addMemProfCallStack(const memprof::CallStackId CSId,
                           const llvm::SmallVector<memprof::FrameId> &CallStack,
                           function_ref<void(Error)> Warn);

  // Add a binary id to the binary ids list.
  void addBinaryIds(ArrayRef<llvm::object::BuildID> BIs);

  /// Merge existing function counts from the given writer.
  void mergeRecordsFromWriter(InstrProfWriter &&IPW,
                              function_ref<void(Error)> Warn);

  /// Write the profile to \c OS
  Error write(raw_fd_ostream &OS);

  /// Write the profile to a string output stream \c OS
  Error write(raw_string_ostream &OS);

  /// Write the profile in text format to \c OS
  Error writeText(raw_fd_ostream &OS);

  /// Write temporal profile trace data to the header in text format to \c OS
  void writeTextTemporalProfTraceData(raw_fd_ostream &OS,
                                      InstrProfSymtab &Symtab);

  Error validateRecord(const InstrProfRecord &Func);

  /// Write \c Record in text format to \c OS
  static void writeRecordInText(StringRef Name, uint64_t Hash,
                                const InstrProfRecord &Counters,
                                InstrProfSymtab &Symtab, raw_fd_ostream &OS);

  /// Write the profile, returning the raw data. For testing.
  std::unique_ptr<MemoryBuffer> writeBuffer();

  /// Update the attributes of the current profile from the attributes
  /// specified. An error is returned if IR and FE profiles are mixed.
  Error mergeProfileKind(const InstrProfKind Other) {
    // If the kind is unset, this is the first profile we are merging so just
    // set it to the given type.
    if (ProfileKind == InstrProfKind::Unknown) {
      ProfileKind = Other;
      return Error::success();
    }

    // Returns true if merging is should fail assuming A and B are incompatible.
    auto testIncompatible = [&](InstrProfKind A, InstrProfKind B) {
      return (static_cast<bool>(ProfileKind & A) &&
              static_cast<bool>(Other & B)) ||
             (static_cast<bool>(ProfileKind & B) &&
              static_cast<bool>(Other & A));
    };

    // Check if the profiles are in-compatible. Clang frontend profiles can't be
    // merged with other profile types.
    if (static_cast<bool>(
            (ProfileKind & InstrProfKind::FrontendInstrumentation) ^
            (Other & InstrProfKind::FrontendInstrumentation))) {
      return make_error<InstrProfError>(instrprof_error::unsupported_version);
    }
    if (testIncompatible(InstrProfKind::FunctionEntryOnly,
                         InstrProfKind::FunctionEntryInstrumentation)) {
      return make_error<InstrProfError>(
          instrprof_error::unsupported_version,
          "cannot merge FunctionEntryOnly profiles and BB profiles together");
    }

    // Now we update the profile type with the bits that are set.
    ProfileKind |= Other;
    return Error::success();
  }

  InstrProfKind getProfileKind() const { return ProfileKind; }

  bool hasSingleByteCoverage() const {
    return static_cast<bool>(ProfileKind & InstrProfKind::SingleByteCoverage);
  }

  // Internal interfaces for testing purpose only.
  void setValueProfDataEndianness(llvm::endianness Endianness);
  void setOutputSparse(bool Sparse);
  void setMemProfVersionRequested(memprof::IndexedVersion Version) {
    MemProfVersionRequested = Version;
  }
  void setMemProfFullSchema(bool Full) { MemProfFullSchema = Full; }
  // Compute the overlap b/w this object and Other. Program level result is
  // stored in Overlap and function level result is stored in FuncLevelOverlap.
  void overlapRecord(NamedInstrProfRecord &&Other, OverlapStats &Overlap,
                     OverlapStats &FuncLevelOverlap,
                     const OverlapFuncFilters &FuncFilter);

private:
  void addRecord(StringRef Name, uint64_t Hash, InstrProfRecord &&I,
                 uint64_t Weight, function_ref<void(Error)> Warn);
  bool shouldEncodeData(const ProfilingData &PD);
  /// Add \p Trace using reservoir sampling.
  void addTemporalProfileTrace(TemporalProfTraceTy Trace);

  Error writeImpl(ProfOStream &OS);

  // Writes known header fields and reserves space for fields whose value are
  // known only after payloads are written. Returns the start byte offset for
  // back patching.
  uint64_t writeHeader(const IndexedInstrProf::Header &header,
                       const bool WritePrevVersion, ProfOStream &OS);

  // Writes compressed vtable names to profiles.
  Error writeVTableNames(ProfOStream &OS);
};

} // end namespace llvm

#endif // LLVM_PROFILEDATA_INSTRPROFWRITER_H
