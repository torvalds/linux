//===-- SBModuleSpec.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBModuleSpec.h"
#include "Utils.h"
#include "lldb/API/SBStream.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Host/Host.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Utility/Instrumentation.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;

SBModuleSpec::SBModuleSpec() : m_opaque_up(new lldb_private::ModuleSpec()) {
  LLDB_INSTRUMENT_VA(this);
}

SBModuleSpec::SBModuleSpec(const SBModuleSpec &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  m_opaque_up = clone(rhs.m_opaque_up);
}

SBModuleSpec::SBModuleSpec(const lldb_private::ModuleSpec &module_spec)
    : m_opaque_up(new lldb_private::ModuleSpec(module_spec)) {
  LLDB_INSTRUMENT_VA(this, module_spec);
}

const SBModuleSpec &SBModuleSpec::operator=(const SBModuleSpec &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs)
    m_opaque_up = clone(rhs.m_opaque_up);
  return *this;
}

SBModuleSpec::~SBModuleSpec() = default;

bool SBModuleSpec::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBModuleSpec::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up->operator bool();
}

void SBModuleSpec::Clear() {
  LLDB_INSTRUMENT_VA(this);

  m_opaque_up->Clear();
}

SBFileSpec SBModuleSpec::GetFileSpec() {
  LLDB_INSTRUMENT_VA(this);

  SBFileSpec sb_spec(m_opaque_up->GetFileSpec());
  return sb_spec;
}

void SBModuleSpec::SetFileSpec(const lldb::SBFileSpec &sb_spec) {
  LLDB_INSTRUMENT_VA(this, sb_spec);

  m_opaque_up->GetFileSpec() = *sb_spec;
}

lldb::SBFileSpec SBModuleSpec::GetPlatformFileSpec() {
  LLDB_INSTRUMENT_VA(this);

  return SBFileSpec(m_opaque_up->GetPlatformFileSpec());
}

void SBModuleSpec::SetPlatformFileSpec(const lldb::SBFileSpec &sb_spec) {
  LLDB_INSTRUMENT_VA(this, sb_spec);

  m_opaque_up->GetPlatformFileSpec() = *sb_spec;
}

lldb::SBFileSpec SBModuleSpec::GetSymbolFileSpec() {
  LLDB_INSTRUMENT_VA(this);

  return SBFileSpec(m_opaque_up->GetSymbolFileSpec());
}

void SBModuleSpec::SetSymbolFileSpec(const lldb::SBFileSpec &sb_spec) {
  LLDB_INSTRUMENT_VA(this, sb_spec);

  m_opaque_up->GetSymbolFileSpec() = *sb_spec;
}

const char *SBModuleSpec::GetObjectName() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up->GetObjectName().GetCString();
}

void SBModuleSpec::SetObjectName(const char *name) {
  LLDB_INSTRUMENT_VA(this, name);

  m_opaque_up->GetObjectName().SetCString(name);
}

const char *SBModuleSpec::GetTriple() {
  LLDB_INSTRUMENT_VA(this);

  std::string triple(m_opaque_up->GetArchitecture().GetTriple().str());
  // Unique the string so we don't run into ownership issues since the const
  // strings put the string into the string pool once and the strings never
  // comes out
  ConstString const_triple(triple.c_str());
  return const_triple.GetCString();
}

void SBModuleSpec::SetTriple(const char *triple) {
  LLDB_INSTRUMENT_VA(this, triple);

  m_opaque_up->GetArchitecture().SetTriple(triple);
}

const uint8_t *SBModuleSpec::GetUUIDBytes() {
  LLDB_INSTRUMENT_VA(this)
  return m_opaque_up->GetUUID().GetBytes().data();
}

size_t SBModuleSpec::GetUUIDLength() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up->GetUUID().GetBytes().size();
}

bool SBModuleSpec::SetUUIDBytes(const uint8_t *uuid, size_t uuid_len) {
  LLDB_INSTRUMENT_VA(this, uuid, uuid_len)
  m_opaque_up->GetUUID() = UUID(uuid, uuid_len);
  return m_opaque_up->GetUUID().IsValid();
}

bool SBModuleSpec::GetDescription(lldb::SBStream &description) {
  LLDB_INSTRUMENT_VA(this, description);

  m_opaque_up->Dump(description.ref());
  return true;
}

uint64_t SBModuleSpec::GetObjectOffset() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up->GetObjectOffset();
}

void SBModuleSpec::SetObjectOffset(uint64_t object_offset) {
  LLDB_INSTRUMENT_VA(this, object_offset);

  m_opaque_up->SetObjectOffset(object_offset);
}

uint64_t SBModuleSpec::GetObjectSize() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up->GetObjectSize();
}

void SBModuleSpec::SetObjectSize(uint64_t object_size) {
  LLDB_INSTRUMENT_VA(this, object_size);

  m_opaque_up->SetObjectSize(object_size);
}

SBModuleSpecList::SBModuleSpecList() : m_opaque_up(new ModuleSpecList()) {
  LLDB_INSTRUMENT_VA(this);
}

SBModuleSpecList::SBModuleSpecList(const SBModuleSpecList &rhs)
    : m_opaque_up(new ModuleSpecList(*rhs.m_opaque_up)) {
  LLDB_INSTRUMENT_VA(this, rhs);
}

SBModuleSpecList &SBModuleSpecList::operator=(const SBModuleSpecList &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs)
    *m_opaque_up = *rhs.m_opaque_up;
  return *this;
}

SBModuleSpecList::~SBModuleSpecList() = default;

SBModuleSpecList SBModuleSpecList::GetModuleSpecifications(const char *path) {
  LLDB_INSTRUMENT_VA(path);

  SBModuleSpecList specs;
  FileSpec file_spec(path);
  FileSystem::Instance().Resolve(file_spec);
  Host::ResolveExecutableInBundle(file_spec);
  ObjectFile::GetModuleSpecifications(file_spec, 0, 0, *specs.m_opaque_up);
  return specs;
}

void SBModuleSpecList::Append(const SBModuleSpec &spec) {
  LLDB_INSTRUMENT_VA(this, spec);

  m_opaque_up->Append(*spec.m_opaque_up);
}

void SBModuleSpecList::Append(const SBModuleSpecList &spec_list) {
  LLDB_INSTRUMENT_VA(this, spec_list);

  m_opaque_up->Append(*spec_list.m_opaque_up);
}

size_t SBModuleSpecList::GetSize() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up->GetSize();
}

SBModuleSpec SBModuleSpecList::GetSpecAtIndex(size_t i) {
  LLDB_INSTRUMENT_VA(this, i);

  SBModuleSpec sb_module_spec;
  m_opaque_up->GetModuleSpecAtIndex(i, *sb_module_spec.m_opaque_up);
  return sb_module_spec;
}

SBModuleSpec
SBModuleSpecList::FindFirstMatchingSpec(const SBModuleSpec &match_spec) {
  LLDB_INSTRUMENT_VA(this, match_spec);

  SBModuleSpec sb_module_spec;
  m_opaque_up->FindMatchingModuleSpec(*match_spec.m_opaque_up,
                                      *sb_module_spec.m_opaque_up);
  return sb_module_spec;
}

SBModuleSpecList
SBModuleSpecList::FindMatchingSpecs(const SBModuleSpec &match_spec) {
  LLDB_INSTRUMENT_VA(this, match_spec);

  SBModuleSpecList specs;
  m_opaque_up->FindMatchingModuleSpecs(*match_spec.m_opaque_up,
                                       *specs.m_opaque_up);
  return specs;
}

bool SBModuleSpecList::GetDescription(lldb::SBStream &description) {
  LLDB_INSTRUMENT_VA(this, description);

  m_opaque_up->Dump(description.ref());
  return true;
}
