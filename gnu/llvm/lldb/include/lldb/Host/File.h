//===-- File.h --------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_FILE_H
#define LLDB_HOST_FILE_H

#include "lldb/Host/PosixApi.h"
#include "lldb/Host/Terminal.h"
#include "lldb/Utility/IOObject.h"
#include "lldb/Utility/Status.h"
#include "lldb/lldb-private.h"
#include "llvm/ADT/BitmaskEnum.h"

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <optional>
#include <sys/types.h>

namespace lldb_private {

LLVM_ENABLE_BITMASK_ENUMS_IN_NAMESPACE();

/// \class File File.h "lldb/Host/File.h"
/// An abstract base class for files.
///
/// Files will often be NativeFiles, which provides a wrapper
/// around host OS file functionality.   But it
/// is also possible to subclass file to provide objects that have file
/// or stream functionality but are not backed by any host OS file.
class File : public IOObject {
public:
  static int kInvalidDescriptor;
  static FILE *kInvalidStream;

  // NB this enum is used in the lldb platform gdb-remote packet
  // vFile:open: and existing values cannot be modified.
  //
  // The first set of values is defined by gdb headers and can be found
  // in the documentation at:
  // * https://sourceware.org/gdb/onlinedocs/gdb/Open-Flags.html#Open-Flags
  //
  // The second half are LLDB extensions and use the highest uint32_t bits
  // to avoid risk of collisions with future gdb remote protocol changes.
  enum OpenOptions : uint32_t {
    eOpenOptionReadOnly = 0x0,  // Open file for reading (only)
    eOpenOptionWriteOnly = 0x1, // Open file for writing (only)
    eOpenOptionReadWrite = 0x2, // Open file for both reading and writing
    eOpenOptionAppend =
        0x8, // Don't truncate file when opening, append to end of file
    eOpenOptionCanCreate = 0x200, // Create file if doesn't already exist
    eOpenOptionTruncate = 0x400,  // Truncate file when opening
    eOpenOptionCanCreateNewOnly =
        0x800, // Can create file only if it doesn't already exist

    eOpenOptionNonBlocking = (1u << 28), // File reads
    eOpenOptionDontFollowSymlinks = (1u << 29),
    eOpenOptionCloseOnExec =
        (1u << 30), // Close the file when executing a new process
    eOpenOptionInvalid = (1u << 31), // Used as invalid value
    LLVM_MARK_AS_BITMASK_ENUM(/* largest_value= */ eOpenOptionInvalid)
  };

  static mode_t ConvertOpenOptionsForPOSIXOpen(OpenOptions open_options);
  static llvm::Expected<OpenOptions> GetOptionsFromMode(llvm::StringRef mode);
  static bool DescriptorIsValid(int descriptor) { return descriptor >= 0; };
  static llvm::Expected<const char *>
  GetStreamOpenModeFromOptions(OpenOptions options);

  File() : IOObject(eFDTypeFile){};

  /// Read bytes from a file from the current file position into buf.
  ///
  /// NOTE: This function is NOT thread safe. Use the read function
  /// that takes an "off_t &offset" to ensure correct operation in multi-
  /// threaded environments.
  ///
  /// \param[in,out] num_bytes
  ///    Pass in the size of buf.  Read will pass out the number
  ///    of bytes read.   Zero bytes read with no error indicates
  ///    EOF.
  ///
  /// \return
  ///    success, ENOTSUP, or another error.
  Status Read(void *buf, size_t &num_bytes) override;

  /// Write bytes from buf to a file at the current file position.
  ///
  /// NOTE: This function is NOT thread safe. Use the write function
  /// that takes an "off_t &offset" to ensure correct operation in multi-
  /// threaded environments.
  ///
  /// \param[in,out] num_bytes
  ///    Pass in the size of buf.  Write will pass out the number
  ///    of bytes written.   Write will attempt write the full number
  ///    of bytes and will not return early except on error.
  ///
  /// \return
  ///    success, ENOTSUP, or another error.
  Status Write(const void *buf, size_t &num_bytes) override;

  /// IsValid
  ///
  /// \return
  ///    true iff the file is valid.
  bool IsValid() const override;

  /// Flush any buffers and release any resources owned by the file.
  /// After Close() the file will be invalid.
  ///
  /// \return
  ///     success or an error.
  Status Close() override;

  /// Get a handle that can be used for OS polling interfaces, such
  /// as WaitForMultipleObjects, select, or epoll.   This may return
  /// IOObject::kInvalidHandleValue if none is available.   This will
  /// generally be the same as the file descriptor, this function
  /// is not interchangeable with GetDescriptor().   A WaitableHandle
  /// must only be used for polling, not actual I/O.
  ///
  /// \return
  ///     a valid handle or IOObject::kInvalidHandleValue
  WaitableHandle GetWaitableHandle() override;

  /// Get the file specification for this file, if possible.
  ///
  /// \param[out] file_spec
  ///     the file specification.
  /// \return
  ///     ENOTSUP, success, or another error.
  virtual Status GetFileSpec(FileSpec &file_spec) const;

  /// Get underlying OS file descriptor for this file, or kInvalidDescriptor.
  /// If the descriptor is valid, then it may be used directly for I/O
  /// However, the File may also perform it's own buffering, so avoid using
  /// this if it is not necessary, or use Flush() appropriately.
  ///
  /// \return
  ///    a valid file descriptor for this file or kInvalidDescriptor
  virtual int GetDescriptor() const;

  /// Get the underlying libc stream for this file, or NULL.
  ///
  /// Not all valid files will have a FILE* stream.   This should only be
  /// used if absolutely necessary, such as to interact with 3rd party
  /// libraries that need FILE* streams.
  ///
  /// \return
  ///    a valid stream or NULL;
  virtual FILE *GetStream();

  /// Seek to an offset relative to the beginning of the file.
  ///
  /// NOTE: This function is NOT thread safe, other threads that
  /// access this object might also change the current file position. For
  /// thread safe reads and writes see the following functions: @see
  /// File::Read (void *, size_t, off_t &) \see File::Write (const void *,
  /// size_t, off_t &)
  ///
  /// \param[in] offset
  ///     The offset to seek to within the file relative to the
  ///     beginning of the file.
  ///
  /// \param[in] error_ptr
  ///     A pointer to a lldb_private::Status object that will be
  ///     filled in if non-nullptr.
  ///
  /// \return
  ///     The resulting seek offset, or -1 on error.
  virtual off_t SeekFromStart(off_t offset, Status *error_ptr = nullptr);

  /// Seek to an offset relative to the current file position.
  ///
  /// NOTE: This function is NOT thread safe, other threads that
  /// access this object might also change the current file position. For
  /// thread safe reads and writes see the following functions: @see
  /// File::Read (void *, size_t, off_t &) \see File::Write (const void *,
  /// size_t, off_t &)
  ///
  /// \param[in] offset
  ///     The offset to seek to within the file relative to the
  ///     current file position.
  ///
  /// \param[in] error_ptr
  ///     A pointer to a lldb_private::Status object that will be
  ///     filled in if non-nullptr.
  ///
  /// \return
  ///     The resulting seek offset, or -1 on error.
  virtual off_t SeekFromCurrent(off_t offset, Status *error_ptr = nullptr);

  /// Seek to an offset relative to the end of the file.
  ///
  /// NOTE: This function is NOT thread safe, other threads that
  /// access this object might also change the current file position. For
  /// thread safe reads and writes see the following functions: @see
  /// File::Read (void *, size_t, off_t &) \see File::Write (const void *,
  /// size_t, off_t &)
  ///
  /// \param[in,out] offset
  ///     The offset to seek to within the file relative to the
  ///     end of the file which gets filled in with the resulting
  ///     absolute file offset.
  ///
  /// \param[in] error_ptr
  ///     A pointer to a lldb_private::Status object that will be
  ///     filled in if non-nullptr.
  ///
  /// \return
  ///     The resulting seek offset, or -1 on error.
  virtual off_t SeekFromEnd(off_t offset, Status *error_ptr = nullptr);

  /// Read bytes from a file from the specified file offset.
  ///
  /// NOTE: This function is thread safe in that clients manager their
  /// own file position markers and reads on other threads won't mess up the
  /// current read.
  ///
  /// \param[in] dst
  ///     A buffer where to put the bytes that are read.
  ///
  /// \param[in,out] num_bytes
  ///     The number of bytes to read from the current file position
  ///     which gets modified with the number of bytes that were read.
  ///
  /// \param[in,out] offset
  ///     The offset within the file from which to read \a num_bytes
  ///     bytes. This offset gets incremented by the number of bytes
  ///     that were read.
  ///
  /// \return
  ///     An error object that indicates success or the reason for
  ///     failure.
  virtual Status Read(void *dst, size_t &num_bytes, off_t &offset);

  /// Write bytes to a file at the specified file offset.
  ///
  /// NOTE: This function is thread safe in that clients manager their
  /// own file position markers, though clients will need to implement their
  /// own locking externally to avoid multiple people writing to the file at
  /// the same time.
  ///
  /// \param[in] src
  ///     A buffer containing the bytes to write.
  ///
  /// \param[in,out] num_bytes
  ///     The number of bytes to write to the file at offset \a offset.
  ///     \a num_bytes gets modified with the number of bytes that
  ///     were read.
  ///
  /// \param[in,out] offset
  ///     The offset within the file at which to write \a num_bytes
  ///     bytes. This offset gets incremented by the number of bytes
  ///     that were written.
  ///
  /// \return
  ///     An error object that indicates success or the reason for
  ///     failure.
  virtual Status Write(const void *src, size_t &num_bytes, off_t &offset);

  /// Flush the current stream
  ///
  /// \return
  ///     An error object that indicates success or the reason for
  ///     failure.
  virtual Status Flush();

  /// Sync to disk.
  ///
  /// \return
  ///     An error object that indicates success or the reason for
  ///     failure.
  virtual Status Sync();

  /// Output printf formatted output to the stream.
  ///
  /// NOTE: this is not virtual, because it just calls the va_list
  /// version of the function.
  ///
  /// Print some formatted output to the stream.
  ///
  /// \param[in] format
  ///     A printf style format string.
  ///
  /// \param[in] ...
  ///     Variable arguments that are needed for the printf style
  ///     format string \a format.
  size_t Printf(const char *format, ...) __attribute__((format(printf, 2, 3)));

  /// Output printf formatted output to the stream.
  ///
  /// Print some formatted output to the stream.
  ///
  /// \param[in] format
  ///     A printf style format string.
  ///
  /// \param[in] args
  ///     Variable arguments that are needed for the printf style
  ///     format string \a format.
  virtual size_t PrintfVarArg(const char *format, va_list args);

  /// Return the OpenOptions for this file.
  ///
  /// Some options like eOpenOptionDontFollowSymlinks only make
  /// sense when a file is being opened (or not at all)
  /// and may not be preserved for this method.  But any valid
  /// File should return either eOpenOptionReadOnly, eOpenOptionWriteOnly
  /// or eOpenOptionReadWrite here.
  ///
  /// \return
  ///    OpenOptions flags for this file, or an error.
  virtual llvm::Expected<OpenOptions> GetOptions() const;

  llvm::Expected<const char *> GetOpenMode() const {
    auto opts = GetOptions();
    if (!opts)
      return opts.takeError();
    return GetStreamOpenModeFromOptions(opts.get());
  }

  /// Get the permissions for a this file.
  ///
  /// \return
  ///     Bits logical OR'ed together from the permission bits defined
  ///     in lldb_private::File::Permissions.
  uint32_t GetPermissions(Status &error) const;

  /// Return true if this file is interactive.
  ///
  /// \return
  ///     True if this file is a terminal (tty or pty), false
  ///     otherwise.
  bool GetIsInteractive();

  /// Return true if this file from a real terminal.
  ///
  /// Just knowing a file is a interactive isn't enough, we also need to know
  /// if the terminal has a width and height so we can do cursor movement and
  /// other terminal manipulations by sending escape sequences.
  ///
  /// \return
  ///     True if this file is a terminal (tty, not a pty) that has
  ///     a non-zero width and height, false otherwise.
  bool GetIsRealTerminal();

  /// Return true if this file is a terminal which supports colors.
  ///
  /// \return
  ///    True iff this is a terminal and it supports colors.
  bool GetIsTerminalWithColors();

  operator bool() const { return IsValid(); };

  bool operator!() const { return !IsValid(); };

  static char ID;
  virtual bool isA(const void *classID) const { return classID == &ID; }
  static bool classof(const File *file) { return file->isA(&ID); }

protected:
  LazyBool m_is_interactive = eLazyBoolCalculate;
  LazyBool m_is_real_terminal = eLazyBoolCalculate;
  LazyBool m_supports_colors = eLazyBoolCalculate;

  void CalculateInteractiveAndTerminal();

private:
  File(const File &) = delete;
  const File &operator=(const File &) = delete;
};

class NativeFile : public File {
public:
  NativeFile() : m_descriptor(kInvalidDescriptor), m_stream(kInvalidStream) {}

  NativeFile(FILE *fh, bool transfer_ownership)
      : m_descriptor(kInvalidDescriptor), m_own_descriptor(false), m_stream(fh),
        m_options(), m_own_stream(transfer_ownership) {}

  NativeFile(int fd, OpenOptions options, bool transfer_ownership)
      : m_descriptor(fd), m_own_descriptor(transfer_ownership),
        m_stream(kInvalidStream), m_options(options), m_own_stream(false) {}

  ~NativeFile() override { Close(); }

  bool IsValid() const override;

  Status Read(void *buf, size_t &num_bytes) override;
  Status Write(const void *buf, size_t &num_bytes) override;
  Status Close() override;
  WaitableHandle GetWaitableHandle() override;
  Status GetFileSpec(FileSpec &file_spec) const override;
  int GetDescriptor() const override;
  FILE *GetStream() override;
  off_t SeekFromStart(off_t offset, Status *error_ptr = nullptr) override;
  off_t SeekFromCurrent(off_t offset, Status *error_ptr = nullptr) override;
  off_t SeekFromEnd(off_t offset, Status *error_ptr = nullptr) override;
  Status Read(void *dst, size_t &num_bytes, off_t &offset) override;
  Status Write(const void *src, size_t &num_bytes, off_t &offset) override;
  Status Flush() override;
  Status Sync() override;
  size_t PrintfVarArg(const char *format, va_list args) override;
  llvm::Expected<OpenOptions> GetOptions() const override;

  static char ID;
  bool isA(const void *classID) const override {
    return classID == &ID || File::isA(classID);
  }
  static bool classof(const File *file) { return file->isA(&ID); }

protected:
  struct ValueGuard {
    ValueGuard(std::mutex &m, bool b) : guard(m, std::adopt_lock), value(b) {}
    std::lock_guard<std::mutex> guard;
    bool value;
    operator bool() { return value; }
  };

  bool DescriptorIsValidUnlocked() const {

    return File::DescriptorIsValid(m_descriptor);
  }

  bool StreamIsValidUnlocked() const { return m_stream != kInvalidStream; }

  ValueGuard DescriptorIsValid() const {
    m_descriptor_mutex.lock();
    return ValueGuard(m_descriptor_mutex, DescriptorIsValidUnlocked());
  }

  ValueGuard StreamIsValid() const {
    m_stream_mutex.lock();
    return ValueGuard(m_stream_mutex, StreamIsValidUnlocked());
  }

  int m_descriptor;
  bool m_own_descriptor = false;
  mutable std::mutex m_descriptor_mutex;

  FILE *m_stream;
  mutable std::mutex m_stream_mutex;

  OpenOptions m_options{};
  bool m_own_stream = false;
  std::mutex offset_access_mutex;

private:
  NativeFile(const NativeFile &) = delete;
  const NativeFile &operator=(const NativeFile &) = delete;
};

class SerialPort : public NativeFile {
public:
  struct Options {
    std::optional<unsigned int> BaudRate;
    std::optional<Terminal::Parity> Parity;
    std::optional<Terminal::ParityCheck> ParityCheck;
    std::optional<unsigned int> StopBits;
  };

  // Obtain Options corresponding to the passed URL query string
  // (i.e. the part after '?').
  static llvm::Expected<Options> OptionsFromURL(llvm::StringRef urlqs);

  static llvm::Expected<std::unique_ptr<SerialPort>>
  Create(int fd, OpenOptions options, Options serial_options,
         bool transfer_ownership);

  bool IsValid() const override {
    return NativeFile::IsValid() && m_is_interactive == eLazyBoolYes;
  }

  Status Close() override;

  static char ID;
  bool isA(const void *classID) const override {
    return classID == &ID || File::isA(classID);
  }
  static bool classof(const File *file) { return file->isA(&ID); }

private:
  SerialPort(int fd, OpenOptions options, Options serial_options,
             bool transfer_ownership);

  SerialPort(const SerialPort &) = delete;
  const SerialPort &operator=(const SerialPort &) = delete;

  TerminalState m_state;
};

} // namespace lldb_private

#endif // LLDB_HOST_FILE_H
