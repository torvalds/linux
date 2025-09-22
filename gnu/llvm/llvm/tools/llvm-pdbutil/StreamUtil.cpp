//===- StreamUtil.cpp - PDB stream utilities --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "StreamUtil.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/DebugInfo/PDB/Native/DbiModuleDescriptor.h"
#include "llvm/DebugInfo/PDB/Native/DbiModuleList.h"
#include "llvm/DebugInfo/PDB/Native/DbiStream.h"
#include "llvm/DebugInfo/PDB/Native/FormatUtil.h"
#include "llvm/DebugInfo/PDB/Native/InfoStream.h"
#include "llvm/DebugInfo/PDB/Native/PDBFile.h"
#include "llvm/DebugInfo/PDB/Native/TpiStream.h"

using namespace llvm;
using namespace llvm::pdb;

std::string StreamInfo::getLongName() const {
  if (Purpose == StreamPurpose::NamedStream)
    return formatv("Named Stream \"{0}\"", Name).str();
  if (Purpose == StreamPurpose::ModuleStream)
    return formatv("Module \"{0}\"", Name).str();
  return Name;
}

StreamInfo StreamInfo::createStream(StreamPurpose Purpose, StringRef Name,
                                    uint32_t StreamIndex) {
  StreamInfo Result;
  Result.Name = std::string(Name);
  Result.StreamIndex = StreamIndex;
  Result.Purpose = Purpose;
  return Result;
}

StreamInfo StreamInfo::createModuleStream(StringRef Module,
                                          uint32_t StreamIndex, uint32_t Modi) {
  StreamInfo Result;
  Result.Name = std::string(Module);
  Result.StreamIndex = StreamIndex;
  Result.ModuleIndex = Modi;
  Result.Purpose = StreamPurpose::ModuleStream;
  return Result;
}

static inline StreamInfo stream(StreamPurpose Purpose, StringRef Label,
                                uint32_t Idx) {
  return StreamInfo::createStream(Purpose, Label, Idx);
}

static inline StreamInfo moduleStream(StringRef Label, uint32_t StreamIdx,
                                      uint32_t Modi) {
  return StreamInfo::createModuleStream(Label, StreamIdx, Modi);
}

struct IndexedModuleDescriptor {
  uint32_t Modi;
  DbiModuleDescriptor Descriptor;
};

void llvm::pdb::discoverStreamPurposes(PDBFile &File,
                                       SmallVectorImpl<StreamInfo> &Streams) {
  // It's OK if we fail to load some of these streams, we still attempt to print
  // what we can.
  auto Dbi = File.getPDBDbiStream();
  auto Tpi = File.getPDBTpiStream();
  auto Ipi = File.getPDBIpiStream();
  auto Info = File.getPDBInfoStream();

  uint32_t StreamCount = File.getNumStreams();
  DenseMap<uint16_t, IndexedModuleDescriptor> ModStreams;
  DenseMap<uint16_t, std::string> NamedStreams;

  if (Dbi) {
    const DbiModuleList &Modules = Dbi->modules();
    for (uint32_t I = 0; I < Modules.getModuleCount(); ++I) {
      IndexedModuleDescriptor IMD;
      IMD.Modi = I;
      IMD.Descriptor = Modules.getModuleDescriptor(I);
      uint16_t SN = IMD.Descriptor.getModuleStreamIndex();
      if (SN != kInvalidStreamIndex)
        ModStreams[SN] = IMD;
    }
  }
  if (Info) {
    for (auto &NSE : Info->named_streams()) {
      if (NSE.second != kInvalidStreamIndex)
        NamedStreams[NSE.second] = std::string(NSE.first());
    }
  }

  Streams.resize(StreamCount);
  for (uint32_t StreamIdx = 0; StreamIdx < StreamCount; ++StreamIdx) {
    if (StreamIdx == OldMSFDirectory)
      Streams[StreamIdx] =
          stream(StreamPurpose::Other, "Old MSF Directory", StreamIdx);
    else if (StreamIdx == StreamPDB)
      Streams[StreamIdx] = stream(StreamPurpose::PDB, "PDB Stream", StreamIdx);
    else if (StreamIdx == StreamDBI)
      Streams[StreamIdx] = stream(StreamPurpose::DBI, "DBI Stream", StreamIdx);
    else if (StreamIdx == StreamTPI)
      Streams[StreamIdx] = stream(StreamPurpose::TPI, "TPI Stream", StreamIdx);
    else if (StreamIdx == StreamIPI)
      Streams[StreamIdx] = stream(StreamPurpose::IPI, "IPI Stream", StreamIdx);
    else if (Dbi && StreamIdx == Dbi->getGlobalSymbolStreamIndex())
      Streams[StreamIdx] =
          stream(StreamPurpose::GlobalHash, "Global Symbol Hash", StreamIdx);
    else if (Dbi && StreamIdx == Dbi->getPublicSymbolStreamIndex())
      Streams[StreamIdx] =
          stream(StreamPurpose::PublicHash, "Public Symbol Hash", StreamIdx);
    else if (Dbi && StreamIdx == Dbi->getSymRecordStreamIndex())
      Streams[StreamIdx] =
          stream(StreamPurpose::Symbols, "Symbol Records", StreamIdx);
    else if (Tpi && StreamIdx == Tpi->getTypeHashStreamIndex())
      Streams[StreamIdx] =
          stream(StreamPurpose::TpiHash, "TPI Hash", StreamIdx);
    else if (Tpi && StreamIdx == Tpi->getTypeHashStreamAuxIndex())
      Streams[StreamIdx] =
          stream(StreamPurpose::Other, "TPI Aux Hash", StreamIdx);
    else if (Ipi && StreamIdx == Ipi->getTypeHashStreamIndex())
      Streams[StreamIdx] =
          stream(StreamPurpose::IpiHash, "IPI Hash", StreamIdx);
    else if (Ipi && StreamIdx == Ipi->getTypeHashStreamAuxIndex())
      Streams[StreamIdx] =
          stream(StreamPurpose::Other, "IPI Aux Hash", StreamIdx);
    else if (Dbi &&
             StreamIdx == Dbi->getDebugStreamIndex(DbgHeaderType::Exception))
      Streams[StreamIdx] =
          stream(StreamPurpose::Other, "Exception Data", StreamIdx);
    else if (Dbi && StreamIdx == Dbi->getDebugStreamIndex(DbgHeaderType::Fixup))
      Streams[StreamIdx] =
          stream(StreamPurpose::Other, "Fixup Data", StreamIdx);
    else if (Dbi && StreamIdx == Dbi->getDebugStreamIndex(DbgHeaderType::FPO))
      Streams[StreamIdx] = stream(StreamPurpose::Other, "FPO Data", StreamIdx);
    else if (Dbi &&
             StreamIdx == Dbi->getDebugStreamIndex(DbgHeaderType::NewFPO))
      Streams[StreamIdx] =
          stream(StreamPurpose::Other, "New FPO Data", StreamIdx);
    else if (Dbi &&
             StreamIdx == Dbi->getDebugStreamIndex(DbgHeaderType::OmapFromSrc))
      Streams[StreamIdx] =
          stream(StreamPurpose::Other, "Omap From Source Data", StreamIdx);
    else if (Dbi &&
             StreamIdx == Dbi->getDebugStreamIndex(DbgHeaderType::OmapToSrc))
      Streams[StreamIdx] =
          stream(StreamPurpose::Other, "Omap To Source Data", StreamIdx);
    else if (Dbi && StreamIdx == Dbi->getDebugStreamIndex(DbgHeaderType::Pdata))
      Streams[StreamIdx] = stream(StreamPurpose::Other, "Pdata", StreamIdx);
    else if (Dbi &&
             StreamIdx == Dbi->getDebugStreamIndex(DbgHeaderType::SectionHdr))
      Streams[StreamIdx] =
          stream(StreamPurpose::Other, "Section Header Data", StreamIdx);
    else if (Dbi &&
             StreamIdx ==
                 Dbi->getDebugStreamIndex(DbgHeaderType::SectionHdrOrig))
      Streams[StreamIdx] = stream(StreamPurpose::Other,
                                  "Section Header Original Data", StreamIdx);
    else if (Dbi &&
             StreamIdx == Dbi->getDebugStreamIndex(DbgHeaderType::TokenRidMap))
      Streams[StreamIdx] =
          stream(StreamPurpose::Other, "Token Rid Data", StreamIdx);
    else if (Dbi && StreamIdx == Dbi->getDebugStreamIndex(DbgHeaderType::Xdata))
      Streams[StreamIdx] = stream(StreamPurpose::Other, "Xdata", StreamIdx);
    else {
      auto ModIter = ModStreams.find(StreamIdx);
      auto NSIter = NamedStreams.find(StreamIdx);
      if (ModIter != ModStreams.end()) {
        Streams[StreamIdx] =
            moduleStream(ModIter->second.Descriptor.getModuleName(), StreamIdx,
                         ModIter->second.Modi);
      } else if (NSIter != NamedStreams.end()) {
        Streams[StreamIdx] =
            stream(StreamPurpose::NamedStream, NSIter->second, StreamIdx);
      } else {
        Streams[StreamIdx] = stream(StreamPurpose::Other, "???", StreamIdx);
      }
    }
  }

  // Consume errors from missing streams.
  if (!Dbi)
    consumeError(Dbi.takeError());
  if (!Tpi)
    consumeError(Tpi.takeError());
  if (!Ipi)
    consumeError(Ipi.takeError());
  if (!Info)
    consumeError(Info.takeError());
}
