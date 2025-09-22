//===-- CFCString.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CFCString.h"
#include <glob.h>
#include <string>

// CFCString constructor
CFCString::CFCString(CFStringRef s) : CFCReleaser<CFStringRef>(s) {}

// CFCString copy constructor
CFCString::CFCString(const CFCString &rhs) = default;

// CFCString copy constructor
CFCString &CFCString::operator=(const CFCString &rhs) {
  if (this != &rhs)
    *this = rhs;
  return *this;
}

CFCString::CFCString(const char *cstr, CFStringEncoding cstr_encoding)
    : CFCReleaser<CFStringRef>() {
  if (cstr && cstr[0]) {
    reset(
        ::CFStringCreateWithCString(kCFAllocatorDefault, cstr, cstr_encoding));
  }
}

// Destructor
CFCString::~CFCString() = default;

const char *CFCString::GetFileSystemRepresentation(std::string &s) {
  return CFCString::FileSystemRepresentation(get(), s);
}

CFStringRef CFCString::SetFileSystemRepresentation(const char *path) {
  CFStringRef new_value = NULL;
  if (path && path[0])
    new_value =
        ::CFStringCreateWithFileSystemRepresentation(kCFAllocatorDefault, path);
  reset(new_value);
  return get();
}

CFStringRef
CFCString::SetFileSystemRepresentationFromCFType(CFTypeRef cf_type) {
  CFStringRef new_value = NULL;
  if (cf_type != NULL) {
    CFTypeID cf_type_id = ::CFGetTypeID(cf_type);

    if (cf_type_id == ::CFStringGetTypeID()) {
      // Retain since we are using the existing object
      new_value = (CFStringRef)::CFRetain(cf_type);
    } else if (cf_type_id == ::CFURLGetTypeID()) {
      new_value =
          ::CFURLCopyFileSystemPath((CFURLRef)cf_type, kCFURLPOSIXPathStyle);
    }
  }
  reset(new_value);
  return get();
}

CFStringRef
CFCString::SetFileSystemRepresentationAndExpandTilde(const char *path) {
  std::string expanded_path;
  if (CFCString::ExpandTildeInPath(path, expanded_path))
    SetFileSystemRepresentation(expanded_path.c_str());
  else
    reset();
  return get();
}

const char *CFCString::UTF8(std::string &str) {
  return CFCString::UTF8(get(), str);
}

// Static function that puts a copy of the UTF8 contents of CF_STR into STR and
// returns the C string pointer that is contained in STR when successful, else
// NULL is returned. This allows the std::string parameter to own the extracted
// string,
// and also allows that string to be returned as a C string pointer that can be
// used.

const char *CFCString::UTF8(CFStringRef cf_str, std::string &str) {
  if (cf_str) {
    const CFStringEncoding encoding = kCFStringEncodingUTF8;
    CFIndex max_utf8_str_len = CFStringGetLength(cf_str);
    max_utf8_str_len =
        CFStringGetMaximumSizeForEncoding(max_utf8_str_len, encoding);
    if (max_utf8_str_len > 0) {
      str.resize(max_utf8_str_len);
      if (!str.empty()) {
        if (CFStringGetCString(cf_str, &str[0], str.size(), encoding)) {
          str.resize(strlen(str.c_str()));
          return str.c_str();
        }
      }
    }
  }
  return NULL;
}

const char *CFCString::ExpandTildeInPath(const char *path,
                                         std::string &expanded_path) {
  glob_t globbuf;
  if (::glob(path, GLOB_TILDE, NULL, &globbuf) == 0) {
    expanded_path = globbuf.gl_pathv[0];
    ::globfree(&globbuf);
  } else
    expanded_path.clear();

  return expanded_path.c_str();
}

// Static function that puts a copy of the file system representation of CF_STR
// into STR and returns the C string pointer that is contained in STR when
// successful, else NULL is returned. This allows the std::string parameter to
// own the extracted string, and also allows that string to be returned as a C
// string pointer that can be used.

const char *CFCString::FileSystemRepresentation(CFStringRef cf_str,
                                                std::string &str) {
  if (cf_str) {
    CFIndex max_length =
        ::CFStringGetMaximumSizeOfFileSystemRepresentation(cf_str);
    if (max_length > 0) {
      str.resize(max_length);
      if (!str.empty()) {
        if (::CFStringGetFileSystemRepresentation(cf_str, &str[0],
                                                  str.size())) {
          str.erase(::strlen(str.c_str()));
          return str.c_str();
        }
      }
    }
  }
  str.erase();
  return NULL;
}

CFIndex CFCString::GetLength() const {
  CFStringRef str = get();
  if (str)
    return CFStringGetLength(str);
  return 0;
}
