//===-- NativeRegisterContextOpenBSD_arch.cpp ---------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// NativeRegisterContextOpenBSD_* contains the implementations for each
// supported architecture, and includes the static initalizer
// CreateHostNativeRegisterContextOpenBSD() implementation which returns a arch
// specific register context. This implementation contains a stub
// which just reports an error and exits on architectures which do
// not have a backend.

#if !defined(__arm64__) && !defined(__aarch64__) && !defined(__x86_64__)

#include "Plugins/Process/OpenBSD/NativeRegisterContextOpenBSD.h"

using namespace lldb_private;
using namespace lldb_private::process_openbsd;

std::unique_ptr<NativeRegisterContextOpenBSD>
NativeRegisterContextOpenBSD::CreateHostNativeRegisterContextOpenBSD(
        const ArchSpec &target_arch, NativeThreadProtocol &native_thread) {
  return std::unique_ptr<NativeRegisterContextOpenBSD>{};
}

#endif
