//===- DbiStream.h - PDB Dbi Stream (Stream 3) Access -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_DBISTREAM_H
#define LLVM_DEBUGINFO_PDB_NATIVE_DBISTREAM_H

#include "llvm/DebugInfo/CodeView/DebugFrameDataSubsection.h"
#include "llvm/DebugInfo/PDB/Native/DbiModuleList.h"
#include "llvm/DebugInfo/PDB/Native/PDBStringTable.h"
#include "llvm/DebugInfo/PDB/Native/RawConstants.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"
#include "llvm/Support/BinaryStreamArray.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"

namespace llvm {
class BinaryStream;
namespace object {
struct FpoData;
struct coff_section;
}
namespace msf {
class MappedBlockStream;
}
namespace pdb {
struct DbiStreamHeader;
struct SecMapEntry;
struct SectionContrib2;
struct SectionContrib;
class PDBFile;
class ISectionContribVisitor;

class DbiStream {
  friend class DbiStreamBuilder;

public:
  explicit DbiStream(std::unique_ptr<BinaryStream> Stream);
  ~DbiStream();
  Error reload(PDBFile *Pdb);

  PdbRaw_DbiVer getDbiVersion() const;
  uint32_t getAge() const;
  uint16_t getPublicSymbolStreamIndex() const;
  uint16_t getGlobalSymbolStreamIndex() const;

  uint16_t getFlags() const;
  bool isIncrementallyLinked() const;
  bool hasCTypes() const;
  bool isStripped() const;

  uint16_t getBuildNumber() const;
  uint16_t getBuildMajorVersion() const;
  uint16_t getBuildMinorVersion() const;

  uint16_t getPdbDllRbld() const;
  uint32_t getPdbDllVersion() const;

  uint32_t getSymRecordStreamIndex() const;

  PDB_Machine getMachineType() const;

  const DbiStreamHeader *getHeader() const { return Header; }

  BinarySubstreamRef getSectionContributionData() const;
  BinarySubstreamRef getSecMapSubstreamData() const;
  BinarySubstreamRef getModiSubstreamData() const;
  BinarySubstreamRef getFileInfoSubstreamData() const;
  BinarySubstreamRef getTypeServerMapSubstreamData() const;
  BinarySubstreamRef getECSubstreamData() const;

  /// If the given stream type is present, returns its stream index. If it is
  /// not present, returns InvalidStreamIndex.
  uint32_t getDebugStreamIndex(DbgHeaderType Type) const;

  const DbiModuleList &modules() const;

  FixedStreamArray<object::coff_section> getSectionHeaders() const;

  bool hasOldFpoRecords() const;
  FixedStreamArray<object::FpoData> getOldFpoRecords() const;
  bool hasNewFpoRecords() const;
  const codeview::DebugFrameDataSubsectionRef &getNewFpoRecords() const;

  FixedStreamArray<SecMapEntry> getSectionMap() const;
  void visitSectionContributions(ISectionContribVisitor &Visitor) const;

  Expected<StringRef> getECName(uint32_t NI) const;

private:
  Error initializeSectionContributionData();
  Error initializeSectionHeadersData(PDBFile *Pdb);
  Error initializeSectionMapData();
  Error initializeOldFpoRecords(PDBFile *Pdb);
  Error initializeNewFpoRecords(PDBFile *Pdb);

  Expected<std::unique_ptr<msf::MappedBlockStream>>
  createIndexedStreamForHeaderType(PDBFile *Pdb, DbgHeaderType Type) const;

  std::unique_ptr<BinaryStream> Stream;

  PDBStringTable ECNames;

  BinarySubstreamRef SecContrSubstream;
  BinarySubstreamRef SecMapSubstream;
  BinarySubstreamRef ModiSubstream;
  BinarySubstreamRef FileInfoSubstream;
  BinarySubstreamRef TypeServerMapSubstream;
  BinarySubstreamRef ECSubstream;

  DbiModuleList Modules;

  FixedStreamArray<support::ulittle16_t> DbgStreams;

  PdbRaw_DbiSecContribVer SectionContribVersion =
      PdbRaw_DbiSecContribVer::DbiSecContribVer60;
  FixedStreamArray<SectionContrib> SectionContribs;
  FixedStreamArray<SectionContrib2> SectionContribs2;
  FixedStreamArray<SecMapEntry> SectionMap;

  std::unique_ptr<msf::MappedBlockStream> SectionHeaderStream;
  FixedStreamArray<object::coff_section> SectionHeaders;

  std::unique_ptr<msf::MappedBlockStream> OldFpoStream;
  FixedStreamArray<object::FpoData> OldFpoRecords;
  
  std::unique_ptr<msf::MappedBlockStream> NewFpoStream;
  codeview::DebugFrameDataSubsectionRef NewFpoRecords;

  const DbiStreamHeader *Header;
};
}
}

#endif
