#include "UdtRecordCompleter.h"

#include "PdbAstBuilder.h"
#include "PdbIndex.h"
#include "PdbSymUid.h"
#include "PdbUtil.h"

#include "Plugins/ExpressionParser/Clang/ClangASTImporter.h"
#include "Plugins/ExpressionParser/Clang/ClangASTMetadata.h"
#include "Plugins/ExpressionParser/Clang/ClangUtil.h"
#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"
#include "SymbolFileNativePDB.h"
#include "lldb/Core/Address.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/DebugInfo/CodeView/SymbolDeserializer.h"
#include "llvm/DebugInfo/CodeView/TypeDeserializer.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/DebugInfo/PDB/Native/GlobalsStream.h"
#include "llvm/DebugInfo/PDB/Native/TpiStream.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"
#include <optional>

using namespace llvm::codeview;
using namespace llvm::pdb;
using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::npdb;

using Error = llvm::Error;

UdtRecordCompleter::UdtRecordCompleter(
    PdbTypeSymId id, CompilerType &derived_ct, clang::TagDecl &tag_decl,
    PdbAstBuilder &ast_builder, PdbIndex &index,
    llvm::DenseMap<clang::Decl *, DeclStatus> &decl_to_status,
    llvm::DenseMap<lldb::opaque_compiler_type_t,
                   llvm::SmallSet<std::pair<llvm::StringRef, CompilerType>, 8>>
        &cxx_record_map)
    : m_id(id), m_derived_ct(derived_ct), m_tag_decl(tag_decl),
      m_ast_builder(ast_builder), m_index(index),
      m_decl_to_status(decl_to_status), m_cxx_record_map(cxx_record_map) {
  CVType cvt = m_index.tpi().getType(m_id.index);
  switch (cvt.kind()) {
  case LF_ENUM:
    m_cvr.er.Options = ClassOptions::None;
    llvm::cantFail(TypeDeserializer::deserializeAs<EnumRecord>(cvt, m_cvr.er));
    break;
  case LF_UNION:
    m_cvr.ur.Options = ClassOptions::None;
    llvm::cantFail(TypeDeserializer::deserializeAs<UnionRecord>(cvt, m_cvr.ur));
    m_layout.bit_size = m_cvr.ur.getSize() * 8;
    m_record.record.kind = Member::Union;
    break;
  case LF_CLASS:
  case LF_STRUCTURE:
    m_cvr.cr.Options = ClassOptions::None;
    llvm::cantFail(TypeDeserializer::deserializeAs<ClassRecord>(cvt, m_cvr.cr));
    m_layout.bit_size = m_cvr.cr.getSize() * 8;
    m_record.record.kind = Member::Struct;
    break;
  default:
    llvm_unreachable("unreachable!");
  }
}

clang::QualType UdtRecordCompleter::AddBaseClassForTypeIndex(
    llvm::codeview::TypeIndex ti, llvm::codeview::MemberAccess access,
    std::optional<uint64_t> vtable_idx) {
  PdbTypeSymId type_id(ti);
  clang::QualType qt = m_ast_builder.GetOrCreateType(type_id);

  CVType udt_cvt = m_index.tpi().getType(ti);

  std::unique_ptr<clang::CXXBaseSpecifier> base_spec =
      m_ast_builder.clang().CreateBaseClassSpecifier(
          qt.getAsOpaquePtr(), TranslateMemberAccess(access),
          vtable_idx.has_value(), udt_cvt.kind() == LF_CLASS);
  if (!base_spec)
    return {};

  m_bases.push_back(
      std::make_pair(vtable_idx.value_or(0), std::move(base_spec)));

  return qt;
}

void UdtRecordCompleter::AddMethod(llvm::StringRef name, TypeIndex type_idx,
                                   MemberAccess access, MethodOptions options,
                                   MemberAttributes attrs) {
  clang::QualType method_qt =
      m_ast_builder.GetOrCreateType(PdbTypeSymId(type_idx));
  if (method_qt.isNull())
    return;
  CompilerType method_ct = m_ast_builder.ToCompilerType(method_qt);
  TypeSystemClang::RequireCompleteType(method_ct);
  lldb::opaque_compiler_type_t derived_opaque_ty =
      m_derived_ct.GetOpaqueQualType();
  auto iter = m_cxx_record_map.find(derived_opaque_ty);
  if (iter != m_cxx_record_map.end()) {
    if (iter->getSecond().contains({name, method_ct})) {
      return;
    }
  }

  lldb::AccessType access_type = TranslateMemberAccess(access);
  bool is_artificial = (options & MethodOptions::CompilerGenerated) ==
                       MethodOptions::CompilerGenerated;
  m_ast_builder.clang().AddMethodToCXXRecordType(
      derived_opaque_ty, name.data(), nullptr, method_ct,
      access_type, attrs.isVirtual(), attrs.isStatic(), false, false, false,
      is_artificial);

  m_cxx_record_map[derived_opaque_ty].insert({name, method_ct});
}

Error UdtRecordCompleter::visitKnownMember(CVMemberRecord &cvr,
                                           BaseClassRecord &base) {
  clang::QualType base_qt =
      AddBaseClassForTypeIndex(base.Type, base.getAccess());

  if (base_qt.isNull())
    return llvm::Error::success();
  auto decl =
      m_ast_builder.clang().GetAsCXXRecordDecl(base_qt.getAsOpaquePtr());
  lldbassert(decl);

  auto offset = clang::CharUnits::fromQuantity(base.getBaseOffset());
  m_layout.base_offsets.insert(std::make_pair(decl, offset));

  return llvm::Error::success();
}

Error UdtRecordCompleter::visitKnownMember(CVMemberRecord &cvr,
                                           VirtualBaseClassRecord &base) {
  AddBaseClassForTypeIndex(base.BaseType, base.getAccess(), base.VTableIndex);

  return Error::success();
}

Error UdtRecordCompleter::visitKnownMember(CVMemberRecord &cvr,
                                           ListContinuationRecord &cont) {
  return Error::success();
}

Error UdtRecordCompleter::visitKnownMember(CVMemberRecord &cvr,
                                           VFPtrRecord &vfptr) {
  return Error::success();
}

Error UdtRecordCompleter::visitKnownMember(
    CVMemberRecord &cvr, StaticDataMemberRecord &static_data_member) {
  clang::QualType member_type =
      m_ast_builder.GetOrCreateType(PdbTypeSymId(static_data_member.Type));
  if (member_type.isNull())
    return llvm::Error::success();

  CompilerType member_ct = m_ast_builder.ToCompilerType(member_type);

  lldb::AccessType access =
      TranslateMemberAccess(static_data_member.getAccess());
  auto decl = TypeSystemClang::AddVariableToRecordType(
      m_derived_ct, static_data_member.Name, member_ct, access);

  // Static constant members may be a const[expr] declaration.
  // Query the symbol's value as the variable initializer if valid.
  if (member_ct.IsConst() && member_ct.IsCompleteType()) {
    std::string qual_name = decl->getQualifiedNameAsString();

    auto results =
        m_index.globals().findRecordsByName(qual_name, m_index.symrecords());

    for (const auto &result : results) {
      if (result.second.kind() == SymbolKind::S_CONSTANT) {
        ConstantSym constant(SymbolRecordKind::ConstantSym);
        cantFail(SymbolDeserializer::deserializeAs<ConstantSym>(result.second,
                                                                constant));

        clang::QualType qual_type = decl->getType();
        unsigned type_width = decl->getASTContext().getIntWidth(qual_type);
        unsigned constant_width = constant.Value.getBitWidth();

        if (qual_type->isIntegralOrEnumerationType()) {
          if (type_width >= constant_width) {
            TypeSystemClang::SetIntegerInitializerForVariable(
                decl, constant.Value.extOrTrunc(type_width));
          } else {
            LLDB_LOG(GetLog(LLDBLog::AST),
                     "Class '{0}' has a member '{1}' of type '{2}' ({3} bits) "
                     "which resolves to a wider constant value ({4} bits). "
                     "Ignoring constant.",
                     m_derived_ct.GetTypeName(), static_data_member.Name,
                     member_ct.GetTypeName(), type_width, constant_width);
          }
        } else {
          lldb::BasicType basic_type_enum = member_ct.GetBasicTypeEnumeration();
          switch (basic_type_enum) {
          case lldb::eBasicTypeFloat:
          case lldb::eBasicTypeDouble:
          case lldb::eBasicTypeLongDouble:
            if (type_width == constant_width) {
              TypeSystemClang::SetFloatingInitializerForVariable(
                  decl, basic_type_enum == lldb::eBasicTypeFloat
                            ? llvm::APFloat(constant.Value.bitsToFloat())
                            : llvm::APFloat(constant.Value.bitsToDouble()));
              decl->setConstexpr(true);
            } else {
              LLDB_LOG(
                  GetLog(LLDBLog::AST),
                  "Class '{0}' has a member '{1}' of type '{2}' ({3} bits) "
                  "which resolves to a constant value of mismatched width "
                  "({4} bits). Ignoring constant.",
                  m_derived_ct.GetTypeName(), static_data_member.Name,
                  member_ct.GetTypeName(), type_width, constant_width);
            }
            break;
          default:
            break;
          }
        }
        break;
      }
    }
  }

  // FIXME: Add a PdbSymUid namespace for field list members and update
  // the m_uid_to_decl map with this decl.
  return Error::success();
}

Error UdtRecordCompleter::visitKnownMember(CVMemberRecord &cvr,
                                           NestedTypeRecord &nested) {
  return Error::success();
}

Error UdtRecordCompleter::visitKnownMember(CVMemberRecord &cvr,
                                           DataMemberRecord &data_member) {

  uint64_t offset = data_member.FieldOffset * 8;
  uint32_t bitfield_width = 0;

  TypeIndex ti(data_member.Type);
  if (!ti.isSimple()) {
    CVType cvt = m_index.tpi().getType(ti);
    if (cvt.kind() == LF_BITFIELD) {
      BitFieldRecord bfr;
      llvm::cantFail(TypeDeserializer::deserializeAs<BitFieldRecord>(cvt, bfr));
      offset += bfr.BitOffset;
      bitfield_width = bfr.BitSize;
      ti = bfr.Type;
    }
  }

  clang::QualType member_qt = m_ast_builder.GetOrCreateType(PdbTypeSymId(ti));
  if (member_qt.isNull())
    return Error::success();
  TypeSystemClang::RequireCompleteType(m_ast_builder.ToCompilerType(member_qt));
  lldb::AccessType access = TranslateMemberAccess(data_member.getAccess());
  size_t field_size =
      bitfield_width ? bitfield_width : GetSizeOfType(ti, m_index.tpi()) * 8;
  if (field_size == 0)
    return Error::success();
  m_record.CollectMember(data_member.Name, offset, field_size, member_qt, access,
                bitfield_width);
  return Error::success();
}

Error UdtRecordCompleter::visitKnownMember(CVMemberRecord &cvr,
                                           OneMethodRecord &one_method) {
  AddMethod(one_method.Name, one_method.Type, one_method.getAccess(),
            one_method.getOptions(), one_method.Attrs);

  return Error::success();
}

Error UdtRecordCompleter::visitKnownMember(CVMemberRecord &cvr,
                                           OverloadedMethodRecord &overloaded) {
  TypeIndex method_list_idx = overloaded.MethodList;

  CVType method_list_type = m_index.tpi().getType(method_list_idx);
  assert(method_list_type.kind() == LF_METHODLIST);

  MethodOverloadListRecord method_list;
  llvm::cantFail(TypeDeserializer::deserializeAs<MethodOverloadListRecord>(
      method_list_type, method_list));

  for (const OneMethodRecord &method : method_list.Methods)
    AddMethod(overloaded.Name, method.Type, method.getAccess(),
              method.getOptions(), method.Attrs);

  return Error::success();
}

Error UdtRecordCompleter::visitKnownMember(CVMemberRecord &cvr,
                                           EnumeratorRecord &enumerator) {
  Declaration decl;
  llvm::StringRef name = DropNameScope(enumerator.getName());

  m_ast_builder.clang().AddEnumerationValueToEnumerationType(
      m_derived_ct, decl, name.str().c_str(), enumerator.Value);
  return Error::success();
}

void UdtRecordCompleter::complete() {
  // Ensure the correct order for virtual bases.
  llvm::stable_sort(m_bases, llvm::less_first());

  std::vector<std::unique_ptr<clang::CXXBaseSpecifier>> bases;
  bases.reserve(m_bases.size());
  for (auto &ib : m_bases)
    bases.push_back(std::move(ib.second));

  TypeSystemClang &clang = m_ast_builder.clang();
  // Make sure all base classes refer to complete types and not forward
  // declarations. If we don't do this, clang will crash with an
  // assertion in the call to clang_type.TransferBaseClasses()
  for (const auto &base_class : bases) {
    clang::TypeSourceInfo *type_source_info =
        base_class->getTypeSourceInfo();
    if (type_source_info) {
      TypeSystemClang::RequireCompleteType(
          clang.GetType(type_source_info->getType()));
    }
  }

  clang.TransferBaseClasses(m_derived_ct.GetOpaqueQualType(), std::move(bases));

  clang.AddMethodOverridesForCXXRecordType(m_derived_ct.GetOpaqueQualType());
  FinishRecord();
  TypeSystemClang::BuildIndirectFields(m_derived_ct);
  TypeSystemClang::CompleteTagDeclarationDefinition(m_derived_ct);

  if (auto *record_decl = llvm::dyn_cast<clang::CXXRecordDecl>(&m_tag_decl)) {
    m_ast_builder.GetClangASTImporter().SetRecordLayout(record_decl, m_layout);
  }
}

uint64_t
UdtRecordCompleter::AddMember(TypeSystemClang &clang, Member *field,
                              uint64_t bit_offset, CompilerType parent_ct,
                              ClangASTImporter::LayoutInfo &parent_layout,
                              clang::DeclContext *parent_decl_ctx) {
  SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
      clang.GetSymbolFile()->GetBackingSymbolFile());
  clang::FieldDecl *field_decl = nullptr;
  uint64_t bit_size = 0;
  switch (field->kind) {
  case Member::Field: {
    field_decl = TypeSystemClang::AddFieldToRecordType(
        parent_ct, field->name, m_ast_builder.ToCompilerType(field->qt),
        field->access, field->bitfield_width);
    bit_size = field->bit_size;
    break;
  };
  case Member::Struct:
  case Member::Union: {
    clang::TagTypeKind kind = field->kind == Member::Struct
                                  ? clang::TagTypeKind::Struct
                                  : clang::TagTypeKind::Union;
    ClangASTMetadata metadata;
    metadata.SetUserID(pdb->anonymous_id);
    metadata.SetIsDynamicCXXType(false);
    CompilerType record_ct = clang.CreateRecordType(
        parent_decl_ctx, OptionalClangModuleID(), lldb::eAccessPublic, "",
        llvm::to_underlying(kind), lldb::eLanguageTypeC_plus_plus, &metadata);
    TypeSystemClang::StartTagDeclarationDefinition(record_ct);
    ClangASTImporter::LayoutInfo layout;
    clang::DeclContext *decl_ctx = clang.GetDeclContextForType(record_ct);
    for (const auto &member : field->fields) {
      uint64_t member_offset = field->kind == Member::Struct
                                   ? member->bit_offset - field->base_offset
                                   : 0;
      uint64_t member_bit_size = AddMember(clang, member.get(), member_offset,
                                          record_ct, layout, decl_ctx);
      if (field->kind == Member::Struct)
        bit_size = std::max(bit_size, member_offset + member_bit_size);
      else
        bit_size = std::max(bit_size, member_bit_size);
    }
    layout.bit_size = bit_size;
    TypeSystemClang::CompleteTagDeclarationDefinition(record_ct);
    clang::RecordDecl *record_decl = clang.GetAsRecordDecl(record_ct);
    m_ast_builder.GetClangASTImporter().SetRecordLayout(record_decl, layout);
    field_decl = TypeSystemClang::AddFieldToRecordType(
        parent_ct, "", record_ct, lldb::eAccessPublic, 0);
    // Mark this record decl as completed.
    DeclStatus status;
    status.resolved = true;
    status.uid = pdb->anonymous_id--;
    m_decl_to_status.insert({record_decl, status});
    break;
  };
  }
  // FIXME: Add a PdbSymUid namespace for field list members and update
  // the m_uid_to_decl map with this decl.
  parent_layout.field_offsets.insert({field_decl, bit_offset});
  return bit_size;
}

void UdtRecordCompleter::FinishRecord() {
  TypeSystemClang &clang = m_ast_builder.clang();
  clang::DeclContext *decl_ctx =
      m_ast_builder.GetOrCreateDeclContextForUid(m_id);
  m_record.ConstructRecord();
  // Maybe we should check the construsted record size with the size in pdb. If
  // they mismatch, it might be pdb has fields info missing.
  for (const auto &field : m_record.record.fields) {
    AddMember(clang, field.get(), field->bit_offset, m_derived_ct, m_layout,
             decl_ctx);
  }
}

void UdtRecordCompleter::Record::CollectMember(
    llvm::StringRef name, uint64_t offset, uint64_t field_size,
    clang::QualType qt, lldb::AccessType access, uint64_t bitfield_width) {
  fields_map[offset].push_back(std::make_unique<Member>(
      name, offset, field_size, qt, access, bitfield_width));
  if (start_offset > offset)
    start_offset = offset;
}

void UdtRecordCompleter::Record::ConstructRecord() {
  // For anonymous unions in a struct, msvc generated pdb doesn't have the
  // entity for that union. So, we need to construct anonymous union and struct
  // based on field offsets. The final AST is likely not matching the exact
  // original AST, but the memory layout is preseved.
  // After we collecting all fields in visitKnownMember, we have all fields in
  // increasing offset order in m_fields. Since we are iterating in increase
  // offset order, if the current offset is equal to m_start_offset, we insert
  // it as direct field of top level record. If the current offset is greater
  // than m_start_offset, we should be able to find a field in end_offset_map
  // whose end offset is less than or equal to current offset. (if not, it might
  // be missing field info. We will ignore the field in this case. e.g. Field A
  // starts at 0 with size 4 bytes, and Field B starts at 2 with size 4 bytes.
  // Normally, there must be something which ends at/before 2.) Then we will
  // append current field to the end of parent record. If parent is struct, we
  // can just grow it. If parent is a field, it's a field inside an union. We
  // convert it into an anonymous struct containing old field and new field.

  // The end offset to a vector of field/struct that ends at the offset.
  std::map<uint64_t, std::vector<Member *>> end_offset_map;
  for (auto &pair : fields_map) {
    uint64_t offset = pair.first;
    auto &fields = pair.second;
    lldbassert(offset >= start_offset);
    Member *parent = &record;
    if (offset > start_offset) {
      // Find the field with largest end offset that is <= offset. If it's less
      // than offset, it indicates there are padding bytes between end offset
      // and offset.
      lldbassert(!end_offset_map.empty());
      auto iter = end_offset_map.lower_bound(offset);
      if (iter == end_offset_map.end())
        --iter;
      else if (iter->first > offset) {
        if (iter == end_offset_map.begin())
          continue;
        --iter;
      }
      if (iter->second.empty())
        continue;
      parent = iter->second.back();
      iter->second.pop_back();
    }
    // If it's a field, then the field is inside a union, so we can safely
    // increase its size by converting it to a struct to hold multiple fields.
    if (parent->kind == Member::Field)
      parent->ConvertToStruct();

    if (fields.size() == 1) {
      uint64_t end_offset = offset + fields.back()->bit_size;
      parent->fields.push_back(std::move(fields.back()));
      if (parent->kind == Member::Struct) {
        end_offset_map[end_offset].push_back(parent);
      } else {
        lldbassert(parent == &record &&
                   "If parent is union, it must be the top level record.");
        end_offset_map[end_offset].push_back(parent->fields.back().get());
      }
    } else {
      if (parent->kind == Member::Struct) {
        parent->fields.push_back(std::make_unique<Member>(Member::Union));
        parent = parent->fields.back().get();
        parent->bit_offset = offset;
      } else {
        lldbassert(parent == &record &&
                   "If parent is union, it must be the top level record.");
      }
      for (auto &field : fields) {
        int64_t bit_size = field->bit_size;
        parent->fields.push_back(std::move(field));
        end_offset_map[offset + bit_size].push_back(
            parent->fields.back().get());
      }
    }
  }
}
