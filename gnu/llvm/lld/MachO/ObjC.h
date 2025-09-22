//===- ObjC.h ---------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MACHO_OBJC_H
#define LLD_MACHO_OBJC_H

#include "llvm/Support/MemoryBuffer.h"

namespace lld::macho {

namespace objc {

namespace symbol_names {
constexpr const char klass[] = "_OBJC_CLASS_$_";
constexpr const char klassPropList[] = "__OBJC_$_CLASS_PROP_LIST_";

constexpr const char metaclass[] = "_OBJC_METACLASS_$_";
constexpr const char ehtype[] = "_OBJC_EHTYPE_$_";
constexpr const char ivar[] = "_OBJC_IVAR_$_";
constexpr const char instanceMethods[] = "__OBJC_$_INSTANCE_METHODS_";
constexpr const char classMethods[] = "__OBJC_$_CLASS_METHODS_";
constexpr const char listProprieties[] = "__OBJC_$_PROP_LIST_";

constexpr const char category[] = "__OBJC_$_CATEGORY_";
constexpr const char categoryInstanceMethods[] =
    "__OBJC_$_CATEGORY_INSTANCE_METHODS_";
constexpr const char categoryClassMethods[] =
    "__OBJC_$_CATEGORY_CLASS_METHODS_";
constexpr const char categoryProtocols[] = "__OBJC_CATEGORY_PROTOCOLS_$_";

constexpr const char swift_objc_category[] = "__CATEGORY_";
constexpr const char swift_objc_klass[] = "_$s";
} // namespace symbol_names

// Check for duplicate method names within related categories / classes.
void checkCategories();
void mergeCategories();

void doCleanup();
} // namespace objc

bool hasObjCSection(llvm::MemoryBufferRef);

} // namespace lld::macho

#endif
