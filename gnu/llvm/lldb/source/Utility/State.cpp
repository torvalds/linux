//===-- State.cpp ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/State.h"

using namespace lldb;
using namespace lldb_private;

const char *lldb_private::StateAsCString(StateType state) {
  switch (state) {
  case eStateInvalid:
    return "invalid";
  case eStateUnloaded:
    return "unloaded";
  case eStateConnected:
    return "connected";
  case eStateAttaching:
    return "attaching";
  case eStateLaunching:
    return "launching";
  case eStateStopped:
    return "stopped";
  case eStateRunning:
    return "running";
  case eStateStepping:
    return "stepping";
  case eStateCrashed:
    return "crashed";
  case eStateDetached:
    return "detached";
  case eStateExited:
    return "exited";
  case eStateSuspended:
    return "suspended";
  }
  return "unknown";
}

const char *lldb_private::GetPermissionsAsCString(uint32_t permissions) {
  switch (permissions) {
  case 0:
    return "---";
  case ePermissionsWritable:
    return "-w-";
  case ePermissionsReadable:
    return "r--";
  case ePermissionsExecutable:
    return "--x";
  case ePermissionsReadable | ePermissionsWritable:
    return "rw-";
  case ePermissionsReadable | ePermissionsExecutable:
    return "r-x";
  case ePermissionsWritable | ePermissionsExecutable:
    return "-wx";
  case ePermissionsReadable | ePermissionsWritable | ePermissionsExecutable:
    return "rwx";
  default:
    break;
  }
  return "???";
}

bool lldb_private::StateIsRunningState(StateType state) {
  switch (state) {
  case eStateAttaching:
  case eStateLaunching:
  case eStateRunning:
  case eStateStepping:
    return true;

  case eStateConnected:
  case eStateDetached:
  case eStateInvalid:
  case eStateUnloaded:
  case eStateStopped:
  case eStateCrashed:
  case eStateExited:
  case eStateSuspended:
    break;
  }
  return false;
}

bool lldb_private::StateIsStoppedState(StateType state, bool must_exist) {
  switch (state) {
  case eStateInvalid:
  case eStateConnected:
  case eStateAttaching:
  case eStateLaunching:
  case eStateRunning:
  case eStateStepping:
  case eStateDetached:
    break;

  case eStateUnloaded:
  case eStateExited:
    return !must_exist;

  case eStateStopped:
  case eStateCrashed:
  case eStateSuspended:
    return true;
  }
  return false;
}
