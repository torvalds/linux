//===-- ModuleDependencyCollector.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_MODULEDEPENDENCYCOLLECTOR_H
#define LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_MODULEDEPENDENCYCOLLECTOR_H

#include "clang/Frontend/Utils.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileCollector.h"

namespace lldb_private {
class ModuleDependencyCollectorAdaptor
    : public clang::ModuleDependencyCollector {
public:
  ModuleDependencyCollectorAdaptor(
      std::shared_ptr<llvm::FileCollectorBase> file_collector)
      : clang::ModuleDependencyCollector(""), m_file_collector(file_collector) {
  }

  void addFile(llvm::StringRef Filename,
               llvm::StringRef FileDst = {}) override {
    if (m_file_collector)
      m_file_collector->addFile(Filename);
  }

  bool insertSeen(llvm::StringRef Filename) override { return false; }
  void addFileMapping(llvm::StringRef VPath, llvm::StringRef RPath) override {}
  void writeFileMap() override {}

private:
  std::shared_ptr<llvm::FileCollectorBase> m_file_collector;
};
} // namespace lldb_private

#endif
