//===----- lib/Support/ExtensibleRTTI.cpp - ExtensibleRTTI utilities ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/ExtensibleRTTI.h"

void llvm::RTTIRoot::anchor() {}
char llvm::RTTIRoot::ID = 0;
