//===-- RichManglingContext.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_RICHMANGLINGCONTEXT_H
#define LLDB_CORE_RICHMANGLINGCONTEXT_H

#include "lldb/lldb-forward.h"
#include "lldb/lldb-private.h"

#include "lldb/Utility/ConstString.h"

#include "llvm/ADT/Any.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Demangle/Demangle.h"

namespace lldb_private {

/// Uniform wrapper for access to rich mangling information from different
/// providers. See Mangled::DemangleWithRichManglingInfo()
class RichManglingContext {
public:
  RichManglingContext() {
    m_ipd_buf = static_cast<char *>(std::malloc(m_ipd_buf_size));
    m_ipd_buf[0] = '\0';
  }

  ~RichManglingContext();

  /// Use the ItaniumPartialDemangler to obtain rich mangling information from
  /// the given mangled name.
  bool FromItaniumName(ConstString mangled);

  /// Use the legacy language parser implementation to obtain rich mangling
  /// information from the given demangled name.
  bool FromCxxMethodName(ConstString demangled);

  /// If this symbol describes a constructor or destructor.
  bool IsCtorOrDtor() const;

  /// Get the base name of a function. This doesn't include trailing template
  /// arguments, ie "a::b<int>" gives "b".
  llvm::StringRef ParseFunctionBaseName();

  /// Get the context name for a function. For "a::b::c", this function returns
  /// "a::b".
  llvm::StringRef ParseFunctionDeclContextName();

  /// Get the entire demangled name.
  llvm::StringRef ParseFullName();

private:
  enum InfoProvider { None, ItaniumPartialDemangler, PluginCxxLanguage };

  /// Selects the rich mangling info provider.
  InfoProvider m_provider = None;

  /// Members for ItaniumPartialDemangler
  llvm::ItaniumPartialDemangler m_ipd;
  /// Note: m_ipd_buf is a raw pointer due to being resized by realloc via
  /// ItaniumPartialDemangler. It should be managed with malloc/free, not
  /// new/delete.
  char *m_ipd_buf;
  size_t m_ipd_buf_size = 2048;

  /// Members for PluginCxxLanguage
  /// Cannot forward declare inner class CPlusPlusLanguage::MethodName. The
  /// respective header is in Plugins and including it from here causes cyclic
  /// dependency. Instead keep a llvm::Any and cast it on-access in the cpp.
  llvm::Any m_cxx_method_parser;

  /// Clean up memory when using PluginCxxLanguage
  void ResetCxxMethodParser();

  /// Clean up memory and set a new info provider for this instance.
  void ResetProvider(InfoProvider new_provider);

  /// Uniform handling of string buffers for ItaniumPartialDemangler.
  llvm::StringRef processIPDStrResult(char *ipd_res, size_t res_len);

  /// Cast the given parser to the given type. Ideally we would have a type
  /// trait to deduce \a ParserT from a given InfoProvider, but unfortunately we
  /// can't access CPlusPlusLanguage::MethodName from within the header.
  template <class ParserT> static ParserT *get(llvm::Any parser) {
    assert(parser.has_value());
    assert(llvm::any_cast<ParserT *>(&parser));
    return *llvm::any_cast<ParserT *>(&parser);
  }
};

} // namespace lldb_private

#endif
