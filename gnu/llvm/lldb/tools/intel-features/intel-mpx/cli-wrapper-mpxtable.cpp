//===-- cli-wrapper-mpxtable.cpp --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// C++ includes
#include <cerrno>
#include <string>

#include "cli-wrapper-mpxtable.h"
#include "lldb/API/SBCommandInterpreter.h"
#include "lldb/API/SBCommandReturnObject.h"
#include "lldb/API/SBMemoryRegionInfo.h"
#include "lldb/API/SBProcess.h"
#include "lldb/API/SBTarget.h"
#include "lldb/API/SBThread.h"

#include "llvm/ADT/Twine.h"
#include "llvm/TargetParser/Triple.h"

static bool GetPtr(char *cptr, uint64_t &ptr, lldb::SBFrame &frame,
                   lldb::SBCommandReturnObject &result) {
  if (!cptr) {
    result.SetError("Bad argument.");
    result.SetStatus(lldb::eReturnStatusFailed);
    return false;
  }

  lldb::SBValue ptr_addr = frame.GetValueForVariablePath(cptr);
  if (!ptr_addr.IsValid()) {
    result.SetError("Invalid pointer.");
    result.SetStatus(lldb::eReturnStatusFailed);
    return false;
  }
  ptr = ptr_addr.GetLoadAddress();
  return true;
}

enum {
  mpx_base_mask_64 = ~(uint64_t)0xFFFULL,
  mpx_bd_mask_64 = 0xFFFFFFF00000ULL,
  bd_r_shift_64 = 20,
  bd_l_shift_64 = 3,
  bt_r_shift_64 = 3,
  bt_l_shift_64 = 5,
  bt_mask_64 = 0x0000000FFFF8ULL,

  mpx_base_mask_32 = 0xFFFFFFFFFFFFF000ULL,
  mpx_bd_mask_32 = 0xFFFFF000ULL,
  bd_r_shift_32 = 12,
  bd_l_shift_32 = 2,
  bt_r_shift_32 = 2,
  bt_l_shift_32 = 4,
  bt_mask_32 = 0x00000FFCULL,
};

static void PrintBTEntry(lldb::addr_t lbound, lldb::addr_t ubound,
                         uint64_t value, uint64_t meta,
                         lldb::SBCommandReturnObject &result) {
  const lldb::addr_t one_cmpl64 = ~((lldb::addr_t)0);
  const lldb::addr_t one_cmpl32 = ~((uint32_t)0);

  if ((lbound == one_cmpl64 || lbound == one_cmpl32) && ubound == 0) {
    result.Printf("Null bounds on map: pointer value = 0x%" PRIu64 "\n", value);
  } else {
    result.Printf("    lbound = 0x%" PRIu64 ",", lbound);
    result.Printf(" ubound = 0x%" PRIu64 , ubound);
    result.Printf(" (pointer value = 0x%" PRIu64 ",", value);
    result.Printf(" metadata = 0x%" PRIu64 ")\n", meta);
  }
}

static bool GetBTEntryAddr(uint64_t bndcfgu, uint64_t ptr,
                           lldb::SBTarget &target, llvm::Triple::ArchType arch,
                           size_t &size, lldb::addr_t &bt_entry_addr,
                           lldb::SBCommandReturnObject &result,
                           lldb::SBError &error) {
  lldb::addr_t mpx_base_mask;
  lldb::addr_t mpx_bd_mask;
  lldb::addr_t bd_r_shift;
  lldb::addr_t bd_l_shift;
  lldb::addr_t bt_r_shift;
  lldb::addr_t bt_l_shift;
  lldb::addr_t bt_mask;

  if (arch == llvm::Triple::ArchType::x86_64) {
    mpx_base_mask = mpx_base_mask_64;
    mpx_bd_mask = mpx_bd_mask_64;
    bd_r_shift = bd_r_shift_64;
    bd_l_shift = bd_l_shift_64;
    bt_r_shift = bt_r_shift_64;
    bt_l_shift = bt_l_shift_64;
    bt_mask = bt_mask_64;
  } else if (arch == llvm::Triple::ArchType::x86) {
    mpx_base_mask = mpx_base_mask_32;
    mpx_bd_mask = mpx_bd_mask_32;
    bd_r_shift = bd_r_shift_32;
    bd_l_shift = bd_l_shift_32;
    bt_r_shift = bt_r_shift_32;
    bt_l_shift = bt_l_shift_32;
    bt_mask = bt_mask_32;
  } else {
    result.SetError("Invalid arch.");
    result.SetStatus(lldb::eReturnStatusFailed);
    return false;
  }

  size = target.GetAddressByteSize();
  lldb::addr_t mpx_bd_base = bndcfgu & mpx_base_mask;
  lldb::addr_t bd_entry_offset = ((ptr & mpx_bd_mask) >> bd_r_shift)
                                 << bd_l_shift;
  lldb::addr_t bd_entry_addr = mpx_bd_base + bd_entry_offset;

  std::vector<uint8_t> bd_entry_v(size);
  size_t ret = target.GetProcess().ReadMemory(
      bd_entry_addr, static_cast<void *>(bd_entry_v.data()), size, error);
  if (ret != size || !error.Success()) {
    result.SetError("Failed access to BD entry.");
    return false;
  }

  lldb::SBData data;
  data.SetData(error, bd_entry_v.data(), bd_entry_v.size(),
               target.GetByteOrder(), size);
  lldb::addr_t bd_entry = data.GetAddress(error, 0);

  if (!error.Success()) {
    result.SetError("Failed access to BD entry.");
    return false;
  }

  if ((bd_entry & 0x01) == 0) {
    result.SetError("Invalid bound directory.");
    result.SetStatus(lldb::eReturnStatusFailed);
    return false;
  }

  // Clear status bit.
  //
  bd_entry--;

  lldb::addr_t bt_addr = bd_entry & ~bt_r_shift;
  lldb::addr_t bt_entry_offset = ((ptr & bt_mask) >> bt_r_shift) << bt_l_shift;
  bt_entry_addr = bt_addr + bt_entry_offset;

  return true;
}

static bool GetBTEntry(uint64_t bndcfgu, uint64_t ptr, lldb::SBTarget &target,
                       llvm::Triple::ArchType arch,
                       lldb::SBCommandReturnObject &result,
                       lldb::SBError &error) {
  lldb::addr_t bt_entry_addr;
  size_t size;
  if (!GetBTEntryAddr(bndcfgu, ptr, target, arch, size, bt_entry_addr, result,
                      error))
    return false;

  // bt_entry_v must have space to store the 4 elements of the BT entry (lower
  // boundary,
  // upper boundary, pointer value and meta data), which all have the same size
  // 'size'.
  //
  std::vector<uint8_t> bt_entry_v(size * 4);
  size_t ret = target.GetProcess().ReadMemory(
      bt_entry_addr, static_cast<void *>(bt_entry_v.data()), size * 4, error);

  if ((ret != (size * 4)) || !error.Success()) {
    result.SetError("Unsuccessful. Failed access to BT entry.");
    result.SetStatus(lldb::eReturnStatusFailed);
    return false;
  }

  lldb::addr_t lbound;
  lldb::addr_t ubound;
  uint64_t value;
  uint64_t meta;
  lldb::SBData data;
  data.SetData(error, bt_entry_v.data(), bt_entry_v.size(),
               target.GetByteOrder(), size);
  lbound = data.GetAddress(error, size * 0);
  ubound = data.GetAddress(error, size * 1);
  value = data.GetAddress(error, size * 2);
  meta = data.GetAddress(error, size * 3);
  // ubound is stored as one's complement.
  if (arch == llvm::Triple::ArchType::x86) {
    ubound = (~ubound) & 0x00000000FFFFFFFF;
  } else {
    ubound = ~ubound;
  }

  if (!error.Success()) {
    result.SetError("Failed access to BT entry.");
    return false;
  }

  PrintBTEntry(lbound, ubound, value, meta, result);

  result.SetStatus(lldb::eReturnStatusSuccessFinishResult);
  return true;
}

static std::vector<uint8_t> uIntToU8(uint64_t input, size_t size) {
  std::vector<uint8_t> output;
  for (size_t i = 0; i < size; i++)
    output.push_back(
        static_cast<uint8_t>((input & (0xFFULL << (i * 8))) >> (i * 8)));

  return output;
}

static bool SetBTEntry(uint64_t bndcfgu, uint64_t ptr, lldb::addr_t lbound,
                       lldb::addr_t ubound, lldb::SBTarget &target,
                       llvm::Triple::ArchType arch,
                       lldb::SBCommandReturnObject &result,
                       lldb::SBError &error) {
  lldb::addr_t bt_entry_addr;
  size_t size;

  if (!GetBTEntryAddr(bndcfgu, ptr, target, arch, size, bt_entry_addr, result,
                      error))
    return false;

  // bt_entry_v must have space to store only 2 elements of the BT Entry, the
  // lower boundary and the upper boundary, which both have size 'size'.
  //
  std::vector<uint8_t> bt_entry_v(size * 2);

  std::vector<uint8_t> lbound_v = uIntToU8(lbound, size);
  bt_entry_v.insert(bt_entry_v.begin(), lbound_v.begin(), lbound_v.end());
  std::vector<uint8_t> ubound_v = uIntToU8(~ubound, size);
  bt_entry_v.insert(bt_entry_v.begin() + size, ubound_v.begin(),
                    ubound_v.end());

  size_t ret = target.GetProcess().WriteMemory(
      bt_entry_addr, (void *)(bt_entry_v.data()), size * 2, error);
  if ((ret != (size * 2)) || !error.Success()) {
    result.SetError("Failed access to BT entry.");
    result.SetStatus(lldb::eReturnStatusFailed);
    return false;
  }

  result.SetStatus(lldb::eReturnStatusSuccessFinishResult);
  return true;
}

static bool GetInitInfo(lldb::SBDebugger debugger, lldb::SBTarget &target,
                        llvm::Triple::ArchType &arch, uint64_t &bndcfgu,
                        char *arg, uint64_t &ptr,
                        lldb::SBCommandReturnObject &result,
                        lldb::SBError &error) {
  target = debugger.GetSelectedTarget();
  if (!target.IsValid()) {
    result.SetError("Invalid target.");
    result.SetStatus(lldb::eReturnStatusFailed);
    return false;
  }

  const std::string triple_s(target.GetTriple());
  const llvm::Triple triple(triple_s);

  arch = triple.getArch();

  if ((arch != llvm::Triple::ArchType::x86) &&
      (arch != llvm::Triple::ArchType::x86_64)) {
    result.SetError("Platform not supported.");
    result.SetStatus(lldb::eReturnStatusFailed);
    return false;
  }

  lldb::SBFrame frame =
      target.GetProcess().GetSelectedThread().GetSelectedFrame();
  if (!frame.IsValid()) {
    result.SetError("No valid process, thread or frame.");
    result.SetStatus(lldb::eReturnStatusFailed);
    return false;
  }

  lldb::SBValue bndcfgu_val = frame.FindRegister("bndcfgu");
  if (!bndcfgu_val.IsValid()) {
    result.SetError("Cannot access register BNDCFGU. Does the target support "
                    "Intel(R) Memory Protection Extensions (Intel(R) MPX)?");
    result.SetStatus(lldb::eReturnStatusFailed);
    return false;
  }

  lldb::SBData bndcfgu_data = bndcfgu_val.GetData();
  bndcfgu = bndcfgu_data.GetUnsignedInt64(error, 0);
  if (!error.Success()) {
    result.SetError(error, "Invalid read of register BNDCFGU.");
    return false;
  }

  if (!GetPtr(arg, ptr, frame, result))
    return false;

  return true;
}

class MPXTableShow : public lldb::SBCommandPluginInterface {
public:
  bool DoExecute(lldb::SBDebugger debugger, char **command,
                 lldb::SBCommandReturnObject &result) override {

    if (command) {
      int arg_c = 0;
      char *arg;

      while (*command) {
        if (arg_c >= 1) {
          result.SetError("Too many arguments. See help.");
          result.SetStatus(lldb::eReturnStatusFailed);
          return false;
        }
        arg_c++;
        arg = *command;
        command++;
      }

      if (!debugger.IsValid()) {
        result.SetError("Invalid debugger.");
        result.SetStatus(lldb::eReturnStatusFailed);
        return false;
      }

      lldb::SBTarget target;
      llvm::Triple::ArchType arch;
      lldb::SBError error;
      uint64_t bndcfgu;
      uint64_t ptr;

      if (!GetInitInfo(debugger, target, arch, bndcfgu, arg, ptr, result,
                       error))
        return false;

      return GetBTEntry(bndcfgu, ptr, target, arch, result, error);
    }

    result.SetError("Too few arguments. See help.");
    result.SetStatus(lldb::eReturnStatusFailed);
    return false;
  }
};

class MPXTableSet : public lldb::SBCommandPluginInterface {
public:
  bool DoExecute(lldb::SBDebugger debugger, char **command,
                 lldb::SBCommandReturnObject &result) override {

    if (command) {
      int arg_c = 0;
      char *arg[3];

      while (*command) {
        arg[arg_c] = *command;
        command++;
        arg_c++;
      }

      if (arg_c != 3) {
        result.SetError("Wrong arguments. See help.");
        return false;
      }

      if (!debugger.IsValid()) {
        result.SetError("Invalid debugger.");
        return false;
      }

      lldb::SBTarget target;
      llvm::Triple::ArchType arch;
      lldb::SBError error;
      uint64_t bndcfgu;
      uint64_t ptr;

      if (!GetInitInfo(debugger, target, arch, bndcfgu, arg[0], ptr, result,
                       error))
        return false;

      char *endptr;
      errno = 0;
      uint64_t lbound = std::strtoul(arg[1], &endptr, 16);
      if (endptr == arg[1] || errno == ERANGE) {
        result.SetError("Lower Bound: bad argument format.");
        errno = 0;
        return false;
      }

      uint64_t ubound = std::strtoul(arg[2], &endptr, 16);
      if (endptr == arg[1] || errno == ERANGE) {
        result.SetError("Upper Bound: bad argument format.");
        errno = 0;
        return false;
      }

      return SetBTEntry(bndcfgu, ptr, lbound, ubound, target, arch, result,
                        error);
    }

    result.SetError("Too few arguments. See help.");
    return false;
  }
};

bool MPXPluginInitialize(lldb::SBDebugger &debugger) {
  lldb::SBCommandInterpreter interpreter = debugger.GetCommandInterpreter();
  lldb::SBCommand mpxTable = interpreter.AddMultiwordCommand(
      "mpx-table", "A utility to access the Intel(R) MPX table entries.");

  const char *mpx_show_help = "Show the Intel(R) MPX table entry of a pointer."
                              "\nmpx-table show <pointer>";
  mpxTable.AddCommand("show", new MPXTableShow(), mpx_show_help);

  const char *mpx_set_help =
      "Set the Intel(R) MPX table entry of a pointer.\n"
      "mpx-table set <pointer> <lower bound> <upper bound>";
  mpxTable.AddCommand("set", new MPXTableSet(), mpx_set_help);

  return true;
}
