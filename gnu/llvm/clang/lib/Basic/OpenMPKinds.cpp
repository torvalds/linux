//===--- OpenMPKinds.cpp - Token Kinds Support ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file implements the OpenMP enum and support functions.
///
//===----------------------------------------------------------------------===//

#include "clang/Basic/OpenMPKinds.h"
#include "clang/Basic/IdentifierTable.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>

using namespace clang;
using namespace llvm::omp;

unsigned clang::getOpenMPSimpleClauseType(OpenMPClauseKind Kind, StringRef Str,
                                          const LangOptions &LangOpts) {
  switch (Kind) {
  case OMPC_default:
    return llvm::StringSwitch<unsigned>(Str)
#define OMP_DEFAULT_KIND(Enum, Name) .Case(Name, unsigned(Enum))
#include "llvm/Frontend/OpenMP/OMPKinds.def"
        .Default(unsigned(llvm::omp::OMP_DEFAULT_unknown));
  case OMPC_proc_bind:
    return llvm::StringSwitch<unsigned>(Str)
#define OMP_PROC_BIND_KIND(Enum, Name, Value) .Case(Name, Value)
#include "llvm/Frontend/OpenMP/OMPKinds.def"
        .Default(unsigned(llvm::omp::OMP_PROC_BIND_unknown));
  case OMPC_schedule:
    return llvm::StringSwitch<unsigned>(Str)
#define OPENMP_SCHEDULE_KIND(Name)                                             \
  .Case(#Name, static_cast<unsigned>(OMPC_SCHEDULE_##Name))
#define OPENMP_SCHEDULE_MODIFIER(Name)                                         \
  .Case(#Name, static_cast<unsigned>(OMPC_SCHEDULE_MODIFIER_##Name))
#include "clang/Basic/OpenMPKinds.def"
        .Default(OMPC_SCHEDULE_unknown);
  case OMPC_depend: {
    unsigned Type = llvm::StringSwitch<unsigned>(Str)
#define OPENMP_DEPEND_KIND(Name) .Case(#Name, OMPC_DEPEND_##Name)
#include "clang/Basic/OpenMPKinds.def"
                        .Default(OMPC_DEPEND_unknown);
    if (LangOpts.OpenMP < 51 && Type == OMPC_DEPEND_inoutset)
      return OMPC_DEPEND_unknown;
    return Type;
  }
  case OMPC_doacross:
    return llvm::StringSwitch<OpenMPDoacrossClauseModifier>(Str)
#define OPENMP_DOACROSS_MODIFIER(Name) .Case(#Name, OMPC_DOACROSS_##Name)
#include "clang/Basic/OpenMPKinds.def"
        .Default(OMPC_DOACROSS_unknown);
  case OMPC_linear:
    return llvm::StringSwitch<OpenMPLinearClauseKind>(Str)
#define OPENMP_LINEAR_KIND(Name) .Case(#Name, OMPC_LINEAR_##Name)
#include "clang/Basic/OpenMPKinds.def"
        .Default(OMPC_LINEAR_unknown);
  case OMPC_map: {
    unsigned Type = llvm::StringSwitch<unsigned>(Str)
#define OPENMP_MAP_KIND(Name)                                                  \
  .Case(#Name, static_cast<unsigned>(OMPC_MAP_##Name))
#define OPENMP_MAP_MODIFIER_KIND(Name)                                         \
  .Case(#Name, static_cast<unsigned>(OMPC_MAP_MODIFIER_##Name))
#include "clang/Basic/OpenMPKinds.def"
        .Default(OMPC_MAP_unknown);
    if (LangOpts.OpenMP < 51 && Type == OMPC_MAP_MODIFIER_present)
      return OMPC_MAP_MODIFIER_unknown;
    if (!LangOpts.OpenMPExtensions && Type == OMPC_MAP_MODIFIER_ompx_hold)
      return OMPC_MAP_MODIFIER_unknown;
    return Type;
  }
  case OMPC_to:
  case OMPC_from: {
    unsigned Type = llvm::StringSwitch<unsigned>(Str)
#define OPENMP_MOTION_MODIFIER_KIND(Name)                                      \
  .Case(#Name, static_cast<unsigned>(OMPC_MOTION_MODIFIER_##Name))
#include "clang/Basic/OpenMPKinds.def"
        .Default(OMPC_MOTION_MODIFIER_unknown);
    if (LangOpts.OpenMP < 51 && Type == OMPC_MOTION_MODIFIER_present)
      return OMPC_MOTION_MODIFIER_unknown;
    return Type;
  }
  case OMPC_dist_schedule:
    return llvm::StringSwitch<OpenMPDistScheduleClauseKind>(Str)
#define OPENMP_DIST_SCHEDULE_KIND(Name) .Case(#Name, OMPC_DIST_SCHEDULE_##Name)
#include "clang/Basic/OpenMPKinds.def"
        .Default(OMPC_DIST_SCHEDULE_unknown);
  case OMPC_defaultmap:
    return llvm::StringSwitch<unsigned>(Str)
#define OPENMP_DEFAULTMAP_KIND(Name)                                           \
  .Case(#Name, static_cast<unsigned>(OMPC_DEFAULTMAP_##Name))
#define OPENMP_DEFAULTMAP_MODIFIER(Name)                                       \
  .Case(#Name, static_cast<unsigned>(OMPC_DEFAULTMAP_MODIFIER_##Name))
#include "clang/Basic/OpenMPKinds.def"
        .Default(OMPC_DEFAULTMAP_unknown);
  case OMPC_atomic_default_mem_order:
     return llvm::StringSwitch<OpenMPAtomicDefaultMemOrderClauseKind>(Str)
#define OPENMP_ATOMIC_DEFAULT_MEM_ORDER_KIND(Name)       \
  .Case(#Name, OMPC_ATOMIC_DEFAULT_MEM_ORDER_##Name)
#include "clang/Basic/OpenMPKinds.def"
        .Default(OMPC_ATOMIC_DEFAULT_MEM_ORDER_unknown);
  case OMPC_fail:
    return static_cast<unsigned int>(llvm::StringSwitch<llvm::omp::Clause>(Str)
#define OPENMP_ATOMIC_FAIL_MODIFIER(Name) .Case(#Name, OMPC_##Name)
#include "clang/Basic/OpenMPKinds.def"
                                         .Default(OMPC_unknown));
  case OMPC_device_type:
    return llvm::StringSwitch<OpenMPDeviceType>(Str)
#define OPENMP_DEVICE_TYPE_KIND(Name) .Case(#Name, OMPC_DEVICE_TYPE_##Name)
#include "clang/Basic/OpenMPKinds.def"
        .Default(OMPC_DEVICE_TYPE_unknown);
  case OMPC_at:
    return llvm::StringSwitch<OpenMPAtClauseKind>(Str)
#define OPENMP_AT_KIND(Name) .Case(#Name, OMPC_AT_##Name)
#include "clang/Basic/OpenMPKinds.def"
        .Default(OMPC_AT_unknown);
  case OMPC_severity:
    return llvm::StringSwitch<OpenMPSeverityClauseKind>(Str)
#define OPENMP_SEVERITY_KIND(Name) .Case(#Name, OMPC_SEVERITY_##Name)
#include "clang/Basic/OpenMPKinds.def"
        .Default(OMPC_SEVERITY_unknown);
  case OMPC_lastprivate:
    return llvm::StringSwitch<OpenMPLastprivateModifier>(Str)
#define OPENMP_LASTPRIVATE_KIND(Name) .Case(#Name, OMPC_LASTPRIVATE_##Name)
#include "clang/Basic/OpenMPKinds.def"
        .Default(OMPC_LASTPRIVATE_unknown);
  case OMPC_order:
    return llvm::StringSwitch<unsigned>(Str)
#define OPENMP_ORDER_KIND(Name)                                                \
  .Case(#Name, static_cast<unsigned>(OMPC_ORDER_##Name))
#define OPENMP_ORDER_MODIFIER(Name)                                            \
  .Case(#Name, static_cast<unsigned>(OMPC_ORDER_MODIFIER_##Name))
#include "clang/Basic/OpenMPKinds.def"
        .Default(OMPC_ORDER_unknown);
  case OMPC_update:
    return llvm::StringSwitch<OpenMPDependClauseKind>(Str)
#define OPENMP_DEPEND_KIND(Name) .Case(#Name, OMPC_DEPEND_##Name)
#include "clang/Basic/OpenMPKinds.def"
        .Default(OMPC_DEPEND_unknown);
  case OMPC_device:
    return llvm::StringSwitch<OpenMPDeviceClauseModifier>(Str)
#define OPENMP_DEVICE_MODIFIER(Name) .Case(#Name, OMPC_DEVICE_##Name)
#include "clang/Basic/OpenMPKinds.def"
        .Default(OMPC_DEVICE_unknown);
  case OMPC_reduction:
    return llvm::StringSwitch<OpenMPReductionClauseModifier>(Str)
#define OPENMP_REDUCTION_MODIFIER(Name) .Case(#Name, OMPC_REDUCTION_##Name)
#include "clang/Basic/OpenMPKinds.def"
        .Default(OMPC_REDUCTION_unknown);
  case OMPC_adjust_args:
    return llvm::StringSwitch<OpenMPAdjustArgsOpKind>(Str)
#define OPENMP_ADJUST_ARGS_KIND(Name) .Case(#Name, OMPC_ADJUST_ARGS_##Name)
#include "clang/Basic/OpenMPKinds.def"
        .Default(OMPC_ADJUST_ARGS_unknown);
  case OMPC_bind:
    return llvm::StringSwitch<unsigned>(Str)
#define OPENMP_BIND_KIND(Name) .Case(#Name, OMPC_BIND_##Name)
#include "clang/Basic/OpenMPKinds.def"
        .Default(OMPC_BIND_unknown);
  case OMPC_grainsize: {
    unsigned Type = llvm::StringSwitch<unsigned>(Str)
#define OPENMP_GRAINSIZE_MODIFIER(Name) .Case(#Name, OMPC_GRAINSIZE_##Name)
#include "clang/Basic/OpenMPKinds.def"
                        .Default(OMPC_GRAINSIZE_unknown);
    if (LangOpts.OpenMP < 51)
      return OMPC_GRAINSIZE_unknown;
    return Type;
  }
  case OMPC_num_tasks: {
    unsigned Type = llvm::StringSwitch<unsigned>(Str)
#define OPENMP_NUMTASKS_MODIFIER(Name) .Case(#Name, OMPC_NUMTASKS_##Name)
#include "clang/Basic/OpenMPKinds.def"
                        .Default(OMPC_NUMTASKS_unknown);
    if (LangOpts.OpenMP < 51)
      return OMPC_NUMTASKS_unknown;
    return Type;
  }
  case OMPC_unknown:
  case OMPC_threadprivate:
  case OMPC_if:
  case OMPC_final:
  case OMPC_num_threads:
  case OMPC_safelen:
  case OMPC_simdlen:
  case OMPC_sizes:
  case OMPC_allocator:
  case OMPC_allocate:
  case OMPC_collapse:
  case OMPC_private:
  case OMPC_firstprivate:
  case OMPC_shared:
  case OMPC_task_reduction:
  case OMPC_in_reduction:
  case OMPC_aligned:
  case OMPC_copyin:
  case OMPC_copyprivate:
  case OMPC_ordered:
  case OMPC_nowait:
  case OMPC_untied:
  case OMPC_mergeable:
  case OMPC_flush:
  case OMPC_depobj:
  case OMPC_read:
  case OMPC_write:
  case OMPC_capture:
  case OMPC_compare:
  case OMPC_seq_cst:
  case OMPC_acq_rel:
  case OMPC_acquire:
  case OMPC_release:
  case OMPC_relaxed:
  case OMPC_threads:
  case OMPC_simd:
  case OMPC_num_teams:
  case OMPC_thread_limit:
  case OMPC_priority:
  case OMPC_nogroup:
  case OMPC_hint:
  case OMPC_uniform:
  case OMPC_use_device_ptr:
  case OMPC_use_device_addr:
  case OMPC_is_device_ptr:
  case OMPC_has_device_addr:
  case OMPC_unified_address:
  case OMPC_unified_shared_memory:
  case OMPC_reverse_offload:
  case OMPC_dynamic_allocators:
  case OMPC_match:
  case OMPC_nontemporal:
  case OMPC_destroy:
  case OMPC_novariants:
  case OMPC_nocontext:
  case OMPC_detach:
  case OMPC_inclusive:
  case OMPC_exclusive:
  case OMPC_uses_allocators:
  case OMPC_affinity:
  case OMPC_when:
  case OMPC_append_args:
    break;
  default:
    break;
  }
  llvm_unreachable("Invalid OpenMP simple clause kind");
}

const char *clang::getOpenMPSimpleClauseTypeName(OpenMPClauseKind Kind,
                                                 unsigned Type) {
  switch (Kind) {
  case OMPC_default:
    switch (llvm::omp::DefaultKind(Type)) {
#define OMP_DEFAULT_KIND(Enum, Name)                                           \
  case Enum:                                                                   \
    return Name;
#include "llvm/Frontend/OpenMP/OMPKinds.def"
    }
    llvm_unreachable("Invalid OpenMP 'default' clause type");
  case OMPC_proc_bind:
    switch (Type) {
#define OMP_PROC_BIND_KIND(Enum, Name, Value)                                  \
  case Value:                                                                  \
    return Name;
#include "llvm/Frontend/OpenMP/OMPKinds.def"
    }
    llvm_unreachable("Invalid OpenMP 'proc_bind' clause type");
  case OMPC_schedule:
    switch (Type) {
    case OMPC_SCHEDULE_unknown:
    case OMPC_SCHEDULE_MODIFIER_last:
      return "unknown";
#define OPENMP_SCHEDULE_KIND(Name)                                             \
    case OMPC_SCHEDULE_##Name:                                                 \
      return #Name;
#define OPENMP_SCHEDULE_MODIFIER(Name)                                         \
    case OMPC_SCHEDULE_MODIFIER_##Name:                                        \
      return #Name;
#include "clang/Basic/OpenMPKinds.def"
    }
    llvm_unreachable("Invalid OpenMP 'schedule' clause type");
  case OMPC_depend:
    switch (Type) {
    case OMPC_DEPEND_unknown:
      return "unknown";
#define OPENMP_DEPEND_KIND(Name)                                             \
  case OMPC_DEPEND_##Name:                                                   \
    return #Name;
#include "clang/Basic/OpenMPKinds.def"
    }
    llvm_unreachable("Invalid OpenMP 'depend' clause type");
  case OMPC_doacross:
    switch (Type) {
    case OMPC_DOACROSS_unknown:
      return "unknown";
#define OPENMP_DOACROSS_MODIFIER(Name)                                         \
  case OMPC_DOACROSS_##Name:                                                   \
    return #Name;
#include "clang/Basic/OpenMPKinds.def"
    }
    llvm_unreachable("Invalid OpenMP 'doacross' clause type");
  case OMPC_linear:
    switch (Type) {
    case OMPC_LINEAR_unknown:
      return "unknown";
#define OPENMP_LINEAR_KIND(Name)                                             \
  case OMPC_LINEAR_##Name:                                                   \
    return #Name;
#include "clang/Basic/OpenMPKinds.def"
    }
    llvm_unreachable("Invalid OpenMP 'linear' clause type");
  case OMPC_map:
    switch (Type) {
    case OMPC_MAP_unknown:
    case OMPC_MAP_MODIFIER_last:
      return "unknown";
#define OPENMP_MAP_KIND(Name)                                                \
  case OMPC_MAP_##Name:                                                      \
    return #Name;
#define OPENMP_MAP_MODIFIER_KIND(Name)                                       \
  case OMPC_MAP_MODIFIER_##Name:                                             \
    return #Name;
#include "clang/Basic/OpenMPKinds.def"
    default:
      break;
    }
    llvm_unreachable("Invalid OpenMP 'map' clause type");
  case OMPC_to:
  case OMPC_from:
    switch (Type) {
    case OMPC_MOTION_MODIFIER_unknown:
      return "unknown";
#define OPENMP_MOTION_MODIFIER_KIND(Name)                                      \
  case OMPC_MOTION_MODIFIER_##Name:                                            \
    return #Name;
#include "clang/Basic/OpenMPKinds.def"
    default:
      break;
    }
    llvm_unreachable("Invalid OpenMP 'to' or 'from' clause type");
  case OMPC_dist_schedule:
    switch (Type) {
    case OMPC_DIST_SCHEDULE_unknown:
      return "unknown";
#define OPENMP_DIST_SCHEDULE_KIND(Name)                                      \
  case OMPC_DIST_SCHEDULE_##Name:                                            \
    return #Name;
#include "clang/Basic/OpenMPKinds.def"
    }
    llvm_unreachable("Invalid OpenMP 'dist_schedule' clause type");
  case OMPC_defaultmap:
    switch (Type) {
    case OMPC_DEFAULTMAP_unknown:
    case OMPC_DEFAULTMAP_MODIFIER_last:
      return "unknown";
#define OPENMP_DEFAULTMAP_KIND(Name)                                         \
    case OMPC_DEFAULTMAP_##Name:                                             \
      return #Name;
#define OPENMP_DEFAULTMAP_MODIFIER(Name)                                     \
    case OMPC_DEFAULTMAP_MODIFIER_##Name:                                    \
      return #Name;
#include "clang/Basic/OpenMPKinds.def"
    }
    llvm_unreachable("Invalid OpenMP 'schedule' clause type");
  case OMPC_atomic_default_mem_order:
    switch (Type) {
    case OMPC_ATOMIC_DEFAULT_MEM_ORDER_unknown:
      return "unknown";
#define OPENMP_ATOMIC_DEFAULT_MEM_ORDER_KIND(Name)                           \
    case OMPC_ATOMIC_DEFAULT_MEM_ORDER_##Name:                               \
      return #Name;
#include "clang/Basic/OpenMPKinds.def"
}
    llvm_unreachable("Invalid OpenMP 'atomic_default_mem_order' clause type");
  case OMPC_device_type:
    switch (Type) {
    case OMPC_DEVICE_TYPE_unknown:
      return "unknown";
#define OPENMP_DEVICE_TYPE_KIND(Name)                                          \
    case OMPC_DEVICE_TYPE_##Name:                                              \
      return #Name;
#include "clang/Basic/OpenMPKinds.def"
    }
    llvm_unreachable("Invalid OpenMP 'device_type' clause type");
  case OMPC_at:
    switch (Type) {
    case OMPC_AT_unknown:
      return "unknown";
#define OPENMP_AT_KIND(Name)                                                   \
  case OMPC_AT_##Name:                                                         \
    return #Name;
#include "clang/Basic/OpenMPKinds.def"
    }
    llvm_unreachable("Invalid OpenMP 'at' clause type");
  case OMPC_severity:
    switch (Type) {
    case OMPC_SEVERITY_unknown:
      return "unknown";
#define OPENMP_SEVERITY_KIND(Name)                                             \
  case OMPC_SEVERITY_##Name:                                                   \
    return #Name;
#include "clang/Basic/OpenMPKinds.def"
    }
    llvm_unreachable("Invalid OpenMP 'severity' clause type");
  case OMPC_lastprivate:
    switch (Type) {
    case OMPC_LASTPRIVATE_unknown:
      return "unknown";
#define OPENMP_LASTPRIVATE_KIND(Name)                                          \
    case OMPC_LASTPRIVATE_##Name:                                              \
      return #Name;
#include "clang/Basic/OpenMPKinds.def"
    }
    llvm_unreachable("Invalid OpenMP 'lastprivate' clause type");
  case OMPC_order:
    switch (Type) {
    case OMPC_ORDER_unknown:
    case OMPC_ORDER_MODIFIER_last:
      return "unknown";
#define OPENMP_ORDER_KIND(Name)                                                \
  case OMPC_ORDER_##Name:                                                      \
    return #Name;
#define OPENMP_ORDER_MODIFIER(Name)                                            \
  case OMPC_ORDER_MODIFIER_##Name:                                             \
    return #Name;
#include "clang/Basic/OpenMPKinds.def"
    }
    llvm_unreachable("Invalid OpenMP 'order' clause type");
  case OMPC_update:
    switch (Type) {
    case OMPC_DEPEND_unknown:
      return "unknown";
#define OPENMP_DEPEND_KIND(Name)                                               \
  case OMPC_DEPEND_##Name:                                                     \
    return #Name;
#include "clang/Basic/OpenMPKinds.def"
    }
    llvm_unreachable("Invalid OpenMP 'depend' clause type");
  case OMPC_fail: {
    OpenMPClauseKind CK = static_cast<OpenMPClauseKind>(Type);
    return getOpenMPClauseName(CK).data();
    llvm_unreachable("Invalid OpenMP 'fail' clause modifier");
  }
  case OMPC_device:
    switch (Type) {
    case OMPC_DEVICE_unknown:
      return "unknown";
#define OPENMP_DEVICE_MODIFIER(Name)                                           \
  case OMPC_DEVICE_##Name:                                                     \
    return #Name;
#include "clang/Basic/OpenMPKinds.def"
    }
    llvm_unreachable("Invalid OpenMP 'device' clause modifier");
  case OMPC_reduction:
    switch (Type) {
    case OMPC_REDUCTION_unknown:
      return "unknown";
#define OPENMP_REDUCTION_MODIFIER(Name)                                        \
  case OMPC_REDUCTION_##Name:                                                  \
    return #Name;
#include "clang/Basic/OpenMPKinds.def"
    }
    llvm_unreachable("Invalid OpenMP 'reduction' clause modifier");
  case OMPC_adjust_args:
    switch (Type) {
    case OMPC_ADJUST_ARGS_unknown:
      return "unknown";
#define OPENMP_ADJUST_ARGS_KIND(Name)                                          \
  case OMPC_ADJUST_ARGS_##Name:                                                \
    return #Name;
#include "clang/Basic/OpenMPKinds.def"
    }
    llvm_unreachable("Invalid OpenMP 'adjust_args' clause kind");
  case OMPC_bind:
    switch (Type) {
    case OMPC_BIND_unknown:
      return "unknown";
#define OPENMP_BIND_KIND(Name)                                                 \
  case OMPC_BIND_##Name:                                                       \
    return #Name;
#include "clang/Basic/OpenMPKinds.def"
    }
    llvm_unreachable("Invalid OpenMP 'bind' clause type");
  case OMPC_grainsize:
    switch (Type) {
    case OMPC_GRAINSIZE_unknown:
      return "unknown";
#define OPENMP_GRAINSIZE_MODIFIER(Name)                                        \
  case OMPC_GRAINSIZE_##Name:                                                  \
    return #Name;
#include "clang/Basic/OpenMPKinds.def"
    }
    llvm_unreachable("Invalid OpenMP 'grainsize' clause modifier");
  case OMPC_num_tasks:
    switch (Type) {
    case OMPC_NUMTASKS_unknown:
      return "unknown";
#define OPENMP_NUMTASKS_MODIFIER(Name)                                         \
  case OMPC_NUMTASKS_##Name:                                                   \
    return #Name;
#include "clang/Basic/OpenMPKinds.def"
    }
    llvm_unreachable("Invalid OpenMP 'num_tasks' clause modifier");
  case OMPC_unknown:
  case OMPC_threadprivate:
  case OMPC_if:
  case OMPC_final:
  case OMPC_num_threads:
  case OMPC_safelen:
  case OMPC_simdlen:
  case OMPC_sizes:
  case OMPC_allocator:
  case OMPC_allocate:
  case OMPC_collapse:
  case OMPC_private:
  case OMPC_firstprivate:
  case OMPC_shared:
  case OMPC_task_reduction:
  case OMPC_in_reduction:
  case OMPC_aligned:
  case OMPC_copyin:
  case OMPC_copyprivate:
  case OMPC_ordered:
  case OMPC_nowait:
  case OMPC_untied:
  case OMPC_mergeable:
  case OMPC_flush:
  case OMPC_depobj:
  case OMPC_read:
  case OMPC_write:
  case OMPC_capture:
  case OMPC_compare:
  case OMPC_seq_cst:
  case OMPC_acq_rel:
  case OMPC_acquire:
  case OMPC_release:
  case OMPC_relaxed:
  case OMPC_threads:
  case OMPC_simd:
  case OMPC_num_teams:
  case OMPC_thread_limit:
  case OMPC_priority:
  case OMPC_nogroup:
  case OMPC_hint:
  case OMPC_uniform:
  case OMPC_use_device_ptr:
  case OMPC_use_device_addr:
  case OMPC_is_device_ptr:
  case OMPC_has_device_addr:
  case OMPC_unified_address:
  case OMPC_unified_shared_memory:
  case OMPC_reverse_offload:
  case OMPC_dynamic_allocators:
  case OMPC_match:
  case OMPC_nontemporal:
  case OMPC_destroy:
  case OMPC_detach:
  case OMPC_novariants:
  case OMPC_nocontext:
  case OMPC_inclusive:
  case OMPC_exclusive:
  case OMPC_uses_allocators:
  case OMPC_affinity:
  case OMPC_when:
  case OMPC_append_args:
    break;
  default:
    break;
  }
  llvm_unreachable("Invalid OpenMP simple clause kind");
}

bool clang::isOpenMPLoopDirective(OpenMPDirectiveKind DKind) {
  return getDirectiveAssociation(DKind) == Association::Loop;
}

bool clang::isOpenMPWorksharingDirective(OpenMPDirectiveKind DKind) {
  return DKind == OMPD_for || DKind == OMPD_for_simd ||
         DKind == OMPD_sections || DKind == OMPD_section ||
         DKind == OMPD_single || DKind == OMPD_parallel_for ||
         DKind == OMPD_parallel_for_simd || DKind == OMPD_parallel_sections ||
         DKind == OMPD_target_parallel_for ||
         DKind == OMPD_distribute_parallel_for ||
         DKind == OMPD_distribute_parallel_for_simd ||
         DKind == OMPD_target_parallel_for_simd ||
         DKind == OMPD_teams_distribute_parallel_for_simd ||
         DKind == OMPD_teams_distribute_parallel_for ||
         DKind == OMPD_target_teams_distribute_parallel_for ||
         DKind == OMPD_target_teams_distribute_parallel_for_simd ||
         DKind == OMPD_parallel_loop || DKind == OMPD_teams_loop ||
         DKind == OMPD_target_parallel_loop || DKind == OMPD_target_teams_loop;
}

bool clang::isOpenMPTaskLoopDirective(OpenMPDirectiveKind DKind) {
  return DKind == OMPD_taskloop ||
         llvm::is_contained(getLeafConstructs(DKind), OMPD_taskloop);
}

bool clang::isOpenMPParallelDirective(OpenMPDirectiveKind DKind) {
  if (DKind == OMPD_teams_loop)
    return true;
  return DKind == OMPD_parallel ||
         llvm::is_contained(getLeafConstructs(DKind), OMPD_parallel);
}

bool clang::isOpenMPTargetExecutionDirective(OpenMPDirectiveKind DKind) {
  return DKind == OMPD_target ||
         llvm::is_contained(getLeafConstructs(DKind), OMPD_target);
}

bool clang::isOpenMPTargetDataManagementDirective(OpenMPDirectiveKind DKind) {
  return DKind == OMPD_target_data || DKind == OMPD_target_enter_data ||
         DKind == OMPD_target_exit_data || DKind == OMPD_target_update;
}

bool clang::isOpenMPNestingTeamsDirective(OpenMPDirectiveKind DKind) {
  if (DKind == OMPD_teams)
    return true;
  ArrayRef<Directive> Leaves = getLeafConstructs(DKind);
  return !Leaves.empty() && Leaves.front() == OMPD_teams;
}

bool clang::isOpenMPTeamsDirective(OpenMPDirectiveKind DKind) {
  return DKind == OMPD_teams ||
         llvm::is_contained(getLeafConstructs(DKind), OMPD_teams);
}

bool clang::isOpenMPSimdDirective(OpenMPDirectiveKind DKind) {
  // Avoid OMPD_declare_simd
  if (getDirectiveAssociation(DKind) != Association::Loop)
    return false;
  // Formally, OMPD_end_do_simd also has a loop association, but
  // it's a Fortran-specific directive.

  return DKind == OMPD_simd ||
         llvm::is_contained(getLeafConstructs(DKind), OMPD_simd);
}

bool clang::isOpenMPNestingDistributeDirective(OpenMPDirectiveKind Kind) {
  if (Kind == OMPD_distribute)
    return true;
  ArrayRef<Directive> Leaves = getLeafConstructs(Kind);
  return !Leaves.empty() && Leaves.front() == OMPD_distribute;
}

bool clang::isOpenMPDistributeDirective(OpenMPDirectiveKind Kind) {
  return Kind == OMPD_distribute ||
         llvm::is_contained(getLeafConstructs(Kind), OMPD_distribute);
}

bool clang::isOpenMPGenericLoopDirective(OpenMPDirectiveKind Kind) {
  if (Kind == OMPD_loop)
    return true;
  ArrayRef<Directive> Leaves = getLeafConstructs(Kind);
  return !Leaves.empty() && Leaves.back() == OMPD_loop;
}

bool clang::isOpenMPPrivate(OpenMPClauseKind Kind) {
  return Kind == OMPC_private || Kind == OMPC_firstprivate ||
         Kind == OMPC_lastprivate || Kind == OMPC_linear ||
         Kind == OMPC_reduction || Kind == OMPC_task_reduction ||
         Kind == OMPC_in_reduction; // TODO add next clauses like 'reduction'.
}

bool clang::isOpenMPThreadPrivate(OpenMPClauseKind Kind) {
  return Kind == OMPC_threadprivate || Kind == OMPC_copyin;
}

bool clang::isOpenMPTaskingDirective(OpenMPDirectiveKind Kind) {
  return Kind == OMPD_task || isOpenMPTaskLoopDirective(Kind);
}

bool clang::isOpenMPLoopBoundSharingDirective(OpenMPDirectiveKind Kind) {
  return Kind == OMPD_distribute_parallel_for ||
         Kind == OMPD_distribute_parallel_for_simd ||
         Kind == OMPD_teams_distribute_parallel_for_simd ||
         Kind == OMPD_teams_distribute_parallel_for ||
         Kind == OMPD_target_teams_distribute_parallel_for ||
         Kind == OMPD_target_teams_distribute_parallel_for_simd ||
         Kind == OMPD_teams_loop || Kind == OMPD_target_teams_loop;
}

bool clang::isOpenMPLoopTransformationDirective(OpenMPDirectiveKind DKind) {
  return DKind == OMPD_tile || DKind == OMPD_unroll || DKind == OMPD_reverse ||
         DKind == OMPD_interchange;
}

bool clang::isOpenMPCombinedParallelADirective(OpenMPDirectiveKind DKind) {
  return DKind == OMPD_parallel_for || DKind == OMPD_parallel_for_simd ||
         DKind == OMPD_parallel_master ||
         DKind == OMPD_parallel_master_taskloop ||
         DKind == OMPD_parallel_master_taskloop_simd ||
         DKind == OMPD_parallel_sections;
}

bool clang::needsTaskBasedThreadLimit(OpenMPDirectiveKind DKind) {
  return DKind == OMPD_target || DKind == OMPD_target_parallel ||
         DKind == OMPD_target_parallel_for ||
         DKind == OMPD_target_parallel_for_simd || DKind == OMPD_target_simd ||
         DKind == OMPD_target_parallel_loop;
}

bool clang::isOpenMPExecutableDirective(OpenMPDirectiveKind DKind) {
  if (DKind == OMPD_error)
    return true;
  Category Cat = getDirectiveCategory(DKind);
  return Cat == Category::Executable || Cat == Category::Subsidiary;
}

bool clang::isOpenMPCapturingDirective(OpenMPDirectiveKind DKind) {
  if (isOpenMPExecutableDirective(DKind)) {
    switch (DKind) {
    case OMPD_atomic:
    case OMPD_barrier:
    case OMPD_cancel:
    case OMPD_cancellation_point:
    case OMPD_critical:
    case OMPD_depobj:
    case OMPD_error:
    case OMPD_flush:
    case OMPD_masked:
    case OMPD_master:
    case OMPD_section:
    case OMPD_taskwait:
    case OMPD_taskyield:
      return false;
    default:
      return !isOpenMPLoopTransformationDirective(DKind);
    }
  }
  // Non-executable directives.
  switch (DKind) {
  case OMPD_metadirective:
  case OMPD_nothing:
    return true;
  default:
    break;
  }
  return false;
}

void clang::getOpenMPCaptureRegions(
    SmallVectorImpl<OpenMPDirectiveKind> &CaptureRegions,
    OpenMPDirectiveKind DKind) {
  assert(unsigned(DKind) < llvm::omp::Directive_enumSize);
  assert(isOpenMPCapturingDirective(DKind) && "Expecting capturing directive");

  auto GetRegionsForLeaf = [&](OpenMPDirectiveKind LKind) {
    assert(isLeafConstruct(LKind) && "Epecting leaf directive");
    // Whether a leaf would require OMPD_unknown if it occured on its own.
    switch (LKind) {
    case OMPD_metadirective:
      CaptureRegions.push_back(OMPD_metadirective);
      break;
    case OMPD_nothing:
      CaptureRegions.push_back(OMPD_nothing);
      break;
    case OMPD_parallel:
      CaptureRegions.push_back(OMPD_parallel);
      break;
    case OMPD_target:
      CaptureRegions.push_back(OMPD_task);
      CaptureRegions.push_back(OMPD_target);
      break;
    case OMPD_task:
    case OMPD_target_enter_data:
    case OMPD_target_exit_data:
    case OMPD_target_update:
      CaptureRegions.push_back(OMPD_task);
      break;
    case OMPD_teams:
      CaptureRegions.push_back(OMPD_teams);
      break;
    case OMPD_taskloop:
      CaptureRegions.push_back(OMPD_taskloop);
      break;
    case OMPD_loop:
      // TODO: 'loop' may require different capture regions depending on the
      // bind clause or the parent directive when there is no bind clause.
      // If any of the directives that push regions here are parents of 'loop',
      // assume 'parallel'. Otherwise do nothing.
      if (!CaptureRegions.empty() &&
          !llvm::is_contained(CaptureRegions, OMPD_parallel))
        CaptureRegions.push_back(OMPD_parallel);
      else
        return true;
      break;
    case OMPD_dispatch:
    case OMPD_distribute:
    case OMPD_for:
    case OMPD_masked:
    case OMPD_master:
    case OMPD_ordered:
    case OMPD_scope:
    case OMPD_sections:
    case OMPD_simd:
    case OMPD_single:
    case OMPD_target_data:
    case OMPD_taskgroup:
      // These directives (when standalone) use OMPD_unknown as the region,
      // but when they're constituents of a compound directive, and other
      // leafs from that directive have specific regions, then these directives
      // add no additional regions.
      return true;
    default:
      llvm::errs() << getOpenMPDirectiveName(LKind) << '\n';
      llvm_unreachable("Unexpected directive");
    }
    return false;
  };

  bool MayNeedUnknownRegion = false;
  for (OpenMPDirectiveKind L : getLeafConstructsOrSelf(DKind))
    MayNeedUnknownRegion |= GetRegionsForLeaf(L);

  // We need OMPD_unknown when no regions were added, and specific leaf
  // constructs were present. Push a single OMPD_unknown as the capture
  /// region.
  if (CaptureRegions.empty() && MayNeedUnknownRegion)
    CaptureRegions.push_back(OMPD_unknown);

  // OMPD_unknown is only expected as the only region. If other regions
  // are present OMPD_unknown should not be present.
  assert((CaptureRegions[0] == OMPD_unknown ||
          !llvm::is_contained(CaptureRegions, OMPD_unknown)) &&
         "Misplaced OMPD_unknown");
}

bool clang::checkFailClauseParameter(OpenMPClauseKind FailClauseParameter) {
  return FailClauseParameter == llvm::omp::OMPC_acquire ||
         FailClauseParameter == llvm::omp::OMPC_relaxed ||
         FailClauseParameter == llvm::omp::OMPC_seq_cst;
}

