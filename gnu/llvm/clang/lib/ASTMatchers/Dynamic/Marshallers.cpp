//===--- Marshallers.cpp ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Marshallers.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Regex.h"
#include <optional>
#include <string>

static std::optional<std::string>
getBestGuess(llvm::StringRef Search, llvm::ArrayRef<llvm::StringRef> Allowed,
             llvm::StringRef DropPrefix = "", unsigned MaxEditDistance = 3) {
  if (MaxEditDistance != ~0U)
    ++MaxEditDistance;
  llvm::StringRef Res;
  for (const llvm::StringRef &Item : Allowed) {
    if (Item.equals_insensitive(Search)) {
      assert(Item != Search && "This should be handled earlier on.");
      MaxEditDistance = 1;
      Res = Item;
      continue;
    }
    unsigned Distance = Item.edit_distance(Search);
    if (Distance < MaxEditDistance) {
      MaxEditDistance = Distance;
      Res = Item;
    }
  }
  if (!Res.empty())
    return Res.str();
  if (!DropPrefix.empty()) {
    --MaxEditDistance; // Treat dropping the prefix as 1 edit
    for (const llvm::StringRef &Item : Allowed) {
      auto NoPrefix = Item;
      if (!NoPrefix.consume_front(DropPrefix))
        continue;
      if (NoPrefix.equals_insensitive(Search)) {
        if (NoPrefix == Search)
          return Item.str();
        MaxEditDistance = 1;
        Res = Item;
        continue;
      }
      unsigned Distance = NoPrefix.edit_distance(Search);
      if (Distance < MaxEditDistance) {
        MaxEditDistance = Distance;
        Res = Item;
      }
    }
    if (!Res.empty())
      return Res.str();
  }
  return std::nullopt;
}

std::optional<std::string>
clang::ast_matchers::dynamic::internal::ArgTypeTraits<
    clang::attr::Kind>::getBestGuess(const VariantValue &Value) {
  static constexpr llvm::StringRef Allowed[] = {
#define ATTR(X) "attr::" #X,
#include "clang/Basic/AttrList.inc"
  };
  if (Value.isString())
    return ::getBestGuess(Value.getString(), llvm::ArrayRef(Allowed), "attr::");
  return std::nullopt;
}

std::optional<std::string>
clang::ast_matchers::dynamic::internal::ArgTypeTraits<
    clang::CastKind>::getBestGuess(const VariantValue &Value) {
  static constexpr llvm::StringRef Allowed[] = {
#define CAST_OPERATION(Name) "CK_" #Name,
#include "clang/AST/OperationKinds.def"
  };
  if (Value.isString())
    return ::getBestGuess(Value.getString(), llvm::ArrayRef(Allowed), "CK_");
  return std::nullopt;
}

std::optional<std::string>
clang::ast_matchers::dynamic::internal::ArgTypeTraits<
    clang::OpenMPClauseKind>::getBestGuess(const VariantValue &Value) {
  static constexpr llvm::StringRef Allowed[] = {
#define GEN_CLANG_CLAUSE_CLASS
#define CLAUSE_CLASS(Enum, Str, Class) #Enum,
#include "llvm/Frontend/OpenMP/OMP.inc"
  };
  if (Value.isString())
    return ::getBestGuess(Value.getString(), llvm::ArrayRef(Allowed), "OMPC_");
  return std::nullopt;
}

std::optional<std::string>
clang::ast_matchers::dynamic::internal::ArgTypeTraits<
    clang::UnaryExprOrTypeTrait>::getBestGuess(const VariantValue &Value) {
  static constexpr llvm::StringRef Allowed[] = {
#define UNARY_EXPR_OR_TYPE_TRAIT(Spelling, Name, Key) "UETT_" #Name,
#define CXX11_UNARY_EXPR_OR_TYPE_TRAIT(Spelling, Name, Key) "UETT_" #Name,
#include "clang/Basic/TokenKinds.def"
  };
  if (Value.isString())
    return ::getBestGuess(Value.getString(), llvm::ArrayRef(Allowed), "UETT_");
  return std::nullopt;
}

static constexpr std::pair<llvm::StringRef, llvm::Regex::RegexFlags>
    RegexMap[] = {
        {"NoFlags", llvm::Regex::RegexFlags::NoFlags},
        {"IgnoreCase", llvm::Regex::RegexFlags::IgnoreCase},
        {"Newline", llvm::Regex::RegexFlags::Newline},
        {"BasicRegex", llvm::Regex::RegexFlags::BasicRegex},
};

static std::optional<llvm::Regex::RegexFlags>
getRegexFlag(llvm::StringRef Flag) {
  for (const auto &StringFlag : RegexMap) {
    if (Flag == StringFlag.first)
      return StringFlag.second;
  }
  return std::nullopt;
}

static std::optional<llvm::StringRef> getCloseRegexMatch(llvm::StringRef Flag) {
  for (const auto &StringFlag : RegexMap) {
    if (Flag.edit_distance(StringFlag.first) < 3)
      return StringFlag.first;
  }
  return std::nullopt;
}

std::optional<llvm::Regex::RegexFlags>
clang::ast_matchers::dynamic::internal::ArgTypeTraits<
    llvm::Regex::RegexFlags>::getFlags(llvm::StringRef Flags) {
  std::optional<llvm::Regex::RegexFlags> Flag;
  SmallVector<StringRef, 4> Split;
  Flags.split(Split, '|', -1, false);
  for (StringRef OrFlag : Split) {
    if (std::optional<llvm::Regex::RegexFlags> NextFlag =
            getRegexFlag(OrFlag.trim()))
      Flag = Flag.value_or(llvm::Regex::NoFlags) | *NextFlag;
    else
      return std::nullopt;
  }
  return Flag;
}

std::optional<std::string>
clang::ast_matchers::dynamic::internal::ArgTypeTraits<
    llvm::Regex::RegexFlags>::getBestGuess(const VariantValue &Value) {
  if (!Value.isString())
    return std::nullopt;
  SmallVector<StringRef, 4> Split;
  llvm::StringRef(Value.getString()).split(Split, '|', -1, false);
  for (llvm::StringRef &Flag : Split) {
    if (std::optional<llvm::StringRef> BestGuess =
            getCloseRegexMatch(Flag.trim()))
      Flag = *BestGuess;
    else
      return std::nullopt;
  }
  if (Split.empty())
    return std::nullopt;
  return llvm::join(Split, " | ");
}
