//===-- llvm/Support/PluginLoader.h - Plugin Loader for Tools ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A tool can #include this file to get a -load option that allows the user to
// load arbitrary shared objects into the tool's address space.  Note that this
// header can only be included by a program ONCE, so it should never to used by
// library authors.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_PLUGINLOADER_H
#define LLVM_SUPPORT_PLUGINLOADER_H

#ifndef DONT_GET_PLUGIN_LOADER_OPTION
#include "llvm/Support/CommandLine.h"
#endif

#include <string>

namespace llvm {
  struct PluginLoader {
    void operator=(const std::string &Filename);
    static unsigned getNumPlugins();
    static std::string& getPlugin(unsigned num);
  };

#ifndef DONT_GET_PLUGIN_LOADER_OPTION
  // This causes operator= above to be invoked for every -load option.
  static cl::opt<PluginLoader, false, cl::parser<std::string>>
      LoadOpt("load", cl::value_desc("pluginfilename"),
              cl::desc("Load the specified plugin"));
#endif
}

#endif
