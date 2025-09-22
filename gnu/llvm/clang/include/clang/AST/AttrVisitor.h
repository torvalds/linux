//===- AttrVisitor.h - Visitor for Attr subclasses --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the AttrVisitor interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_ATTRVISITOR_H
#define LLVM_CLANG_AST_ATTRVISITOR_H

#include "clang/AST/Attr.h"

namespace clang {

namespace attrvisitor {

/// A simple visitor class that helps create attribute visitors.
template <template <typename> class Ptr, typename ImplClass,
          typename RetTy = void, class... ParamTys>
class Base {
public:
#define PTR(CLASS) typename Ptr<CLASS>::type
#define DISPATCH(NAME)                                                         \
  return static_cast<ImplClass *>(this)->Visit##NAME(static_cast<PTR(NAME)>(A))

  RetTy Visit(PTR(Attr) A) {
    switch (A->getKind()) {

#define ATTR(NAME)                                                             \
  case attr::NAME:                                                             \
    DISPATCH(NAME##Attr);
#include "clang/Basic/AttrList.inc"
    }
    llvm_unreachable("Attr that isn't part of AttrList.inc!");
  }

  // If the implementation chooses not to implement a certain visit
  // method, fall back to the parent.
#define ATTR(NAME)                                                             \
  RetTy Visit##NAME##Attr(PTR(NAME##Attr) A) { DISPATCH(Attr); }
#include "clang/Basic/AttrList.inc"

  RetTy VisitAttr(PTR(Attr)) { return RetTy(); }

#undef PTR
#undef DISPATCH
};

} // namespace attrvisitor

/// A simple visitor class that helps create attribute visitors.
///
/// This class does not preserve constness of Attr pointers (see
/// also ConstAttrVisitor).
template <typename ImplClass, typename RetTy = void, typename... ParamTys>
class AttrVisitor : public attrvisitor::Base<std::add_pointer, ImplClass, RetTy,
                                             ParamTys...> {};

/// A simple visitor class that helps create attribute visitors.
///
/// This class preserves constness of Attr pointers (see also
/// AttrVisitor).
template <typename ImplClass, typename RetTy = void, typename... ParamTys>
class ConstAttrVisitor
    : public attrvisitor::Base<llvm::make_const_ptr, ImplClass, RetTy,
                               ParamTys...> {};

} // namespace clang

#endif // LLVM_CLANG_AST_ATTRVISITOR_H
