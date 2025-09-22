//===- ARMErrataFix.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_ELF_ARMA8ERRATAFIX_H
#define LLD_ELF_ARMA8ERRATAFIX_H

#include "lld/Common/LLVM.h"
#include "llvm/ADT/DenseMap.h"
#include <vector>

namespace lld::elf {

class Defined;
class InputSection;
class InputSectionDescription;
class Patch657417Section;

class ARMErr657417Patcher {
public:
  // Return true if Patches have been added to the OutputSections.
  bool createFixes();

private:
  std::vector<Patch657417Section *>
  patchInputSectionDescription(InputSectionDescription &isd);

  void insertPatches(InputSectionDescription &isd,
                     std::vector<Patch657417Section *> &patches);

  void init();

  // A cache of the mapping symbols defined by the InputSection sorted in order
  // of ascending value with redundant symbols removed. These describe
  // the ranges of code and data in an executable InputSection.
  llvm::DenseMap<InputSection *, std::vector<const Defined *>> sectionMap;

  bool initialized = false;
};

} // namespace lld::elf

#endif
