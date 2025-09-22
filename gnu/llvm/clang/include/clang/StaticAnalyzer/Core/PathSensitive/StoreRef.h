//===- StoreRef.h - Smart pointer for store objects -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defined the type StoreRef.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_STOREREF_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_STOREREF_H

#include <cassert>

namespace clang {
namespace ento {

class StoreManager;

/// Store - This opaque type encapsulates an immutable mapping from
///  locations to values.  At a high-level, it represents the symbolic
///  memory model.  Different subclasses of StoreManager may choose
///  different types to represent the locations and values.
using Store = const void *;

class StoreRef {
  Store store;
  StoreManager &mgr;

public:
  StoreRef(Store store, StoreManager &smgr);
  StoreRef(const StoreRef &sr);
  StoreRef &operator=(StoreRef const &newStore);
  ~StoreRef();

  bool operator==(const StoreRef &x) const {
    assert(&mgr == &x.mgr);
    return x.store == store;
  }

  bool operator!=(const StoreRef &x) const { return !operator==(x); }

  Store getStore() const { return store; }
  const StoreManager &getStoreManager() const { return mgr; }
};

} // namespace ento
} // namespace clang

#endif // LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_STOREREF_H
