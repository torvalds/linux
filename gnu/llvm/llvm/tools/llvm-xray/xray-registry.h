//===- xray-registry.h - Define registry mechanism for commands. ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implement a simple subcommand registry.
//
//===----------------------------------------------------------------------===//
#ifndef TOOLS_LLVM_XRAY_XRAY_REGISTRY_H
#define TOOLS_LLVM_XRAY_XRAY_REGISTRY_H

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace xray {

// Use |CommandRegistration| as a global initialiser that registers a function
// and associates it with |SC|. This requires that a command has not been
// registered to a given |SC|.
//
// Usage:
//
//   // At namespace scope.
//   static CommandRegistration Unused(&MySubCommand, [] { ... });
//
struct CommandRegistration {
  CommandRegistration(cl::SubCommand *SC, std::function<Error()> Command);
};

// Requires that |SC| is not null and has an associated function to it.
std::function<Error()> dispatch(cl::SubCommand *SC);

} // namespace xray
} // namespace llvm

#endif // TOOLS_LLVM_XRAY_XRAY_REGISTRY_H
