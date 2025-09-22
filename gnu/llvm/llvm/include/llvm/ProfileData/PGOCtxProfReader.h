//===--- PGOCtxProfReader.h - Contextual profile reader ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
///
/// Reader for contextual iFDO profile, which comes in bitstream format.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_PROFILEDATA_CTXINSTRPROFILEREADER_H
#define LLVM_PROFILEDATA_CTXINSTRPROFILEREADER_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/Bitstream/BitstreamReader.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/ProfileData/PGOCtxProfWriter.h"
#include "llvm/Support/Error.h"
#include <map>
#include <vector>

namespace llvm {
/// The loaded contextual profile, suitable for mutation during IPO passes. We
/// generally expect a fraction of counters and of callsites to be populated.
/// We continue to model counters as vectors, but callsites are modeled as a map
/// of a map. The expectation is that, typically, there is a small number of
/// indirect targets (usually, 1 for direct calls); but potentially a large
/// number of callsites, and, as inlining progresses, the callsite count of a
/// caller will grow.
class PGOContextualProfile final {
public:
  using CallTargetMapTy = std::map<GlobalValue::GUID, PGOContextualProfile>;
  using CallsiteMapTy = DenseMap<uint32_t, CallTargetMapTy>;

private:
  friend class PGOCtxProfileReader;
  GlobalValue::GUID GUID = 0;
  SmallVector<uint64_t, 16> Counters;
  CallsiteMapTy Callsites;

  PGOContextualProfile(GlobalValue::GUID G,
                       SmallVectorImpl<uint64_t> &&Counters)
      : GUID(G), Counters(std::move(Counters)) {}

  Expected<PGOContextualProfile &>
  getOrEmplace(uint32_t Index, GlobalValue::GUID G,
               SmallVectorImpl<uint64_t> &&Counters);

public:
  PGOContextualProfile(const PGOContextualProfile &) = delete;
  PGOContextualProfile &operator=(const PGOContextualProfile &) = delete;
  PGOContextualProfile(PGOContextualProfile &&) = default;
  PGOContextualProfile &operator=(PGOContextualProfile &&) = default;

  GlobalValue::GUID guid() const { return GUID; }
  const SmallVectorImpl<uint64_t> &counters() const { return Counters; }
  const CallsiteMapTy &callsites() const { return Callsites; }
  CallsiteMapTy &callsites() { return Callsites; }

  bool hasCallsite(uint32_t I) const {
    return Callsites.find(I) != Callsites.end();
  }

  const CallTargetMapTy &callsite(uint32_t I) const {
    assert(hasCallsite(I) && "Callsite not found");
    return Callsites.find(I)->second;
  }
  void getContainedGuids(DenseSet<GlobalValue::GUID> &Guids) const;
};

class PGOCtxProfileReader final {
  BitstreamCursor &Cursor;
  Expected<BitstreamEntry> advance();
  Error readMetadata();
  Error wrongValue(const Twine &);
  Error unsupported(const Twine &);

  Expected<std::pair<std::optional<uint32_t>, PGOContextualProfile>>
  readContext(bool ExpectIndex);
  bool canReadContext();

public:
  PGOCtxProfileReader(BitstreamCursor &Cursor) : Cursor(Cursor) {}

  Expected<std::map<GlobalValue::GUID, PGOContextualProfile>> loadContexts();
};
} // namespace llvm
#endif
