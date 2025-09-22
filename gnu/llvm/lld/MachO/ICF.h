//===- ICF.h ----------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MACHO_ICF_H
#define LLD_MACHO_ICF_H

#include "InputFiles.h"
#include "lld/Common/LLVM.h"
#include <vector>

namespace lld::macho {
class Symbol;

void markAddrSigSymbols();
void markSymAsAddrSig(Symbol *s);
void foldIdenticalSections(bool onlyCfStrings);

} // namespace lld::macho

#endif
