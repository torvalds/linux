//===- PGOCtxProfReader.cpp - Contextual Instrumentation profile reader ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Read a contextual profile into a datastructure suitable for maintenance
// throughout IPO
//
//===----------------------------------------------------------------------===//

#include "llvm/ProfileData/PGOCtxProfReader.h"
#include "llvm/Bitstream/BitCodeEnums.h"
#include "llvm/Bitstream/BitstreamReader.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/ProfileData/PGOCtxProfWriter.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"

using namespace llvm;

// FIXME(#92054) - these Error handling macros are (re-)invented in a few
// places.
#define EXPECT_OR_RET(LHS, RHS)                                                \
  auto LHS = RHS;                                                              \
  if (!LHS)                                                                    \
    return LHS.takeError();

#define RET_ON_ERR(EXPR)                                                       \
  if (auto Err = (EXPR))                                                       \
    return Err;

Expected<PGOContextualProfile &>
PGOContextualProfile::getOrEmplace(uint32_t Index, GlobalValue::GUID G,
                                   SmallVectorImpl<uint64_t> &&Counters) {
  auto [Iter, Inserted] = Callsites[Index].insert(
      {G, PGOContextualProfile(G, std::move(Counters))});
  if (!Inserted)
    return make_error<InstrProfError>(instrprof_error::invalid_prof,
                                      "Duplicate GUID for same callsite.");
  return Iter->second;
}

void PGOContextualProfile::getContainedGuids(
    DenseSet<GlobalValue::GUID> &Guids) const {
  Guids.insert(GUID);
  for (const auto &[_, Callsite] : Callsites)
    for (const auto &[_, Callee] : Callsite)
      Callee.getContainedGuids(Guids);
}

Expected<BitstreamEntry> PGOCtxProfileReader::advance() {
  return Cursor.advance(BitstreamCursor::AF_DontAutoprocessAbbrevs);
}

Error PGOCtxProfileReader::wrongValue(const Twine &Msg) {
  return make_error<InstrProfError>(instrprof_error::invalid_prof, Msg);
}

Error PGOCtxProfileReader::unsupported(const Twine &Msg) {
  return make_error<InstrProfError>(instrprof_error::unsupported_version, Msg);
}

bool PGOCtxProfileReader::canReadContext() {
  auto Blk = advance();
  if (!Blk) {
    consumeError(Blk.takeError());
    return false;
  }
  return Blk->Kind == BitstreamEntry::SubBlock &&
         Blk->ID == PGOCtxProfileBlockIDs::ContextNodeBlockID;
}

Expected<std::pair<std::optional<uint32_t>, PGOContextualProfile>>
PGOCtxProfileReader::readContext(bool ExpectIndex) {
  RET_ON_ERR(Cursor.EnterSubBlock(PGOCtxProfileBlockIDs::ContextNodeBlockID));

  std::optional<ctx_profile::GUID> Guid;
  std::optional<SmallVector<uint64_t, 16>> Counters;
  std::optional<uint32_t> CallsiteIndex;

  SmallVector<uint64_t, 1> RecordValues;

  // We don't prescribe the order in which the records come in, and we are ok
  // if other unsupported records appear. We seek in the current subblock until
  // we get all we know.
  auto GotAllWeNeed = [&]() {
    return Guid.has_value() && Counters.has_value() &&
           (!ExpectIndex || CallsiteIndex.has_value());
  };
  while (!GotAllWeNeed()) {
    RecordValues.clear();
    EXPECT_OR_RET(Entry, advance());
    if (Entry->Kind != BitstreamEntry::Record)
      return wrongValue(
          "Expected records before encountering more subcontexts");
    EXPECT_OR_RET(ReadRecord,
                  Cursor.readRecord(bitc::UNABBREV_RECORD, RecordValues));
    switch (*ReadRecord) {
    case PGOCtxProfileRecords::Guid:
      if (RecordValues.size() != 1)
        return wrongValue("The GUID record should have exactly one value");
      Guid = RecordValues[0];
      break;
    case PGOCtxProfileRecords::Counters:
      Counters = std::move(RecordValues);
      if (Counters->empty())
        return wrongValue("Empty counters. At least the entry counter (one "
                          "value) was expected");
      break;
    case PGOCtxProfileRecords::CalleeIndex:
      if (!ExpectIndex)
        return wrongValue("The root context should not have a callee index");
      if (RecordValues.size() != 1)
        return wrongValue("The callee index should have exactly one value");
      CallsiteIndex = RecordValues[0];
      break;
    default:
      // OK if we see records we do not understand, like records (profile
      // components) introduced later.
      break;
    }
  }

  PGOContextualProfile Ret(*Guid, std::move(*Counters));

  while (canReadContext()) {
    EXPECT_OR_RET(SC, readContext(true));
    auto &Targets = Ret.callsites()[*SC->first];
    auto [_, Inserted] =
        Targets.insert({SC->second.guid(), std::move(SC->second)});
    if (!Inserted)
      return wrongValue(
          "Unexpected duplicate target (callee) at the same callsite.");
  }
  return std::make_pair(CallsiteIndex, std::move(Ret));
}

Error PGOCtxProfileReader::readMetadata() {
  EXPECT_OR_RET(Blk, advance());
  if (Blk->Kind != BitstreamEntry::SubBlock)
    return unsupported("Expected Version record");
  RET_ON_ERR(
      Cursor.EnterSubBlock(PGOCtxProfileBlockIDs::ProfileMetadataBlockID));
  EXPECT_OR_RET(MData, advance());
  if (MData->Kind != BitstreamEntry::Record)
    return unsupported("Expected Version record");

  SmallVector<uint64_t, 1> Ver;
  EXPECT_OR_RET(Code, Cursor.readRecord(bitc::UNABBREV_RECORD, Ver));
  if (*Code != PGOCtxProfileRecords::Version)
    return unsupported("Expected Version record");
  if (Ver.size() != 1 || Ver[0] > PGOCtxProfileWriter::CurrentVersion)
    return unsupported("Version " + Twine(*Code) +
                       " is higher than supported version " +
                       Twine(PGOCtxProfileWriter::CurrentVersion));
  return Error::success();
}

Expected<std::map<GlobalValue::GUID, PGOContextualProfile>>
PGOCtxProfileReader::loadContexts() {
  std::map<GlobalValue::GUID, PGOContextualProfile> Ret;
  RET_ON_ERR(readMetadata());
  while (canReadContext()) {
    EXPECT_OR_RET(E, readContext(false));
    auto Key = E->second.guid();
    if (!Ret.insert({Key, std::move(E->second)}).second)
      return wrongValue("Duplicate roots");
  }
  return std::move(Ret);
}
