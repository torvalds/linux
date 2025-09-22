//===- DynamicType.h - Dynamic type related APIs ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines APIs that track and query dynamic type information. This
//  information can be used to devirtualize calls during the symbolic execution
//  or do type checking.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_DYNAMICTYPE_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_DYNAMICTYPE_H

#include "clang/AST/Type.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/DynamicCastInfo.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/DynamicTypeInfo.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState_Fwd.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymbolManager.h"

namespace clang {
namespace ento {

/// Get dynamic type information for the region \p MR.
DynamicTypeInfo getDynamicTypeInfo(ProgramStateRef State, const MemRegion *MR);

/// Get raw dynamic type information for the region \p MR.
/// It might return null.
const DynamicTypeInfo *getRawDynamicTypeInfo(ProgramStateRef State,
                                             const MemRegion *MR);

/// Get dynamic type information stored in a class object represented by \p Sym.
DynamicTypeInfo getClassObjectDynamicTypeInfo(ProgramStateRef State,
                                              SymbolRef Sym);

/// Get dynamic cast information from \p CastFromTy to \p CastToTy of \p MR.
const DynamicCastInfo *getDynamicCastInfo(ProgramStateRef State,
                                          const MemRegion *MR,
                                          QualType CastFromTy,
                                          QualType CastToTy);

/// Set dynamic type information of the region; return the new state.
ProgramStateRef setDynamicTypeInfo(ProgramStateRef State, const MemRegion *MR,
                                   DynamicTypeInfo NewTy);

/// Set dynamic type information of the region; return the new state.
ProgramStateRef setDynamicTypeInfo(ProgramStateRef State, const MemRegion *MR,
                                   QualType NewTy, bool CanBeSubClassed = true);

/// Set constraint on a type contained in a class object; return the new state.
ProgramStateRef setClassObjectDynamicTypeInfo(ProgramStateRef State,
                                              SymbolRef Sym,
                                              DynamicTypeInfo NewTy);

/// Set constraint on a type contained in a class object; return the new state.
ProgramStateRef setClassObjectDynamicTypeInfo(ProgramStateRef State,
                                              SymbolRef Sym, QualType NewTy,
                                              bool CanBeSubClassed = true);

/// Set dynamic type and cast information of the region; return the new state.
ProgramStateRef setDynamicTypeAndCastInfo(ProgramStateRef State,
                                          const MemRegion *MR,
                                          QualType CastFromTy,
                                          QualType CastToTy,
                                          bool IsCastSucceeds);

/// Removes the dead type informations from \p State.
ProgramStateRef removeDeadTypes(ProgramStateRef State, SymbolReaper &SR);

/// Removes the dead cast informations from \p State.
ProgramStateRef removeDeadCasts(ProgramStateRef State, SymbolReaper &SR);

/// Removes the dead Class object type informations from \p State.
ProgramStateRef removeDeadClassObjectTypes(ProgramStateRef State,
                                           SymbolReaper &SR);

void printDynamicTypeInfoJson(raw_ostream &Out, ProgramStateRef State,
                              const char *NL = "\n", unsigned int Space = 0,
                              bool IsDot = false);

} // namespace ento
} // namespace clang

#endif // LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_DYNAMICTYPE_H
