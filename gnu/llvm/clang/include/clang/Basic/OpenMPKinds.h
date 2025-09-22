//===--- OpenMPKinds.h - OpenMP enums ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines some OpenMP-specific enums and functions.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_OPENMPKINDS_H
#define LLVM_CLANG_BASIC_OPENMPKINDS_H

#include "clang/Basic/LangOptions.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Frontend/OpenMP/OMPConstants.h"

namespace clang {

/// OpenMP directives.
using OpenMPDirectiveKind = llvm::omp::Directive;

/// OpenMP clauses.
using OpenMPClauseKind = llvm::omp::Clause;

/// OpenMP attributes for 'schedule' clause.
enum OpenMPScheduleClauseKind {
#define OPENMP_SCHEDULE_KIND(Name) \
  OMPC_SCHEDULE_##Name,
#include "clang/Basic/OpenMPKinds.def"
  OMPC_SCHEDULE_unknown
};

/// OpenMP modifiers for 'schedule' clause.
enum OpenMPScheduleClauseModifier {
  OMPC_SCHEDULE_MODIFIER_unknown = OMPC_SCHEDULE_unknown,
#define OPENMP_SCHEDULE_MODIFIER(Name) \
  OMPC_SCHEDULE_MODIFIER_##Name,
#include "clang/Basic/OpenMPKinds.def"
  OMPC_SCHEDULE_MODIFIER_last
};

/// OpenMP modifiers for 'device' clause.
enum OpenMPDeviceClauseModifier {
#define OPENMP_DEVICE_MODIFIER(Name) OMPC_DEVICE_##Name,
#include "clang/Basic/OpenMPKinds.def"
  OMPC_DEVICE_unknown,
};

/// OpenMP attributes for 'depend' clause.
enum OpenMPDependClauseKind {
#define OPENMP_DEPEND_KIND(Name) \
  OMPC_DEPEND_##Name,
#include "clang/Basic/OpenMPKinds.def"
  OMPC_DEPEND_unknown
};

/// OpenMP attributes for 'linear' clause.
enum OpenMPLinearClauseKind {
#define OPENMP_LINEAR_KIND(Name) \
  OMPC_LINEAR_##Name,
#include "clang/Basic/OpenMPKinds.def"
  OMPC_LINEAR_unknown
};

/// OpenMP mapping kind for 'map' clause.
enum OpenMPMapClauseKind {
#define OPENMP_MAP_KIND(Name) \
  OMPC_MAP_##Name,
#include "clang/Basic/OpenMPKinds.def"
  OMPC_MAP_unknown
};

/// OpenMP modifier kind for 'map' clause.
enum OpenMPMapModifierKind {
  OMPC_MAP_MODIFIER_unknown = OMPC_MAP_unknown,
#define OPENMP_MAP_MODIFIER_KIND(Name) \
  OMPC_MAP_MODIFIER_##Name,
#include "clang/Basic/OpenMPKinds.def"
  OMPC_MAP_MODIFIER_last
};

/// Number of allowed map-type-modifiers.
static constexpr unsigned NumberOfOMPMapClauseModifiers =
    OMPC_MAP_MODIFIER_last - OMPC_MAP_MODIFIER_unknown - 1;

/// OpenMP modifier kind for 'to' or 'from' clause.
enum OpenMPMotionModifierKind {
#define OPENMP_MOTION_MODIFIER_KIND(Name) \
  OMPC_MOTION_MODIFIER_##Name,
#include "clang/Basic/OpenMPKinds.def"
  OMPC_MOTION_MODIFIER_unknown
};

/// Number of allowed motion-modifiers.
static constexpr unsigned NumberOfOMPMotionModifiers =
    OMPC_MOTION_MODIFIER_unknown;

/// OpenMP attributes for 'dist_schedule' clause.
enum OpenMPDistScheduleClauseKind {
#define OPENMP_DIST_SCHEDULE_KIND(Name) OMPC_DIST_SCHEDULE_##Name,
#include "clang/Basic/OpenMPKinds.def"
  OMPC_DIST_SCHEDULE_unknown
};

/// OpenMP attributes for 'defaultmap' clause.
enum OpenMPDefaultmapClauseKind {
#define OPENMP_DEFAULTMAP_KIND(Name) \
  OMPC_DEFAULTMAP_##Name,
#include "clang/Basic/OpenMPKinds.def"
  OMPC_DEFAULTMAP_unknown
};

/// OpenMP modifiers for 'defaultmap' clause.
enum OpenMPDefaultmapClauseModifier {
  OMPC_DEFAULTMAP_MODIFIER_unknown = OMPC_DEFAULTMAP_unknown,
#define OPENMP_DEFAULTMAP_MODIFIER(Name) \
  OMPC_DEFAULTMAP_MODIFIER_##Name,
#include "clang/Basic/OpenMPKinds.def"
  OMPC_DEFAULTMAP_MODIFIER_last
};

/// OpenMP attributes for 'atomic_default_mem_order' clause.
enum OpenMPAtomicDefaultMemOrderClauseKind {
#define OPENMP_ATOMIC_DEFAULT_MEM_ORDER_KIND(Name)  \
  OMPC_ATOMIC_DEFAULT_MEM_ORDER_##Name,
#include "clang/Basic/OpenMPKinds.def"
  OMPC_ATOMIC_DEFAULT_MEM_ORDER_unknown
};

/// OpenMP attributes for 'at' clause.
enum OpenMPAtClauseKind {
#define OPENMP_AT_KIND(Name) OMPC_AT_##Name,
#include "clang/Basic/OpenMPKinds.def"
  OMPC_AT_unknown
};

/// OpenMP attributes for 'severity' clause.
enum OpenMPSeverityClauseKind {
#define OPENMP_SEVERITY_KIND(Name) OMPC_SEVERITY_##Name,
#include "clang/Basic/OpenMPKinds.def"
  OMPC_SEVERITY_unknown
};

/// OpenMP device type for 'device_type' clause.
enum OpenMPDeviceType {
#define OPENMP_DEVICE_TYPE_KIND(Name) \
  OMPC_DEVICE_TYPE_##Name,
#include "clang/Basic/OpenMPKinds.def"
  OMPC_DEVICE_TYPE_unknown
};

/// OpenMP 'lastprivate' clause modifier.
enum OpenMPLastprivateModifier {
#define OPENMP_LASTPRIVATE_KIND(Name) OMPC_LASTPRIVATE_##Name,
#include "clang/Basic/OpenMPKinds.def"
  OMPC_LASTPRIVATE_unknown,
};

/// OpenMP attributes for 'order' clause.
enum OpenMPOrderClauseKind {
#define OPENMP_ORDER_KIND(Name) OMPC_ORDER_##Name,
#include "clang/Basic/OpenMPKinds.def"
  OMPC_ORDER_unknown,
};

/// OpenMP modifiers for 'order' clause.
enum OpenMPOrderClauseModifier {
  OMPC_ORDER_MODIFIER_unknown = OMPC_ORDER_unknown,
#define OPENMP_ORDER_MODIFIER(Name) OMPC_ORDER_MODIFIER_##Name,
#include "clang/Basic/OpenMPKinds.def"
  OMPC_ORDER_MODIFIER_last
};

/// Scheduling data for loop-based OpenMP directives.
struct OpenMPScheduleTy final {
  OpenMPScheduleClauseKind Schedule = OMPC_SCHEDULE_unknown;
  OpenMPScheduleClauseModifier M1 = OMPC_SCHEDULE_MODIFIER_unknown;
  OpenMPScheduleClauseModifier M2 = OMPC_SCHEDULE_MODIFIER_unknown;
};

/// OpenMP modifiers for 'reduction' clause.
enum OpenMPReductionClauseModifier {
#define OPENMP_REDUCTION_MODIFIER(Name) OMPC_REDUCTION_##Name,
#include "clang/Basic/OpenMPKinds.def"
  OMPC_REDUCTION_unknown,
};

/// OpenMP adjust-op kinds for 'adjust_args' clause.
enum OpenMPAdjustArgsOpKind {
#define OPENMP_ADJUST_ARGS_KIND(Name) OMPC_ADJUST_ARGS_##Name,
#include "clang/Basic/OpenMPKinds.def"
  OMPC_ADJUST_ARGS_unknown,
};

/// OpenMP bindings for the 'bind' clause.
enum OpenMPBindClauseKind {
#define OPENMP_BIND_KIND(Name) OMPC_BIND_##Name,
#include "clang/Basic/OpenMPKinds.def"
  OMPC_BIND_unknown
};

enum OpenMPGrainsizeClauseModifier {
#define OPENMP_GRAINSIZE_MODIFIER(Name) OMPC_GRAINSIZE_##Name,
#include "clang/Basic/OpenMPKinds.def"
  OMPC_GRAINSIZE_unknown
};

enum OpenMPNumTasksClauseModifier {
#define OPENMP_NUMTASKS_MODIFIER(Name) OMPC_NUMTASKS_##Name,
#include "clang/Basic/OpenMPKinds.def"
  OMPC_NUMTASKS_unknown
};

/// OpenMP dependence types for 'doacross' clause.
enum OpenMPDoacrossClauseModifier {
#define OPENMP_DOACROSS_MODIFIER(Name) OMPC_DOACROSS_##Name,
#include "clang/Basic/OpenMPKinds.def"
  OMPC_DOACROSS_unknown
};

/// Contains 'interop' data for 'append_args' and 'init' clauses.
class Expr;
struct OMPInteropInfo final {
  OMPInteropInfo(bool IsTarget = false, bool IsTargetSync = false)
      : IsTarget(IsTarget), IsTargetSync(IsTargetSync) {}
  bool IsTarget;
  bool IsTargetSync;
  llvm::SmallVector<Expr *, 4> PreferTypes;
};

unsigned getOpenMPSimpleClauseType(OpenMPClauseKind Kind, llvm::StringRef Str,
                                   const LangOptions &LangOpts);
const char *getOpenMPSimpleClauseTypeName(OpenMPClauseKind Kind, unsigned Type);

/// Checks if the specified directive is a directive with an associated
/// loop construct.
/// \param DKind Specified directive.
/// \return true - the directive is a loop-associated directive like 'omp simd'
/// or 'omp for' directive, otherwise - false.
bool isOpenMPLoopDirective(OpenMPDirectiveKind DKind);

/// Checks if the specified directive is a worksharing directive.
/// \param DKind Specified directive.
/// \return true - the directive is a worksharing directive like 'omp for',
/// otherwise - false.
bool isOpenMPWorksharingDirective(OpenMPDirectiveKind DKind);

/// Checks if the specified directive is a taskloop directive.
/// \param DKind Specified directive.
/// \return true - the directive is a worksharing directive like 'omp taskloop',
/// otherwise - false.
bool isOpenMPTaskLoopDirective(OpenMPDirectiveKind DKind);

/// Checks if the specified directive is a parallel-kind directive.
/// \param DKind Specified directive.
/// \return true - the directive is a parallel-like directive like 'omp
/// parallel', otherwise - false.
bool isOpenMPParallelDirective(OpenMPDirectiveKind DKind);

/// Checks if the specified directive is a target code offload directive.
/// \param DKind Specified directive.
/// \return true - the directive is a target code offload directive like
/// 'omp target', 'omp target parallel', 'omp target xxx'
/// otherwise - false.
bool isOpenMPTargetExecutionDirective(OpenMPDirectiveKind DKind);

/// Checks if the specified directive is a target data offload directive.
/// \param DKind Specified directive.
/// \return true - the directive is a target data offload directive like
/// 'omp target data', 'omp target update', 'omp target enter data',
/// 'omp target exit data'
/// otherwise - false.
bool isOpenMPTargetDataManagementDirective(OpenMPDirectiveKind DKind);

/// Checks if the specified composite/combined directive constitutes a teams
/// directive in the outermost nest.  For example
/// 'omp teams distribute' or 'omp teams distribute parallel for'.
/// \param DKind Specified directive.
/// \return true - the directive has teams on the outermost nest, otherwise -
/// false.
bool isOpenMPNestingTeamsDirective(OpenMPDirectiveKind DKind);

/// Checks if the specified directive is a teams-kind directive.  For example,
/// 'omp teams distribute' or 'omp target teams'.
/// \param DKind Specified directive.
/// \return true - the directive is a teams-like directive, otherwise - false.
bool isOpenMPTeamsDirective(OpenMPDirectiveKind DKind);

/// Checks if the specified directive is a simd directive.
/// \param DKind Specified directive.
/// \return true - the directive is a simd directive like 'omp simd',
/// otherwise - false.
bool isOpenMPSimdDirective(OpenMPDirectiveKind DKind);

/// Checks if the specified directive is a distribute directive.
/// \param DKind Specified directive.
/// \return true - the directive is a distribute-directive like 'omp
/// distribute',
/// otherwise - false.
bool isOpenMPDistributeDirective(OpenMPDirectiveKind DKind);

/// Checks if the specified composite/combined directive constitutes a
/// distribute directive in the outermost nest.  For example,
/// 'omp distribute parallel for' or 'omp distribute'.
/// \param DKind Specified directive.
/// \return true - the directive has distribute on the outermost nest.
/// otherwise - false.
bool isOpenMPNestingDistributeDirective(OpenMPDirectiveKind DKind);

/// Checks if the specified directive constitutes a 'loop' directive in the
/// outermost nest.  For example, 'omp teams loop' or 'omp loop'.
/// \param DKind Specified directive.
/// \return true - the directive has loop on the outermost nest.
/// otherwise - false.
bool isOpenMPGenericLoopDirective(OpenMPDirectiveKind DKind);

/// Checks if the specified clause is one of private clauses like
/// 'private', 'firstprivate', 'reduction' etc..
/// \param Kind Clause kind.
/// \return true - the clause is a private clause, otherwise - false.
bool isOpenMPPrivate(OpenMPClauseKind Kind);

/// Checks if the specified clause is one of threadprivate clauses like
/// 'threadprivate', 'copyin' or 'copyprivate'.
/// \param Kind Clause kind.
/// \return true - the clause is a threadprivate clause, otherwise - false.
bool isOpenMPThreadPrivate(OpenMPClauseKind Kind);

/// Checks if the specified directive kind is one of tasking directives - task,
/// taskloop, taksloop simd, master taskloop, parallel master taskloop, master
/// taskloop simd, or parallel master taskloop simd.
bool isOpenMPTaskingDirective(OpenMPDirectiveKind Kind);

/// Checks if the specified directive kind is one of the composite or combined
/// directives that need loop bound sharing across loops outlined in nested
/// functions
bool isOpenMPLoopBoundSharingDirective(OpenMPDirectiveKind Kind);

/// Checks if the specified directive is a loop transformation directive.
/// \param DKind Specified directive.
/// \return True iff the directive is a loop transformation.
bool isOpenMPLoopTransformationDirective(OpenMPDirectiveKind DKind);

/// Return the captured regions of an OpenMP directive.
void getOpenMPCaptureRegions(
    llvm::SmallVectorImpl<OpenMPDirectiveKind> &CaptureRegions,
    OpenMPDirectiveKind DKind);

/// Checks if the specified directive is a combined construct for which
/// the first construct is a parallel construct.
/// \param DKind Specified directive.
/// \return true - if the above condition is met for this directive
/// otherwise - false.
bool isOpenMPCombinedParallelADirective(OpenMPDirectiveKind DKind);

/// Checks if the specified target directive, combined or not, needs task based
/// thread_limit
/// \param DKind Specified directive.
/// \return true - if the above condition is met for this directive
/// otherwise - false.
bool needsTaskBasedThreadLimit(OpenMPDirectiveKind DKind);

/// Checks if the parameter to the fail clause in "#pragma atomic compare fail"
/// is restricted only to memory order clauses of "OMPC_acquire",
/// "OMPC_relaxed" and "OMPC_seq_cst".
bool checkFailClauseParameter(OpenMPClauseKind FailClauseParameter);

/// Checks if the specified directive is considered as "executable". This
/// combines the OpenMP categories of "executable" and "subsidiary", plus
/// any other directives that should be treated as executable.
/// \param DKind Specified directive.
/// \return true - if the above condition is met for this directive
/// otherwise - false.
bool isOpenMPExecutableDirective(OpenMPDirectiveKind DKind);

/// Checks if the specified directive can capture variables.
/// \param DKind Specified directive.
/// \return true - if the above condition is met for this directive
/// otherwise - false.
bool isOpenMPCapturingDirective(OpenMPDirectiveKind DKind);
}

#endif

