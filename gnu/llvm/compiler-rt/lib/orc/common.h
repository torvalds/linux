//===- common.h - Common utilities for the ORC runtime ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of the ORC runtime support library.
//
//===----------------------------------------------------------------------===//

#ifndef ORC_RT_COMMON_H
#define ORC_RT_COMMON_H

#include "compiler.h"
#include "orc_rt/c_api.h"
#include <type_traits>

/// This macro should be used to define tags that will be associated with
/// handlers in the JIT process, and call can be used to define tags f
#define ORC_RT_JIT_DISPATCH_TAG(X) \
extern "C" char X; \
char X = 0;

/// Opaque struct for external symbols.
struct __orc_rt_Opaque {};

/// Error reporting function.
extern "C" void __orc_rt_log_error(const char *ErrMsg);

/// Context object for dispatching calls to the JIT object.
///
/// This is declared for use by the runtime, but should be implemented in the
/// executor or provided by a definition added to the JIT before the runtime
/// is loaded.
ORC_RT_IMPORT __orc_rt_Opaque __orc_rt_jit_dispatch_ctx ORC_RT_WEAK_IMPORT;

/// For dispatching calls to the JIT object.
///
/// This is declared for use by the runtime, but should be implemented in the
/// executor or provided by a definition added to the JIT before the runtime
/// is loaded.
ORC_RT_IMPORT orc_rt_CWrapperFunctionResult
__orc_rt_jit_dispatch(__orc_rt_Opaque *DispatchCtx, const void *FnTag,
                      const char *Data, size_t Size) ORC_RT_WEAK_IMPORT;

#endif // ORC_RT_COMMON_H
