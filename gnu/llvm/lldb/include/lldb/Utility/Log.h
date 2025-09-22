//===-- Log.h ---------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_LOG_H
#define LLDB_UTILITY_LOG_H

#include "lldb/Utility/Flags.h"
#include "lldb/lldb-defines.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/RWMutex.h"

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>

namespace llvm {
class raw_ostream;
}
// Logging Options
#define LLDB_LOG_OPTION_VERBOSE (1u << 1)
#define LLDB_LOG_OPTION_PREPEND_SEQUENCE (1u << 3)
#define LLDB_LOG_OPTION_PREPEND_TIMESTAMP (1u << 4)
#define LLDB_LOG_OPTION_PREPEND_PROC_AND_THREAD (1u << 5)
#define LLDB_LOG_OPTION_PREPEND_THREAD_NAME (1U << 6)
#define LLDB_LOG_OPTION_BACKTRACE (1U << 7)
#define LLDB_LOG_OPTION_APPEND (1U << 8)
#define LLDB_LOG_OPTION_PREPEND_FILE_FUNCTION (1U << 9)

// Logging Functions
namespace lldb_private {

class LogHandler {
public:
  virtual ~LogHandler() = default;
  virtual void Emit(llvm::StringRef message) = 0;

  virtual bool isA(const void *ClassID) const { return ClassID == &ID; }
  static bool classof(const LogHandler *obj) { return obj->isA(&ID); }

private:
  static char ID;
};

class StreamLogHandler : public LogHandler {
public:
  StreamLogHandler(int fd, bool should_close, size_t buffer_size = 0);
  ~StreamLogHandler() override;

  void Emit(llvm::StringRef message) override;
  void Flush();

  bool isA(const void *ClassID) const override { return ClassID == &ID; }
  static bool classof(const LogHandler *obj) { return obj->isA(&ID); }

private:
  std::mutex m_mutex;
  llvm::raw_fd_ostream m_stream;
  static char ID;
};

class CallbackLogHandler : public LogHandler {
public:
  CallbackLogHandler(lldb::LogOutputCallback callback, void *baton);

  void Emit(llvm::StringRef message) override;

  bool isA(const void *ClassID) const override { return ClassID == &ID; }
  static bool classof(const LogHandler *obj) { return obj->isA(&ID); }

private:
  lldb::LogOutputCallback m_callback;
  void *m_baton;
  static char ID;
};

class RotatingLogHandler : public LogHandler {
public:
  RotatingLogHandler(size_t size);

  void Emit(llvm::StringRef message) override;
  void Dump(llvm::raw_ostream &stream) const;

  bool isA(const void *ClassID) const override { return ClassID == &ID; }
  static bool classof(const LogHandler *obj) { return obj->isA(&ID); }

private:
  size_t NormalizeIndex(size_t i) const;
  size_t GetNumMessages() const;
  size_t GetFirstMessageIndex() const;

  mutable std::mutex m_mutex;
  std::unique_ptr<std::string[]> m_messages;
  const size_t m_size = 0;
  size_t m_next_index = 0;
  size_t m_total_count = 0;
  static char ID;
};

/// A T-style log handler that multiplexes messages to two log handlers.
class TeeLogHandler : public LogHandler {
public:
  TeeLogHandler(std::shared_ptr<LogHandler> first_log_handler,
                std::shared_ptr<LogHandler> second_log_handler);

  void Emit(llvm::StringRef message) override;

  bool isA(const void *ClassID) const override { return ClassID == &ID; }
  static bool classof(const LogHandler *obj) { return obj->isA(&ID); }

private:
  std::shared_ptr<LogHandler> m_first_log_handler;
  std::shared_ptr<LogHandler> m_second_log_handler;
  static char ID;
};

class Log final {
public:
  /// The underlying type of all log channel enums. Declare them as:
  /// enum class MyLog : MaskType {
  ///   Channel0 = Log::ChannelFlag<0>,
  ///   Channel1 = Log::ChannelFlag<1>,
  ///   ...,
  ///   LLVM_MARK_AS_BITMASK_ENUM(LastChannel),
  /// };
  using MaskType = uint64_t;

  template <MaskType Bit>
  static constexpr MaskType ChannelFlag = MaskType(1) << Bit;

  // Description of a log channel category.
  struct Category {
    llvm::StringLiteral name;
    llvm::StringLiteral description;
    MaskType flag;

    template <typename Cat>
    constexpr Category(llvm::StringLiteral name,
                       llvm::StringLiteral description, Cat mask)
        : name(name), description(description), flag(MaskType(mask)) {
      static_assert(
          std::is_same<Log::MaskType, std::underlying_type_t<Cat>>::value);
    }
  };

  // This class describes a log channel. It also encapsulates the behavior
  // necessary to enable a log channel in an atomic manner.
  class Channel {
    std::atomic<Log *> log_ptr;
    friend class Log;

  public:
    const llvm::ArrayRef<Category> categories;
    const MaskType default_flags;

    template <typename Cat>
    constexpr Channel(llvm::ArrayRef<Log::Category> categories,
                      Cat default_flags)
        : log_ptr(nullptr), categories(categories),
          default_flags(MaskType(default_flags)) {
      static_assert(
          std::is_same<Log::MaskType, std::underlying_type_t<Cat>>::value);
    }

    // This function is safe to call at any time. If the channel is disabled
    // after (or concurrently with) this function returning a non-null Log
    // pointer, it is still safe to attempt to write to the Log object -- the
    // output will be discarded.
    Log *GetLog(MaskType mask) {
      Log *log = log_ptr.load(std::memory_order_relaxed);
      if (log && ((log->GetMask() & mask) != 0))
        return log;
      return nullptr;
    }
  };


  // Static accessors for logging channels
  static void Register(llvm::StringRef name, Channel &channel);
  static void Unregister(llvm::StringRef name);

  static bool
  EnableLogChannel(const std::shared_ptr<LogHandler> &log_handler_sp,
                   uint32_t log_options, llvm::StringRef channel,
                   llvm::ArrayRef<const char *> categories,
                   llvm::raw_ostream &error_stream);

  static bool DisableLogChannel(llvm::StringRef channel,
                                llvm::ArrayRef<const char *> categories,
                                llvm::raw_ostream &error_stream);

  static bool DumpLogChannel(llvm::StringRef channel,
                             llvm::raw_ostream &output_stream,
                             llvm::raw_ostream &error_stream);

  static bool ListChannelCategories(llvm::StringRef channel,
                                    llvm::raw_ostream &stream);

  /// Returns the list of log channels.
  static std::vector<llvm::StringRef> ListChannels();
  /// Calls the given lambda for every category in the given channel.
  /// If no channel with the given name exists, lambda is never called.
  static void ForEachChannelCategory(
      llvm::StringRef channel,
      llvm::function_ref<void(llvm::StringRef, llvm::StringRef)> lambda);

  static void DisableAllLogChannels();

  static void ListAllLogChannels(llvm::raw_ostream &stream);

  // Member functions
  //
  // These functions are safe to call at any time you have a Log* obtained from
  // the Channel class. If logging is disabled between you obtaining the Log
  // object and writing to it, the output will be silently discarded.
  Log(Channel &channel) : m_channel(channel) {}
  ~Log() = default;

  void PutCString(const char *cstr);
  void PutString(llvm::StringRef str);

  template <typename... Args>
  void Format(llvm::StringRef file, llvm::StringRef function,
              const char *format, Args &&... args) {
    Format(file, function, llvm::formatv(format, std::forward<Args>(args)...));
  }

  template <typename... Args>
  void FormatError(llvm::Error error, llvm::StringRef file,
                   llvm::StringRef function, const char *format,
                   Args &&... args) {
    Format(file, function,
           llvm::formatv(format, llvm::toString(std::move(error)),
                         std::forward<Args>(args)...));
  }

  void Formatf(llvm::StringRef file, llvm::StringRef function,
               const char *format, ...) __attribute__((format(printf, 4, 5)));

  /// Prefer using LLDB_LOGF whenever possible.
  void Printf(const char *format, ...) __attribute__((format(printf, 2, 3)));

  void Error(const char *fmt, ...) __attribute__((format(printf, 2, 3)));

  void Verbose(const char *fmt, ...) __attribute__((format(printf, 2, 3)));

  void Warning(const char *fmt, ...) __attribute__((format(printf, 2, 3)));

  const Flags GetOptions() const;

  MaskType GetMask() const;

  bool GetVerbose() const;

  void VAPrintf(const char *format, va_list args);
  void VAError(const char *format, va_list args);
  void VAFormatf(llvm::StringRef file, llvm::StringRef function,
                 const char *format, va_list args);

private:
  Channel &m_channel;

  // The mutex makes sure enable/disable operations are thread-safe. The
  // options and mask variables are atomic to enable their reading in
  // Channel::GetLogIfAny without taking the mutex to speed up the fast path.
  // Their modification however, is still protected by this mutex.
  llvm::sys::RWMutex m_mutex;

  std::shared_ptr<LogHandler> m_handler;
  std::atomic<uint32_t> m_options{0};
  std::atomic<MaskType> m_mask{0};

  void WriteHeader(llvm::raw_ostream &OS, llvm::StringRef file,
                   llvm::StringRef function);
  void WriteMessage(llvm::StringRef message);

  void Format(llvm::StringRef file, llvm::StringRef function,
              const llvm::formatv_object_base &payload);

  std::shared_ptr<LogHandler> GetHandler() {
    llvm::sys::ScopedReader lock(m_mutex);
    return m_handler;
  }

  void Enable(const std::shared_ptr<LogHandler> &handler_sp, uint32_t options,
              MaskType flags);

  void Disable(MaskType flags);

  bool Dump(llvm::raw_ostream &stream);

  typedef llvm::StringMap<Log> ChannelMap;
  static llvm::ManagedStatic<ChannelMap> g_channel_map;

  static void ForEachCategory(
      const Log::ChannelMap::value_type &entry,
      llvm::function_ref<void(llvm::StringRef, llvm::StringRef)> lambda);

  static void ListCategories(llvm::raw_ostream &stream,
                             const ChannelMap::value_type &entry);
  static Log::MaskType GetFlags(llvm::raw_ostream &stream,
                                const ChannelMap::value_type &entry,
                                llvm::ArrayRef<const char *> categories);

  Log(const Log &) = delete;
  void operator=(const Log &) = delete;
};

// Must be specialized for a particular log type.
template <typename Cat> Log::Channel &LogChannelFor() = delete;

/// Retrieve the Log object for the channel associated with the given log enum.
///
/// Returns a valid Log object if any of the provided categories are enabled.
/// Otherwise, returns nullptr.
template <typename Cat> Log *GetLog(Cat mask) {
  static_assert(
      std::is_same<Log::MaskType, std::underlying_type_t<Cat>>::value);
  return LogChannelFor<Cat>().GetLog(Log::MaskType(mask));
}

} // namespace lldb_private

/// The LLDB_LOG* macros defined below are the way to emit log messages.
///
/// Note that the macros surround the arguments in a check for the log
/// being on, so you can freely call methods in arguments without affecting
/// the non-log execution flow.
///
/// If you need to do more complex computations to prepare the log message
/// be sure to add your own if (log) check, since we don't want logging to
/// have any effect when not on.
///
/// However, the LLDB_LOG macro uses the llvm::formatv system (see the
/// ProgrammersManual page in the llvm docs for more details).  This allows
/// the use of "format_providers" to auto-format datatypes, and there are
/// already formatters for some of the llvm and lldb datatypes.
///
/// So if you need to do non-trivial formatting of one of these types, be
/// sure to grep the lldb and llvm sources for "format_provider" to see if
/// there is already a formatter before doing in situ formatting, and if
/// possible add a provider if one does not already exist.

#define LLDB_LOG(log, ...)                                                     \
  do {                                                                         \
    ::lldb_private::Log *log_private = (log);                                  \
    if (log_private)                                                           \
      log_private->Format(__FILE__, __func__, __VA_ARGS__);                    \
  } while (0)

#define LLDB_LOGF(log, ...)                                                    \
  do {                                                                         \
    ::lldb_private::Log *log_private = (log);                                  \
    if (log_private)                                                           \
      log_private->Formatf(__FILE__, __func__, __VA_ARGS__);                   \
  } while (0)

#define LLDB_LOGV(log, ...)                                                    \
  do {                                                                         \
    ::lldb_private::Log *log_private = (log);                                  \
    if (log_private && log_private->GetVerbose())                              \
      log_private->Format(__FILE__, __func__, __VA_ARGS__);                    \
  } while (0)

// Write message to log, if error is set. In the log message refer to the error
// with {0}. Error is cleared regardless of whether logging is enabled.
#define LLDB_LOG_ERROR(log, error, ...)                                        \
  do {                                                                         \
    ::lldb_private::Log *log_private = (log);                                  \
    ::llvm::Error error_private = (error);                                     \
    if (log_private && error_private) {                                        \
      log_private->FormatError(::std::move(error_private), __FILE__, __func__, \
                               __VA_ARGS__);                                   \
    } else                                                                     \
      ::llvm::consumeError(::std::move(error_private));                        \
  } while (0)

// Write message to the verbose log, if error is set. In the log
// message refer to the error with {0}. Error is cleared regardless of
// whether logging is enabled.
#define LLDB_LOG_ERRORV(log, error, ...)                                       \
  do {                                                                         \
    ::lldb_private::Log *log_private = (log);                                  \
    ::llvm::Error error_private = (error);                                     \
    if (log_private && log_private->GetVerbose() && error_private) {           \
      log_private->FormatError(::std::move(error_private), __FILE__, __func__, \
                               __VA_ARGS__);                                   \
    } else                                                                     \
      ::llvm::consumeError(::std::move(error_private));                        \
  } while (0)

#endif // LLDB_UTILITY_LOG_H
