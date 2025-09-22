//===-- UniqueCStringMap.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_UNIQUECSTRINGMAP_H
#define LLDB_CORE_UNIQUECSTRINGMAP_H

#include <algorithm>
#include <vector>

#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/RegularExpression.h"

namespace lldb_private {

// Templatized uniqued string map.
//
// This map is useful for mapping unique C string names to values of type T.
// Each "const char *" name added must be unique for a given
// C string value. ConstString::GetCString() can provide such strings.
// Any other string table that has guaranteed unique values can also be used.
template <typename T> class UniqueCStringMap {
public:
  struct Entry {
    Entry(ConstString cstr, const T &v) : cstring(cstr), value(v) {}

    ConstString cstring;
    T value;
  };

  typedef std::vector<Entry> collection;
  typedef typename collection::iterator iterator;
  typedef typename collection::const_iterator const_iterator;

  // Call this function multiple times to add a bunch of entries to this map,
  // then later call UniqueCStringMap<T>::Sort() before doing any searches by
  // name.
  void Append(ConstString unique_cstr, const T &value) {
    m_map.push_back(typename UniqueCStringMap<T>::Entry(unique_cstr, value));
  }

  void Append(const Entry &e) { m_map.push_back(e); }

  void Clear() { m_map.clear(); }

  // Get an entries by index in a variety of forms.
  //
  // The caller is responsible for ensuring that the collection does not change
  // during while using the returned values.
  bool GetValueAtIndex(uint32_t idx, T &value) const {
    if (idx < m_map.size()) {
      value = m_map[idx].value;
      return true;
    }
    return false;
  }

  ConstString GetCStringAtIndexUnchecked(uint32_t idx) const {
    return m_map[idx].cstring;
  }

  // Use this function if you have simple types in your map that you can easily
  // copy when accessing values by index.
  T GetValueAtIndexUnchecked(uint32_t idx) const { return m_map[idx].value; }

  // Use this function if you have complex types in your map that you don't
  // want to copy when accessing values by index.
  const T &GetValueRefAtIndexUnchecked(uint32_t idx) const {
    return m_map[idx].value;
  }

  ConstString GetCStringAtIndex(uint32_t idx) const {
    return ((idx < m_map.size()) ? m_map[idx].cstring : ConstString());
  }

  // Find the value for the unique string in the map.
  //
  // Return the value for \a unique_cstr if one is found, return \a fail_value
  // otherwise. This method works well for simple type
  // T values and only if there is a sensible failure value that can
  // be returned and that won't match any existing values.
  T Find(ConstString unique_cstr, T fail_value) const {
    auto pos = llvm::lower_bound(m_map, unique_cstr, Compare());
    if (pos != m_map.end() && pos->cstring == unique_cstr)
      return pos->value;
    return fail_value;
  }

  // Get a pointer to the first entry that matches "name". nullptr will be
  // returned if there is no entry that matches "name".
  //
  // The caller is responsible for ensuring that the collection does not change
  // during while using the returned pointer.
  const Entry *FindFirstValueForName(ConstString unique_cstr) const {
    auto pos = llvm::lower_bound(m_map, unique_cstr, Compare());
    if (pos != m_map.end() && pos->cstring == unique_cstr)
      return &(*pos);
    return nullptr;
  }

  // Get a pointer to the next entry that matches "name" from a previously
  // returned Entry pointer. nullptr will be returned if there is no subsequent
  // entry that matches "name".
  //
  // The caller is responsible for ensuring that the collection does not change
  // during while using the returned pointer.
  const Entry *FindNextValueForName(const Entry *entry_ptr) const {
    if (!m_map.empty()) {
      const Entry *first_entry = &m_map[0];
      const Entry *after_last_entry = first_entry + m_map.size();
      const Entry *next_entry = entry_ptr + 1;
      if (first_entry <= next_entry && next_entry < after_last_entry) {
        if (next_entry->cstring == entry_ptr->cstring)
          return next_entry;
      }
    }
    return nullptr;
  }

  size_t GetValues(ConstString unique_cstr, std::vector<T> &values) const {
    const size_t start_size = values.size();

    for (const Entry &entry : llvm::make_range(std::equal_range(
             m_map.begin(), m_map.end(), unique_cstr, Compare())))
      values.push_back(entry.value);

    return values.size() - start_size;
  }

  size_t GetValues(const RegularExpression &regex,
                   std::vector<T> &values) const {
    const size_t start_size = values.size();

    const_iterator pos, end = m_map.end();
    for (pos = m_map.begin(); pos != end; ++pos) {
      if (regex.Execute(pos->cstring.GetCString()))
        values.push_back(pos->value);
    }

    return values.size() - start_size;
  }

  // Get the total number of entries in this map.
  size_t GetSize() const { return m_map.size(); }

  // Returns true if this map is empty.
  bool IsEmpty() const { return m_map.empty(); }

  // Reserve memory for at least "n" entries in the map. This is useful to call
  // when you know you will be adding a lot of entries using
  // UniqueCStringMap::Append() (which should be followed by a call to
  // UniqueCStringMap::Sort()) or to UniqueCStringMap::Insert().
  void Reserve(size_t n) { m_map.reserve(n); }

  // Sort the unsorted contents in this map. A typical code flow would be:
  // size_t approximate_num_entries = ....
  // UniqueCStringMap<uint32_t> my_map;
  // my_map.Reserve (approximate_num_entries);
  // for (...)
  // {
  //      my_map.Append (UniqueCStringMap::Entry(GetName(...), GetValue(...)));
  // }
  // my_map.Sort();
  void Sort() {
    Sort([](const T &, const T &) { return false; });
  }

  /// Sort contents of this map using the provided comparator to break ties for
  /// entries with the same string value.
  template <typename TCompare> void Sort(TCompare tc) {
    Compare c;
    llvm::sort(m_map, [&](const Entry &lhs, const Entry &rhs) -> bool {
      int result = c.ThreeWay(lhs.cstring, rhs.cstring);
      if (result == 0)
        return tc(lhs.value, rhs.value);
      return result < 0;
    });
  }

  // Since we are using a vector to contain our items it will always double its
  // memory consumption as things are added to the vector, so if you intend to
  // keep a UniqueCStringMap around and have a lot of entries in the map, you
  // will want to call this function to create a new vector and copy _only_ the
  // exact size needed as part of the finalization of the string map.
  void SizeToFit() {
    if (m_map.size() < m_map.capacity()) {
      collection temp(m_map.begin(), m_map.end());
      m_map.swap(temp);
    }
  }

  iterator begin() { return m_map.begin(); }
  iterator end() { return m_map.end(); }
  const_iterator begin() const { return m_map.begin(); }
  const_iterator end() const { return m_map.end(); }

  // Range-based for loop for all entries of the specified ConstString name.
  llvm::iterator_range<const_iterator>
  equal_range(ConstString unique_cstr) const {
    return llvm::make_range(
        std::equal_range(m_map.begin(), m_map.end(), unique_cstr, Compare()));
  };

protected:
  struct Compare {
    bool operator()(const Entry &lhs, const Entry &rhs) {
      return operator()(lhs.cstring, rhs.cstring);
    }

    bool operator()(const Entry &lhs, ConstString rhs) {
      return operator()(lhs.cstring, rhs);
    }

    bool operator()(ConstString lhs, const Entry &rhs) {
      return operator()(lhs, rhs.cstring);
    }

    bool operator()(ConstString lhs, ConstString rhs) {
      return ThreeWay(lhs, rhs) < 0;
    }

    // This is only for uniqueness, not lexicographical ordering, so we can
    // just compare pointers. *However*, comparing pointers from different
    // allocations is UB, so we need compare their integral values instead.
    int ThreeWay(ConstString lhs, ConstString rhs) {
      auto lhsint = uintptr_t(lhs.GetCString());
      auto rhsint = uintptr_t(rhs.GetCString());
      if (lhsint < rhsint)
        return -1;
      if (lhsint > rhsint)
        return 1;
      return 0;
    }
  };

  collection m_map;
};

} // namespace lldb_private

#endif // LLDB_CORE_UNIQUECSTRINGMAP_H
