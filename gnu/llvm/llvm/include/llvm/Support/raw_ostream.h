//===--- raw_ostream.h - Raw output stream ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the raw_ostream class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_RAW_OSTREAM_H
#define LLVM_SUPPORT_RAW_OSTREAM_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/DataTypes.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>

namespace llvm {

class Duration;
class formatv_object_base;
class format_object_base;
class FormattedString;
class FormattedNumber;
class FormattedBytes;
template <class T> class [[nodiscard]] Expected;

namespace sys {
namespace fs {
enum FileAccess : unsigned;
enum OpenFlags : unsigned;
enum CreationDisposition : unsigned;
class FileLocker;
} // end namespace fs
} // end namespace sys

/// This class implements an extremely fast bulk output stream that can *only*
/// output to a stream.  It does not support seeking, reopening, rewinding, line
/// buffered disciplines etc. It is a simple buffer that outputs
/// a chunk at a time.
class raw_ostream {
public:
  // Class kinds to support LLVM-style RTTI.
  enum class OStreamKind {
    OK_OStream,
    OK_FDStream,
    OK_SVecStream,
  };

private:
  OStreamKind Kind;

  /// The buffer is handled in such a way that the buffer is
  /// uninitialized, unbuffered, or out of space when OutBufCur >=
  /// OutBufEnd. Thus a single comparison suffices to determine if we
  /// need to take the slow path to write a single character.
  ///
  /// The buffer is in one of three states:
  ///  1. Unbuffered (BufferMode == Unbuffered)
  ///  1. Uninitialized (BufferMode != Unbuffered && OutBufStart == 0).
  ///  2. Buffered (BufferMode != Unbuffered && OutBufStart != 0 &&
  ///               OutBufEnd - OutBufStart >= 1).
  ///
  /// If buffered, then the raw_ostream owns the buffer if (BufferMode ==
  /// InternalBuffer); otherwise the buffer has been set via SetBuffer and is
  /// managed by the subclass.
  ///
  /// If a subclass installs an external buffer using SetBuffer then it can wait
  /// for a \see write_impl() call to handle the data which has been put into
  /// this buffer.
  char *OutBufStart, *OutBufEnd, *OutBufCur;
  bool ColorEnabled = false;

  enum class BufferKind {
    Unbuffered = 0,
    InternalBuffer,
    ExternalBuffer
  } BufferMode;

public:
  // color order matches ANSI escape sequence, don't change
  enum class Colors {
    BLACK = 0,
    RED,
    GREEN,
    YELLOW,
    BLUE,
    MAGENTA,
    CYAN,
    WHITE,
    BRIGHT_BLACK,
    BRIGHT_RED,
    BRIGHT_GREEN,
    BRIGHT_YELLOW,
    BRIGHT_BLUE,
    BRIGHT_MAGENTA,
    BRIGHT_CYAN,
    BRIGHT_WHITE,
    SAVEDCOLOR,
    RESET,
  };

  static constexpr Colors BLACK = Colors::BLACK;
  static constexpr Colors RED = Colors::RED;
  static constexpr Colors GREEN = Colors::GREEN;
  static constexpr Colors YELLOW = Colors::YELLOW;
  static constexpr Colors BLUE = Colors::BLUE;
  static constexpr Colors MAGENTA = Colors::MAGENTA;
  static constexpr Colors CYAN = Colors::CYAN;
  static constexpr Colors WHITE = Colors::WHITE;
  static constexpr Colors BRIGHT_BLACK = Colors::BRIGHT_BLACK;
  static constexpr Colors BRIGHT_RED = Colors::BRIGHT_RED;
  static constexpr Colors BRIGHT_GREEN = Colors::BRIGHT_GREEN;
  static constexpr Colors BRIGHT_YELLOW = Colors::BRIGHT_YELLOW;
  static constexpr Colors BRIGHT_BLUE = Colors::BRIGHT_BLUE;
  static constexpr Colors BRIGHT_MAGENTA = Colors::BRIGHT_MAGENTA;
  static constexpr Colors BRIGHT_CYAN = Colors::BRIGHT_CYAN;
  static constexpr Colors BRIGHT_WHITE = Colors::BRIGHT_WHITE;
  static constexpr Colors SAVEDCOLOR = Colors::SAVEDCOLOR;
  static constexpr Colors RESET = Colors::RESET;

  explicit raw_ostream(bool unbuffered = false,
                       OStreamKind K = OStreamKind::OK_OStream)
      : Kind(K), BufferMode(unbuffered ? BufferKind::Unbuffered
                                       : BufferKind::InternalBuffer) {
    // Start out ready to flush.
    OutBufStart = OutBufEnd = OutBufCur = nullptr;
  }

  raw_ostream(const raw_ostream &) = delete;
  void operator=(const raw_ostream &) = delete;

  virtual ~raw_ostream();

  /// tell - Return the current offset with the file.
  uint64_t tell() const { return current_pos() + GetNumBytesInBuffer(); }

  OStreamKind get_kind() const { return Kind; }

  //===--------------------------------------------------------------------===//
  // Configuration Interface
  //===--------------------------------------------------------------------===//

  /// If possible, pre-allocate \p ExtraSize bytes for stream data.
  /// i.e. it extends internal buffers to keep additional ExtraSize bytes.
  /// So that the stream could keep at least tell() + ExtraSize bytes
  /// without re-allocations. reserveExtraSpace() does not change
  /// the size/data of the stream.
  virtual void reserveExtraSpace(uint64_t ExtraSize) {}

  /// Set the stream to be buffered, with an automatically determined buffer
  /// size.
  void SetBuffered();

  /// Set the stream to be buffered, using the specified buffer size.
  void SetBufferSize(size_t Size) {
    flush();
    SetBufferAndMode(new char[Size], Size, BufferKind::InternalBuffer);
  }

  size_t GetBufferSize() const {
    // If we're supposed to be buffered but haven't actually gotten around
    // to allocating the buffer yet, return the value that would be used.
    if (BufferMode != BufferKind::Unbuffered && OutBufStart == nullptr)
      return preferred_buffer_size();

    // Otherwise just return the size of the allocated buffer.
    return OutBufEnd - OutBufStart;
  }

  /// Set the stream to be unbuffered. When unbuffered, the stream will flush
  /// after every write. This routine will also flush the buffer immediately
  /// when the stream is being set to unbuffered.
  void SetUnbuffered() {
    flush();
    SetBufferAndMode(nullptr, 0, BufferKind::Unbuffered);
  }

  size_t GetNumBytesInBuffer() const {
    return OutBufCur - OutBufStart;
  }

  //===--------------------------------------------------------------------===//
  // Data Output Interface
  //===--------------------------------------------------------------------===//

  void flush() {
    if (OutBufCur != OutBufStart)
      flush_nonempty();
  }

  raw_ostream &operator<<(char C) {
    if (OutBufCur >= OutBufEnd)
      return write(C);
    *OutBufCur++ = C;
    return *this;
  }

  raw_ostream &operator<<(unsigned char C) {
    if (OutBufCur >= OutBufEnd)
      return write(C);
    *OutBufCur++ = C;
    return *this;
  }

  raw_ostream &operator<<(signed char C) {
    if (OutBufCur >= OutBufEnd)
      return write(C);
    *OutBufCur++ = C;
    return *this;
  }

  raw_ostream &operator<<(StringRef Str) {
    // Inline fast path, particularly for strings with a known length.
    size_t Size = Str.size();

    // Make sure we can use the fast path.
    if (Size > (size_t)(OutBufEnd - OutBufCur))
      return write(Str.data(), Size);

    if (Size) {
      memcpy(OutBufCur, Str.data(), Size);
      OutBufCur += Size;
    }
    return *this;
  }

#if defined(__cpp_char8_t)
  // When using `char8_t *` integers or pointers are written to the ostream
  // instead of UTF-8 code as one might expect. This might lead to unexpected
  // behavior, especially as `u8""` literals are of type `char8_t*` instead of
  // type `char_t*` from C++20 onwards. Thus we disallow using them with
  // raw_ostreams.
  // If you have u8"" literals to stream, you can rewrite them as ordinary
  // literals with escape sequences
  // e.g.  replace `u8"\u00a0"` by `"\xc2\xa0"`
  // or use `reinterpret_cast`:
  // e.g. replace `u8"\u00a0"` by `reinterpret_cast<const char *>(u8"\u00a0")`
  raw_ostream &operator<<(const char8_t *Str) = delete;
#endif

  raw_ostream &operator<<(const char *Str) {
    // Inline fast path, particularly for constant strings where a sufficiently
    // smart compiler will simplify strlen.

    return this->operator<<(StringRef(Str));
  }

  raw_ostream &operator<<(const std::string &Str) {
    // Avoid the fast path, it would only increase code size for a marginal win.
    return write(Str.data(), Str.length());
  }

  raw_ostream &operator<<(const std::string_view &Str) {
    return write(Str.data(), Str.length());
  }

  raw_ostream &operator<<(const SmallVectorImpl<char> &Str) {
    return write(Str.data(), Str.size());
  }

  raw_ostream &operator<<(unsigned long N);
  raw_ostream &operator<<(long N);
  raw_ostream &operator<<(unsigned long long N);
  raw_ostream &operator<<(long long N);
  raw_ostream &operator<<(const void *P);

  raw_ostream &operator<<(unsigned int N) {
    return this->operator<<(static_cast<unsigned long>(N));
  }

  raw_ostream &operator<<(int N) {
    return this->operator<<(static_cast<long>(N));
  }

  raw_ostream &operator<<(double N);

  /// Output \p N in hexadecimal, without any prefix or padding.
  raw_ostream &write_hex(unsigned long long N);

  // Change the foreground color of text.
  raw_ostream &operator<<(Colors C);

  /// Output a formatted UUID with dash separators.
  using uuid_t = uint8_t[16];
  raw_ostream &write_uuid(const uuid_t UUID);

  /// Output \p Str, turning '\\', '\t', '\n', '"', and anything that doesn't
  /// satisfy llvm::isPrint into an escape sequence.
  raw_ostream &write_escaped(StringRef Str, bool UseHexEscapes = false);

  raw_ostream &write(unsigned char C);
  raw_ostream &write(const char *Ptr, size_t Size);

  // Formatted output, see the format() function in Support/Format.h.
  raw_ostream &operator<<(const format_object_base &Fmt);

  // Formatted output, see the leftJustify() function in Support/Format.h.
  raw_ostream &operator<<(const FormattedString &);

  // Formatted output, see the formatHex() function in Support/Format.h.
  raw_ostream &operator<<(const FormattedNumber &);

  // Formatted output, see the formatv() function in Support/FormatVariadic.h.
  raw_ostream &operator<<(const formatv_object_base &);

  // Formatted output, see the format_bytes() function in Support/Format.h.
  raw_ostream &operator<<(const FormattedBytes &);

  /// indent - Insert 'NumSpaces' spaces.
  raw_ostream &indent(unsigned NumSpaces);

  /// write_zeros - Insert 'NumZeros' nulls.
  raw_ostream &write_zeros(unsigned NumZeros);

  /// Changes the foreground color of text that will be output from this point
  /// forward.
  /// @param Color ANSI color to use, the special SAVEDCOLOR can be used to
  /// change only the bold attribute, and keep colors untouched
  /// @param Bold bold/brighter text, default false
  /// @param BG if true change the background, default: change foreground
  /// @returns itself so it can be used within << invocations
  virtual raw_ostream &changeColor(enum Colors Color, bool Bold = false,
                                   bool BG = false);

  /// Resets the colors to terminal defaults. Call this when you are done
  /// outputting colored text, or before program exit.
  virtual raw_ostream &resetColor();

  /// Reverses the foreground and background colors.
  virtual raw_ostream &reverseColor();

  /// This function determines if this stream is connected to a "tty" or
  /// "console" window. That is, the output would be displayed to the user
  /// rather than being put on a pipe or stored in a file.
  virtual bool is_displayed() const { return false; }

  /// This function determines if this stream is displayed and supports colors.
  /// The result is unaffected by calls to enable_color().
  virtual bool has_colors() const { return is_displayed(); }

  // Enable or disable colors. Once enable_colors(false) is called,
  // changeColor() has no effect until enable_colors(true) is called.
  virtual void enable_colors(bool enable) { ColorEnabled = enable; }

  bool colors_enabled() const { return ColorEnabled; }

  //===--------------------------------------------------------------------===//
  // Subclass Interface
  //===--------------------------------------------------------------------===//

private:
  /// The is the piece of the class that is implemented by subclasses.  This
  /// writes the \p Size bytes starting at
  /// \p Ptr to the underlying stream.
  ///
  /// This function is guaranteed to only be called at a point at which it is
  /// safe for the subclass to install a new buffer via SetBuffer.
  ///
  /// \param Ptr The start of the data to be written. For buffered streams this
  /// is guaranteed to be the start of the buffer.
  ///
  /// \param Size The number of bytes to be written.
  ///
  /// \invariant { Size > 0 }
  virtual void write_impl(const char *Ptr, size_t Size) = 0;

  /// Return the current position within the stream, not counting the bytes
  /// currently in the buffer.
  virtual uint64_t current_pos() const = 0;

protected:
  /// Use the provided buffer as the raw_ostream buffer. This is intended for
  /// use only by subclasses which can arrange for the output to go directly
  /// into the desired output buffer, instead of being copied on each flush.
  void SetBuffer(char *BufferStart, size_t Size) {
    SetBufferAndMode(BufferStart, Size, BufferKind::ExternalBuffer);
  }

  /// Return an efficient buffer size for the underlying output mechanism.
  virtual size_t preferred_buffer_size() const;

  /// Return the beginning of the current stream buffer, or 0 if the stream is
  /// unbuffered.
  const char *getBufferStart() const { return OutBufStart; }

  //===--------------------------------------------------------------------===//
  // Private Interface
  //===--------------------------------------------------------------------===//
private:
  /// Install the given buffer and mode.
  void SetBufferAndMode(char *BufferStart, size_t Size, BufferKind Mode);

  /// Flush the current buffer, which is known to be non-empty. This outputs the
  /// currently buffered data and resets the buffer to empty.
  void flush_nonempty();

  /// Copy data into the buffer. Size must not be greater than the number of
  /// unused bytes in the buffer.
  void copy_to_buffer(const char *Ptr, size_t Size);

  /// Compute whether colors should be used and do the necessary work such as
  /// flushing. The result is affected by calls to enable_color().
  bool prepare_colors();

  virtual void anchor();
};

/// Call the appropriate insertion operator, given an rvalue reference to a
/// raw_ostream object and return a stream of the same type as the argument.
template <typename OStream, typename T>
std::enable_if_t<!std::is_reference_v<OStream> &&
                     std::is_base_of_v<raw_ostream, OStream>,
                 OStream &&>
operator<<(OStream &&OS, const T &Value) {
  OS << Value;
  return std::move(OS);
}

/// An abstract base class for streams implementations that also support a
/// pwrite operation. This is useful for code that can mostly stream out data,
/// but needs to patch in a header that needs to know the output size.
class raw_pwrite_stream : public raw_ostream {
  virtual void pwrite_impl(const char *Ptr, size_t Size, uint64_t Offset) = 0;
  void anchor() override;

public:
  explicit raw_pwrite_stream(bool Unbuffered = false,
                             OStreamKind K = OStreamKind::OK_OStream)
      : raw_ostream(Unbuffered, K) {}
  void pwrite(const char *Ptr, size_t Size, uint64_t Offset) {
#ifndef NDEBUG
    uint64_t Pos = tell();
    // /dev/null always reports a pos of 0, so we cannot perform this check
    // in that case.
    if (Pos)
      assert(Size + Offset <= Pos && "We don't support extending the stream");
#endif
    pwrite_impl(Ptr, Size, Offset);
  }
};

//===----------------------------------------------------------------------===//
// File Output Streams
//===----------------------------------------------------------------------===//

/// A raw_ostream that writes to a file descriptor.
///
class raw_fd_ostream : public raw_pwrite_stream {
  int FD;
  bool ShouldClose;
  bool SupportsSeeking = false;
  bool IsRegularFile = false;
  mutable std::optional<bool> HasColors;

  /// Optional stream this stream is tied to. If this stream is written to, the
  /// tied-to stream will be flushed first.
  raw_ostream *TiedStream = nullptr;

#ifdef _WIN32
  /// True if this fd refers to a Windows console device. Mintty and other
  /// terminal emulators are TTYs, but they are not consoles.
  bool IsWindowsConsole = false;
#endif

  std::error_code EC;

  uint64_t pos = 0;

  /// See raw_ostream::write_impl.
  void write_impl(const char *Ptr, size_t Size) override;

  void pwrite_impl(const char *Ptr, size_t Size, uint64_t Offset) override;

  /// Return the current position within the stream, not counting the bytes
  /// currently in the buffer.
  uint64_t current_pos() const override { return pos; }

  /// Determine an efficient buffer size.
  size_t preferred_buffer_size() const override;

  void anchor() override;

protected:
  /// Set the flag indicating that an output error has been encountered.
  void error_detected(std::error_code EC) { this->EC = EC; }

  /// Return the file descriptor.
  int get_fd() const { return FD; }

  // Update the file position by increasing \p Delta.
  void inc_pos(uint64_t Delta) { pos += Delta; }

public:
  /// Open the specified file for writing. If an error occurs, information
  /// about the error is put into EC, and the stream should be immediately
  /// destroyed;
  /// \p Flags allows optional flags to control how the file will be opened.
  ///
  /// As a special case, if Filename is "-", then the stream will use
  /// STDOUT_FILENO instead of opening a file. This will not close the stdout
  /// descriptor.
  raw_fd_ostream(StringRef Filename, std::error_code &EC);
  raw_fd_ostream(StringRef Filename, std::error_code &EC,
                 sys::fs::CreationDisposition Disp);
  raw_fd_ostream(StringRef Filename, std::error_code &EC,
                 sys::fs::FileAccess Access);
  raw_fd_ostream(StringRef Filename, std::error_code &EC,
                 sys::fs::OpenFlags Flags);
  raw_fd_ostream(StringRef Filename, std::error_code &EC,
                 sys::fs::CreationDisposition Disp, sys::fs::FileAccess Access,
                 sys::fs::OpenFlags Flags);

  /// FD is the file descriptor that this writes to.  If ShouldClose is true,
  /// this closes the file when the stream is destroyed. If FD is for stdout or
  /// stderr, it will not be closed.
  raw_fd_ostream(int fd, bool shouldClose, bool unbuffered = false,
                 OStreamKind K = OStreamKind::OK_OStream);

  ~raw_fd_ostream() override;

  /// Manually flush the stream and close the file. Note that this does not call
  /// fsync.
  void close();

  bool supportsSeeking() const { return SupportsSeeking; }

  bool isRegularFile() const { return IsRegularFile; }

  /// Flushes the stream and repositions the underlying file descriptor position
  /// to the offset specified from the beginning of the file.
  uint64_t seek(uint64_t off);

  bool is_displayed() const override;

  bool has_colors() const override;

  /// Tie this stream to the specified stream. Replaces any existing tied-to
  /// stream. Specifying a nullptr unties the stream. This is intended for to
  /// tie errs() to outs(), so that outs() is flushed whenever something is
  /// written to errs(), preventing weird and hard-to-test output when stderr
  /// is redirected to stdout.
  void tie(raw_ostream *TieTo) { TiedStream = TieTo; }

  std::error_code error() const { return EC; }

  /// Return the value of the flag in this raw_fd_ostream indicating whether an
  /// output error has been encountered.
  /// This doesn't implicitly flush any pending output.  Also, it doesn't
  /// guarantee to detect all errors unless the stream has been closed.
  bool has_error() const { return bool(EC); }

  /// Set the flag read by has_error() to false. If the error flag is set at the
  /// time when this raw_ostream's destructor is called, report_fatal_error is
  /// called to report the error. Use clear_error() after handling the error to
  /// avoid this behavior.
  ///
  ///   "Errors should never pass silently.
  ///    Unless explicitly silenced."
  ///      - from The Zen of Python, by Tim Peters
  ///
  void clear_error() { EC = std::error_code(); }

  /// Locks the underlying file.
  ///
  /// @returns RAII object that releases the lock upon leaving the scope, if the
  ///          locking was successful. Otherwise returns corresponding
  ///          error code.
  ///
  /// The function blocks the current thread until the lock become available or
  /// error occurs.
  ///
  /// Possible use of this function may be as follows:
  ///
  ///   @code{.cpp}
  ///   if (auto L = stream.lock()) {
  ///     // ... do action that require file to be locked.
  ///   } else {
  ///     handleAllErrors(std::move(L.takeError()), [&](ErrorInfoBase &EIB) {
  ///       // ... handle lock error.
  ///     });
  ///   }
  ///   @endcode
  [[nodiscard]] Expected<sys::fs::FileLocker> lock();

  /// Tries to lock the underlying file within the specified period.
  ///
  /// @returns RAII object that releases the lock upon leaving the scope, if the
  ///          locking was successful. Otherwise returns corresponding
  ///          error code.
  ///
  /// It is used as @ref lock.
  [[nodiscard]] Expected<sys::fs::FileLocker>
  tryLockFor(Duration const &Timeout);
};

/// This returns a reference to a raw_fd_ostream for standard output. Use it
/// like: outs() << "foo" << "bar";
raw_fd_ostream &outs();

/// This returns a reference to a raw_ostream for standard error.
/// Use it like: errs() << "foo" << "bar";
/// By default, the stream is tied to stdout to ensure stdout is flushed before
/// stderr is written, to ensure the error messages are written in their
/// expected place.
raw_fd_ostream &errs();

/// This returns a reference to a raw_ostream which simply discards output.
raw_ostream &nulls();

//===----------------------------------------------------------------------===//
// File Streams
//===----------------------------------------------------------------------===//

/// A raw_ostream of a file for reading/writing/seeking.
///
class raw_fd_stream : public raw_fd_ostream {
public:
  /// Open the specified file for reading/writing/seeking. If an error occurs,
  /// information about the error is put into EC, and the stream should be
  /// immediately destroyed.
  raw_fd_stream(StringRef Filename, std::error_code &EC);

  raw_fd_stream(int fd, bool shouldClose);

  /// This reads the \p Size bytes into a buffer pointed by \p Ptr.
  ///
  /// \param Ptr The start of the buffer to hold data to be read.
  ///
  /// \param Size The number of bytes to be read.
  ///
  /// On success, the number of bytes read is returned, and the file position is
  /// advanced by this number. On error, -1 is returned, use error() to get the
  /// error code.
  ssize_t read(char *Ptr, size_t Size);

  /// Check if \p OS is a pointer of type raw_fd_stream*.
  static bool classof(const raw_ostream *OS);
};

//===----------------------------------------------------------------------===//
// Output Stream Adaptors
//===----------------------------------------------------------------------===//

/// A raw_ostream that writes to an std::string.  This is a simple adaptor
/// class. This class does not encounter output errors.
/// raw_string_ostream operates without a buffer, delegating all memory
/// management to the std::string. Thus the std::string is always up-to-date,
/// may be used directly and there is no need to call flush().
class raw_string_ostream : public raw_ostream {
  std::string &OS;

  /// See raw_ostream::write_impl.
  void write_impl(const char *Ptr, size_t Size) override;

  /// Return the current position within the stream, not counting the bytes
  /// currently in the buffer.
  uint64_t current_pos() const override { return OS.size(); }

public:
  explicit raw_string_ostream(std::string &O) : OS(O) {
    SetUnbuffered();
  }

  /// Returns the string's reference. In most cases it is better to simply use
  /// the underlying std::string directly.
  /// TODO: Consider removing this API.
  std::string &str() { return OS; }

  void reserveExtraSpace(uint64_t ExtraSize) override {
    OS.reserve(tell() + ExtraSize);
  }
};

/// A raw_ostream that writes to an SmallVector or SmallString.  This is a
/// simple adaptor class. This class does not encounter output errors.
/// raw_svector_ostream operates without a buffer, delegating all memory
/// management to the SmallString. Thus the SmallString is always up-to-date,
/// may be used directly and there is no need to call flush().
class raw_svector_ostream : public raw_pwrite_stream {
  SmallVectorImpl<char> &OS;

  /// See raw_ostream::write_impl.
  void write_impl(const char *Ptr, size_t Size) override;

  void pwrite_impl(const char *Ptr, size_t Size, uint64_t Offset) override;

  /// Return the current position within the stream.
  uint64_t current_pos() const override;

public:
  /// Construct a new raw_svector_ostream.
  ///
  /// \param O The vector to write to; this should generally have at least 128
  /// bytes free to avoid any extraneous memory overhead.
  explicit raw_svector_ostream(SmallVectorImpl<char> &O)
      : raw_pwrite_stream(false, raw_ostream::OStreamKind::OK_SVecStream),
        OS(O) {
    // FIXME: here and in a few other places, set directly to unbuffered in the
    // ctor.
    SetUnbuffered();
  }

  ~raw_svector_ostream() override = default;

  void flush() = delete;

  /// Return a StringRef for the vector contents.
  StringRef str() const { return StringRef(OS.data(), OS.size()); }
  SmallVectorImpl<char> &buffer() { return OS; }

  void reserveExtraSpace(uint64_t ExtraSize) override {
    OS.reserve(tell() + ExtraSize);
  }

  static bool classof(const raw_ostream *OS);
};

/// A raw_ostream that discards all output.
class raw_null_ostream : public raw_pwrite_stream {
  /// See raw_ostream::write_impl.
  void write_impl(const char *Ptr, size_t size) override;
  void pwrite_impl(const char *Ptr, size_t Size, uint64_t Offset) override;

  /// Return the current position within the stream, not counting the bytes
  /// currently in the buffer.
  uint64_t current_pos() const override;

public:
  explicit raw_null_ostream() = default;
  ~raw_null_ostream() override;
};

class buffer_ostream : public raw_svector_ostream {
  raw_ostream &OS;
  SmallVector<char, 0> Buffer;

  void anchor() override;

public:
  buffer_ostream(raw_ostream &OS) : raw_svector_ostream(Buffer), OS(OS) {}
  ~buffer_ostream() override { OS << str(); }
};

class buffer_unique_ostream : public raw_svector_ostream {
  std::unique_ptr<raw_ostream> OS;
  SmallVector<char, 0> Buffer;

  void anchor() override;

public:
  buffer_unique_ostream(std::unique_ptr<raw_ostream> OS)
      : raw_svector_ostream(Buffer), OS(std::move(OS)) {
    // Turn off buffering on OS, which we now own, to avoid allocating a buffer
    // when the destructor writes only to be immediately flushed again.
    this->OS->SetUnbuffered();
  }
  ~buffer_unique_ostream() override { *OS << str(); }
};

class Error;

/// This helper creates an output stream and then passes it to \p Write.
/// The stream created is based on the specified \p OutputFileName:
/// llvm::outs for "-", raw_null_ostream for "/dev/null", and raw_fd_ostream
/// for other names. For raw_fd_ostream instances, the stream writes to
/// a temporary file. The final output file is atomically replaced with the
/// temporary file after the \p Write function is finished.
Error writeToOutput(StringRef OutputFileName,
                    std::function<Error(raw_ostream &)> Write);

raw_ostream &operator<<(raw_ostream &OS, std::nullopt_t);

template <typename T, typename = decltype(std::declval<raw_ostream &>()
                                          << std::declval<const T &>())>
raw_ostream &operator<<(raw_ostream &OS, const std::optional<T> &O) {
  if (O)
    OS << *O;
  else
    OS << std::nullopt;
  return OS;
}

} // end namespace llvm

#endif // LLVM_SUPPORT_RAW_OSTREAM_H
