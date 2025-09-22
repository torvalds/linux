//===-- SBAddressRange.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBAddressRange.h"
#include "Utils.h"
#include "lldb/API/SBAddress.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBTarget.h"
#include "lldb/Core/AddressRange.h"
#include "lldb/Core/Section.h"
#include "lldb/Utility/Instrumentation.h"
#include "lldb/Utility/Stream.h"
#include <cstddef>
#include <memory>

using namespace lldb;
using namespace lldb_private;

SBAddressRange::SBAddressRange()
    : m_opaque_up(std::make_unique<AddressRange>()) {
  LLDB_INSTRUMENT_VA(this);
}

SBAddressRange::SBAddressRange(const SBAddressRange &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  m_opaque_up = clone(rhs.m_opaque_up);
}

SBAddressRange::SBAddressRange(lldb::SBAddress addr, lldb::addr_t byte_size)
    : m_opaque_up(std::make_unique<AddressRange>(addr.ref(), byte_size)) {
  LLDB_INSTRUMENT_VA(this, addr, byte_size);
}

SBAddressRange::~SBAddressRange() = default;

const SBAddressRange &SBAddressRange::operator=(const SBAddressRange &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs)
    m_opaque_up = clone(rhs.m_opaque_up);
  return *this;
}

bool SBAddressRange::operator==(const SBAddressRange &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  return ref().operator==(rhs.ref());
}

bool SBAddressRange::operator!=(const SBAddressRange &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  return !(*this == rhs);
}

void SBAddressRange::Clear() {
  LLDB_INSTRUMENT_VA(this);

  ref().Clear();
}

bool SBAddressRange::IsValid() const {
  LLDB_INSTRUMENT_VA(this);

  return ref().IsValid();
}

lldb::SBAddress SBAddressRange::GetBaseAddress() const {
  LLDB_INSTRUMENT_VA(this);

  return lldb::SBAddress(ref().GetBaseAddress());
}

lldb::addr_t SBAddressRange::GetByteSize() const {
  LLDB_INSTRUMENT_VA(this);

  return ref().GetByteSize();
}

bool SBAddressRange::GetDescription(SBStream &description,
                                    const SBTarget target) {
  LLDB_INSTRUMENT_VA(this, description, target);

  return ref().GetDescription(&description.ref(), target.GetSP().get());
}

lldb_private::AddressRange &SBAddressRange::ref() const {
  assert(m_opaque_up && "opaque pointer must always be valid");
  return *m_opaque_up;
}
