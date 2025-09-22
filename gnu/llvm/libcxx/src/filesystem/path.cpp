//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <__config>
#include <filesystem>
#include <vector>

#include "error.h"
#include "path_parser.h"

_LIBCPP_BEGIN_NAMESPACE_FILESYSTEM

using detail::ErrorHandler;
using parser::createView;
using parser::PathParser;
using parser::string_view_t;

///////////////////////////////////////////////////////////////////////////////
//                            path definitions
///////////////////////////////////////////////////////////////////////////////

constexpr path::value_type path::preferred_separator;

path& path::replace_extension(path const& replacement) {
  path p = extension();
  if (not p.empty()) {
    __pn_.erase(__pn_.size() - p.native().size());
  }
  if (!replacement.empty()) {
    if (replacement.native()[0] != '.') {
      __pn_ += PATHSTR(".");
    }
    __pn_.append(replacement.__pn_);
  }
  return *this;
}

///////////////////////////////////////////////////////////////////////////////
// path.decompose

string_view_t path::__root_name() const {
  auto PP = PathParser::CreateBegin(__pn_);
  if (PP.State_ == PathParser::PS_InRootName)
    return *PP;
  return {};
}

string_view_t path::__root_directory() const {
  auto PP = PathParser::CreateBegin(__pn_);
  if (PP.State_ == PathParser::PS_InRootName)
    ++PP;
  if (PP.State_ == PathParser::PS_InRootDir)
    return *PP;
  return {};
}

string_view_t path::__root_path_raw() const {
  auto PP = PathParser::CreateBegin(__pn_);
  if (PP.State_ == PathParser::PS_InRootName) {
    auto NextCh = PP.peek();
    if (NextCh && isSeparator(*NextCh)) {
      ++PP;
      return createView(__pn_.data(), &PP.RawEntry.back());
    }
    return PP.RawEntry;
  }
  if (PP.State_ == PathParser::PS_InRootDir)
    return *PP;
  return {};
}

static bool ConsumeRootName(PathParser* PP) {
  static_assert(PathParser::PS_BeforeBegin == 1 && PathParser::PS_InRootName == 2, "Values for enums are incorrect");
  while (PP->State_ <= PathParser::PS_InRootName)
    ++(*PP);
  return PP->State_ == PathParser::PS_AtEnd;
}

static bool ConsumeRootDir(PathParser* PP) {
  static_assert(PathParser::PS_BeforeBegin == 1 && PathParser::PS_InRootName == 2 && PathParser::PS_InRootDir == 3,
                "Values for enums are incorrect");
  while (PP->State_ <= PathParser::PS_InRootDir)
    ++(*PP);
  return PP->State_ == PathParser::PS_AtEnd;
}

string_view_t path::__relative_path() const {
  auto PP = PathParser::CreateBegin(__pn_);
  if (ConsumeRootDir(&PP))
    return {};
  return createView(PP.RawEntry.data(), &__pn_.back());
}

string_view_t path::__parent_path() const {
  if (empty())
    return {};
  // Determine if we have a root path but not a relative path. In that case
  // return *this.
  {
    auto PP = PathParser::CreateBegin(__pn_);
    if (ConsumeRootDir(&PP))
      return __pn_;
  }
  // Otherwise remove a single element from the end of the path, and return
  // a string representing that path
  {
    auto PP = PathParser::CreateEnd(__pn_);
    --PP;
    if (PP.RawEntry.data() == __pn_.data())
      return {};
    --PP;
    return createView(__pn_.data(), &PP.RawEntry.back());
  }
}

string_view_t path::__filename() const {
  if (empty())
    return {};
  {
    PathParser PP = PathParser::CreateBegin(__pn_);
    if (ConsumeRootDir(&PP))
      return {};
  }
  return *(--PathParser::CreateEnd(__pn_));
}

string_view_t path::__stem() const { return parser::separate_filename(__filename()).first; }

string_view_t path::__extension() const { return parser::separate_filename(__filename()).second; }

////////////////////////////////////////////////////////////////////////////
// path.gen

enum PathPartKind : unsigned char { PK_None, PK_RootSep, PK_Filename, PK_Dot, PK_DotDot, PK_TrailingSep };

static PathPartKind ClassifyPathPart(string_view_t Part) {
  if (Part.empty())
    return PK_TrailingSep;
  if (Part == PATHSTR("."))
    return PK_Dot;
  if (Part == PATHSTR(".."))
    return PK_DotDot;
  if (Part == PATHSTR("/"))
    return PK_RootSep;
#if defined(_LIBCPP_WIN32API)
  if (Part == PATHSTR("\\"))
    return PK_RootSep;
#endif
  return PK_Filename;
}

path path::lexically_normal() const {
  if (__pn_.empty())
    return *this;

  using PartKindPair = pair<string_view_t, PathPartKind>;
  vector<PartKindPair> Parts;
  // Guess as to how many elements the path has to avoid reallocating.
  Parts.reserve(32);

  // Track the total size of the parts as we collect them. This allows the
  // resulting path to reserve the correct amount of memory.
  size_t NewPathSize = 0;
  auto AddPart       = [&](PathPartKind K, string_view_t P) {
    NewPathSize += P.size();
    Parts.emplace_back(P, K);
  };
  auto LastPartKind = [&]() {
    if (Parts.empty())
      return PK_None;
    return Parts.back().second;
  };

  bool MaybeNeedTrailingSep = false;
  // Build a stack containing the remaining elements of the path, popping off
  // elements which occur before a '..' entry.
  for (auto PP = PathParser::CreateBegin(__pn_); PP; ++PP) {
    auto Part         = *PP;
    PathPartKind Kind = ClassifyPathPart(Part);
    switch (Kind) {
    case PK_Filename:
    case PK_RootSep: {
      // Add all non-dot and non-dot-dot elements to the stack of elements.
      AddPart(Kind, Part);
      MaybeNeedTrailingSep = false;
      break;
    }
    case PK_DotDot: {
      // Only push a ".." element if there are no elements preceding the "..",
      // or if the preceding element is itself "..".
      auto LastKind = LastPartKind();
      if (LastKind == PK_Filename) {
        NewPathSize -= Parts.back().first.size();
        Parts.pop_back();
      } else if (LastKind != PK_RootSep)
        AddPart(PK_DotDot, PATHSTR(".."));
      MaybeNeedTrailingSep = LastKind == PK_Filename;
      break;
    }
    case PK_Dot:
    case PK_TrailingSep: {
      MaybeNeedTrailingSep = true;
      break;
    }
    case PK_None:
      __libcpp_unreachable();
    }
  }
  // [fs.path.generic]p6.8: If the path is empty, add a dot.
  if (Parts.empty())
    return PATHSTR(".");

  // [fs.path.generic]p6.7: If the last filename is dot-dot, remove any
  // trailing directory-separator.
  bool NeedTrailingSep = MaybeNeedTrailingSep && LastPartKind() == PK_Filename;

  path Result;
  Result.__pn_.reserve(Parts.size() + NewPathSize + NeedTrailingSep);
  for (auto& PK : Parts)
    Result /= PK.first;

  if (NeedTrailingSep)
    Result /= PATHSTR("");

  Result.make_preferred();
  return Result;
}

static int DetermineLexicalElementCount(PathParser PP) {
  int Count = 0;
  for (; PP; ++PP) {
    auto Elem = *PP;
    if (Elem == PATHSTR(".."))
      --Count;
    else if (Elem != PATHSTR(".") && Elem != PATHSTR(""))
      ++Count;
  }
  return Count;
}

path path::lexically_relative(const path& base) const {
  { // perform root-name/root-directory mismatch checks
    auto PP                      = PathParser::CreateBegin(__pn_);
    auto PPBase                  = PathParser::CreateBegin(base.__pn_);
    auto CheckIterMismatchAtBase = [&]() {
      return PP.State_ != PPBase.State_ && (PP.inRootPath() || PPBase.inRootPath());
    };
    if (PP.inRootName() && PPBase.inRootName()) {
      if (*PP != *PPBase)
        return {};
    } else if (CheckIterMismatchAtBase())
      return {};

    if (PP.inRootPath())
      ++PP;
    if (PPBase.inRootPath())
      ++PPBase;
    if (CheckIterMismatchAtBase())
      return {};
  }

  // Find the first mismatching element
  auto PP     = PathParser::CreateBegin(__pn_);
  auto PPBase = PathParser::CreateBegin(base.__pn_);
  while (PP && PPBase && PP.State_ == PPBase.State_ && *PP == *PPBase) {
    ++PP;
    ++PPBase;
  }

  // If there is no mismatch, return ".".
  if (!PP && !PPBase)
    return ".";

  // Otherwise, determine the number of elements, 'n', which are not dot or
  // dot-dot minus the number of dot-dot elements.
  int ElemCount = DetermineLexicalElementCount(PPBase);
  if (ElemCount < 0)
    return {};

  // if n == 0 and (a == end() || a->empty()), returns path("."); otherwise
  if (ElemCount == 0 && (PP.atEnd() || *PP == PATHSTR("")))
    return PATHSTR(".");

  // return a path constructed with 'n' dot-dot elements, followed by the
  // elements of '*this' after the mismatch.
  path Result;
  // FIXME: Reserve enough room in Result that it won't have to re-allocate.
  while (ElemCount--)
    Result /= PATHSTR("..");
  for (; PP; ++PP)
    Result /= *PP;
  return Result;
}

////////////////////////////////////////////////////////////////////////////
// path.comparisons
static int CompareRootName(PathParser* LHS, PathParser* RHS) {
  if (!LHS->inRootName() && !RHS->inRootName())
    return 0;

  auto GetRootName = [](PathParser* Parser) -> string_view_t { return Parser->inRootName() ? **Parser : PATHSTR(""); };
  int res          = GetRootName(LHS).compare(GetRootName(RHS));
  ConsumeRootName(LHS);
  ConsumeRootName(RHS);
  return res;
}

static int CompareRootDir(PathParser* LHS, PathParser* RHS) {
  if (!LHS->inRootDir() && RHS->inRootDir())
    return -1;
  else if (LHS->inRootDir() && !RHS->inRootDir())
    return 1;
  else {
    ConsumeRootDir(LHS);
    ConsumeRootDir(RHS);
    return 0;
  }
}

static int CompareRelative(PathParser* LHSPtr, PathParser* RHSPtr) {
  auto& LHS = *LHSPtr;
  auto& RHS = *RHSPtr;

  int res;
  while (LHS && RHS) {
    if ((res = (*LHS).compare(*RHS)) != 0)
      return res;
    ++LHS;
    ++RHS;
  }
  return 0;
}

static int CompareEndState(PathParser* LHS, PathParser* RHS) {
  if (LHS->atEnd() && !RHS->atEnd())
    return -1;
  else if (!LHS->atEnd() && RHS->atEnd())
    return 1;
  return 0;
}

int path::__compare(string_view_t __s) const {
  auto LHS = PathParser::CreateBegin(__pn_);
  auto RHS = PathParser::CreateBegin(__s);
  int res;

  if ((res = CompareRootName(&LHS, &RHS)) != 0)
    return res;

  if ((res = CompareRootDir(&LHS, &RHS)) != 0)
    return res;

  if ((res = CompareRelative(&LHS, &RHS)) != 0)
    return res;

  return CompareEndState(&LHS, &RHS);
}

////////////////////////////////////////////////////////////////////////////
// path.nonmembers
size_t hash_value(const path& __p) noexcept {
  auto PP           = PathParser::CreateBegin(__p.native());
  size_t hash_value = 0;
  hash<string_view_t> hasher;
  while (PP) {
    hash_value = __hash_combine(hash_value, hasher(*PP));
    ++PP;
  }
  return hash_value;
}

////////////////////////////////////////////////////////////////////////////
// path.itr
path::iterator path::begin() const {
  auto PP = PathParser::CreateBegin(__pn_);
  iterator it;
  it.__path_ptr_ = this;
  it.__state_    = static_cast<path::iterator::_ParserState>(PP.State_);
  it.__entry_    = PP.RawEntry;
  it.__stashed_elem_.__assign_view(*PP);
  return it;
}

path::iterator path::end() const {
  iterator it{};
  it.__state_    = path::iterator::_AtEnd;
  it.__path_ptr_ = this;
  return it;
}

path::iterator& path::iterator::__increment() {
  PathParser PP(__path_ptr_->native(), __entry_, __state_);
  ++PP;
  __state_ = static_cast<_ParserState>(PP.State_);
  __entry_ = PP.RawEntry;
  __stashed_elem_.__assign_view(*PP);
  return *this;
}

path::iterator& path::iterator::__decrement() {
  PathParser PP(__path_ptr_->native(), __entry_, __state_);
  --PP;
  __state_ = static_cast<_ParserState>(PP.State_);
  __entry_ = PP.RawEntry;
  __stashed_elem_.__assign_view(*PP);
  return *this;
}

#if defined(_LIBCPP_WIN32API)
////////////////////////////////////////////////////////////////////////////
// Windows path conversions
size_t __wide_to_char(const wstring& str, char* out, size_t outlen) {
  if (str.empty())
    return 0;
  ErrorHandler<size_t> err("__wide_to_char", nullptr);
  UINT codepage     = AreFileApisANSI() ? CP_ACP : CP_OEMCP;
  BOOL used_default = FALSE;
  int ret           = WideCharToMultiByte(codepage, 0, str.data(), str.size(), out, outlen, nullptr, &used_default);
  if (ret <= 0 || used_default)
    return err.report(errc::illegal_byte_sequence);
  return ret;
}

size_t __char_to_wide(const string& str, wchar_t* out, size_t outlen) {
  if (str.empty())
    return 0;
  ErrorHandler<size_t> err("__char_to_wide", nullptr);
  UINT codepage = AreFileApisANSI() ? CP_ACP : CP_OEMCP;
  int ret       = MultiByteToWideChar(codepage, MB_ERR_INVALID_CHARS, str.data(), str.size(), out, outlen);
  if (ret <= 0)
    return err.report(errc::illegal_byte_sequence);
  return ret;
}
#endif

_LIBCPP_END_NAMESPACE_FILESYSTEM
