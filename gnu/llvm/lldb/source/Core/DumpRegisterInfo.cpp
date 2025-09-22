//===-- DumpRegisterInfo.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/DumpRegisterInfo.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/RegisterFlags.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;

using SetInfo = std::pair<const char *, uint32_t>;

void lldb_private::DumpRegisterInfo(Stream &strm, RegisterContext &ctx,
                                    const RegisterInfo &info,
                                    uint32_t terminal_width) {
  std::vector<const char *> invalidates;
  if (info.invalidate_regs) {
    for (uint32_t *inv_regs = info.invalidate_regs;
         *inv_regs != LLDB_INVALID_REGNUM; ++inv_regs) {
      const RegisterInfo *inv_info =
          ctx.GetRegisterInfo(lldb::eRegisterKindLLDB, *inv_regs);
      assert(
          inv_info &&
          "Register invalidate list refers to a register that does not exist.");
      invalidates.push_back(inv_info->name);
    }
  }

  // We include the index here so that you can use it with "register read -s".
  std::vector<SetInfo> in_sets;
  for (uint32_t set_idx = 0; set_idx < ctx.GetRegisterSetCount(); ++set_idx) {
    const RegisterSet *set = ctx.GetRegisterSet(set_idx);
    assert(set && "Register set should be valid.");
    for (uint32_t reg_idx = 0; reg_idx < set->num_registers; ++reg_idx) {
      const RegisterInfo *set_reg_info =
          ctx.GetRegisterInfoAtIndex(set->registers[reg_idx]);
      assert(set_reg_info && "Register info should be valid.");

      if (set_reg_info == &info) {
        in_sets.push_back({set->name, set_idx});
        break;
      }
    }
  }

  std::vector<const char *> read_from;
  if (info.value_regs) {
    for (uint32_t *read_regs = info.value_regs;
         *read_regs != LLDB_INVALID_REGNUM; ++read_regs) {
      const RegisterInfo *read_info =
          ctx.GetRegisterInfo(lldb::eRegisterKindLLDB, *read_regs);
      assert(read_info && "Register value registers list refers to a register "
                          "that does not exist.");
      read_from.push_back(read_info->name);
    }
  }

  DoDumpRegisterInfo(strm, info.name, info.alt_name, info.byte_size,
                     invalidates, read_from, in_sets, info.flags_type,
                     terminal_width);
}

template <typename ElementType>
static void DumpList(Stream &strm, const char *title,
                     const std::vector<ElementType> &list,
                     std::function<void(Stream &, ElementType)> emitter) {
  if (list.empty())
    return;

  strm.EOL();
  strm << title;
  bool first = true;
  for (ElementType elem : list) {
    if (!first)
      strm << ", ";
    first = false;
    emitter(strm, elem);
  }
}

void lldb_private::DoDumpRegisterInfo(
    Stream &strm, const char *name, const char *alt_name, uint32_t byte_size,
    const std::vector<const char *> &invalidates,
    const std::vector<const char *> &read_from,
    const std::vector<SetInfo> &in_sets, const RegisterFlags *flags_type,
    uint32_t terminal_width) {
  strm << "       Name: " << name;
  if (alt_name)
    strm << " (" << alt_name << ")";
  strm.EOL();

  // Size in bits may seem obvious for the usual 32 or 64 bit registers.
  // When we get to vector registers, then scalable vector registers, it is very
  // useful to know without the user doing extra work.
  strm.Printf("       Size: %d bytes (%d bits)", byte_size, byte_size * 8);

  std::function<void(Stream &, const char *)> emit_str =
      [](Stream &strm, const char *s) { strm << s; };
  DumpList(strm, "Invalidates: ", invalidates, emit_str);
  DumpList(strm, "  Read from: ", read_from, emit_str);

  std::function<void(Stream &, SetInfo)> emit_set = [](Stream &strm,
                                                       SetInfo info) {
    strm.Printf("%s (index %d)", info.first, info.second);
  };
  DumpList(strm, "    In sets: ", in_sets, emit_set);

  if (flags_type) {
    strm.Printf("\n\n%s", flags_type->AsTable(terminal_width).c_str());

    std::string enumerators = flags_type->DumpEnums(terminal_width);
    if (enumerators.size())
      strm << "\n\n" << enumerators;
  }
}
