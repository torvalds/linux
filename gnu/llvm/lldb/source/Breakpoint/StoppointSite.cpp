//===-- StoppointSite.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Breakpoint/StoppointSite.h"


using namespace lldb;
using namespace lldb_private;

StoppointSite::StoppointSite(break_id_t id, addr_t addr, bool hardware)
    : m_id(id), m_addr(addr), m_is_hardware_required(hardware), m_byte_size(0),
      m_hit_counter() {}

StoppointSite::StoppointSite(break_id_t id, addr_t addr, uint32_t byte_size,
                             bool hardware)
    : m_id(id), m_addr(addr), m_is_hardware_required(hardware),
      m_byte_size(byte_size), m_hit_counter() {}
