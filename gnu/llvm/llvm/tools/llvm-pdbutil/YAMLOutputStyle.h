//===- YAMLOutputStyle.h -------------------------------------- *- C++ --*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVMPDBDUMP_YAMLOUTPUTSTYLE_H
#define LLVM_TOOLS_LLVMPDBDUMP_YAMLOUTPUTSTYLE_H

#include "OutputStyle.h"
#include "PdbYaml.h"

#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/YAMLTraits.h"

namespace llvm {
namespace pdb {

class YAMLOutputStyle : public OutputStyle {
public:
  YAMLOutputStyle(PDBFile &File);

  Error dump() override;

private:
  Error dumpStringTable();
  Error dumpFileHeaders();
  Error dumpStreamMetadata();
  Error dumpStreamDirectory();
  Error dumpPDBStream();
  Error dumpDbiStream();
  Error dumpTpiStream();
  Error dumpIpiStream();
  Error dumpPublics();

  void flush();

  PDBFile &File;
  llvm::yaml::Output Out;

  yaml::PdbObject Obj;
};
} // namespace pdb
} // namespace llvm

#endif // LLVM_TOOLS_LLVMPDBDUMP_YAMLOUTPUTSTYLE_H
