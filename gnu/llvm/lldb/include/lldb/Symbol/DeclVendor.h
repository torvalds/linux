//===-- DeclVendor.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SYMBOL_DECLVENDOR_H
#define LLDB_SYMBOL_DECLVENDOR_H

#include "lldb/lldb-defines.h"

#include <vector>

namespace lldb_private {

// The Decl vendor class is intended as a generic interface to search for named
// declarations that are not necessarily backed by a specific symbol file.
class DeclVendor {
public:
  enum DeclVendorKind {
    eClangDeclVendor,
    eClangModuleDeclVendor,
    eAppleObjCDeclVendor,
    eLastClangDeclVendor,
  };
  // Constructors and Destructors
  DeclVendor(DeclVendorKind kind) : m_kind(kind) {}

  virtual ~DeclVendor() = default;

  DeclVendorKind GetKind() const { return m_kind; }

  /// Look up the set of Decls that the DeclVendor currently knows about
  /// matching a given name.
  ///
  /// \param[in] name
  ///     The name to look for.
  ///
  /// \param[in] append
  ///     If true, FindDecls will clear "decls" when it starts.
  ///
  /// \param[in] max_matches
  ///     The maximum number of Decls to return.  UINT32_MAX means "as
  ///     many as possible."
  ///
  /// \return
  ///     The number of Decls added to decls; will not exceed
  ///     max_matches.
  virtual uint32_t FindDecls(ConstString name, bool append,
                             uint32_t max_matches,
                             std::vector<CompilerDecl> &decls) = 0;

  /// Look up the types that the DeclVendor currently knows about matching a
  /// given name.
  ///
  /// \param[in] name
  ///     The name to look for.
  ///
  /// \param[in] max_matches
  //      The maximum number of matches. UINT32_MAX means "as many as possible".
  ///
  /// \return
  ///     The vector of CompilerTypes that was found.
  std::vector<CompilerType> FindTypes(ConstString name, uint32_t max_matches);

private:
  // For DeclVendor only
  DeclVendor(const DeclVendor &) = delete;
  const DeclVendor &operator=(const DeclVendor &) = delete;

  const DeclVendorKind m_kind;
};

} // namespace lldb_private

#endif
