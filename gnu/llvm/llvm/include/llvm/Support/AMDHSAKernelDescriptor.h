//===--- AMDHSAKernelDescriptor.h -----------------------------*- C++ -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// AMDHSA kernel descriptor definitions. For more information, visit
/// https://llvm.org/docs/AMDGPUUsage.html#kernel-descriptor
///
/// \warning
/// Any changes to this file should also be audited for corresponding changes
/// needed in both the assembler and disassembler, namely:
/// * AMDGPUAsmPrinter.{cpp,h}
/// * AMDGPUTargetStreamer.{cpp,h}
/// * AMDGPUDisassembler.{cpp,h}
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_AMDHSAKERNELDESCRIPTOR_H
#define LLVM_SUPPORT_AMDHSAKERNELDESCRIPTOR_H

#include <cstddef>
#include <cstdint>

// Gets offset of specified member in specified type.
#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t)&((TYPE*)0)->MEMBER)
#endif // offsetof

// Creates enumeration entries used for packing bits into integers. Enumeration
// entries include bit shift amount, bit width, and bit mask.
#ifndef AMDHSA_BITS_ENUM_ENTRY
#define AMDHSA_BITS_ENUM_ENTRY(NAME, SHIFT, WIDTH) \
  NAME ## _SHIFT = (SHIFT),                        \
  NAME ## _WIDTH = (WIDTH),                        \
  NAME = (((1 << (WIDTH)) - 1) << (SHIFT))
#endif // AMDHSA_BITS_ENUM_ENTRY

// Gets bits for specified bit mask from specified source.
#ifndef AMDHSA_BITS_GET
#define AMDHSA_BITS_GET(SRC, MSK) ((SRC & MSK) >> MSK ## _SHIFT)
#endif // AMDHSA_BITS_GET

// Sets bits for specified bit mask in specified destination.
#ifndef AMDHSA_BITS_SET
#define AMDHSA_BITS_SET(DST, MSK, VAL)                                         \
  do {                                                                         \
    auto local = VAL;                                                          \
    DST &= ~MSK;                                                               \
    DST |= ((local << MSK##_SHIFT) & MSK);                                     \
  } while (0)
#endif // AMDHSA_BITS_SET

namespace llvm {
namespace amdhsa {

// Floating point rounding modes. Must match hardware definition.
enum : uint8_t {
  FLOAT_ROUND_MODE_NEAR_EVEN = 0,
  FLOAT_ROUND_MODE_PLUS_INFINITY = 1,
  FLOAT_ROUND_MODE_MINUS_INFINITY = 2,
  FLOAT_ROUND_MODE_ZERO = 3,
};

// Floating point denorm modes. Must match hardware definition.
enum : uint8_t {
  FLOAT_DENORM_MODE_FLUSH_SRC_DST = 0,
  FLOAT_DENORM_MODE_FLUSH_DST = 1,
  FLOAT_DENORM_MODE_FLUSH_SRC = 2,
  FLOAT_DENORM_MODE_FLUSH_NONE = 3,
};

// System VGPR workitem IDs. Must match hardware definition.
enum : uint8_t {
  SYSTEM_VGPR_WORKITEM_ID_X = 0,
  SYSTEM_VGPR_WORKITEM_ID_X_Y = 1,
  SYSTEM_VGPR_WORKITEM_ID_X_Y_Z = 2,
  SYSTEM_VGPR_WORKITEM_ID_UNDEFINED = 3,
};

// Compute program resource register 1. Must match hardware definition.
// GFX6+.
#define COMPUTE_PGM_RSRC1(NAME, SHIFT, WIDTH) \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC1_ ## NAME, SHIFT, WIDTH)
// [GFX6-GFX8].
#define COMPUTE_PGM_RSRC1_GFX6_GFX8(NAME, SHIFT, WIDTH) \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC1_GFX6_GFX8_ ## NAME, SHIFT, WIDTH)
// [GFX6-GFX9].
#define COMPUTE_PGM_RSRC1_GFX6_GFX9(NAME, SHIFT, WIDTH) \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC1_GFX6_GFX9_ ## NAME, SHIFT, WIDTH)
// [GFX6-GFX11].
#define COMPUTE_PGM_RSRC1_GFX6_GFX11(NAME, SHIFT, WIDTH)                       \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC1_GFX6_GFX11_##NAME, SHIFT, WIDTH)
// GFX9+.
#define COMPUTE_PGM_RSRC1_GFX9_PLUS(NAME, SHIFT, WIDTH) \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC1_GFX9_PLUS_ ## NAME, SHIFT, WIDTH)
// GFX10+.
#define COMPUTE_PGM_RSRC1_GFX10_PLUS(NAME, SHIFT, WIDTH) \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC1_GFX10_PLUS_ ## NAME, SHIFT, WIDTH)
// GFX12+.
#define COMPUTE_PGM_RSRC1_GFX12_PLUS(NAME, SHIFT, WIDTH)                       \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC1_GFX12_PLUS_##NAME, SHIFT, WIDTH)
enum : int32_t {
  COMPUTE_PGM_RSRC1(GRANULATED_WORKITEM_VGPR_COUNT, 0, 6),
  COMPUTE_PGM_RSRC1(GRANULATED_WAVEFRONT_SGPR_COUNT, 6, 4),
  COMPUTE_PGM_RSRC1(PRIORITY, 10, 2),
  COMPUTE_PGM_RSRC1(FLOAT_ROUND_MODE_32, 12, 2),
  COMPUTE_PGM_RSRC1(FLOAT_ROUND_MODE_16_64, 14, 2),
  COMPUTE_PGM_RSRC1(FLOAT_DENORM_MODE_32, 16, 2),
  COMPUTE_PGM_RSRC1(FLOAT_DENORM_MODE_16_64, 18, 2),
  COMPUTE_PGM_RSRC1(PRIV, 20, 1),
  COMPUTE_PGM_RSRC1_GFX6_GFX11(ENABLE_DX10_CLAMP, 21, 1),
  COMPUTE_PGM_RSRC1_GFX12_PLUS(ENABLE_WG_RR_EN, 21, 1),
  COMPUTE_PGM_RSRC1(DEBUG_MODE, 22, 1),
  COMPUTE_PGM_RSRC1_GFX6_GFX11(ENABLE_IEEE_MODE, 23, 1),
  COMPUTE_PGM_RSRC1_GFX12_PLUS(DISABLE_PERF, 23, 1),
  COMPUTE_PGM_RSRC1(BULKY, 24, 1),
  COMPUTE_PGM_RSRC1(CDBG_USER, 25, 1),
  COMPUTE_PGM_RSRC1_GFX6_GFX8(RESERVED0, 26, 1),
  COMPUTE_PGM_RSRC1_GFX9_PLUS(FP16_OVFL, 26, 1),
  COMPUTE_PGM_RSRC1(RESERVED1, 27, 2),
  COMPUTE_PGM_RSRC1_GFX6_GFX9(RESERVED2, 29, 3),
  COMPUTE_PGM_RSRC1_GFX10_PLUS(WGP_MODE, 29, 1),
  COMPUTE_PGM_RSRC1_GFX10_PLUS(MEM_ORDERED, 30, 1),
  COMPUTE_PGM_RSRC1_GFX10_PLUS(FWD_PROGRESS, 31, 1),
};
#undef COMPUTE_PGM_RSRC1

// Compute program resource register 2. Must match hardware definition.
// GFX6+.
#define COMPUTE_PGM_RSRC2(NAME, SHIFT, WIDTH) \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC2_ ## NAME, SHIFT, WIDTH)
// [GFX6-GFX11].
#define COMPUTE_PGM_RSRC2_GFX6_GFX11(NAME, SHIFT, WIDTH)                       \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC2_GFX6_GFX11_##NAME, SHIFT, WIDTH)
// GFX12+.
#define COMPUTE_PGM_RSRC2_GFX12_PLUS(NAME, SHIFT, WIDTH)                       \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC2_GFX12_PLUS_##NAME, SHIFT, WIDTH)
enum : int32_t {
  COMPUTE_PGM_RSRC2(ENABLE_PRIVATE_SEGMENT, 0, 1),
  COMPUTE_PGM_RSRC2(USER_SGPR_COUNT, 1, 5),
  COMPUTE_PGM_RSRC2_GFX6_GFX11(ENABLE_TRAP_HANDLER, 6, 1),
  COMPUTE_PGM_RSRC2_GFX12_PLUS(RESERVED1, 6, 1),
  COMPUTE_PGM_RSRC2(ENABLE_SGPR_WORKGROUP_ID_X, 7, 1),
  COMPUTE_PGM_RSRC2(ENABLE_SGPR_WORKGROUP_ID_Y, 8, 1),
  COMPUTE_PGM_RSRC2(ENABLE_SGPR_WORKGROUP_ID_Z, 9, 1),
  COMPUTE_PGM_RSRC2(ENABLE_SGPR_WORKGROUP_INFO, 10, 1),
  COMPUTE_PGM_RSRC2(ENABLE_VGPR_WORKITEM_ID, 11, 2),
  COMPUTE_PGM_RSRC2(ENABLE_EXCEPTION_ADDRESS_WATCH, 13, 1),
  COMPUTE_PGM_RSRC2(ENABLE_EXCEPTION_MEMORY, 14, 1),
  COMPUTE_PGM_RSRC2(GRANULATED_LDS_SIZE, 15, 9),
  COMPUTE_PGM_RSRC2(ENABLE_EXCEPTION_IEEE_754_FP_INVALID_OPERATION, 24, 1),
  COMPUTE_PGM_RSRC2(ENABLE_EXCEPTION_FP_DENORMAL_SOURCE, 25, 1),
  COMPUTE_PGM_RSRC2(ENABLE_EXCEPTION_IEEE_754_FP_DIVISION_BY_ZERO, 26, 1),
  COMPUTE_PGM_RSRC2(ENABLE_EXCEPTION_IEEE_754_FP_OVERFLOW, 27, 1),
  COMPUTE_PGM_RSRC2(ENABLE_EXCEPTION_IEEE_754_FP_UNDERFLOW, 28, 1),
  COMPUTE_PGM_RSRC2(ENABLE_EXCEPTION_IEEE_754_FP_INEXACT, 29, 1),
  COMPUTE_PGM_RSRC2(ENABLE_EXCEPTION_INT_DIVIDE_BY_ZERO, 30, 1),
  COMPUTE_PGM_RSRC2(RESERVED0, 31, 1),
};
#undef COMPUTE_PGM_RSRC2

// Compute program resource register 3 for GFX90A+. Must match hardware
// definition.
#define COMPUTE_PGM_RSRC3_GFX90A(NAME, SHIFT, WIDTH) \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC3_GFX90A_ ## NAME, SHIFT, WIDTH)
enum : int32_t {
  COMPUTE_PGM_RSRC3_GFX90A(ACCUM_OFFSET, 0, 6),
  COMPUTE_PGM_RSRC3_GFX90A(RESERVED0, 6, 10),
  COMPUTE_PGM_RSRC3_GFX90A(TG_SPLIT, 16, 1),
  COMPUTE_PGM_RSRC3_GFX90A(RESERVED1, 17, 15),
};
#undef COMPUTE_PGM_RSRC3_GFX90A

// Compute program resource register 3 for GFX10+. Must match hardware
// definition.
// GFX10+.
#define COMPUTE_PGM_RSRC3_GFX10_PLUS(NAME, SHIFT, WIDTH) \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC3_GFX10_PLUS_ ## NAME, SHIFT, WIDTH)
// [GFX10].
#define COMPUTE_PGM_RSRC3_GFX10(NAME, SHIFT, WIDTH)                            \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC3_GFX10_##NAME, SHIFT, WIDTH)
// [GFX10-GFX11].
#define COMPUTE_PGM_RSRC3_GFX10_GFX11(NAME, SHIFT, WIDTH)                      \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC3_GFX10_GFX11_##NAME, SHIFT, WIDTH)
// GFX11+.
#define COMPUTE_PGM_RSRC3_GFX11_PLUS(NAME, SHIFT, WIDTH) \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC3_GFX11_PLUS_ ## NAME, SHIFT, WIDTH)
// [GFX11].
#define COMPUTE_PGM_RSRC3_GFX11(NAME, SHIFT, WIDTH)                            \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC3_GFX11_##NAME, SHIFT, WIDTH)
// GFX12+.
#define COMPUTE_PGM_RSRC3_GFX12_PLUS(NAME, SHIFT, WIDTH)                       \
  AMDHSA_BITS_ENUM_ENTRY(COMPUTE_PGM_RSRC3_GFX12_PLUS_##NAME, SHIFT, WIDTH)
enum : int32_t {
  COMPUTE_PGM_RSRC3_GFX10_GFX11(SHARED_VGPR_COUNT, 0, 4),
  COMPUTE_PGM_RSRC3_GFX12_PLUS(RESERVED0, 0, 4),
  COMPUTE_PGM_RSRC3_GFX10(RESERVED1, 4, 8),
  COMPUTE_PGM_RSRC3_GFX11(INST_PREF_SIZE, 4, 6),
  COMPUTE_PGM_RSRC3_GFX11(TRAP_ON_START, 10, 1),
  COMPUTE_PGM_RSRC3_GFX11(TRAP_ON_END, 11, 1),
  COMPUTE_PGM_RSRC3_GFX12_PLUS(INST_PREF_SIZE, 4, 8),
  COMPUTE_PGM_RSRC3_GFX10_PLUS(RESERVED2, 12, 1),
  COMPUTE_PGM_RSRC3_GFX10_GFX11(RESERVED3, 13, 1),
  COMPUTE_PGM_RSRC3_GFX12_PLUS(GLG_EN, 13, 1),
  COMPUTE_PGM_RSRC3_GFX10_PLUS(RESERVED4, 14, 17),
  COMPUTE_PGM_RSRC3_GFX10(RESERVED5, 31, 1),
  COMPUTE_PGM_RSRC3_GFX11_PLUS(IMAGE_OP, 31, 1),
};
#undef COMPUTE_PGM_RSRC3_GFX10_PLUS

// Kernel code properties. Must be kept backwards compatible.
#define KERNEL_CODE_PROPERTY(NAME, SHIFT, WIDTH) \
  AMDHSA_BITS_ENUM_ENTRY(KERNEL_CODE_PROPERTY_ ## NAME, SHIFT, WIDTH)
enum : int32_t {
  KERNEL_CODE_PROPERTY(ENABLE_SGPR_PRIVATE_SEGMENT_BUFFER, 0, 1),
  KERNEL_CODE_PROPERTY(ENABLE_SGPR_DISPATCH_PTR, 1, 1),
  KERNEL_CODE_PROPERTY(ENABLE_SGPR_QUEUE_PTR, 2, 1),
  KERNEL_CODE_PROPERTY(ENABLE_SGPR_KERNARG_SEGMENT_PTR, 3, 1),
  KERNEL_CODE_PROPERTY(ENABLE_SGPR_DISPATCH_ID, 4, 1),
  KERNEL_CODE_PROPERTY(ENABLE_SGPR_FLAT_SCRATCH_INIT, 5, 1),
  KERNEL_CODE_PROPERTY(ENABLE_SGPR_PRIVATE_SEGMENT_SIZE, 6, 1),
  KERNEL_CODE_PROPERTY(RESERVED0, 7, 3),
  KERNEL_CODE_PROPERTY(ENABLE_WAVEFRONT_SIZE32, 10, 1), // GFX10+
  KERNEL_CODE_PROPERTY(USES_DYNAMIC_STACK, 11, 1),
  KERNEL_CODE_PROPERTY(RESERVED1, 12, 4),
};
#undef KERNEL_CODE_PROPERTY

// Kernarg preload specification.
#define KERNARG_PRELOAD_SPEC(NAME, SHIFT, WIDTH)                               \
  AMDHSA_BITS_ENUM_ENTRY(KERNARG_PRELOAD_SPEC_##NAME, SHIFT, WIDTH)
enum : int32_t {
  KERNARG_PRELOAD_SPEC(LENGTH, 0, 7),
  KERNARG_PRELOAD_SPEC(OFFSET, 7, 9),
};
#undef KERNARG_PRELOAD_SPEC

// Kernel descriptor. Must be kept backwards compatible.
struct kernel_descriptor_t {
  uint32_t group_segment_fixed_size;
  uint32_t private_segment_fixed_size;
  uint32_t kernarg_size;
  uint8_t reserved0[4];
  int64_t kernel_code_entry_byte_offset;
  uint8_t reserved1[20];
  uint32_t compute_pgm_rsrc3; // GFX10+ and GFX90A+
  uint32_t compute_pgm_rsrc1;
  uint32_t compute_pgm_rsrc2;
  uint16_t kernel_code_properties;
  uint16_t kernarg_preload;
  uint8_t reserved3[4];
};

enum : uint32_t {
  GROUP_SEGMENT_FIXED_SIZE_OFFSET = 0,
  PRIVATE_SEGMENT_FIXED_SIZE_OFFSET = 4,
  KERNARG_SIZE_OFFSET = 8,
  RESERVED0_OFFSET = 12,
  KERNEL_CODE_ENTRY_BYTE_OFFSET_OFFSET = 16,
  RESERVED1_OFFSET = 24,
  COMPUTE_PGM_RSRC3_OFFSET = 44,
  COMPUTE_PGM_RSRC1_OFFSET = 48,
  COMPUTE_PGM_RSRC2_OFFSET = 52,
  KERNEL_CODE_PROPERTIES_OFFSET = 56,
  KERNARG_PRELOAD_OFFSET = 58,
  RESERVED3_OFFSET = 60
};

static_assert(
    sizeof(kernel_descriptor_t) == 64,
    "invalid size for kernel_descriptor_t");
static_assert(offsetof(kernel_descriptor_t, group_segment_fixed_size) ==
                  GROUP_SEGMENT_FIXED_SIZE_OFFSET,
              "invalid offset for group_segment_fixed_size");
static_assert(offsetof(kernel_descriptor_t, private_segment_fixed_size) ==
                  PRIVATE_SEGMENT_FIXED_SIZE_OFFSET,
              "invalid offset for private_segment_fixed_size");
static_assert(offsetof(kernel_descriptor_t, kernarg_size) ==
                  KERNARG_SIZE_OFFSET,
              "invalid offset for kernarg_size");
static_assert(offsetof(kernel_descriptor_t, reserved0) == RESERVED0_OFFSET,
              "invalid offset for reserved0");
static_assert(offsetof(kernel_descriptor_t, kernel_code_entry_byte_offset) ==
                  KERNEL_CODE_ENTRY_BYTE_OFFSET_OFFSET,
              "invalid offset for kernel_code_entry_byte_offset");
static_assert(offsetof(kernel_descriptor_t, reserved1) == RESERVED1_OFFSET,
              "invalid offset for reserved1");
static_assert(offsetof(kernel_descriptor_t, compute_pgm_rsrc3) ==
                  COMPUTE_PGM_RSRC3_OFFSET,
              "invalid offset for compute_pgm_rsrc3");
static_assert(offsetof(kernel_descriptor_t, compute_pgm_rsrc1) ==
                  COMPUTE_PGM_RSRC1_OFFSET,
              "invalid offset for compute_pgm_rsrc1");
static_assert(offsetof(kernel_descriptor_t, compute_pgm_rsrc2) ==
                  COMPUTE_PGM_RSRC2_OFFSET,
              "invalid offset for compute_pgm_rsrc2");
static_assert(offsetof(kernel_descriptor_t, kernel_code_properties) ==
                  KERNEL_CODE_PROPERTIES_OFFSET,
              "invalid offset for kernel_code_properties");
static_assert(offsetof(kernel_descriptor_t, kernarg_preload) ==
                  KERNARG_PRELOAD_OFFSET,
              "invalid offset for kernarg_preload");
static_assert(offsetof(kernel_descriptor_t, reserved3) == RESERVED3_OFFSET,
              "invalid offset for reserved3");

} // end namespace amdhsa
} // end namespace llvm

#endif // LLVM_SUPPORT_AMDHSAKERNELDESCRIPTOR_H
