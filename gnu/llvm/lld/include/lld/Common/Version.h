//===- lld/Common/Version.h - LLD Version Number ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines a version-related utility function.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_VERSION_H
#define LLD_VERSION_H

#include "lld/Common/Version.inc"
#include "llvm/ADT/StringRef.h"

namespace lld {
/// Retrieves a string representing the complete lld version.
std::string getLLDVersion();
}

#endif // LLD_VERSION_H
