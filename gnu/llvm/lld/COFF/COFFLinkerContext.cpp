//===- COFFContext.cpp ----------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Description
//
//===----------------------------------------------------------------------===//

#include "COFFLinkerContext.h"
#include "Symbols.h"
#include "lld/Common/Memory.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/DebugInfo/CodeView/TypeHashing.h"
#include "llvm/Demangle/Demangle.h"

namespace lld::coff {
COFFLinkerContext::COFFLinkerContext()
    : driver(*this), symtab(*this),
      ltoTextSection(llvm::COFF::IMAGE_SCN_MEM_EXECUTE),
      ltoDataSection(llvm::COFF::IMAGE_SCN_CNT_INITIALIZED_DATA),
      ltoTextSectionChunk(&ltoTextSection.section),
      ltoDataSectionChunk(&ltoDataSection.section),
      rootTimer("Total Linking Time"),
      inputFileTimer("Input File Reading", rootTimer),
      ltoTimer("LTO", rootTimer), gcTimer("GC", rootTimer),
      icfTimer("ICF", rootTimer), codeLayoutTimer("Code Layout", rootTimer),
      outputCommitTimer("Commit Output File", rootTimer),
      totalMapTimer("MAP Emission (Cumulative)", rootTimer),
      symbolGatherTimer("Gather Symbols", totalMapTimer),
      symbolStringsTimer("Build Symbol Strings", totalMapTimer),
      writeTimer("Write to File", totalMapTimer),
      totalPdbLinkTimer("PDB Emission (Cumulative)", rootTimer),
      addObjectsTimer("Add Objects", totalPdbLinkTimer),
      typeMergingTimer("Type Merging", addObjectsTimer),
      loadGHashTimer("Global Type Hashing", addObjectsTimer),
      mergeGHashTimer("GHash Type Merging", addObjectsTimer),
      symbolMergingTimer("Symbol Merging", addObjectsTimer),
      publicsLayoutTimer("Publics Stream Layout", totalPdbLinkTimer),
      tpiStreamLayoutTimer("TPI Stream Layout", totalPdbLinkTimer),
      diskCommitTimer("Commit to Disk", totalPdbLinkTimer) {}
} // namespace lld::coff
