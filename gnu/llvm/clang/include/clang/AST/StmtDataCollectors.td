class Stmt {
  code Code = [{
    addData(S->getStmtClass());
    // This ensures that non-macro-generated code isn't identical to
    // macro-generated code.
    addData(data_collection::getMacroStack(S->getBeginLoc(), Context));
    addData(data_collection::getMacroStack(S->getEndLoc(), Context));
  }];
}

class Expr {
  code Code = [{
    addData(S->getType());
  }];
}

//--- Builtin functionality ----------------------------------------------//
class ArrayTypeTraitExpr {
  code Code = [{
    addData(S->getTrait());
  }];
}
class ExpressionTraitExpr {
  code Code = [{
    addData(S->getTrait());
  }];
}
class PredefinedExpr {
  code Code = [{
    addData(S->getIdentKind());
  }];
}
class TypeTraitExpr {
  code Code = [{
    addData(S->getTrait());
    for (unsigned i = 0; i < S->getNumArgs(); ++i)
      addData(S->getArg(i)->getType());
  }];
}

//--- Calls --------------------------------------------------------------//
class CallExpr {
  code Code = [{
    // Function pointers don't have a callee and we just skip hashing it.
    if (const FunctionDecl *D = S->getDirectCallee()) {
      // If the function is a template specialization, we also need to handle
      // the template arguments as they are not included in the qualified name.
      if (auto Args = D->getTemplateSpecializationArgs()) {
        std::string ArgString;

        // Print all template arguments into ArgString
        llvm::raw_string_ostream OS(ArgString);
        for (unsigned i = 0; i < Args->size(); ++i) {
          Args->get(i).print(Context.getLangOpts(), OS, /*IncludeType*/ true);
          // Add a padding character so that 'foo<X, XX>()' != 'foo<XX, X>()'.
          OS << '\n';
        }
        OS.flush();

        addData(ArgString);
      }
      addData(D->getQualifiedNameAsString());
    }
  }];
}

//--- Value references ---------------------------------------------------//
class DeclRefExpr {
  code Code = [{
    addData(S->getDecl()->getQualifiedNameAsString());
  }];
}
class MemberExpr {
  code Code = [{
    addData(S->getMemberDecl()->getName());
  }];
}

//--- Literals -----------------------------------------------------------//
class IntegerLiteral {
  code Code = [{
    addData(llvm::hash_value(S->getValue()));
  }];
}
class FloatingLiteral {
  code Code = [{
    addData(llvm::hash_value(S->getValue()));
  }];
}
class StringLiteral {
  code Code = [{
    addData(S->getString());
}];
}
class CXXBoolLiteralExpr {
  code Code = [{
    addData(S->getValue());
  }];
}
class CharacterLiteral {
  code Code = [{
    addData(S->getValue());
  }];
}

//--- Exceptions ---------------------------------------------------------//
class CXXCatchStmt {
  code Code = [{
    addData(S->getCaughtType());
  }];
}

//--- C++ OOP Stmts ------------------------------------------------------//
class CXXDeleteExpr {
  code Code = [{
    addData(S->isArrayFormAsWritten()); addData(S->isGlobalDelete());
  }];
}

//--- Casts --------------------------------------------------------------//
class ObjCBridgedCastExpr {
  code Code = [{
    addData(S->getBridgeKind());
  }];
}

//--- Miscellaneous Exprs ------------------------------------------------//
class BinaryOperator {
  code Code = [{
    addData(S->getOpcode());
  }];
}
class UnaryOperator {
  code Code = [{
    addData(S->getOpcode());
  }];
}

//--- Control flow -------------------------------------------------------//
class GotoStmt {
  code Code = [{
    addData(S->getLabel()->getName());
  }];
}
class IndirectGotoStmt {
  code Code = [{
    if (S->getConstantTarget())
      addData(S->getConstantTarget()->getName());
  }];
}
class LabelStmt {
  code Code = [{
    addData(S->getDecl()->getName());
  }];
}
class MSDependentExistsStmt {
  code Code = [{
    addData(S->isIfExists());
  }];
}
class AddrLabelExpr {
  code Code = [{
    addData(S->getLabel()->getName());
  }];
}

//--- Objective-C --------------------------------------------------------//
class ObjCIndirectCopyRestoreExpr {
  code Code = [{
    addData(S->shouldCopy());
  }];
}
class ObjCPropertyRefExpr {
  code Code = [{
    addData(S->isSuperReceiver()); addData(S->isImplicitProperty());
  }];
}
class ObjCAtCatchStmt {
  code Code = [{
    addData(S->hasEllipsis());
  }];
}

//--- Miscellaneous Stmts ------------------------------------------------//
class CXXFoldExpr {
  code Code = [{
    addData(S->isRightFold()); addData(S->getOperator());
  }];
}
class GenericSelectionExpr {
  code Code = [{
    for (const GenericSelectionExpr::ConstAssociation Assoc : S->associations()) {
      addData(Assoc.getType());
    }
  }];
}
class LambdaExpr {
  code Code = [{
    for (const LambdaCapture &C : S->captures()) {
      addData(C.isPackExpansion());
      addData(C.getCaptureKind());
      if (C.capturesVariable())
        addData(C.getCapturedVar()->getType());
    }
    addData(S->isGenericLambda());
    addData(S->isMutable());
  }];
}
class DeclStmt {
  code Code = [{
    auto numDecls = std::distance(S->decl_begin(), S->decl_end());
    addData(static_cast<unsigned>(numDecls));
    for (const Decl *D : S->decls()) {
      if (const VarDecl *VD = dyn_cast<VarDecl>(D)) {
        addData(VD->getType());
      }
    }
  }];
}
class AsmStmt {
  code Code = [{
    addData(S->isSimple());
    addData(S->isVolatile());
    addData(S->generateAsmString(Context));
    for (unsigned i = 0; i < S->getNumInputs(); ++i) {
      addData(S->getInputConstraint(i));
    }
    for (unsigned i = 0; i < S->getNumOutputs(); ++i) {
      addData(S->getOutputConstraint(i));
    }
    for (unsigned i = 0; i < S->getNumClobbers(); ++i) {
      addData(S->getClobber(i));
    }
  }];
}
class AttributedStmt {
  code Code = [{
    for (const Attr *A : S->getAttrs()) {
      addData(std::string(A->getSpelling()));
    }
  }];
}
