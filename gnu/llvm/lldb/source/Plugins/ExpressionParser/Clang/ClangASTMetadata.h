//===-- ClangASTMetadata.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGASTMETADATA_H
#define LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGASTMETADATA_H

#include "lldb/Core/dwarf.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"

namespace lldb_private {

class ClangASTMetadata {
public:
  ClangASTMetadata()
      : m_user_id(0), m_union_is_user_id(false), m_union_is_isa_ptr(false),
        m_has_object_ptr(false), m_is_self(false), m_is_dynamic_cxx(true),
        m_is_forcefully_completed(false) {}

  bool GetIsDynamicCXXType() const { return m_is_dynamic_cxx; }

  void SetIsDynamicCXXType(bool b) { m_is_dynamic_cxx = b; }

  void SetUserID(lldb::user_id_t user_id) {
    m_user_id = user_id;
    m_union_is_user_id = true;
    m_union_is_isa_ptr = false;
  }

  lldb::user_id_t GetUserID() const {
    if (m_union_is_user_id)
      return m_user_id;
    else
      return LLDB_INVALID_UID;
  }

  void SetISAPtr(uint64_t isa_ptr) {
    m_isa_ptr = isa_ptr;
    m_union_is_user_id = false;
    m_union_is_isa_ptr = true;
  }

  uint64_t GetISAPtr() const {
    if (m_union_is_isa_ptr)
      return m_isa_ptr;
    else
      return 0;
  }

  void SetObjectPtrName(const char *name) {
    m_has_object_ptr = true;
    if (strcmp(name, "self") == 0)
      m_is_self = true;
    else if (strcmp(name, "this") == 0)
      m_is_self = false;
    else
      m_has_object_ptr = false;
  }

  lldb::LanguageType GetObjectPtrLanguage() const {
    if (m_has_object_ptr) {
      if (m_is_self)
        return lldb::eLanguageTypeObjC;
      else
        return lldb::eLanguageTypeC_plus_plus;
    }
    return lldb::eLanguageTypeUnknown;
  }

  const char *GetObjectPtrName() const {
    if (m_has_object_ptr) {
      if (m_is_self)
        return "self";
      else
        return "this";
    } else
      return nullptr;
  }

  bool HasObjectPtr() const { return m_has_object_ptr; }

  /// A type is "forcefully completed" if it was declared complete to satisfy an
  /// AST invariant (e.g. base classes must be complete types), but in fact we
  /// were not able to find a actual definition for it.
  bool IsForcefullyCompleted() const { return m_is_forcefully_completed; }

  void SetIsForcefullyCompleted(bool value = true) {
    m_is_forcefully_completed = true;
  }

  void Dump(Stream *s);

private:
  union {
    lldb::user_id_t m_user_id;
    uint64_t m_isa_ptr;
  };

  bool m_union_is_user_id : 1, m_union_is_isa_ptr : 1, m_has_object_ptr : 1,
      m_is_self : 1, m_is_dynamic_cxx : 1, m_is_forcefully_completed : 1;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGASTMETADATA_H
