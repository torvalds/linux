//===- InteractiveModelRunner.h ---- "gym" ML model runner  -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//

#ifndef LLVM_ANALYSIS_INTERACTIVEMODELRUNNER_H
#define LLVM_ANALYSIS_INTERACTIVEMODELRUNNER_H

#include "llvm/Analysis/MLModelRunner.h"
#include "llvm/Analysis/TensorSpec.h"
#include "llvm/Analysis/Utils/TrainingLogger.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include <system_error>

namespace llvm {

/// A MLModelRunner that asks for advice from an external agent, or host. It
/// uses 2 files - ideally named pipes - one to send data to that agent, and
/// one to receive advice.
/// The data exchange uses the training logger (Utils/TrainingLogger.h) format.
/// Specifically, the compiler will send the log header, set the context, and
/// send observations; the host is expected to reply with a tensor value after
/// each observation as a binary buffer that's conforming to the shape of the
/// advice. Interleaved, the data closely resembles the training log for a
/// log where we don't capture the reward signal.
///
/// Note that the correctness of the received data is the responsibility of the
/// host. In particular, if insufficient data were sent, the compiler will block
/// when waiting for an advice.
///
/// Note that the host can either open the pipes RW, or open first the pipe to
/// the compiler - i.e. the "Inbound" - and then the "Outbound", to avoid
/// deadlock. This is because the compiler first tries to open the inbound
/// (which will hang until there's a writer on the other end).
class InteractiveModelRunner : public MLModelRunner {
public:
  InteractiveModelRunner(LLVMContext &Ctx,
                         const std::vector<TensorSpec> &Inputs,
                         const TensorSpec &Advice, StringRef OutboundName,
                         StringRef InboundName);

  static bool classof(const MLModelRunner *R) {
    return R->getKind() == MLModelRunner::Kind::Interactive;
  }
  void switchContext(StringRef Name) override {
    Log->switchContext(Name);
    Log->flush();
  }

  virtual ~InteractiveModelRunner();

private:
  void *evaluateUntyped() override;
  // This must be declared before InEC if we want to initialize it in the
  // ctor initializer list.
  int Inbound = -1;
  const std::vector<TensorSpec> InputSpecs;
  const TensorSpec OutputSpec;
  std::error_code OutEC;
  std::error_code InEC;
  std::vector<char> OutputBuffer;
  std::unique_ptr<Logger> Log;
};
} // namespace llvm
#endif // LLVM_ANALYSIS_INTERACTIVEMODELRUNNER_H
