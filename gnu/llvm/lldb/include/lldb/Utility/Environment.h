//===-- Environment.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_ENVIRONMENT_H
#define LLDB_UTILITY_ENVIRONMENT_H

#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/FormatProviders.h"

namespace lldb_private {

class Environment : private llvm::StringMap<std::string> {
  using Base = llvm::StringMap<std::string>;

public:
  class Envp {
  public:
    Envp(Envp &&RHS) = default;
    Envp &operator=(Envp &&RHS) = default;

    char *const *get() const { return Data; }
    operator char *const *() const { return get(); }

  private:
    explicit Envp(const Environment &Env);
    char *make_entry(llvm::StringRef Key, llvm::StringRef Value);
    Envp(const Envp &) = delete;
    Envp &operator=(const Envp &) = delete;
    friend class Environment;

    llvm::BumpPtrAllocator Allocator;
    char **Data;
  };

  using Base::const_iterator;
  using Base::iterator;
  using Base::value_type;

  using Base::begin;
  using Base::clear;
  using Base::count;
  using Base::empty;
  using Base::end;
  using Base::erase;
  using Base::find;
  using Base::insert;
  using Base::insert_or_assign;
  using Base::lookup;
  using Base::size;
  using Base::try_emplace;
  using Base::operator[];

  Environment() {}
  Environment(const Environment &RHS) : Base(static_cast<const Base&>(RHS)) {}
  Environment(Environment &&RHS) : Base(std::move(RHS)) {}
  Environment(char *const *Env)
      : Environment(const_cast<const char *const *>(Env)) {}
  Environment(const char *const *Env);

  Environment &operator=(Environment RHS) {
    Base::operator=(std::move(RHS));
    return *this;
  }

  std::pair<iterator, bool> insert(llvm::StringRef KeyEqValue) {
    auto Split = KeyEqValue.split('=');
    return insert(std::make_pair(Split.first, std::string(Split.second)));
  }

  void insert(iterator first, iterator last);

  Envp getEnvp() const { return Envp(*this); }

  static std::string compose(const value_type &KeyValue) {
    return (KeyValue.first() + "=" + KeyValue.second).str();
  }
};

} // namespace lldb_private

namespace llvm {
template <> struct format_provider<lldb_private::Environment> {
  static void format(const lldb_private::Environment &Env, raw_ostream &Stream,
                     StringRef Style) {
    for (const auto &KV : Env)
      Stream << "env[" << KV.first() << "] = " << KV.second << "\n";
  }
};
} // namespace llvm

#endif // LLDB_UTILITY_ENVIRONMENT_H
