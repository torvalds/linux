//===-- ScriptedMetadata.h ------------------------------------ -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INTERPRETER_SCRIPTEDMETADATA_H
#define LLDB_INTERPRETER_SCRIPTEDMETADATA_H

#include "lldb/Utility/ProcessInfo.h"
#include "lldb/Utility/StructuredData.h"

namespace lldb_private {
class ScriptedMetadata {
public:
  ScriptedMetadata(llvm::StringRef class_name,
                   StructuredData::DictionarySP dict_sp)
      : m_class_name(class_name.data()), m_args_sp(dict_sp) {}

  ScriptedMetadata(const ProcessInfo &process_info) {
    lldb::ScriptedMetadataSP metadata_sp = process_info.GetScriptedMetadata();
    if (metadata_sp) {
      m_class_name = metadata_sp->GetClassName();
      m_args_sp = metadata_sp->GetArgsSP();
    }
  }

  explicit operator bool() const { return !m_class_name.empty(); }

  llvm::StringRef GetClassName() const { return m_class_name; }
  StructuredData::DictionarySP GetArgsSP() const { return m_args_sp; }

private:
  std::string m_class_name;
  StructuredData::DictionarySP m_args_sp;
};
} // namespace lldb_private

#endif // LLDB_INTERPRETER_SCRIPTEDMETADATA_H
