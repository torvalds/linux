//===-- NameToDIE.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "NameToDIE.h"
#include "DWARFUnit.h"
#include "lldb/Core/DataFileCache.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/DataEncoder.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/RegularExpression.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StreamString.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::plugin::dwarf;

void NameToDIE::Finalize() {
  m_map.Sort(std::less<DIERef>());
  m_map.SizeToFit();
}

void NameToDIE::Insert(ConstString name, const DIERef &die_ref) {
  m_map.Append(name, die_ref);
}

bool NameToDIE::Find(ConstString name,
                     llvm::function_ref<bool(DIERef ref)> callback) const {
  for (const auto &entry : m_map.equal_range(name))
    if (!callback(entry.value))
      return false;
  return true;
}

bool NameToDIE::Find(const RegularExpression &regex,
                     llvm::function_ref<bool(DIERef ref)> callback) const {
  for (const auto &entry : m_map)
    if (regex.Execute(entry.cstring.GetCString())) {
      if (!callback(entry.value))
        return false;
    }
  return true;
}

void NameToDIE::FindAllEntriesForUnit(
    DWARFUnit &s_unit, llvm::function_ref<bool(DIERef ref)> callback) const {
  const DWARFUnit &ns_unit = s_unit.GetNonSkeletonUnit();
  const uint32_t size = m_map.GetSize();
  for (uint32_t i = 0; i < size; ++i) {
    const DIERef &die_ref = m_map.GetValueAtIndexUnchecked(i);
    if (ns_unit.GetSymbolFileDWARF().GetFileIndex() == die_ref.file_index() &&
        ns_unit.GetDebugSection() == die_ref.section() &&
        ns_unit.GetOffset() <= die_ref.die_offset() &&
        die_ref.die_offset() < ns_unit.GetNextUnitOffset()) {
      if (!callback(die_ref))
        return;
    }
  }
}

void NameToDIE::Dump(Stream *s) {
  const uint32_t size = m_map.GetSize();
  for (uint32_t i = 0; i < size; ++i) {
    s->Format("{0} \"{1}\"\n", m_map.GetValueAtIndexUnchecked(i),
              m_map.GetCStringAtIndexUnchecked(i));
  }
}

void NameToDIE::ForEach(
    std::function<bool(ConstString name, const DIERef &die_ref)> const
        &callback) const {
  const uint32_t size = m_map.GetSize();
  for (uint32_t i = 0; i < size; ++i) {
    if (!callback(m_map.GetCStringAtIndexUnchecked(i),
                  m_map.GetValueAtIndexUnchecked(i)))
      break;
  }
}

void NameToDIE::Append(const NameToDIE &other) {
  const uint32_t size = other.m_map.GetSize();
  for (uint32_t i = 0; i < size; ++i) {
    m_map.Append(other.m_map.GetCStringAtIndexUnchecked(i),
                 other.m_map.GetValueAtIndexUnchecked(i));
  }
}

constexpr llvm::StringLiteral kIdentifierNameToDIE("N2DI");

bool NameToDIE::Decode(const DataExtractor &data, lldb::offset_t *offset_ptr,
                       const StringTableReader &strtab) {
  m_map.Clear();
  llvm::StringRef identifier((const char *)data.GetData(offset_ptr, 4), 4);
  if (identifier != kIdentifierNameToDIE)
    return false;
  const uint32_t count = data.GetU32(offset_ptr);
  m_map.Reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    llvm::StringRef str(strtab.Get(data.GetU32(offset_ptr)));
    // No empty strings allowed in the name to DIE maps.
    if (str.empty())
      return false;
    if (std::optional<DIERef> die_ref = DIERef::Decode(data, offset_ptr))
      m_map.Append(ConstString(str), *die_ref);
    else
      return false;
  }
  // We must sort the UniqueCStringMap after decoding it since it is a vector
  // of UniqueCStringMap::Entry objects which contain a ConstString and type T.
  // ConstString objects are sorted by "const char *" and then type T and
  // the "const char *" are point values that will depend on the order in which
  // ConstString objects are created and in which of the 256 string pools they
  // are created in. So after we decode all of the entries, we must sort the
  // name map to ensure name lookups succeed. If we encode and decode within
  // the same process we wouldn't need to sort, so unit testing didn't catch
  // this issue when first checked in.
  m_map.Sort(std::less<DIERef>());
  return true;
}

void NameToDIE::Encode(DataEncoder &encoder, ConstStringTable &strtab) const {
  encoder.AppendData(kIdentifierNameToDIE);
  encoder.AppendU32(m_map.GetSize());
  for (const auto &entry : m_map) {
    // Make sure there are no empty strings.
    assert((bool)entry.cstring);
    encoder.AppendU32(strtab.Add(entry.cstring));
    entry.value.Encode(encoder);
  }
}

bool NameToDIE::operator==(const NameToDIE &rhs) const {
  const size_t size = m_map.GetSize();
  if (size != rhs.m_map.GetSize())
    return false;
  for (size_t i = 0; i < size; ++i) {
    if (m_map.GetCStringAtIndex(i) != rhs.m_map.GetCStringAtIndex(i))
      return false;
    if (m_map.GetValueRefAtIndexUnchecked(i) !=
        rhs.m_map.GetValueRefAtIndexUnchecked(i))
      return false;
  }
  return true;
}
