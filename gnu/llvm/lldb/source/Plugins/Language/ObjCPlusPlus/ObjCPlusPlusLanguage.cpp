//===-- ObjCPlusPlusLanguage.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ObjCPlusPlusLanguage.h"

#include "lldb/Core/PluginManager.h"
#include "lldb/Utility/ConstString.h"

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE(ObjCPlusPlusLanguage)

bool ObjCPlusPlusLanguage::IsSourceFile(llvm::StringRef file_path) const {
  const auto suffixes = {".h", ".mm"};
  for (auto suffix : suffixes) {
    if (file_path.ends_with_insensitive(suffix))
      return true;
  }
  return false;
}

void ObjCPlusPlusLanguage::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(), "Objective-C++ Language",
                                CreateInstance);
}

void ObjCPlusPlusLanguage::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

// Static Functions
Language *ObjCPlusPlusLanguage::CreateInstance(lldb::LanguageType language) {
  switch (language) {
  case lldb::eLanguageTypeObjC_plus_plus:
    return new ObjCPlusPlusLanguage();
  default:
    return nullptr;
  }
}
