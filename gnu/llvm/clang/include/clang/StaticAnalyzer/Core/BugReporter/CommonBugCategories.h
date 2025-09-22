//=--- CommonBugCategories.h - Provides common issue categories -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_BUGREPORTER_COMMONBUGCATEGORIES_H
#define LLVM_CLANG_STATICANALYZER_CORE_BUGREPORTER_COMMONBUGCATEGORIES_H

// Common strings used for the "category" of many static analyzer issues.
namespace clang {
namespace ento {
namespace categories {
extern const char *const AppleAPIMisuse;
extern const char *const CoreFoundationObjectiveC;
extern const char *const LogicError;
extern const char *const MemoryRefCount;
extern const char *const MemoryError;
extern const char *const UnixAPI;
extern const char *const CXXObjectLifecycle;
extern const char *const CXXMoveSemantics;
extern const char *const SecurityError;
extern const char *const UnusedCode;
extern const char *const TaintedData;
} // namespace categories
} // namespace ento
} // namespace clang
#endif
