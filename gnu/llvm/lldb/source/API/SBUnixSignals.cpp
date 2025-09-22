//===-- SBUnixSignals.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/Platform.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/UnixSignals.h"
#include "lldb/Utility/Instrumentation.h"
#include "lldb/lldb-defines.h"

#include "lldb/API/SBUnixSignals.h"

using namespace lldb;
using namespace lldb_private;

SBUnixSignals::SBUnixSignals() { LLDB_INSTRUMENT_VA(this); }

SBUnixSignals::SBUnixSignals(const SBUnixSignals &rhs)
    : m_opaque_wp(rhs.m_opaque_wp) {
  LLDB_INSTRUMENT_VA(this, rhs);
}

SBUnixSignals::SBUnixSignals(ProcessSP &process_sp)
    : m_opaque_wp(process_sp ? process_sp->GetUnixSignals() : nullptr) {}

SBUnixSignals::SBUnixSignals(PlatformSP &platform_sp)
    : m_opaque_wp(platform_sp ? platform_sp->GetUnixSignals() : nullptr) {}

const SBUnixSignals &SBUnixSignals::operator=(const SBUnixSignals &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs)
    m_opaque_wp = rhs.m_opaque_wp;
  return *this;
}

SBUnixSignals::~SBUnixSignals() = default;

UnixSignalsSP SBUnixSignals::GetSP() const { return m_opaque_wp.lock(); }

void SBUnixSignals::SetSP(const UnixSignalsSP &signals_sp) {
  m_opaque_wp = signals_sp;
}

void SBUnixSignals::Clear() {
  LLDB_INSTRUMENT_VA(this);

  m_opaque_wp.reset();
}

bool SBUnixSignals::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBUnixSignals::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  return static_cast<bool>(GetSP());
}

const char *SBUnixSignals::GetSignalAsCString(int32_t signo) const {
  LLDB_INSTRUMENT_VA(this, signo);

  if (auto signals_sp = GetSP())
    return ConstString(signals_sp->GetSignalAsStringRef(signo)).GetCString();

  return nullptr;
}

int32_t SBUnixSignals::GetSignalNumberFromName(const char *name) const {
  LLDB_INSTRUMENT_VA(this, name);

  if (auto signals_sp = GetSP())
    return signals_sp->GetSignalNumberFromName(name);

  return LLDB_INVALID_SIGNAL_NUMBER;
}

bool SBUnixSignals::GetShouldSuppress(int32_t signo) const {
  LLDB_INSTRUMENT_VA(this, signo);

  if (auto signals_sp = GetSP())
    return signals_sp->GetShouldSuppress(signo);

  return false;
}

bool SBUnixSignals::SetShouldSuppress(int32_t signo, bool value) {
  LLDB_INSTRUMENT_VA(this, signo, value);

  auto signals_sp = GetSP();

  if (signals_sp)
    return signals_sp->SetShouldSuppress(signo, value);

  return false;
}

bool SBUnixSignals::GetShouldStop(int32_t signo) const {
  LLDB_INSTRUMENT_VA(this, signo);

  if (auto signals_sp = GetSP())
    return signals_sp->GetShouldStop(signo);

  return false;
}

bool SBUnixSignals::SetShouldStop(int32_t signo, bool value) {
  LLDB_INSTRUMENT_VA(this, signo, value);

  auto signals_sp = GetSP();

  if (signals_sp)
    return signals_sp->SetShouldStop(signo, value);

  return false;
}

bool SBUnixSignals::GetShouldNotify(int32_t signo) const {
  LLDB_INSTRUMENT_VA(this, signo);

  if (auto signals_sp = GetSP())
    return signals_sp->GetShouldNotify(signo);

  return false;
}

bool SBUnixSignals::SetShouldNotify(int32_t signo, bool value) {
  LLDB_INSTRUMENT_VA(this, signo, value);

  auto signals_sp = GetSP();

  if (signals_sp)
    return signals_sp->SetShouldNotify(signo, value);

  return false;
}

int32_t SBUnixSignals::GetNumSignals() const {
  LLDB_INSTRUMENT_VA(this);

  if (auto signals_sp = GetSP())
    return signals_sp->GetNumSignals();

  return -1;
}

int32_t SBUnixSignals::GetSignalAtIndex(int32_t index) const {
  LLDB_INSTRUMENT_VA(this, index);

  if (auto signals_sp = GetSP())
    return signals_sp->GetSignalAtIndex(index);

  return LLDB_INVALID_SIGNAL_NUMBER;
}
