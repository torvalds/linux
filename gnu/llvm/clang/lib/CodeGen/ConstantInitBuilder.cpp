//===--- ConstantInitBuilder.cpp - Global initializer builder -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines out-of-line routines for building initializers for
// global variables, in particular the kind of globals that are implicitly
// introduced by various language ABIs.
//
//===----------------------------------------------------------------------===//

#include "clang/CodeGen/ConstantInitBuilder.h"
#include "CodeGenModule.h"

using namespace clang;
using namespace CodeGen;

llvm::Type *ConstantInitFuture::getType() const {
  assert(Data && "dereferencing null future");
  if (Data.is<llvm::Constant*>()) {
    return Data.get<llvm::Constant*>()->getType();
  } else {
    return Data.get<ConstantInitBuilderBase*>()->Buffer[0]->getType();
  }
}

void ConstantInitFuture::abandon() {
  assert(Data && "abandoning null future");
  if (auto builder = Data.dyn_cast<ConstantInitBuilderBase*>()) {
    builder->abandon(0);
  }
  Data = nullptr;
}

void ConstantInitFuture::installInGlobal(llvm::GlobalVariable *GV) {
  assert(Data && "installing null future");
  if (Data.is<llvm::Constant*>()) {
    GV->setInitializer(Data.get<llvm::Constant*>());
  } else {
    auto &builder = *Data.get<ConstantInitBuilderBase*>();
    assert(builder.Buffer.size() == 1);
    builder.setGlobalInitializer(GV, builder.Buffer[0]);
    builder.Buffer.clear();
    Data = nullptr;
  }
}

ConstantInitFuture
ConstantInitBuilderBase::createFuture(llvm::Constant *initializer) {
  assert(Buffer.empty() && "buffer not current empty");
  Buffer.push_back(initializer);
  return ConstantInitFuture(this);
}

// Only used in this file.
inline ConstantInitFuture::ConstantInitFuture(ConstantInitBuilderBase *builder)
    : Data(builder) {
  assert(!builder->Frozen);
  assert(builder->Buffer.size() == 1);
  assert(builder->Buffer[0] != nullptr);
}

llvm::GlobalVariable *
ConstantInitBuilderBase::createGlobal(llvm::Constant *initializer,
                                      const llvm::Twine &name,
                                      CharUnits alignment,
                                      bool constant,
                                      llvm::GlobalValue::LinkageTypes linkage,
                                      unsigned addressSpace) {
  auto GV = new llvm::GlobalVariable(CGM.getModule(),
                                     initializer->getType(),
                                     constant,
                                     linkage,
                                     initializer,
                                     name,
                                     /*insert before*/ nullptr,
                                     llvm::GlobalValue::NotThreadLocal,
                                     addressSpace);
  GV->setAlignment(alignment.getAsAlign());
  resolveSelfReferences(GV);
  return GV;
}

void ConstantInitBuilderBase::setGlobalInitializer(llvm::GlobalVariable *GV,
                                                   llvm::Constant *initializer){
  GV->setInitializer(initializer);

  if (!SelfReferences.empty())
    resolveSelfReferences(GV);
}

void ConstantInitBuilderBase::resolveSelfReferences(llvm::GlobalVariable *GV) {
  for (auto &entry : SelfReferences) {
    llvm::Constant *resolvedReference =
      llvm::ConstantExpr::getInBoundsGetElementPtr(
        GV->getValueType(), GV, entry.Indices);
    auto dummy = entry.Dummy;
    dummy->replaceAllUsesWith(resolvedReference);
    dummy->eraseFromParent();
  }
  SelfReferences.clear();
}

void ConstantInitBuilderBase::abandon(size_t newEnd) {
  // Remove all the entries we've added.
  Buffer.erase(Buffer.begin() + newEnd, Buffer.end());

  // If we're abandoning all the way to the beginning, destroy
  // all the self-references, because we might not get another
  // opportunity.
  if (newEnd == 0) {
    for (auto &entry : SelfReferences) {
      auto dummy = entry.Dummy;
      dummy->replaceAllUsesWith(llvm::PoisonValue::get(dummy->getType()));
      dummy->eraseFromParent();
    }
    SelfReferences.clear();
  }
}

void ConstantAggregateBuilderBase::addSize(CharUnits size) {
  add(Builder.CGM.getSize(size));
}

llvm::Constant *
ConstantAggregateBuilderBase::getRelativeOffset(llvm::IntegerType *offsetType,
                                                llvm::Constant *target) {
  return getRelativeOffsetToPosition(offsetType, target,
                                     Builder.Buffer.size() - Begin);
}

llvm::Constant *ConstantAggregateBuilderBase::getRelativeOffsetToPosition(
    llvm::IntegerType *offsetType, llvm::Constant *target, size_t position) {
  // Compute the address of the relative-address slot.
  auto base = getAddrOfPosition(offsetType, position);

  // Subtract.
  base = llvm::ConstantExpr::getPtrToInt(base, Builder.CGM.IntPtrTy);
  target = llvm::ConstantExpr::getPtrToInt(target, Builder.CGM.IntPtrTy);
  llvm::Constant *offset = llvm::ConstantExpr::getSub(target, base);

  // Truncate to the relative-address type if necessary.
  if (Builder.CGM.IntPtrTy != offsetType) {
    offset = llvm::ConstantExpr::getTrunc(offset, offsetType);
  }

  return offset;
}

llvm::Constant *
ConstantAggregateBuilderBase::getAddrOfPosition(llvm::Type *type,
                                                size_t position) {
  // Make a global variable.  We will replace this with a GEP to this
  // position after installing the initializer.
  auto dummy = new llvm::GlobalVariable(Builder.CGM.getModule(), type, true,
                                        llvm::GlobalVariable::PrivateLinkage,
                                        nullptr, "");
  Builder.SelfReferences.emplace_back(dummy);
  auto &entry = Builder.SelfReferences.back();
  (void)getGEPIndicesTo(entry.Indices, position + Begin);
  return dummy;
}

llvm::Constant *
ConstantAggregateBuilderBase::getAddrOfCurrentPosition(llvm::Type *type) {
  // Make a global variable.  We will replace this with a GEP to this
  // position after installing the initializer.
  auto dummy =
    new llvm::GlobalVariable(Builder.CGM.getModule(), type, true,
                             llvm::GlobalVariable::PrivateLinkage,
                             nullptr, "");
  Builder.SelfReferences.emplace_back(dummy);
  auto &entry = Builder.SelfReferences.back();
  (void) getGEPIndicesToCurrentPosition(entry.Indices);
  return dummy;
}

void ConstantAggregateBuilderBase::getGEPIndicesTo(
                               llvm::SmallVectorImpl<llvm::Constant*> &indices,
                               size_t position) const {
  // Recurse on the parent builder if present.
  if (Parent) {
    Parent->getGEPIndicesTo(indices, Begin);

  // Otherwise, add an index to drill into the first level of pointer.
  } else {
    assert(indices.empty());
    indices.push_back(llvm::ConstantInt::get(Builder.CGM.Int32Ty, 0));
  }

  assert(position >= Begin);
  // We have to use i32 here because struct GEPs demand i32 indices.
  // It's rather unlikely to matter in practice.
  indices.push_back(llvm::ConstantInt::get(Builder.CGM.Int32Ty,
                                           position - Begin));
}

ConstantAggregateBuilderBase::PlaceholderPosition
ConstantAggregateBuilderBase::addPlaceholderWithSize(llvm::Type *type) {
  // Bring the offset up to the last field.
  CharUnits offset = getNextOffsetFromGlobal();

  // Create the placeholder.
  auto position = addPlaceholder();

  // Advance the offset past that field.
  auto &layout = Builder.CGM.getDataLayout();
  if (!Packed)
    offset = offset.alignTo(CharUnits::fromQuantity(layout.getABITypeAlign(type)));
  offset += CharUnits::fromQuantity(layout.getTypeStoreSize(type));

  CachedOffsetEnd = Builder.Buffer.size();
  CachedOffsetFromGlobal = offset;

  return position;
}

CharUnits ConstantAggregateBuilderBase::getOffsetFromGlobalTo(size_t end) const{
  size_t cacheEnd = CachedOffsetEnd;
  assert(cacheEnd <= end);

  // Fast path: if the cache is valid, just use it.
  if (cacheEnd == end) {
    return CachedOffsetFromGlobal;
  }

  // If the cached range ends before the index at which the current
  // aggregate starts, recurse for the parent.
  CharUnits offset;
  if (cacheEnd < Begin) {
    assert(cacheEnd == 0);
    assert(Parent && "Begin != 0 for root builder");
    cacheEnd = Begin;
    offset = Parent->getOffsetFromGlobalTo(Begin);
  } else {
    offset = CachedOffsetFromGlobal;
  }

  // Perform simple layout on the elements in cacheEnd..<end.
  if (cacheEnd != end) {
    auto &layout = Builder.CGM.getDataLayout();
    do {
      llvm::Constant *element = Builder.Buffer[cacheEnd];
      assert(element != nullptr &&
             "cannot compute offset when a placeholder is present");
      llvm::Type *elementType = element->getType();
      if (!Packed)
        offset = offset.alignTo(
            CharUnits::fromQuantity(layout.getABITypeAlign(elementType)));
      offset += CharUnits::fromQuantity(layout.getTypeStoreSize(elementType));
    } while (++cacheEnd != end);
  }

  // Cache and return.
  CachedOffsetEnd = cacheEnd;
  CachedOffsetFromGlobal = offset;
  return offset;
}

llvm::Constant *ConstantAggregateBuilderBase::finishArray(llvm::Type *eltTy) {
  markFinished();

  auto &buffer = getBuffer();
  assert((Begin < buffer.size() ||
          (Begin == buffer.size() && eltTy))
         && "didn't add any array elements without element type");
  auto elts = llvm::ArrayRef(buffer).slice(Begin);
  if (!eltTy) eltTy = elts[0]->getType();
  auto type = llvm::ArrayType::get(eltTy, elts.size());
  auto constant = llvm::ConstantArray::get(type, elts);
  buffer.erase(buffer.begin() + Begin, buffer.end());
  return constant;
}

llvm::Constant *
ConstantAggregateBuilderBase::finishStruct(llvm::StructType *ty) {
  markFinished();

  auto &buffer = getBuffer();
  auto elts = llvm::ArrayRef(buffer).slice(Begin);

  if (ty == nullptr && elts.empty())
    ty = llvm::StructType::get(Builder.CGM.getLLVMContext(), {}, Packed);

  llvm::Constant *constant;
  if (ty) {
    assert(ty->isPacked() == Packed);
    constant = llvm::ConstantStruct::get(ty, elts);
  } else {
    constant = llvm::ConstantStruct::getAnon(elts, Packed);
  }

  buffer.erase(buffer.begin() + Begin, buffer.end());
  return constant;
}

/// Sign the given pointer and add it to the constant initializer
/// currently being built.
void ConstantAggregateBuilderBase::addSignedPointer(
    llvm::Constant *Pointer, const PointerAuthSchema &Schema,
    GlobalDecl CalleeDecl, QualType CalleeType) {
  if (!Schema || !Builder.CGM.shouldSignPointer(Schema))
    return add(Pointer);

  llvm::Constant *StorageAddress = nullptr;
  if (Schema.isAddressDiscriminated()) {
    StorageAddress = getAddrOfCurrentPosition(Pointer->getType());
  }

  llvm::Constant *SignedPointer = Builder.CGM.getConstantSignedPointer(
      Pointer, Schema, StorageAddress, CalleeDecl, CalleeType);
  add(SignedPointer);
}
