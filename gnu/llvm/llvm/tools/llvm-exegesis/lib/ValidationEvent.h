//===-- ValidationEvent.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Definitions and utilities for Validation Events.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_EXEGESIS_VALIDATIONEVENT_H
#define LLVM_TOOLS_LLVM_EXEGESIS_VALIDATIONEVENT_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

namespace llvm {

namespace exegesis {

// The main list of supported validation events. The mapping between validation
// events and pfm counters is defined in TableDef files for each target.
enum ValidationEvent {
  InstructionRetired,
  L1DCacheLoadMiss,
  L1DCacheStoreMiss,
  L1ICacheLoadMiss,
  DataTLBLoadMiss,
  DataTLBStoreMiss,
  InstructionTLBLoadMiss,
  BranchPredictionMiss,
  // Number of events.
  NumValidationEvents,
};

// Returns the name/description of the given event.
const char *getValidationEventName(ValidationEvent VE);
const char *getValidationEventDescription(ValidationEvent VE);

// Returns the ValidationEvent with the given name.
Expected<ValidationEvent> getValidationEventByName(StringRef Name);

// Command-line options for validation events.
struct ValidationEventOptions {
  template <class Opt> void apply(Opt &O) const {
    for (int I = 0; I < NumValidationEvents; ++I) {
      const auto VE = static_cast<ValidationEvent>(I);
      O.getParser().addLiteralOption(getValidationEventName(VE), VE,
                                     getValidationEventDescription(VE));
    }
  }
};

} // namespace exegesis
} // namespace llvm

#endif
