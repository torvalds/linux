//===- DynamicExtent.h - Dynamic extent related APIs ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines APIs that track and query dynamic extent information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_DYNAMICEXTENT_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_DYNAMICEXTENT_H

#include "clang/StaticAnalyzer/Core/PathSensitive/MemRegion.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState_Fwd.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SValBuilder.h"

namespace clang {
namespace ento {

/// \returns The stored dynamic extent for the region \p MR.
DefinedOrUnknownSVal getDynamicExtent(ProgramStateRef State,
                                      const MemRegion *MR, SValBuilder &SVB);

/// \returns The element extent of the type \p Ty.
DefinedOrUnknownSVal getElementExtent(QualType Ty, SValBuilder &SVB);

/// \returns The stored element count of the region \p MR.
DefinedOrUnknownSVal getDynamicElementCount(ProgramStateRef State,
                                            const MemRegion *MR,
                                            SValBuilder &SVB, QualType Ty);

/// Set the dynamic extent \p Extent of the region \p MR.
ProgramStateRef setDynamicExtent(ProgramStateRef State, const MemRegion *MR,
                                 DefinedOrUnknownSVal Extent, SValBuilder &SVB);

/// Get the dynamic extent for a symbolic value that represents a buffer. If
/// there is an offsetting to the underlying buffer we consider that too.
/// Returns with an SVal that represents the extent, this is Unknown if the
/// engine cannot deduce the extent.
/// E.g.
///   char buf[3];
///   (buf); // extent is 3
///   (buf + 1); // extent is 2
///   (buf + 3); // extent is 0
///   (buf + 4); // extent is -1
///
///   char *bufptr;
///   (bufptr) // extent is unknown
SVal getDynamicExtentWithOffset(ProgramStateRef State, SVal BufV);

/// \returns The stored element count of the region represented by a symbolic
/// value \p BufV.
DefinedOrUnknownSVal getDynamicElementCountWithOffset(ProgramStateRef State,
                                                      SVal BufV, QualType Ty);

} // namespace ento
} // namespace clang

#endif // LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_DYNAMICEXTENT_H
