//===-- SBValueList.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBVALUELIST_H
#define LLDB_API_SBVALUELIST_H

#include "lldb/API/SBDefines.h"

class ValueListImpl;

namespace lldb {

class LLDB_API SBValueList {
public:
  SBValueList();

  SBValueList(const lldb::SBValueList &rhs);

  ~SBValueList();

  explicit operator bool() const;

  bool IsValid() const;

  void Clear();

  void Append(const lldb::SBValue &val_obj);

  void Append(const lldb::SBValueList &value_list);

  uint32_t GetSize() const;

  lldb::SBValue GetValueAtIndex(uint32_t idx) const;

  lldb::SBValue GetFirstValueByName(const char *name) const;

  lldb::SBValue FindValueObjectByUID(lldb::user_id_t uid);

  const lldb::SBValueList &operator=(const lldb::SBValueList &rhs);

  // Get an error for why this list is empty.
  //
  // If this list is empty, check for an underlying error in the debug
  // information that prevented this list from being populated. This is not
  // meant to return an error if there is no debug information as it is ok for a
  // value list to be empty and no error should be returned in that case. If the
  // debug info is for an assembly file or language that doesn't have any
  // variables, no error should be returned.
  //
  // This is designed as a way to let users know when they enable certain
  // compiler options that enable debug information but provide a degraded
  // debug information content, like -gline-tables-only, which is a compiler
  // option that allows users to set file and line breakpoints, but users get
  // confused when no variables show up during debugging.
  //
  // It is also designed to inform a user that debug information might be
  // available if an external file, like a .dwo file, but that file doesn't
  // exist or wasn't able to be loaded due to a mismatched ID. When debugging
  // with fission enabled, the line tables are linked into the main executable,
  // but if the .dwo or .dwp files are not available or have been modified,
  // users can get confused if they can stop at a file and line breakpoint but
  // can't see variables in this case.
  //
  // This error can give vital clues to the user about the cause is and allow
  // the user to fix the issue.
  lldb::SBError GetError();

protected:
  // only useful for visualizing the pointer or comparing two SBValueLists to
  // see if they are backed by the same underlying Impl.
  void *opaque_ptr();

private:
  friend class SBFrame;

  SBValueList(const ValueListImpl *lldb_object_ptr);

  void Append(lldb::ValueObjectSP &val_obj_sp);

  void CreateIfNeeded();

  ValueListImpl *operator->();

  ValueListImpl &operator*();

  const ValueListImpl *operator->() const;

  const ValueListImpl &operator*() const;

  ValueListImpl &ref();

  std::unique_ptr<ValueListImpl> m_opaque_up;

  void SetError(const lldb_private::Status &status);
};

} // namespace lldb

#endif // LLDB_API_SBVALUELIST_H
