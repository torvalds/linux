//===-- NativeRegisterContextDBReg_x86.cpp --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "NativeRegisterContextDBReg_x86.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/RegisterValue.h"

#include "Plugins/Process/Utility/lldb-x86-register-enums.h"

using namespace lldb_private;

// Returns mask/value for status bit of wp_index in DR6
static inline uint64_t GetStatusBit(uint32_t wp_index) {
  // DR6: ...BBBB
  //         3210 <- status bits for bp./wp. i; 1 if hit
  return 1ULL << wp_index;
}

// Returns mask/value for global enable bit of wp_index in DR7
static inline uint64_t GetEnableBit(uint32_t wp_index) {
  // DR7: ...GLGLGLGL
  //         33221100 <- global/local enable for bp./wp.; 1 if enabled
  // we use global bits because NetBSD kernel does not preserve local
  // bits reliably; Linux seems fine with either
  return 1ULL << (2 * wp_index + 1);
}

// Returns mask for both enable bits of wp_index in DR7
static inline uint64_t GetBothEnableBitMask(uint32_t wp_index) {
  // DR7: ...GLGLGLGL
  //         33221100 <- global/local enable for bp./wp.; 1 if enabled
  return 3ULL << (2 * wp_index + 1);
}

// Returns value for type bits of wp_index in DR7
static inline uint64_t GetWatchTypeBits(uint32_t watch_flags,
                                        uint32_t wp_index) {
  // DR7:
  // bit: 3322222222221111...
  //      1098765432109876...
  // val: SSTTSSTTSSTTSSTT...
  // wp.: 3333222211110000...
  //
  // where T - type is 01 for write, 11 for r/w
  return static_cast<uint64_t>(watch_flags) << (16 + 4 * wp_index);
}

// Returns value for size bits of wp_index in DR7
static inline uint64_t GetWatchSizeBits(uint32_t size, uint32_t wp_index) {
  // DR7:
  // bit: 3322222222221111...
  //      1098765432109876...
  // val: SSTTSSTTSSTTSSTT...
  // wp.: 3333222211110000...
  //
  // where S - size is:
  // 00 for 1 byte
  // 01 for 2 bytes
  // 10 for 8 bytes
  // 11 for 4 bytes
  return static_cast<uint64_t>(size == 8 ? 0x2 : size - 1)
         << (18 + 4 * wp_index);
}

// Returns bitmask for all bits controlling wp_index in DR7
static inline uint64_t GetWatchControlBitmask(uint32_t wp_index) {
  // DR7:
  // bit: 33222222222211111111110000000000
  //      10987654321098765432109876543210
  // val: SSTTSSTTSSTTSSTTxxxxxxGLGLGLGLGL
  // wp.: 3333222211110000xxxxxxEE33221100
  return GetBothEnableBitMask(wp_index) | (0xF << (16 + 4 * wp_index));
}

// Bit mask for control bits regarding all watchpoints.
static constexpr uint64_t watchpoint_all_control_bit_mask = 0xFFFF00FF;

const RegisterInfo *NativeRegisterContextDBReg_x86::GetDR(int num) const {
  assert(num >= 0 && num <= 7);
  switch (GetRegisterInfoInterface().GetTargetArchitecture().GetMachine()) {
  case llvm::Triple::x86:
    return GetRegisterInfoAtIndex(lldb_dr0_i386 + num);
  case llvm::Triple::x86_64:
    return GetRegisterInfoAtIndex(lldb_dr0_x86_64 + num);
  default:
    llvm_unreachable("Unhandled target architecture.");
  }
}

Status NativeRegisterContextDBReg_x86::IsWatchpointHit(uint32_t wp_index,
                                                       bool &is_hit) {
  if (wp_index >= NumSupportedHardwareWatchpoints())
    return Status("Watchpoint index out of range");

  RegisterValue dr6;
  Status error = ReadRegister(GetDR(6), dr6);
  if (error.Fail())
    is_hit = false;
  else
    is_hit = dr6.GetAsUInt64() & GetStatusBit(wp_index);

  return error;
}

Status
NativeRegisterContextDBReg_x86::GetWatchpointHitIndex(uint32_t &wp_index,
                                                      lldb::addr_t trap_addr) {
  uint32_t num_hw_wps = NumSupportedHardwareWatchpoints();
  for (wp_index = 0; wp_index < num_hw_wps; ++wp_index) {
    bool is_hit;
    Status error = IsWatchpointHit(wp_index, is_hit);
    if (error.Fail()) {
      wp_index = LLDB_INVALID_INDEX32;
      return error;
    } else if (is_hit) {
      return error;
    }
  }
  wp_index = LLDB_INVALID_INDEX32;
  return Status();
}

Status NativeRegisterContextDBReg_x86::IsWatchpointVacant(uint32_t wp_index,
                                                          bool &is_vacant) {
  if (wp_index >= NumSupportedHardwareWatchpoints())
    return Status("Watchpoint index out of range");

  RegisterValue dr7;
  Status error = ReadRegister(GetDR(7), dr7);
  if (error.Fail())
    is_vacant = false;
  else
    is_vacant = !(dr7.GetAsUInt64() & GetEnableBit(wp_index));

  return error;
}

Status NativeRegisterContextDBReg_x86::SetHardwareWatchpointWithIndex(
    lldb::addr_t addr, size_t size, uint32_t watch_flags, uint32_t wp_index) {

  if (wp_index >= NumSupportedHardwareWatchpoints())
    return Status("Watchpoint index out of range");

  // Read only watchpoints aren't supported on x86_64. Fall back to read/write
  // waitchpoints instead.
  // TODO: Add logic to detect when a write happens and ignore that watchpoint
  // hit.
  if (watch_flags == 2)
    watch_flags = 3;

  if (watch_flags != 1 && watch_flags != 3)
    return Status("Invalid read/write bits for watchpoint");
  if (size != 1 && size != 2 && size != 4 && size != 8)
    return Status("Invalid size for watchpoint");

  bool is_vacant;
  Status error = IsWatchpointVacant(wp_index, is_vacant);
  if (error.Fail())
    return error;
  if (!is_vacant)
    return Status("Watchpoint index not vacant");

  RegisterValue dr7, drN;
  error = ReadRegister(GetDR(7), dr7);
  if (error.Fail())
    return error;
  error = ReadRegister(GetDR(wp_index), drN);
  if (error.Fail())
    return error;

  uint64_t control_bits = dr7.GetAsUInt64() & ~GetWatchControlBitmask(wp_index);
  control_bits |= GetEnableBit(wp_index) |
                  GetWatchTypeBits(watch_flags, wp_index) |
                  GetWatchSizeBits(size, wp_index);

  // Clear dr6 if address or bits changed (i.e. we're not reenabling the same
  // watchpoint).  This can not be done when clearing watchpoints since
  // the gdb-remote protocol repeatedly clears and readds watchpoints on all
  // program threads, effectively clearing pending events on NetBSD.
  // NB: enable bits in dr7 are always 0 here since we're (re)adding it
  if (drN.GetAsUInt64() != addr ||
      (dr7.GetAsUInt64() & GetWatchControlBitmask(wp_index)) !=
          (GetWatchTypeBits(watch_flags, wp_index) |
           GetWatchSizeBits(size, wp_index))) {
    ClearWatchpointHit(wp_index);

    // We skip update to drN if neither address nor mode changed.
    error = WriteRegister(GetDR(wp_index), RegisterValue(addr));
    if (error.Fail())
      return error;
  }

  error = WriteRegister(GetDR(7), RegisterValue(control_bits));
  if (error.Fail())
    return error;

  return error;
}

bool NativeRegisterContextDBReg_x86::ClearHardwareWatchpoint(
    uint32_t wp_index) {
  if (wp_index >= NumSupportedHardwareWatchpoints())
    return false;

  RegisterValue dr7;
  Status error = ReadRegister(GetDR(7), dr7);
  if (error.Fail())
    return false;

  return WriteRegister(GetDR(7), RegisterValue(dr7.GetAsUInt64() &
                                               ~GetBothEnableBitMask(wp_index)))
      .Success();
}

Status NativeRegisterContextDBReg_x86::ClearWatchpointHit(uint32_t wp_index) {
  if (wp_index >= NumSupportedHardwareWatchpoints())
    return Status("Watchpoint index out of range");

  RegisterValue dr6;
  Status error = ReadRegister(GetDR(6), dr6);
  if (error.Fail())
    return error;

  return WriteRegister(
      GetDR(6), RegisterValue(dr6.GetAsUInt64() & ~GetStatusBit(wp_index)));
}

Status NativeRegisterContextDBReg_x86::ClearAllHardwareWatchpoints() {
  RegisterValue dr7;
  Status error = ReadRegister(GetDR(7), dr7);
  if (error.Fail())
    return error;
  return WriteRegister(
      GetDR(7),
      RegisterValue(dr7.GetAsUInt64() & ~watchpoint_all_control_bit_mask));
}

uint32_t NativeRegisterContextDBReg_x86::SetHardwareWatchpoint(
    lldb::addr_t addr, size_t size, uint32_t watch_flags) {
  Log *log = GetLog(LLDBLog::Watchpoints);
  const uint32_t num_hw_watchpoints = NumSupportedHardwareWatchpoints();
  for (uint32_t wp_index = 0; wp_index < num_hw_watchpoints; ++wp_index) {
    bool is_vacant;
    Status error = IsWatchpointVacant(wp_index, is_vacant);
    if (is_vacant) {
      error = SetHardwareWatchpointWithIndex(addr, size, watch_flags, wp_index);
      if (error.Success())
        return wp_index;
    }
    if (error.Fail() && log) {
      LLDB_LOGF(log, "NativeRegisterContextDBReg_x86::%s Error: %s",
                __FUNCTION__, error.AsCString());
    }
  }
  return LLDB_INVALID_INDEX32;
}

lldb::addr_t
NativeRegisterContextDBReg_x86::GetWatchpointAddress(uint32_t wp_index) {
  if (wp_index >= NumSupportedHardwareWatchpoints())
    return LLDB_INVALID_ADDRESS;
  RegisterValue drN;
  if (ReadRegister(GetDR(wp_index), drN).Fail())
    return LLDB_INVALID_ADDRESS;
  return drN.GetAsUInt64();
}

uint32_t NativeRegisterContextDBReg_x86::NumSupportedHardwareWatchpoints() {
  // Available debug address registers: dr0, dr1, dr2, dr3
  return 4;
}
