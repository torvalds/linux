//===- Action.h - Abstract compilation steps --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_DRIVER_ACTION_H
#define LLVM_CLANG_DRIVER_ACTION_H

#include "clang/Basic/LLVM.h"
#include "clang/Driver/Types.h"
#include "clang/Driver/Util.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include <string>

namespace llvm {
namespace opt {

class Arg;

} // namespace opt
} // namespace llvm

namespace clang {
namespace driver {

class ToolChain;

/// Action - Represent an abstract compilation step to perform.
///
/// An action represents an edge in the compilation graph; typically
/// it is a job to transform an input using some tool.
///
/// The current driver is hard wired to expect actions which produce a
/// single primary output, at least in terms of controlling the
/// compilation. Actions can produce auxiliary files, but can only
/// produce a single output to feed into subsequent actions.
///
/// Actions are usually owned by a Compilation, which creates new
/// actions via MakeAction().
class Action {
public:
  using size_type = ActionList::size_type;
  using input_iterator = ActionList::iterator;
  using input_const_iterator = ActionList::const_iterator;
  using input_range = llvm::iterator_range<input_iterator>;
  using input_const_range = llvm::iterator_range<input_const_iterator>;

  enum ActionClass {
    InputClass = 0,
    BindArchClass,
    OffloadClass,
    PreprocessJobClass,
    PrecompileJobClass,
    ExtractAPIJobClass,
    AnalyzeJobClass,
    MigrateJobClass,
    CompileJobClass,
    BackendJobClass,
    AssembleJobClass,
    LinkJobClass,
    IfsMergeJobClass,
    LipoJobClass,
    DsymutilJobClass,
    VerifyDebugInfoJobClass,
    VerifyPCHJobClass,
    OffloadBundlingJobClass,
    OffloadUnbundlingJobClass,
    OffloadPackagerJobClass,
    LinkerWrapperJobClass,
    StaticLibJobClass,
    BinaryAnalyzeJobClass,

    JobClassFirst = PreprocessJobClass,
    JobClassLast = BinaryAnalyzeJobClass
  };

  // The offloading kind determines if this action is binded to a particular
  // programming model. Each entry reserves one bit. We also have a special kind
  // to designate the host offloading tool chain.
  enum OffloadKind {
    OFK_None = 0x00,

    // The host offloading tool chain.
    OFK_Host = 0x01,

    // The device offloading tool chains - one bit for each programming model.
    OFK_Cuda = 0x02,
    OFK_OpenMP = 0x04,
    OFK_HIP = 0x08,
  };

  static const char *getClassName(ActionClass AC);

private:
  ActionClass Kind;

  /// The output type of this action.
  types::ID Type;

  ActionList Inputs;

  /// Flag that is set to true if this action can be collapsed with others
  /// actions that depend on it. This is true by default and set to false when
  /// the action is used by two different tool chains, which is enabled by the
  /// offloading support implementation.
  bool CanBeCollapsedWithNextDependentAction = true;

protected:
  ///
  /// Offload information.
  ///

  /// The host offloading kind - a combination of kinds encoded in a mask.
  /// Multiple programming models may be supported simultaneously by the same
  /// host.
  unsigned ActiveOffloadKindMask = 0u;

  /// Offloading kind of the device.
  OffloadKind OffloadingDeviceKind = OFK_None;

  /// The Offloading architecture associated with this action.
  const char *OffloadingArch = nullptr;

  /// The Offloading toolchain associated with this device action.
  const ToolChain *OffloadingToolChain = nullptr;

  Action(ActionClass Kind, types::ID Type) : Action(Kind, ActionList(), Type) {}
  Action(ActionClass Kind, Action *Input, types::ID Type)
      : Action(Kind, ActionList({Input}), Type) {}
  Action(ActionClass Kind, Action *Input)
      : Action(Kind, ActionList({Input}), Input->getType()) {}
  Action(ActionClass Kind, const ActionList &Inputs, types::ID Type)
      : Kind(Kind), Type(Type), Inputs(Inputs) {}

public:
  virtual ~Action();

  const char *getClassName() const { return Action::getClassName(getKind()); }

  ActionClass getKind() const { return Kind; }
  types::ID getType() const { return Type; }

  ActionList &getInputs() { return Inputs; }
  const ActionList &getInputs() const { return Inputs; }

  size_type size() const { return Inputs.size(); }

  input_iterator input_begin() { return Inputs.begin(); }
  input_iterator input_end() { return Inputs.end(); }
  input_range inputs() { return input_range(input_begin(), input_end()); }
  input_const_iterator input_begin() const { return Inputs.begin(); }
  input_const_iterator input_end() const { return Inputs.end(); }
  input_const_range inputs() const {
    return input_const_range(input_begin(), input_end());
  }

  /// Mark this action as not legal to collapse.
  void setCannotBeCollapsedWithNextDependentAction() {
    CanBeCollapsedWithNextDependentAction = false;
  }

  /// Return true if this function can be collapsed with others.
  bool isCollapsingWithNextDependentActionLegal() const {
    return CanBeCollapsedWithNextDependentAction;
  }

  /// Return a string containing the offload kind of the action.
  std::string getOffloadingKindPrefix() const;

  /// Return a string that can be used as prefix in order to generate unique
  /// files for each offloading kind. By default, no prefix is used for
  /// non-device kinds, except if \a CreatePrefixForHost is set.
  static std::string
  GetOffloadingFileNamePrefix(OffloadKind Kind,
                              StringRef NormalizedTriple,
                              bool CreatePrefixForHost = false);

  /// Return a string containing a offload kind name.
  static StringRef GetOffloadKindName(OffloadKind Kind);

  /// Set the device offload info of this action and propagate it to its
  /// dependences.
  void propagateDeviceOffloadInfo(OffloadKind OKind, const char *OArch,
                                  const ToolChain *OToolChain);

  /// Append the host offload info of this action and propagate it to its
  /// dependences.
  void propagateHostOffloadInfo(unsigned OKinds, const char *OArch);

  void setHostOffloadInfo(unsigned OKinds, const char *OArch) {
    ActiveOffloadKindMask |= OKinds;
    OffloadingArch = OArch;
  }

  /// Set the offload info of this action to be the same as the provided action,
  /// and propagate it to its dependences.
  void propagateOffloadInfo(const Action *A);

  unsigned getOffloadingHostActiveKinds() const {
    return ActiveOffloadKindMask;
  }

  OffloadKind getOffloadingDeviceKind() const { return OffloadingDeviceKind; }
  const char *getOffloadingArch() const { return OffloadingArch; }
  const ToolChain *getOffloadingToolChain() const {
    return OffloadingToolChain;
  }

  /// Check if this action have any offload kinds. Note that host offload kinds
  /// are only set if the action is a dependence to a host offload action.
  bool isHostOffloading(unsigned int OKind) const {
    return ActiveOffloadKindMask & OKind;
  }
  bool isDeviceOffloading(OffloadKind OKind) const {
    return OffloadingDeviceKind == OKind;
  }
  bool isOffloading(OffloadKind OKind) const {
    return isHostOffloading(OKind) || isDeviceOffloading(OKind);
  }
};

class InputAction : public Action {
  const llvm::opt::Arg &Input;
  std::string Id;
  virtual void anchor();

public:
  InputAction(const llvm::opt::Arg &Input, types::ID Type,
              StringRef Id = StringRef());

  const llvm::opt::Arg &getInputArg() const { return Input; }

  void setId(StringRef _Id) { Id = _Id.str(); }
  StringRef getId() const { return Id; }

  static bool classof(const Action *A) {
    return A->getKind() == InputClass;
  }
};

class BindArchAction : public Action {
  virtual void anchor();

  /// The architecture to bind, or 0 if the default architecture
  /// should be bound.
  StringRef ArchName;

public:
  BindArchAction(Action *Input, StringRef ArchName);

  StringRef getArchName() const { return ArchName; }

  static bool classof(const Action *A) {
    return A->getKind() == BindArchClass;
  }
};

/// An offload action combines host or/and device actions according to the
/// programming model implementation needs and propagates the offloading kind to
/// its dependences.
class OffloadAction final : public Action {
  virtual void anchor();

public:
  /// Type used to communicate device actions. It associates bound architecture,
  /// toolchain, and offload kind to each action.
  class DeviceDependences final {
  public:
    using ToolChainList = SmallVector<const ToolChain *, 3>;
    using BoundArchList = SmallVector<const char *, 3>;
    using OffloadKindList = SmallVector<OffloadKind, 3>;

  private:
    // Lists that keep the information for each dependency. All the lists are
    // meant to be updated in sync. We are adopting separate lists instead of a
    // list of structs, because that simplifies forwarding the actions list to
    // initialize the inputs of the base Action class.

    /// The dependence actions.
    ActionList DeviceActions;

    /// The offloading toolchains that should be used with the action.
    ToolChainList DeviceToolChains;

    /// The architectures that should be used with this action.
    BoundArchList DeviceBoundArchs;

    /// The offload kind of each dependence.
    OffloadKindList DeviceOffloadKinds;

  public:
    /// Add an action along with the associated toolchain, bound arch, and
    /// offload kind.
    void add(Action &A, const ToolChain &TC, const char *BoundArch,
             OffloadKind OKind);

    /// Add an action along with the associated toolchain, bound arch, and
    /// offload kinds.
    void add(Action &A, const ToolChain &TC, const char *BoundArch,
             unsigned OffloadKindMask);

    /// Get each of the individual arrays.
    const ActionList &getActions() const { return DeviceActions; }
    const ToolChainList &getToolChains() const { return DeviceToolChains; }
    const BoundArchList &getBoundArchs() const { return DeviceBoundArchs; }
    const OffloadKindList &getOffloadKinds() const {
      return DeviceOffloadKinds;
    }
  };

  /// Type used to communicate host actions. It associates bound architecture,
  /// toolchain, and offload kinds to the host action.
  class HostDependence final {
    /// The dependence action.
    Action &HostAction;

    /// The offloading toolchain that should be used with the action.
    const ToolChain &HostToolChain;

    /// The architectures that should be used with this action.
    const char *HostBoundArch = nullptr;

    /// The offload kind of each dependence.
    unsigned HostOffloadKinds = 0u;

  public:
    HostDependence(Action &A, const ToolChain &TC, const char *BoundArch,
                   const unsigned OffloadKinds)
        : HostAction(A), HostToolChain(TC), HostBoundArch(BoundArch),
          HostOffloadKinds(OffloadKinds) {}

    /// Constructor version that obtains the offload kinds from the device
    /// dependencies.
    HostDependence(Action &A, const ToolChain &TC, const char *BoundArch,
                   const DeviceDependences &DDeps);
    Action *getAction() const { return &HostAction; }
    const ToolChain *getToolChain() const { return &HostToolChain; }
    const char *getBoundArch() const { return HostBoundArch; }
    unsigned getOffloadKinds() const { return HostOffloadKinds; }
  };

  using OffloadActionWorkTy =
      llvm::function_ref<void(Action *, const ToolChain *, const char *)>;

private:
  /// The host offloading toolchain that should be used with the action.
  const ToolChain *HostTC = nullptr;

  /// The tool chains associated with the list of actions.
  DeviceDependences::ToolChainList DevToolChains;

public:
  OffloadAction(const HostDependence &HDep);
  OffloadAction(const DeviceDependences &DDeps, types::ID Ty);
  OffloadAction(const HostDependence &HDep, const DeviceDependences &DDeps);

  /// Execute the work specified in \a Work on the host dependence.
  void doOnHostDependence(const OffloadActionWorkTy &Work) const;

  /// Execute the work specified in \a Work on each device dependence.
  void doOnEachDeviceDependence(const OffloadActionWorkTy &Work) const;

  /// Execute the work specified in \a Work on each dependence.
  void doOnEachDependence(const OffloadActionWorkTy &Work) const;

  /// Execute the work specified in \a Work on each host or device dependence if
  /// \a IsHostDependenceto is true or false, respectively.
  void doOnEachDependence(bool IsHostDependence,
                          const OffloadActionWorkTy &Work) const;

  /// Return true if the action has a host dependence.
  bool hasHostDependence() const;

  /// Return the host dependence of this action. This function is only expected
  /// to be called if the host dependence exists.
  Action *getHostDependence() const;

  /// Return true if the action has a single device dependence. If \a
  /// DoNotConsiderHostActions is set, ignore the host dependence, if any, while
  /// accounting for the number of dependences.
  bool hasSingleDeviceDependence(bool DoNotConsiderHostActions = false) const;

  /// Return the single device dependence of this action. This function is only
  /// expected to be called if a single device dependence exists. If \a
  /// DoNotConsiderHostActions is set, a host dependence is allowed.
  Action *
  getSingleDeviceDependence(bool DoNotConsiderHostActions = false) const;

  static bool classof(const Action *A) { return A->getKind() == OffloadClass; }
};

class JobAction : public Action {
  virtual void anchor();

protected:
  JobAction(ActionClass Kind, Action *Input, types::ID Type);
  JobAction(ActionClass Kind, const ActionList &Inputs, types::ID Type);

public:
  static bool classof(const Action *A) {
    return (A->getKind() >= JobClassFirst &&
            A->getKind() <= JobClassLast);
  }
};

class PreprocessJobAction : public JobAction {
  void anchor() override;

public:
  PreprocessJobAction(Action *Input, types::ID OutputType);

  static bool classof(const Action *A) {
    return A->getKind() == PreprocessJobClass;
  }
};

class PrecompileJobAction : public JobAction {
  void anchor() override;

protected:
  PrecompileJobAction(ActionClass Kind, Action *Input, types::ID OutputType);

public:
  PrecompileJobAction(Action *Input, types::ID OutputType);

  static bool classof(const Action *A) {
    return A->getKind() == PrecompileJobClass;
  }
};

class ExtractAPIJobAction : public JobAction {
  void anchor() override;

public:
  ExtractAPIJobAction(Action *Input, types::ID OutputType);

  static bool classof(const Action *A) {
    return A->getKind() == ExtractAPIJobClass;
  }

  void addHeaderInput(Action *Input) { getInputs().push_back(Input); }
};

class AnalyzeJobAction : public JobAction {
  void anchor() override;

public:
  AnalyzeJobAction(Action *Input, types::ID OutputType);

  static bool classof(const Action *A) {
    return A->getKind() == AnalyzeJobClass;
  }
};

class MigrateJobAction : public JobAction {
  void anchor() override;

public:
  MigrateJobAction(Action *Input, types::ID OutputType);

  static bool classof(const Action *A) {
    return A->getKind() == MigrateJobClass;
  }
};

class CompileJobAction : public JobAction {
  void anchor() override;

public:
  CompileJobAction(Action *Input, types::ID OutputType);

  static bool classof(const Action *A) {
    return A->getKind() == CompileJobClass;
  }
};

class BackendJobAction : public JobAction {
  void anchor() override;

public:
  BackendJobAction(Action *Input, types::ID OutputType);

  static bool classof(const Action *A) {
    return A->getKind() == BackendJobClass;
  }
};

class AssembleJobAction : public JobAction {
  void anchor() override;

public:
  AssembleJobAction(Action *Input, types::ID OutputType);

  static bool classof(const Action *A) {
    return A->getKind() == AssembleJobClass;
  }
};

class IfsMergeJobAction : public JobAction {
  void anchor() override;

public:
  IfsMergeJobAction(ActionList &Inputs, types::ID Type);

  static bool classof(const Action *A) {
    return A->getKind() == IfsMergeJobClass;
  }
};

class LinkJobAction : public JobAction {
  void anchor() override;

public:
  LinkJobAction(ActionList &Inputs, types::ID Type);

  static bool classof(const Action *A) {
    return A->getKind() == LinkJobClass;
  }
};

class LipoJobAction : public JobAction {
  void anchor() override;

public:
  LipoJobAction(ActionList &Inputs, types::ID Type);

  static bool classof(const Action *A) {
    return A->getKind() == LipoJobClass;
  }
};

class DsymutilJobAction : public JobAction {
  void anchor() override;

public:
  DsymutilJobAction(ActionList &Inputs, types::ID Type);

  static bool classof(const Action *A) {
    return A->getKind() == DsymutilJobClass;
  }
};

class VerifyJobAction : public JobAction {
  void anchor() override;

public:
  VerifyJobAction(ActionClass Kind, Action *Input, types::ID Type);

  static bool classof(const Action *A) {
    return A->getKind() == VerifyDebugInfoJobClass ||
           A->getKind() == VerifyPCHJobClass;
  }
};

class VerifyDebugInfoJobAction : public VerifyJobAction {
  void anchor() override;

public:
  VerifyDebugInfoJobAction(Action *Input, types::ID Type);

  static bool classof(const Action *A) {
    return A->getKind() == VerifyDebugInfoJobClass;
  }
};

class VerifyPCHJobAction : public VerifyJobAction {
  void anchor() override;

public:
  VerifyPCHJobAction(Action *Input, types::ID Type);

  static bool classof(const Action *A) {
    return A->getKind() == VerifyPCHJobClass;
  }
};

class OffloadBundlingJobAction : public JobAction {
  void anchor() override;

public:
  // Offloading bundling doesn't change the type of output.
  OffloadBundlingJobAction(ActionList &Inputs);

  static bool classof(const Action *A) {
    return A->getKind() == OffloadBundlingJobClass;
  }
};

class OffloadUnbundlingJobAction final : public JobAction {
  void anchor() override;

public:
  /// Type that provides information about the actions that depend on this
  /// unbundling action.
  struct DependentActionInfo final {
    /// The tool chain of the dependent action.
    const ToolChain *DependentToolChain = nullptr;

    /// The bound architecture of the dependent action.
    StringRef DependentBoundArch;

    /// The offload kind of the dependent action.
    const OffloadKind DependentOffloadKind = OFK_None;

    DependentActionInfo(const ToolChain *DependentToolChain,
                        StringRef DependentBoundArch,
                        const OffloadKind DependentOffloadKind)
        : DependentToolChain(DependentToolChain),
          DependentBoundArch(DependentBoundArch),
          DependentOffloadKind(DependentOffloadKind) {}
  };

private:
  /// Container that keeps information about each dependence of this unbundling
  /// action.
  SmallVector<DependentActionInfo, 6> DependentActionInfoArray;

public:
  // Offloading unbundling doesn't change the type of output.
  OffloadUnbundlingJobAction(Action *Input);

  /// Register information about a dependent action.
  void registerDependentActionInfo(const ToolChain *TC, StringRef BoundArch,
                                   OffloadKind Kind) {
    DependentActionInfoArray.push_back({TC, BoundArch, Kind});
  }

  /// Return the information about all depending actions.
  ArrayRef<DependentActionInfo> getDependentActionsInfo() const {
    return DependentActionInfoArray;
  }

  static bool classof(const Action *A) {
    return A->getKind() == OffloadUnbundlingJobClass;
  }
};

class OffloadPackagerJobAction : public JobAction {
  void anchor() override;

public:
  OffloadPackagerJobAction(ActionList &Inputs, types::ID Type);

  static bool classof(const Action *A) {
    return A->getKind() == OffloadPackagerJobClass;
  }
};

class LinkerWrapperJobAction : public JobAction {
  void anchor() override;

public:
  LinkerWrapperJobAction(ActionList &Inputs, types::ID Type);

  static bool classof(const Action *A) {
    return A->getKind() == LinkerWrapperJobClass;
  }
};

class StaticLibJobAction : public JobAction {
  void anchor() override;

public:
  StaticLibJobAction(ActionList &Inputs, types::ID Type);

  static bool classof(const Action *A) {
    return A->getKind() == StaticLibJobClass;
  }
};

class BinaryAnalyzeJobAction : public JobAction {
  void anchor() override;

public:
  BinaryAnalyzeJobAction(Action *Input, types::ID Type);

  static bool classof(const Action *A) {
    return A->getKind() == BinaryAnalyzeJobClass;
  }
};

} // namespace driver
} // namespace clang

#endif // LLVM_CLANG_DRIVER_ACTION_H
