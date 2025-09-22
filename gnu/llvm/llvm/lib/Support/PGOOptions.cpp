//===------ PGOOptions.cpp -- PGO option tunables --------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/PGOOptions.h"
#include "llvm/Support/VirtualFileSystem.h"

using namespace llvm;

PGOOptions::PGOOptions(std::string ProfileFile, std::string CSProfileGenFile,
                       std::string ProfileRemappingFile,
                       std::string MemoryProfile,
                       IntrusiveRefCntPtr<vfs::FileSystem> FS, PGOAction Action,
                       CSPGOAction CSAction, ColdFuncOpt ColdType,
                       bool DebugInfoForProfiling, bool PseudoProbeForProfiling,
                       bool AtomicCounterUpdate)
    : ProfileFile(ProfileFile), CSProfileGenFile(CSProfileGenFile),
      ProfileRemappingFile(ProfileRemappingFile), MemoryProfile(MemoryProfile),
      Action(Action), CSAction(CSAction), ColdOptType(ColdType),
      DebugInfoForProfiling(DebugInfoForProfiling ||
                            (Action == SampleUse && !PseudoProbeForProfiling)),
      PseudoProbeForProfiling(PseudoProbeForProfiling),
      AtomicCounterUpdate(AtomicCounterUpdate), FS(std::move(FS)) {
  // Note, we do allow ProfileFile.empty() for Action=IRUse LTO can
  // callback with IRUse action without ProfileFile.

  // If there is a CSAction, PGOAction cannot be IRInstr or SampleUse.
  assert(this->CSAction == NoCSAction ||
         (this->Action != IRInstr && this->Action != SampleUse));

  // For CSIRInstr, CSProfileGenFile also needs to be nonempty.
  assert(this->CSAction != CSIRInstr || !this->CSProfileGenFile.empty());

  // If CSAction is CSIRUse, PGOAction needs to be IRUse as they share
  // a profile.
  assert(this->CSAction != CSIRUse || this->Action == IRUse);

  // Cannot optimize with MemProf profile during IR instrumentation.
  assert(this->MemoryProfile.empty() || this->Action != PGOOptions::IRInstr);

  // If neither Action nor CSAction nor MemoryProfile are set,
  // DebugInfoForProfiling or PseudoProbeForProfiling needs to be true.
  assert(this->Action != NoAction || this->CSAction != NoCSAction ||
         !this->MemoryProfile.empty() || this->DebugInfoForProfiling ||
         this->PseudoProbeForProfiling);

  // If we need to use the profile, the VFS cannot be nullptr.
  assert(this->FS || !(this->Action == IRUse || this->CSAction == CSIRUse ||
                       !this->MemoryProfile.empty()));
}

PGOOptions::PGOOptions(const PGOOptions &) = default;

PGOOptions &PGOOptions::operator=(const PGOOptions &O) = default;

PGOOptions::~PGOOptions() = default;
