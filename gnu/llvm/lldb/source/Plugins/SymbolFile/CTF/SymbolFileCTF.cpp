//===-- SymbolFileCTF.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SymbolFileCTF.h"

#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Host/Config.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/Symtab.h"
#include "lldb/Symbol/TypeList.h"
#include "lldb/Symbol/TypeMap.h"
#include "lldb/Symbol/Variable.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegularExpression.h"
#include "lldb/Utility/StreamBuffer.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/Timer.h"
#include "llvm/Support/MemoryBuffer.h"

#include "Plugins/ExpressionParser/Clang/ClangASTMetadata.h"
#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"

#include <memory>
#include <optional>

#if LLVM_ENABLE_ZLIB
#include <zlib.h>
#endif

using namespace llvm;
using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE(SymbolFileCTF)

char SymbolFileCTF::ID;

SymbolFileCTF::SymbolFileCTF(lldb::ObjectFileSP objfile_sp)
    : SymbolFileCommon(std::move(objfile_sp)) {}

void SymbolFileCTF::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance);
}

void SymbolFileCTF::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

llvm::StringRef SymbolFileCTF::GetPluginDescriptionStatic() {
  return "Compact C Type Format Symbol Reader";
}

SymbolFile *SymbolFileCTF::CreateInstance(ObjectFileSP objfile_sp) {
  return new SymbolFileCTF(std::move(objfile_sp));
}

bool SymbolFileCTF::ParseHeader() {
  if (m_header)
    return true;

  Log *log = GetLog(LLDBLog::Symbols);

  ModuleSP module_sp(m_objfile_sp->GetModule());
  const SectionList *section_list = module_sp->GetSectionList();
  if (!section_list)
    return false;

  SectionSP section_sp(
      section_list->FindSectionByType(lldb::eSectionTypeCTF, true));
  if (!section_sp)
    return false;

  m_objfile_sp->ReadSectionData(section_sp.get(), m_data);

  if (m_data.GetByteSize() == 0)
    return false;

  StreamString module_desc;
  GetObjectFile()->GetModule()->GetDescription(module_desc.AsRawOstream(),
                                               lldb::eDescriptionLevelBrief);
  LLDB_LOG(log, "Parsing Compact C Type format for {0}", module_desc.GetData());

  lldb::offset_t offset = 0;

  // Parse CTF header.
  constexpr size_t ctf_header_size = sizeof(ctf_header_t);
  if (!m_data.ValidOffsetForDataOfSize(offset, ctf_header_size)) {
    LLDB_LOG(log, "CTF parsing failed: insufficient data for CTF header");
    return false;
  }

  m_header.emplace();

  ctf_header_t &ctf_header = *m_header;
  ctf_header.preamble.magic = m_data.GetU16(&offset);
  ctf_header.preamble.version = m_data.GetU8(&offset);
  ctf_header.preamble.flags = m_data.GetU8(&offset);
  ctf_header.parlabel = m_data.GetU32(&offset);
  ctf_header.parname = m_data.GetU32(&offset);
  ctf_header.lbloff = m_data.GetU32(&offset);
  ctf_header.objtoff = m_data.GetU32(&offset);
  ctf_header.funcoff = m_data.GetU32(&offset);
  ctf_header.typeoff = m_data.GetU32(&offset);
  ctf_header.stroff = m_data.GetU32(&offset);
  ctf_header.strlen = m_data.GetU32(&offset);

  // Validate the preamble.
  if (ctf_header.preamble.magic != g_ctf_magic) {
    LLDB_LOG(log, "CTF parsing failed: invalid magic: {0:x}",
             ctf_header.preamble.magic);
    return false;
  }

  if (ctf_header.preamble.version != g_ctf_version) {
    LLDB_LOG(log, "CTF parsing failed: unsupported version: {0}",
             ctf_header.preamble.version);
    return false;
  }

  LLDB_LOG(log, "Parsed valid CTF preamble: version {0}, flags {1:x}",
           ctf_header.preamble.version, ctf_header.preamble.flags);

  m_body_offset = offset;

  if (ctf_header.preamble.flags & eFlagCompress) {
    // The body has been compressed with zlib deflate. Header offsets point into
    // the decompressed data.
#if LLVM_ENABLE_ZLIB
    const std::size_t decompressed_size = ctf_header.stroff + ctf_header.strlen;
    DataBufferSP decompressed_data =
        std::make_shared<DataBufferHeap>(decompressed_size, 0x0);

    z_stream zstr;
    memset(&zstr, 0, sizeof(zstr));
    zstr.next_in = (Bytef *)const_cast<uint8_t *>(m_data.GetDataStart() +
                                                  sizeof(ctf_header_t));
    zstr.avail_in = m_data.BytesLeft(offset);
    zstr.next_out =
        (Bytef *)const_cast<uint8_t *>(decompressed_data->GetBytes());
    zstr.avail_out = decompressed_size;

    int rc = inflateInit(&zstr);
    if (rc != Z_OK) {
      LLDB_LOG(log, "CTF parsing failed: inflate initialization error: {0}",
               zError(rc));
      return false;
    }

    rc = inflate(&zstr, Z_FINISH);
    if (rc != Z_STREAM_END) {
      LLDB_LOG(log, "CTF parsing failed: inflate error: {0}", zError(rc));
      return false;
    }

    rc = inflateEnd(&zstr);
    if (rc != Z_OK) {
      LLDB_LOG(log, "CTF parsing failed: inflate end error: {0}", zError(rc));
      return false;
    }

    if (zstr.total_out != decompressed_size) {
      LLDB_LOG(log,
               "CTF parsing failed: decompressed size ({0}) doesn't match "
               "expected size ([1})",
               zstr.total_out, decompressed_size);
      return false;
    }

    m_data = DataExtractor(decompressed_data, m_data.GetByteOrder(),
                           m_data.GetAddressByteSize());
    m_body_offset = 0;
#else
    LLDB_LOG(
        log,
        "CTF parsing failed: data is compressed but no zlib inflate support");
    return false;
#endif
  }

  // Validate the header.
  if (!m_data.ValidOffset(m_body_offset + ctf_header.lbloff)) {
    LLDB_LOG(log,
             "CTF parsing failed: invalid label section offset in header: {0}",
             ctf_header.lbloff);
    return false;
  }

  if (!m_data.ValidOffset(m_body_offset + ctf_header.objtoff)) {
    LLDB_LOG(log,
             "CTF parsing failed: invalid object section offset in header: {0}",
             ctf_header.objtoff);
    return false;
  }

  if (!m_data.ValidOffset(m_body_offset + ctf_header.funcoff)) {
    LLDB_LOG(
        log,
        "CTF parsing failed: invalid function section offset in header: {0}",
        ctf_header.funcoff);
    return false;
  }

  if (!m_data.ValidOffset(m_body_offset + ctf_header.typeoff)) {
    LLDB_LOG(log,
             "CTF parsing failed: invalid type section offset in header: {0}",
             ctf_header.typeoff);
    return false;
  }

  if (!m_data.ValidOffset(m_body_offset + ctf_header.stroff)) {
    LLDB_LOG(log,
             "CTF parsing failed: invalid string section offset in header: {0}",
             ctf_header.stroff);
    return false;
  }

  const lldb::offset_t str_end_offset =
      m_body_offset + ctf_header.stroff + ctf_header.strlen;
  if (!m_data.ValidOffset(str_end_offset - 1)) {
    LLDB_LOG(log,
             "CTF parsing failed: invalid string section length in header: {0}",
             ctf_header.strlen);
    return false;
  }

  if (m_body_offset + ctf_header.stroff + ctf_header.parlabel >
      str_end_offset) {
    LLDB_LOG(log,
             "CTF parsing failed: invalid parent label offset: {0} exceeds end "
             "of string section ({1})",
             ctf_header.parlabel, str_end_offset);
    return false;
  }

  if (m_body_offset + ctf_header.stroff + ctf_header.parname > str_end_offset) {
    LLDB_LOG(log,
             "CTF parsing failed: invalid parent name offset: {0} exceeds end "
             "of string section ({1})",
             ctf_header.parname, str_end_offset);
    return false;
  }

  LLDB_LOG(log,
           "Parsed valid CTF header: lbloff  = {0}, objtoff = {1}, funcoff = "
           "{2}, typeoff = {3}, stroff = {4}, strlen = {5}",
           ctf_header.lbloff, ctf_header.objtoff, ctf_header.funcoff,
           ctf_header.typeoff, ctf_header.stroff, ctf_header.strlen);

  return true;
}

void SymbolFileCTF::InitializeObject() {
  Log *log = GetLog(LLDBLog::Symbols);

  auto type_system_or_err = GetTypeSystemForLanguage(lldb::eLanguageTypeC);
  if (auto err = type_system_or_err.takeError()) {
    LLDB_LOG_ERROR(log, std::move(err), "Unable to get type system: {0}");
    return;
  }

  auto ts = *type_system_or_err;
  m_ast = llvm::dyn_cast_or_null<TypeSystemClang>(ts.get());
  LazyBool optimized = eLazyBoolNo;
  m_comp_unit_sp = std::make_shared<CompileUnit>(
      m_objfile_sp->GetModule(), nullptr, "", 0, eLanguageTypeC, optimized);

  ParseTypes(*m_comp_unit_sp);
}

llvm::StringRef SymbolFileCTF::ReadString(lldb::offset_t str_offset) const {
  lldb::offset_t offset = m_body_offset + m_header->stroff + str_offset;
  if (!m_data.ValidOffset(offset))
    return "(invalid)";
  const char *str = m_data.GetCStr(&offset);
  if (str && !*str)
    return "(anon)";
  return llvm::StringRef(str);
}

/// Return the integer display representation encoded in the given data.
static uint32_t GetEncoding(uint32_t data) {
  // Mask bits 24–31.
  return ((data)&0xff000000) >> 24;
}

/// Return the integral width in bits encoded in the given data.
static uint32_t GetBits(uint32_t data) {
  // Mask bits 0-15.
  return (data)&0x0000ffff;
}

/// Return the type kind encoded in the given data.
uint32_t GetKind(uint32_t data) {
  // Mask bits 26–31.
  return ((data)&0xf800) >> 11;
}

/// Return the variable length encoded in the given data.
uint32_t GetVLen(uint32_t data) {
  // Mask bits 0–24.
  return (data)&0x3ff;
}

static uint32_t GetBytes(uint32_t bits) { return bits / sizeof(unsigned); }

static clang::TagTypeKind TranslateRecordKind(CTFType::Kind type) {
  switch (type) {
  case CTFType::Kind::eStruct:
    return clang::TagTypeKind::Struct;
  case CTFType::Kind::eUnion:
    return clang::TagTypeKind::Union;
  default:
    lldbassert(false && "Invalid record kind!");
    return clang::TagTypeKind::Struct;
  }
}

llvm::Expected<TypeSP>
SymbolFileCTF::CreateInteger(const CTFInteger &ctf_integer) {
  lldb::BasicType basic_type =
      TypeSystemClang::GetBasicTypeEnumeration(ctf_integer.name);
  if (basic_type == eBasicTypeInvalid)
    return llvm::make_error<llvm::StringError>(
        llvm::formatv("unsupported integer type: no corresponding basic clang "
                      "type for '{0}'",
                      ctf_integer.name),
        llvm::inconvertibleErrorCode());

  CompilerType compiler_type = m_ast->GetBasicType(basic_type);

  if (basic_type != eBasicTypeVoid && basic_type != eBasicTypeBool) {
    // Make sure the type we got is an integer type.
    bool compiler_type_is_signed = false;
    if (!compiler_type.IsIntegerType(compiler_type_is_signed))
      return llvm::make_error<llvm::StringError>(
          llvm::formatv(
              "Found compiler type for '{0}' but it's not an integer type: {1}",
              ctf_integer.name,
              compiler_type.GetDisplayTypeName().GetStringRef()),
          llvm::inconvertibleErrorCode());

    // Make sure the signing matches between the CTF and the compiler type.
    const bool type_is_signed = (ctf_integer.encoding & IntEncoding::eSigned);
    if (compiler_type_is_signed != type_is_signed)
      return llvm::make_error<llvm::StringError>(
          llvm::formatv("Found integer compiler type for {0} but compiler type "
                        "is {1} and {0} is {2}",
                        ctf_integer.name,
                        compiler_type_is_signed ? "signed" : "unsigned",
                        type_is_signed ? "signed" : "unsigned"),
          llvm::inconvertibleErrorCode());
  }

  Declaration decl;
  return MakeType(ctf_integer.uid, ConstString(ctf_integer.name),
                  GetBytes(ctf_integer.bits), nullptr, LLDB_INVALID_UID,
                  lldb_private::Type::eEncodingIsUID, decl, compiler_type,
                  lldb_private::Type::ResolveState::Full);
}

llvm::Expected<lldb::TypeSP>
SymbolFileCTF::CreateModifier(const CTFModifier &ctf_modifier) {
  Type *ref_type = ResolveTypeUID(ctf_modifier.type);
  if (!ref_type)
    return llvm::make_error<llvm::StringError>(
        llvm::formatv("Could not find modified type: {0}", ctf_modifier.type),
        llvm::inconvertibleErrorCode());

  CompilerType compiler_type;

  switch (ctf_modifier.kind) {
  case CTFType::ePointer:
    compiler_type = ref_type->GetFullCompilerType().GetPointerType();
    break;
  case CTFType::eConst:
    compiler_type = ref_type->GetFullCompilerType().AddConstModifier();
    break;
  case CTFType::eVolatile:
    compiler_type = ref_type->GetFullCompilerType().AddVolatileModifier();
    break;
  case CTFType::eRestrict:
    compiler_type = ref_type->GetFullCompilerType().AddRestrictModifier();
    break;
  default:
    return llvm::make_error<llvm::StringError>(
        llvm::formatv("ParseModifier called with unsupported kind: {0}",
                      ctf_modifier.kind),
        llvm::inconvertibleErrorCode());
  }

  Declaration decl;
  return MakeType(ctf_modifier.uid, ConstString(), 0, nullptr, LLDB_INVALID_UID,
                  Type::eEncodingIsUID, decl, compiler_type,
                  lldb_private::Type::ResolveState::Full);
}

llvm::Expected<lldb::TypeSP>
SymbolFileCTF::CreateTypedef(const CTFTypedef &ctf_typedef) {
  Type *underlying_type = ResolveTypeUID(ctf_typedef.type);
  if (!underlying_type)
    return llvm::make_error<llvm::StringError>(
        llvm::formatv("Could not find typedef underlying type: {0}",
                      ctf_typedef.type),
        llvm::inconvertibleErrorCode());

  CompilerType target_ast_type = underlying_type->GetFullCompilerType();
  clang::DeclContext *decl_ctx = m_ast->GetTranslationUnitDecl();
  CompilerType ast_typedef = target_ast_type.CreateTypedef(
      ctf_typedef.name.data(), m_ast->CreateDeclContext(decl_ctx), 0);

  Declaration decl;
  return MakeType(ctf_typedef.uid, ConstString(ctf_typedef.name), 0, nullptr,
                  LLDB_INVALID_UID, lldb_private::Type::eEncodingIsUID, decl,
                  ast_typedef, lldb_private::Type::ResolveState::Full);
}

llvm::Expected<lldb::TypeSP>
SymbolFileCTF::CreateArray(const CTFArray &ctf_array) {
  Type *element_type = ResolveTypeUID(ctf_array.type);
  if (!element_type)
    return llvm::make_error<llvm::StringError>(
        llvm::formatv("Could not find array element type: {0}", ctf_array.type),
        llvm::inconvertibleErrorCode());

  std::optional<uint64_t> element_size = element_type->GetByteSize(nullptr);
  if (!element_size)
    return llvm::make_error<llvm::StringError>(
        llvm::formatv("could not get element size of type: {0}",
                      ctf_array.type),
        llvm::inconvertibleErrorCode());

  uint64_t size = ctf_array.nelems * *element_size;

  CompilerType compiler_type = m_ast->CreateArrayType(
      element_type->GetFullCompilerType(), ctf_array.nelems,
      /*is_gnu_vector*/ false);

  Declaration decl;
  return MakeType(ctf_array.uid, ConstString(), size, nullptr, LLDB_INVALID_UID,
                  Type::eEncodingIsUID, decl, compiler_type,
                  lldb_private::Type::ResolveState::Full);
}

llvm::Expected<lldb::TypeSP>
SymbolFileCTF::CreateEnum(const CTFEnum &ctf_enum) {
  Declaration decl;
  CompilerType enum_type = m_ast->CreateEnumerationType(
      ctf_enum.name, m_ast->GetTranslationUnitDecl(), OptionalClangModuleID(),
      decl, m_ast->GetBasicType(eBasicTypeInt),
      /*is_scoped=*/false);

  for (const CTFEnum::Value &value : ctf_enum.values) {
    Declaration value_decl;
    m_ast->AddEnumerationValueToEnumerationType(
        enum_type, value_decl, value.name.data(), value.value, ctf_enum.size);
  }
  TypeSystemClang::CompleteTagDeclarationDefinition(enum_type);

  return MakeType(ctf_enum.uid, ConstString(), 0, nullptr, LLDB_INVALID_UID,
                  Type::eEncodingIsUID, decl, enum_type,
                  lldb_private::Type::ResolveState::Full);
}

llvm::Expected<lldb::TypeSP>
SymbolFileCTF::CreateFunction(const CTFFunction &ctf_function) {
  std::vector<CompilerType> arg_types;
  for (uint32_t arg : ctf_function.args) {
    if (Type *arg_type = ResolveTypeUID(arg))
      arg_types.push_back(arg_type->GetFullCompilerType());
  }

  Type *ret_type = ResolveTypeUID(ctf_function.return_type);
  if (!ret_type)
    return llvm::make_error<llvm::StringError>(
        llvm::formatv("Could not find function return type: {0}",
                      ctf_function.return_type),
        llvm::inconvertibleErrorCode());

  CompilerType func_type = m_ast->CreateFunctionType(
      ret_type->GetFullCompilerType(), arg_types.data(), arg_types.size(),
      ctf_function.variadic, 0, clang::CallingConv::CC_C);

  Declaration decl;
  return MakeType(ctf_function.uid, ConstString(ctf_function.name), 0, nullptr,
                  LLDB_INVALID_UID, Type::eEncodingIsUID, decl, func_type,
                  lldb_private::Type::ResolveState::Full);
}

llvm::Expected<lldb::TypeSP>
SymbolFileCTF::CreateRecord(const CTFRecord &ctf_record) {
  const clang::TagTypeKind tag_kind = TranslateRecordKind(ctf_record.kind);
  CompilerType record_type = m_ast->CreateRecordType(
      nullptr, OptionalClangModuleID(), eAccessPublic, ctf_record.name.data(),
      llvm::to_underlying(tag_kind), eLanguageTypeC);
  m_compiler_types[record_type.GetOpaqueQualType()] = &ctf_record;
  Declaration decl;
  return MakeType(ctf_record.uid, ConstString(ctf_record.name), ctf_record.size,
                  nullptr, LLDB_INVALID_UID, lldb_private::Type::eEncodingIsUID,
                  decl, record_type, lldb_private::Type::ResolveState::Forward);
}

bool SymbolFileCTF::CompleteType(CompilerType &compiler_type) {
  // Check if we have a CTF type for the given incomplete compiler type.
  auto it = m_compiler_types.find(compiler_type.GetOpaqueQualType());
  if (it == m_compiler_types.end())
    return false;

  const CTFType *ctf_type = it->second;
  assert(ctf_type && "m_compiler_types should only contain valid CTF types");

  // We only support resolving record types.
  assert(llvm::isa<CTFRecord>(ctf_type));

  // Cast to the appropriate CTF type.
  const CTFRecord *ctf_record = static_cast<const CTFRecord *>(ctf_type);

  // If any of the fields are incomplete, we cannot complete the type.
  for (const CTFRecord::Field &field : ctf_record->fields) {
    if (!ResolveTypeUID(field.type)) {
      LLDB_LOG(GetLog(LLDBLog::Symbols),
               "Cannot complete type {0} because field {1} is incomplete",
               ctf_type->uid, field.type);
      return false;
    }
  }

  // Complete the record type.
  m_ast->StartTagDeclarationDefinition(compiler_type);
  for (const CTFRecord::Field &field : ctf_record->fields) {
    Type *field_type = ResolveTypeUID(field.type);
    assert(field_type && "field must be complete");
    const uint32_t field_size = field_type->GetByteSize(nullptr).value_or(0);
    TypeSystemClang::AddFieldToRecordType(compiler_type, field.name,
                                          field_type->GetFullCompilerType(),
                                          eAccessPublic, field_size);
  }
  m_ast->CompleteTagDeclarationDefinition(compiler_type);

  // Now that the compiler type is complete, we don't need to remember it
  // anymore and can remove the CTF record type.
  m_compiler_types.erase(compiler_type.GetOpaqueQualType());
  m_ctf_types.erase(ctf_type->uid);

  return true;
}

llvm::Expected<lldb::TypeSP>
SymbolFileCTF::CreateForward(const CTFForward &ctf_forward) {
  CompilerType forward_compiler_type = m_ast->CreateRecordType(
      nullptr, OptionalClangModuleID(), eAccessPublic, ctf_forward.name,
      llvm::to_underlying(clang::TagTypeKind::Struct), eLanguageTypeC);
  Declaration decl;
  return MakeType(ctf_forward.uid, ConstString(ctf_forward.name), 0, nullptr,
                  LLDB_INVALID_UID, Type::eEncodingIsUID, decl,
                  forward_compiler_type, Type::ResolveState::Forward);
}

llvm::Expected<TypeSP> SymbolFileCTF::CreateType(CTFType *ctf_type) {
  if (!ctf_type)
    return llvm::make_error<llvm::StringError>(
        "cannot create type for unparsed type", llvm::inconvertibleErrorCode());

  switch (ctf_type->kind) {
  case CTFType::Kind::eInteger:
    return CreateInteger(*static_cast<CTFInteger *>(ctf_type));
  case CTFType::Kind::eConst:
  case CTFType::Kind::ePointer:
  case CTFType::Kind::eRestrict:
  case CTFType::Kind::eVolatile:
    return CreateModifier(*static_cast<CTFModifier *>(ctf_type));
  case CTFType::Kind::eTypedef:
    return CreateTypedef(*static_cast<CTFTypedef *>(ctf_type));
  case CTFType::Kind::eArray:
    return CreateArray(*static_cast<CTFArray *>(ctf_type));
  case CTFType::Kind::eEnum:
    return CreateEnum(*static_cast<CTFEnum *>(ctf_type));
  case CTFType::Kind::eFunction:
    return CreateFunction(*static_cast<CTFFunction *>(ctf_type));
  case CTFType::Kind::eStruct:
  case CTFType::Kind::eUnion:
    return CreateRecord(*static_cast<CTFRecord *>(ctf_type));
  case CTFType::Kind::eForward:
    return CreateForward(*static_cast<CTFForward *>(ctf_type));
  case CTFType::Kind::eUnknown:
  case CTFType::Kind::eFloat:
  case CTFType::Kind::eSlice:
    return llvm::make_error<llvm::StringError>(
        llvm::formatv("unsupported type (uid = {0}, name = {1}, kind = {2})",
                      ctf_type->uid, ctf_type->name, ctf_type->kind),
        llvm::inconvertibleErrorCode());
  }
  llvm_unreachable("Unexpected CTF type kind");
}

llvm::Expected<std::unique_ptr<CTFType>>
SymbolFileCTF::ParseType(lldb::offset_t &offset, lldb::user_id_t uid) {
  ctf_stype_t ctf_stype;
  ctf_stype.name = m_data.GetU32(&offset);
  ctf_stype.info = m_data.GetU32(&offset);
  ctf_stype.size = m_data.GetU32(&offset);

  llvm::StringRef name = ReadString(ctf_stype.name);
  const uint32_t kind = GetKind(ctf_stype.info);
  const uint32_t variable_length = GetVLen(ctf_stype.info);
  const uint32_t type = ctf_stype.GetType();
  const uint32_t size = ctf_stype.GetSize();

  switch (kind) {
  case TypeKind::eInteger: {
    const uint32_t vdata = m_data.GetU32(&offset);
    const uint32_t bits = GetBits(vdata);
    const uint32_t encoding = GetEncoding(vdata);
    return std::make_unique<CTFInteger>(uid, name, bits, encoding);
  }
  case TypeKind::eConst:
    return std::make_unique<CTFConst>(uid, type);
  case TypeKind::ePointer:
    return std::make_unique<CTFPointer>(uid, type);
  case TypeKind::eRestrict:
    return std::make_unique<CTFRestrict>(uid, type);
  case TypeKind::eVolatile:
    return std::make_unique<CTFVolatile>(uid, type);
  case TypeKind::eTypedef:
    return std::make_unique<CTFTypedef>(uid, name, type);
  case TypeKind::eArray: {
    const uint32_t type = m_data.GetU32(&offset);
    const uint32_t index = m_data.GetU32(&offset);
    const uint32_t nelems = m_data.GetU32(&offset);
    return std::make_unique<CTFArray>(uid, name, type, index, nelems);
  }
  case TypeKind::eEnum: {
    std::vector<CTFEnum::Value> values;
    for (uint32_t i = 0; i < variable_length; ++i) {
      const uint32_t value_name = m_data.GetU32(&offset);
      const uint32_t value = m_data.GetU32(&offset);
      values.emplace_back(ReadString(value_name), value);
    }
    return std::make_unique<CTFEnum>(uid, name, variable_length, size, values);
  }
  case TypeKind::eFunction: {
    std::vector<uint32_t> args;
    bool variadic = false;
    for (uint32_t i = 0; i < variable_length; ++i) {
      const uint32_t arg_uid = m_data.GetU32(&offset);
      // If the last argument is 0, this is a variadic function.
      if (arg_uid == 0) {
        variadic = true;
        break;
      }
      args.push_back(arg_uid);
    }
    // If the number of arguments is odd, a single uint32_t of padding is
    // inserted to maintain alignment.
    if (variable_length % 2 == 1)
      m_data.GetU32(&offset);
    return std::make_unique<CTFFunction>(uid, name, variable_length, type, args,
                                         variadic);
  }
  case TypeKind::eStruct:
  case TypeKind::eUnion: {
    std::vector<CTFRecord::Field> fields;
    for (uint32_t i = 0; i < variable_length; ++i) {
      const uint32_t field_name = m_data.GetU32(&offset);
      const uint32_t type = m_data.GetU32(&offset);
      uint64_t field_offset = 0;
      if (size < g_ctf_field_threshold) {
        field_offset = m_data.GetU16(&offset);
        m_data.GetU16(&offset); // Padding
      } else {
        const uint32_t offset_hi = m_data.GetU32(&offset);
        const uint32_t offset_lo = m_data.GetU32(&offset);
        field_offset = (((uint64_t)offset_hi) << 32) | ((uint64_t)offset_lo);
      }
      fields.emplace_back(ReadString(field_name), type, field_offset);
    }
    return std::make_unique<CTFRecord>(static_cast<CTFType::Kind>(kind), uid,
                                       name, variable_length, size, fields);
  }
  case TypeKind::eForward:
    return std::make_unique<CTFForward>(uid, name);
  case TypeKind::eUnknown:
    return std::make_unique<CTFType>(static_cast<CTFType::Kind>(kind), uid,
                                     name);
  case TypeKind::eFloat:
  case TypeKind::eSlice:
    offset += (variable_length * sizeof(uint32_t));
    break;
  }

  return llvm::make_error<llvm::StringError>(
      llvm::formatv("unsupported type (name = {0}, kind = {1}, vlength = {2})",
                    name, kind, variable_length),
      llvm::inconvertibleErrorCode());
}

size_t SymbolFileCTF::ParseTypes(CompileUnit &cu) {
  if (!ParseHeader())
    return 0;

  if (!m_types.empty())
    return 0;

  if (!m_ast)
    return 0;

  Log *log = GetLog(LLDBLog::Symbols);
  LLDB_LOG(log, "Parsing CTF types");

  lldb::offset_t type_offset = m_body_offset + m_header->typeoff;
  const lldb::offset_t type_offset_end = m_body_offset + m_header->stroff;

  lldb::user_id_t type_uid = 1;
  while (type_offset < type_offset_end) {
    llvm::Expected<std::unique_ptr<CTFType>> type_or_error =
        ParseType(type_offset, type_uid);
    if (type_or_error) {
      m_ctf_types[(*type_or_error)->uid] = std::move(*type_or_error);
    } else {
      LLDB_LOG_ERROR(log, type_or_error.takeError(),
                     "Failed to parse type {1} at offset {2}: {0}", type_uid,
                     type_offset);
    }
    type_uid++;
  }

  LLDB_LOG(log, "Parsed {0} CTF types", m_ctf_types.size());

  for (lldb::user_id_t uid = 1; uid < type_uid; ++uid)
    ResolveTypeUID(uid);

  LLDB_LOG(log, "Created {0} CTF types", m_types.size());

  return m_types.size();
}

size_t SymbolFileCTF::ParseFunctions(CompileUnit &cu) {
  if (!ParseHeader())
    return 0;

  if (!m_functions.empty())
    return 0;

  if (!m_ast)
    return 0;

  Symtab *symtab = GetObjectFile()->GetModule()->GetSymtab();
  if (!symtab)
    return 0;

  Log *log = GetLog(LLDBLog::Symbols);
  LLDB_LOG(log, "Parsing CTF functions");

  lldb::offset_t function_offset = m_body_offset + m_header->funcoff;
  const lldb::offset_t function_offset_end = m_body_offset + m_header->typeoff;

  uint32_t symbol_idx = 0;
  Declaration decl;
  while (function_offset < function_offset_end) {
    const uint32_t info = m_data.GetU32(&function_offset);
    const uint16_t kind = GetKind(info);
    const uint16_t variable_length = GetVLen(info);

    Symbol *symbol = symtab->FindSymbolWithType(
        eSymbolTypeCode, Symtab::eDebugYes, Symtab::eVisibilityAny, symbol_idx);

    // Skip padding.
    if (kind == TypeKind::eUnknown && variable_length == 0)
      continue;

    // Skip unexpected kinds.
    if (kind != TypeKind::eFunction)
      continue;

    const uint32_t ret_uid = m_data.GetU32(&function_offset);
    const uint32_t num_args = variable_length;

    std::vector<CompilerType> arg_types;
    arg_types.reserve(num_args);

    bool is_variadic = false;
    for (uint32_t i = 0; i < variable_length; i++) {
      const uint32_t arg_uid = m_data.GetU32(&function_offset);

      // If the last argument is 0, this is a variadic function.
      if (arg_uid == 0) {
        is_variadic = true;
        break;
      }

      Type *arg_type = ResolveTypeUID(arg_uid);
      arg_types.push_back(arg_type ? arg_type->GetFullCompilerType()
                                   : CompilerType());
    }

    if (symbol) {
      Type *ret_type = ResolveTypeUID(ret_uid);
      AddressRange func_range =
          AddressRange(symbol->GetFileAddress(), symbol->GetByteSize(),
                       GetObjectFile()->GetModule()->GetSectionList());

      // Create function type.
      CompilerType func_type = m_ast->CreateFunctionType(
          ret_type ? ret_type->GetFullCompilerType() : CompilerType(),
          arg_types.data(), arg_types.size(), is_variadic, 0,
          clang::CallingConv::CC_C);
      lldb::user_id_t function_type_uid = m_types.size() + 1;
      TypeSP type_sp =
          MakeType(function_type_uid, symbol->GetName(), 0, nullptr,
                   LLDB_INVALID_UID, Type::eEncodingIsUID, decl, func_type,
                   lldb_private::Type::ResolveState::Full);
      m_types[function_type_uid] = type_sp;

      // Create function.
      lldb::user_id_t func_uid = m_functions.size();
      FunctionSP function_sp = std::make_shared<Function>(
          &cu, func_uid, function_type_uid, symbol->GetMangled(), type_sp.get(),
          func_range);
      m_functions.emplace_back(function_sp);
      cu.AddFunction(function_sp);
    }
  }

  LLDB_LOG(log, "CTF parsed {0} functions", m_functions.size());

  return m_functions.size();
}

static DWARFExpression CreateDWARFExpression(ModuleSP module_sp,
                                             const Symbol &symbol) {
  if (!module_sp)
    return DWARFExpression();

  const ArchSpec &architecture = module_sp->GetArchitecture();
  ByteOrder byte_order = architecture.GetByteOrder();
  uint32_t address_size = architecture.GetAddressByteSize();
  uint32_t byte_size = architecture.GetDataByteSize();

  StreamBuffer<32> stream(Stream::eBinary, address_size, byte_order);
  stream.PutHex8(lldb_private::dwarf::DW_OP_addr);
  stream.PutMaxHex64(symbol.GetFileAddress(), address_size, byte_order);

  DataBufferSP buffer =
      std::make_shared<DataBufferHeap>(stream.GetData(), stream.GetSize());
  lldb_private::DataExtractor extractor(buffer, byte_order, address_size,
                                        byte_size);
  DWARFExpression result(extractor);
  result.SetRegisterKind(eRegisterKindDWARF);

  return result;
}

size_t SymbolFileCTF::ParseObjects(CompileUnit &comp_unit) {
  if (!ParseHeader())
    return 0;

  if (!m_variables.empty())
    return 0;

  if (!m_ast)
    return 0;

  ModuleSP module_sp = GetObjectFile()->GetModule();
  Symtab *symtab = module_sp->GetSymtab();
  if (!symtab)
    return 0;

  Log *log = GetLog(LLDBLog::Symbols);
  LLDB_LOG(log, "Parsing CTF objects");

  lldb::offset_t object_offset = m_body_offset + m_header->objtoff;
  const lldb::offset_t object_offset_end = m_body_offset + m_header->funcoff;

  uint32_t symbol_idx = 0;
  Declaration decl;
  while (object_offset < object_offset_end) {
    const uint32_t type_uid = m_data.GetU32(&object_offset);

    if (Symbol *symbol =
            symtab->FindSymbolWithType(eSymbolTypeData, Symtab::eDebugYes,
                                       Symtab::eVisibilityAny, symbol_idx)) {
      Variable::RangeList ranges;
      ranges.Append(symbol->GetFileAddress(), symbol->GetByteSize());

      auto type_sp = std::make_shared<SymbolFileType>(*this, type_uid);

      DWARFExpressionList location(
          module_sp, CreateDWARFExpression(module_sp, *symbol), nullptr);

      lldb::user_id_t variable_type_uid = m_variables.size();
      m_variables.emplace_back(std::make_shared<Variable>(
          variable_type_uid, symbol->GetName().AsCString(),
          symbol->GetName().AsCString(), type_sp, eValueTypeVariableGlobal,
          m_comp_unit_sp.get(), ranges, &decl, location, symbol->IsExternal(),
          /*artificial=*/false,
          /*location_is_constant_data*/ false));
    }
  }

  LLDB_LOG(log, "Parsed {0} CTF objects", m_variables.size());

  return m_variables.size();
}

uint32_t SymbolFileCTF::CalculateAbilities() {
  if (!m_objfile_sp)
    return 0;

  if (!ParseHeader())
    return 0;

  return VariableTypes | Functions | GlobalVariables;
}

uint32_t SymbolFileCTF::ResolveSymbolContext(const Address &so_addr,
                                             SymbolContextItem resolve_scope,
                                             SymbolContext &sc) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  if (m_objfile_sp->GetSymtab() == nullptr)
    return 0;

  uint32_t resolved_flags = 0;

  // Resolve symbols.
  if (resolve_scope & eSymbolContextSymbol) {
    sc.symbol = m_objfile_sp->GetSymtab()->FindSymbolContainingFileAddress(
        so_addr.GetFileAddress());
    if (sc.symbol)
      resolved_flags |= eSymbolContextSymbol;
  }

  // Resolve functions.
  if (resolve_scope & eSymbolContextFunction) {
    for (FunctionSP function_sp : m_functions) {
      if (function_sp->GetAddressRange().ContainsFileAddress(
              so_addr.GetFileAddress())) {
        sc.function = function_sp.get();
        resolved_flags |= eSymbolContextFunction;
        break;
      }
    }
  }

  // Resolve variables.
  if (resolve_scope & eSymbolContextVariable) {
    for (VariableSP variable_sp : m_variables) {
      if (variable_sp->LocationIsValidForAddress(so_addr.GetFileAddress())) {
        sc.variable = variable_sp.get();
        break;
      }
    }
  }

  return resolved_flags;
}

CompUnitSP SymbolFileCTF::ParseCompileUnitAtIndex(uint32_t idx) {
  if (idx == 0)
    return m_comp_unit_sp;
  return {};
}

size_t
SymbolFileCTF::ParseVariablesForContext(const lldb_private::SymbolContext &sc) {
  return ParseObjects(*m_comp_unit_sp);
}

void SymbolFileCTF::AddSymbols(Symtab &symtab) {
  // CTF does not encode symbols.
  // We rely on the existing symbol table to map symbols to type.
}

lldb_private::Type *SymbolFileCTF::ResolveTypeUID(lldb::user_id_t type_uid) {
  auto type_it = m_types.find(type_uid);
  if (type_it != m_types.end())
    return type_it->second.get();

  auto ctf_type_it = m_ctf_types.find(type_uid);
  if (ctf_type_it == m_ctf_types.end())
    return nullptr;

  CTFType *ctf_type = ctf_type_it->second.get();
  assert(ctf_type && "m_ctf_types should only contain valid CTF types");

  Log *log = GetLog(LLDBLog::Symbols);

  llvm::Expected<TypeSP> type_or_error = CreateType(ctf_type);
  if (!type_or_error) {
    LLDB_LOG_ERROR(log, type_or_error.takeError(),
                   "Failed to create type for {1}: {0}", ctf_type->uid);
    return {};
  }

  TypeSP type_sp = *type_or_error;

  if (log) {
    StreamString ss;
    type_sp->Dump(&ss, true);
    LLDB_LOGV(log, "Adding type {0}: {1}", type_sp->GetID(),
              llvm::StringRef(ss.GetString()).rtrim());
  }

  m_types[type_uid] = type_sp;

  // Except for record types which we'll need to complete later, we don't need
  // the CTF type anymore.
  if (!isa<CTFRecord>(ctf_type))
    m_ctf_types.erase(type_uid);

  return type_sp.get();
}

void SymbolFileCTF::FindTypes(const lldb_private::TypeQuery &match,
                              lldb_private::TypeResults &results) {
  // Make sure we haven't already searched this SymbolFile before.
  if (results.AlreadySearched(this))
    return;

  ConstString name = match.GetTypeBasename();
  for (TypeSP type_sp : GetTypeList().Types()) {
    if (type_sp && type_sp->GetName() == name) {
      results.InsertUnique(type_sp);
      if (results.Done(match))
        return;
    }
  }
}

void SymbolFileCTF::FindTypesByRegex(
    const lldb_private::RegularExpression &regex, uint32_t max_matches,
    lldb_private::TypeMap &types) {
  ParseTypes(*m_comp_unit_sp);

  size_t matches = 0;
  for (TypeSP type_sp : GetTypeList().Types()) {
    if (matches == max_matches)
      break;
    if (type_sp && regex.Execute(type_sp->GetName()))
      types.Insert(type_sp);
    matches++;
  }
}

void SymbolFileCTF::FindFunctions(
    const lldb_private::Module::LookupInfo &lookup_info,
    const lldb_private::CompilerDeclContext &parent_decl_ctx,
    bool include_inlines, lldb_private::SymbolContextList &sc_list) {
  ParseFunctions(*m_comp_unit_sp);

  ConstString name = lookup_info.GetLookupName();
  for (FunctionSP function_sp : m_functions) {
    if (function_sp && function_sp->GetName() == name) {
      lldb_private::SymbolContext sc;
      sc.comp_unit = m_comp_unit_sp.get();
      sc.function = function_sp.get();
      sc_list.Append(sc);
    }
  }
}

void SymbolFileCTF::FindFunctions(const lldb_private::RegularExpression &regex,
                                  bool include_inlines,
                                  lldb_private::SymbolContextList &sc_list) {
  for (FunctionSP function_sp : m_functions) {
    if (function_sp && regex.Execute(function_sp->GetName())) {
      lldb_private::SymbolContext sc;
      sc.comp_unit = m_comp_unit_sp.get();
      sc.function = function_sp.get();
      sc_list.Append(sc);
    }
  }
}

void SymbolFileCTF::FindGlobalVariables(
    lldb_private::ConstString name,
    const lldb_private::CompilerDeclContext &parent_decl_ctx,
    uint32_t max_matches, lldb_private::VariableList &variables) {
  ParseObjects(*m_comp_unit_sp);

  size_t matches = 0;
  for (VariableSP variable_sp : m_variables) {
    if (matches == max_matches)
      break;
    if (variable_sp && variable_sp->GetName() == name) {
      variables.AddVariable(variable_sp);
      matches++;
    }
  }
}

void SymbolFileCTF::FindGlobalVariables(
    const lldb_private::RegularExpression &regex, uint32_t max_matches,
    lldb_private::VariableList &variables) {
  ParseObjects(*m_comp_unit_sp);

  size_t matches = 0;
  for (VariableSP variable_sp : m_variables) {
    if (matches == max_matches)
      break;
    if (variable_sp && regex.Execute(variable_sp->GetName())) {
      variables.AddVariable(variable_sp);
      matches++;
    }
  }
}
