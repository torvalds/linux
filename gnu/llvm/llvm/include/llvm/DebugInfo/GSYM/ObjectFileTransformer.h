//===- ObjectFileTransformer.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_GSYM_OBJECTFILETRANSFORMER_H
#define LLVM_DEBUGINFO_GSYM_OBJECTFILETRANSFORMER_H

#include "llvm/Support/Error.h"

namespace llvm {

namespace object {
class ObjectFile;
}

namespace gsym {

class GsymCreator;
class OutputAggregator;

class ObjectFileTransformer {
public:
  /// Extract any object file data that is needed by the GsymCreator.
  ///
  /// The extracted information includes the UUID of the binary and converting
  /// all function symbols from any symbol tables into FunctionInfo objects.
  ///
  /// \param Obj The object file that contains the DWARF debug info.
  ///
  /// \param Log The stream to log warnings and non fatal issues to. If NULL,
  ///            don't log.
  ///
  /// \param Gsym The GSYM creator to populate with the function information
  /// from the debug info.
  ///
  /// \returns An error indicating any fatal issues that happen when parsing
  /// the DWARF, or Error::success() if all goes well.
  static llvm::Error convert(const object::ObjectFile &Obj,
                             OutputAggregator &Output, GsymCreator &Gsym);
};

} // namespace gsym
} // namespace llvm

#endif // LLVM_DEBUGINFO_GSYM_OBJECTFILETRANSFORMER_H
