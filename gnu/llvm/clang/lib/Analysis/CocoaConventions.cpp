//===- CocoaConventions.h - Special handling of Cocoa conventions -*- C++ -*--//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements cocoa naming convention analysis.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/DomainSpecific/CocoaConventions.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Type.h"
#include "clang/Basic/CharInfo.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/ErrorHandling.h"

using namespace clang;
using namespace ento;

bool cocoa::isRefType(QualType RetTy, StringRef Prefix,
                      StringRef Name) {
  // Recursively walk the typedef stack, allowing typedefs of reference types.
  while (const TypedefType *TD = RetTy->getAs<TypedefType>()) {
    StringRef TDName = TD->getDecl()->getIdentifier()->getName();
    if (TDName.starts_with(Prefix) && TDName.ends_with("Ref"))
      return true;
    // XPC unfortunately uses CF-style function names, but aren't CF types.
    if (TDName.starts_with("xpc_"))
      return false;
    RetTy = TD->getDecl()->getUnderlyingType();
  }

  if (Name.empty())
    return false;

  // Is the type void*?
  const PointerType* PT = RetTy->castAs<PointerType>();
  if (!PT || !PT->getPointeeType().getUnqualifiedType()->isVoidType())
    return false;

  // Does the name start with the prefix?
  return Name.starts_with(Prefix);
}

/// Returns true when the passed-in type is a CF-style reference-counted
/// type from the DiskArbitration framework.
static bool isDiskArbitrationAPIRefType(QualType T) {
  return cocoa::isRefType(T, "DADisk") ||
      cocoa::isRefType(T, "DADissenter") ||
      cocoa::isRefType(T, "DASessionRef");
}

bool coreFoundation::isCFObjectRef(QualType T) {
  return cocoa::isRefType(T, "CF") || // Core Foundation.
         cocoa::isRefType(T, "CG") || // Core Graphics.
         cocoa::isRefType(T, "CM") || // Core Media.
         isDiskArbitrationAPIRefType(T);
}


bool cocoa::isCocoaObjectRef(QualType Ty) {
  if (!Ty->isObjCObjectPointerType())
    return false;

  const ObjCObjectPointerType *PT = Ty->getAs<ObjCObjectPointerType>();

  // Can be true for objects with the 'NSObject' attribute.
  if (!PT)
    return true;

  // We assume that id<..>, id, Class, and Class<..> all represent tracked
  // objects.
  if (PT->isObjCIdType() || PT->isObjCQualifiedIdType() ||
      PT->isObjCClassType() || PT->isObjCQualifiedClassType())
    return true;

  // Does the interface subclass NSObject?
  // FIXME: We can memoize here if this gets too expensive.
  const ObjCInterfaceDecl *ID = PT->getInterfaceDecl();

  // Assume that anything declared with a forward declaration and no
  // @interface subclasses NSObject.
  if (!ID->hasDefinition())
    return true;

  for ( ; ID ; ID = ID->getSuperClass())
    if (ID->getIdentifier()->getName() == "NSObject")
      return true;

  return false;
}

bool coreFoundation::followsCreateRule(const FunctionDecl *fn) {
  // For now, *just* base this on the function name, not on anything else.

  const IdentifierInfo *ident = fn->getIdentifier();
  if (!ident) return false;
  StringRef functionName = ident->getName();

  StringRef::iterator it = functionName.begin();
  StringRef::iterator start = it;
  StringRef::iterator endI = functionName.end();

  while (true) {
    // Scan for the start of 'create' or 'copy'.
    for ( ; it != endI ; ++it) {
      // Search for the first character.  It can either be 'C' or 'c'.
      char ch = *it;
      if (ch == 'C' || ch == 'c') {
        // Make sure this isn't something like 'recreate' or 'Scopy'.
        if (ch == 'c' && it != start && isLetter(*(it - 1)))
          continue;

        ++it;
        break;
      }
    }

    // Did we hit the end of the string?  If so, we didn't find a match.
    if (it == endI)
      return false;

    // Scan for *lowercase* 'reate' or 'opy', followed by no lowercase
    // character.
    StringRef suffix = functionName.substr(it - start);
    if (suffix.starts_with("reate")) {
      it += 5;
    } else if (suffix.starts_with("opy")) {
      it += 3;
    } else {
      // Keep scanning.
      continue;
    }

    if (it == endI || !isLowercase(*it))
      return true;

    // If we matched a lowercase character, it isn't the end of the
    // word.  Keep scanning.
  }
}
