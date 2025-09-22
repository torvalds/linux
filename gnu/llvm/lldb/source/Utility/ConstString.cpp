//===-- ConstString.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/ConstString.h"

#include "lldb/Utility/Stream.h"

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/iterator.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/DJB.h"
#include "llvm/Support/FormatProviders.h"
#include "llvm/Support/RWMutex.h"
#include "llvm/Support/Threading.h"

#include <array>
#include <utility>

#include <cinttypes>
#include <cstdint>
#include <cstring>

using namespace lldb_private;

class Pool {
public:
  /// The default BumpPtrAllocatorImpl slab size.
  static const size_t AllocatorSlabSize = 4096;
  static const size_t SizeThreshold = AllocatorSlabSize;
  /// Every Pool has its own allocator which receives an equal share of
  /// the ConstString allocations. This means that when allocating many
  /// ConstStrings, every allocator sees only its small share of allocations and
  /// assumes LLDB only allocated a small amount of memory so far. In reality
  /// LLDB allocated a total memory that is N times as large as what the
  /// allocator sees (where N is the number of string pools). This causes that
  /// the BumpPtrAllocator continues a long time to allocate memory in small
  /// chunks which only makes sense when allocating a small amount of memory
  /// (which is true from the perspective of a single allocator). On some
  /// systems doing all these small memory allocations causes LLDB to spend
  /// a lot of time in malloc, so we need to force all these allocators to
  /// behave like one allocator in terms of scaling their memory allocations
  /// with increased demand. To do this we set the growth delay for each single
  /// allocator to a rate so that our pool of allocators scales their memory
  /// allocations similar to a single BumpPtrAllocatorImpl.
  ///
  /// Currently we have 256 string pools and the normal growth delay of the
  /// BumpPtrAllocatorImpl is 128 (i.e., the memory allocation size increases
  /// every 128 full chunks), so by changing the delay to 1 we get a
  /// total growth delay in our allocator collection of 256/1 = 256. This is
  /// still only half as fast as a normal allocator but we can't go any faster
  /// without decreasing the number of string pools.
  static const size_t AllocatorGrowthDelay = 1;
  typedef llvm::BumpPtrAllocatorImpl<llvm::MallocAllocator, AllocatorSlabSize,
                                     SizeThreshold, AllocatorGrowthDelay>
      Allocator;
  typedef const char *StringPoolValueType;
  typedef llvm::StringMap<StringPoolValueType, Allocator> StringPool;
  typedef llvm::StringMapEntry<StringPoolValueType> StringPoolEntryType;

  static StringPoolEntryType &
  GetStringMapEntryFromKeyData(const char *keyData) {
    return StringPoolEntryType::GetStringMapEntryFromKeyData(keyData);
  }

  static size_t GetConstCStringLength(const char *ccstr) {
    if (ccstr != nullptr) {
      // Since the entry is read only, and we derive the entry entirely from
      // the pointer, we don't need the lock.
      const StringPoolEntryType &entry = GetStringMapEntryFromKeyData(ccstr);
      return entry.getKey().size();
    }
    return 0;
  }

  StringPoolValueType GetMangledCounterpart(const char *ccstr) {
    if (ccstr != nullptr) {
      const PoolEntry &pool = selectPool(llvm::StringRef(ccstr));
      llvm::sys::SmartScopedReader<false> rlock(pool.m_mutex);
      return GetStringMapEntryFromKeyData(ccstr).getValue();
    }
    return nullptr;
  }

  const char *GetConstCString(const char *cstr) {
    if (cstr != nullptr)
      return GetConstCStringWithLength(cstr, strlen(cstr));
    return nullptr;
  }

  const char *GetConstCStringWithLength(const char *cstr, size_t cstr_len) {
    if (cstr != nullptr)
      return GetConstCStringWithStringRef(llvm::StringRef(cstr, cstr_len));
    return nullptr;
  }

  const char *GetConstCStringWithStringRef(llvm::StringRef string_ref) {
    if (string_ref.data()) {
      const uint32_t string_hash = StringPool::hash(string_ref);
      PoolEntry &pool = selectPool(string_hash);

      {
        llvm::sys::SmartScopedReader<false> rlock(pool.m_mutex);
        auto it = pool.m_string_map.find(string_ref, string_hash);
        if (it != pool.m_string_map.end())
          return it->getKeyData();
      }

      llvm::sys::SmartScopedWriter<false> wlock(pool.m_mutex);
      StringPoolEntryType &entry =
          *pool.m_string_map
               .insert(std::make_pair(string_ref, nullptr), string_hash)
               .first;
      return entry.getKeyData();
    }
    return nullptr;
  }

  const char *
  GetConstCStringAndSetMangledCounterPart(llvm::StringRef demangled,
                                          const char *mangled_ccstr) {
    const char *demangled_ccstr = nullptr;

    {
      const uint32_t demangled_hash = StringPool::hash(demangled);
      PoolEntry &pool = selectPool(demangled_hash);
      llvm::sys::SmartScopedWriter<false> wlock(pool.m_mutex);

      // Make or update string pool entry with the mangled counterpart
      StringPool &map = pool.m_string_map;
      StringPoolEntryType &entry =
          *map.try_emplace_with_hash(demangled, demangled_hash).first;

      entry.second = mangled_ccstr;

      // Extract the const version of the demangled_cstr
      demangled_ccstr = entry.getKeyData();
    }

    {
      // Now assign the demangled const string as the counterpart of the
      // mangled const string...
      PoolEntry &pool = selectPool(llvm::StringRef(mangled_ccstr));
      llvm::sys::SmartScopedWriter<false> wlock(pool.m_mutex);
      GetStringMapEntryFromKeyData(mangled_ccstr).setValue(demangled_ccstr);
    }

    // Return the constant demangled C string
    return demangled_ccstr;
  }

  const char *GetConstTrimmedCStringWithLength(const char *cstr,
                                               size_t cstr_len) {
    if (cstr != nullptr) {
      const size_t trimmed_len = strnlen(cstr, cstr_len);
      return GetConstCStringWithLength(cstr, trimmed_len);
    }
    return nullptr;
  }

  ConstString::MemoryStats GetMemoryStats() const {
    ConstString::MemoryStats stats;
    for (const auto &pool : m_string_pools) {
      llvm::sys::SmartScopedReader<false> rlock(pool.m_mutex);
      const Allocator &alloc = pool.m_string_map.getAllocator();
      stats.bytes_total += alloc.getTotalMemory();
      stats.bytes_used += alloc.getBytesAllocated();
    }
    return stats;
  }

protected:
  struct PoolEntry {
    mutable llvm::sys::SmartRWMutex<false> m_mutex;
    StringPool m_string_map;
  };

  std::array<PoolEntry, 256> m_string_pools;

  PoolEntry &selectPool(const llvm::StringRef &s) {
    return selectPool(StringPool::hash(s));
  }

  PoolEntry &selectPool(uint32_t h) {
    return m_string_pools[((h >> 24) ^ (h >> 16) ^ (h >> 8) ^ h) & 0xff];
  }
};

// Frameworks and dylibs aren't supposed to have global C++ initializers so we
// hide the string pool in a static function so that it will get initialized on
// the first call to this static function.
//
// Note, for now we make the string pool a pointer to the pool, because we
// can't guarantee that some objects won't get destroyed after the global
// destructor chain is run, and trying to make sure no destructors touch
// ConstStrings is difficult.  So we leak the pool instead.
static Pool &StringPool() {
  static llvm::once_flag g_pool_initialization_flag;
  static Pool *g_string_pool = nullptr;

  llvm::call_once(g_pool_initialization_flag,
                  []() { g_string_pool = new Pool(); });

  return *g_string_pool;
}

ConstString::ConstString(const char *cstr)
    : m_string(StringPool().GetConstCString(cstr)) {}

ConstString::ConstString(const char *cstr, size_t cstr_len)
    : m_string(StringPool().GetConstCStringWithLength(cstr, cstr_len)) {}

ConstString::ConstString(llvm::StringRef s)
    : m_string(StringPool().GetConstCStringWithStringRef(s)) {}

bool ConstString::operator<(ConstString rhs) const {
  if (m_string == rhs.m_string)
    return false;

  llvm::StringRef lhs_string_ref(GetStringRef());
  llvm::StringRef rhs_string_ref(rhs.GetStringRef());

  // If both have valid C strings, then return the comparison
  if (lhs_string_ref.data() && rhs_string_ref.data())
    return lhs_string_ref < rhs_string_ref;

  // Else one of them was nullptr, so if LHS is nullptr then it is less than
  return lhs_string_ref.data() == nullptr;
}

Stream &lldb_private::operator<<(Stream &s, ConstString str) {
  const char *cstr = str.GetCString();
  if (cstr != nullptr)
    s << cstr;

  return s;
}

size_t ConstString::GetLength() const {
  return Pool::GetConstCStringLength(m_string);
}

bool ConstString::Equals(ConstString lhs, ConstString rhs,
                         const bool case_sensitive) {
  if (lhs.m_string == rhs.m_string)
    return true;

  // Since the pointers weren't equal, and identical ConstStrings always have
  // identical pointers, the result must be false for case sensitive equality
  // test.
  if (case_sensitive)
    return false;

  // perform case insensitive equality test
  llvm::StringRef lhs_string_ref(lhs.GetStringRef());
  llvm::StringRef rhs_string_ref(rhs.GetStringRef());
  return lhs_string_ref.equals_insensitive(rhs_string_ref);
}

int ConstString::Compare(ConstString lhs, ConstString rhs,
                         const bool case_sensitive) {
  // If the iterators are the same, this is the same string
  const char *lhs_cstr = lhs.m_string;
  const char *rhs_cstr = rhs.m_string;
  if (lhs_cstr == rhs_cstr)
    return 0;
  if (lhs_cstr && rhs_cstr) {
    llvm::StringRef lhs_string_ref(lhs.GetStringRef());
    llvm::StringRef rhs_string_ref(rhs.GetStringRef());

    if (case_sensitive) {
      return lhs_string_ref.compare(rhs_string_ref);
    } else {
      return lhs_string_ref.compare_insensitive(rhs_string_ref);
    }
  }

  if (lhs_cstr)
    return +1; // LHS isn't nullptr but RHS is
  else
    return -1; // LHS is nullptr but RHS isn't
}

void ConstString::Dump(Stream *s, const char *fail_value) const {
  if (s != nullptr) {
    const char *cstr = AsCString(fail_value);
    if (cstr != nullptr)
      s->PutCString(cstr);
  }
}

void ConstString::DumpDebug(Stream *s) const {
  const char *cstr = GetCString();
  size_t cstr_len = GetLength();
  // Only print the parens if we have a non-nullptr string
  const char *parens = cstr ? "\"" : "";
  s->Printf("%*p: ConstString, string = %s%s%s, length = %" PRIu64,
            static_cast<int>(sizeof(void *) * 2),
            static_cast<const void *>(this), parens, cstr, parens,
            static_cast<uint64_t>(cstr_len));
}

void ConstString::SetCString(const char *cstr) {
  m_string = StringPool().GetConstCString(cstr);
}

void ConstString::SetString(llvm::StringRef s) {
  m_string = StringPool().GetConstCStringWithStringRef(s);
}

void ConstString::SetStringWithMangledCounterpart(llvm::StringRef demangled,
                                                  ConstString mangled) {
  m_string = StringPool().GetConstCStringAndSetMangledCounterPart(
      demangled, mangled.m_string);
}

bool ConstString::GetMangledCounterpart(ConstString &counterpart) const {
  counterpart.m_string = StringPool().GetMangledCounterpart(m_string);
  return (bool)counterpart;
}

void ConstString::SetCStringWithLength(const char *cstr, size_t cstr_len) {
  m_string = StringPool().GetConstCStringWithLength(cstr, cstr_len);
}

void ConstString::SetTrimmedCStringWithLength(const char *cstr,
                                              size_t cstr_len) {
  m_string = StringPool().GetConstTrimmedCStringWithLength(cstr, cstr_len);
}

ConstString::MemoryStats ConstString::GetMemoryStats() {
  return StringPool().GetMemoryStats();
}

void llvm::format_provider<ConstString>::format(const ConstString &CS,
                                                llvm::raw_ostream &OS,
                                                llvm::StringRef Options) {
  format_provider<StringRef>::format(CS.GetStringRef(), OS, Options);
}
