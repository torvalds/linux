//===-- PluginInterface.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_PLUGININTERFACE_H
#define LLDB_CORE_PLUGININTERFACE_H

#include "llvm/ADT/StringRef.h"

namespace lldb_private {

class PluginInterface {
public:
  PluginInterface() = default;
  virtual ~PluginInterface() = default;

  virtual llvm::StringRef GetPluginName() = 0;

  PluginInterface(const PluginInterface &) = delete;
  PluginInterface &operator=(const PluginInterface &) = delete;
};

} // namespace lldb_private

#endif // LLDB_CORE_PLUGININTERFACE_H
