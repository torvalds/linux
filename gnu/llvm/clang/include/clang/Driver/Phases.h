//===--- Phases.h - Transformations on Driver Types -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_DRIVER_PHASES_H
#define LLVM_CLANG_DRIVER_PHASES_H

namespace clang {
namespace driver {
namespace phases {
  /// ID - Ordered values for successive stages in the
  /// compilation process which interact with user options.
  enum ID {
    Preprocess,
    Precompile,
    Compile,
    Backend,
    Assemble,
    Link,
    IfsMerge,
  };

  enum {
    MaxNumberOfPhases = IfsMerge + 1
  };

  const char *getPhaseName(ID Id);

} // end namespace phases
} // end namespace driver
} // end namespace clang

#endif
