//===--- ExceptionSpecificationType.h ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines the ExceptionSpecificationType enumeration and various
/// utility functions.
///
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_BASIC_EXCEPTIONSPECIFICATIONTYPE_H
#define LLVM_CLANG_BASIC_EXCEPTIONSPECIFICATIONTYPE_H

namespace clang {

/// The various types of exception specifications that exist in C++11.
enum ExceptionSpecificationType {
  EST_None,             ///< no exception specification
  EST_DynamicNone,      ///< throw()
  EST_Dynamic,          ///< throw(T1, T2)
  EST_MSAny,            ///< Microsoft throw(...) extension
  EST_NoThrow,          ///< Microsoft __declspec(nothrow) extension
  EST_BasicNoexcept,    ///< noexcept
  EST_DependentNoexcept,///< noexcept(expression), value-dependent
  EST_NoexceptFalse,    ///< noexcept(expression), evals to 'false'
  EST_NoexceptTrue,     ///< noexcept(expression), evals to 'true'
  EST_Unevaluated,      ///< not evaluated yet, for special member function
  EST_Uninstantiated,   ///< not instantiated yet
  EST_Unparsed          ///< not parsed yet
};

inline bool isDynamicExceptionSpec(ExceptionSpecificationType ESpecType) {
  return ESpecType >= EST_DynamicNone && ESpecType <= EST_MSAny;
}

inline bool isComputedNoexcept(ExceptionSpecificationType ESpecType) {
  return ESpecType >= EST_DependentNoexcept &&
         ESpecType <= EST_NoexceptTrue;
}

inline bool isNoexceptExceptionSpec(ExceptionSpecificationType ESpecType) {
  return ESpecType == EST_BasicNoexcept || ESpecType == EST_NoThrow ||
         isComputedNoexcept(ESpecType);
}

inline bool isUnresolvedExceptionSpec(ExceptionSpecificationType ESpecType) {
  return ESpecType == EST_Unevaluated || ESpecType == EST_Uninstantiated;
}

inline bool isExplicitThrowExceptionSpec(ExceptionSpecificationType ESpecType) {
  return ESpecType == EST_Dynamic || ESpecType == EST_MSAny ||
         ESpecType == EST_NoexceptFalse;
}

/// Possible results from evaluation of a noexcept expression.
enum CanThrowResult {
  CT_Cannot,
  CT_Dependent,
  CT_Can
};

inline CanThrowResult mergeCanThrow(CanThrowResult CT1, CanThrowResult CT2) {
  // CanThrowResult constants are ordered so that the maximum is the correct
  // merge result.
  return CT1 > CT2 ? CT1 : CT2;
}

} // end namespace clang

#endif // LLVM_CLANG_BASIC_EXCEPTIONSPECIFICATIONTYPE_H
