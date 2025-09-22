//===-- CTFTypes.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLFILE_CTF_CTFTYPES_H
#define LLDB_SOURCE_PLUGINS_SYMBOLFILE_CTF_CTFTYPES_H

#include "lldb/lldb-types.h"
#include "llvm/ADT/StringRef.h"

namespace lldb_private {

struct CTFType {
  enum Kind : uint32_t {
    eUnknown = 0,
    eInteger = 1,
    eFloat = 2,
    ePointer = 3,
    eArray = 4,
    eFunction = 5,
    eStruct = 6,
    eUnion = 7,
    eEnum = 8,
    eForward = 9,
    eTypedef = 10,
    eVolatile = 11,
    eConst = 12,
    eRestrict = 13,
    eSlice = 14,
  };

  Kind kind;
  lldb::user_id_t uid;
  llvm::StringRef name;

  CTFType(Kind kind, lldb::user_id_t uid, llvm::StringRef name)
      : kind(kind), uid(uid), name(name) {}
};

struct CTFInteger : public CTFType {
  CTFInteger(lldb::user_id_t uid, llvm::StringRef name, uint32_t bits,
             uint32_t encoding)
      : CTFType(eInteger, uid, name), bits(bits), encoding(encoding) {}

  static bool classof(const CTFType *T) { return T->kind == eInteger; }

  uint32_t bits;
  uint32_t encoding;
};

struct CTFModifier : public CTFType {
protected:
  CTFModifier(Kind kind, lldb::user_id_t uid, uint32_t type)
      : CTFType(kind, uid, ""), type(type) {}

  static bool classof(const CTFType *T) {
    return T->kind == ePointer || T->kind == eConst || T->kind == eVolatile ||
           T->kind == eRestrict;
  }

public:
  uint32_t type;
};

struct CTFPointer : public CTFModifier {
  CTFPointer(lldb::user_id_t uid, uint32_t type)
      : CTFModifier(ePointer, uid, type) {}

  static bool classof(const CTFType *T) { return T->kind == ePointer; }
};

struct CTFConst : public CTFModifier {
  CTFConst(lldb::user_id_t uid, uint32_t type)
      : CTFModifier(eConst, uid, type) {}

  static bool classof(const CTFType *T) { return T->kind == eConst; }
};

struct CTFVolatile : public CTFModifier {
  CTFVolatile(lldb::user_id_t uid, uint32_t type)
      : CTFModifier(eVolatile, uid, type) {}

  static bool classof(const CTFType *T) { return T->kind == eVolatile; }
};

struct CTFRestrict : public CTFModifier {
  CTFRestrict(lldb::user_id_t uid, uint32_t type)
      : CTFModifier(eRestrict, uid, type) {}
  static bool classof(const CTFType *T) { return T->kind == eRestrict; }
};

struct CTFTypedef : public CTFType {
  CTFTypedef(lldb::user_id_t uid, llvm::StringRef name, uint32_t type)
      : CTFType(eTypedef, uid, name), type(type) {}

  static bool classof(const CTFType *T) { return T->kind == eTypedef; }

  uint32_t type;
};

struct CTFArray : public CTFType {
  CTFArray(lldb::user_id_t uid, llvm::StringRef name, uint32_t type,
           uint32_t index, uint32_t nelems)
      : CTFType(eArray, uid, name), type(type), index(index), nelems(nelems) {}

  static bool classof(const CTFType *T) { return T->kind == eArray; }

  uint32_t type;
  uint32_t index;
  uint32_t nelems;
};

struct CTFEnum : public CTFType {
  struct Value {
    Value(llvm::StringRef name, uint32_t value) : name(name), value(value){};
    llvm::StringRef name;
    uint32_t value;
  };

  CTFEnum(lldb::user_id_t uid, llvm::StringRef name, uint32_t nelems,
          uint32_t size, std::vector<Value> values)
      : CTFType(eEnum, uid, name), nelems(nelems), size(size),
        values(std::move(values)) {
    assert(this->values.size() == nelems);
  }

  static bool classof(const CTFType *T) { return T->kind == eEnum; }

  uint32_t nelems;
  uint32_t size;
  std::vector<Value> values;
};

struct CTFFunction : public CTFType {
  CTFFunction(lldb::user_id_t uid, llvm::StringRef name, uint32_t nargs,
              uint32_t return_type, std::vector<uint32_t> args, bool variadic)
      : CTFType(eFunction, uid, name), nargs(nargs), return_type(return_type),
        args(std::move(args)), variadic(variadic) {}

  static bool classof(const CTFType *T) { return T->kind == eFunction; }

  uint32_t nargs;
  uint32_t return_type;

  std::vector<uint32_t> args;
  bool variadic = false;
};

struct CTFRecord : public CTFType {
public:
  struct Field {
    Field(llvm::StringRef name, uint32_t type, uint64_t offset)
        : name(name), type(type), offset(offset) {}

    llvm::StringRef name;
    uint32_t type;
    uint64_t offset;
  };

  CTFRecord(Kind kind, lldb::user_id_t uid, llvm::StringRef name,
            uint32_t nfields, uint32_t size, std::vector<Field> fields)
      : CTFType(kind, uid, name), nfields(nfields), size(size),
        fields(std::move(fields)) {}

  static bool classof(const CTFType *T) {
    return T->kind == eStruct || T->kind == eUnion;
  }

  uint32_t nfields;
  uint32_t size;
  std::vector<Field> fields;
};

struct CTFStruct : public CTFRecord {
  CTFStruct(lldb::user_id_t uid, llvm::StringRef name, uint32_t nfields,
            uint32_t size, std::vector<Field> fields)
      : CTFRecord(eStruct, uid, name, nfields, size, std::move(fields)){};

  static bool classof(const CTFType *T) { return T->kind == eStruct; }
};

struct CTFUnion : public CTFRecord {
  CTFUnion(lldb::user_id_t uid, llvm::StringRef name, uint32_t nfields,
           uint32_t size, std::vector<Field> fields)
      : CTFRecord(eUnion, uid, name, nfields, size, std::move(fields)){};

  static bool classof(const CTFType *T) { return T->kind == eUnion; }
};

struct CTFForward : public CTFType {
  CTFForward(lldb::user_id_t uid, llvm::StringRef name)
      : CTFType(eForward, uid, name) {}

  static bool classof(const CTFType *T) { return T->kind == eForward; }
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_SYMBOLFILE_CTF_CTFTYPES_H
