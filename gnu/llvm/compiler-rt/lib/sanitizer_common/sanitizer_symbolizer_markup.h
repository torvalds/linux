//===-- sanitizer_symbolizer_markup.h -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file is shared between various sanitizers' runtime libraries.
//
//  Header for the offline markup symbolizer.
//===----------------------------------------------------------------------===//
#ifndef SANITIZER_SYMBOLIZER_MARKUP_H
#define SANITIZER_SYMBOLIZER_MARKUP_H

#include "sanitizer_common.h"
#include "sanitizer_stacktrace_printer.h"
#include "sanitizer_symbolizer.h"
#include "sanitizer_symbolizer_internal.h"

namespace __sanitizer {

// Simplier view of a LoadedModule. It only holds information necessary to
// identify unique modules.
struct RenderedModule {
  char *full_name;
  uptr base_address;
  u8 uuid[kModuleUUIDSize];  // BuildId
};

class MarkupStackTracePrinter : public StackTracePrinter {
 public:
  // We don't support the stack_trace_format flag at all.
  void RenderFrame(InternalScopedString *buffer, const char *format,
                   int frame_no, uptr address, const AddressInfo *info,
                   bool vs_style, const char *strip_path_prefix = "") override;

  bool RenderNeedsSymbolization(const char *format) override;

  // We ignore the format argument to __sanitizer_symbolize_global.
  void RenderData(InternalScopedString *buffer, const char *format,
                  const DataInfo *DI,
                  const char *strip_path_prefix = "") override;

 private:
  // Keeps track of the modules that have been rendered to avoid re-rendering
  // them
  InternalMmapVector<RenderedModule> renderedModules_;
  void RenderContext(InternalScopedString *buffer);

 protected:
  ~MarkupStackTracePrinter() {}
};

class MarkupSymbolizerTool final : public SymbolizerTool {
 public:
  // This is used in some places for suppression checking, which we
  // don't really support for Fuchsia.  It's also used in UBSan to
  // identify a PC location to a function name, so we always fill in
  // the function member with a string containing markup around the PC
  // value.
  // TODO(mcgrathr): Under SANITIZER_GO, it's currently used by TSan
  // to render stack frames, but that should be changed to use
  // RenderStackFrame.
  bool SymbolizePC(uptr addr, SymbolizedStack *stack) override;

  // Always claim we succeeded, so that RenderDataInfo will be called.
  bool SymbolizeData(uptr addr, DataInfo *info) override;

  // May return NULL if demangling failed.
  // This is used by UBSan for type names, and by ASan for global variable
  // names. It's expected to return a static buffer that will be reused on each
  // call.
  const char *Demangle(const char *name) override;
};

}  // namespace __sanitizer

#endif  // SANITIZER_SYMBOLIZER_MARKUP_H
