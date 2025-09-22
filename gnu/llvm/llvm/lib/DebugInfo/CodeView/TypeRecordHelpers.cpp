//===- TypeRecordHelpers.cpp ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/CodeView/TypeRecordHelpers.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/DebugInfo/CodeView/TypeDeserializer.h"
#include "llvm/DebugInfo/CodeView/TypeIndexDiscovery.h"

using namespace llvm;
using namespace llvm::codeview;

template <typename RecordT> static ClassOptions getUdtOptions(CVType CVT) {
  RecordT Record;
  if (auto EC = TypeDeserializer::deserializeAs<RecordT>(CVT, Record)) {
    consumeError(std::move(EC));
    return ClassOptions::None;
  }
  return Record.getOptions();
}

bool llvm::codeview::isUdtForwardRef(CVType CVT) {
  ClassOptions UdtOptions = ClassOptions::None;
  switch (CVT.kind()) {
  case LF_STRUCTURE:
  case LF_CLASS:
  case LF_INTERFACE:
    UdtOptions = getUdtOptions<ClassRecord>(std::move(CVT));
    break;
  case LF_ENUM:
    UdtOptions = getUdtOptions<EnumRecord>(std::move(CVT));
    break;
  case LF_UNION:
    UdtOptions = getUdtOptions<UnionRecord>(std::move(CVT));
    break;
  default:
    return false;
  }
  return (UdtOptions & ClassOptions::ForwardReference) != ClassOptions::None;
}

TypeIndex llvm::codeview::getModifiedType(const CVType &CVT) {
  assert(CVT.kind() == LF_MODIFIER);
  SmallVector<TypeIndex, 1> Refs;
  discoverTypeIndices(CVT, Refs);
  return Refs.front();
}

uint64_t llvm::codeview::getSizeInBytesForTypeIndex(TypeIndex TI) {
  if (!TI.isSimple())
    return 0;
  if (TI.getSimpleMode() != SimpleTypeMode::Direct) {
    // We have a native pointer.
    switch (TI.getSimpleMode()) {
    case SimpleTypeMode::NearPointer:
    case SimpleTypeMode::FarPointer:
    case SimpleTypeMode::HugePointer:
      return 2;
    case SimpleTypeMode::NearPointer32:
    case SimpleTypeMode::FarPointer32:
      return 4;
    case SimpleTypeMode::NearPointer64:
      return 8;
    case SimpleTypeMode::NearPointer128:
      return 16;
    default:
      assert(false && "invalid simple type mode!");
    }
  }
  switch (TI.getSimpleKind()) {
  case SimpleTypeKind::None:
  case SimpleTypeKind::Void:
    return 0;
  case SimpleTypeKind::HResult:
    return 4;
  case SimpleTypeKind::SByte:
  case SimpleTypeKind::Byte:
    return 1;

  // Signed/unsigned integer.
  case SimpleTypeKind::Int16Short:
  case SimpleTypeKind::UInt16Short:
  case SimpleTypeKind::Int16:
  case SimpleTypeKind::UInt16:
    return 2;
  case SimpleTypeKind::Int32Long:
  case SimpleTypeKind::UInt32Long:
  case SimpleTypeKind::Int32:
  case SimpleTypeKind::UInt32:
    return 4;
  case SimpleTypeKind::Int64Quad:
  case SimpleTypeKind::UInt64Quad:
  case SimpleTypeKind::Int64:
  case SimpleTypeKind::UInt64:
    return 8;
  case SimpleTypeKind::Int128Oct:
  case SimpleTypeKind::UInt128Oct:
  case SimpleTypeKind::Int128:
  case SimpleTypeKind::UInt128:
    return 16;

  // Signed/Unsigned character.
  case SimpleTypeKind::Character8:
  case SimpleTypeKind::SignedCharacter:
  case SimpleTypeKind::UnsignedCharacter:
  case SimpleTypeKind::NarrowCharacter:
    return 1;
  case SimpleTypeKind::WideCharacter:
  case SimpleTypeKind::Character16:
    return 2;
  case SimpleTypeKind::Character32:
    return 4;

  // Float.
  case SimpleTypeKind::Float16:
    return 2;
  case SimpleTypeKind::Float32:
    return 4;
  case SimpleTypeKind::Float48:
    return 6;
  case SimpleTypeKind::Float64:
    return 8;
  case SimpleTypeKind::Float80:
    return 10;
  case SimpleTypeKind::Float128:
    return 16;

  // Boolean.
  case SimpleTypeKind::Boolean8:
    return 1;
  case SimpleTypeKind::Boolean16:
    return 2;
  case SimpleTypeKind::Boolean32:
    return 4;
  case SimpleTypeKind::Boolean64:
    return 8;
  case SimpleTypeKind::Boolean128:
    return 16;

  // Complex float.
  case SimpleTypeKind::Complex16:
    return 4;
  case SimpleTypeKind::Complex32:
    return 8;
  case SimpleTypeKind::Complex64:
    return 16;
  case SimpleTypeKind::Complex80:
    return 20;
  case SimpleTypeKind::Complex128:
    return 32;

  default:
    return 0;
  }
}

template <typename RecordT> static uint64_t getUdtSize(CVType CVT) {
  RecordT Record;
  if (auto EC = TypeDeserializer::deserializeAs<RecordT>(CVT, Record)) {
    consumeError(std::move(EC));
    return 0;
  }
  return Record.getSize();
}

uint64_t llvm::codeview::getSizeInBytesForTypeRecord(CVType CVT) {
  switch (CVT.kind()) {
  case LF_STRUCTURE:
  case LF_CLASS:
  case LF_INTERFACE:
    return getUdtSize<ClassRecord>(std::move(CVT));
  case LF_UNION:
    return getUdtSize<UnionRecord>(std::move(CVT));
  default:
    return CVT.length();
  }
}
