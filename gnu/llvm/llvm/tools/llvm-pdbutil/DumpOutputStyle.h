//===- DumpOutputStyle.h -------------------------------------- *- C++ --*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVMPDBDUMP_DUMPOUTPUTSTYLE_H
#define LLVM_TOOLS_LLVMPDBDUMP_DUMPOUTPUTSTYLE_H

#include "OutputStyle.h"
#include "StreamUtil.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/DebugInfo/PDB/Native/LinePrinter.h"
#include "llvm/DebugInfo/PDB/Native/RawConstants.h"

namespace llvm {
namespace object {
class COFFObjectFile;
}

namespace pdb {
class GSIHashTable;
class InputFile;
class TypeReferenceTracker;

struct StatCollection {
  struct Stat {
    Stat() {}
    Stat(uint32_t Count, uint32_t Size) : Count(Count), Size(Size) {}
    uint32_t Count = 0;
    uint32_t Size = 0;

    void update(uint32_t RecordSize) {
      ++Count;
      Size += RecordSize;
    }
  };

  using KindAndStat = std::pair<uint32_t, Stat>;

  void update(uint32_t Kind, uint32_t RecordSize) {
    Totals.update(RecordSize);
    auto Iter = Individual.try_emplace(Kind, 1, RecordSize);
    if (!Iter.second)
      Iter.first->second.update(RecordSize);
  }
  Stat Totals;
  DenseMap<uint32_t, Stat> Individual;

  std::vector<KindAndStat> getStatsSortedBySize() const;
};

class DumpOutputStyle : public OutputStyle {

public:
  DumpOutputStyle(InputFile &File);
  ~DumpOutputStyle() override;

  Error dump() override;

private:
  PDBFile &getPdb();
  object::COFFObjectFile &getObj();

  void printStreamNotValidForObj();
  void printStreamNotPresent(StringRef StreamName);

  Error dumpFileSummary();
  Error dumpStreamSummary();
  Error dumpSymbolStats();
  Error dumpUdtStats();
  Error dumpTypeStats();
  Error dumpNamedStreams();
  Error dumpStringTable();
  Error dumpStringTableFromPdb();
  Error dumpStringTableFromObj();
  Error dumpLines();
  Error dumpInlineeLines();
  Error dumpXmi();
  Error dumpXme();
  Error dumpFpo();
  Error dumpOldFpo(PDBFile &File);
  Error dumpNewFpo(PDBFile &File);
  Error dumpTpiStream(uint32_t StreamIdx);
  Error dumpTypesFromObjectFile();
  Error dumpTypeRefStats();
  Error dumpModules();
  Error dumpModuleFiles();
  Error dumpModuleSymsForPdb();
  Error dumpModuleSymsForObj();
  Error dumpGSIRecords();
  Error dumpGlobals();
  Error dumpPublics();
  Error dumpSymbolsFromGSI(const GSIHashTable &Table, bool HashExtras);
  Error dumpSectionHeaders();
  Error dumpSectionContribs();
  Error dumpSectionMap();

  void dumpSectionHeaders(StringRef Label, DbgHeaderType Type);

  InputFile &File;
  std::unique_ptr<TypeReferenceTracker> RefTracker;
  LinePrinter P;
  SmallVector<StreamInfo, 32> StreamPurposes;
};
} // namespace pdb
} // namespace llvm

#endif
