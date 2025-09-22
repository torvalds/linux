//===-- SystemLifetimeManager.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Initialization/SystemLifetimeManager.h"

#include "lldb/Core/Debugger.h"
#include "lldb/Initialization/SystemInitializer.h"

#include <utility>

using namespace lldb_private;

SystemLifetimeManager::SystemLifetimeManager() : m_mutex() {}

SystemLifetimeManager::~SystemLifetimeManager() {
  assert(!m_initialized &&
         "SystemLifetimeManager destroyed without calling Terminate!");
}

llvm::Error SystemLifetimeManager::Initialize(
    std::unique_ptr<SystemInitializer> initializer,
    LoadPluginCallbackType plugin_callback) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  if (!m_initialized) {
    assert(!m_initializer && "Attempting to call "
                             "SystemLifetimeManager::Initialize() when it is "
                             "already initialized");
    m_initialized = true;
    m_initializer = std::move(initializer);

    if (auto e = m_initializer->Initialize())
      return e;

    Debugger::Initialize(plugin_callback);
  }

  return llvm::Error::success();
}

void SystemLifetimeManager::Terminate() {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  if (m_initialized) {
    Debugger::Terminate();
    m_initializer->Terminate();

    m_initializer.reset();
    m_initialized = false;
  }
}
