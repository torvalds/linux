//===- llvm/CodeGen/GCStrategy.h - Garbage collection -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// GCStrategy coordinates code generation algorithms and implements some itself
// in order to generate code compatible with a target code generator as
// specified in a function's 'gc' attribute. Algorithms are enabled by setting
// flags in a subclass's constructor, and some virtual methods can be
// overridden.
//
// GCStrategy is relevant for implementations using either gc.root or
// gc.statepoint based lowering strategies, but is currently focused mostly on
// options for gc.root.  This will change over time.
//
// When requested by a subclass of GCStrategy, the gc.root implementation will
// populate GCModuleInfo and GCFunctionInfo with that about each Function in
// the Module that opts in to garbage collection.  Specifically:
//
// - Safe points
//   Garbage collection is generally only possible at certain points in code.
//   GCStrategy can request that the collector insert such points:
//
//     - At and after any call to a subroutine
//     - Before returning from the current function
//     - Before backwards branches (loops)
//
// - Roots
//   When a reference to a GC-allocated object exists on the stack, it must be
//   stored in an alloca registered with llvm.gcoot.
//
// This information can used to emit the metadata tables which are required by
// the target garbage collector runtime.
//
// When used with gc.statepoint, information about safepoint and roots can be
// found in the binary StackMap section after code generation.  Safepoint
// placement is currently the responsibility of the frontend, though late
// insertion support is planned.
//
// The read and write barrier support can be used with either implementation.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_GCSTRATEGY_H
#define LLVM_IR_GCSTRATEGY_H

#include "llvm/Support/Registry.h"
#include <optional>
#include <string>

namespace llvm {

class Type;

/// GCStrategy describes a garbage collector algorithm's code generation
/// requirements, and provides overridable hooks for those needs which cannot
/// be abstractly described.  GCStrategy objects must be looked up through
/// the Function.  The objects themselves are owned by the Context and must
/// be immutable.
class GCStrategy {
private:
  friend class GCModuleInfo;

  std::string Name;

protected:
  bool UseStatepoints = false; /// Uses gc.statepoints as opposed to gc.roots,
                               /// if set, NeededSafePoints and UsesMetadata
                               /// should be left at their default values.

  bool UseRS4GC = false; /// If UseStatepoints is set, this determines whether
                         /// the RewriteStatepointsForGC pass should rewrite
                         /// this function's calls.
                         /// This should only be set if UseStatepoints is set.

  bool NeededSafePoints = false;    ///< if set, calls are inferred to be safepoints
  bool UsesMetadata = false;     ///< If set, backend must emit metadata tables.

public:
  GCStrategy();
  virtual ~GCStrategy() = default;

  /// Return the name of the GC strategy.  This is the value of the collector
  /// name string specified on functions which use this strategy.
  const std::string &getName() const { return Name; }

  /// Returns true if this strategy is expecting the use of gc.statepoints,
  /// and false otherwise.
  bool useStatepoints() const { return UseStatepoints; }

  /** @name Statepoint Specific Properties */
  ///@{

  /// If the type specified can be reliably distinguished, returns true for
  /// pointers to GC managed locations and false for pointers to non-GC
  /// managed locations.  Note a GCStrategy can always return 'std::nullopt'
  /// (i.e. an empty optional indicating it can't reliably distinguish.
  virtual std::optional<bool> isGCManagedPointer(const Type *Ty) const {
    return std::nullopt;
  }

  /// Returns true if the RewriteStatepointsForGC pass should run on functions
  /// using this GC.
  bool useRS4GC() const {
    assert((!UseRS4GC || useStatepoints()) &&
           "GC strategy has useRS4GC but not useStatepoints set");
    return UseRS4GC;
  }

  ///@}

  /// If set, appropriate metadata tables must be emitted by the back-end
  /// (assembler, JIT, or otherwise). The default stackmap information can be
  /// found in the StackMap section as described in the documentation.
  bool usesMetadata() const { return UsesMetadata; }

  /** @name GCRoot Specific Properties
   * These properties and overrides only apply to collector strategies using
   * GCRoot.
   */
  ///@{

  /// True if safe points need to be inferred on call sites
  bool needsSafePoints() const { return NeededSafePoints; }

  ///@}
};

/// Subclasses of GCStrategy are made available for use during compilation by
/// adding them to the global GCRegistry.  This can done either within the
/// LLVM source tree or via a loadable plugin.  An example registeration
/// would be:
/// static GCRegistry::Add<CustomGC> X("custom-name",
///        "my custom supper fancy gc strategy");
///
/// Note that to use a custom GCMetadataPrinter, you must also
/// register your GCMetadataPrinter subclass with the
/// GCMetadataPrinterRegistery as well.
using GCRegistry = Registry<GCStrategy>;

/// Lookup the GCStrategy object associated with the given gc name.
std::unique_ptr<GCStrategy> getGCStrategy(const StringRef Name);

} // end namespace llvm

#endif // LLVM_IR_GCSTRATEGY_H
