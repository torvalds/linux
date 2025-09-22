//===----------------------- OrcRTBootstrap.h -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// OrcRTPrelinkImpl provides functions that should be linked into the executor
// to bootstrap common JIT functionality (e.g. memory allocation and memory
// access).
//
// Call rt_impl::addTo to add these functions to a bootstrap symbols map.
//
// FIXME: The functionality in this file should probably be moved to an ORC
// runtime bootstrap library in compiler-rt.
//
//===----------------------------------------------------------------------===//

#ifndef LIB_EXECUTIONENGINE_ORC_TARGETPROCESS_ORCRTBOOTSTRAP_H
#define LIB_EXECUTIONENGINE_ORC_TARGETPROCESS_ORCRTBOOTSTRAP_H

#include "llvm/ADT/StringMap.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"

namespace llvm {
namespace orc {
namespace rt_bootstrap {

void addTo(StringMap<ExecutorAddr> &M);

} // namespace rt_bootstrap
} // end namespace orc
} // end namespace llvm

#endif // LIB_EXECUTIONENGINE_ORC_TARGETPROCESS_ORCRTBOOTSTRAP_H
