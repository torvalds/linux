//===- PGOCtxProfWriter.h - Contextual Profile Writer -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares a utility for writing a contextual profile to bitstream.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_PROFILEDATA_PGOCTXPROFWRITER_H_
#define LLVM_PROFILEDATA_PGOCTXPROFWRITER_H_

#include "llvm/Bitstream/BitCodeEnums.h"
#include "llvm/Bitstream/BitstreamWriter.h"
#include "llvm/ProfileData/CtxInstrContextNode.h"

namespace llvm {
enum PGOCtxProfileRecords { Invalid = 0, Version, Guid, CalleeIndex, Counters };

enum PGOCtxProfileBlockIDs {
  ProfileMetadataBlockID = bitc::FIRST_APPLICATION_BLOCKID,
  ContextNodeBlockID = ProfileMetadataBlockID + 1
};

/// Write one or more ContextNodes to the provided raw_fd_stream.
/// The caller must destroy the PGOCtxProfileWriter object before closing the
/// stream.
/// The design allows serializing a bunch of contexts embedded in some other
/// file. The overall format is:
///
///  [... other data written to the stream...]
///  SubBlock(ProfileMetadataBlockID)
///   Version
///   SubBlock(ContextNodeBlockID)
///     [RECORDS]
///     SubBlock(ContextNodeBlockID)
///       [RECORDS]
///       [... more SubBlocks]
///     EndBlock
///   EndBlock
///
/// The "RECORDS" are bitsream records. The IDs are in CtxProfileCodes (except)
/// for Version, which is just for metadata). All contexts will have Guid and
/// Counters, and all but the roots have CalleeIndex. The order in which the
/// records appear does not matter, but they must precede any subcontexts,
/// because that helps keep the reader code simpler.
///
/// Subblock containment captures the context->subcontext relationship. The
/// "next()" relationship in the raw profile, between call targets of indirect
/// calls, are just modeled as peer subblocks where the callee index is the
/// same.
///
/// Versioning: the writer may produce additional records not known by the
/// reader. The version number indicates a more structural change.
/// The current version, in particular, is set up to expect optional extensions
/// like value profiling - which would appear as additional records. For
/// example, value profiling would produce a new record with a new record ID,
/// containing the profiled values (much like the counters)
class PGOCtxProfileWriter final {
  BitstreamWriter Writer;

  void writeCounters(const ctx_profile::ContextNode &Node);
  void writeImpl(std::optional<uint32_t> CallerIndex,
                 const ctx_profile::ContextNode &Node);

public:
  PGOCtxProfileWriter(raw_ostream &Out,
                      std::optional<unsigned> VersionOverride = std::nullopt)
      : Writer(Out, 0) {
    Writer.EnterSubblock(PGOCtxProfileBlockIDs::ProfileMetadataBlockID,
                         CodeLen);
    const auto Version = VersionOverride ? *VersionOverride : CurrentVersion;
    Writer.EmitRecord(PGOCtxProfileRecords::Version,
                      SmallVector<unsigned, 1>({Version}));
  }

  ~PGOCtxProfileWriter() { Writer.ExitBlock(); }

  void write(const ctx_profile::ContextNode &);

  // constants used in writing which a reader may find useful.
  static constexpr unsigned CodeLen = 2;
  static constexpr uint32_t CurrentVersion = 1;
  static constexpr unsigned VBREncodingBits = 6;
};

} // namespace llvm
#endif
