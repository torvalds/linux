//===-- DNBThreadResumeActions.cpp ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Created by Greg Clayton on 03/13/2010
//
//===----------------------------------------------------------------------===//

#include "DNBThreadResumeActions.h"

DNBThreadResumeActions::DNBThreadResumeActions()
    : m_actions(), m_signal_handled() {}

DNBThreadResumeActions::DNBThreadResumeActions(
    const DNBThreadResumeAction *actions, size_t num_actions)
    : m_actions(), m_signal_handled() {
  if (actions && num_actions) {
    m_actions.assign(actions, actions + num_actions);
    m_signal_handled.assign(num_actions, false);
  }
}

DNBThreadResumeActions::DNBThreadResumeActions(nub_state_t default_action,
                                               int signal)
    : m_actions(), m_signal_handled() {
  SetDefaultThreadActionIfNeeded(default_action, signal);
}

void DNBThreadResumeActions::Append(const DNBThreadResumeAction &action) {
  m_actions.push_back(action);
  m_signal_handled.push_back(false);
}

void DNBThreadResumeActions::AppendAction(nub_thread_t tid, nub_state_t state,
                                          int signal, nub_addr_t addr) {
  DNBThreadResumeAction action = {tid, state, signal, addr};
  Append(action);
}

const DNBThreadResumeAction *
DNBThreadResumeActions::GetActionForThread(nub_thread_t tid,
                                           bool default_ok) const {
  const size_t num_actions = m_actions.size();
  for (size_t i = 0; i < num_actions; ++i) {
    if (m_actions[i].tid == tid)
      return &m_actions[i];
  }
  if (default_ok && tid != INVALID_NUB_THREAD)
    return GetActionForThread(INVALID_NUB_THREAD, false);
  return NULL;
}

size_t DNBThreadResumeActions::NumActionsWithState(nub_state_t state) const {
  size_t count = 0;
  const size_t num_actions = m_actions.size();
  for (size_t i = 0; i < num_actions; ++i) {
    if (m_actions[i].state == state)
      ++count;
  }
  return count;
}

bool DNBThreadResumeActions::SetDefaultThreadActionIfNeeded(nub_state_t action,
                                                            int signal) {
  if (GetActionForThread(INVALID_NUB_THREAD, true) == NULL) {
    // There isn't a default action so we do need to set it.
    DNBThreadResumeAction default_action = {INVALID_NUB_THREAD, action, signal,
                                            INVALID_NUB_ADDRESS};
    m_actions.push_back(default_action);
    m_signal_handled.push_back(false);
    return true; // Return true as we did add the default action
  }
  return false;
}

void DNBThreadResumeActions::SetSignalHandledForThread(nub_thread_t tid) const {
  if (tid != INVALID_NUB_THREAD) {
    const size_t num_actions = m_actions.size();
    for (size_t i = 0; i < num_actions; ++i) {
      if (m_actions[i].tid == tid)
        m_signal_handled[i] = true;
    }
  }
}
