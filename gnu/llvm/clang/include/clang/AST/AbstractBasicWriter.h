//==--- AbstractBasicWriter.h - Abstract basic value serialization --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_ABSTRACTBASICWRITER_H
#define LLVM_CLANG_AST_ABSTRACTBASICWRITER_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclTemplate.h"
#include <optional>

namespace clang {
namespace serialization {

template <class T>
inline std::optional<T> makeOptionalFromNullable(const T &value) {
  return (value.isNull() ? std::optional<T>() : std::optional<T>(value));
}

template <class T> inline std::optional<T *> makeOptionalFromPointer(T *value) {
  return (value ? std::optional<T *>(value) : std::optional<T *>());
}

// PropertyWriter is a class concept that requires the following method:
//   BasicWriter find(llvm::StringRef propertyName);
// where BasicWriter is some class conforming to the BasicWriter concept.
// An abstract AST-node writer is created with a PropertyWriter and
// performs a sequence of calls like so:
//   propertyWriter.find(propertyName).write##TypeName(value)
// to write the properties of the node it is serializing.

// BasicWriter is a class concept that requires methods like:
//   void write##TypeName(ValueType value);
// where TypeName is the name of a PropertyType node from PropertiesBase.td
// and ValueType is the corresponding C++ type name.
//
// In addition to the concrete property types, BasicWriter is expected
// to implement these methods:
//
//   template <class EnumType>
//   void writeEnum(T value);
//
//     Writes an enum value as the current property.  EnumType will always
//     be an enum type.  Only necessary if the BasicWriter doesn't provide
//     type-specific writers for all the enum types.
//
//   template <class ValueType>
//   void writeOptional(std::optional<ValueType> value);
//
//     Writes an optional value as the current property.
//
//   template <class ValueType>
//   void writeArray(ArrayRef<ValueType> value);
//
//     Writes an array of values as the current property.
//
//   PropertyWriter writeObject();
//
//     Writes an object as the current property; the returned property
//     writer will be subjected to a sequence of property writes and then
//     discarded before any other properties are written to the "outer"
//     property writer (which need not be the same type).  The sub-writer
//     will be used as if with the following code:
//
//       {
//         auto &&widget = W.find("widget").writeObject();
//         widget.find("kind").writeWidgetKind(...);
//         widget.find("declaration").writeDeclRef(...);
//       }

// WriteDispatcher is a template which does type-based forwarding to one
// of the write methods of the BasicWriter passed in:
//
// template <class ValueType>
// struct WriteDispatcher {
//   template <class BasicWriter>
//   static void write(BasicWriter &W, ValueType value);
// };

// BasicWriterBase provides convenience implementations of the write
// methods for EnumPropertyType and SubclassPropertyType types that just
// defer to the "underlying" implementations (for UInt32 and the base class,
// respectively).
//
// template <class Impl>
// class BasicWriterBase {
// protected:
//   Impl &asImpl();
// public:
//   ...
// };

// The actual classes are auto-generated; see ClangASTPropertiesEmitter.cpp.
#include "clang/AST/AbstractBasicWriter.inc"

/// DataStreamBasicWriter provides convenience implementations for many
/// BasicWriter methods based on the assumption that the
/// ultimate writer implementation is based on a variable-length stream
/// of unstructured data (like Clang's module files).  It is designed
/// to pair with DataStreamBasicReader.
///
/// This class can also act as a PropertyWriter, implementing find("...")
/// by simply forwarding to itself.
///
/// Unimplemented methods:
///   writeBool
///   writeUInt32
///   writeUInt64
///   writeIdentifier
///   writeSelector
///   writeSourceLocation
///   writeQualType
///   writeStmtRef
///   writeDeclRef
template <class Impl>
class DataStreamBasicWriter : public BasicWriterBase<Impl> {
protected:
  using BasicWriterBase<Impl>::asImpl;
  DataStreamBasicWriter(ASTContext &ctx) : BasicWriterBase<Impl>(ctx) {}

public:
  /// Implement property-find by ignoring it.  We rely on properties being
  /// serialized and deserialized in a reliable order instead.
  Impl &find(const char *propertyName) {
    return asImpl();
  }

  // Implement object writing by forwarding to this, collapsing the
  // structure into a single data stream.
  Impl &writeObject() { return asImpl(); }

  template <class T>
  void writeEnum(T value) {
    asImpl().writeUInt32(uint32_t(value));
  }

  template <class T>
  void writeArray(llvm::ArrayRef<T> array) {
    asImpl().writeUInt32(array.size());
    for (const T &elt : array) {
      WriteDispatcher<T>::write(asImpl(), elt);
    }
  }

  template <class T> void writeOptional(std::optional<T> value) {
    WriteDispatcher<T>::write(asImpl(), PackOptionalValue<T>::pack(value));
  }

  void writeAPSInt(const llvm::APSInt &value) {
    asImpl().writeBool(value.isUnsigned());
    asImpl().writeAPInt(value);
  }

  void writeAPInt(const llvm::APInt &value) {
    asImpl().writeUInt32(value.getBitWidth());
    const uint64_t *words = value.getRawData();
    for (size_t i = 0, e = value.getNumWords(); i != e; ++i)
      asImpl().writeUInt64(words[i]);
  }

  void writeFixedPointSemantics(const llvm::FixedPointSemantics &sema) {
    asImpl().writeUInt32(sema.getWidth());
    asImpl().writeUInt32(sema.getScale());
    asImpl().writeUInt32(sema.isSigned() | sema.isSaturated() << 1 |
                         sema.hasUnsignedPadding() << 2);
  }

  void writeLValuePathSerializationHelper(
      APValue::LValuePathSerializationHelper lvaluePath) {
    ArrayRef<APValue::LValuePathEntry> path = lvaluePath.Path;
    QualType elemTy = lvaluePath.getType();
    asImpl().writeQualType(elemTy);
    asImpl().writeUInt32(path.size());
    auto &ctx = ((BasicWriterBase<Impl> *)this)->getASTContext();
    for (auto elem : path) {
      if (elemTy->getAs<RecordType>()) {
        asImpl().writeUInt32(elem.getAsBaseOrMember().getInt());
        const Decl *baseOrMember = elem.getAsBaseOrMember().getPointer();
        if (const auto *recordDecl = dyn_cast<CXXRecordDecl>(baseOrMember)) {
          asImpl().writeDeclRef(recordDecl);
          elemTy = ctx.getRecordType(recordDecl);
        } else {
          const auto *valueDecl = cast<ValueDecl>(baseOrMember);
          asImpl().writeDeclRef(valueDecl);
          elemTy = valueDecl->getType();
        }
      } else {
        asImpl().writeUInt32(elem.getAsArrayIndex());
        elemTy = ctx.getAsArrayType(elemTy)->getElementType();
      }
    }
  }

  void writeQualifiers(Qualifiers value) {
    static_assert(sizeof(value.getAsOpaqueValue()) <= sizeof(uint64_t),
                  "update this if the value size changes");
    asImpl().writeUInt64(value.getAsOpaqueValue());
  }

  void writeExceptionSpecInfo(
                        const FunctionProtoType::ExceptionSpecInfo &esi) {
    asImpl().writeUInt32(uint32_t(esi.Type));
    if (esi.Type == EST_Dynamic) {
      asImpl().writeArray(esi.Exceptions);
    } else if (isComputedNoexcept(esi.Type)) {
      asImpl().writeExprRef(esi.NoexceptExpr);
    } else if (esi.Type == EST_Uninstantiated) {
      asImpl().writeDeclRef(esi.SourceDecl);
      asImpl().writeDeclRef(esi.SourceTemplate);
    } else if (esi.Type == EST_Unevaluated) {
      asImpl().writeDeclRef(esi.SourceDecl);
    }
  }

  void writeExtParameterInfo(FunctionProtoType::ExtParameterInfo epi) {
    static_assert(sizeof(epi.getOpaqueValue()) <= sizeof(uint32_t),
                  "opaque value doesn't fit into uint32_t");
    asImpl().writeUInt32(epi.getOpaqueValue());
  }

  void writeFunctionEffect(FunctionEffect E) {
    asImpl().writeUInt32(E.toOpaqueInt32());
  }

  void writeEffectConditionExpr(EffectConditionExpr CE) {
    asImpl().writeExprRef(CE.getCondition());
  }

  void writeNestedNameSpecifier(NestedNameSpecifier *NNS) {
    // Nested name specifiers usually aren't too long. I think that 8 would
    // typically accommodate the vast majority.
    SmallVector<NestedNameSpecifier *, 8> nestedNames;

    // Push each of the NNS's onto a stack for serialization in reverse order.
    while (NNS) {
      nestedNames.push_back(NNS);
      NNS = NNS->getPrefix();
    }

    asImpl().writeUInt32(nestedNames.size());
    while (!nestedNames.empty()) {
      NNS = nestedNames.pop_back_val();
      NestedNameSpecifier::SpecifierKind kind = NNS->getKind();
      asImpl().writeNestedNameSpecifierKind(kind);
      switch (kind) {
      case NestedNameSpecifier::Identifier:
        asImpl().writeIdentifier(NNS->getAsIdentifier());
        continue;

      case NestedNameSpecifier::Namespace:
        asImpl().writeNamespaceDeclRef(NNS->getAsNamespace());
        continue;

      case NestedNameSpecifier::NamespaceAlias:
        asImpl().writeNamespaceAliasDeclRef(NNS->getAsNamespaceAlias());
        continue;

      case NestedNameSpecifier::TypeSpec:
      case NestedNameSpecifier::TypeSpecWithTemplate:
        asImpl().writeQualType(QualType(NNS->getAsType(), 0));
        continue;

      case NestedNameSpecifier::Global:
        // Don't need to write an associated value.
        continue;

      case NestedNameSpecifier::Super:
        asImpl().writeDeclRef(NNS->getAsRecordDecl());
        continue;
      }
      llvm_unreachable("bad nested name specifier kind");
    }
  }
};

} // end namespace serialization
} // end namespace clang

#endif
