//===-- RelLookupTableConverterPass.h - Rel Table Conv ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file implements relative lookup table converter that converts
/// lookup tables to relative lookup tables to make them PIC-friendly.
///
/// Switch lookup table example:
/// @switch.table.foo = private unnamed_addr constant [3 x i8*]
/// [
/// i8* getelementptr inbounds ([5 x i8], [5 x i8]* @.str, i64 0, i64 0),
/// i8* getelementptr inbounds ([4 x i8], [4 x i8]* @.str.1, i64 0, i64 0),
/// i8* getelementptr inbounds ([4 x i8], [4 x i8]* @.str.2, i64 0, i64 0)
/// ], align 8
///
/// switch.lookup:
///   %1 = sext i32 %cond to i64
///   %switch.gep = getelementptr inbounds [3 x i8*],
///                 [3 x i8*]* @switch.table.foo, i64 0, i64 %1
///   %switch.load = load i8*, i8** %switch.gep, align 8
///  ret i8* %switch.load
///
/// Switch lookup table will become a relative lookup table that
/// consists of relative offsets.
///
/// @reltable.foo = private unnamed_addr constant [3 x i32]
/// [
/// i32 trunc (i64 sub (i64 ptrtoint ([5 x i8]* @.str to i64),
///                     i64 ptrtoint ([3 x i32]* @reltable.foo to i64)) to i32),
/// i32 trunc (i64 sub (i64 ptrtoint ([4 x i8]* @.str.1 to i64),
///                     i64 ptrtoint ([3 x i32]* @reltable.foo to i64)) to i32),
/// i32 trunc (i64 sub (i64 ptrtoint ([4 x i8]* @.str.2 to i64),
///                     i64 ptrtoint ([3 x i32]* @reltable.foo to i64)) to i32)
/// ], align 4
///
/// IR after converting to a relative lookup table:
/// switch.lookup:
///  %1 = sext i32 %cond to i64
///  %reltable.shift = shl i64 %1, 2
///  %reltable.intrinsic = call i8* @llvm.load.relative.i64(
///                        i8* bitcast ([3 x i32]* @reltable.foo to i8*),
///                        i64 %reltable.shift)
///  ret i8* %reltable.intrinsic
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_RELLOOKUPTABLECONVERTER_H
#define LLVM_TRANSFORMS_UTILS_RELLOOKUPTABLECONVERTER_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Module;

// Pass that converts lookup tables to relative lookup tables.
class RelLookupTableConverterPass
    : public PassInfoMixin<RelLookupTableConverterPass> {
public:
  RelLookupTableConverterPass() = default;

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_RELLOOKUPTABLECONVERTER_H
