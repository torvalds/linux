//===-- hwasan_registers.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This describes the register state retrieved by hwasan when error reporting.
//
//===----------------------------------------------------------------------===//

#ifndef HWASAN_REGISTERS_H
#define HWASAN_REGISTERS_H

#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_platform.h"

#if defined(__aarch64__)

#  define CAN_GET_REGISTERS 1

struct Registers {
  uptr x[32];
};

__attribute__((always_inline, unused)) static Registers GetRegisters() {
  Registers regs;
  __asm__ volatile(
      "stp x0, x1, [%1, #(8 * 0)]\n"
      "stp x2, x3, [%1, #(8 * 2)]\n"
      "stp x4, x5, [%1, #(8 * 4)]\n"
      "stp x6, x7, [%1, #(8 * 6)]\n"
      "stp x8, x9, [%1, #(8 * 8)]\n"
      "stp x10, x11, [%1, #(8 * 10)]\n"
      "stp x12, x13, [%1, #(8 * 12)]\n"
      "stp x14, x15, [%1, #(8 * 14)]\n"
      "stp x16, x17, [%1, #(8 * 16)]\n"
      "stp x18, x19, [%1, #(8 * 18)]\n"
      "stp x20, x21, [%1, #(8 * 20)]\n"
      "stp x22, x23, [%1, #(8 * 22)]\n"
      "stp x24, x25, [%1, #(8 * 24)]\n"
      "stp x26, x27, [%1, #(8 * 26)]\n"
      "stp x28, x29, [%1, #(8 * 28)]\n"
      : "=m"(regs)
      : "r"(regs.x));
  regs.x[30] = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
  regs.x[31] = reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
  return regs;
}

#else
#  define CAN_GET_REGISTERS 0
#endif

#endif  // HWASAN_REGISTERS_H
