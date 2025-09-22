//===------ SemaPPC.cpp ------ PowerPC target-specific routines -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis functions specific to PowerPC.
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/SemaPPC.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/CharUnits.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Type.h"
#include "clang/Basic/DiagnosticSema.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/TargetBuiltins.h"
#include "clang/Sema/Sema.h"
#include "llvm/ADT/APSInt.h"

namespace clang {

SemaPPC::SemaPPC(Sema &S) : SemaBase(S) {}

void SemaPPC::checkAIXMemberAlignment(SourceLocation Loc, const Expr *Arg) {
  const auto *ICE = dyn_cast<ImplicitCastExpr>(Arg->IgnoreParens());
  if (!ICE)
    return;

  const auto *DR = dyn_cast<DeclRefExpr>(ICE->getSubExpr());
  if (!DR)
    return;

  const auto *PD = dyn_cast<ParmVarDecl>(DR->getDecl());
  if (!PD || !PD->getType()->isRecordType())
    return;

  QualType ArgType = Arg->getType();
  for (const FieldDecl *FD :
       ArgType->castAs<RecordType>()->getDecl()->fields()) {
    if (const auto *AA = FD->getAttr<AlignedAttr>()) {
      CharUnits Alignment = getASTContext().toCharUnitsFromBits(
          AA->getAlignment(getASTContext()));
      if (Alignment.getQuantity() == 16) {
        Diag(FD->getLocation(), diag::warn_not_xl_compatible) << FD;
        Diag(Loc, diag::note_misaligned_member_used_here) << PD;
      }
    }
  }
}

static bool isPPC_64Builtin(unsigned BuiltinID) {
  // These builtins only work on PPC 64bit targets.
  switch (BuiltinID) {
  case PPC::BI__builtin_divde:
  case PPC::BI__builtin_divdeu:
  case PPC::BI__builtin_bpermd:
  case PPC::BI__builtin_pdepd:
  case PPC::BI__builtin_pextd:
  case PPC::BI__builtin_ppc_ldarx:
  case PPC::BI__builtin_ppc_stdcx:
  case PPC::BI__builtin_ppc_tdw:
  case PPC::BI__builtin_ppc_trapd:
  case PPC::BI__builtin_ppc_cmpeqb:
  case PPC::BI__builtin_ppc_setb:
  case PPC::BI__builtin_ppc_mulhd:
  case PPC::BI__builtin_ppc_mulhdu:
  case PPC::BI__builtin_ppc_maddhd:
  case PPC::BI__builtin_ppc_maddhdu:
  case PPC::BI__builtin_ppc_maddld:
  case PPC::BI__builtin_ppc_load8r:
  case PPC::BI__builtin_ppc_store8r:
  case PPC::BI__builtin_ppc_insert_exp:
  case PPC::BI__builtin_ppc_extract_sig:
  case PPC::BI__builtin_ppc_addex:
  case PPC::BI__builtin_darn:
  case PPC::BI__builtin_darn_raw:
  case PPC::BI__builtin_ppc_compare_and_swaplp:
  case PPC::BI__builtin_ppc_fetch_and_addlp:
  case PPC::BI__builtin_ppc_fetch_and_andlp:
  case PPC::BI__builtin_ppc_fetch_and_orlp:
  case PPC::BI__builtin_ppc_fetch_and_swaplp:
    return true;
  }
  return false;
}

bool SemaPPC::CheckPPCBuiltinFunctionCall(const TargetInfo &TI,
                                          unsigned BuiltinID,
                                          CallExpr *TheCall) {
  ASTContext &Context = getASTContext();
  unsigned i = 0, l = 0, u = 0;
  bool IsTarget64Bit = TI.getTypeWidth(TI.getIntPtrType()) == 64;
  llvm::APSInt Result;

  if (isPPC_64Builtin(BuiltinID) && !IsTarget64Bit)
    return Diag(TheCall->getBeginLoc(), diag::err_64_bit_builtin_32_bit_tgt)
           << TheCall->getSourceRange();

  switch (BuiltinID) {
  default:
    return false;
  case PPC::BI__builtin_altivec_crypto_vshasigmaw:
  case PPC::BI__builtin_altivec_crypto_vshasigmad:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 1) ||
           SemaRef.BuiltinConstantArgRange(TheCall, 2, 0, 15);
  case PPC::BI__builtin_altivec_dss:
    return SemaRef.BuiltinConstantArgRange(TheCall, 0, 0, 3);
  case PPC::BI__builtin_tbegin:
  case PPC::BI__builtin_tend:
    return SemaRef.BuiltinConstantArgRange(TheCall, 0, 0, 1);
  case PPC::BI__builtin_tsr:
    return SemaRef.BuiltinConstantArgRange(TheCall, 0, 0, 7);
  case PPC::BI__builtin_tabortwc:
  case PPC::BI__builtin_tabortdc:
    return SemaRef.BuiltinConstantArgRange(TheCall, 0, 0, 31);
  case PPC::BI__builtin_tabortwci:
  case PPC::BI__builtin_tabortdci:
    return SemaRef.BuiltinConstantArgRange(TheCall, 0, 0, 31) ||
           SemaRef.BuiltinConstantArgRange(TheCall, 2, 0, 31);
  // According to GCC 'Basic PowerPC Built-in Functions Available on ISA 2.05',
  // __builtin_(un)pack_longdouble are available only if long double uses IBM
  // extended double representation.
  case PPC::BI__builtin_unpack_longdouble:
    if (SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 1))
      return true;
    [[fallthrough]];
  case PPC::BI__builtin_pack_longdouble:
    if (&TI.getLongDoubleFormat() != &llvm::APFloat::PPCDoubleDouble())
      return Diag(TheCall->getBeginLoc(), diag::err_ppc_builtin_requires_abi)
             << "ibmlongdouble";
    return false;
  case PPC::BI__builtin_altivec_dst:
  case PPC::BI__builtin_altivec_dstt:
  case PPC::BI__builtin_altivec_dstst:
  case PPC::BI__builtin_altivec_dststt:
    return SemaRef.BuiltinConstantArgRange(TheCall, 2, 0, 3);
  case PPC::BI__builtin_vsx_xxpermdi:
  case PPC::BI__builtin_vsx_xxsldwi:
    return BuiltinVSX(TheCall);
  case PPC::BI__builtin_unpack_vector_int128:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 1);
  case PPC::BI__builtin_altivec_vgnb:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 2, 7);
  case PPC::BI__builtin_vsx_xxeval:
    return SemaRef.BuiltinConstantArgRange(TheCall, 3, 0, 255);
  case PPC::BI__builtin_altivec_vsldbi:
    return SemaRef.BuiltinConstantArgRange(TheCall, 2, 0, 7);
  case PPC::BI__builtin_altivec_vsrdbi:
    return SemaRef.BuiltinConstantArgRange(TheCall, 2, 0, 7);
  case PPC::BI__builtin_vsx_xxpermx:
    return SemaRef.BuiltinConstantArgRange(TheCall, 3, 0, 7);
  case PPC::BI__builtin_ppc_tw:
  case PPC::BI__builtin_ppc_tdw:
    return SemaRef.BuiltinConstantArgRange(TheCall, 2, 1, 31);
  case PPC::BI__builtin_ppc_cmprb:
    return SemaRef.BuiltinConstantArgRange(TheCall, 0, 0, 1);
  // For __rlwnm, __rlwimi and __rldimi, the last parameter mask must
  // be a constant that represents a contiguous bit field.
  case PPC::BI__builtin_ppc_rlwnm:
    return SemaRef.ValueIsRunOfOnes(TheCall, 2);
  case PPC::BI__builtin_ppc_rlwimi:
    return SemaRef.BuiltinConstantArgRange(TheCall, 2, 0, 31) ||
           SemaRef.ValueIsRunOfOnes(TheCall, 3);
  case PPC::BI__builtin_ppc_rldimi:
    return SemaRef.BuiltinConstantArgRange(TheCall, 2, 0, 63) ||
           SemaRef.ValueIsRunOfOnes(TheCall, 3);
  case PPC::BI__builtin_ppc_addex: {
    if (SemaRef.BuiltinConstantArgRange(TheCall, 2, 0, 3))
      return true;
    // Output warning for reserved values 1 to 3.
    int ArgValue =
        TheCall->getArg(2)->getIntegerConstantExpr(Context)->getSExtValue();
    if (ArgValue != 0)
      Diag(TheCall->getBeginLoc(), diag::warn_argument_undefined_behaviour)
          << ArgValue;
    return false;
  }
  case PPC::BI__builtin_ppc_mtfsb0:
  case PPC::BI__builtin_ppc_mtfsb1:
    return SemaRef.BuiltinConstantArgRange(TheCall, 0, 0, 31);
  case PPC::BI__builtin_ppc_mtfsf:
    return SemaRef.BuiltinConstantArgRange(TheCall, 0, 0, 255);
  case PPC::BI__builtin_ppc_mtfsfi:
    return SemaRef.BuiltinConstantArgRange(TheCall, 0, 0, 7) ||
           SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 15);
  case PPC::BI__builtin_ppc_alignx:
    return SemaRef.BuiltinConstantArgPower2(TheCall, 0);
  case PPC::BI__builtin_ppc_rdlam:
    return SemaRef.ValueIsRunOfOnes(TheCall, 2);
  case PPC::BI__builtin_vsx_ldrmb:
  case PPC::BI__builtin_vsx_strmb:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 1, 16);
  case PPC::BI__builtin_altivec_vcntmbb:
  case PPC::BI__builtin_altivec_vcntmbh:
  case PPC::BI__builtin_altivec_vcntmbw:
  case PPC::BI__builtin_altivec_vcntmbd:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 1);
  case PPC::BI__builtin_vsx_xxgenpcvbm:
  case PPC::BI__builtin_vsx_xxgenpcvhm:
  case PPC::BI__builtin_vsx_xxgenpcvwm:
  case PPC::BI__builtin_vsx_xxgenpcvdm:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 3);
  case PPC::BI__builtin_ppc_test_data_class: {
    // Check if the first argument of the __builtin_ppc_test_data_class call is
    // valid. The argument must be 'float' or 'double' or '__float128'.
    QualType ArgType = TheCall->getArg(0)->getType();
    if (ArgType != QualType(Context.FloatTy) &&
        ArgType != QualType(Context.DoubleTy) &&
        ArgType != QualType(Context.Float128Ty))
      return Diag(TheCall->getBeginLoc(),
                  diag::err_ppc_invalid_test_data_class_type);
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 127);
  }
  case PPC::BI__builtin_ppc_maxfe:
  case PPC::BI__builtin_ppc_minfe:
  case PPC::BI__builtin_ppc_maxfl:
  case PPC::BI__builtin_ppc_minfl:
  case PPC::BI__builtin_ppc_maxfs:
  case PPC::BI__builtin_ppc_minfs: {
    if (Context.getTargetInfo().getTriple().isOSAIX() &&
        (BuiltinID == PPC::BI__builtin_ppc_maxfe ||
         BuiltinID == PPC::BI__builtin_ppc_minfe))
      return Diag(TheCall->getBeginLoc(), diag::err_target_unsupported_type)
             << "builtin" << true << 128 << QualType(Context.LongDoubleTy)
             << false << Context.getTargetInfo().getTriple().str();
    // Argument type should be exact.
    QualType ArgType = QualType(Context.LongDoubleTy);
    if (BuiltinID == PPC::BI__builtin_ppc_maxfl ||
        BuiltinID == PPC::BI__builtin_ppc_minfl)
      ArgType = QualType(Context.DoubleTy);
    else if (BuiltinID == PPC::BI__builtin_ppc_maxfs ||
             BuiltinID == PPC::BI__builtin_ppc_minfs)
      ArgType = QualType(Context.FloatTy);
    for (unsigned I = 0, E = TheCall->getNumArgs(); I < E; ++I)
      if (TheCall->getArg(I)->getType() != ArgType)
        return Diag(TheCall->getBeginLoc(),
                    diag::err_typecheck_convert_incompatible)
               << TheCall->getArg(I)->getType() << ArgType << 1 << 0 << 0;
    return false;
  }
#define CUSTOM_BUILTIN(Name, Intr, Types, Acc, Feature)                        \
  case PPC::BI__builtin_##Name:                                                \
    return BuiltinPPCMMACall(TheCall, BuiltinID, Types);
#include "clang/Basic/BuiltinsPPC.def"
  }
  return SemaRef.BuiltinConstantArgRange(TheCall, i, l, u);
}

// Check if the given type is a non-pointer PPC MMA type. This function is used
// in Sema to prevent invalid uses of restricted PPC MMA types.
bool SemaPPC::CheckPPCMMAType(QualType Type, SourceLocation TypeLoc) {
  ASTContext &Context = getASTContext();
  if (Type->isPointerType() || Type->isArrayType())
    return false;

  QualType CoreType = Type.getCanonicalType().getUnqualifiedType();
#define PPC_VECTOR_TYPE(Name, Id, Size) || CoreType == Context.Id##Ty
  if (false
#include "clang/Basic/PPCTypes.def"
  ) {
    Diag(TypeLoc, diag::err_ppc_invalid_use_mma_type);
    return true;
  }
  return false;
}

/// DecodePPCMMATypeFromStr - This decodes one PPC MMA type descriptor from Str,
/// advancing the pointer over the consumed characters. The decoded type is
/// returned. If the decoded type represents a constant integer with a
/// constraint on its value then Mask is set to that value. The type descriptors
/// used in Str are specific to PPC MMA builtins and are documented in the file
/// defining the PPC builtins.
static QualType DecodePPCMMATypeFromStr(ASTContext &Context, const char *&Str,
                                        unsigned &Mask) {
  bool RequireICE = false;
  ASTContext::GetBuiltinTypeError Error = ASTContext::GE_None;
  switch (*Str++) {
  case 'V':
    return Context.getVectorType(Context.UnsignedCharTy, 16,
                                 VectorKind::AltiVecVector);
  case 'i': {
    char *End;
    unsigned size = strtoul(Str, &End, 10);
    assert(End != Str && "Missing constant parameter constraint");
    Str = End;
    Mask = size;
    return Context.IntTy;
  }
  case 'W': {
    char *End;
    unsigned size = strtoul(Str, &End, 10);
    assert(End != Str && "Missing PowerPC MMA type size");
    Str = End;
    QualType Type;
    switch (size) {
#define PPC_VECTOR_TYPE(typeName, Id, size)                                    \
  case size:                                                                   \
    Type = Context.Id##Ty;                                                     \
    break;
#include "clang/Basic/PPCTypes.def"
    default:
      llvm_unreachable("Invalid PowerPC MMA vector type");
    }
    bool CheckVectorArgs = false;
    while (!CheckVectorArgs) {
      switch (*Str++) {
      case '*':
        Type = Context.getPointerType(Type);
        break;
      case 'C':
        Type = Type.withConst();
        break;
      default:
        CheckVectorArgs = true;
        --Str;
        break;
      }
    }
    return Type;
  }
  default:
    return Context.DecodeTypeStr(--Str, Context, Error, RequireICE, true);
  }
}

bool SemaPPC::BuiltinPPCMMACall(CallExpr *TheCall, unsigned BuiltinID,
                                const char *TypeStr) {

  assert((TypeStr[0] != '\0') &&
         "Invalid types in PPC MMA builtin declaration");

  ASTContext &Context = getASTContext();
  unsigned Mask = 0;
  unsigned ArgNum = 0;

  // The first type in TypeStr is the type of the value returned by the
  // builtin. So we first read that type and change the type of TheCall.
  QualType type = DecodePPCMMATypeFromStr(Context, TypeStr, Mask);
  TheCall->setType(type);

  while (*TypeStr != '\0') {
    Mask = 0;
    QualType ExpectedType = DecodePPCMMATypeFromStr(Context, TypeStr, Mask);
    if (ArgNum >= TheCall->getNumArgs()) {
      ArgNum++;
      break;
    }

    Expr *Arg = TheCall->getArg(ArgNum);
    QualType PassedType = Arg->getType();
    QualType StrippedRVType = PassedType.getCanonicalType();

    // Strip Restrict/Volatile qualifiers.
    if (StrippedRVType.isRestrictQualified() ||
        StrippedRVType.isVolatileQualified())
      StrippedRVType = StrippedRVType.getCanonicalType().getUnqualifiedType();

    // The only case where the argument type and expected type are allowed to
    // mismatch is if the argument type is a non-void pointer (or array) and
    // expected type is a void pointer.
    if (StrippedRVType != ExpectedType)
      if (!(ExpectedType->isVoidPointerType() &&
            (StrippedRVType->isPointerType() || StrippedRVType->isArrayType())))
        return Diag(Arg->getBeginLoc(),
                    diag::err_typecheck_convert_incompatible)
               << PassedType << ExpectedType << 1 << 0 << 0;

    // If the value of the Mask is not 0, we have a constraint in the size of
    // the integer argument so here we ensure the argument is a constant that
    // is in the valid range.
    if (Mask != 0 &&
        SemaRef.BuiltinConstantArgRange(TheCall, ArgNum, 0, Mask, true))
      return true;

    ArgNum++;
  }

  // In case we exited early from the previous loop, there are other types to
  // read from TypeStr. So we need to read them all to ensure we have the right
  // number of arguments in TheCall and if it is not the case, to display a
  // better error message.
  while (*TypeStr != '\0') {
    (void)DecodePPCMMATypeFromStr(Context, TypeStr, Mask);
    ArgNum++;
  }
  if (SemaRef.checkArgCount(TheCall, ArgNum))
    return true;

  return false;
}

bool SemaPPC::BuiltinVSX(CallExpr *TheCall) {
  unsigned ExpectedNumArgs = 3;
  if (SemaRef.checkArgCount(TheCall, ExpectedNumArgs))
    return true;

  // Check the third argument is a compile time constant
  if (!TheCall->getArg(2)->isIntegerConstantExpr(getASTContext()))
    return Diag(TheCall->getBeginLoc(),
                diag::err_vsx_builtin_nonconstant_argument)
           << 3 /* argument index */ << TheCall->getDirectCallee()
           << SourceRange(TheCall->getArg(2)->getBeginLoc(),
                          TheCall->getArg(2)->getEndLoc());

  QualType Arg1Ty = TheCall->getArg(0)->getType();
  QualType Arg2Ty = TheCall->getArg(1)->getType();

  // Check the type of argument 1 and argument 2 are vectors.
  SourceLocation BuiltinLoc = TheCall->getBeginLoc();
  if ((!Arg1Ty->isVectorType() && !Arg1Ty->isDependentType()) ||
      (!Arg2Ty->isVectorType() && !Arg2Ty->isDependentType())) {
    return Diag(BuiltinLoc, diag::err_vec_builtin_non_vector)
           << TheCall->getDirectCallee() << /*isMorethantwoArgs*/ false
           << SourceRange(TheCall->getArg(0)->getBeginLoc(),
                          TheCall->getArg(1)->getEndLoc());
  }

  // Check the first two arguments are the same type.
  if (!getASTContext().hasSameUnqualifiedType(Arg1Ty, Arg2Ty)) {
    return Diag(BuiltinLoc, diag::err_vec_builtin_incompatible_vector)
           << TheCall->getDirectCallee() << /*isMorethantwoArgs*/ false
           << SourceRange(TheCall->getArg(0)->getBeginLoc(),
                          TheCall->getArg(1)->getEndLoc());
  }

  // When default clang type checking is turned off and the customized type
  // checking is used, the returning type of the function must be explicitly
  // set. Otherwise it is _Bool by default.
  TheCall->setType(Arg1Ty);

  return false;
}

} // namespace clang
