//===-------------------- VTuneSharedStructs.h ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Structs and serialization to share VTune-related information
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_SHARED_VTUNESHAREDSTRUCTS_H
#define LLVM_EXECUTIONENGINE_ORC_SHARED_VTUNESHAREDSTRUCTS_H

#include "ExecutorAddress.h"
#include <utility>
#include <vector>

namespace llvm {
namespace orc {

using VTuneLineTable = std::vector<std::pair<unsigned, unsigned>>;

// SI = String Index, 1-indexed into the VTuneMethodBatch::Strings table.
// SI == 0 means replace with nullptr.

// MI = Method Index, 1-indexed into the VTuneMethodBatch::Methods table.
// MI == 0 means this is a parent method and was not inlined.

struct VTuneMethodInfo {
  VTuneLineTable LineTable;
  ExecutorAddr LoadAddr;
  uint64_t LoadSize;
  uint64_t MethodID;
  uint32_t NameSI;
  uint32_t ClassFileSI;
  uint32_t SourceFileSI;
  uint32_t ParentMI;
};

using VTuneMethodTable = std::vector<VTuneMethodInfo>;
using VTuneStringTable = std::vector<std::string>;

struct VTuneMethodBatch {
  VTuneMethodTable Methods;
  VTuneStringTable Strings;
};

using VTuneUnloadedMethodIDs = SmallVector<std::pair<uint64_t, uint64_t>>;

namespace shared {

using SPSVTuneLineTable = SPSSequence<SPSTuple<uint32_t, uint32_t>>;
using SPSVTuneMethodInfo =
    SPSTuple<SPSVTuneLineTable, SPSExecutorAddr, uint64_t, uint64_t, uint32_t,
             uint32_t, uint32_t, uint32_t>;
using SPSVTuneMethodTable = SPSSequence<SPSVTuneMethodInfo>;
using SPSVTuneStringTable = SPSSequence<SPSString>;
using SPSVTuneMethodBatch = SPSTuple<SPSVTuneMethodTable, SPSVTuneStringTable>;
using SPSVTuneUnloadedMethodIDs = SPSSequence<SPSTuple<uint64_t, uint64_t>>;

template <> class SPSSerializationTraits<SPSVTuneMethodInfo, VTuneMethodInfo> {
public:
  static size_t size(const VTuneMethodInfo &MI) {
    return SPSVTuneMethodInfo::AsArgList::size(
        MI.LineTable, MI.LoadAddr, MI.LoadSize, MI.MethodID, MI.NameSI,
        MI.ClassFileSI, MI.SourceFileSI, MI.ParentMI);
  }

  static bool deserialize(SPSInputBuffer &IB, VTuneMethodInfo &MI) {
    return SPSVTuneMethodInfo::AsArgList::deserialize(
        IB, MI.LineTable, MI.LoadAddr, MI.LoadSize, MI.MethodID, MI.NameSI,
        MI.ClassFileSI, MI.SourceFileSI, MI.ParentMI);
  }

  static bool serialize(SPSOutputBuffer &OB, const VTuneMethodInfo &MI) {
    return SPSVTuneMethodInfo::AsArgList::serialize(
        OB, MI.LineTable, MI.LoadAddr, MI.LoadSize, MI.MethodID, MI.NameSI,
        MI.ClassFileSI, MI.SourceFileSI, MI.ParentMI);
  }
};

template <>
class SPSSerializationTraits<SPSVTuneMethodBatch, VTuneMethodBatch> {
public:
  static size_t size(const VTuneMethodBatch &MB) {
    return SPSVTuneMethodBatch::AsArgList::size(MB.Methods, MB.Strings);
  }

  static bool deserialize(SPSInputBuffer &IB, VTuneMethodBatch &MB) {
    return SPSVTuneMethodBatch::AsArgList::deserialize(IB, MB.Methods,
                                                       MB.Strings);
  }

  static bool serialize(SPSOutputBuffer &OB, const VTuneMethodBatch &MB) {
    return SPSVTuneMethodBatch::AsArgList::serialize(OB, MB.Methods,
                                                     MB.Strings);
  }
};

} // end namespace shared
} // end namespace orc
} // end namespace llvm

#endif
