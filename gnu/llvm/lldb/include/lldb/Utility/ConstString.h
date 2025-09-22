//===-- ConstString.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_CONSTSTRING_H
#define LLDB_UTILITY_CONSTSTRING_H

#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FormatVariadic.h"

#include <cstddef>
#include <string_view>

namespace lldb_private {
class Stream;
}
namespace llvm {
class raw_ostream;
}

namespace lldb_private {

/// \class ConstString ConstString.h "lldb/Utility/ConstString.h"
/// A uniqued constant string class.
///
/// Provides an efficient way to store strings as uniqued strings. After the
/// strings are uniqued, finding strings that are equal to one another is very
/// fast as just the pointers need to be compared. It also allows for many
/// common strings from many different sources to be shared to keep the memory
/// footprint low.
///
/// No reference counting is done on strings that are added to the string
/// pool, once strings are added they are in the string pool for the life of
/// the program.
class ConstString {
public:
  /// Default constructor
  ///
  /// Initializes the string to an empty string.
  ConstString() = default;

  explicit ConstString(llvm::StringRef s);

  /// Construct with C String value
  ///
  /// Constructs this object with a C string by looking to see if the
  /// C string already exists in the global string pool. If it doesn't
  /// exist, it is added to the string pool.
  ///
  /// \param[in] cstr
  ///     A NULL terminated C string to add to the string pool.
  explicit ConstString(const char *cstr);

  /// Construct with C String value with max length
  ///
  /// Constructs this object with a C string with a length. If \a max_cstr_len
  /// is greater than the actual length of the string, the string length will
  /// be truncated. This allows substrings to be created without the need to
  /// NULL terminate the string as it is passed into this function.
  ///
  /// \param[in] cstr
  ///     A pointer to the first character in the C string. The C
  ///     string can be NULL terminated in a buffer that contains
  ///     more characters than the length of the string, or the
  ///     string can be part of another string and a new substring
  ///     can be created.
  ///
  /// \param[in] max_cstr_len
  ///     The max length of \a cstr. If the string length of \a cstr
  ///     is less than \a max_cstr_len, then the string will be
  ///     truncated. If the string length of \a cstr is greater than
  ///     \a max_cstr_len, then only max_cstr_len bytes will be used
  ///     from \a cstr.
  explicit ConstString(const char *cstr, size_t max_cstr_len);

  /// Convert to bool operator.
  ///
  /// This allows code to check a ConstString object to see if it contains a
  /// valid string using code such as:
  ///
  /// \code
  /// ConstString str(...);
  /// if (str)
  /// { ...
  /// \endcode
  ///
  /// \return
  ///     /b True this object contains a valid non-empty C string, \b
  ///     false otherwise.
  explicit operator bool() const { return !IsEmpty(); }

  /// Equal to operator
  ///
  /// Returns true if this string is equal to the string in \a rhs. This
  /// operation is very fast as it results in a pointer comparison since all
  /// strings are in a uniqued in a global string pool.
  ///
  /// \param[in] rhs
  ///     Another string object to compare this object to.
  ///
  /// \return
  ///     true if this object is equal to \a rhs.
  ///     false if this object is not equal to \a rhs.
  bool operator==(ConstString rhs) const {
    // We can do a pointer compare to compare these strings since they must
    // come from the same pool in order to be equal.
    return m_string == rhs.m_string;
  }

  /// Equal to operator against a non-ConstString value.
  ///
  /// Returns true if this string is equal to the string in \a rhs. This
  /// overload is usually slower than comparing against a ConstString value.
  /// However, if the rhs string not already a ConstString and it is impractical
  /// to turn it into a non-temporary variable, then this overload is faster.
  ///
  /// \param[in] rhs
  ///     Another string object to compare this object to.
  ///
  /// \return
  ///     \b true if this object is equal to \a rhs.
  ///     \b false if this object is not equal to \a rhs.
  bool operator==(const char *rhs) const {
    // ConstString differentiates between empty strings and nullptr strings, but
    // StringRef doesn't. Therefore we have to do this check manually now.
    if (m_string == nullptr && rhs != nullptr)
      return false;
    if (m_string != nullptr && rhs == nullptr)
      return false;

    return GetStringRef() == rhs;
  }

  /// Not equal to operator
  ///
  /// Returns true if this string is not equal to the string in \a rhs. This
  /// operation is very fast as it results in a pointer comparison since all
  /// strings are in a uniqued in a global string pool.
  ///
  /// \param[in] rhs
  ///     Another string object to compare this object to.
  ///
  /// \return
  ///     \b true if this object is not equal to \a rhs.
  ///     \b false if this object is equal to \a rhs.
  bool operator!=(ConstString rhs) const { return m_string != rhs.m_string; }

  /// Not equal to operator against a non-ConstString value.
  ///
  /// Returns true if this string is not equal to the string in \a rhs. This
  /// overload is usually slower than comparing against a ConstString value.
  /// However, if the rhs string not already a ConstString and it is impractical
  /// to turn it into a non-temporary variable, then this overload is faster.
  ///
  /// \param[in] rhs
  ///     Another string object to compare this object to.
  ///
  /// \return \b true if this object is not equal to \a rhs, false otherwise.
  bool operator!=(const char *rhs) const { return !(*this == rhs); }

  bool operator<(ConstString rhs) const;

  // Implicitly convert \class ConstString instances to \class StringRef.
  operator llvm::StringRef() const { return GetStringRef(); }

  // Explicitly convert \class ConstString instances to \class std::string_view.
  explicit operator std::string_view() const {
    return std::string_view(m_string, GetLength());
  }

  // Explicitly convert \class ConstString instances to \class std::string.
  explicit operator std::string() const { return GetString(); }

  /// Get the string value as a C string.
  ///
  /// Get the value of the contained string as a NULL terminated C string
  /// value.
  ///
  /// If \a value_if_empty is nullptr, then nullptr will be returned.
  ///
  /// \return Returns \a value_if_empty if the string is empty, otherwise
  ///     the C string value contained in this object.
  const char *AsCString(const char *value_if_empty = nullptr) const {
    return (IsEmpty() ? value_if_empty : m_string);
  }

  /// Get the string value as a llvm::StringRef
  ///
  /// \return
  ///     Returns a new llvm::StringRef object filled in with the
  ///     needed data.
  llvm::StringRef GetStringRef() const {
    return llvm::StringRef(m_string, GetLength());
  }

  /// Get the string value as a std::string
  std::string GetString() const {
    return std::string(AsCString(""), GetLength());
  }

  /// Get the string value as a C string.
  ///
  /// Get the value of the contained string as a NULL terminated C string
  /// value. Similar to the ConstString::AsCString() function, yet this
  /// function will always return nullptr if the string is not valid. So this
  /// function is a direct accessor to the string pointer value.
  ///
  /// \return
  ///     Returns nullptr the string is invalid, otherwise the C string
  ///     value contained in this object.
  const char *GetCString() const { return m_string; }

  /// Get the length in bytes of string value.
  ///
  /// The string pool stores the length of the string, so we can avoid calling
  /// strlen() on the pointer value with this function.
  ///
  /// \return
  ///     Returns the number of bytes that this string occupies in
  ///     memory, not including the NULL termination byte.
  size_t GetLength() const;

  /// Clear this object's state.
  ///
  /// Clear any contained string and reset the value to the empty string
  /// value.
  void Clear() { m_string = nullptr; }

  /// Equal to operator
  ///
  /// Returns true if this string is equal to the string in \a rhs. If case
  /// sensitive equality is tested, this operation is very fast as it results
  /// in a pointer comparison since all strings are in a uniqued in a global
  /// string pool.
  ///
  /// \param[in] lhs
  ///     The Left Hand Side const ConstString object reference.
  ///
  /// \param[in] rhs
  ///     The Right Hand Side const ConstString object reference.
  ///
  /// \param[in] case_sensitive
  ///     Case sensitivity. If true, case sensitive equality
  ///     will be tested, otherwise character case will be ignored
  ///
  /// \return \b true if this object is equal to \a rhs, \b false otherwise.
  static bool Equals(ConstString lhs, ConstString rhs,
                     const bool case_sensitive = true);

  /// Compare two string objects.
  ///
  /// Compares the C string values contained in \a lhs and \a rhs and returns
  /// an integer result.
  ///
  /// NOTE: only call this function when you want a true string
  /// comparison. If you want string equality use the, use the == operator as
  /// it is much more efficient. Also if you want string inequality, use the
  /// != operator for the same reasons.
  ///
  /// \param[in] lhs
  ///     The Left Hand Side const ConstString object reference.
  ///
  /// \param[in] rhs
  ///     The Right Hand Side const ConstString object reference.
  ///
  /// \param[in] case_sensitive
  ///     Case sensitivity of compare. If true, case sensitive compare
  ///     will be performed, otherwise character case will be ignored
  ///
  /// \return -1 if lhs < rhs, 0 if lhs == rhs, 1 if lhs > rhs
  static int Compare(ConstString lhs, ConstString rhs,
                     const bool case_sensitive = true);

  /// Dump the object description to a stream.
  ///
  /// Dump the string value to the stream \a s. If the contained string is
  /// empty, print \a value_if_empty to the stream instead. If \a
  /// value_if_empty is nullptr, then nothing will be dumped to the stream.
  ///
  /// \param[in] s
  ///     The stream that will be used to dump the object description.
  ///
  /// \param[in] value_if_empty
  ///     The value to dump if the string is empty. If nullptr, nothing
  ///     will be output to the stream.
  void Dump(Stream *s, const char *value_if_empty = nullptr) const;

  /// Dump the object debug description to a stream.
  ///
  /// \param[in] s
  ///     The stream that will be used to dump the object description.
  void DumpDebug(Stream *s) const;

  /// Test for empty string.
  ///
  /// \return
  ///     \b true if the contained string is empty.
  ///     \b false if the contained string is not empty.
  bool IsEmpty() const { return m_string == nullptr || m_string[0] == '\0'; }

  /// Test for null string.
  ///
  /// \return
  ///     \b true if there is no string associated with this instance.
  ///     \b false if there is a string associated with this instance.
  bool IsNull() const { return m_string == nullptr; }

  /// Set the C string value.
  ///
  /// Set the string value in the object by uniquing the \a cstr string value
  /// in our global string pool.
  ///
  /// If the C string already exists in the global string pool, it finds the
  /// current entry and returns the existing value. If it doesn't exist, it is
  /// added to the string pool.
  ///
  /// \param[in] cstr
  ///     A NULL terminated C string to add to the string pool.
  void SetCString(const char *cstr);

  void SetString(llvm::StringRef s);

  /// Set the C string value and its mangled counterpart.
  ///
  /// Object files and debug symbols often use mangled string to represent the
  /// linkage name for a symbol, function or global. The string pool can
  /// efficiently store these values and their counterparts so when we run
  /// into another instance of a mangled name, we can avoid calling the name
  /// demangler over and over on the same strings and then trying to unique
  /// them.
  ///
  /// \param[in] demangled
  ///     The demangled string to correlate with the \a mangled name.
  ///
  /// \param[in] mangled
  ///     The already uniqued mangled ConstString to correlate the
  ///     soon to be uniqued version of \a demangled.
  void SetStringWithMangledCounterpart(llvm::StringRef demangled,
                                       ConstString mangled);

  /// Retrieve the mangled or demangled counterpart for a mangled or demangled
  /// ConstString.
  ///
  /// Object files and debug symbols often use mangled string to represent the
  /// linkage name for a symbol, function or global. The string pool can
  /// efficiently store these values and their counterparts so when we run
  /// into another instance of a mangled name, we can avoid calling the name
  /// demangler over and over on the same strings and then trying to unique
  /// them.
  ///
  /// \param[in] counterpart
  ///     A reference to a ConstString object that might get filled in
  ///     with the demangled/mangled counterpart.
  ///
  /// \return
  ///     /b True if \a counterpart was filled in with the counterpart
  ///     /b false otherwise.
  bool GetMangledCounterpart(ConstString &counterpart) const;

  /// Set the C string value with length.
  ///
  /// Set the string value in the object by uniquing \a cstr_len bytes
  /// starting at the \a cstr string value in our global string pool. If trim
  /// is true, then \a cstr_len indicates a maximum length of the CString and
  /// if the actual length of the string is less, then it will be trimmed.
  ///
  /// If the C string already exists in the global string pool, it finds the
  /// current entry and returns the existing value. If it doesn't exist, it is
  /// added to the string pool.
  ///
  /// \param[in] cstr
  ///     A NULL terminated C string to add to the string pool.
  ///
  /// \param[in] cstr_len
  ///     The maximum length of the C string.
  void SetCStringWithLength(const char *cstr, size_t cstr_len);

  /// Set the C string value with the minimum length between \a fixed_cstr_len
  /// and the actual length of the C string. This can be used for data
  /// structures that have a fixed length to store a C string where the string
  /// might not be NULL terminated if the string takes the entire buffer.
  void SetTrimmedCStringWithLength(const char *cstr, size_t fixed_cstr_len);

  /// Get the memory cost of this object.
  ///
  /// Return the size in bytes that this object takes in memory. This returns
  /// the size in bytes of this object, which does not include any the shared
  /// string values it may refer to.
  ///
  /// \return
  ///     The number of bytes that this object occupies in memory.
  size_t MemorySize() const { return sizeof(ConstString); }

  struct MemoryStats {
    size_t GetBytesTotal() const { return bytes_total; }
    size_t GetBytesUsed() const { return bytes_used; }
    size_t GetBytesUnused() const { return bytes_total - bytes_used; }
    size_t bytes_total = 0;
    size_t bytes_used = 0;
  };

  static MemoryStats GetMemoryStats();

protected:
  template <typename T, typename Enable> friend struct ::llvm::DenseMapInfo;
  /// Only used by DenseMapInfo.
  static ConstString FromStringPoolPointer(const char *ptr) {
    ConstString s;
    s.m_string = ptr;
    return s;
  };

  const char *m_string = nullptr;
};

/// Stream the string value \a str to the stream \a s
Stream &operator<<(Stream &s, ConstString str);

} // namespace lldb_private

namespace llvm {
template <> struct format_provider<lldb_private::ConstString> {
  static void format(const lldb_private::ConstString &CS, llvm::raw_ostream &OS,
                     llvm::StringRef Options);
};

/// DenseMapInfo implementation.
/// \{
template <> struct DenseMapInfo<lldb_private::ConstString> {
  static inline lldb_private::ConstString getEmptyKey() {
    return lldb_private::ConstString::FromStringPoolPointer(
        DenseMapInfo<const char *>::getEmptyKey());
  }
  static inline lldb_private::ConstString getTombstoneKey() {
    return lldb_private::ConstString::FromStringPoolPointer(
        DenseMapInfo<const char *>::getTombstoneKey());
  }
  static unsigned getHashValue(lldb_private::ConstString val) {
    return DenseMapInfo<const char *>::getHashValue(val.m_string);
  }
  static bool isEqual(lldb_private::ConstString LHS,
                      lldb_private::ConstString RHS) {
    return LHS == RHS;
  }
};
/// \}

inline raw_ostream &operator<<(raw_ostream &os, lldb_private::ConstString s) {
  os << s.GetStringRef();
  return os;
}
} // namespace llvm

#endif // LLDB_UTILITY_CONSTSTRING_H
