//===- TextAPIContext.h ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines the YAML Context for the TextAPI Reader/Writer.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TEXTAPI_MACHO_CONTEXT_H
#define LLVM_TEXTAPI_MACHO_CONTEXT_H

#include "llvm/TextAPI/FileTypes.h"
#include <string>

namespace llvm {
namespace MachO {

struct TextAPIContext {
  std::string ErrorMessage;
  std::string Path;
  FileType FileKind;
};

} // end namespace MachO.
} // end namespace llvm.

#endif // LLVM_TEXTAPI_MACHO_CONTEXT_H
