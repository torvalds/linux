//===-- BlockPointer.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BlockPointer.h"

#include "Plugins/ExpressionParser/Clang/ClangASTImporter.h"
#include "Plugins/ExpressionParser/Clang/ClangPersistentVariables.h"
#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/DataFormatters/FormattersHelpers.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/TypeSystem.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;

namespace lldb_private {
namespace formatters {

class BlockPointerSyntheticFrontEnd : public SyntheticChildrenFrontEnd {
public:
  BlockPointerSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
      : SyntheticChildrenFrontEnd(*valobj_sp), m_block_struct_type() {
    CompilerType block_pointer_type(m_backend.GetCompilerType());
    CompilerType function_pointer_type;
    block_pointer_type.IsBlockPointerType(&function_pointer_type);

    TargetSP target_sp(m_backend.GetTargetSP());

    if (!target_sp) {
      return;
    }

    auto type_system_or_err = target_sp->GetScratchTypeSystemForLanguage(
        lldb::eLanguageTypeC_plus_plus);
    if (auto err = type_system_or_err.takeError()) {
      LLDB_LOG_ERROR(GetLog(LLDBLog::DataFormatters), std::move(err),
                     "Failed to get scratch TypeSystemClang: {0}");
      return;
    }

    auto ts = block_pointer_type.GetTypeSystem();
    auto clang_ast_context = ts.dyn_cast_or_null<TypeSystemClang>();
    if (!clang_ast_context)
      return;

    const char *const isa_name("__isa");
    const CompilerType isa_type =
        clang_ast_context->GetBasicType(lldb::eBasicTypeObjCClass);
    const char *const flags_name("__flags");
    const CompilerType flags_type =
        clang_ast_context->GetBasicType(lldb::eBasicTypeInt);
    const char *const reserved_name("__reserved");
    const CompilerType reserved_type =
        clang_ast_context->GetBasicType(lldb::eBasicTypeInt);
    const char *const FuncPtr_name("__FuncPtr");

    m_block_struct_type = clang_ast_context->CreateStructForIdentifier(
        llvm::StringRef(), {{isa_name, isa_type},
                            {flags_name, flags_type},
                            {reserved_name, reserved_type},
                            {FuncPtr_name, function_pointer_type}});
  }

  ~BlockPointerSyntheticFrontEnd() override = default;

  llvm::Expected<uint32_t> CalculateNumChildren() override {
    const bool omit_empty_base_classes = false;
    return m_block_struct_type.GetNumChildren(omit_empty_base_classes, nullptr);
  }

  lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) override {
    if (!m_block_struct_type.IsValid()) {
      return lldb::ValueObjectSP();
    }

    if (idx >= CalculateNumChildrenIgnoringErrors()) {
      return lldb::ValueObjectSP();
    }

    const bool thread_and_frame_only_if_stopped = true;
    ExecutionContext exe_ctx = m_backend.GetExecutionContextRef().Lock(
        thread_and_frame_only_if_stopped);
    const bool transparent_pointers = false;
    const bool omit_empty_base_classes = false;
    const bool ignore_array_bounds = false;
    ValueObject *value_object = nullptr;

    std::string child_name;
    uint32_t child_byte_size = 0;
    int32_t child_byte_offset = 0;
    uint32_t child_bitfield_bit_size = 0;
    uint32_t child_bitfield_bit_offset = 0;
    bool child_is_base_class = false;
    bool child_is_deref_of_parent = false;
    uint64_t language_flags = 0;

    auto child_type_or_err = m_block_struct_type.GetChildCompilerTypeAtIndex(
        &exe_ctx, idx, transparent_pointers, omit_empty_base_classes,
        ignore_array_bounds, child_name, child_byte_size, child_byte_offset,
        child_bitfield_bit_size, child_bitfield_bit_offset, child_is_base_class,
        child_is_deref_of_parent, value_object, language_flags);
    if (!child_type_or_err)
      return ValueObjectConstResult::Create(
          exe_ctx.GetBestExecutionContextScope(),
          Status(child_type_or_err.takeError()));
    CompilerType child_type = *child_type_or_err;

    ValueObjectSP struct_pointer_sp =
        m_backend.Cast(m_block_struct_type.GetPointerType());

    if (!struct_pointer_sp) {
      return lldb::ValueObjectSP();
    }

    Status err;
    ValueObjectSP struct_sp = struct_pointer_sp->Dereference(err);

    if (!struct_sp || !err.Success()) {
      return lldb::ValueObjectSP();
    }

    ValueObjectSP child_sp(struct_sp->GetSyntheticChildAtOffset(
        child_byte_offset, child_type, true,
        ConstString(child_name.c_str(), child_name.size())));

    return child_sp;
  }

  // return true if this object is now safe to use forever without ever
  // updating again; the typical (and tested) answer here is 'false'
  lldb::ChildCacheState Update() override {
    return lldb::ChildCacheState::eRefetch;
  }

  // maybe return false if the block pointer is, say, null
  bool MightHaveChildren() override { return true; }

  size_t GetIndexOfChildWithName(ConstString name) override {
    if (!m_block_struct_type.IsValid())
      return UINT32_MAX;

    const bool omit_empty_base_classes = false;
    return m_block_struct_type.GetIndexOfChildWithName(name.AsCString(),
                                                       omit_empty_base_classes);
  }

private:
  CompilerType m_block_struct_type;
};

} // namespace formatters
} // namespace lldb_private

bool lldb_private::formatters::BlockPointerSummaryProvider(
    ValueObject &valobj, Stream &s, const TypeSummaryOptions &) {
  lldb_private::SyntheticChildrenFrontEnd *synthetic_children =
      BlockPointerSyntheticFrontEndCreator(nullptr, valobj.GetSP());
  if (!synthetic_children) {
    return false;
  }

  synthetic_children->Update();

  static const ConstString s_FuncPtr_name("__FuncPtr");

  lldb::ValueObjectSP child_sp = synthetic_children->GetChildAtIndex(
      synthetic_children->GetIndexOfChildWithName(s_FuncPtr_name));

  if (!child_sp) {
    return false;
  }

  lldb::ValueObjectSP qualified_child_representation_sp =
      child_sp->GetQualifiedRepresentationIfAvailable(
          lldb::eDynamicDontRunTarget, true);

  const char *child_value =
      qualified_child_representation_sp->GetValueAsCString();

  s.Printf("%s", child_value);

  return true;
}

lldb_private::SyntheticChildrenFrontEnd *
lldb_private::formatters::BlockPointerSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  if (!valobj_sp)
    return nullptr;
  return new BlockPointerSyntheticFrontEnd(valobj_sp);
}
