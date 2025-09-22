//===-- RecordOps.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Operations on records (structs, classes, and unions).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_FLOWSENSITIVE_RECORDOPS_H
#define LLVM_CLANG_ANALYSIS_FLOWSENSITIVE_RECORDOPS_H

#include "clang/Analysis/FlowSensitive/DataflowEnvironment.h"
#include "clang/Analysis/FlowSensitive/StorageLocation.h"

namespace clang {
namespace dataflow {

/// Copies a record (struct, class, or union) from `Src` to `Dst`.
///
/// This performs a deep copy, i.e. it copies every field (including synthetic
/// fields) and recurses on fields of record type.
///
/// If there is a `RecordValue` associated with `Dst` in the environment, this
/// function creates a new `RecordValue` and associates it with `Dst`; clients
/// need to be aware of this and must not assume that the `RecordValue`
/// associated with `Dst` remains the same after the call.
///
/// Requirements:
///
///  Either:
///    - `Src` and `Dest` must have the same canonical unqualified type, or
///    - The type of `Src` must be derived from `Dest`, or
///    - The type of `Dest` must be derived from `Src` (in this case, any fields
///      that are only present in `Dest` are not overwritten).
void copyRecord(RecordStorageLocation &Src, RecordStorageLocation &Dst,
                Environment &Env);

/// Returns whether the records `Loc1` and `Loc2` are equal.
///
/// Values for `Loc1` are retrieved from `Env1`, and values for `Loc2` are
/// retrieved from `Env2`. A convenience overload retrieves values for `Loc1`
/// and `Loc2` from the same environment.
///
/// This performs a deep comparison, i.e. it compares every field (including
/// synthetic fields) and recurses on fields of record type. Fields of reference
/// type compare equal if they refer to the same storage location.
///
/// Note on how to interpret the result:
/// - If this returns true, the records are guaranteed to be equal at runtime.
/// - If this returns false, the records may still be equal at runtime; our
///   analysis merely cannot guarantee that they will be equal.
///
/// Requirements:
///
///  `Src` and `Dst` must have the same canonical unqualified type.
bool recordsEqual(const RecordStorageLocation &Loc1, const Environment &Env1,
                  const RecordStorageLocation &Loc2, const Environment &Env2);

inline bool recordsEqual(const RecordStorageLocation &Loc1,
                         const RecordStorageLocation &Loc2,
                         const Environment &Env) {
  return recordsEqual(Loc1, Env, Loc2, Env);
}

} // namespace dataflow
} // namespace clang

#endif // LLVM_CLANG_ANALYSIS_FLOWSENSITIVE_RECORDOPS_H
