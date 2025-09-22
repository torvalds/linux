//===----- ABI.h - ABI related declarations ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Enums/classes describing ABI related information about constructors,
/// destructors and thunks.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_ABI_H
#define LLVM_CLANG_BASIC_ABI_H

#include "llvm/Support/DataTypes.h"
#include <cstring>

namespace clang {

/// C++ constructor types.
enum CXXCtorType {
  Ctor_Complete,       ///< Complete object ctor
  Ctor_Base,           ///< Base object ctor
  Ctor_Comdat,         ///< The COMDAT used for ctors
  Ctor_CopyingClosure, ///< Copying closure variant of a ctor
  Ctor_DefaultClosure, ///< Default closure variant of a ctor
};

/// C++ destructor types.
enum CXXDtorType {
    Dtor_Deleting, ///< Deleting dtor
    Dtor_Complete, ///< Complete object dtor
    Dtor_Base,     ///< Base object dtor
    Dtor_Comdat    ///< The COMDAT used for dtors
};

} // end namespace clang

#endif
