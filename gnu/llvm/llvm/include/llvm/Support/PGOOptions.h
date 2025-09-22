//===------ PGOOptions.h -- PGO option tunables ----------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// Define option tunables for PGO.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_PGOOPTIONS_H
#define LLVM_SUPPORT_PGOOPTIONS_H

#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/Support/Error.h"

namespace llvm {

namespace vfs {
class FileSystem;
} // namespace vfs

/// A struct capturing PGO tunables.
struct PGOOptions {
  enum PGOAction { NoAction, IRInstr, IRUse, SampleUse };
  enum CSPGOAction { NoCSAction, CSIRInstr, CSIRUse };
  enum class ColdFuncOpt { Default, OptSize, MinSize, OptNone };
  PGOOptions(std::string ProfileFile, std::string CSProfileGenFile,
             std::string ProfileRemappingFile, std::string MemoryProfile,
             IntrusiveRefCntPtr<vfs::FileSystem> FS,
             PGOAction Action = NoAction, CSPGOAction CSAction = NoCSAction,
             ColdFuncOpt ColdType = ColdFuncOpt::Default,
             bool DebugInfoForProfiling = false,
             bool PseudoProbeForProfiling = false,
             bool AtomicCounterUpdate = false);
  PGOOptions(const PGOOptions &);
  ~PGOOptions();
  PGOOptions &operator=(const PGOOptions &);

  std::string ProfileFile;
  std::string CSProfileGenFile;
  std::string ProfileRemappingFile;
  std::string MemoryProfile;
  PGOAction Action;
  CSPGOAction CSAction;
  ColdFuncOpt ColdOptType;
  bool DebugInfoForProfiling;
  bool PseudoProbeForProfiling;
  bool AtomicCounterUpdate;
  IntrusiveRefCntPtr<vfs::FileSystem> FS;
};
} // namespace llvm

#endif
