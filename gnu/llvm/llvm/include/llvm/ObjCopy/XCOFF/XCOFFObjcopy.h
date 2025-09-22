//===- XCOFFObjcopy.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJCOPY_XCOFF_XCOFFOBJCOPY_H
#define LLVM_OBJCOPY_XCOFF_XCOFFOBJCOPY_H

namespace llvm {
class Error;
class raw_ostream;

namespace object {
class XCOFFObjectFile;
} // end namespace object

namespace objcopy {
struct CommonConfig;
struct XCOFFConfig;

namespace xcoff {
/// Apply the transformations described by \p Config and \p XCOFFConfig
/// to \p In and writes the result into \p Out.
/// \returns any Error encountered whilst performing the operation.
Error executeObjcopyOnBinary(const CommonConfig &Config, const XCOFFConfig &,
                             object::XCOFFObjectFile &In, raw_ostream &Out);

} // end namespace xcoff
} // end namespace objcopy
} // end namespace llvm

#endif // LLVM_OBJCOPY_XCOFF_XCOFFOBJCOPY_H
