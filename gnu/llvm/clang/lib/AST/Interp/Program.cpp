//===--- Program.cpp - Bytecode for the constexpr VM ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Program.h"
#include "Context.h"
#include "Function.h"
#include "Integral.h"
#include "Opcode.h"
#include "PrimType.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"

using namespace clang;
using namespace clang::interp;

unsigned Program::getOrCreateNativePointer(const void *Ptr) {
  auto It = NativePointerIndices.find(Ptr);
  if (It != NativePointerIndices.end())
    return It->second;

  unsigned Idx = NativePointers.size();
  NativePointers.push_back(Ptr);
  NativePointerIndices[Ptr] = Idx;
  return Idx;
}

const void *Program::getNativePointer(unsigned Idx) {
  return NativePointers[Idx];
}

unsigned Program::createGlobalString(const StringLiteral *S) {
  const size_t CharWidth = S->getCharByteWidth();
  const size_t BitWidth = CharWidth * Ctx.getCharBit();

  PrimType CharType;
  switch (CharWidth) {
  case 1:
    CharType = PT_Sint8;
    break;
  case 2:
    CharType = PT_Uint16;
    break;
  case 4:
    CharType = PT_Uint32;
    break;
  default:
    llvm_unreachable("unsupported character width");
  }

  // Create a descriptor for the string.
  Descriptor *Desc =
      allocateDescriptor(S, CharType, Descriptor::GlobalMD, S->getLength() + 1,
                         /*isConst=*/true,
                         /*isTemporary=*/false,
                         /*isMutable=*/false);

  // Allocate storage for the string.
  // The byte length does not include the null terminator.
  unsigned I = Globals.size();
  unsigned Sz = Desc->getAllocSize();
  auto *G = new (Allocator, Sz) Global(Ctx.getEvalID(), Desc, /*isStatic=*/true,
                                       /*isExtern=*/false);
  G->block()->invokeCtor();

  new (G->block()->rawData()) InlineDescriptor(Desc);
  Globals.push_back(G);

  // Construct the string in storage.
  const Pointer Ptr(G->block());
  for (unsigned I = 0, N = S->getLength(); I <= N; ++I) {
    Pointer Field = Ptr.atIndex(I).narrow();
    const uint32_t CodePoint = I == N ? 0 : S->getCodeUnit(I);
    switch (CharType) {
      case PT_Sint8: {
        using T = PrimConv<PT_Sint8>::T;
        Field.deref<T>() = T::from(CodePoint, BitWidth);
        Field.initialize();
        break;
      }
      case PT_Uint16: {
        using T = PrimConv<PT_Uint16>::T;
        Field.deref<T>() = T::from(CodePoint, BitWidth);
        Field.initialize();
        break;
      }
      case PT_Uint32: {
        using T = PrimConv<PT_Uint32>::T;
        Field.deref<T>() = T::from(CodePoint, BitWidth);
        Field.initialize();
        break;
      }
      default:
        llvm_unreachable("unsupported character type");
    }
  }
  return I;
}

Pointer Program::getPtrGlobal(unsigned Idx) const {
  assert(Idx < Globals.size());
  return Pointer(Globals[Idx]->block());
}

std::optional<unsigned> Program::getGlobal(const ValueDecl *VD) {
  if (auto It = GlobalIndices.find(VD); It != GlobalIndices.end())
    return It->second;

  // Find any previous declarations which were already evaluated.
  std::optional<unsigned> Index;
  for (const Decl *P = VD->getPreviousDecl(); P; P = P->getPreviousDecl()) {
    if (auto It = GlobalIndices.find(P); It != GlobalIndices.end()) {
      Index = It->second;
      break;
    }
  }

  // Map the decl to the existing index.
  if (Index)
    GlobalIndices[VD] = *Index;

  return std::nullopt;
}

std::optional<unsigned> Program::getGlobal(const Expr *E) {
  if (auto It = GlobalIndices.find(E); It != GlobalIndices.end())
    return It->second;
  return std::nullopt;
}

std::optional<unsigned> Program::getOrCreateGlobal(const ValueDecl *VD,
                                                   const Expr *Init) {
  if (auto Idx = getGlobal(VD))
    return Idx;

  if (auto Idx = createGlobal(VD, Init)) {
    GlobalIndices[VD] = *Idx;
    return Idx;
  }
  return std::nullopt;
}

std::optional<unsigned> Program::getOrCreateDummy(const ValueDecl *VD) {
  // Dedup blocks since they are immutable and pointers cannot be compared.
  if (auto It = DummyVariables.find(VD); It != DummyVariables.end())
    return It->second;

  QualType QT = VD->getType();
  if (const auto *RT = QT->getAs<ReferenceType>())
    QT = RT->getPointeeType();

  Descriptor *Desc;
  if (std::optional<PrimType> T = Ctx.classify(QT))
    Desc = createDescriptor(VD, *T, std::nullopt, true, false);
  else
    Desc = createDescriptor(VD, QT.getTypePtr(), std::nullopt, true, false);
  if (!Desc)
    Desc = allocateDescriptor(VD);

  assert(Desc);
  Desc->makeDummy();

  assert(Desc->isDummy());

  // Allocate a block for storage.
  unsigned I = Globals.size();

  auto *G = new (Allocator, Desc->getAllocSize())
      Global(Ctx.getEvalID(), getCurrentDecl(), Desc, /*IsStatic=*/true,
             /*IsExtern=*/false);
  G->block()->invokeCtor();

  Globals.push_back(G);
  DummyVariables[VD] = I;
  return I;
}

std::optional<unsigned> Program::createGlobal(const ValueDecl *VD,
                                              const Expr *Init) {
  bool IsStatic, IsExtern;
  if (const auto *Var = dyn_cast<VarDecl>(VD)) {
    IsStatic = Context::shouldBeGloballyIndexed(VD);
    IsExtern = Var->hasExternalStorage();
  } else if (isa<UnnamedGlobalConstantDecl, MSGuidDecl,
                 TemplateParamObjectDecl>(VD)) {
    IsStatic = true;
    IsExtern = false;
  } else {
    IsStatic = false;
    IsExtern = true;
  }
  if (auto Idx = createGlobal(VD, VD->getType(), IsStatic, IsExtern, Init)) {
    for (const Decl *P = VD; P; P = P->getPreviousDecl())
      GlobalIndices[P] = *Idx;
    return *Idx;
  }
  return std::nullopt;
}

std::optional<unsigned> Program::createGlobal(const Expr *E) {
  if (auto Idx = getGlobal(E))
    return Idx;
  if (auto Idx = createGlobal(E, E->getType(), /*isStatic=*/true,
                              /*isExtern=*/false)) {
    GlobalIndices[E] = *Idx;
    return *Idx;
  }
  return std::nullopt;
}

std::optional<unsigned> Program::createGlobal(const DeclTy &D, QualType Ty,
                                              bool IsStatic, bool IsExtern,
                                              const Expr *Init) {
  // Create a descriptor for the global.
  Descriptor *Desc;
  const bool IsConst = Ty.isConstQualified();
  const bool IsTemporary = D.dyn_cast<const Expr *>();
  if (std::optional<PrimType> T = Ctx.classify(Ty))
    Desc = createDescriptor(D, *T, Descriptor::GlobalMD, IsConst, IsTemporary);
  else
    Desc = createDescriptor(D, Ty.getTypePtr(), Descriptor::GlobalMD, IsConst,
                            IsTemporary);

  if (!Desc)
    return std::nullopt;

  // Allocate a block for storage.
  unsigned I = Globals.size();

  auto *G = new (Allocator, Desc->getAllocSize())
      Global(Ctx.getEvalID(), getCurrentDecl(), Desc, IsStatic, IsExtern);
  G->block()->invokeCtor();

  // Initialize InlineDescriptor fields.
  auto *GD = new (G->block()->rawData()) GlobalInlineDescriptor();
  if (!Init)
    GD->InitState = GlobalInitState::NoInitializer;
  Globals.push_back(G);

  return I;
}

Function *Program::getFunction(const FunctionDecl *F) {
  F = F->getCanonicalDecl();
  assert(F);
  auto It = Funcs.find(F);
  return It == Funcs.end() ? nullptr : It->second.get();
}

Record *Program::getOrCreateRecord(const RecordDecl *RD) {
  // Use the actual definition as a key.
  RD = RD->getDefinition();
  if (!RD)
    return nullptr;

  if (!RD->isCompleteDefinition())
    return nullptr;

  // Deduplicate records.
  if (auto It = Records.find(RD); It != Records.end())
    return It->second;

  // We insert nullptr now and replace that later, so recursive calls
  // to this function with the same RecordDecl don't run into
  // infinite recursion.
  Records.insert({RD, nullptr});

  // Number of bytes required by fields and base classes.
  unsigned BaseSize = 0;
  // Number of bytes required by virtual base.
  unsigned VirtSize = 0;

  // Helper to get a base descriptor.
  auto GetBaseDesc = [this](const RecordDecl *BD,
                            const Record *BR) -> const Descriptor * {
    if (!BR)
      return nullptr;
    return allocateDescriptor(BD, BR, std::nullopt, /*isConst=*/false,
                              /*isTemporary=*/false,
                              /*isMutable=*/false);
  };

  // Reserve space for base classes.
  Record::BaseList Bases;
  Record::VirtualBaseList VirtBases;
  if (const auto *CD = dyn_cast<CXXRecordDecl>(RD)) {
    for (const CXXBaseSpecifier &Spec : CD->bases()) {
      if (Spec.isVirtual())
        continue;

      // In error cases, the base might not be a RecordType.
      const auto *RT = Spec.getType()->getAs<RecordType>();
      if (!RT)
        return nullptr;
      const RecordDecl *BD = RT->getDecl();
      const Record *BR = getOrCreateRecord(BD);

      const Descriptor *Desc = GetBaseDesc(BD, BR);
      if (!Desc)
        return nullptr;

      BaseSize += align(sizeof(InlineDescriptor));
      Bases.push_back({BD, BaseSize, Desc, BR});
      BaseSize += align(BR->getSize());
    }

    for (const CXXBaseSpecifier &Spec : CD->vbases()) {
      const auto *RT = Spec.getType()->getAs<RecordType>();
      if (!RT)
        return nullptr;

      const RecordDecl *BD = RT->getDecl();
      const Record *BR = getOrCreateRecord(BD);

      const Descriptor *Desc = GetBaseDesc(BD, BR);
      if (!Desc)
        return nullptr;

      VirtSize += align(sizeof(InlineDescriptor));
      VirtBases.push_back({BD, VirtSize, Desc, BR});
      VirtSize += align(BR->getSize());
    }
  }

  // Reserve space for fields.
  Record::FieldList Fields;
  for (const FieldDecl *FD : RD->fields()) {
    // Note that we DO create fields and descriptors
    // for unnamed bitfields here, even though we later ignore
    // them everywhere. That's so the FieldDecl's getFieldIndex() matches.

    // Reserve space for the field's descriptor and the offset.
    BaseSize += align(sizeof(InlineDescriptor));

    // Classify the field and add its metadata.
    QualType FT = FD->getType();
    const bool IsConst = FT.isConstQualified();
    const bool IsMutable = FD->isMutable();
    const Descriptor *Desc;
    if (std::optional<PrimType> T = Ctx.classify(FT)) {
      Desc = createDescriptor(FD, *T, std::nullopt, IsConst,
                              /*isTemporary=*/false, IsMutable);
    } else {
      Desc = createDescriptor(FD, FT.getTypePtr(), std::nullopt, IsConst,
                              /*isTemporary=*/false, IsMutable);
    }
    if (!Desc)
      return nullptr;
    Fields.push_back({FD, BaseSize, Desc});
    BaseSize += align(Desc->getAllocSize());
  }

  Record *R = new (Allocator) Record(RD, std::move(Bases), std::move(Fields),
                                     std::move(VirtBases), VirtSize, BaseSize);
  Records[RD] = R;
  return R;
}

Descriptor *Program::createDescriptor(const DeclTy &D, const Type *Ty,
                                      Descriptor::MetadataSize MDSize,
                                      bool IsConst, bool IsTemporary,
                                      bool IsMutable, const Expr *Init) {

  // Classes and structures.
  if (const auto *RT = Ty->getAs<RecordType>()) {
    if (const auto *Record = getOrCreateRecord(RT->getDecl()))
      return allocateDescriptor(D, Record, MDSize, IsConst, IsTemporary,
                                IsMutable);
  }

  // Arrays.
  if (const auto ArrayType = Ty->getAsArrayTypeUnsafe()) {
    QualType ElemTy = ArrayType->getElementType();
    // Array of well-known bounds.
    if (auto CAT = dyn_cast<ConstantArrayType>(ArrayType)) {
      size_t NumElems = CAT->getZExtSize();
      if (std::optional<PrimType> T = Ctx.classify(ElemTy)) {
        // Arrays of primitives.
        unsigned ElemSize = primSize(*T);
        if (std::numeric_limits<unsigned>::max() / ElemSize <= NumElems) {
          return {};
        }
        return allocateDescriptor(D, *T, MDSize, NumElems, IsConst, IsTemporary,
                                  IsMutable);
      } else {
        // Arrays of composites. In this case, the array is a list of pointers,
        // followed by the actual elements.
        const Descriptor *ElemDesc = createDescriptor(
            D, ElemTy.getTypePtr(), std::nullopt, IsConst, IsTemporary);
        if (!ElemDesc)
          return nullptr;
        unsigned ElemSize =
            ElemDesc->getAllocSize() + sizeof(InlineDescriptor);
        if (std::numeric_limits<unsigned>::max() / ElemSize <= NumElems)
          return {};
        return allocateDescriptor(D, ElemDesc, MDSize, NumElems, IsConst,
                                  IsTemporary, IsMutable);
      }
    }

    // Array of unknown bounds - cannot be accessed and pointer arithmetic
    // is forbidden on pointers to such objects.
    if (isa<IncompleteArrayType>(ArrayType) ||
        isa<VariableArrayType>(ArrayType)) {
      if (std::optional<PrimType> T = Ctx.classify(ElemTy)) {
        return allocateDescriptor(D, *T, MDSize, IsTemporary,
                                  Descriptor::UnknownSize{});
      } else {
        const Descriptor *Desc = createDescriptor(D, ElemTy.getTypePtr(),
                                                  MDSize, IsConst, IsTemporary);
        if (!Desc)
          return nullptr;
        return allocateDescriptor(D, Desc, MDSize, IsTemporary,
                                  Descriptor::UnknownSize{});
      }
    }
  }

  // Atomic types.
  if (const auto *AT = Ty->getAs<AtomicType>()) {
    const Type *InnerTy = AT->getValueType().getTypePtr();
    return createDescriptor(D, InnerTy, MDSize, IsConst, IsTemporary,
                            IsMutable);
  }

  // Complex types - represented as arrays of elements.
  if (const auto *CT = Ty->getAs<ComplexType>()) {
    PrimType ElemTy = *Ctx.classify(CT->getElementType());
    return allocateDescriptor(D, ElemTy, MDSize, 2, IsConst, IsTemporary,
                              IsMutable);
  }

  // Same with vector types.
  if (const auto *VT = Ty->getAs<VectorType>()) {
    PrimType ElemTy = *Ctx.classify(VT->getElementType());
    return allocateDescriptor(D, ElemTy, MDSize, VT->getNumElements(), IsConst,
                              IsTemporary, IsMutable);
  }

  return nullptr;
}
