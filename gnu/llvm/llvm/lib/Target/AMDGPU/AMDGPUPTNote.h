//===-- AMDGPUNoteType.h - AMDGPU ELF PT_NOTE section info-------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
///
/// Enums and constants for AMDGPU PT_NOTE sections.
///
//
//===----------------------------------------------------------------------===//
//
#ifndef LLVM_LIB_TARGET_AMDGPU_AMDGPUPTNOTE_H
#define LLVM_LIB_TARGET_AMDGPU_AMDGPUPTNOTE_H

namespace llvm {
namespace AMDGPU {

namespace ElfNote {

const char SectionName[] = ".note";

const char NoteNameV2[] = "AMD";
const char NoteNameV3[] = "AMDGPU";

} // End namespace ElfNote
} // End namespace AMDGPU
} // End namespace llvm
#endif // LLVM_LIB_TARGET_AMDGPU_AMDGPUPTNOTE_H
