//===-- LibCxxUnorderedMap.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LibCxx.h"

#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/DataFormatters/FormattersHelpers.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Stream.h"
#include "llvm/ADT/StringRef.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;

namespace lldb_private {
namespace formatters {
class LibcxxStdUnorderedMapSyntheticFrontEnd
    : public SyntheticChildrenFrontEnd {
public:
  LibcxxStdUnorderedMapSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp);

  ~LibcxxStdUnorderedMapSyntheticFrontEnd() override = default;

  llvm::Expected<uint32_t> CalculateNumChildren() override;

  lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) override;

  lldb::ChildCacheState Update() override;

  bool MightHaveChildren() override;

  size_t GetIndexOfChildWithName(ConstString name) override;

private:
  CompilerType m_element_type;
  CompilerType m_node_type;
  ValueObject *m_tree = nullptr;
  size_t m_num_elements = 0;
  ValueObject *m_next_element = nullptr;
  std::vector<std::pair<ValueObject *, uint64_t>> m_elements_cache;
};

class LibCxxUnorderedMapIteratorSyntheticFrontEnd
    : public SyntheticChildrenFrontEnd {
public:
  LibCxxUnorderedMapIteratorSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp);

  ~LibCxxUnorderedMapIteratorSyntheticFrontEnd() override = default;

  llvm::Expected<uint32_t> CalculateNumChildren() override;

  lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) override;

  lldb::ChildCacheState Update() override;

  bool MightHaveChildren() override;

  size_t GetIndexOfChildWithName(ConstString name) override;

private:
  lldb::ValueObjectSP m_pair_sp; ///< ValueObject for the key/value pair
                                 ///< that the iterator currently points
                                 ///< to.
};

} // namespace formatters
} // namespace lldb_private

lldb_private::formatters::LibcxxStdUnorderedMapSyntheticFrontEnd::
    LibcxxStdUnorderedMapSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
    : SyntheticChildrenFrontEnd(*valobj_sp), m_element_type(),
      m_elements_cache() {
  if (valobj_sp)
    Update();
}

llvm::Expected<uint32_t> lldb_private::formatters::
    LibcxxStdUnorderedMapSyntheticFrontEnd::CalculateNumChildren() {
  return m_num_elements;
}

static void consumeInlineNamespace(llvm::StringRef &name) {
  // Delete past an inline namespace, if any: __[a-zA-Z0-9_]+::
  auto scratch = name;
  if (scratch.consume_front("__") && std::isalnum(scratch[0])) {
    scratch = scratch.drop_while([](char c) { return std::isalnum(c); });
    if (scratch.consume_front("::")) {
      // Successfully consumed a namespace.
      name = scratch;
    }
  }
}

static bool isStdTemplate(ConstString type_name, llvm::StringRef type) {
  llvm::StringRef name = type_name.GetStringRef();
  // The type name may be prefixed with `std::__<inline-namespace>::`.
  if (name.consume_front("std::"))
    consumeInlineNamespace(name);
  return name.consume_front(type) && name.starts_with("<");
}

static bool isUnorderedMap(ConstString type_name) {
  return isStdTemplate(type_name, "unordered_map") ||
         isStdTemplate(type_name, "unordered_multimap");
}

lldb::ValueObjectSP lldb_private::formatters::
    LibcxxStdUnorderedMapSyntheticFrontEnd::GetChildAtIndex(uint32_t idx) {
  if (idx >= CalculateNumChildrenIgnoringErrors())
    return lldb::ValueObjectSP();
  if (m_tree == nullptr)
    return lldb::ValueObjectSP();

  while (idx >= m_elements_cache.size()) {
    if (m_next_element == nullptr)
      return lldb::ValueObjectSP();

    Status error;
    ValueObjectSP node_sp = m_next_element->Dereference(error);
    if (!node_sp || error.Fail())
      return lldb::ValueObjectSP();

    ValueObjectSP value_sp = node_sp->GetChildMemberWithName("__value_");
    ValueObjectSP hash_sp = node_sp->GetChildMemberWithName("__hash_");
    if (!hash_sp || !value_sp) {
      if (!m_element_type) {
        auto p1_sp = m_backend.GetChildAtNamePath({"__table_", "__p1_"});
        if (!p1_sp)
          return nullptr;

        ValueObjectSP first_sp = GetFirstValueOfLibCXXCompressedPair(*p1_sp);
        if (!first_sp)
          return nullptr;

        m_element_type = first_sp->GetCompilerType();
        m_element_type = m_element_type.GetTypeTemplateArgument(0);
        m_element_type = m_element_type.GetPointeeType();
        m_node_type = m_element_type;
        m_element_type = m_element_type.GetTypeTemplateArgument(0);
        // This synthetic provider is used for both unordered_(multi)map and
        // unordered_(multi)set. For unordered_map, the element type has an
        // additional type layer, an internal struct (`__hash_value_type`)
        // that wraps a std::pair. Peel away the internal wrapper type - whose
        // structure is of no value to users, to expose the std::pair. This
        // matches the structure returned by the std::map synthetic provider.
        if (isUnorderedMap(m_backend.GetTypeName())) {
          std::string name;
          CompilerType field_type = m_element_type.GetFieldAtIndex(
              0, name, nullptr, nullptr, nullptr);
          CompilerType actual_type = field_type.GetTypedefedType();
          if (isStdTemplate(actual_type.GetTypeName(), "pair"))
            m_element_type = actual_type;
        }
      }
      if (!m_node_type)
        return nullptr;
      node_sp = m_next_element->Cast(m_node_type.GetPointerType())
              ->Dereference(error);
      if (!node_sp || error.Fail())
          return nullptr;

      hash_sp = node_sp->GetChildMemberWithName("__hash_");
      if (!hash_sp)
        return nullptr;

      value_sp = node_sp->GetChildMemberWithName("__value_");
      if (!value_sp) {
        // clang-format off
        // Since D101206 (ba79fb2e1f), libc++ wraps the `__value_` in an
        // anonymous union.
        // Child 0: __hash_node_base base class
        // Child 1: __hash_
        // Child 2: anonymous union
        // clang-format on
        auto anon_union_sp = node_sp->GetChildAtIndex(2);
        if (!anon_union_sp)
          return nullptr;

        value_sp = anon_union_sp->GetChildMemberWithName("__value_");
        if (!value_sp)
          return nullptr;
      }
    }
    m_elements_cache.push_back(
        {value_sp.get(), hash_sp->GetValueAsUnsigned(0)});
    m_next_element = node_sp->GetChildMemberWithName("__next_").get();
    if (!m_next_element || m_next_element->GetValueAsUnsigned(0) == 0)
      m_next_element = nullptr;
  }

  std::pair<ValueObject *, uint64_t> val_hash = m_elements_cache[idx];
  if (!val_hash.first)
    return lldb::ValueObjectSP();
  StreamString stream;
  stream.Printf("[%" PRIu64 "]", (uint64_t)idx);
  DataExtractor data;
  Status error;
  val_hash.first->GetData(data, error);
  if (error.Fail())
    return lldb::ValueObjectSP();
  const bool thread_and_frame_only_if_stopped = true;
  ExecutionContext exe_ctx = val_hash.first->GetExecutionContextRef().Lock(
      thread_and_frame_only_if_stopped);
  return CreateValueObjectFromData(stream.GetString(), data, exe_ctx,
                                   m_element_type);
}

lldb::ChildCacheState
lldb_private::formatters::LibcxxStdUnorderedMapSyntheticFrontEnd::Update() {
  m_num_elements = 0;
  m_next_element = nullptr;
  m_elements_cache.clear();
  ValueObjectSP table_sp = m_backend.GetChildMemberWithName("__table_");
  if (!table_sp)
    return lldb::ChildCacheState::eRefetch;

  ValueObjectSP p2_sp = table_sp->GetChildMemberWithName("__p2_");
  if (!p2_sp)
    return lldb::ChildCacheState::eRefetch;

  ValueObjectSP num_elements_sp = GetFirstValueOfLibCXXCompressedPair(*p2_sp);
  if (!num_elements_sp)
    return lldb::ChildCacheState::eRefetch;

  ValueObjectSP p1_sp = table_sp->GetChildMemberWithName("__p1_");
  if (!p1_sp)
    return lldb::ChildCacheState::eRefetch;

  ValueObjectSP value_sp = GetFirstValueOfLibCXXCompressedPair(*p1_sp);
  if (!value_sp)
    return lldb::ChildCacheState::eRefetch;

  m_tree = value_sp->GetChildMemberWithName("__next_").get();
  if (m_tree == nullptr)
    return lldb::ChildCacheState::eRefetch;

  m_num_elements = num_elements_sp->GetValueAsUnsigned(0);

  if (m_num_elements > 0)
    m_next_element = m_tree;

  return lldb::ChildCacheState::eRefetch;
}

bool lldb_private::formatters::LibcxxStdUnorderedMapSyntheticFrontEnd::
    MightHaveChildren() {
  return true;
}

size_t lldb_private::formatters::LibcxxStdUnorderedMapSyntheticFrontEnd::
    GetIndexOfChildWithName(ConstString name) {
  return ExtractIndexFromString(name.GetCString());
}

SyntheticChildrenFrontEnd *
lldb_private::formatters::LibcxxStdUnorderedMapSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  return (valobj_sp ? new LibcxxStdUnorderedMapSyntheticFrontEnd(valobj_sp)
                    : nullptr);
}

lldb_private::formatters::LibCxxUnorderedMapIteratorSyntheticFrontEnd::
    LibCxxUnorderedMapIteratorSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
    : SyntheticChildrenFrontEnd(*valobj_sp) {
  if (valobj_sp)
    Update();
}

lldb::ChildCacheState lldb_private::formatters::
    LibCxxUnorderedMapIteratorSyntheticFrontEnd::Update() {
  m_pair_sp.reset();

  ValueObjectSP valobj_sp = m_backend.GetSP();
  if (!valobj_sp)
    return lldb::ChildCacheState::eRefetch;

  TargetSP target_sp(valobj_sp->GetTargetSP());

  if (!target_sp)
    return lldb::ChildCacheState::eRefetch;

  // Get the unordered_map::iterator
  // m_backend is an 'unordered_map::iterator', aka a
  // '__hash_map_iterator<__hash_table::iterator>'
  //
  // __hash_map_iterator::__i_ is a __hash_table::iterator (aka
  // __hash_iterator<__node_pointer>)
  auto hash_iter_sp = valobj_sp->GetChildMemberWithName("__i_");
  if (!hash_iter_sp)
    return lldb::ChildCacheState::eRefetch;

  // Type is '__hash_iterator<__node_pointer>'
  auto hash_iter_type = hash_iter_sp->GetCompilerType();
  if (!hash_iter_type.IsValid())
    return lldb::ChildCacheState::eRefetch;

  // Type is '__node_pointer'
  auto node_pointer_type = hash_iter_type.GetTypeTemplateArgument(0);
  if (!node_pointer_type.IsValid())
    return lldb::ChildCacheState::eRefetch;

  // Cast the __hash_iterator to a __node_pointer (which stores our key/value
  // pair)
  auto hash_node_sp = hash_iter_sp->Cast(node_pointer_type);
  if (!hash_node_sp)
    return lldb::ChildCacheState::eRefetch;

  auto key_value_sp = hash_node_sp->GetChildMemberWithName("__value_");
  if (!key_value_sp) {
    // clang-format off
    // Since D101206 (ba79fb2e1f), libc++ wraps the `__value_` in an
    // anonymous union.
    // Child 0: __hash_node_base base class
    // Child 1: __hash_
    // Child 2: anonymous union
    // clang-format on
    auto anon_union_sp = hash_node_sp->GetChildAtIndex(2);
    if (!anon_union_sp)
      return lldb::ChildCacheState::eRefetch;

    key_value_sp = anon_union_sp->GetChildMemberWithName("__value_");
    if (!key_value_sp)
      return lldb::ChildCacheState::eRefetch;
  }

  // Create the synthetic child, which is a pair where the key and value can be
  // retrieved by querying the synthetic frontend for
  // GetIndexOfChildWithName("first") and GetIndexOfChildWithName("second")
  // respectively.
  //
  // std::unordered_map stores the actual key/value pair in
  // __hash_value_type::__cc_ (or previously __cc).
  auto potential_child_sp = key_value_sp->Clone(ConstString("pair"));
  if (potential_child_sp)
    if (potential_child_sp->GetNumChildrenIgnoringErrors() == 1)
      if (auto child0_sp = potential_child_sp->GetChildAtIndex(0);
          child0_sp->GetName() == "__cc_" || child0_sp->GetName() == "__cc")
        potential_child_sp = child0_sp->Clone(ConstString("pair"));

  m_pair_sp = potential_child_sp;

  return lldb::ChildCacheState::eRefetch;
}

llvm::Expected<uint32_t> lldb_private::formatters::
    LibCxxUnorderedMapIteratorSyntheticFrontEnd::CalculateNumChildren() {
  return 2;
}

lldb::ValueObjectSP lldb_private::formatters::
    LibCxxUnorderedMapIteratorSyntheticFrontEnd::GetChildAtIndex(uint32_t idx) {
  if (m_pair_sp)
    return m_pair_sp->GetChildAtIndex(idx);
  return lldb::ValueObjectSP();
}

bool lldb_private::formatters::LibCxxUnorderedMapIteratorSyntheticFrontEnd::
    MightHaveChildren() {
  return true;
}

size_t lldb_private::formatters::LibCxxUnorderedMapIteratorSyntheticFrontEnd::
    GetIndexOfChildWithName(ConstString name) {
  if (name == "first")
    return 0;
  if (name == "second")
    return 1;
  return UINT32_MAX;
}

SyntheticChildrenFrontEnd *
lldb_private::formatters::LibCxxUnorderedMapIteratorSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  return (valobj_sp ? new LibCxxUnorderedMapIteratorSyntheticFrontEnd(valobj_sp)
                    : nullptr);
}
