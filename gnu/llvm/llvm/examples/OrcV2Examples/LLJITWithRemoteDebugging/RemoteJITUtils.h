//===-- RemoteJITUtils.h - Utilities for remote-JITing ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Utilities for ExecutorProcessControl-based remote JITing with Orc and
// JITLink.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXAMPLES_ORCV2EXAMPLES_LLJITWITHREMOTEDEBUGGING_REMOTEJITUTILS_H
#define LLVM_EXAMPLES_ORCV2EXAMPLES_LLJITWITHREMOTEDEBUGGING_REMOTEJITUTILS_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/Layer.h"
#include "llvm/ExecutionEngine/Orc/SimpleRemoteEPC.h"
#include "llvm/Support/Error.h"

#include <cstdint>
#include <memory>
#include <string>

/// Find the default exectuable on disk and create a JITLinkExecutor for it.
std::string findLocalExecutor(const char *HostArgv0);

llvm::Expected<std::pair<std::unique_ptr<llvm::orc::SimpleRemoteEPC>, uint64_t>>
launchLocalExecutor(llvm::StringRef ExecutablePath);

/// Create a JITLinkExecutor that connects to the given network address
/// through a TCP socket. A valid NetworkAddress provides hostname and port,
/// e.g. localhost:20000.
llvm::Expected<std::unique_ptr<llvm::orc::SimpleRemoteEPC>>
connectTCPSocket(llvm::StringRef NetworkAddress);

llvm::Expected<std::unique_ptr<llvm::orc::DefinitionGenerator>>
loadDylib(llvm::orc::ExecutionSession &ES, llvm::StringRef RemotePath);

#endif
