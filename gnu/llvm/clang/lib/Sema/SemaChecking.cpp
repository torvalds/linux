//===- SemaChecking.cpp - Extra Semantic Checking -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements extra semantic analysis beyond what is enforced
//  by the C type system.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/APValue.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/AttrIterator.h"
#include "clang/AST/CharUnits.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/EvaluatedExprVisitor.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/ExprOpenMP.h"
#include "clang/AST/FormatString.h"
#include "clang/AST/IgnoreExpr.h"
#include "clang/AST/NSAPI.h"
#include "clang/AST/NonTrivialTypeVisitor.h"
#include "clang/AST/OperationKinds.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/TemplateBase.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/AST/UnresolvedSet.h"
#include "clang/Basic/AddressSpaces.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/OpenCLOptions.h"
#include "clang/Basic/OperatorKinds.h"
#include "clang/Basic/PartialDiagnostic.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Basic/SyncScope.h"
#include "clang/Basic/TargetBuiltins.h"
#include "clang/Basic/TargetCXXABI.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TypeTraits.h"
#include "clang/Lex/Lexer.h" // TODO: Extract static functions to fix layering.
#include "clang/Sema/Initialization.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/Ownership.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/ScopeInfo.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaAMDGPU.h"
#include "clang/Sema/SemaARM.h"
#include "clang/Sema/SemaBPF.h"
#include "clang/Sema/SemaHLSL.h"
#include "clang/Sema/SemaHexagon.h"
#include "clang/Sema/SemaInternal.h"
#include "clang/Sema/SemaLoongArch.h"
#include "clang/Sema/SemaMIPS.h"
#include "clang/Sema/SemaNVPTX.h"
#include "clang/Sema/SemaObjC.h"
#include "clang/Sema/SemaOpenCL.h"
#include "clang/Sema/SemaPPC.h"
#include "clang/Sema/SemaRISCV.h"
#include "clang/Sema/SemaSystemZ.h"
#include "clang/Sema/SemaWasm.h"
#include "clang/Sema/SemaX86.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/Locale.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/SaveAndRestore.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/RISCVTargetParser.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <bitset>
#include <cassert>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

using namespace clang;
using namespace sema;

SourceLocation Sema::getLocationOfStringLiteralByte(const StringLiteral *SL,
                                                    unsigned ByteNo) const {
  return SL->getLocationOfByte(ByteNo, getSourceManager(), LangOpts,
                               Context.getTargetInfo());
}

static constexpr unsigned short combineFAPK(Sema::FormatArgumentPassingKind A,
                                            Sema::FormatArgumentPassingKind B) {
  return (A << 8) | B;
}

bool Sema::checkArgCountAtLeast(CallExpr *Call, unsigned MinArgCount) {
  unsigned ArgCount = Call->getNumArgs();
  if (ArgCount >= MinArgCount)
    return false;

  return Diag(Call->getEndLoc(), diag::err_typecheck_call_too_few_args)
         << 0 /*function call*/ << MinArgCount << ArgCount
         << /*is non object*/ 0 << Call->getSourceRange();
}

bool Sema::checkArgCountAtMost(CallExpr *Call, unsigned MaxArgCount) {
  unsigned ArgCount = Call->getNumArgs();
  if (ArgCount <= MaxArgCount)
    return false;
  return Diag(Call->getEndLoc(), diag::err_typecheck_call_too_many_args_at_most)
         << 0 /*function call*/ << MaxArgCount << ArgCount
         << /*is non object*/ 0 << Call->getSourceRange();
}

bool Sema::checkArgCountRange(CallExpr *Call, unsigned MinArgCount,
                              unsigned MaxArgCount) {
  return checkArgCountAtLeast(Call, MinArgCount) ||
         checkArgCountAtMost(Call, MaxArgCount);
}

bool Sema::checkArgCount(CallExpr *Call, unsigned DesiredArgCount) {
  unsigned ArgCount = Call->getNumArgs();
  if (ArgCount == DesiredArgCount)
    return false;

  if (checkArgCountAtLeast(Call, DesiredArgCount))
    return true;
  assert(ArgCount > DesiredArgCount && "should have diagnosed this");

  // Highlight all the excess arguments.
  SourceRange Range(Call->getArg(DesiredArgCount)->getBeginLoc(),
                    Call->getArg(ArgCount - 1)->getEndLoc());

  return Diag(Range.getBegin(), diag::err_typecheck_call_too_many_args)
         << 0 /*function call*/ << DesiredArgCount << ArgCount
         << /*is non object*/ 0 << Call->getArg(1)->getSourceRange();
}

static bool checkBuiltinVerboseTrap(CallExpr *Call, Sema &S) {
  bool HasError = false;

  for (unsigned I = 0; I < Call->getNumArgs(); ++I) {
    Expr *Arg = Call->getArg(I);

    if (Arg->isValueDependent())
      continue;

    std::optional<std::string> ArgString = Arg->tryEvaluateString(S.Context);
    int DiagMsgKind = -1;
    // Arguments must be pointers to constant strings and cannot use '$'.
    if (!ArgString.has_value())
      DiagMsgKind = 0;
    else if (ArgString->find('$') != std::string::npos)
      DiagMsgKind = 1;

    if (DiagMsgKind >= 0) {
      S.Diag(Arg->getBeginLoc(), diag::err_builtin_verbose_trap_arg)
          << DiagMsgKind << Arg->getSourceRange();
      HasError = true;
    }
  }

  return !HasError;
}

static bool convertArgumentToType(Sema &S, Expr *&Value, QualType Ty) {
  if (Value->isTypeDependent())
    return false;

  InitializedEntity Entity =
      InitializedEntity::InitializeParameter(S.Context, Ty, false);
  ExprResult Result =
      S.PerformCopyInitialization(Entity, SourceLocation(), Value);
  if (Result.isInvalid())
    return true;
  Value = Result.get();
  return false;
}

/// Check that the first argument to __builtin_annotation is an integer
/// and the second argument is a non-wide string literal.
static bool BuiltinAnnotation(Sema &S, CallExpr *TheCall) {
  if (S.checkArgCount(TheCall, 2))
    return true;

  // First argument should be an integer.
  Expr *ValArg = TheCall->getArg(0);
  QualType Ty = ValArg->getType();
  if (!Ty->isIntegerType()) {
    S.Diag(ValArg->getBeginLoc(), diag::err_builtin_annotation_first_arg)
        << ValArg->getSourceRange();
    return true;
  }

  // Second argument should be a constant string.
  Expr *StrArg = TheCall->getArg(1)->IgnoreParenCasts();
  StringLiteral *Literal = dyn_cast<StringLiteral>(StrArg);
  if (!Literal || !Literal->isOrdinary()) {
    S.Diag(StrArg->getBeginLoc(), diag::err_builtin_annotation_second_arg)
        << StrArg->getSourceRange();
    return true;
  }

  TheCall->setType(Ty);
  return false;
}

static bool BuiltinMSVCAnnotation(Sema &S, CallExpr *TheCall) {
  // We need at least one argument.
  if (TheCall->getNumArgs() < 1) {
    S.Diag(TheCall->getEndLoc(), diag::err_typecheck_call_too_few_args_at_least)
        << 0 << 1 << TheCall->getNumArgs() << /*is non object*/ 0
        << TheCall->getCallee()->getSourceRange();
    return true;
  }

  // All arguments should be wide string literals.
  for (Expr *Arg : TheCall->arguments()) {
    auto *Literal = dyn_cast<StringLiteral>(Arg->IgnoreParenCasts());
    if (!Literal || !Literal->isWide()) {
      S.Diag(Arg->getBeginLoc(), diag::err_msvc_annotation_wide_str)
          << Arg->getSourceRange();
      return true;
    }
  }

  return false;
}

/// Check that the argument to __builtin_addressof is a glvalue, and set the
/// result type to the corresponding pointer type.
static bool BuiltinAddressof(Sema &S, CallExpr *TheCall) {
  if (S.checkArgCount(TheCall, 1))
    return true;

  ExprResult Arg(TheCall->getArg(0));
  QualType ResultType = S.CheckAddressOfOperand(Arg, TheCall->getBeginLoc());
  if (ResultType.isNull())
    return true;

  TheCall->setArg(0, Arg.get());
  TheCall->setType(ResultType);
  return false;
}

/// Check that the argument to __builtin_function_start is a function.
static bool BuiltinFunctionStart(Sema &S, CallExpr *TheCall) {
  if (S.checkArgCount(TheCall, 1))
    return true;

  ExprResult Arg = S.DefaultFunctionArrayLvalueConversion(TheCall->getArg(0));
  if (Arg.isInvalid())
    return true;

  TheCall->setArg(0, Arg.get());
  const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(
      Arg.get()->getAsBuiltinConstantDeclRef(S.getASTContext()));

  if (!FD) {
    S.Diag(TheCall->getBeginLoc(), diag::err_function_start_invalid_type)
        << TheCall->getSourceRange();
    return true;
  }

  return !S.checkAddressOfFunctionIsAvailable(FD, /*Complain=*/true,
                                              TheCall->getBeginLoc());
}

/// Check the number of arguments and set the result type to
/// the argument type.
static bool BuiltinPreserveAI(Sema &S, CallExpr *TheCall) {
  if (S.checkArgCount(TheCall, 1))
    return true;

  TheCall->setType(TheCall->getArg(0)->getType());
  return false;
}

/// Check that the value argument for __builtin_is_aligned(value, alignment) and
/// __builtin_aligned_{up,down}(value, alignment) is an integer or a pointer
/// type (but not a function pointer) and that the alignment is a power-of-two.
static bool BuiltinAlignment(Sema &S, CallExpr *TheCall, unsigned ID) {
  if (S.checkArgCount(TheCall, 2))
    return true;

  clang::Expr *Source = TheCall->getArg(0);
  bool IsBooleanAlignBuiltin = ID == Builtin::BI__builtin_is_aligned;

  auto IsValidIntegerType = [](QualType Ty) {
    return Ty->isIntegerType() && !Ty->isEnumeralType() && !Ty->isBooleanType();
  };
  QualType SrcTy = Source->getType();
  // We should also be able to use it with arrays (but not functions!).
  if (SrcTy->canDecayToPointerType() && SrcTy->isArrayType()) {
    SrcTy = S.Context.getDecayedType(SrcTy);
  }
  if ((!SrcTy->isPointerType() && !IsValidIntegerType(SrcTy)) ||
      SrcTy->isFunctionPointerType()) {
    // FIXME: this is not quite the right error message since we don't allow
    // floating point types, or member pointers.
    S.Diag(Source->getExprLoc(), diag::err_typecheck_expect_scalar_operand)
        << SrcTy;
    return true;
  }

  clang::Expr *AlignOp = TheCall->getArg(1);
  if (!IsValidIntegerType(AlignOp->getType())) {
    S.Diag(AlignOp->getExprLoc(), diag::err_typecheck_expect_int)
        << AlignOp->getType();
    return true;
  }
  Expr::EvalResult AlignResult;
  unsigned MaxAlignmentBits = S.Context.getIntWidth(SrcTy) - 1;
  // We can't check validity of alignment if it is value dependent.
  if (!AlignOp->isValueDependent() &&
      AlignOp->EvaluateAsInt(AlignResult, S.Context,
                             Expr::SE_AllowSideEffects)) {
    llvm::APSInt AlignValue = AlignResult.Val.getInt();
    llvm::APSInt MaxValue(
        llvm::APInt::getOneBitSet(MaxAlignmentBits + 1, MaxAlignmentBits));
    if (AlignValue < 1) {
      S.Diag(AlignOp->getExprLoc(), diag::err_alignment_too_small) << 1;
      return true;
    }
    if (llvm::APSInt::compareValues(AlignValue, MaxValue) > 0) {
      S.Diag(AlignOp->getExprLoc(), diag::err_alignment_too_big)
          << toString(MaxValue, 10);
      return true;
    }
    if (!AlignValue.isPowerOf2()) {
      S.Diag(AlignOp->getExprLoc(), diag::err_alignment_not_power_of_two);
      return true;
    }
    if (AlignValue == 1) {
      S.Diag(AlignOp->getExprLoc(), diag::warn_alignment_builtin_useless)
          << IsBooleanAlignBuiltin;
    }
  }

  ExprResult SrcArg = S.PerformCopyInitialization(
      InitializedEntity::InitializeParameter(S.Context, SrcTy, false),
      SourceLocation(), Source);
  if (SrcArg.isInvalid())
    return true;
  TheCall->setArg(0, SrcArg.get());
  ExprResult AlignArg =
      S.PerformCopyInitialization(InitializedEntity::InitializeParameter(
                                      S.Context, AlignOp->getType(), false),
                                  SourceLocation(), AlignOp);
  if (AlignArg.isInvalid())
    return true;
  TheCall->setArg(1, AlignArg.get());
  // For align_up/align_down, the return type is the same as the (potentially
  // decayed) argument type including qualifiers. For is_aligned(), the result
  // is always bool.
  TheCall->setType(IsBooleanAlignBuiltin ? S.Context.BoolTy : SrcTy);
  return false;
}

static bool BuiltinOverflow(Sema &S, CallExpr *TheCall, unsigned BuiltinID) {
  if (S.checkArgCount(TheCall, 3))
    return true;

  std::pair<unsigned, const char *> Builtins[] = {
    { Builtin::BI__builtin_add_overflow, "ckd_add" },
    { Builtin::BI__builtin_sub_overflow, "ckd_sub" },
    { Builtin::BI__builtin_mul_overflow, "ckd_mul" },
  };

  bool CkdOperation = llvm::any_of(Builtins, [&](const std::pair<unsigned,
    const char *> &P) {
    return BuiltinID == P.first && TheCall->getExprLoc().isMacroID() &&
         Lexer::getImmediateMacroName(TheCall->getExprLoc(),
         S.getSourceManager(), S.getLangOpts()) == P.second;
  });

  auto ValidCkdIntType = [](QualType QT) {
    // A valid checked integer type is an integer type other than a plain char,
    // bool, a bit-precise type, or an enumeration type.
    if (const auto *BT = QT.getCanonicalType()->getAs<BuiltinType>())
      return (BT->getKind() >= BuiltinType::Short &&
           BT->getKind() <= BuiltinType::Int128) || (
           BT->getKind() >= BuiltinType::UShort &&
           BT->getKind() <= BuiltinType::UInt128) ||
           BT->getKind() == BuiltinType::UChar ||
           BT->getKind() == BuiltinType::SChar;
    return false;
  };

  // First two arguments should be integers.
  for (unsigned I = 0; I < 2; ++I) {
    ExprResult Arg = S.DefaultFunctionArrayLvalueConversion(TheCall->getArg(I));
    if (Arg.isInvalid()) return true;
    TheCall->setArg(I, Arg.get());

    QualType Ty = Arg.get()->getType();
    bool IsValid = CkdOperation ? ValidCkdIntType(Ty) : Ty->isIntegerType();
    if (!IsValid) {
      S.Diag(Arg.get()->getBeginLoc(), diag::err_overflow_builtin_must_be_int)
          << CkdOperation << Ty << Arg.get()->getSourceRange();
      return true;
    }
  }

  // Third argument should be a pointer to a non-const integer.
  // IRGen correctly handles volatile, restrict, and address spaces, and
  // the other qualifiers aren't possible.
  {
    ExprResult Arg = S.DefaultFunctionArrayLvalueConversion(TheCall->getArg(2));
    if (Arg.isInvalid()) return true;
    TheCall->setArg(2, Arg.get());

    QualType Ty = Arg.get()->getType();
    const auto *PtrTy = Ty->getAs<PointerType>();
    if (!PtrTy ||
        !PtrTy->getPointeeType()->isIntegerType() ||
        (!ValidCkdIntType(PtrTy->getPointeeType()) && CkdOperation) ||
        PtrTy->getPointeeType().isConstQualified()) {
      S.Diag(Arg.get()->getBeginLoc(),
             diag::err_overflow_builtin_must_be_ptr_int)
        << CkdOperation << Ty << Arg.get()->getSourceRange();
      return true;
    }
  }

  // Disallow signed bit-precise integer args larger than 128 bits to mul
  // function until we improve backend support.
  if (BuiltinID == Builtin::BI__builtin_mul_overflow) {
    for (unsigned I = 0; I < 3; ++I) {
      const auto Arg = TheCall->getArg(I);
      // Third argument will be a pointer.
      auto Ty = I < 2 ? Arg->getType() : Arg->getType()->getPointeeType();
      if (Ty->isBitIntType() && Ty->isSignedIntegerType() &&
          S.getASTContext().getIntWidth(Ty) > 128)
        return S.Diag(Arg->getBeginLoc(),
                      diag::err_overflow_builtin_bit_int_max_size)
               << 128;
    }
  }

  return false;
}

namespace {
struct BuiltinDumpStructGenerator {
  Sema &S;
  CallExpr *TheCall;
  SourceLocation Loc = TheCall->getBeginLoc();
  SmallVector<Expr *, 32> Actions;
  DiagnosticErrorTrap ErrorTracker;
  PrintingPolicy Policy;

  BuiltinDumpStructGenerator(Sema &S, CallExpr *TheCall)
      : S(S), TheCall(TheCall), ErrorTracker(S.getDiagnostics()),
        Policy(S.Context.getPrintingPolicy()) {
    Policy.AnonymousTagLocations = false;
  }

  Expr *makeOpaqueValueExpr(Expr *Inner) {
    auto *OVE = new (S.Context)
        OpaqueValueExpr(Loc, Inner->getType(), Inner->getValueKind(),
                        Inner->getObjectKind(), Inner);
    Actions.push_back(OVE);
    return OVE;
  }

  Expr *getStringLiteral(llvm::StringRef Str) {
    Expr *Lit = S.Context.getPredefinedStringLiteralFromCache(Str);
    // Wrap the literal in parentheses to attach a source location.
    return new (S.Context) ParenExpr(Loc, Loc, Lit);
  }

  bool callPrintFunction(llvm::StringRef Format,
                         llvm::ArrayRef<Expr *> Exprs = {}) {
    SmallVector<Expr *, 8> Args;
    assert(TheCall->getNumArgs() >= 2);
    Args.reserve((TheCall->getNumArgs() - 2) + /*Format*/ 1 + Exprs.size());
    Args.assign(TheCall->arg_begin() + 2, TheCall->arg_end());
    Args.push_back(getStringLiteral(Format));
    Args.insert(Args.end(), Exprs.begin(), Exprs.end());

    // Register a note to explain why we're performing the call.
    Sema::CodeSynthesisContext Ctx;
    Ctx.Kind = Sema::CodeSynthesisContext::BuildingBuiltinDumpStructCall;
    Ctx.PointOfInstantiation = Loc;
    Ctx.CallArgs = Args.data();
    Ctx.NumCallArgs = Args.size();
    S.pushCodeSynthesisContext(Ctx);

    ExprResult RealCall =
        S.BuildCallExpr(/*Scope=*/nullptr, TheCall->getArg(1),
                        TheCall->getBeginLoc(), Args, TheCall->getRParenLoc());

    S.popCodeSynthesisContext();
    if (!RealCall.isInvalid())
      Actions.push_back(RealCall.get());
    // Bail out if we've hit any errors, even if we managed to build the
    // call. We don't want to produce more than one error.
    return RealCall.isInvalid() || ErrorTracker.hasErrorOccurred();
  }

  Expr *getIndentString(unsigned Depth) {
    if (!Depth)
      return nullptr;

    llvm::SmallString<32> Indent;
    Indent.resize(Depth * Policy.Indentation, ' ');
    return getStringLiteral(Indent);
  }

  Expr *getTypeString(QualType T) {
    return getStringLiteral(T.getAsString(Policy));
  }

  bool appendFormatSpecifier(QualType T, llvm::SmallVectorImpl<char> &Str) {
    llvm::raw_svector_ostream OS(Str);

    // Format 'bool', 'char', 'signed char', 'unsigned char' as numbers, rather
    // than trying to print a single character.
    if (auto *BT = T->getAs<BuiltinType>()) {
      switch (BT->getKind()) {
      case BuiltinType::Bool:
        OS << "%d";
        return true;
      case BuiltinType::Char_U:
      case BuiltinType::UChar:
        OS << "%hhu";
        return true;
      case BuiltinType::Char_S:
      case BuiltinType::SChar:
        OS << "%hhd";
        return true;
      default:
        break;
      }
    }

    analyze_printf::PrintfSpecifier Specifier;
    if (Specifier.fixType(T, S.getLangOpts(), S.Context, /*IsObjCLiteral=*/false)) {
      // We were able to guess how to format this.
      if (Specifier.getConversionSpecifier().getKind() ==
          analyze_printf::PrintfConversionSpecifier::sArg) {
        // Wrap double-quotes around a '%s' specifier and limit its maximum
        // length. Ideally we'd also somehow escape special characters in the
        // contents but printf doesn't support that.
        // FIXME: '%s' formatting is not safe in general.
        OS << '"';
        Specifier.setPrecision(analyze_printf::OptionalAmount(32u));
        Specifier.toString(OS);
        OS << '"';
        // FIXME: It would be nice to include a '...' if the string doesn't fit
        // in the length limit.
      } else {
        Specifier.toString(OS);
      }
      return true;
    }

    if (T->isPointerType()) {
      // Format all pointers with '%p'.
      OS << "%p";
      return true;
    }

    return false;
  }

  bool dumpUnnamedRecord(const RecordDecl *RD, Expr *E, unsigned Depth) {
    Expr *IndentLit = getIndentString(Depth);
    Expr *TypeLit = getTypeString(S.Context.getRecordType(RD));
    if (IndentLit ? callPrintFunction("%s%s", {IndentLit, TypeLit})
                  : callPrintFunction("%s", {TypeLit}))
      return true;

    return dumpRecordValue(RD, E, IndentLit, Depth);
  }

  // Dump a record value. E should be a pointer or lvalue referring to an RD.
  bool dumpRecordValue(const RecordDecl *RD, Expr *E, Expr *RecordIndent,
                       unsigned Depth) {
    // FIXME: Decide what to do if RD is a union. At least we should probably
    // turn off printing `const char*` members with `%s`, because that is very
    // likely to crash if that's not the active member. Whatever we decide, we
    // should document it.

    // Build an OpaqueValueExpr so we can refer to E more than once without
    // triggering re-evaluation.
    Expr *RecordArg = makeOpaqueValueExpr(E);
    bool RecordArgIsPtr = RecordArg->getType()->isPointerType();

    if (callPrintFunction(" {\n"))
      return true;

    // Dump each base class, regardless of whether they're aggregates.
    if (const auto *CXXRD = dyn_cast<CXXRecordDecl>(RD)) {
      for (const auto &Base : CXXRD->bases()) {
        QualType BaseType =
            RecordArgIsPtr ? S.Context.getPointerType(Base.getType())
                           : S.Context.getLValueReferenceType(Base.getType());
        ExprResult BasePtr = S.BuildCStyleCastExpr(
            Loc, S.Context.getTrivialTypeSourceInfo(BaseType, Loc), Loc,
            RecordArg);
        if (BasePtr.isInvalid() ||
            dumpUnnamedRecord(Base.getType()->getAsRecordDecl(), BasePtr.get(),
                              Depth + 1))
          return true;
      }
    }

    Expr *FieldIndentArg = getIndentString(Depth + 1);

    // Dump each field.
    for (auto *D : RD->decls()) {
      auto *IFD = dyn_cast<IndirectFieldDecl>(D);
      auto *FD = IFD ? IFD->getAnonField() : dyn_cast<FieldDecl>(D);
      if (!FD || FD->isUnnamedBitField() || FD->isAnonymousStructOrUnion())
        continue;

      llvm::SmallString<20> Format = llvm::StringRef("%s%s %s ");
      llvm::SmallVector<Expr *, 5> Args = {FieldIndentArg,
                                           getTypeString(FD->getType()),
                                           getStringLiteral(FD->getName())};

      if (FD->isBitField()) {
        Format += ": %zu ";
        QualType SizeT = S.Context.getSizeType();
        llvm::APInt BitWidth(S.Context.getIntWidth(SizeT),
                             FD->getBitWidthValue(S.Context));
        Args.push_back(IntegerLiteral::Create(S.Context, BitWidth, SizeT, Loc));
      }

      Format += "=";

      ExprResult Field =
          IFD ? S.BuildAnonymousStructUnionMemberReference(
                    CXXScopeSpec(), Loc, IFD,
                    DeclAccessPair::make(IFD, AS_public), RecordArg, Loc)
              : S.BuildFieldReferenceExpr(
                    RecordArg, RecordArgIsPtr, Loc, CXXScopeSpec(), FD,
                    DeclAccessPair::make(FD, AS_public),
                    DeclarationNameInfo(FD->getDeclName(), Loc));
      if (Field.isInvalid())
        return true;

      auto *InnerRD = FD->getType()->getAsRecordDecl();
      auto *InnerCXXRD = dyn_cast_or_null<CXXRecordDecl>(InnerRD);
      if (InnerRD && (!InnerCXXRD || InnerCXXRD->isAggregate())) {
        // Recursively print the values of members of aggregate record type.
        if (callPrintFunction(Format, Args) ||
            dumpRecordValue(InnerRD, Field.get(), FieldIndentArg, Depth + 1))
          return true;
      } else {
        Format += " ";
        if (appendFormatSpecifier(FD->getType(), Format)) {
          // We know how to print this field.
          Args.push_back(Field.get());
        } else {
          // We don't know how to print this field. Print out its address
          // with a format specifier that a smart tool will be able to
          // recognize and treat specially.
          Format += "*%p";
          ExprResult FieldAddr =
              S.BuildUnaryOp(nullptr, Loc, UO_AddrOf, Field.get());
          if (FieldAddr.isInvalid())
            return true;
          Args.push_back(FieldAddr.get());
        }
        Format += "\n";
        if (callPrintFunction(Format, Args))
          return true;
      }
    }

    return RecordIndent ? callPrintFunction("%s}\n", RecordIndent)
                        : callPrintFunction("}\n");
  }

  Expr *buildWrapper() {
    auto *Wrapper = PseudoObjectExpr::Create(S.Context, TheCall, Actions,
                                             PseudoObjectExpr::NoResult);
    TheCall->setType(Wrapper->getType());
    TheCall->setValueKind(Wrapper->getValueKind());
    return Wrapper;
  }
};
} // namespace

static ExprResult BuiltinDumpStruct(Sema &S, CallExpr *TheCall) {
  if (S.checkArgCountAtLeast(TheCall, 2))
    return ExprError();

  ExprResult PtrArgResult = S.DefaultLvalueConversion(TheCall->getArg(0));
  if (PtrArgResult.isInvalid())
    return ExprError();
  TheCall->setArg(0, PtrArgResult.get());

  // First argument should be a pointer to a struct.
  QualType PtrArgType = PtrArgResult.get()->getType();
  if (!PtrArgType->isPointerType() ||
      !PtrArgType->getPointeeType()->isRecordType()) {
    S.Diag(PtrArgResult.get()->getBeginLoc(),
           diag::err_expected_struct_pointer_argument)
        << 1 << TheCall->getDirectCallee() << PtrArgType;
    return ExprError();
  }
  QualType Pointee = PtrArgType->getPointeeType();
  const RecordDecl *RD = Pointee->getAsRecordDecl();
  // Try to instantiate the class template as appropriate; otherwise, access to
  // its data() may lead to a crash.
  if (S.RequireCompleteType(PtrArgResult.get()->getBeginLoc(), Pointee,
                            diag::err_incomplete_type))
    return ExprError();
  // Second argument is a callable, but we can't fully validate it until we try
  // calling it.
  QualType FnArgType = TheCall->getArg(1)->getType();
  if (!FnArgType->isFunctionType() && !FnArgType->isFunctionPointerType() &&
      !FnArgType->isBlockPointerType() &&
      !(S.getLangOpts().CPlusPlus && FnArgType->isRecordType())) {
    auto *BT = FnArgType->getAs<BuiltinType>();
    switch (BT ? BT->getKind() : BuiltinType::Void) {
    case BuiltinType::Dependent:
    case BuiltinType::Overload:
    case BuiltinType::BoundMember:
    case BuiltinType::PseudoObject:
    case BuiltinType::UnknownAny:
    case BuiltinType::BuiltinFn:
      // This might be a callable.
      break;

    default:
      S.Diag(TheCall->getArg(1)->getBeginLoc(),
             diag::err_expected_callable_argument)
          << 2 << TheCall->getDirectCallee() << FnArgType;
      return ExprError();
    }
  }

  BuiltinDumpStructGenerator Generator(S, TheCall);

  // Wrap parentheses around the given pointer. This is not necessary for
  // correct code generation, but it means that when we pretty-print the call
  // arguments in our diagnostics we will produce '(&s)->n' instead of the
  // incorrect '&s->n'.
  Expr *PtrArg = PtrArgResult.get();
  PtrArg = new (S.Context)
      ParenExpr(PtrArg->getBeginLoc(),
                S.getLocForEndOfToken(PtrArg->getEndLoc()), PtrArg);
  if (Generator.dumpUnnamedRecord(RD, PtrArg, 0))
    return ExprError();

  return Generator.buildWrapper();
}

static bool BuiltinCallWithStaticChain(Sema &S, CallExpr *BuiltinCall) {
  if (S.checkArgCount(BuiltinCall, 2))
    return true;

  SourceLocation BuiltinLoc = BuiltinCall->getBeginLoc();
  Expr *Builtin = BuiltinCall->getCallee()->IgnoreImpCasts();
  Expr *Call = BuiltinCall->getArg(0);
  Expr *Chain = BuiltinCall->getArg(1);

  if (Call->getStmtClass() != Stmt::CallExprClass) {
    S.Diag(BuiltinLoc, diag::err_first_argument_to_cwsc_not_call)
        << Call->getSourceRange();
    return true;
  }

  auto CE = cast<CallExpr>(Call);
  if (CE->getCallee()->getType()->isBlockPointerType()) {
    S.Diag(BuiltinLoc, diag::err_first_argument_to_cwsc_block_call)
        << Call->getSourceRange();
    return true;
  }

  const Decl *TargetDecl = CE->getCalleeDecl();
  if (const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(TargetDecl))
    if (FD->getBuiltinID()) {
      S.Diag(BuiltinLoc, diag::err_first_argument_to_cwsc_builtin_call)
          << Call->getSourceRange();
      return true;
    }

  if (isa<CXXPseudoDestructorExpr>(CE->getCallee()->IgnoreParens())) {
    S.Diag(BuiltinLoc, diag::err_first_argument_to_cwsc_pdtor_call)
        << Call->getSourceRange();
    return true;
  }

  ExprResult ChainResult = S.UsualUnaryConversions(Chain);
  if (ChainResult.isInvalid())
    return true;
  if (!ChainResult.get()->getType()->isPointerType()) {
    S.Diag(BuiltinLoc, diag::err_second_argument_to_cwsc_not_pointer)
        << Chain->getSourceRange();
    return true;
  }

  QualType ReturnTy = CE->getCallReturnType(S.Context);
  QualType ArgTys[2] = { ReturnTy, ChainResult.get()->getType() };
  QualType BuiltinTy = S.Context.getFunctionType(
      ReturnTy, ArgTys, FunctionProtoType::ExtProtoInfo());
  QualType BuiltinPtrTy = S.Context.getPointerType(BuiltinTy);

  Builtin =
      S.ImpCastExprToType(Builtin, BuiltinPtrTy, CK_BuiltinFnToFnPtr).get();

  BuiltinCall->setType(CE->getType());
  BuiltinCall->setValueKind(CE->getValueKind());
  BuiltinCall->setObjectKind(CE->getObjectKind());
  BuiltinCall->setCallee(Builtin);
  BuiltinCall->setArg(1, ChainResult.get());

  return false;
}

namespace {

class ScanfDiagnosticFormatHandler
    : public analyze_format_string::FormatStringHandler {
  // Accepts the argument index (relative to the first destination index) of the
  // argument whose size we want.
  using ComputeSizeFunction =
      llvm::function_ref<std::optional<llvm::APSInt>(unsigned)>;

  // Accepts the argument index (relative to the first destination index), the
  // destination size, and the source size).
  using DiagnoseFunction =
      llvm::function_ref<void(unsigned, unsigned, unsigned)>;

  ComputeSizeFunction ComputeSizeArgument;
  DiagnoseFunction Diagnose;

public:
  ScanfDiagnosticFormatHandler(ComputeSizeFunction ComputeSizeArgument,
                               DiagnoseFunction Diagnose)
      : ComputeSizeArgument(ComputeSizeArgument), Diagnose(Diagnose) {}

  bool HandleScanfSpecifier(const analyze_scanf::ScanfSpecifier &FS,
                            const char *StartSpecifier,
                            unsigned specifierLen) override {
    if (!FS.consumesDataArgument())
      return true;

    unsigned NulByte = 0;
    switch ((FS.getConversionSpecifier().getKind())) {
    default:
      return true;
    case analyze_format_string::ConversionSpecifier::sArg:
    case analyze_format_string::ConversionSpecifier::ScanListArg:
      NulByte = 1;
      break;
    case analyze_format_string::ConversionSpecifier::cArg:
      break;
    }

    analyze_format_string::OptionalAmount FW = FS.getFieldWidth();
    if (FW.getHowSpecified() !=
        analyze_format_string::OptionalAmount::HowSpecified::Constant)
      return true;

    unsigned SourceSize = FW.getConstantAmount() + NulByte;

    std::optional<llvm::APSInt> DestSizeAPS =
        ComputeSizeArgument(FS.getArgIndex());
    if (!DestSizeAPS)
      return true;

    unsigned DestSize = DestSizeAPS->getZExtValue();

    if (DestSize < SourceSize)
      Diagnose(FS.getArgIndex(), DestSize, SourceSize);

    return true;
  }
};

class EstimateSizeFormatHandler
    : public analyze_format_string::FormatStringHandler {
  size_t Size;
  /// Whether the format string contains Linux kernel's format specifier
  /// extension.
  bool IsKernelCompatible = true;

public:
  EstimateSizeFormatHandler(StringRef Format)
      : Size(std::min(Format.find(0), Format.size()) +
             1 /* null byte always written by sprintf */) {}

  bool HandlePrintfSpecifier(const analyze_printf::PrintfSpecifier &FS,
                             const char *, unsigned SpecifierLen,
                             const TargetInfo &) override {

    const size_t FieldWidth = computeFieldWidth(FS);
    const size_t Precision = computePrecision(FS);

    // The actual format.
    switch (FS.getConversionSpecifier().getKind()) {
    // Just a char.
    case analyze_format_string::ConversionSpecifier::cArg:
    case analyze_format_string::ConversionSpecifier::CArg:
      Size += std::max(FieldWidth, (size_t)1);
      break;
    // Just an integer.
    case analyze_format_string::ConversionSpecifier::dArg:
    case analyze_format_string::ConversionSpecifier::DArg:
    case analyze_format_string::ConversionSpecifier::iArg:
    case analyze_format_string::ConversionSpecifier::oArg:
    case analyze_format_string::ConversionSpecifier::OArg:
    case analyze_format_string::ConversionSpecifier::uArg:
    case analyze_format_string::ConversionSpecifier::UArg:
    case analyze_format_string::ConversionSpecifier::xArg:
    case analyze_format_string::ConversionSpecifier::XArg:
      Size += std::max(FieldWidth, Precision);
      break;

    // %g style conversion switches between %f or %e style dynamically.
    // %g removes trailing zeros, and does not print decimal point if there are
    // no digits that follow it. Thus %g can print a single digit.
    // FIXME: If it is alternative form:
    // For g and G conversions, trailing zeros are not removed from the result.
    case analyze_format_string::ConversionSpecifier::gArg:
    case analyze_format_string::ConversionSpecifier::GArg:
      Size += 1;
      break;

    // Floating point number in the form '[+]ddd.ddd'.
    case analyze_format_string::ConversionSpecifier::fArg:
    case analyze_format_string::ConversionSpecifier::FArg:
      Size += std::max(FieldWidth, 1 /* integer part */ +
                                       (Precision ? 1 + Precision
                                                  : 0) /* period + decimal */);
      break;

    // Floating point number in the form '[-]d.ddde[+-]dd'.
    case analyze_format_string::ConversionSpecifier::eArg:
    case analyze_format_string::ConversionSpecifier::EArg:
      Size +=
          std::max(FieldWidth,
                   1 /* integer part */ +
                       (Precision ? 1 + Precision : 0) /* period + decimal */ +
                       1 /* e or E letter */ + 2 /* exponent */);
      break;

    // Floating point number in the form '[-]0xh.hhhhp±dd'.
    case analyze_format_string::ConversionSpecifier::aArg:
    case analyze_format_string::ConversionSpecifier::AArg:
      Size +=
          std::max(FieldWidth,
                   2 /* 0x */ + 1 /* integer part */ +
                       (Precision ? 1 + Precision : 0) /* period + decimal */ +
                       1 /* p or P letter */ + 1 /* + or - */ + 1 /* value */);
      break;

    // Just a string.
    case analyze_format_string::ConversionSpecifier::sArg:
    case analyze_format_string::ConversionSpecifier::SArg:
      Size += FieldWidth;
      break;

    // Just a pointer in the form '0xddd'.
    case analyze_format_string::ConversionSpecifier::pArg:
      // Linux kernel has its own extesion for `%p` specifier.
      // Kernel Document:
      // https://docs.kernel.org/core-api/printk-formats.html#pointer-types
      IsKernelCompatible = false;
      Size += std::max(FieldWidth, 2 /* leading 0x */ + Precision);
      break;

    // A plain percent.
    case analyze_format_string::ConversionSpecifier::PercentArg:
      Size += 1;
      break;

    default:
      break;
    }

    Size += FS.hasPlusPrefix() || FS.hasSpacePrefix();

    if (FS.hasAlternativeForm()) {
      switch (FS.getConversionSpecifier().getKind()) {
      // For o conversion, it increases the precision, if and only if necessary,
      // to force the first digit of the result to be a zero
      // (if the value and precision are both 0, a single 0 is printed)
      case analyze_format_string::ConversionSpecifier::oArg:
      // For b conversion, a nonzero result has 0b prefixed to it.
      case analyze_format_string::ConversionSpecifier::bArg:
      // For x (or X) conversion, a nonzero result has 0x (or 0X) prefixed to
      // it.
      case analyze_format_string::ConversionSpecifier::xArg:
      case analyze_format_string::ConversionSpecifier::XArg:
        // Note: even when the prefix is added, if
        // (prefix_width <= FieldWidth - formatted_length) holds,
        // the prefix does not increase the format
        // size. e.g.(("%#3x", 0xf) is "0xf")

        // If the result is zero, o, b, x, X adds nothing.
        break;
      // For a, A, e, E, f, F, g, and G conversions,
      // the result of converting a floating-point number always contains a
      // decimal-point
      case analyze_format_string::ConversionSpecifier::aArg:
      case analyze_format_string::ConversionSpecifier::AArg:
      case analyze_format_string::ConversionSpecifier::eArg:
      case analyze_format_string::ConversionSpecifier::EArg:
      case analyze_format_string::ConversionSpecifier::fArg:
      case analyze_format_string::ConversionSpecifier::FArg:
      case analyze_format_string::ConversionSpecifier::gArg:
      case analyze_format_string::ConversionSpecifier::GArg:
        Size += (Precision ? 0 : 1);
        break;
      // For other conversions, the behavior is undefined.
      default:
        break;
      }
    }
    assert(SpecifierLen <= Size && "no underflow");
    Size -= SpecifierLen;
    return true;
  }

  size_t getSizeLowerBound() const { return Size; }
  bool isKernelCompatible() const { return IsKernelCompatible; }

private:
  static size_t computeFieldWidth(const analyze_printf::PrintfSpecifier &FS) {
    const analyze_format_string::OptionalAmount &FW = FS.getFieldWidth();
    size_t FieldWidth = 0;
    if (FW.getHowSpecified() == analyze_format_string::OptionalAmount::Constant)
      FieldWidth = FW.getConstantAmount();
    return FieldWidth;
  }

  static size_t computePrecision(const analyze_printf::PrintfSpecifier &FS) {
    const analyze_format_string::OptionalAmount &FW = FS.getPrecision();
    size_t Precision = 0;

    // See man 3 printf for default precision value based on the specifier.
    switch (FW.getHowSpecified()) {
    case analyze_format_string::OptionalAmount::NotSpecified:
      switch (FS.getConversionSpecifier().getKind()) {
      default:
        break;
      case analyze_format_string::ConversionSpecifier::dArg: // %d
      case analyze_format_string::ConversionSpecifier::DArg: // %D
      case analyze_format_string::ConversionSpecifier::iArg: // %i
        Precision = 1;
        break;
      case analyze_format_string::ConversionSpecifier::oArg: // %d
      case analyze_format_string::ConversionSpecifier::OArg: // %D
      case analyze_format_string::ConversionSpecifier::uArg: // %d
      case analyze_format_string::ConversionSpecifier::UArg: // %D
      case analyze_format_string::ConversionSpecifier::xArg: // %d
      case analyze_format_string::ConversionSpecifier::XArg: // %D
        Precision = 1;
        break;
      case analyze_format_string::ConversionSpecifier::fArg: // %f
      case analyze_format_string::ConversionSpecifier::FArg: // %F
      case analyze_format_string::ConversionSpecifier::eArg: // %e
      case analyze_format_string::ConversionSpecifier::EArg: // %E
      case analyze_format_string::ConversionSpecifier::gArg: // %g
      case analyze_format_string::ConversionSpecifier::GArg: // %G
        Precision = 6;
        break;
      case analyze_format_string::ConversionSpecifier::pArg: // %d
        Precision = 1;
        break;
      }
      break;
    case analyze_format_string::OptionalAmount::Constant:
      Precision = FW.getConstantAmount();
      break;
    default:
      break;
    }
    return Precision;
  }
};

} // namespace

static bool ProcessFormatStringLiteral(const Expr *FormatExpr,
                                       StringRef &FormatStrRef, size_t &StrLen,
                                       ASTContext &Context) {
  if (const auto *Format = dyn_cast<StringLiteral>(FormatExpr);
      Format && (Format->isOrdinary() || Format->isUTF8())) {
    FormatStrRef = Format->getString();
    const ConstantArrayType *T =
        Context.getAsConstantArrayType(Format->getType());
    assert(T && "String literal not of constant array type!");
    size_t TypeSize = T->getZExtSize();
    // In case there's a null byte somewhere.
    StrLen = std::min(std::max(TypeSize, size_t(1)) - 1, FormatStrRef.find(0));
    return true;
  }
  return false;
}

void Sema::checkFortifiedBuiltinMemoryFunction(FunctionDecl *FD,
                                               CallExpr *TheCall) {
  if (TheCall->isValueDependent() || TheCall->isTypeDependent() ||
      isConstantEvaluatedContext())
    return;

  bool UseDABAttr = false;
  const FunctionDecl *UseDecl = FD;

  const auto *DABAttr = FD->getAttr<DiagnoseAsBuiltinAttr>();
  if (DABAttr) {
    UseDecl = DABAttr->getFunction();
    assert(UseDecl && "Missing FunctionDecl in DiagnoseAsBuiltin attribute!");
    UseDABAttr = true;
  }

  unsigned BuiltinID = UseDecl->getBuiltinID(/*ConsiderWrappers=*/true);

  if (!BuiltinID)
    return;

  const TargetInfo &TI = getASTContext().getTargetInfo();
  unsigned SizeTypeWidth = TI.getTypeWidth(TI.getSizeType());

  auto TranslateIndex = [&](unsigned Index) -> std::optional<unsigned> {
    // If we refer to a diagnose_as_builtin attribute, we need to change the
    // argument index to refer to the arguments of the called function. Unless
    // the index is out of bounds, which presumably means it's a variadic
    // function.
    if (!UseDABAttr)
      return Index;
    unsigned DABIndices = DABAttr->argIndices_size();
    unsigned NewIndex = Index < DABIndices
                            ? DABAttr->argIndices_begin()[Index]
                            : Index - DABIndices + FD->getNumParams();
    if (NewIndex >= TheCall->getNumArgs())
      return std::nullopt;
    return NewIndex;
  };

  auto ComputeExplicitObjectSizeArgument =
      [&](unsigned Index) -> std::optional<llvm::APSInt> {
    std::optional<unsigned> IndexOptional = TranslateIndex(Index);
    if (!IndexOptional)
      return std::nullopt;
    unsigned NewIndex = *IndexOptional;
    Expr::EvalResult Result;
    Expr *SizeArg = TheCall->getArg(NewIndex);
    if (!SizeArg->EvaluateAsInt(Result, getASTContext()))
      return std::nullopt;
    llvm::APSInt Integer = Result.Val.getInt();
    Integer.setIsUnsigned(true);
    return Integer;
  };

  auto ComputeSizeArgument =
      [&](unsigned Index) -> std::optional<llvm::APSInt> {
    // If the parameter has a pass_object_size attribute, then we should use its
    // (potentially) more strict checking mode. Otherwise, conservatively assume
    // type 0.
    int BOSType = 0;
    // This check can fail for variadic functions.
    if (Index < FD->getNumParams()) {
      if (const auto *POS =
              FD->getParamDecl(Index)->getAttr<PassObjectSizeAttr>())
        BOSType = POS->getType();
    }

    std::optional<unsigned> IndexOptional = TranslateIndex(Index);
    if (!IndexOptional)
      return std::nullopt;
    unsigned NewIndex = *IndexOptional;

    if (NewIndex >= TheCall->getNumArgs())
      return std::nullopt;

    const Expr *ObjArg = TheCall->getArg(NewIndex);
    uint64_t Result;
    if (!ObjArg->tryEvaluateObjectSize(Result, getASTContext(), BOSType))
      return std::nullopt;

    // Get the object size in the target's size_t width.
    return llvm::APSInt::getUnsigned(Result).extOrTrunc(SizeTypeWidth);
  };

  auto ComputeStrLenArgument =
      [&](unsigned Index) -> std::optional<llvm::APSInt> {
    std::optional<unsigned> IndexOptional = TranslateIndex(Index);
    if (!IndexOptional)
      return std::nullopt;
    unsigned NewIndex = *IndexOptional;

    const Expr *ObjArg = TheCall->getArg(NewIndex);
    uint64_t Result;
    if (!ObjArg->tryEvaluateStrLen(Result, getASTContext()))
      return std::nullopt;
    // Add 1 for null byte.
    return llvm::APSInt::getUnsigned(Result + 1).extOrTrunc(SizeTypeWidth);
  };

  std::optional<llvm::APSInt> SourceSize;
  std::optional<llvm::APSInt> DestinationSize;
  unsigned DiagID = 0;
  bool IsChkVariant = false;

  auto GetFunctionName = [&]() {
    StringRef FunctionName = getASTContext().BuiltinInfo.getName(BuiltinID);
    // Skim off the details of whichever builtin was called to produce a better
    // diagnostic, as it's unlikely that the user wrote the __builtin
    // explicitly.
    if (IsChkVariant) {
      FunctionName = FunctionName.drop_front(std::strlen("__builtin___"));
      FunctionName = FunctionName.drop_back(std::strlen("_chk"));
    } else {
      FunctionName.consume_front("__builtin_");
    }
    return FunctionName;
  };

  switch (BuiltinID) {
  default:
    return;
  case Builtin::BI__builtin_strcpy:
  case Builtin::BIstrcpy: {
    DiagID = diag::warn_fortify_strlen_overflow;
    SourceSize = ComputeStrLenArgument(1);
    DestinationSize = ComputeSizeArgument(0);
    break;
  }

  case Builtin::BI__builtin___strcpy_chk: {
    DiagID = diag::warn_fortify_strlen_overflow;
    SourceSize = ComputeStrLenArgument(1);
    DestinationSize = ComputeExplicitObjectSizeArgument(2);
    IsChkVariant = true;
    break;
  }

  case Builtin::BIscanf:
  case Builtin::BIfscanf:
  case Builtin::BIsscanf: {
    unsigned FormatIndex = 1;
    unsigned DataIndex = 2;
    if (BuiltinID == Builtin::BIscanf) {
      FormatIndex = 0;
      DataIndex = 1;
    }

    const auto *FormatExpr =
        TheCall->getArg(FormatIndex)->IgnoreParenImpCasts();

    StringRef FormatStrRef;
    size_t StrLen;
    if (!ProcessFormatStringLiteral(FormatExpr, FormatStrRef, StrLen, Context))
      return;

    auto Diagnose = [&](unsigned ArgIndex, unsigned DestSize,
                        unsigned SourceSize) {
      DiagID = diag::warn_fortify_scanf_overflow;
      unsigned Index = ArgIndex + DataIndex;
      StringRef FunctionName = GetFunctionName();
      DiagRuntimeBehavior(TheCall->getArg(Index)->getBeginLoc(), TheCall,
                          PDiag(DiagID) << FunctionName << (Index + 1)
                                        << DestSize << SourceSize);
    };

    auto ShiftedComputeSizeArgument = [&](unsigned Index) {
      return ComputeSizeArgument(Index + DataIndex);
    };
    ScanfDiagnosticFormatHandler H(ShiftedComputeSizeArgument, Diagnose);
    const char *FormatBytes = FormatStrRef.data();
    analyze_format_string::ParseScanfString(H, FormatBytes,
                                            FormatBytes + StrLen, getLangOpts(),
                                            Context.getTargetInfo());

    // Unlike the other cases, in this one we have already issued the diagnostic
    // here, so no need to continue (because unlike the other cases, here the
    // diagnostic refers to the argument number).
    return;
  }

  case Builtin::BIsprintf:
  case Builtin::BI__builtin___sprintf_chk: {
    size_t FormatIndex = BuiltinID == Builtin::BIsprintf ? 1 : 3;
    auto *FormatExpr = TheCall->getArg(FormatIndex)->IgnoreParenImpCasts();

    StringRef FormatStrRef;
    size_t StrLen;
    if (ProcessFormatStringLiteral(FormatExpr, FormatStrRef, StrLen, Context)) {
      EstimateSizeFormatHandler H(FormatStrRef);
      const char *FormatBytes = FormatStrRef.data();
      if (!analyze_format_string::ParsePrintfString(
              H, FormatBytes, FormatBytes + StrLen, getLangOpts(),
              Context.getTargetInfo(), false)) {
        DiagID = H.isKernelCompatible()
                     ? diag::warn_format_overflow
                     : diag::warn_format_overflow_non_kprintf;
        SourceSize = llvm::APSInt::getUnsigned(H.getSizeLowerBound())
                         .extOrTrunc(SizeTypeWidth);
        if (BuiltinID == Builtin::BI__builtin___sprintf_chk) {
          DestinationSize = ComputeExplicitObjectSizeArgument(2);
          IsChkVariant = true;
        } else {
          DestinationSize = ComputeSizeArgument(0);
        }
        break;
      }
    }
    return;
  }
  case Builtin::BI__builtin___memcpy_chk:
  case Builtin::BI__builtin___memmove_chk:
  case Builtin::BI__builtin___memset_chk:
  case Builtin::BI__builtin___strlcat_chk:
  case Builtin::BI__builtin___strlcpy_chk:
  case Builtin::BI__builtin___strncat_chk:
  case Builtin::BI__builtin___strncpy_chk:
  case Builtin::BI__builtin___stpncpy_chk:
  case Builtin::BI__builtin___memccpy_chk:
  case Builtin::BI__builtin___mempcpy_chk: {
    DiagID = diag::warn_builtin_chk_overflow;
    SourceSize = ComputeExplicitObjectSizeArgument(TheCall->getNumArgs() - 2);
    DestinationSize =
        ComputeExplicitObjectSizeArgument(TheCall->getNumArgs() - 1);
    IsChkVariant = true;
    break;
  }

  case Builtin::BI__builtin___snprintf_chk:
  case Builtin::BI__builtin___vsnprintf_chk: {
    DiagID = diag::warn_builtin_chk_overflow;
    SourceSize = ComputeExplicitObjectSizeArgument(1);
    DestinationSize = ComputeExplicitObjectSizeArgument(3);
    IsChkVariant = true;
    break;
  }

  case Builtin::BIstrncat:
  case Builtin::BI__builtin_strncat:
  case Builtin::BIstrncpy:
  case Builtin::BI__builtin_strncpy:
  case Builtin::BIstpncpy:
  case Builtin::BI__builtin_stpncpy: {
    // Whether these functions overflow depends on the runtime strlen of the
    // string, not just the buffer size, so emitting the "always overflow"
    // diagnostic isn't quite right. We should still diagnose passing a buffer
    // size larger than the destination buffer though; this is a runtime abort
    // in _FORTIFY_SOURCE mode, and is quite suspicious otherwise.
    DiagID = diag::warn_fortify_source_size_mismatch;
    SourceSize = ComputeExplicitObjectSizeArgument(TheCall->getNumArgs() - 1);
    DestinationSize = ComputeSizeArgument(0);
    break;
  }

  case Builtin::BImemcpy:
  case Builtin::BI__builtin_memcpy:
  case Builtin::BImemmove:
  case Builtin::BI__builtin_memmove:
  case Builtin::BImemset:
  case Builtin::BI__builtin_memset:
  case Builtin::BImempcpy:
  case Builtin::BI__builtin_mempcpy: {
    DiagID = diag::warn_fortify_source_overflow;
    SourceSize = ComputeExplicitObjectSizeArgument(TheCall->getNumArgs() - 1);
    DestinationSize = ComputeSizeArgument(0);
    break;
  }
  case Builtin::BIsnprintf:
  case Builtin::BI__builtin_snprintf:
  case Builtin::BIvsnprintf:
  case Builtin::BI__builtin_vsnprintf: {
    DiagID = diag::warn_fortify_source_size_mismatch;
    SourceSize = ComputeExplicitObjectSizeArgument(1);
    const auto *FormatExpr = TheCall->getArg(2)->IgnoreParenImpCasts();
    StringRef FormatStrRef;
    size_t StrLen;
    if (SourceSize &&
        ProcessFormatStringLiteral(FormatExpr, FormatStrRef, StrLen, Context)) {
      EstimateSizeFormatHandler H(FormatStrRef);
      const char *FormatBytes = FormatStrRef.data();
      if (!analyze_format_string::ParsePrintfString(
              H, FormatBytes, FormatBytes + StrLen, getLangOpts(),
              Context.getTargetInfo(), /*isFreeBSDKPrintf=*/false)) {
        llvm::APSInt FormatSize =
            llvm::APSInt::getUnsigned(H.getSizeLowerBound())
                .extOrTrunc(SizeTypeWidth);
        if (FormatSize > *SourceSize && *SourceSize != 0) {
          unsigned TruncationDiagID =
              H.isKernelCompatible() ? diag::warn_format_truncation
                                     : diag::warn_format_truncation_non_kprintf;
          SmallString<16> SpecifiedSizeStr;
          SmallString<16> FormatSizeStr;
          SourceSize->toString(SpecifiedSizeStr, /*Radix=*/10);
          FormatSize.toString(FormatSizeStr, /*Radix=*/10);
          DiagRuntimeBehavior(TheCall->getBeginLoc(), TheCall,
                              PDiag(TruncationDiagID)
                                  << GetFunctionName() << SpecifiedSizeStr
                                  << FormatSizeStr);
        }
      }
    }
    DestinationSize = ComputeSizeArgument(0);
  }
  }

  if (!SourceSize || !DestinationSize ||
      llvm::APSInt::compareValues(*SourceSize, *DestinationSize) <= 0)
    return;

  StringRef FunctionName = GetFunctionName();

  SmallString<16> DestinationStr;
  SmallString<16> SourceStr;
  DestinationSize->toString(DestinationStr, /*Radix=*/10);
  SourceSize->toString(SourceStr, /*Radix=*/10);
  DiagRuntimeBehavior(TheCall->getBeginLoc(), TheCall,
                      PDiag(DiagID)
                          << FunctionName << DestinationStr << SourceStr);
}

static bool BuiltinSEHScopeCheck(Sema &SemaRef, CallExpr *TheCall,
                                 Scope::ScopeFlags NeededScopeFlags,
                                 unsigned DiagID) {
  // Scopes aren't available during instantiation. Fortunately, builtin
  // functions cannot be template args so they cannot be formed through template
  // instantiation. Therefore checking once during the parse is sufficient.
  if (SemaRef.inTemplateInstantiation())
    return false;

  Scope *S = SemaRef.getCurScope();
  while (S && !S->isSEHExceptScope())
    S = S->getParent();
  if (!S || !(S->getFlags() & NeededScopeFlags)) {
    auto *DRE = cast<DeclRefExpr>(TheCall->getCallee()->IgnoreParenCasts());
    SemaRef.Diag(TheCall->getExprLoc(), DiagID)
        << DRE->getDecl()->getIdentifier();
    return true;
  }

  return false;
}

namespace {
enum PointerAuthOpKind {
  PAO_Strip,
  PAO_Sign,
  PAO_Auth,
  PAO_SignGeneric,
  PAO_Discriminator,
  PAO_BlendPointer,
  PAO_BlendInteger
};
}

bool Sema::checkPointerAuthEnabled(SourceLocation Loc, SourceRange Range) {
  if (getLangOpts().PointerAuthIntrinsics)
    return false;

  Diag(Loc, diag::err_ptrauth_disabled) << Range;
  return true;
}

static bool checkPointerAuthEnabled(Sema &S, Expr *E) {
  return S.checkPointerAuthEnabled(E->getExprLoc(), E->getSourceRange());
}

static bool checkPointerAuthKey(Sema &S, Expr *&Arg) {
  // Convert it to type 'int'.
  if (convertArgumentToType(S, Arg, S.Context.IntTy))
    return true;

  // Value-dependent expressions are okay; wait for template instantiation.
  if (Arg->isValueDependent())
    return false;

  unsigned KeyValue;
  return S.checkConstantPointerAuthKey(Arg, KeyValue);
}

bool Sema::checkConstantPointerAuthKey(Expr *Arg, unsigned &Result) {
  // Attempt to constant-evaluate the expression.
  std::optional<llvm::APSInt> KeyValue = Arg->getIntegerConstantExpr(Context);
  if (!KeyValue) {
    Diag(Arg->getExprLoc(), diag::err_expr_not_ice)
        << 0 << Arg->getSourceRange();
    return true;
  }

  // Ask the target to validate the key parameter.
  if (!Context.getTargetInfo().validatePointerAuthKey(*KeyValue)) {
    llvm::SmallString<32> Value;
    {
      llvm::raw_svector_ostream Str(Value);
      Str << *KeyValue;
    }

    Diag(Arg->getExprLoc(), diag::err_ptrauth_invalid_key)
        << Value << Arg->getSourceRange();
    return true;
  }

  Result = KeyValue->getZExtValue();
  return false;
}

static std::pair<const ValueDecl *, CharUnits>
findConstantBaseAndOffset(Sema &S, Expr *E) {
  // Must evaluate as a pointer.
  Expr::EvalResult Result;
  if (!E->EvaluateAsRValue(Result, S.Context) || !Result.Val.isLValue())
    return {nullptr, CharUnits()};

  const auto *BaseDecl =
      Result.Val.getLValueBase().dyn_cast<const ValueDecl *>();
  if (!BaseDecl)
    return {nullptr, CharUnits()};

  return {BaseDecl, Result.Val.getLValueOffset()};
}

static bool checkPointerAuthValue(Sema &S, Expr *&Arg, PointerAuthOpKind OpKind,
                                  bool RequireConstant = false) {
  if (Arg->hasPlaceholderType()) {
    ExprResult R = S.CheckPlaceholderExpr(Arg);
    if (R.isInvalid())
      return true;
    Arg = R.get();
  }

  auto AllowsPointer = [](PointerAuthOpKind OpKind) {
    return OpKind != PAO_BlendInteger;
  };
  auto AllowsInteger = [](PointerAuthOpKind OpKind) {
    return OpKind == PAO_Discriminator || OpKind == PAO_BlendInteger ||
           OpKind == PAO_SignGeneric;
  };

  // Require the value to have the right range of type.
  QualType ExpectedTy;
  if (AllowsPointer(OpKind) && Arg->getType()->isPointerType()) {
    ExpectedTy = Arg->getType().getUnqualifiedType();
  } else if (AllowsPointer(OpKind) && Arg->getType()->isNullPtrType()) {
    ExpectedTy = S.Context.VoidPtrTy;
  } else if (AllowsInteger(OpKind) &&
             Arg->getType()->isIntegralOrUnscopedEnumerationType()) {
    ExpectedTy = S.Context.getUIntPtrType();

  } else {
    // Diagnose the failures.
    S.Diag(Arg->getExprLoc(), diag::err_ptrauth_value_bad_type)
        << unsigned(OpKind == PAO_Discriminator  ? 1
                    : OpKind == PAO_BlendPointer ? 2
                    : OpKind == PAO_BlendInteger ? 3
                                                 : 0)
        << unsigned(AllowsInteger(OpKind) ? (AllowsPointer(OpKind) ? 2 : 1) : 0)
        << Arg->getType() << Arg->getSourceRange();
    return true;
  }

  // Convert to that type.  This should just be an lvalue-to-rvalue
  // conversion.
  if (convertArgumentToType(S, Arg, ExpectedTy))
    return true;

  if (!RequireConstant) {
    // Warn about null pointers for non-generic sign and auth operations.
    if ((OpKind == PAO_Sign || OpKind == PAO_Auth) &&
        Arg->isNullPointerConstant(S.Context, Expr::NPC_ValueDependentIsNull)) {
      S.Diag(Arg->getExprLoc(), OpKind == PAO_Sign
                                    ? diag::warn_ptrauth_sign_null_pointer
                                    : diag::warn_ptrauth_auth_null_pointer)
          << Arg->getSourceRange();
    }

    return false;
  }

  // Perform special checking on the arguments to ptrauth_sign_constant.

  // The main argument.
  if (OpKind == PAO_Sign) {
    // Require the value we're signing to have a special form.
    auto [BaseDecl, Offset] = findConstantBaseAndOffset(S, Arg);
    bool Invalid;

    // Must be rooted in a declaration reference.
    if (!BaseDecl)
      Invalid = true;

    // If it's a function declaration, we can't have an offset.
    else if (isa<FunctionDecl>(BaseDecl))
      Invalid = !Offset.isZero();

    // Otherwise we're fine.
    else
      Invalid = false;

    if (Invalid)
      S.Diag(Arg->getExprLoc(), diag::err_ptrauth_bad_constant_pointer);
    return Invalid;
  }

  // The discriminator argument.
  assert(OpKind == PAO_Discriminator);

  // Must be a pointer or integer or blend thereof.
  Expr *Pointer = nullptr;
  Expr *Integer = nullptr;
  if (auto *Call = dyn_cast<CallExpr>(Arg->IgnoreParens())) {
    if (Call->getBuiltinCallee() ==
        Builtin::BI__builtin_ptrauth_blend_discriminator) {
      Pointer = Call->getArg(0);
      Integer = Call->getArg(1);
    }
  }
  if (!Pointer && !Integer) {
    if (Arg->getType()->isPointerType())
      Pointer = Arg;
    else
      Integer = Arg;
  }

  // Check the pointer.
  bool Invalid = false;
  if (Pointer) {
    assert(Pointer->getType()->isPointerType());

    // TODO: if we're initializing a global, check that the address is
    // somehow related to what we're initializing.  This probably will
    // never really be feasible and we'll have to catch it at link-time.
    auto [BaseDecl, Offset] = findConstantBaseAndOffset(S, Pointer);
    if (!BaseDecl || !isa<VarDecl>(BaseDecl))
      Invalid = true;
  }

  // Check the integer.
  if (Integer) {
    assert(Integer->getType()->isIntegerType());
    if (!Integer->isEvaluatable(S.Context))
      Invalid = true;
  }

  if (Invalid)
    S.Diag(Arg->getExprLoc(), diag::err_ptrauth_bad_constant_discriminator);
  return Invalid;
}

static ExprResult PointerAuthStrip(Sema &S, CallExpr *Call) {
  if (S.checkArgCount(Call, 2))
    return ExprError();
  if (checkPointerAuthEnabled(S, Call))
    return ExprError();
  if (checkPointerAuthValue(S, Call->getArgs()[0], PAO_Strip) ||
      checkPointerAuthKey(S, Call->getArgs()[1]))
    return ExprError();

  Call->setType(Call->getArgs()[0]->getType());
  return Call;
}

static ExprResult PointerAuthBlendDiscriminator(Sema &S, CallExpr *Call) {
  if (S.checkArgCount(Call, 2))
    return ExprError();
  if (checkPointerAuthEnabled(S, Call))
    return ExprError();
  if (checkPointerAuthValue(S, Call->getArgs()[0], PAO_BlendPointer) ||
      checkPointerAuthValue(S, Call->getArgs()[1], PAO_BlendInteger))
    return ExprError();

  Call->setType(S.Context.getUIntPtrType());
  return Call;
}

static ExprResult PointerAuthSignGenericData(Sema &S, CallExpr *Call) {
  if (S.checkArgCount(Call, 2))
    return ExprError();
  if (checkPointerAuthEnabled(S, Call))
    return ExprError();
  if (checkPointerAuthValue(S, Call->getArgs()[0], PAO_SignGeneric) ||
      checkPointerAuthValue(S, Call->getArgs()[1], PAO_Discriminator))
    return ExprError();

  Call->setType(S.Context.getUIntPtrType());
  return Call;
}

static ExprResult PointerAuthSignOrAuth(Sema &S, CallExpr *Call,
                                        PointerAuthOpKind OpKind,
                                        bool RequireConstant) {
  if (S.checkArgCount(Call, 3))
    return ExprError();
  if (checkPointerAuthEnabled(S, Call))
    return ExprError();
  if (checkPointerAuthValue(S, Call->getArgs()[0], OpKind, RequireConstant) ||
      checkPointerAuthKey(S, Call->getArgs()[1]) ||
      checkPointerAuthValue(S, Call->getArgs()[2], PAO_Discriminator,
                            RequireConstant))
    return ExprError();

  Call->setType(Call->getArgs()[0]->getType());
  return Call;
}

static ExprResult PointerAuthAuthAndResign(Sema &S, CallExpr *Call) {
  if (S.checkArgCount(Call, 5))
    return ExprError();
  if (checkPointerAuthEnabled(S, Call))
    return ExprError();
  if (checkPointerAuthValue(S, Call->getArgs()[0], PAO_Auth) ||
      checkPointerAuthKey(S, Call->getArgs()[1]) ||
      checkPointerAuthValue(S, Call->getArgs()[2], PAO_Discriminator) ||
      checkPointerAuthKey(S, Call->getArgs()[3]) ||
      checkPointerAuthValue(S, Call->getArgs()[4], PAO_Discriminator))
    return ExprError();

  Call->setType(Call->getArgs()[0]->getType());
  return Call;
}

static ExprResult PointerAuthStringDiscriminator(Sema &S, CallExpr *Call) {
  if (checkPointerAuthEnabled(S, Call))
    return ExprError();

  // We've already performed normal call type-checking.
  const Expr *Arg = Call->getArg(0)->IgnoreParenImpCasts();

  // Operand must be an ordinary or UTF-8 string literal.
  const auto *Literal = dyn_cast<StringLiteral>(Arg);
  if (!Literal || Literal->getCharByteWidth() != 1) {
    S.Diag(Arg->getExprLoc(), diag::err_ptrauth_string_not_literal)
        << (Literal ? 1 : 0) << Arg->getSourceRange();
    return ExprError();
  }

  return Call;
}

static ExprResult BuiltinLaunder(Sema &S, CallExpr *TheCall) {
  if (S.checkArgCount(TheCall, 1))
    return ExprError();

  // Compute __builtin_launder's parameter type from the argument.
  // The parameter type is:
  //  * The type of the argument if it's not an array or function type,
  //  Otherwise,
  //  * The decayed argument type.
  QualType ParamTy = [&]() {
    QualType ArgTy = TheCall->getArg(0)->getType();
    if (const ArrayType *Ty = ArgTy->getAsArrayTypeUnsafe())
      return S.Context.getPointerType(Ty->getElementType());
    if (ArgTy->isFunctionType()) {
      return S.Context.getPointerType(ArgTy);
    }
    return ArgTy;
  }();

  TheCall->setType(ParamTy);

  auto DiagSelect = [&]() -> std::optional<unsigned> {
    if (!ParamTy->isPointerType())
      return 0;
    if (ParamTy->isFunctionPointerType())
      return 1;
    if (ParamTy->isVoidPointerType())
      return 2;
    return std::optional<unsigned>{};
  }();
  if (DiagSelect) {
    S.Diag(TheCall->getBeginLoc(), diag::err_builtin_launder_invalid_arg)
        << *DiagSelect << TheCall->getSourceRange();
    return ExprError();
  }

  // We either have an incomplete class type, or we have a class template
  // whose instantiation has not been forced. Example:
  //
  //   template <class T> struct Foo { T value; };
  //   Foo<int> *p = nullptr;
  //   auto *d = __builtin_launder(p);
  if (S.RequireCompleteType(TheCall->getBeginLoc(), ParamTy->getPointeeType(),
                            diag::err_incomplete_type))
    return ExprError();

  assert(ParamTy->getPointeeType()->isObjectType() &&
         "Unhandled non-object pointer case");

  InitializedEntity Entity =
      InitializedEntity::InitializeParameter(S.Context, ParamTy, false);
  ExprResult Arg =
      S.PerformCopyInitialization(Entity, SourceLocation(), TheCall->getArg(0));
  if (Arg.isInvalid())
    return ExprError();
  TheCall->setArg(0, Arg.get());

  return TheCall;
}

// Emit an error and return true if the current object format type is in the
// list of unsupported types.
static bool CheckBuiltinTargetNotInUnsupported(
    Sema &S, unsigned BuiltinID, CallExpr *TheCall,
    ArrayRef<llvm::Triple::ObjectFormatType> UnsupportedObjectFormatTypes) {
  llvm::Triple::ObjectFormatType CurObjFormat =
      S.getASTContext().getTargetInfo().getTriple().getObjectFormat();
  if (llvm::is_contained(UnsupportedObjectFormatTypes, CurObjFormat)) {
    S.Diag(TheCall->getBeginLoc(), diag::err_builtin_target_unsupported)
        << TheCall->getSourceRange();
    return true;
  }
  return false;
}

// Emit an error and return true if the current architecture is not in the list
// of supported architectures.
static bool
CheckBuiltinTargetInSupported(Sema &S, unsigned BuiltinID, CallExpr *TheCall,
                              ArrayRef<llvm::Triple::ArchType> SupportedArchs) {
  llvm::Triple::ArchType CurArch =
      S.getASTContext().getTargetInfo().getTriple().getArch();
  if (llvm::is_contained(SupportedArchs, CurArch))
    return false;
  S.Diag(TheCall->getBeginLoc(), diag::err_builtin_target_unsupported)
      << TheCall->getSourceRange();
  return true;
}

static void CheckNonNullArgument(Sema &S, const Expr *ArgExpr,
                                 SourceLocation CallSiteLoc);

bool Sema::CheckTSBuiltinFunctionCall(const TargetInfo &TI, unsigned BuiltinID,
                                      CallExpr *TheCall) {
  switch (TI.getTriple().getArch()) {
  default:
    // Some builtins don't require additional checking, so just consider these
    // acceptable.
    return false;
  case llvm::Triple::arm:
  case llvm::Triple::armeb:
  case llvm::Triple::thumb:
  case llvm::Triple::thumbeb:
    return ARM().CheckARMBuiltinFunctionCall(TI, BuiltinID, TheCall);
  case llvm::Triple::aarch64:
  case llvm::Triple::aarch64_32:
  case llvm::Triple::aarch64_be:
    return ARM().CheckAArch64BuiltinFunctionCall(TI, BuiltinID, TheCall);
  case llvm::Triple::bpfeb:
  case llvm::Triple::bpfel:
    return BPF().CheckBPFBuiltinFunctionCall(BuiltinID, TheCall);
  case llvm::Triple::hexagon:
    return Hexagon().CheckHexagonBuiltinFunctionCall(BuiltinID, TheCall);
  case llvm::Triple::mips:
  case llvm::Triple::mipsel:
  case llvm::Triple::mips64:
  case llvm::Triple::mips64el:
    return MIPS().CheckMipsBuiltinFunctionCall(TI, BuiltinID, TheCall);
  case llvm::Triple::systemz:
    return SystemZ().CheckSystemZBuiltinFunctionCall(BuiltinID, TheCall);
  case llvm::Triple::x86:
  case llvm::Triple::x86_64:
    return X86().CheckBuiltinFunctionCall(TI, BuiltinID, TheCall);
  case llvm::Triple::ppc:
  case llvm::Triple::ppcle:
  case llvm::Triple::ppc64:
  case llvm::Triple::ppc64le:
    return PPC().CheckPPCBuiltinFunctionCall(TI, BuiltinID, TheCall);
  case llvm::Triple::amdgcn:
    return AMDGPU().CheckAMDGCNBuiltinFunctionCall(BuiltinID, TheCall);
  case llvm::Triple::riscv32:
  case llvm::Triple::riscv64:
    return RISCV().CheckBuiltinFunctionCall(TI, BuiltinID, TheCall);
  case llvm::Triple::loongarch32:
  case llvm::Triple::loongarch64:
    return LoongArch().CheckLoongArchBuiltinFunctionCall(TI, BuiltinID,
                                                         TheCall);
  case llvm::Triple::wasm32:
  case llvm::Triple::wasm64:
    return Wasm().CheckWebAssemblyBuiltinFunctionCall(TI, BuiltinID, TheCall);
  case llvm::Triple::nvptx:
  case llvm::Triple::nvptx64:
    return NVPTX().CheckNVPTXBuiltinFunctionCall(TI, BuiltinID, TheCall);
  }
}

// Check if \p Ty is a valid type for the elementwise math builtins. If it is
// not a valid type, emit an error message and return true. Otherwise return
// false.
static bool checkMathBuiltinElementType(Sema &S, SourceLocation Loc,
                                        QualType ArgTy, int ArgIndex) {
  if (!ArgTy->getAs<VectorType>() &&
      !ConstantMatrixType::isValidElementType(ArgTy)) {
    return S.Diag(Loc, diag::err_builtin_invalid_arg_type)
           << ArgIndex << /* vector, integer or float ty*/ 0 << ArgTy;
  }

  return false;
}

static bool checkFPMathBuiltinElementType(Sema &S, SourceLocation Loc,
                                          QualType ArgTy, int ArgIndex) {
  QualType EltTy = ArgTy;
  if (auto *VecTy = EltTy->getAs<VectorType>())
    EltTy = VecTy->getElementType();

  if (!EltTy->isRealFloatingType()) {
    return S.Diag(Loc, diag::err_builtin_invalid_arg_type)
           << ArgIndex << /* vector or float ty*/ 5 << ArgTy;
  }

  return false;
}

/// BuiltinCpu{Supports|Is} - Handle __builtin_cpu_{supports|is}(char *).
/// This checks that the target supports the builtin and that the string
/// argument is constant and valid.
static bool BuiltinCpu(Sema &S, const TargetInfo &TI, CallExpr *TheCall,
                       const TargetInfo *AuxTI, unsigned BuiltinID) {
  assert((BuiltinID == Builtin::BI__builtin_cpu_supports ||
          BuiltinID == Builtin::BI__builtin_cpu_is) &&
         "Expecting __builtin_cpu_...");

  bool IsCPUSupports = BuiltinID == Builtin::BI__builtin_cpu_supports;
  const TargetInfo *TheTI = &TI;
  auto SupportsBI = [=](const TargetInfo *TInfo) {
    return TInfo && ((IsCPUSupports && TInfo->supportsCpuSupports()) ||
                     (!IsCPUSupports && TInfo->supportsCpuIs()));
  };
  if (!SupportsBI(&TI) && SupportsBI(AuxTI))
    TheTI = AuxTI;

  if ((!IsCPUSupports && !TheTI->supportsCpuIs()) ||
      (IsCPUSupports && !TheTI->supportsCpuSupports()))
    return S.Diag(TheCall->getBeginLoc(),
                  TI.getTriple().isOSAIX()
                      ? diag::err_builtin_aix_os_unsupported
                      : diag::err_builtin_target_unsupported)
           << SourceRange(TheCall->getBeginLoc(), TheCall->getEndLoc());

  Expr *Arg = TheCall->getArg(0)->IgnoreParenImpCasts();
  // Check if the argument is a string literal.
  if (!isa<StringLiteral>(Arg))
    return S.Diag(TheCall->getBeginLoc(), diag::err_expr_not_string_literal)
           << Arg->getSourceRange();

  // Check the contents of the string.
  StringRef Feature = cast<StringLiteral>(Arg)->getString();
  if (IsCPUSupports && !TheTI->validateCpuSupports(Feature)) {
    S.Diag(TheCall->getBeginLoc(), diag::warn_invalid_cpu_supports)
        << Arg->getSourceRange();
    return false;
  }
  if (!IsCPUSupports && !TheTI->validateCpuIs(Feature))
    return S.Diag(TheCall->getBeginLoc(), diag::err_invalid_cpu_is)
           << Arg->getSourceRange();
  return false;
}

/// Checks that __builtin_popcountg was called with a single argument, which is
/// an unsigned integer.
static bool BuiltinPopcountg(Sema &S, CallExpr *TheCall) {
  if (S.checkArgCount(TheCall, 1))
    return true;

  ExprResult ArgRes = S.DefaultLvalueConversion(TheCall->getArg(0));
  if (ArgRes.isInvalid())
    return true;

  Expr *Arg = ArgRes.get();
  TheCall->setArg(0, Arg);

  QualType ArgTy = Arg->getType();

  if (!ArgTy->isUnsignedIntegerType()) {
    S.Diag(Arg->getBeginLoc(), diag::err_builtin_invalid_arg_type)
        << 1 << /*unsigned integer ty*/ 7 << ArgTy;
    return true;
  }
  return false;
}

/// Checks that __builtin_{clzg,ctzg} was called with a first argument, which is
/// an unsigned integer, and an optional second argument, which is promoted to
/// an 'int'.
static bool BuiltinCountZeroBitsGeneric(Sema &S, CallExpr *TheCall) {
  if (S.checkArgCountRange(TheCall, 1, 2))
    return true;

  ExprResult Arg0Res = S.DefaultLvalueConversion(TheCall->getArg(0));
  if (Arg0Res.isInvalid())
    return true;

  Expr *Arg0 = Arg0Res.get();
  TheCall->setArg(0, Arg0);

  QualType Arg0Ty = Arg0->getType();

  if (!Arg0Ty->isUnsignedIntegerType()) {
    S.Diag(Arg0->getBeginLoc(), diag::err_builtin_invalid_arg_type)
        << 1 << /*unsigned integer ty*/ 7 << Arg0Ty;
    return true;
  }

  if (TheCall->getNumArgs() > 1) {
    ExprResult Arg1Res = S.UsualUnaryConversions(TheCall->getArg(1));
    if (Arg1Res.isInvalid())
      return true;

    Expr *Arg1 = Arg1Res.get();
    TheCall->setArg(1, Arg1);

    QualType Arg1Ty = Arg1->getType();

    if (!Arg1Ty->isSpecificBuiltinType(BuiltinType::Int)) {
      S.Diag(Arg1->getBeginLoc(), diag::err_builtin_invalid_arg_type)
          << 2 << /*'int' ty*/ 8 << Arg1Ty;
      return true;
    }
  }

  return false;
}

ExprResult
Sema::CheckBuiltinFunctionCall(FunctionDecl *FDecl, unsigned BuiltinID,
                               CallExpr *TheCall) {
  ExprResult TheCallResult(TheCall);

  // Find out if any arguments are required to be integer constant expressions.
  unsigned ICEArguments = 0;
  ASTContext::GetBuiltinTypeError Error;
  Context.GetBuiltinType(BuiltinID, Error, &ICEArguments);
  if (Error != ASTContext::GE_None)
    ICEArguments = 0;  // Don't diagnose previously diagnosed errors.

  // If any arguments are required to be ICE's, check and diagnose.
  for (unsigned ArgNo = 0; ICEArguments != 0; ++ArgNo) {
    // Skip arguments not required to be ICE's.
    if ((ICEArguments & (1 << ArgNo)) == 0) continue;

    llvm::APSInt Result;
    // If we don't have enough arguments, continue so we can issue better
    // diagnostic in checkArgCount(...)
    if (ArgNo < TheCall->getNumArgs() &&
        BuiltinConstantArg(TheCall, ArgNo, Result))
      return true;
    ICEArguments &= ~(1 << ArgNo);
  }

  FPOptions FPO;
  switch (BuiltinID) {
  case Builtin::BI__builtin_cpu_supports:
  case Builtin::BI__builtin_cpu_is:
    if (BuiltinCpu(*this, Context.getTargetInfo(), TheCall,
                   Context.getAuxTargetInfo(), BuiltinID))
      return ExprError();
    break;
  case Builtin::BI__builtin_cpu_init:
    if (!Context.getTargetInfo().supportsCpuInit()) {
      Diag(TheCall->getBeginLoc(), diag::err_builtin_target_unsupported)
          << SourceRange(TheCall->getBeginLoc(), TheCall->getEndLoc());
      return ExprError();
    }
    break;
  case Builtin::BI__builtin___CFStringMakeConstantString:
    // CFStringMakeConstantString is currently not implemented for GOFF (i.e.,
    // on z/OS) and for XCOFF (i.e., on AIX). Emit unsupported
    if (CheckBuiltinTargetNotInUnsupported(
            *this, BuiltinID, TheCall,
            {llvm::Triple::GOFF, llvm::Triple::XCOFF}))
      return ExprError();
    assert(TheCall->getNumArgs() == 1 &&
           "Wrong # arguments to builtin CFStringMakeConstantString");
    if (ObjC().CheckObjCString(TheCall->getArg(0)))
      return ExprError();
    break;
  case Builtin::BI__builtin_ms_va_start:
  case Builtin::BI__builtin_stdarg_start:
  case Builtin::BI__builtin_va_start:
    if (BuiltinVAStart(BuiltinID, TheCall))
      return ExprError();
    break;
  case Builtin::BI__va_start: {
    switch (Context.getTargetInfo().getTriple().getArch()) {
    case llvm::Triple::aarch64:
    case llvm::Triple::arm:
    case llvm::Triple::thumb:
      if (BuiltinVAStartARMMicrosoft(TheCall))
        return ExprError();
      break;
    default:
      if (BuiltinVAStart(BuiltinID, TheCall))
        return ExprError();
      break;
    }
    break;
  }

  // The acquire, release, and no fence variants are ARM and AArch64 only.
  case Builtin::BI_interlockedbittestandset_acq:
  case Builtin::BI_interlockedbittestandset_rel:
  case Builtin::BI_interlockedbittestandset_nf:
  case Builtin::BI_interlockedbittestandreset_acq:
  case Builtin::BI_interlockedbittestandreset_rel:
  case Builtin::BI_interlockedbittestandreset_nf:
    if (CheckBuiltinTargetInSupported(
            *this, BuiltinID, TheCall,
            {llvm::Triple::arm, llvm::Triple::thumb, llvm::Triple::aarch64}))
      return ExprError();
    break;

  // The 64-bit bittest variants are x64, ARM, and AArch64 only.
  case Builtin::BI_bittest64:
  case Builtin::BI_bittestandcomplement64:
  case Builtin::BI_bittestandreset64:
  case Builtin::BI_bittestandset64:
  case Builtin::BI_interlockedbittestandreset64:
  case Builtin::BI_interlockedbittestandset64:
    if (CheckBuiltinTargetInSupported(
            *this, BuiltinID, TheCall,
            {llvm::Triple::x86_64, llvm::Triple::arm, llvm::Triple::thumb,
             llvm::Triple::aarch64, llvm::Triple::amdgcn}))
      return ExprError();
    break;

  case Builtin::BI__builtin_set_flt_rounds:
    if (CheckBuiltinTargetInSupported(
            *this, BuiltinID, TheCall,
            {llvm::Triple::x86, llvm::Triple::x86_64, llvm::Triple::arm,
             llvm::Triple::thumb, llvm::Triple::aarch64, llvm::Triple::amdgcn}))
      return ExprError();
    break;

  case Builtin::BI__builtin_isgreater:
  case Builtin::BI__builtin_isgreaterequal:
  case Builtin::BI__builtin_isless:
  case Builtin::BI__builtin_islessequal:
  case Builtin::BI__builtin_islessgreater:
  case Builtin::BI__builtin_isunordered:
    if (BuiltinUnorderedCompare(TheCall, BuiltinID))
      return ExprError();
    break;
  case Builtin::BI__builtin_fpclassify:
    if (BuiltinFPClassification(TheCall, 6, BuiltinID))
      return ExprError();
    break;
  case Builtin::BI__builtin_isfpclass:
    if (BuiltinFPClassification(TheCall, 2, BuiltinID))
      return ExprError();
    break;
  case Builtin::BI__builtin_isfinite:
  case Builtin::BI__builtin_isinf:
  case Builtin::BI__builtin_isinf_sign:
  case Builtin::BI__builtin_isnan:
  case Builtin::BI__builtin_issignaling:
  case Builtin::BI__builtin_isnormal:
  case Builtin::BI__builtin_issubnormal:
  case Builtin::BI__builtin_iszero:
  case Builtin::BI__builtin_signbit:
  case Builtin::BI__builtin_signbitf:
  case Builtin::BI__builtin_signbitl:
    if (BuiltinFPClassification(TheCall, 1, BuiltinID))
      return ExprError();
    break;
  case Builtin::BI__builtin_shufflevector:
    return BuiltinShuffleVector(TheCall);
    // TheCall will be freed by the smart pointer here, but that's fine, since
    // BuiltinShuffleVector guts it, but then doesn't release it.
  case Builtin::BI__builtin_prefetch:
    if (BuiltinPrefetch(TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_alloca_with_align:
  case Builtin::BI__builtin_alloca_with_align_uninitialized:
    if (BuiltinAllocaWithAlign(TheCall))
      return ExprError();
    [[fallthrough]];
  case Builtin::BI__builtin_alloca:
  case Builtin::BI__builtin_alloca_uninitialized:
    Diag(TheCall->getBeginLoc(), diag::warn_alloca)
        << TheCall->getDirectCallee();
    break;
  case Builtin::BI__arithmetic_fence:
    if (BuiltinArithmeticFence(TheCall))
      return ExprError();
    break;
  case Builtin::BI__assume:
  case Builtin::BI__builtin_assume:
    if (BuiltinAssume(TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_assume_aligned:
    if (BuiltinAssumeAligned(TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_dynamic_object_size:
  case Builtin::BI__builtin_object_size:
    if (BuiltinConstantArgRange(TheCall, 1, 0, 3))
      return ExprError();
    break;
  case Builtin::BI__builtin_longjmp:
    if (BuiltinLongjmp(TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_setjmp:
    if (BuiltinSetjmp(TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_classify_type:
    if (checkArgCount(TheCall, 1))
      return true;
    TheCall->setType(Context.IntTy);
    break;
  case Builtin::BI__builtin_complex:
    if (BuiltinComplex(TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_constant_p: {
    if (checkArgCount(TheCall, 1))
      return true;
    ExprResult Arg = DefaultFunctionArrayLvalueConversion(TheCall->getArg(0));
    if (Arg.isInvalid()) return true;
    TheCall->setArg(0, Arg.get());
    TheCall->setType(Context.IntTy);
    break;
  }
  case Builtin::BI__builtin_launder:
    return BuiltinLaunder(*this, TheCall);
  case Builtin::BI__sync_fetch_and_add:
  case Builtin::BI__sync_fetch_and_add_1:
  case Builtin::BI__sync_fetch_and_add_2:
  case Builtin::BI__sync_fetch_and_add_4:
  case Builtin::BI__sync_fetch_and_add_8:
  case Builtin::BI__sync_fetch_and_add_16:
  case Builtin::BI__sync_fetch_and_sub:
  case Builtin::BI__sync_fetch_and_sub_1:
  case Builtin::BI__sync_fetch_and_sub_2:
  case Builtin::BI__sync_fetch_and_sub_4:
  case Builtin::BI__sync_fetch_and_sub_8:
  case Builtin::BI__sync_fetch_and_sub_16:
  case Builtin::BI__sync_fetch_and_or:
  case Builtin::BI__sync_fetch_and_or_1:
  case Builtin::BI__sync_fetch_and_or_2:
  case Builtin::BI__sync_fetch_and_or_4:
  case Builtin::BI__sync_fetch_and_or_8:
  case Builtin::BI__sync_fetch_and_or_16:
  case Builtin::BI__sync_fetch_and_and:
  case Builtin::BI__sync_fetch_and_and_1:
  case Builtin::BI__sync_fetch_and_and_2:
  case Builtin::BI__sync_fetch_and_and_4:
  case Builtin::BI__sync_fetch_and_and_8:
  case Builtin::BI__sync_fetch_and_and_16:
  case Builtin::BI__sync_fetch_and_xor:
  case Builtin::BI__sync_fetch_and_xor_1:
  case Builtin::BI__sync_fetch_and_xor_2:
  case Builtin::BI__sync_fetch_and_xor_4:
  case Builtin::BI__sync_fetch_and_xor_8:
  case Builtin::BI__sync_fetch_and_xor_16:
  case Builtin::BI__sync_fetch_and_nand:
  case Builtin::BI__sync_fetch_and_nand_1:
  case Builtin::BI__sync_fetch_and_nand_2:
  case Builtin::BI__sync_fetch_and_nand_4:
  case Builtin::BI__sync_fetch_and_nand_8:
  case Builtin::BI__sync_fetch_and_nand_16:
  case Builtin::BI__sync_add_and_fetch:
  case Builtin::BI__sync_add_and_fetch_1:
  case Builtin::BI__sync_add_and_fetch_2:
  case Builtin::BI__sync_add_and_fetch_4:
  case Builtin::BI__sync_add_and_fetch_8:
  case Builtin::BI__sync_add_and_fetch_16:
  case Builtin::BI__sync_sub_and_fetch:
  case Builtin::BI__sync_sub_and_fetch_1:
  case Builtin::BI__sync_sub_and_fetch_2:
  case Builtin::BI__sync_sub_and_fetch_4:
  case Builtin::BI__sync_sub_and_fetch_8:
  case Builtin::BI__sync_sub_and_fetch_16:
  case Builtin::BI__sync_and_and_fetch:
  case Builtin::BI__sync_and_and_fetch_1:
  case Builtin::BI__sync_and_and_fetch_2:
  case Builtin::BI__sync_and_and_fetch_4:
  case Builtin::BI__sync_and_and_fetch_8:
  case Builtin::BI__sync_and_and_fetch_16:
  case Builtin::BI__sync_or_and_fetch:
  case Builtin::BI__sync_or_and_fetch_1:
  case Builtin::BI__sync_or_and_fetch_2:
  case Builtin::BI__sync_or_and_fetch_4:
  case Builtin::BI__sync_or_and_fetch_8:
  case Builtin::BI__sync_or_and_fetch_16:
  case Builtin::BI__sync_xor_and_fetch:
  case Builtin::BI__sync_xor_and_fetch_1:
  case Builtin::BI__sync_xor_and_fetch_2:
  case Builtin::BI__sync_xor_and_fetch_4:
  case Builtin::BI__sync_xor_and_fetch_8:
  case Builtin::BI__sync_xor_and_fetch_16:
  case Builtin::BI__sync_nand_and_fetch:
  case Builtin::BI__sync_nand_and_fetch_1:
  case Builtin::BI__sync_nand_and_fetch_2:
  case Builtin::BI__sync_nand_and_fetch_4:
  case Builtin::BI__sync_nand_and_fetch_8:
  case Builtin::BI__sync_nand_and_fetch_16:
  case Builtin::BI__sync_val_compare_and_swap:
  case Builtin::BI__sync_val_compare_and_swap_1:
  case Builtin::BI__sync_val_compare_and_swap_2:
  case Builtin::BI__sync_val_compare_and_swap_4:
  case Builtin::BI__sync_val_compare_and_swap_8:
  case Builtin::BI__sync_val_compare_and_swap_16:
  case Builtin::BI__sync_bool_compare_and_swap:
  case Builtin::BI__sync_bool_compare_and_swap_1:
  case Builtin::BI__sync_bool_compare_and_swap_2:
  case Builtin::BI__sync_bool_compare_and_swap_4:
  case Builtin::BI__sync_bool_compare_and_swap_8:
  case Builtin::BI__sync_bool_compare_and_swap_16:
  case Builtin::BI__sync_lock_test_and_set:
  case Builtin::BI__sync_lock_test_and_set_1:
  case Builtin::BI__sync_lock_test_and_set_2:
  case Builtin::BI__sync_lock_test_and_set_4:
  case Builtin::BI__sync_lock_test_and_set_8:
  case Builtin::BI__sync_lock_test_and_set_16:
  case Builtin::BI__sync_lock_release:
  case Builtin::BI__sync_lock_release_1:
  case Builtin::BI__sync_lock_release_2:
  case Builtin::BI__sync_lock_release_4:
  case Builtin::BI__sync_lock_release_8:
  case Builtin::BI__sync_lock_release_16:
  case Builtin::BI__sync_swap:
  case Builtin::BI__sync_swap_1:
  case Builtin::BI__sync_swap_2:
  case Builtin::BI__sync_swap_4:
  case Builtin::BI__sync_swap_8:
  case Builtin::BI__sync_swap_16:
    return BuiltinAtomicOverloaded(TheCallResult);
  case Builtin::BI__sync_synchronize:
    Diag(TheCall->getBeginLoc(), diag::warn_atomic_implicit_seq_cst)
        << TheCall->getCallee()->getSourceRange();
    break;
  case Builtin::BI__builtin_nontemporal_load:
  case Builtin::BI__builtin_nontemporal_store:
    return BuiltinNontemporalOverloaded(TheCallResult);
  case Builtin::BI__builtin_memcpy_inline: {
    clang::Expr *SizeOp = TheCall->getArg(2);
    // We warn about copying to or from `nullptr` pointers when `size` is
    // greater than 0. When `size` is value dependent we cannot evaluate its
    // value so we bail out.
    if (SizeOp->isValueDependent())
      break;
    if (!SizeOp->EvaluateKnownConstInt(Context).isZero()) {
      CheckNonNullArgument(*this, TheCall->getArg(0), TheCall->getExprLoc());
      CheckNonNullArgument(*this, TheCall->getArg(1), TheCall->getExprLoc());
    }
    break;
  }
  case Builtin::BI__builtin_memset_inline: {
    clang::Expr *SizeOp = TheCall->getArg(2);
    // We warn about filling to `nullptr` pointers when `size` is greater than
    // 0. When `size` is value dependent we cannot evaluate its value so we bail
    // out.
    if (SizeOp->isValueDependent())
      break;
    if (!SizeOp->EvaluateKnownConstInt(Context).isZero())
      CheckNonNullArgument(*this, TheCall->getArg(0), TheCall->getExprLoc());
    break;
  }
#define BUILTIN(ID, TYPE, ATTRS)
#define ATOMIC_BUILTIN(ID, TYPE, ATTRS)                                        \
  case Builtin::BI##ID:                                                        \
    return AtomicOpsOverloaded(TheCallResult, AtomicExpr::AO##ID);
#include "clang/Basic/Builtins.inc"
  case Builtin::BI__annotation:
    if (BuiltinMSVCAnnotation(*this, TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_annotation:
    if (BuiltinAnnotation(*this, TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_addressof:
    if (BuiltinAddressof(*this, TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_function_start:
    if (BuiltinFunctionStart(*this, TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_is_aligned:
  case Builtin::BI__builtin_align_up:
  case Builtin::BI__builtin_align_down:
    if (BuiltinAlignment(*this, TheCall, BuiltinID))
      return ExprError();
    break;
  case Builtin::BI__builtin_add_overflow:
  case Builtin::BI__builtin_sub_overflow:
  case Builtin::BI__builtin_mul_overflow:
    if (BuiltinOverflow(*this, TheCall, BuiltinID))
      return ExprError();
    break;
  case Builtin::BI__builtin_operator_new:
  case Builtin::BI__builtin_operator_delete: {
    bool IsDelete = BuiltinID == Builtin::BI__builtin_operator_delete;
    ExprResult Res =
        BuiltinOperatorNewDeleteOverloaded(TheCallResult, IsDelete);
    if (Res.isInvalid())
      CorrectDelayedTyposInExpr(TheCallResult.get());
    return Res;
  }
  case Builtin::BI__builtin_dump_struct:
    return BuiltinDumpStruct(*this, TheCall);
  case Builtin::BI__builtin_expect_with_probability: {
    // We first want to ensure we are called with 3 arguments
    if (checkArgCount(TheCall, 3))
      return ExprError();
    // then check probability is constant float in range [0.0, 1.0]
    const Expr *ProbArg = TheCall->getArg(2);
    SmallVector<PartialDiagnosticAt, 8> Notes;
    Expr::EvalResult Eval;
    Eval.Diag = &Notes;
    if ((!ProbArg->EvaluateAsConstantExpr(Eval, Context)) ||
        !Eval.Val.isFloat()) {
      Diag(ProbArg->getBeginLoc(), diag::err_probability_not_constant_float)
          << ProbArg->getSourceRange();
      for (const PartialDiagnosticAt &PDiag : Notes)
        Diag(PDiag.first, PDiag.second);
      return ExprError();
    }
    llvm::APFloat Probability = Eval.Val.getFloat();
    bool LoseInfo = false;
    Probability.convert(llvm::APFloat::IEEEdouble(),
                        llvm::RoundingMode::Dynamic, &LoseInfo);
    if (!(Probability >= llvm::APFloat(0.0) &&
          Probability <= llvm::APFloat(1.0))) {
      Diag(ProbArg->getBeginLoc(), diag::err_probability_out_of_range)
          << ProbArg->getSourceRange();
      return ExprError();
    }
    break;
  }
  case Builtin::BI__builtin_preserve_access_index:
    if (BuiltinPreserveAI(*this, TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_call_with_static_chain:
    if (BuiltinCallWithStaticChain(*this, TheCall))
      return ExprError();
    break;
  case Builtin::BI__exception_code:
  case Builtin::BI_exception_code:
    if (BuiltinSEHScopeCheck(*this, TheCall, Scope::SEHExceptScope,
                             diag::err_seh___except_block))
      return ExprError();
    break;
  case Builtin::BI__exception_info:
  case Builtin::BI_exception_info:
    if (BuiltinSEHScopeCheck(*this, TheCall, Scope::SEHFilterScope,
                             diag::err_seh___except_filter))
      return ExprError();
    break;
  case Builtin::BI__GetExceptionInfo:
    if (checkArgCount(TheCall, 1))
      return ExprError();

    if (CheckCXXThrowOperand(
            TheCall->getBeginLoc(),
            Context.getExceptionObjectType(FDecl->getParamDecl(0)->getType()),
            TheCall))
      return ExprError();

    TheCall->setType(Context.VoidPtrTy);
    break;
  case Builtin::BIaddressof:
  case Builtin::BI__addressof:
  case Builtin::BIforward:
  case Builtin::BIforward_like:
  case Builtin::BImove:
  case Builtin::BImove_if_noexcept:
  case Builtin::BIas_const: {
    // These are all expected to be of the form
    //   T &/&&/* f(U &/&&)
    // where T and U only differ in qualification.
    if (checkArgCount(TheCall, 1))
      return ExprError();
    QualType Param = FDecl->getParamDecl(0)->getType();
    QualType Result = FDecl->getReturnType();
    bool ReturnsPointer = BuiltinID == Builtin::BIaddressof ||
                          BuiltinID == Builtin::BI__addressof;
    if (!(Param->isReferenceType() &&
          (ReturnsPointer ? Result->isAnyPointerType()
                          : Result->isReferenceType()) &&
          Context.hasSameUnqualifiedType(Param->getPointeeType(),
                                         Result->getPointeeType()))) {
      Diag(TheCall->getBeginLoc(), diag::err_builtin_move_forward_unsupported)
          << FDecl;
      return ExprError();
    }
    break;
  }
  case Builtin::BI__builtin_ptrauth_strip:
    return PointerAuthStrip(*this, TheCall);
  case Builtin::BI__builtin_ptrauth_blend_discriminator:
    return PointerAuthBlendDiscriminator(*this, TheCall);
  case Builtin::BI__builtin_ptrauth_sign_constant:
    return PointerAuthSignOrAuth(*this, TheCall, PAO_Sign,
                                 /*RequireConstant=*/true);
  case Builtin::BI__builtin_ptrauth_sign_unauthenticated:
    return PointerAuthSignOrAuth(*this, TheCall, PAO_Sign,
                                 /*RequireConstant=*/false);
  case Builtin::BI__builtin_ptrauth_auth:
    return PointerAuthSignOrAuth(*this, TheCall, PAO_Auth,
                                 /*RequireConstant=*/false);
  case Builtin::BI__builtin_ptrauth_sign_generic_data:
    return PointerAuthSignGenericData(*this, TheCall);
  case Builtin::BI__builtin_ptrauth_auth_and_resign:
    return PointerAuthAuthAndResign(*this, TheCall);
  case Builtin::BI__builtin_ptrauth_string_discriminator:
    return PointerAuthStringDiscriminator(*this, TheCall);
  // OpenCL v2.0, s6.13.16 - Pipe functions
  case Builtin::BIread_pipe:
  case Builtin::BIwrite_pipe:
    // Since those two functions are declared with var args, we need a semantic
    // check for the argument.
    if (OpenCL().checkBuiltinRWPipe(TheCall))
      return ExprError();
    break;
  case Builtin::BIreserve_read_pipe:
  case Builtin::BIreserve_write_pipe:
  case Builtin::BIwork_group_reserve_read_pipe:
  case Builtin::BIwork_group_reserve_write_pipe:
    if (OpenCL().checkBuiltinReserveRWPipe(TheCall))
      return ExprError();
    break;
  case Builtin::BIsub_group_reserve_read_pipe:
  case Builtin::BIsub_group_reserve_write_pipe:
    if (OpenCL().checkSubgroupExt(TheCall) ||
        OpenCL().checkBuiltinReserveRWPipe(TheCall))
      return ExprError();
    break;
  case Builtin::BIcommit_read_pipe:
  case Builtin::BIcommit_write_pipe:
  case Builtin::BIwork_group_commit_read_pipe:
  case Builtin::BIwork_group_commit_write_pipe:
    if (OpenCL().checkBuiltinCommitRWPipe(TheCall))
      return ExprError();
    break;
  case Builtin::BIsub_group_commit_read_pipe:
  case Builtin::BIsub_group_commit_write_pipe:
    if (OpenCL().checkSubgroupExt(TheCall) ||
        OpenCL().checkBuiltinCommitRWPipe(TheCall))
      return ExprError();
    break;
  case Builtin::BIget_pipe_num_packets:
  case Builtin::BIget_pipe_max_packets:
    if (OpenCL().checkBuiltinPipePackets(TheCall))
      return ExprError();
    break;
  case Builtin::BIto_global:
  case Builtin::BIto_local:
  case Builtin::BIto_private:
    if (OpenCL().checkBuiltinToAddr(BuiltinID, TheCall))
      return ExprError();
    break;
  // OpenCL v2.0, s6.13.17 - Enqueue kernel functions.
  case Builtin::BIenqueue_kernel:
    if (OpenCL().checkBuiltinEnqueueKernel(TheCall))
      return ExprError();
    break;
  case Builtin::BIget_kernel_work_group_size:
  case Builtin::BIget_kernel_preferred_work_group_size_multiple:
    if (OpenCL().checkBuiltinKernelWorkGroupSize(TheCall))
      return ExprError();
    break;
  case Builtin::BIget_kernel_max_sub_group_size_for_ndrange:
  case Builtin::BIget_kernel_sub_group_count_for_ndrange:
    if (OpenCL().checkBuiltinNDRangeAndBlock(TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_os_log_format:
    Cleanup.setExprNeedsCleanups(true);
    [[fallthrough]];
  case Builtin::BI__builtin_os_log_format_buffer_size:
    if (BuiltinOSLogFormat(TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_frame_address:
  case Builtin::BI__builtin_return_address: {
    if (BuiltinConstantArgRange(TheCall, 0, 0, 0xFFFF))
      return ExprError();

    // -Wframe-address warning if non-zero passed to builtin
    // return/frame address.
    Expr::EvalResult Result;
    if (!TheCall->getArg(0)->isValueDependent() &&
        TheCall->getArg(0)->EvaluateAsInt(Result, getASTContext()) &&
        Result.Val.getInt() != 0)
      Diag(TheCall->getBeginLoc(), diag::warn_frame_address)
          << ((BuiltinID == Builtin::BI__builtin_return_address)
                  ? "__builtin_return_address"
                  : "__builtin_frame_address")
          << TheCall->getSourceRange();
    break;
  }

  case Builtin::BI__builtin_nondeterministic_value: {
    if (BuiltinNonDeterministicValue(TheCall))
      return ExprError();
    break;
  }

  // __builtin_elementwise_abs restricts the element type to signed integers or
  // floating point types only.
  case Builtin::BI__builtin_elementwise_abs: {
    if (PrepareBuiltinElementwiseMathOneArgCall(TheCall))
      return ExprError();

    QualType ArgTy = TheCall->getArg(0)->getType();
    QualType EltTy = ArgTy;

    if (auto *VecTy = EltTy->getAs<VectorType>())
      EltTy = VecTy->getElementType();
    if (EltTy->isUnsignedIntegerType()) {
      Diag(TheCall->getArg(0)->getBeginLoc(),
           diag::err_builtin_invalid_arg_type)
          << 1 << /* signed integer or float ty*/ 3 << ArgTy;
      return ExprError();
    }
    break;
  }

  // These builtins restrict the element type to floating point
  // types only.
  case Builtin::BI__builtin_elementwise_acos:
  case Builtin::BI__builtin_elementwise_asin:
  case Builtin::BI__builtin_elementwise_atan:
  case Builtin::BI__builtin_elementwise_ceil:
  case Builtin::BI__builtin_elementwise_cos:
  case Builtin::BI__builtin_elementwise_cosh:
  case Builtin::BI__builtin_elementwise_exp:
  case Builtin::BI__builtin_elementwise_exp2:
  case Builtin::BI__builtin_elementwise_floor:
  case Builtin::BI__builtin_elementwise_log:
  case Builtin::BI__builtin_elementwise_log2:
  case Builtin::BI__builtin_elementwise_log10:
  case Builtin::BI__builtin_elementwise_roundeven:
  case Builtin::BI__builtin_elementwise_round:
  case Builtin::BI__builtin_elementwise_rint:
  case Builtin::BI__builtin_elementwise_nearbyint:
  case Builtin::BI__builtin_elementwise_sin:
  case Builtin::BI__builtin_elementwise_sinh:
  case Builtin::BI__builtin_elementwise_sqrt:
  case Builtin::BI__builtin_elementwise_tan:
  case Builtin::BI__builtin_elementwise_tanh:
  case Builtin::BI__builtin_elementwise_trunc:
  case Builtin::BI__builtin_elementwise_canonicalize: {
    if (PrepareBuiltinElementwiseMathOneArgCall(TheCall))
      return ExprError();

    QualType ArgTy = TheCall->getArg(0)->getType();
    if (checkFPMathBuiltinElementType(*this, TheCall->getArg(0)->getBeginLoc(),
                                      ArgTy, 1))
      return ExprError();
    break;
  }
  case Builtin::BI__builtin_elementwise_fma: {
    if (BuiltinElementwiseTernaryMath(TheCall))
      return ExprError();
    break;
  }

  // These builtins restrict the element type to floating point
  // types only, and take in two arguments.
  case Builtin::BI__builtin_elementwise_pow: {
    if (BuiltinElementwiseMath(TheCall))
      return ExprError();

    QualType ArgTy = TheCall->getArg(0)->getType();
    if (checkFPMathBuiltinElementType(*this, TheCall->getArg(0)->getBeginLoc(),
                                      ArgTy, 1) ||
        checkFPMathBuiltinElementType(*this, TheCall->getArg(1)->getBeginLoc(),
                                      ArgTy, 2))
      return ExprError();
    break;
  }

  // These builtins restrict the element type to integer
  // types only.
  case Builtin::BI__builtin_elementwise_add_sat:
  case Builtin::BI__builtin_elementwise_sub_sat: {
    if (BuiltinElementwiseMath(TheCall))
      return ExprError();

    const Expr *Arg = TheCall->getArg(0);
    QualType ArgTy = Arg->getType();
    QualType EltTy = ArgTy;

    if (auto *VecTy = EltTy->getAs<VectorType>())
      EltTy = VecTy->getElementType();

    if (!EltTy->isIntegerType()) {
      Diag(Arg->getBeginLoc(), diag::err_builtin_invalid_arg_type)
          << 1 << /* integer ty */ 6 << ArgTy;
      return ExprError();
    }
    break;
  }

  case Builtin::BI__builtin_elementwise_min:
  case Builtin::BI__builtin_elementwise_max:
    if (BuiltinElementwiseMath(TheCall))
      return ExprError();
    break;

  case Builtin::BI__builtin_elementwise_bitreverse: {
    if (PrepareBuiltinElementwiseMathOneArgCall(TheCall))
      return ExprError();

    const Expr *Arg = TheCall->getArg(0);
    QualType ArgTy = Arg->getType();
    QualType EltTy = ArgTy;

    if (auto *VecTy = EltTy->getAs<VectorType>())
      EltTy = VecTy->getElementType();

    if (!EltTy->isIntegerType()) {
      Diag(Arg->getBeginLoc(), diag::err_builtin_invalid_arg_type)
          << 1 << /* integer ty */ 6 << ArgTy;
      return ExprError();
    }
    break;
  }

  case Builtin::BI__builtin_elementwise_copysign: {
    if (checkArgCount(TheCall, 2))
      return ExprError();

    ExprResult Magnitude = UsualUnaryConversions(TheCall->getArg(0));
    ExprResult Sign = UsualUnaryConversions(TheCall->getArg(1));
    if (Magnitude.isInvalid() || Sign.isInvalid())
      return ExprError();

    QualType MagnitudeTy = Magnitude.get()->getType();
    QualType SignTy = Sign.get()->getType();
    if (checkFPMathBuiltinElementType(*this, TheCall->getArg(0)->getBeginLoc(),
                                      MagnitudeTy, 1) ||
        checkFPMathBuiltinElementType(*this, TheCall->getArg(1)->getBeginLoc(),
                                      SignTy, 2)) {
      return ExprError();
    }

    if (MagnitudeTy.getCanonicalType() != SignTy.getCanonicalType()) {
      return Diag(Sign.get()->getBeginLoc(),
                  diag::err_typecheck_call_different_arg_types)
             << MagnitudeTy << SignTy;
    }

    TheCall->setArg(0, Magnitude.get());
    TheCall->setArg(1, Sign.get());
    TheCall->setType(Magnitude.get()->getType());
    break;
  }
  case Builtin::BI__builtin_reduce_max:
  case Builtin::BI__builtin_reduce_min: {
    if (PrepareBuiltinReduceMathOneArgCall(TheCall))
      return ExprError();

    const Expr *Arg = TheCall->getArg(0);
    const auto *TyA = Arg->getType()->getAs<VectorType>();

    QualType ElTy;
    if (TyA)
      ElTy = TyA->getElementType();
    else if (Arg->getType()->isSizelessVectorType())
      ElTy = Arg->getType()->getSizelessVectorEltType(Context);

    if (ElTy.isNull()) {
      Diag(Arg->getBeginLoc(), diag::err_builtin_invalid_arg_type)
          << 1 << /* vector ty*/ 4 << Arg->getType();
      return ExprError();
    }

    TheCall->setType(ElTy);
    break;
  }

  // These builtins support vectors of integers only.
  // TODO: ADD/MUL should support floating-point types.
  case Builtin::BI__builtin_reduce_add:
  case Builtin::BI__builtin_reduce_mul:
  case Builtin::BI__builtin_reduce_xor:
  case Builtin::BI__builtin_reduce_or:
  case Builtin::BI__builtin_reduce_and: {
    if (PrepareBuiltinReduceMathOneArgCall(TheCall))
      return ExprError();

    const Expr *Arg = TheCall->getArg(0);
    const auto *TyA = Arg->getType()->getAs<VectorType>();

    QualType ElTy;
    if (TyA)
      ElTy = TyA->getElementType();
    else if (Arg->getType()->isSizelessVectorType())
      ElTy = Arg->getType()->getSizelessVectorEltType(Context);

    if (ElTy.isNull() || !ElTy->isIntegerType()) {
      Diag(Arg->getBeginLoc(), diag::err_builtin_invalid_arg_type)
          << 1  << /* vector of integers */ 6 << Arg->getType();
      return ExprError();
    }

    TheCall->setType(ElTy);
    break;
  }

  case Builtin::BI__builtin_matrix_transpose:
    return BuiltinMatrixTranspose(TheCall, TheCallResult);

  case Builtin::BI__builtin_matrix_column_major_load:
    return BuiltinMatrixColumnMajorLoad(TheCall, TheCallResult);

  case Builtin::BI__builtin_matrix_column_major_store:
    return BuiltinMatrixColumnMajorStore(TheCall, TheCallResult);

  case Builtin::BI__builtin_verbose_trap:
    if (!checkBuiltinVerboseTrap(TheCall, *this))
      return ExprError();
    break;

  case Builtin::BI__builtin_get_device_side_mangled_name: {
    auto Check = [](CallExpr *TheCall) {
      if (TheCall->getNumArgs() != 1)
        return false;
      auto *DRE = dyn_cast<DeclRefExpr>(TheCall->getArg(0)->IgnoreImpCasts());
      if (!DRE)
        return false;
      auto *D = DRE->getDecl();
      if (!isa<FunctionDecl>(D) && !isa<VarDecl>(D))
        return false;
      return D->hasAttr<CUDAGlobalAttr>() || D->hasAttr<CUDADeviceAttr>() ||
             D->hasAttr<CUDAConstantAttr>() || D->hasAttr<HIPManagedAttr>();
    };
    if (!Check(TheCall)) {
      Diag(TheCall->getBeginLoc(),
           diag::err_hip_invalid_args_builtin_mangled_name);
      return ExprError();
    }
    break;
  }
  case Builtin::BI__builtin_popcountg:
    if (BuiltinPopcountg(*this, TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_clzg:
  case Builtin::BI__builtin_ctzg:
    if (BuiltinCountZeroBitsGeneric(*this, TheCall))
      return ExprError();
    break;

  case Builtin::BI__builtin_allow_runtime_check: {
    Expr *Arg = TheCall->getArg(0);
    // Check if the argument is a string literal.
    if (!isa<StringLiteral>(Arg->IgnoreParenImpCasts())) {
      Diag(TheCall->getBeginLoc(), diag::err_expr_not_string_literal)
          << Arg->getSourceRange();
      return ExprError();
    }
    break;
  }
  }

  if (getLangOpts().HLSL && HLSL().CheckBuiltinFunctionCall(BuiltinID, TheCall))
    return ExprError();

  // Since the target specific builtins for each arch overlap, only check those
  // of the arch we are compiling for.
  if (Context.BuiltinInfo.isTSBuiltin(BuiltinID)) {
    if (Context.BuiltinInfo.isAuxBuiltinID(BuiltinID)) {
      assert(Context.getAuxTargetInfo() &&
             "Aux Target Builtin, but not an aux target?");

      if (CheckTSBuiltinFunctionCall(
              *Context.getAuxTargetInfo(),
              Context.BuiltinInfo.getAuxBuiltinID(BuiltinID), TheCall))
        return ExprError();
    } else {
      if (CheckTSBuiltinFunctionCall(Context.getTargetInfo(), BuiltinID,
                                     TheCall))
        return ExprError();
    }
  }

  return TheCallResult;
}

bool Sema::ValueIsRunOfOnes(CallExpr *TheCall, unsigned ArgNum) {
  llvm::APSInt Result;
  // We can't check the value of a dependent argument.
  Expr *Arg = TheCall->getArg(ArgNum);
  if (Arg->isTypeDependent() || Arg->isValueDependent())
    return false;

  // Check constant-ness first.
  if (BuiltinConstantArg(TheCall, ArgNum, Result))
    return true;

  // Check contiguous run of 1s, 0xFF0000FF is also a run of 1s.
  if (Result.isShiftedMask() || (~Result).isShiftedMask())
    return false;

  return Diag(TheCall->getBeginLoc(),
              diag::err_argument_not_contiguous_bit_field)
         << ArgNum << Arg->getSourceRange();
}

bool Sema::getFormatStringInfo(const FormatAttr *Format, bool IsCXXMember,
                               bool IsVariadic, FormatStringInfo *FSI) {
  if (Format->getFirstArg() == 0)
    FSI->ArgPassingKind = FAPK_VAList;
  else if (IsVariadic)
    FSI->ArgPassingKind = FAPK_Variadic;
  else
    FSI->ArgPassingKind = FAPK_Fixed;
  FSI->FormatIdx = Format->getFormatIdx() - 1;
  FSI->FirstDataArg =
      FSI->ArgPassingKind == FAPK_VAList ? 0 : Format->getFirstArg() - 1;

  // The way the format attribute works in GCC, the implicit this argument
  // of member functions is counted. However, it doesn't appear in our own
  // lists, so decrement format_idx in that case.
  if (IsCXXMember) {
    if(FSI->FormatIdx == 0)
      return false;
    --FSI->FormatIdx;
    if (FSI->FirstDataArg != 0)
      --FSI->FirstDataArg;
  }
  return true;
}

/// Checks if a the given expression evaluates to null.
///
/// Returns true if the value evaluates to null.
static bool CheckNonNullExpr(Sema &S, const Expr *Expr) {
  // Treat (smart) pointers constructed from nullptr as null, whether we can
  // const-evaluate them or not.
  // This must happen first: the smart pointer expr might have _Nonnull type!
  if (isa<CXXNullPtrLiteralExpr>(
          IgnoreExprNodes(Expr, IgnoreImplicitAsWrittenSingleStep,
                          IgnoreElidableImplicitConstructorSingleStep)))
    return true;

  // If the expression has non-null type, it doesn't evaluate to null.
  if (auto nullability = Expr->IgnoreImplicit()->getType()->getNullability()) {
    if (*nullability == NullabilityKind::NonNull)
      return false;
  }

  // As a special case, transparent unions initialized with zero are
  // considered null for the purposes of the nonnull attribute.
  if (const RecordType *UT = Expr->getType()->getAsUnionType();
      UT && UT->getDecl()->hasAttr<TransparentUnionAttr>()) {
    if (const auto *CLE = dyn_cast<CompoundLiteralExpr>(Expr))
      if (const auto *ILE = dyn_cast<InitListExpr>(CLE->getInitializer()))
        Expr = ILE->getInit(0);
  }

  bool Result;
  return (!Expr->isValueDependent() &&
          Expr->EvaluateAsBooleanCondition(Result, S.Context) &&
          !Result);
}

static void CheckNonNullArgument(Sema &S,
                                 const Expr *ArgExpr,
                                 SourceLocation CallSiteLoc) {
  if (CheckNonNullExpr(S, ArgExpr))
    S.DiagRuntimeBehavior(CallSiteLoc, ArgExpr,
                          S.PDiag(diag::warn_null_arg)
                              << ArgExpr->getSourceRange());
}

/// Determine whether the given type has a non-null nullability annotation.
static bool isNonNullType(QualType type) {
  if (auto nullability = type->getNullability())
    return *nullability == NullabilityKind::NonNull;

  return false;
}

static void CheckNonNullArguments(Sema &S,
                                  const NamedDecl *FDecl,
                                  const FunctionProtoType *Proto,
                                  ArrayRef<const Expr *> Args,
                                  SourceLocation CallSiteLoc) {
  assert((FDecl || Proto) && "Need a function declaration or prototype");

  // Already checked by constant evaluator.
  if (S.isConstantEvaluatedContext())
    return;
  // Check the attributes attached to the method/function itself.
  llvm::SmallBitVector NonNullArgs;
  if (FDecl) {
    // Handle the nonnull attribute on the function/method declaration itself.
    for (const auto *NonNull : FDecl->specific_attrs<NonNullAttr>()) {
      if (!NonNull->args_size()) {
        // Easy case: all pointer arguments are nonnull.
        for (const auto *Arg : Args)
          if (S.isValidPointerAttrType(Arg->getType()))
            CheckNonNullArgument(S, Arg, CallSiteLoc);
        return;
      }

      for (const ParamIdx &Idx : NonNull->args()) {
        unsigned IdxAST = Idx.getASTIndex();
        if (IdxAST >= Args.size())
          continue;
        if (NonNullArgs.empty())
          NonNullArgs.resize(Args.size());
        NonNullArgs.set(IdxAST);
      }
    }
  }

  if (FDecl && (isa<FunctionDecl>(FDecl) || isa<ObjCMethodDecl>(FDecl))) {
    // Handle the nonnull attribute on the parameters of the
    // function/method.
    ArrayRef<ParmVarDecl*> parms;
    if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(FDecl))
      parms = FD->parameters();
    else
      parms = cast<ObjCMethodDecl>(FDecl)->parameters();

    unsigned ParamIndex = 0;
    for (ArrayRef<ParmVarDecl*>::iterator I = parms.begin(), E = parms.end();
         I != E; ++I, ++ParamIndex) {
      const ParmVarDecl *PVD = *I;
      if (PVD->hasAttr<NonNullAttr>() || isNonNullType(PVD->getType())) {
        if (NonNullArgs.empty())
          NonNullArgs.resize(Args.size());

        NonNullArgs.set(ParamIndex);
      }
    }
  } else {
    // If we have a non-function, non-method declaration but no
    // function prototype, try to dig out the function prototype.
    if (!Proto) {
      if (const ValueDecl *VD = dyn_cast<ValueDecl>(FDecl)) {
        QualType type = VD->getType().getNonReferenceType();
        if (auto pointerType = type->getAs<PointerType>())
          type = pointerType->getPointeeType();
        else if (auto blockType = type->getAs<BlockPointerType>())
          type = blockType->getPointeeType();
        // FIXME: data member pointers?

        // Dig out the function prototype, if there is one.
        Proto = type->getAs<FunctionProtoType>();
      }
    }

    // Fill in non-null argument information from the nullability
    // information on the parameter types (if we have them).
    if (Proto) {
      unsigned Index = 0;
      for (auto paramType : Proto->getParamTypes()) {
        if (isNonNullType(paramType)) {
          if (NonNullArgs.empty())
            NonNullArgs.resize(Args.size());

          NonNullArgs.set(Index);
        }

        ++Index;
      }
    }
  }

  // Check for non-null arguments.
  for (unsigned ArgIndex = 0, ArgIndexEnd = NonNullArgs.size();
       ArgIndex != ArgIndexEnd; ++ArgIndex) {
    if (NonNullArgs[ArgIndex])
      CheckNonNullArgument(S, Args[ArgIndex], Args[ArgIndex]->getExprLoc());
  }
}

void Sema::CheckArgAlignment(SourceLocation Loc, NamedDecl *FDecl,
                             StringRef ParamName, QualType ArgTy,
                             QualType ParamTy) {

  // If a function accepts a pointer or reference type
  if (!ParamTy->isPointerType() && !ParamTy->isReferenceType())
    return;

  // If the parameter is a pointer type, get the pointee type for the
  // argument too. If the parameter is a reference type, don't try to get
  // the pointee type for the argument.
  if (ParamTy->isPointerType())
    ArgTy = ArgTy->getPointeeType();

  // Remove reference or pointer
  ParamTy = ParamTy->getPointeeType();

  // Find expected alignment, and the actual alignment of the passed object.
  // getTypeAlignInChars requires complete types
  if (ArgTy.isNull() || ParamTy->isDependentType() ||
      ParamTy->isIncompleteType() || ArgTy->isIncompleteType() ||
      ParamTy->isUndeducedType() || ArgTy->isUndeducedType())
    return;

  CharUnits ParamAlign = Context.getTypeAlignInChars(ParamTy);
  CharUnits ArgAlign = Context.getTypeAlignInChars(ArgTy);

  // If the argument is less aligned than the parameter, there is a
  // potential alignment issue.
  if (ArgAlign < ParamAlign)
    Diag(Loc, diag::warn_param_mismatched_alignment)
        << (int)ArgAlign.getQuantity() << (int)ParamAlign.getQuantity()
        << ParamName << (FDecl != nullptr) << FDecl;
}

void Sema::checkCall(NamedDecl *FDecl, const FunctionProtoType *Proto,
                     const Expr *ThisArg, ArrayRef<const Expr *> Args,
                     bool IsMemberFunction, SourceLocation Loc,
                     SourceRange Range, VariadicCallType CallType) {
  // FIXME: We should check as much as we can in the template definition.
  if (CurContext->isDependentContext())
    return;

  // Printf and scanf checking.
  llvm::SmallBitVector CheckedVarArgs;
  if (FDecl) {
    for (const auto *I : FDecl->specific_attrs<FormatAttr>()) {
      // Only create vector if there are format attributes.
      CheckedVarArgs.resize(Args.size());

      CheckFormatArguments(I, Args, IsMemberFunction, CallType, Loc, Range,
                           CheckedVarArgs);
    }
  }

  // Refuse POD arguments that weren't caught by the format string
  // checks above.
  auto *FD = dyn_cast_or_null<FunctionDecl>(FDecl);
  if (CallType != VariadicDoesNotApply &&
      (!FD || FD->getBuiltinID() != Builtin::BI__noop)) {
    unsigned NumParams = Proto ? Proto->getNumParams()
                         : isa_and_nonnull<FunctionDecl>(FDecl)
                             ? cast<FunctionDecl>(FDecl)->getNumParams()
                         : isa_and_nonnull<ObjCMethodDecl>(FDecl)
                             ? cast<ObjCMethodDecl>(FDecl)->param_size()
                             : 0;

    for (unsigned ArgIdx = NumParams; ArgIdx < Args.size(); ++ArgIdx) {
      // Args[ArgIdx] can be null in malformed code.
      if (const Expr *Arg = Args[ArgIdx]) {
        if (CheckedVarArgs.empty() || !CheckedVarArgs[ArgIdx])
          checkVariadicArgument(Arg, CallType);
      }
    }
  }

  if (FDecl || Proto) {
    CheckNonNullArguments(*this, FDecl, Proto, Args, Loc);

    // Type safety checking.
    if (FDecl) {
      for (const auto *I : FDecl->specific_attrs<ArgumentWithTypeTagAttr>())
        CheckArgumentWithTypeTag(I, Args, Loc);
    }
  }

  // Check that passed arguments match the alignment of original arguments.
  // Try to get the missing prototype from the declaration.
  if (!Proto && FDecl) {
    const auto *FT = FDecl->getFunctionType();
    if (isa_and_nonnull<FunctionProtoType>(FT))
      Proto = cast<FunctionProtoType>(FDecl->getFunctionType());
  }
  if (Proto) {
    // For variadic functions, we may have more args than parameters.
    // For some K&R functions, we may have less args than parameters.
    const auto N = std::min<unsigned>(Proto->getNumParams(), Args.size());
    bool IsScalableRet = Proto->getReturnType()->isSizelessVectorType();
    bool IsScalableArg = false;
    for (unsigned ArgIdx = 0; ArgIdx < N; ++ArgIdx) {
      // Args[ArgIdx] can be null in malformed code.
      if (const Expr *Arg = Args[ArgIdx]) {
        if (Arg->containsErrors())
          continue;

        if (Context.getTargetInfo().getTriple().isOSAIX() && FDecl && Arg &&
            FDecl->hasLinkage() &&
            FDecl->getFormalLinkage() != Linkage::Internal &&
            CallType == VariadicDoesNotApply)
          PPC().checkAIXMemberAlignment((Arg->getExprLoc()), Arg);

        QualType ParamTy = Proto->getParamType(ArgIdx);
        if (ParamTy->isSizelessVectorType())
          IsScalableArg = true;
        QualType ArgTy = Arg->getType();
        CheckArgAlignment(Arg->getExprLoc(), FDecl, std::to_string(ArgIdx + 1),
                          ArgTy, ParamTy);
      }
    }

    // If the callee has an AArch64 SME attribute to indicate that it is an
    // __arm_streaming function, then the caller requires SME to be available.
    FunctionProtoType::ExtProtoInfo ExtInfo = Proto->getExtProtoInfo();
    if (ExtInfo.AArch64SMEAttributes & FunctionType::SME_PStateSMEnabledMask) {
      if (auto *CallerFD = dyn_cast<FunctionDecl>(CurContext)) {
        llvm::StringMap<bool> CallerFeatureMap;
        Context.getFunctionFeatureMap(CallerFeatureMap, CallerFD);
        if (!CallerFeatureMap.contains("sme"))
          Diag(Loc, diag::err_sme_call_in_non_sme_target);
      } else if (!Context.getTargetInfo().hasFeature("sme")) {
        Diag(Loc, diag::err_sme_call_in_non_sme_target);
      }
    }

    // If the call requires a streaming-mode change and has scalable vector
    // arguments or return values, then warn the user that the streaming and
    // non-streaming vector lengths may be different.
    const auto *CallerFD = dyn_cast<FunctionDecl>(CurContext);
    if (CallerFD && (!FD || !FD->getBuiltinID()) &&
        (IsScalableArg || IsScalableRet)) {
      bool IsCalleeStreaming =
          ExtInfo.AArch64SMEAttributes & FunctionType::SME_PStateSMEnabledMask;
      bool IsCalleeStreamingCompatible =
          ExtInfo.AArch64SMEAttributes &
          FunctionType::SME_PStateSMCompatibleMask;
      SemaARM::ArmStreamingType CallerFnType = getArmStreamingFnType(CallerFD);
      if (!IsCalleeStreamingCompatible &&
          (CallerFnType == SemaARM::ArmStreamingCompatible ||
           ((CallerFnType == SemaARM::ArmStreaming) ^ IsCalleeStreaming))) {
        if (IsScalableArg)
          Diag(Loc, diag::warn_sme_streaming_pass_return_vl_to_non_streaming)
              << /*IsArg=*/true;
        if (IsScalableRet)
          Diag(Loc, diag::warn_sme_streaming_pass_return_vl_to_non_streaming)
              << /*IsArg=*/false;
      }
    }

    FunctionType::ArmStateValue CalleeArmZAState =
        FunctionType::getArmZAState(ExtInfo.AArch64SMEAttributes);
    FunctionType::ArmStateValue CalleeArmZT0State =
        FunctionType::getArmZT0State(ExtInfo.AArch64SMEAttributes);
    if (CalleeArmZAState != FunctionType::ARM_None ||
        CalleeArmZT0State != FunctionType::ARM_None) {
      bool CallerHasZAState = false;
      bool CallerHasZT0State = false;
      if (CallerFD) {
        auto *Attr = CallerFD->getAttr<ArmNewAttr>();
        if (Attr && Attr->isNewZA())
          CallerHasZAState = true;
        if (Attr && Attr->isNewZT0())
          CallerHasZT0State = true;
        if (const auto *FPT = CallerFD->getType()->getAs<FunctionProtoType>()) {
          CallerHasZAState |=
              FunctionType::getArmZAState(
                  FPT->getExtProtoInfo().AArch64SMEAttributes) !=
              FunctionType::ARM_None;
          CallerHasZT0State |=
              FunctionType::getArmZT0State(
                  FPT->getExtProtoInfo().AArch64SMEAttributes) !=
              FunctionType::ARM_None;
        }
      }

      if (CalleeArmZAState != FunctionType::ARM_None && !CallerHasZAState)
        Diag(Loc, diag::err_sme_za_call_no_za_state);

      if (CalleeArmZT0State != FunctionType::ARM_None && !CallerHasZT0State)
        Diag(Loc, diag::err_sme_zt0_call_no_zt0_state);

      if (CallerHasZAState && CalleeArmZAState == FunctionType::ARM_None &&
          CalleeArmZT0State != FunctionType::ARM_None) {
        Diag(Loc, diag::err_sme_unimplemented_za_save_restore);
        Diag(Loc, diag::note_sme_use_preserves_za);
      }
    }
  }

  if (FDecl && FDecl->hasAttr<AllocAlignAttr>()) {
    auto *AA = FDecl->getAttr<AllocAlignAttr>();
    const Expr *Arg = Args[AA->getParamIndex().getASTIndex()];
    if (!Arg->isValueDependent()) {
      Expr::EvalResult Align;
      if (Arg->EvaluateAsInt(Align, Context)) {
        const llvm::APSInt &I = Align.Val.getInt();
        if (!I.isPowerOf2())
          Diag(Arg->getExprLoc(), diag::warn_alignment_not_power_of_two)
              << Arg->getSourceRange();

        if (I > Sema::MaximumAlignment)
          Diag(Arg->getExprLoc(), diag::warn_assume_aligned_too_great)
              << Arg->getSourceRange() << Sema::MaximumAlignment;
      }
    }
  }

  if (FD)
    diagnoseArgDependentDiagnoseIfAttrs(FD, ThisArg, Args, Loc);
}

void Sema::CheckConstrainedAuto(const AutoType *AutoT, SourceLocation Loc) {
  if (ConceptDecl *Decl = AutoT->getTypeConstraintConcept()) {
    DiagnoseUseOfDecl(Decl, Loc);
  }
}

void Sema::CheckConstructorCall(FunctionDecl *FDecl, QualType ThisType,
                                ArrayRef<const Expr *> Args,
                                const FunctionProtoType *Proto,
                                SourceLocation Loc) {
  VariadicCallType CallType =
      Proto->isVariadic() ? VariadicConstructor : VariadicDoesNotApply;

  auto *Ctor = cast<CXXConstructorDecl>(FDecl);
  CheckArgAlignment(
      Loc, FDecl, "'this'", Context.getPointerType(ThisType),
      Context.getPointerType(Ctor->getFunctionObjectParameterType()));

  checkCall(FDecl, Proto, /*ThisArg=*/nullptr, Args, /*IsMemberFunction=*/true,
            Loc, SourceRange(), CallType);
}

bool Sema::CheckFunctionCall(FunctionDecl *FDecl, CallExpr *TheCall,
                             const FunctionProtoType *Proto) {
  bool IsMemberOperatorCall = isa<CXXOperatorCallExpr>(TheCall) &&
                              isa<CXXMethodDecl>(FDecl);
  bool IsMemberFunction = isa<CXXMemberCallExpr>(TheCall) ||
                          IsMemberOperatorCall;
  VariadicCallType CallType = getVariadicCallType(FDecl, Proto,
                                                  TheCall->getCallee());
  Expr** Args = TheCall->getArgs();
  unsigned NumArgs = TheCall->getNumArgs();

  Expr *ImplicitThis = nullptr;
  if (IsMemberOperatorCall && !FDecl->hasCXXExplicitFunctionObjectParameter()) {
    // If this is a call to a member operator, hide the first
    // argument from checkCall.
    // FIXME: Our choice of AST representation here is less than ideal.
    ImplicitThis = Args[0];
    ++Args;
    --NumArgs;
  } else if (IsMemberFunction && !FDecl->isStatic() &&
             !FDecl->hasCXXExplicitFunctionObjectParameter())
    ImplicitThis =
        cast<CXXMemberCallExpr>(TheCall)->getImplicitObjectArgument();

  if (ImplicitThis) {
    // ImplicitThis may or may not be a pointer, depending on whether . or -> is
    // used.
    QualType ThisType = ImplicitThis->getType();
    if (!ThisType->isPointerType()) {
      assert(!ThisType->isReferenceType());
      ThisType = Context.getPointerType(ThisType);
    }

    QualType ThisTypeFromDecl = Context.getPointerType(
        cast<CXXMethodDecl>(FDecl)->getFunctionObjectParameterType());

    CheckArgAlignment(TheCall->getRParenLoc(), FDecl, "'this'", ThisType,
                      ThisTypeFromDecl);
  }

  checkCall(FDecl, Proto, ImplicitThis, llvm::ArrayRef(Args, NumArgs),
            IsMemberFunction, TheCall->getRParenLoc(),
            TheCall->getCallee()->getSourceRange(), CallType);

  IdentifierInfo *FnInfo = FDecl->getIdentifier();
  // None of the checks below are needed for functions that don't have
  // simple names (e.g., C++ conversion functions).
  if (!FnInfo)
    return false;

  // Enforce TCB except for builtin calls, which are always allowed.
  if (FDecl->getBuiltinID() == 0)
    CheckTCBEnforcement(TheCall->getExprLoc(), FDecl);

  CheckAbsoluteValueFunction(TheCall, FDecl);
  CheckMaxUnsignedZero(TheCall, FDecl);
  CheckInfNaNFunction(TheCall, FDecl);

  if (getLangOpts().ObjC)
    ObjC().DiagnoseCStringFormatDirectiveInCFAPI(FDecl, Args, NumArgs);

  unsigned CMId = FDecl->getMemoryFunctionKind();

  // Handle memory setting and copying functions.
  switch (CMId) {
  case 0:
    return false;
  case Builtin::BIstrlcpy: // fallthrough
  case Builtin::BIstrlcat:
    CheckStrlcpycatArguments(TheCall, FnInfo);
    break;
  case Builtin::BIstrncat:
    CheckStrncatArguments(TheCall, FnInfo);
    break;
  case Builtin::BIfree:
    CheckFreeArguments(TheCall);
    break;
  default:
    CheckMemaccessArguments(TheCall, CMId, FnInfo);
  }

  return false;
}

bool Sema::CheckPointerCall(NamedDecl *NDecl, CallExpr *TheCall,
                            const FunctionProtoType *Proto) {
  QualType Ty;
  if (const auto *V = dyn_cast<VarDecl>(NDecl))
    Ty = V->getType().getNonReferenceType();
  else if (const auto *F = dyn_cast<FieldDecl>(NDecl))
    Ty = F->getType().getNonReferenceType();
  else
    return false;

  if (!Ty->isBlockPointerType() && !Ty->isFunctionPointerType() &&
      !Ty->isFunctionProtoType())
    return false;

  VariadicCallType CallType;
  if (!Proto || !Proto->isVariadic()) {
    CallType = VariadicDoesNotApply;
  } else if (Ty->isBlockPointerType()) {
    CallType = VariadicBlock;
  } else { // Ty->isFunctionPointerType()
    CallType = VariadicFunction;
  }

  checkCall(NDecl, Proto, /*ThisArg=*/nullptr,
            llvm::ArrayRef(TheCall->getArgs(), TheCall->getNumArgs()),
            /*IsMemberFunction=*/false, TheCall->getRParenLoc(),
            TheCall->getCallee()->getSourceRange(), CallType);

  return false;
}

bool Sema::CheckOtherCall(CallExpr *TheCall, const FunctionProtoType *Proto) {
  VariadicCallType CallType = getVariadicCallType(/*FDecl=*/nullptr, Proto,
                                                  TheCall->getCallee());
  checkCall(/*FDecl=*/nullptr, Proto, /*ThisArg=*/nullptr,
            llvm::ArrayRef(TheCall->getArgs(), TheCall->getNumArgs()),
            /*IsMemberFunction=*/false, TheCall->getRParenLoc(),
            TheCall->getCallee()->getSourceRange(), CallType);

  return false;
}

static bool isValidOrderingForOp(int64_t Ordering, AtomicExpr::AtomicOp Op) {
  if (!llvm::isValidAtomicOrderingCABI(Ordering))
    return false;

  auto OrderingCABI = (llvm::AtomicOrderingCABI)Ordering;
  switch (Op) {
  case AtomicExpr::AO__c11_atomic_init:
  case AtomicExpr::AO__opencl_atomic_init:
    llvm_unreachable("There is no ordering argument for an init");

  case AtomicExpr::AO__c11_atomic_load:
  case AtomicExpr::AO__opencl_atomic_load:
  case AtomicExpr::AO__hip_atomic_load:
  case AtomicExpr::AO__atomic_load_n:
  case AtomicExpr::AO__atomic_load:
  case AtomicExpr::AO__scoped_atomic_load_n:
  case AtomicExpr::AO__scoped_atomic_load:
    return OrderingCABI != llvm::AtomicOrderingCABI::release &&
           OrderingCABI != llvm::AtomicOrderingCABI::acq_rel;

  case AtomicExpr::AO__c11_atomic_store:
  case AtomicExpr::AO__opencl_atomic_store:
  case AtomicExpr::AO__hip_atomic_store:
  case AtomicExpr::AO__atomic_store:
  case AtomicExpr::AO__atomic_store_n:
  case AtomicExpr::AO__scoped_atomic_store:
  case AtomicExpr::AO__scoped_atomic_store_n:
    return OrderingCABI != llvm::AtomicOrderingCABI::consume &&
           OrderingCABI != llvm::AtomicOrderingCABI::acquire &&
           OrderingCABI != llvm::AtomicOrderingCABI::acq_rel;

  default:
    return true;
  }
}

ExprResult Sema::AtomicOpsOverloaded(ExprResult TheCallResult,
                                     AtomicExpr::AtomicOp Op) {
  CallExpr *TheCall = cast<CallExpr>(TheCallResult.get());
  DeclRefExpr *DRE =cast<DeclRefExpr>(TheCall->getCallee()->IgnoreParenCasts());
  MultiExprArg Args{TheCall->getArgs(), TheCall->getNumArgs()};
  return BuildAtomicExpr({TheCall->getBeginLoc(), TheCall->getEndLoc()},
                         DRE->getSourceRange(), TheCall->getRParenLoc(), Args,
                         Op);
}

ExprResult Sema::BuildAtomicExpr(SourceRange CallRange, SourceRange ExprRange,
                                 SourceLocation RParenLoc, MultiExprArg Args,
                                 AtomicExpr::AtomicOp Op,
                                 AtomicArgumentOrder ArgOrder) {
  // All the non-OpenCL operations take one of the following forms.
  // The OpenCL operations take the __c11 forms with one extra argument for
  // synchronization scope.
  enum {
    // C    __c11_atomic_init(A *, C)
    Init,

    // C    __c11_atomic_load(A *, int)
    Load,

    // void __atomic_load(A *, CP, int)
    LoadCopy,

    // void __atomic_store(A *, CP, int)
    Copy,

    // C    __c11_atomic_add(A *, M, int)
    Arithmetic,

    // C    __atomic_exchange_n(A *, CP, int)
    Xchg,

    // void __atomic_exchange(A *, C *, CP, int)
    GNUXchg,

    // bool __c11_atomic_compare_exchange_strong(A *, C *, CP, int, int)
    C11CmpXchg,

    // bool __atomic_compare_exchange(A *, C *, CP, bool, int, int)
    GNUCmpXchg
  } Form = Init;

  const unsigned NumForm = GNUCmpXchg + 1;
  const unsigned NumArgs[] = { 2, 2, 3, 3, 3, 3, 4, 5, 6 };
  const unsigned NumVals[] = { 1, 0, 1, 1, 1, 1, 2, 2, 3 };
  // where:
  //   C is an appropriate type,
  //   A is volatile _Atomic(C) for __c11 builtins and is C for GNU builtins,
  //   CP is C for __c11 builtins and GNU _n builtins and is C * otherwise,
  //   M is C if C is an integer, and ptrdiff_t if C is a pointer, and
  //   the int parameters are for orderings.

  static_assert(sizeof(NumArgs)/sizeof(NumArgs[0]) == NumForm
      && sizeof(NumVals)/sizeof(NumVals[0]) == NumForm,
      "need to update code for modified forms");
  static_assert(AtomicExpr::AO__atomic_add_fetch == 0 &&
                    AtomicExpr::AO__atomic_xor_fetch + 1 ==
                        AtomicExpr::AO__c11_atomic_compare_exchange_strong,
                "need to update code for modified C11 atomics");
  bool IsOpenCL = Op >= AtomicExpr::AO__opencl_atomic_compare_exchange_strong &&
                  Op <= AtomicExpr::AO__opencl_atomic_store;
  bool IsHIP = Op >= AtomicExpr::AO__hip_atomic_compare_exchange_strong &&
               Op <= AtomicExpr::AO__hip_atomic_store;
  bool IsScoped = Op >= AtomicExpr::AO__scoped_atomic_add_fetch &&
                  Op <= AtomicExpr::AO__scoped_atomic_xor_fetch;
  bool IsC11 = (Op >= AtomicExpr::AO__c11_atomic_compare_exchange_strong &&
                Op <= AtomicExpr::AO__c11_atomic_store) ||
               IsOpenCL;
  bool IsN = Op == AtomicExpr::AO__atomic_load_n ||
             Op == AtomicExpr::AO__atomic_store_n ||
             Op == AtomicExpr::AO__atomic_exchange_n ||
             Op == AtomicExpr::AO__atomic_compare_exchange_n ||
             Op == AtomicExpr::AO__scoped_atomic_load_n ||
             Op == AtomicExpr::AO__scoped_atomic_store_n ||
             Op == AtomicExpr::AO__scoped_atomic_exchange_n ||
             Op == AtomicExpr::AO__scoped_atomic_compare_exchange_n;
  // Bit mask for extra allowed value types other than integers for atomic
  // arithmetic operations. Add/sub allow pointer and floating point. Min/max
  // allow floating point.
  enum ArithOpExtraValueType {
    AOEVT_None = 0,
    AOEVT_Pointer = 1,
    AOEVT_FP = 2,
  };
  unsigned ArithAllows = AOEVT_None;

  switch (Op) {
  case AtomicExpr::AO__c11_atomic_init:
  case AtomicExpr::AO__opencl_atomic_init:
    Form = Init;
    break;

  case AtomicExpr::AO__c11_atomic_load:
  case AtomicExpr::AO__opencl_atomic_load:
  case AtomicExpr::AO__hip_atomic_load:
  case AtomicExpr::AO__atomic_load_n:
  case AtomicExpr::AO__scoped_atomic_load_n:
    Form = Load;
    break;

  case AtomicExpr::AO__atomic_load:
  case AtomicExpr::AO__scoped_atomic_load:
    Form = LoadCopy;
    break;

  case AtomicExpr::AO__c11_atomic_store:
  case AtomicExpr::AO__opencl_atomic_store:
  case AtomicExpr::AO__hip_atomic_store:
  case AtomicExpr::AO__atomic_store:
  case AtomicExpr::AO__atomic_store_n:
  case AtomicExpr::AO__scoped_atomic_store:
  case AtomicExpr::AO__scoped_atomic_store_n:
    Form = Copy;
    break;
  case AtomicExpr::AO__atomic_fetch_add:
  case AtomicExpr::AO__atomic_fetch_sub:
  case AtomicExpr::AO__atomic_add_fetch:
  case AtomicExpr::AO__atomic_sub_fetch:
  case AtomicExpr::AO__scoped_atomic_fetch_add:
  case AtomicExpr::AO__scoped_atomic_fetch_sub:
  case AtomicExpr::AO__scoped_atomic_add_fetch:
  case AtomicExpr::AO__scoped_atomic_sub_fetch:
  case AtomicExpr::AO__c11_atomic_fetch_add:
  case AtomicExpr::AO__c11_atomic_fetch_sub:
  case AtomicExpr::AO__opencl_atomic_fetch_add:
  case AtomicExpr::AO__opencl_atomic_fetch_sub:
  case AtomicExpr::AO__hip_atomic_fetch_add:
  case AtomicExpr::AO__hip_atomic_fetch_sub:
    ArithAllows = AOEVT_Pointer | AOEVT_FP;
    Form = Arithmetic;
    break;
  case AtomicExpr::AO__atomic_fetch_max:
  case AtomicExpr::AO__atomic_fetch_min:
  case AtomicExpr::AO__atomic_max_fetch:
  case AtomicExpr::AO__atomic_min_fetch:
  case AtomicExpr::AO__scoped_atomic_fetch_max:
  case AtomicExpr::AO__scoped_atomic_fetch_min:
  case AtomicExpr::AO__scoped_atomic_max_fetch:
  case AtomicExpr::AO__scoped_atomic_min_fetch:
  case AtomicExpr::AO__c11_atomic_fetch_max:
  case AtomicExpr::AO__c11_atomic_fetch_min:
  case AtomicExpr::AO__opencl_atomic_fetch_max:
  case AtomicExpr::AO__opencl_atomic_fetch_min:
  case AtomicExpr::AO__hip_atomic_fetch_max:
  case AtomicExpr::AO__hip_atomic_fetch_min:
    ArithAllows = AOEVT_FP;
    Form = Arithmetic;
    break;
  case AtomicExpr::AO__c11_atomic_fetch_and:
  case AtomicExpr::AO__c11_atomic_fetch_or:
  case AtomicExpr::AO__c11_atomic_fetch_xor:
  case AtomicExpr::AO__hip_atomic_fetch_and:
  case AtomicExpr::AO__hip_atomic_fetch_or:
  case AtomicExpr::AO__hip_atomic_fetch_xor:
  case AtomicExpr::AO__c11_atomic_fetch_nand:
  case AtomicExpr::AO__opencl_atomic_fetch_and:
  case AtomicExpr::AO__opencl_atomic_fetch_or:
  case AtomicExpr::AO__opencl_atomic_fetch_xor:
  case AtomicExpr::AO__atomic_fetch_and:
  case AtomicExpr::AO__atomic_fetch_or:
  case AtomicExpr::AO__atomic_fetch_xor:
  case AtomicExpr::AO__atomic_fetch_nand:
  case AtomicExpr::AO__atomic_and_fetch:
  case AtomicExpr::AO__atomic_or_fetch:
  case AtomicExpr::AO__atomic_xor_fetch:
  case AtomicExpr::AO__atomic_nand_fetch:
  case AtomicExpr::AO__scoped_atomic_fetch_and:
  case AtomicExpr::AO__scoped_atomic_fetch_or:
  case AtomicExpr::AO__scoped_atomic_fetch_xor:
  case AtomicExpr::AO__scoped_atomic_fetch_nand:
  case AtomicExpr::AO__scoped_atomic_and_fetch:
  case AtomicExpr::AO__scoped_atomic_or_fetch:
  case AtomicExpr::AO__scoped_atomic_xor_fetch:
  case AtomicExpr::AO__scoped_atomic_nand_fetch:
    Form = Arithmetic;
    break;

  case AtomicExpr::AO__c11_atomic_exchange:
  case AtomicExpr::AO__hip_atomic_exchange:
  case AtomicExpr::AO__opencl_atomic_exchange:
  case AtomicExpr::AO__atomic_exchange_n:
  case AtomicExpr::AO__scoped_atomic_exchange_n:
    Form = Xchg;
    break;

  case AtomicExpr::AO__atomic_exchange:
  case AtomicExpr::AO__scoped_atomic_exchange:
    Form = GNUXchg;
    break;

  case AtomicExpr::AO__c11_atomic_compare_exchange_strong:
  case AtomicExpr::AO__c11_atomic_compare_exchange_weak:
  case AtomicExpr::AO__hip_atomic_compare_exchange_strong:
  case AtomicExpr::AO__opencl_atomic_compare_exchange_strong:
  case AtomicExpr::AO__opencl_atomic_compare_exchange_weak:
  case AtomicExpr::AO__hip_atomic_compare_exchange_weak:
    Form = C11CmpXchg;
    break;

  case AtomicExpr::AO__atomic_compare_exchange:
  case AtomicExpr::AO__atomic_compare_exchange_n:
  case AtomicExpr::AO__scoped_atomic_compare_exchange:
  case AtomicExpr::AO__scoped_atomic_compare_exchange_n:
    Form = GNUCmpXchg;
    break;
  }

  unsigned AdjustedNumArgs = NumArgs[Form];
  if ((IsOpenCL || IsHIP || IsScoped) &&
      Op != AtomicExpr::AO__opencl_atomic_init)
    ++AdjustedNumArgs;
  // Check we have the right number of arguments.
  if (Args.size() < AdjustedNumArgs) {
    Diag(CallRange.getEnd(), diag::err_typecheck_call_too_few_args)
        << 0 << AdjustedNumArgs << static_cast<unsigned>(Args.size())
        << /*is non object*/ 0 << ExprRange;
    return ExprError();
  } else if (Args.size() > AdjustedNumArgs) {
    Diag(Args[AdjustedNumArgs]->getBeginLoc(),
         diag::err_typecheck_call_too_many_args)
        << 0 << AdjustedNumArgs << static_cast<unsigned>(Args.size())
        << /*is non object*/ 0 << ExprRange;
    return ExprError();
  }

  // Inspect the first argument of the atomic operation.
  Expr *Ptr = Args[0];
  ExprResult ConvertedPtr = DefaultFunctionArrayLvalueConversion(Ptr);
  if (ConvertedPtr.isInvalid())
    return ExprError();

  Ptr = ConvertedPtr.get();
  const PointerType *pointerType = Ptr->getType()->getAs<PointerType>();
  if (!pointerType) {
    Diag(ExprRange.getBegin(), diag::err_atomic_builtin_must_be_pointer)
        << Ptr->getType() << 0 << Ptr->getSourceRange();
    return ExprError();
  }

  // For a __c11 builtin, this should be a pointer to an _Atomic type.
  QualType AtomTy = pointerType->getPointeeType(); // 'A'
  QualType ValType = AtomTy; // 'C'
  if (IsC11) {
    if (!AtomTy->isAtomicType()) {
      Diag(ExprRange.getBegin(), diag::err_atomic_op_needs_atomic)
          << Ptr->getType() << Ptr->getSourceRange();
      return ExprError();
    }
    if ((Form != Load && Form != LoadCopy && AtomTy.isConstQualified()) ||
        AtomTy.getAddressSpace() == LangAS::opencl_constant) {
      Diag(ExprRange.getBegin(), diag::err_atomic_op_needs_non_const_atomic)
          << (AtomTy.isConstQualified() ? 0 : 1) << Ptr->getType()
          << Ptr->getSourceRange();
      return ExprError();
    }
    ValType = AtomTy->castAs<AtomicType>()->getValueType();
  } else if (Form != Load && Form != LoadCopy) {
    if (ValType.isConstQualified()) {
      Diag(ExprRange.getBegin(), diag::err_atomic_op_needs_non_const_pointer)
          << Ptr->getType() << Ptr->getSourceRange();
      return ExprError();
    }
  }

  // Pointer to object of size zero is not allowed.
  if (RequireCompleteType(Ptr->getBeginLoc(), AtomTy,
                          diag::err_incomplete_type))
    return ExprError();
  if (Context.getTypeInfoInChars(AtomTy).Width.isZero()) {
    Diag(ExprRange.getBegin(), diag::err_atomic_builtin_must_be_pointer)
        << Ptr->getType() << 1 << Ptr->getSourceRange();
    return ExprError();
  }

  // For an arithmetic operation, the implied arithmetic must be well-formed.
  if (Form == Arithmetic) {
    // GCC does not enforce these rules for GNU atomics, but we do to help catch
    // trivial type errors.
    auto IsAllowedValueType = [&](QualType ValType,
                                  unsigned AllowedType) -> bool {
      if (ValType->isIntegerType())
        return true;
      if (ValType->isPointerType())
        return AllowedType & AOEVT_Pointer;
      if (!(ValType->isFloatingType() && (AllowedType & AOEVT_FP)))
        return false;
      // LLVM Parser does not allow atomicrmw with x86_fp80 type.
      if (ValType->isSpecificBuiltinType(BuiltinType::LongDouble) &&
          &Context.getTargetInfo().getLongDoubleFormat() ==
              &llvm::APFloat::x87DoubleExtended())
        return false;
      return true;
    };
    if (!IsAllowedValueType(ValType, ArithAllows)) {
      auto DID = ArithAllows & AOEVT_FP
                     ? (ArithAllows & AOEVT_Pointer
                            ? diag::err_atomic_op_needs_atomic_int_ptr_or_fp
                            : diag::err_atomic_op_needs_atomic_int_or_fp)
                     : diag::err_atomic_op_needs_atomic_int;
      Diag(ExprRange.getBegin(), DID)
          << IsC11 << Ptr->getType() << Ptr->getSourceRange();
      return ExprError();
    }
    if (IsC11 && ValType->isPointerType() &&
        RequireCompleteType(Ptr->getBeginLoc(), ValType->getPointeeType(),
                            diag::err_incomplete_type)) {
      return ExprError();
    }
  } else if (IsN && !ValType->isIntegerType() && !ValType->isPointerType()) {
    // For __atomic_*_n operations, the value type must be a scalar integral or
    // pointer type which is 1, 2, 4, 8 or 16 bytes in length.
    Diag(ExprRange.getBegin(), diag::err_atomic_op_needs_atomic_int_or_ptr)
        << IsC11 << Ptr->getType() << Ptr->getSourceRange();
    return ExprError();
  }

  if (!IsC11 && !AtomTy.isTriviallyCopyableType(Context) &&
      !AtomTy->isScalarType()) {
    // For GNU atomics, require a trivially-copyable type. This is not part of
    // the GNU atomics specification but we enforce it for consistency with
    // other atomics which generally all require a trivially-copyable type. This
    // is because atomics just copy bits.
    Diag(ExprRange.getBegin(), diag::err_atomic_op_needs_trivial_copy)
        << Ptr->getType() << Ptr->getSourceRange();
    return ExprError();
  }

  switch (ValType.getObjCLifetime()) {
  case Qualifiers::OCL_None:
  case Qualifiers::OCL_ExplicitNone:
    // okay
    break;

  case Qualifiers::OCL_Weak:
  case Qualifiers::OCL_Strong:
  case Qualifiers::OCL_Autoreleasing:
    // FIXME: Can this happen? By this point, ValType should be known
    // to be trivially copyable.
    Diag(ExprRange.getBegin(), diag::err_arc_atomic_ownership)
        << ValType << Ptr->getSourceRange();
    return ExprError();
  }

  // All atomic operations have an overload which takes a pointer to a volatile
  // 'A'.  We shouldn't let the volatile-ness of the pointee-type inject itself
  // into the result or the other operands. Similarly atomic_load takes a
  // pointer to a const 'A'.
  ValType.removeLocalVolatile();
  ValType.removeLocalConst();
  QualType ResultType = ValType;
  if (Form == Copy || Form == LoadCopy || Form == GNUXchg ||
      Form == Init)
    ResultType = Context.VoidTy;
  else if (Form == C11CmpXchg || Form == GNUCmpXchg)
    ResultType = Context.BoolTy;

  // The type of a parameter passed 'by value'. In the GNU atomics, such
  // arguments are actually passed as pointers.
  QualType ByValType = ValType; // 'CP'
  bool IsPassedByAddress = false;
  if (!IsC11 && !IsHIP && !IsN) {
    ByValType = Ptr->getType();
    IsPassedByAddress = true;
  }

  SmallVector<Expr *, 5> APIOrderedArgs;
  if (ArgOrder == Sema::AtomicArgumentOrder::AST) {
    APIOrderedArgs.push_back(Args[0]);
    switch (Form) {
    case Init:
    case Load:
      APIOrderedArgs.push_back(Args[1]); // Val1/Order
      break;
    case LoadCopy:
    case Copy:
    case Arithmetic:
    case Xchg:
      APIOrderedArgs.push_back(Args[2]); // Val1
      APIOrderedArgs.push_back(Args[1]); // Order
      break;
    case GNUXchg:
      APIOrderedArgs.push_back(Args[2]); // Val1
      APIOrderedArgs.push_back(Args[3]); // Val2
      APIOrderedArgs.push_back(Args[1]); // Order
      break;
    case C11CmpXchg:
      APIOrderedArgs.push_back(Args[2]); // Val1
      APIOrderedArgs.push_back(Args[4]); // Val2
      APIOrderedArgs.push_back(Args[1]); // Order
      APIOrderedArgs.push_back(Args[3]); // OrderFail
      break;
    case GNUCmpXchg:
      APIOrderedArgs.push_back(Args[2]); // Val1
      APIOrderedArgs.push_back(Args[4]); // Val2
      APIOrderedArgs.push_back(Args[5]); // Weak
      APIOrderedArgs.push_back(Args[1]); // Order
      APIOrderedArgs.push_back(Args[3]); // OrderFail
      break;
    }
  } else
    APIOrderedArgs.append(Args.begin(), Args.end());

  // The first argument's non-CV pointer type is used to deduce the type of
  // subsequent arguments, except for:
  //  - weak flag (always converted to bool)
  //  - memory order (always converted to int)
  //  - scope  (always converted to int)
  for (unsigned i = 0; i != APIOrderedArgs.size(); ++i) {
    QualType Ty;
    if (i < NumVals[Form] + 1) {
      switch (i) {
      case 0:
        // The first argument is always a pointer. It has a fixed type.
        // It is always dereferenced, a nullptr is undefined.
        CheckNonNullArgument(*this, APIOrderedArgs[i], ExprRange.getBegin());
        // Nothing else to do: we already know all we want about this pointer.
        continue;
      case 1:
        // The second argument is the non-atomic operand. For arithmetic, this
        // is always passed by value, and for a compare_exchange it is always
        // passed by address. For the rest, GNU uses by-address and C11 uses
        // by-value.
        assert(Form != Load);
        if (Form == Arithmetic && ValType->isPointerType())
          Ty = Context.getPointerDiffType();
        else if (Form == Init || Form == Arithmetic)
          Ty = ValType;
        else if (Form == Copy || Form == Xchg) {
          if (IsPassedByAddress) {
            // The value pointer is always dereferenced, a nullptr is undefined.
            CheckNonNullArgument(*this, APIOrderedArgs[i],
                                 ExprRange.getBegin());
          }
          Ty = ByValType;
        } else {
          Expr *ValArg = APIOrderedArgs[i];
          // The value pointer is always dereferenced, a nullptr is undefined.
          CheckNonNullArgument(*this, ValArg, ExprRange.getBegin());
          LangAS AS = LangAS::Default;
          // Keep address space of non-atomic pointer type.
          if (const PointerType *PtrTy =
                  ValArg->getType()->getAs<PointerType>()) {
            AS = PtrTy->getPointeeType().getAddressSpace();
          }
          Ty = Context.getPointerType(
              Context.getAddrSpaceQualType(ValType.getUnqualifiedType(), AS));
        }
        break;
      case 2:
        // The third argument to compare_exchange / GNU exchange is the desired
        // value, either by-value (for the C11 and *_n variant) or as a pointer.
        if (IsPassedByAddress)
          CheckNonNullArgument(*this, APIOrderedArgs[i], ExprRange.getBegin());
        Ty = ByValType;
        break;
      case 3:
        // The fourth argument to GNU compare_exchange is a 'weak' flag.
        Ty = Context.BoolTy;
        break;
      }
    } else {
      // The order(s) and scope are always converted to int.
      Ty = Context.IntTy;
    }

    InitializedEntity Entity =
        InitializedEntity::InitializeParameter(Context, Ty, false);
    ExprResult Arg = APIOrderedArgs[i];
    Arg = PerformCopyInitialization(Entity, SourceLocation(), Arg);
    if (Arg.isInvalid())
      return true;
    APIOrderedArgs[i] = Arg.get();
  }

  // Permute the arguments into a 'consistent' order.
  SmallVector<Expr*, 5> SubExprs;
  SubExprs.push_back(Ptr);
  switch (Form) {
  case Init:
    // Note, AtomicExpr::getVal1() has a special case for this atomic.
    SubExprs.push_back(APIOrderedArgs[1]); // Val1
    break;
  case Load:
    SubExprs.push_back(APIOrderedArgs[1]); // Order
    break;
  case LoadCopy:
  case Copy:
  case Arithmetic:
  case Xchg:
    SubExprs.push_back(APIOrderedArgs[2]); // Order
    SubExprs.push_back(APIOrderedArgs[1]); // Val1
    break;
  case GNUXchg:
    // Note, AtomicExpr::getVal2() has a special case for this atomic.
    SubExprs.push_back(APIOrderedArgs[3]); // Order
    SubExprs.push_back(APIOrderedArgs[1]); // Val1
    SubExprs.push_back(APIOrderedArgs[2]); // Val2
    break;
  case C11CmpXchg:
    SubExprs.push_back(APIOrderedArgs[3]); // Order
    SubExprs.push_back(APIOrderedArgs[1]); // Val1
    SubExprs.push_back(APIOrderedArgs[4]); // OrderFail
    SubExprs.push_back(APIOrderedArgs[2]); // Val2
    break;
  case GNUCmpXchg:
    SubExprs.push_back(APIOrderedArgs[4]); // Order
    SubExprs.push_back(APIOrderedArgs[1]); // Val1
    SubExprs.push_back(APIOrderedArgs[5]); // OrderFail
    SubExprs.push_back(APIOrderedArgs[2]); // Val2
    SubExprs.push_back(APIOrderedArgs[3]); // Weak
    break;
  }

  // If the memory orders are constants, check they are valid.
  if (SubExprs.size() >= 2 && Form != Init) {
    std::optional<llvm::APSInt> Success =
        SubExprs[1]->getIntegerConstantExpr(Context);
    if (Success && !isValidOrderingForOp(Success->getSExtValue(), Op)) {
      Diag(SubExprs[1]->getBeginLoc(),
           diag::warn_atomic_op_has_invalid_memory_order)
          << /*success=*/(Form == C11CmpXchg || Form == GNUCmpXchg)
          << SubExprs[1]->getSourceRange();
    }
    if (SubExprs.size() >= 5) {
      if (std::optional<llvm::APSInt> Failure =
              SubExprs[3]->getIntegerConstantExpr(Context)) {
        if (!llvm::is_contained(
                {llvm::AtomicOrderingCABI::relaxed,
                 llvm::AtomicOrderingCABI::consume,
                 llvm::AtomicOrderingCABI::acquire,
                 llvm::AtomicOrderingCABI::seq_cst},
                (llvm::AtomicOrderingCABI)Failure->getSExtValue())) {
          Diag(SubExprs[3]->getBeginLoc(),
               diag::warn_atomic_op_has_invalid_memory_order)
              << /*failure=*/2 << SubExprs[3]->getSourceRange();
        }
      }
    }
  }

  if (auto ScopeModel = AtomicExpr::getScopeModel(Op)) {
    auto *Scope = Args[Args.size() - 1];
    if (std::optional<llvm::APSInt> Result =
            Scope->getIntegerConstantExpr(Context)) {
      if (!ScopeModel->isValid(Result->getZExtValue()))
        Diag(Scope->getBeginLoc(), diag::err_atomic_op_has_invalid_synch_scope)
            << Scope->getSourceRange();
    }
    SubExprs.push_back(Scope);
  }

  AtomicExpr *AE = new (Context)
      AtomicExpr(ExprRange.getBegin(), SubExprs, ResultType, Op, RParenLoc);

  if ((Op == AtomicExpr::AO__c11_atomic_load ||
       Op == AtomicExpr::AO__c11_atomic_store ||
       Op == AtomicExpr::AO__opencl_atomic_load ||
       Op == AtomicExpr::AO__hip_atomic_load ||
       Op == AtomicExpr::AO__opencl_atomic_store ||
       Op == AtomicExpr::AO__hip_atomic_store) &&
      Context.AtomicUsesUnsupportedLibcall(AE))
    Diag(AE->getBeginLoc(), diag::err_atomic_load_store_uses_lib)
        << ((Op == AtomicExpr::AO__c11_atomic_load ||
             Op == AtomicExpr::AO__opencl_atomic_load ||
             Op == AtomicExpr::AO__hip_atomic_load)
                ? 0
                : 1);

  if (ValType->isBitIntType()) {
    Diag(Ptr->getExprLoc(), diag::err_atomic_builtin_bit_int_prohibit);
    return ExprError();
  }

  return AE;
}

/// checkBuiltinArgument - Given a call to a builtin function, perform
/// normal type-checking on the given argument, updating the call in
/// place.  This is useful when a builtin function requires custom
/// type-checking for some of its arguments but not necessarily all of
/// them.
///
/// Returns true on error.
static bool checkBuiltinArgument(Sema &S, CallExpr *E, unsigned ArgIndex) {
  FunctionDecl *Fn = E->getDirectCallee();
  assert(Fn && "builtin call without direct callee!");

  ParmVarDecl *Param = Fn->getParamDecl(ArgIndex);
  InitializedEntity Entity =
    InitializedEntity::InitializeParameter(S.Context, Param);

  ExprResult Arg = E->getArg(ArgIndex);
  Arg = S.PerformCopyInitialization(Entity, SourceLocation(), Arg);
  if (Arg.isInvalid())
    return true;

  E->setArg(ArgIndex, Arg.get());
  return false;
}

ExprResult Sema::BuiltinAtomicOverloaded(ExprResult TheCallResult) {
  CallExpr *TheCall = static_cast<CallExpr *>(TheCallResult.get());
  Expr *Callee = TheCall->getCallee();
  DeclRefExpr *DRE = cast<DeclRefExpr>(Callee->IgnoreParenCasts());
  FunctionDecl *FDecl = cast<FunctionDecl>(DRE->getDecl());

  // Ensure that we have at least one argument to do type inference from.
  if (TheCall->getNumArgs() < 1) {
    Diag(TheCall->getEndLoc(), diag::err_typecheck_call_too_few_args_at_least)
        << 0 << 1 << TheCall->getNumArgs() << /*is non object*/ 0
        << Callee->getSourceRange();
    return ExprError();
  }

  // Inspect the first argument of the atomic builtin.  This should always be
  // a pointer type, whose element is an integral scalar or pointer type.
  // Because it is a pointer type, we don't have to worry about any implicit
  // casts here.
  // FIXME: We don't allow floating point scalars as input.
  Expr *FirstArg = TheCall->getArg(0);
  ExprResult FirstArgResult = DefaultFunctionArrayLvalueConversion(FirstArg);
  if (FirstArgResult.isInvalid())
    return ExprError();
  FirstArg = FirstArgResult.get();
  TheCall->setArg(0, FirstArg);

  const PointerType *pointerType = FirstArg->getType()->getAs<PointerType>();
  if (!pointerType) {
    Diag(DRE->getBeginLoc(), diag::err_atomic_builtin_must_be_pointer)
        << FirstArg->getType() << 0 << FirstArg->getSourceRange();
    return ExprError();
  }

  QualType ValType = pointerType->getPointeeType();
  if (!ValType->isIntegerType() && !ValType->isAnyPointerType() &&
      !ValType->isBlockPointerType()) {
    Diag(DRE->getBeginLoc(), diag::err_atomic_builtin_must_be_pointer_intptr)
        << FirstArg->getType() << 0 << FirstArg->getSourceRange();
    return ExprError();
  }

  if (ValType.isConstQualified()) {
    Diag(DRE->getBeginLoc(), diag::err_atomic_builtin_cannot_be_const)
        << FirstArg->getType() << FirstArg->getSourceRange();
    return ExprError();
  }

  switch (ValType.getObjCLifetime()) {
  case Qualifiers::OCL_None:
  case Qualifiers::OCL_ExplicitNone:
    // okay
    break;

  case Qualifiers::OCL_Weak:
  case Qualifiers::OCL_Strong:
  case Qualifiers::OCL_Autoreleasing:
    Diag(DRE->getBeginLoc(), diag::err_arc_atomic_ownership)
        << ValType << FirstArg->getSourceRange();
    return ExprError();
  }

  // Strip any qualifiers off ValType.
  ValType = ValType.getUnqualifiedType();

  // The majority of builtins return a value, but a few have special return
  // types, so allow them to override appropriately below.
  QualType ResultType = ValType;

  // We need to figure out which concrete builtin this maps onto.  For example,
  // __sync_fetch_and_add with a 2 byte object turns into
  // __sync_fetch_and_add_2.
#define BUILTIN_ROW(x) \
  { Builtin::BI##x##_1, Builtin::BI##x##_2, Builtin::BI##x##_4, \
    Builtin::BI##x##_8, Builtin::BI##x##_16 }

  static const unsigned BuiltinIndices[][5] = {
    BUILTIN_ROW(__sync_fetch_and_add),
    BUILTIN_ROW(__sync_fetch_and_sub),
    BUILTIN_ROW(__sync_fetch_and_or),
    BUILTIN_ROW(__sync_fetch_and_and),
    BUILTIN_ROW(__sync_fetch_and_xor),
    BUILTIN_ROW(__sync_fetch_and_nand),

    BUILTIN_ROW(__sync_add_and_fetch),
    BUILTIN_ROW(__sync_sub_and_fetch),
    BUILTIN_ROW(__sync_and_and_fetch),
    BUILTIN_ROW(__sync_or_and_fetch),
    BUILTIN_ROW(__sync_xor_and_fetch),
    BUILTIN_ROW(__sync_nand_and_fetch),

    BUILTIN_ROW(__sync_val_compare_and_swap),
    BUILTIN_ROW(__sync_bool_compare_and_swap),
    BUILTIN_ROW(__sync_lock_test_and_set),
    BUILTIN_ROW(__sync_lock_release),
    BUILTIN_ROW(__sync_swap)
  };
#undef BUILTIN_ROW

  // Determine the index of the size.
  unsigned SizeIndex;
  switch (Context.getTypeSizeInChars(ValType).getQuantity()) {
  case 1: SizeIndex = 0; break;
  case 2: SizeIndex = 1; break;
  case 4: SizeIndex = 2; break;
  case 8: SizeIndex = 3; break;
  case 16: SizeIndex = 4; break;
  default:
    Diag(DRE->getBeginLoc(), diag::err_atomic_builtin_pointer_size)
        << FirstArg->getType() << FirstArg->getSourceRange();
    return ExprError();
  }

  // Each of these builtins has one pointer argument, followed by some number of
  // values (0, 1 or 2) followed by a potentially empty varags list of stuff
  // that we ignore.  Find out which row of BuiltinIndices to read from as well
  // as the number of fixed args.
  unsigned BuiltinID = FDecl->getBuiltinID();
  unsigned BuiltinIndex, NumFixed = 1;
  bool WarnAboutSemanticsChange = false;
  switch (BuiltinID) {
  default: llvm_unreachable("Unknown overloaded atomic builtin!");
  case Builtin::BI__sync_fetch_and_add:
  case Builtin::BI__sync_fetch_and_add_1:
  case Builtin::BI__sync_fetch_and_add_2:
  case Builtin::BI__sync_fetch_and_add_4:
  case Builtin::BI__sync_fetch_and_add_8:
  case Builtin::BI__sync_fetch_and_add_16:
    BuiltinIndex = 0;
    break;

  case Builtin::BI__sync_fetch_and_sub:
  case Builtin::BI__sync_fetch_and_sub_1:
  case Builtin::BI__sync_fetch_and_sub_2:
  case Builtin::BI__sync_fetch_and_sub_4:
  case Builtin::BI__sync_fetch_and_sub_8:
  case Builtin::BI__sync_fetch_and_sub_16:
    BuiltinIndex = 1;
    break;

  case Builtin::BI__sync_fetch_and_or:
  case Builtin::BI__sync_fetch_and_or_1:
  case Builtin::BI__sync_fetch_and_or_2:
  case Builtin::BI__sync_fetch_and_or_4:
  case Builtin::BI__sync_fetch_and_or_8:
  case Builtin::BI__sync_fetch_and_or_16:
    BuiltinIndex = 2;
    break;

  case Builtin::BI__sync_fetch_and_and:
  case Builtin::BI__sync_fetch_and_and_1:
  case Builtin::BI__sync_fetch_and_and_2:
  case Builtin::BI__sync_fetch_and_and_4:
  case Builtin::BI__sync_fetch_and_and_8:
  case Builtin::BI__sync_fetch_and_and_16:
    BuiltinIndex = 3;
    break;

  case Builtin::BI__sync_fetch_and_xor:
  case Builtin::BI__sync_fetch_and_xor_1:
  case Builtin::BI__sync_fetch_and_xor_2:
  case Builtin::BI__sync_fetch_and_xor_4:
  case Builtin::BI__sync_fetch_and_xor_8:
  case Builtin::BI__sync_fetch_and_xor_16:
    BuiltinIndex = 4;
    break;

  case Builtin::BI__sync_fetch_and_nand:
  case Builtin::BI__sync_fetch_and_nand_1:
  case Builtin::BI__sync_fetch_and_nand_2:
  case Builtin::BI__sync_fetch_and_nand_4:
  case Builtin::BI__sync_fetch_and_nand_8:
  case Builtin::BI__sync_fetch_and_nand_16:
    BuiltinIndex = 5;
    WarnAboutSemanticsChange = true;
    break;

  case Builtin::BI__sync_add_and_fetch:
  case Builtin::BI__sync_add_and_fetch_1:
  case Builtin::BI__sync_add_and_fetch_2:
  case Builtin::BI__sync_add_and_fetch_4:
  case Builtin::BI__sync_add_and_fetch_8:
  case Builtin::BI__sync_add_and_fetch_16:
    BuiltinIndex = 6;
    break;

  case Builtin::BI__sync_sub_and_fetch:
  case Builtin::BI__sync_sub_and_fetch_1:
  case Builtin::BI__sync_sub_and_fetch_2:
  case Builtin::BI__sync_sub_and_fetch_4:
  case Builtin::BI__sync_sub_and_fetch_8:
  case Builtin::BI__sync_sub_and_fetch_16:
    BuiltinIndex = 7;
    break;

  case Builtin::BI__sync_and_and_fetch:
  case Builtin::BI__sync_and_and_fetch_1:
  case Builtin::BI__sync_and_and_fetch_2:
  case Builtin::BI__sync_and_and_fetch_4:
  case Builtin::BI__sync_and_and_fetch_8:
  case Builtin::BI__sync_and_and_fetch_16:
    BuiltinIndex = 8;
    break;

  case Builtin::BI__sync_or_and_fetch:
  case Builtin::BI__sync_or_and_fetch_1:
  case Builtin::BI__sync_or_and_fetch_2:
  case Builtin::BI__sync_or_and_fetch_4:
  case Builtin::BI__sync_or_and_fetch_8:
  case Builtin::BI__sync_or_and_fetch_16:
    BuiltinIndex = 9;
    break;

  case Builtin::BI__sync_xor_and_fetch:
  case Builtin::BI__sync_xor_and_fetch_1:
  case Builtin::BI__sync_xor_and_fetch_2:
  case Builtin::BI__sync_xor_and_fetch_4:
  case Builtin::BI__sync_xor_and_fetch_8:
  case Builtin::BI__sync_xor_and_fetch_16:
    BuiltinIndex = 10;
    break;

  case Builtin::BI__sync_nand_and_fetch:
  case Builtin::BI__sync_nand_and_fetch_1:
  case Builtin::BI__sync_nand_and_fetch_2:
  case Builtin::BI__sync_nand_and_fetch_4:
  case Builtin::BI__sync_nand_and_fetch_8:
  case Builtin::BI__sync_nand_and_fetch_16:
    BuiltinIndex = 11;
    WarnAboutSemanticsChange = true;
    break;

  case Builtin::BI__sync_val_compare_and_swap:
  case Builtin::BI__sync_val_compare_and_swap_1:
  case Builtin::BI__sync_val_compare_and_swap_2:
  case Builtin::BI__sync_val_compare_and_swap_4:
  case Builtin::BI__sync_val_compare_and_swap_8:
  case Builtin::BI__sync_val_compare_and_swap_16:
    BuiltinIndex = 12;
    NumFixed = 2;
    break;

  case Builtin::BI__sync_bool_compare_and_swap:
  case Builtin::BI__sync_bool_compare_and_swap_1:
  case Builtin::BI__sync_bool_compare_and_swap_2:
  case Builtin::BI__sync_bool_compare_and_swap_4:
  case Builtin::BI__sync_bool_compare_and_swap_8:
  case Builtin::BI__sync_bool_compare_and_swap_16:
    BuiltinIndex = 13;
    NumFixed = 2;
    ResultType = Context.BoolTy;
    break;

  case Builtin::BI__sync_lock_test_and_set:
  case Builtin::BI__sync_lock_test_and_set_1:
  case Builtin::BI__sync_lock_test_and_set_2:
  case Builtin::BI__sync_lock_test_and_set_4:
  case Builtin::BI__sync_lock_test_and_set_8:
  case Builtin::BI__sync_lock_test_and_set_16:
    BuiltinIndex = 14;
    break;

  case Builtin::BI__sync_lock_release:
  case Builtin::BI__sync_lock_release_1:
  case Builtin::BI__sync_lock_release_2:
  case Builtin::BI__sync_lock_release_4:
  case Builtin::BI__sync_lock_release_8:
  case Builtin::BI__sync_lock_release_16:
    BuiltinIndex = 15;
    NumFixed = 0;
    ResultType = Context.VoidTy;
    break;

  case Builtin::BI__sync_swap:
  case Builtin::BI__sync_swap_1:
  case Builtin::BI__sync_swap_2:
  case Builtin::BI__sync_swap_4:
  case Builtin::BI__sync_swap_8:
  case Builtin::BI__sync_swap_16:
    BuiltinIndex = 16;
    break;
  }

  // Now that we know how many fixed arguments we expect, first check that we
  // have at least that many.
  if (TheCall->getNumArgs() < 1+NumFixed) {
    Diag(TheCall->getEndLoc(), diag::err_typecheck_call_too_few_args_at_least)
        << 0 << 1 + NumFixed << TheCall->getNumArgs() << /*is non object*/ 0
        << Callee->getSourceRange();
    return ExprError();
  }

  Diag(TheCall->getEndLoc(), diag::warn_atomic_implicit_seq_cst)
      << Callee->getSourceRange();

  if (WarnAboutSemanticsChange) {
    Diag(TheCall->getEndLoc(), diag::warn_sync_fetch_and_nand_semantics_change)
        << Callee->getSourceRange();
  }

  // Get the decl for the concrete builtin from this, we can tell what the
  // concrete integer type we should convert to is.
  unsigned NewBuiltinID = BuiltinIndices[BuiltinIndex][SizeIndex];
  StringRef NewBuiltinName = Context.BuiltinInfo.getName(NewBuiltinID);
  FunctionDecl *NewBuiltinDecl;
  if (NewBuiltinID == BuiltinID)
    NewBuiltinDecl = FDecl;
  else {
    // Perform builtin lookup to avoid redeclaring it.
    DeclarationName DN(&Context.Idents.get(NewBuiltinName));
    LookupResult Res(*this, DN, DRE->getBeginLoc(), LookupOrdinaryName);
    LookupName(Res, TUScope, /*AllowBuiltinCreation=*/true);
    assert(Res.getFoundDecl());
    NewBuiltinDecl = dyn_cast<FunctionDecl>(Res.getFoundDecl());
    if (!NewBuiltinDecl)
      return ExprError();
  }

  // The first argument --- the pointer --- has a fixed type; we
  // deduce the types of the rest of the arguments accordingly.  Walk
  // the remaining arguments, converting them to the deduced value type.
  for (unsigned i = 0; i != NumFixed; ++i) {
    ExprResult Arg = TheCall->getArg(i+1);

    // GCC does an implicit conversion to the pointer or integer ValType.  This
    // can fail in some cases (1i -> int**), check for this error case now.
    // Initialize the argument.
    InitializedEntity Entity = InitializedEntity::InitializeParameter(Context,
                                                   ValType, /*consume*/ false);
    Arg = PerformCopyInitialization(Entity, SourceLocation(), Arg);
    if (Arg.isInvalid())
      return ExprError();

    // Okay, we have something that *can* be converted to the right type.  Check
    // to see if there is a potentially weird extension going on here.  This can
    // happen when you do an atomic operation on something like an char* and
    // pass in 42.  The 42 gets converted to char.  This is even more strange
    // for things like 45.123 -> char, etc.
    // FIXME: Do this check.
    TheCall->setArg(i+1, Arg.get());
  }

  // Create a new DeclRefExpr to refer to the new decl.
  DeclRefExpr *NewDRE = DeclRefExpr::Create(
      Context, DRE->getQualifierLoc(), SourceLocation(), NewBuiltinDecl,
      /*enclosing*/ false, DRE->getLocation(), Context.BuiltinFnTy,
      DRE->getValueKind(), nullptr, nullptr, DRE->isNonOdrUse());

  // Set the callee in the CallExpr.
  // FIXME: This loses syntactic information.
  QualType CalleePtrTy = Context.getPointerType(NewBuiltinDecl->getType());
  ExprResult PromotedCall = ImpCastExprToType(NewDRE, CalleePtrTy,
                                              CK_BuiltinFnToFnPtr);
  TheCall->setCallee(PromotedCall.get());

  // Change the result type of the call to match the original value type. This
  // is arbitrary, but the codegen for these builtins ins design to handle it
  // gracefully.
  TheCall->setType(ResultType);

  // Prohibit problematic uses of bit-precise integer types with atomic
  // builtins. The arguments would have already been converted to the first
  // argument's type, so only need to check the first argument.
  const auto *BitIntValType = ValType->getAs<BitIntType>();
  if (BitIntValType && !llvm::isPowerOf2_64(BitIntValType->getNumBits())) {
    Diag(FirstArg->getExprLoc(), diag::err_atomic_builtin_ext_int_size);
    return ExprError();
  }

  return TheCallResult;
}

ExprResult Sema::BuiltinNontemporalOverloaded(ExprResult TheCallResult) {
  CallExpr *TheCall = (CallExpr *)TheCallResult.get();
  DeclRefExpr *DRE =
      cast<DeclRefExpr>(TheCall->getCallee()->IgnoreParenCasts());
  FunctionDecl *FDecl = cast<FunctionDecl>(DRE->getDecl());
  unsigned BuiltinID = FDecl->getBuiltinID();
  assert((BuiltinID == Builtin::BI__builtin_nontemporal_store ||
          BuiltinID == Builtin::BI__builtin_nontemporal_load) &&
         "Unexpected nontemporal load/store builtin!");
  bool isStore = BuiltinID == Builtin::BI__builtin_nontemporal_store;
  unsigned numArgs = isStore ? 2 : 1;

  // Ensure that we have the proper number of arguments.
  if (checkArgCount(TheCall, numArgs))
    return ExprError();

  // Inspect the last argument of the nontemporal builtin.  This should always
  // be a pointer type, from which we imply the type of the memory access.
  // Because it is a pointer type, we don't have to worry about any implicit
  // casts here.
  Expr *PointerArg = TheCall->getArg(numArgs - 1);
  ExprResult PointerArgResult =
      DefaultFunctionArrayLvalueConversion(PointerArg);

  if (PointerArgResult.isInvalid())
    return ExprError();
  PointerArg = PointerArgResult.get();
  TheCall->setArg(numArgs - 1, PointerArg);

  const PointerType *pointerType = PointerArg->getType()->getAs<PointerType>();
  if (!pointerType) {
    Diag(DRE->getBeginLoc(), diag::err_nontemporal_builtin_must_be_pointer)
        << PointerArg->getType() << PointerArg->getSourceRange();
    return ExprError();
  }

  QualType ValType = pointerType->getPointeeType();

  // Strip any qualifiers off ValType.
  ValType = ValType.getUnqualifiedType();
  if (!ValType->isIntegerType() && !ValType->isAnyPointerType() &&
      !ValType->isBlockPointerType() && !ValType->isFloatingType() &&
      !ValType->isVectorType()) {
    Diag(DRE->getBeginLoc(),
         diag::err_nontemporal_builtin_must_be_pointer_intfltptr_or_vector)
        << PointerArg->getType() << PointerArg->getSourceRange();
    return ExprError();
  }

  if (!isStore) {
    TheCall->setType(ValType);
    return TheCallResult;
  }

  ExprResult ValArg = TheCall->getArg(0);
  InitializedEntity Entity = InitializedEntity::InitializeParameter(
      Context, ValType, /*consume*/ false);
  ValArg = PerformCopyInitialization(Entity, SourceLocation(), ValArg);
  if (ValArg.isInvalid())
    return ExprError();

  TheCall->setArg(0, ValArg.get());
  TheCall->setType(Context.VoidTy);
  return TheCallResult;
}

/// CheckObjCString - Checks that the format string argument to the os_log()
/// and os_trace() functions is correct, and converts it to const char *.
ExprResult Sema::CheckOSLogFormatStringArg(Expr *Arg) {
  Arg = Arg->IgnoreParenCasts();
  auto *Literal = dyn_cast<StringLiteral>(Arg);
  if (!Literal) {
    if (auto *ObjcLiteral = dyn_cast<ObjCStringLiteral>(Arg)) {
      Literal = ObjcLiteral->getString();
    }
  }

  if (!Literal || (!Literal->isOrdinary() && !Literal->isUTF8())) {
    return ExprError(
        Diag(Arg->getBeginLoc(), diag::err_os_log_format_not_string_constant)
        << Arg->getSourceRange());
  }

  ExprResult Result(Literal);
  QualType ResultTy = Context.getPointerType(Context.CharTy.withConst());
  InitializedEntity Entity =
      InitializedEntity::InitializeParameter(Context, ResultTy, false);
  Result = PerformCopyInitialization(Entity, SourceLocation(), Result);
  return Result;
}

/// Check that the user is calling the appropriate va_start builtin for the
/// target and calling convention.
static bool checkVAStartABI(Sema &S, unsigned BuiltinID, Expr *Fn) {
  const llvm::Triple &TT = S.Context.getTargetInfo().getTriple();
  bool IsX64 = TT.getArch() == llvm::Triple::x86_64;
  bool IsAArch64 = (TT.getArch() == llvm::Triple::aarch64 ||
                    TT.getArch() == llvm::Triple::aarch64_32);
  bool IsWindows = TT.isOSWindows();
  bool IsMSVAStart = BuiltinID == Builtin::BI__builtin_ms_va_start;
  if (IsX64 || IsAArch64) {
    CallingConv CC = CC_C;
    if (const FunctionDecl *FD = S.getCurFunctionDecl())
      CC = FD->getType()->castAs<FunctionType>()->getCallConv();
    if (IsMSVAStart) {
      // Don't allow this in System V ABI functions.
      if (CC == CC_X86_64SysV || (!IsWindows && CC != CC_Win64))
        return S.Diag(Fn->getBeginLoc(),
                      diag::err_ms_va_start_used_in_sysv_function);
    } else {
      // On x86-64/AArch64 Unix, don't allow this in Win64 ABI functions.
      // On x64 Windows, don't allow this in System V ABI functions.
      // (Yes, that means there's no corresponding way to support variadic
      // System V ABI functions on Windows.)
      if ((IsWindows && CC == CC_X86_64SysV) ||
          (!IsWindows && CC == CC_Win64))
        return S.Diag(Fn->getBeginLoc(),
                      diag::err_va_start_used_in_wrong_abi_function)
               << !IsWindows;
    }
    return false;
  }

  if (IsMSVAStart)
    return S.Diag(Fn->getBeginLoc(), diag::err_builtin_x64_aarch64_only);
  return false;
}

static bool checkVAStartIsInVariadicFunction(Sema &S, Expr *Fn,
                                             ParmVarDecl **LastParam = nullptr) {
  // Determine whether the current function, block, or obj-c method is variadic
  // and get its parameter list.
  bool IsVariadic = false;
  ArrayRef<ParmVarDecl *> Params;
  DeclContext *Caller = S.CurContext;
  if (auto *Block = dyn_cast<BlockDecl>(Caller)) {
    IsVariadic = Block->isVariadic();
    Params = Block->parameters();
  } else if (auto *FD = dyn_cast<FunctionDecl>(Caller)) {
    IsVariadic = FD->isVariadic();
    Params = FD->parameters();
  } else if (auto *MD = dyn_cast<ObjCMethodDecl>(Caller)) {
    IsVariadic = MD->isVariadic();
    // FIXME: This isn't correct for methods (results in bogus warning).
    Params = MD->parameters();
  } else if (isa<CapturedDecl>(Caller)) {
    // We don't support va_start in a CapturedDecl.
    S.Diag(Fn->getBeginLoc(), diag::err_va_start_captured_stmt);
    return true;
  } else {
    // This must be some other declcontext that parses exprs.
    S.Diag(Fn->getBeginLoc(), diag::err_va_start_outside_function);
    return true;
  }

  if (!IsVariadic) {
    S.Diag(Fn->getBeginLoc(), diag::err_va_start_fixed_function);
    return true;
  }

  if (LastParam)
    *LastParam = Params.empty() ? nullptr : Params.back();

  return false;
}

bool Sema::BuiltinVAStart(unsigned BuiltinID, CallExpr *TheCall) {
  Expr *Fn = TheCall->getCallee();

  if (checkVAStartABI(*this, BuiltinID, Fn))
    return true;

  // In C23 mode, va_start only needs one argument. However, the builtin still
  // requires two arguments (which matches the behavior of the GCC builtin),
  // <stdarg.h> passes `0` as the second argument in C23 mode.
  if (checkArgCount(TheCall, 2))
    return true;

  // Type-check the first argument normally.
  if (checkBuiltinArgument(*this, TheCall, 0))
    return true;

  // Check that the current function is variadic, and get its last parameter.
  ParmVarDecl *LastParam;
  if (checkVAStartIsInVariadicFunction(*this, Fn, &LastParam))
    return true;

  // Verify that the second argument to the builtin is the last argument of the
  // current function or method. In C23 mode, if the second argument is an
  // integer constant expression with value 0, then we don't bother with this
  // check.
  bool SecondArgIsLastNamedArgument = false;
  const Expr *Arg = TheCall->getArg(1)->IgnoreParenCasts();
  if (std::optional<llvm::APSInt> Val =
          TheCall->getArg(1)->getIntegerConstantExpr(Context);
      Val && LangOpts.C23 && *Val == 0)
    return false;

  // These are valid if SecondArgIsLastNamedArgument is false after the next
  // block.
  QualType Type;
  SourceLocation ParamLoc;
  bool IsCRegister = false;

  if (const DeclRefExpr *DR = dyn_cast<DeclRefExpr>(Arg)) {
    if (const ParmVarDecl *PV = dyn_cast<ParmVarDecl>(DR->getDecl())) {
      SecondArgIsLastNamedArgument = PV == LastParam;

      Type = PV->getType();
      ParamLoc = PV->getLocation();
      IsCRegister =
          PV->getStorageClass() == SC_Register && !getLangOpts().CPlusPlus;
    }
  }

  if (!SecondArgIsLastNamedArgument)
    Diag(TheCall->getArg(1)->getBeginLoc(),
         diag::warn_second_arg_of_va_start_not_last_named_param);
  else if (IsCRegister || Type->isReferenceType() ||
           Type->isSpecificBuiltinType(BuiltinType::Float) || [=] {
             // Promotable integers are UB, but enumerations need a bit of
             // extra checking to see what their promotable type actually is.
             if (!Context.isPromotableIntegerType(Type))
               return false;
             if (!Type->isEnumeralType())
               return true;
             const EnumDecl *ED = Type->castAs<EnumType>()->getDecl();
             return !(ED &&
                      Context.typesAreCompatible(ED->getPromotionType(), Type));
           }()) {
    unsigned Reason = 0;
    if (Type->isReferenceType())  Reason = 1;
    else if (IsCRegister)         Reason = 2;
    Diag(Arg->getBeginLoc(), diag::warn_va_start_type_is_undefined) << Reason;
    Diag(ParamLoc, diag::note_parameter_type) << Type;
  }

  return false;
}

bool Sema::BuiltinVAStartARMMicrosoft(CallExpr *Call) {
  auto IsSuitablyTypedFormatArgument = [this](const Expr *Arg) -> bool {
    const LangOptions &LO = getLangOpts();

    if (LO.CPlusPlus)
      return Arg->getType()
                 .getCanonicalType()
                 .getTypePtr()
                 ->getPointeeType()
                 .withoutLocalFastQualifiers() == Context.CharTy;

    // In C, allow aliasing through `char *`, this is required for AArch64 at
    // least.
    return true;
  };

  // void __va_start(va_list *ap, const char *named_addr, size_t slot_size,
  //                 const char *named_addr);

  Expr *Func = Call->getCallee();

  if (Call->getNumArgs() < 3)
    return Diag(Call->getEndLoc(),
                diag::err_typecheck_call_too_few_args_at_least)
           << 0 /*function call*/ << 3 << Call->getNumArgs()
           << /*is non object*/ 0;

  // Type-check the first argument normally.
  if (checkBuiltinArgument(*this, Call, 0))
    return true;

  // Check that the current function is variadic.
  if (checkVAStartIsInVariadicFunction(*this, Func))
    return true;

  // __va_start on Windows does not validate the parameter qualifiers

  const Expr *Arg1 = Call->getArg(1)->IgnoreParens();
  const Type *Arg1Ty = Arg1->getType().getCanonicalType().getTypePtr();

  const Expr *Arg2 = Call->getArg(2)->IgnoreParens();
  const Type *Arg2Ty = Arg2->getType().getCanonicalType().getTypePtr();

  const QualType &ConstCharPtrTy =
      Context.getPointerType(Context.CharTy.withConst());
  if (!Arg1Ty->isPointerType() || !IsSuitablyTypedFormatArgument(Arg1))
    Diag(Arg1->getBeginLoc(), diag::err_typecheck_convert_incompatible)
        << Arg1->getType() << ConstCharPtrTy << 1 /* different class */
        << 0                                      /* qualifier difference */
        << 3                                      /* parameter mismatch */
        << 2 << Arg1->getType() << ConstCharPtrTy;

  const QualType SizeTy = Context.getSizeType();
  if (Arg2Ty->getCanonicalTypeInternal().withoutLocalFastQualifiers() != SizeTy)
    Diag(Arg2->getBeginLoc(), diag::err_typecheck_convert_incompatible)
        << Arg2->getType() << SizeTy << 1 /* different class */
        << 0                              /* qualifier difference */
        << 3                              /* parameter mismatch */
        << 3 << Arg2->getType() << SizeTy;

  return false;
}

bool Sema::BuiltinUnorderedCompare(CallExpr *TheCall, unsigned BuiltinID) {
  if (checkArgCount(TheCall, 2))
    return true;

  if (BuiltinID == Builtin::BI__builtin_isunordered &&
      TheCall->getFPFeaturesInEffect(getLangOpts()).getNoHonorNaNs())
    Diag(TheCall->getBeginLoc(), diag::warn_fp_nan_inf_when_disabled)
        << 1 << 0 << TheCall->getSourceRange();

  ExprResult OrigArg0 = TheCall->getArg(0);
  ExprResult OrigArg1 = TheCall->getArg(1);

  // Do standard promotions between the two arguments, returning their common
  // type.
  QualType Res = UsualArithmeticConversions(
      OrigArg0, OrigArg1, TheCall->getExprLoc(), ACK_Comparison);
  if (OrigArg0.isInvalid() || OrigArg1.isInvalid())
    return true;

  // Make sure any conversions are pushed back into the call; this is
  // type safe since unordered compare builtins are declared as "_Bool
  // foo(...)".
  TheCall->setArg(0, OrigArg0.get());
  TheCall->setArg(1, OrigArg1.get());

  if (OrigArg0.get()->isTypeDependent() || OrigArg1.get()->isTypeDependent())
    return false;

  // If the common type isn't a real floating type, then the arguments were
  // invalid for this operation.
  if (Res.isNull() || !Res->isRealFloatingType())
    return Diag(OrigArg0.get()->getBeginLoc(),
                diag::err_typecheck_call_invalid_ordered_compare)
           << OrigArg0.get()->getType() << OrigArg1.get()->getType()
           << SourceRange(OrigArg0.get()->getBeginLoc(),
                          OrigArg1.get()->getEndLoc());

  return false;
}

bool Sema::BuiltinFPClassification(CallExpr *TheCall, unsigned NumArgs,
                                   unsigned BuiltinID) {
  if (checkArgCount(TheCall, NumArgs))
    return true;

  FPOptions FPO = TheCall->getFPFeaturesInEffect(getLangOpts());
  if (FPO.getNoHonorInfs() && (BuiltinID == Builtin::BI__builtin_isfinite ||
                               BuiltinID == Builtin::BI__builtin_isinf ||
                               BuiltinID == Builtin::BI__builtin_isinf_sign))
    Diag(TheCall->getBeginLoc(), diag::warn_fp_nan_inf_when_disabled)
        << 0 << 0 << TheCall->getSourceRange();

  if (FPO.getNoHonorNaNs() && (BuiltinID == Builtin::BI__builtin_isnan ||
                               BuiltinID == Builtin::BI__builtin_isunordered))
    Diag(TheCall->getBeginLoc(), diag::warn_fp_nan_inf_when_disabled)
        << 1 << 0 << TheCall->getSourceRange();

  bool IsFPClass = NumArgs == 2;

  // Find out position of floating-point argument.
  unsigned FPArgNo = IsFPClass ? 0 : NumArgs - 1;

  // We can count on all parameters preceding the floating-point just being int.
  // Try all of those.
  for (unsigned i = 0; i < FPArgNo; ++i) {
    Expr *Arg = TheCall->getArg(i);

    if (Arg->isTypeDependent())
      return false;

    ExprResult Res = PerformImplicitConversion(Arg, Context.IntTy, AA_Passing);

    if (Res.isInvalid())
      return true;
    TheCall->setArg(i, Res.get());
  }

  Expr *OrigArg = TheCall->getArg(FPArgNo);

  if (OrigArg->isTypeDependent())
    return false;

  // Usual Unary Conversions will convert half to float, which we want for
  // machines that use fp16 conversion intrinsics. Else, we wnat to leave the
  // type how it is, but do normal L->Rvalue conversions.
  if (Context.getTargetInfo().useFP16ConversionIntrinsics())
    OrigArg = UsualUnaryConversions(OrigArg).get();
  else
    OrigArg = DefaultFunctionArrayLvalueConversion(OrigArg).get();
  TheCall->setArg(FPArgNo, OrigArg);

  QualType VectorResultTy;
  QualType ElementTy = OrigArg->getType();
  // TODO: When all classification function are implemented with is_fpclass,
  // vector argument can be supported in all of them.
  if (ElementTy->isVectorType() && IsFPClass) {
    VectorResultTy = GetSignedVectorType(ElementTy);
    ElementTy = ElementTy->castAs<VectorType>()->getElementType();
  }

  // This operation requires a non-_Complex floating-point number.
  if (!ElementTy->isRealFloatingType())
    return Diag(OrigArg->getBeginLoc(),
                diag::err_typecheck_call_invalid_unary_fp)
           << OrigArg->getType() << OrigArg->getSourceRange();

  // __builtin_isfpclass has integer parameter that specify test mask. It is
  // passed in (...), so it should be analyzed completely here.
  if (IsFPClass)
    if (BuiltinConstantArgRange(TheCall, 1, 0, llvm::fcAllFlags))
      return true;

  // TODO: enable this code to all classification functions.
  if (IsFPClass) {
    QualType ResultTy;
    if (!VectorResultTy.isNull())
      ResultTy = VectorResultTy;
    else
      ResultTy = Context.IntTy;
    TheCall->setType(ResultTy);
  }

  return false;
}

bool Sema::BuiltinComplex(CallExpr *TheCall) {
  if (checkArgCount(TheCall, 2))
    return true;

  bool Dependent = false;
  for (unsigned I = 0; I != 2; ++I) {
    Expr *Arg = TheCall->getArg(I);
    QualType T = Arg->getType();
    if (T->isDependentType()) {
      Dependent = true;
      continue;
    }

    // Despite supporting _Complex int, GCC requires a real floating point type
    // for the operands of __builtin_complex.
    if (!T->isRealFloatingType()) {
      return Diag(Arg->getBeginLoc(), diag::err_typecheck_call_requires_real_fp)
             << Arg->getType() << Arg->getSourceRange();
    }

    ExprResult Converted = DefaultLvalueConversion(Arg);
    if (Converted.isInvalid())
      return true;
    TheCall->setArg(I, Converted.get());
  }

  if (Dependent) {
    TheCall->setType(Context.DependentTy);
    return false;
  }

  Expr *Real = TheCall->getArg(0);
  Expr *Imag = TheCall->getArg(1);
  if (!Context.hasSameType(Real->getType(), Imag->getType())) {
    return Diag(Real->getBeginLoc(),
                diag::err_typecheck_call_different_arg_types)
           << Real->getType() << Imag->getType()
           << Real->getSourceRange() << Imag->getSourceRange();
  }

  // We don't allow _Complex _Float16 nor _Complex __fp16 as type specifiers;
  // don't allow this builtin to form those types either.
  // FIXME: Should we allow these types?
  if (Real->getType()->isFloat16Type())
    return Diag(TheCall->getBeginLoc(), diag::err_invalid_complex_spec)
           << "_Float16";
  if (Real->getType()->isHalfType())
    return Diag(TheCall->getBeginLoc(), diag::err_invalid_complex_spec)
           << "half";

  TheCall->setType(Context.getComplexType(Real->getType()));
  return false;
}

/// BuiltinShuffleVector - Handle __builtin_shufflevector.
// This is declared to take (...), so we have to check everything.
ExprResult Sema::BuiltinShuffleVector(CallExpr *TheCall) {
  if (TheCall->getNumArgs() < 2)
    return ExprError(Diag(TheCall->getEndLoc(),
                          diag::err_typecheck_call_too_few_args_at_least)
                     << 0 /*function call*/ << 2 << TheCall->getNumArgs()
                     << /*is non object*/ 0 << TheCall->getSourceRange());

  // Determine which of the following types of shufflevector we're checking:
  // 1) unary, vector mask: (lhs, mask)
  // 2) binary, scalar mask: (lhs, rhs, index, ..., index)
  QualType resType = TheCall->getArg(0)->getType();
  unsigned numElements = 0;

  if (!TheCall->getArg(0)->isTypeDependent() &&
      !TheCall->getArg(1)->isTypeDependent()) {
    QualType LHSType = TheCall->getArg(0)->getType();
    QualType RHSType = TheCall->getArg(1)->getType();

    if (!LHSType->isVectorType() || !RHSType->isVectorType())
      return ExprError(
          Diag(TheCall->getBeginLoc(), diag::err_vec_builtin_non_vector)
          << TheCall->getDirectCallee() << /*isMorethantwoArgs*/ false
          << SourceRange(TheCall->getArg(0)->getBeginLoc(),
                         TheCall->getArg(1)->getEndLoc()));

    numElements = LHSType->castAs<VectorType>()->getNumElements();
    unsigned numResElements = TheCall->getNumArgs() - 2;

    // Check to see if we have a call with 2 vector arguments, the unary shuffle
    // with mask.  If so, verify that RHS is an integer vector type with the
    // same number of elts as lhs.
    if (TheCall->getNumArgs() == 2) {
      if (!RHSType->hasIntegerRepresentation() ||
          RHSType->castAs<VectorType>()->getNumElements() != numElements)
        return ExprError(Diag(TheCall->getBeginLoc(),
                              diag::err_vec_builtin_incompatible_vector)
                         << TheCall->getDirectCallee()
                         << /*isMorethantwoArgs*/ false
                         << SourceRange(TheCall->getArg(1)->getBeginLoc(),
                                        TheCall->getArg(1)->getEndLoc()));
    } else if (!Context.hasSameUnqualifiedType(LHSType, RHSType)) {
      return ExprError(Diag(TheCall->getBeginLoc(),
                            diag::err_vec_builtin_incompatible_vector)
                       << TheCall->getDirectCallee()
                       << /*isMorethantwoArgs*/ false
                       << SourceRange(TheCall->getArg(0)->getBeginLoc(),
                                      TheCall->getArg(1)->getEndLoc()));
    } else if (numElements != numResElements) {
      QualType eltType = LHSType->castAs<VectorType>()->getElementType();
      resType =
          Context.getVectorType(eltType, numResElements, VectorKind::Generic);
    }
  }

  for (unsigned i = 2; i < TheCall->getNumArgs(); i++) {
    if (TheCall->getArg(i)->isTypeDependent() ||
        TheCall->getArg(i)->isValueDependent())
      continue;

    std::optional<llvm::APSInt> Result;
    if (!(Result = TheCall->getArg(i)->getIntegerConstantExpr(Context)))
      return ExprError(Diag(TheCall->getBeginLoc(),
                            diag::err_shufflevector_nonconstant_argument)
                       << TheCall->getArg(i)->getSourceRange());

    // Allow -1 which will be translated to undef in the IR.
    if (Result->isSigned() && Result->isAllOnes())
      continue;

    if (Result->getActiveBits() > 64 ||
        Result->getZExtValue() >= numElements * 2)
      return ExprError(Diag(TheCall->getBeginLoc(),
                            diag::err_shufflevector_argument_too_large)
                       << TheCall->getArg(i)->getSourceRange());
  }

  SmallVector<Expr*, 32> exprs;

  for (unsigned i = 0, e = TheCall->getNumArgs(); i != e; i++) {
    exprs.push_back(TheCall->getArg(i));
    TheCall->setArg(i, nullptr);
  }

  return new (Context) ShuffleVectorExpr(Context, exprs, resType,
                                         TheCall->getCallee()->getBeginLoc(),
                                         TheCall->getRParenLoc());
}

ExprResult Sema::ConvertVectorExpr(Expr *E, TypeSourceInfo *TInfo,
                                   SourceLocation BuiltinLoc,
                                   SourceLocation RParenLoc) {
  ExprValueKind VK = VK_PRValue;
  ExprObjectKind OK = OK_Ordinary;
  QualType DstTy = TInfo->getType();
  QualType SrcTy = E->getType();

  if (!SrcTy->isVectorType() && !SrcTy->isDependentType())
    return ExprError(Diag(BuiltinLoc,
                          diag::err_convertvector_non_vector)
                     << E->getSourceRange());
  if (!DstTy->isVectorType() && !DstTy->isDependentType())
    return ExprError(Diag(BuiltinLoc, diag::err_builtin_non_vector_type)
                     << "second"
                     << "__builtin_convertvector");

  if (!SrcTy->isDependentType() && !DstTy->isDependentType()) {
    unsigned SrcElts = SrcTy->castAs<VectorType>()->getNumElements();
    unsigned DstElts = DstTy->castAs<VectorType>()->getNumElements();
    if (SrcElts != DstElts)
      return ExprError(Diag(BuiltinLoc,
                            diag::err_convertvector_incompatible_vector)
                       << E->getSourceRange());
  }

  return new (Context) class ConvertVectorExpr(E, TInfo, DstTy, VK, OK,
                                               BuiltinLoc, RParenLoc);
}

bool Sema::BuiltinPrefetch(CallExpr *TheCall) {
  unsigned NumArgs = TheCall->getNumArgs();

  if (NumArgs > 3)
    return Diag(TheCall->getEndLoc(),
                diag::err_typecheck_call_too_many_args_at_most)
           << 0 /*function call*/ << 3 << NumArgs << /*is non object*/ 0
           << TheCall->getSourceRange();

  // Argument 0 is checked for us and the remaining arguments must be
  // constant integers.
  for (unsigned i = 1; i != NumArgs; ++i)
    if (BuiltinConstantArgRange(TheCall, i, 0, i == 1 ? 1 : 3))
      return true;

  return false;
}

bool Sema::BuiltinArithmeticFence(CallExpr *TheCall) {
  if (!Context.getTargetInfo().checkArithmeticFenceSupported())
    return Diag(TheCall->getBeginLoc(), diag::err_builtin_target_unsupported)
           << SourceRange(TheCall->getBeginLoc(), TheCall->getEndLoc());
  if (checkArgCount(TheCall, 1))
    return true;
  Expr *Arg = TheCall->getArg(0);
  if (Arg->isInstantiationDependent())
    return false;

  QualType ArgTy = Arg->getType();
  if (!ArgTy->hasFloatingRepresentation())
    return Diag(TheCall->getEndLoc(), diag::err_typecheck_expect_flt_or_vector)
           << ArgTy;
  if (Arg->isLValue()) {
    ExprResult FirstArg = DefaultLvalueConversion(Arg);
    TheCall->setArg(0, FirstArg.get());
  }
  TheCall->setType(TheCall->getArg(0)->getType());
  return false;
}

bool Sema::BuiltinAssume(CallExpr *TheCall) {
  Expr *Arg = TheCall->getArg(0);
  if (Arg->isInstantiationDependent()) return false;

  if (Arg->HasSideEffects(Context))
    Diag(Arg->getBeginLoc(), diag::warn_assume_side_effects)
        << Arg->getSourceRange()
        << cast<FunctionDecl>(TheCall->getCalleeDecl())->getIdentifier();

  return false;
}

bool Sema::BuiltinAllocaWithAlign(CallExpr *TheCall) {
  // The alignment must be a constant integer.
  Expr *Arg = TheCall->getArg(1);

  // We can't check the value of a dependent argument.
  if (!Arg->isTypeDependent() && !Arg->isValueDependent()) {
    if (const auto *UE =
            dyn_cast<UnaryExprOrTypeTraitExpr>(Arg->IgnoreParenImpCasts()))
      if (UE->getKind() == UETT_AlignOf ||
          UE->getKind() == UETT_PreferredAlignOf)
        Diag(TheCall->getBeginLoc(), diag::warn_alloca_align_alignof)
            << Arg->getSourceRange();

    llvm::APSInt Result = Arg->EvaluateKnownConstInt(Context);

    if (!Result.isPowerOf2())
      return Diag(TheCall->getBeginLoc(), diag::err_alignment_not_power_of_two)
             << Arg->getSourceRange();

    if (Result < Context.getCharWidth())
      return Diag(TheCall->getBeginLoc(), diag::err_alignment_too_small)
             << (unsigned)Context.getCharWidth() << Arg->getSourceRange();

    if (Result > std::numeric_limits<int32_t>::max())
      return Diag(TheCall->getBeginLoc(), diag::err_alignment_too_big)
             << std::numeric_limits<int32_t>::max() << Arg->getSourceRange();
  }

  return false;
}

bool Sema::BuiltinAssumeAligned(CallExpr *TheCall) {
  if (checkArgCountRange(TheCall, 2, 3))
    return true;

  unsigned NumArgs = TheCall->getNumArgs();
  Expr *FirstArg = TheCall->getArg(0);

  {
    ExprResult FirstArgResult =
        DefaultFunctionArrayLvalueConversion(FirstArg);
    if (checkBuiltinArgument(*this, TheCall, 0))
      return true;
    /// In-place updation of FirstArg by checkBuiltinArgument is ignored.
    TheCall->setArg(0, FirstArgResult.get());
  }

  // The alignment must be a constant integer.
  Expr *SecondArg = TheCall->getArg(1);

  // We can't check the value of a dependent argument.
  if (!SecondArg->isValueDependent()) {
    llvm::APSInt Result;
    if (BuiltinConstantArg(TheCall, 1, Result))
      return true;

    if (!Result.isPowerOf2())
      return Diag(TheCall->getBeginLoc(), diag::err_alignment_not_power_of_two)
             << SecondArg->getSourceRange();

    if (Result > Sema::MaximumAlignment)
      Diag(TheCall->getBeginLoc(), diag::warn_assume_aligned_too_great)
          << SecondArg->getSourceRange() << Sema::MaximumAlignment;
  }

  if (NumArgs > 2) {
    Expr *ThirdArg = TheCall->getArg(2);
    if (convertArgumentToType(*this, ThirdArg, Context.getSizeType()))
      return true;
    TheCall->setArg(2, ThirdArg);
  }

  return false;
}

bool Sema::BuiltinOSLogFormat(CallExpr *TheCall) {
  unsigned BuiltinID =
      cast<FunctionDecl>(TheCall->getCalleeDecl())->getBuiltinID();
  bool IsSizeCall = BuiltinID == Builtin::BI__builtin_os_log_format_buffer_size;

  unsigned NumArgs = TheCall->getNumArgs();
  unsigned NumRequiredArgs = IsSizeCall ? 1 : 2;
  if (NumArgs < NumRequiredArgs) {
    return Diag(TheCall->getEndLoc(), diag::err_typecheck_call_too_few_args)
           << 0 /* function call */ << NumRequiredArgs << NumArgs
           << /*is non object*/ 0 << TheCall->getSourceRange();
  }
  if (NumArgs >= NumRequiredArgs + 0x100) {
    return Diag(TheCall->getEndLoc(),
                diag::err_typecheck_call_too_many_args_at_most)
           << 0 /* function call */ << (NumRequiredArgs + 0xff) << NumArgs
           << /*is non object*/ 0 << TheCall->getSourceRange();
  }
  unsigned i = 0;

  // For formatting call, check buffer arg.
  if (!IsSizeCall) {
    ExprResult Arg(TheCall->getArg(i));
    InitializedEntity Entity = InitializedEntity::InitializeParameter(
        Context, Context.VoidPtrTy, false);
    Arg = PerformCopyInitialization(Entity, SourceLocation(), Arg);
    if (Arg.isInvalid())
      return true;
    TheCall->setArg(i, Arg.get());
    i++;
  }

  // Check string literal arg.
  unsigned FormatIdx = i;
  {
    ExprResult Arg = CheckOSLogFormatStringArg(TheCall->getArg(i));
    if (Arg.isInvalid())
      return true;
    TheCall->setArg(i, Arg.get());
    i++;
  }

  // Make sure variadic args are scalar.
  unsigned FirstDataArg = i;
  while (i < NumArgs) {
    ExprResult Arg = DefaultVariadicArgumentPromotion(
        TheCall->getArg(i), VariadicFunction, nullptr);
    if (Arg.isInvalid())
      return true;
    CharUnits ArgSize = Context.getTypeSizeInChars(Arg.get()->getType());
    if (ArgSize.getQuantity() >= 0x100) {
      return Diag(Arg.get()->getEndLoc(), diag::err_os_log_argument_too_big)
             << i << (int)ArgSize.getQuantity() << 0xff
             << TheCall->getSourceRange();
    }
    TheCall->setArg(i, Arg.get());
    i++;
  }

  // Check formatting specifiers. NOTE: We're only doing this for the non-size
  // call to avoid duplicate diagnostics.
  if (!IsSizeCall) {
    llvm::SmallBitVector CheckedVarArgs(NumArgs, false);
    ArrayRef<const Expr *> Args(TheCall->getArgs(), TheCall->getNumArgs());
    bool Success = CheckFormatArguments(
        Args, FAPK_Variadic, FormatIdx, FirstDataArg, FST_OSLog,
        VariadicFunction, TheCall->getBeginLoc(), SourceRange(),
        CheckedVarArgs);
    if (!Success)
      return true;
  }

  if (IsSizeCall) {
    TheCall->setType(Context.getSizeType());
  } else {
    TheCall->setType(Context.VoidPtrTy);
  }
  return false;
}

bool Sema::BuiltinConstantArg(CallExpr *TheCall, int ArgNum,
                              llvm::APSInt &Result) {
  Expr *Arg = TheCall->getArg(ArgNum);
  DeclRefExpr *DRE =cast<DeclRefExpr>(TheCall->getCallee()->IgnoreParenCasts());
  FunctionDecl *FDecl = cast<FunctionDecl>(DRE->getDecl());

  if (Arg->isTypeDependent() || Arg->isValueDependent()) return false;

  std::optional<llvm::APSInt> R;
  if (!(R = Arg->getIntegerConstantExpr(Context)))
    return Diag(TheCall->getBeginLoc(), diag::err_constant_integer_arg_type)
           << FDecl->getDeclName() << Arg->getSourceRange();
  Result = *R;
  return false;
}

bool Sema::BuiltinConstantArgRange(CallExpr *TheCall, int ArgNum, int Low,
                                   int High, bool RangeIsError) {
  if (isConstantEvaluatedContext())
    return false;
  llvm::APSInt Result;

  // We can't check the value of a dependent argument.
  Expr *Arg = TheCall->getArg(ArgNum);
  if (Arg->isTypeDependent() || Arg->isValueDependent())
    return false;

  // Check constant-ness first.
  if (BuiltinConstantArg(TheCall, ArgNum, Result))
    return true;

  if (Result.getSExtValue() < Low || Result.getSExtValue() > High) {
    if (RangeIsError)
      return Diag(TheCall->getBeginLoc(), diag::err_argument_invalid_range)
             << toString(Result, 10) << Low << High << Arg->getSourceRange();
    else
      // Defer the warning until we know if the code will be emitted so that
      // dead code can ignore this.
      DiagRuntimeBehavior(TheCall->getBeginLoc(), TheCall,
                          PDiag(diag::warn_argument_invalid_range)
                              << toString(Result, 10) << Low << High
                              << Arg->getSourceRange());
  }

  return false;
}

bool Sema::BuiltinConstantArgMultiple(CallExpr *TheCall, int ArgNum,
                                      unsigned Num) {
  llvm::APSInt Result;

  // We can't check the value of a dependent argument.
  Expr *Arg = TheCall->getArg(ArgNum);
  if (Arg->isTypeDependent() || Arg->isValueDependent())
    return false;

  // Check constant-ness first.
  if (BuiltinConstantArg(TheCall, ArgNum, Result))
    return true;

  if (Result.getSExtValue() % Num != 0)
    return Diag(TheCall->getBeginLoc(), diag::err_argument_not_multiple)
           << Num << Arg->getSourceRange();

  return false;
}

bool Sema::BuiltinConstantArgPower2(CallExpr *TheCall, int ArgNum) {
  llvm::APSInt Result;

  // We can't check the value of a dependent argument.
  Expr *Arg = TheCall->getArg(ArgNum);
  if (Arg->isTypeDependent() || Arg->isValueDependent())
    return false;

  // Check constant-ness first.
  if (BuiltinConstantArg(TheCall, ArgNum, Result))
    return true;

  // Bit-twiddling to test for a power of 2: for x > 0, x & (x-1) is zero if
  // and only if x is a power of 2.
  if (Result.isStrictlyPositive() && (Result & (Result - 1)) == 0)
    return false;

  return Diag(TheCall->getBeginLoc(), diag::err_argument_not_power_of_2)
         << Arg->getSourceRange();
}

static bool IsShiftedByte(llvm::APSInt Value) {
  if (Value.isNegative())
    return false;

  // Check if it's a shifted byte, by shifting it down
  while (true) {
    // If the value fits in the bottom byte, the check passes.
    if (Value < 0x100)
      return true;

    // Otherwise, if the value has _any_ bits in the bottom byte, the check
    // fails.
    if ((Value & 0xFF) != 0)
      return false;

    // If the bottom 8 bits are all 0, but something above that is nonzero,
    // then shifting the value right by 8 bits won't affect whether it's a
    // shifted byte or not. So do that, and go round again.
    Value >>= 8;
  }
}

bool Sema::BuiltinConstantArgShiftedByte(CallExpr *TheCall, int ArgNum,
                                         unsigned ArgBits) {
  llvm::APSInt Result;

  // We can't check the value of a dependent argument.
  Expr *Arg = TheCall->getArg(ArgNum);
  if (Arg->isTypeDependent() || Arg->isValueDependent())
    return false;

  // Check constant-ness first.
  if (BuiltinConstantArg(TheCall, ArgNum, Result))
    return true;

  // Truncate to the given size.
  Result = Result.getLoBits(ArgBits);
  Result.setIsUnsigned(true);

  if (IsShiftedByte(Result))
    return false;

  return Diag(TheCall->getBeginLoc(), diag::err_argument_not_shifted_byte)
         << Arg->getSourceRange();
}

bool Sema::BuiltinConstantArgShiftedByteOrXXFF(CallExpr *TheCall, int ArgNum,
                                               unsigned ArgBits) {
  llvm::APSInt Result;

  // We can't check the value of a dependent argument.
  Expr *Arg = TheCall->getArg(ArgNum);
  if (Arg->isTypeDependent() || Arg->isValueDependent())
    return false;

  // Check constant-ness first.
  if (BuiltinConstantArg(TheCall, ArgNum, Result))
    return true;

  // Truncate to the given size.
  Result = Result.getLoBits(ArgBits);
  Result.setIsUnsigned(true);

  // Check to see if it's in either of the required forms.
  if (IsShiftedByte(Result) ||
      (Result > 0 && Result < 0x10000 && (Result & 0xFF) == 0xFF))
    return false;

  return Diag(TheCall->getBeginLoc(),
              diag::err_argument_not_shifted_byte_or_xxff)
         << Arg->getSourceRange();
}

bool Sema::BuiltinLongjmp(CallExpr *TheCall) {
  if (!Context.getTargetInfo().hasSjLjLowering())
    return Diag(TheCall->getBeginLoc(), diag::err_builtin_longjmp_unsupported)
           << SourceRange(TheCall->getBeginLoc(), TheCall->getEndLoc());

  Expr *Arg = TheCall->getArg(1);
  llvm::APSInt Result;

  // TODO: This is less than ideal. Overload this to take a value.
  if (BuiltinConstantArg(TheCall, 1, Result))
    return true;

  if (Result != 1)
    return Diag(TheCall->getBeginLoc(), diag::err_builtin_longjmp_invalid_val)
           << SourceRange(Arg->getBeginLoc(), Arg->getEndLoc());

  return false;
}

bool Sema::BuiltinSetjmp(CallExpr *TheCall) {
  if (!Context.getTargetInfo().hasSjLjLowering())
    return Diag(TheCall->getBeginLoc(), diag::err_builtin_setjmp_unsupported)
           << SourceRange(TheCall->getBeginLoc(), TheCall->getEndLoc());
  return false;
}

namespace {

class UncoveredArgHandler {
  enum { Unknown = -1, AllCovered = -2 };

  signed FirstUncoveredArg = Unknown;
  SmallVector<const Expr *, 4> DiagnosticExprs;

public:
  UncoveredArgHandler() = default;

  bool hasUncoveredArg() const {
    return (FirstUncoveredArg >= 0);
  }

  unsigned getUncoveredArg() const {
    assert(hasUncoveredArg() && "no uncovered argument");
    return FirstUncoveredArg;
  }

  void setAllCovered() {
    // A string has been found with all arguments covered, so clear out
    // the diagnostics.
    DiagnosticExprs.clear();
    FirstUncoveredArg = AllCovered;
  }

  void Update(signed NewFirstUncoveredArg, const Expr *StrExpr) {
    assert(NewFirstUncoveredArg >= 0 && "Outside range");

    // Don't update if a previous string covers all arguments.
    if (FirstUncoveredArg == AllCovered)
      return;

    // UncoveredArgHandler tracks the highest uncovered argument index
    // and with it all the strings that match this index.
    if (NewFirstUncoveredArg == FirstUncoveredArg)
      DiagnosticExprs.push_back(StrExpr);
    else if (NewFirstUncoveredArg > FirstUncoveredArg) {
      DiagnosticExprs.clear();
      DiagnosticExprs.push_back(StrExpr);
      FirstUncoveredArg = NewFirstUncoveredArg;
    }
  }

  void Diagnose(Sema &S, bool IsFunctionCall, const Expr *ArgExpr);
};

enum StringLiteralCheckType {
  SLCT_NotALiteral,
  SLCT_UncheckedLiteral,
  SLCT_CheckedLiteral
};

} // namespace

static void sumOffsets(llvm::APSInt &Offset, llvm::APSInt Addend,
                                     BinaryOperatorKind BinOpKind,
                                     bool AddendIsRight) {
  unsigned BitWidth = Offset.getBitWidth();
  unsigned AddendBitWidth = Addend.getBitWidth();
  // There might be negative interim results.
  if (Addend.isUnsigned()) {
    Addend = Addend.zext(++AddendBitWidth);
    Addend.setIsSigned(true);
  }
  // Adjust the bit width of the APSInts.
  if (AddendBitWidth > BitWidth) {
    Offset = Offset.sext(AddendBitWidth);
    BitWidth = AddendBitWidth;
  } else if (BitWidth > AddendBitWidth) {
    Addend = Addend.sext(BitWidth);
  }

  bool Ov = false;
  llvm::APSInt ResOffset = Offset;
  if (BinOpKind == BO_Add)
    ResOffset = Offset.sadd_ov(Addend, Ov);
  else {
    assert(AddendIsRight && BinOpKind == BO_Sub &&
           "operator must be add or sub with addend on the right");
    ResOffset = Offset.ssub_ov(Addend, Ov);
  }

  // We add an offset to a pointer here so we should support an offset as big as
  // possible.
  if (Ov) {
    assert(BitWidth <= std::numeric_limits<unsigned>::max() / 2 &&
           "index (intermediate) result too big");
    Offset = Offset.sext(2 * BitWidth);
    sumOffsets(Offset, Addend, BinOpKind, AddendIsRight);
    return;
  }

  Offset = ResOffset;
}

namespace {

// This is a wrapper class around StringLiteral to support offsetted string
// literals as format strings. It takes the offset into account when returning
// the string and its length or the source locations to display notes correctly.
class FormatStringLiteral {
  const StringLiteral *FExpr;
  int64_t Offset;

 public:
  FormatStringLiteral(const StringLiteral *fexpr, int64_t Offset = 0)
      : FExpr(fexpr), Offset(Offset) {}

  StringRef getString() const {
    return FExpr->getString().drop_front(Offset);
  }

  unsigned getByteLength() const {
    return FExpr->getByteLength() - getCharByteWidth() * Offset;
  }

  unsigned getLength() const { return FExpr->getLength() - Offset; }
  unsigned getCharByteWidth() const { return FExpr->getCharByteWidth(); }

  StringLiteralKind getKind() const { return FExpr->getKind(); }

  QualType getType() const { return FExpr->getType(); }

  bool isAscii() const { return FExpr->isOrdinary(); }
  bool isWide() const { return FExpr->isWide(); }
  bool isUTF8() const { return FExpr->isUTF8(); }
  bool isUTF16() const { return FExpr->isUTF16(); }
  bool isUTF32() const { return FExpr->isUTF32(); }
  bool isPascal() const { return FExpr->isPascal(); }

  SourceLocation getLocationOfByte(
      unsigned ByteNo, const SourceManager &SM, const LangOptions &Features,
      const TargetInfo &Target, unsigned *StartToken = nullptr,
      unsigned *StartTokenByteOffset = nullptr) const {
    return FExpr->getLocationOfByte(ByteNo + Offset, SM, Features, Target,
                                    StartToken, StartTokenByteOffset);
  }

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return FExpr->getBeginLoc().getLocWithOffset(Offset);
  }

  SourceLocation getEndLoc() const LLVM_READONLY { return FExpr->getEndLoc(); }
};

} // namespace

static void CheckFormatString(
    Sema &S, const FormatStringLiteral *FExpr, const Expr *OrigFormatExpr,
    ArrayRef<const Expr *> Args, Sema::FormatArgumentPassingKind APK,
    unsigned format_idx, unsigned firstDataArg, Sema::FormatStringType Type,
    bool inFunctionCall, Sema::VariadicCallType CallType,
    llvm::SmallBitVector &CheckedVarArgs, UncoveredArgHandler &UncoveredArg,
    bool IgnoreStringsWithoutSpecifiers);

static const Expr *maybeConstEvalStringLiteral(ASTContext &Context,
                                               const Expr *E);

// Determine if an expression is a string literal or constant string.
// If this function returns false on the arguments to a function expecting a
// format string, we will usually need to emit a warning.
// True string literals are then checked by CheckFormatString.
static StringLiteralCheckType
checkFormatStringExpr(Sema &S, const Expr *E, ArrayRef<const Expr *> Args,
                      Sema::FormatArgumentPassingKind APK, unsigned format_idx,
                      unsigned firstDataArg, Sema::FormatStringType Type,
                      Sema::VariadicCallType CallType, bool InFunctionCall,
                      llvm::SmallBitVector &CheckedVarArgs,
                      UncoveredArgHandler &UncoveredArg, llvm::APSInt Offset,
                      bool IgnoreStringsWithoutSpecifiers = false) {
  if (S.isConstantEvaluatedContext())
    return SLCT_NotALiteral;
tryAgain:
  assert(Offset.isSigned() && "invalid offset");

  if (E->isTypeDependent() || E->isValueDependent())
    return SLCT_NotALiteral;

  E = E->IgnoreParenCasts();

  if (E->isNullPointerConstant(S.Context, Expr::NPC_ValueDependentIsNotNull))
    // Technically -Wformat-nonliteral does not warn about this case.
    // The behavior of printf and friends in this case is implementation
    // dependent.  Ideally if the format string cannot be null then
    // it should have a 'nonnull' attribute in the function prototype.
    return SLCT_UncheckedLiteral;

  switch (E->getStmtClass()) {
  case Stmt::InitListExprClass:
    // Handle expressions like {"foobar"}.
    if (const clang::Expr *SLE = maybeConstEvalStringLiteral(S.Context, E)) {
      return checkFormatStringExpr(S, SLE, Args, APK, format_idx, firstDataArg,
                                   Type, CallType, /*InFunctionCall*/ false,
                                   CheckedVarArgs, UncoveredArg, Offset,
                                   IgnoreStringsWithoutSpecifiers);
    }
    return SLCT_NotALiteral;
  case Stmt::BinaryConditionalOperatorClass:
  case Stmt::ConditionalOperatorClass: {
    // The expression is a literal if both sub-expressions were, and it was
    // completely checked only if both sub-expressions were checked.
    const AbstractConditionalOperator *C =
        cast<AbstractConditionalOperator>(E);

    // Determine whether it is necessary to check both sub-expressions, for
    // example, because the condition expression is a constant that can be
    // evaluated at compile time.
    bool CheckLeft = true, CheckRight = true;

    bool Cond;
    if (C->getCond()->EvaluateAsBooleanCondition(
            Cond, S.getASTContext(), S.isConstantEvaluatedContext())) {
      if (Cond)
        CheckRight = false;
      else
        CheckLeft = false;
    }

    // We need to maintain the offsets for the right and the left hand side
    // separately to check if every possible indexed expression is a valid
    // string literal. They might have different offsets for different string
    // literals in the end.
    StringLiteralCheckType Left;
    if (!CheckLeft)
      Left = SLCT_UncheckedLiteral;
    else {
      Left = checkFormatStringExpr(S, C->getTrueExpr(), Args, APK, format_idx,
                                   firstDataArg, Type, CallType, InFunctionCall,
                                   CheckedVarArgs, UncoveredArg, Offset,
                                   IgnoreStringsWithoutSpecifiers);
      if (Left == SLCT_NotALiteral || !CheckRight) {
        return Left;
      }
    }

    StringLiteralCheckType Right = checkFormatStringExpr(
        S, C->getFalseExpr(), Args, APK, format_idx, firstDataArg, Type,
        CallType, InFunctionCall, CheckedVarArgs, UncoveredArg, Offset,
        IgnoreStringsWithoutSpecifiers);

    return (CheckLeft && Left < Right) ? Left : Right;
  }

  case Stmt::ImplicitCastExprClass:
    E = cast<ImplicitCastExpr>(E)->getSubExpr();
    goto tryAgain;

  case Stmt::OpaqueValueExprClass:
    if (const Expr *src = cast<OpaqueValueExpr>(E)->getSourceExpr()) {
      E = src;
      goto tryAgain;
    }
    return SLCT_NotALiteral;

  case Stmt::PredefinedExprClass:
    // While __func__, etc., are technically not string literals, they
    // cannot contain format specifiers and thus are not a security
    // liability.
    return SLCT_UncheckedLiteral;

  case Stmt::DeclRefExprClass: {
    const DeclRefExpr *DR = cast<DeclRefExpr>(E);

    // As an exception, do not flag errors for variables binding to
    // const string literals.
    if (const VarDecl *VD = dyn_cast<VarDecl>(DR->getDecl())) {
      bool isConstant = false;
      QualType T = DR->getType();

      if (const ArrayType *AT = S.Context.getAsArrayType(T)) {
        isConstant = AT->getElementType().isConstant(S.Context);
      } else if (const PointerType *PT = T->getAs<PointerType>()) {
        isConstant = T.isConstant(S.Context) &&
                     PT->getPointeeType().isConstant(S.Context);
      } else if (T->isObjCObjectPointerType()) {
        // In ObjC, there is usually no "const ObjectPointer" type,
        // so don't check if the pointee type is constant.
        isConstant = T.isConstant(S.Context);
      }

      if (isConstant) {
        if (const Expr *Init = VD->getAnyInitializer()) {
          // Look through initializers like const char c[] = { "foo" }
          if (const InitListExpr *InitList = dyn_cast<InitListExpr>(Init)) {
            if (InitList->isStringLiteralInit())
              Init = InitList->getInit(0)->IgnoreParenImpCasts();
          }
          return checkFormatStringExpr(
              S, Init, Args, APK, format_idx, firstDataArg, Type, CallType,
              /*InFunctionCall*/ false, CheckedVarArgs, UncoveredArg, Offset);
        }
      }

      // When the format argument is an argument of this function, and this
      // function also has the format attribute, there are several interactions
      // for which there shouldn't be a warning. For instance, when calling
      // v*printf from a function that has the printf format attribute, we
      // should not emit a warning about using `fmt`, even though it's not
      // constant, because the arguments have already been checked for the
      // caller of `logmessage`:
      //
      //  __attribute__((format(printf, 1, 2)))
      //  void logmessage(char const *fmt, ...) {
      //    va_list ap;
      //    va_start(ap, fmt);
      //    vprintf(fmt, ap);  /* do not emit a warning about "fmt" */
      //    ...
      // }
      //
      // Another interaction that we need to support is calling a variadic
      // format function from a format function that has fixed arguments. For
      // instance:
      //
      //  __attribute__((format(printf, 1, 2)))
      //  void logstring(char const *fmt, char const *str) {
      //    printf(fmt, str);  /* do not emit a warning about "fmt" */
      //  }
      //
      // Same (and perhaps more relatably) for the variadic template case:
      //
      //  template<typename... Args>
      //  __attribute__((format(printf, 1, 2)))
      //  void log(const char *fmt, Args&&... args) {
      //    printf(fmt, forward<Args>(args)...);
      //           /* do not emit a warning about "fmt" */
      //  }
      //
      // Due to implementation difficulty, we only check the format, not the
      // format arguments, in all cases.
      //
      if (const auto *PV = dyn_cast<ParmVarDecl>(VD)) {
        if (const auto *D = dyn_cast<Decl>(PV->getDeclContext())) {
          for (const auto *PVFormat : D->specific_attrs<FormatAttr>()) {
            bool IsCXXMember = false;
            if (const auto *MD = dyn_cast<CXXMethodDecl>(D))
              IsCXXMember = MD->isInstance();

            bool IsVariadic = false;
            if (const FunctionType *FnTy = D->getFunctionType())
              IsVariadic = cast<FunctionProtoType>(FnTy)->isVariadic();
            else if (const auto *BD = dyn_cast<BlockDecl>(D))
              IsVariadic = BD->isVariadic();
            else if (const auto *OMD = dyn_cast<ObjCMethodDecl>(D))
              IsVariadic = OMD->isVariadic();

            Sema::FormatStringInfo CallerFSI;
            if (Sema::getFormatStringInfo(PVFormat, IsCXXMember, IsVariadic,
                                          &CallerFSI)) {
              // We also check if the formats are compatible.
              // We can't pass a 'scanf' string to a 'printf' function.
              if (PV->getFunctionScopeIndex() == CallerFSI.FormatIdx &&
                  Type == S.GetFormatStringType(PVFormat)) {
                // Lastly, check that argument passing kinds transition in a
                // way that makes sense:
                // from a caller with FAPK_VAList, allow FAPK_VAList
                // from a caller with FAPK_Fixed, allow FAPK_Fixed
                // from a caller with FAPK_Fixed, allow FAPK_Variadic
                // from a caller with FAPK_Variadic, allow FAPK_VAList
                switch (combineFAPK(CallerFSI.ArgPassingKind, APK)) {
                case combineFAPK(Sema::FAPK_VAList, Sema::FAPK_VAList):
                case combineFAPK(Sema::FAPK_Fixed, Sema::FAPK_Fixed):
                case combineFAPK(Sema::FAPK_Fixed, Sema::FAPK_Variadic):
                case combineFAPK(Sema::FAPK_Variadic, Sema::FAPK_VAList):
                  return SLCT_UncheckedLiteral;
                }
              }
            }
          }
        }
      }
    }

    return SLCT_NotALiteral;
  }

  case Stmt::CallExprClass:
  case Stmt::CXXMemberCallExprClass: {
    const CallExpr *CE = cast<CallExpr>(E);
    if (const NamedDecl *ND = dyn_cast_or_null<NamedDecl>(CE->getCalleeDecl())) {
      bool IsFirst = true;
      StringLiteralCheckType CommonResult;
      for (const auto *FA : ND->specific_attrs<FormatArgAttr>()) {
        const Expr *Arg = CE->getArg(FA->getFormatIdx().getASTIndex());
        StringLiteralCheckType Result = checkFormatStringExpr(
            S, Arg, Args, APK, format_idx, firstDataArg, Type, CallType,
            InFunctionCall, CheckedVarArgs, UncoveredArg, Offset,
            IgnoreStringsWithoutSpecifiers);
        if (IsFirst) {
          CommonResult = Result;
          IsFirst = false;
        }
      }
      if (!IsFirst)
        return CommonResult;

      if (const auto *FD = dyn_cast<FunctionDecl>(ND)) {
        unsigned BuiltinID = FD->getBuiltinID();
        if (BuiltinID == Builtin::BI__builtin___CFStringMakeConstantString ||
            BuiltinID == Builtin::BI__builtin___NSStringMakeConstantString) {
          const Expr *Arg = CE->getArg(0);
          return checkFormatStringExpr(
              S, Arg, Args, APK, format_idx, firstDataArg, Type, CallType,
              InFunctionCall, CheckedVarArgs, UncoveredArg, Offset,
              IgnoreStringsWithoutSpecifiers);
        }
      }
    }
    if (const Expr *SLE = maybeConstEvalStringLiteral(S.Context, E))
      return checkFormatStringExpr(S, SLE, Args, APK, format_idx, firstDataArg,
                                   Type, CallType, /*InFunctionCall*/ false,
                                   CheckedVarArgs, UncoveredArg, Offset,
                                   IgnoreStringsWithoutSpecifiers);
    return SLCT_NotALiteral;
  }
  case Stmt::ObjCMessageExprClass: {
    const auto *ME = cast<ObjCMessageExpr>(E);
    if (const auto *MD = ME->getMethodDecl()) {
      if (const auto *FA = MD->getAttr<FormatArgAttr>()) {
        // As a special case heuristic, if we're using the method -[NSBundle
        // localizedStringForKey:value:table:], ignore any key strings that lack
        // format specifiers. The idea is that if the key doesn't have any
        // format specifiers then its probably just a key to map to the
        // localized strings. If it does have format specifiers though, then its
        // likely that the text of the key is the format string in the
        // programmer's language, and should be checked.
        const ObjCInterfaceDecl *IFace;
        if (MD->isInstanceMethod() && (IFace = MD->getClassInterface()) &&
            IFace->getIdentifier()->isStr("NSBundle") &&
            MD->getSelector().isKeywordSelector(
                {"localizedStringForKey", "value", "table"})) {
          IgnoreStringsWithoutSpecifiers = true;
        }

        const Expr *Arg = ME->getArg(FA->getFormatIdx().getASTIndex());
        return checkFormatStringExpr(
            S, Arg, Args, APK, format_idx, firstDataArg, Type, CallType,
            InFunctionCall, CheckedVarArgs, UncoveredArg, Offset,
            IgnoreStringsWithoutSpecifiers);
      }
    }

    return SLCT_NotALiteral;
  }
  case Stmt::ObjCStringLiteralClass:
  case Stmt::StringLiteralClass: {
    const StringLiteral *StrE = nullptr;

    if (const ObjCStringLiteral *ObjCFExpr = dyn_cast<ObjCStringLiteral>(E))
      StrE = ObjCFExpr->getString();
    else
      StrE = cast<StringLiteral>(E);

    if (StrE) {
      if (Offset.isNegative() || Offset > StrE->getLength()) {
        // TODO: It would be better to have an explicit warning for out of
        // bounds literals.
        return SLCT_NotALiteral;
      }
      FormatStringLiteral FStr(StrE, Offset.sextOrTrunc(64).getSExtValue());
      CheckFormatString(S, &FStr, E, Args, APK, format_idx, firstDataArg, Type,
                        InFunctionCall, CallType, CheckedVarArgs, UncoveredArg,
                        IgnoreStringsWithoutSpecifiers);
      return SLCT_CheckedLiteral;
    }

    return SLCT_NotALiteral;
  }
  case Stmt::BinaryOperatorClass: {
    const BinaryOperator *BinOp = cast<BinaryOperator>(E);

    // A string literal + an int offset is still a string literal.
    if (BinOp->isAdditiveOp()) {
      Expr::EvalResult LResult, RResult;

      bool LIsInt = BinOp->getLHS()->EvaluateAsInt(
          LResult, S.Context, Expr::SE_NoSideEffects,
          S.isConstantEvaluatedContext());
      bool RIsInt = BinOp->getRHS()->EvaluateAsInt(
          RResult, S.Context, Expr::SE_NoSideEffects,
          S.isConstantEvaluatedContext());

      if (LIsInt != RIsInt) {
        BinaryOperatorKind BinOpKind = BinOp->getOpcode();

        if (LIsInt) {
          if (BinOpKind == BO_Add) {
            sumOffsets(Offset, LResult.Val.getInt(), BinOpKind, RIsInt);
            E = BinOp->getRHS();
            goto tryAgain;
          }
        } else {
          sumOffsets(Offset, RResult.Val.getInt(), BinOpKind, RIsInt);
          E = BinOp->getLHS();
          goto tryAgain;
        }
      }
    }

    return SLCT_NotALiteral;
  }
  case Stmt::UnaryOperatorClass: {
    const UnaryOperator *UnaOp = cast<UnaryOperator>(E);
    auto ASE = dyn_cast<ArraySubscriptExpr>(UnaOp->getSubExpr());
    if (UnaOp->getOpcode() == UO_AddrOf && ASE) {
      Expr::EvalResult IndexResult;
      if (ASE->getRHS()->EvaluateAsInt(IndexResult, S.Context,
                                       Expr::SE_NoSideEffects,
                                       S.isConstantEvaluatedContext())) {
        sumOffsets(Offset, IndexResult.Val.getInt(), BO_Add,
                   /*RHS is int*/ true);
        E = ASE->getBase();
        goto tryAgain;
      }
    }

    return SLCT_NotALiteral;
  }

  default:
    return SLCT_NotALiteral;
  }
}

// If this expression can be evaluated at compile-time,
// check if the result is a StringLiteral and return it
// otherwise return nullptr
static const Expr *maybeConstEvalStringLiteral(ASTContext &Context,
                                               const Expr *E) {
  Expr::EvalResult Result;
  if (E->EvaluateAsRValue(Result, Context) && Result.Val.isLValue()) {
    const auto *LVE = Result.Val.getLValueBase().dyn_cast<const Expr *>();
    if (isa_and_nonnull<StringLiteral>(LVE))
      return LVE;
  }
  return nullptr;
}

Sema::FormatStringType Sema::GetFormatStringType(const FormatAttr *Format) {
  return llvm::StringSwitch<FormatStringType>(Format->getType()->getName())
      .Case("scanf", FST_Scanf)
      .Cases("printf", "printf0", "syslog", FST_Printf)
      .Cases("NSString", "CFString", FST_NSString)
      .Case("strftime", FST_Strftime)
      .Case("strfmon", FST_Strfmon)
      .Cases("kprintf", "cmn_err", "vcmn_err", "zcmn_err", FST_Kprintf)
      .Case("freebsd_kprintf", FST_FreeBSDKPrintf)
      .Case("os_trace", FST_OSLog)
      .Case("os_log", FST_OSLog)
      .Default(FST_Unknown);
}

bool Sema::CheckFormatArguments(const FormatAttr *Format,
                                ArrayRef<const Expr *> Args, bool IsCXXMember,
                                VariadicCallType CallType, SourceLocation Loc,
                                SourceRange Range,
                                llvm::SmallBitVector &CheckedVarArgs) {
  FormatStringInfo FSI;
  if (getFormatStringInfo(Format, IsCXXMember, CallType != VariadicDoesNotApply,
                          &FSI))
    return CheckFormatArguments(Args, FSI.ArgPassingKind, FSI.FormatIdx,
                                FSI.FirstDataArg, GetFormatStringType(Format),
                                CallType, Loc, Range, CheckedVarArgs);
  return false;
}

bool Sema::CheckFormatArguments(ArrayRef<const Expr *> Args,
                                Sema::FormatArgumentPassingKind APK,
                                unsigned format_idx, unsigned firstDataArg,
                                FormatStringType Type,
                                VariadicCallType CallType, SourceLocation Loc,
                                SourceRange Range,
                                llvm::SmallBitVector &CheckedVarArgs) {
  // CHECK: printf/scanf-like function is called with no format string.
  if (format_idx >= Args.size()) {
    Diag(Loc, diag::warn_missing_format_string) << Range;
    return false;
  }

  const Expr *OrigFormatExpr = Args[format_idx]->IgnoreParenCasts();

  // CHECK: format string is not a string literal.
  //
  // Dynamically generated format strings are difficult to
  // automatically vet at compile time.  Requiring that format strings
  // are string literals: (1) permits the checking of format strings by
  // the compiler and thereby (2) can practically remove the source of
  // many format string exploits.

  // Format string can be either ObjC string (e.g. @"%d") or
  // C string (e.g. "%d")
  // ObjC string uses the same format specifiers as C string, so we can use
  // the same format string checking logic for both ObjC and C strings.
  UncoveredArgHandler UncoveredArg;
  StringLiteralCheckType CT = checkFormatStringExpr(
      *this, OrigFormatExpr, Args, APK, format_idx, firstDataArg, Type,
      CallType,
      /*IsFunctionCall*/ true, CheckedVarArgs, UncoveredArg,
      /*no string offset*/ llvm::APSInt(64, false) = 0);

  // Generate a diagnostic where an uncovered argument is detected.
  if (UncoveredArg.hasUncoveredArg()) {
    unsigned ArgIdx = UncoveredArg.getUncoveredArg() + firstDataArg;
    assert(ArgIdx < Args.size() && "ArgIdx outside bounds");
    UncoveredArg.Diagnose(*this, /*IsFunctionCall*/true, Args[ArgIdx]);
  }

  if (CT != SLCT_NotALiteral)
    // Literal format string found, check done!
    return CT == SLCT_CheckedLiteral;

  // Strftime is particular as it always uses a single 'time' argument,
  // so it is safe to pass a non-literal string.
  if (Type == FST_Strftime)
    return false;

  // Do not emit diag when the string param is a macro expansion and the
  // format is either NSString or CFString. This is a hack to prevent
  // diag when using the NSLocalizedString and CFCopyLocalizedString macros
  // which are usually used in place of NS and CF string literals.
  SourceLocation FormatLoc = Args[format_idx]->getBeginLoc();
  if (Type == FST_NSString && SourceMgr.isInSystemMacro(FormatLoc))
    return false;

  // If there are no arguments specified, warn with -Wformat-security, otherwise
  // warn only with -Wformat-nonliteral.
  if (Args.size() == firstDataArg) {
    Diag(FormatLoc, diag::warn_format_nonliteral_noargs)
      << OrigFormatExpr->getSourceRange();
    switch (Type) {
    default:
      break;
    case FST_Kprintf:
    case FST_FreeBSDKPrintf:
    case FST_Printf:
    case FST_Syslog:
      Diag(FormatLoc, diag::note_format_security_fixit)
        << FixItHint::CreateInsertion(FormatLoc, "\"%s\", ");
      break;
    case FST_NSString:
      Diag(FormatLoc, diag::note_format_security_fixit)
        << FixItHint::CreateInsertion(FormatLoc, "@\"%@\", ");
      break;
    }
  } else {
    Diag(FormatLoc, diag::warn_format_nonliteral)
      << OrigFormatExpr->getSourceRange();
  }
  return false;
}

namespace {

class CheckFormatHandler : public analyze_format_string::FormatStringHandler {
protected:
  Sema &S;
  const FormatStringLiteral *FExpr;
  const Expr *OrigFormatExpr;
  const Sema::FormatStringType FSType;
  const unsigned FirstDataArg;
  const unsigned NumDataArgs;
  const char *Beg; // Start of format string.
  const Sema::FormatArgumentPassingKind ArgPassingKind;
  ArrayRef<const Expr *> Args;
  unsigned FormatIdx;
  llvm::SmallBitVector CoveredArgs;
  bool usesPositionalArgs = false;
  bool atFirstArg = true;
  bool inFunctionCall;
  Sema::VariadicCallType CallType;
  llvm::SmallBitVector &CheckedVarArgs;
  UncoveredArgHandler &UncoveredArg;

public:
  CheckFormatHandler(Sema &s, const FormatStringLiteral *fexpr,
                     const Expr *origFormatExpr,
                     const Sema::FormatStringType type, unsigned firstDataArg,
                     unsigned numDataArgs, const char *beg,
                     Sema::FormatArgumentPassingKind APK,
                     ArrayRef<const Expr *> Args, unsigned formatIdx,
                     bool inFunctionCall, Sema::VariadicCallType callType,
                     llvm::SmallBitVector &CheckedVarArgs,
                     UncoveredArgHandler &UncoveredArg)
      : S(s), FExpr(fexpr), OrigFormatExpr(origFormatExpr), FSType(type),
        FirstDataArg(firstDataArg), NumDataArgs(numDataArgs), Beg(beg),
        ArgPassingKind(APK), Args(Args), FormatIdx(formatIdx),
        inFunctionCall(inFunctionCall), CallType(callType),
        CheckedVarArgs(CheckedVarArgs), UncoveredArg(UncoveredArg) {
    CoveredArgs.resize(numDataArgs);
    CoveredArgs.reset();
  }

  void DoneProcessing();

  void HandleIncompleteSpecifier(const char *startSpecifier,
                                 unsigned specifierLen) override;

  void HandleInvalidLengthModifier(
                           const analyze_format_string::FormatSpecifier &FS,
                           const analyze_format_string::ConversionSpecifier &CS,
                           const char *startSpecifier, unsigned specifierLen,
                           unsigned DiagID);

  void HandleNonStandardLengthModifier(
                    const analyze_format_string::FormatSpecifier &FS,
                    const char *startSpecifier, unsigned specifierLen);

  void HandleNonStandardConversionSpecifier(
                    const analyze_format_string::ConversionSpecifier &CS,
                    const char *startSpecifier, unsigned specifierLen);

  void HandlePosition(const char *startPos, unsigned posLen) override;

  void HandleInvalidPosition(const char *startSpecifier,
                             unsigned specifierLen,
                             analyze_format_string::PositionContext p) override;

  void HandleZeroPosition(const char *startPos, unsigned posLen) override;

  void HandleNullChar(const char *nullCharacter) override;

  template <typename Range>
  static void
  EmitFormatDiagnostic(Sema &S, bool inFunctionCall, const Expr *ArgumentExpr,
                       const PartialDiagnostic &PDiag, SourceLocation StringLoc,
                       bool IsStringLocation, Range StringRange,
                       ArrayRef<FixItHint> Fixit = std::nullopt);

protected:
  bool HandleInvalidConversionSpecifier(unsigned argIndex, SourceLocation Loc,
                                        const char *startSpec,
                                        unsigned specifierLen,
                                        const char *csStart, unsigned csLen);

  void HandlePositionalNonpositionalArgs(SourceLocation Loc,
                                         const char *startSpec,
                                         unsigned specifierLen);

  SourceRange getFormatStringRange();
  CharSourceRange getSpecifierRange(const char *startSpecifier,
                                    unsigned specifierLen);
  SourceLocation getLocationOfByte(const char *x);

  const Expr *getDataArg(unsigned i) const;

  bool CheckNumArgs(const analyze_format_string::FormatSpecifier &FS,
                    const analyze_format_string::ConversionSpecifier &CS,
                    const char *startSpecifier, unsigned specifierLen,
                    unsigned argIndex);

  template <typename Range>
  void EmitFormatDiagnostic(PartialDiagnostic PDiag, SourceLocation StringLoc,
                            bool IsStringLocation, Range StringRange,
                            ArrayRef<FixItHint> Fixit = std::nullopt);
};

} // namespace

SourceRange CheckFormatHandler::getFormatStringRange() {
  return OrigFormatExpr->getSourceRange();
}

CharSourceRange CheckFormatHandler::
getSpecifierRange(const char *startSpecifier, unsigned specifierLen) {
  SourceLocation Start = getLocationOfByte(startSpecifier);
  SourceLocation End   = getLocationOfByte(startSpecifier + specifierLen - 1);

  // Advance the end SourceLocation by one due to half-open ranges.
  End = End.getLocWithOffset(1);

  return CharSourceRange::getCharRange(Start, End);
}

SourceLocation CheckFormatHandler::getLocationOfByte(const char *x) {
  return FExpr->getLocationOfByte(x - Beg, S.getSourceManager(),
                                  S.getLangOpts(), S.Context.getTargetInfo());
}

void CheckFormatHandler::HandleIncompleteSpecifier(const char *startSpecifier,
                                                   unsigned specifierLen){
  EmitFormatDiagnostic(S.PDiag(diag::warn_printf_incomplete_specifier),
                       getLocationOfByte(startSpecifier),
                       /*IsStringLocation*/true,
                       getSpecifierRange(startSpecifier, specifierLen));
}

void CheckFormatHandler::HandleInvalidLengthModifier(
    const analyze_format_string::FormatSpecifier &FS,
    const analyze_format_string::ConversionSpecifier &CS,
    const char *startSpecifier, unsigned specifierLen, unsigned DiagID) {
  using namespace analyze_format_string;

  const LengthModifier &LM = FS.getLengthModifier();
  CharSourceRange LMRange = getSpecifierRange(LM.getStart(), LM.getLength());

  // See if we know how to fix this length modifier.
  std::optional<LengthModifier> FixedLM = FS.getCorrectedLengthModifier();
  if (FixedLM) {
    EmitFormatDiagnostic(S.PDiag(DiagID) << LM.toString() << CS.toString(),
                         getLocationOfByte(LM.getStart()),
                         /*IsStringLocation*/true,
                         getSpecifierRange(startSpecifier, specifierLen));

    S.Diag(getLocationOfByte(LM.getStart()), diag::note_format_fix_specifier)
      << FixedLM->toString()
      << FixItHint::CreateReplacement(LMRange, FixedLM->toString());

  } else {
    FixItHint Hint;
    if (DiagID == diag::warn_format_nonsensical_length)
      Hint = FixItHint::CreateRemoval(LMRange);

    EmitFormatDiagnostic(S.PDiag(DiagID) << LM.toString() << CS.toString(),
                         getLocationOfByte(LM.getStart()),
                         /*IsStringLocation*/true,
                         getSpecifierRange(startSpecifier, specifierLen),
                         Hint);
  }
}

void CheckFormatHandler::HandleNonStandardLengthModifier(
    const analyze_format_string::FormatSpecifier &FS,
    const char *startSpecifier, unsigned specifierLen) {
  using namespace analyze_format_string;

  const LengthModifier &LM = FS.getLengthModifier();
  CharSourceRange LMRange = getSpecifierRange(LM.getStart(), LM.getLength());

  // See if we know how to fix this length modifier.
  std::optional<LengthModifier> FixedLM = FS.getCorrectedLengthModifier();
  if (FixedLM) {
    EmitFormatDiagnostic(S.PDiag(diag::warn_format_non_standard)
                           << LM.toString() << 0,
                         getLocationOfByte(LM.getStart()),
                         /*IsStringLocation*/true,
                         getSpecifierRange(startSpecifier, specifierLen));

    S.Diag(getLocationOfByte(LM.getStart()), diag::note_format_fix_specifier)
      << FixedLM->toString()
      << FixItHint::CreateReplacement(LMRange, FixedLM->toString());

  } else {
    EmitFormatDiagnostic(S.PDiag(diag::warn_format_non_standard)
                           << LM.toString() << 0,
                         getLocationOfByte(LM.getStart()),
                         /*IsStringLocation*/true,
                         getSpecifierRange(startSpecifier, specifierLen));
  }
}

void CheckFormatHandler::HandleNonStandardConversionSpecifier(
    const analyze_format_string::ConversionSpecifier &CS,
    const char *startSpecifier, unsigned specifierLen) {
  using namespace analyze_format_string;

  // See if we know how to fix this conversion specifier.
  std::optional<ConversionSpecifier> FixedCS = CS.getStandardSpecifier();
  if (FixedCS) {
    EmitFormatDiagnostic(S.PDiag(diag::warn_format_non_standard)
                          << CS.toString() << /*conversion specifier*/1,
                         getLocationOfByte(CS.getStart()),
                         /*IsStringLocation*/true,
                         getSpecifierRange(startSpecifier, specifierLen));

    CharSourceRange CSRange = getSpecifierRange(CS.getStart(), CS.getLength());
    S.Diag(getLocationOfByte(CS.getStart()), diag::note_format_fix_specifier)
      << FixedCS->toString()
      << FixItHint::CreateReplacement(CSRange, FixedCS->toString());
  } else {
    EmitFormatDiagnostic(S.PDiag(diag::warn_format_non_standard)
                          << CS.toString() << /*conversion specifier*/1,
                         getLocationOfByte(CS.getStart()),
                         /*IsStringLocation*/true,
                         getSpecifierRange(startSpecifier, specifierLen));
  }
}

void CheckFormatHandler::HandlePosition(const char *startPos,
                                        unsigned posLen) {
  EmitFormatDiagnostic(S.PDiag(diag::warn_format_non_standard_positional_arg),
                               getLocationOfByte(startPos),
                               /*IsStringLocation*/true,
                               getSpecifierRange(startPos, posLen));
}

void CheckFormatHandler::HandleInvalidPosition(
    const char *startSpecifier, unsigned specifierLen,
    analyze_format_string::PositionContext p) {
  EmitFormatDiagnostic(
      S.PDiag(diag::warn_format_invalid_positional_specifier) << (unsigned)p,
      getLocationOfByte(startSpecifier), /*IsStringLocation*/ true,
      getSpecifierRange(startSpecifier, specifierLen));
}

void CheckFormatHandler::HandleZeroPosition(const char *startPos,
                                            unsigned posLen) {
  EmitFormatDiagnostic(S.PDiag(diag::warn_format_zero_positional_specifier),
                               getLocationOfByte(startPos),
                               /*IsStringLocation*/true,
                               getSpecifierRange(startPos, posLen));
}

void CheckFormatHandler::HandleNullChar(const char *nullCharacter) {
  if (!isa<ObjCStringLiteral>(OrigFormatExpr)) {
    // The presence of a null character is likely an error.
    EmitFormatDiagnostic(
      S.PDiag(diag::warn_printf_format_string_contains_null_char),
      getLocationOfByte(nullCharacter), /*IsStringLocation*/true,
      getFormatStringRange());
  }
}

// Note that this may return NULL if there was an error parsing or building
// one of the argument expressions.
const Expr *CheckFormatHandler::getDataArg(unsigned i) const {
  return Args[FirstDataArg + i];
}

void CheckFormatHandler::DoneProcessing() {
  // Does the number of data arguments exceed the number of
  // format conversions in the format string?
  if (ArgPassingKind != Sema::FAPK_VAList) {
    // Find any arguments that weren't covered.
    CoveredArgs.flip();
    signed notCoveredArg = CoveredArgs.find_first();
    if (notCoveredArg >= 0) {
      assert((unsigned)notCoveredArg < NumDataArgs);
      UncoveredArg.Update(notCoveredArg, OrigFormatExpr);
    } else {
      UncoveredArg.setAllCovered();
    }
  }
}

void UncoveredArgHandler::Diagnose(Sema &S, bool IsFunctionCall,
                                   const Expr *ArgExpr) {
  assert(hasUncoveredArg() && !DiagnosticExprs.empty() &&
         "Invalid state");

  if (!ArgExpr)
    return;

  SourceLocation Loc = ArgExpr->getBeginLoc();

  if (S.getSourceManager().isInSystemMacro(Loc))
    return;

  PartialDiagnostic PDiag = S.PDiag(diag::warn_printf_data_arg_not_used);
  for (auto E : DiagnosticExprs)
    PDiag << E->getSourceRange();

  CheckFormatHandler::EmitFormatDiagnostic(
                                  S, IsFunctionCall, DiagnosticExprs[0],
                                  PDiag, Loc, /*IsStringLocation*/false,
                                  DiagnosticExprs[0]->getSourceRange());
}

bool
CheckFormatHandler::HandleInvalidConversionSpecifier(unsigned argIndex,
                                                     SourceLocation Loc,
                                                     const char *startSpec,
                                                     unsigned specifierLen,
                                                     const char *csStart,
                                                     unsigned csLen) {
  bool keepGoing = true;
  if (argIndex < NumDataArgs) {
    // Consider the argument coverered, even though the specifier doesn't
    // make sense.
    CoveredArgs.set(argIndex);
  }
  else {
    // If argIndex exceeds the number of data arguments we
    // don't issue a warning because that is just a cascade of warnings (and
    // they may have intended '%%' anyway). We don't want to continue processing
    // the format string after this point, however, as we will like just get
    // gibberish when trying to match arguments.
    keepGoing = false;
  }

  StringRef Specifier(csStart, csLen);

  // If the specifier in non-printable, it could be the first byte of a UTF-8
  // sequence. In that case, print the UTF-8 code point. If not, print the byte
  // hex value.
  std::string CodePointStr;
  if (!llvm::sys::locale::isPrint(*csStart)) {
    llvm::UTF32 CodePoint;
    const llvm::UTF8 **B = reinterpret_cast<const llvm::UTF8 **>(&csStart);
    const llvm::UTF8 *E =
        reinterpret_cast<const llvm::UTF8 *>(csStart + csLen);
    llvm::ConversionResult Result =
        llvm::convertUTF8Sequence(B, E, &CodePoint, llvm::strictConversion);

    if (Result != llvm::conversionOK) {
      unsigned char FirstChar = *csStart;
      CodePoint = (llvm::UTF32)FirstChar;
    }

    llvm::raw_string_ostream OS(CodePointStr);
    if (CodePoint < 256)
      OS << "\\x" << llvm::format("%02x", CodePoint);
    else if (CodePoint <= 0xFFFF)
      OS << "\\u" << llvm::format("%04x", CodePoint);
    else
      OS << "\\U" << llvm::format("%08x", CodePoint);
    OS.flush();
    Specifier = CodePointStr;
  }

  EmitFormatDiagnostic(
      S.PDiag(diag::warn_format_invalid_conversion) << Specifier, Loc,
      /*IsStringLocation*/ true, getSpecifierRange(startSpec, specifierLen));

  return keepGoing;
}

void
CheckFormatHandler::HandlePositionalNonpositionalArgs(SourceLocation Loc,
                                                      const char *startSpec,
                                                      unsigned specifierLen) {
  EmitFormatDiagnostic(
    S.PDiag(diag::warn_format_mix_positional_nonpositional_args),
    Loc, /*isStringLoc*/true, getSpecifierRange(startSpec, specifierLen));
}

bool
CheckFormatHandler::CheckNumArgs(
  const analyze_format_string::FormatSpecifier &FS,
  const analyze_format_string::ConversionSpecifier &CS,
  const char *startSpecifier, unsigned specifierLen, unsigned argIndex) {

  if (argIndex >= NumDataArgs) {
    PartialDiagnostic PDiag = FS.usesPositionalArg()
      ? (S.PDiag(diag::warn_printf_positional_arg_exceeds_data_args)
           << (argIndex+1) << NumDataArgs)
      : S.PDiag(diag::warn_printf_insufficient_data_args);
    EmitFormatDiagnostic(
      PDiag, getLocationOfByte(CS.getStart()), /*IsStringLocation*/true,
      getSpecifierRange(startSpecifier, specifierLen));

    // Since more arguments than conversion tokens are given, by extension
    // all arguments are covered, so mark this as so.
    UncoveredArg.setAllCovered();
    return false;
  }
  return true;
}

template<typename Range>
void CheckFormatHandler::EmitFormatDiagnostic(PartialDiagnostic PDiag,
                                              SourceLocation Loc,
                                              bool IsStringLocation,
                                              Range StringRange,
                                              ArrayRef<FixItHint> FixIt) {
  EmitFormatDiagnostic(S, inFunctionCall, Args[FormatIdx], PDiag,
                       Loc, IsStringLocation, StringRange, FixIt);
}

/// If the format string is not within the function call, emit a note
/// so that the function call and string are in diagnostic messages.
///
/// \param InFunctionCall if true, the format string is within the function
/// call and only one diagnostic message will be produced.  Otherwise, an
/// extra note will be emitted pointing to location of the format string.
///
/// \param ArgumentExpr the expression that is passed as the format string
/// argument in the function call.  Used for getting locations when two
/// diagnostics are emitted.
///
/// \param PDiag the callee should already have provided any strings for the
/// diagnostic message.  This function only adds locations and fixits
/// to diagnostics.
///
/// \param Loc primary location for diagnostic.  If two diagnostics are
/// required, one will be at Loc and a new SourceLocation will be created for
/// the other one.
///
/// \param IsStringLocation if true, Loc points to the format string should be
/// used for the note.  Otherwise, Loc points to the argument list and will
/// be used with PDiag.
///
/// \param StringRange some or all of the string to highlight.  This is
/// templated so it can accept either a CharSourceRange or a SourceRange.
///
/// \param FixIt optional fix it hint for the format string.
template <typename Range>
void CheckFormatHandler::EmitFormatDiagnostic(
    Sema &S, bool InFunctionCall, const Expr *ArgumentExpr,
    const PartialDiagnostic &PDiag, SourceLocation Loc, bool IsStringLocation,
    Range StringRange, ArrayRef<FixItHint> FixIt) {
  if (InFunctionCall) {
    const Sema::SemaDiagnosticBuilder &D = S.Diag(Loc, PDiag);
    D << StringRange;
    D << FixIt;
  } else {
    S.Diag(IsStringLocation ? ArgumentExpr->getExprLoc() : Loc, PDiag)
      << ArgumentExpr->getSourceRange();

    const Sema::SemaDiagnosticBuilder &Note =
      S.Diag(IsStringLocation ? Loc : StringRange.getBegin(),
             diag::note_format_string_defined);

    Note << StringRange;
    Note << FixIt;
  }
}

//===--- CHECK: Printf format string checking -----------------------------===//

namespace {

class CheckPrintfHandler : public CheckFormatHandler {
public:
  CheckPrintfHandler(Sema &s, const FormatStringLiteral *fexpr,
                     const Expr *origFormatExpr,
                     const Sema::FormatStringType type, unsigned firstDataArg,
                     unsigned numDataArgs, bool isObjC, const char *beg,
                     Sema::FormatArgumentPassingKind APK,
                     ArrayRef<const Expr *> Args, unsigned formatIdx,
                     bool inFunctionCall, Sema::VariadicCallType CallType,
                     llvm::SmallBitVector &CheckedVarArgs,
                     UncoveredArgHandler &UncoveredArg)
      : CheckFormatHandler(s, fexpr, origFormatExpr, type, firstDataArg,
                           numDataArgs, beg, APK, Args, formatIdx,
                           inFunctionCall, CallType, CheckedVarArgs,
                           UncoveredArg) {}

  bool isObjCContext() const { return FSType == Sema::FST_NSString; }

  /// Returns true if '%@' specifiers are allowed in the format string.
  bool allowsObjCArg() const {
    return FSType == Sema::FST_NSString || FSType == Sema::FST_OSLog ||
           FSType == Sema::FST_OSTrace;
  }

  bool HandleInvalidPrintfConversionSpecifier(
                                      const analyze_printf::PrintfSpecifier &FS,
                                      const char *startSpecifier,
                                      unsigned specifierLen) override;

  void handleInvalidMaskType(StringRef MaskType) override;

  bool HandlePrintfSpecifier(const analyze_printf::PrintfSpecifier &FS,
                             const char *startSpecifier, unsigned specifierLen,
                             const TargetInfo &Target) override;
  bool checkFormatExpr(const analyze_printf::PrintfSpecifier &FS,
                       const char *StartSpecifier,
                       unsigned SpecifierLen,
                       const Expr *E);

  bool HandleAmount(const analyze_format_string::OptionalAmount &Amt, unsigned k,
                    const char *startSpecifier, unsigned specifierLen);
  void HandleInvalidAmount(const analyze_printf::PrintfSpecifier &FS,
                           const analyze_printf::OptionalAmount &Amt,
                           unsigned type,
                           const char *startSpecifier, unsigned specifierLen);
  void HandleFlag(const analyze_printf::PrintfSpecifier &FS,
                  const analyze_printf::OptionalFlag &flag,
                  const char *startSpecifier, unsigned specifierLen);
  void HandleIgnoredFlag(const analyze_printf::PrintfSpecifier &FS,
                         const analyze_printf::OptionalFlag &ignoredFlag,
                         const analyze_printf::OptionalFlag &flag,
                         const char *startSpecifier, unsigned specifierLen);
  bool checkForCStrMembers(const analyze_printf::ArgType &AT,
                           const Expr *E);

  void HandleEmptyObjCModifierFlag(const char *startFlag,
                                   unsigned flagLen) override;

  void HandleInvalidObjCModifierFlag(const char *startFlag,
                                            unsigned flagLen) override;

  void HandleObjCFlagsWithNonObjCConversion(const char *flagsStart,
                                           const char *flagsEnd,
                                           const char *conversionPosition)
                                             override;
};

} // namespace

bool CheckPrintfHandler::HandleInvalidPrintfConversionSpecifier(
                                      const analyze_printf::PrintfSpecifier &FS,
                                      const char *startSpecifier,
                                      unsigned specifierLen) {
  const analyze_printf::PrintfConversionSpecifier &CS =
    FS.getConversionSpecifier();

  return HandleInvalidConversionSpecifier(FS.getArgIndex(),
                                          getLocationOfByte(CS.getStart()),
                                          startSpecifier, specifierLen,
                                          CS.getStart(), CS.getLength());
}

void CheckPrintfHandler::handleInvalidMaskType(StringRef MaskType) {
  S.Diag(getLocationOfByte(MaskType.data()), diag::err_invalid_mask_type_size);
}

bool CheckPrintfHandler::HandleAmount(
    const analyze_format_string::OptionalAmount &Amt, unsigned k,
    const char *startSpecifier, unsigned specifierLen) {
  if (Amt.hasDataArgument()) {
    if (ArgPassingKind != Sema::FAPK_VAList) {
      unsigned argIndex = Amt.getArgIndex();
      if (argIndex >= NumDataArgs) {
        EmitFormatDiagnostic(S.PDiag(diag::warn_printf_asterisk_missing_arg)
                                 << k,
                             getLocationOfByte(Amt.getStart()),
                             /*IsStringLocation*/ true,
                             getSpecifierRange(startSpecifier, specifierLen));
        // Don't do any more checking.  We will just emit
        // spurious errors.
        return false;
      }

      // Type check the data argument.  It should be an 'int'.
      // Although not in conformance with C99, we also allow the argument to be
      // an 'unsigned int' as that is a reasonably safe case.  GCC also
      // doesn't emit a warning for that case.
      CoveredArgs.set(argIndex);
      const Expr *Arg = getDataArg(argIndex);
      if (!Arg)
        return false;

      QualType T = Arg->getType();

      const analyze_printf::ArgType &AT = Amt.getArgType(S.Context);
      assert(AT.isValid());

      if (!AT.matchesType(S.Context, T)) {
        EmitFormatDiagnostic(S.PDiag(diag::warn_printf_asterisk_wrong_type)
                               << k << AT.getRepresentativeTypeName(S.Context)
                               << T << Arg->getSourceRange(),
                             getLocationOfByte(Amt.getStart()),
                             /*IsStringLocation*/true,
                             getSpecifierRange(startSpecifier, specifierLen));
        // Don't do any more checking.  We will just emit
        // spurious errors.
        return false;
      }
    }
  }
  return true;
}

void CheckPrintfHandler::HandleInvalidAmount(
                                      const analyze_printf::PrintfSpecifier &FS,
                                      const analyze_printf::OptionalAmount &Amt,
                                      unsigned type,
                                      const char *startSpecifier,
                                      unsigned specifierLen) {
  const analyze_printf::PrintfConversionSpecifier &CS =
    FS.getConversionSpecifier();

  FixItHint fixit =
    Amt.getHowSpecified() == analyze_printf::OptionalAmount::Constant
      ? FixItHint::CreateRemoval(getSpecifierRange(Amt.getStart(),
                                 Amt.getConstantLength()))
      : FixItHint();

  EmitFormatDiagnostic(S.PDiag(diag::warn_printf_nonsensical_optional_amount)
                         << type << CS.toString(),
                       getLocationOfByte(Amt.getStart()),
                       /*IsStringLocation*/true,
                       getSpecifierRange(startSpecifier, specifierLen),
                       fixit);
}

void CheckPrintfHandler::HandleFlag(const analyze_printf::PrintfSpecifier &FS,
                                    const analyze_printf::OptionalFlag &flag,
                                    const char *startSpecifier,
                                    unsigned specifierLen) {
  // Warn about pointless flag with a fixit removal.
  const analyze_printf::PrintfConversionSpecifier &CS =
    FS.getConversionSpecifier();
  EmitFormatDiagnostic(S.PDiag(diag::warn_printf_nonsensical_flag)
                         << flag.toString() << CS.toString(),
                       getLocationOfByte(flag.getPosition()),
                       /*IsStringLocation*/true,
                       getSpecifierRange(startSpecifier, specifierLen),
                       FixItHint::CreateRemoval(
                         getSpecifierRange(flag.getPosition(), 1)));
}

void CheckPrintfHandler::HandleIgnoredFlag(
                                const analyze_printf::PrintfSpecifier &FS,
                                const analyze_printf::OptionalFlag &ignoredFlag,
                                const analyze_printf::OptionalFlag &flag,
                                const char *startSpecifier,
                                unsigned specifierLen) {
  // Warn about ignored flag with a fixit removal.
  EmitFormatDiagnostic(S.PDiag(diag::warn_printf_ignored_flag)
                         << ignoredFlag.toString() << flag.toString(),
                       getLocationOfByte(ignoredFlag.getPosition()),
                       /*IsStringLocation*/true,
                       getSpecifierRange(startSpecifier, specifierLen),
                       FixItHint::CreateRemoval(
                         getSpecifierRange(ignoredFlag.getPosition(), 1)));
}

void CheckPrintfHandler::HandleEmptyObjCModifierFlag(const char *startFlag,
                                                     unsigned flagLen) {
  // Warn about an empty flag.
  EmitFormatDiagnostic(S.PDiag(diag::warn_printf_empty_objc_flag),
                       getLocationOfByte(startFlag),
                       /*IsStringLocation*/true,
                       getSpecifierRange(startFlag, flagLen));
}

void CheckPrintfHandler::HandleInvalidObjCModifierFlag(const char *startFlag,
                                                       unsigned flagLen) {
  // Warn about an invalid flag.
  auto Range = getSpecifierRange(startFlag, flagLen);
  StringRef flag(startFlag, flagLen);
  EmitFormatDiagnostic(S.PDiag(diag::warn_printf_invalid_objc_flag) << flag,
                      getLocationOfByte(startFlag),
                      /*IsStringLocation*/true,
                      Range, FixItHint::CreateRemoval(Range));
}

void CheckPrintfHandler::HandleObjCFlagsWithNonObjCConversion(
    const char *flagsStart, const char *flagsEnd, const char *conversionPosition) {
    // Warn about using '[...]' without a '@' conversion.
    auto Range = getSpecifierRange(flagsStart, flagsEnd - flagsStart + 1);
    auto diag = diag::warn_printf_ObjCflags_without_ObjCConversion;
    EmitFormatDiagnostic(S.PDiag(diag) << StringRef(conversionPosition, 1),
                         getLocationOfByte(conversionPosition),
                         /*IsStringLocation*/true,
                         Range, FixItHint::CreateRemoval(Range));
}

// Determines if the specified is a C++ class or struct containing
// a member with the specified name and kind (e.g. a CXXMethodDecl named
// "c_str()").
template<typename MemberKind>
static llvm::SmallPtrSet<MemberKind*, 1>
CXXRecordMembersNamed(StringRef Name, Sema &S, QualType Ty) {
  const RecordType *RT = Ty->getAs<RecordType>();
  llvm::SmallPtrSet<MemberKind*, 1> Results;

  if (!RT)
    return Results;
  const CXXRecordDecl *RD = dyn_cast<CXXRecordDecl>(RT->getDecl());
  if (!RD || !RD->getDefinition())
    return Results;

  LookupResult R(S, &S.Context.Idents.get(Name), SourceLocation(),
                 Sema::LookupMemberName);
  R.suppressDiagnostics();

  // We just need to include all members of the right kind turned up by the
  // filter, at this point.
  if (S.LookupQualifiedName(R, RT->getDecl()))
    for (LookupResult::iterator I = R.begin(), E = R.end(); I != E; ++I) {
      NamedDecl *decl = (*I)->getUnderlyingDecl();
      if (MemberKind *FK = dyn_cast<MemberKind>(decl))
        Results.insert(FK);
    }
  return Results;
}

/// Check if we could call '.c_str()' on an object.
///
/// FIXME: This returns the wrong results in some cases (if cv-qualifiers don't
/// allow the call, or if it would be ambiguous).
bool Sema::hasCStrMethod(const Expr *E) {
  using MethodSet = llvm::SmallPtrSet<CXXMethodDecl *, 1>;

  MethodSet Results =
      CXXRecordMembersNamed<CXXMethodDecl>("c_str", *this, E->getType());
  for (MethodSet::iterator MI = Results.begin(), ME = Results.end();
       MI != ME; ++MI)
    if ((*MI)->getMinRequiredArguments() == 0)
      return true;
  return false;
}

// Check if a (w)string was passed when a (w)char* was needed, and offer a
// better diagnostic if so. AT is assumed to be valid.
// Returns true when a c_str() conversion method is found.
bool CheckPrintfHandler::checkForCStrMembers(
    const analyze_printf::ArgType &AT, const Expr *E) {
  using MethodSet = llvm::SmallPtrSet<CXXMethodDecl *, 1>;

  MethodSet Results =
      CXXRecordMembersNamed<CXXMethodDecl>("c_str", S, E->getType());

  for (MethodSet::iterator MI = Results.begin(), ME = Results.end();
       MI != ME; ++MI) {
    const CXXMethodDecl *Method = *MI;
    if (Method->getMinRequiredArguments() == 0 &&
        AT.matchesType(S.Context, Method->getReturnType())) {
      // FIXME: Suggest parens if the expression needs them.
      SourceLocation EndLoc = S.getLocForEndOfToken(E->getEndLoc());
      S.Diag(E->getBeginLoc(), diag::note_printf_c_str)
          << "c_str()" << FixItHint::CreateInsertion(EndLoc, ".c_str()");
      return true;
    }
  }

  return false;
}

bool CheckPrintfHandler::HandlePrintfSpecifier(
    const analyze_printf::PrintfSpecifier &FS, const char *startSpecifier,
    unsigned specifierLen, const TargetInfo &Target) {
  using namespace analyze_format_string;
  using namespace analyze_printf;

  const PrintfConversionSpecifier &CS = FS.getConversionSpecifier();

  if (FS.consumesDataArgument()) {
    if (atFirstArg) {
        atFirstArg = false;
        usesPositionalArgs = FS.usesPositionalArg();
    }
    else if (usesPositionalArgs != FS.usesPositionalArg()) {
      HandlePositionalNonpositionalArgs(getLocationOfByte(CS.getStart()),
                                        startSpecifier, specifierLen);
      return false;
    }
  }

  // First check if the field width, precision, and conversion specifier
  // have matching data arguments.
  if (!HandleAmount(FS.getFieldWidth(), /* field width */ 0,
                    startSpecifier, specifierLen)) {
    return false;
  }

  if (!HandleAmount(FS.getPrecision(), /* precision */ 1,
                    startSpecifier, specifierLen)) {
    return false;
  }

  if (!CS.consumesDataArgument()) {
    // FIXME: Technically specifying a precision or field width here
    // makes no sense.  Worth issuing a warning at some point.
    return true;
  }

  // Consume the argument.
  unsigned argIndex = FS.getArgIndex();
  if (argIndex < NumDataArgs) {
    // The check to see if the argIndex is valid will come later.
    // We set the bit here because we may exit early from this
    // function if we encounter some other error.
    CoveredArgs.set(argIndex);
  }

  // FreeBSD kernel extensions.
  if (CS.getKind() == ConversionSpecifier::FreeBSDbArg ||
      CS.getKind() == ConversionSpecifier::FreeBSDDArg) {
    // We need at least two arguments.
    if (!CheckNumArgs(FS, CS, startSpecifier, specifierLen, argIndex + 1))
      return false;

    // Claim the second argument.
    CoveredArgs.set(argIndex + 1);

    const Expr *Ex = getDataArg(argIndex);
    if (CS.getKind() == ConversionSpecifier::FreeBSDDArg) {
      // Type check the first argument (pointer for %D)
      const analyze_printf::ArgType &AT = ArgType::CPointerTy;
      if (AT.isValid() && !AT.matchesType(S.Context, Ex->getType()))
        EmitFormatDiagnostic(
          S.PDiag(diag::warn_format_conversion_argument_type_mismatch)
          << AT.getRepresentativeTypeName(S.Context) << Ex->getType()
          << false << Ex->getSourceRange(),
          Ex->getBeginLoc(), /*IsStringLocation*/false,
          getSpecifierRange(startSpecifier, specifierLen));
    } else {
      // Check the length modifier for %b
      if (!FS.hasValidLengthModifier(S.getASTContext().getTargetInfo(),
                                     S.getLangOpts()))
        HandleInvalidLengthModifier(FS, CS, startSpecifier, specifierLen,
                                    diag::warn_format_nonsensical_length);
      else if (!FS.hasStandardLengthModifier())
        HandleNonStandardLengthModifier(FS, startSpecifier, specifierLen);
      else if (!FS.hasStandardLengthConversionCombination())
        HandleInvalidLengthModifier(FS, CS, startSpecifier, specifierLen,
                                    diag::warn_format_non_standard_conversion_spec);

      // Type check the first argument of %b
      if (!checkFormatExpr(FS, startSpecifier, specifierLen, Ex))
        return false;
    }

    // Type check the second argument (char * for both %b and %D)
    Ex = getDataArg(argIndex + 1);
    const analyze_printf::ArgType &AT2 = ArgType::CStrTy;
    if (AT2.isValid() && !AT2.matchesType(S.Context, Ex->getType()))
      EmitFormatDiagnostic(
          S.PDiag(diag::warn_format_conversion_argument_type_mismatch)
              << AT2.getRepresentativeTypeName(S.Context) << Ex->getType()
              << false << Ex->getSourceRange(),
          Ex->getBeginLoc(), /*IsStringLocation*/ false,
          getSpecifierRange(startSpecifier, specifierLen));

     return true;
  }

  // Check for using an Objective-C specific conversion specifier
  // in a non-ObjC literal.
  if (!allowsObjCArg() && CS.isObjCArg()) {
    return HandleInvalidPrintfConversionSpecifier(FS, startSpecifier,
                                                  specifierLen);
  }

  // %P can only be used with os_log.
  if (FSType != Sema::FST_OSLog && CS.getKind() == ConversionSpecifier::PArg) {
    return HandleInvalidPrintfConversionSpecifier(FS, startSpecifier,
                                                  specifierLen);
  }

  // %n is not allowed with os_log.
  if (FSType == Sema::FST_OSLog && CS.getKind() == ConversionSpecifier::nArg) {
    EmitFormatDiagnostic(S.PDiag(diag::warn_os_log_format_narg),
                         getLocationOfByte(CS.getStart()),
                         /*IsStringLocation*/ false,
                         getSpecifierRange(startSpecifier, specifierLen));

    return true;
  }

  // %n is not allowed anywhere
  if (CS.getKind() == ConversionSpecifier::nArg) {
    EmitFormatDiagnostic(S.PDiag(diag::warn_format_narg),
                         getLocationOfByte(CS.getStart()),
                         /*IsStringLocation*/ false,
                         getSpecifierRange(startSpecifier, specifierLen));
    return true;
  }

  // Only scalars are allowed for os_trace.
  if (FSType == Sema::FST_OSTrace &&
      (CS.getKind() == ConversionSpecifier::PArg ||
       CS.getKind() == ConversionSpecifier::sArg ||
       CS.getKind() == ConversionSpecifier::ObjCObjArg)) {
    return HandleInvalidPrintfConversionSpecifier(FS, startSpecifier,
                                                  specifierLen);
  }

  // Check for use of public/private annotation outside of os_log().
  if (FSType != Sema::FST_OSLog) {
    if (FS.isPublic().isSet()) {
      EmitFormatDiagnostic(S.PDiag(diag::warn_format_invalid_annotation)
                               << "public",
                           getLocationOfByte(FS.isPublic().getPosition()),
                           /*IsStringLocation*/ false,
                           getSpecifierRange(startSpecifier, specifierLen));
    }
    if (FS.isPrivate().isSet()) {
      EmitFormatDiagnostic(S.PDiag(diag::warn_format_invalid_annotation)
                               << "private",
                           getLocationOfByte(FS.isPrivate().getPosition()),
                           /*IsStringLocation*/ false,
                           getSpecifierRange(startSpecifier, specifierLen));
    }
  }

  const llvm::Triple &Triple = Target.getTriple();
  if (CS.getKind() == ConversionSpecifier::nArg &&
      (Triple.isAndroid() || Triple.isOSFuchsia())) {
    EmitFormatDiagnostic(S.PDiag(diag::warn_printf_narg_not_supported),
                         getLocationOfByte(CS.getStart()),
                         /*IsStringLocation*/ false,
                         getSpecifierRange(startSpecifier, specifierLen));
  }

  // Check for invalid use of field width
  if (!FS.hasValidFieldWidth()) {
    HandleInvalidAmount(FS, FS.getFieldWidth(), /* field width */ 0,
        startSpecifier, specifierLen);
  }

  // Check for invalid use of precision
  if (!FS.hasValidPrecision()) {
    HandleInvalidAmount(FS, FS.getPrecision(), /* precision */ 1,
        startSpecifier, specifierLen);
  }

  // Precision is mandatory for %P specifier.
  if (CS.getKind() == ConversionSpecifier::PArg &&
      FS.getPrecision().getHowSpecified() == OptionalAmount::NotSpecified) {
    EmitFormatDiagnostic(S.PDiag(diag::warn_format_P_no_precision),
                         getLocationOfByte(startSpecifier),
                         /*IsStringLocation*/ false,
                         getSpecifierRange(startSpecifier, specifierLen));
  }

  // Check each flag does not conflict with any other component.
  if (!FS.hasValidThousandsGroupingPrefix())
    HandleFlag(FS, FS.hasThousandsGrouping(), startSpecifier, specifierLen);
  if (!FS.hasValidLeadingZeros())
    HandleFlag(FS, FS.hasLeadingZeros(), startSpecifier, specifierLen);
  if (!FS.hasValidPlusPrefix())
    HandleFlag(FS, FS.hasPlusPrefix(), startSpecifier, specifierLen);
  if (!FS.hasValidSpacePrefix())
    HandleFlag(FS, FS.hasSpacePrefix(), startSpecifier, specifierLen);
  if (!FS.hasValidAlternativeForm())
    HandleFlag(FS, FS.hasAlternativeForm(), startSpecifier, specifierLen);
  if (!FS.hasValidLeftJustified())
    HandleFlag(FS, FS.isLeftJustified(), startSpecifier, specifierLen);

  // Check that flags are not ignored by another flag
  if (FS.hasSpacePrefix() && FS.hasPlusPrefix()) // ' ' ignored by '+'
    HandleIgnoredFlag(FS, FS.hasSpacePrefix(), FS.hasPlusPrefix(),
        startSpecifier, specifierLen);
  if (FS.hasLeadingZeros() && FS.isLeftJustified()) // '0' ignored by '-'
    HandleIgnoredFlag(FS, FS.hasLeadingZeros(), FS.isLeftJustified(),
            startSpecifier, specifierLen);

  // Check the length modifier is valid with the given conversion specifier.
  if (!FS.hasValidLengthModifier(S.getASTContext().getTargetInfo(),
                                 S.getLangOpts()))
    HandleInvalidLengthModifier(FS, CS, startSpecifier, specifierLen,
                                diag::warn_format_nonsensical_length);
  else if (!FS.hasStandardLengthModifier())
    HandleNonStandardLengthModifier(FS, startSpecifier, specifierLen);
  else if (!FS.hasStandardLengthConversionCombination())
    HandleInvalidLengthModifier(FS, CS, startSpecifier, specifierLen,
                                diag::warn_format_non_standard_conversion_spec);

  if (!FS.hasStandardConversionSpecifier(S.getLangOpts()))
    HandleNonStandardConversionSpecifier(CS, startSpecifier, specifierLen);

  // The remaining checks depend on the data arguments.
  if (ArgPassingKind == Sema::FAPK_VAList)
    return true;

  if (!CheckNumArgs(FS, CS, startSpecifier, specifierLen, argIndex))
    return false;

  const Expr *Arg = getDataArg(argIndex);
  if (!Arg)
    return true;

  return checkFormatExpr(FS, startSpecifier, specifierLen, Arg);
}

static bool requiresParensToAddCast(const Expr *E) {
  // FIXME: We should have a general way to reason about operator
  // precedence and whether parens are actually needed here.
  // Take care of a few common cases where they aren't.
  const Expr *Inside = E->IgnoreImpCasts();
  if (const PseudoObjectExpr *POE = dyn_cast<PseudoObjectExpr>(Inside))
    Inside = POE->getSyntacticForm()->IgnoreImpCasts();

  switch (Inside->getStmtClass()) {
  case Stmt::ArraySubscriptExprClass:
  case Stmt::CallExprClass:
  case Stmt::CharacterLiteralClass:
  case Stmt::CXXBoolLiteralExprClass:
  case Stmt::DeclRefExprClass:
  case Stmt::FloatingLiteralClass:
  case Stmt::IntegerLiteralClass:
  case Stmt::MemberExprClass:
  case Stmt::ObjCArrayLiteralClass:
  case Stmt::ObjCBoolLiteralExprClass:
  case Stmt::ObjCBoxedExprClass:
  case Stmt::ObjCDictionaryLiteralClass:
  case Stmt::ObjCEncodeExprClass:
  case Stmt::ObjCIvarRefExprClass:
  case Stmt::ObjCMessageExprClass:
  case Stmt::ObjCPropertyRefExprClass:
  case Stmt::ObjCStringLiteralClass:
  case Stmt::ObjCSubscriptRefExprClass:
  case Stmt::ParenExprClass:
  case Stmt::StringLiteralClass:
  case Stmt::UnaryOperatorClass:
    return false;
  default:
    return true;
  }
}

static std::pair<QualType, StringRef>
shouldNotPrintDirectly(const ASTContext &Context,
                       QualType IntendedTy,
                       const Expr *E) {
  // Use a 'while' to peel off layers of typedefs.
  QualType TyTy = IntendedTy;
  while (const TypedefType *UserTy = TyTy->getAs<TypedefType>()) {
    StringRef Name = UserTy->getDecl()->getName();
    QualType CastTy = llvm::StringSwitch<QualType>(Name)
      .Case("CFIndex", Context.getNSIntegerType())
      .Case("NSInteger", Context.getNSIntegerType())
      .Case("NSUInteger", Context.getNSUIntegerType())
      .Case("SInt32", Context.IntTy)
      .Case("UInt32", Context.UnsignedIntTy)
      .Default(QualType());

    if (!CastTy.isNull())
      return std::make_pair(CastTy, Name);

    TyTy = UserTy->desugar();
  }

  // Strip parens if necessary.
  if (const ParenExpr *PE = dyn_cast<ParenExpr>(E))
    return shouldNotPrintDirectly(Context,
                                  PE->getSubExpr()->getType(),
                                  PE->getSubExpr());

  // If this is a conditional expression, then its result type is constructed
  // via usual arithmetic conversions and thus there might be no necessary
  // typedef sugar there.  Recurse to operands to check for NSInteger &
  // Co. usage condition.
  if (const ConditionalOperator *CO = dyn_cast<ConditionalOperator>(E)) {
    QualType TrueTy, FalseTy;
    StringRef TrueName, FalseName;

    std::tie(TrueTy, TrueName) =
      shouldNotPrintDirectly(Context,
                             CO->getTrueExpr()->getType(),
                             CO->getTrueExpr());
    std::tie(FalseTy, FalseName) =
      shouldNotPrintDirectly(Context,
                             CO->getFalseExpr()->getType(),
                             CO->getFalseExpr());

    if (TrueTy == FalseTy)
      return std::make_pair(TrueTy, TrueName);
    else if (TrueTy.isNull())
      return std::make_pair(FalseTy, FalseName);
    else if (FalseTy.isNull())
      return std::make_pair(TrueTy, TrueName);
  }

  return std::make_pair(QualType(), StringRef());
}

/// Return true if \p ICE is an implicit argument promotion of an arithmetic
/// type. Bit-field 'promotions' from a higher ranked type to a lower ranked
/// type do not count.
static bool
isArithmeticArgumentPromotion(Sema &S, const ImplicitCastExpr *ICE) {
  QualType From = ICE->getSubExpr()->getType();
  QualType To = ICE->getType();
  // It's an integer promotion if the destination type is the promoted
  // source type.
  if (ICE->getCastKind() == CK_IntegralCast &&
      S.Context.isPromotableIntegerType(From) &&
      S.Context.getPromotedIntegerType(From) == To)
    return true;
  // Look through vector types, since we do default argument promotion for
  // those in OpenCL.
  if (const auto *VecTy = From->getAs<ExtVectorType>())
    From = VecTy->getElementType();
  if (const auto *VecTy = To->getAs<ExtVectorType>())
    To = VecTy->getElementType();
  // It's a floating promotion if the source type is a lower rank.
  return ICE->getCastKind() == CK_FloatingCast &&
         S.Context.getFloatingTypeOrder(From, To) < 0;
}

static analyze_format_string::ArgType::MatchKind
handleFormatSignedness(analyze_format_string::ArgType::MatchKind Match,
                       DiagnosticsEngine &Diags, SourceLocation Loc) {
  if (Match == analyze_format_string::ArgType::NoMatchSignedness) {
    Match =
        Diags.isIgnored(
            diag::warn_format_conversion_argument_type_mismatch_signedness, Loc)
            ? analyze_format_string::ArgType::Match
            : analyze_format_string::ArgType::NoMatch;
  }
  return Match;
}

bool
CheckPrintfHandler::checkFormatExpr(const analyze_printf::PrintfSpecifier &FS,
                                    const char *StartSpecifier,
                                    unsigned SpecifierLen,
                                    const Expr *E) {
  using namespace analyze_format_string;
  using namespace analyze_printf;

  // Now type check the data expression that matches the
  // format specifier.
  const analyze_printf::ArgType &AT = FS.getArgType(S.Context, isObjCContext());
  if (!AT.isValid())
    return true;

  QualType ExprTy = E->getType();
  while (const TypeOfExprType *TET = dyn_cast<TypeOfExprType>(ExprTy)) {
    ExprTy = TET->getUnderlyingExpr()->getType();
  }

  // When using the format attribute in C++, you can receive a function or an
  // array that will necessarily decay to a pointer when passed to the final
  // format consumer. Apply decay before type comparison.
  if (ExprTy->canDecayToPointerType())
    ExprTy = S.Context.getDecayedType(ExprTy);

  // Diagnose attempts to print a boolean value as a character. Unlike other
  // -Wformat diagnostics, this is fine from a type perspective, but it still
  // doesn't make sense.
  if (FS.getConversionSpecifier().getKind() == ConversionSpecifier::cArg &&
      E->isKnownToHaveBooleanValue()) {
    const CharSourceRange &CSR =
        getSpecifierRange(StartSpecifier, SpecifierLen);
    SmallString<4> FSString;
    llvm::raw_svector_ostream os(FSString);
    FS.toString(os);
    EmitFormatDiagnostic(S.PDiag(diag::warn_format_bool_as_character)
                             << FSString,
                         E->getExprLoc(), false, CSR);
    return true;
  }

  // Diagnose attempts to use '%P' with ObjC object types, which will result in
  // dumping raw class data (like is-a pointer), not actual data.
  if (FS.getConversionSpecifier().getKind() == ConversionSpecifier::PArg &&
      ExprTy->isObjCObjectPointerType()) {
    const CharSourceRange &CSR =
        getSpecifierRange(StartSpecifier, SpecifierLen);
    EmitFormatDiagnostic(S.PDiag(diag::warn_format_P_with_objc_pointer),
                         E->getExprLoc(), false, CSR);
    return true;
  }

  ArgType::MatchKind ImplicitMatch = ArgType::NoMatch;
  ArgType::MatchKind Match = AT.matchesType(S.Context, ExprTy);
  ArgType::MatchKind OrigMatch = Match;

  Match = handleFormatSignedness(Match, S.getDiagnostics(), E->getExprLoc());
  if (Match == ArgType::Match)
    return true;

  // NoMatchPromotionTypeConfusion should be only returned in ImplictCastExpr
  assert(Match != ArgType::NoMatchPromotionTypeConfusion);

  // Look through argument promotions for our error message's reported type.
  // This includes the integral and floating promotions, but excludes array
  // and function pointer decay (seeing that an argument intended to be a
  // string has type 'char [6]' is probably more confusing than 'char *') and
  // certain bitfield promotions (bitfields can be 'demoted' to a lesser type).
  if (const ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(E)) {
    if (isArithmeticArgumentPromotion(S, ICE)) {
      E = ICE->getSubExpr();
      ExprTy = E->getType();

      // Check if we didn't match because of an implicit cast from a 'char'
      // or 'short' to an 'int'.  This is done because printf is a varargs
      // function.
      if (ICE->getType() == S.Context.IntTy ||
          ICE->getType() == S.Context.UnsignedIntTy) {
        // All further checking is done on the subexpression
        ImplicitMatch = AT.matchesType(S.Context, ExprTy);
        if (OrigMatch == ArgType::NoMatchSignedness &&
            ImplicitMatch != ArgType::NoMatchSignedness)
          // If the original match was a signedness match this match on the
          // implicit cast type also need to be signedness match otherwise we
          // might introduce new unexpected warnings from -Wformat-signedness.
          return true;
        ImplicitMatch = handleFormatSignedness(
            ImplicitMatch, S.getDiagnostics(), E->getExprLoc());
        if (ImplicitMatch == ArgType::Match)
          return true;
      }
    }
  } else if (const CharacterLiteral *CL = dyn_cast<CharacterLiteral>(E)) {
    // Special case for 'a', which has type 'int' in C.
    // Note, however, that we do /not/ want to treat multibyte constants like
    // 'MooV' as characters! This form is deprecated but still exists. In
    // addition, don't treat expressions as of type 'char' if one byte length
    // modifier is provided.
    if (ExprTy == S.Context.IntTy &&
        FS.getLengthModifier().getKind() != LengthModifier::AsChar)
      if (llvm::isUIntN(S.Context.getCharWidth(), CL->getValue())) {
        ExprTy = S.Context.CharTy;
        // To improve check results, we consider a character literal in C
        // to be a 'char' rather than an 'int'. 'printf("%hd", 'a');' is
        // more likely a type confusion situation, so we will suggest to
        // use '%hhd' instead by discarding the MatchPromotion.
        if (Match == ArgType::MatchPromotion)
          Match = ArgType::NoMatch;
      }
  }
  if (Match == ArgType::MatchPromotion) {
    // WG14 N2562 only clarified promotions in *printf
    // For NSLog in ObjC, just preserve -Wformat behavior
    if (!S.getLangOpts().ObjC &&
        ImplicitMatch != ArgType::NoMatchPromotionTypeConfusion &&
        ImplicitMatch != ArgType::NoMatchTypeConfusion)
      return true;
    Match = ArgType::NoMatch;
  }
  if (ImplicitMatch == ArgType::NoMatchPedantic ||
      ImplicitMatch == ArgType::NoMatchTypeConfusion)
    Match = ImplicitMatch;
  assert(Match != ArgType::MatchPromotion);

  // Look through unscoped enums to their underlying type.
  bool IsEnum = false;
  bool IsScopedEnum = false;
  QualType IntendedTy = ExprTy;
  if (auto EnumTy = ExprTy->getAs<EnumType>()) {
    IntendedTy = EnumTy->getDecl()->getIntegerType();
    if (EnumTy->isUnscopedEnumerationType()) {
      ExprTy = IntendedTy;
      // This controls whether we're talking about the underlying type or not,
      // which we only want to do when it's an unscoped enum.
      IsEnum = true;
    } else {
      IsScopedEnum = true;
    }
  }

  // %C in an Objective-C context prints a unichar, not a wchar_t.
  // If the argument is an integer of some kind, believe the %C and suggest
  // a cast instead of changing the conversion specifier.
  if (isObjCContext() &&
      FS.getConversionSpecifier().getKind() == ConversionSpecifier::CArg) {
    if (ExprTy->isIntegralOrUnscopedEnumerationType() &&
        !ExprTy->isCharType()) {
      // 'unichar' is defined as a typedef of unsigned short, but we should
      // prefer using the typedef if it is visible.
      IntendedTy = S.Context.UnsignedShortTy;

      // While we are here, check if the value is an IntegerLiteral that happens
      // to be within the valid range.
      if (const IntegerLiteral *IL = dyn_cast<IntegerLiteral>(E)) {
        const llvm::APInt &V = IL->getValue();
        if (V.getActiveBits() <= S.Context.getTypeSize(IntendedTy))
          return true;
      }

      LookupResult Result(S, &S.Context.Idents.get("unichar"), E->getBeginLoc(),
                          Sema::LookupOrdinaryName);
      if (S.LookupName(Result, S.getCurScope())) {
        NamedDecl *ND = Result.getFoundDecl();
        if (TypedefNameDecl *TD = dyn_cast<TypedefNameDecl>(ND))
          if (TD->getUnderlyingType() == IntendedTy)
            IntendedTy = S.Context.getTypedefType(TD);
      }
    }
  }

  // Special-case some of Darwin's platform-independence types by suggesting
  // casts to primitive types that are known to be large enough.
  bool ShouldNotPrintDirectly = false; StringRef CastTyName;
  if (S.Context.getTargetInfo().getTriple().isOSDarwin()) {
    QualType CastTy;
    std::tie(CastTy, CastTyName) = shouldNotPrintDirectly(S.Context, IntendedTy, E);
    if (!CastTy.isNull()) {
      // %zi/%zu and %td/%tu are OK to use for NSInteger/NSUInteger of type int
      // (long in ASTContext). Only complain to pedants or when they're the
      // underlying type of a scoped enum (which always needs a cast).
      if (!IsScopedEnum &&
          (CastTyName == "NSInteger" || CastTyName == "NSUInteger") &&
          (AT.isSizeT() || AT.isPtrdiffT()) &&
          AT.matchesType(S.Context, CastTy))
        Match = ArgType::NoMatchPedantic;
      IntendedTy = CastTy;
      ShouldNotPrintDirectly = true;
    }
  }

  // We may be able to offer a FixItHint if it is a supported type.
  PrintfSpecifier fixedFS = FS;
  bool Success =
      fixedFS.fixType(IntendedTy, S.getLangOpts(), S.Context, isObjCContext());

  if (Success) {
    // Get the fix string from the fixed format specifier
    SmallString<16> buf;
    llvm::raw_svector_ostream os(buf);
    fixedFS.toString(os);

    CharSourceRange SpecRange = getSpecifierRange(StartSpecifier, SpecifierLen);

    if (IntendedTy == ExprTy && !ShouldNotPrintDirectly && !IsScopedEnum) {
      unsigned Diag;
      switch (Match) {
      case ArgType::Match:
      case ArgType::MatchPromotion:
      case ArgType::NoMatchPromotionTypeConfusion:
      case ArgType::NoMatchSignedness:
        llvm_unreachable("expected non-matching");
      case ArgType::NoMatchPedantic:
        Diag = diag::warn_format_conversion_argument_type_mismatch_pedantic;
        break;
      case ArgType::NoMatchTypeConfusion:
        Diag = diag::warn_format_conversion_argument_type_mismatch_confusion;
        break;
      case ArgType::NoMatch:
        Diag = diag::warn_format_conversion_argument_type_mismatch;
        break;
      }

      // In this case, the specifier is wrong and should be changed to match
      // the argument.
      EmitFormatDiagnostic(S.PDiag(Diag)
                               << AT.getRepresentativeTypeName(S.Context)
                               << IntendedTy << IsEnum << E->getSourceRange(),
                           E->getBeginLoc(),
                           /*IsStringLocation*/ false, SpecRange,
                           FixItHint::CreateReplacement(SpecRange, os.str()));
    } else {
      // The canonical type for formatting this value is different from the
      // actual type of the expression. (This occurs, for example, with Darwin's
      // NSInteger on 32-bit platforms, where it is typedef'd as 'int', but
      // should be printed as 'long' for 64-bit compatibility.)
      // Rather than emitting a normal format/argument mismatch, we want to
      // add a cast to the recommended type (and correct the format string
      // if necessary). We should also do so for scoped enumerations.
      SmallString<16> CastBuf;
      llvm::raw_svector_ostream CastFix(CastBuf);
      CastFix << (S.LangOpts.CPlusPlus ? "static_cast<" : "(");
      IntendedTy.print(CastFix, S.Context.getPrintingPolicy());
      CastFix << (S.LangOpts.CPlusPlus ? ">" : ")");

      SmallVector<FixItHint,4> Hints;
      ArgType::MatchKind IntendedMatch = AT.matchesType(S.Context, IntendedTy);
      IntendedMatch = handleFormatSignedness(IntendedMatch, S.getDiagnostics(),
                                             E->getExprLoc());
      if ((IntendedMatch != ArgType::Match) || ShouldNotPrintDirectly)
        Hints.push_back(FixItHint::CreateReplacement(SpecRange, os.str()));

      if (const CStyleCastExpr *CCast = dyn_cast<CStyleCastExpr>(E)) {
        // If there's already a cast present, just replace it.
        SourceRange CastRange(CCast->getLParenLoc(), CCast->getRParenLoc());
        Hints.push_back(FixItHint::CreateReplacement(CastRange, CastFix.str()));

      } else if (!requiresParensToAddCast(E) && !S.LangOpts.CPlusPlus) {
        // If the expression has high enough precedence,
        // just write the C-style cast.
        Hints.push_back(
            FixItHint::CreateInsertion(E->getBeginLoc(), CastFix.str()));
      } else {
        // Otherwise, add parens around the expression as well as the cast.
        CastFix << "(";
        Hints.push_back(
            FixItHint::CreateInsertion(E->getBeginLoc(), CastFix.str()));

        // We don't use getLocForEndOfToken because it returns invalid source
        // locations for macro expansions (by design).
        SourceLocation EndLoc = S.SourceMgr.getSpellingLoc(E->getEndLoc());
        SourceLocation After = EndLoc.getLocWithOffset(
            Lexer::MeasureTokenLength(EndLoc, S.SourceMgr, S.LangOpts));
        Hints.push_back(FixItHint::CreateInsertion(After, ")"));
      }

      if (ShouldNotPrintDirectly && !IsScopedEnum) {
        // The expression has a type that should not be printed directly.
        // We extract the name from the typedef because we don't want to show
        // the underlying type in the diagnostic.
        StringRef Name;
        if (const auto *TypedefTy = ExprTy->getAs<TypedefType>())
          Name = TypedefTy->getDecl()->getName();
        else
          Name = CastTyName;
        unsigned Diag = Match == ArgType::NoMatchPedantic
                            ? diag::warn_format_argument_needs_cast_pedantic
                            : diag::warn_format_argument_needs_cast;
        EmitFormatDiagnostic(S.PDiag(Diag) << Name << IntendedTy << IsEnum
                                           << E->getSourceRange(),
                             E->getBeginLoc(), /*IsStringLocation=*/false,
                             SpecRange, Hints);
      } else {
        // In this case, the expression could be printed using a different
        // specifier, but we've decided that the specifier is probably correct
        // and we should cast instead. Just use the normal warning message.

        unsigned Diag =
            IsScopedEnum
                ? diag::warn_format_conversion_argument_type_mismatch_pedantic
                : diag::warn_format_conversion_argument_type_mismatch;

        EmitFormatDiagnostic(
            S.PDiag(Diag) << AT.getRepresentativeTypeName(S.Context) << ExprTy
                          << IsEnum << E->getSourceRange(),
            E->getBeginLoc(), /*IsStringLocation*/ false, SpecRange, Hints);
      }
    }
  } else {
    const CharSourceRange &CSR = getSpecifierRange(StartSpecifier,
                                                   SpecifierLen);
    // Since the warning for passing non-POD types to variadic functions
    // was deferred until now, we emit a warning for non-POD
    // arguments here.
    bool EmitTypeMismatch = false;
    switch (S.isValidVarArgType(ExprTy)) {
    case Sema::VAK_Valid:
    case Sema::VAK_ValidInCXX11: {
      unsigned Diag;
      switch (Match) {
      case ArgType::Match:
      case ArgType::MatchPromotion:
      case ArgType::NoMatchPromotionTypeConfusion:
      case ArgType::NoMatchSignedness:
        llvm_unreachable("expected non-matching");
      case ArgType::NoMatchPedantic:
        Diag = diag::warn_format_conversion_argument_type_mismatch_pedantic;
        break;
      case ArgType::NoMatchTypeConfusion:
        Diag = diag::warn_format_conversion_argument_type_mismatch_confusion;
        break;
      case ArgType::NoMatch:
        Diag = diag::warn_format_conversion_argument_type_mismatch;
        break;
      }

      EmitFormatDiagnostic(
          S.PDiag(Diag) << AT.getRepresentativeTypeName(S.Context) << ExprTy
                        << IsEnum << CSR << E->getSourceRange(),
          E->getBeginLoc(), /*IsStringLocation*/ false, CSR);
      break;
    }
    case Sema::VAK_Undefined:
    case Sema::VAK_MSVCUndefined:
      if (CallType == Sema::VariadicDoesNotApply) {
        EmitTypeMismatch = true;
      } else {
        EmitFormatDiagnostic(
            S.PDiag(diag::warn_non_pod_vararg_with_format_string)
                << S.getLangOpts().CPlusPlus11 << ExprTy << CallType
                << AT.getRepresentativeTypeName(S.Context) << CSR
                << E->getSourceRange(),
            E->getBeginLoc(), /*IsStringLocation*/ false, CSR);
        checkForCStrMembers(AT, E);
      }
      break;

    case Sema::VAK_Invalid:
      if (CallType == Sema::VariadicDoesNotApply)
        EmitTypeMismatch = true;
      else if (ExprTy->isObjCObjectType())
        EmitFormatDiagnostic(
            S.PDiag(diag::err_cannot_pass_objc_interface_to_vararg_format)
                << S.getLangOpts().CPlusPlus11 << ExprTy << CallType
                << AT.getRepresentativeTypeName(S.Context) << CSR
                << E->getSourceRange(),
            E->getBeginLoc(), /*IsStringLocation*/ false, CSR);
      else
        // FIXME: If this is an initializer list, suggest removing the braces
        // or inserting a cast to the target type.
        S.Diag(E->getBeginLoc(), diag::err_cannot_pass_to_vararg_format)
            << isa<InitListExpr>(E) << ExprTy << CallType
            << AT.getRepresentativeTypeName(S.Context) << E->getSourceRange();
      break;
    }

    if (EmitTypeMismatch) {
      // The function is not variadic, so we do not generate warnings about
      // being allowed to pass that object as a variadic argument. Instead,
      // since there are inherently no printf specifiers for types which cannot
      // be passed as variadic arguments, emit a plain old specifier mismatch
      // argument.
      EmitFormatDiagnostic(
          S.PDiag(diag::warn_format_conversion_argument_type_mismatch)
              << AT.getRepresentativeTypeName(S.Context) << ExprTy << false
              << E->getSourceRange(),
          E->getBeginLoc(), false, CSR);
    }

    assert(FirstDataArg + FS.getArgIndex() < CheckedVarArgs.size() &&
           "format string specifier index out of range");
    CheckedVarArgs[FirstDataArg + FS.getArgIndex()] = true;
  }

  return true;
}

//===--- CHECK: Scanf format string checking ------------------------------===//

namespace {

class CheckScanfHandler : public CheckFormatHandler {
public:
  CheckScanfHandler(Sema &s, const FormatStringLiteral *fexpr,
                    const Expr *origFormatExpr, Sema::FormatStringType type,
                    unsigned firstDataArg, unsigned numDataArgs,
                    const char *beg, Sema::FormatArgumentPassingKind APK,
                    ArrayRef<const Expr *> Args, unsigned formatIdx,
                    bool inFunctionCall, Sema::VariadicCallType CallType,
                    llvm::SmallBitVector &CheckedVarArgs,
                    UncoveredArgHandler &UncoveredArg)
      : CheckFormatHandler(s, fexpr, origFormatExpr, type, firstDataArg,
                           numDataArgs, beg, APK, Args, formatIdx,
                           inFunctionCall, CallType, CheckedVarArgs,
                           UncoveredArg) {}

  bool HandleScanfSpecifier(const analyze_scanf::ScanfSpecifier &FS,
                            const char *startSpecifier,
                            unsigned specifierLen) override;

  bool HandleInvalidScanfConversionSpecifier(
          const analyze_scanf::ScanfSpecifier &FS,
          const char *startSpecifier,
          unsigned specifierLen) override;

  void HandleIncompleteScanList(const char *start, const char *end) override;
};

} // namespace

void CheckScanfHandler::HandleIncompleteScanList(const char *start,
                                                 const char *end) {
  EmitFormatDiagnostic(S.PDiag(diag::warn_scanf_scanlist_incomplete),
                       getLocationOfByte(end), /*IsStringLocation*/true,
                       getSpecifierRange(start, end - start));
}

bool CheckScanfHandler::HandleInvalidScanfConversionSpecifier(
                                        const analyze_scanf::ScanfSpecifier &FS,
                                        const char *startSpecifier,
                                        unsigned specifierLen) {
  const analyze_scanf::ScanfConversionSpecifier &CS =
    FS.getConversionSpecifier();

  return HandleInvalidConversionSpecifier(FS.getArgIndex(),
                                          getLocationOfByte(CS.getStart()),
                                          startSpecifier, specifierLen,
                                          CS.getStart(), CS.getLength());
}

bool CheckScanfHandler::HandleScanfSpecifier(
                                       const analyze_scanf::ScanfSpecifier &FS,
                                       const char *startSpecifier,
                                       unsigned specifierLen) {
  using namespace analyze_scanf;
  using namespace analyze_format_string;

  const ScanfConversionSpecifier &CS = FS.getConversionSpecifier();

  // Handle case where '%' and '*' don't consume an argument.  These shouldn't
  // be used to decide if we are using positional arguments consistently.
  if (FS.consumesDataArgument()) {
    if (atFirstArg) {
      atFirstArg = false;
      usesPositionalArgs = FS.usesPositionalArg();
    }
    else if (usesPositionalArgs != FS.usesPositionalArg()) {
      HandlePositionalNonpositionalArgs(getLocationOfByte(CS.getStart()),
                                        startSpecifier, specifierLen);
      return false;
    }
  }

  // Check if the field with is non-zero.
  const OptionalAmount &Amt = FS.getFieldWidth();
  if (Amt.getHowSpecified() == OptionalAmount::Constant) {
    if (Amt.getConstantAmount() == 0) {
      const CharSourceRange &R = getSpecifierRange(Amt.getStart(),
                                                   Amt.getConstantLength());
      EmitFormatDiagnostic(S.PDiag(diag::warn_scanf_nonzero_width),
                           getLocationOfByte(Amt.getStart()),
                           /*IsStringLocation*/true, R,
                           FixItHint::CreateRemoval(R));
    }
  }

  if (!FS.consumesDataArgument()) {
    // FIXME: Technically specifying a precision or field width here
    // makes no sense.  Worth issuing a warning at some point.
    return true;
  }

  // Consume the argument.
  unsigned argIndex = FS.getArgIndex();
  if (argIndex < NumDataArgs) {
      // The check to see if the argIndex is valid will come later.
      // We set the bit here because we may exit early from this
      // function if we encounter some other error.
    CoveredArgs.set(argIndex);
  }

  // Check the length modifier is valid with the given conversion specifier.
  if (!FS.hasValidLengthModifier(S.getASTContext().getTargetInfo(),
                                 S.getLangOpts()))
    HandleInvalidLengthModifier(FS, CS, startSpecifier, specifierLen,
                                diag::warn_format_nonsensical_length);
  else if (!FS.hasStandardLengthModifier())
    HandleNonStandardLengthModifier(FS, startSpecifier, specifierLen);
  else if (!FS.hasStandardLengthConversionCombination())
    HandleInvalidLengthModifier(FS, CS, startSpecifier, specifierLen,
                                diag::warn_format_non_standard_conversion_spec);

  if (!FS.hasStandardConversionSpecifier(S.getLangOpts()))
    HandleNonStandardConversionSpecifier(CS, startSpecifier, specifierLen);

  // The remaining checks depend on the data arguments.
  if (ArgPassingKind == Sema::FAPK_VAList)
    return true;

  if (!CheckNumArgs(FS, CS, startSpecifier, specifierLen, argIndex))
    return false;

  // Check that the argument type matches the format specifier.
  const Expr *Ex = getDataArg(argIndex);
  if (!Ex)
    return true;

  const analyze_format_string::ArgType &AT = FS.getArgType(S.Context);

  if (!AT.isValid()) {
    return true;
  }

  analyze_format_string::ArgType::MatchKind Match =
      AT.matchesType(S.Context, Ex->getType());
  Match = handleFormatSignedness(Match, S.getDiagnostics(), Ex->getExprLoc());
  bool Pedantic = Match == analyze_format_string::ArgType::NoMatchPedantic;
  if (Match == analyze_format_string::ArgType::Match)
    return true;

  ScanfSpecifier fixedFS = FS;
  bool Success = fixedFS.fixType(Ex->getType(), Ex->IgnoreImpCasts()->getType(),
                                 S.getLangOpts(), S.Context);

  unsigned Diag =
      Pedantic ? diag::warn_format_conversion_argument_type_mismatch_pedantic
               : diag::warn_format_conversion_argument_type_mismatch;

  if (Success) {
    // Get the fix string from the fixed format specifier.
    SmallString<128> buf;
    llvm::raw_svector_ostream os(buf);
    fixedFS.toString(os);

    EmitFormatDiagnostic(
        S.PDiag(Diag) << AT.getRepresentativeTypeName(S.Context)
                      << Ex->getType() << false << Ex->getSourceRange(),
        Ex->getBeginLoc(),
        /*IsStringLocation*/ false,
        getSpecifierRange(startSpecifier, specifierLen),
        FixItHint::CreateReplacement(
            getSpecifierRange(startSpecifier, specifierLen), os.str()));
  } else {
    EmitFormatDiagnostic(S.PDiag(Diag)
                             << AT.getRepresentativeTypeName(S.Context)
                             << Ex->getType() << false << Ex->getSourceRange(),
                         Ex->getBeginLoc(),
                         /*IsStringLocation*/ false,
                         getSpecifierRange(startSpecifier, specifierLen));
  }

  return true;
}

static void CheckFormatString(
    Sema &S, const FormatStringLiteral *FExpr, const Expr *OrigFormatExpr,
    ArrayRef<const Expr *> Args, Sema::FormatArgumentPassingKind APK,
    unsigned format_idx, unsigned firstDataArg, Sema::FormatStringType Type,
    bool inFunctionCall, Sema::VariadicCallType CallType,
    llvm::SmallBitVector &CheckedVarArgs, UncoveredArgHandler &UncoveredArg,
    bool IgnoreStringsWithoutSpecifiers) {
  // CHECK: is the format string a wide literal?
  if (!FExpr->isAscii() && !FExpr->isUTF8()) {
    CheckFormatHandler::EmitFormatDiagnostic(
        S, inFunctionCall, Args[format_idx],
        S.PDiag(diag::warn_format_string_is_wide_literal), FExpr->getBeginLoc(),
        /*IsStringLocation*/ true, OrigFormatExpr->getSourceRange());
    return;
  }

  // Str - The format string.  NOTE: this is NOT null-terminated!
  StringRef StrRef = FExpr->getString();
  const char *Str = StrRef.data();
  // Account for cases where the string literal is truncated in a declaration.
  const ConstantArrayType *T =
    S.Context.getAsConstantArrayType(FExpr->getType());
  assert(T && "String literal not of constant array type!");
  size_t TypeSize = T->getZExtSize();
  size_t StrLen = std::min(std::max(TypeSize, size_t(1)) - 1, StrRef.size());
  const unsigned numDataArgs = Args.size() - firstDataArg;

  if (IgnoreStringsWithoutSpecifiers &&
      !analyze_format_string::parseFormatStringHasFormattingSpecifiers(
          Str, Str + StrLen, S.getLangOpts(), S.Context.getTargetInfo()))
    return;

  // Emit a warning if the string literal is truncated and does not contain an
  // embedded null character.
  if (TypeSize <= StrRef.size() && !StrRef.substr(0, TypeSize).contains('\0')) {
    CheckFormatHandler::EmitFormatDiagnostic(
        S, inFunctionCall, Args[format_idx],
        S.PDiag(diag::warn_printf_format_string_not_null_terminated),
        FExpr->getBeginLoc(),
        /*IsStringLocation=*/true, OrigFormatExpr->getSourceRange());
    return;
  }

  // CHECK: empty format string?
  if (StrLen == 0 && numDataArgs > 0) {
    CheckFormatHandler::EmitFormatDiagnostic(
        S, inFunctionCall, Args[format_idx],
        S.PDiag(diag::warn_empty_format_string), FExpr->getBeginLoc(),
        /*IsStringLocation*/ true, OrigFormatExpr->getSourceRange());
    return;
  }

  if (Type == Sema::FST_Printf || Type == Sema::FST_NSString ||
      Type == Sema::FST_Kprintf || Type == Sema::FST_FreeBSDKPrintf ||
      Type == Sema::FST_OSLog || Type == Sema::FST_OSTrace ||
      Type == Sema::FST_Syslog) {
    CheckPrintfHandler H(
        S, FExpr, OrigFormatExpr, Type, firstDataArg, numDataArgs,
        (Type == Sema::FST_NSString || Type == Sema::FST_OSTrace), Str, APK,
        Args, format_idx, inFunctionCall, CallType, CheckedVarArgs,
        UncoveredArg);

    if (!analyze_format_string::ParsePrintfString(
            H, Str, Str + StrLen, S.getLangOpts(), S.Context.getTargetInfo(),
            Type == Sema::FST_Kprintf || Type == Sema::FST_FreeBSDKPrintf))
      H.DoneProcessing();
  } else if (Type == Sema::FST_Scanf) {
    CheckScanfHandler H(S, FExpr, OrigFormatExpr, Type, firstDataArg,
                        numDataArgs, Str, APK, Args, format_idx, inFunctionCall,
                        CallType, CheckedVarArgs, UncoveredArg);

    if (!analyze_format_string::ParseScanfString(
            H, Str, Str + StrLen, S.getLangOpts(), S.Context.getTargetInfo()))
      H.DoneProcessing();
  } // TODO: handle other formats
}

bool Sema::FormatStringHasSArg(const StringLiteral *FExpr) {
  // Str - The format string.  NOTE: this is NOT null-terminated!
  StringRef StrRef = FExpr->getString();
  const char *Str = StrRef.data();
  // Account for cases where the string literal is truncated in a declaration.
  const ConstantArrayType *T = Context.getAsConstantArrayType(FExpr->getType());
  assert(T && "String literal not of constant array type!");
  size_t TypeSize = T->getZExtSize();
  size_t StrLen = std::min(std::max(TypeSize, size_t(1)) - 1, StrRef.size());
  return analyze_format_string::ParseFormatStringHasSArg(Str, Str + StrLen,
                                                         getLangOpts(),
                                                         Context.getTargetInfo());
}

//===--- CHECK: Warn on use of wrong absolute value function. -------------===//

// Returns the related absolute value function that is larger, of 0 if one
// does not exist.
static unsigned getLargerAbsoluteValueFunction(unsigned AbsFunction) {
  switch (AbsFunction) {
  default:
    return 0;

  case Builtin::BI__builtin_abs:
    return Builtin::BI__builtin_labs;
  case Builtin::BI__builtin_labs:
    return Builtin::BI__builtin_llabs;
  case Builtin::BI__builtin_llabs:
    return 0;

  case Builtin::BI__builtin_fabsf:
    return Builtin::BI__builtin_fabs;
  case Builtin::BI__builtin_fabs:
    return Builtin::BI__builtin_fabsl;
  case Builtin::BI__builtin_fabsl:
    return 0;

  case Builtin::BI__builtin_cabsf:
    return Builtin::BI__builtin_cabs;
  case Builtin::BI__builtin_cabs:
    return Builtin::BI__builtin_cabsl;
  case Builtin::BI__builtin_cabsl:
    return 0;

  case Builtin::BIabs:
    return Builtin::BIlabs;
  case Builtin::BIlabs:
    return Builtin::BIllabs;
  case Builtin::BIllabs:
    return 0;

  case Builtin::BIfabsf:
    return Builtin::BIfabs;
  case Builtin::BIfabs:
    return Builtin::BIfabsl;
  case Builtin::BIfabsl:
    return 0;

  case Builtin::BIcabsf:
   return Builtin::BIcabs;
  case Builtin::BIcabs:
    return Builtin::BIcabsl;
  case Builtin::BIcabsl:
    return 0;
  }
}

// Returns the argument type of the absolute value function.
static QualType getAbsoluteValueArgumentType(ASTContext &Context,
                                             unsigned AbsType) {
  if (AbsType == 0)
    return QualType();

  ASTContext::GetBuiltinTypeError Error = ASTContext::GE_None;
  QualType BuiltinType = Context.GetBuiltinType(AbsType, Error);
  if (Error != ASTContext::GE_None)
    return QualType();

  const FunctionProtoType *FT = BuiltinType->getAs<FunctionProtoType>();
  if (!FT)
    return QualType();

  if (FT->getNumParams() != 1)
    return QualType();

  return FT->getParamType(0);
}

// Returns the best absolute value function, or zero, based on type and
// current absolute value function.
static unsigned getBestAbsFunction(ASTContext &Context, QualType ArgType,
                                   unsigned AbsFunctionKind) {
  unsigned BestKind = 0;
  uint64_t ArgSize = Context.getTypeSize(ArgType);
  for (unsigned Kind = AbsFunctionKind; Kind != 0;
       Kind = getLargerAbsoluteValueFunction(Kind)) {
    QualType ParamType = getAbsoluteValueArgumentType(Context, Kind);
    if (Context.getTypeSize(ParamType) >= ArgSize) {
      if (BestKind == 0)
        BestKind = Kind;
      else if (Context.hasSameType(ParamType, ArgType)) {
        BestKind = Kind;
        break;
      }
    }
  }
  return BestKind;
}

enum AbsoluteValueKind {
  AVK_Integer,
  AVK_Floating,
  AVK_Complex
};

static AbsoluteValueKind getAbsoluteValueKind(QualType T) {
  if (T->isIntegralOrEnumerationType())
    return AVK_Integer;
  if (T->isRealFloatingType())
    return AVK_Floating;
  if (T->isAnyComplexType())
    return AVK_Complex;

  llvm_unreachable("Type not integer, floating, or complex");
}

// Changes the absolute value function to a different type.  Preserves whether
// the function is a builtin.
static unsigned changeAbsFunction(unsigned AbsKind,
                                  AbsoluteValueKind ValueKind) {
  switch (ValueKind) {
  case AVK_Integer:
    switch (AbsKind) {
    default:
      return 0;
    case Builtin::BI__builtin_fabsf:
    case Builtin::BI__builtin_fabs:
    case Builtin::BI__builtin_fabsl:
    case Builtin::BI__builtin_cabsf:
    case Builtin::BI__builtin_cabs:
    case Builtin::BI__builtin_cabsl:
      return Builtin::BI__builtin_abs;
    case Builtin::BIfabsf:
    case Builtin::BIfabs:
    case Builtin::BIfabsl:
    case Builtin::BIcabsf:
    case Builtin::BIcabs:
    case Builtin::BIcabsl:
      return Builtin::BIabs;
    }
  case AVK_Floating:
    switch (AbsKind) {
    default:
      return 0;
    case Builtin::BI__builtin_abs:
    case Builtin::BI__builtin_labs:
    case Builtin::BI__builtin_llabs:
    case Builtin::BI__builtin_cabsf:
    case Builtin::BI__builtin_cabs:
    case Builtin::BI__builtin_cabsl:
      return Builtin::BI__builtin_fabsf;
    case Builtin::BIabs:
    case Builtin::BIlabs:
    case Builtin::BIllabs:
    case Builtin::BIcabsf:
    case Builtin::BIcabs:
    case Builtin::BIcabsl:
      return Builtin::BIfabsf;
    }
  case AVK_Complex:
    switch (AbsKind) {
    default:
      return 0;
    case Builtin::BI__builtin_abs:
    case Builtin::BI__builtin_labs:
    case Builtin::BI__builtin_llabs:
    case Builtin::BI__builtin_fabsf:
    case Builtin::BI__builtin_fabs:
    case Builtin::BI__builtin_fabsl:
      return Builtin::BI__builtin_cabsf;
    case Builtin::BIabs:
    case Builtin::BIlabs:
    case Builtin::BIllabs:
    case Builtin::BIfabsf:
    case Builtin::BIfabs:
    case Builtin::BIfabsl:
      return Builtin::BIcabsf;
    }
  }
  llvm_unreachable("Unable to convert function");
}

static unsigned getAbsoluteValueFunctionKind(const FunctionDecl *FDecl) {
  const IdentifierInfo *FnInfo = FDecl->getIdentifier();
  if (!FnInfo)
    return 0;

  switch (FDecl->getBuiltinID()) {
  default:
    return 0;
  case Builtin::BI__builtin_abs:
  case Builtin::BI__builtin_fabs:
  case Builtin::BI__builtin_fabsf:
  case Builtin::BI__builtin_fabsl:
  case Builtin::BI__builtin_labs:
  case Builtin::BI__builtin_llabs:
  case Builtin::BI__builtin_cabs:
  case Builtin::BI__builtin_cabsf:
  case Builtin::BI__builtin_cabsl:
  case Builtin::BIabs:
  case Builtin::BIlabs:
  case Builtin::BIllabs:
  case Builtin::BIfabs:
  case Builtin::BIfabsf:
  case Builtin::BIfabsl:
  case Builtin::BIcabs:
  case Builtin::BIcabsf:
  case Builtin::BIcabsl:
    return FDecl->getBuiltinID();
  }
  llvm_unreachable("Unknown Builtin type");
}

// If the replacement is valid, emit a note with replacement function.
// Additionally, suggest including the proper header if not already included.
static void emitReplacement(Sema &S, SourceLocation Loc, SourceRange Range,
                            unsigned AbsKind, QualType ArgType) {
  bool EmitHeaderHint = true;
  const char *HeaderName = nullptr;
  StringRef FunctionName;
  if (S.getLangOpts().CPlusPlus && !ArgType->isAnyComplexType()) {
    FunctionName = "std::abs";
    if (ArgType->isIntegralOrEnumerationType()) {
      HeaderName = "cstdlib";
    } else if (ArgType->isRealFloatingType()) {
      HeaderName = "cmath";
    } else {
      llvm_unreachable("Invalid Type");
    }

    // Lookup all std::abs
    if (NamespaceDecl *Std = S.getStdNamespace()) {
      LookupResult R(S, &S.Context.Idents.get("abs"), Loc, Sema::LookupAnyName);
      R.suppressDiagnostics();
      S.LookupQualifiedName(R, Std);

      for (const auto *I : R) {
        const FunctionDecl *FDecl = nullptr;
        if (const UsingShadowDecl *UsingD = dyn_cast<UsingShadowDecl>(I)) {
          FDecl = dyn_cast<FunctionDecl>(UsingD->getTargetDecl());
        } else {
          FDecl = dyn_cast<FunctionDecl>(I);
        }
        if (!FDecl)
          continue;

        // Found std::abs(), check that they are the right ones.
        if (FDecl->getNumParams() != 1)
          continue;

        // Check that the parameter type can handle the argument.
        QualType ParamType = FDecl->getParamDecl(0)->getType();
        if (getAbsoluteValueKind(ArgType) == getAbsoluteValueKind(ParamType) &&
            S.Context.getTypeSize(ArgType) <=
                S.Context.getTypeSize(ParamType)) {
          // Found a function, don't need the header hint.
          EmitHeaderHint = false;
          break;
        }
      }
    }
  } else {
    FunctionName = S.Context.BuiltinInfo.getName(AbsKind);
    HeaderName = S.Context.BuiltinInfo.getHeaderName(AbsKind);

    if (HeaderName) {
      DeclarationName DN(&S.Context.Idents.get(FunctionName));
      LookupResult R(S, DN, Loc, Sema::LookupAnyName);
      R.suppressDiagnostics();
      S.LookupName(R, S.getCurScope());

      if (R.isSingleResult()) {
        FunctionDecl *FD = dyn_cast<FunctionDecl>(R.getFoundDecl());
        if (FD && FD->getBuiltinID() == AbsKind) {
          EmitHeaderHint = false;
        } else {
          return;
        }
      } else if (!R.empty()) {
        return;
      }
    }
  }

  S.Diag(Loc, diag::note_replace_abs_function)
      << FunctionName << FixItHint::CreateReplacement(Range, FunctionName);

  if (!HeaderName)
    return;

  if (!EmitHeaderHint)
    return;

  S.Diag(Loc, diag::note_include_header_or_declare) << HeaderName
                                                    << FunctionName;
}

template <std::size_t StrLen>
static bool IsStdFunction(const FunctionDecl *FDecl,
                          const char (&Str)[StrLen]) {
  if (!FDecl)
    return false;
  if (!FDecl->getIdentifier() || !FDecl->getIdentifier()->isStr(Str))
    return false;
  if (!FDecl->isInStdNamespace())
    return false;

  return true;
}

void Sema::CheckInfNaNFunction(const CallExpr *Call,
                               const FunctionDecl *FDecl) {
  FPOptions FPO = Call->getFPFeaturesInEffect(getLangOpts());
  if ((IsStdFunction(FDecl, "isnan") || IsStdFunction(FDecl, "isunordered") ||
       (Call->getBuiltinCallee() == Builtin::BI__builtin_nanf)) &&
      FPO.getNoHonorNaNs())
    Diag(Call->getBeginLoc(), diag::warn_fp_nan_inf_when_disabled)
        << 1 << 0 << Call->getSourceRange();
  else if ((IsStdFunction(FDecl, "isinf") ||
            (IsStdFunction(FDecl, "isfinite") ||
             (FDecl->getIdentifier() && FDecl->getName() == "infinity"))) &&
           FPO.getNoHonorInfs())
    Diag(Call->getBeginLoc(), diag::warn_fp_nan_inf_when_disabled)
        << 0 << 0 << Call->getSourceRange();
}

void Sema::CheckAbsoluteValueFunction(const CallExpr *Call,
                                      const FunctionDecl *FDecl) {
  if (Call->getNumArgs() != 1)
    return;

  unsigned AbsKind = getAbsoluteValueFunctionKind(FDecl);
  bool IsStdAbs = IsStdFunction(FDecl, "abs");
  if (AbsKind == 0 && !IsStdAbs)
    return;

  QualType ArgType = Call->getArg(0)->IgnoreParenImpCasts()->getType();
  QualType ParamType = Call->getArg(0)->getType();

  // Unsigned types cannot be negative.  Suggest removing the absolute value
  // function call.
  if (ArgType->isUnsignedIntegerType()) {
    StringRef FunctionName =
        IsStdAbs ? "std::abs" : Context.BuiltinInfo.getName(AbsKind);
    Diag(Call->getExprLoc(), diag::warn_unsigned_abs) << ArgType << ParamType;
    Diag(Call->getExprLoc(), diag::note_remove_abs)
        << FunctionName
        << FixItHint::CreateRemoval(Call->getCallee()->getSourceRange());
    return;
  }

  // Taking the absolute value of a pointer is very suspicious, they probably
  // wanted to index into an array, dereference a pointer, call a function, etc.
  if (ArgType->isPointerType() || ArgType->canDecayToPointerType()) {
    unsigned DiagType = 0;
    if (ArgType->isFunctionType())
      DiagType = 1;
    else if (ArgType->isArrayType())
      DiagType = 2;

    Diag(Call->getExprLoc(), diag::warn_pointer_abs) << DiagType << ArgType;
    return;
  }

  // std::abs has overloads which prevent most of the absolute value problems
  // from occurring.
  if (IsStdAbs)
    return;

  AbsoluteValueKind ArgValueKind = getAbsoluteValueKind(ArgType);
  AbsoluteValueKind ParamValueKind = getAbsoluteValueKind(ParamType);

  // The argument and parameter are the same kind.  Check if they are the right
  // size.
  if (ArgValueKind == ParamValueKind) {
    if (Context.getTypeSize(ArgType) <= Context.getTypeSize(ParamType))
      return;

    unsigned NewAbsKind = getBestAbsFunction(Context, ArgType, AbsKind);
    Diag(Call->getExprLoc(), diag::warn_abs_too_small)
        << FDecl << ArgType << ParamType;

    if (NewAbsKind == 0)
      return;

    emitReplacement(*this, Call->getExprLoc(),
                    Call->getCallee()->getSourceRange(), NewAbsKind, ArgType);
    return;
  }

  // ArgValueKind != ParamValueKind
  // The wrong type of absolute value function was used.  Attempt to find the
  // proper one.
  unsigned NewAbsKind = changeAbsFunction(AbsKind, ArgValueKind);
  NewAbsKind = getBestAbsFunction(Context, ArgType, NewAbsKind);
  if (NewAbsKind == 0)
    return;

  Diag(Call->getExprLoc(), diag::warn_wrong_absolute_value_type)
      << FDecl << ParamValueKind << ArgValueKind;

  emitReplacement(*this, Call->getExprLoc(),
                  Call->getCallee()->getSourceRange(), NewAbsKind, ArgType);
}

//===--- CHECK: Warn on use of std::max and unsigned zero. r---------------===//
void Sema::CheckMaxUnsignedZero(const CallExpr *Call,
                                const FunctionDecl *FDecl) {
  if (!Call || !FDecl) return;

  // Ignore template specializations and macros.
  if (inTemplateInstantiation()) return;
  if (Call->getExprLoc().isMacroID()) return;

  // Only care about the one template argument, two function parameter std::max
  if (Call->getNumArgs() != 2) return;
  if (!IsStdFunction(FDecl, "max")) return;
  const auto * ArgList = FDecl->getTemplateSpecializationArgs();
  if (!ArgList) return;
  if (ArgList->size() != 1) return;

  // Check that template type argument is unsigned integer.
  const auto& TA = ArgList->get(0);
  if (TA.getKind() != TemplateArgument::Type) return;
  QualType ArgType = TA.getAsType();
  if (!ArgType->isUnsignedIntegerType()) return;

  // See if either argument is a literal zero.
  auto IsLiteralZeroArg = [](const Expr* E) -> bool {
    const auto *MTE = dyn_cast<MaterializeTemporaryExpr>(E);
    if (!MTE) return false;
    const auto *Num = dyn_cast<IntegerLiteral>(MTE->getSubExpr());
    if (!Num) return false;
    if (Num->getValue() != 0) return false;
    return true;
  };

  const Expr *FirstArg = Call->getArg(0);
  const Expr *SecondArg = Call->getArg(1);
  const bool IsFirstArgZero = IsLiteralZeroArg(FirstArg);
  const bool IsSecondArgZero = IsLiteralZeroArg(SecondArg);

  // Only warn when exactly one argument is zero.
  if (IsFirstArgZero == IsSecondArgZero) return;

  SourceRange FirstRange = FirstArg->getSourceRange();
  SourceRange SecondRange = SecondArg->getSourceRange();

  SourceRange ZeroRange = IsFirstArgZero ? FirstRange : SecondRange;

  Diag(Call->getExprLoc(), diag::warn_max_unsigned_zero)
      << IsFirstArgZero << Call->getCallee()->getSourceRange() << ZeroRange;

  // Deduce what parts to remove so that "std::max(0u, foo)" becomes "(foo)".
  SourceRange RemovalRange;
  if (IsFirstArgZero) {
    RemovalRange = SourceRange(FirstRange.getBegin(),
                               SecondRange.getBegin().getLocWithOffset(-1));
  } else {
    RemovalRange = SourceRange(getLocForEndOfToken(FirstRange.getEnd()),
                               SecondRange.getEnd());
  }

  Diag(Call->getExprLoc(), diag::note_remove_max_call)
        << FixItHint::CreateRemoval(Call->getCallee()->getSourceRange())
        << FixItHint::CreateRemoval(RemovalRange);
}

//===--- CHECK: Standard memory functions ---------------------------------===//

/// Takes the expression passed to the size_t parameter of functions
/// such as memcmp, strncat, etc and warns if it's a comparison.
///
/// This is to catch typos like `if (memcmp(&a, &b, sizeof(a) > 0))`.
static bool CheckMemorySizeofForComparison(Sema &S, const Expr *E,
                                           IdentifierInfo *FnName,
                                           SourceLocation FnLoc,
                                           SourceLocation RParenLoc) {
  const BinaryOperator *Size = dyn_cast<BinaryOperator>(E);
  if (!Size)
    return false;

  // if E is binop and op is <=>, >, <, >=, <=, ==, &&, ||:
  if (!Size->isComparisonOp() && !Size->isLogicalOp())
    return false;

  SourceRange SizeRange = Size->getSourceRange();
  S.Diag(Size->getOperatorLoc(), diag::warn_memsize_comparison)
      << SizeRange << FnName;
  S.Diag(FnLoc, diag::note_memsize_comparison_paren)
      << FnName
      << FixItHint::CreateInsertion(
             S.getLocForEndOfToken(Size->getLHS()->getEndLoc()), ")")
      << FixItHint::CreateRemoval(RParenLoc);
  S.Diag(SizeRange.getBegin(), diag::note_memsize_comparison_cast_silence)
      << FixItHint::CreateInsertion(SizeRange.getBegin(), "(size_t)(")
      << FixItHint::CreateInsertion(S.getLocForEndOfToken(SizeRange.getEnd()),
                                    ")");

  return true;
}

/// Determine whether the given type is or contains a dynamic class type
/// (e.g., whether it has a vtable).
static const CXXRecordDecl *getContainedDynamicClass(QualType T,
                                                     bool &IsContained) {
  // Look through array types while ignoring qualifiers.
  const Type *Ty = T->getBaseElementTypeUnsafe();
  IsContained = false;

  const CXXRecordDecl *RD = Ty->getAsCXXRecordDecl();
  RD = RD ? RD->getDefinition() : nullptr;
  if (!RD || RD->isInvalidDecl())
    return nullptr;

  if (RD->isDynamicClass())
    return RD;

  // Check all the fields.  If any bases were dynamic, the class is dynamic.
  // It's impossible for a class to transitively contain itself by value, so
  // infinite recursion is impossible.
  for (auto *FD : RD->fields()) {
    bool SubContained;
    if (const CXXRecordDecl *ContainedRD =
            getContainedDynamicClass(FD->getType(), SubContained)) {
      IsContained = true;
      return ContainedRD;
    }
  }

  return nullptr;
}

static const UnaryExprOrTypeTraitExpr *getAsSizeOfExpr(const Expr *E) {
  if (const auto *Unary = dyn_cast<UnaryExprOrTypeTraitExpr>(E))
    if (Unary->getKind() == UETT_SizeOf)
      return Unary;
  return nullptr;
}

/// If E is a sizeof expression, returns its argument expression,
/// otherwise returns NULL.
static const Expr *getSizeOfExprArg(const Expr *E) {
  if (const UnaryExprOrTypeTraitExpr *SizeOf = getAsSizeOfExpr(E))
    if (!SizeOf->isArgumentType())
      return SizeOf->getArgumentExpr()->IgnoreParenImpCasts();
  return nullptr;
}

/// If E is a sizeof expression, returns its argument type.
static QualType getSizeOfArgType(const Expr *E) {
  if (const UnaryExprOrTypeTraitExpr *SizeOf = getAsSizeOfExpr(E))
    return SizeOf->getTypeOfArgument();
  return QualType();
}

namespace {

struct SearchNonTrivialToInitializeField
    : DefaultInitializedTypeVisitor<SearchNonTrivialToInitializeField> {
  using Super =
      DefaultInitializedTypeVisitor<SearchNonTrivialToInitializeField>;

  SearchNonTrivialToInitializeField(const Expr *E, Sema &S) : E(E), S(S) {}

  void visitWithKind(QualType::PrimitiveDefaultInitializeKind PDIK, QualType FT,
                     SourceLocation SL) {
    if (const auto *AT = asDerived().getContext().getAsArrayType(FT)) {
      asDerived().visitArray(PDIK, AT, SL);
      return;
    }

    Super::visitWithKind(PDIK, FT, SL);
  }

  void visitARCStrong(QualType FT, SourceLocation SL) {
    S.DiagRuntimeBehavior(SL, E, S.PDiag(diag::note_nontrivial_field) << 1);
  }
  void visitARCWeak(QualType FT, SourceLocation SL) {
    S.DiagRuntimeBehavior(SL, E, S.PDiag(diag::note_nontrivial_field) << 1);
  }
  void visitStruct(QualType FT, SourceLocation SL) {
    for (const FieldDecl *FD : FT->castAs<RecordType>()->getDecl()->fields())
      visit(FD->getType(), FD->getLocation());
  }
  void visitArray(QualType::PrimitiveDefaultInitializeKind PDIK,
                  const ArrayType *AT, SourceLocation SL) {
    visit(getContext().getBaseElementType(AT), SL);
  }
  void visitTrivial(QualType FT, SourceLocation SL) {}

  static void diag(QualType RT, const Expr *E, Sema &S) {
    SearchNonTrivialToInitializeField(E, S).visitStruct(RT, SourceLocation());
  }

  ASTContext &getContext() { return S.getASTContext(); }

  const Expr *E;
  Sema &S;
};

struct SearchNonTrivialToCopyField
    : CopiedTypeVisitor<SearchNonTrivialToCopyField, false> {
  using Super = CopiedTypeVisitor<SearchNonTrivialToCopyField, false>;

  SearchNonTrivialToCopyField(const Expr *E, Sema &S) : E(E), S(S) {}

  void visitWithKind(QualType::PrimitiveCopyKind PCK, QualType FT,
                     SourceLocation SL) {
    if (const auto *AT = asDerived().getContext().getAsArrayType(FT)) {
      asDerived().visitArray(PCK, AT, SL);
      return;
    }

    Super::visitWithKind(PCK, FT, SL);
  }

  void visitARCStrong(QualType FT, SourceLocation SL) {
    S.DiagRuntimeBehavior(SL, E, S.PDiag(diag::note_nontrivial_field) << 0);
  }
  void visitARCWeak(QualType FT, SourceLocation SL) {
    S.DiagRuntimeBehavior(SL, E, S.PDiag(diag::note_nontrivial_field) << 0);
  }
  void visitStruct(QualType FT, SourceLocation SL) {
    for (const FieldDecl *FD : FT->castAs<RecordType>()->getDecl()->fields())
      visit(FD->getType(), FD->getLocation());
  }
  void visitArray(QualType::PrimitiveCopyKind PCK, const ArrayType *AT,
                  SourceLocation SL) {
    visit(getContext().getBaseElementType(AT), SL);
  }
  void preVisit(QualType::PrimitiveCopyKind PCK, QualType FT,
                SourceLocation SL) {}
  void visitTrivial(QualType FT, SourceLocation SL) {}
  void visitVolatileTrivial(QualType FT, SourceLocation SL) {}

  static void diag(QualType RT, const Expr *E, Sema &S) {
    SearchNonTrivialToCopyField(E, S).visitStruct(RT, SourceLocation());
  }

  ASTContext &getContext() { return S.getASTContext(); }

  const Expr *E;
  Sema &S;
};

}

/// Detect if \c SizeofExpr is likely to calculate the sizeof an object.
static bool doesExprLikelyComputeSize(const Expr *SizeofExpr) {
  SizeofExpr = SizeofExpr->IgnoreParenImpCasts();

  if (const auto *BO = dyn_cast<BinaryOperator>(SizeofExpr)) {
    if (BO->getOpcode() != BO_Mul && BO->getOpcode() != BO_Add)
      return false;

    return doesExprLikelyComputeSize(BO->getLHS()) ||
           doesExprLikelyComputeSize(BO->getRHS());
  }

  return getAsSizeOfExpr(SizeofExpr) != nullptr;
}

/// Check if the ArgLoc originated from a macro passed to the call at CallLoc.
///
/// \code
///   #define MACRO 0
///   foo(MACRO);
///   foo(0);
/// \endcode
///
/// This should return true for the first call to foo, but not for the second
/// (regardless of whether foo is a macro or function).
static bool isArgumentExpandedFromMacro(SourceManager &SM,
                                        SourceLocation CallLoc,
                                        SourceLocation ArgLoc) {
  if (!CallLoc.isMacroID())
    return SM.getFileID(CallLoc) != SM.getFileID(ArgLoc);

  return SM.getFileID(SM.getImmediateMacroCallerLoc(CallLoc)) !=
         SM.getFileID(SM.getImmediateMacroCallerLoc(ArgLoc));
}

/// Diagnose cases like 'memset(buf, sizeof(buf), 0)', which should have the
/// last two arguments transposed.
static void CheckMemaccessSize(Sema &S, unsigned BId, const CallExpr *Call) {
  if (BId != Builtin::BImemset && BId != Builtin::BIbzero)
    return;

  const Expr *SizeArg =
    Call->getArg(BId == Builtin::BImemset ? 2 : 1)->IgnoreImpCasts();

  auto isLiteralZero = [](const Expr *E) {
    return (isa<IntegerLiteral>(E) &&
            cast<IntegerLiteral>(E)->getValue() == 0) ||
           (isa<CharacterLiteral>(E) &&
            cast<CharacterLiteral>(E)->getValue() == 0);
  };

  // If we're memsetting or bzeroing 0 bytes, then this is likely an error.
  SourceLocation CallLoc = Call->getRParenLoc();
  SourceManager &SM = S.getSourceManager();
  if (isLiteralZero(SizeArg) &&
      !isArgumentExpandedFromMacro(SM, CallLoc, SizeArg->getExprLoc())) {

    SourceLocation DiagLoc = SizeArg->getExprLoc();

    // Some platforms #define bzero to __builtin_memset. See if this is the
    // case, and if so, emit a better diagnostic.
    if (BId == Builtin::BIbzero ||
        (CallLoc.isMacroID() && Lexer::getImmediateMacroName(
                                    CallLoc, SM, S.getLangOpts()) == "bzero")) {
      S.Diag(DiagLoc, diag::warn_suspicious_bzero_size);
      S.Diag(DiagLoc, diag::note_suspicious_bzero_size_silence);
    } else if (!isLiteralZero(Call->getArg(1)->IgnoreImpCasts())) {
      S.Diag(DiagLoc, diag::warn_suspicious_sizeof_memset) << 0;
      S.Diag(DiagLoc, diag::note_suspicious_sizeof_memset_silence) << 0;
    }
    return;
  }

  // If the second argument to a memset is a sizeof expression and the third
  // isn't, this is also likely an error. This should catch
  // 'memset(buf, sizeof(buf), 0xff)'.
  if (BId == Builtin::BImemset &&
      doesExprLikelyComputeSize(Call->getArg(1)) &&
      !doesExprLikelyComputeSize(Call->getArg(2))) {
    SourceLocation DiagLoc = Call->getArg(1)->getExprLoc();
    S.Diag(DiagLoc, diag::warn_suspicious_sizeof_memset) << 1;
    S.Diag(DiagLoc, diag::note_suspicious_sizeof_memset_silence) << 1;
    return;
  }
}

void Sema::CheckMemaccessArguments(const CallExpr *Call,
                                   unsigned BId,
                                   IdentifierInfo *FnName) {
  assert(BId != 0);

  // It is possible to have a non-standard definition of memset.  Validate
  // we have enough arguments, and if not, abort further checking.
  unsigned ExpectedNumArgs =
      (BId == Builtin::BIstrndup || BId == Builtin::BIbzero ? 2 : 3);
  if (Call->getNumArgs() < ExpectedNumArgs)
    return;

  unsigned LastArg = (BId == Builtin::BImemset || BId == Builtin::BIbzero ||
                      BId == Builtin::BIstrndup ? 1 : 2);
  unsigned LenArg =
      (BId == Builtin::BIbzero || BId == Builtin::BIstrndup ? 1 : 2);
  const Expr *LenExpr = Call->getArg(LenArg)->IgnoreParenImpCasts();

  if (CheckMemorySizeofForComparison(*this, LenExpr, FnName,
                                     Call->getBeginLoc(), Call->getRParenLoc()))
    return;

  // Catch cases like 'memset(buf, sizeof(buf), 0)'.
  CheckMemaccessSize(*this, BId, Call);

  // We have special checking when the length is a sizeof expression.
  QualType SizeOfArgTy = getSizeOfArgType(LenExpr);
  const Expr *SizeOfArg = getSizeOfExprArg(LenExpr);
  llvm::FoldingSetNodeID SizeOfArgID;

  // Although widely used, 'bzero' is not a standard function. Be more strict
  // with the argument types before allowing diagnostics and only allow the
  // form bzero(ptr, sizeof(...)).
  QualType FirstArgTy = Call->getArg(0)->IgnoreParenImpCasts()->getType();
  if (BId == Builtin::BIbzero && !FirstArgTy->getAs<PointerType>())
    return;

  for (unsigned ArgIdx = 0; ArgIdx != LastArg; ++ArgIdx) {
    const Expr *Dest = Call->getArg(ArgIdx)->IgnoreParenImpCasts();
    SourceRange ArgRange = Call->getArg(ArgIdx)->getSourceRange();

    QualType DestTy = Dest->getType();
    QualType PointeeTy;
    if (const PointerType *DestPtrTy = DestTy->getAs<PointerType>()) {
      PointeeTy = DestPtrTy->getPointeeType();

      // Never warn about void type pointers. This can be used to suppress
      // false positives.
      if (PointeeTy->isVoidType())
        continue;

      // Catch "memset(p, 0, sizeof(p))" -- needs to be sizeof(*p). Do this by
      // actually comparing the expressions for equality. Because computing the
      // expression IDs can be expensive, we only do this if the diagnostic is
      // enabled.
      if (SizeOfArg &&
          !Diags.isIgnored(diag::warn_sizeof_pointer_expr_memaccess,
                           SizeOfArg->getExprLoc())) {
        // We only compute IDs for expressions if the warning is enabled, and
        // cache the sizeof arg's ID.
        if (SizeOfArgID == llvm::FoldingSetNodeID())
          SizeOfArg->Profile(SizeOfArgID, Context, true);
        llvm::FoldingSetNodeID DestID;
        Dest->Profile(DestID, Context, true);
        if (DestID == SizeOfArgID) {
          // TODO: For strncpy() and friends, this could suggest sizeof(dst)
          //       over sizeof(src) as well.
          unsigned ActionIdx = 0; // Default is to suggest dereferencing.
          StringRef ReadableName = FnName->getName();

          if (const UnaryOperator *UnaryOp = dyn_cast<UnaryOperator>(Dest))
            if (UnaryOp->getOpcode() == UO_AddrOf)
              ActionIdx = 1; // If its an address-of operator, just remove it.
          if (!PointeeTy->isIncompleteType() &&
              (Context.getTypeSize(PointeeTy) == Context.getCharWidth()))
            ActionIdx = 2; // If the pointee's size is sizeof(char),
                           // suggest an explicit length.

          // If the function is defined as a builtin macro, do not show macro
          // expansion.
          SourceLocation SL = SizeOfArg->getExprLoc();
          SourceRange DSR = Dest->getSourceRange();
          SourceRange SSR = SizeOfArg->getSourceRange();
          SourceManager &SM = getSourceManager();

          if (SM.isMacroArgExpansion(SL)) {
            ReadableName = Lexer::getImmediateMacroName(SL, SM, LangOpts);
            SL = SM.getSpellingLoc(SL);
            DSR = SourceRange(SM.getSpellingLoc(DSR.getBegin()),
                             SM.getSpellingLoc(DSR.getEnd()));
            SSR = SourceRange(SM.getSpellingLoc(SSR.getBegin()),
                             SM.getSpellingLoc(SSR.getEnd()));
          }

          DiagRuntimeBehavior(SL, SizeOfArg,
                              PDiag(diag::warn_sizeof_pointer_expr_memaccess)
                                << ReadableName
                                << PointeeTy
                                << DestTy
                                << DSR
                                << SSR);
          DiagRuntimeBehavior(SL, SizeOfArg,
                         PDiag(diag::warn_sizeof_pointer_expr_memaccess_note)
                                << ActionIdx
                                << SSR);

          break;
        }
      }

      // Also check for cases where the sizeof argument is the exact same
      // type as the memory argument, and where it points to a user-defined
      // record type.
      if (SizeOfArgTy != QualType()) {
        if (PointeeTy->isRecordType() &&
            Context.typesAreCompatible(SizeOfArgTy, DestTy)) {
          DiagRuntimeBehavior(LenExpr->getExprLoc(), Dest,
                              PDiag(diag::warn_sizeof_pointer_type_memaccess)
                                << FnName << SizeOfArgTy << ArgIdx
                                << PointeeTy << Dest->getSourceRange()
                                << LenExpr->getSourceRange());
          break;
        }
      }
    } else if (DestTy->isArrayType()) {
      PointeeTy = DestTy;
    }

    if (PointeeTy == QualType())
      continue;

    // Always complain about dynamic classes.
    bool IsContained;
    if (const CXXRecordDecl *ContainedRD =
            getContainedDynamicClass(PointeeTy, IsContained)) {

      unsigned OperationType = 0;
      const bool IsCmp = BId == Builtin::BImemcmp || BId == Builtin::BIbcmp;
      // "overwritten" if we're warning about the destination for any call
      // but memcmp; otherwise a verb appropriate to the call.
      if (ArgIdx != 0 || IsCmp) {
        if (BId == Builtin::BImemcpy)
          OperationType = 1;
        else if(BId == Builtin::BImemmove)
          OperationType = 2;
        else if (IsCmp)
          OperationType = 3;
      }

      DiagRuntimeBehavior(Dest->getExprLoc(), Dest,
                          PDiag(diag::warn_dyn_class_memaccess)
                              << (IsCmp ? ArgIdx + 2 : ArgIdx) << FnName
                              << IsContained << ContainedRD << OperationType
                              << Call->getCallee()->getSourceRange());
    } else if (PointeeTy.hasNonTrivialObjCLifetime() &&
             BId != Builtin::BImemset)
      DiagRuntimeBehavior(
        Dest->getExprLoc(), Dest,
        PDiag(diag::warn_arc_object_memaccess)
          << ArgIdx << FnName << PointeeTy
          << Call->getCallee()->getSourceRange());
    else if (const auto *RT = PointeeTy->getAs<RecordType>()) {
      if ((BId == Builtin::BImemset || BId == Builtin::BIbzero) &&
          RT->getDecl()->isNonTrivialToPrimitiveDefaultInitialize()) {
        DiagRuntimeBehavior(Dest->getExprLoc(), Dest,
                            PDiag(diag::warn_cstruct_memaccess)
                                << ArgIdx << FnName << PointeeTy << 0);
        SearchNonTrivialToInitializeField::diag(PointeeTy, Dest, *this);
      } else if ((BId == Builtin::BImemcpy || BId == Builtin::BImemmove) &&
                 RT->getDecl()->isNonTrivialToPrimitiveCopy()) {
        DiagRuntimeBehavior(Dest->getExprLoc(), Dest,
                            PDiag(diag::warn_cstruct_memaccess)
                                << ArgIdx << FnName << PointeeTy << 1);
        SearchNonTrivialToCopyField::diag(PointeeTy, Dest, *this);
      } else {
        continue;
      }
    } else
      continue;

    DiagRuntimeBehavior(
      Dest->getExprLoc(), Dest,
      PDiag(diag::note_bad_memaccess_silence)
        << FixItHint::CreateInsertion(ArgRange.getBegin(), "(void*)"));
    break;
  }
}

// A little helper routine: ignore addition and subtraction of integer literals.
// This intentionally does not ignore all integer constant expressions because
// we don't want to remove sizeof().
static const Expr *ignoreLiteralAdditions(const Expr *Ex, ASTContext &Ctx) {
  Ex = Ex->IgnoreParenCasts();

  while (true) {
    const BinaryOperator * BO = dyn_cast<BinaryOperator>(Ex);
    if (!BO || !BO->isAdditiveOp())
      break;

    const Expr *RHS = BO->getRHS()->IgnoreParenCasts();
    const Expr *LHS = BO->getLHS()->IgnoreParenCasts();

    if (isa<IntegerLiteral>(RHS))
      Ex = LHS;
    else if (isa<IntegerLiteral>(LHS))
      Ex = RHS;
    else
      break;
  }

  return Ex;
}

static bool isConstantSizeArrayWithMoreThanOneElement(QualType Ty,
                                                      ASTContext &Context) {
  // Only handle constant-sized or VLAs, but not flexible members.
  if (const ConstantArrayType *CAT = Context.getAsConstantArrayType(Ty)) {
    // Only issue the FIXIT for arrays of size > 1.
    if (CAT->getZExtSize() <= 1)
      return false;
  } else if (!Ty->isVariableArrayType()) {
    return false;
  }
  return true;
}

void Sema::CheckStrlcpycatArguments(const CallExpr *Call,
                                    IdentifierInfo *FnName) {

  // Don't crash if the user has the wrong number of arguments
  unsigned NumArgs = Call->getNumArgs();
  if ((NumArgs != 3) && (NumArgs != 4))
    return;

  const Expr *SrcArg = ignoreLiteralAdditions(Call->getArg(1), Context);
  const Expr *SizeArg = ignoreLiteralAdditions(Call->getArg(2), Context);
  const Expr *CompareWithSrc = nullptr;

  if (CheckMemorySizeofForComparison(*this, SizeArg, FnName,
                                     Call->getBeginLoc(), Call->getRParenLoc()))
    return;

  // Look for 'strlcpy(dst, x, sizeof(x))'
  if (const Expr *Ex = getSizeOfExprArg(SizeArg))
    CompareWithSrc = Ex;
  else {
    // Look for 'strlcpy(dst, x, strlen(x))'
    if (const CallExpr *SizeCall = dyn_cast<CallExpr>(SizeArg)) {
      if (SizeCall->getBuiltinCallee() == Builtin::BIstrlen &&
          SizeCall->getNumArgs() == 1)
        CompareWithSrc = ignoreLiteralAdditions(SizeCall->getArg(0), Context);
    }
  }

  if (!CompareWithSrc)
    return;

  // Determine if the argument to sizeof/strlen is equal to the source
  // argument.  In principle there's all kinds of things you could do
  // here, for instance creating an == expression and evaluating it with
  // EvaluateAsBooleanCondition, but this uses a more direct technique:
  const DeclRefExpr *SrcArgDRE = dyn_cast<DeclRefExpr>(SrcArg);
  if (!SrcArgDRE)
    return;

  const DeclRefExpr *CompareWithSrcDRE = dyn_cast<DeclRefExpr>(CompareWithSrc);
  if (!CompareWithSrcDRE ||
      SrcArgDRE->getDecl() != CompareWithSrcDRE->getDecl())
    return;

  const Expr *OriginalSizeArg = Call->getArg(2);
  Diag(CompareWithSrcDRE->getBeginLoc(), diag::warn_strlcpycat_wrong_size)
      << OriginalSizeArg->getSourceRange() << FnName;

  // Output a FIXIT hint if the destination is an array (rather than a
  // pointer to an array).  This could be enhanced to handle some
  // pointers if we know the actual size, like if DstArg is 'array+2'
  // we could say 'sizeof(array)-2'.
  const Expr *DstArg = Call->getArg(0)->IgnoreParenImpCasts();
  if (!isConstantSizeArrayWithMoreThanOneElement(DstArg->getType(), Context))
    return;

  SmallString<128> sizeString;
  llvm::raw_svector_ostream OS(sizeString);
  OS << "sizeof(";
  DstArg->printPretty(OS, nullptr, getPrintingPolicy());
  OS << ")";

  Diag(OriginalSizeArg->getBeginLoc(), diag::note_strlcpycat_wrong_size)
      << FixItHint::CreateReplacement(OriginalSizeArg->getSourceRange(),
                                      OS.str());
}

/// Check if two expressions refer to the same declaration.
static bool referToTheSameDecl(const Expr *E1, const Expr *E2) {
  if (const DeclRefExpr *D1 = dyn_cast_or_null<DeclRefExpr>(E1))
    if (const DeclRefExpr *D2 = dyn_cast_or_null<DeclRefExpr>(E2))
      return D1->getDecl() == D2->getDecl();
  return false;
}

static const Expr *getStrlenExprArg(const Expr *E) {
  if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
    const FunctionDecl *FD = CE->getDirectCallee();
    if (!FD || FD->getMemoryFunctionKind() != Builtin::BIstrlen)
      return nullptr;
    return CE->getArg(0)->IgnoreParenCasts();
  }
  return nullptr;
}

void Sema::CheckStrncatArguments(const CallExpr *CE,
                                 IdentifierInfo *FnName) {
  // Don't crash if the user has the wrong number of arguments.
  if (CE->getNumArgs() < 3)
    return;
  const Expr *DstArg = CE->getArg(0)->IgnoreParenCasts();
  const Expr *SrcArg = CE->getArg(1)->IgnoreParenCasts();
  const Expr *LenArg = CE->getArg(2)->IgnoreParenCasts();

  if (CheckMemorySizeofForComparison(*this, LenArg, FnName, CE->getBeginLoc(),
                                     CE->getRParenLoc()))
    return;

  // Identify common expressions, which are wrongly used as the size argument
  // to strncat and may lead to buffer overflows.
  unsigned PatternType = 0;
  if (const Expr *SizeOfArg = getSizeOfExprArg(LenArg)) {
    // - sizeof(dst)
    if (referToTheSameDecl(SizeOfArg, DstArg))
      PatternType = 1;
    // - sizeof(src)
    else if (referToTheSameDecl(SizeOfArg, SrcArg))
      PatternType = 2;
  } else if (const BinaryOperator *BE = dyn_cast<BinaryOperator>(LenArg)) {
    if (BE->getOpcode() == BO_Sub) {
      const Expr *L = BE->getLHS()->IgnoreParenCasts();
      const Expr *R = BE->getRHS()->IgnoreParenCasts();
      // - sizeof(dst) - strlen(dst)
      if (referToTheSameDecl(DstArg, getSizeOfExprArg(L)) &&
          referToTheSameDecl(DstArg, getStrlenExprArg(R)))
        PatternType = 1;
      // - sizeof(src) - (anything)
      else if (referToTheSameDecl(SrcArg, getSizeOfExprArg(L)))
        PatternType = 2;
    }
  }

  if (PatternType == 0)
    return;

  // Generate the diagnostic.
  SourceLocation SL = LenArg->getBeginLoc();
  SourceRange SR = LenArg->getSourceRange();
  SourceManager &SM = getSourceManager();

  // If the function is defined as a builtin macro, do not show macro expansion.
  if (SM.isMacroArgExpansion(SL)) {
    SL = SM.getSpellingLoc(SL);
    SR = SourceRange(SM.getSpellingLoc(SR.getBegin()),
                     SM.getSpellingLoc(SR.getEnd()));
  }

  // Check if the destination is an array (rather than a pointer to an array).
  QualType DstTy = DstArg->getType();
  bool isKnownSizeArray = isConstantSizeArrayWithMoreThanOneElement(DstTy,
                                                                    Context);
  if (!isKnownSizeArray) {
    if (PatternType == 1)
      Diag(SL, diag::warn_strncat_wrong_size) << SR;
    else
      Diag(SL, diag::warn_strncat_src_size) << SR;
    return;
  }

  if (PatternType == 1)
    Diag(SL, diag::warn_strncat_large_size) << SR;
  else
    Diag(SL, diag::warn_strncat_src_size) << SR;

  SmallString<128> sizeString;
  llvm::raw_svector_ostream OS(sizeString);
  OS << "sizeof(";
  DstArg->printPretty(OS, nullptr, getPrintingPolicy());
  OS << ") - ";
  OS << "strlen(";
  DstArg->printPretty(OS, nullptr, getPrintingPolicy());
  OS << ") - 1";

  Diag(SL, diag::note_strncat_wrong_size)
    << FixItHint::CreateReplacement(SR, OS.str());
}

namespace {
void CheckFreeArgumentsOnLvalue(Sema &S, const std::string &CalleeName,
                                const UnaryOperator *UnaryExpr, const Decl *D) {
  if (isa<FieldDecl, FunctionDecl, VarDecl>(D)) {
    S.Diag(UnaryExpr->getBeginLoc(), diag::warn_free_nonheap_object)
        << CalleeName << 0 /*object: */ << cast<NamedDecl>(D);
    return;
  }
}

void CheckFreeArgumentsAddressof(Sema &S, const std::string &CalleeName,
                                 const UnaryOperator *UnaryExpr) {
  if (const auto *Lvalue = dyn_cast<DeclRefExpr>(UnaryExpr->getSubExpr())) {
    const Decl *D = Lvalue->getDecl();
    if (isa<DeclaratorDecl>(D))
      if (!dyn_cast<DeclaratorDecl>(D)->getType()->isReferenceType())
        return CheckFreeArgumentsOnLvalue(S, CalleeName, UnaryExpr, D);
  }

  if (const auto *Lvalue = dyn_cast<MemberExpr>(UnaryExpr->getSubExpr()))
    return CheckFreeArgumentsOnLvalue(S, CalleeName, UnaryExpr,
                                      Lvalue->getMemberDecl());
}

void CheckFreeArgumentsPlus(Sema &S, const std::string &CalleeName,
                            const UnaryOperator *UnaryExpr) {
  const auto *Lambda = dyn_cast<LambdaExpr>(
      UnaryExpr->getSubExpr()->IgnoreImplicitAsWritten()->IgnoreParens());
  if (!Lambda)
    return;

  S.Diag(Lambda->getBeginLoc(), diag::warn_free_nonheap_object)
      << CalleeName << 2 /*object: lambda expression*/;
}

void CheckFreeArgumentsStackArray(Sema &S, const std::string &CalleeName,
                                  const DeclRefExpr *Lvalue) {
  const auto *Var = dyn_cast<VarDecl>(Lvalue->getDecl());
  if (Var == nullptr)
    return;

  S.Diag(Lvalue->getBeginLoc(), diag::warn_free_nonheap_object)
      << CalleeName << 0 /*object: */ << Var;
}

void CheckFreeArgumentsCast(Sema &S, const std::string &CalleeName,
                            const CastExpr *Cast) {
  SmallString<128> SizeString;
  llvm::raw_svector_ostream OS(SizeString);

  clang::CastKind Kind = Cast->getCastKind();
  if (Kind == clang::CK_BitCast &&
      !Cast->getSubExpr()->getType()->isFunctionPointerType())
    return;
  if (Kind == clang::CK_IntegralToPointer &&
      !isa<IntegerLiteral>(
          Cast->getSubExpr()->IgnoreParenImpCasts()->IgnoreParens()))
    return;

  switch (Cast->getCastKind()) {
  case clang::CK_BitCast:
  case clang::CK_IntegralToPointer:
  case clang::CK_FunctionToPointerDecay:
    OS << '\'';
    Cast->printPretty(OS, nullptr, S.getPrintingPolicy());
    OS << '\'';
    break;
  default:
    return;
  }

  S.Diag(Cast->getBeginLoc(), diag::warn_free_nonheap_object)
      << CalleeName << 0 /*object: */ << OS.str();
}
} // namespace

void Sema::CheckFreeArguments(const CallExpr *E) {
  const std::string CalleeName =
      cast<FunctionDecl>(E->getCalleeDecl())->getQualifiedNameAsString();

  { // Prefer something that doesn't involve a cast to make things simpler.
    const Expr *Arg = E->getArg(0)->IgnoreParenCasts();
    if (const auto *UnaryExpr = dyn_cast<UnaryOperator>(Arg))
      switch (UnaryExpr->getOpcode()) {
      case UnaryOperator::Opcode::UO_AddrOf:
        return CheckFreeArgumentsAddressof(*this, CalleeName, UnaryExpr);
      case UnaryOperator::Opcode::UO_Plus:
        return CheckFreeArgumentsPlus(*this, CalleeName, UnaryExpr);
      default:
        break;
      }

    if (const auto *Lvalue = dyn_cast<DeclRefExpr>(Arg))
      if (Lvalue->getType()->isArrayType())
        return CheckFreeArgumentsStackArray(*this, CalleeName, Lvalue);

    if (const auto *Label = dyn_cast<AddrLabelExpr>(Arg)) {
      Diag(Label->getBeginLoc(), diag::warn_free_nonheap_object)
          << CalleeName << 0 /*object: */ << Label->getLabel()->getIdentifier();
      return;
    }

    if (isa<BlockExpr>(Arg)) {
      Diag(Arg->getBeginLoc(), diag::warn_free_nonheap_object)
          << CalleeName << 1 /*object: block*/;
      return;
    }
  }
  // Maybe the cast was important, check after the other cases.
  if (const auto *Cast = dyn_cast<CastExpr>(E->getArg(0)))
    return CheckFreeArgumentsCast(*this, CalleeName, Cast);
}

void
Sema::CheckReturnValExpr(Expr *RetValExp, QualType lhsType,
                         SourceLocation ReturnLoc,
                         bool isObjCMethod,
                         const AttrVec *Attrs,
                         const FunctionDecl *FD) {
  // Check if the return value is null but should not be.
  if (((Attrs && hasSpecificAttr<ReturnsNonNullAttr>(*Attrs)) ||
       (!isObjCMethod && isNonNullType(lhsType))) &&
      CheckNonNullExpr(*this, RetValExp))
    Diag(ReturnLoc, diag::warn_null_ret)
      << (isObjCMethod ? 1 : 0) << RetValExp->getSourceRange();

  // C++11 [basic.stc.dynamic.allocation]p4:
  //   If an allocation function declared with a non-throwing
  //   exception-specification fails to allocate storage, it shall return
  //   a null pointer. Any other allocation function that fails to allocate
  //   storage shall indicate failure only by throwing an exception [...]
  if (FD) {
    OverloadedOperatorKind Op = FD->getOverloadedOperator();
    if (Op == OO_New || Op == OO_Array_New) {
      const FunctionProtoType *Proto
        = FD->getType()->castAs<FunctionProtoType>();
      if (!Proto->isNothrow(/*ResultIfDependent*/true) &&
          CheckNonNullExpr(*this, RetValExp))
        Diag(ReturnLoc, diag::warn_operator_new_returns_null)
          << FD << getLangOpts().CPlusPlus11;
    }
  }

  if (RetValExp && RetValExp->getType()->isWebAssemblyTableType()) {
    Diag(ReturnLoc, diag::err_wasm_table_art) << 1;
  }

  // PPC MMA non-pointer types are not allowed as return type. Checking the type
  // here prevent the user from using a PPC MMA type as trailing return type.
  if (Context.getTargetInfo().getTriple().isPPC64())
    PPC().CheckPPCMMAType(RetValExp->getType(), ReturnLoc);
}

void Sema::CheckFloatComparison(SourceLocation Loc, Expr *LHS, Expr *RHS,
                                BinaryOperatorKind Opcode) {
  if (!BinaryOperator::isEqualityOp(Opcode))
    return;

  // Match and capture subexpressions such as "(float) X == 0.1".
  FloatingLiteral *FPLiteral;
  CastExpr *FPCast;
  auto getCastAndLiteral = [&FPLiteral, &FPCast](Expr *L, Expr *R) {
    FPLiteral = dyn_cast<FloatingLiteral>(L->IgnoreParens());
    FPCast = dyn_cast<CastExpr>(R->IgnoreParens());
    return FPLiteral && FPCast;
  };

  if (getCastAndLiteral(LHS, RHS) || getCastAndLiteral(RHS, LHS)) {
    auto *SourceTy = FPCast->getSubExpr()->getType()->getAs<BuiltinType>();
    auto *TargetTy = FPLiteral->getType()->getAs<BuiltinType>();
    if (SourceTy && TargetTy && SourceTy->isFloatingPoint() &&
        TargetTy->isFloatingPoint()) {
      bool Lossy;
      llvm::APFloat TargetC = FPLiteral->getValue();
      TargetC.convert(Context.getFloatTypeSemantics(QualType(SourceTy, 0)),
                      llvm::APFloat::rmNearestTiesToEven, &Lossy);
      if (Lossy) {
        // If the literal cannot be represented in the source type, then a
        // check for == is always false and check for != is always true.
        Diag(Loc, diag::warn_float_compare_literal)
            << (Opcode == BO_EQ) << QualType(SourceTy, 0)
            << LHS->getSourceRange() << RHS->getSourceRange();
        return;
      }
    }
  }

  // Match a more general floating-point equality comparison (-Wfloat-equal).
  Expr* LeftExprSansParen = LHS->IgnoreParenImpCasts();
  Expr* RightExprSansParen = RHS->IgnoreParenImpCasts();

  // Special case: check for x == x (which is OK).
  // Do not emit warnings for such cases.
  if (auto *DRL = dyn_cast<DeclRefExpr>(LeftExprSansParen))
    if (auto *DRR = dyn_cast<DeclRefExpr>(RightExprSansParen))
      if (DRL->getDecl() == DRR->getDecl())
        return;

  // Special case: check for comparisons against literals that can be exactly
  //  represented by APFloat.  In such cases, do not emit a warning.  This
  //  is a heuristic: often comparison against such literals are used to
  //  detect if a value in a variable has not changed.  This clearly can
  //  lead to false negatives.
  if (FloatingLiteral* FLL = dyn_cast<FloatingLiteral>(LeftExprSansParen)) {
    if (FLL->isExact())
      return;
  } else
    if (FloatingLiteral* FLR = dyn_cast<FloatingLiteral>(RightExprSansParen))
      if (FLR->isExact())
        return;

  // Check for comparisons with builtin types.
  if (CallExpr* CL = dyn_cast<CallExpr>(LeftExprSansParen))
    if (CL->getBuiltinCallee())
      return;

  if (CallExpr* CR = dyn_cast<CallExpr>(RightExprSansParen))
    if (CR->getBuiltinCallee())
      return;

  // Emit the diagnostic.
  Diag(Loc, diag::warn_floatingpoint_eq)
    << LHS->getSourceRange() << RHS->getSourceRange();
}

//===--- CHECK: Integer mixed-sign comparisons (-Wsign-compare) --------===//
//===--- CHECK: Lossy implicit conversions (-Wconversion) --------------===//

namespace {

/// Structure recording the 'active' range of an integer-valued
/// expression.
struct IntRange {
  /// The number of bits active in the int. Note that this includes exactly one
  /// sign bit if !NonNegative.
  unsigned Width;

  /// True if the int is known not to have negative values. If so, all leading
  /// bits before Width are known zero, otherwise they are known to be the
  /// same as the MSB within Width.
  bool NonNegative;

  IntRange(unsigned Width, bool NonNegative)
      : Width(Width), NonNegative(NonNegative) {}

  /// Number of bits excluding the sign bit.
  unsigned valueBits() const {
    return NonNegative ? Width : Width - 1;
  }

  /// Returns the range of the bool type.
  static IntRange forBoolType() {
    return IntRange(1, true);
  }

  /// Returns the range of an opaque value of the given integral type.
  static IntRange forValueOfType(ASTContext &C, QualType T) {
    return forValueOfCanonicalType(C,
                          T->getCanonicalTypeInternal().getTypePtr());
  }

  /// Returns the range of an opaque value of a canonical integral type.
  static IntRange forValueOfCanonicalType(ASTContext &C, const Type *T) {
    assert(T->isCanonicalUnqualified());

    if (const VectorType *VT = dyn_cast<VectorType>(T))
      T = VT->getElementType().getTypePtr();
    if (const ComplexType *CT = dyn_cast<ComplexType>(T))
      T = CT->getElementType().getTypePtr();
    if (const AtomicType *AT = dyn_cast<AtomicType>(T))
      T = AT->getValueType().getTypePtr();

    if (!C.getLangOpts().CPlusPlus) {
      // For enum types in C code, use the underlying datatype.
      if (const EnumType *ET = dyn_cast<EnumType>(T))
        T = ET->getDecl()->getIntegerType().getDesugaredType(C).getTypePtr();
    } else if (const EnumType *ET = dyn_cast<EnumType>(T)) {
      // For enum types in C++, use the known bit width of the enumerators.
      EnumDecl *Enum = ET->getDecl();
      // In C++11, enums can have a fixed underlying type. Use this type to
      // compute the range.
      if (Enum->isFixed()) {
        return IntRange(C.getIntWidth(QualType(T, 0)),
                        !ET->isSignedIntegerOrEnumerationType());
      }

      unsigned NumPositive = Enum->getNumPositiveBits();
      unsigned NumNegative = Enum->getNumNegativeBits();

      if (NumNegative == 0)
        return IntRange(NumPositive, true/*NonNegative*/);
      else
        return IntRange(std::max(NumPositive + 1, NumNegative),
                        false/*NonNegative*/);
    }

    if (const auto *EIT = dyn_cast<BitIntType>(T))
      return IntRange(EIT->getNumBits(), EIT->isUnsigned());

    const BuiltinType *BT = cast<BuiltinType>(T);
    assert(BT->isInteger());

    return IntRange(C.getIntWidth(QualType(T, 0)), BT->isUnsignedInteger());
  }

  /// Returns the "target" range of a canonical integral type, i.e.
  /// the range of values expressible in the type.
  ///
  /// This matches forValueOfCanonicalType except that enums have the
  /// full range of their type, not the range of their enumerators.
  static IntRange forTargetOfCanonicalType(ASTContext &C, const Type *T) {
    assert(T->isCanonicalUnqualified());

    if (const VectorType *VT = dyn_cast<VectorType>(T))
      T = VT->getElementType().getTypePtr();
    if (const ComplexType *CT = dyn_cast<ComplexType>(T))
      T = CT->getElementType().getTypePtr();
    if (const AtomicType *AT = dyn_cast<AtomicType>(T))
      T = AT->getValueType().getTypePtr();
    if (const EnumType *ET = dyn_cast<EnumType>(T))
      T = C.getCanonicalType(ET->getDecl()->getIntegerType()).getTypePtr();

    if (const auto *EIT = dyn_cast<BitIntType>(T))
      return IntRange(EIT->getNumBits(), EIT->isUnsigned());

    const BuiltinType *BT = cast<BuiltinType>(T);
    assert(BT->isInteger());

    return IntRange(C.getIntWidth(QualType(T, 0)), BT->isUnsignedInteger());
  }

  /// Returns the supremum of two ranges: i.e. their conservative merge.
  static IntRange join(IntRange L, IntRange R) {
    bool Unsigned = L.NonNegative && R.NonNegative;
    return IntRange(std::max(L.valueBits(), R.valueBits()) + !Unsigned,
                    L.NonNegative && R.NonNegative);
  }

  /// Return the range of a bitwise-AND of the two ranges.
  static IntRange bit_and(IntRange L, IntRange R) {
    unsigned Bits = std::max(L.Width, R.Width);
    bool NonNegative = false;
    if (L.NonNegative) {
      Bits = std::min(Bits, L.Width);
      NonNegative = true;
    }
    if (R.NonNegative) {
      Bits = std::min(Bits, R.Width);
      NonNegative = true;
    }
    return IntRange(Bits, NonNegative);
  }

  /// Return the range of a sum of the two ranges.
  static IntRange sum(IntRange L, IntRange R) {
    bool Unsigned = L.NonNegative && R.NonNegative;
    return IntRange(std::max(L.valueBits(), R.valueBits()) + 1 + !Unsigned,
                    Unsigned);
  }

  /// Return the range of a difference of the two ranges.
  static IntRange difference(IntRange L, IntRange R) {
    // We need a 1-bit-wider range if:
    //   1) LHS can be negative: least value can be reduced.
    //   2) RHS can be negative: greatest value can be increased.
    bool CanWiden = !L.NonNegative || !R.NonNegative;
    bool Unsigned = L.NonNegative && R.Width == 0;
    return IntRange(std::max(L.valueBits(), R.valueBits()) + CanWiden +
                        !Unsigned,
                    Unsigned);
  }

  /// Return the range of a product of the two ranges.
  static IntRange product(IntRange L, IntRange R) {
    // If both LHS and RHS can be negative, we can form
    //   -2^L * -2^R = 2^(L + R)
    // which requires L + R + 1 value bits to represent.
    bool CanWiden = !L.NonNegative && !R.NonNegative;
    bool Unsigned = L.NonNegative && R.NonNegative;
    return IntRange(L.valueBits() + R.valueBits() + CanWiden + !Unsigned,
                    Unsigned);
  }

  /// Return the range of a remainder operation between the two ranges.
  static IntRange rem(IntRange L, IntRange R) {
    // The result of a remainder can't be larger than the result of
    // either side. The sign of the result is the sign of the LHS.
    bool Unsigned = L.NonNegative;
    return IntRange(std::min(L.valueBits(), R.valueBits()) + !Unsigned,
                    Unsigned);
  }
};

} // namespace

static IntRange GetValueRange(ASTContext &C, llvm::APSInt &value,
                              unsigned MaxWidth) {
  if (value.isSigned() && value.isNegative())
    return IntRange(value.getSignificantBits(), false);

  if (value.getBitWidth() > MaxWidth)
    value = value.trunc(MaxWidth);

  // isNonNegative() just checks the sign bit without considering
  // signedness.
  return IntRange(value.getActiveBits(), true);
}

static IntRange GetValueRange(ASTContext &C, APValue &result, QualType Ty,
                              unsigned MaxWidth) {
  if (result.isInt())
    return GetValueRange(C, result.getInt(), MaxWidth);

  if (result.isVector()) {
    IntRange R = GetValueRange(C, result.getVectorElt(0), Ty, MaxWidth);
    for (unsigned i = 1, e = result.getVectorLength(); i != e; ++i) {
      IntRange El = GetValueRange(C, result.getVectorElt(i), Ty, MaxWidth);
      R = IntRange::join(R, El);
    }
    return R;
  }

  if (result.isComplexInt()) {
    IntRange R = GetValueRange(C, result.getComplexIntReal(), MaxWidth);
    IntRange I = GetValueRange(C, result.getComplexIntImag(), MaxWidth);
    return IntRange::join(R, I);
  }

  // This can happen with lossless casts to intptr_t of "based" lvalues.
  // Assume it might use arbitrary bits.
  // FIXME: The only reason we need to pass the type in here is to get
  // the sign right on this one case.  It would be nice if APValue
  // preserved this.
  assert(result.isLValue() || result.isAddrLabelDiff());
  return IntRange(MaxWidth, Ty->isUnsignedIntegerOrEnumerationType());
}

static QualType GetExprType(const Expr *E) {
  QualType Ty = E->getType();
  if (const AtomicType *AtomicRHS = Ty->getAs<AtomicType>())
    Ty = AtomicRHS->getValueType();
  return Ty;
}

/// Pseudo-evaluate the given integer expression, estimating the
/// range of values it might take.
///
/// \param MaxWidth The width to which the value will be truncated.
/// \param Approximate If \c true, return a likely range for the result: in
///        particular, assume that arithmetic on narrower types doesn't leave
///        those types. If \c false, return a range including all possible
///        result values.
static IntRange GetExprRange(ASTContext &C, const Expr *E, unsigned MaxWidth,
                             bool InConstantContext, bool Approximate) {
  E = E->IgnoreParens();

  // Try a full evaluation first.
  Expr::EvalResult result;
  if (E->EvaluateAsRValue(result, C, InConstantContext))
    return GetValueRange(C, result.Val, GetExprType(E), MaxWidth);

  // I think we only want to look through implicit casts here; if the
  // user has an explicit widening cast, we should treat the value as
  // being of the new, wider type.
  if (const auto *CE = dyn_cast<ImplicitCastExpr>(E)) {
    if (CE->getCastKind() == CK_NoOp || CE->getCastKind() == CK_LValueToRValue)
      return GetExprRange(C, CE->getSubExpr(), MaxWidth, InConstantContext,
                          Approximate);

    IntRange OutputTypeRange = IntRange::forValueOfType(C, GetExprType(CE));

    bool isIntegerCast = CE->getCastKind() == CK_IntegralCast ||
                         CE->getCastKind() == CK_BooleanToSignedIntegral;

    // Assume that non-integer casts can span the full range of the type.
    if (!isIntegerCast)
      return OutputTypeRange;

    IntRange SubRange = GetExprRange(C, CE->getSubExpr(),
                                     std::min(MaxWidth, OutputTypeRange.Width),
                                     InConstantContext, Approximate);

    // Bail out if the subexpr's range is as wide as the cast type.
    if (SubRange.Width >= OutputTypeRange.Width)
      return OutputTypeRange;

    // Otherwise, we take the smaller width, and we're non-negative if
    // either the output type or the subexpr is.
    return IntRange(SubRange.Width,
                    SubRange.NonNegative || OutputTypeRange.NonNegative);
  }

  if (const auto *CO = dyn_cast<ConditionalOperator>(E)) {
    // If we can fold the condition, just take that operand.
    bool CondResult;
    if (CO->getCond()->EvaluateAsBooleanCondition(CondResult, C))
      return GetExprRange(C,
                          CondResult ? CO->getTrueExpr() : CO->getFalseExpr(),
                          MaxWidth, InConstantContext, Approximate);

    // Otherwise, conservatively merge.
    // GetExprRange requires an integer expression, but a throw expression
    // results in a void type.
    Expr *E = CO->getTrueExpr();
    IntRange L = E->getType()->isVoidType()
                     ? IntRange{0, true}
                     : GetExprRange(C, E, MaxWidth, InConstantContext, Approximate);
    E = CO->getFalseExpr();
    IntRange R = E->getType()->isVoidType()
                     ? IntRange{0, true}
                     : GetExprRange(C, E, MaxWidth, InConstantContext, Approximate);
    return IntRange::join(L, R);
  }

  if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
    IntRange (*Combine)(IntRange, IntRange) = IntRange::join;

    switch (BO->getOpcode()) {
    case BO_Cmp:
      llvm_unreachable("builtin <=> should have class type");

    // Boolean-valued operations are single-bit and positive.
    case BO_LAnd:
    case BO_LOr:
    case BO_LT:
    case BO_GT:
    case BO_LE:
    case BO_GE:
    case BO_EQ:
    case BO_NE:
      return IntRange::forBoolType();

    // The type of the assignments is the type of the LHS, so the RHS
    // is not necessarily the same type.
    case BO_MulAssign:
    case BO_DivAssign:
    case BO_RemAssign:
    case BO_AddAssign:
    case BO_SubAssign:
    case BO_XorAssign:
    case BO_OrAssign:
      // TODO: bitfields?
      return IntRange::forValueOfType(C, GetExprType(E));

    // Simple assignments just pass through the RHS, which will have
    // been coerced to the LHS type.
    case BO_Assign:
      // TODO: bitfields?
      return GetExprRange(C, BO->getRHS(), MaxWidth, InConstantContext,
                          Approximate);

    // Operations with opaque sources are black-listed.
    case BO_PtrMemD:
    case BO_PtrMemI:
      return IntRange::forValueOfType(C, GetExprType(E));

    // Bitwise-and uses the *infinum* of the two source ranges.
    case BO_And:
    case BO_AndAssign:
      Combine = IntRange::bit_and;
      break;

    // Left shift gets black-listed based on a judgement call.
    case BO_Shl:
      // ...except that we want to treat '1 << (blah)' as logically
      // positive.  It's an important idiom.
      if (IntegerLiteral *I
            = dyn_cast<IntegerLiteral>(BO->getLHS()->IgnoreParenCasts())) {
        if (I->getValue() == 1) {
          IntRange R = IntRange::forValueOfType(C, GetExprType(E));
          return IntRange(R.Width, /*NonNegative*/ true);
        }
      }
      [[fallthrough]];

    case BO_ShlAssign:
      return IntRange::forValueOfType(C, GetExprType(E));

    // Right shift by a constant can narrow its left argument.
    case BO_Shr:
    case BO_ShrAssign: {
      IntRange L = GetExprRange(C, BO->getLHS(), MaxWidth, InConstantContext,
                                Approximate);

      // If the shift amount is a positive constant, drop the width by
      // that much.
      if (std::optional<llvm::APSInt> shift =
              BO->getRHS()->getIntegerConstantExpr(C)) {
        if (shift->isNonNegative()) {
          if (shift->uge(L.Width))
            L.Width = (L.NonNegative ? 0 : 1);
          else
            L.Width -= shift->getZExtValue();
        }
      }

      return L;
    }

    // Comma acts as its right operand.
    case BO_Comma:
      return GetExprRange(C, BO->getRHS(), MaxWidth, InConstantContext,
                          Approximate);

    case BO_Add:
      if (!Approximate)
        Combine = IntRange::sum;
      break;

    case BO_Sub:
      if (BO->getLHS()->getType()->isPointerType())
        return IntRange::forValueOfType(C, GetExprType(E));
      if (!Approximate)
        Combine = IntRange::difference;
      break;

    case BO_Mul:
      if (!Approximate)
        Combine = IntRange::product;
      break;

    // The width of a division result is mostly determined by the size
    // of the LHS.
    case BO_Div: {
      // Don't 'pre-truncate' the operands.
      unsigned opWidth = C.getIntWidth(GetExprType(E));
      IntRange L = GetExprRange(C, BO->getLHS(), opWidth, InConstantContext,
                                Approximate);

      // If the divisor is constant, use that.
      if (std::optional<llvm::APSInt> divisor =
              BO->getRHS()->getIntegerConstantExpr(C)) {
        unsigned log2 = divisor->logBase2(); // floor(log_2(divisor))
        if (log2 >= L.Width)
          L.Width = (L.NonNegative ? 0 : 1);
        else
          L.Width = std::min(L.Width - log2, MaxWidth);
        return L;
      }

      // Otherwise, just use the LHS's width.
      // FIXME: This is wrong if the LHS could be its minimal value and the RHS
      // could be -1.
      IntRange R = GetExprRange(C, BO->getRHS(), opWidth, InConstantContext,
                                Approximate);
      return IntRange(L.Width, L.NonNegative && R.NonNegative);
    }

    case BO_Rem:
      Combine = IntRange::rem;
      break;

    // The default behavior is okay for these.
    case BO_Xor:
    case BO_Or:
      break;
    }

    // Combine the two ranges, but limit the result to the type in which we
    // performed the computation.
    QualType T = GetExprType(E);
    unsigned opWidth = C.getIntWidth(T);
    IntRange L =
        GetExprRange(C, BO->getLHS(), opWidth, InConstantContext, Approximate);
    IntRange R =
        GetExprRange(C, BO->getRHS(), opWidth, InConstantContext, Approximate);
    IntRange C = Combine(L, R);
    C.NonNegative |= T->isUnsignedIntegerOrEnumerationType();
    C.Width = std::min(C.Width, MaxWidth);
    return C;
  }

  if (const auto *UO = dyn_cast<UnaryOperator>(E)) {
    switch (UO->getOpcode()) {
    // Boolean-valued operations are white-listed.
    case UO_LNot:
      return IntRange::forBoolType();

    // Operations with opaque sources are black-listed.
    case UO_Deref:
    case UO_AddrOf: // should be impossible
      return IntRange::forValueOfType(C, GetExprType(E));

    default:
      return GetExprRange(C, UO->getSubExpr(), MaxWidth, InConstantContext,
                          Approximate);
    }
  }

  if (const auto *OVE = dyn_cast<OpaqueValueExpr>(E))
    return GetExprRange(C, OVE->getSourceExpr(), MaxWidth, InConstantContext,
                        Approximate);

  if (const auto *BitField = E->getSourceBitField())
    return IntRange(BitField->getBitWidthValue(C),
                    BitField->getType()->isUnsignedIntegerOrEnumerationType());

  return IntRange::forValueOfType(C, GetExprType(E));
}

static IntRange GetExprRange(ASTContext &C, const Expr *E,
                             bool InConstantContext, bool Approximate) {
  return GetExprRange(C, E, C.getIntWidth(GetExprType(E)), InConstantContext,
                      Approximate);
}

/// Checks whether the given value, which currently has the given
/// source semantics, has the same value when coerced through the
/// target semantics.
static bool IsSameFloatAfterCast(const llvm::APFloat &value,
                                 const llvm::fltSemantics &Src,
                                 const llvm::fltSemantics &Tgt) {
  llvm::APFloat truncated = value;

  bool ignored;
  truncated.convert(Src, llvm::APFloat::rmNearestTiesToEven, &ignored);
  truncated.convert(Tgt, llvm::APFloat::rmNearestTiesToEven, &ignored);

  return truncated.bitwiseIsEqual(value);
}

/// Checks whether the given value, which currently has the given
/// source semantics, has the same value when coerced through the
/// target semantics.
///
/// The value might be a vector of floats (or a complex number).
static bool IsSameFloatAfterCast(const APValue &value,
                                 const llvm::fltSemantics &Src,
                                 const llvm::fltSemantics &Tgt) {
  if (value.isFloat())
    return IsSameFloatAfterCast(value.getFloat(), Src, Tgt);

  if (value.isVector()) {
    for (unsigned i = 0, e = value.getVectorLength(); i != e; ++i)
      if (!IsSameFloatAfterCast(value.getVectorElt(i), Src, Tgt))
        return false;
    return true;
  }

  assert(value.isComplexFloat());
  return (IsSameFloatAfterCast(value.getComplexFloatReal(), Src, Tgt) &&
          IsSameFloatAfterCast(value.getComplexFloatImag(), Src, Tgt));
}

static void AnalyzeImplicitConversions(Sema &S, Expr *E, SourceLocation CC,
                                       bool IsListInit = false);

static bool IsEnumConstOrFromMacro(Sema &S, Expr *E) {
  // Suppress cases where we are comparing against an enum constant.
  if (const DeclRefExpr *DR =
      dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts()))
    if (isa<EnumConstantDecl>(DR->getDecl()))
      return true;

  // Suppress cases where the value is expanded from a macro, unless that macro
  // is how a language represents a boolean literal. This is the case in both C
  // and Objective-C.
  SourceLocation BeginLoc = E->getBeginLoc();
  if (BeginLoc.isMacroID()) {
    StringRef MacroName = Lexer::getImmediateMacroName(
        BeginLoc, S.getSourceManager(), S.getLangOpts());
    return MacroName != "YES" && MacroName != "NO" &&
           MacroName != "true" && MacroName != "false";
  }

  return false;
}

static bool isKnownToHaveUnsignedValue(Expr *E) {
  return E->getType()->isIntegerType() &&
         (!E->getType()->isSignedIntegerType() ||
          !E->IgnoreParenImpCasts()->getType()->isSignedIntegerType());
}

namespace {
/// The promoted range of values of a type. In general this has the
/// following structure:
///
///     |-----------| . . . |-----------|
///     ^           ^       ^           ^
///    Min       HoleMin  HoleMax      Max
///
/// ... where there is only a hole if a signed type is promoted to unsigned
/// (in which case Min and Max are the smallest and largest representable
/// values).
struct PromotedRange {
  // Min, or HoleMax if there is a hole.
  llvm::APSInt PromotedMin;
  // Max, or HoleMin if there is a hole.
  llvm::APSInt PromotedMax;

  PromotedRange(IntRange R, unsigned BitWidth, bool Unsigned) {
    if (R.Width == 0)
      PromotedMin = PromotedMax = llvm::APSInt(BitWidth, Unsigned);
    else if (R.Width >= BitWidth && !Unsigned) {
      // Promotion made the type *narrower*. This happens when promoting
      // a < 32-bit unsigned / <= 32-bit signed bit-field to 'signed int'.
      // Treat all values of 'signed int' as being in range for now.
      PromotedMin = llvm::APSInt::getMinValue(BitWidth, Unsigned);
      PromotedMax = llvm::APSInt::getMaxValue(BitWidth, Unsigned);
    } else {
      PromotedMin = llvm::APSInt::getMinValue(R.Width, R.NonNegative)
                        .extOrTrunc(BitWidth);
      PromotedMin.setIsUnsigned(Unsigned);

      PromotedMax = llvm::APSInt::getMaxValue(R.Width, R.NonNegative)
                        .extOrTrunc(BitWidth);
      PromotedMax.setIsUnsigned(Unsigned);
    }
  }

  // Determine whether this range is contiguous (has no hole).
  bool isContiguous() const { return PromotedMin <= PromotedMax; }

  // Where a constant value is within the range.
  enum ComparisonResult {
    LT = 0x1,
    LE = 0x2,
    GT = 0x4,
    GE = 0x8,
    EQ = 0x10,
    NE = 0x20,
    InRangeFlag = 0x40,

    Less = LE | LT | NE,
    Min = LE | InRangeFlag,
    InRange = InRangeFlag,
    Max = GE | InRangeFlag,
    Greater = GE | GT | NE,

    OnlyValue = LE | GE | EQ | InRangeFlag,
    InHole = NE
  };

  ComparisonResult compare(const llvm::APSInt &Value) const {
    assert(Value.getBitWidth() == PromotedMin.getBitWidth() &&
           Value.isUnsigned() == PromotedMin.isUnsigned());
    if (!isContiguous()) {
      assert(Value.isUnsigned() && "discontiguous range for signed compare");
      if (Value.isMinValue()) return Min;
      if (Value.isMaxValue()) return Max;
      if (Value >= PromotedMin) return InRange;
      if (Value <= PromotedMax) return InRange;
      return InHole;
    }

    switch (llvm::APSInt::compareValues(Value, PromotedMin)) {
    case -1: return Less;
    case 0: return PromotedMin == PromotedMax ? OnlyValue : Min;
    case 1:
      switch (llvm::APSInt::compareValues(Value, PromotedMax)) {
      case -1: return InRange;
      case 0: return Max;
      case 1: return Greater;
      }
    }

    llvm_unreachable("impossible compare result");
  }

  static std::optional<StringRef>
  constantValue(BinaryOperatorKind Op, ComparisonResult R, bool ConstantOnRHS) {
    if (Op == BO_Cmp) {
      ComparisonResult LTFlag = LT, GTFlag = GT;
      if (ConstantOnRHS) std::swap(LTFlag, GTFlag);

      if (R & EQ) return StringRef("'std::strong_ordering::equal'");
      if (R & LTFlag) return StringRef("'std::strong_ordering::less'");
      if (R & GTFlag) return StringRef("'std::strong_ordering::greater'");
      return std::nullopt;
    }

    ComparisonResult TrueFlag, FalseFlag;
    if (Op == BO_EQ) {
      TrueFlag = EQ;
      FalseFlag = NE;
    } else if (Op == BO_NE) {
      TrueFlag = NE;
      FalseFlag = EQ;
    } else {
      if ((Op == BO_LT || Op == BO_GE) ^ ConstantOnRHS) {
        TrueFlag = LT;
        FalseFlag = GE;
      } else {
        TrueFlag = GT;
        FalseFlag = LE;
      }
      if (Op == BO_GE || Op == BO_LE)
        std::swap(TrueFlag, FalseFlag);
    }
    if (R & TrueFlag)
      return StringRef("true");
    if (R & FalseFlag)
      return StringRef("false");
    return std::nullopt;
  }
};
}

static bool HasEnumType(Expr *E) {
  // Strip off implicit integral promotions.
  while (ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(E)) {
    if (ICE->getCastKind() != CK_IntegralCast &&
        ICE->getCastKind() != CK_NoOp)
      break;
    E = ICE->getSubExpr();
  }

  return E->getType()->isEnumeralType();
}

static int classifyConstantValue(Expr *Constant) {
  // The values of this enumeration are used in the diagnostics
  // diag::warn_out_of_range_compare and diag::warn_tautological_bool_compare.
  enum ConstantValueKind {
    Miscellaneous = 0,
    LiteralTrue,
    LiteralFalse
  };
  if (auto *BL = dyn_cast<CXXBoolLiteralExpr>(Constant))
    return BL->getValue() ? ConstantValueKind::LiteralTrue
                          : ConstantValueKind::LiteralFalse;
  return ConstantValueKind::Miscellaneous;
}

static bool CheckTautologicalComparison(Sema &S, BinaryOperator *E,
                                        Expr *Constant, Expr *Other,
                                        const llvm::APSInt &Value,
                                        bool RhsConstant) {
  if (S.inTemplateInstantiation())
    return false;

  Expr *OriginalOther = Other;

  Constant = Constant->IgnoreParenImpCasts();
  Other = Other->IgnoreParenImpCasts();

  // Suppress warnings on tautological comparisons between values of the same
  // enumeration type. There are only two ways we could warn on this:
  //  - If the constant is outside the range of representable values of
  //    the enumeration. In such a case, we should warn about the cast
  //    to enumeration type, not about the comparison.
  //  - If the constant is the maximum / minimum in-range value. For an
  //    enumeratin type, such comparisons can be meaningful and useful.
  if (Constant->getType()->isEnumeralType() &&
      S.Context.hasSameUnqualifiedType(Constant->getType(), Other->getType()))
    return false;

  IntRange OtherValueRange = GetExprRange(
      S.Context, Other, S.isConstantEvaluatedContext(), /*Approximate=*/false);

  QualType OtherT = Other->getType();
  if (const auto *AT = OtherT->getAs<AtomicType>())
    OtherT = AT->getValueType();
  IntRange OtherTypeRange = IntRange::forValueOfType(S.Context, OtherT);

  // Special case for ObjC BOOL on targets where its a typedef for a signed char
  // (Namely, macOS). FIXME: IntRange::forValueOfType should do this.
  bool IsObjCSignedCharBool = S.getLangOpts().ObjC &&
                              S.ObjC().NSAPIObj->isObjCBOOLType(OtherT) &&
                              OtherT->isSpecificBuiltinType(BuiltinType::SChar);

  // Whether we're treating Other as being a bool because of the form of
  // expression despite it having another type (typically 'int' in C).
  bool OtherIsBooleanDespiteType =
      !OtherT->isBooleanType() && Other->isKnownToHaveBooleanValue();
  if (OtherIsBooleanDespiteType || IsObjCSignedCharBool)
    OtherTypeRange = OtherValueRange = IntRange::forBoolType();

  // Check if all values in the range of possible values of this expression
  // lead to the same comparison outcome.
  PromotedRange OtherPromotedValueRange(OtherValueRange, Value.getBitWidth(),
                                        Value.isUnsigned());
  auto Cmp = OtherPromotedValueRange.compare(Value);
  auto Result = PromotedRange::constantValue(E->getOpcode(), Cmp, RhsConstant);
  if (!Result)
    return false;

  // Also consider the range determined by the type alone. This allows us to
  // classify the warning under the proper diagnostic group.
  bool TautologicalTypeCompare = false;
  {
    PromotedRange OtherPromotedTypeRange(OtherTypeRange, Value.getBitWidth(),
                                         Value.isUnsigned());
    auto TypeCmp = OtherPromotedTypeRange.compare(Value);
    if (auto TypeResult = PromotedRange::constantValue(E->getOpcode(), TypeCmp,
                                                       RhsConstant)) {
      TautologicalTypeCompare = true;
      Cmp = TypeCmp;
      Result = TypeResult;
    }
  }

  // Don't warn if the non-constant operand actually always evaluates to the
  // same value.
  if (!TautologicalTypeCompare && OtherValueRange.Width == 0)
    return false;

  // Suppress the diagnostic for an in-range comparison if the constant comes
  // from a macro or enumerator. We don't want to diagnose
  //
  //   some_long_value <= INT_MAX
  //
  // when sizeof(int) == sizeof(long).
  bool InRange = Cmp & PromotedRange::InRangeFlag;
  if (InRange && IsEnumConstOrFromMacro(S, Constant))
    return false;

  // A comparison of an unsigned bit-field against 0 is really a type problem,
  // even though at the type level the bit-field might promote to 'signed int'.
  if (Other->refersToBitField() && InRange && Value == 0 &&
      Other->getType()->isUnsignedIntegerOrEnumerationType())
    TautologicalTypeCompare = true;

  // If this is a comparison to an enum constant, include that
  // constant in the diagnostic.
  const EnumConstantDecl *ED = nullptr;
  if (const DeclRefExpr *DR = dyn_cast<DeclRefExpr>(Constant))
    ED = dyn_cast<EnumConstantDecl>(DR->getDecl());

  // Should be enough for uint128 (39 decimal digits)
  SmallString<64> PrettySourceValue;
  llvm::raw_svector_ostream OS(PrettySourceValue);
  if (ED) {
    OS << '\'' << *ED << "' (" << Value << ")";
  } else if (auto *BL = dyn_cast<ObjCBoolLiteralExpr>(
               Constant->IgnoreParenImpCasts())) {
    OS << (BL->getValue() ? "YES" : "NO");
  } else {
    OS << Value;
  }

  if (!TautologicalTypeCompare) {
    S.Diag(E->getOperatorLoc(), diag::warn_tautological_compare_value_range)
        << RhsConstant << OtherValueRange.Width << OtherValueRange.NonNegative
        << E->getOpcodeStr() << OS.str() << *Result
        << E->getLHS()->getSourceRange() << E->getRHS()->getSourceRange();
    return true;
  }

  if (IsObjCSignedCharBool) {
    S.DiagRuntimeBehavior(E->getOperatorLoc(), E,
                          S.PDiag(diag::warn_tautological_compare_objc_bool)
                              << OS.str() << *Result);
    return true;
  }

  // FIXME: We use a somewhat different formatting for the in-range cases and
  // cases involving boolean values for historical reasons. We should pick a
  // consistent way of presenting these diagnostics.
  if (!InRange || Other->isKnownToHaveBooleanValue()) {

    S.DiagRuntimeBehavior(
        E->getOperatorLoc(), E,
        S.PDiag(!InRange ? diag::warn_out_of_range_compare
                         : diag::warn_tautological_bool_compare)
            << OS.str() << classifyConstantValue(Constant) << OtherT
            << OtherIsBooleanDespiteType << *Result
            << E->getLHS()->getSourceRange() << E->getRHS()->getSourceRange());
  } else {
    bool IsCharTy = OtherT.withoutLocalFastQualifiers() == S.Context.CharTy;
    unsigned Diag =
        (isKnownToHaveUnsignedValue(OriginalOther) && Value == 0)
            ? (HasEnumType(OriginalOther)
                   ? diag::warn_unsigned_enum_always_true_comparison
                   : IsCharTy ? diag::warn_unsigned_char_always_true_comparison
                              : diag::warn_unsigned_always_true_comparison)
            : diag::warn_tautological_constant_compare;

    S.Diag(E->getOperatorLoc(), Diag)
        << RhsConstant << OtherT << E->getOpcodeStr() << OS.str() << *Result
        << E->getLHS()->getSourceRange() << E->getRHS()->getSourceRange();
  }

  return true;
}

/// Analyze the operands of the given comparison.  Implements the
/// fallback case from AnalyzeComparison.
static void AnalyzeImpConvsInComparison(Sema &S, BinaryOperator *E) {
  AnalyzeImplicitConversions(S, E->getLHS(), E->getOperatorLoc());
  AnalyzeImplicitConversions(S, E->getRHS(), E->getOperatorLoc());
}

/// Implements -Wsign-compare.
///
/// \param E the binary operator to check for warnings
static void AnalyzeComparison(Sema &S, BinaryOperator *E) {
  // The type the comparison is being performed in.
  QualType T = E->getLHS()->getType();

  // Only analyze comparison operators where both sides have been converted to
  // the same type.
  if (!S.Context.hasSameUnqualifiedType(T, E->getRHS()->getType()))
    return AnalyzeImpConvsInComparison(S, E);

  // Don't analyze value-dependent comparisons directly.
  if (E->isValueDependent())
    return AnalyzeImpConvsInComparison(S, E);

  Expr *LHS = E->getLHS();
  Expr *RHS = E->getRHS();

  if (T->isIntegralType(S.Context)) {
    std::optional<llvm::APSInt> RHSValue =
        RHS->getIntegerConstantExpr(S.Context);
    std::optional<llvm::APSInt> LHSValue =
        LHS->getIntegerConstantExpr(S.Context);

    // We don't care about expressions whose result is a constant.
    if (RHSValue && LHSValue)
      return AnalyzeImpConvsInComparison(S, E);

    // We only care about expressions where just one side is literal
    if ((bool)RHSValue ^ (bool)LHSValue) {
      // Is the constant on the RHS or LHS?
      const bool RhsConstant = (bool)RHSValue;
      Expr *Const = RhsConstant ? RHS : LHS;
      Expr *Other = RhsConstant ? LHS : RHS;
      const llvm::APSInt &Value = RhsConstant ? *RHSValue : *LHSValue;

      // Check whether an integer constant comparison results in a value
      // of 'true' or 'false'.
      if (CheckTautologicalComparison(S, E, Const, Other, Value, RhsConstant))
        return AnalyzeImpConvsInComparison(S, E);
    }
  }

  if (!T->hasUnsignedIntegerRepresentation()) {
    // We don't do anything special if this isn't an unsigned integral
    // comparison:  we're only interested in integral comparisons, and
    // signed comparisons only happen in cases we don't care to warn about.
    return AnalyzeImpConvsInComparison(S, E);
  }

  LHS = LHS->IgnoreParenImpCasts();
  RHS = RHS->IgnoreParenImpCasts();

  if (!S.getLangOpts().CPlusPlus) {
    // Avoid warning about comparison of integers with different signs when
    // RHS/LHS has a `typeof(E)` type whose sign is different from the sign of
    // the type of `E`.
    if (const auto *TET = dyn_cast<TypeOfExprType>(LHS->getType()))
      LHS = TET->getUnderlyingExpr()->IgnoreParenImpCasts();
    if (const auto *TET = dyn_cast<TypeOfExprType>(RHS->getType()))
      RHS = TET->getUnderlyingExpr()->IgnoreParenImpCasts();
  }

  // Check to see if one of the (unmodified) operands is of different
  // signedness.
  Expr *signedOperand, *unsignedOperand;
  if (LHS->getType()->hasSignedIntegerRepresentation()) {
    assert(!RHS->getType()->hasSignedIntegerRepresentation() &&
           "unsigned comparison between two signed integer expressions?");
    signedOperand = LHS;
    unsignedOperand = RHS;
  } else if (RHS->getType()->hasSignedIntegerRepresentation()) {
    signedOperand = RHS;
    unsignedOperand = LHS;
  } else {
    return AnalyzeImpConvsInComparison(S, E);
  }

  // Otherwise, calculate the effective range of the signed operand.
  IntRange signedRange =
      GetExprRange(S.Context, signedOperand, S.isConstantEvaluatedContext(),
                   /*Approximate=*/true);

  // Go ahead and analyze implicit conversions in the operands.  Note
  // that we skip the implicit conversions on both sides.
  AnalyzeImplicitConversions(S, LHS, E->getOperatorLoc());
  AnalyzeImplicitConversions(S, RHS, E->getOperatorLoc());

  // If the signed range is non-negative, -Wsign-compare won't fire.
  if (signedRange.NonNegative)
    return;

  // For (in)equality comparisons, if the unsigned operand is a
  // constant which cannot collide with a overflowed signed operand,
  // then reinterpreting the signed operand as unsigned will not
  // change the result of the comparison.
  if (E->isEqualityOp()) {
    unsigned comparisonWidth = S.Context.getIntWidth(T);
    IntRange unsignedRange =
        GetExprRange(S.Context, unsignedOperand, S.isConstantEvaluatedContext(),
                     /*Approximate=*/true);

    // We should never be unable to prove that the unsigned operand is
    // non-negative.
    assert(unsignedRange.NonNegative && "unsigned range includes negative?");

    if (unsignedRange.Width < comparisonWidth)
      return;
  }

  S.DiagRuntimeBehavior(E->getOperatorLoc(), E,
                        S.PDiag(diag::warn_mixed_sign_comparison)
                            << LHS->getType() << RHS->getType()
                            << LHS->getSourceRange() << RHS->getSourceRange());
}

/// Analyzes an attempt to assign the given value to a bitfield.
///
/// Returns true if there was something fishy about the attempt.
static bool AnalyzeBitFieldAssignment(Sema &S, FieldDecl *Bitfield, Expr *Init,
                                      SourceLocation InitLoc) {
  assert(Bitfield->isBitField());
  if (Bitfield->isInvalidDecl())
    return false;

  // White-list bool bitfields.
  QualType BitfieldType = Bitfield->getType();
  if (BitfieldType->isBooleanType())
     return false;

  if (BitfieldType->isEnumeralType()) {
    EnumDecl *BitfieldEnumDecl = BitfieldType->castAs<EnumType>()->getDecl();
    // If the underlying enum type was not explicitly specified as an unsigned
    // type and the enum contain only positive values, MSVC++ will cause an
    // inconsistency by storing this as a signed type.
    if (S.getLangOpts().CPlusPlus11 &&
        !BitfieldEnumDecl->getIntegerTypeSourceInfo() &&
        BitfieldEnumDecl->getNumPositiveBits() > 0 &&
        BitfieldEnumDecl->getNumNegativeBits() == 0) {
      S.Diag(InitLoc, diag::warn_no_underlying_type_specified_for_enum_bitfield)
          << BitfieldEnumDecl;
    }
  }

  // Ignore value- or type-dependent expressions.
  if (Bitfield->getBitWidth()->isValueDependent() ||
      Bitfield->getBitWidth()->isTypeDependent() ||
      Init->isValueDependent() ||
      Init->isTypeDependent())
    return false;

  Expr *OriginalInit = Init->IgnoreParenImpCasts();
  unsigned FieldWidth = Bitfield->getBitWidthValue(S.Context);

  Expr::EvalResult Result;
  if (!OriginalInit->EvaluateAsInt(Result, S.Context,
                                   Expr::SE_AllowSideEffects)) {
    // The RHS is not constant.  If the RHS has an enum type, make sure the
    // bitfield is wide enough to hold all the values of the enum without
    // truncation.
    if (const auto *EnumTy = OriginalInit->getType()->getAs<EnumType>()) {
      EnumDecl *ED = EnumTy->getDecl();
      bool SignedBitfield = BitfieldType->isSignedIntegerType();

      // Enum types are implicitly signed on Windows, so check if there are any
      // negative enumerators to see if the enum was intended to be signed or
      // not.
      bool SignedEnum = ED->getNumNegativeBits() > 0;

      // Check for surprising sign changes when assigning enum values to a
      // bitfield of different signedness.  If the bitfield is signed and we
      // have exactly the right number of bits to store this unsigned enum,
      // suggest changing the enum to an unsigned type. This typically happens
      // on Windows where unfixed enums always use an underlying type of 'int'.
      unsigned DiagID = 0;
      if (SignedEnum && !SignedBitfield) {
        DiagID = diag::warn_unsigned_bitfield_assigned_signed_enum;
      } else if (SignedBitfield && !SignedEnum &&
                 ED->getNumPositiveBits() == FieldWidth) {
        DiagID = diag::warn_signed_bitfield_enum_conversion;
      }

      if (DiagID) {
        S.Diag(InitLoc, DiagID) << Bitfield << ED;
        TypeSourceInfo *TSI = Bitfield->getTypeSourceInfo();
        SourceRange TypeRange =
            TSI ? TSI->getTypeLoc().getSourceRange() : SourceRange();
        S.Diag(Bitfield->getTypeSpecStartLoc(), diag::note_change_bitfield_sign)
            << SignedEnum << TypeRange;
      }

      // Compute the required bitwidth. If the enum has negative values, we need
      // one more bit than the normal number of positive bits to represent the
      // sign bit.
      unsigned BitsNeeded = SignedEnum ? std::max(ED->getNumPositiveBits() + 1,
                                                  ED->getNumNegativeBits())
                                       : ED->getNumPositiveBits();

      // Check the bitwidth.
      if (BitsNeeded > FieldWidth) {
        Expr *WidthExpr = Bitfield->getBitWidth();
        S.Diag(InitLoc, diag::warn_bitfield_too_small_for_enum)
            << Bitfield << ED;
        S.Diag(WidthExpr->getExprLoc(), diag::note_widen_bitfield)
            << BitsNeeded << ED << WidthExpr->getSourceRange();
      }
    }

    return false;
  }

  llvm::APSInt Value = Result.Val.getInt();

  unsigned OriginalWidth = Value.getBitWidth();

  // In C, the macro 'true' from stdbool.h will evaluate to '1'; To reduce
  // false positives where the user is demonstrating they intend to use the
  // bit-field as a Boolean, check to see if the value is 1 and we're assigning
  // to a one-bit bit-field to see if the value came from a macro named 'true'.
  bool OneAssignedToOneBitBitfield = FieldWidth == 1 && Value == 1;
  if (OneAssignedToOneBitBitfield && !S.LangOpts.CPlusPlus) {
    SourceLocation MaybeMacroLoc = OriginalInit->getBeginLoc();
    if (S.SourceMgr.isInSystemMacro(MaybeMacroLoc) &&
        S.findMacroSpelling(MaybeMacroLoc, "true"))
      return false;
  }

  if (!Value.isSigned() || Value.isNegative())
    if (UnaryOperator *UO = dyn_cast<UnaryOperator>(OriginalInit))
      if (UO->getOpcode() == UO_Minus || UO->getOpcode() == UO_Not)
        OriginalWidth = Value.getSignificantBits();

  if (OriginalWidth <= FieldWidth)
    return false;

  // Compute the value which the bitfield will contain.
  llvm::APSInt TruncatedValue = Value.trunc(FieldWidth);
  TruncatedValue.setIsSigned(BitfieldType->isSignedIntegerType());

  // Check whether the stored value is equal to the original value.
  TruncatedValue = TruncatedValue.extend(OriginalWidth);
  if (llvm::APSInt::isSameValue(Value, TruncatedValue))
    return false;

  std::string PrettyValue = toString(Value, 10);
  std::string PrettyTrunc = toString(TruncatedValue, 10);

  S.Diag(InitLoc, OneAssignedToOneBitBitfield
                      ? diag::warn_impcast_single_bit_bitield_precision_constant
                      : diag::warn_impcast_bitfield_precision_constant)
      << PrettyValue << PrettyTrunc << OriginalInit->getType()
      << Init->getSourceRange();

  return true;
}

/// Analyze the given simple or compound assignment for warning-worthy
/// operations.
static void AnalyzeAssignment(Sema &S, BinaryOperator *E) {
  // Just recurse on the LHS.
  AnalyzeImplicitConversions(S, E->getLHS(), E->getOperatorLoc());

  // We want to recurse on the RHS as normal unless we're assigning to
  // a bitfield.
  if (FieldDecl *Bitfield = E->getLHS()->getSourceBitField()) {
    if (AnalyzeBitFieldAssignment(S, Bitfield, E->getRHS(),
                                  E->getOperatorLoc())) {
      // Recurse, ignoring any implicit conversions on the RHS.
      return AnalyzeImplicitConversions(S, E->getRHS()->IgnoreParenImpCasts(),
                                        E->getOperatorLoc());
    }
  }

  AnalyzeImplicitConversions(S, E->getRHS(), E->getOperatorLoc());

  // Diagnose implicitly sequentially-consistent atomic assignment.
  if (E->getLHS()->getType()->isAtomicType())
    S.Diag(E->getRHS()->getBeginLoc(), diag::warn_atomic_implicit_seq_cst);
}

/// Diagnose an implicit cast;  purely a helper for CheckImplicitConversion.
static void DiagnoseImpCast(Sema &S, Expr *E, QualType SourceType, QualType T,
                            SourceLocation CContext, unsigned diag,
                            bool pruneControlFlow = false) {
  if (pruneControlFlow) {
    S.DiagRuntimeBehavior(E->getExprLoc(), E,
                          S.PDiag(diag)
                              << SourceType << T << E->getSourceRange()
                              << SourceRange(CContext));
    return;
  }
  S.Diag(E->getExprLoc(), diag)
    << SourceType << T << E->getSourceRange() << SourceRange(CContext);
}

/// Diagnose an implicit cast;  purely a helper for CheckImplicitConversion.
static void DiagnoseImpCast(Sema &S, Expr *E, QualType T,
                            SourceLocation CContext,
                            unsigned diag, bool pruneControlFlow = false) {
  DiagnoseImpCast(S, E, E->getType(), T, CContext, diag, pruneControlFlow);
}

/// Diagnose an implicit cast from a floating point value to an integer value.
static void DiagnoseFloatingImpCast(Sema &S, Expr *E, QualType T,
                                    SourceLocation CContext) {
  const bool IsBool = T->isSpecificBuiltinType(BuiltinType::Bool);
  const bool PruneWarnings = S.inTemplateInstantiation();

  Expr *InnerE = E->IgnoreParenImpCasts();
  // We also want to warn on, e.g., "int i = -1.234"
  if (UnaryOperator *UOp = dyn_cast<UnaryOperator>(InnerE))
    if (UOp->getOpcode() == UO_Minus || UOp->getOpcode() == UO_Plus)
      InnerE = UOp->getSubExpr()->IgnoreParenImpCasts();

  const bool IsLiteral =
      isa<FloatingLiteral>(E) || isa<FloatingLiteral>(InnerE);

  llvm::APFloat Value(0.0);
  bool IsConstant =
    E->EvaluateAsFloat(Value, S.Context, Expr::SE_AllowSideEffects);
  if (!IsConstant) {
    if (S.ObjC().isSignedCharBool(T)) {
      return S.ObjC().adornBoolConversionDiagWithTernaryFixit(
          E, S.Diag(CContext, diag::warn_impcast_float_to_objc_signed_char_bool)
                 << E->getType());
    }

    return DiagnoseImpCast(S, E, T, CContext,
                           diag::warn_impcast_float_integer, PruneWarnings);
  }

  bool isExact = false;

  llvm::APSInt IntegerValue(S.Context.getIntWidth(T),
                            T->hasUnsignedIntegerRepresentation());
  llvm::APFloat::opStatus Result = Value.convertToInteger(
      IntegerValue, llvm::APFloat::rmTowardZero, &isExact);

  // FIXME: Force the precision of the source value down so we don't print
  // digits which are usually useless (we don't really care here if we
  // truncate a digit by accident in edge cases).  Ideally, APFloat::toString
  // would automatically print the shortest representation, but it's a bit
  // tricky to implement.
  SmallString<16> PrettySourceValue;
  unsigned precision = llvm::APFloat::semanticsPrecision(Value.getSemantics());
  precision = (precision * 59 + 195) / 196;
  Value.toString(PrettySourceValue, precision);

  if (S.ObjC().isSignedCharBool(T) && IntegerValue != 0 && IntegerValue != 1) {
    return S.ObjC().adornBoolConversionDiagWithTernaryFixit(
        E, S.Diag(CContext, diag::warn_impcast_constant_value_to_objc_bool)
               << PrettySourceValue);
  }

  if (Result == llvm::APFloat::opOK && isExact) {
    if (IsLiteral) return;
    return DiagnoseImpCast(S, E, T, CContext, diag::warn_impcast_float_integer,
                           PruneWarnings);
  }

  // Conversion of a floating-point value to a non-bool integer where the
  // integral part cannot be represented by the integer type is undefined.
  if (!IsBool && Result == llvm::APFloat::opInvalidOp)
    return DiagnoseImpCast(
        S, E, T, CContext,
        IsLiteral ? diag::warn_impcast_literal_float_to_integer_out_of_range
                  : diag::warn_impcast_float_to_integer_out_of_range,
        PruneWarnings);

  unsigned DiagID = 0;
  if (IsLiteral) {
    // Warn on floating point literal to integer.
    DiagID = diag::warn_impcast_literal_float_to_integer;
  } else if (IntegerValue == 0) {
    if (Value.isZero()) {  // Skip -0.0 to 0 conversion.
      return DiagnoseImpCast(S, E, T, CContext,
                             diag::warn_impcast_float_integer, PruneWarnings);
    }
    // Warn on non-zero to zero conversion.
    DiagID = diag::warn_impcast_float_to_integer_zero;
  } else {
    if (IntegerValue.isUnsigned()) {
      if (!IntegerValue.isMaxValue()) {
        return DiagnoseImpCast(S, E, T, CContext,
                               diag::warn_impcast_float_integer, PruneWarnings);
      }
    } else {  // IntegerValue.isSigned()
      if (!IntegerValue.isMaxSignedValue() &&
          !IntegerValue.isMinSignedValue()) {
        return DiagnoseImpCast(S, E, T, CContext,
                               diag::warn_impcast_float_integer, PruneWarnings);
      }
    }
    // Warn on evaluatable floating point expression to integer conversion.
    DiagID = diag::warn_impcast_float_to_integer;
  }

  SmallString<16> PrettyTargetValue;
  if (IsBool)
    PrettyTargetValue = Value.isZero() ? "false" : "true";
  else
    IntegerValue.toString(PrettyTargetValue);

  if (PruneWarnings) {
    S.DiagRuntimeBehavior(E->getExprLoc(), E,
                          S.PDiag(DiagID)
                              << E->getType() << T.getUnqualifiedType()
                              << PrettySourceValue << PrettyTargetValue
                              << E->getSourceRange() << SourceRange(CContext));
  } else {
    S.Diag(E->getExprLoc(), DiagID)
        << E->getType() << T.getUnqualifiedType() << PrettySourceValue
        << PrettyTargetValue << E->getSourceRange() << SourceRange(CContext);
  }
}

/// Analyze the given compound assignment for the possible losing of
/// floating-point precision.
static void AnalyzeCompoundAssignment(Sema &S, BinaryOperator *E) {
  assert(isa<CompoundAssignOperator>(E) &&
         "Must be compound assignment operation");
  // Recurse on the LHS and RHS in here
  AnalyzeImplicitConversions(S, E->getLHS(), E->getOperatorLoc());
  AnalyzeImplicitConversions(S, E->getRHS(), E->getOperatorLoc());

  if (E->getLHS()->getType()->isAtomicType())
    S.Diag(E->getOperatorLoc(), diag::warn_atomic_implicit_seq_cst);

  // Now check the outermost expression
  const auto *ResultBT = E->getLHS()->getType()->getAs<BuiltinType>();
  const auto *RBT = cast<CompoundAssignOperator>(E)
                        ->getComputationResultType()
                        ->getAs<BuiltinType>();

  // The below checks assume source is floating point.
  if (!ResultBT || !RBT || !RBT->isFloatingPoint()) return;

  // If source is floating point but target is an integer.
  if (ResultBT->isInteger())
    return DiagnoseImpCast(S, E, E->getRHS()->getType(), E->getLHS()->getType(),
                           E->getExprLoc(), diag::warn_impcast_float_integer);

  if (!ResultBT->isFloatingPoint())
    return;

  // If both source and target are floating points, warn about losing precision.
  int Order = S.getASTContext().getFloatingTypeSemanticOrder(
      QualType(ResultBT, 0), QualType(RBT, 0));
  if (Order < 0 && !S.SourceMgr.isInSystemMacro(E->getOperatorLoc()))
    // warn about dropping FP rank.
    DiagnoseImpCast(S, E->getRHS(), E->getLHS()->getType(), E->getOperatorLoc(),
                    diag::warn_impcast_float_result_precision);
}

static std::string PrettyPrintInRange(const llvm::APSInt &Value,
                                      IntRange Range) {
  if (!Range.Width) return "0";

  llvm::APSInt ValueInRange = Value;
  ValueInRange.setIsSigned(!Range.NonNegative);
  ValueInRange = ValueInRange.trunc(Range.Width);
  return toString(ValueInRange, 10);
}

static bool IsImplicitBoolFloatConversion(Sema &S, Expr *Ex, bool ToBool) {
  if (!isa<ImplicitCastExpr>(Ex))
    return false;

  Expr *InnerE = Ex->IgnoreParenImpCasts();
  const Type *Target = S.Context.getCanonicalType(Ex->getType()).getTypePtr();
  const Type *Source =
    S.Context.getCanonicalType(InnerE->getType()).getTypePtr();
  if (Target->isDependentType())
    return false;

  const BuiltinType *FloatCandidateBT =
    dyn_cast<BuiltinType>(ToBool ? Source : Target);
  const Type *BoolCandidateType = ToBool ? Target : Source;

  return (BoolCandidateType->isSpecificBuiltinType(BuiltinType::Bool) &&
          FloatCandidateBT && (FloatCandidateBT->isFloatingPoint()));
}

static void CheckImplicitArgumentConversions(Sema &S, CallExpr *TheCall,
                                             SourceLocation CC) {
  unsigned NumArgs = TheCall->getNumArgs();
  for (unsigned i = 0; i < NumArgs; ++i) {
    Expr *CurrA = TheCall->getArg(i);
    if (!IsImplicitBoolFloatConversion(S, CurrA, true))
      continue;

    bool IsSwapped = ((i > 0) &&
        IsImplicitBoolFloatConversion(S, TheCall->getArg(i - 1), false));
    IsSwapped |= ((i < (NumArgs - 1)) &&
        IsImplicitBoolFloatConversion(S, TheCall->getArg(i + 1), false));
    if (IsSwapped) {
      // Warn on this floating-point to bool conversion.
      DiagnoseImpCast(S, CurrA->IgnoreParenImpCasts(),
                      CurrA->getType(), CC,
                      diag::warn_impcast_floating_point_to_bool);
    }
  }
}

static void DiagnoseNullConversion(Sema &S, Expr *E, QualType T,
                                   SourceLocation CC) {
  if (S.Diags.isIgnored(diag::warn_impcast_null_pointer_to_integer,
                        E->getExprLoc()))
    return;

  // Don't warn on functions which have return type nullptr_t.
  if (isa<CallExpr>(E))
    return;

  // Check for NULL (GNUNull) or nullptr (CXX11_nullptr).
  const Expr *NewE = E->IgnoreParenImpCasts();
  bool IsGNUNullExpr = isa<GNUNullExpr>(NewE);
  bool HasNullPtrType = NewE->getType()->isNullPtrType();
  if (!IsGNUNullExpr && !HasNullPtrType)
    return;

  // Return if target type is a safe conversion.
  if (T->isAnyPointerType() || T->isBlockPointerType() ||
      T->isMemberPointerType() || !T->isScalarType() || T->isNullPtrType())
    return;

  SourceLocation Loc = E->getSourceRange().getBegin();

  // Venture through the macro stacks to get to the source of macro arguments.
  // The new location is a better location than the complete location that was
  // passed in.
  Loc = S.SourceMgr.getTopMacroCallerLoc(Loc);
  CC = S.SourceMgr.getTopMacroCallerLoc(CC);

  // __null is usually wrapped in a macro.  Go up a macro if that is the case.
  if (IsGNUNullExpr && Loc.isMacroID()) {
    StringRef MacroName = Lexer::getImmediateMacroNameForDiagnostics(
        Loc, S.SourceMgr, S.getLangOpts());
    if (MacroName == "NULL")
      Loc = S.SourceMgr.getImmediateExpansionRange(Loc).getBegin();
  }

  // Only warn if the null and context location are in the same macro expansion.
  if (S.SourceMgr.getFileID(Loc) != S.SourceMgr.getFileID(CC))
    return;

  S.Diag(Loc, diag::warn_impcast_null_pointer_to_integer)
      << HasNullPtrType << T << SourceRange(CC)
      << FixItHint::CreateReplacement(Loc,
                                      S.getFixItZeroLiteralForType(T, Loc));
}

// Helper function to filter out cases for constant width constant conversion.
// Don't warn on char array initialization or for non-decimal values.
static bool isSameWidthConstantConversion(Sema &S, Expr *E, QualType T,
                                          SourceLocation CC) {
  // If initializing from a constant, and the constant starts with '0',
  // then it is a binary, octal, or hexadecimal.  Allow these constants
  // to fill all the bits, even if there is a sign change.
  if (auto *IntLit = dyn_cast<IntegerLiteral>(E->IgnoreParenImpCasts())) {
    const char FirstLiteralCharacter =
        S.getSourceManager().getCharacterData(IntLit->getBeginLoc())[0];
    if (FirstLiteralCharacter == '0')
      return false;
  }

  // If the CC location points to a '{', and the type is char, then assume
  // assume it is an array initialization.
  if (CC.isValid() && T->isCharType()) {
    const char FirstContextCharacter =
        S.getSourceManager().getCharacterData(CC)[0];
    if (FirstContextCharacter == '{')
      return false;
  }

  return true;
}

static const IntegerLiteral *getIntegerLiteral(Expr *E) {
  const auto *IL = dyn_cast<IntegerLiteral>(E);
  if (!IL) {
    if (auto *UO = dyn_cast<UnaryOperator>(E)) {
      if (UO->getOpcode() == UO_Minus)
        return dyn_cast<IntegerLiteral>(UO->getSubExpr());
    }
  }

  return IL;
}

static void DiagnoseIntInBoolContext(Sema &S, Expr *E) {
  E = E->IgnoreParenImpCasts();
  SourceLocation ExprLoc = E->getExprLoc();

  if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
    BinaryOperator::Opcode Opc = BO->getOpcode();
    Expr::EvalResult Result;
    // Do not diagnose unsigned shifts.
    if (Opc == BO_Shl) {
      const auto *LHS = getIntegerLiteral(BO->getLHS());
      const auto *RHS = getIntegerLiteral(BO->getRHS());
      if (LHS && LHS->getValue() == 0)
        S.Diag(ExprLoc, diag::warn_left_shift_always) << 0;
      else if (!E->isValueDependent() && LHS && RHS &&
               RHS->getValue().isNonNegative() &&
               E->EvaluateAsInt(Result, S.Context, Expr::SE_AllowSideEffects))
        S.Diag(ExprLoc, diag::warn_left_shift_always)
            << (Result.Val.getInt() != 0);
      else if (E->getType()->isSignedIntegerType())
        S.Diag(ExprLoc, diag::warn_left_shift_in_bool_context) << E;
    }
  }

  if (const auto *CO = dyn_cast<ConditionalOperator>(E)) {
    const auto *LHS = getIntegerLiteral(CO->getTrueExpr());
    const auto *RHS = getIntegerLiteral(CO->getFalseExpr());
    if (!LHS || !RHS)
      return;
    if ((LHS->getValue() == 0 || LHS->getValue() == 1) &&
        (RHS->getValue() == 0 || RHS->getValue() == 1))
      // Do not diagnose common idioms.
      return;
    if (LHS->getValue() != 0 && RHS->getValue() != 0)
      S.Diag(ExprLoc, diag::warn_integer_constants_in_conditional_always_true);
  }
}

void Sema::CheckImplicitConversion(Expr *E, QualType T, SourceLocation CC,
                                   bool *ICContext, bool IsListInit) {
  if (E->isTypeDependent() || E->isValueDependent()) return;

  const Type *Source = Context.getCanonicalType(E->getType()).getTypePtr();
  const Type *Target = Context.getCanonicalType(T).getTypePtr();
  if (Source == Target) return;
  if (Target->isDependentType()) return;

  // If the conversion context location is invalid don't complain. We also
  // don't want to emit a warning if the issue occurs from the expansion of
  // a system macro. The problem is that 'getSpellingLoc()' is slow, so we
  // delay this check as long as possible. Once we detect we are in that
  // scenario, we just return.
  if (CC.isInvalid())
    return;

  if (Source->isAtomicType())
    Diag(E->getExprLoc(), diag::warn_atomic_implicit_seq_cst);

  // Diagnose implicit casts to bool.
  if (Target->isSpecificBuiltinType(BuiltinType::Bool)) {
    if (isa<StringLiteral>(E))
      // Warn on string literal to bool.  Checks for string literals in logical
      // and expressions, for instance, assert(0 && "error here"), are
      // prevented by a check in AnalyzeImplicitConversions().
      return DiagnoseImpCast(*this, E, T, CC,
                             diag::warn_impcast_string_literal_to_bool);
    if (isa<ObjCStringLiteral>(E) || isa<ObjCArrayLiteral>(E) ||
        isa<ObjCDictionaryLiteral>(E) || isa<ObjCBoxedExpr>(E)) {
      // This covers the literal expressions that evaluate to Objective-C
      // objects.
      return DiagnoseImpCast(*this, E, T, CC,
                             diag::warn_impcast_objective_c_literal_to_bool);
    }
    if (Source->isPointerType() || Source->canDecayToPointerType()) {
      // Warn on pointer to bool conversion that is always true.
      DiagnoseAlwaysNonNullPointer(E, Expr::NPCK_NotNull, /*IsEqual*/ false,
                                   SourceRange(CC));
    }
  }

  // If the we're converting a constant to an ObjC BOOL on a platform where BOOL
  // is a typedef for signed char (macOS), then that constant value has to be 1
  // or 0.
  if (ObjC().isSignedCharBool(T) && Source->isIntegralType(Context)) {
    Expr::EvalResult Result;
    if (E->EvaluateAsInt(Result, getASTContext(), Expr::SE_AllowSideEffects)) {
      if (Result.Val.getInt() != 1 && Result.Val.getInt() != 0) {
        ObjC().adornBoolConversionDiagWithTernaryFixit(
            E, Diag(CC, diag::warn_impcast_constant_value_to_objc_bool)
                   << toString(Result.Val.getInt(), 10));
      }
      return;
    }
  }

  // Check implicit casts from Objective-C collection literals to specialized
  // collection types, e.g., NSArray<NSString *> *.
  if (auto *ArrayLiteral = dyn_cast<ObjCArrayLiteral>(E))
    ObjC().checkArrayLiteral(QualType(Target, 0), ArrayLiteral);
  else if (auto *DictionaryLiteral = dyn_cast<ObjCDictionaryLiteral>(E))
    ObjC().checkDictionaryLiteral(QualType(Target, 0), DictionaryLiteral);

  // Strip vector types.
  if (isa<VectorType>(Source)) {
    if (Target->isSveVLSBuiltinType() &&
        (Context.areCompatibleSveTypes(QualType(Target, 0),
                                       QualType(Source, 0)) ||
         Context.areLaxCompatibleSveTypes(QualType(Target, 0),
                                          QualType(Source, 0))))
      return;

    if (Target->isRVVVLSBuiltinType() &&
        (Context.areCompatibleRVVTypes(QualType(Target, 0),
                                       QualType(Source, 0)) ||
         Context.areLaxCompatibleRVVTypes(QualType(Target, 0),
                                          QualType(Source, 0))))
      return;

    if (!isa<VectorType>(Target)) {
      if (SourceMgr.isInSystemMacro(CC))
        return;
      return DiagnoseImpCast(*this, E, T, CC, diag::warn_impcast_vector_scalar);
    } else if (getLangOpts().HLSL &&
               Target->castAs<VectorType>()->getNumElements() <
                   Source->castAs<VectorType>()->getNumElements()) {
      // Diagnose vector truncation but don't return. We may also want to
      // diagnose an element conversion.
      DiagnoseImpCast(*this, E, T, CC,
                      diag::warn_hlsl_impcast_vector_truncation);
    }

    // If the vector cast is cast between two vectors of the same size, it is
    // a bitcast, not a conversion, except under HLSL where it is a conversion.
    if (!getLangOpts().HLSL &&
        Context.getTypeSize(Source) == Context.getTypeSize(Target))
      return;

    Source = cast<VectorType>(Source)->getElementType().getTypePtr();
    Target = cast<VectorType>(Target)->getElementType().getTypePtr();
  }
  if (auto VecTy = dyn_cast<VectorType>(Target))
    Target = VecTy->getElementType().getTypePtr();

  // Strip complex types.
  if (isa<ComplexType>(Source)) {
    if (!isa<ComplexType>(Target)) {
      if (SourceMgr.isInSystemMacro(CC) || Target->isBooleanType())
        return;

      return DiagnoseImpCast(*this, E, T, CC,
                             getLangOpts().CPlusPlus
                                 ? diag::err_impcast_complex_scalar
                                 : diag::warn_impcast_complex_scalar);
    }

    Source = cast<ComplexType>(Source)->getElementType().getTypePtr();
    Target = cast<ComplexType>(Target)->getElementType().getTypePtr();
  }

  const BuiltinType *SourceBT = dyn_cast<BuiltinType>(Source);
  const BuiltinType *TargetBT = dyn_cast<BuiltinType>(Target);

  // Strip SVE vector types
  if (SourceBT && SourceBT->isSveVLSBuiltinType()) {
    // Need the original target type for vector type checks
    const Type *OriginalTarget = Context.getCanonicalType(T).getTypePtr();
    // Handle conversion from scalable to fixed when msve-vector-bits is
    // specified
    if (Context.areCompatibleSveTypes(QualType(OriginalTarget, 0),
                                      QualType(Source, 0)) ||
        Context.areLaxCompatibleSveTypes(QualType(OriginalTarget, 0),
                                         QualType(Source, 0)))
      return;

    // If the vector cast is cast between two vectors of the same size, it is
    // a bitcast, not a conversion.
    if (Context.getTypeSize(Source) == Context.getTypeSize(Target))
      return;

    Source = SourceBT->getSveEltType(Context).getTypePtr();
  }

  if (TargetBT && TargetBT->isSveVLSBuiltinType())
    Target = TargetBT->getSveEltType(Context).getTypePtr();

  // If the source is floating point...
  if (SourceBT && SourceBT->isFloatingPoint()) {
    // ...and the target is floating point...
    if (TargetBT && TargetBT->isFloatingPoint()) {
      // ...then warn if we're dropping FP rank.

      int Order = getASTContext().getFloatingTypeSemanticOrder(
          QualType(SourceBT, 0), QualType(TargetBT, 0));
      if (Order > 0) {
        // Don't warn about float constants that are precisely
        // representable in the target type.
        Expr::EvalResult result;
        if (E->EvaluateAsRValue(result, Context)) {
          // Value might be a float, a float vector, or a float complex.
          if (IsSameFloatAfterCast(
                  result.Val,
                  Context.getFloatTypeSemantics(QualType(TargetBT, 0)),
                  Context.getFloatTypeSemantics(QualType(SourceBT, 0))))
            return;
        }

        if (SourceMgr.isInSystemMacro(CC))
          return;

        DiagnoseImpCast(*this, E, T, CC, diag::warn_impcast_float_precision);
      }
      // ... or possibly if we're increasing rank, too
      else if (Order < 0) {
        if (SourceMgr.isInSystemMacro(CC))
          return;

        DiagnoseImpCast(*this, E, T, CC, diag::warn_impcast_double_promotion);
      }
      return;
    }

    // If the target is integral, always warn.
    if (TargetBT && TargetBT->isInteger()) {
      if (SourceMgr.isInSystemMacro(CC))
        return;

      DiagnoseFloatingImpCast(*this, E, T, CC);
    }

    // Detect the case where a call result is converted from floating-point to
    // to bool, and the final argument to the call is converted from bool, to
    // discover this typo:
    //
    //    bool b = fabs(x < 1.0);  // should be "bool b = fabs(x) < 1.0;"
    //
    // FIXME: This is an incredibly special case; is there some more general
    // way to detect this class of misplaced-parentheses bug?
    if (Target->isBooleanType() && isa<CallExpr>(E)) {
      // Check last argument of function call to see if it is an
      // implicit cast from a type matching the type the result
      // is being cast to.
      CallExpr *CEx = cast<CallExpr>(E);
      if (unsigned NumArgs = CEx->getNumArgs()) {
        Expr *LastA = CEx->getArg(NumArgs - 1);
        Expr *InnerE = LastA->IgnoreParenImpCasts();
        if (isa<ImplicitCastExpr>(LastA) &&
            InnerE->getType()->isBooleanType()) {
          // Warn on this floating-point to bool conversion
          DiagnoseImpCast(*this, E, T, CC,
                          diag::warn_impcast_floating_point_to_bool);
        }
      }
    }
    return;
  }

  // Valid casts involving fixed point types should be accounted for here.
  if (Source->isFixedPointType()) {
    if (Target->isUnsaturatedFixedPointType()) {
      Expr::EvalResult Result;
      if (E->EvaluateAsFixedPoint(Result, Context, Expr::SE_AllowSideEffects,
                                  isConstantEvaluatedContext())) {
        llvm::APFixedPoint Value = Result.Val.getFixedPoint();
        llvm::APFixedPoint MaxVal = Context.getFixedPointMax(T);
        llvm::APFixedPoint MinVal = Context.getFixedPointMin(T);
        if (Value > MaxVal || Value < MinVal) {
          DiagRuntimeBehavior(E->getExprLoc(), E,
                              PDiag(diag::warn_impcast_fixed_point_range)
                                  << Value.toString() << T
                                  << E->getSourceRange()
                                  << clang::SourceRange(CC));
          return;
        }
      }
    } else if (Target->isIntegerType()) {
      Expr::EvalResult Result;
      if (!isConstantEvaluatedContext() &&
          E->EvaluateAsFixedPoint(Result, Context, Expr::SE_AllowSideEffects)) {
        llvm::APFixedPoint FXResult = Result.Val.getFixedPoint();

        bool Overflowed;
        llvm::APSInt IntResult = FXResult.convertToInt(
            Context.getIntWidth(T), Target->isSignedIntegerOrEnumerationType(),
            &Overflowed);

        if (Overflowed) {
          DiagRuntimeBehavior(E->getExprLoc(), E,
                              PDiag(diag::warn_impcast_fixed_point_range)
                                  << FXResult.toString() << T
                                  << E->getSourceRange()
                                  << clang::SourceRange(CC));
          return;
        }
      }
    }
  } else if (Target->isUnsaturatedFixedPointType()) {
    if (Source->isIntegerType()) {
      Expr::EvalResult Result;
      if (!isConstantEvaluatedContext() &&
          E->EvaluateAsInt(Result, Context, Expr::SE_AllowSideEffects)) {
        llvm::APSInt Value = Result.Val.getInt();

        bool Overflowed;
        llvm::APFixedPoint IntResult = llvm::APFixedPoint::getFromIntValue(
            Value, Context.getFixedPointSemantics(T), &Overflowed);

        if (Overflowed) {
          DiagRuntimeBehavior(E->getExprLoc(), E,
                              PDiag(diag::warn_impcast_fixed_point_range)
                                  << toString(Value, /*Radix=*/10) << T
                                  << E->getSourceRange()
                                  << clang::SourceRange(CC));
          return;
        }
      }
    }
  }

  // If we are casting an integer type to a floating point type without
  // initialization-list syntax, we might lose accuracy if the floating
  // point type has a narrower significand than the integer type.
  if (SourceBT && TargetBT && SourceBT->isIntegerType() &&
      TargetBT->isFloatingType() && !IsListInit) {
    // Determine the number of precision bits in the source integer type.
    IntRange SourceRange =
        GetExprRange(Context, E, isConstantEvaluatedContext(),
                     /*Approximate=*/true);
    unsigned int SourcePrecision = SourceRange.Width;

    // Determine the number of precision bits in the
    // target floating point type.
    unsigned int TargetPrecision = llvm::APFloatBase::semanticsPrecision(
        Context.getFloatTypeSemantics(QualType(TargetBT, 0)));

    if (SourcePrecision > 0 && TargetPrecision > 0 &&
        SourcePrecision > TargetPrecision) {

      if (std::optional<llvm::APSInt> SourceInt =
              E->getIntegerConstantExpr(Context)) {
        // If the source integer is a constant, convert it to the target
        // floating point type. Issue a warning if the value changes
        // during the whole conversion.
        llvm::APFloat TargetFloatValue(
            Context.getFloatTypeSemantics(QualType(TargetBT, 0)));
        llvm::APFloat::opStatus ConversionStatus =
            TargetFloatValue.convertFromAPInt(
                *SourceInt, SourceBT->isSignedInteger(),
                llvm::APFloat::rmNearestTiesToEven);

        if (ConversionStatus != llvm::APFloat::opOK) {
          SmallString<32> PrettySourceValue;
          SourceInt->toString(PrettySourceValue, 10);
          SmallString<32> PrettyTargetValue;
          TargetFloatValue.toString(PrettyTargetValue, TargetPrecision);

          DiagRuntimeBehavior(
              E->getExprLoc(), E,
              PDiag(diag::warn_impcast_integer_float_precision_constant)
                  << PrettySourceValue << PrettyTargetValue << E->getType() << T
                  << E->getSourceRange() << clang::SourceRange(CC));
        }
      } else {
        // Otherwise, the implicit conversion may lose precision.
        DiagnoseImpCast(*this, E, T, CC,
                        diag::warn_impcast_integer_float_precision);
      }
    }
  }

  DiagnoseNullConversion(*this, E, T, CC);

  DiscardMisalignedMemberAddress(Target, E);

  if (Target->isBooleanType())
    DiagnoseIntInBoolContext(*this, E);

  if (!Source->isIntegerType() || !Target->isIntegerType())
    return;

  // TODO: remove this early return once the false positives for constant->bool
  // in templates, macros, etc, are reduced or removed.
  if (Target->isSpecificBuiltinType(BuiltinType::Bool))
    return;

  if (ObjC().isSignedCharBool(T) && !Source->isCharType() &&
      !E->isKnownToHaveBooleanValue(/*Semantic=*/false)) {
    return ObjC().adornBoolConversionDiagWithTernaryFixit(
        E, Diag(CC, diag::warn_impcast_int_to_objc_signed_char_bool)
               << E->getType());
  }

  IntRange SourceTypeRange =
      IntRange::forTargetOfCanonicalType(Context, Source);
  IntRange LikelySourceRange = GetExprRange(
      Context, E, isConstantEvaluatedContext(), /*Approximate=*/true);
  IntRange TargetRange = IntRange::forTargetOfCanonicalType(Context, Target);

  if (LikelySourceRange.Width > TargetRange.Width) {
    // If the source is a constant, use a default-on diagnostic.
    // TODO: this should happen for bitfield stores, too.
    Expr::EvalResult Result;
    if (E->EvaluateAsInt(Result, Context, Expr::SE_AllowSideEffects,
                         isConstantEvaluatedContext())) {
      llvm::APSInt Value(32);
      Value = Result.Val.getInt();

      if (SourceMgr.isInSystemMacro(CC))
        return;

      std::string PrettySourceValue = toString(Value, 10);
      std::string PrettyTargetValue = PrettyPrintInRange(Value, TargetRange);

      DiagRuntimeBehavior(E->getExprLoc(), E,
                          PDiag(diag::warn_impcast_integer_precision_constant)
                              << PrettySourceValue << PrettyTargetValue
                              << E->getType() << T << E->getSourceRange()
                              << SourceRange(CC));
      return;
    }

    // People want to build with -Wshorten-64-to-32 and not -Wconversion.
    if (SourceMgr.isInSystemMacro(CC))
      return;

    if (TargetRange.Width == 32 && Context.getIntWidth(E->getType()) == 64)
      return DiagnoseImpCast(*this, E, T, CC, diag::warn_impcast_integer_64_32,
                             /* pruneControlFlow */ true);
    return DiagnoseImpCast(*this, E, T, CC,
                           diag::warn_impcast_integer_precision);
  }

  if (TargetRange.Width > SourceTypeRange.Width) {
    if (auto *UO = dyn_cast<UnaryOperator>(E))
      if (UO->getOpcode() == UO_Minus)
        if (Source->isUnsignedIntegerType()) {
          if (Target->isUnsignedIntegerType())
            return DiagnoseImpCast(*this, E, T, CC,
                                   diag::warn_impcast_high_order_zero_bits);
          if (Target->isSignedIntegerType())
            return DiagnoseImpCast(*this, E, T, CC,
                                   diag::warn_impcast_nonnegative_result);
        }
  }

  if (TargetRange.Width == LikelySourceRange.Width &&
      !TargetRange.NonNegative && LikelySourceRange.NonNegative &&
      Source->isSignedIntegerType()) {
    // Warn when doing a signed to signed conversion, warn if the positive
    // source value is exactly the width of the target type, which will
    // cause a negative value to be stored.

    Expr::EvalResult Result;
    if (E->EvaluateAsInt(Result, Context, Expr::SE_AllowSideEffects) &&
        !SourceMgr.isInSystemMacro(CC)) {
      llvm::APSInt Value = Result.Val.getInt();
      if (isSameWidthConstantConversion(*this, E, T, CC)) {
        std::string PrettySourceValue = toString(Value, 10);
        std::string PrettyTargetValue = PrettyPrintInRange(Value, TargetRange);

        Diag(E->getExprLoc(),
             PDiag(diag::warn_impcast_integer_precision_constant)
                 << PrettySourceValue << PrettyTargetValue << E->getType() << T
                 << E->getSourceRange() << SourceRange(CC));
        return;
      }
    }

    // Fall through for non-constants to give a sign conversion warning.
  }

  if ((!isa<EnumType>(Target) || !isa<EnumType>(Source)) &&
      ((TargetRange.NonNegative && !LikelySourceRange.NonNegative) ||
       (!TargetRange.NonNegative && LikelySourceRange.NonNegative &&
        LikelySourceRange.Width == TargetRange.Width))) {
    if (SourceMgr.isInSystemMacro(CC))
      return;

    if (SourceBT && SourceBT->isInteger() && TargetBT &&
        TargetBT->isInteger() &&
        Source->isSignedIntegerType() == Target->isSignedIntegerType()) {
      return;
    }

    unsigned DiagID = diag::warn_impcast_integer_sign;

    // Traditionally, gcc has warned about this under -Wsign-compare.
    // We also want to warn about it in -Wconversion.
    // So if -Wconversion is off, use a completely identical diagnostic
    // in the sign-compare group.
    // The conditional-checking code will
    if (ICContext) {
      DiagID = diag::warn_impcast_integer_sign_conditional;
      *ICContext = true;
    }

    return DiagnoseImpCast(*this, E, T, CC, DiagID);
  }

  // Diagnose conversions between different enumeration types.
  // In C, we pretend that the type of an EnumConstantDecl is its enumeration
  // type, to give us better diagnostics.
  QualType SourceType = E->getEnumCoercedType(Context);
  Source = Context.getCanonicalType(SourceType).getTypePtr();

  if (const EnumType *SourceEnum = Source->getAs<EnumType>())
    if (const EnumType *TargetEnum = Target->getAs<EnumType>())
      if (SourceEnum->getDecl()->hasNameForLinkage() &&
          TargetEnum->getDecl()->hasNameForLinkage() &&
          SourceEnum != TargetEnum) {
        if (SourceMgr.isInSystemMacro(CC))
          return;

        return DiagnoseImpCast(*this, E, SourceType, T, CC,
                               diag::warn_impcast_different_enum_types);
      }
}

static void CheckConditionalOperator(Sema &S, AbstractConditionalOperator *E,
                                     SourceLocation CC, QualType T);

static void CheckConditionalOperand(Sema &S, Expr *E, QualType T,
                                    SourceLocation CC, bool &ICContext) {
  E = E->IgnoreParenImpCasts();
  // Diagnose incomplete type for second or third operand in C.
  if (!S.getLangOpts().CPlusPlus && E->getType()->isRecordType())
    S.RequireCompleteExprType(E, diag::err_incomplete_type);

  if (auto *CO = dyn_cast<AbstractConditionalOperator>(E))
    return CheckConditionalOperator(S, CO, CC, T);

  AnalyzeImplicitConversions(S, E, CC);
  if (E->getType() != T)
    return S.CheckImplicitConversion(E, T, CC, &ICContext);
}

static void CheckConditionalOperator(Sema &S, AbstractConditionalOperator *E,
                                     SourceLocation CC, QualType T) {
  AnalyzeImplicitConversions(S, E->getCond(), E->getQuestionLoc());

  Expr *TrueExpr = E->getTrueExpr();
  if (auto *BCO = dyn_cast<BinaryConditionalOperator>(E))
    TrueExpr = BCO->getCommon();

  bool Suspicious = false;
  CheckConditionalOperand(S, TrueExpr, T, CC, Suspicious);
  CheckConditionalOperand(S, E->getFalseExpr(), T, CC, Suspicious);

  if (T->isBooleanType())
    DiagnoseIntInBoolContext(S, E);

  // If -Wconversion would have warned about either of the candidates
  // for a signedness conversion to the context type...
  if (!Suspicious) return;

  // ...but it's currently ignored...
  if (!S.Diags.isIgnored(diag::warn_impcast_integer_sign_conditional, CC))
    return;

  // ...then check whether it would have warned about either of the
  // candidates for a signedness conversion to the condition type.
  if (E->getType() == T) return;

  Suspicious = false;
  S.CheckImplicitConversion(TrueExpr->IgnoreParenImpCasts(), E->getType(), CC,
                            &Suspicious);
  if (!Suspicious)
    S.CheckImplicitConversion(E->getFalseExpr()->IgnoreParenImpCasts(),
                              E->getType(), CC, &Suspicious);
}

/// Check conversion of given expression to boolean.
/// Input argument E is a logical expression.
static void CheckBoolLikeConversion(Sema &S, Expr *E, SourceLocation CC) {
  // Run the bool-like conversion checks only for C since there bools are
  // still not used as the return type from "boolean" operators or as the input
  // type for conditional operators.
  if (S.getLangOpts().CPlusPlus)
    return;
  if (E->IgnoreParenImpCasts()->getType()->isAtomicType())
    return;
  S.CheckImplicitConversion(E->IgnoreParenImpCasts(), S.Context.BoolTy, CC);
}

namespace {
struct AnalyzeImplicitConversionsWorkItem {
  Expr *E;
  SourceLocation CC;
  bool IsListInit;
};
}

/// Data recursive variant of AnalyzeImplicitConversions. Subexpressions
/// that should be visited are added to WorkList.
static void AnalyzeImplicitConversions(
    Sema &S, AnalyzeImplicitConversionsWorkItem Item,
    llvm::SmallVectorImpl<AnalyzeImplicitConversionsWorkItem> &WorkList) {
  Expr *OrigE = Item.E;
  SourceLocation CC = Item.CC;

  QualType T = OrigE->getType();
  Expr *E = OrigE->IgnoreParenImpCasts();

  // Propagate whether we are in a C++ list initialization expression.
  // If so, we do not issue warnings for implicit int-float conversion
  // precision loss, because C++11 narrowing already handles it.
  bool IsListInit = Item.IsListInit ||
                    (isa<InitListExpr>(OrigE) && S.getLangOpts().CPlusPlus);

  if (E->isTypeDependent() || E->isValueDependent())
    return;

  Expr *SourceExpr = E;
  // Examine, but don't traverse into the source expression of an
  // OpaqueValueExpr, since it may have multiple parents and we don't want to
  // emit duplicate diagnostics. Its fine to examine the form or attempt to
  // evaluate it in the context of checking the specific conversion to T though.
  if (auto *OVE = dyn_cast<OpaqueValueExpr>(E))
    if (auto *Src = OVE->getSourceExpr())
      SourceExpr = Src;

  if (const auto *UO = dyn_cast<UnaryOperator>(SourceExpr))
    if (UO->getOpcode() == UO_Not &&
        UO->getSubExpr()->isKnownToHaveBooleanValue())
      S.Diag(UO->getBeginLoc(), diag::warn_bitwise_negation_bool)
          << OrigE->getSourceRange() << T->isBooleanType()
          << FixItHint::CreateReplacement(UO->getBeginLoc(), "!");

  if (const auto *BO = dyn_cast<BinaryOperator>(SourceExpr))
    if ((BO->getOpcode() == BO_And || BO->getOpcode() == BO_Or) &&
        BO->getLHS()->isKnownToHaveBooleanValue() &&
        BO->getRHS()->isKnownToHaveBooleanValue() &&
        BO->getLHS()->HasSideEffects(S.Context) &&
        BO->getRHS()->HasSideEffects(S.Context)) {
      SourceManager &SM = S.getSourceManager();
      const LangOptions &LO = S.getLangOpts();
      SourceLocation BLoc = BO->getOperatorLoc();
      SourceLocation ELoc = Lexer::getLocForEndOfToken(BLoc, 0, SM, LO);
      StringRef SR = clang::Lexer::getSourceText(
          clang::CharSourceRange::getTokenRange(BLoc, ELoc), SM, LO);
      // To reduce false positives, only issue the diagnostic if the operator
      // is explicitly spelled as a punctuator. This suppresses the diagnostic
      // when using 'bitand' or 'bitor' either as keywords in C++ or as macros
      // in C, along with other macro spellings the user might invent.
      if (SR.str() == "&" || SR.str() == "|") {

        S.Diag(BO->getBeginLoc(), diag::warn_bitwise_instead_of_logical)
            << (BO->getOpcode() == BO_And ? "&" : "|")
            << OrigE->getSourceRange()
            << FixItHint::CreateReplacement(
                   BO->getOperatorLoc(),
                   (BO->getOpcode() == BO_And ? "&&" : "||"));
        S.Diag(BO->getBeginLoc(), diag::note_cast_operand_to_int);
      }
    }

  // For conditional operators, we analyze the arguments as if they
  // were being fed directly into the output.
  if (auto *CO = dyn_cast<AbstractConditionalOperator>(SourceExpr)) {
    CheckConditionalOperator(S, CO, CC, T);
    return;
  }

  // Check implicit argument conversions for function calls.
  if (CallExpr *Call = dyn_cast<CallExpr>(SourceExpr))
    CheckImplicitArgumentConversions(S, Call, CC);

  // Go ahead and check any implicit conversions we might have skipped.
  // The non-canonical typecheck is just an optimization;
  // CheckImplicitConversion will filter out dead implicit conversions.
  if (SourceExpr->getType() != T)
    S.CheckImplicitConversion(SourceExpr, T, CC, nullptr, IsListInit);

  // Now continue drilling into this expression.

  if (PseudoObjectExpr *POE = dyn_cast<PseudoObjectExpr>(E)) {
    // The bound subexpressions in a PseudoObjectExpr are not reachable
    // as transitive children.
    // FIXME: Use a more uniform representation for this.
    for (auto *SE : POE->semantics())
      if (auto *OVE = dyn_cast<OpaqueValueExpr>(SE))
        WorkList.push_back({OVE->getSourceExpr(), CC, IsListInit});
  }

  // Skip past explicit casts.
  if (auto *CE = dyn_cast<ExplicitCastExpr>(E)) {
    E = CE->getSubExpr()->IgnoreParenImpCasts();
    if (!CE->getType()->isVoidType() && E->getType()->isAtomicType())
      S.Diag(E->getBeginLoc(), diag::warn_atomic_implicit_seq_cst);
    WorkList.push_back({E, CC, IsListInit});
    return;
  }

  if (BinaryOperator *BO = dyn_cast<BinaryOperator>(E)) {
    // Do a somewhat different check with comparison operators.
    if (BO->isComparisonOp())
      return AnalyzeComparison(S, BO);

    // And with simple assignments.
    if (BO->getOpcode() == BO_Assign)
      return AnalyzeAssignment(S, BO);
    // And with compound assignments.
    if (BO->isAssignmentOp())
      return AnalyzeCompoundAssignment(S, BO);
  }

  // These break the otherwise-useful invariant below.  Fortunately,
  // we don't really need to recurse into them, because any internal
  // expressions should have been analyzed already when they were
  // built into statements.
  if (isa<StmtExpr>(E)) return;

  // Don't descend into unevaluated contexts.
  if (isa<UnaryExprOrTypeTraitExpr>(E)) return;

  // Now just recurse over the expression's children.
  CC = E->getExprLoc();
  BinaryOperator *BO = dyn_cast<BinaryOperator>(E);
  bool IsLogicalAndOperator = BO && BO->getOpcode() == BO_LAnd;
  for (Stmt *SubStmt : E->children()) {
    Expr *ChildExpr = dyn_cast_or_null<Expr>(SubStmt);
    if (!ChildExpr)
      continue;

    if (auto *CSE = dyn_cast<CoroutineSuspendExpr>(E))
      if (ChildExpr == CSE->getOperand())
        // Do not recurse over a CoroutineSuspendExpr's operand.
        // The operand is also a subexpression of getCommonExpr(), and
        // recursing into it directly would produce duplicate diagnostics.
        continue;

    if (IsLogicalAndOperator &&
        isa<StringLiteral>(ChildExpr->IgnoreParenImpCasts()))
      // Ignore checking string literals that are in logical and operators.
      // This is a common pattern for asserts.
      continue;
    WorkList.push_back({ChildExpr, CC, IsListInit});
  }

  if (BO && BO->isLogicalOp()) {
    Expr *SubExpr = BO->getLHS()->IgnoreParenImpCasts();
    if (!IsLogicalAndOperator || !isa<StringLiteral>(SubExpr))
      ::CheckBoolLikeConversion(S, SubExpr, BO->getExprLoc());

    SubExpr = BO->getRHS()->IgnoreParenImpCasts();
    if (!IsLogicalAndOperator || !isa<StringLiteral>(SubExpr))
      ::CheckBoolLikeConversion(S, SubExpr, BO->getExprLoc());
  }

  if (const UnaryOperator *U = dyn_cast<UnaryOperator>(E)) {
    if (U->getOpcode() == UO_LNot) {
      ::CheckBoolLikeConversion(S, U->getSubExpr(), CC);
    } else if (U->getOpcode() != UO_AddrOf) {
      if (U->getSubExpr()->getType()->isAtomicType())
        S.Diag(U->getSubExpr()->getBeginLoc(),
               diag::warn_atomic_implicit_seq_cst);
    }
  }
}

/// AnalyzeImplicitConversions - Find and report any interesting
/// implicit conversions in the given expression.  There are a couple
/// of competing diagnostics here, -Wconversion and -Wsign-compare.
static void AnalyzeImplicitConversions(Sema &S, Expr *OrigE, SourceLocation CC,
                                       bool IsListInit/*= false*/) {
  llvm::SmallVector<AnalyzeImplicitConversionsWorkItem, 16> WorkList;
  WorkList.push_back({OrigE, CC, IsListInit});
  while (!WorkList.empty())
    AnalyzeImplicitConversions(S, WorkList.pop_back_val(), WorkList);
}

// Helper function for Sema::DiagnoseAlwaysNonNullPointer.
// Returns true when emitting a warning about taking the address of a reference.
static bool CheckForReference(Sema &SemaRef, const Expr *E,
                              const PartialDiagnostic &PD) {
  E = E->IgnoreParenImpCasts();

  const FunctionDecl *FD = nullptr;

  if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E)) {
    if (!DRE->getDecl()->getType()->isReferenceType())
      return false;
  } else if (const MemberExpr *M = dyn_cast<MemberExpr>(E)) {
    if (!M->getMemberDecl()->getType()->isReferenceType())
      return false;
  } else if (const CallExpr *Call = dyn_cast<CallExpr>(E)) {
    if (!Call->getCallReturnType(SemaRef.Context)->isReferenceType())
      return false;
    FD = Call->getDirectCallee();
  } else {
    return false;
  }

  SemaRef.Diag(E->getExprLoc(), PD);

  // If possible, point to location of function.
  if (FD) {
    SemaRef.Diag(FD->getLocation(), diag::note_reference_is_return_value) << FD;
  }

  return true;
}

// Returns true if the SourceLocation is expanded from any macro body.
// Returns false if the SourceLocation is invalid, is from not in a macro
// expansion, or is from expanded from a top-level macro argument.
static bool IsInAnyMacroBody(const SourceManager &SM, SourceLocation Loc) {
  if (Loc.isInvalid())
    return false;

  while (Loc.isMacroID()) {
    if (SM.isMacroBodyExpansion(Loc))
      return true;
    Loc = SM.getImmediateMacroCallerLoc(Loc);
  }

  return false;
}

void Sema::DiagnoseAlwaysNonNullPointer(Expr *E,
                                        Expr::NullPointerConstantKind NullKind,
                                        bool IsEqual, SourceRange Range) {
  if (!E)
    return;

  // Don't warn inside macros.
  if (E->getExprLoc().isMacroID()) {
    const SourceManager &SM = getSourceManager();
    if (IsInAnyMacroBody(SM, E->getExprLoc()) ||
        IsInAnyMacroBody(SM, Range.getBegin()))
      return;
  }
  E = E->IgnoreImpCasts();

  const bool IsCompare = NullKind != Expr::NPCK_NotNull;

  if (isa<CXXThisExpr>(E)) {
    unsigned DiagID = IsCompare ? diag::warn_this_null_compare
                                : diag::warn_this_bool_conversion;
    Diag(E->getExprLoc(), DiagID) << E->getSourceRange() << Range << IsEqual;
    return;
  }

  bool IsAddressOf = false;

  if (auto *UO = dyn_cast<UnaryOperator>(E->IgnoreParens())) {
    if (UO->getOpcode() != UO_AddrOf)
      return;
    IsAddressOf = true;
    E = UO->getSubExpr();
  }

  if (IsAddressOf) {
    unsigned DiagID = IsCompare
                          ? diag::warn_address_of_reference_null_compare
                          : diag::warn_address_of_reference_bool_conversion;
    PartialDiagnostic PD = PDiag(DiagID) << E->getSourceRange() << Range
                                         << IsEqual;
    if (CheckForReference(*this, E, PD)) {
      return;
    }
  }

  auto ComplainAboutNonnullParamOrCall = [&](const Attr *NonnullAttr) {
    bool IsParam = isa<NonNullAttr>(NonnullAttr);
    std::string Str;
    llvm::raw_string_ostream S(Str);
    E->printPretty(S, nullptr, getPrintingPolicy());
    unsigned DiagID = IsCompare ? diag::warn_nonnull_expr_compare
                                : diag::warn_cast_nonnull_to_bool;
    Diag(E->getExprLoc(), DiagID) << IsParam << S.str()
      << E->getSourceRange() << Range << IsEqual;
    Diag(NonnullAttr->getLocation(), diag::note_declared_nonnull) << IsParam;
  };

  // If we have a CallExpr that is tagged with returns_nonnull, we can complain.
  if (auto *Call = dyn_cast<CallExpr>(E->IgnoreParenImpCasts())) {
    if (auto *Callee = Call->getDirectCallee()) {
      if (const Attr *A = Callee->getAttr<ReturnsNonNullAttr>()) {
        ComplainAboutNonnullParamOrCall(A);
        return;
      }
    }
  }

  // Complain if we are converting a lambda expression to a boolean value
  // outside of instantiation.
  if (!inTemplateInstantiation()) {
    if (const auto *MCallExpr = dyn_cast<CXXMemberCallExpr>(E)) {
      if (const auto *MRecordDecl = MCallExpr->getRecordDecl();
          MRecordDecl && MRecordDecl->isLambda()) {
        Diag(E->getExprLoc(), diag::warn_impcast_pointer_to_bool)
            << /*LambdaPointerConversionOperatorType=*/3
            << MRecordDecl->getSourceRange() << Range << IsEqual;
        return;
      }
    }
  }

  // Expect to find a single Decl.  Skip anything more complicated.
  ValueDecl *D = nullptr;
  if (DeclRefExpr *R = dyn_cast<DeclRefExpr>(E)) {
    D = R->getDecl();
  } else if (MemberExpr *M = dyn_cast<MemberExpr>(E)) {
    D = M->getMemberDecl();
  }

  // Weak Decls can be null.
  if (!D || D->isWeak())
    return;

  // Check for parameter decl with nonnull attribute
  if (const auto* PV = dyn_cast<ParmVarDecl>(D)) {
    if (getCurFunction() &&
        !getCurFunction()->ModifiedNonNullParams.count(PV)) {
      if (const Attr *A = PV->getAttr<NonNullAttr>()) {
        ComplainAboutNonnullParamOrCall(A);
        return;
      }

      if (const auto *FD = dyn_cast<FunctionDecl>(PV->getDeclContext())) {
        // Skip function template not specialized yet.
        if (FD->getTemplatedKind() == FunctionDecl::TK_FunctionTemplate)
          return;
        auto ParamIter = llvm::find(FD->parameters(), PV);
        assert(ParamIter != FD->param_end());
        unsigned ParamNo = std::distance(FD->param_begin(), ParamIter);

        for (const auto *NonNull : FD->specific_attrs<NonNullAttr>()) {
          if (!NonNull->args_size()) {
              ComplainAboutNonnullParamOrCall(NonNull);
              return;
          }

          for (const ParamIdx &ArgNo : NonNull->args()) {
            if (ArgNo.getASTIndex() == ParamNo) {
              ComplainAboutNonnullParamOrCall(NonNull);
              return;
            }
          }
        }
      }
    }
  }

  QualType T = D->getType();
  const bool IsArray = T->isArrayType();
  const bool IsFunction = T->isFunctionType();

  // Address of function is used to silence the function warning.
  if (IsAddressOf && IsFunction) {
    return;
  }

  // Found nothing.
  if (!IsAddressOf && !IsFunction && !IsArray)
    return;

  // Pretty print the expression for the diagnostic.
  std::string Str;
  llvm::raw_string_ostream S(Str);
  E->printPretty(S, nullptr, getPrintingPolicy());

  unsigned DiagID = IsCompare ? diag::warn_null_pointer_compare
                              : diag::warn_impcast_pointer_to_bool;
  enum {
    AddressOf,
    FunctionPointer,
    ArrayPointer
  } DiagType;
  if (IsAddressOf)
    DiagType = AddressOf;
  else if (IsFunction)
    DiagType = FunctionPointer;
  else if (IsArray)
    DiagType = ArrayPointer;
  else
    llvm_unreachable("Could not determine diagnostic.");
  Diag(E->getExprLoc(), DiagID) << DiagType << S.str() << E->getSourceRange()
                                << Range << IsEqual;

  if (!IsFunction)
    return;

  // Suggest '&' to silence the function warning.
  Diag(E->getExprLoc(), diag::note_function_warning_silence)
      << FixItHint::CreateInsertion(E->getBeginLoc(), "&");

  // Check to see if '()' fixit should be emitted.
  QualType ReturnType;
  UnresolvedSet<4> NonTemplateOverloads;
  tryExprAsCall(*E, ReturnType, NonTemplateOverloads);
  if (ReturnType.isNull())
    return;

  if (IsCompare) {
    // There are two cases here.  If there is null constant, the only suggest
    // for a pointer return type.  If the null is 0, then suggest if the return
    // type is a pointer or an integer type.
    if (!ReturnType->isPointerType()) {
      if (NullKind == Expr::NPCK_ZeroExpression ||
          NullKind == Expr::NPCK_ZeroLiteral) {
        if (!ReturnType->isIntegerType())
          return;
      } else {
        return;
      }
    }
  } else { // !IsCompare
    // For function to bool, only suggest if the function pointer has bool
    // return type.
    if (!ReturnType->isSpecificBuiltinType(BuiltinType::Bool))
      return;
  }
  Diag(E->getExprLoc(), diag::note_function_to_function_call)
      << FixItHint::CreateInsertion(getLocForEndOfToken(E->getEndLoc()), "()");
}

void Sema::CheckImplicitConversions(Expr *E, SourceLocation CC) {
  // Don't diagnose in unevaluated contexts.
  if (isUnevaluatedContext())
    return;

  // Don't diagnose for value- or type-dependent expressions.
  if (E->isTypeDependent() || E->isValueDependent())
    return;

  // Check for array bounds violations in cases where the check isn't triggered
  // elsewhere for other Expr types (like BinaryOperators), e.g. when an
  // ArraySubscriptExpr is on the RHS of a variable initialization.
  CheckArrayAccess(E);

  // This is not the right CC for (e.g.) a variable initialization.
  AnalyzeImplicitConversions(*this, E, CC);
}

void Sema::CheckBoolLikeConversion(Expr *E, SourceLocation CC) {
  ::CheckBoolLikeConversion(*this, E, CC);
}

void Sema::CheckForIntOverflow (const Expr *E) {
  // Use a work list to deal with nested struct initializers.
  SmallVector<const Expr *, 2> Exprs(1, E);

  do {
    const Expr *OriginalE = Exprs.pop_back_val();
    const Expr *E = OriginalE->IgnoreParenCasts();

    if (isa<BinaryOperator, UnaryOperator>(E)) {
      E->EvaluateForOverflow(Context);
      continue;
    }

    if (const auto *InitList = dyn_cast<InitListExpr>(OriginalE))
      Exprs.append(InitList->inits().begin(), InitList->inits().end());
    else if (isa<ObjCBoxedExpr>(OriginalE))
      E->EvaluateForOverflow(Context);
    else if (const auto *Call = dyn_cast<CallExpr>(E))
      Exprs.append(Call->arg_begin(), Call->arg_end());
    else if (const auto *Message = dyn_cast<ObjCMessageExpr>(E))
      Exprs.append(Message->arg_begin(), Message->arg_end());
    else if (const auto *Construct = dyn_cast<CXXConstructExpr>(E))
      Exprs.append(Construct->arg_begin(), Construct->arg_end());
    else if (const auto *Temporary = dyn_cast<CXXBindTemporaryExpr>(E))
      Exprs.push_back(Temporary->getSubExpr());
    else if (const auto *Array = dyn_cast<ArraySubscriptExpr>(E))
      Exprs.push_back(Array->getIdx());
    else if (const auto *Compound = dyn_cast<CompoundLiteralExpr>(E))
      Exprs.push_back(Compound->getInitializer());
    else if (const auto *New = dyn_cast<CXXNewExpr>(E);
             New && New->isArray()) {
      if (auto ArraySize = New->getArraySize())
        Exprs.push_back(*ArraySize);
    }
  } while (!Exprs.empty());
}

namespace {

/// Visitor for expressions which looks for unsequenced operations on the
/// same object.
class SequenceChecker : public ConstEvaluatedExprVisitor<SequenceChecker> {
  using Base = ConstEvaluatedExprVisitor<SequenceChecker>;

  /// A tree of sequenced regions within an expression. Two regions are
  /// unsequenced if one is an ancestor or a descendent of the other. When we
  /// finish processing an expression with sequencing, such as a comma
  /// expression, we fold its tree nodes into its parent, since they are
  /// unsequenced with respect to nodes we will visit later.
  class SequenceTree {
    struct Value {
      explicit Value(unsigned Parent) : Parent(Parent), Merged(false) {}
      unsigned Parent : 31;
      LLVM_PREFERRED_TYPE(bool)
      unsigned Merged : 1;
    };
    SmallVector<Value, 8> Values;

  public:
    /// A region within an expression which may be sequenced with respect
    /// to some other region.
    class Seq {
      friend class SequenceTree;

      unsigned Index;

      explicit Seq(unsigned N) : Index(N) {}

    public:
      Seq() : Index(0) {}
    };

    SequenceTree() { Values.push_back(Value(0)); }
    Seq root() const { return Seq(0); }

    /// Create a new sequence of operations, which is an unsequenced
    /// subset of \p Parent. This sequence of operations is sequenced with
    /// respect to other children of \p Parent.
    Seq allocate(Seq Parent) {
      Values.push_back(Value(Parent.Index));
      return Seq(Values.size() - 1);
    }

    /// Merge a sequence of operations into its parent.
    void merge(Seq S) {
      Values[S.Index].Merged = true;
    }

    /// Determine whether two operations are unsequenced. This operation
    /// is asymmetric: \p Cur should be the more recent sequence, and \p Old
    /// should have been merged into its parent as appropriate.
    bool isUnsequenced(Seq Cur, Seq Old) {
      unsigned C = representative(Cur.Index);
      unsigned Target = representative(Old.Index);
      while (C >= Target) {
        if (C == Target)
          return true;
        C = Values[C].Parent;
      }
      return false;
    }

  private:
    /// Pick a representative for a sequence.
    unsigned representative(unsigned K) {
      if (Values[K].Merged)
        // Perform path compression as we go.
        return Values[K].Parent = representative(Values[K].Parent);
      return K;
    }
  };

  /// An object for which we can track unsequenced uses.
  using Object = const NamedDecl *;

  /// Different flavors of object usage which we track. We only track the
  /// least-sequenced usage of each kind.
  enum UsageKind {
    /// A read of an object. Multiple unsequenced reads are OK.
    UK_Use,

    /// A modification of an object which is sequenced before the value
    /// computation of the expression, such as ++n in C++.
    UK_ModAsValue,

    /// A modification of an object which is not sequenced before the value
    /// computation of the expression, such as n++.
    UK_ModAsSideEffect,

    UK_Count = UK_ModAsSideEffect + 1
  };

  /// Bundle together a sequencing region and the expression corresponding
  /// to a specific usage. One Usage is stored for each usage kind in UsageInfo.
  struct Usage {
    const Expr *UsageExpr = nullptr;
    SequenceTree::Seq Seq;

    Usage() = default;
  };

  struct UsageInfo {
    Usage Uses[UK_Count];

    /// Have we issued a diagnostic for this object already?
    bool Diagnosed = false;

    UsageInfo();
  };
  using UsageInfoMap = llvm::SmallDenseMap<Object, UsageInfo, 16>;

  Sema &SemaRef;

  /// Sequenced regions within the expression.
  SequenceTree Tree;

  /// Declaration modifications and references which we have seen.
  UsageInfoMap UsageMap;

  /// The region we are currently within.
  SequenceTree::Seq Region;

  /// Filled in with declarations which were modified as a side-effect
  /// (that is, post-increment operations).
  SmallVectorImpl<std::pair<Object, Usage>> *ModAsSideEffect = nullptr;

  /// Expressions to check later. We defer checking these to reduce
  /// stack usage.
  SmallVectorImpl<const Expr *> &WorkList;

  /// RAII object wrapping the visitation of a sequenced subexpression of an
  /// expression. At the end of this process, the side-effects of the evaluation
  /// become sequenced with respect to the value computation of the result, so
  /// we downgrade any UK_ModAsSideEffect within the evaluation to
  /// UK_ModAsValue.
  struct SequencedSubexpression {
    SequencedSubexpression(SequenceChecker &Self)
      : Self(Self), OldModAsSideEffect(Self.ModAsSideEffect) {
      Self.ModAsSideEffect = &ModAsSideEffect;
    }

    ~SequencedSubexpression() {
      for (const std::pair<Object, Usage> &M : llvm::reverse(ModAsSideEffect)) {
        // Add a new usage with usage kind UK_ModAsValue, and then restore
        // the previous usage with UK_ModAsSideEffect (thus clearing it if
        // the previous one was empty).
        UsageInfo &UI = Self.UsageMap[M.first];
        auto &SideEffectUsage = UI.Uses[UK_ModAsSideEffect];
        Self.addUsage(M.first, UI, SideEffectUsage.UsageExpr, UK_ModAsValue);
        SideEffectUsage = M.second;
      }
      Self.ModAsSideEffect = OldModAsSideEffect;
    }

    SequenceChecker &Self;
    SmallVector<std::pair<Object, Usage>, 4> ModAsSideEffect;
    SmallVectorImpl<std::pair<Object, Usage>> *OldModAsSideEffect;
  };

  /// RAII object wrapping the visitation of a subexpression which we might
  /// choose to evaluate as a constant. If any subexpression is evaluated and
  /// found to be non-constant, this allows us to suppress the evaluation of
  /// the outer expression.
  class EvaluationTracker {
  public:
    EvaluationTracker(SequenceChecker &Self)
        : Self(Self), Prev(Self.EvalTracker) {
      Self.EvalTracker = this;
    }

    ~EvaluationTracker() {
      Self.EvalTracker = Prev;
      if (Prev)
        Prev->EvalOK &= EvalOK;
    }

    bool evaluate(const Expr *E, bool &Result) {
      if (!EvalOK || E->isValueDependent())
        return false;
      EvalOK = E->EvaluateAsBooleanCondition(
          Result, Self.SemaRef.Context,
          Self.SemaRef.isConstantEvaluatedContext());
      return EvalOK;
    }

  private:
    SequenceChecker &Self;
    EvaluationTracker *Prev;
    bool EvalOK = true;
  } *EvalTracker = nullptr;

  /// Find the object which is produced by the specified expression,
  /// if any.
  Object getObject(const Expr *E, bool Mod) const {
    E = E->IgnoreParenCasts();
    if (const UnaryOperator *UO = dyn_cast<UnaryOperator>(E)) {
      if (Mod && (UO->getOpcode() == UO_PreInc || UO->getOpcode() == UO_PreDec))
        return getObject(UO->getSubExpr(), Mod);
    } else if (const BinaryOperator *BO = dyn_cast<BinaryOperator>(E)) {
      if (BO->getOpcode() == BO_Comma)
        return getObject(BO->getRHS(), Mod);
      if (Mod && BO->isAssignmentOp())
        return getObject(BO->getLHS(), Mod);
    } else if (const MemberExpr *ME = dyn_cast<MemberExpr>(E)) {
      // FIXME: Check for more interesting cases, like "x.n = ++x.n".
      if (isa<CXXThisExpr>(ME->getBase()->IgnoreParenCasts()))
        return ME->getMemberDecl();
    } else if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E))
      // FIXME: If this is a reference, map through to its value.
      return DRE->getDecl();
    return nullptr;
  }

  /// Note that an object \p O was modified or used by an expression
  /// \p UsageExpr with usage kind \p UK. \p UI is the \p UsageInfo for
  /// the object \p O as obtained via the \p UsageMap.
  void addUsage(Object O, UsageInfo &UI, const Expr *UsageExpr, UsageKind UK) {
    // Get the old usage for the given object and usage kind.
    Usage &U = UI.Uses[UK];
    if (!U.UsageExpr || !Tree.isUnsequenced(Region, U.Seq)) {
      // If we have a modification as side effect and are in a sequenced
      // subexpression, save the old Usage so that we can restore it later
      // in SequencedSubexpression::~SequencedSubexpression.
      if (UK == UK_ModAsSideEffect && ModAsSideEffect)
        ModAsSideEffect->push_back(std::make_pair(O, U));
      // Then record the new usage with the current sequencing region.
      U.UsageExpr = UsageExpr;
      U.Seq = Region;
    }
  }

  /// Check whether a modification or use of an object \p O in an expression
  /// \p UsageExpr conflicts with a prior usage of kind \p OtherKind. \p UI is
  /// the \p UsageInfo for the object \p O as obtained via the \p UsageMap.
  /// \p IsModMod is true when we are checking for a mod-mod unsequenced
  /// usage and false we are checking for a mod-use unsequenced usage.
  void checkUsage(Object O, UsageInfo &UI, const Expr *UsageExpr,
                  UsageKind OtherKind, bool IsModMod) {
    if (UI.Diagnosed)
      return;

    const Usage &U = UI.Uses[OtherKind];
    if (!U.UsageExpr || !Tree.isUnsequenced(Region, U.Seq))
      return;

    const Expr *Mod = U.UsageExpr;
    const Expr *ModOrUse = UsageExpr;
    if (OtherKind == UK_Use)
      std::swap(Mod, ModOrUse);

    SemaRef.DiagRuntimeBehavior(
        Mod->getExprLoc(), {Mod, ModOrUse},
        SemaRef.PDiag(IsModMod ? diag::warn_unsequenced_mod_mod
                               : diag::warn_unsequenced_mod_use)
            << O << SourceRange(ModOrUse->getExprLoc()));
    UI.Diagnosed = true;
  }

  // A note on note{Pre, Post}{Use, Mod}:
  //
  // (It helps to follow the algorithm with an expression such as
  //  "((++k)++, k) = k" or "k = (k++, k++)". Both contain unsequenced
  //  operations before C++17 and both are well-defined in C++17).
  //
  // When visiting a node which uses/modify an object we first call notePreUse
  // or notePreMod before visiting its sub-expression(s). At this point the
  // children of the current node have not yet been visited and so the eventual
  // uses/modifications resulting from the children of the current node have not
  // been recorded yet.
  //
  // We then visit the children of the current node. After that notePostUse or
  // notePostMod is called. These will 1) detect an unsequenced modification
  // as side effect (as in "k++ + k") and 2) add a new usage with the
  // appropriate usage kind.
  //
  // We also have to be careful that some operation sequences modification as
  // side effect as well (for example: || or ,). To account for this we wrap
  // the visitation of such a sub-expression (for example: the LHS of || or ,)
  // with SequencedSubexpression. SequencedSubexpression is an RAII object
  // which record usages which are modifications as side effect, and then
  // downgrade them (or more accurately restore the previous usage which was a
  // modification as side effect) when exiting the scope of the sequenced
  // subexpression.

  void notePreUse(Object O, const Expr *UseExpr) {
    UsageInfo &UI = UsageMap[O];
    // Uses conflict with other modifications.
    checkUsage(O, UI, UseExpr, /*OtherKind=*/UK_ModAsValue, /*IsModMod=*/false);
  }

  void notePostUse(Object O, const Expr *UseExpr) {
    UsageInfo &UI = UsageMap[O];
    checkUsage(O, UI, UseExpr, /*OtherKind=*/UK_ModAsSideEffect,
               /*IsModMod=*/false);
    addUsage(O, UI, UseExpr, /*UsageKind=*/UK_Use);
  }

  void notePreMod(Object O, const Expr *ModExpr) {
    UsageInfo &UI = UsageMap[O];
    // Modifications conflict with other modifications and with uses.
    checkUsage(O, UI, ModExpr, /*OtherKind=*/UK_ModAsValue, /*IsModMod=*/true);
    checkUsage(O, UI, ModExpr, /*OtherKind=*/UK_Use, /*IsModMod=*/false);
  }

  void notePostMod(Object O, const Expr *ModExpr, UsageKind UK) {
    UsageInfo &UI = UsageMap[O];
    checkUsage(O, UI, ModExpr, /*OtherKind=*/UK_ModAsSideEffect,
               /*IsModMod=*/true);
    addUsage(O, UI, ModExpr, /*UsageKind=*/UK);
  }

public:
  SequenceChecker(Sema &S, const Expr *E,
                  SmallVectorImpl<const Expr *> &WorkList)
      : Base(S.Context), SemaRef(S), Region(Tree.root()), WorkList(WorkList) {
    Visit(E);
    // Silence a -Wunused-private-field since WorkList is now unused.
    // TODO: Evaluate if it can be used, and if not remove it.
    (void)this->WorkList;
  }

  void VisitStmt(const Stmt *S) {
    // Skip all statements which aren't expressions for now.
  }

  void VisitExpr(const Expr *E) {
    // By default, just recurse to evaluated subexpressions.
    Base::VisitStmt(E);
  }

  void VisitCoroutineSuspendExpr(const CoroutineSuspendExpr *CSE) {
    for (auto *Sub : CSE->children()) {
      const Expr *ChildExpr = dyn_cast_or_null<Expr>(Sub);
      if (!ChildExpr)
        continue;

      if (ChildExpr == CSE->getOperand())
        // Do not recurse over a CoroutineSuspendExpr's operand.
        // The operand is also a subexpression of getCommonExpr(), and
        // recursing into it directly could confuse object management
        // for the sake of sequence tracking.
        continue;

      Visit(Sub);
    }
  }

  void VisitCastExpr(const CastExpr *E) {
    Object O = Object();
    if (E->getCastKind() == CK_LValueToRValue)
      O = getObject(E->getSubExpr(), false);

    if (O)
      notePreUse(O, E);
    VisitExpr(E);
    if (O)
      notePostUse(O, E);
  }

  void VisitSequencedExpressions(const Expr *SequencedBefore,
                                 const Expr *SequencedAfter) {
    SequenceTree::Seq BeforeRegion = Tree.allocate(Region);
    SequenceTree::Seq AfterRegion = Tree.allocate(Region);
    SequenceTree::Seq OldRegion = Region;

    {
      SequencedSubexpression SeqBefore(*this);
      Region = BeforeRegion;
      Visit(SequencedBefore);
    }

    Region = AfterRegion;
    Visit(SequencedAfter);

    Region = OldRegion;

    Tree.merge(BeforeRegion);
    Tree.merge(AfterRegion);
  }

  void VisitArraySubscriptExpr(const ArraySubscriptExpr *ASE) {
    // C++17 [expr.sub]p1:
    //   The expression E1[E2] is identical (by definition) to *((E1)+(E2)). The
    //   expression E1 is sequenced before the expression E2.
    if (SemaRef.getLangOpts().CPlusPlus17)
      VisitSequencedExpressions(ASE->getLHS(), ASE->getRHS());
    else {
      Visit(ASE->getLHS());
      Visit(ASE->getRHS());
    }
  }

  void VisitBinPtrMemD(const BinaryOperator *BO) { VisitBinPtrMem(BO); }
  void VisitBinPtrMemI(const BinaryOperator *BO) { VisitBinPtrMem(BO); }
  void VisitBinPtrMem(const BinaryOperator *BO) {
    // C++17 [expr.mptr.oper]p4:
    //  Abbreviating pm-expression.*cast-expression as E1.*E2, [...]
    //  the expression E1 is sequenced before the expression E2.
    if (SemaRef.getLangOpts().CPlusPlus17)
      VisitSequencedExpressions(BO->getLHS(), BO->getRHS());
    else {
      Visit(BO->getLHS());
      Visit(BO->getRHS());
    }
  }

  void VisitBinShl(const BinaryOperator *BO) { VisitBinShlShr(BO); }
  void VisitBinShr(const BinaryOperator *BO) { VisitBinShlShr(BO); }
  void VisitBinShlShr(const BinaryOperator *BO) {
    // C++17 [expr.shift]p4:
    //  The expression E1 is sequenced before the expression E2.
    if (SemaRef.getLangOpts().CPlusPlus17)
      VisitSequencedExpressions(BO->getLHS(), BO->getRHS());
    else {
      Visit(BO->getLHS());
      Visit(BO->getRHS());
    }
  }

  void VisitBinComma(const BinaryOperator *BO) {
    // C++11 [expr.comma]p1:
    //   Every value computation and side effect associated with the left
    //   expression is sequenced before every value computation and side
    //   effect associated with the right expression.
    VisitSequencedExpressions(BO->getLHS(), BO->getRHS());
  }

  void VisitBinAssign(const BinaryOperator *BO) {
    SequenceTree::Seq RHSRegion;
    SequenceTree::Seq LHSRegion;
    if (SemaRef.getLangOpts().CPlusPlus17) {
      RHSRegion = Tree.allocate(Region);
      LHSRegion = Tree.allocate(Region);
    } else {
      RHSRegion = Region;
      LHSRegion = Region;
    }
    SequenceTree::Seq OldRegion = Region;

    // C++11 [expr.ass]p1:
    //  [...] the assignment is sequenced after the value computation
    //  of the right and left operands, [...]
    //
    // so check it before inspecting the operands and update the
    // map afterwards.
    Object O = getObject(BO->getLHS(), /*Mod=*/true);
    if (O)
      notePreMod(O, BO);

    if (SemaRef.getLangOpts().CPlusPlus17) {
      // C++17 [expr.ass]p1:
      //  [...] The right operand is sequenced before the left operand. [...]
      {
        SequencedSubexpression SeqBefore(*this);
        Region = RHSRegion;
        Visit(BO->getRHS());
      }

      Region = LHSRegion;
      Visit(BO->getLHS());

      if (O && isa<CompoundAssignOperator>(BO))
        notePostUse(O, BO);

    } else {
      // C++11 does not specify any sequencing between the LHS and RHS.
      Region = LHSRegion;
      Visit(BO->getLHS());

      if (O && isa<CompoundAssignOperator>(BO))
        notePostUse(O, BO);

      Region = RHSRegion;
      Visit(BO->getRHS());
    }

    // C++11 [expr.ass]p1:
    //  the assignment is sequenced [...] before the value computation of the
    //  assignment expression.
    // C11 6.5.16/3 has no such rule.
    Region = OldRegion;
    if (O)
      notePostMod(O, BO,
                  SemaRef.getLangOpts().CPlusPlus ? UK_ModAsValue
                                                  : UK_ModAsSideEffect);
    if (SemaRef.getLangOpts().CPlusPlus17) {
      Tree.merge(RHSRegion);
      Tree.merge(LHSRegion);
    }
  }

  void VisitCompoundAssignOperator(const CompoundAssignOperator *CAO) {
    VisitBinAssign(CAO);
  }

  void VisitUnaryPreInc(const UnaryOperator *UO) { VisitUnaryPreIncDec(UO); }
  void VisitUnaryPreDec(const UnaryOperator *UO) { VisitUnaryPreIncDec(UO); }
  void VisitUnaryPreIncDec(const UnaryOperator *UO) {
    Object O = getObject(UO->getSubExpr(), true);
    if (!O)
      return VisitExpr(UO);

    notePreMod(O, UO);
    Visit(UO->getSubExpr());
    // C++11 [expr.pre.incr]p1:
    //   the expression ++x is equivalent to x+=1
    notePostMod(O, UO,
                SemaRef.getLangOpts().CPlusPlus ? UK_ModAsValue
                                                : UK_ModAsSideEffect);
  }

  void VisitUnaryPostInc(const UnaryOperator *UO) { VisitUnaryPostIncDec(UO); }
  void VisitUnaryPostDec(const UnaryOperator *UO) { VisitUnaryPostIncDec(UO); }
  void VisitUnaryPostIncDec(const UnaryOperator *UO) {
    Object O = getObject(UO->getSubExpr(), true);
    if (!O)
      return VisitExpr(UO);

    notePreMod(O, UO);
    Visit(UO->getSubExpr());
    notePostMod(O, UO, UK_ModAsSideEffect);
  }

  void VisitBinLOr(const BinaryOperator *BO) {
    // C++11 [expr.log.or]p2:
    //  If the second expression is evaluated, every value computation and
    //  side effect associated with the first expression is sequenced before
    //  every value computation and side effect associated with the
    //  second expression.
    SequenceTree::Seq LHSRegion = Tree.allocate(Region);
    SequenceTree::Seq RHSRegion = Tree.allocate(Region);
    SequenceTree::Seq OldRegion = Region;

    EvaluationTracker Eval(*this);
    {
      SequencedSubexpression Sequenced(*this);
      Region = LHSRegion;
      Visit(BO->getLHS());
    }

    // C++11 [expr.log.or]p1:
    //  [...] the second operand is not evaluated if the first operand
    //  evaluates to true.
    bool EvalResult = false;
    bool EvalOK = Eval.evaluate(BO->getLHS(), EvalResult);
    bool ShouldVisitRHS = !EvalOK || !EvalResult;
    if (ShouldVisitRHS) {
      Region = RHSRegion;
      Visit(BO->getRHS());
    }

    Region = OldRegion;
    Tree.merge(LHSRegion);
    Tree.merge(RHSRegion);
  }

  void VisitBinLAnd(const BinaryOperator *BO) {
    // C++11 [expr.log.and]p2:
    //  If the second expression is evaluated, every value computation and
    //  side effect associated with the first expression is sequenced before
    //  every value computation and side effect associated with the
    //  second expression.
    SequenceTree::Seq LHSRegion = Tree.allocate(Region);
    SequenceTree::Seq RHSRegion = Tree.allocate(Region);
    SequenceTree::Seq OldRegion = Region;

    EvaluationTracker Eval(*this);
    {
      SequencedSubexpression Sequenced(*this);
      Region = LHSRegion;
      Visit(BO->getLHS());
    }

    // C++11 [expr.log.and]p1:
    //  [...] the second operand is not evaluated if the first operand is false.
    bool EvalResult = false;
    bool EvalOK = Eval.evaluate(BO->getLHS(), EvalResult);
    bool ShouldVisitRHS = !EvalOK || EvalResult;
    if (ShouldVisitRHS) {
      Region = RHSRegion;
      Visit(BO->getRHS());
    }

    Region = OldRegion;
    Tree.merge(LHSRegion);
    Tree.merge(RHSRegion);
  }

  void VisitAbstractConditionalOperator(const AbstractConditionalOperator *CO) {
    // C++11 [expr.cond]p1:
    //  [...] Every value computation and side effect associated with the first
    //  expression is sequenced before every value computation and side effect
    //  associated with the second or third expression.
    SequenceTree::Seq ConditionRegion = Tree.allocate(Region);

    // No sequencing is specified between the true and false expression.
    // However since exactly one of both is going to be evaluated we can
    // consider them to be sequenced. This is needed to avoid warning on
    // something like "x ? y+= 1 : y += 2;" in the case where we will visit
    // both the true and false expressions because we can't evaluate x.
    // This will still allow us to detect an expression like (pre C++17)
    // "(x ? y += 1 : y += 2) = y".
    //
    // We don't wrap the visitation of the true and false expression with
    // SequencedSubexpression because we don't want to downgrade modifications
    // as side effect in the true and false expressions after the visition
    // is done. (for example in the expression "(x ? y++ : y++) + y" we should
    // not warn between the two "y++", but we should warn between the "y++"
    // and the "y".
    SequenceTree::Seq TrueRegion = Tree.allocate(Region);
    SequenceTree::Seq FalseRegion = Tree.allocate(Region);
    SequenceTree::Seq OldRegion = Region;

    EvaluationTracker Eval(*this);
    {
      SequencedSubexpression Sequenced(*this);
      Region = ConditionRegion;
      Visit(CO->getCond());
    }

    // C++11 [expr.cond]p1:
    // [...] The first expression is contextually converted to bool (Clause 4).
    // It is evaluated and if it is true, the result of the conditional
    // expression is the value of the second expression, otherwise that of the
    // third expression. Only one of the second and third expressions is
    // evaluated. [...]
    bool EvalResult = false;
    bool EvalOK = Eval.evaluate(CO->getCond(), EvalResult);
    bool ShouldVisitTrueExpr = !EvalOK || EvalResult;
    bool ShouldVisitFalseExpr = !EvalOK || !EvalResult;
    if (ShouldVisitTrueExpr) {
      Region = TrueRegion;
      Visit(CO->getTrueExpr());
    }
    if (ShouldVisitFalseExpr) {
      Region = FalseRegion;
      Visit(CO->getFalseExpr());
    }

    Region = OldRegion;
    Tree.merge(ConditionRegion);
    Tree.merge(TrueRegion);
    Tree.merge(FalseRegion);
  }

  void VisitCallExpr(const CallExpr *CE) {
    // FIXME: CXXNewExpr and CXXDeleteExpr implicitly call functions.

    if (CE->isUnevaluatedBuiltinCall(Context))
      return;

    // C++11 [intro.execution]p15:
    //   When calling a function [...], every value computation and side effect
    //   associated with any argument expression, or with the postfix expression
    //   designating the called function, is sequenced before execution of every
    //   expression or statement in the body of the function [and thus before
    //   the value computation of its result].
    SequencedSubexpression Sequenced(*this);
    SemaRef.runWithSufficientStackSpace(CE->getExprLoc(), [&] {
      // C++17 [expr.call]p5
      //   The postfix-expression is sequenced before each expression in the
      //   expression-list and any default argument. [...]
      SequenceTree::Seq CalleeRegion;
      SequenceTree::Seq OtherRegion;
      if (SemaRef.getLangOpts().CPlusPlus17) {
        CalleeRegion = Tree.allocate(Region);
        OtherRegion = Tree.allocate(Region);
      } else {
        CalleeRegion = Region;
        OtherRegion = Region;
      }
      SequenceTree::Seq OldRegion = Region;

      // Visit the callee expression first.
      Region = CalleeRegion;
      if (SemaRef.getLangOpts().CPlusPlus17) {
        SequencedSubexpression Sequenced(*this);
        Visit(CE->getCallee());
      } else {
        Visit(CE->getCallee());
      }

      // Then visit the argument expressions.
      Region = OtherRegion;
      for (const Expr *Argument : CE->arguments())
        Visit(Argument);

      Region = OldRegion;
      if (SemaRef.getLangOpts().CPlusPlus17) {
        Tree.merge(CalleeRegion);
        Tree.merge(OtherRegion);
      }
    });
  }

  void VisitCXXOperatorCallExpr(const CXXOperatorCallExpr *CXXOCE) {
    // C++17 [over.match.oper]p2:
    //   [...] the operator notation is first transformed to the equivalent
    //   function-call notation as summarized in Table 12 (where @ denotes one
    //   of the operators covered in the specified subclause). However, the
    //   operands are sequenced in the order prescribed for the built-in
    //   operator (Clause 8).
    //
    // From the above only overloaded binary operators and overloaded call
    // operators have sequencing rules in C++17 that we need to handle
    // separately.
    if (!SemaRef.getLangOpts().CPlusPlus17 ||
        (CXXOCE->getNumArgs() != 2 && CXXOCE->getOperator() != OO_Call))
      return VisitCallExpr(CXXOCE);

    enum {
      NoSequencing,
      LHSBeforeRHS,
      RHSBeforeLHS,
      LHSBeforeRest
    } SequencingKind;
    switch (CXXOCE->getOperator()) {
    case OO_Equal:
    case OO_PlusEqual:
    case OO_MinusEqual:
    case OO_StarEqual:
    case OO_SlashEqual:
    case OO_PercentEqual:
    case OO_CaretEqual:
    case OO_AmpEqual:
    case OO_PipeEqual:
    case OO_LessLessEqual:
    case OO_GreaterGreaterEqual:
      SequencingKind = RHSBeforeLHS;
      break;

    case OO_LessLess:
    case OO_GreaterGreater:
    case OO_AmpAmp:
    case OO_PipePipe:
    case OO_Comma:
    case OO_ArrowStar:
    case OO_Subscript:
      SequencingKind = LHSBeforeRHS;
      break;

    case OO_Call:
      SequencingKind = LHSBeforeRest;
      break;

    default:
      SequencingKind = NoSequencing;
      break;
    }

    if (SequencingKind == NoSequencing)
      return VisitCallExpr(CXXOCE);

    // This is a call, so all subexpressions are sequenced before the result.
    SequencedSubexpression Sequenced(*this);

    SemaRef.runWithSufficientStackSpace(CXXOCE->getExprLoc(), [&] {
      assert(SemaRef.getLangOpts().CPlusPlus17 &&
             "Should only get there with C++17 and above!");
      assert((CXXOCE->getNumArgs() == 2 || CXXOCE->getOperator() == OO_Call) &&
             "Should only get there with an overloaded binary operator"
             " or an overloaded call operator!");

      if (SequencingKind == LHSBeforeRest) {
        assert(CXXOCE->getOperator() == OO_Call &&
               "We should only have an overloaded call operator here!");

        // This is very similar to VisitCallExpr, except that we only have the
        // C++17 case. The postfix-expression is the first argument of the
        // CXXOperatorCallExpr. The expressions in the expression-list, if any,
        // are in the following arguments.
        //
        // Note that we intentionally do not visit the callee expression since
        // it is just a decayed reference to a function.
        SequenceTree::Seq PostfixExprRegion = Tree.allocate(Region);
        SequenceTree::Seq ArgsRegion = Tree.allocate(Region);
        SequenceTree::Seq OldRegion = Region;

        assert(CXXOCE->getNumArgs() >= 1 &&
               "An overloaded call operator must have at least one argument"
               " for the postfix-expression!");
        const Expr *PostfixExpr = CXXOCE->getArgs()[0];
        llvm::ArrayRef<const Expr *> Args(CXXOCE->getArgs() + 1,
                                          CXXOCE->getNumArgs() - 1);

        // Visit the postfix-expression first.
        {
          Region = PostfixExprRegion;
          SequencedSubexpression Sequenced(*this);
          Visit(PostfixExpr);
        }

        // Then visit the argument expressions.
        Region = ArgsRegion;
        for (const Expr *Arg : Args)
          Visit(Arg);

        Region = OldRegion;
        Tree.merge(PostfixExprRegion);
        Tree.merge(ArgsRegion);
      } else {
        assert(CXXOCE->getNumArgs() == 2 &&
               "Should only have two arguments here!");
        assert((SequencingKind == LHSBeforeRHS ||
                SequencingKind == RHSBeforeLHS) &&
               "Unexpected sequencing kind!");

        // We do not visit the callee expression since it is just a decayed
        // reference to a function.
        const Expr *E1 = CXXOCE->getArg(0);
        const Expr *E2 = CXXOCE->getArg(1);
        if (SequencingKind == RHSBeforeLHS)
          std::swap(E1, E2);

        return VisitSequencedExpressions(E1, E2);
      }
    });
  }

  void VisitCXXConstructExpr(const CXXConstructExpr *CCE) {
    // This is a call, so all subexpressions are sequenced before the result.
    SequencedSubexpression Sequenced(*this);

    if (!CCE->isListInitialization())
      return VisitExpr(CCE);

    // In C++11, list initializations are sequenced.
    SequenceExpressionsInOrder(
        llvm::ArrayRef(CCE->getArgs(), CCE->getNumArgs()));
  }

  void VisitInitListExpr(const InitListExpr *ILE) {
    if (!SemaRef.getLangOpts().CPlusPlus11)
      return VisitExpr(ILE);

    // In C++11, list initializations are sequenced.
    SequenceExpressionsInOrder(ILE->inits());
  }

  void VisitCXXParenListInitExpr(const CXXParenListInitExpr *PLIE) {
    // C++20 parenthesized list initializations are sequenced. See C++20
    // [decl.init.general]p16.5 and [decl.init.general]p16.6.2.2.
    SequenceExpressionsInOrder(PLIE->getInitExprs());
  }

private:
  void SequenceExpressionsInOrder(ArrayRef<const Expr *> ExpressionList) {
    SmallVector<SequenceTree::Seq, 32> Elts;
    SequenceTree::Seq Parent = Region;
    for (const Expr *E : ExpressionList) {
      if (!E)
        continue;
      Region = Tree.allocate(Parent);
      Elts.push_back(Region);
      Visit(E);
    }

    // Forget that the initializers are sequenced.
    Region = Parent;
    for (unsigned I = 0; I < Elts.size(); ++I)
      Tree.merge(Elts[I]);
  }
};

SequenceChecker::UsageInfo::UsageInfo() = default;

} // namespace

void Sema::CheckUnsequencedOperations(const Expr *E) {
  SmallVector<const Expr *, 8> WorkList;
  WorkList.push_back(E);
  while (!WorkList.empty()) {
    const Expr *Item = WorkList.pop_back_val();
    SequenceChecker(*this, Item, WorkList);
  }
}

void Sema::CheckCompletedExpr(Expr *E, SourceLocation CheckLoc,
                              bool IsConstexpr) {
  llvm::SaveAndRestore ConstantContext(isConstantEvaluatedOverride,
                                       IsConstexpr || isa<ConstantExpr>(E));
  CheckImplicitConversions(E, CheckLoc);
  if (!E->isInstantiationDependent())
    CheckUnsequencedOperations(E);
  if (!IsConstexpr && !E->isValueDependent())
    CheckForIntOverflow(E);
  DiagnoseMisalignedMembers();
}

void Sema::CheckBitFieldInitialization(SourceLocation InitLoc,
                                       FieldDecl *BitField,
                                       Expr *Init) {
  (void) AnalyzeBitFieldAssignment(*this, BitField, Init, InitLoc);
}

static void diagnoseArrayStarInParamType(Sema &S, QualType PType,
                                         SourceLocation Loc) {
  if (!PType->isVariablyModifiedType())
    return;
  if (const auto *PointerTy = dyn_cast<PointerType>(PType)) {
    diagnoseArrayStarInParamType(S, PointerTy->getPointeeType(), Loc);
    return;
  }
  if (const auto *ReferenceTy = dyn_cast<ReferenceType>(PType)) {
    diagnoseArrayStarInParamType(S, ReferenceTy->getPointeeType(), Loc);
    return;
  }
  if (const auto *ParenTy = dyn_cast<ParenType>(PType)) {
    diagnoseArrayStarInParamType(S, ParenTy->getInnerType(), Loc);
    return;
  }

  const ArrayType *AT = S.Context.getAsArrayType(PType);
  if (!AT)
    return;

  if (AT->getSizeModifier() != ArraySizeModifier::Star) {
    diagnoseArrayStarInParamType(S, AT->getElementType(), Loc);
    return;
  }

  S.Diag(Loc, diag::err_array_star_in_function_definition);
}

bool Sema::CheckParmsForFunctionDef(ArrayRef<ParmVarDecl *> Parameters,
                                    bool CheckParameterNames) {
  bool HasInvalidParm = false;
  for (ParmVarDecl *Param : Parameters) {
    assert(Param && "null in a parameter list");
    // C99 6.7.5.3p4: the parameters in a parameter type list in a
    // function declarator that is part of a function definition of
    // that function shall not have incomplete type.
    //
    // C++23 [dcl.fct.def.general]/p2
    // The type of a parameter [...] for a function definition
    // shall not be a (possibly cv-qualified) class type that is incomplete
    // or abstract within the function body unless the function is deleted.
    if (!Param->isInvalidDecl() &&
        (RequireCompleteType(Param->getLocation(), Param->getType(),
                             diag::err_typecheck_decl_incomplete_type) ||
         RequireNonAbstractType(Param->getBeginLoc(), Param->getOriginalType(),
                                diag::err_abstract_type_in_decl,
                                AbstractParamType))) {
      Param->setInvalidDecl();
      HasInvalidParm = true;
    }

    // C99 6.9.1p5: If the declarator includes a parameter type list, the
    // declaration of each parameter shall include an identifier.
    if (CheckParameterNames && Param->getIdentifier() == nullptr &&
        !Param->isImplicit() && !getLangOpts().CPlusPlus) {
      // Diagnose this as an extension in C17 and earlier.
      if (!getLangOpts().C23)
        Diag(Param->getLocation(), diag::ext_parameter_name_omitted_c23);
    }

    // C99 6.7.5.3p12:
    //   If the function declarator is not part of a definition of that
    //   function, parameters may have incomplete type and may use the [*]
    //   notation in their sequences of declarator specifiers to specify
    //   variable length array types.
    QualType PType = Param->getOriginalType();
    // FIXME: This diagnostic should point the '[*]' if source-location
    // information is added for it.
    diagnoseArrayStarInParamType(*this, PType, Param->getLocation());

    // If the parameter is a c++ class type and it has to be destructed in the
    // callee function, declare the destructor so that it can be called by the
    // callee function. Do not perform any direct access check on the dtor here.
    if (!Param->isInvalidDecl()) {
      if (CXXRecordDecl *ClassDecl = Param->getType()->getAsCXXRecordDecl()) {
        if (!ClassDecl->isInvalidDecl() &&
            !ClassDecl->hasIrrelevantDestructor() &&
            !ClassDecl->isDependentContext() &&
            ClassDecl->isParamDestroyedInCallee()) {
          CXXDestructorDecl *Destructor = LookupDestructor(ClassDecl);
          MarkFunctionReferenced(Param->getLocation(), Destructor);
          DiagnoseUseOfDecl(Destructor, Param->getLocation());
        }
      }
    }

    // Parameters with the pass_object_size attribute only need to be marked
    // constant at function definitions. Because we lack information about
    // whether we're on a declaration or definition when we're instantiating the
    // attribute, we need to check for constness here.
    if (const auto *Attr = Param->getAttr<PassObjectSizeAttr>())
      if (!Param->getType().isConstQualified())
        Diag(Param->getLocation(), diag::err_attribute_pointers_only)
            << Attr->getSpelling() << 1;

    // Check for parameter names shadowing fields from the class.
    if (LangOpts.CPlusPlus && !Param->isInvalidDecl()) {
      // The owning context for the parameter should be the function, but we
      // want to see if this function's declaration context is a record.
      DeclContext *DC = Param->getDeclContext();
      if (DC && DC->isFunctionOrMethod()) {
        if (auto *RD = dyn_cast<CXXRecordDecl>(DC->getParent()))
          CheckShadowInheritedFields(Param->getLocation(), Param->getDeclName(),
                                     RD, /*DeclIsField*/ false);
      }
    }

    if (!Param->isInvalidDecl() &&
        Param->getOriginalType()->isWebAssemblyTableType()) {
      Param->setInvalidDecl();
      HasInvalidParm = true;
      Diag(Param->getLocation(), diag::err_wasm_table_as_function_parameter);
    }
  }

  return HasInvalidParm;
}

std::optional<std::pair<
    CharUnits, CharUnits>> static getBaseAlignmentAndOffsetFromPtr(const Expr
                                                                       *E,
                                                                   ASTContext
                                                                       &Ctx);

/// Compute the alignment and offset of the base class object given the
/// derived-to-base cast expression and the alignment and offset of the derived
/// class object.
static std::pair<CharUnits, CharUnits>
getDerivedToBaseAlignmentAndOffset(const CastExpr *CE, QualType DerivedType,
                                   CharUnits BaseAlignment, CharUnits Offset,
                                   ASTContext &Ctx) {
  for (auto PathI = CE->path_begin(), PathE = CE->path_end(); PathI != PathE;
       ++PathI) {
    const CXXBaseSpecifier *Base = *PathI;
    const CXXRecordDecl *BaseDecl = Base->getType()->getAsCXXRecordDecl();
    if (Base->isVirtual()) {
      // The complete object may have a lower alignment than the non-virtual
      // alignment of the base, in which case the base may be misaligned. Choose
      // the smaller of the non-virtual alignment and BaseAlignment, which is a
      // conservative lower bound of the complete object alignment.
      CharUnits NonVirtualAlignment =
          Ctx.getASTRecordLayout(BaseDecl).getNonVirtualAlignment();
      BaseAlignment = std::min(BaseAlignment, NonVirtualAlignment);
      Offset = CharUnits::Zero();
    } else {
      const ASTRecordLayout &RL =
          Ctx.getASTRecordLayout(DerivedType->getAsCXXRecordDecl());
      Offset += RL.getBaseClassOffset(BaseDecl);
    }
    DerivedType = Base->getType();
  }

  return std::make_pair(BaseAlignment, Offset);
}

/// Compute the alignment and offset of a binary additive operator.
static std::optional<std::pair<CharUnits, CharUnits>>
getAlignmentAndOffsetFromBinAddOrSub(const Expr *PtrE, const Expr *IntE,
                                     bool IsSub, ASTContext &Ctx) {
  QualType PointeeType = PtrE->getType()->getPointeeType();

  if (!PointeeType->isConstantSizeType())
    return std::nullopt;

  auto P = getBaseAlignmentAndOffsetFromPtr(PtrE, Ctx);

  if (!P)
    return std::nullopt;

  CharUnits EltSize = Ctx.getTypeSizeInChars(PointeeType);
  if (std::optional<llvm::APSInt> IdxRes = IntE->getIntegerConstantExpr(Ctx)) {
    CharUnits Offset = EltSize * IdxRes->getExtValue();
    if (IsSub)
      Offset = -Offset;
    return std::make_pair(P->first, P->second + Offset);
  }

  // If the integer expression isn't a constant expression, compute the lower
  // bound of the alignment using the alignment and offset of the pointer
  // expression and the element size.
  return std::make_pair(
      P->first.alignmentAtOffset(P->second).alignmentAtOffset(EltSize),
      CharUnits::Zero());
}

/// This helper function takes an lvalue expression and returns the alignment of
/// a VarDecl and a constant offset from the VarDecl.
std::optional<std::pair<
    CharUnits,
    CharUnits>> static getBaseAlignmentAndOffsetFromLValue(const Expr *E,
                                                           ASTContext &Ctx) {
  E = E->IgnoreParens();
  switch (E->getStmtClass()) {
  default:
    break;
  case Stmt::CStyleCastExprClass:
  case Stmt::CXXStaticCastExprClass:
  case Stmt::ImplicitCastExprClass: {
    auto *CE = cast<CastExpr>(E);
    const Expr *From = CE->getSubExpr();
    switch (CE->getCastKind()) {
    default:
      break;
    case CK_NoOp:
      return getBaseAlignmentAndOffsetFromLValue(From, Ctx);
    case CK_UncheckedDerivedToBase:
    case CK_DerivedToBase: {
      auto P = getBaseAlignmentAndOffsetFromLValue(From, Ctx);
      if (!P)
        break;
      return getDerivedToBaseAlignmentAndOffset(CE, From->getType(), P->first,
                                                P->second, Ctx);
    }
    }
    break;
  }
  case Stmt::ArraySubscriptExprClass: {
    auto *ASE = cast<ArraySubscriptExpr>(E);
    return getAlignmentAndOffsetFromBinAddOrSub(ASE->getBase(), ASE->getIdx(),
                                                false, Ctx);
  }
  case Stmt::DeclRefExprClass: {
    if (auto *VD = dyn_cast<VarDecl>(cast<DeclRefExpr>(E)->getDecl())) {
      // FIXME: If VD is captured by copy or is an escaping __block variable,
      // use the alignment of VD's type.
      if (!VD->getType()->isReferenceType()) {
        // Dependent alignment cannot be resolved -> bail out.
        if (VD->hasDependentAlignment())
          break;
        return std::make_pair(Ctx.getDeclAlign(VD), CharUnits::Zero());
      }
      if (VD->hasInit())
        return getBaseAlignmentAndOffsetFromLValue(VD->getInit(), Ctx);
    }
    break;
  }
  case Stmt::MemberExprClass: {
    auto *ME = cast<MemberExpr>(E);
    auto *FD = dyn_cast<FieldDecl>(ME->getMemberDecl());
    if (!FD || FD->getType()->isReferenceType() ||
        FD->getParent()->isInvalidDecl())
      break;
    std::optional<std::pair<CharUnits, CharUnits>> P;
    if (ME->isArrow())
      P = getBaseAlignmentAndOffsetFromPtr(ME->getBase(), Ctx);
    else
      P = getBaseAlignmentAndOffsetFromLValue(ME->getBase(), Ctx);
    if (!P)
      break;
    const ASTRecordLayout &Layout = Ctx.getASTRecordLayout(FD->getParent());
    uint64_t Offset = Layout.getFieldOffset(FD->getFieldIndex());
    return std::make_pair(P->first,
                          P->second + CharUnits::fromQuantity(Offset));
  }
  case Stmt::UnaryOperatorClass: {
    auto *UO = cast<UnaryOperator>(E);
    switch (UO->getOpcode()) {
    default:
      break;
    case UO_Deref:
      return getBaseAlignmentAndOffsetFromPtr(UO->getSubExpr(), Ctx);
    }
    break;
  }
  case Stmt::BinaryOperatorClass: {
    auto *BO = cast<BinaryOperator>(E);
    auto Opcode = BO->getOpcode();
    switch (Opcode) {
    default:
      break;
    case BO_Comma:
      return getBaseAlignmentAndOffsetFromLValue(BO->getRHS(), Ctx);
    }
    break;
  }
  }
  return std::nullopt;
}

/// This helper function takes a pointer expression and returns the alignment of
/// a VarDecl and a constant offset from the VarDecl.
std::optional<std::pair<
    CharUnits, CharUnits>> static getBaseAlignmentAndOffsetFromPtr(const Expr
                                                                       *E,
                                                                   ASTContext
                                                                       &Ctx) {
  E = E->IgnoreParens();
  switch (E->getStmtClass()) {
  default:
    break;
  case Stmt::CStyleCastExprClass:
  case Stmt::CXXStaticCastExprClass:
  case Stmt::ImplicitCastExprClass: {
    auto *CE = cast<CastExpr>(E);
    const Expr *From = CE->getSubExpr();
    switch (CE->getCastKind()) {
    default:
      break;
    case CK_NoOp:
      return getBaseAlignmentAndOffsetFromPtr(From, Ctx);
    case CK_ArrayToPointerDecay:
      return getBaseAlignmentAndOffsetFromLValue(From, Ctx);
    case CK_UncheckedDerivedToBase:
    case CK_DerivedToBase: {
      auto P = getBaseAlignmentAndOffsetFromPtr(From, Ctx);
      if (!P)
        break;
      return getDerivedToBaseAlignmentAndOffset(
          CE, From->getType()->getPointeeType(), P->first, P->second, Ctx);
    }
    }
    break;
  }
  case Stmt::CXXThisExprClass: {
    auto *RD = E->getType()->getPointeeType()->getAsCXXRecordDecl();
    CharUnits Alignment = Ctx.getASTRecordLayout(RD).getNonVirtualAlignment();
    return std::make_pair(Alignment, CharUnits::Zero());
  }
  case Stmt::UnaryOperatorClass: {
    auto *UO = cast<UnaryOperator>(E);
    if (UO->getOpcode() == UO_AddrOf)
      return getBaseAlignmentAndOffsetFromLValue(UO->getSubExpr(), Ctx);
    break;
  }
  case Stmt::BinaryOperatorClass: {
    auto *BO = cast<BinaryOperator>(E);
    auto Opcode = BO->getOpcode();
    switch (Opcode) {
    default:
      break;
    case BO_Add:
    case BO_Sub: {
      const Expr *LHS = BO->getLHS(), *RHS = BO->getRHS();
      if (Opcode == BO_Add && !RHS->getType()->isIntegralOrEnumerationType())
        std::swap(LHS, RHS);
      return getAlignmentAndOffsetFromBinAddOrSub(LHS, RHS, Opcode == BO_Sub,
                                                  Ctx);
    }
    case BO_Comma:
      return getBaseAlignmentAndOffsetFromPtr(BO->getRHS(), Ctx);
    }
    break;
  }
  }
  return std::nullopt;
}

static CharUnits getPresumedAlignmentOfPointer(const Expr *E, Sema &S) {
  // See if we can compute the alignment of a VarDecl and an offset from it.
  std::optional<std::pair<CharUnits, CharUnits>> P =
      getBaseAlignmentAndOffsetFromPtr(E, S.Context);

  if (P)
    return P->first.alignmentAtOffset(P->second);

  // If that failed, return the type's alignment.
  return S.Context.getTypeAlignInChars(E->getType()->getPointeeType());
}

void Sema::CheckCastAlign(Expr *Op, QualType T, SourceRange TRange) {
  // This is actually a lot of work to potentially be doing on every
  // cast; don't do it if we're ignoring -Wcast_align (as is the default).
  if (getDiagnostics().isIgnored(diag::warn_cast_align, TRange.getBegin()))
    return;

  // Ignore dependent types.
  if (T->isDependentType() || Op->getType()->isDependentType())
    return;

  // Require that the destination be a pointer type.
  const PointerType *DestPtr = T->getAs<PointerType>();
  if (!DestPtr) return;

  // If the destination has alignment 1, we're done.
  QualType DestPointee = DestPtr->getPointeeType();
  if (DestPointee->isIncompleteType()) return;
  CharUnits DestAlign = Context.getTypeAlignInChars(DestPointee);
  if (DestAlign.isOne()) return;

  // Require that the source be a pointer type.
  const PointerType *SrcPtr = Op->getType()->getAs<PointerType>();
  if (!SrcPtr) return;
  QualType SrcPointee = SrcPtr->getPointeeType();

  // Explicitly allow casts from cv void*.  We already implicitly
  // allowed casts to cv void*, since they have alignment 1.
  // Also allow casts involving incomplete types, which implicitly
  // includes 'void'.
  if (SrcPointee->isIncompleteType()) return;

  CharUnits SrcAlign = getPresumedAlignmentOfPointer(Op, *this);

  if (SrcAlign >= DestAlign) return;

  Diag(TRange.getBegin(), diag::warn_cast_align)
    << Op->getType() << T
    << static_cast<unsigned>(SrcAlign.getQuantity())
    << static_cast<unsigned>(DestAlign.getQuantity())
    << TRange << Op->getSourceRange();
}

void Sema::CheckArrayAccess(const Expr *BaseExpr, const Expr *IndexExpr,
                            const ArraySubscriptExpr *ASE,
                            bool AllowOnePastEnd, bool IndexNegated) {
  // Already diagnosed by the constant evaluator.
  if (isConstantEvaluatedContext())
    return;

  IndexExpr = IndexExpr->IgnoreParenImpCasts();
  if (IndexExpr->isValueDependent())
    return;

  const Type *EffectiveType =
      BaseExpr->getType()->getPointeeOrArrayElementType();
  BaseExpr = BaseExpr->IgnoreParenCasts();
  const ConstantArrayType *ArrayTy =
      Context.getAsConstantArrayType(BaseExpr->getType());

  LangOptions::StrictFlexArraysLevelKind
    StrictFlexArraysLevel = getLangOpts().getStrictFlexArraysLevel();

  const Type *BaseType =
      ArrayTy == nullptr ? nullptr : ArrayTy->getElementType().getTypePtr();
  bool IsUnboundedArray =
      BaseType == nullptr || BaseExpr->isFlexibleArrayMemberLike(
                                 Context, StrictFlexArraysLevel,
                                 /*IgnoreTemplateOrMacroSubstitution=*/true);
  if (EffectiveType->isDependentType() ||
      (!IsUnboundedArray && BaseType->isDependentType()))
    return;

  Expr::EvalResult Result;
  if (!IndexExpr->EvaluateAsInt(Result, Context, Expr::SE_AllowSideEffects))
    return;

  llvm::APSInt index = Result.Val.getInt();
  if (IndexNegated) {
    index.setIsUnsigned(false);
    index = -index;
  }

  if (IsUnboundedArray) {
    if (EffectiveType->isFunctionType())
      return;
    if (index.isUnsigned() || !index.isNegative()) {
      const auto &ASTC = getASTContext();
      unsigned AddrBits = ASTC.getTargetInfo().getPointerWidth(
          EffectiveType->getCanonicalTypeInternal().getAddressSpace());
      if (index.getBitWidth() < AddrBits)
        index = index.zext(AddrBits);
      std::optional<CharUnits> ElemCharUnits =
          ASTC.getTypeSizeInCharsIfKnown(EffectiveType);
      // PR50741 - If EffectiveType has unknown size (e.g., if it's a void
      // pointer) bounds-checking isn't meaningful.
      if (!ElemCharUnits || ElemCharUnits->isZero())
        return;
      llvm::APInt ElemBytes(index.getBitWidth(), ElemCharUnits->getQuantity());
      // If index has more active bits than address space, we already know
      // we have a bounds violation to warn about.  Otherwise, compute
      // address of (index + 1)th element, and warn about bounds violation
      // only if that address exceeds address space.
      if (index.getActiveBits() <= AddrBits) {
        bool Overflow;
        llvm::APInt Product(index);
        Product += 1;
        Product = Product.umul_ov(ElemBytes, Overflow);
        if (!Overflow && Product.getActiveBits() <= AddrBits)
          return;
      }

      // Need to compute max possible elements in address space, since that
      // is included in diag message.
      llvm::APInt MaxElems = llvm::APInt::getMaxValue(AddrBits);
      MaxElems = MaxElems.zext(std::max(AddrBits + 1, ElemBytes.getBitWidth()));
      MaxElems += 1;
      ElemBytes = ElemBytes.zextOrTrunc(MaxElems.getBitWidth());
      MaxElems = MaxElems.udiv(ElemBytes);

      unsigned DiagID =
          ASE ? diag::warn_array_index_exceeds_max_addressable_bounds
              : diag::warn_ptr_arith_exceeds_max_addressable_bounds;

      // Diag message shows element size in bits and in "bytes" (platform-
      // dependent CharUnits)
      DiagRuntimeBehavior(BaseExpr->getBeginLoc(), BaseExpr,
                          PDiag(DiagID)
                              << toString(index, 10, true) << AddrBits
                              << (unsigned)ASTC.toBits(*ElemCharUnits)
                              << toString(ElemBytes, 10, false)
                              << toString(MaxElems, 10, false)
                              << (unsigned)MaxElems.getLimitedValue(~0U)
                              << IndexExpr->getSourceRange());

      const NamedDecl *ND = nullptr;
      // Try harder to find a NamedDecl to point at in the note.
      while (const auto *ASE = dyn_cast<ArraySubscriptExpr>(BaseExpr))
        BaseExpr = ASE->getBase()->IgnoreParenCasts();
      if (const auto *DRE = dyn_cast<DeclRefExpr>(BaseExpr))
        ND = DRE->getDecl();
      if (const auto *ME = dyn_cast<MemberExpr>(BaseExpr))
        ND = ME->getMemberDecl();

      if (ND)
        DiagRuntimeBehavior(ND->getBeginLoc(), BaseExpr,
                            PDiag(diag::note_array_declared_here) << ND);
    }
    return;
  }

  if (index.isUnsigned() || !index.isNegative()) {
    // It is possible that the type of the base expression after
    // IgnoreParenCasts is incomplete, even though the type of the base
    // expression before IgnoreParenCasts is complete (see PR39746 for an
    // example). In this case we have no information about whether the array
    // access exceeds the array bounds. However we can still diagnose an array
    // access which precedes the array bounds.
    if (BaseType->isIncompleteType())
      return;

    llvm::APInt size = ArrayTy->getSize();

    if (BaseType != EffectiveType) {
      // Make sure we're comparing apples to apples when comparing index to
      // size.
      uint64_t ptrarith_typesize = Context.getTypeSize(EffectiveType);
      uint64_t array_typesize = Context.getTypeSize(BaseType);

      // Handle ptrarith_typesize being zero, such as when casting to void*.
      // Use the size in bits (what "getTypeSize()" returns) rather than bytes.
      if (!ptrarith_typesize)
        ptrarith_typesize = Context.getCharWidth();

      if (ptrarith_typesize != array_typesize) {
        // There's a cast to a different size type involved.
        uint64_t ratio = array_typesize / ptrarith_typesize;

        // TODO: Be smarter about handling cases where array_typesize is not a
        // multiple of ptrarith_typesize.
        if (ptrarith_typesize * ratio == array_typesize)
          size *= llvm::APInt(size.getBitWidth(), ratio);
      }
    }

    if (size.getBitWidth() > index.getBitWidth())
      index = index.zext(size.getBitWidth());
    else if (size.getBitWidth() < index.getBitWidth())
      size = size.zext(index.getBitWidth());

    // For array subscripting the index must be less than size, but for pointer
    // arithmetic also allow the index (offset) to be equal to size since
    // computing the next address after the end of the array is legal and
    // commonly done e.g. in C++ iterators and range-based for loops.
    if (AllowOnePastEnd ? index.ule(size) : index.ult(size))
      return;

    // Suppress the warning if the subscript expression (as identified by the
    // ']' location) and the index expression are both from macro expansions
    // within a system header.
    if (ASE) {
      SourceLocation RBracketLoc = SourceMgr.getSpellingLoc(
          ASE->getRBracketLoc());
      if (SourceMgr.isInSystemHeader(RBracketLoc)) {
        SourceLocation IndexLoc =
            SourceMgr.getSpellingLoc(IndexExpr->getBeginLoc());
        if (SourceMgr.isWrittenInSameFile(RBracketLoc, IndexLoc))
          return;
      }
    }

    unsigned DiagID = ASE ? diag::warn_array_index_exceeds_bounds
                          : diag::warn_ptr_arith_exceeds_bounds;
    unsigned CastMsg = (!ASE || BaseType == EffectiveType) ? 0 : 1;
    QualType CastMsgTy = ASE ? ASE->getLHS()->getType() : QualType();

    DiagRuntimeBehavior(
        BaseExpr->getBeginLoc(), BaseExpr,
        PDiag(DiagID) << toString(index, 10, true) << ArrayTy->desugar()
                      << CastMsg << CastMsgTy << IndexExpr->getSourceRange());
  } else {
    unsigned DiagID = diag::warn_array_index_precedes_bounds;
    if (!ASE) {
      DiagID = diag::warn_ptr_arith_precedes_bounds;
      if (index.isNegative()) index = -index;
    }

    DiagRuntimeBehavior(BaseExpr->getBeginLoc(), BaseExpr,
                        PDiag(DiagID) << toString(index, 10, true)
                                      << IndexExpr->getSourceRange());
  }

  const NamedDecl *ND = nullptr;
  // Try harder to find a NamedDecl to point at in the note.
  while (const auto *ASE = dyn_cast<ArraySubscriptExpr>(BaseExpr))
    BaseExpr = ASE->getBase()->IgnoreParenCasts();
  if (const auto *DRE = dyn_cast<DeclRefExpr>(BaseExpr))
    ND = DRE->getDecl();
  if (const auto *ME = dyn_cast<MemberExpr>(BaseExpr))
    ND = ME->getMemberDecl();

  if (ND)
    DiagRuntimeBehavior(ND->getBeginLoc(), BaseExpr,
                        PDiag(diag::note_array_declared_here) << ND);
}

void Sema::CheckArrayAccess(const Expr *expr) {
  int AllowOnePastEnd = 0;
  while (expr) {
    expr = expr->IgnoreParenImpCasts();
    switch (expr->getStmtClass()) {
      case Stmt::ArraySubscriptExprClass: {
        const ArraySubscriptExpr *ASE = cast<ArraySubscriptExpr>(expr);
        CheckArrayAccess(ASE->getBase(), ASE->getIdx(), ASE,
                         AllowOnePastEnd > 0);
        expr = ASE->getBase();
        break;
      }
      case Stmt::MemberExprClass: {
        expr = cast<MemberExpr>(expr)->getBase();
        break;
      }
      case Stmt::ArraySectionExprClass: {
        const ArraySectionExpr *ASE = cast<ArraySectionExpr>(expr);
        // FIXME: We should probably be checking all of the elements to the
        // 'length' here as well.
        if (ASE->getLowerBound())
          CheckArrayAccess(ASE->getBase(), ASE->getLowerBound(),
                           /*ASE=*/nullptr, AllowOnePastEnd > 0);
        return;
      }
      case Stmt::UnaryOperatorClass: {
        // Only unwrap the * and & unary operators
        const UnaryOperator *UO = cast<UnaryOperator>(expr);
        expr = UO->getSubExpr();
        switch (UO->getOpcode()) {
          case UO_AddrOf:
            AllowOnePastEnd++;
            break;
          case UO_Deref:
            AllowOnePastEnd--;
            break;
          default:
            return;
        }
        break;
      }
      case Stmt::ConditionalOperatorClass: {
        const ConditionalOperator *cond = cast<ConditionalOperator>(expr);
        if (const Expr *lhs = cond->getLHS())
          CheckArrayAccess(lhs);
        if (const Expr *rhs = cond->getRHS())
          CheckArrayAccess(rhs);
        return;
      }
      case Stmt::CXXOperatorCallExprClass: {
        const auto *OCE = cast<CXXOperatorCallExpr>(expr);
        for (const auto *Arg : OCE->arguments())
          CheckArrayAccess(Arg);
        return;
      }
      default:
        return;
    }
  }
}

static bool checkUnsafeAssignLiteral(Sema &S, SourceLocation Loc,
                                     Expr *RHS, bool isProperty) {
  // Check if RHS is an Objective-C object literal, which also can get
  // immediately zapped in a weak reference.  Note that we explicitly
  // allow ObjCStringLiterals, since those are designed to never really die.
  RHS = RHS->IgnoreParenImpCasts();

  // This enum needs to match with the 'select' in
  // warn_objc_arc_literal_assign (off-by-1).
  SemaObjC::ObjCLiteralKind Kind = S.ObjC().CheckLiteralKind(RHS);
  if (Kind == SemaObjC::LK_String || Kind == SemaObjC::LK_None)
    return false;

  S.Diag(Loc, diag::warn_arc_literal_assign)
    << (unsigned) Kind
    << (isProperty ? 0 : 1)
    << RHS->getSourceRange();

  return true;
}

static bool checkUnsafeAssignObject(Sema &S, SourceLocation Loc,
                                    Qualifiers::ObjCLifetime LT,
                                    Expr *RHS, bool isProperty) {
  // Strip off any implicit cast added to get to the one ARC-specific.
  while (ImplicitCastExpr *cast = dyn_cast<ImplicitCastExpr>(RHS)) {
    if (cast->getCastKind() == CK_ARCConsumeObject) {
      S.Diag(Loc, diag::warn_arc_retained_assign)
        << (LT == Qualifiers::OCL_ExplicitNone)
        << (isProperty ? 0 : 1)
        << RHS->getSourceRange();
      return true;
    }
    RHS = cast->getSubExpr();
  }

  if (LT == Qualifiers::OCL_Weak &&
      checkUnsafeAssignLiteral(S, Loc, RHS, isProperty))
    return true;

  return false;
}

bool Sema::checkUnsafeAssigns(SourceLocation Loc,
                              QualType LHS, Expr *RHS) {
  Qualifiers::ObjCLifetime LT = LHS.getObjCLifetime();

  if (LT != Qualifiers::OCL_Weak && LT != Qualifiers::OCL_ExplicitNone)
    return false;

  if (checkUnsafeAssignObject(*this, Loc, LT, RHS, false))
    return true;

  return false;
}

void Sema::checkUnsafeExprAssigns(SourceLocation Loc,
                              Expr *LHS, Expr *RHS) {
  QualType LHSType;
  // PropertyRef on LHS type need be directly obtained from
  // its declaration as it has a PseudoType.
  ObjCPropertyRefExpr *PRE
    = dyn_cast<ObjCPropertyRefExpr>(LHS->IgnoreParens());
  if (PRE && !PRE->isImplicitProperty()) {
    const ObjCPropertyDecl *PD = PRE->getExplicitProperty();
    if (PD)
      LHSType = PD->getType();
  }

  if (LHSType.isNull())
    LHSType = LHS->getType();

  Qualifiers::ObjCLifetime LT = LHSType.getObjCLifetime();

  if (LT == Qualifiers::OCL_Weak) {
    if (!Diags.isIgnored(diag::warn_arc_repeated_use_of_weak, Loc))
      getCurFunction()->markSafeWeakUse(LHS);
  }

  if (checkUnsafeAssigns(Loc, LHSType, RHS))
    return;

  // FIXME. Check for other life times.
  if (LT != Qualifiers::OCL_None)
    return;

  if (PRE) {
    if (PRE->isImplicitProperty())
      return;
    const ObjCPropertyDecl *PD = PRE->getExplicitProperty();
    if (!PD)
      return;

    unsigned Attributes = PD->getPropertyAttributes();
    if (Attributes & ObjCPropertyAttribute::kind_assign) {
      // when 'assign' attribute was not explicitly specified
      // by user, ignore it and rely on property type itself
      // for lifetime info.
      unsigned AsWrittenAttr = PD->getPropertyAttributesAsWritten();
      if (!(AsWrittenAttr & ObjCPropertyAttribute::kind_assign) &&
          LHSType->isObjCRetainableType())
        return;

      while (ImplicitCastExpr *cast = dyn_cast<ImplicitCastExpr>(RHS)) {
        if (cast->getCastKind() == CK_ARCConsumeObject) {
          Diag(Loc, diag::warn_arc_retained_property_assign)
          << RHS->getSourceRange();
          return;
        }
        RHS = cast->getSubExpr();
      }
    } else if (Attributes & ObjCPropertyAttribute::kind_weak) {
      if (checkUnsafeAssignObject(*this, Loc, Qualifiers::OCL_Weak, RHS, true))
        return;
    }
  }
}

//===--- CHECK: Empty statement body (-Wempty-body) ---------------------===//

static bool ShouldDiagnoseEmptyStmtBody(const SourceManager &SourceMgr,
                                        SourceLocation StmtLoc,
                                        const NullStmt *Body) {
  // Do not warn if the body is a macro that expands to nothing, e.g:
  //
  // #define CALL(x)
  // if (condition)
  //   CALL(0);
  if (Body->hasLeadingEmptyMacro())
    return false;

  // Get line numbers of statement and body.
  bool StmtLineInvalid;
  unsigned StmtLine = SourceMgr.getPresumedLineNumber(StmtLoc,
                                                      &StmtLineInvalid);
  if (StmtLineInvalid)
    return false;

  bool BodyLineInvalid;
  unsigned BodyLine = SourceMgr.getSpellingLineNumber(Body->getSemiLoc(),
                                                      &BodyLineInvalid);
  if (BodyLineInvalid)
    return false;

  // Warn if null statement and body are on the same line.
  if (StmtLine != BodyLine)
    return false;

  return true;
}

void Sema::DiagnoseEmptyStmtBody(SourceLocation StmtLoc,
                                 const Stmt *Body,
                                 unsigned DiagID) {
  // Since this is a syntactic check, don't emit diagnostic for template
  // instantiations, this just adds noise.
  if (CurrentInstantiationScope)
    return;

  // The body should be a null statement.
  const NullStmt *NBody = dyn_cast<NullStmt>(Body);
  if (!NBody)
    return;

  // Do the usual checks.
  if (!ShouldDiagnoseEmptyStmtBody(SourceMgr, StmtLoc, NBody))
    return;

  Diag(NBody->getSemiLoc(), DiagID);
  Diag(NBody->getSemiLoc(), diag::note_empty_body_on_separate_line);
}

void Sema::DiagnoseEmptyLoopBody(const Stmt *S,
                                 const Stmt *PossibleBody) {
  assert(!CurrentInstantiationScope); // Ensured by caller

  SourceLocation StmtLoc;
  const Stmt *Body;
  unsigned DiagID;
  if (const ForStmt *FS = dyn_cast<ForStmt>(S)) {
    StmtLoc = FS->getRParenLoc();
    Body = FS->getBody();
    DiagID = diag::warn_empty_for_body;
  } else if (const WhileStmt *WS = dyn_cast<WhileStmt>(S)) {
    StmtLoc = WS->getRParenLoc();
    Body = WS->getBody();
    DiagID = diag::warn_empty_while_body;
  } else
    return; // Neither `for' nor `while'.

  // The body should be a null statement.
  const NullStmt *NBody = dyn_cast<NullStmt>(Body);
  if (!NBody)
    return;

  // Skip expensive checks if diagnostic is disabled.
  if (Diags.isIgnored(DiagID, NBody->getSemiLoc()))
    return;

  // Do the usual checks.
  if (!ShouldDiagnoseEmptyStmtBody(SourceMgr, StmtLoc, NBody))
    return;

  // `for(...);' and `while(...);' are popular idioms, so in order to keep
  // noise level low, emit diagnostics only if for/while is followed by a
  // CompoundStmt, e.g.:
  //    for (int i = 0; i < n; i++);
  //    {
  //      a(i);
  //    }
  // or if for/while is followed by a statement with more indentation
  // than for/while itself:
  //    for (int i = 0; i < n; i++);
  //      a(i);
  bool ProbableTypo = isa<CompoundStmt>(PossibleBody);
  if (!ProbableTypo) {
    bool BodyColInvalid;
    unsigned BodyCol = SourceMgr.getPresumedColumnNumber(
        PossibleBody->getBeginLoc(), &BodyColInvalid);
    if (BodyColInvalid)
      return;

    bool StmtColInvalid;
    unsigned StmtCol =
        SourceMgr.getPresumedColumnNumber(S->getBeginLoc(), &StmtColInvalid);
    if (StmtColInvalid)
      return;

    if (BodyCol > StmtCol)
      ProbableTypo = true;
  }

  if (ProbableTypo) {
    Diag(NBody->getSemiLoc(), DiagID);
    Diag(NBody->getSemiLoc(), diag::note_empty_body_on_separate_line);
  }
}

//===--- CHECK: Warn on self move with std::move. -------------------------===//

void Sema::DiagnoseSelfMove(const Expr *LHSExpr, const Expr *RHSExpr,
                             SourceLocation OpLoc) {
  if (Diags.isIgnored(diag::warn_sizeof_pointer_expr_memaccess, OpLoc))
    return;

  if (inTemplateInstantiation())
    return;

  // Strip parens and casts away.
  LHSExpr = LHSExpr->IgnoreParenImpCasts();
  RHSExpr = RHSExpr->IgnoreParenImpCasts();

  // Check for a call to std::move or for a static_cast<T&&>(..) to an xvalue
  // which we can treat as an inlined std::move
  if (const auto *CE = dyn_cast<CallExpr>(RHSExpr);
      CE && CE->getNumArgs() == 1 && CE->isCallToStdMove())
    RHSExpr = CE->getArg(0);
  else if (const auto *CXXSCE = dyn_cast<CXXStaticCastExpr>(RHSExpr);
           CXXSCE && CXXSCE->isXValue())
    RHSExpr = CXXSCE->getSubExpr();
  else
    return;

  const DeclRefExpr *LHSDeclRef = dyn_cast<DeclRefExpr>(LHSExpr);
  const DeclRefExpr *RHSDeclRef = dyn_cast<DeclRefExpr>(RHSExpr);

  // Two DeclRefExpr's, check that the decls are the same.
  if (LHSDeclRef && RHSDeclRef) {
    if (!LHSDeclRef->getDecl() || !RHSDeclRef->getDecl())
      return;
    if (LHSDeclRef->getDecl()->getCanonicalDecl() !=
        RHSDeclRef->getDecl()->getCanonicalDecl())
      return;

    auto D = Diag(OpLoc, diag::warn_self_move)
             << LHSExpr->getType() << LHSExpr->getSourceRange()
             << RHSExpr->getSourceRange();
    if (const FieldDecl *F =
            getSelfAssignmentClassMemberCandidate(RHSDeclRef->getDecl()))
      D << 1 << F
        << FixItHint::CreateInsertion(LHSDeclRef->getBeginLoc(), "this->");
    else
      D << 0;
    return;
  }

  // Member variables require a different approach to check for self moves.
  // MemberExpr's are the same if every nested MemberExpr refers to the same
  // Decl and that the base Expr's are DeclRefExpr's with the same Decl or
  // the base Expr's are CXXThisExpr's.
  const Expr *LHSBase = LHSExpr;
  const Expr *RHSBase = RHSExpr;
  const MemberExpr *LHSME = dyn_cast<MemberExpr>(LHSExpr);
  const MemberExpr *RHSME = dyn_cast<MemberExpr>(RHSExpr);
  if (!LHSME || !RHSME)
    return;

  while (LHSME && RHSME) {
    if (LHSME->getMemberDecl()->getCanonicalDecl() !=
        RHSME->getMemberDecl()->getCanonicalDecl())
      return;

    LHSBase = LHSME->getBase();
    RHSBase = RHSME->getBase();
    LHSME = dyn_cast<MemberExpr>(LHSBase);
    RHSME = dyn_cast<MemberExpr>(RHSBase);
  }

  LHSDeclRef = dyn_cast<DeclRefExpr>(LHSBase);
  RHSDeclRef = dyn_cast<DeclRefExpr>(RHSBase);
  if (LHSDeclRef && RHSDeclRef) {
    if (!LHSDeclRef->getDecl() || !RHSDeclRef->getDecl())
      return;
    if (LHSDeclRef->getDecl()->getCanonicalDecl() !=
        RHSDeclRef->getDecl()->getCanonicalDecl())
      return;

    Diag(OpLoc, diag::warn_self_move)
        << LHSExpr->getType() << 0 << LHSExpr->getSourceRange()
        << RHSExpr->getSourceRange();
    return;
  }

  if (isa<CXXThisExpr>(LHSBase) && isa<CXXThisExpr>(RHSBase))
    Diag(OpLoc, diag::warn_self_move)
        << LHSExpr->getType() << 0 << LHSExpr->getSourceRange()
        << RHSExpr->getSourceRange();
}

//===--- Layout compatibility ----------------------------------------------//

static bool isLayoutCompatible(const ASTContext &C, QualType T1, QualType T2);

/// Check if two enumeration types are layout-compatible.
static bool isLayoutCompatible(const ASTContext &C, const EnumDecl *ED1,
                               const EnumDecl *ED2) {
  // C++11 [dcl.enum] p8:
  // Two enumeration types are layout-compatible if they have the same
  // underlying type.
  return ED1->isComplete() && ED2->isComplete() &&
         C.hasSameType(ED1->getIntegerType(), ED2->getIntegerType());
}

/// Check if two fields are layout-compatible.
/// Can be used on union members, which are exempt from alignment requirement
/// of common initial sequence.
static bool isLayoutCompatible(const ASTContext &C, const FieldDecl *Field1,
                               const FieldDecl *Field2,
                               bool AreUnionMembers = false) {
  [[maybe_unused]] const Type *Field1Parent =
      Field1->getParent()->getTypeForDecl();
  [[maybe_unused]] const Type *Field2Parent =
      Field2->getParent()->getTypeForDecl();
  assert(((Field1Parent->isStructureOrClassType() &&
           Field2Parent->isStructureOrClassType()) ||
          (Field1Parent->isUnionType() && Field2Parent->isUnionType())) &&
         "Can't evaluate layout compatibility between a struct field and a "
         "union field.");
  assert(((!AreUnionMembers && Field1Parent->isStructureOrClassType()) ||
          (AreUnionMembers && Field1Parent->isUnionType())) &&
         "AreUnionMembers should be 'true' for union fields (only).");

  if (!isLayoutCompatible(C, Field1->getType(), Field2->getType()))
    return false;

  if (Field1->isBitField() != Field2->isBitField())
    return false;

  if (Field1->isBitField()) {
    // Make sure that the bit-fields are the same length.
    unsigned Bits1 = Field1->getBitWidthValue(C);
    unsigned Bits2 = Field2->getBitWidthValue(C);

    if (Bits1 != Bits2)
      return false;
  }

  if (Field1->hasAttr<clang::NoUniqueAddressAttr>() ||
      Field2->hasAttr<clang::NoUniqueAddressAttr>())
    return false;

  if (!AreUnionMembers &&
      Field1->getMaxAlignment() != Field2->getMaxAlignment())
    return false;

  return true;
}

/// Check if two standard-layout structs are layout-compatible.
/// (C++11 [class.mem] p17)
static bool isLayoutCompatibleStruct(const ASTContext &C, const RecordDecl *RD1,
                                     const RecordDecl *RD2) {
  // Get to the class where the fields are declared
  if (const CXXRecordDecl *D1CXX = dyn_cast<CXXRecordDecl>(RD1))
    RD1 = D1CXX->getStandardLayoutBaseWithFields();

  if (const CXXRecordDecl *D2CXX = dyn_cast<CXXRecordDecl>(RD2))
    RD2 = D2CXX->getStandardLayoutBaseWithFields();

  // Check the fields.
  return llvm::equal(RD1->fields(), RD2->fields(),
                     [&C](const FieldDecl *F1, const FieldDecl *F2) -> bool {
                       return isLayoutCompatible(C, F1, F2);
                     });
}

/// Check if two standard-layout unions are layout-compatible.
/// (C++11 [class.mem] p18)
static bool isLayoutCompatibleUnion(const ASTContext &C, const RecordDecl *RD1,
                                    const RecordDecl *RD2) {
  llvm::SmallPtrSet<const FieldDecl *, 8> UnmatchedFields;
  for (auto *Field2 : RD2->fields())
    UnmatchedFields.insert(Field2);

  for (auto *Field1 : RD1->fields()) {
    auto I = UnmatchedFields.begin();
    auto E = UnmatchedFields.end();

    for ( ; I != E; ++I) {
      if (isLayoutCompatible(C, Field1, *I, /*IsUnionMember=*/true)) {
        bool Result = UnmatchedFields.erase(*I);
        (void) Result;
        assert(Result);
        break;
      }
    }
    if (I == E)
      return false;
  }

  return UnmatchedFields.empty();
}

static bool isLayoutCompatible(const ASTContext &C, const RecordDecl *RD1,
                               const RecordDecl *RD2) {
  if (RD1->isUnion() != RD2->isUnion())
    return false;

  if (RD1->isUnion())
    return isLayoutCompatibleUnion(C, RD1, RD2);
  else
    return isLayoutCompatibleStruct(C, RD1, RD2);
}

/// Check if two types are layout-compatible in C++11 sense.
static bool isLayoutCompatible(const ASTContext &C, QualType T1, QualType T2) {
  if (T1.isNull() || T2.isNull())
    return false;

  // C++20 [basic.types] p11:
  // Two types cv1 T1 and cv2 T2 are layout-compatible types
  // if T1 and T2 are the same type, layout-compatible enumerations (9.7.1),
  // or layout-compatible standard-layout class types (11.4).
  T1 = T1.getCanonicalType().getUnqualifiedType();
  T2 = T2.getCanonicalType().getUnqualifiedType();

  if (C.hasSameType(T1, T2))
    return true;

  const Type::TypeClass TC1 = T1->getTypeClass();
  const Type::TypeClass TC2 = T2->getTypeClass();

  if (TC1 != TC2)
    return false;

  if (TC1 == Type::Enum) {
    return isLayoutCompatible(C,
                              cast<EnumType>(T1)->getDecl(),
                              cast<EnumType>(T2)->getDecl());
  } else if (TC1 == Type::Record) {
    if (!T1->isStandardLayoutType() || !T2->isStandardLayoutType())
      return false;

    return isLayoutCompatible(C,
                              cast<RecordType>(T1)->getDecl(),
                              cast<RecordType>(T2)->getDecl());
  }

  return false;
}

bool Sema::IsLayoutCompatible(QualType T1, QualType T2) const {
  return isLayoutCompatible(getASTContext(), T1, T2);
}

//===-------------- Pointer interconvertibility ----------------------------//

bool Sema::IsPointerInterconvertibleBaseOf(const TypeSourceInfo *Base,
                                           const TypeSourceInfo *Derived) {
  QualType BaseT = Base->getType()->getCanonicalTypeUnqualified();
  QualType DerivedT = Derived->getType()->getCanonicalTypeUnqualified();

  if (BaseT->isStructureOrClassType() && DerivedT->isStructureOrClassType() &&
      getASTContext().hasSameType(BaseT, DerivedT))
    return true;

  if (!IsDerivedFrom(Derived->getTypeLoc().getBeginLoc(), DerivedT, BaseT))
    return false;

  // Per [basic.compound]/4.3, containing object has to be standard-layout.
  if (DerivedT->getAsCXXRecordDecl()->isStandardLayout())
    return true;

  return false;
}

//===--- CHECK: pointer_with_type_tag attribute: datatypes should match ----//

/// Given a type tag expression find the type tag itself.
///
/// \param TypeExpr Type tag expression, as it appears in user's code.
///
/// \param VD Declaration of an identifier that appears in a type tag.
///
/// \param MagicValue Type tag magic value.
///
/// \param isConstantEvaluated whether the evalaution should be performed in

/// constant context.
static bool FindTypeTagExpr(const Expr *TypeExpr, const ASTContext &Ctx,
                            const ValueDecl **VD, uint64_t *MagicValue,
                            bool isConstantEvaluated) {
  while(true) {
    if (!TypeExpr)
      return false;

    TypeExpr = TypeExpr->IgnoreParenImpCasts()->IgnoreParenCasts();

    switch (TypeExpr->getStmtClass()) {
    case Stmt::UnaryOperatorClass: {
      const UnaryOperator *UO = cast<UnaryOperator>(TypeExpr);
      if (UO->getOpcode() == UO_AddrOf || UO->getOpcode() == UO_Deref) {
        TypeExpr = UO->getSubExpr();
        continue;
      }
      return false;
    }

    case Stmt::DeclRefExprClass: {
      const DeclRefExpr *DRE = cast<DeclRefExpr>(TypeExpr);
      *VD = DRE->getDecl();
      return true;
    }

    case Stmt::IntegerLiteralClass: {
      const IntegerLiteral *IL = cast<IntegerLiteral>(TypeExpr);
      llvm::APInt MagicValueAPInt = IL->getValue();
      if (MagicValueAPInt.getActiveBits() <= 64) {
        *MagicValue = MagicValueAPInt.getZExtValue();
        return true;
      } else
        return false;
    }

    case Stmt::BinaryConditionalOperatorClass:
    case Stmt::ConditionalOperatorClass: {
      const AbstractConditionalOperator *ACO =
          cast<AbstractConditionalOperator>(TypeExpr);
      bool Result;
      if (ACO->getCond()->EvaluateAsBooleanCondition(Result, Ctx,
                                                     isConstantEvaluated)) {
        if (Result)
          TypeExpr = ACO->getTrueExpr();
        else
          TypeExpr = ACO->getFalseExpr();
        continue;
      }
      return false;
    }

    case Stmt::BinaryOperatorClass: {
      const BinaryOperator *BO = cast<BinaryOperator>(TypeExpr);
      if (BO->getOpcode() == BO_Comma) {
        TypeExpr = BO->getRHS();
        continue;
      }
      return false;
    }

    default:
      return false;
    }
  }
}

/// Retrieve the C type corresponding to type tag TypeExpr.
///
/// \param TypeExpr Expression that specifies a type tag.
///
/// \param MagicValues Registered magic values.
///
/// \param FoundWrongKind Set to true if a type tag was found, but of a wrong
///        kind.
///
/// \param TypeInfo Information about the corresponding C type.
///
/// \param isConstantEvaluated whether the evalaution should be performed in
/// constant context.
///
/// \returns true if the corresponding C type was found.
static bool GetMatchingCType(
    const IdentifierInfo *ArgumentKind, const Expr *TypeExpr,
    const ASTContext &Ctx,
    const llvm::DenseMap<Sema::TypeTagMagicValue, Sema::TypeTagData>
        *MagicValues,
    bool &FoundWrongKind, Sema::TypeTagData &TypeInfo,
    bool isConstantEvaluated) {
  FoundWrongKind = false;

  // Variable declaration that has type_tag_for_datatype attribute.
  const ValueDecl *VD = nullptr;

  uint64_t MagicValue;

  if (!FindTypeTagExpr(TypeExpr, Ctx, &VD, &MagicValue, isConstantEvaluated))
    return false;

  if (VD) {
    if (TypeTagForDatatypeAttr *I = VD->getAttr<TypeTagForDatatypeAttr>()) {
      if (I->getArgumentKind() != ArgumentKind) {
        FoundWrongKind = true;
        return false;
      }
      TypeInfo.Type = I->getMatchingCType();
      TypeInfo.LayoutCompatible = I->getLayoutCompatible();
      TypeInfo.MustBeNull = I->getMustBeNull();
      return true;
    }
    return false;
  }

  if (!MagicValues)
    return false;

  llvm::DenseMap<Sema::TypeTagMagicValue,
                 Sema::TypeTagData>::const_iterator I =
      MagicValues->find(std::make_pair(ArgumentKind, MagicValue));
  if (I == MagicValues->end())
    return false;

  TypeInfo = I->second;
  return true;
}

void Sema::RegisterTypeTagForDatatype(const IdentifierInfo *ArgumentKind,
                                      uint64_t MagicValue, QualType Type,
                                      bool LayoutCompatible,
                                      bool MustBeNull) {
  if (!TypeTagForDatatypeMagicValues)
    TypeTagForDatatypeMagicValues.reset(
        new llvm::DenseMap<TypeTagMagicValue, TypeTagData>);

  TypeTagMagicValue Magic(ArgumentKind, MagicValue);
  (*TypeTagForDatatypeMagicValues)[Magic] =
      TypeTagData(Type, LayoutCompatible, MustBeNull);
}

static bool IsSameCharType(QualType T1, QualType T2) {
  const BuiltinType *BT1 = T1->getAs<BuiltinType>();
  if (!BT1)
    return false;

  const BuiltinType *BT2 = T2->getAs<BuiltinType>();
  if (!BT2)
    return false;

  BuiltinType::Kind T1Kind = BT1->getKind();
  BuiltinType::Kind T2Kind = BT2->getKind();

  return (T1Kind == BuiltinType::SChar  && T2Kind == BuiltinType::Char_S) ||
         (T1Kind == BuiltinType::UChar  && T2Kind == BuiltinType::Char_U) ||
         (T1Kind == BuiltinType::Char_U && T2Kind == BuiltinType::UChar) ||
         (T1Kind == BuiltinType::Char_S && T2Kind == BuiltinType::SChar);
}

void Sema::CheckArgumentWithTypeTag(const ArgumentWithTypeTagAttr *Attr,
                                    const ArrayRef<const Expr *> ExprArgs,
                                    SourceLocation CallSiteLoc) {
  const IdentifierInfo *ArgumentKind = Attr->getArgumentKind();
  bool IsPointerAttr = Attr->getIsPointer();

  // Retrieve the argument representing the 'type_tag'.
  unsigned TypeTagIdxAST = Attr->getTypeTagIdx().getASTIndex();
  if (TypeTagIdxAST >= ExprArgs.size()) {
    Diag(CallSiteLoc, diag::err_tag_index_out_of_range)
        << 0 << Attr->getTypeTagIdx().getSourceIndex();
    return;
  }
  const Expr *TypeTagExpr = ExprArgs[TypeTagIdxAST];
  bool FoundWrongKind;
  TypeTagData TypeInfo;
  if (!GetMatchingCType(ArgumentKind, TypeTagExpr, Context,
                        TypeTagForDatatypeMagicValues.get(), FoundWrongKind,
                        TypeInfo, isConstantEvaluatedContext())) {
    if (FoundWrongKind)
      Diag(TypeTagExpr->getExprLoc(),
           diag::warn_type_tag_for_datatype_wrong_kind)
        << TypeTagExpr->getSourceRange();
    return;
  }

  // Retrieve the argument representing the 'arg_idx'.
  unsigned ArgumentIdxAST = Attr->getArgumentIdx().getASTIndex();
  if (ArgumentIdxAST >= ExprArgs.size()) {
    Diag(CallSiteLoc, diag::err_tag_index_out_of_range)
        << 1 << Attr->getArgumentIdx().getSourceIndex();
    return;
  }
  const Expr *ArgumentExpr = ExprArgs[ArgumentIdxAST];
  if (IsPointerAttr) {
    // Skip implicit cast of pointer to `void *' (as a function argument).
    if (const ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(ArgumentExpr))
      if (ICE->getType()->isVoidPointerType() &&
          ICE->getCastKind() == CK_BitCast)
        ArgumentExpr = ICE->getSubExpr();
  }
  QualType ArgumentType = ArgumentExpr->getType();

  // Passing a `void*' pointer shouldn't trigger a warning.
  if (IsPointerAttr && ArgumentType->isVoidPointerType())
    return;

  if (TypeInfo.MustBeNull) {
    // Type tag with matching void type requires a null pointer.
    if (!ArgumentExpr->isNullPointerConstant(Context,
                                             Expr::NPC_ValueDependentIsNotNull)) {
      Diag(ArgumentExpr->getExprLoc(),
           diag::warn_type_safety_null_pointer_required)
          << ArgumentKind->getName()
          << ArgumentExpr->getSourceRange()
          << TypeTagExpr->getSourceRange();
    }
    return;
  }

  QualType RequiredType = TypeInfo.Type;
  if (IsPointerAttr)
    RequiredType = Context.getPointerType(RequiredType);

  bool mismatch = false;
  if (!TypeInfo.LayoutCompatible) {
    mismatch = !Context.hasSameType(ArgumentType, RequiredType);

    // C++11 [basic.fundamental] p1:
    // Plain char, signed char, and unsigned char are three distinct types.
    //
    // But we treat plain `char' as equivalent to `signed char' or `unsigned
    // char' depending on the current char signedness mode.
    if (mismatch)
      if ((IsPointerAttr && IsSameCharType(ArgumentType->getPointeeType(),
                                           RequiredType->getPointeeType())) ||
          (!IsPointerAttr && IsSameCharType(ArgumentType, RequiredType)))
        mismatch = false;
  } else
    if (IsPointerAttr)
      mismatch = !isLayoutCompatible(Context,
                                     ArgumentType->getPointeeType(),
                                     RequiredType->getPointeeType());
    else
      mismatch = !isLayoutCompatible(Context, ArgumentType, RequiredType);

  if (mismatch)
    Diag(ArgumentExpr->getExprLoc(), diag::warn_type_safety_type_mismatch)
        << ArgumentType << ArgumentKind
        << TypeInfo.LayoutCompatible << RequiredType
        << ArgumentExpr->getSourceRange()
        << TypeTagExpr->getSourceRange();
}

void Sema::AddPotentialMisalignedMembers(Expr *E, RecordDecl *RD, ValueDecl *MD,
                                         CharUnits Alignment) {
  MisalignedMembers.emplace_back(E, RD, MD, Alignment);
}

void Sema::DiagnoseMisalignedMembers() {
  for (MisalignedMember &m : MisalignedMembers) {
    const NamedDecl *ND = m.RD;
    if (ND->getName().empty()) {
      if (const TypedefNameDecl *TD = m.RD->getTypedefNameForAnonDecl())
        ND = TD;
    }
    Diag(m.E->getBeginLoc(), diag::warn_taking_address_of_packed_member)
        << m.MD << ND << m.E->getSourceRange();
  }
  MisalignedMembers.clear();
}

void Sema::DiscardMisalignedMemberAddress(const Type *T, Expr *E) {
  E = E->IgnoreParens();
  if (!T->isPointerType() && !T->isIntegerType() && !T->isDependentType())
    return;
  if (isa<UnaryOperator>(E) &&
      cast<UnaryOperator>(E)->getOpcode() == UO_AddrOf) {
    auto *Op = cast<UnaryOperator>(E)->getSubExpr()->IgnoreParens();
    if (isa<MemberExpr>(Op)) {
      auto *MA = llvm::find(MisalignedMembers, MisalignedMember(Op));
      if (MA != MisalignedMembers.end() &&
          (T->isDependentType() || T->isIntegerType() ||
           (T->isPointerType() && (T->getPointeeType()->isIncompleteType() ||
                                   Context.getTypeAlignInChars(
                                       T->getPointeeType()) <= MA->Alignment))))
        MisalignedMembers.erase(MA);
    }
  }
}

void Sema::RefersToMemberWithReducedAlignment(
    Expr *E,
    llvm::function_ref<void(Expr *, RecordDecl *, FieldDecl *, CharUnits)>
        Action) {
  const auto *ME = dyn_cast<MemberExpr>(E);
  if (!ME)
    return;

  // No need to check expressions with an __unaligned-qualified type.
  if (E->getType().getQualifiers().hasUnaligned())
    return;

  // For a chain of MemberExpr like "a.b.c.d" this list
  // will keep FieldDecl's like [d, c, b].
  SmallVector<FieldDecl *, 4> ReverseMemberChain;
  const MemberExpr *TopME = nullptr;
  bool AnyIsPacked = false;
  do {
    QualType BaseType = ME->getBase()->getType();
    if (BaseType->isDependentType())
      return;
    if (ME->isArrow())
      BaseType = BaseType->getPointeeType();
    RecordDecl *RD = BaseType->castAs<RecordType>()->getDecl();
    if (RD->isInvalidDecl())
      return;

    ValueDecl *MD = ME->getMemberDecl();
    auto *FD = dyn_cast<FieldDecl>(MD);
    // We do not care about non-data members.
    if (!FD || FD->isInvalidDecl())
      return;

    AnyIsPacked =
        AnyIsPacked || (RD->hasAttr<PackedAttr>() || MD->hasAttr<PackedAttr>());
    ReverseMemberChain.push_back(FD);

    TopME = ME;
    ME = dyn_cast<MemberExpr>(ME->getBase()->IgnoreParens());
  } while (ME);
  assert(TopME && "We did not compute a topmost MemberExpr!");

  // Not the scope of this diagnostic.
  if (!AnyIsPacked)
    return;

  const Expr *TopBase = TopME->getBase()->IgnoreParenImpCasts();
  const auto *DRE = dyn_cast<DeclRefExpr>(TopBase);
  // TODO: The innermost base of the member expression may be too complicated.
  // For now, just disregard these cases. This is left for future
  // improvement.
  if (!DRE && !isa<CXXThisExpr>(TopBase))
      return;

  // Alignment expected by the whole expression.
  CharUnits ExpectedAlignment = Context.getTypeAlignInChars(E->getType());

  // No need to do anything else with this case.
  if (ExpectedAlignment.isOne())
    return;

  // Synthesize offset of the whole access.
  CharUnits Offset;
  for (const FieldDecl *FD : llvm::reverse(ReverseMemberChain))
    Offset += Context.toCharUnitsFromBits(Context.getFieldOffset(FD));

  // Compute the CompleteObjectAlignment as the alignment of the whole chain.
  CharUnits CompleteObjectAlignment = Context.getTypeAlignInChars(
      ReverseMemberChain.back()->getParent()->getTypeForDecl());

  // The base expression of the innermost MemberExpr may give
  // stronger guarantees than the class containing the member.
  if (DRE && !TopME->isArrow()) {
    const ValueDecl *VD = DRE->getDecl();
    if (!VD->getType()->isReferenceType())
      CompleteObjectAlignment =
          std::max(CompleteObjectAlignment, Context.getDeclAlign(VD));
  }

  // Check if the synthesized offset fulfills the alignment.
  if (Offset % ExpectedAlignment != 0 ||
      // It may fulfill the offset it but the effective alignment may still be
      // lower than the expected expression alignment.
      CompleteObjectAlignment < ExpectedAlignment) {
    // If this happens, we want to determine a sensible culprit of this.
    // Intuitively, watching the chain of member expressions from right to
    // left, we start with the required alignment (as required by the field
    // type) but some packed attribute in that chain has reduced the alignment.
    // It may happen that another packed structure increases it again. But if
    // we are here such increase has not been enough. So pointing the first
    // FieldDecl that either is packed or else its RecordDecl is,
    // seems reasonable.
    FieldDecl *FD = nullptr;
    CharUnits Alignment;
    for (FieldDecl *FDI : ReverseMemberChain) {
      if (FDI->hasAttr<PackedAttr>() ||
          FDI->getParent()->hasAttr<PackedAttr>()) {
        FD = FDI;
        Alignment = std::min(
            Context.getTypeAlignInChars(FD->getType()),
            Context.getTypeAlignInChars(FD->getParent()->getTypeForDecl()));
        break;
      }
    }
    assert(FD && "We did not find a packed FieldDecl!");
    Action(E, FD->getParent(), FD, Alignment);
  }
}

void Sema::CheckAddressOfPackedMember(Expr *rhs) {
  using namespace std::placeholders;

  RefersToMemberWithReducedAlignment(
      rhs, std::bind(&Sema::AddPotentialMisalignedMembers, std::ref(*this), _1,
                     _2, _3, _4));
}

bool Sema::PrepareBuiltinElementwiseMathOneArgCall(CallExpr *TheCall) {
  if (checkArgCount(TheCall, 1))
    return true;

  ExprResult A = UsualUnaryConversions(TheCall->getArg(0));
  if (A.isInvalid())
    return true;

  TheCall->setArg(0, A.get());
  QualType TyA = A.get()->getType();

  if (checkMathBuiltinElementType(*this, A.get()->getBeginLoc(), TyA, 1))
    return true;

  TheCall->setType(TyA);
  return false;
}

bool Sema::BuiltinElementwiseMath(CallExpr *TheCall) {
  QualType Res;
  if (BuiltinVectorMath(TheCall, Res))
    return true;
  TheCall->setType(Res);
  return false;
}

bool Sema::BuiltinVectorToScalarMath(CallExpr *TheCall) {
  QualType Res;
  if (BuiltinVectorMath(TheCall, Res))
    return true;

  if (auto *VecTy0 = Res->getAs<VectorType>())
    TheCall->setType(VecTy0->getElementType());
  else
    TheCall->setType(Res);

  return false;
}

bool Sema::BuiltinVectorMath(CallExpr *TheCall, QualType &Res) {
  if (checkArgCount(TheCall, 2))
    return true;

  ExprResult A = TheCall->getArg(0);
  ExprResult B = TheCall->getArg(1);
  // Do standard promotions between the two arguments, returning their common
  // type.
  Res = UsualArithmeticConversions(A, B, TheCall->getExprLoc(), ACK_Comparison);
  if (A.isInvalid() || B.isInvalid())
    return true;

  QualType TyA = A.get()->getType();
  QualType TyB = B.get()->getType();

  if (Res.isNull() || TyA.getCanonicalType() != TyB.getCanonicalType())
    return Diag(A.get()->getBeginLoc(),
                diag::err_typecheck_call_different_arg_types)
           << TyA << TyB;

  if (checkMathBuiltinElementType(*this, A.get()->getBeginLoc(), TyA, 1))
    return true;

  TheCall->setArg(0, A.get());
  TheCall->setArg(1, B.get());
  return false;
}

bool Sema::BuiltinElementwiseTernaryMath(CallExpr *TheCall,
                                         bool CheckForFloatArgs) {
  if (checkArgCount(TheCall, 3))
    return true;

  Expr *Args[3];
  for (int I = 0; I < 3; ++I) {
    ExprResult Converted = UsualUnaryConversions(TheCall->getArg(I));
    if (Converted.isInvalid())
      return true;
    Args[I] = Converted.get();
  }

  if (CheckForFloatArgs) {
    int ArgOrdinal = 1;
    for (Expr *Arg : Args) {
      if (checkFPMathBuiltinElementType(*this, Arg->getBeginLoc(),
                                        Arg->getType(), ArgOrdinal++))
        return true;
    }
  } else {
    int ArgOrdinal = 1;
    for (Expr *Arg : Args) {
      if (checkMathBuiltinElementType(*this, Arg->getBeginLoc(), Arg->getType(),
                                      ArgOrdinal++))
        return true;
    }
  }

  for (int I = 1; I < 3; ++I) {
    if (Args[0]->getType().getCanonicalType() !=
        Args[I]->getType().getCanonicalType()) {
      return Diag(Args[0]->getBeginLoc(),
                  diag::err_typecheck_call_different_arg_types)
             << Args[0]->getType() << Args[I]->getType();
    }

    TheCall->setArg(I, Args[I]);
  }

  TheCall->setType(Args[0]->getType());
  return false;
}

bool Sema::PrepareBuiltinReduceMathOneArgCall(CallExpr *TheCall) {
  if (checkArgCount(TheCall, 1))
    return true;

  ExprResult A = UsualUnaryConversions(TheCall->getArg(0));
  if (A.isInvalid())
    return true;

  TheCall->setArg(0, A.get());
  return false;
}

bool Sema::BuiltinNonDeterministicValue(CallExpr *TheCall) {
  if (checkArgCount(TheCall, 1))
    return true;

  ExprResult Arg = TheCall->getArg(0);
  QualType TyArg = Arg.get()->getType();

  if (!TyArg->isBuiltinType() && !TyArg->isVectorType())
    return Diag(TheCall->getArg(0)->getBeginLoc(), diag::err_builtin_invalid_arg_type)
           << 1 << /*vector, integer or floating point ty*/ 0 << TyArg;

  TheCall->setType(TyArg);
  return false;
}

ExprResult Sema::BuiltinMatrixTranspose(CallExpr *TheCall,
                                        ExprResult CallResult) {
  if (checkArgCount(TheCall, 1))
    return ExprError();

  ExprResult MatrixArg = DefaultLvalueConversion(TheCall->getArg(0));
  if (MatrixArg.isInvalid())
    return MatrixArg;
  Expr *Matrix = MatrixArg.get();

  auto *MType = Matrix->getType()->getAs<ConstantMatrixType>();
  if (!MType) {
    Diag(Matrix->getBeginLoc(), diag::err_builtin_invalid_arg_type)
        << 1 << /* matrix ty*/ 1 << Matrix->getType();
    return ExprError();
  }

  // Create returned matrix type by swapping rows and columns of the argument
  // matrix type.
  QualType ResultType = Context.getConstantMatrixType(
      MType->getElementType(), MType->getNumColumns(), MType->getNumRows());

  // Change the return type to the type of the returned matrix.
  TheCall->setType(ResultType);

  // Update call argument to use the possibly converted matrix argument.
  TheCall->setArg(0, Matrix);
  return CallResult;
}

// Get and verify the matrix dimensions.
static std::optional<unsigned>
getAndVerifyMatrixDimension(Expr *Expr, StringRef Name, Sema &S) {
  SourceLocation ErrorPos;
  std::optional<llvm::APSInt> Value =
      Expr->getIntegerConstantExpr(S.Context, &ErrorPos);
  if (!Value) {
    S.Diag(Expr->getBeginLoc(), diag::err_builtin_matrix_scalar_unsigned_arg)
        << Name;
    return {};
  }
  uint64_t Dim = Value->getZExtValue();
  if (!ConstantMatrixType::isDimensionValid(Dim)) {
    S.Diag(Expr->getBeginLoc(), diag::err_builtin_matrix_invalid_dimension)
        << Name << ConstantMatrixType::getMaxElementsPerDimension();
    return {};
  }
  return Dim;
}

ExprResult Sema::BuiltinMatrixColumnMajorLoad(CallExpr *TheCall,
                                              ExprResult CallResult) {
  if (!getLangOpts().MatrixTypes) {
    Diag(TheCall->getBeginLoc(), diag::err_builtin_matrix_disabled);
    return ExprError();
  }

  if (checkArgCount(TheCall, 4))
    return ExprError();

  unsigned PtrArgIdx = 0;
  Expr *PtrExpr = TheCall->getArg(PtrArgIdx);
  Expr *RowsExpr = TheCall->getArg(1);
  Expr *ColumnsExpr = TheCall->getArg(2);
  Expr *StrideExpr = TheCall->getArg(3);

  bool ArgError = false;

  // Check pointer argument.
  {
    ExprResult PtrConv = DefaultFunctionArrayLvalueConversion(PtrExpr);
    if (PtrConv.isInvalid())
      return PtrConv;
    PtrExpr = PtrConv.get();
    TheCall->setArg(0, PtrExpr);
    if (PtrExpr->isTypeDependent()) {
      TheCall->setType(Context.DependentTy);
      return TheCall;
    }
  }

  auto *PtrTy = PtrExpr->getType()->getAs<PointerType>();
  QualType ElementTy;
  if (!PtrTy) {
    Diag(PtrExpr->getBeginLoc(), diag::err_builtin_invalid_arg_type)
        << PtrArgIdx + 1 << /*pointer to element ty*/ 2 << PtrExpr->getType();
    ArgError = true;
  } else {
    ElementTy = PtrTy->getPointeeType().getUnqualifiedType();

    if (!ConstantMatrixType::isValidElementType(ElementTy)) {
      Diag(PtrExpr->getBeginLoc(), diag::err_builtin_invalid_arg_type)
          << PtrArgIdx + 1 << /* pointer to element ty*/ 2
          << PtrExpr->getType();
      ArgError = true;
    }
  }

  // Apply default Lvalue conversions and convert the expression to size_t.
  auto ApplyArgumentConversions = [this](Expr *E) {
    ExprResult Conv = DefaultLvalueConversion(E);
    if (Conv.isInvalid())
      return Conv;

    return tryConvertExprToType(Conv.get(), Context.getSizeType());
  };

  // Apply conversion to row and column expressions.
  ExprResult RowsConv = ApplyArgumentConversions(RowsExpr);
  if (!RowsConv.isInvalid()) {
    RowsExpr = RowsConv.get();
    TheCall->setArg(1, RowsExpr);
  } else
    RowsExpr = nullptr;

  ExprResult ColumnsConv = ApplyArgumentConversions(ColumnsExpr);
  if (!ColumnsConv.isInvalid()) {
    ColumnsExpr = ColumnsConv.get();
    TheCall->setArg(2, ColumnsExpr);
  } else
    ColumnsExpr = nullptr;

  // If any part of the result matrix type is still pending, just use
  // Context.DependentTy, until all parts are resolved.
  if ((RowsExpr && RowsExpr->isTypeDependent()) ||
      (ColumnsExpr && ColumnsExpr->isTypeDependent())) {
    TheCall->setType(Context.DependentTy);
    return CallResult;
  }

  // Check row and column dimensions.
  std::optional<unsigned> MaybeRows;
  if (RowsExpr)
    MaybeRows = getAndVerifyMatrixDimension(RowsExpr, "row", *this);

  std::optional<unsigned> MaybeColumns;
  if (ColumnsExpr)
    MaybeColumns = getAndVerifyMatrixDimension(ColumnsExpr, "column", *this);

  // Check stride argument.
  ExprResult StrideConv = ApplyArgumentConversions(StrideExpr);
  if (StrideConv.isInvalid())
    return ExprError();
  StrideExpr = StrideConv.get();
  TheCall->setArg(3, StrideExpr);

  if (MaybeRows) {
    if (std::optional<llvm::APSInt> Value =
            StrideExpr->getIntegerConstantExpr(Context)) {
      uint64_t Stride = Value->getZExtValue();
      if (Stride < *MaybeRows) {
        Diag(StrideExpr->getBeginLoc(),
             diag::err_builtin_matrix_stride_too_small);
        ArgError = true;
      }
    }
  }

  if (ArgError || !MaybeRows || !MaybeColumns)
    return ExprError();

  TheCall->setType(
      Context.getConstantMatrixType(ElementTy, *MaybeRows, *MaybeColumns));
  return CallResult;
}

ExprResult Sema::BuiltinMatrixColumnMajorStore(CallExpr *TheCall,
                                               ExprResult CallResult) {
  if (checkArgCount(TheCall, 3))
    return ExprError();

  unsigned PtrArgIdx = 1;
  Expr *MatrixExpr = TheCall->getArg(0);
  Expr *PtrExpr = TheCall->getArg(PtrArgIdx);
  Expr *StrideExpr = TheCall->getArg(2);

  bool ArgError = false;

  {
    ExprResult MatrixConv = DefaultLvalueConversion(MatrixExpr);
    if (MatrixConv.isInvalid())
      return MatrixConv;
    MatrixExpr = MatrixConv.get();
    TheCall->setArg(0, MatrixExpr);
  }
  if (MatrixExpr->isTypeDependent()) {
    TheCall->setType(Context.DependentTy);
    return TheCall;
  }

  auto *MatrixTy = MatrixExpr->getType()->getAs<ConstantMatrixType>();
  if (!MatrixTy) {
    Diag(MatrixExpr->getBeginLoc(), diag::err_builtin_invalid_arg_type)
        << 1 << /*matrix ty */ 1 << MatrixExpr->getType();
    ArgError = true;
  }

  {
    ExprResult PtrConv = DefaultFunctionArrayLvalueConversion(PtrExpr);
    if (PtrConv.isInvalid())
      return PtrConv;
    PtrExpr = PtrConv.get();
    TheCall->setArg(1, PtrExpr);
    if (PtrExpr->isTypeDependent()) {
      TheCall->setType(Context.DependentTy);
      return TheCall;
    }
  }

  // Check pointer argument.
  auto *PtrTy = PtrExpr->getType()->getAs<PointerType>();
  if (!PtrTy) {
    Diag(PtrExpr->getBeginLoc(), diag::err_builtin_invalid_arg_type)
        << PtrArgIdx + 1 << /*pointer to element ty*/ 2 << PtrExpr->getType();
    ArgError = true;
  } else {
    QualType ElementTy = PtrTy->getPointeeType();
    if (ElementTy.isConstQualified()) {
      Diag(PtrExpr->getBeginLoc(), diag::err_builtin_matrix_store_to_const);
      ArgError = true;
    }
    ElementTy = ElementTy.getUnqualifiedType().getCanonicalType();
    if (MatrixTy &&
        !Context.hasSameType(ElementTy, MatrixTy->getElementType())) {
      Diag(PtrExpr->getBeginLoc(),
           diag::err_builtin_matrix_pointer_arg_mismatch)
          << ElementTy << MatrixTy->getElementType();
      ArgError = true;
    }
  }

  // Apply default Lvalue conversions and convert the stride expression to
  // size_t.
  {
    ExprResult StrideConv = DefaultLvalueConversion(StrideExpr);
    if (StrideConv.isInvalid())
      return StrideConv;

    StrideConv = tryConvertExprToType(StrideConv.get(), Context.getSizeType());
    if (StrideConv.isInvalid())
      return StrideConv;
    StrideExpr = StrideConv.get();
    TheCall->setArg(2, StrideExpr);
  }

  // Check stride argument.
  if (MatrixTy) {
    if (std::optional<llvm::APSInt> Value =
            StrideExpr->getIntegerConstantExpr(Context)) {
      uint64_t Stride = Value->getZExtValue();
      if (Stride < MatrixTy->getNumRows()) {
        Diag(StrideExpr->getBeginLoc(),
             diag::err_builtin_matrix_stride_too_small);
        ArgError = true;
      }
    }
  }

  if (ArgError)
    return ExprError();

  return CallResult;
}

void Sema::CheckTCBEnforcement(const SourceLocation CallExprLoc,
                               const NamedDecl *Callee) {
  // This warning does not make sense in code that has no runtime behavior.
  if (isUnevaluatedContext())
    return;

  const NamedDecl *Caller = getCurFunctionOrMethodDecl();

  if (!Caller || !Caller->hasAttr<EnforceTCBAttr>())
    return;

  // Search through the enforce_tcb and enforce_tcb_leaf attributes to find
  // all TCBs the callee is a part of.
  llvm::StringSet<> CalleeTCBs;
  for (const auto *A : Callee->specific_attrs<EnforceTCBAttr>())
    CalleeTCBs.insert(A->getTCBName());
  for (const auto *A : Callee->specific_attrs<EnforceTCBLeafAttr>())
    CalleeTCBs.insert(A->getTCBName());

  // Go through the TCBs the caller is a part of and emit warnings if Caller
  // is in a TCB that the Callee is not.
  for (const auto *A : Caller->specific_attrs<EnforceTCBAttr>()) {
    StringRef CallerTCB = A->getTCBName();
    if (CalleeTCBs.count(CallerTCB) == 0) {
      this->Diag(CallExprLoc, diag::warn_tcb_enforcement_violation)
          << Callee << CallerTCB;
    }
  }
}
