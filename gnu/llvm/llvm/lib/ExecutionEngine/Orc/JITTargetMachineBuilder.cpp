//===----- JITTargetMachineBuilder.cpp - Build TargetMachines for JIT -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"

#include "llvm/ADT/StringMap.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"

namespace llvm {
namespace orc {

JITTargetMachineBuilder::JITTargetMachineBuilder(Triple TT)
    : TT(std::move(TT)) {
  Options.EmulatedTLS = true;
  Options.UseInitArray = true;
}

Expected<JITTargetMachineBuilder> JITTargetMachineBuilder::detectHost() {
  JITTargetMachineBuilder TMBuilder((Triple(sys::getProcessTriple())));

  // Retrieve host CPU name and sub-target features and add them to builder.
  // Relocation model, code model and codegen opt level are kept to default
  // values.
  for (const auto &Feature : llvm::sys::getHostCPUFeatures())
    TMBuilder.getFeatures().AddFeature(Feature.first(), Feature.second);

  TMBuilder.setCPU(std::string(llvm::sys::getHostCPUName()));

  return TMBuilder;
}

Expected<std::unique_ptr<TargetMachine>>
JITTargetMachineBuilder::createTargetMachine() {

  std::string ErrMsg;
  auto *TheTarget = TargetRegistry::lookupTarget(TT.getTriple(), ErrMsg);
  if (!TheTarget)
    return make_error<StringError>(std::move(ErrMsg), inconvertibleErrorCode());

  if (!TheTarget->hasJIT())
    return make_error<StringError>("Target has no JIT support",
                                   inconvertibleErrorCode());

  auto *TM =
      TheTarget->createTargetMachine(TT.getTriple(), CPU, Features.getString(),
                                     Options, RM, CM, OptLevel, /*JIT*/ true);
  if (!TM)
    return make_error<StringError>("Could not allocate target machine",
                                   inconvertibleErrorCode());

  return std::unique_ptr<TargetMachine>(TM);
}

JITTargetMachineBuilder &JITTargetMachineBuilder::addFeatures(
    const std::vector<std::string> &FeatureVec) {
  for (const auto &F : FeatureVec)
    Features.AddFeature(F);
  return *this;
}

#ifndef NDEBUG
void JITTargetMachineBuilderPrinter::print(raw_ostream &OS) const {
  OS << Indent << "{\n"
     << Indent << "  Triple = \"" << JTMB.TT.str() << "\"\n"
     << Indent << "  CPU = \"" << JTMB.CPU << "\"\n"
     << Indent << "  Features = \"" << JTMB.Features.getString() << "\"\n"
     << Indent << "  Options = <not-printable>\n"
     << Indent << "  Relocation Model = ";

  if (JTMB.RM) {
    switch (*JTMB.RM) {
    case Reloc::Static:
      OS << "Static";
      break;
    case Reloc::PIC_:
      OS << "PIC_";
      break;
    case Reloc::DynamicNoPIC:
      OS << "DynamicNoPIC";
      break;
    case Reloc::ROPI:
      OS << "ROPI";
      break;
    case Reloc::RWPI:
      OS << "RWPI";
      break;
    case Reloc::ROPI_RWPI:
      OS << "ROPI_RWPI";
      break;
    }
  } else
    OS << "unspecified (will use target default)";

  OS << "\n"
     << Indent << "  Code Model = ";

  if (JTMB.CM) {
    switch (*JTMB.CM) {
    case CodeModel::Tiny:
      OS << "Tiny";
      break;
    case CodeModel::Small:
      OS << "Small";
      break;
    case CodeModel::Kernel:
      OS << "Kernel";
      break;
    case CodeModel::Medium:
      OS << "Medium";
      break;
    case CodeModel::Large:
      OS << "Large";
      break;
    }
  } else
    OS << "unspecified (will use target default)";

  OS << "\n"
     << Indent << "  Optimization Level = ";
  switch (JTMB.OptLevel) {
  case CodeGenOptLevel::None:
    OS << "None";
    break;
  case CodeGenOptLevel::Less:
    OS << "Less";
    break;
  case CodeGenOptLevel::Default:
    OS << "Default";
    break;
  case CodeGenOptLevel::Aggressive:
    OS << "Aggressive";
    break;
  }

  OS << "\n" << Indent << "}\n";
}
#endif // NDEBUG

} // End namespace orc.
} // End namespace llvm.
