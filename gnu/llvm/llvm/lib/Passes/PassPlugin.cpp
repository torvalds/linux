//===- lib/Passes/PassPluginLoader.cpp - Load Plugins for New PM Passes ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdint>

using namespace llvm;

Expected<PassPlugin> PassPlugin::Load(const std::string &Filename) {
  std::string Error;
  auto Library =
      sys::DynamicLibrary::getPermanentLibrary(Filename.c_str(), &Error);
  if (!Library.isValid())
    return make_error<StringError>(Twine("Could not load library '") +
                                       Filename + "': " + Error,
                                   inconvertibleErrorCode());

  PassPlugin P{Filename, Library};

  // llvmGetPassPluginInfo should be resolved to the definition from the plugin
  // we are currently loading.
  intptr_t getDetailsFn =
      (intptr_t)Library.getAddressOfSymbol("llvmGetPassPluginInfo");

  if (!getDetailsFn)
    // If the symbol isn't found, this is probably a legacy plugin, which is an
    // error
    return make_error<StringError>(Twine("Plugin entry point not found in '") +
                                       Filename + "'. Is this a legacy plugin?",
                                   inconvertibleErrorCode());

  P.Info = reinterpret_cast<decltype(llvmGetPassPluginInfo) *>(getDetailsFn)();

  if (P.Info.APIVersion != LLVM_PLUGIN_API_VERSION)
    return make_error<StringError>(
        Twine("Wrong API version on plugin '") + Filename + "'. Got version " +
            Twine(P.Info.APIVersion) + ", supported version is " +
            Twine(LLVM_PLUGIN_API_VERSION) + ".",
        inconvertibleErrorCode());

  if (!P.Info.RegisterPassBuilderCallbacks)
    return make_error<StringError>(Twine("Empty entry callback in plugin '") +
                                       Filename + "'.'",
                                   inconvertibleErrorCode());

  return P;
}
