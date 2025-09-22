//===-- Regex.cpp - Regular Expression matcher implementation -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a POSIX regular expression matcher.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/Regex.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "regex_impl.h"

#include <cassert>
#include <string>

using namespace llvm;

Regex::Regex() : preg(nullptr), error(REG_BADPAT) {}

Regex::Regex(StringRef regex, RegexFlags Flags) {
  unsigned flags = 0;
  preg = new llvm_regex();
  preg->re_endp = regex.end();
  if (Flags & IgnoreCase)
    flags |= REG_ICASE;
  if (Flags & Newline)
    flags |= REG_NEWLINE;
  if (!(Flags & BasicRegex))
    flags |= REG_EXTENDED;
  error = llvm_regcomp(preg, regex.data(), flags|REG_PEND);
}

Regex::Regex(StringRef regex, unsigned Flags)
    : Regex(regex, static_cast<RegexFlags>(Flags)) {}

Regex::Regex(Regex &&regex) {
  preg = regex.preg;
  error = regex.error;
  regex.preg = nullptr;
  regex.error = REG_BADPAT;
}

Regex::~Regex() {
  if (preg) {
    llvm_regfree(preg);
    delete preg;
  }
}

namespace {

/// Utility to convert a regex error code into a human-readable string.
void RegexErrorToString(int error, struct llvm_regex *preg,
                        std::string &Error) {
  size_t len = llvm_regerror(error, preg, nullptr, 0);

  Error.resize(len - 1);
  llvm_regerror(error, preg, &Error[0], len);
}

} // namespace

bool Regex::isValid(std::string &Error) const {
  if (!error)
    return true;

  RegexErrorToString(error, preg, Error);
  return false;
}

/// getNumMatches - In a valid regex, return the number of parenthesized
/// matches it contains.
unsigned Regex::getNumMatches() const {
  return preg->re_nsub;
}

bool Regex::match(StringRef String, SmallVectorImpl<StringRef> *Matches,
                  std::string *Error) const {
  // Reset error, if given.
  if (Error && !Error->empty())
    *Error = "";

  // Check if the regex itself didn't successfully compile.
  if (Error ? !isValid(*Error) : !isValid())
    return false;

  unsigned nmatch = Matches ? preg->re_nsub+1 : 0;

  // Update null string to empty string.
  if (String.data() == nullptr)
    String = "";

  // pmatch needs to have at least one element.
  SmallVector<llvm_regmatch_t, 8> pm;
  pm.resize(nmatch > 0 ? nmatch : 1);
  pm[0].rm_so = 0;
  pm[0].rm_eo = String.size();

  int rc = llvm_regexec(preg, String.data(), nmatch, pm.data(), REG_STARTEND);

  // Failure to match is not an error, it's just a normal return value.
  // Any other error code is considered abnormal, and is logged in the Error.
  if (rc == REG_NOMATCH)
    return false;
  if (rc != 0) {
    if (Error)
      RegexErrorToString(error, preg, *Error);
    return false;
  }

  // There was a match.

  if (Matches) { // match position requested
    Matches->clear();

    for (unsigned i = 0; i != nmatch; ++i) {
      if (pm[i].rm_so == -1) {
        // this group didn't match
        Matches->push_back(StringRef());
        continue;
      }
      assert(pm[i].rm_eo >= pm[i].rm_so);
      Matches->push_back(StringRef(String.data()+pm[i].rm_so,
                                   pm[i].rm_eo-pm[i].rm_so));
    }
  }

  return true;
}

std::string Regex::sub(StringRef Repl, StringRef String,
                       std::string *Error) const {
  SmallVector<StringRef, 8> Matches;

  // Return the input if there was no match.
  if (!match(String, &Matches, Error))
    return std::string(String);

  // Otherwise splice in the replacement string, starting with the prefix before
  // the match.
  std::string Res(String.begin(), Matches[0].begin());

  // Then the replacement string, honoring possible substitutions.
  while (!Repl.empty()) {
    // Skip to the next escape.
    std::pair<StringRef, StringRef> Split = Repl.split('\\');

    // Add the skipped substring.
    Res += Split.first;

    // Check for terminimation and trailing backslash.
    if (Split.second.empty()) {
      if (Repl.size() != Split.first.size() &&
          Error && Error->empty())
        *Error = "replacement string contained trailing backslash";
      break;
    }

    // Otherwise update the replacement string and interpret escapes.
    Repl = Split.second;

    // FIXME: We should have a StringExtras function for mapping C99 escapes.
    switch (Repl[0]) {

      // Backreference with the "\g<ref>" syntax
    case 'g':
      if (Repl.size() >= 4 && Repl[1] == '<') {
        size_t End = Repl.find('>');
        StringRef Ref = Repl.slice(2, End);
        unsigned RefValue;
        if (End != StringRef::npos && !Ref.getAsInteger(10, RefValue)) {
          Repl = Repl.substr(End + 1);
          if (RefValue < Matches.size())
            Res += Matches[RefValue];
          else if (Error && Error->empty())
            *Error =
                ("invalid backreference string 'g<" + Twine(Ref) + ">'").str();
          break;
        }
      }
      [[fallthrough]];

      // Treat all unrecognized characters as self-quoting.
    default:
      Res += Repl[0];
      Repl = Repl.substr(1);
      break;

      // Single character escapes.
    case 't':
      Res += '\t';
      Repl = Repl.substr(1);
      break;
    case 'n':
      Res += '\n';
      Repl = Repl.substr(1);
      break;

      // Decimal escapes are backreferences.
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9': {
      // Extract the backreference number.
      StringRef Ref = Repl.slice(0, Repl.find_first_not_of("0123456789"));
      Repl = Repl.substr(Ref.size());

      unsigned RefValue;
      if (!Ref.getAsInteger(10, RefValue) &&
          RefValue < Matches.size())
        Res += Matches[RefValue];
      else if (Error && Error->empty())
        *Error = ("invalid backreference string '" + Twine(Ref) + "'").str();
      break;
    }
    }
  }

  // And finally the suffix.
  Res += StringRef(Matches[0].end(), String.end() - Matches[0].end());

  return Res;
}

// These are the special characters matched in functions like "p_ere_exp".
static const char RegexMetachars[] = "()^$|*+?.[]\\{}";

bool Regex::isLiteralERE(StringRef Str) {
  // Check for regex metacharacters.  This list was derived from our regex
  // implementation in regcomp.c and double checked against the POSIX extended
  // regular expression specification.
  return Str.find_first_of(RegexMetachars) == StringRef::npos;
}

std::string Regex::escape(StringRef String) {
  std::string RegexStr;
  for (char C : String) {
    if (strchr(RegexMetachars, C))
      RegexStr += '\\';
    RegexStr += C;
  }

  return RegexStr;
}
