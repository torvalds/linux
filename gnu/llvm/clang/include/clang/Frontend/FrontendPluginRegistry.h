//===- FrontendPluginRegistry.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Pluggable Frontend Action Interface
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_FRONTEND_FRONTENDPLUGINREGISTRY_H
#define LLVM_CLANG_FRONTEND_FRONTENDPLUGINREGISTRY_H

#include "clang/Frontend/FrontendAction.h"
#include "llvm/Support/Registry.h"

namespace clang {

/// The frontend plugin registry.
using FrontendPluginRegistry = llvm::Registry<PluginASTAction>;

} // namespace clang

#endif // LLVM_CLANG_FRONTEND_FRONTENDPLUGINREGISTRY_H
