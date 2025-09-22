//===--- SemaOpenCL.cpp --- Semantic Analysis for OpenCL constructs -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file implements semantic analysis for OpenCL.
///
//===----------------------------------------------------------------------===//

#include "clang/Sema/SemaOpenCL.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclBase.h"
#include "clang/Basic/DiagnosticSema.h"
#include "clang/Sema/ParsedAttr.h"
#include "clang/Sema/Sema.h"

namespace clang {
SemaOpenCL::SemaOpenCL(Sema &S) : SemaBase(S) {}

void SemaOpenCL::handleNoSVMAttr(Decl *D, const ParsedAttr &AL) {
  if (getLangOpts().getOpenCLCompatibleVersion() < 200)
    Diag(AL.getLoc(), diag::err_attribute_requires_opencl_version)
        << AL << "2.0" << 1;
  else
    Diag(AL.getLoc(), diag::warn_opencl_attr_deprecated_ignored)
        << AL << getLangOpts().getOpenCLVersionString();
}

void SemaOpenCL::handleAccessAttr(Decl *D, const ParsedAttr &AL) {
  if (D->isInvalidDecl())
    return;

  // Check if there is only one access qualifier.
  if (D->hasAttr<OpenCLAccessAttr>()) {
    if (D->getAttr<OpenCLAccessAttr>()->getSemanticSpelling() ==
        AL.getSemanticSpelling()) {
      Diag(AL.getLoc(), diag::warn_duplicate_declspec)
          << AL.getAttrName()->getName() << AL.getRange();
    } else {
      Diag(AL.getLoc(), diag::err_opencl_multiple_access_qualifiers)
          << D->getSourceRange();
      D->setInvalidDecl(true);
      return;
    }
  }

  // OpenCL v2.0 s6.6 - read_write can be used for image types to specify that
  // an image object can be read and written. OpenCL v2.0 s6.13.6 - A kernel
  // cannot read from and write to the same pipe object. Using the read_write
  // (or __read_write) qualifier with the pipe qualifier is a compilation error.
  // OpenCL v3.0 s6.8 - For OpenCL C 2.0, or with the
  // __opencl_c_read_write_images feature, image objects specified as arguments
  // to a kernel can additionally be declared to be read-write.
  // C++ for OpenCL 1.0 inherits rule from OpenCL C v2.0.
  // C++ for OpenCL 2021 inherits rule from OpenCL C v3.0.
  if (const auto *PDecl = dyn_cast<ParmVarDecl>(D)) {
    const Type *DeclTy = PDecl->getType().getCanonicalType().getTypePtr();
    if (AL.getAttrName()->getName().contains("read_write")) {
      bool ReadWriteImagesUnsupported =
          (getLangOpts().getOpenCLCompatibleVersion() < 200) ||
          (getLangOpts().getOpenCLCompatibleVersion() == 300 &&
           !SemaRef.getOpenCLOptions().isSupported(
               "__opencl_c_read_write_images", getLangOpts()));
      if (ReadWriteImagesUnsupported || DeclTy->isPipeType()) {
        Diag(AL.getLoc(), diag::err_opencl_invalid_read_write)
            << AL << PDecl->getType() << DeclTy->isImageType();
        D->setInvalidDecl(true);
        return;
      }
    }
  }

  D->addAttr(::new (getASTContext()) OpenCLAccessAttr(getASTContext(), AL));
}

void SemaOpenCL::handleSubGroupSize(Decl *D, const ParsedAttr &AL) {
  uint32_t SGSize;
  const Expr *E = AL.getArgAsExpr(0);
  if (!SemaRef.checkUInt32Argument(AL, E, SGSize))
    return;
  if (SGSize == 0) {
    Diag(AL.getLoc(), diag::err_attribute_argument_is_zero)
        << AL << E->getSourceRange();
    return;
  }

  OpenCLIntelReqdSubGroupSizeAttr *Existing =
      D->getAttr<OpenCLIntelReqdSubGroupSizeAttr>();
  if (Existing && Existing->getSubGroupSize() != SGSize)
    Diag(AL.getLoc(), diag::warn_duplicate_attribute) << AL;

  D->addAttr(::new (getASTContext())
                 OpenCLIntelReqdSubGroupSizeAttr(getASTContext(), AL, SGSize));
}

static inline bool isBlockPointer(Expr *Arg) {
  return Arg->getType()->isBlockPointerType();
}

/// OpenCL C v2.0, s6.13.17.2 - Checks that the block parameters are all local
/// void*, which is a requirement of device side enqueue.
static bool checkBlockArgs(Sema &S, Expr *BlockArg) {
  const BlockPointerType *BPT =
      cast<BlockPointerType>(BlockArg->getType().getCanonicalType());
  ArrayRef<QualType> Params =
      BPT->getPointeeType()->castAs<FunctionProtoType>()->getParamTypes();
  unsigned ArgCounter = 0;
  bool IllegalParams = false;
  // Iterate through the block parameters until either one is found that is not
  // a local void*, or the block is valid.
  for (ArrayRef<QualType>::iterator I = Params.begin(), E = Params.end();
       I != E; ++I, ++ArgCounter) {
    if (!(*I)->isPointerType() || !(*I)->getPointeeType()->isVoidType() ||
        (*I)->getPointeeType().getQualifiers().getAddressSpace() !=
            LangAS::opencl_local) {
      // Get the location of the error. If a block literal has been passed
      // (BlockExpr) then we can point straight to the offending argument,
      // else we just point to the variable reference.
      SourceLocation ErrorLoc;
      if (isa<BlockExpr>(BlockArg)) {
        BlockDecl *BD = cast<BlockExpr>(BlockArg)->getBlockDecl();
        ErrorLoc = BD->getParamDecl(ArgCounter)->getBeginLoc();
      } else if (isa<DeclRefExpr>(BlockArg)) {
        ErrorLoc = cast<DeclRefExpr>(BlockArg)->getBeginLoc();
      }
      S.Diag(ErrorLoc,
             diag::err_opencl_enqueue_kernel_blocks_non_local_void_args);
      IllegalParams = true;
    }
  }

  return IllegalParams;
}

bool SemaOpenCL::checkSubgroupExt(CallExpr *Call) {
  // OpenCL device can support extension but not the feature as extension
  // requires subgroup independent forward progress, but subgroup independent
  // forward progress is optional in OpenCL C 3.0 __opencl_c_subgroups feature.
  if (!SemaRef.getOpenCLOptions().isSupported("cl_khr_subgroups",
                                              getLangOpts()) &&
      !SemaRef.getOpenCLOptions().isSupported("__opencl_c_subgroups",
                                              getLangOpts())) {
    Diag(Call->getBeginLoc(), diag::err_opencl_requires_extension)
        << 1 << Call->getDirectCallee()
        << "cl_khr_subgroups or __opencl_c_subgroups";
    return true;
  }
  return false;
}

bool SemaOpenCL::checkBuiltinNDRangeAndBlock(CallExpr *TheCall) {
  if (SemaRef.checkArgCount(TheCall, 2))
    return true;

  if (checkSubgroupExt(TheCall))
    return true;

  // First argument is an ndrange_t type.
  Expr *NDRangeArg = TheCall->getArg(0);
  if (NDRangeArg->getType().getUnqualifiedType().getAsString() != "ndrange_t") {
    Diag(NDRangeArg->getBeginLoc(), diag::err_opencl_builtin_expected_type)
        << TheCall->getDirectCallee() << "'ndrange_t'";
    return true;
  }

  Expr *BlockArg = TheCall->getArg(1);
  if (!isBlockPointer(BlockArg)) {
    Diag(BlockArg->getBeginLoc(), diag::err_opencl_builtin_expected_type)
        << TheCall->getDirectCallee() << "block";
    return true;
  }
  return checkBlockArgs(SemaRef, BlockArg);
}

bool SemaOpenCL::checkBuiltinKernelWorkGroupSize(CallExpr *TheCall) {
  if (SemaRef.checkArgCount(TheCall, 1))
    return true;

  Expr *BlockArg = TheCall->getArg(0);
  if (!isBlockPointer(BlockArg)) {
    Diag(BlockArg->getBeginLoc(), diag::err_opencl_builtin_expected_type)
        << TheCall->getDirectCallee() << "block";
    return true;
  }
  return checkBlockArgs(SemaRef, BlockArg);
}

/// Diagnose integer type and any valid implicit conversion to it.
static bool checkOpenCLEnqueueIntType(Sema &S, Expr *E, const QualType &IntT) {
  // Taking into account implicit conversions,
  // allow any integer.
  if (!E->getType()->isIntegerType()) {
    S.Diag(E->getBeginLoc(),
           diag::err_opencl_enqueue_kernel_invalid_local_size_type);
    return true;
  }
  // Potentially emit standard warnings for implicit conversions if enabled
  // using -Wconversion.
  S.CheckImplicitConversion(E, IntT, E->getBeginLoc());
  return false;
}

static bool checkOpenCLEnqueueLocalSizeArgs(Sema &S, CallExpr *TheCall,
                                            unsigned Start, unsigned End) {
  bool IllegalParams = false;
  for (unsigned I = Start; I <= End; ++I)
    IllegalParams |= checkOpenCLEnqueueIntType(S, TheCall->getArg(I),
                                               S.Context.getSizeType());
  return IllegalParams;
}

/// OpenCL v2.0, s6.13.17.1 - Check that sizes are provided for all
/// 'local void*' parameter of passed block.
static bool checkOpenCLEnqueueVariadicArgs(Sema &S, CallExpr *TheCall,
                                           Expr *BlockArg,
                                           unsigned NumNonVarArgs) {
  const BlockPointerType *BPT =
      cast<BlockPointerType>(BlockArg->getType().getCanonicalType());
  unsigned NumBlockParams =
      BPT->getPointeeType()->castAs<FunctionProtoType>()->getNumParams();
  unsigned TotalNumArgs = TheCall->getNumArgs();

  // For each argument passed to the block, a corresponding uint needs to
  // be passed to describe the size of the local memory.
  if (TotalNumArgs != NumBlockParams + NumNonVarArgs) {
    S.Diag(TheCall->getBeginLoc(),
           diag::err_opencl_enqueue_kernel_local_size_args);
    return true;
  }

  // Check that the sizes of the local memory are specified by integers.
  return checkOpenCLEnqueueLocalSizeArgs(S, TheCall, NumNonVarArgs,
                                         TotalNumArgs - 1);
}

bool SemaOpenCL::checkBuiltinEnqueueKernel(CallExpr *TheCall) {
  ASTContext &Context = getASTContext();
  unsigned NumArgs = TheCall->getNumArgs();

  if (NumArgs < 4) {
    Diag(TheCall->getBeginLoc(), diag::err_typecheck_call_too_few_args_at_least)
        << 0 << 4 << NumArgs << /*is non object*/ 0;
    return true;
  }

  Expr *Arg0 = TheCall->getArg(0);
  Expr *Arg1 = TheCall->getArg(1);
  Expr *Arg2 = TheCall->getArg(2);
  Expr *Arg3 = TheCall->getArg(3);

  // First argument always needs to be a queue_t type.
  if (!Arg0->getType()->isQueueT()) {
    Diag(TheCall->getArg(0)->getBeginLoc(),
         diag::err_opencl_builtin_expected_type)
        << TheCall->getDirectCallee() << getASTContext().OCLQueueTy;
    return true;
  }

  // Second argument always needs to be a kernel_enqueue_flags_t enum value.
  if (!Arg1->getType()->isIntegerType()) {
    Diag(TheCall->getArg(1)->getBeginLoc(),
         diag::err_opencl_builtin_expected_type)
        << TheCall->getDirectCallee() << "'kernel_enqueue_flags_t' (i.e. uint)";
    return true;
  }

  // Third argument is always an ndrange_t type.
  if (Arg2->getType().getUnqualifiedType().getAsString() != "ndrange_t") {
    Diag(TheCall->getArg(2)->getBeginLoc(),
         diag::err_opencl_builtin_expected_type)
        << TheCall->getDirectCallee() << "'ndrange_t'";
    return true;
  }

  // With four arguments, there is only one form that the function could be
  // called in: no events and no variable arguments.
  if (NumArgs == 4) {
    // check that the last argument is the right block type.
    if (!isBlockPointer(Arg3)) {
      Diag(Arg3->getBeginLoc(), diag::err_opencl_builtin_expected_type)
          << TheCall->getDirectCallee() << "block";
      return true;
    }
    // we have a block type, check the prototype
    const BlockPointerType *BPT =
        cast<BlockPointerType>(Arg3->getType().getCanonicalType());
    if (BPT->getPointeeType()->castAs<FunctionProtoType>()->getNumParams() >
        0) {
      Diag(Arg3->getBeginLoc(), diag::err_opencl_enqueue_kernel_blocks_no_args);
      return true;
    }
    return false;
  }
  // we can have block + varargs.
  if (isBlockPointer(Arg3))
    return (checkBlockArgs(SemaRef, Arg3) ||
            checkOpenCLEnqueueVariadicArgs(SemaRef, TheCall, Arg3, 4));
  // last two cases with either exactly 7 args or 7 args and varargs.
  if (NumArgs >= 7) {
    // check common block argument.
    Expr *Arg6 = TheCall->getArg(6);
    if (!isBlockPointer(Arg6)) {
      Diag(Arg6->getBeginLoc(), diag::err_opencl_builtin_expected_type)
          << TheCall->getDirectCallee() << "block";
      return true;
    }
    if (checkBlockArgs(SemaRef, Arg6))
      return true;

    // Forth argument has to be any integer type.
    if (!Arg3->getType()->isIntegerType()) {
      Diag(TheCall->getArg(3)->getBeginLoc(),
           diag::err_opencl_builtin_expected_type)
          << TheCall->getDirectCallee() << "integer";
      return true;
    }
    // check remaining common arguments.
    Expr *Arg4 = TheCall->getArg(4);
    Expr *Arg5 = TheCall->getArg(5);

    // Fifth argument is always passed as a pointer to clk_event_t.
    if (!Arg4->isNullPointerConstant(Context,
                                     Expr::NPC_ValueDependentIsNotNull) &&
        !Arg4->getType()->getPointeeOrArrayElementType()->isClkEventT()) {
      Diag(TheCall->getArg(4)->getBeginLoc(),
           diag::err_opencl_builtin_expected_type)
          << TheCall->getDirectCallee()
          << Context.getPointerType(Context.OCLClkEventTy);
      return true;
    }

    // Sixth argument is always passed as a pointer to clk_event_t.
    if (!Arg5->isNullPointerConstant(Context,
                                     Expr::NPC_ValueDependentIsNotNull) &&
        !(Arg5->getType()->isPointerType() &&
          Arg5->getType()->getPointeeType()->isClkEventT())) {
      Diag(TheCall->getArg(5)->getBeginLoc(),
           diag::err_opencl_builtin_expected_type)
          << TheCall->getDirectCallee()
          << Context.getPointerType(Context.OCLClkEventTy);
      return true;
    }

    if (NumArgs == 7)
      return false;

    return checkOpenCLEnqueueVariadicArgs(SemaRef, TheCall, Arg6, 7);
  }

  // None of the specific case has been detected, give generic error
  Diag(TheCall->getBeginLoc(), diag::err_opencl_enqueue_kernel_incorrect_args);
  return true;
}

/// Returns OpenCL access qual.
static OpenCLAccessAttr *getOpenCLArgAccess(const Decl *D) {
  return D->getAttr<OpenCLAccessAttr>();
}

/// Returns true if pipe element type is different from the pointer.
static bool checkPipeArg(Sema &S, CallExpr *Call) {
  const Expr *Arg0 = Call->getArg(0);
  // First argument type should always be pipe.
  if (!Arg0->getType()->isPipeType()) {
    S.Diag(Call->getBeginLoc(), diag::err_opencl_builtin_pipe_first_arg)
        << Call->getDirectCallee() << Arg0->getSourceRange();
    return true;
  }
  OpenCLAccessAttr *AccessQual =
      getOpenCLArgAccess(cast<DeclRefExpr>(Arg0)->getDecl());
  // Validates the access qualifier is compatible with the call.
  // OpenCL v2.0 s6.13.16 - The access qualifiers for pipe should only be
  // read_only and write_only, and assumed to be read_only if no qualifier is
  // specified.
  switch (Call->getDirectCallee()->getBuiltinID()) {
  case Builtin::BIread_pipe:
  case Builtin::BIreserve_read_pipe:
  case Builtin::BIcommit_read_pipe:
  case Builtin::BIwork_group_reserve_read_pipe:
  case Builtin::BIsub_group_reserve_read_pipe:
  case Builtin::BIwork_group_commit_read_pipe:
  case Builtin::BIsub_group_commit_read_pipe:
    if (!(!AccessQual || AccessQual->isReadOnly())) {
      S.Diag(Arg0->getBeginLoc(),
             diag::err_opencl_builtin_pipe_invalid_access_modifier)
          << "read_only" << Arg0->getSourceRange();
      return true;
    }
    break;
  case Builtin::BIwrite_pipe:
  case Builtin::BIreserve_write_pipe:
  case Builtin::BIcommit_write_pipe:
  case Builtin::BIwork_group_reserve_write_pipe:
  case Builtin::BIsub_group_reserve_write_pipe:
  case Builtin::BIwork_group_commit_write_pipe:
  case Builtin::BIsub_group_commit_write_pipe:
    if (!(AccessQual && AccessQual->isWriteOnly())) {
      S.Diag(Arg0->getBeginLoc(),
             diag::err_opencl_builtin_pipe_invalid_access_modifier)
          << "write_only" << Arg0->getSourceRange();
      return true;
    }
    break;
  default:
    break;
  }
  return false;
}

/// Returns true if pipe element type is different from the pointer.
static bool checkPipePacketType(Sema &S, CallExpr *Call, unsigned Idx) {
  const Expr *Arg0 = Call->getArg(0);
  const Expr *ArgIdx = Call->getArg(Idx);
  const PipeType *PipeTy = cast<PipeType>(Arg0->getType());
  const QualType EltTy = PipeTy->getElementType();
  const PointerType *ArgTy = ArgIdx->getType()->getAs<PointerType>();
  // The Idx argument should be a pointer and the type of the pointer and
  // the type of pipe element should also be the same.
  if (!ArgTy ||
      !S.Context.hasSameType(
          EltTy, ArgTy->getPointeeType()->getCanonicalTypeInternal())) {
    S.Diag(Call->getBeginLoc(), diag::err_opencl_builtin_pipe_invalid_arg)
        << Call->getDirectCallee() << S.Context.getPointerType(EltTy)
        << ArgIdx->getType() << ArgIdx->getSourceRange();
    return true;
  }
  return false;
}

bool SemaOpenCL::checkBuiltinRWPipe(CallExpr *Call) {
  // OpenCL v2.0 s6.13.16.2 - The built-in read/write
  // functions have two forms.
  switch (Call->getNumArgs()) {
  case 2:
    if (checkPipeArg(SemaRef, Call))
      return true;
    // The call with 2 arguments should be
    // read/write_pipe(pipe T, T*).
    // Check packet type T.
    if (checkPipePacketType(SemaRef, Call, 1))
      return true;
    break;

  case 4: {
    if (checkPipeArg(SemaRef, Call))
      return true;
    // The call with 4 arguments should be
    // read/write_pipe(pipe T, reserve_id_t, uint, T*).
    // Check reserve_id_t.
    if (!Call->getArg(1)->getType()->isReserveIDT()) {
      Diag(Call->getBeginLoc(), diag::err_opencl_builtin_pipe_invalid_arg)
          << Call->getDirectCallee() << getASTContext().OCLReserveIDTy
          << Call->getArg(1)->getType() << Call->getArg(1)->getSourceRange();
      return true;
    }

    // Check the index.
    const Expr *Arg2 = Call->getArg(2);
    if (!Arg2->getType()->isIntegerType() &&
        !Arg2->getType()->isUnsignedIntegerType()) {
      Diag(Call->getBeginLoc(), diag::err_opencl_builtin_pipe_invalid_arg)
          << Call->getDirectCallee() << getASTContext().UnsignedIntTy
          << Arg2->getType() << Arg2->getSourceRange();
      return true;
    }

    // Check packet type T.
    if (checkPipePacketType(SemaRef, Call, 3))
      return true;
  } break;
  default:
    Diag(Call->getBeginLoc(), diag::err_opencl_builtin_pipe_arg_num)
        << Call->getDirectCallee() << Call->getSourceRange();
    return true;
  }

  return false;
}

bool SemaOpenCL::checkBuiltinReserveRWPipe(CallExpr *Call) {
  if (SemaRef.checkArgCount(Call, 2))
    return true;

  if (checkPipeArg(SemaRef, Call))
    return true;

  // Check the reserve size.
  if (!Call->getArg(1)->getType()->isIntegerType() &&
      !Call->getArg(1)->getType()->isUnsignedIntegerType()) {
    Diag(Call->getBeginLoc(), diag::err_opencl_builtin_pipe_invalid_arg)
        << Call->getDirectCallee() << getASTContext().UnsignedIntTy
        << Call->getArg(1)->getType() << Call->getArg(1)->getSourceRange();
    return true;
  }

  // Since return type of reserve_read/write_pipe built-in function is
  // reserve_id_t, which is not defined in the builtin def file , we used int
  // as return type and need to override the return type of these functions.
  Call->setType(getASTContext().OCLReserveIDTy);

  return false;
}

bool SemaOpenCL::checkBuiltinCommitRWPipe(CallExpr *Call) {
  if (SemaRef.checkArgCount(Call, 2))
    return true;

  if (checkPipeArg(SemaRef, Call))
    return true;

  // Check reserve_id_t.
  if (!Call->getArg(1)->getType()->isReserveIDT()) {
    Diag(Call->getBeginLoc(), diag::err_opencl_builtin_pipe_invalid_arg)
        << Call->getDirectCallee() << getASTContext().OCLReserveIDTy
        << Call->getArg(1)->getType() << Call->getArg(1)->getSourceRange();
    return true;
  }

  return false;
}

bool SemaOpenCL::checkBuiltinPipePackets(CallExpr *Call) {
  if (SemaRef.checkArgCount(Call, 1))
    return true;

  if (!Call->getArg(0)->getType()->isPipeType()) {
    Diag(Call->getBeginLoc(), diag::err_opencl_builtin_pipe_first_arg)
        << Call->getDirectCallee() << Call->getArg(0)->getSourceRange();
    return true;
  }

  return false;
}

bool SemaOpenCL::checkBuiltinToAddr(unsigned BuiltinID, CallExpr *Call) {
  if (SemaRef.checkArgCount(Call, 1))
    return true;

  auto RT = Call->getArg(0)->getType();
  if (!RT->isPointerType() ||
      RT->getPointeeType().getAddressSpace() == LangAS::opencl_constant) {
    Diag(Call->getBeginLoc(), diag::err_opencl_builtin_to_addr_invalid_arg)
        << Call->getArg(0) << Call->getDirectCallee() << Call->getSourceRange();
    return true;
  }

  if (RT->getPointeeType().getAddressSpace() != LangAS::opencl_generic) {
    Diag(Call->getArg(0)->getBeginLoc(),
         diag::warn_opencl_generic_address_space_arg)
        << Call->getDirectCallee()->getNameInfo().getAsString()
        << Call->getArg(0)->getSourceRange();
  }

  RT = RT->getPointeeType();
  auto Qual = RT.getQualifiers();
  switch (BuiltinID) {
  case Builtin::BIto_global:
    Qual.setAddressSpace(LangAS::opencl_global);
    break;
  case Builtin::BIto_local:
    Qual.setAddressSpace(LangAS::opencl_local);
    break;
  case Builtin::BIto_private:
    Qual.setAddressSpace(LangAS::opencl_private);
    break;
  default:
    llvm_unreachable("Invalid builtin function");
  }
  Call->setType(getASTContext().getPointerType(
      getASTContext().getQualifiedType(RT.getUnqualifiedType(), Qual)));

  return false;
}

} // namespace clang
