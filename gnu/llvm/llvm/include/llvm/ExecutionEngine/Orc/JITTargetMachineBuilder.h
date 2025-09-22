//===- JITTargetMachineBuilder.h - Build TargetMachines for JIT -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A utitily for building TargetMachines for JITs.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_JITTARGETMACHINEBUILDER_H
#define LLVM_EXECUTIONENGINE_ORC_JITTARGETMACHINEBUILDER_H

#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Error.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include "llvm/TargetParser/Triple.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace llvm {

class raw_ostream;

namespace orc {

/// A utility class for building TargetMachines for JITs.
class JITTargetMachineBuilder {
#ifndef NDEBUG
  friend class JITTargetMachineBuilderPrinter;
#endif
public:
  /// Create a JITTargetMachineBuilder based on the given triple.
  ///
  /// Note: TargetOptions is default-constructed, then EmulatedTLS is set to
  /// true. If EmulatedTLS is not required, these values should be reset before
  /// calling createTargetMachine.
  JITTargetMachineBuilder(Triple TT);

  /// Create a JITTargetMachineBuilder for the host system.
  ///
  /// Note: TargetOptions is default-constructed, then EmulatedTLS is set to
  /// true. If EmulatedTLS is not required, these values should be reset before
  /// calling createTargetMachine.
  static Expected<JITTargetMachineBuilder> detectHost();

  /// Create a TargetMachine.
  ///
  /// This operation will fail if the requested target is not registered,
  /// in which case see llvm/Support/TargetSelect.h. To JIT IR the Target and
  /// the target's AsmPrinter must both be registered. To JIT assembly
  /// (including inline and module level assembly) the target's AsmParser must
  /// also be registered.
  Expected<std::unique_ptr<TargetMachine>> createTargetMachine();

  /// Get the default DataLayout for the target.
  ///
  /// Note: This is reasonably expensive, as it creates a temporary
  /// TargetMachine instance under the hood. It is only suitable for use during
  /// JIT setup.
  Expected<DataLayout> getDefaultDataLayoutForTarget() {
    auto TM = createTargetMachine();
    if (!TM)
      return TM.takeError();
    return (*TM)->createDataLayout();
  }

  /// Set the CPU string.
  JITTargetMachineBuilder &setCPU(std::string CPU) {
    this->CPU = std::move(CPU);
    return *this;
  }

  /// Returns the CPU string.
  const std::string &getCPU() const { return CPU; }

  /// Set the relocation model.
  JITTargetMachineBuilder &setRelocationModel(std::optional<Reloc::Model> RM) {
    this->RM = std::move(RM);
    return *this;
  }

  /// Get the relocation model.
  const std::optional<Reloc::Model> &getRelocationModel() const { return RM; }

  /// Set the code model.
  JITTargetMachineBuilder &setCodeModel(std::optional<CodeModel::Model> CM) {
    this->CM = std::move(CM);
    return *this;
  }

  /// Get the code model.
  const std::optional<CodeModel::Model> &getCodeModel() const { return CM; }

  /// Set the LLVM CodeGen optimization level.
  JITTargetMachineBuilder &setCodeGenOptLevel(CodeGenOptLevel OptLevel) {
    this->OptLevel = OptLevel;
    return *this;
  }

  /// Set subtarget features.
  JITTargetMachineBuilder &setFeatures(StringRef FeatureString) {
    Features = SubtargetFeatures(FeatureString);
    return *this;
  }

  /// Add subtarget features.
  JITTargetMachineBuilder &
  addFeatures(const std::vector<std::string> &FeatureVec);

  /// Access subtarget features.
  SubtargetFeatures &getFeatures() { return Features; }

  /// Access subtarget features.
  const SubtargetFeatures &getFeatures() const { return Features; }

  /// Set TargetOptions.
  ///
  /// Note: This operation will overwrite any previously configured options,
  /// including EmulatedTLS and UseInitArray which the JITTargetMachineBuilder
  /// sets by default. Clients are responsible for re-enabling these overwritten
  /// options.
  JITTargetMachineBuilder &setOptions(TargetOptions Options) {
    this->Options = std::move(Options);
    return *this;
  }

  /// Access TargetOptions.
  TargetOptions &getOptions() { return Options; }

  /// Access TargetOptions.
  const TargetOptions &getOptions() const { return Options; }

  /// Access Triple.
  Triple &getTargetTriple() { return TT; }

  /// Access Triple.
  const Triple &getTargetTriple() const { return TT; }

private:
  Triple TT;
  std::string CPU;
  SubtargetFeatures Features;
  TargetOptions Options;
  std::optional<Reloc::Model> RM;
  std::optional<CodeModel::Model> CM;
  CodeGenOptLevel OptLevel = CodeGenOptLevel::Default;
};

#ifndef NDEBUG
class JITTargetMachineBuilderPrinter {
public:
  JITTargetMachineBuilderPrinter(JITTargetMachineBuilder &JTMB,
                                 StringRef Indent)
      : JTMB(JTMB), Indent(Indent) {}
  void print(raw_ostream &OS) const;

  friend raw_ostream &operator<<(raw_ostream &OS,
                                 const JITTargetMachineBuilderPrinter &JTMBP) {
    JTMBP.print(OS);
    return OS;
  }

private:
  JITTargetMachineBuilder &JTMB;
  StringRef Indent;
};
#endif // NDEBUG

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_JITTARGETMACHINEBUILDER_H
