//==--- AbstractBasiceReader.h - Abstract basic value deserialization -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_ABSTRACTBASICREADER_H
#define LLVM_CLANG_AST_ABSTRACTBASICREADER_H

#include "clang/AST/DeclTemplate.h"
#include <optional>

namespace clang {
namespace serialization {

template <class T>
inline T makeNullableFromOptional(const std::optional<T> &value) {
  return (value ? *value : T());
}

template <class T> inline T *makePointerFromOptional(std::optional<T *> value) {
  return value.value_or(nullptr);
}

// PropertyReader is a class concept that requires the following method:
//   BasicReader find(llvm::StringRef propertyName);
// where BasicReader is some class conforming to the BasicReader concept.
// An abstract AST-node reader is created with a PropertyReader and
// performs a sequence of calls like so:
//   propertyReader.find(propertyName).read##TypeName()
// to read the properties of the node it is deserializing.

// BasicReader is a class concept that requires methods like:
//   ValueType read##TypeName();
// where TypeName is the name of a PropertyType node from PropertiesBase.td
// and ValueType is the corresponding C++ type name.  The read method may
// require one or more buffer arguments.
//
// In addition to the concrete type names, BasicReader is expected to
// implement these methods:
//
//   template <class EnumType>
//   void writeEnum(T value);
//
//     Reads an enum value from the current property.  EnumType will always
//     be an enum type.  Only necessary if the BasicReader doesn't provide
//     type-specific readers for all the enum types.
//
//   template <class ValueType>
//   std::optional<ValueType> writeOptional();
//
//     Reads an optional value from the current property.
//
//   template <class ValueType>
//   ArrayRef<ValueType> readArray(llvm::SmallVectorImpl<ValueType> &buffer);
//
//     Reads an array of values from the current property.
//
//   PropertyReader readObject();
//
//     Reads an object from the current property; the returned property
//     reader will be subjected to a sequence of property reads and then
//     discarded before any other properties are reader from the "outer"
//     property reader (which need not be the same type).  The sub-reader
//     will be used as if with the following code:
//
//       {
//         auto &&widget = W.find("widget").readObject();
//         auto kind = widget.find("kind").readWidgetKind();
//         auto declaration = widget.find("declaration").readDeclRef();
//         return Widget(kind, declaration);
//       }

// ReadDispatcher does type-based forwarding to one of the read methods
// on the BasicReader passed in:
//
// template <class ValueType>
// struct ReadDispatcher {
//   template <class BasicReader, class... BufferTypes>
//   static ValueType read(BasicReader &R, BufferTypes &&...);
// };

// BasicReaderBase provides convenience implementations of the read methods
// for EnumPropertyType and SubclassPropertyType types that just defer to
// the "underlying" implementations (for UInt32 and the base class,
// respectively).
//
// template <class Impl>
// class BasicReaderBase {
// protected:
//   BasicReaderBase(ASTContext &ctx);
//   Impl &asImpl();
// public:
//   ASTContext &getASTContext();
//   ...
// };

// The actual classes are auto-generated; see ClangASTPropertiesEmitter.cpp.
#include "clang/AST/AbstractBasicReader.inc"

/// DataStreamBasicReader provides convenience implementations for many
/// BasicReader methods based on the assumption that the
/// ultimate reader implementation is based on a variable-length stream
/// of unstructured data (like Clang's module files).  It is designed
/// to pair with DataStreamBasicWriter.
///
/// This class can also act as a PropertyReader, implementing find("...")
/// by simply forwarding to itself.
///
/// Unimplemented methods:
///   readBool
///   readUInt32
///   readUInt64
///   readIdentifier
///   readSelector
///   readSourceLocation
///   readQualType
///   readStmtRef
///   readDeclRef
template <class Impl>
class DataStreamBasicReader : public BasicReaderBase<Impl> {
protected:
  using BasicReaderBase<Impl>::asImpl;
  DataStreamBasicReader(ASTContext &ctx) : BasicReaderBase<Impl>(ctx) {}

public:
  using BasicReaderBase<Impl>::getASTContext;

  /// Implement property-find by ignoring it.  We rely on properties being
  /// serialized and deserialized in a reliable order instead.
  Impl &find(const char *propertyName) {
    return asImpl();
  }

  template <class T>
  T readEnum() {
    return T(asImpl().readUInt32());
  }

  // Implement object reading by forwarding to this, collapsing the
  // structure into a single data stream.
  Impl &readObject() { return asImpl(); }

  template <class T>
  llvm::ArrayRef<T> readArray(llvm::SmallVectorImpl<T> &buffer) {
    assert(buffer.empty());

    uint32_t size = asImpl().readUInt32();
    buffer.reserve(size);

    for (uint32_t i = 0; i != size; ++i) {
      buffer.push_back(ReadDispatcher<T>::read(asImpl()));
    }
    return buffer;
  }

  template <class T, class... Args>
  std::optional<T> readOptional(Args &&...args) {
    return UnpackOptionalValue<T>::unpack(
             ReadDispatcher<T>::read(asImpl(), std::forward<Args>(args)...));
  }

  llvm::APSInt readAPSInt() {
    bool isUnsigned = asImpl().readBool();
    llvm::APInt value = asImpl().readAPInt();
    return llvm::APSInt(std::move(value), isUnsigned);
  }

  llvm::APInt readAPInt() {
    unsigned bitWidth = asImpl().readUInt32();
    unsigned numWords = llvm::APInt::getNumWords(bitWidth);
    llvm::SmallVector<uint64_t, 4> data;
    for (uint32_t i = 0; i != numWords; ++i)
      data.push_back(asImpl().readUInt64());
    return llvm::APInt(bitWidth, numWords, &data[0]);
  }

  llvm::FixedPointSemantics readFixedPointSemantics() {
    unsigned width = asImpl().readUInt32();
    unsigned scale = asImpl().readUInt32();
    unsigned tmp = asImpl().readUInt32();
    bool isSigned = tmp & 0x1;
    bool isSaturated = tmp & 0x2;
    bool hasUnsignedPadding = tmp & 0x4;
    return llvm::FixedPointSemantics(width, scale, isSigned, isSaturated,
                                     hasUnsignedPadding);
  }

  APValue::LValuePathSerializationHelper readLValuePathSerializationHelper(
      SmallVectorImpl<APValue::LValuePathEntry> &path) {
    auto origTy = asImpl().readQualType();
    auto elemTy = origTy;
    unsigned pathLength = asImpl().readUInt32();
    for (unsigned i = 0; i < pathLength; ++i) {
      if (elemTy->template getAs<RecordType>()) {
        unsigned int_ = asImpl().readUInt32();
        Decl *decl = asImpl().template readDeclAs<Decl>();
        if (auto *recordDecl = dyn_cast<CXXRecordDecl>(decl))
          elemTy = getASTContext().getRecordType(recordDecl);
        else
          elemTy = cast<ValueDecl>(decl)->getType();
        path.push_back(
            APValue::LValuePathEntry(APValue::BaseOrMemberType(decl, int_)));
      } else {
        elemTy = getASTContext().getAsArrayType(elemTy)->getElementType();
        path.push_back(
            APValue::LValuePathEntry::ArrayIndex(asImpl().readUInt32()));
      }
    }
    return APValue::LValuePathSerializationHelper(path, origTy);
  }

  Qualifiers readQualifiers() {
    static_assert(sizeof(Qualifiers().getAsOpaqueValue()) <= sizeof(uint64_t),
                  "update this if the value size changes");
    uint64_t value = asImpl().readUInt64();
    return Qualifiers::fromOpaqueValue(value);
  }

  FunctionProtoType::ExceptionSpecInfo
  readExceptionSpecInfo(llvm::SmallVectorImpl<QualType> &buffer) {
    FunctionProtoType::ExceptionSpecInfo esi;
    esi.Type = ExceptionSpecificationType(asImpl().readUInt32());
    if (esi.Type == EST_Dynamic) {
      esi.Exceptions = asImpl().template readArray<QualType>(buffer);
    } else if (isComputedNoexcept(esi.Type)) {
      esi.NoexceptExpr = asImpl().readExprRef();
    } else if (esi.Type == EST_Uninstantiated) {
      esi.SourceDecl = asImpl().readFunctionDeclRef();
      esi.SourceTemplate = asImpl().readFunctionDeclRef();
    } else if (esi.Type == EST_Unevaluated) {
      esi.SourceDecl = asImpl().readFunctionDeclRef();
    }
    return esi;
  }

  FunctionProtoType::ExtParameterInfo readExtParameterInfo() {
    static_assert(sizeof(FunctionProtoType::ExtParameterInfo().getOpaqueValue())
                    <= sizeof(uint32_t),
                  "opaque value doesn't fit into uint32_t");
    uint32_t value = asImpl().readUInt32();
    return FunctionProtoType::ExtParameterInfo::getFromOpaqueValue(value);
  }

  FunctionEffect readFunctionEffect() {
    uint32_t value = asImpl().readUInt32();
    return FunctionEffect::fromOpaqueInt32(value);
  }

  EffectConditionExpr readEffectConditionExpr() {
    return EffectConditionExpr{asImpl().readExprRef()};
  }

  NestedNameSpecifier *readNestedNameSpecifier() {
    auto &ctx = getASTContext();

    // We build this up iteratively.
    NestedNameSpecifier *cur = nullptr;

    uint32_t depth = asImpl().readUInt32();
    for (uint32_t i = 0; i != depth; ++i) {
      auto kind = asImpl().readNestedNameSpecifierKind();
      switch (kind) {
      case NestedNameSpecifier::Identifier:
        cur = NestedNameSpecifier::Create(ctx, cur,
                                          asImpl().readIdentifier());
        continue;

      case NestedNameSpecifier::Namespace:
        cur = NestedNameSpecifier::Create(ctx, cur,
                                          asImpl().readNamespaceDeclRef());
        continue;

      case NestedNameSpecifier::NamespaceAlias:
        cur = NestedNameSpecifier::Create(ctx, cur,
                                     asImpl().readNamespaceAliasDeclRef());
        continue;

      case NestedNameSpecifier::TypeSpec:
      case NestedNameSpecifier::TypeSpecWithTemplate:
        cur = NestedNameSpecifier::Create(ctx, cur,
                          kind == NestedNameSpecifier::TypeSpecWithTemplate,
                          asImpl().readQualType().getTypePtr());
        continue;

      case NestedNameSpecifier::Global:
        cur = NestedNameSpecifier::GlobalSpecifier(ctx);
        continue;

      case NestedNameSpecifier::Super:
        cur = NestedNameSpecifier::SuperSpecifier(ctx,
                                            asImpl().readCXXRecordDeclRef());
        continue;
      }
      llvm_unreachable("bad nested name specifier kind");
    }

    return cur;
  }
};

} // end namespace serialization
} // end namespace clang

#endif
