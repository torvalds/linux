//===- OMPConstants.h - OpenMP related constants and helpers ------ C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines constans and helpers used when dealing with OpenMP.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_FRONTEND_OPENMP_OMPCONSTANTS_H
#define LLVM_FRONTEND_OPENMP_OMPCONSTANTS_H

#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Frontend/OpenMP/OMP.h"

namespace llvm {
namespace omp {
LLVM_ENABLE_BITMASK_ENUMS_IN_NAMESPACE();

/// IDs for all Internal Control Variables (ICVs).
enum class InternalControlVar {
#define ICV_DATA_ENV(Enum, ...) Enum,
#include "llvm/Frontend/OpenMP/OMPKinds.def"
};

#define ICV_DATA_ENV(Enum, ...)                                                \
  constexpr auto Enum = omp::InternalControlVar::Enum;
#include "llvm/Frontend/OpenMP/OMPKinds.def"

enum class ICVInitValue {
#define ICV_INIT_VALUE(Enum, Name) Enum,
#include "llvm/Frontend/OpenMP/OMPKinds.def"
};

#define ICV_INIT_VALUE(Enum, Name)                                             \
  constexpr auto Enum = omp::ICVInitValue::Enum;
#include "llvm/Frontend/OpenMP/OMPKinds.def"

/// IDs for all omp runtime library (RTL) functions.
enum class RuntimeFunction {
#define OMP_RTL(Enum, ...) Enum,
#include "llvm/Frontend/OpenMP/OMPKinds.def"
};

#define OMP_RTL(Enum, ...) constexpr auto Enum = omp::RuntimeFunction::Enum;
#include "llvm/Frontend/OpenMP/OMPKinds.def"

/// IDs for the different default kinds.
enum class DefaultKind {
#define OMP_DEFAULT_KIND(Enum, Str) Enum,
#include "llvm/Frontend/OpenMP/OMPKinds.def"
};

#define OMP_DEFAULT_KIND(Enum, ...)                                            \
  constexpr auto Enum = omp::DefaultKind::Enum;
#include "llvm/Frontend/OpenMP/OMPKinds.def"

/// IDs for all omp runtime library ident_t flag encodings (see
/// their defintion in openmp/runtime/src/kmp.h).
enum class IdentFlag {
#define OMP_IDENT_FLAG(Enum, Str, Value) Enum = Value,
#include "llvm/Frontend/OpenMP/OMPKinds.def"
  LLVM_MARK_AS_BITMASK_ENUM(0x7FFFFFFF)
};

#define OMP_IDENT_FLAG(Enum, ...) constexpr auto Enum = omp::IdentFlag::Enum;
#include "llvm/Frontend/OpenMP/OMPKinds.def"

// Version of the kernel argument format used by the omp runtime.
#define OMP_KERNEL_ARG_VERSION 3

// Minimum version of the compiler that generates a kernel dynamic pointer.
#define OMP_KERNEL_ARG_MIN_VERSION_WITH_DYN_PTR 3

/// \note This needs to be kept in sync with kmp.h enum sched_type.
/// Todo: Update kmp.h to include this file, and remove the enums in kmp.h
enum class OMPScheduleType {
  // For typed comparisons, not a valid schedule
  None = 0,

  // Schedule algorithms
  BaseStaticChunked = 1,
  BaseStatic = 2,
  BaseDynamicChunked = 3,
  BaseGuidedChunked = 4,
  BaseRuntime = 5,
  BaseAuto = 6,
  BaseTrapezoidal = 7,
  BaseGreedy = 8,
  BaseBalanced = 9,
  BaseGuidedIterativeChunked = 10,
  BaseGuidedAnalyticalChunked = 11,
  BaseSteal = 12,

  // with chunk adjustment (e.g., simd)
  BaseStaticBalancedChunked = 13,
  BaseGuidedSimd = 14,
  BaseRuntimeSimd = 15,

  // static schedules algorithims for distribute
  BaseDistributeChunked = 27,
  BaseDistribute = 28,

  // Modifier flags to be combined with schedule algorithms
  ModifierUnordered = (1 << 5),
  ModifierOrdered = (1 << 6),
  ModifierNomerge = (1 << 7),
  ModifierMonotonic = (1 << 29),
  ModifierNonmonotonic = (1 << 30),

  // Masks combining multiple flags
  OrderingMask = ModifierUnordered | ModifierOrdered | ModifierNomerge,
  MonotonicityMask = ModifierMonotonic | ModifierNonmonotonic,
  ModifierMask = OrderingMask | MonotonicityMask,

  // valid schedule type values, without monotonicity flags
  UnorderedStaticChunked = BaseStaticChunked | ModifierUnordered,        //  33
  UnorderedStatic = BaseStatic | ModifierUnordered,                      //  34
  UnorderedDynamicChunked = BaseDynamicChunked | ModifierUnordered,      //  35
  UnorderedGuidedChunked = BaseGuidedChunked | ModifierUnordered,        //  36
  UnorderedRuntime = BaseRuntime | ModifierUnordered,                    //  37
  UnorderedAuto = BaseAuto | ModifierUnordered,                          //  38
  UnorderedTrapezoidal = BaseTrapezoidal | ModifierUnordered,            //  39
  UnorderedGreedy = BaseGreedy | ModifierUnordered,                      //  40
  UnorderedBalanced = BaseBalanced | ModifierUnordered,                  //  41
  UnorderedGuidedIterativeChunked =
      BaseGuidedIterativeChunked | ModifierUnordered,                    //  42
  UnorderedGuidedAnalyticalChunked =
      BaseGuidedAnalyticalChunked | ModifierUnordered,                   //  43
  UnorderedSteal = BaseSteal | ModifierUnordered,                        //  44

  UnorderedStaticBalancedChunked =
      BaseStaticBalancedChunked | ModifierUnordered,                     //  45
  UnorderedGuidedSimd = BaseGuidedSimd | ModifierUnordered,              //  46
  UnorderedRuntimeSimd = BaseRuntimeSimd | ModifierUnordered,            //  47

  OrderedStaticChunked = BaseStaticChunked | ModifierOrdered,            //  65
  OrderedStatic = BaseStatic | ModifierOrdered,                          //  66
  OrderedDynamicChunked = BaseDynamicChunked | ModifierOrdered,          //  67
  OrderedGuidedChunked = BaseGuidedChunked | ModifierOrdered,            //  68
  OrderedRuntime = BaseRuntime | ModifierOrdered,                        //  69
  OrderedAuto = BaseAuto | ModifierOrdered,                              //  70
  OrderdTrapezoidal = BaseTrapezoidal | ModifierOrdered,                 //  71

  OrderedDistributeChunked = BaseDistributeChunked | ModifierOrdered,    //  91
  OrderedDistribute = BaseDistribute | ModifierOrdered,                  //  92

  NomergeUnorderedStaticChunked =
      BaseStaticChunked | ModifierUnordered | ModifierNomerge,           // 161
  NomergeUnorderedStatic =
      BaseStatic | ModifierUnordered | ModifierNomerge,                  // 162
  NomergeUnorderedDynamicChunked =
      BaseDynamicChunked | ModifierUnordered | ModifierNomerge,          // 163
  NomergeUnorderedGuidedChunked =
      BaseGuidedChunked | ModifierUnordered | ModifierNomerge,           // 164
  NomergeUnorderedRuntime =
      BaseRuntime | ModifierUnordered | ModifierNomerge,                 // 165
  NomergeUnorderedAuto = BaseAuto | ModifierUnordered | ModifierNomerge, // 166
  NomergeUnorderedTrapezoidal =
      BaseTrapezoidal | ModifierUnordered | ModifierNomerge,             // 167
  NomergeUnorderedGreedy =
      BaseGreedy | ModifierUnordered | ModifierNomerge,                  // 168
  NomergeUnorderedBalanced =
      BaseBalanced | ModifierUnordered | ModifierNomerge,                // 169
  NomergeUnorderedGuidedIterativeChunked =
      BaseGuidedIterativeChunked | ModifierUnordered | ModifierNomerge,  // 170
  NomergeUnorderedGuidedAnalyticalChunked =
      BaseGuidedAnalyticalChunked | ModifierUnordered | ModifierNomerge, // 171
  NomergeUnorderedSteal =
      BaseSteal | ModifierUnordered | ModifierNomerge,                   // 172

  NomergeOrderedStaticChunked =
      BaseStaticChunked | ModifierOrdered | ModifierNomerge,             // 193
  NomergeOrderedStatic = BaseStatic | ModifierOrdered | ModifierNomerge, // 194
  NomergeOrderedDynamicChunked =
      BaseDynamicChunked | ModifierOrdered | ModifierNomerge,            // 195
  NomergeOrderedGuidedChunked =
      BaseGuidedChunked | ModifierOrdered | ModifierNomerge,             // 196
  NomergeOrderedRuntime =
      BaseRuntime | ModifierOrdered | ModifierNomerge,                   // 197
  NomergeOrderedAuto = BaseAuto | ModifierOrdered | ModifierNomerge,     // 198
  NomergeOrderedTrapezoidal =
      BaseTrapezoidal | ModifierOrdered | ModifierNomerge,               // 199

  LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue */ ModifierMask)
};

/// Values for bit flags used to specify the mapping type for
/// offloading.
enum class OpenMPOffloadMappingFlags : uint64_t {
  /// No flags
  OMP_MAP_NONE = 0x0,
  /// Allocate memory on the device and move data from host to device.
  OMP_MAP_TO = 0x01,
  /// Allocate memory on the device and move data from device to host.
  OMP_MAP_FROM = 0x02,
  /// Always perform the requested mapping action on the element, even
  /// if it was already mapped before.
  OMP_MAP_ALWAYS = 0x04,
  /// Delete the element from the device environment, ignoring the
  /// current reference count associated with the element.
  OMP_MAP_DELETE = 0x08,
  /// The element being mapped is a pointer-pointee pair; both the
  /// pointer and the pointee should be mapped.
  OMP_MAP_PTR_AND_OBJ = 0x10,
  /// This flags signals that the base address of an entry should be
  /// passed to the target kernel as an argument.
  OMP_MAP_TARGET_PARAM = 0x20,
  /// Signal that the runtime library has to return the device pointer
  /// in the current position for the data being mapped. Used when we have the
  /// use_device_ptr or use_device_addr clause.
  OMP_MAP_RETURN_PARAM = 0x40,
  /// This flag signals that the reference being passed is a pointer to
  /// private data.
  OMP_MAP_PRIVATE = 0x80,
  /// Pass the element to the device by value.
  OMP_MAP_LITERAL = 0x100,
  /// Implicit map
  OMP_MAP_IMPLICIT = 0x200,
  /// Close is a hint to the runtime to allocate memory close to
  /// the target device.
  OMP_MAP_CLOSE = 0x400,
  /// 0x800 is reserved for compatibility with XLC.
  /// Produce a runtime error if the data is not already allocated.
  OMP_MAP_PRESENT = 0x1000,
  // Increment and decrement a separate reference counter so that the data
  // cannot be unmapped within the associated region.  Thus, this flag is
  // intended to be used on 'target' and 'target data' directives because they
  // are inherently structured.  It is not intended to be used on 'target
  // enter data' and 'target exit data' directives because they are inherently
  // dynamic.
  // This is an OpenMP extension for the sake of OpenACC support.
  OMP_MAP_OMPX_HOLD = 0x2000,
  /// Signal that the runtime library should use args as an array of
  /// descriptor_dim pointers and use args_size as dims. Used when we have
  /// non-contiguous list items in target update directive
  OMP_MAP_NON_CONTIG = 0x100000000000,
  /// The 16 MSBs of the flags indicate whether the entry is member of some
  /// struct/class.
  OMP_MAP_MEMBER_OF = 0xffff000000000000,
  LLVM_MARK_AS_BITMASK_ENUM(/* LargestFlag = */ OMP_MAP_MEMBER_OF)
};

enum OpenMPOffloadingReservedDeviceIDs {
  /// Device ID if the device was not defined, runtime should get it
  /// from environment variables in the spec.
  OMP_DEVICEID_UNDEF = -1
};

enum class AddressSpace : unsigned {
  Generic = 0,
  Global = 1,
  Shared = 3,
  Constant = 4,
  Local = 5,
};

/// \note This needs to be kept in sync with interop.h enum kmp_interop_type_t.:
enum class OMPInteropType { Unknown, Target, TargetSync };

/// Atomic compare operations. Currently OpenMP only supports ==, >, and <.
enum class OMPAtomicCompareOp : unsigned { EQ, MIN, MAX };

/// Fields ids in kmp_depend_info record.
enum class RTLDependInfoFields { BaseAddr, Len, Flags };

/// Dependence kind for RTL.
enum class RTLDependenceKindTy {
  DepUnknown = 0x0,
  DepIn = 0x01,
  DepInOut = 0x3,
  DepMutexInOutSet = 0x4,
  DepInOutSet = 0x8,
  DepOmpAllMem = 0x80,
};

/// A type of worksharing loop construct
enum class WorksharingLoopType {
  // Worksharing `for`-loop
  ForStaticLoop,
  // Worksharing `distrbute`-loop
  DistributeStaticLoop,
  // Worksharing `distrbute parallel for`-loop
  DistributeForStaticLoop
};

} // end namespace omp

} // end namespace llvm

#include "OMPDeviceConstants.h"

#endif // LLVM_FRONTEND_OPENMP_OMPCONSTANTS_H
