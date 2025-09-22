//===----- opencl-c-base.h - OpenCL C language base definitions -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _OPENCL_BASE_H_
#define _OPENCL_BASE_H_

// Define extension macros

#if (defined(__OPENCL_CPP_VERSION__) || __OPENCL_C_VERSION__ >= 200)
// For SPIR and SPIR-V all extensions are supported.
#if defined(__SPIR__) || defined(__SPIRV__)
#define cl_khr_subgroup_extended_types 1
#define cl_khr_subgroup_non_uniform_vote 1
#define cl_khr_subgroup_ballot 1
#define cl_khr_subgroup_non_uniform_arithmetic 1
#define cl_khr_subgroup_shuffle 1
#define cl_khr_subgroup_shuffle_relative 1
#define cl_khr_subgroup_clustered_reduce 1
#define cl_khr_subgroup_rotate 1
#define cl_khr_extended_bit_ops 1
#define cl_khr_integer_dot_product 1
#define __opencl_c_integer_dot_product_input_4x8bit 1
#define __opencl_c_integer_dot_product_input_4x8bit_packed 1
#define cl_ext_float_atomics 1
#ifdef cl_khr_fp16
#define __opencl_c_ext_fp16_global_atomic_load_store 1
#define __opencl_c_ext_fp16_local_atomic_load_store 1
#define __opencl_c_ext_fp16_global_atomic_add 1
#define __opencl_c_ext_fp16_local_atomic_add 1
#define __opencl_c_ext_fp16_global_atomic_min_max 1
#define __opencl_c_ext_fp16_local_atomic_min_max 1
#endif
#ifdef cl_khr_fp64
#define __opencl_c_ext_fp64_global_atomic_add 1
#define __opencl_c_ext_fp64_local_atomic_add 1
#define __opencl_c_ext_fp64_global_atomic_min_max 1
#define __opencl_c_ext_fp64_local_atomic_min_max 1
#endif
#define __opencl_c_ext_fp32_global_atomic_add 1
#define __opencl_c_ext_fp32_local_atomic_add 1
#define __opencl_c_ext_fp32_global_atomic_min_max 1
#define __opencl_c_ext_fp32_local_atomic_min_max 1
#define __opencl_c_ext_image_raw10_raw12 1
#define cl_khr_kernel_clock 1
#define __opencl_c_kernel_clock_scope_device 1
#define __opencl_c_kernel_clock_scope_work_group 1
#define __opencl_c_kernel_clock_scope_sub_group 1

#endif // defined(__SPIR__) || defined(__SPIRV__)
#endif // (defined(__OPENCL_CPP_VERSION__) || __OPENCL_C_VERSION__ >= 200)

// Define feature macros for OpenCL C 2.0
#if (__OPENCL_CPP_VERSION__ == 100 || __OPENCL_C_VERSION__ == 200)
#define __opencl_c_pipes 1
#define __opencl_c_generic_address_space 1
#define __opencl_c_work_group_collective_functions 1
#define __opencl_c_atomic_order_acq_rel 1
#define __opencl_c_atomic_order_seq_cst 1
#define __opencl_c_atomic_scope_device 1
#define __opencl_c_atomic_scope_all_devices 1
#define __opencl_c_device_enqueue 1
#define __opencl_c_read_write_images 1
#define __opencl_c_program_scope_global_variables 1
#define __opencl_c_images 1
#endif

// Define header-only feature macros for OpenCL C 3.0.
#if (__OPENCL_CPP_VERSION__ == 202100 || __OPENCL_C_VERSION__ == 300)
// For the SPIR and SPIR-V target all features are supported.
#if defined(__SPIR__) || defined(__SPIRV__)
#define __opencl_c_work_group_collective_functions 1
#define __opencl_c_atomic_order_seq_cst 1
#define __opencl_c_atomic_scope_device 1
#define __opencl_c_atomic_scope_all_devices 1
#define __opencl_c_read_write_images 1
#endif // defined(__SPIR__)

// Undefine any feature macros that have been explicitly disabled using
// an __undef_<feature> macro.
#ifdef __undef___opencl_c_work_group_collective_functions
#undef __opencl_c_work_group_collective_functions
#endif
#ifdef __undef___opencl_c_atomic_order_seq_cst
#undef __opencl_c_atomic_order_seq_cst
#endif
#ifdef __undef___opencl_c_atomic_scope_device
#undef __opencl_c_atomic_scope_device
#endif
#ifdef __undef___opencl_c_atomic_scope_all_devices
#undef __opencl_c_atomic_scope_all_devices
#endif
#ifdef __undef___opencl_c_read_write_images
#undef __opencl_c_read_write_images
#endif

#endif // (__OPENCL_CPP_VERSION__ == 202100 || __OPENCL_C_VERSION__ == 300)

#if !defined(__opencl_c_generic_address_space)
// Internal feature macro to provide named (global, local, private) address
// space overloads for builtin functions that take a pointer argument.
#define __opencl_c_named_address_space_builtins 1
#endif // !defined(__opencl_c_generic_address_space)

#if defined(cl_intel_subgroups) || defined(cl_khr_subgroups) || defined(__opencl_c_subgroups)
// Internal feature macro to provide subgroup builtins.
#define __opencl_subgroup_builtins 1
#endif

// built-in scalar data types:

/**
 * An unsigned 8-bit integer.
 */
typedef unsigned char uchar;

/**
 * An unsigned 16-bit integer.
 */
typedef unsigned short ushort;

/**
 * An unsigned 32-bit integer.
 */
typedef unsigned int uint;

/**
 * An unsigned 64-bit integer.
 */
typedef unsigned long ulong;

/**
 * The unsigned integer type of the result of the sizeof operator. This
 * is a 32-bit unsigned integer if CL_DEVICE_ADDRESS_BITS
 * defined in table 4.3 is 32-bits and is a 64-bit unsigned integer if
 * CL_DEVICE_ADDRESS_BITS is 64-bits.
 */
typedef __SIZE_TYPE__ size_t;

/**
 * A signed integer type that is the result of subtracting two pointers.
 * This is a 32-bit signed integer if CL_DEVICE_ADDRESS_BITS
 * defined in table 4.3 is 32-bits and is a 64-bit signed integer if
 * CL_DEVICE_ADDRESS_BITS is 64-bits.
 */
typedef __PTRDIFF_TYPE__ ptrdiff_t;

/**
 * A signed integer type with the property that any valid pointer to
 * void can be converted to this type, then converted back to pointer
 * to void, and the result will compare equal to the original pointer.
 */
typedef __INTPTR_TYPE__ intptr_t;

/**
 * An unsigned integer type with the property that any valid pointer to
 * void can be converted to this type, then converted back to pointer
 * to void, and the result will compare equal to the original pointer.
 */
typedef __UINTPTR_TYPE__ uintptr_t;

// built-in vector data types:
typedef char char2 __attribute__((ext_vector_type(2)));
typedef char char3 __attribute__((ext_vector_type(3)));
typedef char char4 __attribute__((ext_vector_type(4)));
typedef char char8 __attribute__((ext_vector_type(8)));
typedef char char16 __attribute__((ext_vector_type(16)));
typedef uchar uchar2 __attribute__((ext_vector_type(2)));
typedef uchar uchar3 __attribute__((ext_vector_type(3)));
typedef uchar uchar4 __attribute__((ext_vector_type(4)));
typedef uchar uchar8 __attribute__((ext_vector_type(8)));
typedef uchar uchar16 __attribute__((ext_vector_type(16)));
typedef short short2 __attribute__((ext_vector_type(2)));
typedef short short3 __attribute__((ext_vector_type(3)));
typedef short short4 __attribute__((ext_vector_type(4)));
typedef short short8 __attribute__((ext_vector_type(8)));
typedef short short16 __attribute__((ext_vector_type(16)));
typedef ushort ushort2 __attribute__((ext_vector_type(2)));
typedef ushort ushort3 __attribute__((ext_vector_type(3)));
typedef ushort ushort4 __attribute__((ext_vector_type(4)));
typedef ushort ushort8 __attribute__((ext_vector_type(8)));
typedef ushort ushort16 __attribute__((ext_vector_type(16)));
typedef int int2 __attribute__((ext_vector_type(2)));
typedef int int3 __attribute__((ext_vector_type(3)));
typedef int int4 __attribute__((ext_vector_type(4)));
typedef int int8 __attribute__((ext_vector_type(8)));
typedef int int16 __attribute__((ext_vector_type(16)));
typedef uint uint2 __attribute__((ext_vector_type(2)));
typedef uint uint3 __attribute__((ext_vector_type(3)));
typedef uint uint4 __attribute__((ext_vector_type(4)));
typedef uint uint8 __attribute__((ext_vector_type(8)));
typedef uint uint16 __attribute__((ext_vector_type(16)));
typedef long long2 __attribute__((ext_vector_type(2)));
typedef long long3 __attribute__((ext_vector_type(3)));
typedef long long4 __attribute__((ext_vector_type(4)));
typedef long long8 __attribute__((ext_vector_type(8)));
typedef long long16 __attribute__((ext_vector_type(16)));
typedef ulong ulong2 __attribute__((ext_vector_type(2)));
typedef ulong ulong3 __attribute__((ext_vector_type(3)));
typedef ulong ulong4 __attribute__((ext_vector_type(4)));
typedef ulong ulong8 __attribute__((ext_vector_type(8)));
typedef ulong ulong16 __attribute__((ext_vector_type(16)));
typedef float float2 __attribute__((ext_vector_type(2)));
typedef float float3 __attribute__((ext_vector_type(3)));
typedef float float4 __attribute__((ext_vector_type(4)));
typedef float float8 __attribute__((ext_vector_type(8)));
typedef float float16 __attribute__((ext_vector_type(16)));
#ifdef cl_khr_fp16
#pragma OPENCL EXTENSION cl_khr_fp16 : enable
typedef half half2 __attribute__((ext_vector_type(2)));
typedef half half3 __attribute__((ext_vector_type(3)));
typedef half half4 __attribute__((ext_vector_type(4)));
typedef half half8 __attribute__((ext_vector_type(8)));
typedef half half16 __attribute__((ext_vector_type(16)));
#endif
#ifdef cl_khr_fp64
#if __OPENCL_C_VERSION__ < CL_VERSION_1_2
#pragma OPENCL EXTENSION cl_khr_fp64 : enable
#endif
typedef double double2 __attribute__((ext_vector_type(2)));
typedef double double3 __attribute__((ext_vector_type(3)));
typedef double double4 __attribute__((ext_vector_type(4)));
typedef double double8 __attribute__((ext_vector_type(8)));
typedef double double16 __attribute__((ext_vector_type(16)));
#endif

// An internal alias for half, for use by OpenCLBuiltins.td.
#define __half half

#if defined(__OPENCL_CPP_VERSION__)
#define NULL nullptr
#elif defined(__OPENCL_C_VERSION__)
#define NULL ((void*)0)
#endif

/**
 * Value of maximum non-infinite single-precision floating-point
 * number.
 */
#define MAXFLOAT 0x1.fffffep127f

/**
 * A positive float constant expression. HUGE_VALF evaluates
 * to +infinity. Used as an error value returned by the built-in
 * math functions.
 */
#define HUGE_VALF (__builtin_huge_valf())

/**
 * A positive double constant expression. HUGE_VAL evaluates
 * to +infinity. Used as an error value returned by the built-in
 * math functions.
 */
#define HUGE_VAL (__builtin_huge_val())

/**
 * A constant expression of type float representing positive or
 * unsigned infinity.
 */
#define INFINITY (__builtin_inff())

/**
 * A constant expression of type float representing a quiet NaN.
 */
#define NAN as_float(INT_MAX)

#define FP_ILOGB0    INT_MIN
#define FP_ILOGBNAN  INT_MAX

#define FLT_DIG 6
#define FLT_MANT_DIG 24
#define FLT_MAX_10_EXP +38
#define FLT_MAX_EXP +128
#define FLT_MIN_10_EXP -37
#define FLT_MIN_EXP -125
#define FLT_RADIX 2
#define FLT_MAX 0x1.fffffep127f
#define FLT_MIN 0x1.0p-126f
#define FLT_EPSILON 0x1.0p-23f

#define M_E_F         2.71828182845904523536028747135266250f
#define M_LOG2E_F     1.44269504088896340735992468100189214f
#define M_LOG10E_F    0.434294481903251827651128918916605082f
#define M_LN2_F       0.693147180559945309417232121458176568f
#define M_LN10_F      2.30258509299404568401799145468436421f
#define M_PI_F        3.14159265358979323846264338327950288f
#define M_PI_2_F      1.57079632679489661923132169163975144f
#define M_PI_4_F      0.785398163397448309615660845819875721f
#define M_1_PI_F      0.318309886183790671537767526745028724f
#define M_2_PI_F      0.636619772367581343075535053490057448f
#define M_2_SQRTPI_F  1.12837916709551257389615890312154517f
#define M_SQRT2_F     1.41421356237309504880168872420969808f
#define M_SQRT1_2_F   0.707106781186547524400844362104849039f

#define DBL_DIG 15
#define DBL_MANT_DIG 53
#define DBL_MAX_10_EXP +308
#define DBL_MAX_EXP +1024
#define DBL_MIN_10_EXP -307
#define DBL_MIN_EXP -1021
#define DBL_RADIX 2
#define DBL_MAX 0x1.fffffffffffffp1023
#define DBL_MIN 0x1.0p-1022
#define DBL_EPSILON 0x1.0p-52

#define M_E           0x1.5bf0a8b145769p+1
#define M_LOG2E       0x1.71547652b82fep+0
#define M_LOG10E      0x1.bcb7b1526e50ep-2
#define M_LN2         0x1.62e42fefa39efp-1
#define M_LN10        0x1.26bb1bbb55516p+1
#define M_PI          0x1.921fb54442d18p+1
#define M_PI_2        0x1.921fb54442d18p+0
#define M_PI_4        0x1.921fb54442d18p-1
#define M_1_PI        0x1.45f306dc9c883p-2
#define M_2_PI        0x1.45f306dc9c883p-1
#define M_2_SQRTPI    0x1.20dd750429b6dp+0
#define M_SQRT2       0x1.6a09e667f3bcdp+0
#define M_SQRT1_2     0x1.6a09e667f3bcdp-1

#ifdef cl_khr_fp16

#define HALF_DIG 3
#define HALF_MANT_DIG 11
#define HALF_MAX_10_EXP +4
#define HALF_MAX_EXP +16
#define HALF_MIN_10_EXP -4
#define HALF_MIN_EXP -13
#define HALF_RADIX 2
#define HALF_MAX ((0x1.ffcp15h))
#define HALF_MIN ((0x1.0p-14h))
#define HALF_EPSILON ((0x1.0p-10h))

#define M_E_H         2.71828182845904523536028747135266250h
#define M_LOG2E_H     1.44269504088896340735992468100189214h
#define M_LOG10E_H    0.434294481903251827651128918916605082h
#define M_LN2_H       0.693147180559945309417232121458176568h
#define M_LN10_H      2.30258509299404568401799145468436421h
#define M_PI_H        3.14159265358979323846264338327950288h
#define M_PI_2_H      1.57079632679489661923132169163975144h
#define M_PI_4_H      0.785398163397448309615660845819875721h
#define M_1_PI_H      0.318309886183790671537767526745028724h
#define M_2_PI_H      0.636619772367581343075535053490057448h
#define M_2_SQRTPI_H  1.12837916709551257389615890312154517h
#define M_SQRT2_H     1.41421356237309504880168872420969808h
#define M_SQRT1_2_H   0.707106781186547524400844362104849039h

#endif //cl_khr_fp16

#define CHAR_BIT  8
#define SCHAR_MAX 127
#define SCHAR_MIN (-128)
#define UCHAR_MAX 255
#define CHAR_MAX  SCHAR_MAX
#define CHAR_MIN  SCHAR_MIN
#define USHRT_MAX 65535
#define SHRT_MAX  32767
#define SHRT_MIN  (-32768)
#define UINT_MAX  0xffffffff
#define INT_MAX   2147483647
#define INT_MIN   (-2147483647-1)
#define ULONG_MAX 0xffffffffffffffffUL
#define LONG_MAX  0x7fffffffffffffffL
#define LONG_MIN  (-0x7fffffffffffffffL-1)

// OpenCL v1.1 s6.11.8, v1.2 s6.12.8, v2.0 s6.13.8 - Synchronization Functions

// Flag type and values for barrier, mem_fence, read_mem_fence, write_mem_fence
typedef uint cl_mem_fence_flags;

/**
 * Queue a memory fence to ensure correct
 * ordering of memory operations to local memory
 */
#define CLK_LOCAL_MEM_FENCE    0x01

/**
 * Queue a memory fence to ensure correct
 * ordering of memory operations to global memory
 */
#define CLK_GLOBAL_MEM_FENCE   0x02

#if defined(__OPENCL_CPP_VERSION__) || (__OPENCL_C_VERSION__ >= CL_VERSION_2_0)

typedef enum memory_scope {
  memory_scope_work_item = __OPENCL_MEMORY_SCOPE_WORK_ITEM,
  memory_scope_work_group = __OPENCL_MEMORY_SCOPE_WORK_GROUP,
  memory_scope_device = __OPENCL_MEMORY_SCOPE_DEVICE,
#if defined(__opencl_c_atomic_scope_all_devices)
  memory_scope_all_svm_devices = __OPENCL_MEMORY_SCOPE_ALL_SVM_DEVICES,
#if (__OPENCL_C_VERSION__ >= CL_VERSION_3_0 || __OPENCL_CPP_VERSION__ >= 202100)
  memory_scope_all_devices = memory_scope_all_svm_devices,
#endif // (__OPENCL_C_VERSION__ >= CL_VERSION_3_0 || __OPENCL_CPP_VERSION__ >= 202100)
#endif // defined(__opencl_c_atomic_scope_all_devices)
/**
 * Subgroups have different requirements on forward progress, so just test
 * all the relevant macros.
 * CL 3.0 sub-groups "they are not guaranteed to make independent forward progress"
 * KHR subgroups "Subgroups within a workgroup are independent, make forward progress with respect to each other"
 */
#if defined(cl_intel_subgroups) || defined(cl_khr_subgroups) || defined(__opencl_c_subgroups)
  memory_scope_sub_group = __OPENCL_MEMORY_SCOPE_SUB_GROUP
#endif
} memory_scope;

/**
 * Queue a memory fence to ensure correct ordering of memory
 * operations between work-items of a work-group to
 * image memory.
 */
#define CLK_IMAGE_MEM_FENCE  0x04

#ifndef ATOMIC_VAR_INIT
#define ATOMIC_VAR_INIT(x) (x)
#endif //ATOMIC_VAR_INIT
#define ATOMIC_FLAG_INIT 0

// enum values aligned with what clang uses in EmitAtomicExpr()
typedef enum memory_order
{
  memory_order_relaxed = __ATOMIC_RELAXED,
  memory_order_acquire = __ATOMIC_ACQUIRE,
  memory_order_release = __ATOMIC_RELEASE,
  memory_order_acq_rel = __ATOMIC_ACQ_REL,
#if defined(__opencl_c_atomic_order_seq_cst)
  memory_order_seq_cst = __ATOMIC_SEQ_CST
#endif
} memory_order;

#endif // defined(__OPENCL_CPP_VERSION__) || (__OPENCL_C_VERSION__ >= CL_VERSION_2_0)

// OpenCL v1.1 s6.11.3, v1.2 s6.12.14, v2.0 s6.13.14 - Image Read and Write Functions

// These values need to match the runtime equivalent
//
// Addressing Mode.
//
#define CLK_ADDRESS_NONE                0
#define CLK_ADDRESS_CLAMP_TO_EDGE       2
#define CLK_ADDRESS_CLAMP               4
#define CLK_ADDRESS_REPEAT              6
#define CLK_ADDRESS_MIRRORED_REPEAT     8

//
// Coordination Normalization
//
#define CLK_NORMALIZED_COORDS_FALSE     0
#define CLK_NORMALIZED_COORDS_TRUE      1

//
// Filtering Mode.
//
#define CLK_FILTER_NEAREST              0x10
#define CLK_FILTER_LINEAR               0x20

#ifdef cl_khr_gl_msaa_sharing
#pragma OPENCL EXTENSION cl_khr_gl_msaa_sharing : enable
#endif //cl_khr_gl_msaa_sharing

//
// Channel Datatype.
//
#define CLK_SNORM_INT8        0x10D0
#define CLK_SNORM_INT16       0x10D1
#define CLK_UNORM_INT8        0x10D2
#define CLK_UNORM_INT16       0x10D3
#define CLK_UNORM_SHORT_565   0x10D4
#define CLK_UNORM_SHORT_555   0x10D5
#define CLK_UNORM_INT_101010  0x10D6
#define CLK_SIGNED_INT8       0x10D7
#define CLK_SIGNED_INT16      0x10D8
#define CLK_SIGNED_INT32      0x10D9
#define CLK_UNSIGNED_INT8     0x10DA
#define CLK_UNSIGNED_INT16    0x10DB
#define CLK_UNSIGNED_INT32    0x10DC
#define CLK_HALF_FLOAT        0x10DD
#define CLK_FLOAT             0x10DE
#define CLK_UNORM_INT24       0x10DF
#if __OPENCL_C_VERSION__ >= CL_VERSION_3_0
#define CLK_UNORM_INT_101010_2 0x10E0
#endif // __OPENCL_C_VERSION__ >= CL_VERSION_3_0
#ifdef __opencl_c_ext_image_raw10_raw12
#define CLK_UNSIGNED_INT_RAW10_EXT 0x10E3
#define CLK_UNSIGNED_INT_RAW12_EXT 0x10E4
#endif // __opencl_c_ext_image_raw10_raw12

// Channel order, numbering must be aligned with cl_channel_order in cl.h
//
#define CLK_R         0x10B0
#define CLK_A         0x10B1
#define CLK_RG        0x10B2
#define CLK_RA        0x10B3
#define CLK_RGB       0x10B4
#define CLK_RGBA      0x10B5
#define CLK_BGRA      0x10B6
#define CLK_ARGB      0x10B7
#define CLK_INTENSITY 0x10B8
#define CLK_LUMINANCE 0x10B9
#define CLK_Rx                0x10BA
#define CLK_RGx               0x10BB
#define CLK_RGBx              0x10BC
#define CLK_DEPTH             0x10BD
#define CLK_DEPTH_STENCIL     0x10BE
#if __OPENCL_C_VERSION__ >= CL_VERSION_2_0
#define CLK_sRGB              0x10BF
#define CLK_sRGBx             0x10C0
#define CLK_sRGBA             0x10C1
#define CLK_sBGRA             0x10C2
#define CLK_ABGR              0x10C3
#endif //__OPENCL_C_VERSION__ >= CL_VERSION_2_0

// OpenCL v2.0 s6.13.16 - Pipe Functions
#if defined(__OPENCL_CPP_VERSION__) || (__OPENCL_C_VERSION__ >= CL_VERSION_2_0)
#define CLK_NULL_RESERVE_ID (__builtin_astype(((void*)(__SIZE_MAX__)), reserve_id_t))

// OpenCL v2.0 s6.13.17 - Enqueue Kernels
#define CL_COMPLETE                                 0x0
#define CL_RUNNING                                  0x1
#define CL_SUBMITTED                                0x2
#define CL_QUEUED                                   0x3

#define CLK_SUCCESS                                 0
#define CLK_ENQUEUE_FAILURE                         -101
#define CLK_INVALID_QUEUE                           -102
#define CLK_INVALID_NDRANGE                         -160
#define CLK_INVALID_EVENT_WAIT_LIST                 -57
#define CLK_DEVICE_QUEUE_FULL                       -161
#define CLK_INVALID_ARG_SIZE                        -51
#define CLK_EVENT_ALLOCATION_FAILURE                -100
#define CLK_OUT_OF_RESOURCES                        -5

#define CLK_NULL_QUEUE                              0
#define CLK_NULL_EVENT (__builtin_astype(((__SIZE_MAX__)), clk_event_t))

// execution model related definitions
#define CLK_ENQUEUE_FLAGS_NO_WAIT                   0x0
#define CLK_ENQUEUE_FLAGS_WAIT_KERNEL               0x1
#define CLK_ENQUEUE_FLAGS_WAIT_WORK_GROUP           0x2

typedef int kernel_enqueue_flags_t;
typedef int clk_profiling_info;

// Profiling info name (see capture_event_profiling_info)
#define CLK_PROFILING_COMMAND_EXEC_TIME 0x1

#define MAX_WORK_DIM 3

#ifdef __opencl_c_device_enqueue
typedef struct {
  unsigned int workDimension;
  size_t globalWorkOffset[MAX_WORK_DIM];
  size_t globalWorkSize[MAX_WORK_DIM];
  size_t localWorkSize[MAX_WORK_DIM];
} ndrange_t;
#endif // __opencl_c_device_enqueue

#endif // defined(__OPENCL_CPP_VERSION__) || (__OPENCL_C_VERSION__ >= CL_VERSION_2_0)

/**
 * OpenCL v1.1/1.2/2.0 s6.2.4.2 - as_type operators
 * Reinterprets a data type as another data type of the same size
 */
#define as_char(x) __builtin_astype((x), char)
#define as_char2(x) __builtin_astype((x), char2)
#define as_char3(x) __builtin_astype((x), char3)
#define as_char4(x) __builtin_astype((x), char4)
#define as_char8(x) __builtin_astype((x), char8)
#define as_char16(x) __builtin_astype((x), char16)

#define as_uchar(x) __builtin_astype((x), uchar)
#define as_uchar2(x) __builtin_astype((x), uchar2)
#define as_uchar3(x) __builtin_astype((x), uchar3)
#define as_uchar4(x) __builtin_astype((x), uchar4)
#define as_uchar8(x) __builtin_astype((x), uchar8)
#define as_uchar16(x) __builtin_astype((x), uchar16)

#define as_short(x) __builtin_astype((x), short)
#define as_short2(x) __builtin_astype((x), short2)
#define as_short3(x) __builtin_astype((x), short3)
#define as_short4(x) __builtin_astype((x), short4)
#define as_short8(x) __builtin_astype((x), short8)
#define as_short16(x) __builtin_astype((x), short16)

#define as_ushort(x) __builtin_astype((x), ushort)
#define as_ushort2(x) __builtin_astype((x), ushort2)
#define as_ushort3(x) __builtin_astype((x), ushort3)
#define as_ushort4(x) __builtin_astype((x), ushort4)
#define as_ushort8(x) __builtin_astype((x), ushort8)
#define as_ushort16(x) __builtin_astype((x), ushort16)

#define as_int(x) __builtin_astype((x), int)
#define as_int2(x) __builtin_astype((x), int2)
#define as_int3(x) __builtin_astype((x), int3)
#define as_int4(x) __builtin_astype((x), int4)
#define as_int8(x) __builtin_astype((x), int8)
#define as_int16(x) __builtin_astype((x), int16)

#define as_uint(x) __builtin_astype((x), uint)
#define as_uint2(x) __builtin_astype((x), uint2)
#define as_uint3(x) __builtin_astype((x), uint3)
#define as_uint4(x) __builtin_astype((x), uint4)
#define as_uint8(x) __builtin_astype((x), uint8)
#define as_uint16(x) __builtin_astype((x), uint16)

#define as_long(x) __builtin_astype((x), long)
#define as_long2(x) __builtin_astype((x), long2)
#define as_long3(x) __builtin_astype((x), long3)
#define as_long4(x) __builtin_astype((x), long4)
#define as_long8(x) __builtin_astype((x), long8)
#define as_long16(x) __builtin_astype((x), long16)

#define as_ulong(x) __builtin_astype((x), ulong)
#define as_ulong2(x) __builtin_astype((x), ulong2)
#define as_ulong3(x) __builtin_astype((x), ulong3)
#define as_ulong4(x) __builtin_astype((x), ulong4)
#define as_ulong8(x) __builtin_astype((x), ulong8)
#define as_ulong16(x) __builtin_astype((x), ulong16)

#define as_float(x) __builtin_astype((x), float)
#define as_float2(x) __builtin_astype((x), float2)
#define as_float3(x) __builtin_astype((x), float3)
#define as_float4(x) __builtin_astype((x), float4)
#define as_float8(x) __builtin_astype((x), float8)
#define as_float16(x) __builtin_astype((x), float16)

#ifdef cl_khr_fp64
#define as_double(x) __builtin_astype((x), double)
#define as_double2(x) __builtin_astype((x), double2)
#define as_double3(x) __builtin_astype((x), double3)
#define as_double4(x) __builtin_astype((x), double4)
#define as_double8(x) __builtin_astype((x), double8)
#define as_double16(x) __builtin_astype((x), double16)
#endif // cl_khr_fp64

#ifdef cl_khr_fp16
#define as_half(x) __builtin_astype((x), half)
#define as_half2(x) __builtin_astype((x), half2)
#define as_half3(x) __builtin_astype((x), half3)
#define as_half4(x) __builtin_astype((x), half4)
#define as_half8(x) __builtin_astype((x), half8)
#define as_half16(x) __builtin_astype((x), half16)
#endif // cl_khr_fp16

#define as_size_t(x) __builtin_astype((x), size_t)
#define as_ptrdiff_t(x) __builtin_astype((x), ptrdiff_t)
#define as_intptr_t(x) __builtin_astype((x), intptr_t)
#define as_uintptr_t(x) __builtin_astype((x), uintptr_t)

// C++ for OpenCL - __remove_address_space
#if defined(__OPENCL_CPP_VERSION__)
template <typename _Tp> struct __remove_address_space { using type = _Tp; };
#if defined(__opencl_c_generic_address_space)
template <typename _Tp> struct __remove_address_space<__generic _Tp> {
  using type = _Tp;
};
#endif
template <typename _Tp> struct __remove_address_space<__global _Tp> {
  using type = _Tp;
};
template <typename _Tp> struct __remove_address_space<__private _Tp> {
  using type = _Tp;
};
template <typename _Tp> struct __remove_address_space<__local _Tp> {
  using type = _Tp;
};
template <typename _Tp> struct __remove_address_space<__constant _Tp> {
  using type = _Tp;
};
#endif

// OpenCL v1.1 s6.9, v1.2/2.0 s6.10 - Function qualifiers

#define __kernel_exec(X, typen) __kernel \
	__attribute__((work_group_size_hint(X, 1, 1))) \
	__attribute__((vec_type_hint(typen)))

#define kernel_exec(X, typen) __kernel \
	__attribute__((work_group_size_hint(X, 1, 1))) \
	__attribute__((vec_type_hint(typen)))

#if defined(__OPENCL_CPP_VERSION__) || (__OPENCL_C_VERSION__ >= CL_VERSION_1_2)
// OpenCL v1.2 s6.12.13, v2.0 s6.13.13 - printf

int printf(__constant const char* st, ...) __attribute__((format(printf, 1, 2)));
#endif

#ifdef cl_intel_device_side_avc_motion_estimation

#define CLK_AVC_ME_MAJOR_16x16_INTEL 0x0
#define CLK_AVC_ME_MAJOR_16x8_INTEL 0x1
#define CLK_AVC_ME_MAJOR_8x16_INTEL 0x2
#define CLK_AVC_ME_MAJOR_8x8_INTEL 0x3

#define CLK_AVC_ME_MINOR_8x8_INTEL 0x0
#define CLK_AVC_ME_MINOR_8x4_INTEL 0x1
#define CLK_AVC_ME_MINOR_4x8_INTEL 0x2
#define CLK_AVC_ME_MINOR_4x4_INTEL 0x3

#define CLK_AVC_ME_MAJOR_FORWARD_INTEL 0x0
#define CLK_AVC_ME_MAJOR_BACKWARD_INTEL 0x1
#define CLK_AVC_ME_MAJOR_BIDIRECTIONAL_INTEL 0x2

#define CLK_AVC_ME_PARTITION_MASK_ALL_INTEL 0x0
#define CLK_AVC_ME_PARTITION_MASK_16x16_INTEL 0x7E
#define CLK_AVC_ME_PARTITION_MASK_16x8_INTEL 0x7D
#define CLK_AVC_ME_PARTITION_MASK_8x16_INTEL 0x7B
#define CLK_AVC_ME_PARTITION_MASK_8x8_INTEL 0x77
#define CLK_AVC_ME_PARTITION_MASK_8x4_INTEL 0x6F
#define CLK_AVC_ME_PARTITION_MASK_4x8_INTEL 0x5F
#define CLK_AVC_ME_PARTITION_MASK_4x4_INTEL 0x3F

#define CLK_AVC_ME_SLICE_TYPE_PRED_INTEL 0x0
#define CLK_AVC_ME_SLICE_TYPE_BPRED_INTEL 0x1
#define CLK_AVC_ME_SLICE_TYPE_INTRA_INTEL 0x2

#define CLK_AVC_ME_SEARCH_WINDOW_EXHAUSTIVE_INTEL 0x0
#define CLK_AVC_ME_SEARCH_WINDOW_SMALL_INTEL 0x1
#define CLK_AVC_ME_SEARCH_WINDOW_TINY_INTEL 0x2
#define CLK_AVC_ME_SEARCH_WINDOW_EXTRA_TINY_INTEL 0x3
#define CLK_AVC_ME_SEARCH_WINDOW_DIAMOND_INTEL 0x4
#define CLK_AVC_ME_SEARCH_WINDOW_LARGE_DIAMOND_INTEL 0x5
#define CLK_AVC_ME_SEARCH_WINDOW_RESERVED0_INTEL 0x6
#define CLK_AVC_ME_SEARCH_WINDOW_RESERVED1_INTEL 0x7
#define CLK_AVC_ME_SEARCH_WINDOW_CUSTOM_INTEL 0x8

#define CLK_AVC_ME_SAD_ADJUST_MODE_NONE_INTEL 0x0
#define CLK_AVC_ME_SAD_ADJUST_MODE_HAAR_INTEL 0x2

#define CLK_AVC_ME_SUBPIXEL_MODE_INTEGER_INTEL 0x0
#define CLK_AVC_ME_SUBPIXEL_MODE_HPEL_INTEL 0x1
#define CLK_AVC_ME_SUBPIXEL_MODE_QPEL_INTEL 0x3

#define CLK_AVC_ME_COST_PRECISION_QPEL_INTEL 0x0
#define CLK_AVC_ME_COST_PRECISION_HPEL_INTEL 0x1
#define CLK_AVC_ME_COST_PRECISION_PEL_INTEL 0x2
#define CLK_AVC_ME_COST_PRECISION_DPEL_INTEL 0x3

#define CLK_AVC_ME_BIDIR_WEIGHT_QUARTER_INTEL 0x10
#define CLK_AVC_ME_BIDIR_WEIGHT_THIRD_INTEL 0x15
#define CLK_AVC_ME_BIDIR_WEIGHT_HALF_INTEL 0x20
#define CLK_AVC_ME_BIDIR_WEIGHT_TWO_THIRD_INTEL 0x2B
#define CLK_AVC_ME_BIDIR_WEIGHT_THREE_QUARTER_INTEL 0x30

#define CLK_AVC_ME_BORDER_REACHED_LEFT_INTEL 0x0
#define CLK_AVC_ME_BORDER_REACHED_RIGHT_INTEL 0x2
#define CLK_AVC_ME_BORDER_REACHED_TOP_INTEL 0x4
#define CLK_AVC_ME_BORDER_REACHED_BOTTOM_INTEL 0x8

#define CLK_AVC_ME_INTRA_16x16_INTEL 0x0
#define CLK_AVC_ME_INTRA_8x8_INTEL 0x1
#define CLK_AVC_ME_INTRA_4x4_INTEL 0x2

#define CLK_AVC_ME_SKIP_BLOCK_PARTITION_16x16_INTEL 0x0
#define CLK_AVC_ME_SKIP_BLOCK_PARTITION_8x8_INTEL 0x4000

#define CLK_AVC_ME_SKIP_BLOCK_16x16_FORWARD_ENABLE_INTEL (0x1 << 24)
#define CLK_AVC_ME_SKIP_BLOCK_16x16_BACKWARD_ENABLE_INTEL (0x2 << 24)
#define CLK_AVC_ME_SKIP_BLOCK_16x16_DUAL_ENABLE_INTEL (0x3 << 24)
#define CLK_AVC_ME_SKIP_BLOCK_8x8_FORWARD_ENABLE_INTEL (0x55 << 24)
#define CLK_AVC_ME_SKIP_BLOCK_8x8_BACKWARD_ENABLE_INTEL (0xAA << 24)
#define CLK_AVC_ME_SKIP_BLOCK_8x8_DUAL_ENABLE_INTEL (0xFF << 24)
#define CLK_AVC_ME_SKIP_BLOCK_8x8_0_FORWARD_ENABLE_INTEL (0x1 << 24)
#define CLK_AVC_ME_SKIP_BLOCK_8x8_0_BACKWARD_ENABLE_INTEL (0x2 << 24)
#define CLK_AVC_ME_SKIP_BLOCK_8x8_1_FORWARD_ENABLE_INTEL (0x1 << 26)
#define CLK_AVC_ME_SKIP_BLOCK_8x8_1_BACKWARD_ENABLE_INTEL (0x2 << 26)
#define CLK_AVC_ME_SKIP_BLOCK_8x8_2_FORWARD_ENABLE_INTEL (0x1 << 28)
#define CLK_AVC_ME_SKIP_BLOCK_8x8_2_BACKWARD_ENABLE_INTEL (0x2 << 28)
#define CLK_AVC_ME_SKIP_BLOCK_8x8_3_FORWARD_ENABLE_INTEL (0x1 << 30)
#define CLK_AVC_ME_SKIP_BLOCK_8x8_3_BACKWARD_ENABLE_INTEL (0x2 << 30)

#define CLK_AVC_ME_BLOCK_BASED_SKIP_4x4_INTEL 0x00
#define CLK_AVC_ME_BLOCK_BASED_SKIP_8x8_INTEL 0x80

#define CLK_AVC_ME_INTRA_LUMA_PARTITION_MASK_ALL_INTEL 0x0
#define CLK_AVC_ME_INTRA_LUMA_PARTITION_MASK_16x16_INTEL 0x6
#define CLK_AVC_ME_INTRA_LUMA_PARTITION_MASK_8x8_INTEL 0x5
#define CLK_AVC_ME_INTRA_LUMA_PARTITION_MASK_4x4_INTEL 0x3

#define CLK_AVC_ME_INTRA_NEIGHBOR_LEFT_MASK_ENABLE_INTEL 0x60
#define CLK_AVC_ME_INTRA_NEIGHBOR_UPPER_MASK_ENABLE_INTEL 0x10
#define CLK_AVC_ME_INTRA_NEIGHBOR_UPPER_RIGHT_MASK_ENABLE_INTEL 0x8
#define CLK_AVC_ME_INTRA_NEIGHBOR_UPPER_LEFT_MASK_ENABLE_INTEL 0x4

#define CLK_AVC_ME_LUMA_PREDICTOR_MODE_VERTICAL_INTEL 0x0
#define CLK_AVC_ME_LUMA_PREDICTOR_MODE_HORIZONTAL_INTEL 0x1
#define CLK_AVC_ME_LUMA_PREDICTOR_MODE_DC_INTEL 0x2
#define CLK_AVC_ME_LUMA_PREDICTOR_MODE_DIAGONAL_DOWN_LEFT_INTEL 0x3
#define CLK_AVC_ME_LUMA_PREDICTOR_MODE_DIAGONAL_DOWN_RIGHT_INTEL 0x4
#define CLK_AVC_ME_LUMA_PREDICTOR_MODE_PLANE_INTEL 0x4
#define CLK_AVC_ME_LUMA_PREDICTOR_MODE_VERTICAL_RIGHT_INTEL 0x5
#define CLK_AVC_ME_LUMA_PREDICTOR_MODE_HORIZONTAL_DOWN_INTEL 0x6
#define CLK_AVC_ME_LUMA_PREDICTOR_MODE_VERTICAL_LEFT_INTEL 0x7
#define CLK_AVC_ME_LUMA_PREDICTOR_MODE_HORIZONTAL_UP_INTEL 0x8
#define CLK_AVC_ME_CHROMA_PREDICTOR_MODE_DC_INTEL 0x0
#define CLK_AVC_ME_CHROMA_PREDICTOR_MODE_HORIZONTAL_INTEL 0x1
#define CLK_AVC_ME_CHROMA_PREDICTOR_MODE_VERTICAL_INTEL 0x2
#define CLK_AVC_ME_CHROMA_PREDICTOR_MODE_PLANE_INTEL 0x3

#define CLK_AVC_ME_FRAME_FORWARD_INTEL 0x1
#define CLK_AVC_ME_FRAME_BACKWARD_INTEL 0x2
#define CLK_AVC_ME_FRAME_DUAL_INTEL 0x3

#define CLK_AVC_ME_INTERLACED_SCAN_TOP_FIELD_INTEL 0x0
#define CLK_AVC_ME_INTERLACED_SCAN_BOTTOM_FIELD_INTEL 0x1

#define CLK_AVC_ME_INITIALIZE_INTEL 0x0

#define CLK_AVC_IME_PAYLOAD_INITIALIZE_INTEL 0x0
#define CLK_AVC_REF_PAYLOAD_INITIALIZE_INTEL 0x0
#define CLK_AVC_SIC_PAYLOAD_INITIALIZE_INTEL 0x0

#define CLK_AVC_IME_RESULT_INITIALIZE_INTEL 0x0
#define CLK_AVC_REF_RESULT_INITIALIZE_INTEL 0x0
#define CLK_AVC_SIC_RESULT_INITIALIZE_INTEL 0x0

#define CLK_AVC_IME_RESULT_SINGLE_REFERENCE_STREAMOUT_INITIALIZE_INTEL 0x0
#define CLK_AVC_IME_RESULT_SINGLE_REFERENCE_STREAMIN_INITIALIZE_INTEL 0x0
#define CLK_AVC_IME_RESULT_DUAL_REFERENCE_STREAMOUT_INITIALIZE_INTEL 0x0
#define CLK_AVC_IME_RESULT_DUAL_REFERENCE_STREAMIN_INITIALIZE_INTEL 0x0

#endif // cl_intel_device_side_avc_motion_estimation

// Disable any extensions we may have enabled previously.
#pragma OPENCL EXTENSION all : disable

#endif //_OPENCL_BASE_H_
