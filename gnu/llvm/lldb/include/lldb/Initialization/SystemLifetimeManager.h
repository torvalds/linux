//===-- SystemLifetimeManager.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INITIALIZATION_SYSTEMLIFETIMEMANAGER_H
#define LLDB_INITIALIZATION_SYSTEMLIFETIMEMANAGER_H

#include "lldb/Initialization/SystemInitializer.h"
#include "lldb/lldb-private-types.h"
#include "llvm/Support/Error.h"

#include <memory>
#include <mutex>

namespace lldb_private {

class SystemLifetimeManager {
public:
  SystemLifetimeManager();
  ~SystemLifetimeManager();

  llvm::Error Initialize(std::unique_ptr<SystemInitializer> initializer,
                         LoadPluginCallbackType plugin_callback);
  void Terminate();

private:
  std::recursive_mutex m_mutex;
  std::unique_ptr<SystemInitializer> m_initializer;
  bool m_initialized = false;

  // Noncopyable.
  SystemLifetimeManager(const SystemLifetimeManager &other) = delete;
  SystemLifetimeManager &operator=(const SystemLifetimeManager &other) = delete;
};
}

#endif
