//===------ EPCEHFrameRegistrar.cpp - EPC-based eh-frame registration -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/EPCEHFrameRegistrar.h"

#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/Shared/OrcRTBridge.h"

using namespace llvm::orc::shared;

namespace llvm {
namespace orc {

Expected<std::unique_ptr<EPCEHFrameRegistrar>>
EPCEHFrameRegistrar::Create(ExecutionSession &ES) {

  // Lookup addresseses of the registration/deregistration functions in the
  // bootstrap map.
  ExecutorAddr RegisterEHFrameSectionWrapper;
  ExecutorAddr DeregisterEHFrameSectionWrapper;
  if (auto Err = ES.getExecutorProcessControl().getBootstrapSymbols(
          {{RegisterEHFrameSectionWrapper,
            rt::RegisterEHFrameSectionWrapperName},
           {DeregisterEHFrameSectionWrapper,
            rt::DeregisterEHFrameSectionWrapperName}}))
    return std::move(Err);

  return std::make_unique<EPCEHFrameRegistrar>(
      ES, RegisterEHFrameSectionWrapper, DeregisterEHFrameSectionWrapper);
}

Error EPCEHFrameRegistrar::registerEHFrames(ExecutorAddrRange EHFrameSection) {
  return ES.callSPSWrapper<void(SPSExecutorAddrRange)>(
      RegisterEHFrameSectionWrapper, EHFrameSection);
}

Error EPCEHFrameRegistrar::deregisterEHFrames(
    ExecutorAddrRange EHFrameSection) {
  return ES.callSPSWrapper<void(SPSExecutorAddrRange)>(
      DeregisterEHFrameSectionWrapper, EHFrameSection);
}

} // end namespace orc
} // end namespace llvm
