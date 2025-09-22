//===- BPFCORE.h - Common info for Compile-Once Run-EveryWhere  -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BPF_BPFCORE_H
#define LLVM_LIB_TARGET_BPF_BPFCORE_H

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Instructions.h"

namespace llvm {

class BasicBlock;
class Instruction;
class Module;

class BPFCoreSharedInfo {
public:
  enum BTFTypeIdFlag : uint32_t {
    BTF_TYPE_ID_LOCAL_RELOC = 0,
    BTF_TYPE_ID_REMOTE_RELOC,

    MAX_BTF_TYPE_ID_FLAG,
  };

  enum PreserveTypeInfo : uint32_t {
    PRESERVE_TYPE_INFO_EXISTENCE = 0,
    PRESERVE_TYPE_INFO_SIZE,
    PRESERVE_TYPE_INFO_MATCH,

    MAX_PRESERVE_TYPE_INFO_FLAG,
  };

  enum PreserveEnumValue : uint32_t {
    PRESERVE_ENUM_VALUE_EXISTENCE = 0,
    PRESERVE_ENUM_VALUE,

    MAX_PRESERVE_ENUM_VALUE_FLAG,
  };

  /// The attribute attached to globals representing a field access
  static constexpr StringRef AmaAttr = "btf_ama";
  /// The attribute attached to globals representing a type id
  static constexpr StringRef TypeIdAttr = "btf_type_id";

  /// llvm.bpf.passthrough builtin seq number
  static uint32_t SeqNum;

  /// Insert a bpf passthrough builtin function.
  static Instruction *insertPassThrough(Module *M, BasicBlock *BB,
                                        Instruction *Input,
                                        Instruction *Before);
  static void removeArrayAccessCall(CallInst *Call);
  static void removeStructAccessCall(CallInst *Call);
  static void removeUnionAccessCall(CallInst *Call);
};

} // namespace llvm

#endif
