//===- Linker.h - Module Linker Interface -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LINKER_LINKER_H
#define LLVM_LINKER_LINKER_H

#include "llvm/ADT/StringSet.h"
#include "llvm/Linker/IRMover.h"

namespace llvm {
class Module;

/// This class provides the core functionality of linking in LLVM. It keeps a
/// pointer to the merged module so far. It doesn't take ownership of the
/// module since it is assumed that the user of this class will want to do
/// something with it after the linking.
class Linker {
  IRMover Mover;

public:
  enum Flags {
    None = 0,
    OverrideFromSrc = (1 << 0),
    LinkOnlyNeeded = (1 << 1),
  };

  Linker(Module &M);

  /// Link \p Src into the composite.
  ///
  /// Passing OverrideSymbols as true will have symbols from Src
  /// shadow those in the Dest.
  ///
  /// Passing InternalizeCallback will have the linker call the function with
  /// the new module and a list of global value names to be internalized by the
  /// callback.
  ///
  /// Returns true on error.
  bool linkInModule(std::unique_ptr<Module> Src, unsigned Flags = Flags::None,
                    std::function<void(Module &, const StringSet<> &)>
                        InternalizeCallback = {});

  static bool linkModules(Module &Dest, std::unique_ptr<Module> Src,
                          unsigned Flags = Flags::None,
                          std::function<void(Module &, const StringSet<> &)>
                              InternalizeCallback = {});
};

} // End llvm namespace

#endif
