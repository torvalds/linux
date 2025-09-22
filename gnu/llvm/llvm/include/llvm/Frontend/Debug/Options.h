//===--- DebugInfoOptions.h - Debug Info Emission Types ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FRONTEND_DEBUG_OPTIONS_H
#define LLVM_FRONTEND_DEBUG_OPTIONS_H

namespace llvm {
namespace codegenoptions {

enum DebugInfoFormat {
  DIF_DWARF,
  DIF_CodeView,
};

enum DebugInfoKind {
  /// Don't generate debug info.
  NoDebugInfo,

  /// Emit location information but do not generate debug info in the output.
  /// This is useful in cases where the backend wants to track source
  /// locations for instructions without actually emitting debug info for them
  /// (e.g., when -Rpass is used).
  LocTrackingOnly,

  /// Emit only debug directives with the line numbers data
  DebugDirectivesOnly,

  /// Emit only debug info necessary for generating line number tables
  /// (-gline-tables-only).
  DebugLineTablesOnly,

  /// Limit generated debug info for classes to reduce size. This emits class
  /// type info only where the constructor is emitted, if it is a class that
  /// has a constructor.
  /// FIXME: Consider combining this with LimitedDebugInfo.
  DebugInfoConstructor,

  /// Limit generated debug info to reduce size (-fno-standalone-debug). This
  /// emits forward decls for types that could be replaced with forward decls in
  /// the source code. For dynamic C++ classes type info is only emitted into
  /// the module that contains the classe's vtable.
  LimitedDebugInfo,

  /// Generate complete debug info.
  FullDebugInfo,

  /// Generate debug info for types that may be unused in the source
  /// (-fno-eliminate-unused-debug-types).
  UnusedTypeInfo,
};

enum class DebugTemplateNamesKind { Full, Simple, Mangled };

} // end namespace codegenoptions
} // end namespace llvm

#endif
