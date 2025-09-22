//===-- ResourceProcessor.h -------------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//

#ifndef LLVM_INCLUDE_LLVM_SUPPORT_WINDOWS_RESOURCE_PROCESSOR_H
#define LLVM_INCLUDE_LLVM_SUPPORT_WINDOWS_RESOURCE_PROCESSOR_H

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"

#include <memory>
#include <vector>


namespace llvm {

class WindowsResourceProcessor {
public:
  using PathType = SmallVector<char, 64>;

  WindowsResourceProcessor() {}

  void addDefine(StringRef Key, StringRef Value = StringRef()) {
    PreprocessorDefines.emplace_back(Key, Value);
  }
  void addInclude(const PathType &IncludePath) {
    IncludeList.push_back(IncludePath);
  }
  void setVerbose(bool Verbose) { IsVerbose = Verbose; }
  void setNullAtEnd(bool NullAtEnd) { AppendNull = NullAtEnd; }

  Error process(StringRef InputData,
    std::unique_ptr<raw_fd_ostream> OutputStream);

private:
  StringRef InputData;
  std::vector<PathType> IncludeList;
  std::vector<std::pair<StringRef, StringRef>> PreprocessorDefines;
  bool IsVerbose, AppendNull;
};

}

#endif
