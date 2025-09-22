//===-- Mangled.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/Mangled.h"

#include "lldb/Core/DataFileCache.h"
#include "lldb/Core/RichManglingContext.h"
#include "lldb/Target/Language.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/DataEncoder.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegularExpression.h"
#include "lldb/Utility/Stream.h"
#include "lldb/lldb-enumerations.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/Support/Compiler.h"

#include <mutex>
#include <string>
#include <string_view>
#include <utility>

#include <cstdlib>
#include <cstring>
using namespace lldb_private;

static inline bool cstring_is_mangled(llvm::StringRef s) {
  return Mangled::GetManglingScheme(s) != Mangled::eManglingSchemeNone;
}

#pragma mark Mangled

Mangled::ManglingScheme Mangled::GetManglingScheme(llvm::StringRef const name) {
  if (name.empty())
    return Mangled::eManglingSchemeNone;

  if (name.starts_with("?"))
    return Mangled::eManglingSchemeMSVC;

  if (name.starts_with("_R"))
    return Mangled::eManglingSchemeRustV0;

  if (name.starts_with("_D")) {
    // A dlang mangled name begins with `_D`, followed by a numeric length. One
    // known exception is the symbol `_Dmain`.
    // See `SymbolName` and `LName` in
    // https://dlang.org/spec/abi.html#name_mangling
    llvm::StringRef buf = name.drop_front(2);
    if (!buf.empty() && (llvm::isDigit(buf.front()) || name == "_Dmain"))
      return Mangled::eManglingSchemeD;
  }

  if (name.starts_with("_Z"))
    return Mangled::eManglingSchemeItanium;

  // ___Z is a clang extension of block invocations
  if (name.starts_with("___Z"))
    return Mangled::eManglingSchemeItanium;

  // Swift's older style of mangling used "_T" as a mangling prefix. This can
  // lead to false positives with other symbols that just so happen to start
  // with "_T". To minimize the chance of that happening, we only return true
  // for select old-style swift mangled names. The known cases are ObjC classes
  // and protocols. Classes are either prefixed with "_TtC" or "_TtGC".
  // Protocols are prefixed with "_TtP".
  if (name.starts_with("_TtC") || name.starts_with("_TtGC") ||
      name.starts_with("_TtP"))
    return Mangled::eManglingSchemeSwift;

  // Swift 4.2 used "$S" and "_$S".
  // Swift 5 and onward uses "$s" and "_$s".
  // Swift also uses "@__swiftmacro_" as a prefix for mangling filenames.
  if (name.starts_with("$S") || name.starts_with("_$S") ||
      name.starts_with("$s") || name.starts_with("_$s") ||
      name.starts_with("@__swiftmacro_"))
    return Mangled::eManglingSchemeSwift;

  return Mangled::eManglingSchemeNone;
}

Mangled::Mangled(ConstString s) : m_mangled(), m_demangled() {
  if (s)
    SetValue(s);
}

Mangled::Mangled(llvm::StringRef name) {
  if (!name.empty())
    SetValue(ConstString(name));
}

// Convert to bool operator. This allows code to check any Mangled objects
// to see if they contain anything valid using code such as:
//
//  Mangled mangled(...);
//  if (mangled)
//  { ...
Mangled::operator bool() const { return m_mangled || m_demangled; }

// Clear the mangled and demangled values.
void Mangled::Clear() {
  m_mangled.Clear();
  m_demangled.Clear();
}

// Compare the string values.
int Mangled::Compare(const Mangled &a, const Mangled &b) {
  return ConstString::Compare(a.GetName(ePreferMangled),
                              b.GetName(ePreferMangled));
}

void Mangled::SetValue(ConstString name) {
  if (name) {
    if (cstring_is_mangled(name.GetStringRef())) {
      m_demangled.Clear();
      m_mangled = name;
    } else {
      m_demangled = name;
      m_mangled.Clear();
    }
  } else {
    m_demangled.Clear();
    m_mangled.Clear();
  }
}

// Local helpers for different demangling implementations.
static char *GetMSVCDemangledStr(llvm::StringRef M) {
  char *demangled_cstr = llvm::microsoftDemangle(
      M, nullptr, nullptr,
      llvm::MSDemangleFlags(
          llvm::MSDF_NoAccessSpecifier | llvm::MSDF_NoCallingConvention |
          llvm::MSDF_NoMemberType | llvm::MSDF_NoVariableType));

  if (Log *log = GetLog(LLDBLog::Demangle)) {
    if (demangled_cstr && demangled_cstr[0])
      LLDB_LOGF(log, "demangled msvc: %s -> \"%s\"", M.data(), demangled_cstr);
    else
      LLDB_LOGF(log, "demangled msvc: %s -> error", M.data());
  }

  return demangled_cstr;
}

static char *GetItaniumDemangledStr(const char *M) {
  char *demangled_cstr = nullptr;

  llvm::ItaniumPartialDemangler ipd;
  bool err = ipd.partialDemangle(M);
  if (!err) {
    // Default buffer and size (will realloc in case it's too small).
    size_t demangled_size = 80;
    demangled_cstr = static_cast<char *>(std::malloc(demangled_size));
    demangled_cstr = ipd.finishDemangle(demangled_cstr, &demangled_size);

    assert(demangled_cstr &&
           "finishDemangle must always succeed if partialDemangle did");
    assert(demangled_cstr[demangled_size - 1] == '\0' &&
           "Expected demangled_size to return length including trailing null");
  }

  if (Log *log = GetLog(LLDBLog::Demangle)) {
    if (demangled_cstr)
      LLDB_LOGF(log, "demangled itanium: %s -> \"%s\"", M, demangled_cstr);
    else
      LLDB_LOGF(log, "demangled itanium: %s -> error: failed to demangle", M);
  }

  return demangled_cstr;
}

static char *GetRustV0DemangledStr(llvm::StringRef M) {
  char *demangled_cstr = llvm::rustDemangle(M);

  if (Log *log = GetLog(LLDBLog::Demangle)) {
    if (demangled_cstr && demangled_cstr[0])
      LLDB_LOG(log, "demangled rustv0: {0} -> \"{1}\"", M, demangled_cstr);
    else
      LLDB_LOG(log, "demangled rustv0: {0} -> error: failed to demangle",
               static_cast<std::string_view>(M));
  }

  return demangled_cstr;
}

static char *GetDLangDemangledStr(llvm::StringRef M) {
  char *demangled_cstr = llvm::dlangDemangle(M);

  if (Log *log = GetLog(LLDBLog::Demangle)) {
    if (demangled_cstr && demangled_cstr[0])
      LLDB_LOG(log, "demangled dlang: {0} -> \"{1}\"", M, demangled_cstr);
    else
      LLDB_LOG(log, "demangled dlang: {0} -> error: failed to demangle",
               static_cast<std::string_view>(M));
  }

  return demangled_cstr;
}

// Explicit demangling for scheduled requests during batch processing. This
// makes use of ItaniumPartialDemangler's rich demangle info
bool Mangled::GetRichManglingInfo(RichManglingContext &context,
                                  SkipMangledNameFn *skip_mangled_name) {
  // Others are not meant to arrive here. ObjC names or C's main() for example
  // have their names stored in m_demangled, while m_mangled is empty.
  assert(m_mangled);

  // Check whether or not we are interested in this name at all.
  ManglingScheme scheme = GetManglingScheme(m_mangled.GetStringRef());
  if (skip_mangled_name && skip_mangled_name(m_mangled.GetStringRef(), scheme))
    return false;

  switch (scheme) {
  case eManglingSchemeNone:
    // The current mangled_name_filter would allow llvm_unreachable here.
    return false;

  case eManglingSchemeItanium:
    // We want the rich mangling info here, so we don't care whether or not
    // there is a demangled string in the pool already.
    return context.FromItaniumName(m_mangled);

  case eManglingSchemeMSVC: {
    // We have no rich mangling for MSVC-mangled names yet, so first try to
    // demangle it if necessary.
    if (!m_demangled && !m_mangled.GetMangledCounterpart(m_demangled)) {
      if (char *d = GetMSVCDemangledStr(m_mangled)) {
        // Without the rich mangling info we have to demangle the full name.
        // Copy it to string pool and connect the counterparts to accelerate
        // later access in GetDemangledName().
        m_demangled.SetStringWithMangledCounterpart(llvm::StringRef(d),
                                                    m_mangled);
        ::free(d);
      } else {
        m_demangled.SetCString("");
      }
    }

    if (m_demangled.IsEmpty()) {
      // Cannot demangle it, so don't try parsing.
      return false;
    } else {
      // Demangled successfully, we can try and parse it with
      // CPlusPlusLanguage::MethodName.
      return context.FromCxxMethodName(m_demangled);
    }
  }

  case eManglingSchemeRustV0:
  case eManglingSchemeD:
  case eManglingSchemeSwift:
    // Rich demangling scheme is not supported
    return false;
  }
  llvm_unreachable("Fully covered switch above!");
}

// Generate the demangled name on demand using this accessor. Code in this
// class will need to use this accessor if it wishes to decode the demangled
// name. The result is cached and will be kept until a new string value is
// supplied to this object, or until the end of the object's lifetime.
ConstString Mangled::GetDemangledName() const {
  // Check to make sure we have a valid mangled name and that we haven't
  // already decoded our mangled name.
  if (m_mangled && m_demangled.IsNull()) {
    // Don't bother running anything that isn't mangled
    const char *mangled_name = m_mangled.GetCString();
    ManglingScheme mangling_scheme =
        GetManglingScheme(m_mangled.GetStringRef());
    if (mangling_scheme != eManglingSchemeNone &&
        !m_mangled.GetMangledCounterpart(m_demangled)) {
      // We didn't already mangle this name, demangle it and if all goes well
      // add it to our map.
      char *demangled_name = nullptr;
      switch (mangling_scheme) {
      case eManglingSchemeMSVC:
        demangled_name = GetMSVCDemangledStr(mangled_name);
        break;
      case eManglingSchemeItanium: {
        demangled_name = GetItaniumDemangledStr(mangled_name);
        break;
      }
      case eManglingSchemeRustV0:
        demangled_name = GetRustV0DemangledStr(m_mangled);
        break;
      case eManglingSchemeD:
        demangled_name = GetDLangDemangledStr(m_mangled);
        break;
      case eManglingSchemeSwift:
        // Demangling a swift name requires the swift compiler. This is
        // explicitly unsupported on llvm.org.
        break;
      case eManglingSchemeNone:
        llvm_unreachable("eManglingSchemeNone was handled already");
      }
      if (demangled_name) {
        m_demangled.SetStringWithMangledCounterpart(
            llvm::StringRef(demangled_name), m_mangled);
        free(demangled_name);
      }
    }
    if (m_demangled.IsNull()) {
      // Set the demangled string to the empty string to indicate we tried to
      // parse it once and failed.
      m_demangled.SetCString("");
    }
  }

  return m_demangled;
}

ConstString Mangled::GetDisplayDemangledName() const {
  if (Language *lang = Language::FindPlugin(GuessLanguage()))
    return lang->GetDisplayDemangledName(*this);
  return GetDemangledName();
}

bool Mangled::NameMatches(const RegularExpression &regex) const {
  if (m_mangled && regex.Execute(m_mangled.GetStringRef()))
    return true;

  ConstString demangled = GetDemangledName();
  return demangled && regex.Execute(demangled.GetStringRef());
}

// Get the demangled name if there is one, else return the mangled name.
ConstString Mangled::GetName(Mangled::NamePreference preference) const {
  if (preference == ePreferMangled && m_mangled)
    return m_mangled;

  // Call the accessor to make sure we get a demangled name in case it hasn't
  // been demangled yet...
  ConstString demangled = GetDemangledName();

  if (preference == ePreferDemangledWithoutArguments) {
    if (Language *lang = Language::FindPlugin(GuessLanguage())) {
      return lang->GetDemangledFunctionNameWithoutArguments(*this);
    }
  }
  if (preference == ePreferDemangled) {
    if (demangled)
      return demangled;
    return m_mangled;
  }
  return demangled;
}

// Dump a Mangled object to stream "s". We don't force our demangled name to be
// computed currently (we don't use the accessor).
void Mangled::Dump(Stream *s) const {
  if (m_mangled) {
    *s << ", mangled = " << m_mangled;
  }
  if (m_demangled) {
    const char *demangled = m_demangled.AsCString();
    s->Printf(", demangled = %s", demangled[0] ? demangled : "<error>");
  }
}

// Dumps a debug version of this string with extra object and state information
// to stream "s".
void Mangled::DumpDebug(Stream *s) const {
  s->Printf("%*p: Mangled mangled = ", static_cast<int>(sizeof(void *) * 2),
            static_cast<const void *>(this));
  m_mangled.DumpDebug(s);
  s->Printf(", demangled = ");
  m_demangled.DumpDebug(s);
}

// Return the size in byte that this object takes in memory. The size includes
// the size of the objects it owns, and not the strings that it references
// because they are shared strings.
size_t Mangled::MemorySize() const {
  return m_mangled.MemorySize() + m_demangled.MemorySize();
}

// We "guess" the language because we can't determine a symbol's language from
// it's name.  For example, a Pascal symbol can be mangled using the C++
// Itanium scheme, and defined in a compilation unit within the same module as
// other C++ units.  In addition, different targets could have different ways
// of mangling names from a given language, likewise the compilation units
// within those targets.
lldb::LanguageType Mangled::GuessLanguage() const {
  lldb::LanguageType result = lldb::eLanguageTypeUnknown;
  // Ask each language plugin to check if the mangled name belongs to it.
  Language::ForEach([this, &result](Language *l) {
    if (l->SymbolNameFitsToLanguage(*this)) {
      result = l->GetLanguageType();
      return false;
    }
    return true;
  });
  return result;
}

// Dump OBJ to the supplied stream S.
Stream &operator<<(Stream &s, const Mangled &obj) {
  if (obj.GetMangledName())
    s << "mangled = '" << obj.GetMangledName() << "'";

  ConstString demangled = obj.GetDemangledName();
  if (demangled)
    s << ", demangled = '" << demangled << '\'';
  else
    s << ", demangled = <error>";
  return s;
}

// When encoding Mangled objects we can get away with encoding as little
// information as is required. The enumeration below helps us to efficiently
// encode Mangled objects.
enum MangledEncoding {
  /// If the Mangled object has neither a mangled name or demangled name we can
  /// encode the object with one zero byte using the Empty enumeration.
  Empty = 0u,
  /// If the Mangled object has only a demangled name and no mangled named, we
  /// can encode only the demangled name.
  DemangledOnly = 1u,
  /// If the mangle name can calculate the demangled name (it is the
  /// mangled/demangled counterpart), then we only need to encode the mangled
  /// name as the demangled name can be recomputed.
  MangledOnly = 2u,
  /// If we have a Mangled object with two different names that are not related
  /// then we need to save both strings. This can happen if we have a name that
  /// isn't a true mangled name, but we want to be able to lookup a symbol by
  /// name and type in the symbol table. We do this for Objective C symbols like
  /// "OBJC_CLASS_$_NSValue" where the mangled named will be set to
  /// "OBJC_CLASS_$_NSValue" and the demangled name will be manually set to
  /// "NSValue". If we tried to demangled the name "OBJC_CLASS_$_NSValue" it
  /// would fail, but in these cases we want these unrelated names to be
  /// preserved.
  MangledAndDemangled = 3u
};

bool Mangled::Decode(const DataExtractor &data, lldb::offset_t *offset_ptr,
                     const StringTableReader &strtab) {
  m_mangled.Clear();
  m_demangled.Clear();
  MangledEncoding encoding = (MangledEncoding)data.GetU8(offset_ptr);
  switch (encoding) {
    case Empty:
      return true;

    case DemangledOnly:
      m_demangled.SetString(strtab.Get(data.GetU32(offset_ptr)));
      return true;

    case MangledOnly:
      m_mangled.SetString(strtab.Get(data.GetU32(offset_ptr)));
      return true;

    case MangledAndDemangled:
      m_mangled.SetString(strtab.Get(data.GetU32(offset_ptr)));
      m_demangled.SetString(strtab.Get(data.GetU32(offset_ptr)));
      return true;
  }
  return false;
}
/// The encoding format for the Mangled object is as follows:
///
/// uint8_t encoding;
/// char str1[]; (only if DemangledOnly, MangledOnly)
/// char str2[]; (only if MangledAndDemangled)
///
/// The strings are stored as NULL terminated UTF8 strings and str1 and str2
/// are only saved if we need them based on the encoding.
///
/// Some mangled names have a mangled name that can be demangled by the built
/// in demanglers. These kinds of mangled objects know when the mangled and
/// demangled names are the counterparts for each other. This is done because
/// demangling is very expensive and avoiding demangling the same name twice
/// saves us a lot of compute time. For these kinds of names we only need to
/// save the mangled name and have the encoding set to "MangledOnly".
///
/// If a mangled obejct has only a demangled name, then we save only that string
/// and have the encoding set to "DemangledOnly".
///
/// Some mangled objects have both mangled and demangled names, but the
/// demangled name can not be computed from the mangled name. This is often used
/// for runtime named, like Objective C runtime V2 and V3 names. Both these
/// names must be saved and the encoding is set to "MangledAndDemangled".
///
/// For a Mangled object with no names, we only need to set the encoding to
/// "Empty" and not store any string values.
void Mangled::Encode(DataEncoder &file, ConstStringTable &strtab) const {
  MangledEncoding encoding = Empty;
  if (m_mangled) {
    encoding = MangledOnly;
    if (m_demangled) {
      // We have both mangled and demangled names. If the demangled name is the
      // counterpart of the mangled name, then we only need to save the mangled
      // named. If they are different, we need to save both.
      ConstString s;
      if (!(m_mangled.GetMangledCounterpart(s) && s == m_demangled))
        encoding = MangledAndDemangled;
    }
  } else if (m_demangled) {
    encoding = DemangledOnly;
  }
  file.AppendU8(encoding);
  switch (encoding) {
    case Empty:
      break;
    case DemangledOnly:
      file.AppendU32(strtab.Add(m_demangled));
      break;
    case MangledOnly:
      file.AppendU32(strtab.Add(m_mangled));
      break;
    case MangledAndDemangled:
      file.AppendU32(strtab.Add(m_mangled));
      file.AppendU32(strtab.Add(m_demangled));
      break;
  }
}
