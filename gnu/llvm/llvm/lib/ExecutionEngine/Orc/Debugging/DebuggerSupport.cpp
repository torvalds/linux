//===------ DebuggerSupport.cpp - Utils for enabling debugger support -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/Debugging/DebuggerSupport.h"
#include "llvm/ExecutionEngine/Orc/DebugObjectManagerPlugin.h"
#include "llvm/ExecutionEngine/Orc/Debugging/DebuggerSupportPlugin.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"

#define DEBUG_TYPE "orc"

using namespace llvm;
using namespace llvm::orc;

namespace llvm::orc {

Error enableDebuggerSupport(LLJIT &J) {
  auto *ObjLinkingLayer = dyn_cast<ObjectLinkingLayer>(&J.getObjLinkingLayer());
  if (!ObjLinkingLayer)
    return make_error<StringError>("Cannot enable LLJIT debugger support: "
                                   "Debugger support requires JITLink",
                                   inconvertibleErrorCode());
  auto ProcessSymsJD = J.getProcessSymbolsJITDylib();
  if (!ProcessSymsJD)
    return make_error<StringError>("Cannot enable LLJIT debugger support: "
                                   "Process symbols are not available",
                                   inconvertibleErrorCode());

  auto &ES = J.getExecutionSession();
  const auto &TT = J.getTargetTriple();

  switch (TT.getObjectFormat()) {
  case Triple::ELF: {
    auto Registrar = createJITLoaderGDBRegistrar(ES);
    if (!Registrar)
      return Registrar.takeError();
    ObjLinkingLayer->addPlugin(std::make_unique<DebugObjectManagerPlugin>(
        ES, std::move(*Registrar), false, true));
    return Error::success();
  }
  case Triple::MachO: {
    auto DS = GDBJITDebugInfoRegistrationPlugin::Create(ES, *ProcessSymsJD, TT);
    if (!DS)
      return DS.takeError();
    ObjLinkingLayer->addPlugin(std::move(*DS));
    return Error::success();
  }
  default:
    return make_error<StringError>(
        "Cannot enable LLJIT debugger support: " +
            Triple::getObjectFormatTypeName(TT.getObjectFormat()) +
            " is not supported",
        inconvertibleErrorCode());
  }
}

} // namespace llvm::orc
