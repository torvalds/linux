//===- AtomicExpandUtils.h - Utilities for expanding atomic instructions --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_ATOMICEXPANDUTILS_H
#define LLVM_CODEGEN_ATOMICEXPANDUTILS_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/AtomicOrdering.h"

namespace llvm {

class AtomicRMWInst;
class Value;

/// Parameters (see the expansion example below):
/// (the builder, %addr, %loaded, %new_val, ordering,
///  /* OUT */ %success, /* OUT */ %new_loaded)
using CreateCmpXchgInstFun =
    function_ref<void(IRBuilderBase &, Value *, Value *, Value *, Align,
                      AtomicOrdering, SyncScope::ID, Value *&, Value *&)>;

/// Expand an atomic RMW instruction into a loop utilizing
/// cmpxchg. You'll want to make sure your target machine likes cmpxchg
/// instructions in the first place and that there isn't another, better,
/// transformation available (for example AArch32/AArch64 have linked loads).
///
/// This is useful in passes which can't rewrite the more exotic RMW
/// instructions directly into a platform specific intrinsics (because, say,
/// those intrinsics don't exist). If such a pass is able to expand cmpxchg
/// instructions directly however, then, with this function, it could avoid two
/// extra module passes (avoiding passes by `-atomic-expand` and itself). A
/// specific example would be PNaCl's `RewriteAtomics` pass.
///
/// Given: atomicrmw some_op iN* %addr, iN %incr ordering
///
/// The standard expansion we produce is:
///     [...]
///     %init_loaded = load atomic iN* %addr
///     br label %loop
/// loop:
///     %loaded = phi iN [ %init_loaded, %entry ], [ %new_loaded, %loop ]
///     %new = some_op iN %loaded, %incr
/// ; This is what -atomic-expand will produce using this function on i686
/// targets:
///     %pair = cmpxchg iN* %addr, iN %loaded, iN %new_val
///     %new_loaded = extractvalue { iN, i1 } %pair, 0
///     %success = extractvalue { iN, i1 } %pair, 1
/// ; End callback produced IR
///     br i1 %success, label %atomicrmw.end, label %loop
/// atomicrmw.end:
///     [...]
///
/// Returns true if the containing function was modified.
bool expandAtomicRMWToCmpXchg(AtomicRMWInst *AI, CreateCmpXchgInstFun CreateCmpXchg);

} // end namespace llvm

#endif // LLVM_CODEGEN_ATOMICEXPANDUTILS_H
