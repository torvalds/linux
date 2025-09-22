//===-- hwasan_report.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of HWAddressSanitizer.
//
// Error reporting.
//===----------------------------------------------------------------------===//

#include "hwasan_report.h"

#include <dlfcn.h>

#include "hwasan.h"
#include "hwasan_allocator.h"
#include "hwasan_globals.h"
#include "hwasan_mapping.h"
#include "hwasan_thread.h"
#include "hwasan_thread_list.h"
#include "sanitizer_common/sanitizer_allocator_internal.h"
#include "sanitizer_common/sanitizer_array_ref.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_internal_defs.h"
#include "sanitizer_common/sanitizer_mutex.h"
#include "sanitizer_common/sanitizer_placement_new.h"
#include "sanitizer_common/sanitizer_report_decorator.h"
#include "sanitizer_common/sanitizer_stackdepot.h"
#include "sanitizer_common/sanitizer_stacktrace_printer.h"
#include "sanitizer_common/sanitizer_symbolizer.h"

using namespace __sanitizer;

namespace __hwasan {

class ScopedReport {
 public:
  explicit ScopedReport(bool fatal) : fatal(fatal) {
    Lock lock(&error_message_lock_);
    error_message_ptr_ = &error_message_;
    ++hwasan_report_count;
  }

  ~ScopedReport() {
    void (*report_cb)(const char *);
    {
      Lock lock(&error_message_lock_);
      report_cb = error_report_callback_;
      error_message_ptr_ = nullptr;
    }
    if (report_cb)
      report_cb(error_message_.data());
    if (fatal)
      SetAbortMessage(error_message_.data());
    if (common_flags()->print_module_map >= 2 ||
        (fatal && common_flags()->print_module_map))
      DumpProcessMap();
    if (fatal)
      Die();
  }

  static void MaybeAppendToErrorMessage(const char *msg) {
    Lock lock(&error_message_lock_);
    if (!error_message_ptr_)
      return;
    error_message_ptr_->Append(msg);
  }

  static void SetErrorReportCallback(void (*callback)(const char *)) {
    Lock lock(&error_message_lock_);
    error_report_callback_ = callback;
  }

 private:
  InternalScopedString error_message_;
  bool fatal;

  static Mutex error_message_lock_;
  static InternalScopedString *error_message_ptr_
      SANITIZER_GUARDED_BY(error_message_lock_);
  static void (*error_report_callback_)(const char *);
};

Mutex ScopedReport::error_message_lock_;
InternalScopedString *ScopedReport::error_message_ptr_;
void (*ScopedReport::error_report_callback_)(const char *);

// If there is an active ScopedReport, append to its error message.
void AppendToErrorMessageBuffer(const char *buffer) {
  ScopedReport::MaybeAppendToErrorMessage(buffer);
}

static StackTrace GetStackTraceFromId(u32 id) {
  CHECK(id);
  StackTrace res = StackDepotGet(id);
  CHECK(res.trace);
  return res;
}

static void MaybePrintAndroidHelpUrl() {
#if SANITIZER_ANDROID
  Printf(
      "Learn more about HWASan reports: "
      "https://source.android.com/docs/security/test/memory-safety/"
      "hwasan-reports\n");
#endif
}

namespace {
// A RAII object that holds a copy of the current thread stack ring buffer.
// The actual stack buffer may change while we are iterating over it (for
// example, Printf may call syslog() which can itself be built with hwasan).
class SavedStackAllocations {
 public:
  SavedStackAllocations() = default;

  explicit SavedStackAllocations(Thread *t) { CopyFrom(t); }

  void CopyFrom(Thread *t) {
    StackAllocationsRingBuffer *rb = t->stack_allocations();
    uptr size = rb->size() * sizeof(uptr);
    void *storage =
        MmapAlignedOrDieOnFatalError(size, size * 2, "saved stack allocations");
    new (&rb_) StackAllocationsRingBuffer(*rb, storage);
    thread_id_ = t->unique_id();
  }

  ~SavedStackAllocations() {
    if (rb_) {
      StackAllocationsRingBuffer *rb = get();
      UnmapOrDie(rb->StartOfStorage(), rb->size() * sizeof(uptr));
    }
  }

  const StackAllocationsRingBuffer *get() const {
    return (const StackAllocationsRingBuffer *)&rb_;
  }

  StackAllocationsRingBuffer *get() {
    return (StackAllocationsRingBuffer *)&rb_;
  }

  u32 thread_id() const { return thread_id_; }

 private:
  uptr rb_ = 0;
  u32 thread_id_;
};

class Decorator: public __sanitizer::SanitizerCommonDecorator {
 public:
  Decorator() : SanitizerCommonDecorator() { }
  const char *Access() { return Blue(); }
  const char *Allocation() const { return Magenta(); }
  const char *Origin() const { return Magenta(); }
  const char *Name() const { return Green(); }
  const char *Location() { return Green(); }
  const char *Thread() { return Green(); }
};
}  // namespace

static bool FindHeapAllocation(HeapAllocationsRingBuffer *rb, uptr tagged_addr,
                               HeapAllocationRecord *har, uptr *ring_index,
                               uptr *num_matching_addrs,
                               uptr *num_matching_addrs_4b) {
  if (!rb) return false;

  *num_matching_addrs = 0;
  *num_matching_addrs_4b = 0;
  for (uptr i = 0, size = rb->size(); i < size; i++) {
    auto h = (*rb)[i];
    if (h.tagged_addr <= tagged_addr &&
        h.tagged_addr + h.requested_size > tagged_addr) {
      *har = h;
      *ring_index = i;
      return true;
    }

    // Measure the number of heap ring buffer entries that would have matched
    // if we had only one entry per address (e.g. if the ring buffer data was
    // stored at the address itself). This will help us tune the allocator
    // implementation for MTE.
    if (UntagAddr(h.tagged_addr) <= UntagAddr(tagged_addr) &&
        UntagAddr(h.tagged_addr) + h.requested_size > UntagAddr(tagged_addr)) {
      ++*num_matching_addrs;
    }

    // Measure the number of heap ring buffer entries that would have matched
    // if we only had 4 tag bits, which is the case for MTE.
    auto untag_4b = [](uptr p) {
      return p & ((1ULL << 60) - 1);
    };
    if (untag_4b(h.tagged_addr) <= untag_4b(tagged_addr) &&
        untag_4b(h.tagged_addr) + h.requested_size > untag_4b(tagged_addr)) {
      ++*num_matching_addrs_4b;
    }
  }
  return false;
}

static void PrintStackAllocations(const StackAllocationsRingBuffer *sa,
                                  tag_t addr_tag, uptr untagged_addr) {
  uptr frames = Min((uptr)flags()->stack_history_size, sa->size());
  bool found_local = false;
  InternalScopedString location;
  for (uptr i = 0; i < frames; i++) {
    const uptr *record_addr = &(*sa)[i];
    uptr record = *record_addr;
    if (!record)
      break;
    tag_t base_tag =
        reinterpret_cast<uptr>(record_addr) >> kRecordAddrBaseTagShift;
    const uptr fp = (record >> kRecordFPShift) << kRecordFPLShift;
    CHECK_LT(fp, kRecordFPModulus);
    uptr pc_mask = (1ULL << kRecordFPShift) - 1;
    uptr pc = record & pc_mask;
    FrameInfo frame;
    if (!Symbolizer::GetOrInit()->SymbolizeFrame(pc, &frame))
      continue;
    for (LocalInfo &local : frame.locals) {
      if (!local.has_frame_offset || !local.has_size || !local.has_tag_offset)
        continue;
      if (!(local.name && internal_strlen(local.name)) &&
          !(local.function_name && internal_strlen(local.function_name)) &&
          !(local.decl_file && internal_strlen(local.decl_file)))
        continue;
      tag_t obj_tag = base_tag ^ local.tag_offset;
      if (obj_tag != addr_tag)
        continue;

      // We only store bits 4-19 of FP (bits 0-3 are guaranteed to be zero).
      // So we know only `FP % kRecordFPModulus`, and we can only calculate
      // `local_beg % kRecordFPModulus`.
      // Out of all possible `local_beg` we will only consider 2 candidates
      // nearest to the `untagged_addr`.
      uptr local_beg_mod = (fp + local.frame_offset) % kRecordFPModulus;
      // Pick `local_beg` in the same 1 MiB block as `untagged_addr`.
      uptr local_beg =
          RoundDownTo(untagged_addr, kRecordFPModulus) + local_beg_mod;
      // Pick the largest `local_beg <= untagged_addr`. It's either the current
      // one or the one before.
      if (local_beg > untagged_addr)
        local_beg -= kRecordFPModulus;

      uptr offset = -1ull;
      const char *whence;
      const char *cause = nullptr;
      uptr best_beg;

      // Try two 1 MiB blocks options and pick nearest one.
      for (uptr i = 0; i < 2; ++i, local_beg += kRecordFPModulus) {
        uptr local_end = local_beg + local.size;
        if (local_beg > local_end)
          continue;  // This is a wraparound.
        if (local_beg <= untagged_addr && untagged_addr < local_end) {
          offset = untagged_addr - local_beg;
          whence = "inside";
          cause = "use-after-scope";
          best_beg = local_beg;
          break;  // This is as close at it can be.
        }

        if (untagged_addr >= local_end) {
          uptr new_offset = untagged_addr - local_end;
          if (new_offset < offset) {
            offset = new_offset;
            whence = "after";
            cause = "stack-buffer-overflow";
            best_beg = local_beg;
          }
        } else {
          uptr new_offset = local_beg - untagged_addr;
          if (new_offset < offset) {
            offset = new_offset;
            whence = "before";
            cause = "stack-buffer-overflow";
            best_beg = local_beg;
          }
        }
      }

      // To fail the `untagged_addr` must be near nullptr, which is impossible
      // with Linux user space memory layout.
      if (!cause)
        continue;

      if (!found_local) {
        Printf("\nPotentially referenced stack objects:\n");
        found_local = true;
      }

      Decorator d;
      Printf("%s", d.Error());
      Printf("Cause: %s\n", cause);
      Printf("%s", d.Default());
      Printf("%s", d.Location());
      StackTracePrinter::GetOrInit()->RenderSourceLocation(
          &location, local.decl_file, local.decl_line, /* column= */ 0,
          common_flags()->symbolize_vs_style,
          common_flags()->strip_path_prefix);
      Printf(
          "%p is located %zd bytes %s a %zd-byte local variable %s "
          "[%p,%p) "
          "in %s %s\n",
          untagged_addr, offset, whence, local.size, local.name, best_beg,
          best_beg + local.size, local.function_name, location.data());
      location.clear();
      Printf("%s\n", d.Default());
    }
    frame.Clear();
  }

  if (found_local)
    return;

  // We didn't find any locals. Most likely we don't have symbols, so dump
  // the information that we have for offline analysis.
  InternalScopedString frame_desc;
  Printf("Previously allocated frames:\n");
  for (uptr i = 0; i < frames; i++) {
    const uptr *record_addr = &(*sa)[i];
    uptr record = *record_addr;
    if (!record)
      break;
    uptr pc_mask = (1ULL << 48) - 1;
    uptr pc = record & pc_mask;
    frame_desc.AppendF("  record_addr:%p record:0x%zx",
                       reinterpret_cast<const void *>(record_addr), record);
    SymbolizedStackHolder symbolized_stack(
        Symbolizer::GetOrInit()->SymbolizePC(pc));
    const SymbolizedStack *frame = symbolized_stack.get();
    if (frame) {
      StackTracePrinter::GetOrInit()->RenderFrame(
          &frame_desc, " %F %L", 0, frame->info.address, &frame->info,
          common_flags()->symbolize_vs_style,
          common_flags()->strip_path_prefix);
    }
    Printf("%s\n", frame_desc.data());
    frame_desc.clear();
  }
}

// Returns true if tag == *tag_ptr, reading tags from short granules if
// necessary. This may return a false positive if tags 1-15 are used as a
// regular tag rather than a short granule marker.
static bool TagsEqual(tag_t tag, tag_t *tag_ptr) {
  if (tag == *tag_ptr)
    return true;
  if (*tag_ptr == 0 || *tag_ptr > kShadowAlignment - 1)
    return false;
  uptr mem = ShadowToMem(reinterpret_cast<uptr>(tag_ptr));
  tag_t inline_tag = *reinterpret_cast<tag_t *>(mem + kShadowAlignment - 1);
  return tag == inline_tag;
}

// HWASan globals store the size of the global in the descriptor. In cases where
// we don't have a binary with symbols, we can't grab the size of the global
// from the debug info - but we might be able to retrieve it from the
// descriptor. Returns zero if the lookup failed.
static uptr GetGlobalSizeFromDescriptor(uptr ptr) {
  // Find the ELF object that this global resides in.
  Dl_info info;
  if (dladdr(reinterpret_cast<void *>(ptr), &info) == 0)
    return 0;
  auto *ehdr = reinterpret_cast<const ElfW(Ehdr) *>(info.dli_fbase);
  auto *phdr_begin = reinterpret_cast<const ElfW(Phdr) *>(
      reinterpret_cast<const u8 *>(ehdr) + ehdr->e_phoff);

  // Get the load bias. This is normally the same as the dli_fbase address on
  // position-independent code, but can be different on non-PIE executables,
  // binaries using LLD's partitioning feature, or binaries compiled with a
  // linker script.
  ElfW(Addr) load_bias = 0;
  for (const auto &phdr :
       ArrayRef<const ElfW(Phdr)>(phdr_begin, phdr_begin + ehdr->e_phnum)) {
    if (phdr.p_type != PT_LOAD || phdr.p_offset != 0)
      continue;
    load_bias = reinterpret_cast<ElfW(Addr)>(ehdr) - phdr.p_vaddr;
    break;
  }

  // Walk all globals in this ELF object, looking for the one we're interested
  // in. Once we find it, we can stop iterating and return the size of the
  // global we're interested in.
  for (const hwasan_global &global :
       HwasanGlobalsFor(load_bias, phdr_begin, ehdr->e_phnum))
    if (global.addr() <= ptr && ptr < global.addr() + global.size())
      return global.size();

  return 0;
}

void ReportStats() {}

constexpr uptr kDumpWidth = 16;
constexpr uptr kShadowLines = 17;
constexpr uptr kShadowDumpSize = kShadowLines * kDumpWidth;

constexpr uptr kShortLines = 3;
constexpr uptr kShortDumpSize = kShortLines * kDumpWidth;
constexpr uptr kShortDumpOffset = (kShadowLines - kShortLines) / 2 * kDumpWidth;

static uptr GetPrintTagStart(uptr addr) {
  addr = MemToShadow(addr);
  addr = RoundDownTo(addr, kDumpWidth);
  addr -= kDumpWidth * (kShadowLines / 2);
  return addr;
}

template <typename PrintTag>
static void PrintTagInfoAroundAddr(uptr addr, uptr num_rows,
                                   InternalScopedString &s,
                                   PrintTag print_tag) {
  uptr center_row_beg = RoundDownTo(addr, kDumpWidth);
  uptr beg_row = center_row_beg - kDumpWidth * (num_rows / 2);
  uptr end_row = center_row_beg + kDumpWidth * ((num_rows + 1) / 2);
  for (uptr row = beg_row; row < end_row; row += kDumpWidth) {
    s.Append(row == center_row_beg ? "=>" : "  ");
    s.AppendF("%p:", (void *)ShadowToMem(row));
    for (uptr i = 0; i < kDumpWidth; i++) {
      s.Append(row + i == addr ? "[" : " ");
      print_tag(s, row + i);
      s.Append(row + i == addr ? "]" : " ");
    }
    s.Append("\n");
  }
}

template <typename GetTag, typename GetShortTag>
static void PrintTagsAroundAddr(uptr addr, GetTag get_tag,
                                GetShortTag get_short_tag) {
  InternalScopedString s;
  addr = MemToShadow(addr);
  s.AppendF(
      "\nMemory tags around the buggy address (one tag corresponds to %zd "
      "bytes):\n",
      kShadowAlignment);
  PrintTagInfoAroundAddr(addr, kShadowLines, s,
                         [&](InternalScopedString &s, uptr tag_addr) {
                           tag_t tag = get_tag(tag_addr);
                           s.AppendF("%02x", tag);
                         });

  s.AppendF(
      "Tags for short granules around the buggy address (one tag corresponds "
      "to %zd bytes):\n",
      kShadowAlignment);
  PrintTagInfoAroundAddr(addr, kShortLines, s,
                         [&](InternalScopedString &s, uptr tag_addr) {
                           tag_t tag = get_tag(tag_addr);
                           if (tag >= 1 && tag <= kShadowAlignment) {
                             tag_t short_tag = get_short_tag(tag_addr);
                             s.AppendF("%02x", short_tag);
                           } else {
                             s.Append("..");
                           }
                         });
  s.Append(
      "See "
      "https://clang.llvm.org/docs/"
      "HardwareAssistedAddressSanitizerDesign.html#short-granules for a "
      "description of short granule tags\n");
  Printf("%s", s.data());
}

static uptr GetTopPc(const StackTrace *stack) {
  return stack->size ? StackTrace::GetPreviousInstructionPc(stack->trace[0])
                     : 0;
}

namespace {
class BaseReport {
 public:
  BaseReport(StackTrace *stack, bool fatal, uptr tagged_addr, uptr access_size)
      : scoped_report(fatal),
        stack(stack),
        tagged_addr(tagged_addr),
        access_size(access_size),
        untagged_addr(UntagAddr(tagged_addr)),
        ptr_tag(GetTagFromPointer(tagged_addr)),
        mismatch_offset(FindMismatchOffset()),
        heap(CopyHeapChunk()),
        allocations(CopyAllocations()),
        candidate(FindBufferOverflowCandidate()),
        shadow(CopyShadow()) {}

 protected:
  struct OverflowCandidate {
    uptr untagged_addr = 0;
    bool after = false;
    bool is_close = false;

    struct {
      uptr begin = 0;
      uptr end = 0;
      u32 thread_id = 0;
      u32 stack_id = 0;
      bool is_allocated = false;
    } heap;
  };

  struct HeapAllocation {
    HeapAllocationRecord har = {};
    uptr ring_index = 0;
    uptr num_matching_addrs = 0;
    uptr num_matching_addrs_4b = 0;
    u32 free_thread_id = 0;
  };

  struct Allocations {
    ArrayRef<SavedStackAllocations> stack;
    ArrayRef<HeapAllocation> heap;
  };

  struct HeapChunk {
    uptr begin = 0;
    uptr size = 0;
    u32 stack_id = 0;
    bool from_small_heap = false;
    bool is_allocated = false;
  };

  struct Shadow {
    uptr addr = 0;
    tag_t tags[kShadowDumpSize] = {};
    tag_t short_tags[kShortDumpSize] = {};
  };

  sptr FindMismatchOffset() const;
  Shadow CopyShadow() const;
  tag_t GetTagCopy(uptr addr) const;
  tag_t GetShortTagCopy(uptr addr) const;
  HeapChunk CopyHeapChunk() const;
  Allocations CopyAllocations();
  OverflowCandidate FindBufferOverflowCandidate() const;
  void PrintAddressDescription() const;
  void PrintHeapOrGlobalCandidate() const;
  void PrintTags(uptr addr) const;

  SavedStackAllocations stack_allocations_storage[16];
  HeapAllocation heap_allocations_storage[256];

  const ScopedReport scoped_report;
  const StackTrace *stack = nullptr;
  const uptr tagged_addr = 0;
  const uptr access_size = 0;
  const uptr untagged_addr = 0;
  const tag_t ptr_tag = 0;
  const sptr mismatch_offset = 0;

  const HeapChunk heap;
  const Allocations allocations;
  const OverflowCandidate candidate;

  const Shadow shadow;
};

sptr BaseReport::FindMismatchOffset() const {
  if (!access_size)
    return 0;
  sptr offset =
      __hwasan_test_shadow(reinterpret_cast<void *>(tagged_addr), access_size);
  CHECK_GE(offset, 0);
  CHECK_LT(offset, static_cast<sptr>(access_size));
  tag_t *tag_ptr =
      reinterpret_cast<tag_t *>(MemToShadow(untagged_addr + offset));
  tag_t mem_tag = *tag_ptr;

  if (mem_tag && mem_tag < kShadowAlignment) {
    tag_t *granule_ptr = reinterpret_cast<tag_t *>((untagged_addr + offset) &
                                                   ~(kShadowAlignment - 1));
    // If offset is 0, (untagged_addr + offset) is not aligned to granules.
    // This is the offset of the leftmost accessed byte within the bad granule.
    u8 in_granule_offset = (untagged_addr + offset) & (kShadowAlignment - 1);
    tag_t short_tag = granule_ptr[kShadowAlignment - 1];
    // The first mismatch was a short granule that matched the ptr_tag.
    if (short_tag == ptr_tag) {
      // If the access starts after the end of the short granule, then the first
      // bad byte is the first byte of the access; otherwise it is the first
      // byte past the end of the short granule
      if (mem_tag > in_granule_offset) {
        offset += mem_tag - in_granule_offset;
      }
    }
  }
  return offset;
}

BaseReport::Shadow BaseReport::CopyShadow() const {
  Shadow result;
  if (!MemIsApp(untagged_addr))
    return result;

  result.addr = GetPrintTagStart(untagged_addr + mismatch_offset);
  uptr tag_addr = result.addr;
  uptr short_end = kShortDumpOffset + ARRAY_SIZE(shadow.short_tags);
  for (uptr i = 0; i < ARRAY_SIZE(result.tags); ++i, ++tag_addr) {
    if (!MemIsShadow(tag_addr))
      continue;
    result.tags[i] = *reinterpret_cast<tag_t *>(tag_addr);
    if (i < kShortDumpOffset || i >= short_end)
      continue;
    uptr granule_addr = ShadowToMem(tag_addr);
    if (1 <= result.tags[i] && result.tags[i] <= kShadowAlignment &&
        IsAccessibleMemoryRange(granule_addr, kShadowAlignment)) {
      result.short_tags[i - kShortDumpOffset] =
          *reinterpret_cast<tag_t *>(granule_addr + kShadowAlignment - 1);
    }
  }
  return result;
}

tag_t BaseReport::GetTagCopy(uptr addr) const {
  CHECK_GE(addr, shadow.addr);
  uptr idx = addr - shadow.addr;
  CHECK_LT(idx, ARRAY_SIZE(shadow.tags));
  return shadow.tags[idx];
}

tag_t BaseReport::GetShortTagCopy(uptr addr) const {
  CHECK_GE(addr, shadow.addr + kShortDumpOffset);
  uptr idx = addr - shadow.addr - kShortDumpOffset;
  CHECK_LT(idx, ARRAY_SIZE(shadow.short_tags));
  return shadow.short_tags[idx];
}

BaseReport::HeapChunk BaseReport::CopyHeapChunk() const {
  HeapChunk result = {};
  if (MemIsShadow(untagged_addr))
    return result;
  HwasanChunkView chunk = FindHeapChunkByAddress(untagged_addr);
  result.begin = chunk.Beg();
  if (result.begin) {
    result.size = chunk.ActualSize();
    result.from_small_heap = chunk.FromSmallHeap();
    result.is_allocated = chunk.IsAllocated();
    result.stack_id = chunk.GetAllocStackId();
  }
  return result;
}

BaseReport::Allocations BaseReport::CopyAllocations() {
  if (MemIsShadow(untagged_addr))
    return {};
  uptr stack_allocations_count = 0;
  uptr heap_allocations_count = 0;
  hwasanThreadList().VisitAllLiveThreads([&](Thread *t) {
    if (stack_allocations_count < ARRAY_SIZE(stack_allocations_storage) &&
        t->AddrIsInStack(untagged_addr)) {
      stack_allocations_storage[stack_allocations_count++].CopyFrom(t);
    }

    if (heap_allocations_count < ARRAY_SIZE(heap_allocations_storage)) {
      // Scan all threads' ring buffers to find if it's a heap-use-after-free.
      HeapAllocationRecord har;
      uptr ring_index, num_matching_addrs, num_matching_addrs_4b;
      if (FindHeapAllocation(t->heap_allocations(), tagged_addr, &har,
                             &ring_index, &num_matching_addrs,
                             &num_matching_addrs_4b)) {
        auto &ha = heap_allocations_storage[heap_allocations_count++];
        ha.har = har;
        ha.ring_index = ring_index;
        ha.num_matching_addrs = num_matching_addrs;
        ha.num_matching_addrs_4b = num_matching_addrs_4b;
        ha.free_thread_id = t->unique_id();
      }
    }
  });

  return {{stack_allocations_storage, stack_allocations_count},
          {heap_allocations_storage, heap_allocations_count}};
}

BaseReport::OverflowCandidate BaseReport::FindBufferOverflowCandidate() const {
  OverflowCandidate result = {};
  if (MemIsShadow(untagged_addr))
    return result;
  // Check if this looks like a heap buffer overflow by scanning
  // the shadow left and right and looking for the first adjacent
  // object with a different memory tag. If that tag matches ptr_tag,
  // check the allocator if it has a live chunk there.
  tag_t *tag_ptr = reinterpret_cast<tag_t *>(MemToShadow(untagged_addr));
  tag_t *candidate_tag_ptr = nullptr, *left = tag_ptr, *right = tag_ptr;
  uptr candidate_distance = 0;
  for (; candidate_distance < 1000; candidate_distance++) {
    if (MemIsShadow(reinterpret_cast<uptr>(left)) && TagsEqual(ptr_tag, left)) {
      candidate_tag_ptr = left;
      break;
    }
    --left;
    if (MemIsShadow(reinterpret_cast<uptr>(right)) &&
        TagsEqual(ptr_tag, right)) {
      candidate_tag_ptr = right;
      break;
    }
    ++right;
  }

  constexpr auto kCloseCandidateDistance = 1;
  result.is_close = candidate_distance <= kCloseCandidateDistance;

  result.after = candidate_tag_ptr == left;
  result.untagged_addr = ShadowToMem(reinterpret_cast<uptr>(candidate_tag_ptr));
  HwasanChunkView chunk = FindHeapChunkByAddress(result.untagged_addr);
  if (chunk.IsAllocated()) {
    result.heap.is_allocated = true;
    result.heap.begin = chunk.Beg();
    result.heap.end = chunk.End();
    result.heap.thread_id = chunk.GetAllocThreadId();
    result.heap.stack_id = chunk.GetAllocStackId();
  }
  return result;
}

void BaseReport::PrintHeapOrGlobalCandidate() const {
  Decorator d;
  if (candidate.heap.is_allocated) {
    uptr offset;
    const char *whence;
    if (candidate.heap.begin <= untagged_addr &&
        untagged_addr < candidate.heap.end) {
      offset = untagged_addr - candidate.heap.begin;
      whence = "inside";
    } else if (candidate.after) {
      offset = untagged_addr - candidate.heap.end;
      whence = "after";
    } else {
      offset = candidate.heap.begin - untagged_addr;
      whence = "before";
    }
    Printf("%s", d.Error());
    Printf("\nCause: heap-buffer-overflow\n");
    Printf("%s", d.Default());
    Printf("%s", d.Location());
    Printf("%p is located %zd bytes %s a %zd-byte region [%p,%p)\n",
           untagged_addr, offset, whence,
           candidate.heap.end - candidate.heap.begin, candidate.heap.begin,
           candidate.heap.end);
    Printf("%s", d.Allocation());
    Printf("allocated by thread T%u here:\n", candidate.heap.thread_id);
    Printf("%s", d.Default());
    GetStackTraceFromId(candidate.heap.stack_id).Print();
    return;
  }
  // Check whether the address points into a loaded library. If so, this is
  // most likely a global variable.
  const char *module_name;
  uptr module_address;
  Symbolizer *sym = Symbolizer::GetOrInit();
  if (sym->GetModuleNameAndOffsetForPC(candidate.untagged_addr, &module_name,
                                       &module_address)) {
    Printf("%s", d.Error());
    Printf("\nCause: global-overflow\n");
    Printf("%s", d.Default());
    DataInfo info;
    Printf("%s", d.Location());
    if (sym->SymbolizeData(candidate.untagged_addr, &info) && info.start) {
      Printf(
          "%p is located %zd bytes %s a %zd-byte global variable "
          "%s [%p,%p) in %s\n",
          untagged_addr,
          candidate.after ? untagged_addr - (info.start + info.size)
                          : info.start - untagged_addr,
          candidate.after ? "after" : "before", info.size, info.name,
          info.start, info.start + info.size, module_name);
    } else {
      uptr size = GetGlobalSizeFromDescriptor(candidate.untagged_addr);
      if (size == 0)
        // We couldn't find the size of the global from the descriptors.
        Printf(
            "%p is located %s a global variable in "
            "\n    #0 0x%x (%s+0x%x)\n",
            untagged_addr, candidate.after ? "after" : "before",
            candidate.untagged_addr, module_name, module_address);
      else
        Printf(
            "%p is located %s a %zd-byte global variable in "
            "\n    #0 0x%x (%s+0x%x)\n",
            untagged_addr, candidate.after ? "after" : "before", size,
            candidate.untagged_addr, module_name, module_address);
    }
    Printf("%s", d.Default());
  }
}

void BaseReport::PrintAddressDescription() const {
  Decorator d;
  int num_descriptions_printed = 0;

  if (MemIsShadow(untagged_addr)) {
    Printf("%s%p is HWAsan shadow memory.\n%s", d.Location(), untagged_addr,
           d.Default());
    return;
  }

  // Print some very basic information about the address, if it's a heap.
  if (heap.begin) {
    Printf(
        "%s[%p,%p) is a %s %s heap chunk; "
        "size: %zd offset: %zd\n%s",
        d.Location(), heap.begin, heap.begin + heap.size,
        heap.from_small_heap ? "small" : "large",
        heap.is_allocated ? "allocated" : "unallocated", heap.size,
        untagged_addr - heap.begin, d.Default());
  }

  auto announce_by_id = [](u32 thread_id) {
    hwasanThreadList().VisitAllLiveThreads([&](Thread *t) {
      if (thread_id == t->unique_id())
        t->Announce();
    });
  };

  // Check stack first. If the address is on the stack of a live thread, we
  // know it cannot be a heap / global overflow.
  for (const auto &sa : allocations.stack) {
    Printf("%s", d.Error());
    Printf("\nCause: stack tag-mismatch\n");
    Printf("%s", d.Location());
    Printf("Address %p is located in stack of thread T%zd\n", untagged_addr,
           sa.thread_id());
    Printf("%s", d.Default());
    announce_by_id(sa.thread_id());
    PrintStackAllocations(sa.get(), ptr_tag, untagged_addr);
    num_descriptions_printed++;
  }

  if (allocations.stack.empty() && candidate.untagged_addr &&
      candidate.is_close) {
    PrintHeapOrGlobalCandidate();
    num_descriptions_printed++;
  }

  for (const auto &ha : allocations.heap) {
    const HeapAllocationRecord har = ha.har;

    Printf("%s", d.Error());
    Printf("\nCause: use-after-free\n");
    Printf("%s", d.Location());
    Printf("%p is located %zd bytes inside a %zd-byte region [%p,%p)\n",
           untagged_addr, untagged_addr - UntagAddr(har.tagged_addr),
           har.requested_size, UntagAddr(har.tagged_addr),
           UntagAddr(har.tagged_addr) + har.requested_size);
    Printf("%s", d.Allocation());
    Printf("freed by thread T%u here:\n", ha.free_thread_id);
    Printf("%s", d.Default());
    GetStackTraceFromId(har.free_context_id).Print();

    Printf("%s", d.Allocation());
    Printf("previously allocated by thread T%u here:\n", har.alloc_thread_id);
    Printf("%s", d.Default());
    GetStackTraceFromId(har.alloc_context_id).Print();

    // Print a developer note: the index of this heap object
    // in the thread's deallocation ring buffer.
    Printf("hwasan_dev_note_heap_rb_distance: %zd %zd\n", ha.ring_index + 1,
           flags()->heap_history_size);
    Printf("hwasan_dev_note_num_matching_addrs: %zd\n", ha.num_matching_addrs);
    Printf("hwasan_dev_note_num_matching_addrs_4b: %zd\n",
           ha.num_matching_addrs_4b);

    announce_by_id(ha.free_thread_id);
    // TODO: announce_by_id(har.alloc_thread_id);
    num_descriptions_printed++;
  }

  if (candidate.untagged_addr && num_descriptions_printed == 0) {
    PrintHeapOrGlobalCandidate();
    num_descriptions_printed++;
  }

  // Print the remaining threads, as an extra information, 1 line per thread.
  if (flags()->print_live_threads_info) {
    Printf("\n");
    hwasanThreadList().VisitAllLiveThreads([&](Thread *t) { t->Announce(); });
  }

  if (!num_descriptions_printed)
    // We exhausted our possibilities. Bail out.
    Printf("HWAddressSanitizer can not describe address in more detail.\n");
  if (num_descriptions_printed > 1) {
    Printf(
        "There are %d potential causes, printed above in order "
        "of likeliness.\n",
        num_descriptions_printed);
  }
}

void BaseReport::PrintTags(uptr addr) const {
  if (shadow.addr) {
    PrintTagsAroundAddr(
        addr, [&](uptr addr) { return GetTagCopy(addr); },
        [&](uptr addr) { return GetShortTagCopy(addr); });
  }
}

class InvalidFreeReport : public BaseReport {
 public:
  InvalidFreeReport(StackTrace *stack, uptr tagged_addr)
      : BaseReport(stack, flags()->halt_on_error, tagged_addr, 0) {}
  ~InvalidFreeReport();

 private:
};

InvalidFreeReport::~InvalidFreeReport() {
  Decorator d;
  Printf("%s", d.Error());
  uptr pc = GetTopPc(stack);
  const char *bug_type = "invalid-free";
  const Thread *thread = GetCurrentThread();
  if (thread) {
    Report("ERROR: %s: %s on address %p at pc %p on thread T%zd\n",
           SanitizerToolName, bug_type, untagged_addr, pc, thread->unique_id());
  } else {
    Report("ERROR: %s: %s on address %p at pc %p on unknown thread\n",
           SanitizerToolName, bug_type, untagged_addr, pc);
  }
  Printf("%s", d.Access());
  if (shadow.addr) {
    Printf("tags: %02x/%02x (ptr/mem)\n", ptr_tag,
           GetTagCopy(MemToShadow(untagged_addr)));
  }
  Printf("%s", d.Default());

  stack->Print();

  PrintAddressDescription();
  PrintTags(untagged_addr);
  MaybePrintAndroidHelpUrl();
  ReportErrorSummary(bug_type, stack);
}

class TailOverwrittenReport : public BaseReport {
 public:
  explicit TailOverwrittenReport(StackTrace *stack, uptr tagged_addr,
                                 uptr orig_size, const u8 *expected)
      : BaseReport(stack, flags()->halt_on_error, tagged_addr, 0),
        orig_size(orig_size),
        tail_size(kShadowAlignment - (orig_size % kShadowAlignment)) {
    CHECK_GT(tail_size, 0U);
    CHECK_LT(tail_size, kShadowAlignment);
    internal_memcpy(tail_copy,
                    reinterpret_cast<u8 *>(untagged_addr + orig_size),
                    tail_size);
    internal_memcpy(actual_expected, expected, tail_size);
    // Short granule is stashed in the last byte of the magic string. To avoid
    // confusion, make the expected magic string contain the short granule tag.
    if (orig_size % kShadowAlignment != 0)
      actual_expected[tail_size - 1] = ptr_tag;
  }
  ~TailOverwrittenReport();

 private:
  const uptr orig_size = 0;
  const uptr tail_size = 0;
  u8 actual_expected[kShadowAlignment] = {};
  u8 tail_copy[kShadowAlignment] = {};
};

TailOverwrittenReport::~TailOverwrittenReport() {
  Decorator d;
  Printf("%s", d.Error());
  const char *bug_type = "allocation-tail-overwritten";
  Report("ERROR: %s: %s; heap object [%p,%p) of size %zd\n", SanitizerToolName,
         bug_type, untagged_addr, untagged_addr + orig_size, orig_size);
  Printf("\n%s", d.Default());
  Printf(
      "Stack of invalid access unknown. Issue detected at deallocation "
      "time.\n");
  Printf("%s", d.Allocation());
  Printf("deallocated here:\n");
  Printf("%s", d.Default());
  stack->Print();
  if (heap.begin) {
    Printf("%s", d.Allocation());
    Printf("allocated here:\n");
    Printf("%s", d.Default());
    GetStackTraceFromId(heap.stack_id).Print();
  }

  InternalScopedString s;
  u8 *tail = tail_copy;
  s.Append("Tail contains: ");
  for (uptr i = 0; i < kShadowAlignment - tail_size; i++) s.Append(".. ");
  for (uptr i = 0; i < tail_size; i++) s.AppendF("%02x ", tail[i]);
  s.Append("\n");
  s.Append("Expected:      ");
  for (uptr i = 0; i < kShadowAlignment - tail_size; i++) s.Append(".. ");
  for (uptr i = 0; i < tail_size; i++) s.AppendF("%02x ", actual_expected[i]);
  s.Append("\n");
  s.Append("               ");
  for (uptr i = 0; i < kShadowAlignment - tail_size; i++) s.Append("   ");
  for (uptr i = 0; i < tail_size; i++)
    s.AppendF("%s ", actual_expected[i] != tail[i] ? "^^" : "  ");

  s.AppendF(
      "\nThis error occurs when a buffer overflow overwrites memory\n"
      "after a heap object, but within the %zd-byte granule, e.g.\n"
      "   char *x = new char[20];\n"
      "   x[25] = 42;\n"
      "%s does not detect such bugs in uninstrumented code at the time of "
      "write,"
      "\nbut can detect them at the time of free/delete.\n"
      "To disable this feature set HWASAN_OPTIONS=free_checks_tail_magic=0\n",
      kShadowAlignment, SanitizerToolName);
  Printf("%s", s.data());
  GetCurrentThread()->Announce();
  PrintTags(untagged_addr);
  MaybePrintAndroidHelpUrl();
  ReportErrorSummary(bug_type, stack);
}

class TagMismatchReport : public BaseReport {
 public:
  explicit TagMismatchReport(StackTrace *stack, uptr tagged_addr,
                             uptr access_size, bool is_store, bool fatal,
                             uptr *registers_frame)
      : BaseReport(stack, fatal, tagged_addr, access_size),
        is_store(is_store),
        registers_frame(registers_frame) {}
  ~TagMismatchReport();

 private:
  const bool is_store;
  const uptr *registers_frame;
};

TagMismatchReport::~TagMismatchReport() {
  Decorator d;
  // TODO: when possible, try to print heap-use-after-free, etc.
  const char *bug_type = "tag-mismatch";
  uptr pc = GetTopPc(stack);
  Printf("%s", d.Error());
  Report("ERROR: %s: %s on address %p at pc %p\n", SanitizerToolName, bug_type,
         untagged_addr, pc);

  Thread *t = GetCurrentThread();

  tag_t mem_tag = GetTagCopy(MemToShadow(untagged_addr + mismatch_offset));

  Printf("%s", d.Access());
  if (mem_tag && mem_tag < kShadowAlignment) {
    tag_t short_tag =
        GetShortTagCopy(MemToShadow(untagged_addr + mismatch_offset));
    Printf(
        "%s of size %zu at %p tags: %02x/%02x(%02x) (ptr/mem) in thread T%zd\n",
        is_store ? "WRITE" : "READ", access_size, untagged_addr, ptr_tag,
        mem_tag, short_tag, t->unique_id());
  } else {
    Printf("%s of size %zu at %p tags: %02x/%02x (ptr/mem) in thread T%zd\n",
           is_store ? "WRITE" : "READ", access_size, untagged_addr, ptr_tag,
           mem_tag, t->unique_id());
  }
  if (mismatch_offset)
    Printf("Invalid access starting at offset %zu\n", mismatch_offset);
  Printf("%s", d.Default());

  stack->Print();

  PrintAddressDescription();
  t->Announce();

  PrintTags(untagged_addr + mismatch_offset);

  if (registers_frame)
    ReportRegisters(registers_frame, pc);

  MaybePrintAndroidHelpUrl();
  ReportErrorSummary(bug_type, stack);
}
}  // namespace

void ReportInvalidFree(StackTrace *stack, uptr tagged_addr) {
  InvalidFreeReport R(stack, tagged_addr);
}

void ReportTailOverwritten(StackTrace *stack, uptr tagged_addr, uptr orig_size,
                           const u8 *expected) {
  TailOverwrittenReport R(stack, tagged_addr, orig_size, expected);
}

void ReportTagMismatch(StackTrace *stack, uptr tagged_addr, uptr access_size,
                       bool is_store, bool fatal, uptr *registers_frame) {
  TagMismatchReport R(stack, tagged_addr, access_size, is_store, fatal,
                      registers_frame);
}

// See the frame breakdown defined in __hwasan_tag_mismatch (from
// hwasan_tag_mismatch_{aarch64,riscv64}.S).
void ReportRegisters(const uptr *frame, uptr pc) {
  Printf("\nRegisters where the failure occurred (pc %p):\n", pc);

  // We explicitly print a single line (4 registers/line) each iteration to
  // reduce the amount of logcat error messages printed. Each Printf() will
  // result in a new logcat line, irrespective of whether a newline is present,
  // and so we wish to reduce the number of Printf() calls we have to make.
#if defined(__aarch64__)
  Printf("    x0  %016llx  x1  %016llx  x2  %016llx  x3  %016llx\n",
       frame[0], frame[1], frame[2], frame[3]);
#elif SANITIZER_RISCV64
  Printf("    sp  %016llx  x1  %016llx  x2  %016llx  x3  %016llx\n",
         reinterpret_cast<const u8 *>(frame) + 256, frame[1], frame[2],
         frame[3]);
#endif
  Printf("    x4  %016llx  x5  %016llx  x6  %016llx  x7  %016llx\n",
       frame[4], frame[5], frame[6], frame[7]);
  Printf("    x8  %016llx  x9  %016llx  x10 %016llx  x11 %016llx\n",
       frame[8], frame[9], frame[10], frame[11]);
  Printf("    x12 %016llx  x13 %016llx  x14 %016llx  x15 %016llx\n",
       frame[12], frame[13], frame[14], frame[15]);
  Printf("    x16 %016llx  x17 %016llx  x18 %016llx  x19 %016llx\n",
       frame[16], frame[17], frame[18], frame[19]);
  Printf("    x20 %016llx  x21 %016llx  x22 %016llx  x23 %016llx\n",
       frame[20], frame[21], frame[22], frame[23]);
  Printf("    x24 %016llx  x25 %016llx  x26 %016llx  x27 %016llx\n",
       frame[24], frame[25], frame[26], frame[27]);
  // hwasan_check* reduces the stack pointer by 256, then __hwasan_tag_mismatch
  // passes it to this function.
#if defined(__aarch64__)
  Printf("    x28 %016llx  x29 %016llx  x30 %016llx   sp %016llx\n", frame[28],
         frame[29], frame[30], reinterpret_cast<const u8 *>(frame) + 256);
#elif SANITIZER_RISCV64
  Printf("    x28 %016llx  x29 %016llx  x30 %016llx  x31 %016llx\n", frame[28],
         frame[29], frame[30], frame[31]);
#else
#endif
}

}  // namespace __hwasan

void __hwasan_set_error_report_callback(void (*callback)(const char *)) {
  __hwasan::ScopedReport::SetErrorReportCallback(callback);
}
