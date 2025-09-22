//===- DeclObjCCommon.h - Classes for representing declarations -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file contains common ObjC enums and classes used in AST and
//  Sema.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_DECLOBJCCOMMON_H
#define LLVM_CLANG_AST_DECLOBJCCOMMON_H

namespace clang {

/// ObjCPropertyAttribute::Kind - list of property attributes.
/// Keep this list in sync with LLVM's Dwarf.h ApplePropertyAttributes.s
namespace ObjCPropertyAttribute {
enum Kind {
  kind_noattr = 0x00,
  kind_readonly = 0x01,
  kind_getter = 0x02,
  kind_assign = 0x04,
  kind_readwrite = 0x08,
  kind_retain = 0x10,
  kind_copy = 0x20,
  kind_nonatomic = 0x40,
  kind_setter = 0x80,
  kind_atomic = 0x100,
  kind_weak = 0x200,
  kind_strong = 0x400,
  kind_unsafe_unretained = 0x800,
  /// Indicates that the nullability of the type was spelled with a
  /// property attribute rather than a type qualifier.
  kind_nullability = 0x1000,
  kind_null_resettable = 0x2000,
  kind_class = 0x4000,
  kind_direct = 0x8000,
  // Adding a property should change NumObjCPropertyAttrsBits
  // Also, don't forget to update the Clang C API at CXObjCPropertyAttrKind and
  // clang_Cursor_getObjCPropertyAttributes.
};
} // namespace ObjCPropertyAttribute::Kind

enum {
  /// Number of bits fitting all the property attributes.
  NumObjCPropertyAttrsBits = 16
};

} // namespace clang

#endif // LLVM_CLANG_AST_DECLOBJCCOMMON_H
