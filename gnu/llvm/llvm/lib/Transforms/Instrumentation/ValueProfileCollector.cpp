//===- ValueProfileCollector.cpp - determine what to value profile --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The implementation of the ValueProfileCollector via ValueProfileCollectorImpl
//
//===----------------------------------------------------------------------===//

#include "ValueProfileCollector.h"
#include "ValueProfilePlugins.inc"
#include "llvm/ProfileData/InstrProf.h"

using namespace llvm;

namespace {

/// A plugin-based class that takes an arbitrary number of Plugin types.
/// Each plugin type must satisfy the following API:
///  1) the constructor must take a `Function &f`. Typically, the plugin would
///     scan the function looking for candidates.
///  2) contain a member function with the following signature and name:
///        void run(std::vector<CandidateInfo> &Candidates);
///    such that the plugin would append its result into the vector parameter.
///
/// Plugins are defined in ValueProfilePlugins.inc
template <class... Ts> class PluginChain;

/// The type PluginChainFinal is the final chain of plugins that will be used by
/// ValueProfileCollectorImpl.
using PluginChainFinal = PluginChain<VP_PLUGIN_LIST>;

template <> class PluginChain<> {
public:
  PluginChain(Function &F, TargetLibraryInfo &TLI) {}
  void get(InstrProfValueKind K, std::vector<CandidateInfo> &Candidates) {}
};

template <class PluginT, class... Ts>
class PluginChain<PluginT, Ts...> : public PluginChain<Ts...> {
  PluginT Plugin;
  using Base = PluginChain<Ts...>;

public:
  PluginChain(Function &F, TargetLibraryInfo &TLI)
      : PluginChain<Ts...>(F, TLI), Plugin(F, TLI) {}

  void get(InstrProfValueKind K, std::vector<CandidateInfo> &Candidates) {
    if (K == PluginT::Kind)
      Plugin.run(Candidates);
    Base::get(K, Candidates);
  }
};

} // end anonymous namespace

/// ValueProfileCollectorImpl inherits the API of PluginChainFinal.
class ValueProfileCollector::ValueProfileCollectorImpl : public PluginChainFinal {
public:
  using PluginChainFinal::PluginChainFinal;
};

ValueProfileCollector::ValueProfileCollector(Function &F,
                                             TargetLibraryInfo &TLI)
    : PImpl(new ValueProfileCollectorImpl(F, TLI)) {}

ValueProfileCollector::~ValueProfileCollector() = default;

std::vector<CandidateInfo>
ValueProfileCollector::get(InstrProfValueKind Kind) const {
  std::vector<CandidateInfo> Result;
  PImpl->get(Kind, Result);
  return Result;
}
