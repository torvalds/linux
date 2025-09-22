//===- WithColor.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_WITHCOLOR_H
#define LLVM_SUPPORT_WITHCOLOR_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

class Error;
class StringRef;

namespace cl {
class OptionCategory;
}

extern cl::OptionCategory &getColorCategory();

// Symbolic names for various syntax elements.
enum class HighlightColor {
  Address,
  String,
  Tag,
  Attribute,
  Enumerator,
  Macro,
  Error,
  Warning,
  Note,
  Remark
};

enum class ColorMode {
  /// Determine whether to use color based on the command line argument and the
  /// raw_ostream.
  Auto,
  /// Enable colors. Because raw_ostream is the one implementing colors, this
  /// has no effect if the stream does not support colors or has colors
  /// disabled.
  Enable,
  /// Disable colors.
  Disable,
};

/// An RAII object that temporarily switches an output stream to a specific
/// color.
class WithColor {
public:
  using AutoDetectFunctionType = bool (*)(const raw_ostream &OS);

  /// To be used like this: WithColor(OS, HighlightColor::String) << "text";
  /// @param OS The output stream
  /// @param S Symbolic name for syntax element to color
  /// @param Mode Enable, disable or compute whether to use colors.
  LLVM_CTOR_NODISCARD WithColor(raw_ostream &OS, HighlightColor S,
                                ColorMode Mode = ColorMode::Auto);
  /// To be used like this: WithColor(OS, raw_ostream::BLACK) << "text";
  /// @param OS The output stream
  /// @param Color ANSI color to use, the special SAVEDCOLOR can be used to
  /// change only the bold attribute, and keep colors untouched
  /// @param Bold Bold/brighter text, default false
  /// @param BG If true, change the background, default: change foreground
  /// @param Mode Enable, disable or compute whether to use colors.
  LLVM_CTOR_NODISCARD WithColor(
      raw_ostream &OS, raw_ostream::Colors Color = raw_ostream::SAVEDCOLOR,
      bool Bold = false, bool BG = false, ColorMode Mode = ColorMode::Auto)
      : OS(OS), Mode(Mode) {
    changeColor(Color, Bold, BG);
  }
  ~WithColor();

  raw_ostream &get() { return OS; }
  operator raw_ostream &() { return OS; }
  template <typename T> WithColor &operator<<(T &O) {
    OS << O;
    return *this;
  }
  template <typename T> WithColor &operator<<(const T &O) {
    OS << O;
    return *this;
  }

  /// Convenience method for printing "error: " to stderr.
  static raw_ostream &error();
  /// Convenience method for printing "warning: " to stderr.
  static raw_ostream &warning();
  /// Convenience method for printing "note: " to stderr.
  static raw_ostream &note();
  /// Convenience method for printing "remark: " to stderr.
  static raw_ostream &remark();

  /// Convenience method for printing "error: " to the given stream.
  static raw_ostream &error(raw_ostream &OS, StringRef Prefix = "",
                            bool DisableColors = false);
  /// Convenience method for printing "warning: " to the given stream.
  static raw_ostream &warning(raw_ostream &OS, StringRef Prefix = "",
                              bool DisableColors = false);
  /// Convenience method for printing "note: " to the given stream.
  static raw_ostream &note(raw_ostream &OS, StringRef Prefix = "",
                           bool DisableColors = false);
  /// Convenience method for printing "remark: " to the given stream.
  static raw_ostream &remark(raw_ostream &OS, StringRef Prefix = "",
                             bool DisableColors = false);

  /// Determine whether colors are displayed.
  bool colorsEnabled();

  /// Change the color of text that will be output from this point forward.
  /// @param Color ANSI color to use, the special SAVEDCOLOR can be used to
  /// change only the bold attribute, and keep colors untouched
  /// @param Bold Bold/brighter text, default false
  /// @param BG If true, change the background, default: change foreground
  WithColor &changeColor(raw_ostream::Colors Color, bool Bold = false,
                         bool BG = false);

  /// Reset the colors to terminal defaults. Call this when you are done
  /// outputting colored text, or before program exit.
  WithColor &resetColor();

  /// Implement default handling for Error.
  /// Print "error: " to stderr.
  static void defaultErrorHandler(Error Err);

  /// Implement default handling for Warning.
  /// Print "warning: " to stderr.
  static void defaultWarningHandler(Error Warning);

  /// Retrieve the default color auto detection function.
  static AutoDetectFunctionType defaultAutoDetectFunction();

  /// Change the global auto detection function.
  static void
  setAutoDetectFunction(AutoDetectFunctionType NewAutoDetectFunction);

private:
  raw_ostream &OS;
  ColorMode Mode;

  static AutoDetectFunctionType AutoDetectFunction;
};

} // end namespace llvm

#endif // LLVM_SUPPORT_WITHCOLOR_H
