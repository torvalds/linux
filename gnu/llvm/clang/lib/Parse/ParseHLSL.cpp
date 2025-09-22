//===--- ParseHLSL.cpp - HLSL-specific parsing support --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the parsing logic for HLSL language features.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/Attr.h"
#include "clang/Basic/AttributeCommonInfo.h"
#include "clang/Parse/ParseDiagnostic.h"
#include "clang/Parse/Parser.h"
#include "clang/Parse/RAIIObjectsForParser.h"
#include "clang/Sema/SemaHLSL.h"

using namespace clang;

static bool validateDeclsInsideHLSLBuffer(Parser::DeclGroupPtrTy DG,
                                          SourceLocation BufferLoc,
                                          bool IsCBuffer, Parser &P) {
  // The parse is failed, just return false.
  if (!DG)
    return false;
  DeclGroupRef Decls = DG.get();
  bool IsValid = true;
  // Only allow function, variable, record decls inside HLSLBuffer.
  for (DeclGroupRef::iterator I = Decls.begin(), E = Decls.end(); I != E; ++I) {
    Decl *D = *I;
    if (isa<CXXRecordDecl, RecordDecl, FunctionDecl, VarDecl>(D))
      continue;

    // FIXME: support nested HLSLBuffer and namespace inside HLSLBuffer.
    if (isa<HLSLBufferDecl, NamespaceDecl>(D)) {
      P.Diag(D->getLocation(), diag::err_invalid_declaration_in_hlsl_buffer)
          << IsCBuffer;
      IsValid = false;
      continue;
    }

    IsValid = false;
    P.Diag(D->getLocation(), diag::err_invalid_declaration_in_hlsl_buffer)
        << IsCBuffer;
  }
  return IsValid;
}

Decl *Parser::ParseHLSLBuffer(SourceLocation &DeclEnd) {
  assert((Tok.is(tok::kw_cbuffer) || Tok.is(tok::kw_tbuffer)) &&
         "Not a cbuffer or tbuffer!");
  bool IsCBuffer = Tok.is(tok::kw_cbuffer);
  SourceLocation BufferLoc = ConsumeToken(); // Eat the 'cbuffer' or 'tbuffer'.

  if (!Tok.is(tok::identifier)) {
    Diag(Tok, diag::err_expected) << tok::identifier;
    return nullptr;
  }

  IdentifierInfo *Identifier = Tok.getIdentifierInfo();
  SourceLocation IdentifierLoc = ConsumeToken();

  ParsedAttributes Attrs(AttrFactory);
  MaybeParseHLSLAnnotations(Attrs, nullptr);

  ParseScope BufferScope(this, Scope::DeclScope);
  BalancedDelimiterTracker T(*this, tok::l_brace);
  if (T.consumeOpen()) {
    Diag(Tok, diag::err_expected) << tok::l_brace;
    return nullptr;
  }

  Decl *D = Actions.HLSL().ActOnStartBuffer(getCurScope(), IsCBuffer, BufferLoc,
                                            Identifier, IdentifierLoc,
                                            T.getOpenLocation());

  while (Tok.isNot(tok::r_brace) && Tok.isNot(tok::eof)) {
    // FIXME: support attribute on constants inside cbuffer/tbuffer.
    ParsedAttributes DeclAttrs(AttrFactory);
    ParsedAttributes EmptyDeclSpecAttrs(AttrFactory);

    DeclGroupPtrTy Result =
        ParseExternalDeclaration(DeclAttrs, EmptyDeclSpecAttrs);
    if (!validateDeclsInsideHLSLBuffer(Result, IdentifierLoc, IsCBuffer,
                                       *this)) {
      T.skipToEnd();
      DeclEnd = T.getCloseLocation();
      BufferScope.Exit();
      Actions.HLSL().ActOnFinishBuffer(D, DeclEnd);
      return nullptr;
    }
  }

  T.consumeClose();
  DeclEnd = T.getCloseLocation();
  BufferScope.Exit();
  Actions.HLSL().ActOnFinishBuffer(D, DeclEnd);

  Actions.ProcessDeclAttributeList(Actions.CurScope, D, Attrs);
  return D;
}

static void fixSeparateAttrArgAndNumber(StringRef ArgStr, SourceLocation ArgLoc,
                                        Token Tok, ArgsVector &ArgExprs,
                                        Parser &P, ASTContext &Ctx,
                                        Preprocessor &PP) {
  StringRef Num = StringRef(Tok.getLiteralData(), Tok.getLength());
  SourceLocation EndNumLoc = Tok.getEndLoc();

  P.ConsumeToken(); // consume constant.
  std::string FixedArg = ArgStr.str() + Num.str();
  P.Diag(ArgLoc, diag::err_hlsl_separate_attr_arg_and_number)
      << FixedArg
      << FixItHint::CreateReplacement(SourceRange(ArgLoc, EndNumLoc), FixedArg);
  ArgsUnion &Slot = ArgExprs.back();
  Slot = IdentifierLoc::create(Ctx, ArgLoc, PP.getIdentifierInfo(FixedArg));
}

void Parser::ParseHLSLAnnotations(ParsedAttributes &Attrs,
                                  SourceLocation *EndLoc,
                                  bool CouldBeBitField) {

  assert(Tok.is(tok::colon) && "Not a HLSL Annotation");
  Token OldToken = Tok;
  ConsumeToken();

  IdentifierInfo *II = nullptr;
  if (Tok.is(tok::kw_register))
    II = PP.getIdentifierInfo("register");
  else if (Tok.is(tok::identifier))
    II = Tok.getIdentifierInfo();

  if (!II) {
    if (CouldBeBitField) {
      UnconsumeToken(OldToken);
      return;
    }
    Diag(Tok.getLocation(), diag::err_expected_semantic_identifier);
    return;
  }

  SourceLocation Loc = ConsumeToken();
  if (EndLoc)
    *EndLoc = Tok.getLocation();
  ParsedAttr::Kind AttrKind =
      ParsedAttr::getParsedKind(II, nullptr, ParsedAttr::AS_HLSLAnnotation);

  ArgsVector ArgExprs;
  switch (AttrKind) {
  case ParsedAttr::AT_HLSLResourceBinding: {
    if (ExpectAndConsume(tok::l_paren, diag::err_expected_lparen_after)) {
      SkipUntil(tok::r_paren, StopAtSemi); // skip through )
      return;
    }
    if (!Tok.is(tok::identifier)) {
      Diag(Tok.getLocation(), diag::err_expected) << tok::identifier;
      SkipUntil(tok::r_paren, StopAtSemi); // skip through )
      return;
    }
    StringRef SlotStr = Tok.getIdentifierInfo()->getName();
    SourceLocation SlotLoc = Tok.getLocation();
    ArgExprs.push_back(ParseIdentifierLoc());

    // Add numeric_constant for fix-it.
    if (SlotStr.size() == 1 && Tok.is(tok::numeric_constant))
      fixSeparateAttrArgAndNumber(SlotStr, SlotLoc, Tok, ArgExprs, *this,
                                  Actions.Context, PP);

    if (Tok.is(tok::comma)) {
      ConsumeToken(); // consume comma
      if (!Tok.is(tok::identifier)) {
        Diag(Tok.getLocation(), diag::err_expected) << tok::identifier;
        SkipUntil(tok::r_paren, StopAtSemi); // skip through )
        return;
      }
      StringRef SpaceStr = Tok.getIdentifierInfo()->getName();
      SourceLocation SpaceLoc = Tok.getLocation();
      ArgExprs.push_back(ParseIdentifierLoc());

      // Add numeric_constant for fix-it.
      if (SpaceStr == "space" && Tok.is(tok::numeric_constant))
        fixSeparateAttrArgAndNumber(SpaceStr, SpaceLoc, Tok, ArgExprs, *this,
                                    Actions.Context, PP);
    }
    if (ExpectAndConsume(tok::r_paren, diag::err_expected)) {
      SkipUntil(tok::r_paren, StopAtSemi); // skip through )
      return;
    }
  } break;
  case ParsedAttr::AT_HLSLPackOffset: {
    // Parse 'packoffset( c[Subcomponent][.component] )'.
    // Check '('.
    if (ExpectAndConsume(tok::l_paren, diag::err_expected_lparen_after)) {
      SkipUntil(tok::r_paren, StopAtSemi); // skip through )
      return;
    }
    // Check c[Subcomponent] as an identifier.
    if (!Tok.is(tok::identifier)) {
      Diag(Tok.getLocation(), diag::err_expected) << tok::identifier;
      SkipUntil(tok::r_paren, StopAtSemi); // skip through )
      return;
    }
    StringRef OffsetStr = Tok.getIdentifierInfo()->getName();
    SourceLocation SubComponentLoc = Tok.getLocation();
    if (OffsetStr[0] != 'c') {
      Diag(Tok.getLocation(), diag::err_hlsl_packoffset_invalid_reg)
          << OffsetStr;
      SkipUntil(tok::r_paren, StopAtSemi); // skip through )
      return;
    }
    OffsetStr = OffsetStr.substr(1);
    unsigned SubComponent = 0;
    if (!OffsetStr.empty()) {
      // Make sure SubComponent is a number.
      if (OffsetStr.getAsInteger(10, SubComponent)) {
        Diag(SubComponentLoc.getLocWithOffset(1),
             diag::err_hlsl_unsupported_register_number);
        SkipUntil(tok::r_paren, StopAtSemi); // skip through )
        return;
      }
    }
    unsigned Component = 0;
    ConsumeToken(); // consume identifier.
    SourceLocation ComponentLoc;
    if (Tok.is(tok::period)) {
      ConsumeToken(); // consume period.
      if (!Tok.is(tok::identifier)) {
        Diag(Tok.getLocation(), diag::err_expected) << tok::identifier;
        SkipUntil(tok::r_paren, StopAtSemi); // skip through )
        return;
      }
      StringRef ComponentStr = Tok.getIdentifierInfo()->getName();
      ComponentLoc = Tok.getLocation();
      ConsumeToken(); // consume identifier.
      // Make sure Component is a single character.
      if (ComponentStr.size() != 1) {
        Diag(ComponentLoc, diag::err_hlsl_unsupported_component)
            << ComponentStr;
        SkipUntil(tok::r_paren, StopAtSemi); // skip through )
        return;
      }
      switch (ComponentStr[0]) {
      case 'x':
      case 'r':
        Component = 0;
        break;
      case 'y':
      case 'g':
        Component = 1;
        break;
      case 'z':
      case 'b':
        Component = 2;
        break;
      case 'w':
      case 'a':
        Component = 3;
        break;
      default:
        Diag(ComponentLoc, diag::err_hlsl_unsupported_component)
            << ComponentStr;
        SkipUntil(tok::r_paren, StopAtSemi); // skip through )
        return;
      }
    }
    ASTContext &Ctx = Actions.getASTContext();
    QualType SizeTy = Ctx.getSizeType();
    uint64_t SizeTySize = Ctx.getTypeSize(SizeTy);
    ArgExprs.push_back(IntegerLiteral::Create(
        Ctx, llvm::APInt(SizeTySize, SubComponent), SizeTy, SubComponentLoc));
    ArgExprs.push_back(IntegerLiteral::Create(
        Ctx, llvm::APInt(SizeTySize, Component), SizeTy, ComponentLoc));
    if (ExpectAndConsume(tok::r_paren, diag::err_expected)) {
      SkipUntil(tok::r_paren, StopAtSemi); // skip through )
      return;
    }
  } break;
  case ParsedAttr::UnknownAttribute:
    Diag(Loc, diag::err_unknown_hlsl_semantic) << II;
    return;
  case ParsedAttr::AT_HLSLSV_GroupIndex:
  case ParsedAttr::AT_HLSLSV_DispatchThreadID:
    break;
  default:
    llvm_unreachable("invalid HLSL Annotation");
    break;
  }

  Attrs.addNew(II, Loc, nullptr, SourceLocation(), ArgExprs.data(),
               ArgExprs.size(), ParsedAttr::Form::HLSLAnnotation());
}
