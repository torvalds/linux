//===-- SBCommandInterpreterRunOptions.cpp --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/lldb-types.h"

#include "lldb/Utility/Instrumentation.h"

#include "lldb/API/SBCommandInterpreterRunOptions.h"
#include "lldb/Interpreter/CommandInterpreter.h"

#include <memory>

using namespace lldb;
using namespace lldb_private;

SBCommandInterpreterRunOptions::SBCommandInterpreterRunOptions() {
  LLDB_INSTRUMENT_VA(this);

  m_opaque_up = std::make_unique<CommandInterpreterRunOptions>();
}

SBCommandInterpreterRunOptions::SBCommandInterpreterRunOptions(
    const SBCommandInterpreterRunOptions &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  m_opaque_up = std::make_unique<CommandInterpreterRunOptions>(rhs.ref());
}

SBCommandInterpreterRunOptions::~SBCommandInterpreterRunOptions() = default;

SBCommandInterpreterRunOptions &SBCommandInterpreterRunOptions::operator=(
    const SBCommandInterpreterRunOptions &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this == &rhs)
    return *this;
  *m_opaque_up = *rhs.m_opaque_up;
  return *this;
}

bool SBCommandInterpreterRunOptions::GetStopOnContinue() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up->GetStopOnContinue();
}

void SBCommandInterpreterRunOptions::SetStopOnContinue(bool stop_on_continue) {
  LLDB_INSTRUMENT_VA(this, stop_on_continue);

  m_opaque_up->SetStopOnContinue(stop_on_continue);
}

bool SBCommandInterpreterRunOptions::GetStopOnError() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up->GetStopOnError();
}

void SBCommandInterpreterRunOptions::SetStopOnError(bool stop_on_error) {
  LLDB_INSTRUMENT_VA(this, stop_on_error);

  m_opaque_up->SetStopOnError(stop_on_error);
}

bool SBCommandInterpreterRunOptions::GetStopOnCrash() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up->GetStopOnCrash();
}

void SBCommandInterpreterRunOptions::SetStopOnCrash(bool stop_on_crash) {
  LLDB_INSTRUMENT_VA(this, stop_on_crash);

  m_opaque_up->SetStopOnCrash(stop_on_crash);
}

bool SBCommandInterpreterRunOptions::GetEchoCommands() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up->GetEchoCommands();
}

void SBCommandInterpreterRunOptions::SetEchoCommands(bool echo_commands) {
  LLDB_INSTRUMENT_VA(this, echo_commands);

  m_opaque_up->SetEchoCommands(echo_commands);
}

bool SBCommandInterpreterRunOptions::GetEchoCommentCommands() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up->GetEchoCommentCommands();
}

void SBCommandInterpreterRunOptions::SetEchoCommentCommands(bool echo) {
  LLDB_INSTRUMENT_VA(this, echo);

  m_opaque_up->SetEchoCommentCommands(echo);
}

bool SBCommandInterpreterRunOptions::GetPrintResults() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up->GetPrintResults();
}

void SBCommandInterpreterRunOptions::SetPrintResults(bool print_results) {
  LLDB_INSTRUMENT_VA(this, print_results);

  m_opaque_up->SetPrintResults(print_results);
}

bool SBCommandInterpreterRunOptions::GetPrintErrors() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up->GetPrintErrors();
}

void SBCommandInterpreterRunOptions::SetPrintErrors(bool print_errors) {
  LLDB_INSTRUMENT_VA(this, print_errors);

  m_opaque_up->SetPrintErrors(print_errors);
}

bool SBCommandInterpreterRunOptions::GetAddToHistory() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up->GetAddToHistory();
}

void SBCommandInterpreterRunOptions::SetAddToHistory(bool add_to_history) {
  LLDB_INSTRUMENT_VA(this, add_to_history);

  m_opaque_up->SetAddToHistory(add_to_history);
}

bool SBCommandInterpreterRunOptions::GetAutoHandleEvents() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up->GetAutoHandleEvents();
}

void SBCommandInterpreterRunOptions::SetAutoHandleEvents(
    bool auto_handle_events) {
  LLDB_INSTRUMENT_VA(this, auto_handle_events);

  m_opaque_up->SetAutoHandleEvents(auto_handle_events);
}

bool SBCommandInterpreterRunOptions::GetSpawnThread() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up->GetSpawnThread();
}

void SBCommandInterpreterRunOptions::SetSpawnThread(bool spawn_thread) {
  LLDB_INSTRUMENT_VA(this, spawn_thread);

  m_opaque_up->SetSpawnThread(spawn_thread);
}

bool SBCommandInterpreterRunOptions::GetAllowRepeats() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up->GetAllowRepeats();
}

void SBCommandInterpreterRunOptions::SetAllowRepeats(bool allow_repeats) {
  LLDB_INSTRUMENT_VA(this, allow_repeats);

  m_opaque_up->SetAllowRepeats(allow_repeats);
}

lldb_private::CommandInterpreterRunOptions *
SBCommandInterpreterRunOptions::get() const {
  return m_opaque_up.get();
}

lldb_private::CommandInterpreterRunOptions &
SBCommandInterpreterRunOptions::ref() const {
  return *m_opaque_up;
}

SBCommandInterpreterRunResult::SBCommandInterpreterRunResult()
    : m_opaque_up(new CommandInterpreterRunResult())

{
  LLDB_INSTRUMENT_VA(this);
}

SBCommandInterpreterRunResult::SBCommandInterpreterRunResult(
    const SBCommandInterpreterRunResult &rhs)
    : m_opaque_up(new CommandInterpreterRunResult()) {
  LLDB_INSTRUMENT_VA(this, rhs);

  *m_opaque_up = *rhs.m_opaque_up;
}

SBCommandInterpreterRunResult::SBCommandInterpreterRunResult(
    const CommandInterpreterRunResult &rhs) {
  m_opaque_up = std::make_unique<CommandInterpreterRunResult>(rhs);
}

SBCommandInterpreterRunResult::~SBCommandInterpreterRunResult() = default;

SBCommandInterpreterRunResult &SBCommandInterpreterRunResult::operator=(
    const SBCommandInterpreterRunResult &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this == &rhs)
    return *this;
  *m_opaque_up = *rhs.m_opaque_up;
  return *this;
}

int SBCommandInterpreterRunResult::GetNumberOfErrors() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up->GetNumErrors();
}

lldb::CommandInterpreterResult
SBCommandInterpreterRunResult::GetResult() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up->GetResult();
}
