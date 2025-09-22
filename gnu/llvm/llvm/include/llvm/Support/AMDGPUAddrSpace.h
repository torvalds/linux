//===---------------- AMDGPUAddrSpace.h -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// AMDGPU address space definition
///
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_AMDGPUADDRSPACE_H
#define LLVM_SUPPORT_AMDGPUADDRSPACE_H

namespace llvm {
/// OpenCL uses address spaces to differentiate between
/// various memory regions on the hardware. On the CPU
/// all of the address spaces point to the same memory,
/// however on the GPU, each address space points to
/// a separate piece of memory that is unique from other
/// memory locations.
namespace AMDGPUAS {
enum : unsigned {
  // The maximum value for flat, generic, local, private, constant and region.
  MAX_AMDGPU_ADDRESS = 9,

  FLAT_ADDRESS = 0,   ///< Address space for flat memory.
  GLOBAL_ADDRESS = 1, ///< Address space for global memory (RAT0, VTX0).
  REGION_ADDRESS = 2, ///< Address space for region memory. (GDS)

  CONSTANT_ADDRESS = 4, ///< Address space for constant memory (VTX2).
  LOCAL_ADDRESS = 3,    ///< Address space for local memory.
  PRIVATE_ADDRESS = 5,  ///< Address space for private memory.

  CONSTANT_ADDRESS_32BIT = 6, ///< Address space for 32-bit constant memory.

  BUFFER_FAT_POINTER = 7, ///< Address space for 160-bit buffer fat pointers.
                          ///< Not used in backend.

  BUFFER_RESOURCE = 8, ///< Address space for 128-bit buffer resources.

  BUFFER_STRIDED_POINTER = 9, ///< Address space for 192-bit fat buffer
                              ///< pointers with an additional index.

  /// Internal address spaces. Can be freely renumbered.
  STREAMOUT_REGISTER = 128, ///< Address space for GS NGG Streamout registers.
  /// end Internal address spaces.

  /// Address space for direct addressable parameter memory (CONST0).
  PARAM_D_ADDRESS = 6,
  /// Address space for indirect addressable parameter memory (VTX1).
  PARAM_I_ADDRESS = 7,

  // Do not re-order the CONSTANT_BUFFER_* enums.  Several places depend on
  // this order to be able to dynamically index a constant buffer, for
  // example:
  //
  // ConstantBufferAS = CONSTANT_BUFFER_0 + CBIdx

  CONSTANT_BUFFER_0 = 8,
  CONSTANT_BUFFER_1 = 9,
  CONSTANT_BUFFER_2 = 10,
  CONSTANT_BUFFER_3 = 11,
  CONSTANT_BUFFER_4 = 12,
  CONSTANT_BUFFER_5 = 13,
  CONSTANT_BUFFER_6 = 14,
  CONSTANT_BUFFER_7 = 15,
  CONSTANT_BUFFER_8 = 16,
  CONSTANT_BUFFER_9 = 17,
  CONSTANT_BUFFER_10 = 18,
  CONSTANT_BUFFER_11 = 19,
  CONSTANT_BUFFER_12 = 20,
  CONSTANT_BUFFER_13 = 21,
  CONSTANT_BUFFER_14 = 22,
  CONSTANT_BUFFER_15 = 23,

  // Some places use this if the address space can't be determined.
  UNKNOWN_ADDRESS_SPACE = ~0u,
};
} // end namespace AMDGPUAS
} // end namespace llvm

#endif // LLVM_SUPPORT_AMDGPUADDRSPACE_H
