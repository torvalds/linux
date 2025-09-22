//===-- Mangled.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_MANGLED_H
#define LLDB_CORE_MANGLED_H

#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-types.h"
#include "lldb/Utility/ConstString.h"
#include "llvm/ADT/StringRef.h"

#include <cstddef>
#include <memory>

namespace lldb_private {

/// \class Mangled Mangled.h "lldb/Core/Mangled.h"
/// A class that handles mangled names.
///
/// Designed to handle mangled names. The demangled version of any names will
/// be computed when the demangled name is accessed through the Demangled()
/// accessor. This class can also tokenize the demangled version of the name
/// for powerful searches. Functions and symbols could make instances of this
/// class for their mangled names. Uniqued string pools are used for the
/// mangled, demangled, and token string values to allow for faster
/// comparisons and for efficient memory use.
class Mangled {
public:
  enum NamePreference {
    ePreferMangled,
    ePreferDemangled,
    ePreferDemangledWithoutArguments
  };

  enum ManglingScheme {
    eManglingSchemeNone = 0,
    eManglingSchemeMSVC,
    eManglingSchemeItanium,
    eManglingSchemeRustV0,
    eManglingSchemeD,
    eManglingSchemeSwift,
  };

  /// Default constructor.
  ///
  /// Initialize with both mangled and demangled names empty.
  Mangled() = default;

  /// Construct with name.
  ///
  /// Constructor with an optional string and auto-detect if \a name is
  /// mangled or not.
  ///
  /// \param[in] name
  ///     The already const name to copy into this object.
  explicit Mangled(ConstString name);

  explicit Mangled(llvm::StringRef name);

  bool operator==(const Mangled &rhs) const {
    return m_mangled == rhs.m_mangled &&
           GetDemangledName() == rhs.GetDemangledName();
  }

  bool operator!=(const Mangled &rhs) const {
    return !(*this == rhs);
  }

  /// Convert to bool operator.
  ///
  /// This allows code to check any Mangled objects to see if they contain
  /// anything valid using code such as:
  ///
  /// \code
  /// Mangled mangled(...);
  /// if (mangled)
  /// { ...
  /// \endcode
  ///
  /// \return
  ///     Returns \b true if either the mangled or unmangled name is set,
  ///     \b false if the object has an empty mangled and unmangled name.
  explicit operator bool() const;

  /// Clear the mangled and demangled values.
  void Clear();

  /// Compare the mangled string values
  ///
  /// Compares the Mangled::GetName() string in \a lhs and \a rhs.
  ///
  /// \param[in] lhs
  ///     A const reference to the Left Hand Side object to compare.
  ///
  /// \param[in] rhs
  ///     A const reference to the Right Hand Side object to compare.
  ///
  /// \return
  ///     -1 if \a lhs is less than \a rhs
  ///     0 if \a lhs is equal to \a rhs
  ///     1 if \a lhs is greater than \a rhs
  static int Compare(const Mangled &lhs, const Mangled &rhs);

  /// Dump a description of this object to a Stream \a s.
  ///
  /// Dump a Mangled object to stream \a s. We don't force our demangled name
  /// to be computed currently (we don't use the accessor).
  ///
  /// \param[in] s
  ///     The stream to which to dump the object description.
  void Dump(Stream *s) const;

  /// Dump a debug description of this object to a Stream \a s.
  ///
  /// \param[in] s
  ///     The stream to which to dump the object description.
  void DumpDebug(Stream *s) const;

  /// Demangled name get accessor.
  ///
  /// \return
  ///     A const reference to the demangled name string object.
  ConstString GetDemangledName() const;

  /// Display demangled name get accessor.
  ///
  /// \return
  ///     A const reference to the display demangled name string object.
  ConstString GetDisplayDemangledName() const;

  void SetDemangledName(ConstString name) { m_demangled = name; }

  void SetMangledName(ConstString name) { m_mangled = name; }

  /// Mangled name get accessor.
  ///
  /// \return
  ///     A reference to the mangled name string object.
  ConstString &GetMangledName() { return m_mangled; }

  /// Mangled name get accessor.
  ///
  /// \return
  ///     A const reference to the mangled name string object.
  ConstString GetMangledName() const { return m_mangled; }

  /// Best name get accessor.
  ///
  /// \param[in] preference
  ///     Which name would you prefer to get?
  ///
  /// \return
  ///     A const reference to the preferred name string object if this
  ///     object has a valid name of that kind, else a const reference to the
  ///     other name is returned.
  ConstString GetName(NamePreference preference = ePreferDemangled) const;

  /// Check if "name" matches either the mangled or demangled name.
  ///
  /// \param[in] name
  ///     A name to match against both strings.
  ///
  /// \return
  ///     \b True if \a name matches either name, \b false otherwise.
  bool NameMatches(ConstString name) const {
    if (m_mangled == name)
      return true;
    return GetDemangledName() == name;
  }
  bool NameMatches(const RegularExpression &regex) const;

  /// Get the memory cost of this object.
  ///
  /// Return the size in bytes that this object takes in memory. This returns
  /// the size in bytes of this object, not any shared string values it may
  /// refer to.
  ///
  /// \return
  ///     The number of bytes that this object occupies in memory.
  size_t MemorySize() const;

  /// Set the string value in this object.
  ///
  /// This version auto detects if the string is mangled by inspecting the
  /// string value and looking for common mangling prefixes.
  ///
  /// \param[in] name
  ///     The already const version of the name for this object.
  void SetValue(ConstString name);

  /// Try to guess the language from the mangling.
  ///
  /// For a mangled name to have a language it must have both a mangled and a
  /// demangled name and it can be guessed from the mangling what the language
  /// is.  Note: this will return C++ for any language that uses Itanium ABI
  /// mangling.
  ///
  /// Standard C function names will return eLanguageTypeUnknown because they
  /// aren't mangled and it isn't clear what language the name represents
  /// (there will be no mangled name).
  ///
  /// \return
  ///     The language for the mangled/demangled name, eLanguageTypeUnknown
  ///     if there is no mangled or demangled counterpart.
  lldb::LanguageType GuessLanguage() const;

  /// Function signature for filtering mangled names.
  using SkipMangledNameFn = bool(llvm::StringRef, ManglingScheme);

  /// Get rich mangling information. This is optimized for batch processing
  /// while populating a name index. To get the pure demangled name string for
  /// a single entity, use GetDemangledName() instead.
  ///
  /// For names that match the Itanium mangling scheme, this uses LLVM's
  /// ItaniumPartialDemangler. All other names fall back to LLDB's builtin
  /// parser currently.
  ///
  /// This function is thread-safe when used with different \a context
  /// instances in different threads.
  ///
  /// \param[in] context
  ///     The context for this function. A single instance can be stack-
  ///     allocated in the caller's frame and used for multiple calls.
  ///
  /// \param[in] skip_mangled_name
  ///     A filtering function for skipping entities based on name and mangling
  ///     scheme. This can be null if unused.
  ///
  /// \return
  ///     True on success, false otherwise.
  bool GetRichManglingInfo(RichManglingContext &context,
                           SkipMangledNameFn *skip_mangled_name);

  /// Try to identify the mangling scheme used.
  /// \param[in] name
  ///     The name we are attempting to identify the mangling scheme for.
  ///
  /// \return
  ///     eManglingSchemeNone if no known mangling scheme could be identified
  ///     for s, otherwise the enumerator for the mangling scheme detected.
  static Mangled::ManglingScheme GetManglingScheme(llvm::StringRef const name);

  /// Decode a serialized version of this object from data.
  ///
  /// \param data
  ///   The decoder object that references the serialized data.
  ///
  /// \param offset_ptr
  ///   A pointer that contains the offset from which the data will be decoded
  ///   from that gets updated as data gets decoded.
  ///
  /// \param strtab
  ///   All strings in cache files are put into string tables for efficiency
  ///   and cache file size reduction. Strings are stored as uint32_t string
  ///   table offsets in the cache data.
  bool Decode(const DataExtractor &data, lldb::offset_t *offset_ptr,
              const StringTableReader &strtab);

  /// Encode this object into a data encoder object.
  ///
  /// This allows this object to be serialized to disk.
  ///
  /// \param encoder
  ///   A data encoder object that serialized bytes will be encoded into.
  ///
  /// \param strtab
  ///   All strings in cache files are put into string tables for efficiency
  ///   and cache file size reduction. Strings are stored as uint32_t string
  ///   table offsets in the cache data.
  void Encode(DataEncoder &encoder, ConstStringTable &strtab) const;

private:
  /// Mangled member variables.
  ConstString m_mangled;           ///< The mangled version of the name
  mutable ConstString m_demangled; ///< Mutable so we can get it on demand with
                                   ///a const version of this object
};

Stream &operator<<(Stream &s, const Mangled &obj);

} // namespace lldb_private

#endif // LLDB_CORE_MANGLED_H
