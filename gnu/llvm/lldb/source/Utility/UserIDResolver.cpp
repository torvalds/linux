//===-- UserIDResolver.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/UserIDResolver.h"
#include "llvm/Support/ManagedStatic.h"
#include <optional>

using namespace lldb_private;

UserIDResolver::~UserIDResolver() = default;

std::optional<llvm::StringRef> UserIDResolver::Get(
    id_t id, Map &cache,
    std::optional<std::string> (UserIDResolver::*do_get)(id_t)) {

  std::lock_guard<std::mutex> guard(m_mutex);
  auto iter_bool = cache.try_emplace(id, std::nullopt);
  if (iter_bool.second)
    iter_bool.first->second = (this->*do_get)(id);
  if (iter_bool.first->second)
    return llvm::StringRef(*iter_bool.first->second);
  return std::nullopt;
}

namespace {
class NoopResolver : public UserIDResolver {
protected:
  std::optional<std::string> DoGetUserName(id_t uid) override {
    return std::nullopt;
  }

  std::optional<std::string> DoGetGroupName(id_t gid) override {
    return std::nullopt;
  }
};
} // namespace

static llvm::ManagedStatic<NoopResolver> g_noop_resolver;

UserIDResolver &UserIDResolver::GetNoopResolver() { return *g_noop_resolver; }
