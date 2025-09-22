//===- DiagnosticBuilderWrappers.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// Diagnostic wrappers for TextAPI types for error reporting.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_INSTALLAPI_DIAGNOSTICBUILDER_WRAPPER_H
#define LLVM_CLANG_INSTALLAPI_DIAGNOSTICBUILDER_WRAPPER_H

#include "clang/Basic/Diagnostic.h"
#include "llvm/TextAPI/Architecture.h"
#include "llvm/TextAPI/ArchitectureSet.h"
#include "llvm/TextAPI/InterfaceFile.h"
#include "llvm/TextAPI/Platform.h"

namespace llvm {
namespace MachO {

const clang::DiagnosticBuilder &operator<<(const clang::DiagnosticBuilder &DB,
                                           const PlatformType &Platform);

const clang::DiagnosticBuilder &operator<<(const clang::DiagnosticBuilder &DB,
                                           const PlatformVersionSet &Platforms);

const clang::DiagnosticBuilder &operator<<(const clang::DiagnosticBuilder &DB,
                                           const Architecture &Arch);

const clang::DiagnosticBuilder &operator<<(const clang::DiagnosticBuilder &DB,
                                           const ArchitectureSet &ArchSet);

const clang::DiagnosticBuilder &operator<<(const clang::DiagnosticBuilder &DB,
                                           const FileType &Type);

const clang::DiagnosticBuilder &operator<<(const clang::DiagnosticBuilder &DB,
                                           const PackedVersion &Version);

const clang::DiagnosticBuilder &
operator<<(const clang::DiagnosticBuilder &DB,
           const StringMapEntry<ArchitectureSet> &LibAttr);

} // namespace MachO
} // namespace llvm
#endif // LLVM_CLANG_INSTALLAPI_DIAGNOSTICBUILDER_WRAPPER_H
