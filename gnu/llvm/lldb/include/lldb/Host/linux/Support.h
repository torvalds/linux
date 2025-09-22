//===-- Support.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_LINUX_SUPPORT_H
#define LLDB_HOST_LINUX_SUPPORT_H

#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/MemoryBuffer.h"
#include <memory>

namespace lldb_private {

llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>>
getProcFile(::pid_t pid, ::pid_t tid, const llvm::Twine &file);

llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>>
getProcFile(::pid_t pid, const llvm::Twine &file);

llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>>
getProcFile(const llvm::Twine &file);

} // namespace lldb_private

#endif // #ifndef LLDB_HOST_LINUX_SUPPORT_H
