//===- JITLoaderGDB.h - Register objects via GDB JIT interface -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Register objects for access by debuggers via the GDB JIT interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_JITLOADERGDB_H
#define LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_JITLOADERGDB_H

#include "llvm/ExecutionEngine/Orc/Shared/WrapperFunctionUtils.h"
#include <cstdint>

// Keep in sync with gdb/gdb/jit.h
extern "C" {

typedef enum {
  JIT_NOACTION = 0,
  JIT_REGISTER_FN,
  JIT_UNREGISTER_FN
} jit_actions_t;

struct jit_code_entry {
  struct jit_code_entry *next_entry;
  struct jit_code_entry *prev_entry;
  const char *symfile_addr;
  uint64_t symfile_size;
};

struct jit_descriptor {
  uint32_t version;
  // This should be jit_actions_t, but we want to be specific about the
  // bit-width.
  uint32_t action_flag;
  struct jit_code_entry *relevant_entry;
  struct jit_code_entry *first_entry;
};
}

extern "C" llvm::orc::shared::CWrapperFunctionResult
llvm_orc_registerJITLoaderGDBWrapper(const char *Data, uint64_t Size);

extern "C" llvm::orc::shared::CWrapperFunctionResult
llvm_orc_registerJITLoaderGDBAllocAction(const char *Data, size_t Size);

#endif // LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_JITLOADERGDB_H
