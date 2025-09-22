//===-- UdtRecordCompleter.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLFILE_NATIVEPDB_UDTRECORDCOMPLETER_H
#define LLDB_SOURCE_PLUGINS_SYMBOLFILE_NATIVEPDB_UDTRECORDCOMPLETER_H

#include "PdbAstBuilder.h"
#include "PdbSymUid.h"
#include "Plugins/ExpressionParser/Clang/ClangASTImporter.h"
#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/CodeView/TypeVisitorCallbacks.h"
#include <optional>

namespace clang {
class CXXBaseSpecifier;
class QualType;
class TagDecl;
} // namespace clang

namespace llvm {
namespace pdb {
class TpiStream;
class GlobalsStream;
}
} // namespace llvm

namespace lldb_private {
class Type;
class CompilerType;
namespace npdb {
class PdbAstBuilder;
class PdbIndex;

class UdtRecordCompleter : public llvm::codeview::TypeVisitorCallbacks {
  using IndexedBase =
      std::pair<uint64_t, std::unique_ptr<clang::CXXBaseSpecifier>>;

  union UdtTagRecord {
    UdtTagRecord() {}
    llvm::codeview::UnionRecord ur;
    llvm::codeview::ClassRecord cr;
    llvm::codeview::EnumRecord er;
  } m_cvr;

  PdbTypeSymId m_id;
  CompilerType &m_derived_ct;
  clang::TagDecl &m_tag_decl;
  PdbAstBuilder &m_ast_builder;
  PdbIndex &m_index;
  std::vector<IndexedBase> m_bases;
  ClangASTImporter::LayoutInfo m_layout;
  llvm::DenseMap<clang::Decl *, DeclStatus> &m_decl_to_status;
  llvm::DenseMap<lldb::opaque_compiler_type_t,
                 llvm::SmallSet<std::pair<llvm::StringRef, CompilerType>, 8>>
      &m_cxx_record_map;

public:
  UdtRecordCompleter(
      PdbTypeSymId id, CompilerType &derived_ct, clang::TagDecl &tag_decl,
      PdbAstBuilder &ast_builder, PdbIndex &index,
      llvm::DenseMap<clang::Decl *, DeclStatus> &decl_to_status,
      llvm::DenseMap<lldb::opaque_compiler_type_t,
                     llvm::SmallSet<std::pair<llvm::StringRef, CompilerType>,
                                    8>> &cxx_record_map);

#define MEMBER_RECORD(EnumName, EnumVal, Name)                                 \
  llvm::Error visitKnownMember(llvm::codeview::CVMemberRecord &CVR,            \
                               llvm::codeview::Name##Record &Record) override;
#define MEMBER_RECORD_ALIAS(EnumName, EnumVal, Name, AliasName)
#include "llvm/DebugInfo/CodeView/CodeViewTypes.def"

  struct Member;
  using MemberUP = std::unique_ptr<Member>;

  struct Member {
    enum Kind { Field, Struct, Union } kind;
    // Following are only used for field.
    llvm::StringRef name;
    uint64_t bit_offset;
    uint64_t bit_size;
    clang::QualType qt;
    lldb::AccessType access;
    uint32_t bitfield_width;
    // Following are Only used for struct or union.
    uint64_t base_offset;
    llvm::SmallVector<MemberUP, 1> fields;

    Member() = default;
    Member(Kind kind)
        : kind(kind), name(), bit_offset(0), bit_size(0), qt(),
          access(lldb::eAccessPublic), bitfield_width(0), base_offset(0) {}
    Member(llvm::StringRef name, uint64_t bit_offset, uint64_t bit_size,
           clang::QualType qt, lldb::AccessType access, uint32_t bitfield_width)
        : kind(Field), name(name), bit_offset(bit_offset), bit_size(bit_size),
          qt(qt), access(access), bitfield_width(bitfield_width),
          base_offset(0) {}
    void ConvertToStruct() {
      kind = Struct;
      base_offset = bit_offset;
      fields.push_back(std::make_unique<Member>(name, bit_offset, bit_size, qt,
                                                access, bitfield_width));
      name = llvm::StringRef();
      qt = clang::QualType();
      access = lldb::eAccessPublic;
      bit_offset = bit_size = bitfield_width = 0;
    }
  };

  struct Record {
    // Top level record.
    Member record;
    uint64_t start_offset = UINT64_MAX;
    std::map<uint64_t, llvm::SmallVector<MemberUP, 1>> fields_map;
    void CollectMember(llvm::StringRef name, uint64_t offset,
                       uint64_t field_size, clang::QualType qt,
                       lldb::AccessType access, uint64_t bitfield_width);
    void ConstructRecord();
  };
  void complete();

private:
  Record m_record;
  clang::QualType AddBaseClassForTypeIndex(
      llvm::codeview::TypeIndex ti, llvm::codeview::MemberAccess access,
      std::optional<uint64_t> vtable_idx = std::optional<uint64_t>());
  void AddMethod(llvm::StringRef name, llvm::codeview::TypeIndex type_idx,
                 llvm::codeview::MemberAccess access,
                 llvm::codeview::MethodOptions options,
                 llvm::codeview::MemberAttributes attrs);
  void FinishRecord();
  uint64_t AddMember(TypeSystemClang &clang, Member *field, uint64_t bit_offset,
                     CompilerType parent_ct,
                     ClangASTImporter::LayoutInfo &parent_layout,
                     clang::DeclContext *decl_ctx);
};

} // namespace npdb
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_SYMBOLFILE_NATIVEPDB_UDTRECORDCOMPLETER_H
