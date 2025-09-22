//===-- Log.cpp -----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/Log.h"
#include "lldb/Utility/VASPrintf.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/iterator.h"

#include "llvm/Support/Casting.h"
#include "llvm/Support/Chrono.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/raw_ostream.h"

#include <chrono>
#include <cstdarg>
#include <mutex>
#include <utility>

#include <cassert>
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

using namespace lldb_private;

char LogHandler::ID;
char StreamLogHandler::ID;
char CallbackLogHandler::ID;
char RotatingLogHandler::ID;
char TeeLogHandler::ID;

llvm::ManagedStatic<Log::ChannelMap> Log::g_channel_map;

void Log::ForEachCategory(
    const Log::ChannelMap::value_type &entry,
    llvm::function_ref<void(llvm::StringRef, llvm::StringRef)> lambda) {
  lambda("all", "all available logging categories");
  lambda("default", "default set of logging categories");
  for (const auto &category : entry.second.m_channel.categories)
    lambda(category.name, category.description);
}

void Log::ListCategories(llvm::raw_ostream &stream,
                         const ChannelMap::value_type &entry) {
  stream << llvm::formatv("Logging categories for '{0}':\n", entry.first());
  ForEachCategory(entry,
                  [&stream](llvm::StringRef name, llvm::StringRef description) {
                    stream << llvm::formatv("  {0} - {1}\n", name, description);
                  });
}

Log::MaskType Log::GetFlags(llvm::raw_ostream &stream,
                            const ChannelMap::value_type &entry,
                            llvm::ArrayRef<const char *> categories) {
  bool list_categories = false;
  Log::MaskType flags = 0;
  for (const char *category : categories) {
    if (llvm::StringRef("all").equals_insensitive(category)) {
      flags |= std::numeric_limits<Log::MaskType>::max();
      continue;
    }
    if (llvm::StringRef("default").equals_insensitive(category)) {
      flags |= entry.second.m_channel.default_flags;
      continue;
    }
    auto cat = llvm::find_if(entry.second.m_channel.categories,
                             [&](const Log::Category &c) {
                               return c.name.equals_insensitive(category);
                             });
    if (cat != entry.second.m_channel.categories.end()) {
      flags |= cat->flag;
      continue;
    }
    stream << llvm::formatv("error: unrecognized log category '{0}'\n",
                            category);
    list_categories = true;
  }
  if (list_categories)
    ListCategories(stream, entry);
  return flags;
}

void Log::Enable(const std::shared_ptr<LogHandler> &handler_sp,
                 uint32_t options, Log::MaskType flags) {
  llvm::sys::ScopedWriter lock(m_mutex);

  MaskType mask = m_mask.fetch_or(flags, std::memory_order_relaxed);
  if (mask | flags) {
    m_options.store(options, std::memory_order_relaxed);
    m_handler = handler_sp;
    m_channel.log_ptr.store(this, std::memory_order_relaxed);
  }
}

void Log::Disable(Log::MaskType flags) {
  llvm::sys::ScopedWriter lock(m_mutex);

  MaskType mask = m_mask.fetch_and(~flags, std::memory_order_relaxed);
  if (!(mask & ~flags)) {
    m_handler.reset();
    m_channel.log_ptr.store(nullptr, std::memory_order_relaxed);
  }
}

bool Log::Dump(llvm::raw_ostream &output_stream) {
  llvm::sys::ScopedReader lock(m_mutex);
  if (RotatingLogHandler *handler =
          llvm::dyn_cast_or_null<RotatingLogHandler>(m_handler.get())) {
    handler->Dump(output_stream);
    return true;
  }
  return false;
}

const Flags Log::GetOptions() const {
  return m_options.load(std::memory_order_relaxed);
}

Log::MaskType Log::GetMask() const {
  return m_mask.load(std::memory_order_relaxed);
}

void Log::PutCString(const char *cstr) { PutString(cstr); }

void Log::PutString(llvm::StringRef str) {
  std::string FinalMessage;
  llvm::raw_string_ostream Stream(FinalMessage);
  WriteHeader(Stream, "", "");
  Stream << str << "\n";
  WriteMessage(FinalMessage);
}

// Simple variable argument logging with flags.
void Log::Printf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  VAPrintf(format, args);
  va_end(args);
}

void Log::VAPrintf(const char *format, va_list args) {
  llvm::SmallString<64> Content;
  lldb_private::VASprintf(Content, format, args);
  PutString(Content);
}

void Log::Formatf(llvm::StringRef file, llvm::StringRef function,
                  const char *format, ...) {
  va_list args;
  va_start(args, format);
  VAFormatf(file, function, format, args);
  va_end(args);
}

void Log::VAFormatf(llvm::StringRef file, llvm::StringRef function,
                    const char *format, va_list args) {
  llvm::SmallString<64> Content;
  lldb_private::VASprintf(Content, format, args);
  Format(file, function, llvm::formatv("{0}", Content));
}

// Printing of errors that are not fatal.
void Log::Error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  VAError(format, args);
  va_end(args);
}

void Log::VAError(const char *format, va_list args) {
  llvm::SmallString<64> Content;
  VASprintf(Content, format, args);

  Printf("error: %s", Content.c_str());
}

// Printing of warnings that are not fatal only if verbose mode is enabled.
void Log::Verbose(const char *format, ...) {
  if (!GetVerbose())
    return;

  va_list args;
  va_start(args, format);
  VAPrintf(format, args);
  va_end(args);
}

// Printing of warnings that are not fatal.
void Log::Warning(const char *format, ...) {
  llvm::SmallString<64> Content;
  va_list args;
  va_start(args, format);
  VASprintf(Content, format, args);
  va_end(args);

  Printf("warning: %s", Content.c_str());
}

void Log::Register(llvm::StringRef name, Channel &channel) {
  auto iter = g_channel_map->try_emplace(name, channel);
  assert(iter.second == true);
  UNUSED_IF_ASSERT_DISABLED(iter);
}

void Log::Unregister(llvm::StringRef name) {
  auto iter = g_channel_map->find(name);
  assert(iter != g_channel_map->end());
  iter->second.Disable(std::numeric_limits<MaskType>::max());
  g_channel_map->erase(iter);
}

bool Log::EnableLogChannel(const std::shared_ptr<LogHandler> &log_handler_sp,
                           uint32_t log_options, llvm::StringRef channel,
                           llvm::ArrayRef<const char *> categories,
                           llvm::raw_ostream &error_stream) {
  auto iter = g_channel_map->find(channel);
  if (iter == g_channel_map->end()) {
    error_stream << llvm::formatv("Invalid log channel '{0}'.\n", channel);
    return false;
  }
  MaskType flags = categories.empty()
                       ? iter->second.m_channel.default_flags
                       : GetFlags(error_stream, *iter, categories);
  iter->second.Enable(log_handler_sp, log_options, flags);
  return true;
}

bool Log::DisableLogChannel(llvm::StringRef channel,
                            llvm::ArrayRef<const char *> categories,
                            llvm::raw_ostream &error_stream) {
  auto iter = g_channel_map->find(channel);
  if (iter == g_channel_map->end()) {
    error_stream << llvm::formatv("Invalid log channel '{0}'.\n", channel);
    return false;
  }
  MaskType flags = categories.empty()
                       ? std::numeric_limits<MaskType>::max()
                       : GetFlags(error_stream, *iter, categories);
  iter->second.Disable(flags);
  return true;
}

bool Log::DumpLogChannel(llvm::StringRef channel,
                         llvm::raw_ostream &output_stream,
                         llvm::raw_ostream &error_stream) {
  auto iter = g_channel_map->find(channel);
  if (iter == g_channel_map->end()) {
    error_stream << llvm::formatv("Invalid log channel '{0}'.\n", channel);
    return false;
  }
  if (!iter->second.Dump(output_stream)) {
    error_stream << llvm::formatv(
        "log channel '{0}' does not support dumping.\n", channel);
    return false;
  }
  return true;
}

bool Log::ListChannelCategories(llvm::StringRef channel,
                                llvm::raw_ostream &stream) {
  auto ch = g_channel_map->find(channel);
  if (ch == g_channel_map->end()) {
    stream << llvm::formatv("Invalid log channel '{0}'.\n", channel);
    return false;
  }
  ListCategories(stream, *ch);
  return true;
}

void Log::DisableAllLogChannels() {
  for (auto &entry : *g_channel_map)
    entry.second.Disable(std::numeric_limits<MaskType>::max());
}

void Log::ForEachChannelCategory(
    llvm::StringRef channel,
    llvm::function_ref<void(llvm::StringRef, llvm::StringRef)> lambda) {
  auto ch = g_channel_map->find(channel);
  if (ch == g_channel_map->end())
    return;

  ForEachCategory(*ch, lambda);
}

std::vector<llvm::StringRef> Log::ListChannels() {
  std::vector<llvm::StringRef> result;
  for (const auto &channel : *g_channel_map)
    result.push_back(channel.first());
  return result;
}

void Log::ListAllLogChannels(llvm::raw_ostream &stream) {
  if (g_channel_map->empty()) {
    stream << "No logging channels are currently registered.\n";
    return;
  }

  for (const auto &channel : *g_channel_map)
    ListCategories(stream, channel);
}

bool Log::GetVerbose() const {
  return m_options.load(std::memory_order_relaxed) & LLDB_LOG_OPTION_VERBOSE;
}

void Log::WriteHeader(llvm::raw_ostream &OS, llvm::StringRef file,
                      llvm::StringRef function) {
  Flags options = GetOptions();
  static uint32_t g_sequence_id = 0;
  // Add a sequence ID if requested
  if (options.Test(LLDB_LOG_OPTION_PREPEND_SEQUENCE))
    OS << ++g_sequence_id << " ";

  // Timestamp if requested
  if (options.Test(LLDB_LOG_OPTION_PREPEND_TIMESTAMP)) {
    auto now = std::chrono::duration<double>(
        std::chrono::system_clock::now().time_since_epoch());
    OS << llvm::formatv("{0:f9} ", now.count());
  }

  // Add the process and thread if requested
  if (options.Test(LLDB_LOG_OPTION_PREPEND_PROC_AND_THREAD))
    OS << llvm::formatv("[{0,0+4}/{1,0+4}] ", getpid(),
                        llvm::get_threadid());

  // Add the thread name if requested
  if (options.Test(LLDB_LOG_OPTION_PREPEND_THREAD_NAME)) {
    llvm::SmallString<32> thread_name;
    llvm::get_thread_name(thread_name);

    llvm::SmallString<12> format_str;
    llvm::raw_svector_ostream format_os(format_str);
    format_os << "{0,-" << llvm::alignTo<16>(thread_name.size()) << "} ";
    OS << llvm::formatv(format_str.c_str(), thread_name);
  }

  if (options.Test(LLDB_LOG_OPTION_BACKTRACE))
    llvm::sys::PrintStackTrace(OS);

  if (options.Test(LLDB_LOG_OPTION_PREPEND_FILE_FUNCTION) &&
      (!file.empty() || !function.empty())) {
    file = llvm::sys::path::filename(file).take_front(40);
    function = function.take_front(40);
    OS << llvm::formatv("{0,-60:60} ", (file + ":" + function).str());
  }
}

// If we have a callback registered, then we call the logging callback. If we
// have a valid file handle, we also log to the file.
void Log::WriteMessage(llvm::StringRef message) {
  // Make a copy of our stream shared pointer in case someone disables our log
  // while we are logging and releases the stream
  auto handler_sp = GetHandler();
  if (!handler_sp)
    return;
  handler_sp->Emit(message);
}

void Log::Format(llvm::StringRef file, llvm::StringRef function,
                 const llvm::formatv_object_base &payload) {
  std::string message_string;
  llvm::raw_string_ostream message(message_string);
  WriteHeader(message, file, function);
  message << payload << "\n";
  WriteMessage(message.str());
}

StreamLogHandler::StreamLogHandler(int fd, bool should_close,
                                   size_t buffer_size)
    : m_stream(fd, should_close, buffer_size == 0) {
  if (buffer_size > 0)
    m_stream.SetBufferSize(buffer_size);
}

StreamLogHandler::~StreamLogHandler() { Flush(); }

void StreamLogHandler::Flush() {
  std::lock_guard<std::mutex> guard(m_mutex);
  m_stream.flush();
}

void StreamLogHandler::Emit(llvm::StringRef message) {
  if (m_stream.GetBufferSize() > 0) {
    std::lock_guard<std::mutex> guard(m_mutex);
    m_stream << message;
  } else {
    m_stream << message;
  }
}

CallbackLogHandler::CallbackLogHandler(lldb::LogOutputCallback callback,
                                       void *baton)
    : m_callback(callback), m_baton(baton) {}

void CallbackLogHandler::Emit(llvm::StringRef message) {
  m_callback(message.data(), m_baton);
}

RotatingLogHandler::RotatingLogHandler(size_t size)
    : m_messages(std::make_unique<std::string[]>(size)), m_size(size) {}

void RotatingLogHandler::Emit(llvm::StringRef message) {
  std::lock_guard<std::mutex> guard(m_mutex);
  ++m_total_count;
  const size_t index = m_next_index;
  m_next_index = NormalizeIndex(index + 1);
  m_messages[index] = message.str();
}

size_t RotatingLogHandler::NormalizeIndex(size_t i) const { return i % m_size; }

size_t RotatingLogHandler::GetNumMessages() const {
  return m_total_count < m_size ? m_total_count : m_size;
}

size_t RotatingLogHandler::GetFirstMessageIndex() const {
  return m_total_count < m_size ? 0 : m_next_index;
}

void RotatingLogHandler::Dump(llvm::raw_ostream &stream) const {
  std::lock_guard<std::mutex> guard(m_mutex);
  const size_t start_idx = GetFirstMessageIndex();
  const size_t stop_idx = start_idx + GetNumMessages();
  for (size_t i = start_idx; i < stop_idx; ++i) {
    const size_t idx = NormalizeIndex(i);
    stream << m_messages[idx];
  }
  stream.flush();
}

TeeLogHandler::TeeLogHandler(std::shared_ptr<LogHandler> first_log_handler,
                             std::shared_ptr<LogHandler> second_log_handler)
    : m_first_log_handler(first_log_handler),
      m_second_log_handler(second_log_handler) {
  assert(m_first_log_handler && "first log handler must be valid");
  assert(m_second_log_handler && "second log handler must be valid");
}

void TeeLogHandler::Emit(llvm::StringRef message) {
  m_first_log_handler->Emit(message);
  m_second_log_handler->Emit(message);
}
