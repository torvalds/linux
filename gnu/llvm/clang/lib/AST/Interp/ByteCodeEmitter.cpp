//===--- ByteCodeEmitter.cpp - Instruction emitter for the VM ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ByteCodeEmitter.h"
#include "Context.h"
#include "Floating.h"
#include "IntegralAP.h"
#include "Opcode.h"
#include "Program.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclCXX.h"
#include "clang/Basic/Builtins.h"
#include <type_traits>

using namespace clang;
using namespace clang::interp;

/// Unevaluated builtins don't get their arguments put on the stack
/// automatically. They instead operate on the AST of their Call
/// Expression.
/// Similar information is available via ASTContext::BuiltinInfo,
/// but that is not correct for our use cases.
static bool isUnevaluatedBuiltin(unsigned BuiltinID) {
  return BuiltinID == Builtin::BI__builtin_classify_type ||
         BuiltinID == Builtin::BI__builtin_os_log_format_buffer_size;
}

Function *ByteCodeEmitter::compileFunc(const FunctionDecl *FuncDecl) {

  // Manually created functions that haven't been assigned proper
  // parameters yet.
  if (!FuncDecl->param_empty() && !FuncDecl->param_begin())
    return nullptr;

  bool IsLambdaStaticInvoker = false;
  if (const auto *MD = dyn_cast<CXXMethodDecl>(FuncDecl);
      MD && MD->isLambdaStaticInvoker()) {
    // For a lambda static invoker, we might have to pick a specialized
    // version if the lambda is generic. In that case, the picked function
    // will *NOT* be a static invoker anymore. However, it will still
    // be a non-static member function, this (usually) requiring an
    // instance pointer. We suppress that later in this function.
    IsLambdaStaticInvoker = true;

    const CXXRecordDecl *ClosureClass = MD->getParent();
    assert(ClosureClass->captures_begin() == ClosureClass->captures_end());
    if (ClosureClass->isGenericLambda()) {
      const CXXMethodDecl *LambdaCallOp = ClosureClass->getLambdaCallOperator();
      assert(MD->isFunctionTemplateSpecialization() &&
             "A generic lambda's static-invoker function must be a "
             "template specialization");
      const TemplateArgumentList *TAL = MD->getTemplateSpecializationArgs();
      FunctionTemplateDecl *CallOpTemplate =
          LambdaCallOp->getDescribedFunctionTemplate();
      void *InsertPos = nullptr;
      const FunctionDecl *CorrespondingCallOpSpecialization =
          CallOpTemplate->findSpecialization(TAL->asArray(), InsertPos);
      assert(CorrespondingCallOpSpecialization);
      FuncDecl = cast<CXXMethodDecl>(CorrespondingCallOpSpecialization);
    }
  }

  // Set up argument indices.
  unsigned ParamOffset = 0;
  SmallVector<PrimType, 8> ParamTypes;
  SmallVector<unsigned, 8> ParamOffsets;
  llvm::DenseMap<unsigned, Function::ParamDescriptor> ParamDescriptors;

  // If the return is not a primitive, a pointer to the storage where the
  // value is initialized in is passed as the first argument. See 'RVO'
  // elsewhere in the code.
  QualType Ty = FuncDecl->getReturnType();
  bool HasRVO = false;
  if (!Ty->isVoidType() && !Ctx.classify(Ty)) {
    HasRVO = true;
    ParamTypes.push_back(PT_Ptr);
    ParamOffsets.push_back(ParamOffset);
    ParamOffset += align(primSize(PT_Ptr));
  }

  // If the function decl is a member decl, the next parameter is
  // the 'this' pointer. This parameter is pop()ed from the
  // InterpStack when calling the function.
  bool HasThisPointer = false;
  if (const auto *MD = dyn_cast<CXXMethodDecl>(FuncDecl)) {
    if (!IsLambdaStaticInvoker) {
      HasThisPointer = MD->isInstance();
      if (MD->isImplicitObjectMemberFunction()) {
        ParamTypes.push_back(PT_Ptr);
        ParamOffsets.push_back(ParamOffset);
        ParamOffset += align(primSize(PT_Ptr));
      }
    }

    // Set up lambda capture to closure record field mapping.
    if (isLambdaCallOperator(MD)) {
      // The parent record needs to be complete, we need to know about all
      // the lambda captures.
      if (!MD->getParent()->isCompleteDefinition())
        return nullptr;

      const Record *R = P.getOrCreateRecord(MD->getParent());
      llvm::DenseMap<const ValueDecl *, FieldDecl *> LC;
      FieldDecl *LTC;

      MD->getParent()->getCaptureFields(LC, LTC);

      for (auto Cap : LC) {
        // Static lambdas cannot have any captures. If this one does,
        // it has already been diagnosed and we can only ignore it.
        if (MD->isStatic())
          return nullptr;

        unsigned Offset = R->getField(Cap.second)->Offset;
        this->LambdaCaptures[Cap.first] = {
            Offset, Cap.second->getType()->isReferenceType()};
      }
      if (LTC) {
        QualType CaptureType = R->getField(LTC)->Decl->getType();
        this->LambdaThisCapture = {R->getField(LTC)->Offset,
                                   CaptureType->isReferenceType() ||
                                       CaptureType->isPointerType()};
      }
    }
  }

  // Assign descriptors to all parameters.
  // Composite objects are lowered to pointers.
  for (const ParmVarDecl *PD : FuncDecl->parameters()) {
    std::optional<PrimType> T = Ctx.classify(PD->getType());
    PrimType PT = T.value_or(PT_Ptr);
    Descriptor *Desc = P.createDescriptor(PD, PT);
    ParamDescriptors.insert({ParamOffset, {PT, Desc}});
    Params.insert({PD, {ParamOffset, T != std::nullopt}});
    ParamOffsets.push_back(ParamOffset);
    ParamOffset += align(primSize(PT));
    ParamTypes.push_back(PT);
  }

  // Create a handle over the emitted code.
  Function *Func = P.getFunction(FuncDecl);
  if (!Func) {
    bool IsUnevaluatedBuiltin = false;
    if (unsigned BI = FuncDecl->getBuiltinID())
      IsUnevaluatedBuiltin = isUnevaluatedBuiltin(BI);

    Func =
        P.createFunction(FuncDecl, ParamOffset, std::move(ParamTypes),
                         std::move(ParamDescriptors), std::move(ParamOffsets),
                         HasThisPointer, HasRVO, IsUnevaluatedBuiltin);
  }

  assert(Func);
  // For not-yet-defined functions, we only create a Function instance and
  // compile their body later.
  if (!FuncDecl->isDefined() ||
      (FuncDecl->willHaveBody() && !FuncDecl->hasBody())) {
    Func->setDefined(false);
    return Func;
  }

  Func->setDefined(true);

  // Lambda static invokers are a special case that we emit custom code for.
  bool IsEligibleForCompilation = false;
  if (const auto *MD = dyn_cast<CXXMethodDecl>(FuncDecl))
    IsEligibleForCompilation = MD->isLambdaStaticInvoker();
  if (!IsEligibleForCompilation)
    IsEligibleForCompilation =
        FuncDecl->isConstexpr() || FuncDecl->hasAttr<MSConstexprAttr>();

  // Compile the function body.
  if (!IsEligibleForCompilation || !visitFunc(FuncDecl)) {
    Func->setIsFullyCompiled(true);
    return Func;
  }

  // Create scopes from descriptors.
  llvm::SmallVector<Scope, 2> Scopes;
  for (auto &DS : Descriptors) {
    Scopes.emplace_back(std::move(DS));
  }

  // Set the function's code.
  Func->setCode(NextLocalOffset, std::move(Code), std::move(SrcMap),
                std::move(Scopes), FuncDecl->hasBody());
  Func->setIsFullyCompiled(true);
  return Func;
}

Scope::Local ByteCodeEmitter::createLocal(Descriptor *D) {
  NextLocalOffset += sizeof(Block);
  unsigned Location = NextLocalOffset;
  NextLocalOffset += align(D->getAllocSize());
  return {Location, D};
}

void ByteCodeEmitter::emitLabel(LabelTy Label) {
  const size_t Target = Code.size();
  LabelOffsets.insert({Label, Target});

  if (auto It = LabelRelocs.find(Label);
      It != LabelRelocs.end()) {
    for (unsigned Reloc : It->second) {
      using namespace llvm::support;

      // Rewrite the operand of all jumps to this label.
      void *Location = Code.data() + Reloc - align(sizeof(int32_t));
      assert(aligned(Location));
      const int32_t Offset = Target - static_cast<int64_t>(Reloc);
      endian::write<int32_t, llvm::endianness::native>(Location, Offset);
    }
    LabelRelocs.erase(It);
  }
}

int32_t ByteCodeEmitter::getOffset(LabelTy Label) {
  // Compute the PC offset which the jump is relative to.
  const int64_t Position =
      Code.size() + align(sizeof(Opcode)) + align(sizeof(int32_t));
  assert(aligned(Position));

  // If target is known, compute jump offset.
  if (auto It = LabelOffsets.find(Label);
      It != LabelOffsets.end())
    return It->second - Position;

  // Otherwise, record relocation and return dummy offset.
  LabelRelocs[Label].push_back(Position);
  return 0ull;
}

/// Helper to write bytecode and bail out if 32-bit offsets become invalid.
/// Pointers will be automatically marshalled as 32-bit IDs.
template <typename T>
static void emit(Program &P, std::vector<std::byte> &Code, const T &Val,
                 bool &Success) {
  size_t Size;

  if constexpr (std::is_pointer_v<T>)
    Size = sizeof(uint32_t);
  else
    Size = sizeof(T);

  if (Code.size() + Size > std::numeric_limits<unsigned>::max()) {
    Success = false;
    return;
  }

  // Access must be aligned!
  size_t ValPos = align(Code.size());
  Size = align(Size);
  assert(aligned(ValPos + Size));
  Code.resize(ValPos + Size);

  if constexpr (!std::is_pointer_v<T>) {
    new (Code.data() + ValPos) T(Val);
  } else {
    uint32_t ID = P.getOrCreateNativePointer(Val);
    new (Code.data() + ValPos) uint32_t(ID);
  }
}

/// Emits a serializable value. These usually (potentially) contain
/// heap-allocated memory and aren't trivially copyable.
template <typename T>
static void emitSerialized(std::vector<std::byte> &Code, const T &Val,
                           bool &Success) {
  size_t Size = Val.bytesToSerialize();

  if (Code.size() + Size > std::numeric_limits<unsigned>::max()) {
    Success = false;
    return;
  }

  // Access must be aligned!
  size_t ValPos = align(Code.size());
  Size = align(Size);
  assert(aligned(ValPos + Size));
  Code.resize(ValPos + Size);

  Val.serialize(Code.data() + ValPos);
}

template <>
void emit(Program &P, std::vector<std::byte> &Code, const Floating &Val,
          bool &Success) {
  emitSerialized(Code, Val, Success);
}

template <>
void emit(Program &P, std::vector<std::byte> &Code,
          const IntegralAP<false> &Val, bool &Success) {
  emitSerialized(Code, Val, Success);
}

template <>
void emit(Program &P, std::vector<std::byte> &Code, const IntegralAP<true> &Val,
          bool &Success) {
  emitSerialized(Code, Val, Success);
}

template <typename... Tys>
bool ByteCodeEmitter::emitOp(Opcode Op, const Tys &... Args, const SourceInfo &SI) {
  bool Success = true;

  // The opcode is followed by arguments. The source info is
  // attached to the address after the opcode.
  emit(P, Code, Op, Success);
  if (SI)
    SrcMap.emplace_back(Code.size(), SI);

  (..., emit(P, Code, Args, Success));
  return Success;
}

bool ByteCodeEmitter::jumpTrue(const LabelTy &Label) {
  return emitJt(getOffset(Label), SourceInfo{});
}

bool ByteCodeEmitter::jumpFalse(const LabelTy &Label) {
  return emitJf(getOffset(Label), SourceInfo{});
}

bool ByteCodeEmitter::jump(const LabelTy &Label) {
  return emitJmp(getOffset(Label), SourceInfo{});
}

bool ByteCodeEmitter::fallthrough(const LabelTy &Label) {
  emitLabel(Label);
  return true;
}

//===----------------------------------------------------------------------===//
// Opcode emitters
//===----------------------------------------------------------------------===//

#define GET_LINK_IMPL
#include "Opcodes.inc"
#undef GET_LINK_IMPL
