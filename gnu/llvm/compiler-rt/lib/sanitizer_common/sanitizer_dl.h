//===-- sanitizer_dl.h ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file has helper functions that depend on libc's dynamic loading
// introspection.
//
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_DL_H
#define SANITIZER_DL_H

namespace __sanitizer {

// Returns the path to the shared object or - in the case of statically linked
// sanitizers
// - the main program itself, that contains the sanitizer.
const char* DladdrSelfFName(void);

}  // namespace __sanitizer

#endif  // SANITIZER_DL_H
