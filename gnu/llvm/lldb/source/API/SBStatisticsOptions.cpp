//===-- SBStatisticsOptions.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBStatisticsOptions.h"
#include "lldb/Target/Statistics.h"
#include "lldb/Utility/Instrumentation.h"

#include "Utils.h"

using namespace lldb;
using namespace lldb_private;

SBStatisticsOptions::SBStatisticsOptions()
    : m_opaque_up(new StatisticsOptions()) {
  LLDB_INSTRUMENT_VA(this);
}

SBStatisticsOptions::SBStatisticsOptions(const SBStatisticsOptions &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  m_opaque_up = clone(rhs.m_opaque_up);
}

SBStatisticsOptions::~SBStatisticsOptions() = default;

const SBStatisticsOptions &
SBStatisticsOptions::operator=(const SBStatisticsOptions &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs)
    m_opaque_up = clone(rhs.m_opaque_up);
  return *this;
}

void SBStatisticsOptions::SetSummaryOnly(bool b) {
  m_opaque_up->SetSummaryOnly(b);
}

bool SBStatisticsOptions::GetSummaryOnly() {
  return m_opaque_up->GetSummaryOnly();
}

void SBStatisticsOptions::SetIncludeTargets(bool b) {
  m_opaque_up->SetIncludeTargets(b);
}

bool SBStatisticsOptions::GetIncludeTargets() const {
  return m_opaque_up->GetIncludeTargets();
}

void SBStatisticsOptions::SetIncludeModules(bool b) {
  m_opaque_up->SetIncludeModules(b);
}

bool SBStatisticsOptions::GetIncludeModules() const {
  return m_opaque_up->GetIncludeModules();
}

void SBStatisticsOptions::SetIncludeTranscript(bool b) {
  m_opaque_up->SetIncludeTranscript(b);
}

bool SBStatisticsOptions::GetIncludeTranscript() const {
  return m_opaque_up->GetIncludeTranscript();
}

void SBStatisticsOptions::SetReportAllAvailableDebugInfo(bool b) {
  m_opaque_up->SetLoadAllDebugInfo(b);
}

bool SBStatisticsOptions::GetReportAllAvailableDebugInfo() {
  return m_opaque_up->GetLoadAllDebugInfo();
}

const lldb_private::StatisticsOptions &SBStatisticsOptions::ref() const {
  return *m_opaque_up;
}
