//===-- SBExpressionOptions.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBExpressionOptions.h"
#include "Utils.h"
#include "lldb/API/SBStream.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/Instrumentation.h"

using namespace lldb;
using namespace lldb_private;

SBExpressionOptions::SBExpressionOptions()
    : m_opaque_up(new EvaluateExpressionOptions()) {
  LLDB_INSTRUMENT_VA(this);
}

SBExpressionOptions::SBExpressionOptions(const SBExpressionOptions &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  m_opaque_up = clone(rhs.m_opaque_up);
}

const SBExpressionOptions &SBExpressionOptions::
operator=(const SBExpressionOptions &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs)
    m_opaque_up = clone(rhs.m_opaque_up);
  return *this;
}

SBExpressionOptions::~SBExpressionOptions() = default;

bool SBExpressionOptions::GetCoerceResultToId() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up->DoesCoerceToId();
}

void SBExpressionOptions::SetCoerceResultToId(bool coerce) {
  LLDB_INSTRUMENT_VA(this, coerce);

  m_opaque_up->SetCoerceToId(coerce);
}

bool SBExpressionOptions::GetUnwindOnError() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up->DoesUnwindOnError();
}

void SBExpressionOptions::SetUnwindOnError(bool unwind) {
  LLDB_INSTRUMENT_VA(this, unwind);

  m_opaque_up->SetUnwindOnError(unwind);
}

bool SBExpressionOptions::GetIgnoreBreakpoints() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up->DoesIgnoreBreakpoints();
}

void SBExpressionOptions::SetIgnoreBreakpoints(bool ignore) {
  LLDB_INSTRUMENT_VA(this, ignore);

  m_opaque_up->SetIgnoreBreakpoints(ignore);
}

lldb::DynamicValueType SBExpressionOptions::GetFetchDynamicValue() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up->GetUseDynamic();
}

void SBExpressionOptions::SetFetchDynamicValue(lldb::DynamicValueType dynamic) {
  LLDB_INSTRUMENT_VA(this, dynamic);

  m_opaque_up->SetUseDynamic(dynamic);
}

uint32_t SBExpressionOptions::GetTimeoutInMicroSeconds() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up->GetTimeout() ? m_opaque_up->GetTimeout()->count() : 0;
}

void SBExpressionOptions::SetTimeoutInMicroSeconds(uint32_t timeout) {
  LLDB_INSTRUMENT_VA(this, timeout);

  m_opaque_up->SetTimeout(timeout == 0 ? Timeout<std::micro>(std::nullopt)
                                       : std::chrono::microseconds(timeout));
}

uint32_t SBExpressionOptions::GetOneThreadTimeoutInMicroSeconds() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up->GetOneThreadTimeout()
             ? m_opaque_up->GetOneThreadTimeout()->count()
             : 0;
}

void SBExpressionOptions::SetOneThreadTimeoutInMicroSeconds(uint32_t timeout) {
  LLDB_INSTRUMENT_VA(this, timeout);

  m_opaque_up->SetOneThreadTimeout(timeout == 0
                                       ? Timeout<std::micro>(std::nullopt)
                                       : std::chrono::microseconds(timeout));
}

bool SBExpressionOptions::GetTryAllThreads() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up->GetTryAllThreads();
}

void SBExpressionOptions::SetTryAllThreads(bool run_others) {
  LLDB_INSTRUMENT_VA(this, run_others);

  m_opaque_up->SetTryAllThreads(run_others);
}

bool SBExpressionOptions::GetStopOthers() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up->GetStopOthers();
}

void SBExpressionOptions::SetStopOthers(bool run_others) {
  LLDB_INSTRUMENT_VA(this, run_others);

  m_opaque_up->SetStopOthers(run_others);
}

bool SBExpressionOptions::GetTrapExceptions() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up->GetTrapExceptions();
}

void SBExpressionOptions::SetTrapExceptions(bool trap_exceptions) {
  LLDB_INSTRUMENT_VA(this, trap_exceptions);

  m_opaque_up->SetTrapExceptions(trap_exceptions);
}

void SBExpressionOptions::SetLanguage(lldb::LanguageType language) {
  LLDB_INSTRUMENT_VA(this, language);

  m_opaque_up->SetLanguage(language);
}

void SBExpressionOptions::SetLanguage(lldb::SBSourceLanguageName name,
                                      uint32_t version) {
  LLDB_INSTRUMENT_VA(this, name, version);

  m_opaque_up->SetLanguage(name, version);
}

void SBExpressionOptions::SetCancelCallback(
    lldb::ExpressionCancelCallback callback, void *baton) {
  LLDB_INSTRUMENT_VA(this, callback, baton);

  m_opaque_up->SetCancelCallback(callback, baton);
}

bool SBExpressionOptions::GetGenerateDebugInfo() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up->GetGenerateDebugInfo();
}

void SBExpressionOptions::SetGenerateDebugInfo(bool b) {
  LLDB_INSTRUMENT_VA(this, b);

  return m_opaque_up->SetGenerateDebugInfo(b);
}

bool SBExpressionOptions::GetSuppressPersistentResult() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up->GetSuppressPersistentResult();
}

void SBExpressionOptions::SetSuppressPersistentResult(bool b) {
  LLDB_INSTRUMENT_VA(this, b);

  return m_opaque_up->SetSuppressPersistentResult(b);
}

const char *SBExpressionOptions::GetPrefix() const {
  LLDB_INSTRUMENT_VA(this);

  return ConstString(m_opaque_up->GetPrefix()).GetCString();
}

void SBExpressionOptions::SetPrefix(const char *prefix) {
  LLDB_INSTRUMENT_VA(this, prefix);

  return m_opaque_up->SetPrefix(prefix);
}

bool SBExpressionOptions::GetAutoApplyFixIts() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up->GetAutoApplyFixIts();
}

void SBExpressionOptions::SetAutoApplyFixIts(bool b) {
  LLDB_INSTRUMENT_VA(this, b);

  return m_opaque_up->SetAutoApplyFixIts(b);
}

uint64_t SBExpressionOptions::GetRetriesWithFixIts() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up->GetRetriesWithFixIts();
}

void SBExpressionOptions::SetRetriesWithFixIts(uint64_t retries) {
  LLDB_INSTRUMENT_VA(this, retries);

  return m_opaque_up->SetRetriesWithFixIts(retries);
}

bool SBExpressionOptions::GetTopLevel() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up->GetExecutionPolicy() == eExecutionPolicyTopLevel;
}

void SBExpressionOptions::SetTopLevel(bool b) {
  LLDB_INSTRUMENT_VA(this, b);

  m_opaque_up->SetExecutionPolicy(b ? eExecutionPolicyTopLevel
                                    : m_opaque_up->default_execution_policy);
}

bool SBExpressionOptions::GetAllowJIT() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up->GetExecutionPolicy() != eExecutionPolicyNever;
}

void SBExpressionOptions::SetAllowJIT(bool allow) {
  LLDB_INSTRUMENT_VA(this, allow);

  m_opaque_up->SetExecutionPolicy(allow ? m_opaque_up->default_execution_policy
                                        : eExecutionPolicyNever);
}

EvaluateExpressionOptions *SBExpressionOptions::get() const {
  return m_opaque_up.get();
}

EvaluateExpressionOptions &SBExpressionOptions::ref() const {
  return *m_opaque_up;
}
