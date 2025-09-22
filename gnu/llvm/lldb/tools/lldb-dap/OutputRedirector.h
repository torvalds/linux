//===-- OutputRedirector.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===/

#ifndef LLDB_TOOLS_LLDB_DAP_OUTPUT_REDIRECTOR_H
#define LLDB_TOOLS_LLDB_DAP_OUTPUT_REDIRECTOR_H

#include <thread>

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

namespace lldb_dap {

/// Redirects the output of a given file descriptor to a callback.
///
/// \return
///     \a Error::success if the redirection was set up correctly, or an error
///     otherwise.
llvm::Error RedirectFd(int fd, std::function<void(llvm::StringRef)> callback);

} // namespace lldb_dap

#endif // LLDB_TOOLS_LLDB_DAP_OUTPUT_REDIRECTOR_H
