//===- llvm/Support/Path.h - Path Operating System Concept ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the llvm::sys::path namespace. It is designed after
// TR2/boost filesystem (v3), but modified to remove exception handling and the
// path class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_PATH_H
#define LLVM_SUPPORT_PATH_H

#include "llvm/ADT/Twine.h"
#include "llvm/ADT/iterator.h"
#include "llvm/Support/DataTypes.h"
#include <iterator>

namespace llvm {
namespace sys {
namespace path {

enum class Style {
  native,
  posix,
  windows_slash,
  windows_backslash,
  windows = windows_backslash, // deprecated
};

/// Check if \p S uses POSIX path rules.
constexpr bool is_style_posix(Style S) {
  if (S == Style::posix)
    return true;
  if (S != Style::native)
    return false;
#if defined(_WIN32)
  return false;
#else
  return true;
#endif
}

/// Check if \p S uses Windows path rules.
constexpr bool is_style_windows(Style S) { return !is_style_posix(S); }

/// @name Lexical Component Iterator
/// @{

/// Path iterator.
///
/// This is an input iterator that iterates over the individual components in
/// \a path. The traversal order is as follows:
/// * The root-name element, if present.
/// * The root-directory element, if present.
/// * Each successive filename element, if present.
/// * Dot, if one or more trailing non-root slash characters are present.
/// Traversing backwards is possible with \a reverse_iterator
///
/// Iteration examples. Each component is separated by ',':
/// @code
///   /          => /
///   /foo       => /,foo
///   foo/       => foo,.
///   /foo/bar   => /,foo,bar
///   ../        => ..,.
///   C:\foo\bar => C:,\,foo,bar
/// @endcode
class const_iterator
    : public iterator_facade_base<const_iterator, std::input_iterator_tag,
                                  const StringRef> {
  StringRef Path;          ///< The entire path.
  StringRef Component;     ///< The current component. Not necessarily in Path.
  size_t    Position = 0;  ///< The iterators current position within Path.
  Style S = Style::native; ///< The path style to use.

  // An end iterator has Position = Path.size() + 1.
  friend const_iterator begin(StringRef path, Style style);
  friend const_iterator end(StringRef path);

public:
  reference operator*() const { return Component; }
  const_iterator &operator++();    // preincrement
  bool operator==(const const_iterator &RHS) const;

  /// Difference in bytes between this and RHS.
  ptrdiff_t operator-(const const_iterator &RHS) const;
};

/// Reverse path iterator.
///
/// This is an input iterator that iterates over the individual components in
/// \a path in reverse order. The traversal order is exactly reversed from that
/// of \a const_iterator
class reverse_iterator
    : public iterator_facade_base<reverse_iterator, std::input_iterator_tag,
                                  const StringRef> {
  StringRef Path;          ///< The entire path.
  StringRef Component;     ///< The current component. Not necessarily in Path.
  size_t    Position = 0;  ///< The iterators current position within Path.
  Style S = Style::native; ///< The path style to use.

  friend reverse_iterator rbegin(StringRef path, Style style);
  friend reverse_iterator rend(StringRef path);

public:
  reference operator*() const { return Component; }
  reverse_iterator &operator++();    // preincrement
  bool operator==(const reverse_iterator &RHS) const;

  /// Difference in bytes between this and RHS.
  ptrdiff_t operator-(const reverse_iterator &RHS) const;
};

/// Get begin iterator over \a path.
/// @param path Input path.
/// @returns Iterator initialized with the first component of \a path.
const_iterator begin(StringRef path, Style style = Style::native);

/// Get end iterator over \a path.
/// @param path Input path.
/// @returns Iterator initialized to the end of \a path.
const_iterator end(StringRef path);

/// Get reverse begin iterator over \a path.
/// @param path Input path.
/// @returns Iterator initialized with the first reverse component of \a path.
reverse_iterator rbegin(StringRef path, Style style = Style::native);

/// Get reverse end iterator over \a path.
/// @param path Input path.
/// @returns Iterator initialized to the reverse end of \a path.
reverse_iterator rend(StringRef path);

/// @}
/// @name Lexical Modifiers
/// @{

/// Remove the last component from \a path unless it is the root dir.
///
/// Similar to the POSIX "dirname" utility.
///
/// @code
///   directory/filename.cpp => directory/
///   directory/             => directory
///   filename.cpp           => <empty>
///   /                      => /
/// @endcode
///
/// @param path A path that is modified to not have a file component.
void remove_filename(SmallVectorImpl<char> &path, Style style = Style::native);

/// Replace the file extension of \a path with \a extension.
///
/// @code
///   ./filename.cpp => ./filename.extension
///   ./filename     => ./filename.extension
///   ./             => ./.extension
/// @endcode
///
/// @param path A path that has its extension replaced with \a extension.
/// @param extension The extension to be added. It may be empty. It may also
///                  optionally start with a '.', if it does not, one will be
///                  prepended.
void replace_extension(SmallVectorImpl<char> &path, const Twine &extension,
                       Style style = Style::native);

/// Replace matching path prefix with another path.
///
/// @code
///   /foo, /old, /new => /foo
///   /old, /old, /new => /new
///   /old, /old/, /new => /old
///   /old/foo, /old, /new => /new/foo
///   /old/foo, /old/, /new => /new/foo
///   /old/foo, /old/, /new/ => /new/foo
///   /oldfoo, /old, /new => /oldfoo
///   /foo, <empty>, /new => /new/foo
///   /foo, <empty>, new => new/foo
///   /old/foo, /old, <empty> => /foo
/// @endcode
///
/// @param Path If \a Path starts with \a OldPrefix modify to instead
///        start with \a NewPrefix.
/// @param OldPrefix The path prefix to strip from \a Path.
/// @param NewPrefix The path prefix to replace \a NewPrefix with.
/// @param style The style used to match the prefix. Exact match using
/// Posix style, case/separator insensitive match for Windows style.
/// @result true if \a Path begins with OldPrefix
bool replace_path_prefix(SmallVectorImpl<char> &Path, StringRef OldPrefix,
                         StringRef NewPrefix,
                         Style style = Style::native);

/// Remove redundant leading "./" pieces and consecutive separators.
///
/// @param path Input path.
/// @result The cleaned-up \a path.
StringRef remove_leading_dotslash(StringRef path, Style style = Style::native);

/// In-place remove any './' and optionally '../' components from a path.
///
/// @param path processed path
/// @param remove_dot_dot specify if '../' (except for leading "../") should be
/// removed
/// @result True if path was changed
bool remove_dots(SmallVectorImpl<char> &path, bool remove_dot_dot = false,
                 Style style = Style::native);

/// Append to path.
///
/// @code
///   /foo  + bar/f => /foo/bar/f
///   /foo/ + bar/f => /foo/bar/f
///   foo   + bar/f => foo/bar/f
/// @endcode
///
/// @param path Set to \a path + \a component.
/// @param a The component to be appended to \a path.
void append(SmallVectorImpl<char> &path, const Twine &a,
                                         const Twine &b = "",
                                         const Twine &c = "",
                                         const Twine &d = "");

void append(SmallVectorImpl<char> &path, Style style, const Twine &a,
            const Twine &b = "", const Twine &c = "", const Twine &d = "");

/// Append to path.
///
/// @code
///   /foo  + [bar,f] => /foo/bar/f
///   /foo/ + [bar,f] => /foo/bar/f
///   foo   + [bar,f] => foo/bar/f
/// @endcode
///
/// @param path Set to \a path + [\a begin, \a end).
/// @param begin Start of components to append.
/// @param end One past the end of components to append.
void append(SmallVectorImpl<char> &path, const_iterator begin,
            const_iterator end, Style style = Style::native);

/// @}
/// @name Transforms (or some other better name)
/// @{

/// Convert path to the native form. This is used to give paths to users and
/// operating system calls in the platform's normal way. For example, on Windows
/// all '/' are converted to '\'. On Unix, it converts all '\' to '/'.
///
/// @param path A path that is transformed to native format.
/// @param result Holds the result of the transformation.
void native(const Twine &path, SmallVectorImpl<char> &result,
            Style style = Style::native);

/// Convert path to the native form in place. This is used to give paths to
/// users and operating system calls in the platform's normal way. For example,
/// on Windows all '/' are converted to '\'.
///
/// @param path A path that is transformed to native format.
void native(SmallVectorImpl<char> &path, Style style = Style::native);

/// For Windows path styles, convert path to use the preferred path separators.
/// For other styles, do nothing.
///
/// @param path A path that is transformed to preferred format.
inline void make_preferred(SmallVectorImpl<char> &path,
                           Style style = Style::native) {
  if (!is_style_windows(style))
    return;
  native(path, style);
}

/// Replaces backslashes with slashes if Windows.
///
/// @param path processed path
/// @result The result of replacing backslashes with forward slashes if Windows.
/// On Unix, this function is a no-op because backslashes are valid path
/// chracters.
std::string convert_to_slash(StringRef path, Style style = Style::native);

/// @}
/// @name Lexical Observers
/// @{

/// Get root name.
///
/// @code
///   //net/hello => //net
///   c:/hello    => c: (on Windows, on other platforms nothing)
///   /hello      => <empty>
/// @endcode
///
/// @param path Input path.
/// @result The root name of \a path if it has one, otherwise "".
StringRef root_name(StringRef path, Style style = Style::native);

/// Get root directory.
///
/// @code
///   /goo/hello => /
///   c:/hello   => /
///   d/file.txt => <empty>
/// @endcode
///
/// @param path Input path.
/// @result The root directory of \a path if it has one, otherwise
///               "".
StringRef root_directory(StringRef path, Style style = Style::native);

/// Get root path.
///
/// Equivalent to root_name + root_directory.
///
/// @param path Input path.
/// @result The root path of \a path if it has one, otherwise "".
StringRef root_path(StringRef path, Style style = Style::native);

/// Get relative path.
///
/// @code
///   C:\hello\world => hello\world
///   foo/bar        => foo/bar
///   /foo/bar       => foo/bar
/// @endcode
///
/// @param path Input path.
/// @result The path starting after root_path if one exists, otherwise "".
StringRef relative_path(StringRef path, Style style = Style::native);

/// Get parent path.
///
/// @code
///   /          => <empty>
///   /foo       => /
///   foo/../bar => foo/..
/// @endcode
///
/// @param path Input path.
/// @result The parent path of \a path if one exists, otherwise "".
StringRef parent_path(StringRef path, Style style = Style::native);

/// Get filename.
///
/// @code
///   /foo.txt    => foo.txt
///   .          => .
///   ..         => ..
///   /          => /
/// @endcode
///
/// @param path Input path.
/// @result The filename part of \a path. This is defined as the last component
///         of \a path. Similar to the POSIX "basename" utility.
StringRef filename(StringRef path, Style style = Style::native);

/// Get stem.
///
/// If filename contains a dot but not solely one or two dots, result is the
/// substring of filename ending at (but not including) the last dot. Otherwise
/// it is filename.
///
/// @code
///   /foo/bar.txt => bar
///   /foo/bar     => bar
///   /foo/.txt    => <empty>
///   /foo/.       => .
///   /foo/..      => ..
/// @endcode
///
/// @param path Input path.
/// @result The stem of \a path.
StringRef stem(StringRef path, Style style = Style::native);

/// Get extension.
///
/// If filename contains a dot but not solely one or two dots, result is the
/// substring of filename starting at (and including) the last dot, and ending
/// at the end of \a path. Otherwise "".
///
/// @code
///   /foo/bar.txt => .txt
///   /foo/bar     => <empty>
///   /foo/.txt    => .txt
/// @endcode
///
/// @param path Input path.
/// @result The extension of \a path.
StringRef extension(StringRef path, Style style = Style::native);

/// Check whether the given char is a path separator on the host OS.
///
/// @param value a character
/// @result true if \a value is a path separator character on the host OS
bool is_separator(char value, Style style = Style::native);

/// Return the preferred separator for this platform.
///
/// @result StringRef of the preferred separator, null-terminated.
StringRef get_separator(Style style = Style::native);

/// Get the typical temporary directory for the system, e.g.,
/// "/var/tmp" or "C:/TEMP"
///
/// @param erasedOnReboot Whether to favor a path that is erased on reboot
/// rather than one that potentially persists longer. This parameter will be
/// ignored if the user or system has set the typical environment variable
/// (e.g., TEMP on Windows, TMPDIR on *nix) to specify a temporary directory.
///
/// @param result Holds the resulting path name.
void system_temp_directory(bool erasedOnReboot, SmallVectorImpl<char> &result);

/// Get the user's home directory.
///
/// @param result Holds the resulting path name.
/// @result True if a home directory is set, false otherwise.
bool home_directory(SmallVectorImpl<char> &result);

/// Get the directory where packages should read user-specific configurations.
/// e.g. $XDG_CONFIG_HOME.
///
/// @param result Holds the resulting path name.
/// @result True if the appropriate path was determined, it need not exist.
bool user_config_directory(SmallVectorImpl<char> &result);

/// Get the directory where installed packages should put their
/// machine-local cache, e.g. $XDG_CACHE_HOME.
///
/// @param result Holds the resulting path name.
/// @result True if the appropriate path was determined, it need not exist.
bool cache_directory(SmallVectorImpl<char> &result);

/// Has root name?
///
/// root_name != ""
///
/// @param path Input path.
/// @result True if the path has a root name, false otherwise.
bool has_root_name(const Twine &path, Style style = Style::native);

/// Has root directory?
///
/// root_directory != ""
///
/// @param path Input path.
/// @result True if the path has a root directory, false otherwise.
bool has_root_directory(const Twine &path, Style style = Style::native);

/// Has root path?
///
/// root_path != ""
///
/// @param path Input path.
/// @result True if the path has a root path, false otherwise.
bool has_root_path(const Twine &path, Style style = Style::native);

/// Has relative path?
///
/// relative_path != ""
///
/// @param path Input path.
/// @result True if the path has a relative path, false otherwise.
bool has_relative_path(const Twine &path, Style style = Style::native);

/// Has parent path?
///
/// parent_path != ""
///
/// @param path Input path.
/// @result True if the path has a parent path, false otherwise.
bool has_parent_path(const Twine &path, Style style = Style::native);

/// Has filename?
///
/// filename != ""
///
/// @param path Input path.
/// @result True if the path has a filename, false otherwise.
bool has_filename(const Twine &path, Style style = Style::native);

/// Has stem?
///
/// stem != ""
///
/// @param path Input path.
/// @result True if the path has a stem, false otherwise.
bool has_stem(const Twine &path, Style style = Style::native);

/// Has extension?
///
/// extension != ""
///
/// @param path Input path.
/// @result True if the path has a extension, false otherwise.
bool has_extension(const Twine &path, Style style = Style::native);

/// Is path absolute?
///
/// According to cppreference.com, C++17 states: "An absolute path is a path
/// that unambiguously identifies the location of a file without reference to
/// an additional starting location."
///
/// In other words, the rules are:
/// 1) POSIX style paths with nonempty root directory are absolute.
/// 2) Windows style paths with nonempty root name and root directory are
///    absolute.
/// 3) No other paths are absolute.
///
/// \see has_root_name
/// \see has_root_directory
///
/// @param path Input path.
/// @result True if the path is absolute, false if it is not.
bool is_absolute(const Twine &path, Style style = Style::native);

/// Is path absolute using GNU rules?
///
/// GNU rules are:
/// 1) Paths starting with a path separator are absolute.
/// 2) Windows style paths are also absolute if they start with a character
///    followed by ':'.
/// 3) No other paths are absolute.
///
/// On Windows style the path "C:\Users\Default" has "C:" as root name and "\"
/// as root directory.
///
/// Hence "C:" on Windows is absolute under GNU rules and not absolute under
/// C++17 because it has no root directory. Likewise "/" and "\" on Windows are
/// absolute under GNU and are not absolute under C++17 due to empty root name.
///
/// \see has_root_name
/// \see has_root_directory
///
/// @param path Input path.
/// @param style The style of \p path (e.g. Windows or POSIX). "native" style
/// means to derive the style from the host.
/// @result True if the path is absolute following GNU rules, false if it is
/// not.
bool is_absolute_gnu(const Twine &path, Style style = Style::native);

/// Is path relative?
///
/// @param path Input path.
/// @result True if the path is relative, false if it is not.
bool is_relative(const Twine &path, Style style = Style::native);

} // end namespace path
} // end namespace sys
} // end namespace llvm

#endif
