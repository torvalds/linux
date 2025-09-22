//===- tools/dsymutil/CFBundle.h - CFBundle helper --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_DSYMUTIL_CFBUNDLE_H
#define LLVM_TOOLS_DSYMUTIL_CFBUNDLE_H

#include "llvm/ADT/StringRef.h"
#include <string>

namespace llvm {
namespace dsymutil {

struct CFBundleInfo {
  std::string VersionStr = "1";
  std::string ShortVersionStr = "1.0";
  std::string IDStr;
  bool OmitShortVersion() const { return ShortVersionStr.empty(); }
};

CFBundleInfo getBundleInfo(llvm::StringRef ExePath);

} // end namespace dsymutil
} // end namespace llvm

#endif
