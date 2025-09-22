//===- DelayedDiagnostic.cpp - Delayed declarator diagnostics -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the DelayedDiagnostic class implementation, which
// is used to record diagnostics that are being conditionally produced
// during declarator parsing.
//
// This file also defines AccessedEntity.
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/DelayedDiagnostic.h"
#include <cstring>

using namespace clang;
using namespace sema;

DelayedDiagnostic
DelayedDiagnostic::makeAvailability(AvailabilityResult AR,
                                    ArrayRef<SourceLocation> Locs,
                                    const NamedDecl *ReferringDecl,
                                    const NamedDecl *OffendingDecl,
                                    const ObjCInterfaceDecl *UnknownObjCClass,
                                    const ObjCPropertyDecl  *ObjCProperty,
                                    StringRef Msg,
                                    bool ObjCPropertyAccess) {
  assert(!Locs.empty());
  DelayedDiagnostic DD;
  DD.Kind = Availability;
  DD.Triggered = false;
  DD.Loc = Locs.front();
  DD.AvailabilityData.ReferringDecl = ReferringDecl;
  DD.AvailabilityData.OffendingDecl = OffendingDecl;
  DD.AvailabilityData.UnknownObjCClass = UnknownObjCClass;
  DD.AvailabilityData.ObjCProperty = ObjCProperty;
  char *MessageData = nullptr;
  if (!Msg.empty()) {
    MessageData = new char [Msg.size()];
    memcpy(MessageData, Msg.data(), Msg.size());
  }
  DD.AvailabilityData.Message = MessageData;
  DD.AvailabilityData.MessageLen = Msg.size();

  DD.AvailabilityData.SelectorLocs = new SourceLocation[Locs.size()];
  memcpy(DD.AvailabilityData.SelectorLocs, Locs.data(),
         sizeof(SourceLocation) * Locs.size());
  DD.AvailabilityData.NumSelectorLocs = Locs.size();

  DD.AvailabilityData.AR = AR;
  DD.AvailabilityData.ObjCPropertyAccess = ObjCPropertyAccess;
  return DD;
}

void DelayedDiagnostic::Destroy() {
  switch (Kind) {
  case Access:
    getAccessData().~AccessedEntity();
    break;

  case Availability:
    delete[] AvailabilityData.Message;
    delete[] AvailabilityData.SelectorLocs;
    break;

  case ForbiddenType:
    break;
  }
}
