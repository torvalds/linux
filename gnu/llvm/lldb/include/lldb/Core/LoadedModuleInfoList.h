//===-- LoadedModuleInfoList.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_LOADEDMODULEINFOLIST_H
#define LLDB_CORE_LOADEDMODULEINFOLIST_H

#include <cassert>
#include <string>
#include <vector>

#include "lldb/lldb-defines.h"
#include "lldb/lldb-private-forward.h"
#include "lldb/lldb-types.h"

namespace lldb_private {
class LoadedModuleInfoList {
public:
  class LoadedModuleInfo {
  public:
    enum e_data_point {
      e_has_name = 0,
      e_has_base,
      e_has_dynamic,
      e_has_link_map,
      e_num
    };

    LoadedModuleInfo() {
      for (uint32_t i = 0; i < e_num; ++i)
        m_has[i] = false;
    };

    void set_name(const std::string &name) {
      m_name = name;
      m_has[e_has_name] = true;
    }
    bool get_name(std::string &out) const {
      out = m_name;
      return m_has[e_has_name];
    }

    void set_base(const lldb::addr_t base) {
      m_base = base;
      m_has[e_has_base] = true;
    }
    bool get_base(lldb::addr_t &out) const {
      out = m_base;
      return m_has[e_has_base];
    }

    void set_base_is_offset(bool is_offset) { m_base_is_offset = is_offset; }
    bool get_base_is_offset(bool &out) const {
      out = m_base_is_offset;
      return m_has[e_has_base];
    }

    void set_link_map(const lldb::addr_t addr) {
      m_link_map = addr;
      m_has[e_has_link_map] = true;
    }
    bool get_link_map(lldb::addr_t &out) const {
      out = m_link_map;
      return m_has[e_has_link_map];
    }

    void set_dynamic(const lldb::addr_t addr) {
      m_dynamic = addr;
      m_has[e_has_dynamic] = true;
    }
    bool get_dynamic(lldb::addr_t &out) const {
      out = m_dynamic;
      return m_has[e_has_dynamic];
    }

    bool has_info(e_data_point datum) const {
      assert(datum < e_num);
      return m_has[datum];
    }

    bool operator==(LoadedModuleInfo const &rhs) const {
      for (size_t i = 0; i < e_num; ++i) {
        if (m_has[i] != rhs.m_has[i])
          return false;
      }

      return (m_base == rhs.m_base) && (m_link_map == rhs.m_link_map) &&
             (m_dynamic == rhs.m_dynamic) && (m_name == rhs.m_name);
    }

  protected:
    bool m_has[e_num];
    std::string m_name;
    lldb::addr_t m_link_map = LLDB_INVALID_ADDRESS;
    lldb::addr_t m_base = LLDB_INVALID_ADDRESS;
    bool m_base_is_offset = false;
    lldb::addr_t m_dynamic = LLDB_INVALID_ADDRESS;
  };

  LoadedModuleInfoList() = default;

  void add(const LoadedModuleInfo &mod) { m_list.push_back(mod); }

  void clear() { m_list.clear(); }

  std::vector<LoadedModuleInfo> m_list;
  lldb::addr_t m_link_map = LLDB_INVALID_ADDRESS;
};
} // namespace lldb_private

#endif // LLDB_CORE_LOADEDMODULEINFOLIST_H
