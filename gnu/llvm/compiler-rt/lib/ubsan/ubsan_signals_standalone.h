//=-- ubsan_signals_standalone.h
//------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Installs signal handlers and related interceptors for UBSan standalone.
//
//===----------------------------------------------------------------------===//

#ifndef UBSAN_SIGNALS_STANDALONE_H
#define UBSAN_SIGNALS_STANDALONE_H

namespace __ubsan {

// Initializes signal handlers and interceptors.
void InitializeDeadlySignals();

} // namespace __ubsan

#endif // UBSAN_SIGNALS_STANDALONE_H
