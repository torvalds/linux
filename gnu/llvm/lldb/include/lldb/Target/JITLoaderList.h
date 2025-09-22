//===-- JITLoaderList.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_JITLOADERLIST_H
#define LLDB_TARGET_JITLOADERLIST_H

#include <mutex>
#include <vector>

#include "lldb/lldb-forward.h"

namespace lldb_private {

/// \class JITLoaderList JITLoaderList.h "lldb/Target/JITLoaderList.h"
///
/// Class used by the Process to hold a list of its JITLoaders.
class JITLoaderList {
public:
  JITLoaderList();
  ~JITLoaderList();

  void Append(const lldb::JITLoaderSP &jit_loader_sp);

  void Remove(const lldb::JITLoaderSP &jit_loader_sp);

  size_t GetSize() const;

  lldb::JITLoaderSP GetLoaderAtIndex(size_t idx);

  void DidLaunch();

  void DidAttach();

  void ModulesDidLoad(ModuleList &module_list);

private:
  std::vector<lldb::JITLoaderSP> m_jit_loaders_vec;
  std::recursive_mutex m_jit_loaders_mutex;
};

} // namespace lldb_private

#endif // LLDB_TARGET_JITLOADERLIST_H
