//===- InfoStream.h - PDB Info Stream (Stream 1) Access ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_INFOSTREAM_H
#define LLVM_DEBUGINFO_PDB_NATIVE_INFOSTREAM_H

#include "llvm/ADT/StringMap.h"
#include "llvm/DebugInfo/CodeView/GUID.h"
#include "llvm/DebugInfo/PDB/Native/NamedStreamMap.h"
#include "llvm/DebugInfo/PDB/Native/RawConstants.h"
#include "llvm/Support/BinaryStream.h"
#include "llvm/Support/BinaryStreamRef.h"

#include "llvm/Support/Error.h"

namespace llvm {
namespace pdb {
struct InfoStreamHeader;
class InfoStream {
  friend class InfoStreamBuilder;

public:
  InfoStream(std::unique_ptr<BinaryStream> Stream);

  Error reload();

  uint32_t getStreamSize() const;

  const InfoStreamHeader *getHeader() const { return Header; }

  bool containsIdStream() const;
  PdbRaw_ImplVer getVersion() const;
  uint32_t getSignature() const;
  uint32_t getAge() const;
  codeview::GUID getGuid() const;
  uint32_t getNamedStreamMapByteSize() const;

  PdbRaw_Features getFeatures() const;
  ArrayRef<PdbRaw_FeatureSig> getFeatureSignatures() const;

  const NamedStreamMap &getNamedStreams() const;

  BinarySubstreamRef getNamedStreamsBuffer() const;

  Expected<uint32_t> getNamedStreamIndex(llvm::StringRef Name) const;
  StringMap<uint32_t> named_streams() const;

private:
  std::unique_ptr<BinaryStream> Stream;

  const InfoStreamHeader *Header;

  BinarySubstreamRef SubNamedStreams;

  std::vector<PdbRaw_FeatureSig> FeatureSignatures;
  PdbRaw_Features Features = PdbFeatureNone;

  uint32_t NamedStreamMapByteSize = 0;

  NamedStreamMap NamedStreams;
};
}
}

#endif
