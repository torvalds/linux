//===- TemplateArgumentVisitor.h - Visitor for TArg subclasses --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the TemplateArgumentVisitor interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_TEMPLATEARGUMENTVISITOR_H
#define LLVM_CLANG_AST_TEMPLATEARGUMENTVISITOR_H

#include "clang/AST/TemplateBase.h"

namespace clang {

namespace templateargumentvisitor {

/// A simple visitor class that helps create template argument visitors.
template <template <typename> class Ref, typename ImplClass,
          typename RetTy = void, typename... ParamTys>
class Base {
public:
#define REF(CLASS) typename Ref<CLASS>::type
#define DISPATCH(NAME)                                                         \
  case TemplateArgument::NAME:                                                 \
    return static_cast<ImplClass *>(this)->Visit##NAME##TemplateArgument(      \
        TA, std::forward<ParamTys>(P)...)

  RetTy Visit(REF(TemplateArgument) TA, ParamTys... P) {
    switch (TA.getKind()) {
      DISPATCH(Null);
      DISPATCH(Type);
      DISPATCH(Declaration);
      DISPATCH(NullPtr);
      DISPATCH(Integral);
      DISPATCH(StructuralValue);
      DISPATCH(Template);
      DISPATCH(TemplateExpansion);
      DISPATCH(Expression);
      DISPATCH(Pack);
    }
    llvm_unreachable("TemplateArgument is not covered in switch!");
  }

  // If the implementation chooses not to implement a certain visit
  // method, fall back to the parent.

#define VISIT_METHOD(CATEGORY)                                                 \
  RetTy Visit##CATEGORY##TemplateArgument(REF(TemplateArgument) TA,            \
                                          ParamTys... P) {                     \
    return VisitTemplateArgument(TA, std::forward<ParamTys>(P)...);            \
  }

  VISIT_METHOD(Null);
  VISIT_METHOD(Type);
  VISIT_METHOD(Declaration);
  VISIT_METHOD(NullPtr);
  VISIT_METHOD(Integral);
  VISIT_METHOD(StructuralValue);
  VISIT_METHOD(Template);
  VISIT_METHOD(TemplateExpansion);
  VISIT_METHOD(Expression);
  VISIT_METHOD(Pack);

  RetTy VisitTemplateArgument(REF(TemplateArgument), ParamTys...) {
    return RetTy();
  }

#undef REF
#undef DISPATCH
#undef VISIT_METHOD
};

} // namespace templateargumentvisitor

/// A simple visitor class that helps create template argument visitors.
///
/// This class does not preserve constness of TemplateArgument references (see
/// also ConstTemplateArgumentVisitor).
template <typename ImplClass, typename RetTy = void, typename... ParamTys>
class TemplateArgumentVisitor
    : public templateargumentvisitor::Base<std::add_lvalue_reference, ImplClass,
                                           RetTy, ParamTys...> {};

/// A simple visitor class that helps create template argument visitors.
///
/// This class preserves constness of TemplateArgument references (see also
/// TemplateArgumentVisitor).
template <typename ImplClass, typename RetTy = void, typename... ParamTys>
class ConstTemplateArgumentVisitor
    : public templateargumentvisitor::Base<llvm::make_const_ref, ImplClass,
                                           RetTy, ParamTys...> {};

} // namespace clang

#endif // LLVM_CLANG_AST_TEMPLATEARGUMENTVISITOR_H
