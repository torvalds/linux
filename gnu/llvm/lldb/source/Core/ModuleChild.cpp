//===-- ModuleChild.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/ModuleChild.h"

using namespace lldb_private;

ModuleChild::ModuleChild(const lldb::ModuleSP &module_sp)
    : m_module_wp(module_sp) {}

ModuleChild::~ModuleChild() = default;

const ModuleChild &ModuleChild::operator=(const ModuleChild &rhs) {
  if (this != &rhs)
    m_module_wp = rhs.m_module_wp;
  return *this;
}

lldb::ModuleSP ModuleChild::GetModule() const { return m_module_wp.lock(); }

void ModuleChild::SetModule(const lldb::ModuleSP &module_sp) {
  m_module_wp = module_sp;
}
