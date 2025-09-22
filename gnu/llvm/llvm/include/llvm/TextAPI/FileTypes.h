//===- llvm/TextAPI/FileTypes.h - TAPI Interface File -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TEXTAPI_FILETYPES_H
#define LLVM_TEXTAPI_FILETYPES_H

#include "llvm/ADT/BitmaskEnum.h"
namespace llvm::MachO {
/// Defines the file type TextAPI files can represent.
enum FileType : unsigned {
  /// Invalid file type.
  Invalid = 0U,

  /// \brief MachO Dynamic Library file.
  MachO_DynamicLibrary = 1U << 0,

  /// \brief MachO Dynamic Library Stub file.
  MachO_DynamicLibrary_Stub = 1U << 1,

  /// \brief MachO Bundle file.
  MachO_Bundle = 1U << 2,

  /// Text-based stub file (.tbd) version 1.0
  TBD_V1 = 1U << 3,

  /// Text-based stub file (.tbd) version 2.0
  TBD_V2 = 1U << 4,

  /// Text-based stub file (.tbd) version 3.0
  TBD_V3 = 1U << 5,

  /// Text-based stub file (.tbd) version 4.0
  TBD_V4 = 1U << 6,

  /// Text-based stub file (.tbd) version 5.0
  TBD_V5 = 1U << 7,

  All = ~0U,

  LLVM_MARK_AS_BITMASK_ENUM(/*LargestValue=*/All),
};

} // namespace llvm::MachO
#endif // LLVM_TEXTAPI_FILETYPES_H
