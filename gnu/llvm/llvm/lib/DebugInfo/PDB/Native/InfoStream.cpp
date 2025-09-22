//===- InfoStream.cpp - PDB Info Stream (Stream 1) Access -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/InfoStream.h"
#include "llvm/DebugInfo/PDB/Native/RawConstants.h"
#include "llvm/DebugInfo/PDB/Native/RawError.h"
#include "llvm/DebugInfo/PDB/Native/RawTypes.h"
#include "llvm/Support/BinaryStreamReader.h"

using namespace llvm;
using namespace llvm::codeview;
// using namespace llvm::msf;
using namespace llvm::pdb;

InfoStream::InfoStream(std::unique_ptr<BinaryStream> Stream)
    : Stream(std::move(Stream)), Header(nullptr) {}

Error InfoStream::reload() {
  BinaryStreamReader Reader(*Stream);

  if (auto EC = Reader.readObject(Header))
    return joinErrors(
        std::move(EC),
        make_error<RawError>(raw_error_code::corrupt_file,
                             "PDB Stream does not contain a header."));

  switch (Header->Version) {
  case PdbImplVC70:
  case PdbImplVC80:
  case PdbImplVC110:
  case PdbImplVC140:
    break;
  default:
    return make_error<RawError>(raw_error_code::corrupt_file,
                                "Unsupported PDB stream version.");
  }

  uint32_t Offset = Reader.getOffset();
  if (auto EC = NamedStreams.load(Reader))
    return EC;
  uint32_t NewOffset = Reader.getOffset();
  NamedStreamMapByteSize = NewOffset - Offset;

  Reader.setOffset(Offset);
  if (auto EC = Reader.readSubstream(SubNamedStreams, NamedStreamMapByteSize))
    return EC;

  bool Stop = false;
  while (!Stop && !Reader.empty()) {
    PdbRaw_FeatureSig Sig;
    if (auto EC = Reader.readEnum(Sig))
      return EC;
    // Since this value comes from a file, it's possible we have some strange
    // value which doesn't correspond to any value.  We don't want to warn on
    // -Wcovered-switch-default in this case, so switch on the integral value
    // instead of the enumeration value.
    switch (uint32_t(Sig)) {
    case uint32_t(PdbRaw_FeatureSig::VC110):
      // No other flags for VC110 PDB.
      Stop = true;
      [[fallthrough]];
    case uint32_t(PdbRaw_FeatureSig::VC140):
      Features |= PdbFeatureContainsIdStream;
      break;
    case uint32_t(PdbRaw_FeatureSig::NoTypeMerge):
      Features |= PdbFeatureNoTypeMerging;
      break;
    case uint32_t(PdbRaw_FeatureSig::MinimalDebugInfo):
      Features |= PdbFeatureMinimalDebugInfo;
      break;
    default:
      continue;
    }
    FeatureSignatures.push_back(Sig);
  }
  return Error::success();
}

uint32_t InfoStream::getStreamSize() const { return Stream->getLength(); }

Expected<uint32_t> InfoStream::getNamedStreamIndex(llvm::StringRef Name) const {
  uint32_t Result;
  if (!NamedStreams.get(Name, Result))
    return make_error<RawError>(raw_error_code::no_stream);
  return Result;
}

StringMap<uint32_t> InfoStream::named_streams() const {
  return NamedStreams.entries();
}

bool InfoStream::containsIdStream() const {
  return !!(Features & PdbFeatureContainsIdStream);
}

PdbRaw_ImplVer InfoStream::getVersion() const {
  return static_cast<PdbRaw_ImplVer>(uint32_t(Header->Version));
}

uint32_t InfoStream::getSignature() const {
  return uint32_t(Header->Signature);
}

uint32_t InfoStream::getAge() const { return uint32_t(Header->Age); }

GUID InfoStream::getGuid() const { return Header->Guid; }

uint32_t InfoStream::getNamedStreamMapByteSize() const {
  return NamedStreamMapByteSize;
}

PdbRaw_Features InfoStream::getFeatures() const { return Features; }

ArrayRef<PdbRaw_FeatureSig> InfoStream::getFeatureSignatures() const {
  return FeatureSignatures;
}

const NamedStreamMap &InfoStream::getNamedStreams() const {
  return NamedStreams;
}

BinarySubstreamRef InfoStream::getNamedStreamsBuffer() const {
  return SubNamedStreams;
}
