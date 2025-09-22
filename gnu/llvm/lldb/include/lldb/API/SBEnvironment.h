//===-- SBEnvironment.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBENVIRONMENT_H
#define LLDB_API_SBENVIRONMENT_H

#include "lldb/API/SBDefines.h"

namespace lldb {

class LLDB_API SBEnvironment {
public:
  SBEnvironment();

  SBEnvironment(const lldb::SBEnvironment &rhs);

  ~SBEnvironment();

  const lldb::SBEnvironment &operator=(const lldb::SBEnvironment &rhs);

  /// Return the value of a given environment variable.
  ///
  /// \param [in] name
  ///     The name of the environment variable.
  ///
  /// \return
  ///     The value of the environment variable or null if not present.
  ///     If the environment variable has no value but is present, a valid
  ///     pointer to an empty string will be returned.
  const char *Get(const char *name);

  /// \return
  ///     The number of environment variables.
  size_t GetNumValues();

  /// Return the name of the environment variable at a given index from the
  /// internal list of environment variables.
  ///
  /// \param [in] index
  ///     The index of the environment variable in the internal list.
  ///
  /// \return
  ///     The name at the given index or null if the index is invalid.
  const char *GetNameAtIndex(size_t index);

  /// Return the value of the environment variable at a given index from the
  /// internal list of environment variables.
  ///
  /// \param [in] index
  ///     The index of the environment variable in the internal list.
  ///
  /// \return
  ///     The value at the given index or null if the index is invalid.
  ///     If the environment variable has no value but is present, a valid
  ///     pointer to an empty string will be returned.
  const char *GetValueAtIndex(size_t index);

  /// Return all environment variables contained in this object. Each variable
  /// is returned as a string with the following format
  ///     name=value
  ///
  /// \return
  ///     Return an lldb::SBStringList object with the environment variables.
  SBStringList GetEntries();

  /// Add or replace an existing environment variable. The input must be a
  /// string with the format
  ///     name=value
  ///
  /// \param [in] name_and_value
  ///     The entry to set which conforms to the format mentioned above.
  void PutEntry(const char *name_and_value);

  /// Update this object with the given environment variables. The input is a
  /// list of entries with the same format required by SBEnvironment::PutEntry.
  ///
  /// If append is false, the provided environment will replace the existing
  /// environment. Otherwise, existing values will be updated of left untouched
  /// accordingly.
  ///
  /// \param [in] entries
  ///     The environment variable entries.
  ///
  /// \param [in] append
  ///     Flag that controls whether to replace the existing environment.
  void SetEntries(const SBStringList &entries, bool append);

  /// Set the value of a given environment variable.
  /// If the variable exists, its value is updated only if overwrite is true.
  ///
  /// \param [in] name
  ///     The name of the environment variable to set.
  ///
  /// \param [in] value
  ///     The value of the environment variable to set.
  ///
  /// \param [in] overwrite
  ///     Flag that indicates whether to overwrite an existing environment
  ///     variable.
  ///
  /// \return
  ///     Return whether the variable was added or modified.
  bool Set(const char *name, const char *value, bool overwrite);

  /// Unset an environment variable if exists.
  ///
  /// \param [in] name
  ///     The name of the environment variable to unset.
  ///
  /// \return
  ///     Return whether a variable was actually unset.
  bool Unset(const char *name);

  /// Delete all the environment variables.
  void Clear();

protected:
  friend class SBPlatform;
  friend class SBTarget;
  friend class SBLaunchInfo;

  SBEnvironment(lldb_private::Environment rhs);

  lldb_private::Environment &ref() const;

private:
  std::unique_ptr<lldb_private::Environment> m_opaque_up;
};

} // namespace lldb

#endif // LLDB_API_SBENVIRONMENT_H
