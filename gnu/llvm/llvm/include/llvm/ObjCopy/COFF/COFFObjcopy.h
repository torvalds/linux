//===- COFFObjcopy.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJCOPY_COFF_COFFOBJCOPY_H
#define LLVM_OBJCOPY_COFF_COFFOBJCOPY_H

namespace llvm {
class Error;
class raw_ostream;

namespace object {
class COFFObjectFile;
} // end namespace object

namespace objcopy {
struct CommonConfig;
struct COFFConfig;

namespace coff {

/// Apply the transformations described by \p Config and \p COFFConfig
/// to \p In and writes the result into \p Out.
/// \returns any Error encountered whilst performing the operation.
Error executeObjcopyOnBinary(const CommonConfig &Config, const COFFConfig &,
                             object::COFFObjectFile &In, raw_ostream &Out);

} // end namespace coff
} // end namespace objcopy
} // end namespace llvm

#endif // LLVM_OBJCOPY_COFF_COFFOBJCOPY_H
