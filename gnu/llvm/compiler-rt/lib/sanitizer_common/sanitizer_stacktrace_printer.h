//===-- sanitizer_stacktrace_printer.h --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is shared between sanitizers' run-time libraries.
//
//===----------------------------------------------------------------------===//
#ifndef SANITIZER_STACKTRACE_PRINTER_H
#define SANITIZER_STACKTRACE_PRINTER_H

#include "sanitizer_common.h"
#include "sanitizer_internal_defs.h"
#include "sanitizer_symbolizer.h"

namespace __sanitizer {

// StacktracePrinter is an interface that is implemented by
// classes that can perform rendering of the different parts
// of a stacktrace.
class StackTracePrinter {
 public:
  static StackTracePrinter *GetOrInit();

  // Strip interceptor prefixes from function name.
  const char *StripFunctionName(const char *function);

  virtual void RenderFrame(InternalScopedString *buffer, const char *format,
                           int frame_no, uptr address, const AddressInfo *info,
                           bool vs_style, const char *strip_path_prefix = "") {
    // Should be pure virtual, but we can't depend on __cxa_pure_virtual.
    UNIMPLEMENTED();
  }

  virtual bool RenderNeedsSymbolization(const char *format) {
    // Should be pure virtual, but we can't depend on __cxa_pure_virtual.
    UNIMPLEMENTED();
  }

  void RenderSourceLocation(InternalScopedString *buffer, const char *file,
                            int line, int column, bool vs_style,
                            const char *strip_path_prefix);

  void RenderModuleLocation(InternalScopedString *buffer, const char *module,
                            uptr offset, ModuleArch arch,
                            const char *strip_path_prefix);
  virtual void RenderData(InternalScopedString *buffer, const char *format,
                          const DataInfo *DI,
                          const char *strip_path_prefix = "") {
    // Should be pure virtual, but we can't depend on __cxa_pure_virtual.
    UNIMPLEMENTED();
  }

 private:
  // To be called from StackTracePrinter::GetOrInit
  static StackTracePrinter *NewStackTracePrinter();

 protected:
  ~StackTracePrinter() {}
};

class FormattedStackTracePrinter : public StackTracePrinter {
 public:
  // Render the contents of "info" structure, which represents the contents of
  // stack frame "frame_no" and appends it to the "buffer". "format" is a
  // string with placeholders, which is copied to the output with
  // placeholders substituted with the contents of "info". For example,
  // format string
  //   "  frame %n: function %F at %S"
  // will be turned into
  //   "  frame 10: function foo::bar() at my/file.cc:10"
  // You may additionally pass "strip_path_prefix" to strip prefixes of paths to
  // source files and modules.
  // Here's the full list of available placeholders:
  //   %% - represents a '%' character;
  //   %n - frame number (copy of frame_no);
  //   %p - PC in hex format;
  //   %m - path to module (binary or shared object);
  //   %o - offset in the module in hex format;
  //   %f - function name;
  //   %q - offset in the function in hex format (*if available*);
  //   %s - path to source file;
  //   %l - line in the source file;
  //   %c - column in the source file;
  //   %F - if function is known to be <foo>, prints "in <foo>", possibly
  //        followed by the offset in this function, but only if source file
  //        is unknown;
  //   %S - prints file/line/column information;
  //   %L - prints location information: file/line/column, if it is known, or
  //        module+offset if it is known, or (<unknown module>) string.
  //   %M - prints module basename and offset, if it is known, or PC.
  void RenderFrame(InternalScopedString *buffer, const char *format,
                   int frame_no, uptr address, const AddressInfo *info,
                   bool vs_style, const char *strip_path_prefix = "") override;

  bool RenderNeedsSymbolization(const char *format) override;

  // Same as RenderFrame, but for data section (global variables).
  // Accepts %s, %l from above.
  // Also accepts:
  //   %g - name of the global variable.
  void RenderData(InternalScopedString *buffer, const char *format,
                  const DataInfo *DI,
                  const char *strip_path_prefix = "") override;

 protected:
  ~FormattedStackTracePrinter() {}
};

}  // namespace __sanitizer

#endif  // SANITIZER_STACKTRACE_PRINTER_H
