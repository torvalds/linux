//===-- Timer.cpp ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "lldb/Utility/Timer.h"
#include "lldb/Utility/Stream.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Signposts.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

#include <cassert>
#include <cinttypes>
#include <cstdarg>
#include <cstdio>

using namespace lldb_private;

#define TIMER_INDENT_AMOUNT 2

namespace {
typedef std::vector<Timer *> TimerStack;
static std::atomic<Timer::Category *> g_categories;
} // end of anonymous namespace

/// Allows llvm::Timer to emit signposts when supported.
static llvm::ManagedStatic<llvm::SignpostEmitter> Signposts;

std::atomic<bool> Timer::g_quiet(true);
std::atomic<unsigned> Timer::g_display_depth(0);
static std::mutex &GetFileMutex() {
  static std::mutex *g_file_mutex_ptr = new std::mutex();
  return *g_file_mutex_ptr;
}

static TimerStack &GetTimerStackForCurrentThread() {
  static thread_local TimerStack g_stack;
  return g_stack;
}

Timer::Category::Category(const char *cat) : m_name(cat) {
  m_nanos.store(0, std::memory_order_release);
  m_nanos_total.store(0, std::memory_order_release);
  m_count.store(0, std::memory_order_release);
  Category *expected = g_categories;
  do {
    m_next = expected;
  } while (!g_categories.compare_exchange_weak(expected, this));
}

void Timer::SetQuiet(bool value) { g_quiet = value; }

Timer::Timer(Timer::Category &category, const char *format, ...)
    : m_category(category), m_total_start(std::chrono::steady_clock::now()) {
  Signposts->startInterval(this, m_category.GetName());
  TimerStack &stack = GetTimerStackForCurrentThread();

  stack.push_back(this);
  if (!g_quiet && stack.size() <= g_display_depth) {
    std::lock_guard<std::mutex> lock(GetFileMutex());

    // Indent
    ::fprintf(stdout, "%*s", int(stack.size() - 1) * TIMER_INDENT_AMOUNT, "");
    // Print formatted string
    va_list args;
    va_start(args, format);
    ::vfprintf(stdout, format, args);
    va_end(args);

    // Newline
    ::fprintf(stdout, "\n");
  }
}

Timer::~Timer() {
  using namespace std::chrono;

  auto stop_time = steady_clock::now();
  auto total_dur = stop_time - m_total_start;
  auto timer_dur = total_dur - m_child_duration;

  Signposts->endInterval(this, m_category.GetName());

  TimerStack &stack = GetTimerStackForCurrentThread();
  if (!g_quiet && stack.size() <= g_display_depth) {
    std::lock_guard<std::mutex> lock(GetFileMutex());
    ::fprintf(stdout, "%*s%.9f sec (%.9f sec)\n",
              int(stack.size() - 1) * TIMER_INDENT_AMOUNT, "",
              duration<double>(total_dur).count(),
              duration<double>(timer_dur).count());
  }

  assert(stack.back() == this);
  stack.pop_back();
  if (!stack.empty())
    stack.back()->ChildDuration(total_dur);

  // Keep total results for each category so we can dump results.
  m_category.m_nanos += std::chrono::nanoseconds(timer_dur).count();
  m_category.m_nanos_total += std::chrono::nanoseconds(total_dur).count();
  m_category.m_count++;
}

void Timer::SetDisplayDepth(uint32_t depth) { g_display_depth = depth; }

/* binary function predicate:
 * - returns whether a person is less than another person
 */
namespace {
struct Stats {
  const char *name;
  uint64_t nanos;
  uint64_t nanos_total;
  uint64_t count;
};
} // namespace

static bool CategoryMapIteratorSortCriterion(const Stats &lhs,
                                             const Stats &rhs) {
  return lhs.nanos > rhs.nanos;
}

void Timer::ResetCategoryTimes() {
  for (Category *i = g_categories; i; i = i->m_next) {
    i->m_nanos.store(0, std::memory_order_release);
    i->m_nanos_total.store(0, std::memory_order_release);
    i->m_count.store(0, std::memory_order_release);
  }
}

void Timer::DumpCategoryTimes(Stream &s) {
  std::vector<Stats> sorted;
  for (Category *i = g_categories; i; i = i->m_next) {
    uint64_t nanos = i->m_nanos.load(std::memory_order_acquire);
    if (nanos) {
      uint64_t nanos_total = i->m_nanos_total.load(std::memory_order_acquire);
      uint64_t count = i->m_count.load(std::memory_order_acquire);
      Stats stats{i->m_name, nanos, nanos_total, count};
      sorted.push_back(stats);
    }
  }
  if (sorted.empty())
    return; // Later code will break without any elements.

  // Sort by time
  llvm::sort(sorted, CategoryMapIteratorSortCriterion);

  for (const auto &stats : sorted)
    s.Printf("%.9f sec (total: %.3fs; child: %.3fs; count: %" PRIu64
             ") for %s\n",
             stats.nanos / 1000000000., stats.nanos_total / 1000000000.,
             (stats.nanos_total - stats.nanos) / 1000000000., stats.count,
             stats.name);
}
