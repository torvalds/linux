//===----- sifive_vector.h - SiFive Vector definitions --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _SIFIVE_VECTOR_H_
#define _SIFIVE_VECTOR_H_

#include "riscv_vector.h"

#pragma clang riscv intrinsic sifive_vector

#define __riscv_sf_vc_x_se_u8mf4(p27_26, p24_20, p11_7, rs1, vl)               \
  __riscv_sf_vc_x_se(p27_26, p24_20, p11_7, (uint8_t)rs1, 8, 6, vl)
#define __riscv_sf_vc_x_se_u8mf2(p27_26, p24_20, p11_7, rs1, vl)               \
  __riscv_sf_vc_x_se(p27_26, p24_20, p11_7, (uint8_t)rs1, 8, 7, vl)
#define __riscv_sf_vc_x_se_u8m1(p27_26, p24_20, p11_7, rs1, vl)                \
  __riscv_sf_vc_x_se(p27_26, p24_20, p11_7, (uint8_t)rs1, 8, 0, vl)
#define __riscv_sf_vc_x_se_u8m2(p27_26, p24_20, p11_7, rs1, vl)                \
  __riscv_sf_vc_x_se(p27_26, p24_20, p11_7, (uint8_t)rs1, 8, 1, vl)
#define __riscv_sf_vc_x_se_u8m4(p27_26, p24_20, p11_7, rs1, vl)                \
  __riscv_sf_vc_x_se(p27_26, p24_20, p11_7, (uint8_t)rs1, 8, 2, vl)
#define __riscv_sf_vc_x_se_u8m8(p27_26, p24_20, p11_7, rs1, vl)                \
  __riscv_sf_vc_x_se(p27_26, p24_20, p11_7, (uint8_t)rs1, 8, 3, vl)

#define __riscv_sf_vc_x_se_u16mf2(p27_26, p24_20, p11_7, rs1, vl)              \
  __riscv_sf_vc_x_se(p27_26, p24_20, p11_7, (uint16_t)rs1, 16, 7, vl)
#define __riscv_sf_vc_x_se_u16m1(p27_26, p24_20, p11_7, rs1, vl)               \
  __riscv_sf_vc_x_se(p27_26, p24_20, p11_7, (uint16_t)rs1, 16, 0, vl)
#define __riscv_sf_vc_x_se_u16m2(p27_26, p24_20, p11_7, rs1, vl)               \
  __riscv_sf_vc_x_se(p27_26, p24_20, p11_7, (uint16_t)rs1, 16, 1, vl)
#define __riscv_sf_vc_x_se_u16m4(p27_26, p24_20, p11_7, rs1, vl)               \
  __riscv_sf_vc_x_se(p27_26, p24_20, p11_7, (uint16_t)rs1, 16, 2, vl)
#define __riscv_sf_vc_x_se_u16m8(p27_26, p24_20, p11_7, rs1, vl)               \
  __riscv_sf_vc_x_se(p27_26, p24_20, p11_7, (uint16_t)rs1, 16, 3, vl)

#define __riscv_sf_vc_x_se_u32m1(p27_26, p24_20, p11_7, rs1, vl)               \
  __riscv_sf_vc_x_se(p27_26, p24_20, p11_7, (uint32_t)rs1, 32, 0, vl)
#define __riscv_sf_vc_x_se_u32m2(p27_26, p24_20, p11_7, rs1, vl)               \
  __riscv_sf_vc_x_se(p27_26, p24_20, p11_7, (uint32_t)rs1, 32, 1, vl)
#define __riscv_sf_vc_x_se_u32m4(p27_26, p24_20, p11_7, rs1, vl)               \
  __riscv_sf_vc_x_se(p27_26, p24_20, p11_7, (uint32_t)rs1, 32, 2, vl)
#define __riscv_sf_vc_x_se_u32m8(p27_26, p24_20, p11_7, rs1, vl)               \
  __riscv_sf_vc_x_se(p27_26, p24_20, p11_7, (uint32_t)rs1, 32, 3, vl)

#define __riscv_sf_vc_i_se_u8mf4(p27_26, p24_20, p11_7, simm5, vl)             \
  __riscv_sf_vc_i_se(p27_26, p24_20, p11_7, simm5, 8, 7, vl)
#define __riscv_sf_vc_i_se_u8mf2(p27_26, p24_20, p11_7, simm5, vl)             \
  __riscv_sf_vc_i_se(p27_26, p24_20, p11_7, simm5, 8, 6, vl)
#define __riscv_sf_vc_i_se_u8m1(p27_26, p24_20, p11_7, simm5, vl)              \
  __riscv_sf_vc_i_se(p27_26, p24_20, p11_7, simm5, 8, 0, vl)
#define __riscv_sf_vc_i_se_u8m2(p27_26, p24_20, p11_7, simm5, vl)              \
  __riscv_sf_vc_i_se(p27_26, p24_20, p11_7, simm5, 8, 1, vl)
#define __riscv_sf_vc_i_se_u8m4(p27_26, p24_20, p11_7, simm5, vl)              \
  __riscv_sf_vc_i_se(p27_26, p24_20, p11_7, simm5, 8, 2, vl)
#define __riscv_sf_vc_i_se_u8m8(p27_26, p24_20, p11_7, simm5, vl)              \
  __riscv_sf_vc_i_se(p27_26, p24_20, p11_7, simm5, 8, 3, vl)

#define __riscv_sf_vc_i_se_u16mf2(p27_26, p24_20, p11_7, simm5, vl)            \
  __riscv_sf_vc_i_se(p27_26, p24_20, p11_7, simm5, 16, 7, vl)
#define __riscv_sf_vc_i_se_u16m1(p27_26, p24_20, p11_7, simm5, vl)             \
  __riscv_sf_vc_i_se(p27_26, p24_20, p11_7, simm5, 16, 0, vl)
#define __riscv_sf_vc_i_se_u16m2(p27_26, p24_20, p11_7, simm5, vl)             \
  __riscv_sf_vc_i_se(p27_26, p24_20, p11_7, simm5, 16, 1, vl)
#define __riscv_sf_vc_i_se_u16m4(p27_26, p24_20, p11_7, simm5, vl)             \
  __riscv_sf_vc_i_se(p27_26, p24_20, p11_7, simm5, 16, 2, vl)
#define __riscv_sf_vc_i_se_u16m8(p27_26, p24_20, p11_7, simm5, vl)             \
  __riscv_sf_vc_i_se(p27_26, p24_20, p11_7, simm5, 16, 3, vl)

#define __riscv_sf_vc_i_se_u32m1(p27_26, p24_20, p11_7, simm5, vl)             \
  __riscv_sf_vc_i_se(p27_26, p24_20, p11_7, simm5, 32, 0, vl)
#define __riscv_sf_vc_i_se_u32m2(p27_26, p24_20, p11_7, simm5, vl)             \
  __riscv_sf_vc_i_se(p27_26, p24_20, p11_7, simm5, 32, 1, vl)
#define __riscv_sf_vc_i_se_u32m4(p27_26, p24_20, p11_7, simm5, vl)             \
  __riscv_sf_vc_i_se(p27_26, p24_20, p11_7, simm5, 32, 2, vl)
#define __riscv_sf_vc_i_se_u32m8(p27_26, p24_20, p11_7, simm5, vl)             \
  __riscv_sf_vc_i_se(p27_26, p24_20, p11_7, simm5, 32, 3, vl)

#if __riscv_v_elen >= 64
#define __riscv_sf_vc_x_se_u8mf8(p27_26, p24_20, p11_7, rs1, vl)               \
  __riscv_sf_vc_x_se(p27_26, p24_20, p11_7, (uint8_t)rs1, 8, 5, vl)
#define __riscv_sf_vc_x_se_u16mf4(p27_26, p24_20, p11_7, rs1, vl)              \
  __riscv_sf_vc_x_se(p27_26, p24_20, p11_7, (uint16_t)rs1, 16, 6, vl)
#define __riscv_sf_vc_x_se_u32mf2(p27_26, p24_20, p11_7, rs1, vl)              \
  __riscv_sf_vc_x_se(p27_26, p24_20, p11_7, (uint32_t)rs1, 32, 7, vl)

#define __riscv_sf_vc_i_se_u8mf8(p27_26, p24_20, p11_7, simm5, vl)             \
  __riscv_sf_vc_i_se(p27_26, p24_20, p11_7, simm5, 8, 5, vl)
#define __riscv_sf_vc_i_se_u16mf4(p27_26, p24_20, p11_7, simm5, vl)            \
  __riscv_sf_vc_i_se(p27_26, p24_20, p11_7, simm5, 16, 6, vl)
#define __riscv_sf_vc_i_se_u32mf2(p27_26, p24_20, p11_7, simm5, vl)            \
  __riscv_sf_vc_i_se(p27_26, p24_20, p11_7, simm5, 32, 7, vl)

#define __riscv_sf_vc_i_se_u64m1(p27_26, p24_20, p11_7, simm5, vl)             \
  __riscv_sf_vc_i_se(p27_26, p24_20, p11_7, simm5, 64, 0, vl)
#define __riscv_sf_vc_i_se_u64m2(p27_26, p24_20, p11_7, simm5, vl)             \
  __riscv_sf_vc_i_se(p27_26, p24_20, p11_7, simm5, 64, 1, vl)
#define __riscv_sf_vc_i_se_u64m4(p27_26, p24_20, p11_7, simm5, vl)             \
  __riscv_sf_vc_i_se(p27_26, p24_20, p11_7, simm5, 64, 2, vl)
#define __riscv_sf_vc_i_se_u64m8(p27_26, p24_20, p11_7, simm5, vl)             \
  __riscv_sf_vc_i_se(p27_26, p24_20, p11_7, simm5, 64, 3, vl)

#if __riscv_xlen >= 64
#define __riscv_sf_vc_x_se_u64m1(p27_26, p24_20, p11_7, rs1, vl)               \
  __riscv_sf_vc_x_se(p27_26, p24_20, p11_7, (uint64_t)rs1, 64, 0, vl)
#define __riscv_sf_vc_x_se_u64m2(p27_26, p24_20, p11_7, rs1, vl)               \
  __riscv_sf_vc_x_se(p27_26, p24_20, p11_7, (uint64_t)rs1, 64, 1, vl)
#define __riscv_sf_vc_x_se_u64m4(p27_26, p24_20, p11_7, rs1, vl)               \
  __riscv_sf_vc_x_se(p27_26, p24_20, p11_7, (uint64_t)rs1, 64, 2, vl)
#define __riscv_sf_vc_x_se_u64m8(p27_26, p24_20, p11_7, rs1, vl)               \
  __riscv_sf_vc_x_se(p27_26, p24_20, p11_7, (uint64_t)rs1, 64, 3, vl)
#endif
#endif

#endif //_SIFIVE_VECTOR_H_
