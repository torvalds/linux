//===--- LexicallyOrderedRecursiveASTVisitor.h - ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the LexicallyOrderedRecursiveASTVisitor interface, which
//  recursively traverses the entire AST in a lexical order.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_LEXICALLYORDEREDRECURSIVEASTVISITOR_H
#define LLVM_CLANG_AST_LEXICALLYORDEREDRECURSIVEASTVISITOR_H

#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/Support/SaveAndRestore.h"

namespace clang {

/// A RecursiveASTVisitor subclass that guarantees that AST traversal is
/// performed in a lexical order (i.e. the order in which declarations are
/// written in the source).
///
/// RecursiveASTVisitor doesn't guarantee lexical ordering because there are
/// some declarations, like Objective-C @implementation declarations
/// that might be represented in the AST differently to how they were written
/// in the source.
/// In particular, Objective-C @implementation declarations may contain
/// non-Objective-C declarations, like functions:
///
///   @implementation MyClass
///
///   - (void) method { }
///   void normalFunction() { }
///
///   @end
///
/// Clang's AST stores these declarations outside of the @implementation
/// declaration, so the example above would be represented using the following
/// AST:
///   |-ObjCImplementationDecl ... MyClass
///   | `-ObjCMethodDecl ... method
///   |    ...
///   `-FunctionDecl ... normalFunction
///       ...
///
/// This class ensures that these declarations are traversed before the
/// corresponding TraverseDecl for the @implementation returns. This ensures
/// that the lexical parent relationship between these declarations and the
/// @implementation is preserved while traversing the AST. Note that the
/// current implementation doesn't mix these declarations with the declarations
/// contained in the @implementation, so the traversal of all of the
/// declarations in the @implementation still doesn't follow the lexical order.
template <typename Derived>
class LexicallyOrderedRecursiveASTVisitor
    : public RecursiveASTVisitor<Derived> {
  using BaseType = RecursiveASTVisitor<Derived>;

public:
  LexicallyOrderedRecursiveASTVisitor(const SourceManager &SM) : SM(SM) {}

  bool TraverseObjCImplementationDecl(ObjCImplementationDecl *D) {
    // Objective-C @implementation declarations should not trigger early exit
    // until the additional decls are traversed as their children are not
    // lexically ordered.
    bool Result = BaseType::TraverseObjCImplementationDecl(D);
    return TraverseAdditionalLexicallyNestedDeclarations() ? Result : false;
  }

  bool TraverseObjCCategoryImplDecl(ObjCCategoryImplDecl *D) {
    bool Result = BaseType::TraverseObjCCategoryImplDecl(D);
    return TraverseAdditionalLexicallyNestedDeclarations() ? Result : false;
  }

  bool TraverseDeclContextHelper(DeclContext *DC) {
    if (!DC)
      return true;

    for (auto I = DC->decls_begin(), E = DC->decls_end(); I != E;) {
      Decl *Child = *I;
      if (BaseType::canIgnoreChildDeclWhileTraversingDeclContext(Child)) {
        ++I;
        continue;
      }
      if (!isa<ObjCImplementationDecl>(Child) &&
          !isa<ObjCCategoryImplDecl>(Child)) {
        if (!BaseType::getDerived().TraverseDecl(Child))
          return false;
        ++I;
        continue;
      }
      // Gather declarations that follow the Objective-C implementation
      // declarations but are lexically contained in the implementation.
      LexicallyNestedDeclarations.clear();
      for (++I; I != E; ++I) {
        Decl *Sibling = *I;
        if (!SM.isBeforeInTranslationUnit(Sibling->getBeginLoc(),
                                          Child->getEndLoc()))
          break;
        if (!BaseType::canIgnoreChildDeclWhileTraversingDeclContext(Sibling))
          LexicallyNestedDeclarations.push_back(Sibling);
      }
      if (!BaseType::getDerived().TraverseDecl(Child))
        return false;
    }
    return true;
  }

  Stmt::child_range getStmtChildren(Stmt *S) { return S->children(); }

  SmallVector<Stmt *, 8> getStmtChildren(CXXOperatorCallExpr *CE) {
    SmallVector<Stmt *, 8> Children(CE->children());
    bool Swap;
    // Switch the operator and the first operand for all infix and postfix
    // operations.
    switch (CE->getOperator()) {
    case OO_Arrow:
    case OO_Call:
    case OO_Subscript:
      Swap = true;
      break;
    case OO_PlusPlus:
    case OO_MinusMinus:
      // These are postfix unless there is exactly one argument.
      Swap = Children.size() != 2;
      break;
    default:
      Swap = CE->isInfixBinaryOp();
      break;
    }
    if (Swap && Children.size() > 1)
      std::swap(Children[0], Children[1]);
    return Children;
  }

private:
  bool TraverseAdditionalLexicallyNestedDeclarations() {
    // FIXME: Ideally the gathered declarations and the declarations in the
    // @implementation should be mixed and sorted to get a true lexical order,
    // but right now we only care about getting the correct lexical parent, so
    // we can traverse the gathered nested declarations after the declarations
    // in the decl context.
    assert(!BaseType::getDerived().shouldTraversePostOrder() &&
           "post-order traversal is not supported for lexically ordered "
           "recursive ast visitor");
    for (Decl *D : LexicallyNestedDeclarations) {
      if (!BaseType::getDerived().TraverseDecl(D))
        return false;
    }
    return true;
  }

  const SourceManager &SM;
  llvm::SmallVector<Decl *, 8> LexicallyNestedDeclarations;
};

} // end namespace clang

#endif // LLVM_CLANG_AST_LEXICALLYORDEREDRECURSIVEASTVISITOR_H
