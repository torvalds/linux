//===-- LibCxxMap.cpp -----------------------------------------------------===//
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
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Stream.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"
#include <cstdint>
#include <locale>
#include <optional>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;

// The flattened layout of the std::__tree_iterator::__ptr_ looks
// as follows:
//
// The following shows the contiguous block of memory:
//
//        +-----------------------------+ class __tree_end_node
// __ptr_ | pointer __left_;            |
//        +-----------------------------+ class __tree_node_base
//        | pointer __right_;           |
//        | __parent_pointer __parent_; |
//        | bool __is_black_;           |
//        +-----------------------------+ class __tree_node
//        | __node_value_type __value_; | <<< our key/value pair
//        +-----------------------------+
//
// where __ptr_ has type __iter_pointer.

class MapEntry {
public:
  MapEntry() = default;
  explicit MapEntry(ValueObjectSP entry_sp) : m_entry_sp(entry_sp) {}
  explicit MapEntry(ValueObject *entry)
      : m_entry_sp(entry ? entry->GetSP() : ValueObjectSP()) {}

  ValueObjectSP left() const {
    if (!m_entry_sp)
      return m_entry_sp;
    return m_entry_sp->GetSyntheticChildAtOffset(
        0, m_entry_sp->GetCompilerType(), true);
  }

  ValueObjectSP right() const {
    if (!m_entry_sp)
      return m_entry_sp;
    return m_entry_sp->GetSyntheticChildAtOffset(
        m_entry_sp->GetProcessSP()->GetAddressByteSize(),
        m_entry_sp->GetCompilerType(), true);
  }

  ValueObjectSP parent() const {
    if (!m_entry_sp)
      return m_entry_sp;
    return m_entry_sp->GetSyntheticChildAtOffset(
        2 * m_entry_sp->GetProcessSP()->GetAddressByteSize(),
        m_entry_sp->GetCompilerType(), true);
  }

  uint64_t value() const {
    if (!m_entry_sp)
      return 0;
    return m_entry_sp->GetValueAsUnsigned(0);
  }

  bool error() const {
    if (!m_entry_sp)
      return true;
    return m_entry_sp->GetError().Fail();
  }

  bool null() const { return (value() == 0); }

  ValueObjectSP GetEntry() const { return m_entry_sp; }

  void SetEntry(ValueObjectSP entry) { m_entry_sp = entry; }

  bool operator==(const MapEntry &rhs) const {
    return (rhs.m_entry_sp.get() == m_entry_sp.get());
  }

private:
  ValueObjectSP m_entry_sp;
};

class MapIterator {
public:
  MapIterator(ValueObject *entry, size_t depth = 0)
      : m_entry(entry), m_max_depth(depth), m_error(false) {}

  MapIterator() = default;

  ValueObjectSP value() { return m_entry.GetEntry(); }

  ValueObjectSP advance(size_t count) {
    ValueObjectSP fail;
    if (m_error)
      return fail;
    size_t steps = 0;
    while (count > 0) {
      next();
      count--, steps++;
      if (m_error || m_entry.null() || (steps > m_max_depth))
        return fail;
    }
    return m_entry.GetEntry();
  }

private:
  /// Mimicks libc++'s __tree_next algorithm, which libc++ uses
  /// in its __tree_iteartor::operator++.
  void next() {
    if (m_entry.null())
      return;
    MapEntry right(m_entry.right());
    if (!right.null()) {
      m_entry = tree_min(std::move(right));
      return;
    }
    size_t steps = 0;
    while (!is_left_child(m_entry)) {
      if (m_entry.error()) {
        m_error = true;
        return;
      }
      m_entry.SetEntry(m_entry.parent());
      steps++;
      if (steps > m_max_depth) {
        m_entry = MapEntry();
        return;
      }
    }
    m_entry = MapEntry(m_entry.parent());
  }

  /// Mimicks libc++'s __tree_min algorithm.
  MapEntry tree_min(MapEntry x) {
    if (x.null())
      return MapEntry();
    MapEntry left(x.left());
    size_t steps = 0;
    while (!left.null()) {
      if (left.error()) {
        m_error = true;
        return MapEntry();
      }
      x = left;
      left.SetEntry(x.left());
      steps++;
      if (steps > m_max_depth)
        return MapEntry();
    }
    return x;
  }

  bool is_left_child(const MapEntry &x) {
    if (x.null())
      return false;
    MapEntry rhs(x.parent());
    rhs.SetEntry(rhs.left());
    return x.value() == rhs.value();
  }

  MapEntry m_entry;
  size_t m_max_depth = 0;
  bool m_error = false;
};

namespace lldb_private {
namespace formatters {
class LibcxxStdMapSyntheticFrontEnd : public SyntheticChildrenFrontEnd {
public:
  LibcxxStdMapSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp);

  ~LibcxxStdMapSyntheticFrontEnd() override = default;

  llvm::Expected<uint32_t> CalculateNumChildren() override;

  lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) override;

  lldb::ChildCacheState Update() override;

  bool MightHaveChildren() override;

  size_t GetIndexOfChildWithName(ConstString name) override;

private:
  /// Returns the ValueObject for the __tree_node type that
  /// holds the key/value pair of the node at index \ref idx.
  ///
  /// \param[in] idx The child index that we're looking to get
  ///                the key/value pair for.
  ///
  /// \param[in] max_depth The maximum search depth after which
  ///                      we stop trying to find the key/value
  ///                      pair for.
  ///
  /// \returns On success, returns the ValueObjectSP corresponding
  ///          to the __tree_node's __value_ member (which holds
  ///          the key/value pair the formatter wants to display).
  ///          On failure, will return nullptr.
  ValueObjectSP GetKeyValuePair(size_t idx, size_t max_depth);

  ValueObject *m_tree = nullptr;
  ValueObject *m_root_node = nullptr;
  CompilerType m_node_ptr_type;
  size_t m_count = UINT32_MAX;
  std::map<size_t, MapIterator> m_iterators;
};

class LibCxxMapIteratorSyntheticFrontEnd : public SyntheticChildrenFrontEnd {
public:
  LibCxxMapIteratorSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp);

  llvm::Expected<uint32_t> CalculateNumChildren() override;

  lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) override;

  lldb::ChildCacheState Update() override;

  bool MightHaveChildren() override;

  size_t GetIndexOfChildWithName(ConstString name) override;

  ~LibCxxMapIteratorSyntheticFrontEnd() override = default;

private:
  ValueObjectSP m_pair_sp = nullptr;
};
} // namespace formatters
} // namespace lldb_private

lldb_private::formatters::LibcxxStdMapSyntheticFrontEnd::
    LibcxxStdMapSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
    : SyntheticChildrenFrontEnd(*valobj_sp) {
  if (valobj_sp)
    Update();
}

llvm::Expected<uint32_t> lldb_private::formatters::
    LibcxxStdMapSyntheticFrontEnd::CalculateNumChildren() {
  if (m_count != UINT32_MAX)
    return m_count;

  if (m_tree == nullptr)
    return 0;

  ValueObjectSP size_node(m_tree->GetChildMemberWithName("__pair3_"));
  if (!size_node)
    return 0;

  size_node = GetFirstValueOfLibCXXCompressedPair(*size_node);

  if (!size_node)
    return 0;

  m_count = size_node->GetValueAsUnsigned(0);
  return m_count;
}

ValueObjectSP
lldb_private::formatters::LibcxxStdMapSyntheticFrontEnd::GetKeyValuePair(
    size_t idx, size_t max_depth) {
  MapIterator iterator(m_root_node, max_depth);

  size_t advance_by = idx;
  if (idx > 0) {
    // If we have already created the iterator for the previous
    // index, we can start from there and advance by 1.
    auto cached_iterator = m_iterators.find(idx - 1);
    if (cached_iterator != m_iterators.end()) {
      iterator = cached_iterator->second;
      advance_by = 1;
    }
  }

  ValueObjectSP iterated_sp(iterator.advance(advance_by));
  if (!iterated_sp)
    // this tree is garbage - stop
    return nullptr;

  if (!m_node_ptr_type.IsValid())
    return nullptr;

  // iterated_sp is a __iter_pointer at this point.
  // We can cast it to a __node_pointer (which is what libc++ does).
  auto value_type_sp = iterated_sp->Cast(m_node_ptr_type);
  if (!value_type_sp)
    return nullptr;

  // Finally, get the key/value pair.
  value_type_sp = value_type_sp->GetChildMemberWithName("__value_");
  if (!value_type_sp)
    return nullptr;

  m_iterators[idx] = iterator;

  return value_type_sp;
}

lldb::ValueObjectSP
lldb_private::formatters::LibcxxStdMapSyntheticFrontEnd::GetChildAtIndex(
    uint32_t idx) {
  static ConstString g_cc_("__cc_"), g_cc("__cc");
  static ConstString g_nc("__nc");
  uint32_t num_children = CalculateNumChildrenIgnoringErrors();
  if (idx >= num_children)
    return nullptr;

  if (m_tree == nullptr || m_root_node == nullptr)
    return nullptr;

  ValueObjectSP key_val_sp = GetKeyValuePair(idx, /*max_depth=*/num_children);
  if (!key_val_sp) {
    // this will stop all future searches until an Update() happens
    m_tree = nullptr;
    return nullptr;
  }

  // at this point we have a valid
  // we need to copy current_sp into a new object otherwise we will end up with
  // all items named __value_
  StreamString name;
  name.Printf("[%" PRIu64 "]", (uint64_t)idx);
  auto potential_child_sp = key_val_sp->Clone(ConstString(name.GetString()));
  if (potential_child_sp) {
    switch (potential_child_sp->GetNumChildrenIgnoringErrors()) {
    case 1: {
      auto child0_sp = potential_child_sp->GetChildAtIndex(0);
      if (child0_sp &&
          (child0_sp->GetName() == g_cc_ || child0_sp->GetName() == g_cc))
        potential_child_sp = child0_sp->Clone(ConstString(name.GetString()));
      break;
    }
    case 2: {
      auto child0_sp = potential_child_sp->GetChildAtIndex(0);
      auto child1_sp = potential_child_sp->GetChildAtIndex(1);
      if (child0_sp &&
          (child0_sp->GetName() == g_cc_ || child0_sp->GetName() == g_cc) &&
          child1_sp && child1_sp->GetName() == g_nc)
        potential_child_sp = child0_sp->Clone(ConstString(name.GetString()));
      break;
    }
    }
  }
  return potential_child_sp;
}

lldb::ChildCacheState
lldb_private::formatters::LibcxxStdMapSyntheticFrontEnd::Update() {
  m_count = UINT32_MAX;
  m_tree = m_root_node = nullptr;
  m_iterators.clear();
  m_tree = m_backend.GetChildMemberWithName("__tree_").get();
  if (!m_tree)
    return lldb::ChildCacheState::eRefetch;
  m_root_node = m_tree->GetChildMemberWithName("__begin_node_").get();
  m_node_ptr_type =
      m_tree->GetCompilerType().GetDirectNestedTypeWithName("__node_pointer");

  return lldb::ChildCacheState::eRefetch;
}

bool lldb_private::formatters::LibcxxStdMapSyntheticFrontEnd::
    MightHaveChildren() {
  return true;
}

size_t lldb_private::formatters::LibcxxStdMapSyntheticFrontEnd::
    GetIndexOfChildWithName(ConstString name) {
  return ExtractIndexFromString(name.GetCString());
}

SyntheticChildrenFrontEnd *
lldb_private::formatters::LibcxxStdMapSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  return (valobj_sp ? new LibcxxStdMapSyntheticFrontEnd(valobj_sp) : nullptr);
}

lldb_private::formatters::LibCxxMapIteratorSyntheticFrontEnd::
    LibCxxMapIteratorSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
    : SyntheticChildrenFrontEnd(*valobj_sp) {
  if (valobj_sp)
    Update();
}

lldb::ChildCacheState
lldb_private::formatters::LibCxxMapIteratorSyntheticFrontEnd::Update() {
  m_pair_sp.reset();

  ValueObjectSP valobj_sp = m_backend.GetSP();
  if (!valobj_sp)
    return lldb::ChildCacheState::eRefetch;

  TargetSP target_sp(valobj_sp->GetTargetSP());
  if (!target_sp)
    return lldb::ChildCacheState::eRefetch;

  // m_backend is a std::map::iterator
  // ...which is a __map_iterator<__tree_iterator<..., __node_pointer, ...>>
  //
  // Then, __map_iterator::__i_ is a __tree_iterator
  auto tree_iter_sp = valobj_sp->GetChildMemberWithName("__i_");
  if (!tree_iter_sp)
    return lldb::ChildCacheState::eRefetch;

  // Type is __tree_iterator::__node_pointer
  // (We could alternatively also get this from the template argument)
  auto node_pointer_type =
      tree_iter_sp->GetCompilerType().GetDirectNestedTypeWithName(
          "__node_pointer");
  if (!node_pointer_type.IsValid())
    return lldb::ChildCacheState::eRefetch;

  // __ptr_ is a __tree_iterator::__iter_pointer
  auto iter_pointer_sp = tree_iter_sp->GetChildMemberWithName("__ptr_");
  if (!iter_pointer_sp)
    return lldb::ChildCacheState::eRefetch;

  // Cast the __iter_pointer to a __node_pointer (which stores our key/value
  // pair)
  auto node_pointer_sp = iter_pointer_sp->Cast(node_pointer_type);
  if (!node_pointer_sp)
    return lldb::ChildCacheState::eRefetch;

  auto key_value_sp = node_pointer_sp->GetChildMemberWithName("__value_");
  if (!key_value_sp)
    return lldb::ChildCacheState::eRefetch;

  // Create the synthetic child, which is a pair where the key and value can be
  // retrieved by querying the synthetic frontend for
  // GetIndexOfChildWithName("first") and GetIndexOfChildWithName("second")
  // respectively.
  //
  // std::map stores the actual key/value pair in value_type::__cc_ (or
  // previously __cc).
  key_value_sp = key_value_sp->Clone(ConstString("pair"));
  if (key_value_sp->GetNumChildrenIgnoringErrors() == 1) {
    auto child0_sp = key_value_sp->GetChildAtIndex(0);
    if (child0_sp &&
        (child0_sp->GetName() == "__cc_" || child0_sp->GetName() == "__cc"))
      key_value_sp = child0_sp->Clone(ConstString("pair"));
  }

  m_pair_sp = key_value_sp;

  return lldb::ChildCacheState::eRefetch;
}

llvm::Expected<uint32_t> lldb_private::formatters::
    LibCxxMapIteratorSyntheticFrontEnd::CalculateNumChildren() {
  return 2;
}

lldb::ValueObjectSP
lldb_private::formatters::LibCxxMapIteratorSyntheticFrontEnd::GetChildAtIndex(
    uint32_t idx) {
  if (!m_pair_sp)
    return nullptr;

  return m_pair_sp->GetChildAtIndex(idx);
}

bool lldb_private::formatters::LibCxxMapIteratorSyntheticFrontEnd::
    MightHaveChildren() {
  return true;
}

size_t lldb_private::formatters::LibCxxMapIteratorSyntheticFrontEnd::
    GetIndexOfChildWithName(ConstString name) {
  if (!m_pair_sp)
    return UINT32_MAX;

  return m_pair_sp->GetIndexOfChildWithName(name);
}

SyntheticChildrenFrontEnd *
lldb_private::formatters::LibCxxMapIteratorSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  return (valobj_sp ? new LibCxxMapIteratorSyntheticFrontEnd(valobj_sp)
                    : nullptr);
}
