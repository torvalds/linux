//===-- ClangExpressionSourceCode.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGEXPRESSIONSOURCECODE_H
#define LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGEXPRESSIONSOURCECODE_H

#include "lldb/Expression/Expression.h"
#include "lldb/Expression/ExpressionSourceCode.h"
#include "lldb/lldb-enumerations.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"

#include <string>

namespace lldb_private {

class ExecutionContext;

class ClangExpressionSourceCode : public ExpressionSourceCode {
public:
  /// The file name we use for the wrapper code that we inject before
  /// the user expression.
  static const llvm::StringRef g_prefix_file_name;
  static const char *g_expression_prefix;
  static const char *g_expression_suffix;

  /// The possible ways an expression can be wrapped.
  enum class WrapKind {
    /// Wrapped in a non-static member function of a C++ class.
    CppMemberFunction,
    /// Wrapped in an instance Objective-C method.
    ObjCInstanceMethod,
    /// Wrapped in a static Objective-C method.
    ObjCStaticMethod,
    /// Wrapped in a non-member function.
    /// Note that this is also used for static member functions of a C++ class.
    Function
  };

  static ClangExpressionSourceCode *CreateWrapped(llvm::StringRef filename,
                                                  llvm::StringRef prefix,
                                                  llvm::StringRef body,
                                                  WrapKind wrap_kind) {
    return new ClangExpressionSourceCode(filename, "$__lldb_expr", prefix, body,
                                         Wrap, wrap_kind);
  }

  /// Generates the source code that will evaluate the expression.
  ///
  /// \param text output parameter containing the source code string.
  /// \param exe_ctx The execution context in which the expression will be
  ///        evaluated.
  /// \param add_locals True iff local variables should be injected into the
  ///        expression source code.
  /// \param force_add_all_locals True iff all local variables should be
  ///        injected even if they are not used in the expression.
  /// \param modules A list of (C++) modules that the expression should import.
  ///
  /// \return true iff the source code was successfully generated.
  bool GetText(std::string &text, ExecutionContext &exe_ctx, bool add_locals,
               bool force_add_all_locals,
               llvm::ArrayRef<std::string> modules) const;

  // Given a string returned by GetText, find the beginning and end of the body
  // passed to CreateWrapped. Return true if the bounds could be found.  This
  // will also work on text with FixItHints applied.
  bool GetOriginalBodyBounds(std::string transformed_text,
                             size_t &start_loc, size_t &end_loc);

protected:
  ClangExpressionSourceCode(llvm::StringRef filename, llvm::StringRef name,
                            llvm::StringRef prefix, llvm::StringRef body,
                            Wrapping wrap, WrapKind wrap_kind);

private:
  /// Writes "using" declarations for local variables into the specified stream.
  ///
  /// Behaviour is undefined if 'frame == nullptr'.
  ///
  /// \param[out] stream Stream that this function generates "using"
  ///             declarations into.
  ///
  /// \param[in]  expr Expression source that we're evaluating.
  ///
  /// \param[in]  frame StackFrame which carries information about the local
  ///             variables that we're generating "using" declarations for.
  void AddLocalVariableDecls(StreamString &stream, const std::string &expr,
                             StackFrame *frame) const;

  /// String marking the start of the user expression.
  std::string m_start_marker;
  /// String marking the end of the user expression.
  std::string m_end_marker;
  /// How the expression has been wrapped.
  const WrapKind m_wrap_kind;
};

} // namespace lldb_private

#endif
