//===-- BasicBlockSectionsProfileReader.cpp -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementation of the basic block sections profile reader pass. It parses
// and stores the basic block sections profile file (which is specified via the
// `-basic-block-sections` flag).
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/BasicBlockSectionsProfileReader.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/Pass.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/LineIterator.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include <llvm/ADT/STLExtras.h>

using namespace llvm;

char BasicBlockSectionsProfileReaderWrapperPass::ID = 0;
INITIALIZE_PASS(BasicBlockSectionsProfileReaderWrapperPass,
                "bbsections-profile-reader",
                "Reads and parses a basic block sections profile.", false,
                false)

Expected<UniqueBBID>
BasicBlockSectionsProfileReader::parseUniqueBBID(StringRef S) const {
  SmallVector<StringRef, 2> Parts;
  S.split(Parts, '.');
  if (Parts.size() > 2)
    return createProfileParseError(Twine("unable to parse basic block id: '") +
                                   S + "'");
  unsigned long long BaseBBID;
  if (getAsUnsignedInteger(Parts[0], 10, BaseBBID))
    return createProfileParseError(
        Twine("unable to parse BB id: '" + Parts[0]) +
        "': unsigned integer expected");
  unsigned long long CloneID = 0;
  if (Parts.size() > 1 && getAsUnsignedInteger(Parts[1], 10, CloneID))
    return createProfileParseError(Twine("unable to parse clone id: '") +
                                   Parts[1] + "': unsigned integer expected");
  return UniqueBBID{static_cast<unsigned>(BaseBBID),
                    static_cast<unsigned>(CloneID)};
}

bool BasicBlockSectionsProfileReader::isFunctionHot(StringRef FuncName) const {
  return getClusterInfoForFunction(FuncName).first;
}

std::pair<bool, SmallVector<BBClusterInfo>>
BasicBlockSectionsProfileReader::getClusterInfoForFunction(
    StringRef FuncName) const {
  auto R = ProgramPathAndClusterInfo.find(getAliasName(FuncName));
  return R != ProgramPathAndClusterInfo.end()
             ? std::pair(true, R->second.ClusterInfo)
             : std::pair(false, SmallVector<BBClusterInfo>());
}

SmallVector<SmallVector<unsigned>>
BasicBlockSectionsProfileReader::getClonePathsForFunction(
    StringRef FuncName) const {
  return ProgramPathAndClusterInfo.lookup(getAliasName(FuncName)).ClonePaths;
}

// Reads the version 1 basic block sections profile. Profile for each function
// is encoded as follows:
//   m <module_name>
//   f <function_name_1> <function_name_2> ...
//   c <bb_id_1> <bb_id_2> <bb_id_3>
//   c <bb_id_4> <bb_id_5>
//   ...
// Module name specifier (starting with 'm') is optional and allows
// distinguishing profile for internal-linkage functions with the same name. If
// not specified, it will apply to any function with the same name. Function
// name specifier (starting with 'f') can specify multiple function name
// aliases. Basic block clusters are specified by 'c' and specify the cluster of
// basic blocks, and the internal order in which they must be placed in the same
// section.
// This profile can also specify cloning paths which instruct the compiler to
// clone basic blocks along a path. The cloned blocks are then specified in the
// cluster information.
// The following profile lists two cloning paths (starting with 'p') for
// function bar and places the total 9 blocks within two clusters. The first two
// blocks of a cloning path specify the edge along which the path is cloned. For
// instance, path 1 (1 -> 3 -> 4) instructs that 3 and 4 must be cloned along
// the edge 1->3. Within the given clusters, each cloned block is identified by
// "<original block id>.<clone id>". For instance, 3.1 represents the first
// clone of block 3. Original blocks are specified just with their block ids. A
// block cloned multiple times appears with distinct clone ids. The CFG for bar
// is shown below before and after cloning with its final clusters labeled.
//
// f main
// f bar
// p 1 3 4           # cloning path 1
// p 4 2             # cloning path 2
// c 1 3.1 4.1 6     # basic block cluster 1
// c 0 2 3 4 2.1 5   # basic block cluster 2
// ****************************************************************************
// function bar before and after cloning with basic block clusters shown.
// ****************************************************************************
//                                ....      ..............
//      0 -------+                : 0 :---->: 1 ---> 3.1 :
//      |        |                : | :     :........ |  :
//      v        v                : v :             : v  :
// +--> 2 --> 5  1   ~~~~~~>  +---: 2 :             : 4.1: clsuter 1
// |    |        |            |   : | :             : |  :
// |    v        |            |   : v .......       : v  :
// |    3 <------+            |   : 3 <--+  :       : 6  :
// |    |                     |   : |    |  :       :....:
// |    v                     |   : v    |  :
// +--- 4 ---> 6              |   : 4    |  :
//                            |   : |    |  :
//                            |   : v    |  :
//                            |   :2.1---+  : cluster 2
//                            |   : | ......:
//                            |   : v :
//                            +-->: 5 :
//                                ....
// ****************************************************************************
Error BasicBlockSectionsProfileReader::ReadV1Profile() {
  auto FI = ProgramPathAndClusterInfo.end();

  // Current cluster ID corresponding to this function.
  unsigned CurrentCluster = 0;
  // Current position in the current cluster.
  unsigned CurrentPosition = 0;

  // Temporary set to ensure every basic block ID appears once in the clusters
  // of a function.
  DenseSet<UniqueBBID> FuncBBIDs;

  // Debug-info-based module filename for the current function. Empty string
  // means no filename.
  StringRef DIFilename;

  for (; !LineIt.is_at_eof(); ++LineIt) {
    StringRef S(*LineIt);
    char Specifier = S[0];
    S = S.drop_front().trim();
    SmallVector<StringRef, 4> Values;
    S.split(Values, ' ');
    switch (Specifier) {
    case '@':
      continue;
    case 'm': // Module name speicifer.
      if (Values.size() != 1) {
        return createProfileParseError(Twine("invalid module name value: '") +
                                       S + "'");
      }
      DIFilename = sys::path::remove_leading_dotslash(Values[0]);
      continue;
    case 'f': { // Function names specifier.
      bool FunctionFound = any_of(Values, [&](StringRef Alias) {
        auto It = FunctionNameToDIFilename.find(Alias);
        // No match if this function name is not found in this module.
        if (It == FunctionNameToDIFilename.end())
          return false;
        // Return a match if debug-info-filename is not specified. Otherwise,
        // check for equality.
        return DIFilename.empty() || It->second == DIFilename;
      });
      if (!FunctionFound) {
        // Skip the following profile by setting the profile iterator (FI) to
        // the past-the-end element.
        FI = ProgramPathAndClusterInfo.end();
        DIFilename = "";
        continue;
      }
      for (size_t i = 1; i < Values.size(); ++i)
        FuncAliasMap.try_emplace(Values[i], Values.front());

      // Prepare for parsing clusters of this function name.
      // Start a new cluster map for this function name.
      auto R = ProgramPathAndClusterInfo.try_emplace(Values.front());
      // Report error when multiple profiles have been specified for the same
      // function.
      if (!R.second)
        return createProfileParseError("duplicate profile for function '" +
                                       Values.front() + "'");
      FI = R.first;
      CurrentCluster = 0;
      FuncBBIDs.clear();
      // We won't need DIFilename anymore. Clean it up to avoid its application
      // on the next function.
      DIFilename = "";
      continue;
    }
    case 'c': // Basic block cluster specifier.
      // Skip the profile when we the profile iterator (FI) refers to the
      // past-the-end element.
      if (FI == ProgramPathAndClusterInfo.end())
        continue;
      // Reset current cluster position.
      CurrentPosition = 0;
      for (auto BasicBlockIDStr : Values) {
        auto BasicBlockID = parseUniqueBBID(BasicBlockIDStr);
        if (!BasicBlockID)
          return BasicBlockID.takeError();
        if (!FuncBBIDs.insert(*BasicBlockID).second)
          return createProfileParseError(
              Twine("duplicate basic block id found '") + BasicBlockIDStr +
              "'");

        FI->second.ClusterInfo.emplace_back(BBClusterInfo{
            *std::move(BasicBlockID), CurrentCluster, CurrentPosition++});
      }
      CurrentCluster++;
      continue;
    case 'p': { // Basic block cloning path specifier.
      // Skip the profile when we the profile iterator (FI) refers to the
      // past-the-end element.
      if (FI == ProgramPathAndClusterInfo.end())
        continue;
      SmallSet<unsigned, 5> BBsInPath;
      FI->second.ClonePaths.push_back({});
      for (size_t I = 0; I < Values.size(); ++I) {
        auto BaseBBIDStr = Values[I];
        unsigned long long BaseBBID = 0;
        if (getAsUnsignedInteger(BaseBBIDStr, 10, BaseBBID))
          return createProfileParseError(Twine("unsigned integer expected: '") +
                                         BaseBBIDStr + "'");
        if (I != 0 && !BBsInPath.insert(BaseBBID).second)
          return createProfileParseError(
              Twine("duplicate cloned block in path: '") + BaseBBIDStr + "'");
        FI->second.ClonePaths.back().push_back(BaseBBID);
      }
      continue;
    }
    default:
      return createProfileParseError(Twine("invalid specifier: '") +
                                     Twine(Specifier) + "'");
    }
    llvm_unreachable("should not break from this switch statement");
  }
  return Error::success();
}

Error BasicBlockSectionsProfileReader::ReadV0Profile() {
  auto FI = ProgramPathAndClusterInfo.end();
  // Current cluster ID corresponding to this function.
  unsigned CurrentCluster = 0;
  // Current position in the current cluster.
  unsigned CurrentPosition = 0;

  // Temporary set to ensure every basic block ID appears once in the clusters
  // of a function.
  SmallSet<unsigned, 4> FuncBBIDs;

  for (; !LineIt.is_at_eof(); ++LineIt) {
    StringRef S(*LineIt);
    if (S[0] == '@')
      continue;
    // Check for the leading "!"
    if (!S.consume_front("!") || S.empty())
      break;
    // Check for second "!" which indicates a cluster of basic blocks.
    if (S.consume_front("!")) {
      // Skip the profile when we the profile iterator (FI) refers to the
      // past-the-end element.
      if (FI == ProgramPathAndClusterInfo.end())
        continue;
      SmallVector<StringRef, 4> BBIDs;
      S.split(BBIDs, ' ');
      // Reset current cluster position.
      CurrentPosition = 0;
      for (auto BBIDStr : BBIDs) {
        unsigned long long BBID;
        if (getAsUnsignedInteger(BBIDStr, 10, BBID))
          return createProfileParseError(Twine("unsigned integer expected: '") +
                                         BBIDStr + "'");
        if (!FuncBBIDs.insert(BBID).second)
          return createProfileParseError(
              Twine("duplicate basic block id found '") + BBIDStr + "'");

        FI->second.ClusterInfo.emplace_back(
            BBClusterInfo({{static_cast<unsigned>(BBID), 0},
                           CurrentCluster,
                           CurrentPosition++}));
      }
      CurrentCluster++;
    } else {
      // This is a function name specifier. It may include a debug info filename
      // specifier starting with `M=`.
      auto [AliasesStr, DIFilenameStr] = S.split(' ');
      SmallString<128> DIFilename;
      if (DIFilenameStr.starts_with("M=")) {
        DIFilename =
            sys::path::remove_leading_dotslash(DIFilenameStr.substr(2));
        if (DIFilename.empty())
          return createProfileParseError("empty module name specifier");
      } else if (!DIFilenameStr.empty()) {
        return createProfileParseError("unknown string found: '" +
                                       DIFilenameStr + "'");
      }
      // Function aliases are separated using '/'. We use the first function
      // name for the cluster info mapping and delegate all other aliases to
      // this one.
      SmallVector<StringRef, 4> Aliases;
      AliasesStr.split(Aliases, '/');
      bool FunctionFound = any_of(Aliases, [&](StringRef Alias) {
        auto It = FunctionNameToDIFilename.find(Alias);
        // No match if this function name is not found in this module.
        if (It == FunctionNameToDIFilename.end())
          return false;
        // Return a match if debug-info-filename is not specified. Otherwise,
        // check for equality.
        return DIFilename.empty() || It->second == DIFilename;
      });
      if (!FunctionFound) {
        // Skip the following profile by setting the profile iterator (FI) to
        // the past-the-end element.
        FI = ProgramPathAndClusterInfo.end();
        continue;
      }
      for (size_t i = 1; i < Aliases.size(); ++i)
        FuncAliasMap.try_emplace(Aliases[i], Aliases.front());

      // Prepare for parsing clusters of this function name.
      // Start a new cluster map for this function name.
      auto R = ProgramPathAndClusterInfo.try_emplace(Aliases.front());
      // Report error when multiple profiles have been specified for the same
      // function.
      if (!R.second)
        return createProfileParseError("duplicate profile for function '" +
                                       Aliases.front() + "'");
      FI = R.first;
      CurrentCluster = 0;
      FuncBBIDs.clear();
    }
  }
  return Error::success();
}

// Basic Block Sections can be enabled for a subset of machine basic blocks.
// This is done by passing a file containing names of functions for which basic
// block sections are desired. Additionally, machine basic block ids of the
// functions can also be specified for a finer granularity. Moreover, a cluster
// of basic blocks could be assigned to the same section.
// Optionally, a debug-info filename can be specified for each function to allow
// distinguishing internal-linkage functions of the same name.
// A file with basic block sections for all of function main and three blocks
// for function foo (of which 1 and 2 are placed in a cluster) looks like this:
// (Profile for function foo is only loaded when its debug-info filename
// matches 'path/to/foo_file.cc').
// ----------------------------
// list.txt:
// !main
// !foo M=path/to/foo_file.cc
// !!1 2
// !!4
Error BasicBlockSectionsProfileReader::ReadProfile() {
  assert(MBuf);

  unsigned long long Version = 0;
  StringRef FirstLine(*LineIt);
  if (FirstLine.consume_front("v")) {
    if (getAsUnsignedInteger(FirstLine, 10, Version)) {
      return createProfileParseError(Twine("version number expected: '") +
                                     FirstLine + "'");
    }
    if (Version > 1) {
      return createProfileParseError(Twine("invalid profile version: ") +
                                     Twine(Version));
    }
    ++LineIt;
  }

  switch (Version) {
  case 0:
    // TODO: Deprecate V0 once V1 is fully integrated downstream.
    return ReadV0Profile();
  case 1:
    return ReadV1Profile();
  default:
    llvm_unreachable("Invalid profile version.");
  }
}

bool BasicBlockSectionsProfileReaderWrapperPass::doInitialization(Module &M) {
  if (!BBSPR.MBuf)
    return false;
  // Get the function name to debug info filename mapping.
  BBSPR.FunctionNameToDIFilename.clear();
  for (const Function &F : M) {
    SmallString<128> DIFilename;
    if (F.isDeclaration())
      continue;
    DISubprogram *Subprogram = F.getSubprogram();
    if (Subprogram) {
      llvm::DICompileUnit *CU = Subprogram->getUnit();
      if (CU)
        DIFilename = sys::path::remove_leading_dotslash(CU->getFilename());
    }
    [[maybe_unused]] bool inserted =
        BBSPR.FunctionNameToDIFilename.try_emplace(F.getName(), DIFilename)
            .second;
    assert(inserted);
  }
  if (auto Err = BBSPR.ReadProfile())
    report_fatal_error(std::move(Err));
  return false;
}

AnalysisKey BasicBlockSectionsProfileReaderAnalysis::Key;

BasicBlockSectionsProfileReader
BasicBlockSectionsProfileReaderAnalysis::run(Function &F,
                                             FunctionAnalysisManager &AM) {
  return BasicBlockSectionsProfileReader(TM->getBBSectionsFuncListBuf());
}

bool BasicBlockSectionsProfileReaderWrapperPass::isFunctionHot(
    StringRef FuncName) const {
  return BBSPR.isFunctionHot(FuncName);
}

std::pair<bool, SmallVector<BBClusterInfo>>
BasicBlockSectionsProfileReaderWrapperPass::getClusterInfoForFunction(
    StringRef FuncName) const {
  return BBSPR.getClusterInfoForFunction(FuncName);
}

SmallVector<SmallVector<unsigned>>
BasicBlockSectionsProfileReaderWrapperPass::getClonePathsForFunction(
    StringRef FuncName) const {
  return BBSPR.getClonePathsForFunction(FuncName);
}

BasicBlockSectionsProfileReader &
BasicBlockSectionsProfileReaderWrapperPass::getBBSPR() {
  return BBSPR;
}

ImmutablePass *llvm::createBasicBlockSectionsProfileReaderWrapperPass(
    const MemoryBuffer *Buf) {
  return new BasicBlockSectionsProfileReaderWrapperPass(Buf);
}
