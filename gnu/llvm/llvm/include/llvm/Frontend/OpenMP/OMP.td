//===-- OMP.td - OpenMP directive definition file ----------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is the definition file for OpenMP directives and clauses.
//
//===----------------------------------------------------------------------===//

include "llvm/Frontend/Directive/DirectiveBase.td"

//===----------------------------------------------------------------------===//
// Definition of general OpenMP information
//===----------------------------------------------------------------------===//

def OpenMP : DirectiveLanguage {
  let name = "OpenMP";
  let cppNamespace = "omp"; // final namespace will be llvm::omp
  let directivePrefix = "OMPD_";
  let clausePrefix = "OMPC_";
  let makeEnumAvailableInNamespace = true;
  let enableBitmaskEnumInNamespace = true;
  let clauseEnumSetClass = "OmpClauseSet";
  let flangClauseBaseClass = "OmpClause";
}

//===----------------------------------------------------------------------===//
// Definitions of OpenMP clauses
// Sorted alphabetically wrt clause spelling.
//===----------------------------------------------------------------------===//

def OMPC_Acquire : Clause<"acquire"> {
  let clangClass = "OMPAcquireClause";
}
def OMPC_AcqRel : Clause<"acq_rel"> {
  let clangClass = "OMPAcqRelClause";
}
def OMPC_AdjustArgs : Clause<"adjust_args"> {
}
def OMPC_Affinity : Clause<"affinity"> {
  let clangClass = "OMPAffinityClause";
}
def OMPC_Align : Clause<"align"> {
  let clangClass = "OMPAlignClause";
}
def OMPC_Aligned : Clause<"aligned"> {
  let clangClass = "OMPAlignedClause";
  let flangClass = "OmpAlignedClause";
}
def OMPC_Allocate : Clause<"allocate"> {
  let clangClass = "OMPAllocateClause";
  let flangClass = "OmpAllocateClause";
}
def OMPC_Allocator : Clause<"allocator"> {
  let clangClass = "OMPAllocatorClause";
  let flangClass = "ScalarIntExpr";
}
def OMPC_AppendArgs : Clause<"append_args"> {
}
def OMPC_At : Clause<"at"> {
  let clangClass = "OMPAtClause";
}
def OMPC_AtomicDefaultMemOrder : Clause<"atomic_default_mem_order"> {
  let clangClass = "OMPAtomicDefaultMemOrderClause";
  let flangClass = "OmpAtomicDefaultMemOrderClause";
}
def OMPC_Bind : Clause<"bind"> {
  let clangClass = "OMPBindClause";
}
def OMP_CANCELLATION_CONSTRUCT_Parallel : ClauseVal<"parallel", 1, 1> {}
def OMP_CANCELLATION_CONSTRUCT_Loop : ClauseVal<"loop", 2, 1> {}
def OMP_CANCELLATION_CONSTRUCT_Sections : ClauseVal<"sections", 3, 1> {}
def OMP_CANCELLATION_CONSTRUCT_Taskgroup : ClauseVal<"taskgroup", 4, 1> {}
def OMP_CANCELLATION_CONSTRUCT_None : ClauseVal<"none", 5, 0> {
  let isDefault = 1;
}
def OMPC_CancellationConstructType : Clause<"cancellation_construct_type"> {
  let enumClauseValue = "CancellationConstructType";
  let allowedClauseValues = [
    OMP_CANCELLATION_CONSTRUCT_Parallel,
    OMP_CANCELLATION_CONSTRUCT_Loop,
    OMP_CANCELLATION_CONSTRUCT_Sections,
    OMP_CANCELLATION_CONSTRUCT_Taskgroup,
    OMP_CANCELLATION_CONSTRUCT_None
  ];
}
def OMPC_Capture : Clause<"capture"> {
  let clangClass = "OMPCaptureClause";
}
def OMPC_Collapse : Clause<"collapse"> {
  let clangClass = "OMPCollapseClause";
  let flangClass = "ScalarIntConstantExpr";
}
def OMPC_Compare : Clause<"compare"> {
  let clangClass = "OMPCompareClause";
}
def OMPC_Copyin : Clause<"copyin"> {
  let clangClass = "OMPCopyinClause";
  let flangClass = "OmpObjectList";
}
def OMPC_CopyPrivate : Clause<"copyprivate"> {
  let clangClass = "OMPCopyprivateClause";
  let flangClass = "OmpObjectList";
}
def OMPC_Default : Clause<"default"> {
  let clangClass = "OMPDefaultClause";
  let flangClass = "OmpDefaultClause";
}
def OMPC_DefaultMap : Clause<"defaultmap"> {
  let clangClass = "OMPDefaultmapClause";
  let flangClass = "OmpDefaultmapClause";
}
def OMPC_Depend : Clause<"depend"> {
  let clangClass = "OMPDependClause";
  let flangClass = "OmpDependClause";
}
def OMPC_Depobj : Clause<"depobj"> {
  let clangClass = "OMPDepobjClause";
  let isImplicit = true;
}
def OMPC_Destroy : Clause<"destroy"> {
  let clangClass = "OMPDestroyClause";
}
def OMPC_Detach : Clause<"detach"> {
  let clangClass = "OMPDetachClause";
}
def OMPC_Device : Clause<"device"> {
  let clangClass = "OMPDeviceClause";
  let flangClass = "OmpDeviceClause";
}
def OMPC_DeviceType : Clause<"device_type"> {
  let flangClass = "OmpDeviceTypeClause";
}
def OMPC_DistSchedule : Clause<"dist_schedule"> {
  let clangClass = "OMPDistScheduleClause";
  let flangClass = "ScalarIntExpr";
  let isValueOptional = true;
}
def OMPC_Doacross : Clause<"doacross"> {
  let clangClass = "OMPDoacrossClause";
}
def OMPC_DynamicAllocators : Clause<"dynamic_allocators"> {
  let clangClass = "OMPDynamicAllocatorsClause";
}
def OMPC_Enter : Clause<"enter"> {
  let flangClass = "OmpObjectList";
}
def OMPC_Exclusive : Clause<"exclusive"> {
  let clangClass = "OMPExclusiveClause";
}
def OMPC_Fail : Clause<"fail"> {
  let clangClass = "OMPFailClause";
}
def OMPC_Filter : Clause<"filter"> {
  let clangClass = "OMPFilterClause";
  let flangClass = "ScalarIntExpr";
}
def OMPC_Final : Clause<"final"> {
  let clangClass = "OMPFinalClause";
  let flangClass = "ScalarLogicalExpr";
}
def OMPC_FirstPrivate : Clause<"firstprivate"> {
  let clangClass = "OMPFirstprivateClause";
  let flangClass = "OmpObjectList";
}
def OMPC_Flush : Clause<"flush"> {
  let clangClass = "OMPFlushClause";
  let isImplicit = true;
}
def OMPC_From : Clause<"from"> {
  let clangClass = "OMPFromClause";
  let flangClass = "OmpObjectList";
}
def OMPC_Full: Clause<"full"> {
  let clangClass = "OMPFullClause";
}
def OMP_GRAINSIZE_Strict : ClauseVal<"strict", 1, 1> {}
def OMP_GRAINSIZE_Unknown : ClauseVal<"unkonwn", 2, 0> { let isDefault = 1; }
def OMPC_GrainSize : Clause<"grainsize"> {
  let clangClass = "OMPGrainsizeClause";
  let flangClass = "ScalarIntExpr";
  let enumClauseValue = "GrainsizeType";
  let allowedClauseValues = [
    OMP_GRAINSIZE_Strict,
    OMP_GRAINSIZE_Unknown
  ];
}
def OMPC_HasDeviceAddr : Clause<"has_device_addr"> {
  let clangClass = "OMPHasDeviceAddrClause";
  let flangClass = "OmpObjectList";
}
def OMPC_Hint : Clause<"hint"> {
  let clangClass = "OMPHintClause";
  let flangClass = "ConstantExpr";
}
def OMPC_If : Clause<"if"> {
  let clangClass = "OMPIfClause";
  let flangClass = "OmpIfClause";
}
def OMPC_Inbranch : Clause<"inbranch"> {
}
def OMPC_Inclusive : Clause<"inclusive"> {
  let clangClass = "OMPInclusiveClause";
}
def OMPC_Indirect : Clause<"indirect"> {
}
def OMPC_Init : Clause<"init"> {
  let clangClass = "OMPInitClause";
}
def OMPC_InReduction : Clause<"in_reduction"> {
  let clangClass = "OMPInReductionClause";
  let flangClass = "OmpInReductionClause";
}
def OMPC_IsDevicePtr : Clause<"is_device_ptr"> {
  let clangClass = "OMPIsDevicePtrClause";
  let flangClass = "OmpObjectList";
}
def OMPC_LastPrivate : Clause<"lastprivate"> {
  let clangClass = "OMPLastprivateClause";
  let flangClass = "OmpObjectList";
}
def OMPC_Linear : Clause<"linear"> {
  let clangClass = "OMPLinearClause";
  let flangClass = "OmpLinearClause";
}
def OMPC_Link : Clause<"link"> {
  let flangClass = "OmpObjectList";
}
def OMPC_Map : Clause<"map"> {
  let clangClass = "OMPMapClause";
  let flangClass = "OmpMapClause";
}
def OMPC_Match : Clause<"match"> {
}
def OMP_MEMORY_ORDER_SeqCst : ClauseVal<"seq_cst", 1, 1> {}
def OMP_MEMORY_ORDER_AcqRel : ClauseVal<"acq_rel", 2, 1> {}
def OMP_MEMORY_ORDER_Acquire : ClauseVal<"acquire", 3, 1> {}
def OMP_MEMORY_ORDER_Release : ClauseVal<"release", 4, 1> {}
def OMP_MEMORY_ORDER_Relaxed : ClauseVal<"relaxed", 5, 1> {}
def OMP_MEMORY_ORDER_Default : ClauseVal<"default", 6, 0> {
  let isDefault = 1;
}
def OMPC_MemoryOrder : Clause<"memory_order"> {
  let enumClauseValue = "MemoryOrderKind";
  let allowedClauseValues = [
    OMP_MEMORY_ORDER_SeqCst,
    OMP_MEMORY_ORDER_AcqRel,
    OMP_MEMORY_ORDER_Acquire,
    OMP_MEMORY_ORDER_Release,
    OMP_MEMORY_ORDER_Relaxed,
    OMP_MEMORY_ORDER_Default
  ];
}
def OMPC_Mergeable : Clause<"mergeable"> {
  let clangClass = "OMPMergeableClause";
}
def OMPC_Message : Clause<"message"> {
  let clangClass = "OMPMessageClause";
}
def OMPC_Nocontext : Clause<"nocontext"> {
  let clangClass = "OMPNocontextClause";
  let flangClass = "ScalarLogicalExpr";
}
def OMPC_NoGroup : Clause<"nogroup"> {
  let clangClass = "OMPNogroupClause";
}
def OMPC_NonTemporal : Clause<"nontemporal"> {
  let clangClass = "OMPNontemporalClause";
  let flangClass = "Name";
  let isValueList = true;
}
def OMPC_Notinbranch : Clause<"notinbranch"> {
}
def OMPC_Novariants : Clause<"novariants"> {
  let clangClass = "OMPNovariantsClause";
  let flangClass = "ScalarLogicalExpr";
}
def OMPC_NoWait : Clause<"nowait"> {
  let clangClass = "OMPNowaitClause";
}
def OMP_NUMTASKS_Strict : ClauseVal<"strict", 1, 1> {}
def OMP_NUMTASKS_Unknown : ClauseVal<"unkonwn", 2, 0> { let isDefault = 1; }
def OMPC_NumTasks : Clause<"num_tasks"> {
  let clangClass = "OMPNumTasksClause";
  let flangClass = "ScalarIntExpr";
  let enumClauseValue = "NumTasksType";
  let allowedClauseValues = [
    OMP_NUMTASKS_Strict,
    OMP_NUMTASKS_Unknown
  ];
}
def OMPC_NumTeams : Clause<"num_teams"> {
  let clangClass = "OMPNumTeamsClause";
  let flangClass = "ScalarIntExpr";
}
def OMPC_NumThreads : Clause<"num_threads"> {
  let clangClass = "OMPNumThreadsClause";
  let flangClass = "ScalarIntExpr";
}
def OMPC_OMPX_Attribute : Clause<"ompx_attribute"> {
  let clangClass = "OMPXAttributeClause";
}
def OMPC_OMPX_Bare : Clause<"ompx_bare"> {
  let clangClass = "OMPXBareClause";
}
def OMPC_OMPX_DynCGroupMem : Clause<"ompx_dyn_cgroup_mem"> {
  let clangClass = "OMPXDynCGroupMemClause";
  let flangClass = "ScalarIntExpr";
}
def OMP_ORDER_concurrent : ClauseVal<"concurrent",1,1> {}
def OMP_ORDER_unknown : ClauseVal<"unknown",2,0> { let isDefault = 1; }
def OMPC_Order : Clause<"order"> {
  let clangClass = "OMPOrderClause";
  let flangClass = "OmpOrderClause";
  let enumClauseValue = "OrderKind";
  let allowedClauseValues = [
    OMP_ORDER_unknown,
    OMP_ORDER_concurrent
  ];
}
def OMPC_Ordered : Clause<"ordered"> {
  let clangClass = "OMPOrderedClause";
  let flangClass = "ScalarIntConstantExpr";
  let isValueOptional = true;
}
def OMPC_Partial: Clause<"partial"> {
  let clangClass = "OMPPartialClause";
  let flangClass = "ScalarIntConstantExpr";
  let isValueOptional = true;
}
def OMPC_Priority : Clause<"priority"> {
  let clangClass = "OMPPriorityClause";
  let flangClass = "ScalarIntExpr";
}
def OMPC_Private : Clause<"private"> {
  let clangClass = "OMPPrivateClause";
  let flangClass = "OmpObjectList";
}
def OMP_PROC_BIND_master : ClauseVal<"master",2,1> {}
def OMP_PROC_BIND_close : ClauseVal<"close",3,1> {}
def OMP_PROC_BIND_spread : ClauseVal<"spread",4,1> {}
def OMP_PROC_BIND_primary : ClauseVal<"primary",5,1> {}
def OMP_PROC_BIND_default : ClauseVal<"default",6,0> {}
def OMP_PROC_BIND_unknown : ClauseVal<"unknown",7,0> { let isDefault = true; }
def OMPC_ProcBind : Clause<"proc_bind"> {
  let clangClass = "OMPProcBindClause";
  let flangClass = "OmpProcBindClause";
  let enumClauseValue = "ProcBindKind";
  let allowedClauseValues = [
    OMP_PROC_BIND_primary,
    OMP_PROC_BIND_master,
    OMP_PROC_BIND_close,
    OMP_PROC_BIND_spread,
    OMP_PROC_BIND_default,
    OMP_PROC_BIND_unknown
  ];
}
def OMPC_Read : Clause<"read"> {
  let clangClass = "OMPReadClause";
}
def OMPC_Reduction : Clause<"reduction"> {
  let clangClass = "OMPReductionClause";
  let flangClass = "OmpReductionClause";
}
def OMPC_Relaxed : Clause<"relaxed"> {
  let clangClass = "OMPRelaxedClause";
}
def OMPC_Release : Clause<"release"> {
  let clangClass = "OMPReleaseClause";
}
def OMPC_ReverseOffload : Clause<"reverse_offload"> {
  let clangClass = "OMPReverseOffloadClause";
}
def OMPC_SafeLen : Clause<"safelen"> {
  let clangClass = "OMPSafelenClause";
  let flangClass = "ScalarIntConstantExpr";
}
def OMP_SCHEDULE_Static : ClauseVal<"static", 2, 1> {}
def OMP_SCHEDULE_Dynamic : ClauseVal<"dynamic", 3, 1> {}
def OMP_SCHEDULE_Guided : ClauseVal<"guided", 4, 1> {}
def OMP_SCHEDULE_Auto : ClauseVal<"auto", 5, 1> {}
def OMP_SCHEDULE_Runtime : ClauseVal<"runtime", 6, 1> {}
def OMP_SCHEDULE_Default : ClauseVal<"default", 7, 0> { let isDefault = 1; }
def OMPC_Schedule : Clause<"schedule"> {
  let clangClass = "OMPScheduleClause";
  let flangClass = "OmpScheduleClause";
  let enumClauseValue = "ScheduleKind";
  let allowedClauseValues = [
    OMP_SCHEDULE_Static,
    OMP_SCHEDULE_Dynamic,
    OMP_SCHEDULE_Guided,
    OMP_SCHEDULE_Auto,
    OMP_SCHEDULE_Runtime,
    OMP_SCHEDULE_Default
  ];
}
def OMPC_SeqCst : Clause<"seq_cst"> {
  let clangClass = "OMPSeqCstClause";
}
def OMPC_Severity : Clause<"severity"> {
  let clangClass = "OMPSeverityClause";
}
def OMPC_Shared : Clause<"shared"> {
  let clangClass = "OMPSharedClause";
  let flangClass = "OmpObjectList";
}
def OMPC_Simd : Clause<"simd"> {
  let clangClass = "OMPSIMDClause";
}
def OMPC_SimdLen : Clause<"simdlen"> {
  let clangClass = "OMPSimdlenClause";
  let flangClass = "ScalarIntConstantExpr";
}
def OMPC_Sizes: Clause<"sizes"> {
  let clangClass = "OMPSizesClause";
  let flangClass = "ScalarIntExpr";
  let isValueList = true;
}
def OMPC_TaskReduction : Clause<"task_reduction"> {
  let clangClass = "OMPTaskReductionClause";
  let flangClass = "OmpReductionClause";
}
def OMPC_ThreadLimit : Clause<"thread_limit"> {
  let clangClass = "OMPThreadLimitClause";
  let flangClass = "ScalarIntExpr";
}
def OMPC_ThreadPrivate : Clause<"threadprivate"> {
  let alternativeName = "threadprivate or thread local";
  let isImplicit = true;
}
def OMPC_Threads : Clause<"threads"> {
  let clangClass = "OMPThreadsClause";
}
def OMPC_To : Clause<"to"> {
  let clangClass = "OMPToClause";
  let flangClass = "OmpObjectList";
}
def OMPC_UnifiedAddress : Clause<"unified_address"> {
  let clangClass = "OMPUnifiedAddressClause";
}
def OMPC_UnifiedSharedMemory : Clause<"unified_shared_memory"> {
  let clangClass = "OMPUnifiedSharedMemoryClause";
}
def OMPC_Uniform : Clause<"uniform"> {
  let flangClass = "Name";
  let isValueList = true;
}
def OMPC_Unknown : Clause<"unknown"> {
  let isImplicit = true;
  let isDefault = true;
}
def OMPC_Untied : Clause<"untied"> {
  let clangClass = "OMPUntiedClause";
}
def OMPC_Update : Clause<"update"> {
  let clangClass = "OMPUpdateClause";
}
def OMPC_Use : Clause<"use"> {
  let clangClass = "OMPUseClause";
}
def OMPC_UsesAllocators : Clause<"uses_allocators"> {
  let clangClass = "OMPUsesAllocatorsClause";
}
def OMPC_UseDeviceAddr : Clause<"use_device_addr"> {
  let clangClass = "OMPUseDeviceAddrClause";
  let flangClass = "OmpObjectList";
}
def OMPC_UseDevicePtr : Clause<"use_device_ptr"> {
  let clangClass = "OMPUseDevicePtrClause";
  let flangClass = "OmpObjectList";
}
def OMPC_Weak : Clause<"weak"> {
  let clangClass = "OMPWeakClause";
}
def OMPC_When: Clause<"when"> {
}
def OMPC_Write : Clause<"write"> {
  let clangClass = "OMPWriteClause";
}

//===----------------------------------------------------------------------===//
// Definitions of OpenMP leaf directives
// Sorted alphabetically wrt directive spelling, except "end xyz" immediately
// follows "xyz".
//===----------------------------------------------------------------------===//

def OMP_Allocate : Directive<"allocate"> {
  let allowedOnceClauses = [
    VersionedClause<OMPC_Align, 51>,
    VersionedClause<OMPC_Allocator>,
  ];
  let association = AS_None;
  let category = CA_Declarative;
}
def OMP_Allocators : Directive<"allocators"> {
  let allowedClauses = [
    VersionedClause<OMPC_Allocate>,
  ];
  let association = AS_Block;
  let category = CA_Executable;
}
def OMP_Assumes : Directive<"assumes"> {
  let association = AS_None;
  let category = CA_Informational;
}
def OMP_EndAssumes : Directive<"end assumes"> {
  let association = AS_Delimited;
  let category = OMP_Assumes.category;
}
def OMP_Atomic : Directive<"atomic"> {
  let allowedClauses = [
    VersionedClause<OMPC_Capture>,
    VersionedClause<OMPC_Compare, 51>,
    VersionedClause<OMPC_Read>,
    VersionedClause<OMPC_Update>,
    VersionedClause<OMPC_Write>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_AcqRel, 50>,
    VersionedClause<OMPC_Acquire, 50>,
    VersionedClause<OMPC_Fail, 51>,
    VersionedClause<OMPC_Hint, 50>,
    VersionedClause<OMPC_Relaxed, 50>,
    VersionedClause<OMPC_Release, 50>,
    VersionedClause<OMPC_SeqCst>,
    VersionedClause<OMPC_Weak, 51>,
  ];
  let association = AS_Block;
  let category = CA_Executable;
}
def OMP_Barrier : Directive<"barrier"> {
  let association = AS_None;
  let category = CA_Executable;
}
def OMP_BeginAssumes : Directive<"begin assumes"> {
  let association = AS_Delimited;
  let category = CA_Informational;
}
def OMP_BeginDeclareTarget : Directive<"begin declare target"> {
  let allowedClauses = [
    VersionedClause<OMPC_DeviceType>,
    VersionedClause<OMPC_Indirect>,
    VersionedClause<OMPC_Link>,
    VersionedClause<OMPC_To>,
  ];
  let association = AS_Delimited;
  let category = CA_Declarative;
}
def OMP_BeginDeclareVariant : Directive<"begin declare variant"> {
  let association = AS_Delimited;
  let category = CA_Declarative;
}
def OMP_Cancel : Directive<"cancel"> {
  let allowedOnceClauses = [
    VersionedClause<OMPC_If>,
  ];
  let association = AS_None;
  let category = CA_Executable;
}
def OMP_CancellationPoint : Directive<"cancellation point"> {
  let association = AS_None;
  let category = CA_Executable;
}
def OMP_Critical : Directive<"critical"> {
  let allowedClauses = [
    VersionedClause<OMPC_Hint>,
  ];
  let association = AS_Block;
  let category = CA_Executable;
}
def OMP_DeclareMapper : Directive<"declare mapper"> {
  let allowedClauses = [
    VersionedClause<OMPC_Map>,
  ];
  let association = AS_None;
  let category = CA_Declarative;
}
def OMP_DeclareReduction : Directive<"declare reduction"> {
  let association = AS_None;
  let category = CA_Declarative;
}
def OMP_DeclareSimd : Directive<"declare simd"> {
  let allowedClauses = [
    VersionedClause<OMPC_Aligned>,
    VersionedClause<OMPC_Linear>,
    VersionedClause<OMPC_Uniform>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_SimdLen>,
  ];
  let allowedExclusiveClauses = [
    VersionedClause<OMPC_Inbranch>,
    VersionedClause<OMPC_Notinbranch>,
  ];
  let association = AS_Declaration;
  let category = CA_Declarative;
}
def OMP_DeclareTarget : Directive<"declare target"> {
  let allowedClauses = [
    VersionedClause<OMPC_Enter, 52>,
    VersionedClause<OMPC_Indirect>,
    VersionedClause<OMPC_Link>,
    VersionedClause<OMPC_To>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_DeviceType, 50>,
  ];
  let association = AS_None;
  let category = CA_Declarative;
}
def OMP_EndDeclareTarget : Directive<"end declare target"> {
  let association = AS_Delimited;
  let category = OMP_DeclareTarget.category;
}
def OMP_DeclareVariant : Directive<"declare variant"> {
  let allowedClauses = [
    VersionedClause<OMPC_Match>,
  ];
  let allowedExclusiveClauses = [
    VersionedClause<OMPC_AdjustArgs, 51>,
    VersionedClause<OMPC_AppendArgs, 51>,
  ];
  let association = AS_Declaration;
  let category = CA_Declarative;
}
def OMP_EndDeclareVariant : Directive<"end declare variant"> {
  let association = AS_Delimited;
  let category = OMP_DeclareVariant.category;
}
def OMP_Depobj : Directive<"depobj"> {
  let allowedClauses = [
    VersionedClause<OMPC_Depend, 50>,
    // TODO This should ne `none` instead. Comment carried over from
    // OMPKinds.def.
    VersionedClause<OMPC_Depobj, 50>,
    VersionedClause<OMPC_Destroy, 50>,
    VersionedClause<OMPC_Update, 50>,
  ];
  let association = AS_None;
  let category = CA_Executable;
}
def OMP_dispatch : Directive<"dispatch"> {
  let allowedClauses = [
    VersionedClause<OMPC_Depend>,
    VersionedClause<OMPC_Device>,
    VersionedClause<OMPC_HasDeviceAddr, 51>,
    VersionedClause<OMPC_IsDevicePtr>,
    VersionedClause<OMPC_Nocontext>,
    VersionedClause<OMPC_Novariants>,
    VersionedClause<OMPC_NoWait>,
  ];
  let association = AS_Block;
  let category = CA_Executable;
}
def OMP_Distribute : Directive<"distribute"> {
  let allowedClauses = [
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Private>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_DistSchedule>,
    VersionedClause<OMPC_Order, 50>,
  ];
  let association = AS_Loop;
  let category = CA_Executable;
}
def OMP_Do : Directive<"do"> {
  let allowedClauses = [
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Linear>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_Reduction>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_NoWait>,
    VersionedClause<OMPC_Order, 50>,
    VersionedClause<OMPC_Ordered>,
    VersionedClause<OMPC_Schedule>,
  ];
  let association = AS_Loop;
  let category = CA_Executable;
}
def OMP_EndDo : Directive<"end do"> {
  let allowedOnceClauses = [
    VersionedClause<OMPC_NoWait>,
  ];
  // Needed for association computation, since OMP_Do has it "from leafConstructs".
  let leafConstructs = OMP_Do.leafConstructs;
  let association = OMP_Do.association;
  let category = OMP_Do.category;
}
def OMP_Error : Directive<"error"> {
  let allowedClauses = [
    VersionedClause<OMPC_At, 51>,
    VersionedClause<OMPC_Message, 51>,
    VersionedClause<OMPC_Severity, 51>,
  ];
  let association = AS_None;
  let category = CA_Utility;
}
def OMP_Flush : Directive<"flush"> {
  let allowedOnceClauses = [
    VersionedClause<OMPC_AcqRel, 50>,
    VersionedClause<OMPC_Acquire, 50>,
    // TODO This should ne `none` instead. Comment carried over from
    // OMPKinds.def.
    VersionedClause<OMPC_Flush>,
    VersionedClause<OMPC_Release, 50>,
  ];
  let association = AS_None;
  let category = CA_Executable;
}
def OMP_For : Directive<"for"> {
  let allowedClauses = [
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Linear>,
    VersionedClause<OMPC_NoWait>,
    VersionedClause<OMPC_Order, 50>,
    VersionedClause<OMPC_Ordered>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_Schedule>,
  ];
  let association = AS_Loop;
  let category = CA_Executable;
}
def OMP_Interchange : Directive<"interchange"> {
  let association = AS_Loop;
  let category = CA_Executable;
}
def OMP_interop : Directive<"interop"> {
  let allowedClauses = [
    VersionedClause<OMPC_Depend>,
    VersionedClause<OMPC_Destroy>,
    VersionedClause<OMPC_Device>,
    VersionedClause<OMPC_Init>,
    VersionedClause<OMPC_NoWait>,
    VersionedClause<OMPC_Use>,
  ];
  let association = AS_None;
  let category = CA_Executable;
}
def OMP_loop : Directive<"loop"> {
  let allowedClauses = [
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_Reduction>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_Bind, 50>,
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_Order, 50>,
  ];
  let association = AS_Loop;
  let category = CA_Executable;
}
def OMP_masked : Directive<"masked"> {
  let allowedOnceClauses = [
    VersionedClause<OMPC_Filter>,
  ];
  let association = AS_Block;
  let category = CA_Executable;
}
def OMP_Master : Directive<"master"> {
  let association = AS_Block;
  let category = CA_Executable;
}
def OMP_Metadirective : Directive<"metadirective"> {
  let allowedClauses = [
    VersionedClause<OMPC_When>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_Default>,
  ];
  let association = AS_None;
  let category = CA_Meta;
}
def OMP_Nothing : Directive<"nothing"> {
  let association = AS_None;
  let category = CA_Utility;
}
def OMP_Ordered : Directive<"ordered"> {
  let allowedClauses = [
    VersionedClause<OMPC_Depend>,
    VersionedClause<OMPC_Doacross, 52>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_Simd>,
    VersionedClause<OMPC_Threads>,
  ];
  let association = AS_None;
  // There is also a block-associated "ordered" directive.
  let category = CA_Executable;
}
def OMP_Parallel : Directive<"parallel"> {
  let allowedClauses = [
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Copyin>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_OMPX_Attribute>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_Shared>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_NumThreads>,
    VersionedClause<OMPC_ProcBind>,
  ];
  let association = AS_Block;
  let category = CA_Executable;
}
def OMP_Requires : Directive<"requires"> {
  let allowedOnceClauses = [
    VersionedClause<OMPC_UnifiedAddress>,
    VersionedClause<OMPC_UnifiedSharedMemory>,
    // OpenMP 5.2 Spec: If an implementation is not supporting a requirement
    // (reverse offload in this case) then it should give compile-time error
    // termination.
    // Seeting supported version for reverse_offload to a distant future version
    // 9.9 so that its partial support can be tested in the meantime.
    //
    // TODO: Correct this supprted version number whenever complete
    // implementation of reverse_offload is available.
    VersionedClause<OMPC_AtomicDefaultMemOrder>,
    VersionedClause<OMPC_DynamicAllocators>,
    VersionedClause<OMPC_ReverseOffload, 99>,
  ];
  let association = AS_None;
  let category = CA_Informational;
}
def OMP_Reverse : Directive<"reverse"> {
  let association = AS_Loop;
  let category = CA_Executable;
}
def OMP_Scan : Directive<"scan"> {
  let allowedClauses = [
    VersionedClause<OMPC_Exclusive, 50>,
    VersionedClause<OMPC_Inclusive, 50>,
  ];
  let association = AS_Separating;
  let category = CA_Subsidiary;
}
def OMP_scope : Directive<"scope"> {
  let allowedClauses = [
    VersionedClause<OMPC_Private, 51>,
    VersionedClause<OMPC_Reduction, 51>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_NoWait, 51>,
  ];
  let association = AS_Block;
  let category = CA_Executable;
}
def OMP_Section : Directive<"section"> {
  let association = AS_Separating;
  let category = CA_Subsidiary;
}
def OMP_Sections : Directive<"sections"> {
  let allowedClauses = [
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_NoWait>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_Reduction>,
  ];
  let association = AS_Block;
  let category = CA_Executable;
}
def OMP_EndSections : Directive<"end sections"> {
  let allowedOnceClauses = [
    VersionedClause<OMPC_NoWait>,
  ];
  let leafConstructs = OMP_Sections.leafConstructs;
  let association = OMP_Sections.association;
  let category = OMP_Sections.category;
}
def OMP_Simd : Directive<"simd"> {
  let allowedClauses = [
    VersionedClause<OMPC_Aligned>,
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Linear>,
    VersionedClause<OMPC_NonTemporal, 50>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_Reduction>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_If, 50>,
    VersionedClause<OMPC_Order, 50>,
    VersionedClause<OMPC_SafeLen>,
    VersionedClause<OMPC_SimdLen>,
  ];
  let association = AS_Loop;
  let category = CA_Executable;
}
def OMP_Single : Directive<"single"> {
  let allowedClauses = [
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_CopyPrivate>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_NoWait>,
    VersionedClause<OMPC_Private>,
  ];
  let association = AS_Block;
  let category = CA_Executable;
}
def OMP_EndSingle : Directive<"end single"> {
  let allowedClauses = [
    VersionedClause<OMPC_CopyPrivate>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_NoWait>,
  ];
  let leafConstructs = OMP_Single.leafConstructs;
  let association = OMP_Single.association;
  let category = OMP_Single.category;
}
def OMP_Target : Directive<"target"> {
  let allowedClauses = [
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Depend>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_HasDeviceAddr, 51>,
    VersionedClause<OMPC_InReduction, 50>,
    VersionedClause<OMPC_IsDevicePtr>,
    VersionedClause<OMPC_Map>,
    VersionedClause<OMPC_OMPX_Attribute>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_UsesAllocators, 50>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_DefaultMap>,
    VersionedClause<OMPC_Device>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_NoWait>,
    VersionedClause<OMPC_OMPX_DynCGroupMem>,
    VersionedClause<OMPC_ThreadLimit, 51>,
  ];
  let association = AS_Block;
  let category = CA_Executable;
}
def OMP_TargetData : Directive<"target data"> {
  let allowedOnceClauses = [
    VersionedClause<OMPC_Device>,
    VersionedClause<OMPC_If>,
  ];
  let requiredClauses = [
    VersionedClause<OMPC_Map>,
    VersionedClause<OMPC_UseDeviceAddr, 50>,
    VersionedClause<OMPC_UseDevicePtr>,
  ];
  let association = AS_Block;
  let category = CA_Executable;
}
def OMP_TargetEnterData : Directive<"target enter data"> {
  let allowedClauses = [
    VersionedClause<OMPC_Depend>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_Device>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_NoWait>,
  ];
  let requiredClauses = [
    VersionedClause<OMPC_Map>,
  ];
  let association = AS_None;
  let category = CA_Executable;
}
def OMP_TargetExitData : Directive<"target exit data"> {
  let allowedClauses = [
    VersionedClause<OMPC_Depend>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_Device>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_NoWait>,
  ];
  let requiredClauses = [
    VersionedClause<OMPC_Map>,
  ];
  let association = AS_None;
  let category = CA_Executable;
}
def OMP_TargetUpdate : Directive<"target update"> {
  let allowedClauses = [
    VersionedClause<OMPC_Depend>,
    VersionedClause<OMPC_From>,
    VersionedClause<OMPC_To>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_Device>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_NoWait>,
  ];
  let association = AS_None;
  let category = CA_Executable;
}
def OMP_Task : Directive<"task"> {
  let allowedClauses = [
    VersionedClause<OMPC_Affinity, 50>,
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Depend>,
    VersionedClause<OMPC_Detach, 50>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_InReduction>,
    VersionedClause<OMPC_Mergeable>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_Shared>,
    VersionedClause<OMPC_Untied>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_Final>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_Priority>,
  ];
  let association = AS_Block;
  let category = CA_Executable;
}
def OMP_TaskGroup : Directive<"taskgroup"> {
  let allowedClauses = [
    VersionedClause<OMPC_Allocate, 50>,
    VersionedClause<OMPC_TaskReduction, 50>,
  ];
  let association = AS_Block;
  let category = CA_Executable;
}
def OMP_TaskLoop : Directive<"taskloop"> {
  let allowedClauses = [
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_InReduction>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Mergeable>,
    VersionedClause<OMPC_NoGroup>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_Shared>,
    VersionedClause<OMPC_Untied>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_Final>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_Priority>,
  ];
  let allowedExclusiveClauses = [
    VersionedClause<OMPC_GrainSize>,
    VersionedClause<OMPC_NumTasks>,
  ];
  let association = AS_Loop;
  let category = CA_Executable;
}
def OMP_TaskWait : Directive<"taskwait"> {
  let allowedClauses = [
    VersionedClause<OMPC_Depend, 50>,
    VersionedClause<OMPC_NoWait, 51>,
  ];
  let association = AS_None;
  let category = CA_Executable;
}
def OMP_TaskYield : Directive<"taskyield"> {
  let association = AS_None;
  let category = CA_Executable;
}
def OMP_Teams : Directive<"teams"> {
  let allowedClauses = [
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_OMPX_Attribute>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_Shared>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_If, 52>,
    VersionedClause<OMPC_NumTeams>,
    VersionedClause<OMPC_ThreadLimit>,
  ];
  let association = AS_Block;
  let category = CA_Executable;
}
def OMP_ThreadPrivate : Directive<"threadprivate"> {
  let association = AS_None;
  let category = CA_Declarative;
}
def OMP_Tile : Directive<"tile"> {
  let allowedOnceClauses = [
    VersionedClause<OMPC_Sizes, 51>,
  ];
  let association = AS_Loop;
  let category = CA_Executable;
}
def OMP_Unknown : Directive<"unknown"> {
  let isDefault = true;
  let association = AS_None;
  let category = CA_Utility;
}
def OMP_Unroll : Directive<"unroll"> {
  let allowedOnceClauses = [
    VersionedClause<OMPC_Full, 51>,
    VersionedClause<OMPC_Partial, 51>,
  ];
  let association = AS_Loop;
  let category = CA_Executable;
}
def OMP_Workshare : Directive<"workshare"> {
  let allowedOnceClauses = [
    VersionedClause<OMPC_NoWait>,
  ];
  let association = AS_Block;
  let category = CA_Executable;
}
def OMP_EndWorkshare : Directive<"end workshare"> {
  let allowedClauses = [
    VersionedClause<OMPC_NoWait>,
  ];
  let leafConstructs = OMP_Workshare.leafConstructs;
  let association = OMP_Workshare.association;
  let category = OMP_Workshare.category;
}

//===----------------------------------------------------------------------===//
// Definitions of OpenMP compound directives
// Sorted alphabetically wrt directive spelling, except "end xyz" immediately
// follows "xyz".
//===----------------------------------------------------------------------===//

def OMP_DistributeParallelDo : Directive<"distribute parallel do"> {
  let allowedClauses = [
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Copyin>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Linear>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_Shared>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_DistSchedule>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_NumThreads>,
    VersionedClause<OMPC_Order, 50>,
    VersionedClause<OMPC_Ordered>,
    VersionedClause<OMPC_ProcBind>,
    VersionedClause<OMPC_Schedule>,
  ];
  let leafConstructs = [OMP_Distribute, OMP_Parallel, OMP_Do];
  let category = CA_Executable;
}
def OMP_DistributeParallelDoSimd : Directive<"distribute parallel do simd"> {
  let allowedClauses = [
    VersionedClause<OMPC_Aligned>,
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_Copyin>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_DistSchedule>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Linear>,
    VersionedClause<OMPC_NonTemporal>,
    VersionedClause<OMPC_NumThreads>,
    VersionedClause<OMPC_Order, 50>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_ProcBind>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_SafeLen>,
    VersionedClause<OMPC_Schedule>,
    VersionedClause<OMPC_Shared>,
    VersionedClause<OMPC_SimdLen>,
  ];
  let leafConstructs = [OMP_Distribute, OMP_Parallel, OMP_Do, OMP_Simd];
  let category = CA_Executable;
}
def OMP_DistributeParallelFor : Directive<"distribute parallel for"> {
  let allowedClauses = [
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_Copyin>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_DistSchedule>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_NumThreads>,
    VersionedClause<OMPC_OMPX_Attribute>,
    VersionedClause<OMPC_Order, 50>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_ProcBind>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_Schedule>,
    VersionedClause<OMPC_Shared>,
  ];
  let leafConstructs = [OMP_Distribute, OMP_Parallel, OMP_For];
  let category = CA_Executable;
}
def OMP_DistributeParallelForSimd : Directive<"distribute parallel for simd"> {
  let allowedClauses = [
    VersionedClause<OMPC_Aligned>,
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_Copyin>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_DistSchedule>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Linear>,
    VersionedClause<OMPC_NonTemporal, 50>,
    VersionedClause<OMPC_NumThreads>,
    VersionedClause<OMPC_OMPX_Attribute>,
    VersionedClause<OMPC_Order, 50>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_ProcBind>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_SafeLen>,
    VersionedClause<OMPC_Schedule>,
    VersionedClause<OMPC_Shared>,
    VersionedClause<OMPC_SimdLen>,
  ];
  let leafConstructs = [OMP_Distribute, OMP_Parallel, OMP_For, OMP_Simd];
  let category = CA_Executable;
}
def OMP_DistributeSimd : Directive<"distribute simd"> {
  let allowedClauses = [
    VersionedClause<OMPC_Aligned>,
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Copyin>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Linear>,
    VersionedClause<OMPC_NonTemporal, 50>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_Reduction>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_DistSchedule>,
    VersionedClause<OMPC_If, 50>,
    VersionedClause<OMPC_NumThreads>,
    VersionedClause<OMPC_Order, 50>,
    VersionedClause<OMPC_Ordered>,
    VersionedClause<OMPC_ProcBind>,
    VersionedClause<OMPC_SafeLen>,
    VersionedClause<OMPC_Schedule>,
    VersionedClause<OMPC_SimdLen>,
  ];
  let leafConstructs = [OMP_Distribute, OMP_Simd];
  let category = CA_Executable;
}
def OMP_DoSimd : Directive<"do simd"> {
  let allowedClauses = [
    VersionedClause<OMPC_Aligned>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Linear>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_Reduction>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_NoWait>,
    VersionedClause<OMPC_Order, 50>,
    VersionedClause<OMPC_Ordered>,
    VersionedClause<OMPC_SafeLen>,
    VersionedClause<OMPC_Schedule>,
    VersionedClause<OMPC_SimdLen>,
  ];
  let leafConstructs = [OMP_Do, OMP_Simd];
  let category = CA_Executable;
}
def OMP_EndDoSimd : Directive<"end do simd"> {
  let allowedOnceClauses = [
    VersionedClause<OMPC_NoWait>,
  ];
  let leafConstructs = OMP_DoSimd.leafConstructs;
  let association = OMP_DoSimd.association;
  let category = OMP_DoSimd.category;
}
def OMP_ForSimd : Directive<"for simd"> {
  let allowedClauses = [
    VersionedClause<OMPC_Aligned>,
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_If, 50>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Linear>,
    VersionedClause<OMPC_NonTemporal, 50>,
    VersionedClause<OMPC_NoWait>,
    VersionedClause<OMPC_Order, 50>,
    VersionedClause<OMPC_Ordered>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_SafeLen>,
    VersionedClause<OMPC_Schedule>,
    VersionedClause<OMPC_SimdLen>,
  ];
  let leafConstructs = [OMP_For, OMP_Simd];
  let category = CA_Executable;
}
def OMP_target_loop : Directive<"target loop"> {
  let allowedClauses = [
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Depend>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_IsDevicePtr>,
    VersionedClause<OMPC_HasDeviceAddr, 51>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Map>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_UsesAllocators, 50>,
    VersionedClause<OMPC_OMPX_Attribute>,
    VersionedClause<OMPC_InReduction, 50>,

  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_Bind, 50>,
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_Order>,
    VersionedClause<OMPC_ThreadLimit>,
    VersionedClause<OMPC_OMPX_DynCGroupMem>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_Device>,
    VersionedClause<OMPC_DefaultMap>,
    VersionedClause<OMPC_NoWait>,
  ];
  let leafConstructs = [OMP_Target, OMP_loop];
  let category = CA_Executable;
}
def OMP_MaskedTaskloop : Directive<"masked taskloop"> {
  let allowedClauses = [
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_Filter>,
    VersionedClause<OMPC_Final>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_GrainSize>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_InReduction>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Mergeable>,
    VersionedClause<OMPC_NoGroup>,
    VersionedClause<OMPC_NumTasks>,
    VersionedClause<OMPC_Priority>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_Shared>,
    VersionedClause<OMPC_Untied>,
  ];
  let leafConstructs = [OMP_masked, OMP_TaskLoop];
  let category = CA_Executable;
}
def OMP_MaskedTaskloopSimd : Directive<"masked taskloop simd"> {
  let allowedClauses = [
    VersionedClause<OMPC_Aligned>,
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_Filter>,
    VersionedClause<OMPC_Final>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_GrainSize>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_InReduction>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Linear>,
    VersionedClause<OMPC_Mergeable>,
    VersionedClause<OMPC_NoGroup>,
    VersionedClause<OMPC_NonTemporal, 50>,
    VersionedClause<OMPC_NumTasks>,
    VersionedClause<OMPC_Order, 50>,
    VersionedClause<OMPC_Priority>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_SafeLen>,
    VersionedClause<OMPC_Shared>,
    VersionedClause<OMPC_SimdLen>,
    VersionedClause<OMPC_Untied>,
  ];
  let leafConstructs = [OMP_masked, OMP_TaskLoop, OMP_Simd];
  let category = CA_Executable;
}
def OMP_MasterTaskloop : Directive<"master taskloop"> {
  let allowedClauses = [
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_Final>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_GrainSize>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_InReduction>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Mergeable>,
    VersionedClause<OMPC_NoGroup>,
    VersionedClause<OMPC_NumTasks>,
    VersionedClause<OMPC_Priority>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_Shared>,
    VersionedClause<OMPC_Untied>,
  ];
  let leafConstructs = [OMP_Master, OMP_TaskLoop];
  let category = CA_Executable;
}
def OMP_MasterTaskloopSimd : Directive<"master taskloop simd"> {
  let allowedClauses = [
    VersionedClause<OMPC_Aligned>,
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_Final>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_GrainSize>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_InReduction>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Linear>,
    VersionedClause<OMPC_Mergeable>,
    VersionedClause<OMPC_NoGroup>,
    VersionedClause<OMPC_NonTemporal, 50>,
    VersionedClause<OMPC_NumTasks>,
    VersionedClause<OMPC_Order, 50>,
    VersionedClause<OMPC_Priority>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_SafeLen>,
    VersionedClause<OMPC_Shared>,
    VersionedClause<OMPC_SimdLen>,
    VersionedClause<OMPC_Untied>,
  ];
  let leafConstructs = [OMP_Master, OMP_TaskLoop, OMP_Simd];
  let category = CA_Executable;
}
def OMP_ParallelDo : Directive<"parallel do"> {
  let allowedClauses = [
    VersionedClause<OMPC_Copyin>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Linear>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_Shared>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_NumThreads>,
    VersionedClause<OMPC_Order, 50>,
    VersionedClause<OMPC_Ordered>,
    VersionedClause<OMPC_ProcBind>,
    VersionedClause<OMPC_Schedule>,
  ];
  let leafConstructs = [OMP_Parallel, OMP_Do];
  let category = CA_Executable;
}
def OMP_ParallelDoSimd : Directive<"parallel do simd"> {
  let allowedClauses = [
    VersionedClause<OMPC_Aligned>,
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Copyin>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Linear>,
    VersionedClause<OMPC_NonTemporal>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_Shared>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_NumThreads>,
    VersionedClause<OMPC_Order, 50>,
    VersionedClause<OMPC_Ordered>,
    VersionedClause<OMPC_ProcBind>,
    VersionedClause<OMPC_SafeLen>,
    VersionedClause<OMPC_Schedule>,
    VersionedClause<OMPC_SimdLen>,
  ];
  let leafConstructs = [OMP_Parallel, OMP_Do, OMP_Simd];
  let category = CA_Executable;
}
def OMP_ParallelFor : Directive<"parallel for"> {
  let allowedClauses = [
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_Copyin>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Linear>,
    VersionedClause<OMPC_NumThreads>,
    VersionedClause<OMPC_OMPX_Attribute>,
    VersionedClause<OMPC_Order, 50>,
    VersionedClause<OMPC_Ordered>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_ProcBind>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_Schedule>,
    VersionedClause<OMPC_Shared>,
  ];
  let leafConstructs = [OMP_Parallel, OMP_For];
  let category = CA_Executable;
}
def OMP_ParallelForSimd : Directive<"parallel for simd"> {
  let allowedClauses = [
    VersionedClause<OMPC_Aligned>,
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_Copyin>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Linear>,
    VersionedClause<OMPC_NonTemporal, 50>,
    VersionedClause<OMPC_NumThreads>,
    VersionedClause<OMPC_OMPX_Attribute>,
    VersionedClause<OMPC_Order, 50>,
    VersionedClause<OMPC_Ordered>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_ProcBind>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_SafeLen>,
    VersionedClause<OMPC_Schedule>,
    VersionedClause<OMPC_Shared>,
    VersionedClause<OMPC_SimdLen>,
  ];
  let leafConstructs = [OMP_Parallel, OMP_For, OMP_Simd];
  let category = CA_Executable;
}
def OMP_parallel_loop : Directive<"parallel loop"> {
  let allowedClauses = [
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Copyin>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_OMPX_Attribute>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_Shared>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_Bind, 50>,
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_NumThreads>,
    VersionedClause<OMPC_Order>,
    VersionedClause<OMPC_ProcBind>,
  ];
  let leafConstructs = [OMP_Parallel, OMP_loop];
  let category = CA_Executable;
}
def OMP_ParallelMasked : Directive<"parallel masked"> {
  let allowedClauses = [
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Copyin>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_Filter>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_NumThreads>,
    VersionedClause<OMPC_OMPX_Attribute>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_ProcBind>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_Shared>,
  ];
  let leafConstructs = [OMP_Parallel, OMP_masked];
  let category = CA_Executable;
}
def OMP_ParallelMaskedTaskloop :
    Directive<"parallel masked taskloop"> {
  let allowedClauses = [
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_Copyin>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_Filter>,
    VersionedClause<OMPC_Final>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_GrainSize>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Mergeable>,
    VersionedClause<OMPC_NoGroup>,
    VersionedClause<OMPC_NumTasks>,
    VersionedClause<OMPC_NumThreads>,
    VersionedClause<OMPC_OMPX_Attribute>,
    VersionedClause<OMPC_Priority>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_ProcBind>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_Shared>,
    VersionedClause<OMPC_Untied>,
  ];
  let leafConstructs = [OMP_Parallel, OMP_masked, OMP_TaskLoop];
  let category = CA_Executable;
}
def OMP_ParallelMaskedTaskloopSimd :
    Directive<"parallel masked taskloop simd"> {
  let allowedClauses = [
    VersionedClause<OMPC_Aligned>,
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_Copyin>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_Filter>,
    VersionedClause<OMPC_Final>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_GrainSize>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Linear>,
    VersionedClause<OMPC_Mergeable>,
    VersionedClause<OMPC_NoGroup>,
    VersionedClause<OMPC_NonTemporal, 50>,
    VersionedClause<OMPC_NumTasks>,
    VersionedClause<OMPC_NumThreads>,
    VersionedClause<OMPC_OMPX_Attribute>,
    VersionedClause<OMPC_Order, 50>,
    VersionedClause<OMPC_Priority>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_ProcBind>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_SafeLen>,
    VersionedClause<OMPC_Shared>,
    VersionedClause<OMPC_SimdLen>,
    VersionedClause<OMPC_Untied>,
  ];
  let leafConstructs = [OMP_Parallel, OMP_masked, OMP_TaskLoop, OMP_Simd];
  let category = CA_Executable;
}
def OMP_ParallelMaster : Directive<"parallel master"> {
  let allowedClauses = [
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Copyin>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_NumThreads>,
    VersionedClause<OMPC_OMPX_Attribute>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_ProcBind>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_Shared>,
  ];
  let leafConstructs = [OMP_Parallel, OMP_Master];
  let category = CA_Executable;
}
def OMP_ParallelMasterTaskloop :
    Directive<"parallel master taskloop"> {
  let allowedClauses = [
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_Copyin>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_Final>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_GrainSize>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Mergeable>,
    VersionedClause<OMPC_NoGroup>,
    VersionedClause<OMPC_NumTasks>,
    VersionedClause<OMPC_NumThreads>,
    VersionedClause<OMPC_OMPX_Attribute>,
    VersionedClause<OMPC_Priority>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_ProcBind>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_Shared>,
    VersionedClause<OMPC_Untied>,
  ];
  let leafConstructs = [OMP_Parallel, OMP_Master, OMP_TaskLoop];
  let category = CA_Executable;
}
def OMP_ParallelMasterTaskloopSimd :
    Directive<"parallel master taskloop simd"> {
  let allowedClauses = [
    VersionedClause<OMPC_Aligned>,
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_Copyin>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_Final>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_GrainSize>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Linear>,
    VersionedClause<OMPC_Mergeable>,
    VersionedClause<OMPC_NoGroup>,
    VersionedClause<OMPC_NonTemporal, 50>,
    VersionedClause<OMPC_NumTasks>,
    VersionedClause<OMPC_NumThreads>,
    VersionedClause<OMPC_OMPX_Attribute>,
    VersionedClause<OMPC_Order, 50>,
    VersionedClause<OMPC_Priority>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_ProcBind>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_SafeLen>,
    VersionedClause<OMPC_Shared>,
    VersionedClause<OMPC_SimdLen>,
    VersionedClause<OMPC_Untied>,
  ];
  let leafConstructs = [OMP_Parallel, OMP_Master, OMP_TaskLoop, OMP_Simd];
  let category = CA_Executable;
}
def OMP_ParallelSections : Directive<"parallel sections"> {
  let allowedClauses = [
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Copyin>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_OMPX_Attribute>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_ProcBind>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_Shared>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_NumThreads>,
  ];
  let leafConstructs = [OMP_Parallel, OMP_Sections];
  let category = CA_Executable;
}
def OMP_ParallelWorkshare : Directive<"parallel workshare"> {
  let allowedClauses = [
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Copyin>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_Shared>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_NumThreads>,
    VersionedClause<OMPC_ProcBind>,
  ];
  let leafConstructs = [OMP_Parallel, OMP_Workshare];
  let category = CA_Executable;
}
def OMP_TargetParallel : Directive<"target parallel"> {
  let allowedClauses = [
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_Depend>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_HasDeviceAddr, 51>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_IsDevicePtr>,
    VersionedClause<OMPC_Map>,
    VersionedClause<OMPC_NoWait>,
    VersionedClause<OMPC_OMPX_Attribute>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_Shared>,
    VersionedClause<OMPC_UsesAllocators, 50>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_DefaultMap>,
    VersionedClause<OMPC_Device>,
    VersionedClause<OMPC_NumThreads>,
    VersionedClause<OMPC_OMPX_DynCGroupMem>,
    VersionedClause<OMPC_ProcBind>,
    VersionedClause<OMPC_ThreadLimit, 51>,
  ];
  let leafConstructs = [OMP_Target, OMP_Parallel];
  let category = CA_Executable;
}
def OMP_TargetParallelDo : Directive<"target parallel do"> {
  let allowedClauses = [
    VersionedClause<OMPC_Allocator>,
    VersionedClause<OMPC_Copyin>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_Depend>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_HasDeviceAddr, 51>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_IsDevicePtr>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Linear>,
    VersionedClause<OMPC_Map>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_Shared>,
    VersionedClause<OMPC_UsesAllocators>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_DefaultMap>,
    VersionedClause<OMPC_Device>,
    VersionedClause<OMPC_NoWait>,
    VersionedClause<OMPC_NumThreads>,
    VersionedClause<OMPC_Order, 50>,
    VersionedClause<OMPC_Ordered>,
    VersionedClause<OMPC_ProcBind>,
    VersionedClause<OMPC_Schedule>,
  ];
  let leafConstructs = [OMP_Target, OMP_Parallel, OMP_Do];
  let category = CA_Executable;
}
def OMP_TargetParallelDoSimd : Directive<"target parallel do simd"> {
  let allowedClauses = [
    VersionedClause<OMPC_Aligned>,
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_DefaultMap>,
    VersionedClause<OMPC_Depend>,
    VersionedClause<OMPC_Device>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_HasDeviceAddr, 51>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_IsDevicePtr>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Linear>,
    VersionedClause<OMPC_Map>,
    VersionedClause<OMPC_NonTemporal>,
    VersionedClause<OMPC_NoWait>,
    VersionedClause<OMPC_NumThreads>,
    VersionedClause<OMPC_Order, 50>,
    VersionedClause<OMPC_Ordered>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_ProcBind>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_SafeLen>,
    VersionedClause<OMPC_Schedule>,
    VersionedClause<OMPC_Shared>,
    VersionedClause<OMPC_SimdLen>,
    VersionedClause<OMPC_UsesAllocators>,
  ];
  let leafConstructs = [OMP_Target, OMP_Parallel, OMP_Do, OMP_Simd];
  let category = CA_Executable;
}
def OMP_TargetParallelFor : Directive<"target parallel for"> {
  let allowedClauses = [
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_DefaultMap>,
    VersionedClause<OMPC_Depend>,
    VersionedClause<OMPC_Device>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_HasDeviceAddr, 51>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_IsDevicePtr>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Linear>,
    VersionedClause<OMPC_Map>,
    VersionedClause<OMPC_NoWait>,
    VersionedClause<OMPC_NumThreads>,
    VersionedClause<OMPC_OMPX_Attribute>,
    VersionedClause<OMPC_Order, 50>,
    VersionedClause<OMPC_Ordered>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_ProcBind>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_Schedule>,
    VersionedClause<OMPC_Shared>,
    VersionedClause<OMPC_UsesAllocators, 50>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_OMPX_DynCGroupMem>,
    VersionedClause<OMPC_ThreadLimit, 51>,
  ];
  let leafConstructs = [OMP_Target, OMP_Parallel, OMP_For];
  let category = CA_Executable;
}
def OMP_TargetParallelForSimd : Directive<"target parallel for simd"> {
  let allowedClauses = [
    VersionedClause<OMPC_Aligned>,
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_DefaultMap>,
    VersionedClause<OMPC_Depend>,
    VersionedClause<OMPC_Device>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_HasDeviceAddr, 51>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_IsDevicePtr>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Linear>,
    VersionedClause<OMPC_Map>,
    VersionedClause<OMPC_NonTemporal, 50>,
    VersionedClause<OMPC_NoWait>,
    VersionedClause<OMPC_NumThreads>,
    VersionedClause<OMPC_OMPX_Attribute>,
    VersionedClause<OMPC_Order, 50>,
    VersionedClause<OMPC_Ordered>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_ProcBind>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_SafeLen>,
    VersionedClause<OMPC_Schedule>,
    VersionedClause<OMPC_Shared>,
    VersionedClause<OMPC_SimdLen>,
    VersionedClause<OMPC_UsesAllocators, 50>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_OMPX_DynCGroupMem>,
    VersionedClause<OMPC_ThreadLimit, 51>,
  ];
  let leafConstructs = [OMP_Target, OMP_Parallel, OMP_For, OMP_Simd];
  let category = CA_Executable;
}
def OMP_target_parallel_loop : Directive<"target parallel loop"> {
  let allowedClauses = [
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Copyin>,
    VersionedClause<OMPC_Depend>,
    VersionedClause<OMPC_Device>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_HasDeviceAddr, 51>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_IsDevicePtr>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Map>,
    VersionedClause<OMPC_OMPX_Attribute>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_Shared>,
    VersionedClause<OMPC_UsesAllocators, 50>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_Bind, 50>,
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_DefaultMap>,
    VersionedClause<OMPC_NoWait>,
    VersionedClause<OMPC_NumThreads>,
    VersionedClause<OMPC_OMPX_DynCGroupMem>,
    VersionedClause<OMPC_Order>,
    VersionedClause<OMPC_ProcBind>,
    VersionedClause<OMPC_ThreadLimit, 51>,
  ];
  let leafConstructs = [OMP_Target, OMP_Parallel, OMP_loop];
  let category = CA_Executable;
}
def OMP_TargetSimd : Directive<"target simd"> {
  let allowedClauses = [
    VersionedClause<OMPC_Aligned>,
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Depend>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_HasDeviceAddr, 51>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_IsDevicePtr>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Linear>,
    VersionedClause<OMPC_Map>,
    VersionedClause<OMPC_NonTemporal, 50>,
    VersionedClause<OMPC_NoWait>,
    VersionedClause<OMPC_OMPX_Attribute>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_Shared>,
    VersionedClause<OMPC_UsesAllocators, 50>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_DefaultMap>,
    VersionedClause<OMPC_Device>,
    VersionedClause<OMPC_NumThreads>,
    VersionedClause<OMPC_OMPX_DynCGroupMem>,
    VersionedClause<OMPC_Order, 50>,
    VersionedClause<OMPC_ProcBind>,
    VersionedClause<OMPC_SafeLen>,
    VersionedClause<OMPC_Schedule>,
    VersionedClause<OMPC_SimdLen>,
    VersionedClause<OMPC_ThreadLimit, 51>,
  ];
  let leafConstructs = [OMP_Target, OMP_Simd];
  let category = CA_Executable;
}
def OMP_TargetTeams : Directive<"target teams"> {
  let allowedClauses = [
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Depend>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_HasDeviceAddr, 51>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_IsDevicePtr>,
    VersionedClause<OMPC_Map>,
    VersionedClause<OMPC_OMPX_Attribute>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_Shared>,
    VersionedClause<OMPC_UsesAllocators, 50>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_DefaultMap>,
    VersionedClause<OMPC_Device>,
    VersionedClause<OMPC_NoWait>,
    VersionedClause<OMPC_NumTeams>,
    VersionedClause<OMPC_OMPX_DynCGroupMem>,
    VersionedClause<OMPC_OMPX_Bare>,
    VersionedClause<OMPC_ThreadLimit>,
  ];
  let leafConstructs = [OMP_Target, OMP_Teams];
  let category = CA_Executable;
}
def OMP_TargetTeamsDistribute : Directive<"target teams distribute"> {
  let allowedClauses = [
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Depend>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_HasDeviceAddr, 51>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_IsDevicePtr>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Map>,
    VersionedClause<OMPC_OMPX_Attribute>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_Shared>,
    VersionedClause<OMPC_UsesAllocators, 50>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_DefaultMap>,
    VersionedClause<OMPC_Device>,
    VersionedClause<OMPC_DistSchedule>,
    VersionedClause<OMPC_NoWait>,
    VersionedClause<OMPC_NumTeams>,
    VersionedClause<OMPC_OMPX_DynCGroupMem>,
    VersionedClause<OMPC_Order, 50>,
    VersionedClause<OMPC_ThreadLimit>,
  ];
  let leafConstructs = [OMP_Target, OMP_Teams, OMP_Distribute];
  let category = CA_Executable;
}
def OMP_TargetTeamsDistributeParallelDo :
    Directive<"target teams distribute parallel do"> {
  let allowedClauses = [
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Copyin>,
    VersionedClause<OMPC_Depend>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_HasDeviceAddr, 51>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_IsDevicePtr>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Linear>,
    VersionedClause<OMPC_Map>,
    VersionedClause<OMPC_Ordered>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_Shared>,
    VersionedClause<OMPC_UsesAllocators>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_DefaultMap>,
    VersionedClause<OMPC_Device>,
    VersionedClause<OMPC_DistSchedule>,
    VersionedClause<OMPC_NoWait>,
    VersionedClause<OMPC_NumTeams>,
    VersionedClause<OMPC_NumThreads>,
    VersionedClause<OMPC_Order, 50>,
    VersionedClause<OMPC_ProcBind>,
    VersionedClause<OMPC_Schedule>,
    VersionedClause<OMPC_ThreadLimit>,
  ];
  let leafConstructs = [OMP_Target, OMP_Teams, OMP_Distribute, OMP_Parallel, OMP_Do];
  let category = CA_Executable;
}
def OMP_TargetTeamsDistributeParallelDoSimd :
    Directive<"target teams distribute parallel do simd"> {
  let allowedClauses = [
    VersionedClause<OMPC_Aligned>,
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Copyin>,
    VersionedClause<OMPC_Depend>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_HasDeviceAddr, 51>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_IsDevicePtr>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Linear>,
    VersionedClause<OMPC_Map>,
    VersionedClause<OMPC_NonTemporal>,
    VersionedClause<OMPC_Ordered>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_Shared>,
    VersionedClause<OMPC_UsesAllocators>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_DefaultMap>,
    VersionedClause<OMPC_Device>,
    VersionedClause<OMPC_DistSchedule>,
    VersionedClause<OMPC_NoWait>,
    VersionedClause<OMPC_NumTeams>,
    VersionedClause<OMPC_NumThreads>,
    VersionedClause<OMPC_Order, 50>,
    VersionedClause<OMPC_ProcBind>,
    VersionedClause<OMPC_SafeLen>,
    VersionedClause<OMPC_Schedule>,
    VersionedClause<OMPC_SimdLen>,
    VersionedClause<OMPC_ThreadLimit>,
  ];
  let leafConstructs = [OMP_Target, OMP_Teams, OMP_Distribute, OMP_Parallel, OMP_Do, OMP_Simd];
  let category = CA_Executable;
}
def OMP_TargetTeamsDistributeParallelFor :
    Directive<"target teams distribute parallel for"> {
  let allowedClauses = [
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_DefaultMap>,
    VersionedClause<OMPC_Depend>,
    VersionedClause<OMPC_Device>,
    VersionedClause<OMPC_DistSchedule>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_HasDeviceAddr, 51>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_IsDevicePtr>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Map>,
    VersionedClause<OMPC_NoWait>,
    VersionedClause<OMPC_NumTeams>,
    VersionedClause<OMPC_NumThreads>,
    VersionedClause<OMPC_OMPX_Attribute>,
    VersionedClause<OMPC_Order, 50>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_ProcBind>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_Schedule>,
    VersionedClause<OMPC_Shared>,
    VersionedClause<OMPC_ThreadLimit>,
    VersionedClause<OMPC_UsesAllocators, 50>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_OMPX_DynCGroupMem>,
  ];
  let leafConstructs = [OMP_Target, OMP_Teams, OMP_Distribute, OMP_Parallel, OMP_For];
  let category = CA_Executable;
}
def OMP_TargetTeamsDistributeParallelForSimd :
    Directive<"target teams distribute parallel for simd"> {
  let allowedClauses = [
    VersionedClause<OMPC_Aligned>,
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_DefaultMap>,
    VersionedClause<OMPC_Depend>,
    VersionedClause<OMPC_Device>,
    VersionedClause<OMPC_DistSchedule>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_HasDeviceAddr, 51>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_IsDevicePtr>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Linear>,
    VersionedClause<OMPC_Map>,
    VersionedClause<OMPC_NonTemporal, 50>,
    VersionedClause<OMPC_NoWait>,
    VersionedClause<OMPC_NumTeams>,
    VersionedClause<OMPC_NumThreads>,
    VersionedClause<OMPC_OMPX_Attribute>,
    VersionedClause<OMPC_Order, 50>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_ProcBind>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_SafeLen>,
    VersionedClause<OMPC_Schedule>,
    VersionedClause<OMPC_Shared>,
    VersionedClause<OMPC_SimdLen>,
    VersionedClause<OMPC_ThreadLimit>,
    VersionedClause<OMPC_UsesAllocators, 50>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_OMPX_DynCGroupMem>,
  ];
  let leafConstructs = [OMP_Target, OMP_Teams, OMP_Distribute, OMP_Parallel, OMP_For, OMP_Simd];
  let category = CA_Executable;
}
def OMP_TargetTeamsDistributeSimd :
    Directive<"target teams distribute simd"> {
  let allowedClauses = [
    VersionedClause<OMPC_Aligned>,
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Depend>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_HasDeviceAddr, 51>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_IsDevicePtr>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Linear>,
    VersionedClause<OMPC_Map>,
    VersionedClause<OMPC_NonTemporal, 50>,
    VersionedClause<OMPC_OMPX_Attribute>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_Shared>,
    VersionedClause<OMPC_UsesAllocators, 50>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_DefaultMap>,
    VersionedClause<OMPC_Device>,
    VersionedClause<OMPC_DistSchedule>,
    VersionedClause<OMPC_NoWait>,
    VersionedClause<OMPC_NumTeams>,
    VersionedClause<OMPC_OMPX_DynCGroupMem>,
    VersionedClause<OMPC_Order, 50>,
    VersionedClause<OMPC_SafeLen>,
    VersionedClause<OMPC_SimdLen>,
    VersionedClause<OMPC_ThreadLimit>,
  ];
  let leafConstructs = [OMP_Target, OMP_Teams, OMP_Distribute, OMP_Simd];
  let category = CA_Executable;
}
def OMP_target_teams_loop : Directive<"target teams loop"> {
  let allowedClauses = [
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_DefaultMap>,
    VersionedClause<OMPC_Depend>,
    VersionedClause<OMPC_Device>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_HasDeviceAddr, 51>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_IsDevicePtr>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Map>,
    VersionedClause<OMPC_OMPX_Attribute>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_Shared>,
    VersionedClause<OMPC_UsesAllocators, 50>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_Bind, 50>,
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_NoWait>,
    VersionedClause<OMPC_NumTeams>,
    VersionedClause<OMPC_OMPX_DynCGroupMem>,
    VersionedClause<OMPC_Order>,
    VersionedClause<OMPC_ThreadLimit>,
  ];
  let leafConstructs = [OMP_Target, OMP_Teams, OMP_loop];
  let category = CA_Executable;
}
def OMP_TaskLoopSimd : Directive<"taskloop simd"> {
  let allowedClauses = [
    VersionedClause<OMPC_Aligned>,
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_InReduction>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Linear>,
    VersionedClause<OMPC_Mergeable>,
    VersionedClause<OMPC_NoGroup>,
    VersionedClause<OMPC_NonTemporal, 50>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_Shared>,
    VersionedClause<OMPC_Untied>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_Final>,
    VersionedClause<OMPC_Order, 50>,
    VersionedClause<OMPC_Priority>,
    VersionedClause<OMPC_SafeLen>,
    VersionedClause<OMPC_SimdLen>,
  ];
  let allowedExclusiveClauses = [
    VersionedClause<OMPC_GrainSize>,
    VersionedClause<OMPC_NumTasks>,
  ];
  let leafConstructs = [OMP_TaskLoop, OMP_Simd];
  let category = CA_Executable;
}
def OMP_TeamsDistribute : Directive<"teams distribute"> {
  let allowedClauses = [
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_DistSchedule>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_NumTeams>,
    VersionedClause<OMPC_OMPX_Attribute>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_Shared>,
    VersionedClause<OMPC_ThreadLimit>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_Order, 50>,
  ];
  let leafConstructs = [OMP_Teams, OMP_Distribute];
  let category = CA_Executable;
}
def OMP_TeamsDistributeParallelDo :
    Directive<"teams distribute parallel do"> {
  let allowedClauses = [
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Copyin>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Linear>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_Shared>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_DistSchedule>,
    VersionedClause<OMPC_NumTeams>,
    VersionedClause<OMPC_NumThreads>,
    VersionedClause<OMPC_Order, 50>,
    VersionedClause<OMPC_Ordered>,
    VersionedClause<OMPC_ProcBind>,
    VersionedClause<OMPC_Schedule>,
    VersionedClause<OMPC_ThreadLimit>,
  ];
  let leafConstructs = [OMP_Teams, OMP_Distribute, OMP_Parallel, OMP_Do];
  let category = CA_Executable;
}
def OMP_TeamsDistributeParallelDoSimd :
    Directive<"teams distribute parallel do simd"> {
  let allowedClauses = [
    VersionedClause<OMPC_Aligned>,
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Linear>,
    VersionedClause<OMPC_NonTemporal>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_Shared>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_DistSchedule>,
    VersionedClause<OMPC_NumTeams>,
    VersionedClause<OMPC_NumThreads>,
    VersionedClause<OMPC_Order, 50>,
    VersionedClause<OMPC_ProcBind>,
    VersionedClause<OMPC_SafeLen>,
    VersionedClause<OMPC_Schedule>,
    VersionedClause<OMPC_SimdLen>,
    VersionedClause<OMPC_ThreadLimit>,
  ];
  let leafConstructs = [OMP_Teams, OMP_Distribute, OMP_Parallel, OMP_Do, OMP_Simd];
  let category = CA_Executable;
}
def OMP_TeamsDistributeParallelFor :
    Directive<"teams distribute parallel for"> {
  let allowedClauses = [
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_Copyin>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_DistSchedule>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_NumTeams>,
    VersionedClause<OMPC_NumThreads>,
    VersionedClause<OMPC_OMPX_Attribute>,
    VersionedClause<OMPC_Order, 50>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_ProcBind>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_Schedule>,
    VersionedClause<OMPC_Shared>,
    VersionedClause<OMPC_ThreadLimit>,
  ];
  let leafConstructs = [OMP_Teams, OMP_Distribute, OMP_Parallel, OMP_For];
  let category = CA_Executable;
}
def OMP_TeamsDistributeParallelForSimd :
    Directive<"teams distribute parallel for simd"> {
  let allowedClauses = [
    VersionedClause<OMPC_Aligned>,
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_DistSchedule>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_If>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Linear>,
    VersionedClause<OMPC_NonTemporal, 50>,
    VersionedClause<OMPC_NumTeams>,
    VersionedClause<OMPC_NumThreads>,
    VersionedClause<OMPC_OMPX_Attribute>,
    VersionedClause<OMPC_Order, 50>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_ProcBind>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_SafeLen>,
    VersionedClause<OMPC_Schedule>,
    VersionedClause<OMPC_Shared>,
    VersionedClause<OMPC_SimdLen>,
    VersionedClause<OMPC_ThreadLimit>,
  ];
  let leafConstructs = [OMP_Teams, OMP_Distribute, OMP_Parallel, OMP_For, OMP_Simd];
  let category = CA_Executable;
}
def OMP_TeamsDistributeSimd : Directive<"teams distribute simd"> {
  let allowedClauses = [
    VersionedClause<OMPC_Aligned>,
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_If, 50>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_Linear>,
    VersionedClause<OMPC_NonTemporal, 50>,
    VersionedClause<OMPC_OMPX_Attribute>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_Shared>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_DistSchedule>,
    VersionedClause<OMPC_NumTeams>,
    VersionedClause<OMPC_Order, 50>,
    VersionedClause<OMPC_SafeLen>,
    VersionedClause<OMPC_SimdLen>,
    VersionedClause<OMPC_ThreadLimit>,
  ];
  let leafConstructs = [OMP_Teams, OMP_Distribute, OMP_Simd];
  let category = CA_Executable;
}
def OMP_teams_loop : Directive<"teams loop"> {
  let allowedClauses = [
    VersionedClause<OMPC_Allocate>,
    VersionedClause<OMPC_FirstPrivate>,
    VersionedClause<OMPC_LastPrivate>,
    VersionedClause<OMPC_OMPX_Attribute>,
    VersionedClause<OMPC_Private>,
    VersionedClause<OMPC_Reduction>,
    VersionedClause<OMPC_Shared>,
  ];
  let allowedOnceClauses = [
    VersionedClause<OMPC_Bind, 50>,
    VersionedClause<OMPC_Collapse>,
    VersionedClause<OMPC_Default>,
    VersionedClause<OMPC_NumTeams>,
    VersionedClause<OMPC_Order>,
    VersionedClause<OMPC_ThreadLimit>,
  ];
  let leafConstructs = [OMP_Teams, OMP_loop];
  let category = CA_Executable;
}
