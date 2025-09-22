//===-- SBModuleSpec.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBMODULESPEC_H
#define LLDB_API_SBMODULESPEC_H

#include "lldb/API/SBDefines.h"
#include "lldb/API/SBFileSpec.h"

namespace lldb {

class LLDB_API SBModuleSpec {
public:
  SBModuleSpec();

  SBModuleSpec(const SBModuleSpec &rhs);

  ~SBModuleSpec();

  const SBModuleSpec &operator=(const SBModuleSpec &rhs);

  explicit operator bool() const;

  bool IsValid() const;

  void Clear();

  /// Get const accessor for the module file.
  ///
  /// This function returns the file for the module on the host system
  /// that is running LLDB. This can differ from the path on the
  /// platform since we might be doing remote debugging.
  ///
  /// \return
  ///     A const reference to the file specification object.
  lldb::SBFileSpec GetFileSpec();

  void SetFileSpec(const lldb::SBFileSpec &fspec);

  /// Get accessor for the module platform file.
  ///
  /// Platform file refers to the path of the module as it is known on
  /// the remote system on which it is being debugged. For local
  /// debugging this is always the same as Module::GetFileSpec(). But
  /// remote debugging might mention a file '/usr/lib/liba.dylib'
  /// which might be locally downloaded and cached. In this case the
  /// platform file could be something like:
  /// '/tmp/lldb/platform-cache/remote.host.computer/usr/lib/liba.dylib'
  /// The file could also be cached in a local developer kit directory.
  ///
  /// \return
  ///     A const reference to the file specification object.
  lldb::SBFileSpec GetPlatformFileSpec();

  void SetPlatformFileSpec(const lldb::SBFileSpec &fspec);

  lldb::SBFileSpec GetSymbolFileSpec();

  void SetSymbolFileSpec(const lldb::SBFileSpec &fspec);

  const char *GetObjectName();

  void SetObjectName(const char *name);

  const char *GetTriple();

  void SetTriple(const char *triple);

  const uint8_t *GetUUIDBytes();

  size_t GetUUIDLength();

  bool SetUUIDBytes(const uint8_t *uuid, size_t uuid_len);

  uint64_t GetObjectOffset();

  void SetObjectOffset(uint64_t object_offset);

  uint64_t GetObjectSize();

  void SetObjectSize(uint64_t object_size);

  bool GetDescription(lldb::SBStream &description);

private:
  friend class SBModuleSpecList;
  friend class SBModule;
  friend class SBPlatform;
  friend class SBTarget;

  SBModuleSpec(const lldb_private::ModuleSpec &module_spec);

  std::unique_ptr<lldb_private::ModuleSpec> m_opaque_up;
};

class SBModuleSpecList {
public:
  SBModuleSpecList();

  SBModuleSpecList(const SBModuleSpecList &rhs);

  ~SBModuleSpecList();

  SBModuleSpecList &operator=(const SBModuleSpecList &rhs);

  static SBModuleSpecList GetModuleSpecifications(const char *path);

  void Append(const SBModuleSpec &spec);

  void Append(const SBModuleSpecList &spec_list);

  SBModuleSpec FindFirstMatchingSpec(const SBModuleSpec &match_spec);

  SBModuleSpecList FindMatchingSpecs(const SBModuleSpec &match_spec);

  size_t GetSize();

  SBModuleSpec GetSpecAtIndex(size_t i);

  bool GetDescription(lldb::SBStream &description);

private:
  std::unique_ptr<lldb_private::ModuleSpecList> m_opaque_up;
};

} // namespace lldb

#endif // LLDB_API_SBMODULESPEC_H
