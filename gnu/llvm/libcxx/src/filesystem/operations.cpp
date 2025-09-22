//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <__assert>
#include <__config>
#include <__utility/unreachable.h>
#include <array>
#include <climits>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <string_view>
#include <type_traits>
#include <vector>

#include "error.h"
#include "file_descriptor.h"
#include "path_parser.h"
#include "posix_compat.h"
#include "time_utils.h"

#if defined(_LIBCPP_WIN32API)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#else
#  include <dirent.h>
#  include <sys/stat.h>
#  include <sys/statvfs.h>
#  include <unistd.h>
#endif
#include <fcntl.h> /* values for fchmodat */
#include <time.h>

#if __has_include(<sys/sendfile.h>)
#  include <sys/sendfile.h>
#  define _LIBCPP_FILESYSTEM_USE_SENDFILE
#elif defined(__APPLE__) || __has_include(<copyfile.h>)
#  include <copyfile.h>
#  define _LIBCPP_FILESYSTEM_USE_COPYFILE
#else
#  include <fstream>
#  define _LIBCPP_FILESYSTEM_USE_FSTREAM
#endif

#if defined(__ELF__) && defined(_LIBCPP_LINK_RT_LIB)
#  pragma comment(lib, "rt")
#endif

_LIBCPP_BEGIN_NAMESPACE_FILESYSTEM

using detail::capture_errno;
using detail::ErrorHandler;
using detail::StatT;
using detail::TimeSpec;
using parser::createView;
using parser::PathParser;
using parser::string_view_t;

static path __do_absolute(const path& p, path* cwd, error_code* ec) {
  if (ec)
    ec->clear();
  if (p.is_absolute())
    return p;
  *cwd = __current_path(ec);
  if (ec && *ec)
    return {};
  return (*cwd) / p;
}

path __absolute(const path& p, error_code* ec) {
  path cwd;
  return __do_absolute(p, &cwd, ec);
}

path __canonical(path const& orig_p, error_code* ec) {
  path cwd;
  ErrorHandler<path> err("canonical", ec, &orig_p, &cwd);

  path p = __do_absolute(orig_p, &cwd, ec);
#if (defined(_POSIX_VERSION) && _POSIX_VERSION >= 200112) || defined(_LIBCPP_WIN32API)
  std::unique_ptr<path::value_type, decltype(&::free)> hold(detail::realpath(p.c_str(), nullptr), &::free);
  if (hold.get() == nullptr)
    return err.report(capture_errno());
  return {hold.get()};
#else
#  if defined(__MVS__) && !defined(PATH_MAX)
  path::value_type buff[_XOPEN_PATH_MAX + 1];
#  else
  path::value_type buff[PATH_MAX + 1];
#  endif
  path::value_type* ret;
  if ((ret = detail::realpath(p.c_str(), buff)) == nullptr)
    return err.report(capture_errno());
  return {ret};
#endif
}

void __copy(const path& from, const path& to, copy_options options, error_code* ec) {
  ErrorHandler<void> err("copy", ec, &from, &to);

  const bool sym_status = bool(options & (copy_options::create_symlinks | copy_options::skip_symlinks));

  const bool sym_status2 = bool(options & copy_options::copy_symlinks);

  error_code m_ec1;
  StatT f_st;
  const file_status f =
      sym_status || sym_status2 ? detail::posix_lstat(from, f_st, &m_ec1) : detail::posix_stat(from, f_st, &m_ec1);
  if (m_ec1)
    return err.report(m_ec1);

  StatT t_st;
  const file_status t = sym_status ? detail::posix_lstat(to, t_st, &m_ec1) : detail::posix_stat(to, t_st, &m_ec1);

  if (not status_known(t))
    return err.report(m_ec1);

  if (!exists(f) || is_other(f) || is_other(t) || (is_directory(f) && is_regular_file(t)) ||
      (exists(t) && detail::stat_equivalent(f_st, t_st))) {
    return err.report(errc::function_not_supported);
  }

  if (is_symlink(f)) {
    if (bool(copy_options::skip_symlinks & options)) {
      // do nothing
    } else if (not exists(t)) {
      __copy_symlink(from, to, ec);
    } else {
      return err.report(errc::file_exists);
    }
    return;
  } else if (is_regular_file(f)) {
    if (bool(copy_options::directories_only & options)) {
      // do nothing
    } else if (bool(copy_options::create_symlinks & options)) {
      __create_symlink(from, to, ec);
    } else if (bool(copy_options::create_hard_links & options)) {
      __create_hard_link(from, to, ec);
    } else if (is_directory(t)) {
      __copy_file(from, to / from.filename(), options, ec);
    } else {
      __copy_file(from, to, options, ec);
    }
    return;
  } else if (is_directory(f) && bool(copy_options::create_symlinks & options)) {
    return err.report(errc::is_a_directory);
  } else if (is_directory(f) && (bool(copy_options::recursive & options) || copy_options::none == options)) {
    if (!exists(t)) {
      // create directory to with attributes from 'from'.
      __create_directory(to, from, ec);
      if (ec && *ec) {
        return;
      }
    }
    directory_iterator it = ec ? directory_iterator(from, *ec) : directory_iterator(from);
    if (ec && *ec) {
      return;
    }
    error_code m_ec2;
    for (; !m_ec2 && it != directory_iterator(); it.increment(m_ec2)) {
      __copy(it->path(), to / it->path().filename(), options | copy_options::__in_recursive_copy, ec);
      if (ec && *ec) {
        return;
      }
    }
    if (m_ec2) {
      return err.report(m_ec2);
    }
  }
}

namespace detail {
namespace {

#if defined(_LIBCPP_FILESYSTEM_USE_SENDFILE)
bool copy_file_impl(FileDescriptor& read_fd, FileDescriptor& write_fd, error_code& ec) {
  size_t count = read_fd.get_stat().st_size;
  do {
    ssize_t res;
    if ((res = ::sendfile(write_fd.fd, read_fd.fd, nullptr, count)) == -1) {
      ec = capture_errno();
      return false;
    }
    count -= res;
  } while (count > 0);

  ec.clear();

  return true;
}
#elif defined(_LIBCPP_FILESYSTEM_USE_COPYFILE)
bool copy_file_impl(FileDescriptor& read_fd, FileDescriptor& write_fd, error_code& ec) {
  struct CopyFileState {
    copyfile_state_t state;
    CopyFileState() { state = copyfile_state_alloc(); }
    ~CopyFileState() { copyfile_state_free(state); }

  private:
    CopyFileState(CopyFileState const&)            = delete;
    CopyFileState& operator=(CopyFileState const&) = delete;
  };

  CopyFileState cfs;
  if (fcopyfile(read_fd.fd, write_fd.fd, cfs.state, COPYFILE_DATA) < 0) {
    ec = capture_errno();
    return false;
  }

  ec.clear();
  return true;
}
#elif defined(_LIBCPP_FILESYSTEM_USE_FSTREAM)
bool copy_file_impl(FileDescriptor& read_fd, FileDescriptor& write_fd, error_code& ec) {
  ifstream in;
  in.__open(read_fd.fd, ios::binary);
  if (!in.is_open()) {
    // This assumes that __open didn't reset the error code.
    ec = capture_errno();
    return false;
  }
  read_fd.fd = -1;
  ofstream out;
  out.__open(write_fd.fd, ios::binary);
  if (!out.is_open()) {
    ec = capture_errno();
    return false;
  }
  write_fd.fd = -1;

  if (in.good() && out.good()) {
    using InIt  = istreambuf_iterator<char>;
    using OutIt = ostreambuf_iterator<char>;
    InIt bin(in);
    InIt ein;
    OutIt bout(out);
    copy(bin, ein, bout);
  }
  if (out.fail() || in.fail()) {
    ec = make_error_code(errc::io_error);
    return false;
  }

  ec.clear();
  return true;
}
#else
#  error "Unknown implementation for copy_file_impl"
#endif // copy_file_impl implementation

} // end anonymous namespace
} // end namespace detail

bool __copy_file(const path& from, const path& to, copy_options options, error_code* ec) {
  using detail::FileDescriptor;
  ErrorHandler<bool> err("copy_file", ec, &to, &from);

  error_code m_ec;
  FileDescriptor from_fd = FileDescriptor::create_with_status(&from, m_ec, O_RDONLY | O_NONBLOCK | O_BINARY);
  if (m_ec)
    return err.report(m_ec);

  auto from_st           = from_fd.get_status();
  StatT const& from_stat = from_fd.get_stat();
  if (!is_regular_file(from_st)) {
    if (not m_ec)
      m_ec = make_error_code(errc::not_supported);
    return err.report(m_ec);
  }

  const bool skip_existing      = bool(copy_options::skip_existing & options);
  const bool update_existing    = bool(copy_options::update_existing & options);
  const bool overwrite_existing = bool(copy_options::overwrite_existing & options);

  StatT to_stat_path;
  file_status to_st = detail::posix_stat(to, to_stat_path, &m_ec);
  if (!status_known(to_st))
    return err.report(m_ec);

  const bool to_exists = exists(to_st);
  if (to_exists && !is_regular_file(to_st))
    return err.report(errc::not_supported);

  if (to_exists && detail::stat_equivalent(from_stat, to_stat_path))
    return err.report(errc::file_exists);

  if (to_exists && skip_existing)
    return false;

  bool ShouldCopy = [&]() {
    if (to_exists && update_existing) {
      auto from_time = detail::extract_mtime(from_stat);
      auto to_time   = detail::extract_mtime(to_stat_path);
      if (from_time.tv_sec < to_time.tv_sec)
        return false;
      if (from_time.tv_sec == to_time.tv_sec && from_time.tv_nsec <= to_time.tv_nsec)
        return false;
      return true;
    }
    if (!to_exists || overwrite_existing)
      return true;
    return err.report(errc::file_exists);
  }();
  if (!ShouldCopy)
    return false;

  // Don't truncate right away. We may not be opening the file we originally
  // looked at; we'll check this later.
  int to_open_flags = O_WRONLY | O_BINARY;
  if (!to_exists)
    to_open_flags |= O_CREAT;
  FileDescriptor to_fd = FileDescriptor::create_with_status(&to, m_ec, to_open_flags, from_stat.st_mode);
  if (m_ec)
    return err.report(m_ec);

  if (to_exists) {
    // Check that the file we initially stat'ed is equivalent to the one
    // we opened.
    // FIXME: report this better.
    if (!detail::stat_equivalent(to_stat_path, to_fd.get_stat()))
      return err.report(errc::bad_file_descriptor);

    // Set the permissions and truncate the file we opened.
    if (detail::posix_fchmod(to_fd, from_stat, m_ec))
      return err.report(m_ec);
    if (detail::posix_ftruncate(to_fd, 0, m_ec))
      return err.report(m_ec);
  }

  if (!detail::copy_file_impl(from_fd, to_fd, m_ec)) {
    // FIXME: Remove the dest file if we failed, and it didn't exist previously.
    return err.report(m_ec);
  }

  return true;
}

void __copy_symlink(const path& existing_symlink, const path& new_symlink, error_code* ec) {
  const path real_path(__read_symlink(existing_symlink, ec));
  if (ec && *ec) {
    return;
  }
#if defined(_LIBCPP_WIN32API)
  error_code local_ec;
  if (is_directory(real_path, local_ec))
    __create_directory_symlink(real_path, new_symlink, ec);
  else
#endif
    __create_symlink(real_path, new_symlink, ec);
}

bool __create_directories(const path& p, error_code* ec) {
  ErrorHandler<bool> err("create_directories", ec, &p);

  error_code m_ec;
  auto const st = detail::posix_stat(p, &m_ec);
  if (!status_known(st))
    return err.report(m_ec);
  else if (is_directory(st))
    return false;
  else if (exists(st))
    return err.report(errc::file_exists);

  const path parent = p.parent_path();
  if (!parent.empty()) {
    const file_status parent_st = status(parent, m_ec);
    if (not status_known(parent_st))
      return err.report(m_ec);
    if (not exists(parent_st)) {
      if (parent == p)
        return err.report(errc::invalid_argument);
      __create_directories(parent, ec);
      if (ec && *ec) {
        return false;
      }
    } else if (not is_directory(parent_st))
      return err.report(errc::not_a_directory);
  }
  bool ret = __create_directory(p, &m_ec);
  if (m_ec)
    return err.report(m_ec);
  return ret;
}

bool __create_directory(const path& p, error_code* ec) {
  ErrorHandler<bool> err("create_directory", ec, &p);

  if (detail::mkdir(p.c_str(), static_cast<int>(perms::all)) == 0)
    return true;

  if (errno != EEXIST)
    return err.report(capture_errno());
  error_code mec = capture_errno();
  error_code ignored_ec;
  const file_status st = status(p, ignored_ec);
  if (!is_directory(st))
    return err.report(mec);
  return false;
}

bool __create_directory(path const& p, path const& attributes, error_code* ec) {
  ErrorHandler<bool> err("create_directory", ec, &p, &attributes);

  StatT attr_stat;
  error_code mec;
  file_status st = detail::posix_stat(attributes, attr_stat, &mec);
  if (!status_known(st))
    return err.report(mec);
  if (!is_directory(st))
    return err.report(errc::not_a_directory, "the specified attribute path is invalid");

  if (detail::mkdir(p.c_str(), attr_stat.st_mode) == 0)
    return true;

  if (errno != EEXIST)
    return err.report(capture_errno());

  mec = capture_errno();
  error_code ignored_ec;
  st = status(p, ignored_ec);
  if (!is_directory(st))
    return err.report(mec);
  return false;
}

void __create_directory_symlink(path const& from, path const& to, error_code* ec) {
  ErrorHandler<void> err("create_directory_symlink", ec, &from, &to);
  if (detail::symlink_dir(from.c_str(), to.c_str()) == -1)
    return err.report(capture_errno());
}

void __create_hard_link(const path& from, const path& to, error_code* ec) {
  ErrorHandler<void> err("create_hard_link", ec, &from, &to);
  if (detail::link(from.c_str(), to.c_str()) == -1)
    return err.report(capture_errno());
}

void __create_symlink(path const& from, path const& to, error_code* ec) {
  ErrorHandler<void> err("create_symlink", ec, &from, &to);
  if (detail::symlink_file(from.c_str(), to.c_str()) == -1)
    return err.report(capture_errno());
}

path __current_path(error_code* ec) {
  ErrorHandler<path> err("current_path", ec);

#if defined(_LIBCPP_WIN32API) || defined(__GLIBC__) || defined(__APPLE__)
  // Common extension outside of POSIX getcwd() spec, without needing to
  // preallocate a buffer. Also supported by a number of other POSIX libcs.
  int size              = 0;
  path::value_type* ptr = nullptr;
  typedef decltype(&::free) Deleter;
  Deleter deleter = &::free;
#else
  errno     = 0; // Note: POSIX mandates that modifying `errno` is thread-safe.
  auto size = ::pathconf(".", _PC_PATH_MAX);
  if (size == -1) {
    if (errno != 0) {
      return err.report(capture_errno(), "call to pathconf failed");

      // `pathconf` returns `-1` without an error to indicate no limit.
    } else {
#  if defined(__MVS__) && !defined(PATH_MAX)
      size = _XOPEN_PATH_MAX + 1;
#  else
      size = PATH_MAX + 1;
#  endif
    }
  }

  auto buff             = unique_ptr<path::value_type[]>(new path::value_type[size + 1]);
  path::value_type* ptr = buff.get();

  // Preallocated buffer, don't free the buffer in the second unique_ptr
  // below.
  struct Deleter {
    void operator()(void*) const {}
  };
  Deleter deleter;
#endif

  unique_ptr<path::value_type, Deleter> hold(detail::getcwd(ptr, size), deleter);
  if (hold.get() == nullptr)
    return err.report(capture_errno(), "call to getcwd failed");

  return {hold.get()};
}

void __current_path(const path& p, error_code* ec) {
  ErrorHandler<void> err("current_path", ec, &p);
  if (detail::chdir(p.c_str()) == -1)
    err.report(capture_errno());
}

bool __equivalent(const path& p1, const path& p2, error_code* ec) {
  ErrorHandler<bool> err("equivalent", ec, &p1, &p2);

  error_code ec1, ec2;
  StatT st1 = {}, st2 = {};
  auto s1 = detail::posix_stat(p1.native(), st1, &ec1);
  if (!exists(s1))
    return err.report(errc::not_supported);
  auto s2 = detail::posix_stat(p2.native(), st2, &ec2);
  if (!exists(s2))
    return err.report(errc::not_supported);

  return detail::stat_equivalent(st1, st2);
}

uintmax_t __file_size(const path& p, error_code* ec) {
  ErrorHandler<uintmax_t> err("file_size", ec, &p);

  error_code m_ec;
  StatT st;
  file_status fst = detail::posix_stat(p, st, &m_ec);
  if (!exists(fst) || !is_regular_file(fst)) {
    errc error_kind = is_directory(fst) ? errc::is_a_directory : errc::not_supported;
    if (!m_ec)
      m_ec = make_error_code(error_kind);
    return err.report(m_ec);
  }
  // is_regular_file(p) == true
  return static_cast<uintmax_t>(st.st_size);
}

uintmax_t __hard_link_count(const path& p, error_code* ec) {
  ErrorHandler<uintmax_t> err("hard_link_count", ec, &p);

  error_code m_ec;
  StatT st;
  detail::posix_stat(p, st, &m_ec);
  if (m_ec)
    return err.report(m_ec);
  return static_cast<uintmax_t>(st.st_nlink);
}

bool __fs_is_empty(const path& p, error_code* ec) {
  ErrorHandler<bool> err("is_empty", ec, &p);

  error_code m_ec;
  StatT pst;
  auto st = detail::posix_stat(p, pst, &m_ec);
  if (m_ec)
    return err.report(m_ec);
  else if (!is_directory(st) && !is_regular_file(st))
    return err.report(errc::not_supported);
  else if (is_directory(st)) {
    auto it = ec ? directory_iterator(p, *ec) : directory_iterator(p);
    if (ec && *ec)
      return false;
    return it == directory_iterator{};
  } else if (is_regular_file(st))
    return static_cast<uintmax_t>(pst.st_size) == 0;

  __libcpp_unreachable();
}

file_time_type __last_write_time(const path& p, error_code* ec) {
  using namespace chrono;
  ErrorHandler<file_time_type> err("last_write_time", ec, &p);

  error_code m_ec;
  StatT st;
  detail::posix_stat(p, st, &m_ec);
  if (m_ec)
    return err.report(m_ec);
  return detail::__extract_last_write_time(p, st, ec);
}

void __last_write_time(const path& p, file_time_type new_time, error_code* ec) {
  using detail::fs_time;
  ErrorHandler<void> err("last_write_time", ec, &p);

#if defined(_LIBCPP_WIN32API)
  TimeSpec ts;
  if (!fs_time::convert_to_timespec(ts, new_time))
    return err.report(errc::value_too_large);
  detail::WinHandle h(p.c_str(), FILE_WRITE_ATTRIBUTES, 0);
  if (!h)
    return err.report(detail::make_windows_error(GetLastError()));
  FILETIME last_write = timespec_to_filetime(ts);
  if (!SetFileTime(h, nullptr, nullptr, &last_write))
    return err.report(detail::make_windows_error(GetLastError()));
#else
  error_code m_ec;
  array<TimeSpec, 2> tbuf;
#  if !defined(_LIBCPP_USE_UTIMENSAT)
  // This implementation has a race condition between determining the
  // last access time and attempting to set it to the same value using
  // ::utimes
  StatT st;
  file_status fst = detail::posix_stat(p, st, &m_ec);
  if (m_ec)
    return err.report(m_ec);
  tbuf[0] = detail::extract_atime(st);
#  else
  tbuf[0].tv_sec  = 0;
  tbuf[0].tv_nsec = UTIME_OMIT;
#  endif
  if (!fs_time::convert_to_timespec(tbuf[1], new_time))
    return err.report(errc::value_too_large);

  detail::set_file_times(p, tbuf, m_ec);
  if (m_ec)
    return err.report(m_ec);
#endif
}

void __permissions(const path& p, perms prms, perm_options opts, error_code* ec) {
  ErrorHandler<void> err("permissions", ec, &p);

  auto has_opt                = [&](perm_options o) { return bool(o & opts); };
  const bool resolve_symlinks = !has_opt(perm_options::nofollow);
  const bool add_perms        = has_opt(perm_options::add);
  const bool remove_perms     = has_opt(perm_options::remove);
  _LIBCPP_ASSERT_ARGUMENT_WITHIN_DOMAIN(
      (add_perms + remove_perms + has_opt(perm_options::replace)) == 1,
      "One and only one of the perm_options constants 'replace', 'add', or 'remove' must be present in opts");

  bool set_sym_perms = false;
  prms &= perms::mask;
  if (!resolve_symlinks || (add_perms || remove_perms)) {
    error_code m_ec;
    file_status st = resolve_symlinks ? detail::posix_stat(p, &m_ec) : detail::posix_lstat(p, &m_ec);
    set_sym_perms  = is_symlink(st);
    if (m_ec)
      return err.report(m_ec);
    // TODO(hardening): double-check this assertion -- it might be a valid (if rare) case when the permissions are
    // unknown.
    _LIBCPP_ASSERT_VALID_EXTERNAL_API_CALL(st.permissions() != perms::unknown, "Permissions unexpectedly unknown");
    if (add_perms)
      prms |= st.permissions();
    else if (remove_perms)
      prms = st.permissions() & ~prms;
  }
  const auto real_perms = static_cast<detail::ModeT>(prms & perms::mask);

#if defined(AT_SYMLINK_NOFOLLOW) && defined(AT_FDCWD)
  const int flags = set_sym_perms ? AT_SYMLINK_NOFOLLOW : 0;
  if (detail::fchmodat(AT_FDCWD, p.c_str(), real_perms, flags) == -1) {
    return err.report(capture_errno());
  }
#else
  if (set_sym_perms)
    return err.report(errc::operation_not_supported);
  if (::chmod(p.c_str(), real_perms) == -1) {
    return err.report(capture_errno());
  }
#endif
}

path __read_symlink(const path& p, error_code* ec) {
  ErrorHandler<path> err("read_symlink", ec, &p);

#if defined(PATH_MAX) || defined(MAX_SYMLINK_SIZE)
  struct NullDeleter {
    void operator()(void*) const {}
  };
#  ifdef MAX_SYMLINK_SIZE
  const size_t size = MAX_SYMLINK_SIZE + 1;
#  else
  const size_t size = PATH_MAX + 1;
#  endif
  path::value_type stack_buff[size];
  auto buff = std::unique_ptr<path::value_type[], NullDeleter>(stack_buff);
#else
  StatT sb;
  if (detail::lstat(p.c_str(), &sb) == -1) {
    return err.report(capture_errno());
  }
  const size_t size = sb.st_size + 1;
  auto buff         = unique_ptr<path::value_type[]>(new path::value_type[size]);
#endif
  detail::SSizeT ret;
  if ((ret = detail::readlink(p.c_str(), buff.get(), size)) == -1)
    return err.report(capture_errno());
  // Note that `ret` returning `0` would work, resulting in a valid empty string being returned.
  if (static_cast<size_t>(ret) >= size)
    return err.report(errc::value_too_large);
  buff[ret] = 0;
  return {buff.get()};
}

bool __remove(const path& p, error_code* ec) {
  ErrorHandler<bool> err("remove", ec, &p);
  if (detail::remove(p.c_str()) == -1) {
    if (errno != ENOENT)
      err.report(capture_errno());
    return false;
  }
  return true;
}

// We currently have two implementations of `__remove_all`. The first one is general and
// used on platforms where we don't have access to the `openat()` family of POSIX functions.
// That implementation uses `directory_iterator`, however it is vulnerable to some race
// conditions, see https://reviews.llvm.org/D118134 for details.
//
// The second implementation is used on platforms where `openat()` & friends are available,
// and it threads file descriptors through recursive calls to avoid such race conditions.
#if defined(_LIBCPP_WIN32API) || defined(__MVS__)
#  define REMOVE_ALL_USE_DIRECTORY_ITERATOR
#endif

#if defined(REMOVE_ALL_USE_DIRECTORY_ITERATOR)

namespace {

uintmax_t remove_all_impl(path const& p, error_code& ec) {
  const auto npos      = static_cast<uintmax_t>(-1);
  const file_status st = __symlink_status(p, &ec);
  if (ec)
    return npos;
  uintmax_t count = 1;
  if (is_directory(st)) {
    for (directory_iterator it(p, ec); !ec && it != directory_iterator(); it.increment(ec)) {
      auto other_count = remove_all_impl(it->path(), ec);
      if (ec)
        return npos;
      count += other_count;
    }
    if (ec)
      return npos;
  }
  if (!__remove(p, &ec))
    return npos;
  return count;
}

} // end namespace

uintmax_t __remove_all(const path& p, error_code* ec) {
  ErrorHandler<uintmax_t> err("remove_all", ec, &p);

  error_code mec;
  auto count = remove_all_impl(p, mec);
  if (mec) {
    if (mec == errc::no_such_file_or_directory)
      return 0;
    return err.report(mec);
  }
  return count;
}

#else // !REMOVE_ALL_USE_DIRECTORY_ITERATOR

namespace {

template <class Cleanup>
struct scope_exit {
  explicit scope_exit(Cleanup const& cleanup) : cleanup_(cleanup) {}

  ~scope_exit() { cleanup_(); }

private:
  Cleanup cleanup_;
};
_LIBCPP_CTAD_SUPPORTED_FOR_TYPE(scope_exit);

uintmax_t remove_all_impl(int parent_directory, const path& p, error_code& ec) {
  // First, try to open the path as a directory.
  const int options = O_CLOEXEC | O_RDONLY | O_DIRECTORY | O_NOFOLLOW;
  int fd            = ::openat(parent_directory, p.c_str(), options);
  if (fd != -1) {
    // If that worked, iterate over the contents of the directory and
    // remove everything in it, recursively.
    DIR* stream = ::fdopendir(fd);
    if (stream == nullptr) {
      ::close(fd);
      ec = detail::capture_errno();
      return 0;
    }
    // Note: `::closedir` will also close the associated file descriptor, so
    // there should be no call to `close(fd)`.
    scope_exit close_stream([=] { ::closedir(stream); });

    uintmax_t count = 0;
    while (true) {
      auto [str, type] = detail::posix_readdir(stream, ec);
      static_assert(std::is_same_v<decltype(str), std::string_view>);
      if (str == "." || str == "..") {
        continue;
      } else if (ec || str.empty()) {
        break; // we're done iterating through the directory
      } else {
        count += remove_all_impl(fd, str, ec);
      }
    }

    // Then, remove the now-empty directory itself.
    if (::unlinkat(parent_directory, p.c_str(), AT_REMOVEDIR) == -1) {
      ec = detail::capture_errno();
      return count;
    }

    return count + 1; // the contents of the directory + the directory itself
  }

  ec = detail::capture_errno();

  // If we failed to open `p` because it didn't exist, it's not an
  // error -- it might have moved or have been deleted already.
  if (ec == errc::no_such_file_or_directory) {
    ec.clear();
    return 0;
  }

  // If opening `p` failed because it wasn't a directory, remove it as
  // a normal file instead. Note that `openat()` can return either ENOTDIR
  // or ELOOP depending on the exact reason of the failure. On FreeBSD it
  // may return EMLINK instead of ELOOP, contradicting POSIX.
  if (ec == errc::not_a_directory || ec == errc::too_many_symbolic_link_levels || ec == errc::too_many_links) {
    ec.clear();
    if (::unlinkat(parent_directory, p.c_str(), /* flags = */ 0) == -1) {
      ec = detail::capture_errno();
      return 0;
    }
    return 1;
  }

  // Otherwise, it's a real error -- we don't remove anything.
  return 0;
}

} // end namespace

uintmax_t __remove_all(const path& p, error_code* ec) {
  ErrorHandler<uintmax_t> err("remove_all", ec, &p);
  error_code mec;
  uintmax_t count = remove_all_impl(AT_FDCWD, p, mec);
  if (mec)
    return err.report(mec);
  return count;
}

#endif // REMOVE_ALL_USE_DIRECTORY_ITERATOR

void __rename(const path& from, const path& to, error_code* ec) {
  ErrorHandler<void> err("rename", ec, &from, &to);
  if (detail::rename(from.c_str(), to.c_str()) == -1)
    err.report(capture_errno());
}

void __resize_file(const path& p, uintmax_t size, error_code* ec) {
  ErrorHandler<void> err("resize_file", ec, &p);
  if (detail::truncate(p.c_str(), static_cast< ::off_t>(size)) == -1)
    return err.report(capture_errno());
}

space_info __space(const path& p, error_code* ec) {
  ErrorHandler<void> err("space", ec, &p);
  space_info si;
  detail::StatVFS m_svfs = {};
  if (detail::statvfs(p.c_str(), &m_svfs) == -1) {
    err.report(capture_errno());
    si.capacity = si.free = si.available = static_cast<uintmax_t>(-1);
    return si;
  }
  // Multiply with overflow checking.
  auto do_mult = [&](uintmax_t& out, uintmax_t other) {
    out = other * m_svfs.f_frsize;
    if (other == 0 || out / other != m_svfs.f_frsize)
      out = static_cast<uintmax_t>(-1);
  };
  do_mult(si.capacity, m_svfs.f_blocks);
  do_mult(si.free, m_svfs.f_bfree);
  do_mult(si.available, m_svfs.f_bavail);
  return si;
}

file_status __status(const path& p, error_code* ec) { return detail::posix_stat(p, ec); }

file_status __symlink_status(const path& p, error_code* ec) { return detail::posix_lstat(p, ec); }

path __temp_directory_path(error_code* ec) {
  ErrorHandler<path> err("temp_directory_path", ec);

#if defined(_LIBCPP_WIN32API)
  wchar_t buf[MAX_PATH];
  DWORD retval = GetTempPathW(MAX_PATH, buf);
  if (!retval)
    return err.report(detail::make_windows_error(GetLastError()));
  if (retval > MAX_PATH)
    return err.report(errc::filename_too_long);
  // GetTempPathW returns a path with a trailing slash, which we
  // shouldn't include for consistency.
  if (buf[retval - 1] == L'\\')
    buf[retval - 1] = L'\0';
  path p(buf);
#else
  const char* env_paths[] = {"TMPDIR", "TMP", "TEMP", "TEMPDIR"};
  const char* ret         = nullptr;

  for (auto& ep : env_paths)
    if ((ret = getenv(ep)))
      break;
  if (ret == nullptr) {
#  if defined(__ANDROID__)
    ret = "/data/local/tmp";
#  else
    ret = "/tmp";
#  endif
  }

  path p(ret);
#endif
  error_code m_ec;
  file_status st = detail::posix_stat(p, &m_ec);
  if (!status_known(st))
    return err.report(m_ec, "cannot access path " PATH_CSTR_FMT, p.c_str());

  if (!exists(st) || !is_directory(st))
    return err.report(errc::not_a_directory, "path " PATH_CSTR_FMT " is not a directory", p.c_str());

  return p;
}

path __weakly_canonical(const path& p, error_code* ec) {
  ErrorHandler<path> err("weakly_canonical", ec, &p);

  if (p.empty())
    return __canonical("", ec);

  path result;
  path tmp;
  tmp.__reserve(p.native().size());
  auto PP = PathParser::CreateEnd(p.native());
  --PP;
  vector<string_view_t> DNEParts;

  error_code m_ec;
  while (PP.State_ != PathParser::PS_BeforeBegin) {
    tmp.assign(createView(p.native().data(), &PP.RawEntry.back()));
    file_status st = __status(tmp, &m_ec);
    if (!status_known(st)) {
      return err.report(m_ec);
    } else if (exists(st)) {
      result = __canonical(tmp, &m_ec);
      if (m_ec) {
        return err.report(m_ec);
      }
      break;
    }
    DNEParts.push_back(*PP);
    --PP;
  }
  if (PP.State_ == PathParser::PS_BeforeBegin) {
    result = __canonical("", &m_ec);
    if (m_ec) {
      return err.report(m_ec);
    }
  }
  if (DNEParts.empty())
    return result;
  for (auto It = DNEParts.rbegin(); It != DNEParts.rend(); ++It)
    result /= *It;
  return result.lexically_normal();
}

_LIBCPP_END_NAMESPACE_FILESYSTEM
