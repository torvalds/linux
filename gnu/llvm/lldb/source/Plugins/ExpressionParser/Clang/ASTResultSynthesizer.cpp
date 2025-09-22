//===-- ASTResultSynthesizer.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ASTResultSynthesizer.h"

#include "ClangASTImporter.h"
#include "ClangPersistentVariables.h"

#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclGroup.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include "clang/Parse/Parser.h"
#include "clang/Sema/SemaDiagnostic.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdlib>

using namespace llvm;
using namespace clang;
using namespace lldb_private;

ASTResultSynthesizer::ASTResultSynthesizer(ASTConsumer *passthrough,
                                           bool top_level, Target &target)
    : m_ast_context(nullptr), m_passthrough(passthrough),
      m_passthrough_sema(nullptr), m_target(target), m_sema(nullptr),
      m_top_level(top_level) {
  if (!m_passthrough)
    return;

  m_passthrough_sema = dyn_cast<SemaConsumer>(passthrough);
}

ASTResultSynthesizer::~ASTResultSynthesizer() = default;

void ASTResultSynthesizer::Initialize(ASTContext &Context) {
  m_ast_context = &Context;

  if (m_passthrough)
    m_passthrough->Initialize(Context);
}

void ASTResultSynthesizer::TransformTopLevelDecl(Decl *D) {
  Log *log = GetLog(LLDBLog::Expressions);

  if (NamedDecl *named_decl = dyn_cast<NamedDecl>(D)) {
    if (log && log->GetVerbose()) {
      if (named_decl->getIdentifier())
        LLDB_LOGF(log, "TransformTopLevelDecl(%s)",
                  named_decl->getIdentifier()->getNameStart());
      else if (ObjCMethodDecl *method_decl = dyn_cast<ObjCMethodDecl>(D))
        LLDB_LOGF(log, "TransformTopLevelDecl(%s)",
                  method_decl->getSelector().getAsString().c_str());
      else
        LLDB_LOGF(log, "TransformTopLevelDecl(<complex>)");
    }

    if (m_top_level) {
      RecordPersistentDecl(named_decl);
    }
  }

  if (LinkageSpecDecl *linkage_spec_decl = dyn_cast<LinkageSpecDecl>(D)) {
    RecordDecl::decl_iterator decl_iterator;

    for (decl_iterator = linkage_spec_decl->decls_begin();
         decl_iterator != linkage_spec_decl->decls_end(); ++decl_iterator) {
      TransformTopLevelDecl(*decl_iterator);
    }
  } else if (!m_top_level) {
    if (ObjCMethodDecl *method_decl = dyn_cast<ObjCMethodDecl>(D)) {
      if (m_ast_context &&
          !method_decl->getSelector().getAsString().compare("$__lldb_expr:")) {
        RecordPersistentTypes(method_decl);
        SynthesizeObjCMethodResult(method_decl);
      }
    } else if (FunctionDecl *function_decl = dyn_cast<FunctionDecl>(D)) {
      // When completing user input the body of the function may be a nullptr.
      if (m_ast_context && function_decl->hasBody() &&
          !function_decl->getNameInfo().getAsString().compare("$__lldb_expr")) {
        RecordPersistentTypes(function_decl);
        SynthesizeFunctionResult(function_decl);
      }
    }
  }
}

bool ASTResultSynthesizer::HandleTopLevelDecl(DeclGroupRef D) {
  DeclGroupRef::iterator decl_iterator;

  for (decl_iterator = D.begin(); decl_iterator != D.end(); ++decl_iterator) {
    Decl *decl = *decl_iterator;

    TransformTopLevelDecl(decl);
  }

  if (m_passthrough)
    return m_passthrough->HandleTopLevelDecl(D);
  return true;
}

bool ASTResultSynthesizer::SynthesizeFunctionResult(FunctionDecl *FunDecl) {
  Log *log = GetLog(LLDBLog::Expressions);

  if (!m_sema)
    return false;

  FunctionDecl *function_decl = FunDecl;

  if (!function_decl)
    return false;

  if (log && log->GetVerbose()) {
    std::string s;
    raw_string_ostream os(s);

    function_decl->print(os);

    os.flush();

    LLDB_LOGF(log, "Untransformed function AST:\n%s", s.c_str());
  }

  Stmt *function_body = function_decl->getBody();
  CompoundStmt *compound_stmt = dyn_cast<CompoundStmt>(function_body);

  bool ret = SynthesizeBodyResult(compound_stmt, function_decl);

  if (log && log->GetVerbose()) {
    std::string s;
    raw_string_ostream os(s);

    function_decl->print(os);

    os.flush();

    LLDB_LOGF(log, "Transformed function AST:\n%s", s.c_str());
  }

  return ret;
}

bool ASTResultSynthesizer::SynthesizeObjCMethodResult(
    ObjCMethodDecl *MethodDecl) {
  Log *log = GetLog(LLDBLog::Expressions);

  if (!m_sema)
    return false;

  if (!MethodDecl)
    return false;

  if (log && log->GetVerbose()) {
    std::string s;
    raw_string_ostream os(s);

    MethodDecl->print(os);

    os.flush();

    LLDB_LOGF(log, "Untransformed method AST:\n%s", s.c_str());
  }

  Stmt *method_body = MethodDecl->getBody();

  if (!method_body)
    return false;

  CompoundStmt *compound_stmt = dyn_cast<CompoundStmt>(method_body);

  bool ret = SynthesizeBodyResult(compound_stmt, MethodDecl);

  if (log && log->GetVerbose()) {
    std::string s;
    raw_string_ostream os(s);

    MethodDecl->print(os);

    os.flush();

    LLDB_LOGF(log, "Transformed method AST:\n%s", s.c_str());
  }

  return ret;
}

/// Returns true if LLDB can take the address of the given lvalue for the sake
/// of capturing the expression result. Returns false if LLDB should instead
/// store the expression result in a result variable.
static bool CanTakeAddressOfLValue(const Expr *lvalue_expr) {
  assert(lvalue_expr->getValueKind() == VK_LValue &&
         "lvalue_expr not a lvalue");

  QualType qt = lvalue_expr->getType();
  // If the lvalue has const-qualified non-volatile integral or enum type, then
  // the underlying value might come from a const static data member as
  // described in C++11 [class.static.data]p3. If that's the case, then the
  // value might not have an address if the user didn't also define the member
  // in a namespace scope. Taking the address would cause that LLDB later fails
  // to link the expression, so those lvalues should be stored in a result
  // variable.
  if (qt->isIntegralOrEnumerationType() && qt.isConstQualified() &&
      !qt.isVolatileQualified())
    return false;
  return true;
}

bool ASTResultSynthesizer::SynthesizeBodyResult(CompoundStmt *Body,
                                                DeclContext *DC) {
  Log *log = GetLog(LLDBLog::Expressions);

  ASTContext &Ctx(*m_ast_context);

  if (!Body)
    return false;

  if (Body->body_empty())
    return false;

  Stmt **last_stmt_ptr = Body->body_end() - 1;
  Stmt *last_stmt = *last_stmt_ptr;

  while (isa<NullStmt>(last_stmt)) {
    if (last_stmt_ptr != Body->body_begin()) {
      last_stmt_ptr--;
      last_stmt = *last_stmt_ptr;
    } else {
      return false;
    }
  }

  Expr *last_expr = dyn_cast<Expr>(last_stmt);

  if (!last_expr)
    // No auxiliary variable necessary; expression returns void
    return true;

  // In C++11, last_expr can be a LValueToRvalue implicit cast.  Strip that off
  // if that's the case.

  do {
    ImplicitCastExpr *implicit_cast = dyn_cast<ImplicitCastExpr>(last_expr);

    if (!implicit_cast)
      break;

    if (implicit_cast->getCastKind() != CK_LValueToRValue)
      break;

    last_expr = implicit_cast->getSubExpr();
  } while (false);

  // is_lvalue is used to record whether the expression returns an assignable
  // Lvalue or an Rvalue.  This is relevant because they are handled
  // differently.
  //
  // For Lvalues
  //
  //   - In AST result synthesis (here!) the expression E is transformed into an
  //     initialization T *$__lldb_expr_result_ptr = &E.
  //
  //   - In structure allocation, a pointer-sized slot is allocated in the
  //     struct that is to be passed into the expression.
  //
  //   - In IR transformations, reads and writes to $__lldb_expr_result_ptr are
  //     redirected at an entry in the struct ($__lldb_arg) passed into the
  //     expression. (Other persistent variables are treated similarly, having
  //     been materialized as references, but in those cases the value of the
  //     reference itself is never modified.)
  //
  //   - During materialization, $0 (the result persistent variable) is ignored.
  //
  //   - During dematerialization, $0 is marked up as a load address with value
  //     equal to the contents of the structure entry.
  //
  //   - Note: if we cannot take an address of the resulting Lvalue (e.g. it's
  //     a static const member without an out-of-class definition), then we
  //     follow the Rvalue route.
  //
  // For Rvalues
  //
  //   - In AST result synthesis the expression E is transformed into an
  //     initialization static T $__lldb_expr_result = E.
  //
  //   - In structure allocation, a pointer-sized slot is allocated in the
  //     struct that is to be passed into the expression.
  //
  //   - In IR transformations, an instruction is inserted at the beginning of
  //     the function to dereference the pointer resident in the slot. Reads and
  //     writes to $__lldb_expr_result are redirected at that dereferenced
  //     version. Guard variables for the static variable are excised.
  //
  //   - During materialization, $0 (the result persistent variable) is
  //     populated with the location of a newly-allocated area of memory.
  //
  //   - During dematerialization, $0 is ignored.

  bool is_lvalue = last_expr->getValueKind() == VK_LValue &&
                   last_expr->getObjectKind() == OK_Ordinary;

  QualType expr_qual_type = last_expr->getType();
  const clang::Type *expr_type = expr_qual_type.getTypePtr();

  if (!expr_type)
    return false;

  if (expr_type->isVoidType())
    return true;

  if (log) {
    std::string s = expr_qual_type.getAsString();

    LLDB_LOGF(log, "Last statement is an %s with type: %s",
              (is_lvalue ? "lvalue" : "rvalue"), s.c_str());
  }

  clang::VarDecl *result_decl = nullptr;

  if (is_lvalue && CanTakeAddressOfLValue(last_expr)) {
    IdentifierInfo *result_ptr_id;

    if (expr_type->isFunctionType())
      result_ptr_id =
          &Ctx.Idents.get("$__lldb_expr_result"); // functions actually should
                                                  // be treated like function
                                                  // pointers
    else
      result_ptr_id = &Ctx.Idents.get("$__lldb_expr_result_ptr");

    m_sema->RequireCompleteType(last_expr->getSourceRange().getBegin(),
                                expr_qual_type,
                                clang::diag::err_incomplete_type);

    QualType ptr_qual_type;

    if (expr_qual_type->getAs<ObjCObjectType>() != nullptr)
      ptr_qual_type = Ctx.getObjCObjectPointerType(expr_qual_type);
    else
      ptr_qual_type = Ctx.getPointerType(expr_qual_type);

    result_decl =
        VarDecl::Create(Ctx, DC, SourceLocation(), SourceLocation(),
                        result_ptr_id, ptr_qual_type, nullptr, SC_Static);

    if (!result_decl)
      return false;

    ExprResult address_of_expr =
        m_sema->CreateBuiltinUnaryOp(SourceLocation(), UO_AddrOf, last_expr);
    if (address_of_expr.get())
      m_sema->AddInitializerToDecl(result_decl, address_of_expr.get(), true);
    else
      return false;
  } else {
    IdentifierInfo &result_id = Ctx.Idents.get("$__lldb_expr_result");

    result_decl =
        VarDecl::Create(Ctx, DC, SourceLocation(), SourceLocation(), &result_id,
                        expr_qual_type, nullptr, SC_Static);

    if (!result_decl)
      return false;

    m_sema->AddInitializerToDecl(result_decl, last_expr, true);
  }

  DC->addDecl(result_decl);

  ///////////////////////////////
  // call AddInitializerToDecl
  //

  // m_sema->AddInitializerToDecl(result_decl, last_expr);

  /////////////////////////////////
  // call ConvertDeclToDeclGroup
  //

  Sema::DeclGroupPtrTy result_decl_group_ptr;

  result_decl_group_ptr = m_sema->ConvertDeclToDeclGroup(result_decl);

  ////////////////////////
  // call ActOnDeclStmt
  //

  StmtResult result_initialization_stmt_result(m_sema->ActOnDeclStmt(
      result_decl_group_ptr, SourceLocation(), SourceLocation()));

  ////////////////////////////////////////////////
  // replace the old statement with the new one
  //

  *last_stmt_ptr = static_cast<Stmt *>(result_initialization_stmt_result.get());

  return true;
}

void ASTResultSynthesizer::HandleTranslationUnit(ASTContext &Ctx) {
  if (m_passthrough)
    m_passthrough->HandleTranslationUnit(Ctx);
}

void ASTResultSynthesizer::RecordPersistentTypes(DeclContext *FunDeclCtx) {
  typedef DeclContext::specific_decl_iterator<TypeDecl> TypeDeclIterator;

  for (TypeDeclIterator i = TypeDeclIterator(FunDeclCtx->decls_begin()),
                        e = TypeDeclIterator(FunDeclCtx->decls_end());
       i != e; ++i) {
    MaybeRecordPersistentType(*i);
  }
}

void ASTResultSynthesizer::MaybeRecordPersistentType(TypeDecl *D) {
  if (!D->getIdentifier())
    return;

  StringRef name = D->getName();
  if (name.empty() || name.front() != '$')
    return;

  LLDB_LOG(GetLog(LLDBLog::Expressions), "Recording persistent type {0}", name);

  m_decls.push_back(D);
}

void ASTResultSynthesizer::RecordPersistentDecl(NamedDecl *D) {
  lldbassert(m_top_level);

  if (!D->getIdentifier())
    return;

  StringRef name = D->getName();
  if (name.empty())
    return;

  LLDB_LOG(GetLog(LLDBLog::Expressions), "Recording persistent decl {0}", name);

  m_decls.push_back(D);
}

void ASTResultSynthesizer::CommitPersistentDecls() {
  auto *state =
      m_target.GetPersistentExpressionStateForLanguage(lldb::eLanguageTypeC);
  if (!state)
    return;

  auto *persistent_vars = llvm::cast<ClangPersistentVariables>(state);

  lldb::TypeSystemClangSP scratch_ts_sp = ScratchTypeSystemClang::GetForTarget(
      m_target, m_ast_context->getLangOpts());

  for (clang::NamedDecl *decl : m_decls) {
    StringRef name = decl->getName();

    Decl *D_scratch = persistent_vars->GetClangASTImporter()->DeportDecl(
        &scratch_ts_sp->getASTContext(), decl);

    if (!D_scratch) {
      Log *log = GetLog(LLDBLog::Expressions);

      if (log) {
        std::string s;
        llvm::raw_string_ostream ss(s);
        decl->dump(ss);
        ss.flush();

        LLDB_LOGF(log, "Couldn't commit persistent  decl: %s\n", s.c_str());
      }

      continue;
    }

    if (NamedDecl *NamedDecl_scratch = dyn_cast<NamedDecl>(D_scratch))
      persistent_vars->RegisterPersistentDecl(ConstString(name),
                                              NamedDecl_scratch, scratch_ts_sp);
  }
}

void ASTResultSynthesizer::HandleTagDeclDefinition(TagDecl *D) {
  if (m_passthrough)
    m_passthrough->HandleTagDeclDefinition(D);
}

void ASTResultSynthesizer::CompleteTentativeDefinition(VarDecl *D) {
  if (m_passthrough)
    m_passthrough->CompleteTentativeDefinition(D);
}

void ASTResultSynthesizer::HandleVTable(CXXRecordDecl *RD) {
  if (m_passthrough)
    m_passthrough->HandleVTable(RD);
}

void ASTResultSynthesizer::PrintStats() {
  if (m_passthrough)
    m_passthrough->PrintStats();
}

void ASTResultSynthesizer::InitializeSema(Sema &S) {
  m_sema = &S;

  if (m_passthrough_sema)
    m_passthrough_sema->InitializeSema(S);
}

void ASTResultSynthesizer::ForgetSema() {
  m_sema = nullptr;

  if (m_passthrough_sema)
    m_passthrough_sema->ForgetSema();
}
