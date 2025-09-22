//===-- ASTStructExtractor.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ASTStructExtractor.h"

#include "lldb/Utility/Log.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclGroup.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/Stmt.h"
#include "clang/Parse/Parser.h"
#include "clang/Sema/Sema.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdlib>

using namespace llvm;
using namespace clang;
using namespace lldb_private;

ASTStructExtractor::ASTStructExtractor(ASTConsumer *passthrough,
                                       const char *struct_name,
                                       ClangFunctionCaller &function)
    : m_ast_context(nullptr), m_passthrough(passthrough),
      m_passthrough_sema(nullptr), m_sema(nullptr), m_function(function),
      m_struct_name(struct_name) {
  if (!m_passthrough)
    return;

  m_passthrough_sema = dyn_cast<SemaConsumer>(passthrough);
}

ASTStructExtractor::~ASTStructExtractor() = default;

void ASTStructExtractor::Initialize(ASTContext &Context) {
  m_ast_context = &Context;

  if (m_passthrough)
    m_passthrough->Initialize(Context);
}

void ASTStructExtractor::ExtractFromFunctionDecl(FunctionDecl *F) {
  if (!F->hasBody())
    return;

  Stmt *body_stmt = F->getBody();
  CompoundStmt *body_compound_stmt = dyn_cast<CompoundStmt>(body_stmt);

  if (!body_compound_stmt)
    return; // do we have to handle this?

  RecordDecl *struct_decl = nullptr;

  StringRef desired_name(m_struct_name);

  for (CompoundStmt::const_body_iterator bi = body_compound_stmt->body_begin(),
                                         be = body_compound_stmt->body_end();
       bi != be; ++bi) {
    Stmt *curr_stmt = *bi;
    DeclStmt *curr_decl_stmt = dyn_cast<DeclStmt>(curr_stmt);
    if (!curr_decl_stmt)
      continue;
    DeclGroupRef decl_group = curr_decl_stmt->getDeclGroup();
    for (Decl *candidate_decl : decl_group) {
      RecordDecl *candidate_record_decl = dyn_cast<RecordDecl>(candidate_decl);
      if (!candidate_record_decl)
        continue;
      if (candidate_record_decl->getName() == desired_name) {
        struct_decl = candidate_record_decl;
        break;
      }
    }
    if (struct_decl)
      break;
  }

  if (!struct_decl)
    return;

  const ASTRecordLayout *struct_layout(
      &m_ast_context->getASTRecordLayout(struct_decl));

  if (!struct_layout)
    return;

  m_function.m_struct_size =
      struct_layout->getSize()
          .getQuantity(); // TODO Store m_struct_size as CharUnits
  m_function.m_return_offset =
      struct_layout->getFieldOffset(struct_layout->getFieldCount() - 1) / 8;
  m_function.m_return_size =
      struct_layout->getDataSize().getQuantity() - m_function.m_return_offset;

  for (unsigned field_index = 0, num_fields = struct_layout->getFieldCount();
       field_index < num_fields; ++field_index) {
    m_function.m_member_offsets.push_back(
        struct_layout->getFieldOffset(field_index) / 8);
  }

  m_function.m_struct_valid = true;
}

void ASTStructExtractor::ExtractFromTopLevelDecl(Decl *D) {
  LinkageSpecDecl *linkage_spec_decl = dyn_cast<LinkageSpecDecl>(D);

  if (linkage_spec_decl) {
    RecordDecl::decl_iterator decl_iterator;

    for (decl_iterator = linkage_spec_decl->decls_begin();
         decl_iterator != linkage_spec_decl->decls_end(); ++decl_iterator) {
      ExtractFromTopLevelDecl(*decl_iterator);
    }
  }

  FunctionDecl *function_decl = dyn_cast<FunctionDecl>(D);

  if (m_ast_context && function_decl &&
      !m_function.m_wrapper_function_name.compare(
          function_decl->getNameAsString())) {
    ExtractFromFunctionDecl(function_decl);
  }
}

bool ASTStructExtractor::HandleTopLevelDecl(DeclGroupRef D) {
  DeclGroupRef::iterator decl_iterator;

  for (decl_iterator = D.begin(); decl_iterator != D.end(); ++decl_iterator) {
    Decl *decl = *decl_iterator;

    ExtractFromTopLevelDecl(decl);
  }

  if (m_passthrough)
    return m_passthrough->HandleTopLevelDecl(D);
  return true;
}

void ASTStructExtractor::HandleTranslationUnit(ASTContext &Ctx) {
  if (m_passthrough)
    m_passthrough->HandleTranslationUnit(Ctx);
}

void ASTStructExtractor::HandleTagDeclDefinition(TagDecl *D) {
  if (m_passthrough)
    m_passthrough->HandleTagDeclDefinition(D);
}

void ASTStructExtractor::CompleteTentativeDefinition(VarDecl *D) {
  if (m_passthrough)
    m_passthrough->CompleteTentativeDefinition(D);
}

void ASTStructExtractor::HandleVTable(CXXRecordDecl *RD) {
  if (m_passthrough)
    m_passthrough->HandleVTable(RD);
}

void ASTStructExtractor::PrintStats() {
  if (m_passthrough)
    m_passthrough->PrintStats();
}

void ASTStructExtractor::InitializeSema(Sema &S) {
  m_sema = &S;

  if (m_passthrough_sema)
    m_passthrough_sema->InitializeSema(S);
}

void ASTStructExtractor::ForgetSema() {
  m_sema = nullptr;

  if (m_passthrough_sema)
    m_passthrough_sema->ForgetSema();
}
