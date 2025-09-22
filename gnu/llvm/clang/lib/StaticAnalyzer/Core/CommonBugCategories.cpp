//=--- CommonBugCategories.cpp - Provides common issue categories -*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/BugReporter/CommonBugCategories.h"

// Common strings used for the "category" of many static analyzer issues.
namespace clang {
namespace ento {
namespace categories {

const char *const AppleAPIMisuse = "API Misuse (Apple)";
const char *const CoreFoundationObjectiveC = "Core Foundation/Objective-C";
const char *const LogicError = "Logic error";
const char *const MemoryRefCount =
    "Memory (Core Foundation/Objective-C/OSObject)";
const char *const MemoryError = "Memory error";
const char *const UnixAPI = "Unix API";
const char *const CXXObjectLifecycle = "C++ object lifecycle";
const char *const CXXMoveSemantics = "C++ move semantics";
const char *const SecurityError = "Security error";
const char *const UnusedCode = "Unused code";
const char *const TaintedData = "Tainted data used";
} // namespace categories
} // namespace ento
} // namespace clang
