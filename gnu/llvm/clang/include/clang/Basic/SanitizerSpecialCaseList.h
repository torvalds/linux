//===--- SanitizerSpecialCaseList.h - SCL for sanitizers --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// An extension of SpecialCaseList to allowing querying sections by
// SanitizerMask.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_SANITIZERSPECIALCASELIST_H
#define LLVM_CLANG_BASIC_SANITIZERSPECIALCASELIST_H

#include "clang/Basic/LLVM.h"
#include "clang/Basic/Sanitizers.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/SpecialCaseList.h"
#include <memory>
#include <vector>

namespace llvm {
namespace vfs {
class FileSystem;
}
} // namespace llvm

namespace clang {

class SanitizerSpecialCaseList : public llvm::SpecialCaseList {
public:
  static std::unique_ptr<SanitizerSpecialCaseList>
  create(const std::vector<std::string> &Paths, llvm::vfs::FileSystem &VFS,
         std::string &Error);

  static std::unique_ptr<SanitizerSpecialCaseList>
  createOrDie(const std::vector<std::string> &Paths,
              llvm::vfs::FileSystem &VFS);

  // Query ignorelisted entries if any bit in Mask matches the entry's section.
  bool inSection(SanitizerMask Mask, StringRef Prefix, StringRef Query,
                 StringRef Category = StringRef()) const;

protected:
  // Initialize SanitizerSections.
  void createSanitizerSections();

  struct SanitizerSection {
    SanitizerSection(SanitizerMask SM, SectionEntries &E)
        : Mask(SM), Entries(E){};

    SanitizerMask Mask;
    SectionEntries &Entries;
  };

  std::vector<SanitizerSection> SanitizerSections;
};

} // end namespace clang

#endif
