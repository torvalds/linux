/** @file
 * VirtualBox Status Codes.
 */

/*
 * Copyright (C) 2006-2019 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */

#ifndef VBOX_INCLUDED_err_h
#define VBOX_INCLUDED_err_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/cdefs.h>
#include <iprt/err.h>


/** @defgroup grp_err       VBox Error Codes
 * @{
 */

/* SED-START */

/** @name Misc. Status Codes
 * @{
 */
/** Failed to allocate VM memory. */
#define VERR_NO_VM_MEMORY                   (-1000)
/** RC is toasted and the VMM should be terminated at once, but no need to
 * panic about it :-) */
#define VERR_DONT_PANIC                     (-1001)
/** Unsupported CPU. */
#define VERR_UNSUPPORTED_CPU                (-1002)
/** Unsupported CPU mode. */
#define VERR_UNSUPPORTED_CPU_MODE           (-1003)
/** Page not present. */
#define VERR_PAGE_NOT_PRESENT               (-1004)
/** Invalid/Corrupted configuration file. */
#define VERR_CFG_INVALID_FORMAT             (-1005)
/** No configuration value exists. */
#define VERR_CFG_NO_VALUE                   (-1006)
/** Selector not present. */
#define VERR_SELECTOR_NOT_PRESENT           (-1007)
/** Not code selector. */
#define VERR_NOT_CODE_SELECTOR              (-1008)
/** Not data selector. */
#define VERR_NOT_DATA_SELECTOR              (-1009)
/** Out of selector bounds. */
#define VERR_OUT_OF_SELECTOR_BOUNDS         (-1010)
/** Invalid selector. Usually beyond table limits. */
#define VERR_INVALID_SELECTOR               (-1011)
/** Invalid requested privilege level. */
#define VERR_INVALID_RPL                    (-1012)
/** PML4 entry not present. */
#define VERR_PAGE_MAP_LEVEL4_NOT_PRESENT    (-1013)
/** Page directory pointer not present. */
#define VERR_PAGE_DIRECTORY_PTR_NOT_PRESENT (-1014)
/** Raw mode doesn't support SMP. */
#define VERR_RAW_MODE_INVALID_SMP           (-1015)
/** Invalid VM handle. */
#define VERR_INVALID_VM_HANDLE              (-1016)
/** Invalid VM handle. */
#define VERR_INVALID_VMCPU_HANDLE           (-1017)
/** Invalid Virtual CPU ID. */
#define VERR_INVALID_CPU_ID                 (-1018)
/** Too many VCPUs. */
#define VERR_TOO_MANY_CPUS                  (-1019)
/** The service was disabled on the host.
 * Returned by pfnInit in VBoxService to indicated a non-fatal error that
 * should results in the particular service being disabled. */
#define VERR_SERVICE_DISABLED               (-1020)
/** The requested feature is not supported in raw-mode. */
#define VERR_NOT_SUP_IN_RAW_MODE            (-1021)
/** Invalid CPU index. */
#define VERR_INVALID_CPU_INDEX              (-1022)
/** This VirtualBox build does not support raw-mode. */
#define VERR_RAW_MODE_NOT_SUPPORTED         (-1023)
/** Essential fields in the shared VM structure doesn't match the global one. */
#define VERR_INCONSISTENT_VM_HANDLE         (-1024)
/** The VM has been restored. */
#define VERR_VM_RESTORED                    (-1025)
/** @} */


/** @name Execution Monitor/Manager (EM) Status Codes
 *
 * The order of the status codes between VINF_EM_FIRST and VINF_EM_LAST
 * are of vital importance. The lower the number the higher importance
 * as a scheduling instruction.
 * @{
 */
/** First scheduling related status code. */
#define VINF_EM_FIRST                       1100
/** Indicating that the VM is being terminated and that the execution
 * shall stop. */
#define VINF_EM_TERMINATE                   1100
/** Hypervisor code was stepped.
 * EM will first send this to the debugger, and if the issue isn't
 * resolved there it will enter guru meditation. */
#define VINF_EM_DBG_HYPER_STEPPED           1101
/** Hit a breakpoint in the hypervisor code,
 * EM will first send this to the debugger, and if the issue isn't
 * resolved there it will enter guru meditation. */
#define VINF_EM_DBG_HYPER_BREAKPOINT        1102
/** Hit a possible assertion in the hypervisor code,
 * EM will first send this to the debugger, and if the issue isn't
 * resolved there it will enter guru meditation. */
#define VINF_EM_DBG_HYPER_ASSERTION         1103
/** Generic debug event, suspend the VM for debugging. */
#define VINF_EM_DBG_EVENT                   1104
/** Indicating that the VM should be suspended for debugging because
 * the developer wants to inspect the VM state. */
#define VINF_EM_DBG_STOP                    1105
/** Indicating success single stepping and that EM should report that
 * event to the debugger. */
#define VINF_EM_DBG_STEPPED                 1106
/** Indicating that a breakpoint was hit and that EM should notify the debugger
 * and in the event there is no debugger fail fatally. */
#define VINF_EM_DBG_BREAKPOINT              1107
/** Indicating that EM should single step an instruction.
 * The instruction is stepped in the current execution mode (RAW/REM). */
#define VINF_EM_DBG_STEP                    1108
/** Indicating that the VM is being turned off and that the EM should
 * exit to the VM awaiting the destruction request. */
#define VINF_EM_OFF                         1109
/** Indicating that the VM has been suspended and that the thread
 * should wait for request telling it what to do next. */
#define VINF_EM_SUSPEND                     1110
/** Indicating that the VM has been reset and that scheduling goes
 * back to startup defaults. */
#define VINF_EM_RESET                       1111
/** Indicating that the VM has executed a halt instruction and that
 * the emulation thread should wait for an interrupt before resuming
 * execution. */
#define VINF_EM_HALT                        1112
/** Indicating that the VM has been resumed and that the thread should
 * start executing. */
#define VINF_EM_RESUME                      1113
/** Indicating that we've got an out-of-memory condition and that we need
 * to take the appropriate actions to deal with this.
 * @remarks It might seem odd at first that this has lower priority than VINF_EM_HALT,
 *          VINF_EM_SUSPEND, and VINF_EM_RESUME. The reason is that these events are
 *          vital to correctly operating the VM. Also, they can't normally occur together
 *          with an out-of-memory condition, and even if that should happen the condition
 *          will be rediscovered before executing any more code. */
#define VINF_EM_NO_MEMORY                   1114
/** The fatal variant of VINF_EM_NO_MEMORY. */
#define VERR_EM_NO_MEMORY                   (-1114)
/** Indicating that a rescheduling to recompiled execution.
 * Typically caused by raw-mode executing code which is difficult/slow
 * to virtualize rawly.
 * @remarks Important to have a higher priority (lower number) than the other rescheduling status codes. */
#define VINF_EM_RESCHEDULE_REM              1115
/** Indicating that a rescheduling to vmx-mode execution (HM/NEM).
 * Typically caused by REM detecting that hardware-accelerated raw-mode execution is possible. */
#define VINF_EM_RESCHEDULE_HM               1116
/** Indicating that a rescheduling to raw-mode execution.
 * Typically caused by REM detecting that raw-mode execution is possible.
 * @remarks Important to have a higher priority (lower number) than VINF_EM_RESCHEDULE. */
#define VINF_EM_RESCHEDULE_RAW              1117
/** Indicating that a rescheduling now is required. Typically caused by
 * interrupts having changed the EIP. */
#define VINF_EM_RESCHEDULE                  1118
/** PARAV call */
#define VINF_EM_RESCHEDULE_PARAV            1119
/** Go back into wait for SIPI mode */
#define VINF_EM_WAIT_SIPI                   1120
/** Last scheduling related status code. (inclusive) */
#define VINF_EM_LAST                        1120

/** Reason for leaving RC: Guest trap which couldn't be handled in RC.
 * The trap is generally forwarded to the REM and executed there. */
#define VINF_EM_RAW_GUEST_TRAP              1121
/** Reason for leaving RC: Interrupted by external interrupt.
 * The interrupt needed to be handled by the host OS. */
#define VINF_EM_RAW_INTERRUPT               1122
/** Reason for leaving RC: Interrupted by external interrupt while in hypervisor
 * code. The interrupt needed to be handled by the host OS and hypervisor
 * execution must be resumed. VM state is not complete at this point. */
#define VINF_EM_RAW_INTERRUPT_HYPER         1123
/** Reason for leaving RC: A Ring switch was attempted.
 * Normal cause of action is to execute this in REM. */
#define VINF_EM_RAW_RING_SWITCH             1124
/** Reason for leaving RC: A Ring switch was attempted using software interrupt.
 * Normal cause of action is to execute this in REM. */
#define VINF_EM_RAW_RING_SWITCH_INT         1125
/** Reason for leaving RC: A privileged instruction was attempted executed.
 * Normal cause of action is to execute this in REM. */
#define VINF_EM_RAW_EXCEPTION_PRIVILEGED    1126

/** Reason for leaving RZ: Emulate instruction. */
#define VINF_EM_RAW_EMULATE_INSTR           1127
/** Reason for leaving RC: Unhandled TSS write.
 * Recompiler gets control. */
#define VINF_EM_RAW_EMULATE_INSTR_TSS_FAULT 1128
/** Reason for leaving RC: Unhandled LDT write.
 * Recompiler gets control. */
#define VINF_EM_RAW_EMULATE_INSTR_LDT_FAULT 1129
/** Reason for leaving RC: Unhandled IDT write.
 * Recompiler gets control. */
#define VINF_EM_RAW_EMULATE_INSTR_IDT_FAULT 1130
/** Reason for leaving RC: Partly handled GDT write.
 * Recompiler gets control. */
#define VINF_EM_RAW_EMULATE_INSTR_GDT_FAULT 1131
/** Reason for leaving RC: jump inside generated patch jump.
 * Fatal error. */
#define VERR_EM_RAW_PATCH_CONFLICT          (-1133)
/** Reason for leaving RZ: Ring-3 operation pending. */
#define VINF_EM_RAW_TO_R3                   1135
/** Reason for leaving RZ: Timer pending. */
#define VINF_EM_RAW_TIMER_PENDING           1136
/** Reason for leaving RC: Interrupt pending (guest). */
#define VINF_EM_RAW_INTERRUPT_PENDING       1137
/** Reason for leaving RC: Encountered a stale selector. */
#define VINF_EM_RAW_STALE_SELECTOR          1138
/** Reason for leaving RC: The IRET resuming guest code trapped. */
#define VINF_EM_RAW_IRET_TRAP               1139
/** The interpreter was unable to deal with the instruction at hand. */
#define VERR_EM_INTERPRETER                 (-1148)
/** Internal EM error caused by an unknown warning or informational status code. */
#define VERR_EM_INTERNAL_ERROR              (-1149)
/** Pending VM request packet. */
#define VINF_EM_PENDING_REQUEST             1150
/** Start instruction stepping (debug only). */
#define VINF_EM_RAW_EMULATE_DBG_STEP        1151
/** Patch TPR access instruction. */
#define VINF_EM_HM_PATCH_TPR_INSTR          1152
/** Unexpected guest mapping conflict detected. */
#define VERR_EM_UNEXPECTED_MAPPING_CONFLICT (-1154)
/** Reason for leaving RC: A triple-fault condition. Currently, causes
 *  a guru meditation. */
#define VINF_EM_TRIPLE_FAULT                1155
/** The specified execution engine cannot execute guest code in the current
 *  state. */
#define VERR_EM_CANNOT_EXEC_GUEST           (-1156)
/** Reason for leaving RC: Inject a TRPM event. */
#define VINF_EM_RAW_INJECT_TRPM_EVENT       1157
/** Guest tried to trigger a CPU hang.  The guest is probably up to no good. */
#define VERR_EM_GUEST_CPU_HANG              (-1158)
/** Reason for leaving RZ: Pending ring-3 IN instruction. */
#define VINF_EM_PENDING_R3_IOPORT_READ       1159
/** Reason for leaving RZ: Pending ring-3 OUT instruction. */
#define VINF_EM_PENDING_R3_IOPORT_WRITE      1160
/** Trick for resuming EMHistoryExec after a VMCPU_FF_IOM is handled. */
#define VINF_EM_RESUME_R3_HISTORY_EXEC       1161
/** @} */


/** @name Debugging Facility (DBGF) DBGF Status Codes
 * @{
 */
/** The function called requires the caller to be attached as a
 * debugger to the VM. */
#define VERR_DBGF_NOT_ATTACHED              (-1200)
/** Someone (including the caller) was already attached as
 * debugger to the VM. */
#define VERR_DBGF_ALREADY_ATTACHED          (-1201)
/** Tried to halt a debugger which was already halted.
 * (This is a warning and not an error.) */
#define VWRN_DBGF_ALREADY_HALTED            1202
/** The DBGF has no more free breakpoint slots. */
#define VERR_DBGF_NO_MORE_BP_SLOTS          (-1203)
/** The DBGF couldn't find the specified breakpoint. */
#define VERR_DBGF_BP_NOT_FOUND              (-1204)
/** Attempted to enabled a breakpoint which was already enabled. */
#define VINF_DBGF_BP_ALREADY_ENABLED        1205
/** Attempted to disabled a breakpoint which was already disabled. */
#define VINF_DBGF_BP_ALREADY_DISABLED       1206
/** The breakpoint already exists. */
#define VINF_DBGF_BP_ALREADY_EXIST          1207
/** The byte string was not found. */
#define VERR_DBGF_MEM_NOT_FOUND             (-1208)
/** The OS was not detected. */
#define VERR_DBGF_OS_NOT_DETCTED            (-1209)
/** The OS was not detected. */
#define VINF_DBGF_OS_NOT_DETCTED            1209
/** The specified register was not found. */
#define VERR_DBGF_REGISTER_NOT_FOUND        (-1210)
/** The value was truncated to fit.
 * For queries this means that the register is wider than the queried value.
 * For setters this means that the value is wider than the register. */
#define VINF_DBGF_TRUNCATED_REGISTER        1211
/** The value was zero extended to fit.
 * For queries this means that the register is narrower than the queried value.
 * For setters this means that the value is narrower than the register. */
#define VINF_DBGF_ZERO_EXTENDED_REGISTER    1212
/** The requested type conversion was not supported. */
#define VERR_DBGF_UNSUPPORTED_CAST          (-1213)
/** The register is read-only and cannot be modified. */
#define VERR_DBGF_READ_ONLY_REGISTER        (-1214)
/** Internal processing error \#1 in the DBGF register code. */
#define VERR_DBGF_REG_IPE_1                 (-1215)
/** Internal processing error \#2 in the DBGF register code. */
#define VERR_DBGF_REG_IPE_2                 (-1216)
/** Unhandled \#DB in hypervisor code. */
#define VERR_DBGF_HYPER_DB_XCPT             (-1217)
/** Internal processing error \#1 in the DBGF stack code. */
#define VERR_DBGF_STACK_IPE_1               (-1218)
/** Internal processing error \#2 in the DBGF stack code. */
#define VERR_DBGF_STACK_IPE_2               (-1219)
/** No trace buffer available, please change the VM config. */
#define VERR_DBGF_NO_TRACE_BUFFER           (-1220)
/** @} */


/** @name Patch Manager (PATM) Status Codes
 * @{
 */
/** Non fatal Patch Manager analysis phase warning */
#define VWRN_CONTINUE_ANALYSIS              1400
/** Non fatal Patch Manager recompile phase warning (mapped to VWRN_CONTINUE_ANALYSIS). */
#define VWRN_CONTINUE_RECOMPILE             VWRN_CONTINUE_ANALYSIS
/** Continue search (mapped to VWRN_CONTINUE_ANALYSIS). */
#define VWRN_PATM_CONTINUE_SEARCH           VWRN_CONTINUE_ANALYSIS
/** Patch installation refused (patch too complex or unsupported instructions ) */
#define VERR_PATCHING_REFUSED               (-1401)
/** Unable to find patch */
#define VERR_PATCH_NOT_FOUND                (-1402)
/** Patch disabled */
#define VERR_PATCH_DISABLED                 (-1403)
/** Patch enabled */
#define VWRN_PATCH_ENABLED                  1404
/** Patch was already disabled */
#define VERR_PATCH_ALREADY_DISABLED         (-1405)
/** Patch was already enabled */
#define VERR_PATCH_ALREADY_ENABLED          (-1406)
/** Patch was removed. */
#define VWRN_PATCH_REMOVED                  1407

/** Reason for leaving RC: \#GP with EIP pointing to patch code. */
#define VINF_PATM_PATCH_TRAP_GP             1408
/** First leave RC code. */
#define VINF_PATM_LEAVE_RC_FIRST             VINF_PATM_PATCH_TRAP_GP
/** Reason for leaving RC: \#PF with EIP pointing to patch code. */
#define VINF_PATM_PATCH_TRAP_PF             1409
/** Reason for leaving RC: int3 with EIP pointing to patch code. */
#define VINF_PATM_PATCH_INT3                1410
/** Reason for leaving RC: \#PF for monitored patch page. */
#define VINF_PATM_CHECK_PATCH_PAGE          1411
/** Reason for leaving RC: duplicate instruction called at current eip. */
#define VINF_PATM_DUPLICATE_FUNCTION        1412
/** Execute one instruction with the recompiler */
#define VINF_PATCH_EMULATE_INSTR            1413
/** Reason for leaving RC: attempt to patch MMIO write. */
#define VINF_PATM_HC_MMIO_PATCH_WRITE       1414
/** Reason for leaving RC: attempt to patch MMIO read. */
#define VINF_PATM_HC_MMIO_PATCH_READ        1415
/** Reason for leaving RC: pending irq after iret that sets IF. */
#define VINF_PATM_PENDING_IRQ_AFTER_IRET    1416
/** Last leave RC code. */
#define VINF_PATM_LEAVE_RC_LAST              VINF_PATM_PENDING_IRQ_AFTER_IRET

/** No conflicts to resolve */
#define VERR_PATCH_NO_CONFLICT              (-1425)
/** Detected unsafe code for patching */
#define VERR_PATM_UNSAFE_CODE               (-1426)
/** Terminate search branch */
#define VWRN_PATCH_END_BRANCH                1427
/** Already patched */
#define VERR_PATM_ALREADY_PATCHED           (-1428)
/** Spinlock detection failed. */
#define VINF_PATM_SPINLOCK_FAILED           (1429)
/** Continue execution after patch trap. */
#define VINF_PATCH_CONTINUE                 (1430)
/** The patch manager is not used because we're using HM and VT-x/AMD-V. */
#define VERR_PATM_HM_IPE                    (-1431)
/** Unexpected trap in patch code. */
#define VERR_PATM_IPE_TRAP_IN_PATCH_CODE    (-1432)

/** @} */


/** @name Code Scanning and Analysis Manager (CSAM) Status Codes
 * @{
 */
/** Trap not handled */
#define VWRN_CSAM_TRAP_NOT_HANDLED          1500
/** Patch installed */
#define VWRN_CSAM_INSTRUCTION_PATCHED       1501
/** Page record not found */
#define VWRN_CSAM_PAGE_NOT_FOUND            1502
/** Reason for leaving RC: CSAM wants perform a task in ring-3. */
#define VINF_CSAM_PENDING_ACTION            1503
/** The CSAM is not used because we're using HM and VT-x/AMD-V. */
#define VERR_CSAM_HM_IPE                    (-1504)
/** @} */


/** @name Page Monitor/Manager (PGM) Status Codes
 * @{
 */
/** Attempt to create a GC mapping which conflicts with an existing mapping. */
#define VERR_PGM_MAPPING_CONFLICT           (-1600)
/** The physical handler range has no corresponding RAM range.
 * If this is MMIO, see todo above the return. If not MMIO, then it's
 * someone else's fault... */
#define VERR_PGM_HANDLER_PHYSICAL_NO_RAM_RANGE (-1601)
/** Attempt to register an access handler for a virtual range of which a part
 * was already handled. */
#define VERR_PGM_HANDLER_VIRTUAL_CONFLICT   (-1602)
/** Attempt to register an access handler for a physical range of which a part
 * was already handled. */
#define VERR_PGM_HANDLER_PHYSICAL_CONFLICT  (-1603)
/** Invalid page directory specified to PGM. */
#define VERR_PGM_INVALID_PAGE_DIRECTORY     (-1604)
/** Invalid GC physical address. */
#define VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS (-1605)
/** Invalid GC physical range. Usually used when a specified range crosses
 * a RAM region boundary. */
#define VERR_PGM_INVALID_GC_PHYSICAL_RANGE  (-1606)
/** Specified access handler was not found. */
#define VERR_PGM_HANDLER_NOT_FOUND          (-1607)
/** Attempt to register a RAM range of which parts are already
 * covered by existing RAM ranges. */
#define VERR_PGM_RAM_CONFLICT               (-1608)
/** Failed to add new mappings because the current mappings are fixed
 * in guest os memory. */
#define VERR_PGM_MAPPINGS_FIXED             (-1609)
/** Failed to fix mappings because of a conflict with the intermediate code. */
#define VERR_PGM_MAPPINGS_FIX_CONFLICT      (-1610)
/** Failed to fix mappings because a mapping rejected the address. */
#define VERR_PGM_MAPPINGS_FIX_REJECTED      (-1611)
/** Failed to fix mappings because the proposed memory area was to small. */
#define VERR_PGM_MAPPINGS_FIX_TOO_SMALL     (-1612)
/** Reason for leaving RZ: The urge to syncing CR3. */
#define VINF_PGM_SYNC_CR3                   1613
/** Page not marked for dirty bit tracking */
#define VINF_PGM_NO_DIRTY_BIT_TRACKING      1614
/** Page fault caused by dirty bit tracking; corrected */
#define VINF_PGM_HANDLED_DIRTY_BIT_FAULT    1615
/** Go ahead with the default Read/Write operation.
 * This is returned by a R3 physical or virtual handler when it wants the
 * PGMPhys[Read|Write] routine do the reading/writing. */
#define VINF_PGM_HANDLER_DO_DEFAULT         1616
/** The paging mode of the host is not supported yet. */
#define VERR_PGM_UNSUPPORTED_HOST_PAGING_MODE (-1617)
/** The physical guest page is a reserved/MMIO page and does not have any HC
 *  address. */
#define VERR_PGM_PHYS_PAGE_RESERVED         (-1618)
/** No page directory available for the hypervisor. */
#define VERR_PGM_NO_HYPERVISOR_ADDRESS      (-1619)


/** The returned shadow page is cached. */
#define VINF_PGM_CACHED_PAGE                1622
/** Returned by handler registration, modification and deregistration
 * when the shadow PTs could be updated because the guest page
 * aliased or/and mapped by multiple PTs. */
#define VINF_PGM_GCPHYS_ALIASED             1623
/** Reason for leaving RC: Paging mode changed.
 * PGMChangeMode() uses this to force a switch to R3 so it can safely deal with
 * a mode switch. */
#define VINF_PGM_CHANGE_MODE                1624
/** SyncPage modified the PDE.
 * This is an internal status code used to communicate back to the \#PF handler
 * that the PDE was (probably) marked not-present and it should restart the instruction. */
#define VINF_PGM_SYNCPAGE_MODIFIED_PDE      1625
/** Physical range crosses dynamic ram chunk boundary; translation to HC ptr not safe. */
#define VERR_PGM_GCPHYS_RANGE_CROSSES_BOUNDARY  (-1626)
/** Conflict between the core memory and the intermediate paging context, try again.
 * There are some very special conditions applying to the intermediate paging context
 * (used during the world switches), and some times we continuously run into these
 * when asking the host kernel for memory during VM init. Let us know if you run into
 * this and we'll adjust the code so it tries harder to avoid it.
 */
#define VERR_PGM_INTERMEDIATE_PAGING_CONFLICT   (-1627)
/** The shadow paging mode is not supported yet. */
#define VERR_PGM_UNSUPPORTED_SHADOW_PAGING_MODE (-1628)
/** The dynamic mapping cache for physical memory failed. */
#define VERR_PGM_DYNMAP_FAILED                  (-1629)
/** The auto usage cache for the dynamic mapping set is full. */
#define VERR_PGM_DYNMAP_FULL_SET                (-1630)
/** The initialization of the dynamic mapping cache failed. */
#define VERR_PGM_DYNMAP_SETUP_ERROR             (-1631)
/** The expanding of the dynamic mapping cache failed. */
#define VERR_PGM_DYNMAP_EXPAND_ERROR            (-1632)
/** The page is unassigned (akin to VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS). */
#define VERR_PGM_PHYS_TLB_UNASSIGNED            (-1633)
/** Catch any access and route it thru PGM. */
#define VERR_PGM_PHYS_TLB_CATCH_ALL             (-1634)
/** Catch write access and route it thru PGM. */
#define VINF_PGM_PHYS_TLB_CATCH_WRITE           1635
/** Catch write access and route it thru PGM. */
#define VERR_PGM_PHYS_TLB_CATCH_WRITE           (-1635)
/** No CR3 root shadow page table. */
#define VERR_PGM_NO_CR3_SHADOW_ROOT             (-1636)
/** Trying to free a page with an invalid Page ID. */
#define VERR_PGM_PHYS_INVALID_PAGE_ID           (-1637)
/** PGMPhysWrite/Read hit a handler in Ring-0 or raw-mode context. */
#define VERR_PGM_PHYS_WR_HIT_HANDLER            (-1638)
/** Trying to free a page that isn't RAM. */
#define VERR_PGM_PHYS_NOT_RAM                   (-1639)
/** Not ROM page. */
#define VERR_PGM_PHYS_NOT_ROM                   (-1640)
/** Not MMIO page. */
#define VERR_PGM_PHYS_NOT_MMIO                  (-1641)
/** Not MMIO2 page. */
#define VERR_PGM_PHYS_NOT_MMIO2                 (-1642)
/** Already aliased to a different page. */
#define VERR_PGM_HANDLER_ALREADY_ALIASED        (-1643)
/** Already aliased to the same page. */
#define VINF_PGM_HANDLER_ALREADY_ALIASED        (1643)
/** PGM pool flush pending - return to ring 3. */
#define VINF_PGM_POOL_FLUSH_PENDING             (1644)
/** Unable to use the range for a large page. */
#define VERR_PGM_INVALID_LARGE_PAGE_RANGE       (-1645)
/** Don't mess around with ballooned pages. */
#define VERR_PGM_PHYS_PAGE_BALLOONED            (-1646)
/** Internal processing error \#1 in page access handler code. */
#define VERR_PGM_HANDLER_IPE_1                  (-1647)


/** pgmPhysPageMapCommon encountered PGMPAGETYPE_MMIO2_ALIAS_MMIO. */
#define VERR_PGM_MAP_MMIO2_ALIAS_MMIO           (-1651)
/** Guest mappings are disabled. */
#define VERR_PGM_MAPPINGS_DISABLED              (-1652)
/** No guest mappings when SMP is enabled. */
#define VERR_PGM_MAPPINGS_SMP                   (-1653)
/** Invalid saved page state. */
#define VERR_PGM_INVALID_SAVED_PAGE_STATE       (-1654)
/** Encountered an unexpected page type in the saved state. */
#define VERR_PGM_LOAD_UNEXPECTED_PAGE_TYPE      (-1655)
/** Encountered an unexpected page state in the saved state. */
#define VERR_PGM_UNEXPECTED_PAGE_STATE          (-1656)
/** Couldn't find MMIO2 range from saved state. */
#define VERR_PGM_SAVED_MMIO2_RANGE_NOT_FOUND    (-1657)
/** Couldn't find MMIO2 page from saved state. */
#define VERR_PGM_SAVED_MMIO2_PAGE_NOT_FOUND     (-1658)
/** Couldn't find ROM range from saved state. */
#define VERR_PGM_SAVED_ROM_RANGE_NOT_FOUND      (-1659)
/** Couldn't find ROM page from saved state. */
#define VERR_PGM_SAVED_ROM_PAGE_NOT_FOUND       (-1660)
/** ROM page mismatch between saved state and the VM. */
#define VERR_PGM_SAVED_ROM_PAGE_PROT            (-1661)
/** Unknown saved state record. */
#define VERR_PGM_SAVED_REC_TYPE                 (-1662)
/** Internal processing error in the PGM dynmap (r0/rc). */
#define VERR_PGM_DYNMAP_IPE                     (-1663)
/** Internal processing error in the PGM handy page allocator. */
#define VERR_PGM_HANDY_PAGE_IPE                 (-1664)
/** Failed to map the guest PML4. */
#define VERR_PGM_PML4_MAPPING                   (-1665)
/** Failed to obtain a pool page.  */
#define VERR_PGM_POOL_GET_PAGE_FAILED           (-1666)
/** A PGM function was called in a mode where it isn't supposed to be used. */
#define VERR_PGM_NOT_USED_IN_MODE               (-1667)
/** The CR3 address specified memory we don't know about. */
#define VERR_PGM_INVALID_CR3_ADDR               (-1668)
/** One or the PDPEs specified memory we don't know about. */
#define VERR_PGM_INVALID_PDPE_ADDR              (-1669)
/** Internal processing error in the PGM physical handler code. */
#define VERR_PGM_PHYS_HANDLER_IPE               (-1670)
/** Internal processing error \#1 in the PGM physial page mapping code. */
#define VERR_PGM_PHYS_PAGE_MAP_IPE_1            (-1671)
/** Internal processing error \#2 in the PGM physial page mapping code. */
#define VERR_PGM_PHYS_PAGE_MAP_IPE_2            (-1672)
/** Internal processing error \#3 in the PGM physial page mapping code. */
#define VERR_PGM_PHYS_PAGE_MAP_IPE_3            (-1673)
/** Internal processing error \#4 in the PGM physial page mapping code. */
#define VERR_PGM_PHYS_PAGE_MAP_IPE_4            (-1674)
/** Too many loops looking for a page to reuse. */
#define VERR_PGM_POOL_TOO_MANY_LOOPS            (-1675)
/** Internal processing error related to guest mappings. */
#define VERR_PGM_MAPPING_IPE                    (-1676)
/** An attempt was made to grow an already maxed out page pool. */
#define VERR_PGM_POOL_MAXED_OUT_ALREADY         (-1677)
/** Internal processing error in the page pool code. */
#define VERR_PGM_POOL_IPE                       (-1678)
/** The write monitor is already engaged. */
#define VERR_PGM_WRITE_MONITOR_ENGAGED          (-1679)
/** Failed to get a guest page which is expected to be present.  */
#define VERR_PGM_PHYS_PAGE_GET_IPE              (-1680)
/** We were given a NULL pPage parameter. */
#define VERR_PGM_PHYS_NULL_PAGE_PARAM           (-1681)
/** PCI passthru is not supported by this build. */
#define VERR_PGM_PCI_PASSTHRU_MISCONFIG         (-1682)
/** Too many MMIO2 ranges. */
#define VERR_PGM_TOO_MANY_MMIO2_RANGES          (-1683)
/** Internal processing error in the PGM physical page mapping code dealing
 * with MMIO2 pages. */
#define VERR_PGM_PHYS_PAGE_MAP_MMIO2_IPE        (-1684)
/** Internal processing error in the PGM physcal page handling code related to
 *  MMIO/MMIO2. */
#define VERR_PGM_PHYS_MMIO_EX_IPE               (-1685)
/** Mode table internal error. */
#define VERR_PGM_MODE_IPE                       (-1686)
/** Shadow mode 'none' internal error. */
#define VERR_PGM_SHW_NONE_IPE                   (-1687)
/** @} */


/** @name Memory Monitor (MM) Status Codes
 * @{
 */
/** Attempt to register a RAM range of which parts are already
 * covered by existing RAM ranges. */
#define VERR_MM_RAM_CONFLICT                    (-1700)
/** Hypervisor memory allocation failed. */
#define VERR_MM_HYPER_NO_MEMORY                 (-1701)
/** A bad trap type ended up in mmGCRamTrap0eHandler. */
#define VERR_MM_BAD_TRAP_TYPE_IPE               (-1702)
/** @} */


/** @name CPU Monitor (CPUM) Status Codes
 * @{
 */
/** The caller shall raise an \#GP(0) exception. */
#define VERR_CPUM_RAISE_GP_0                    (-1750)
/** Incompatible CPUM configuration. */
#define VERR_CPUM_INCOMPATIBLE_CONFIG           (-1751)
/** CPUMR3DisasmInstrCPU unexpectedly failed to determine the hidden
 * parts of the CS register. */
#define VERR_CPUM_HIDDEN_CS_LOAD_ERROR          (-1752)
/** Couldn't find the end of CPUID sub-leaves. */
#define VERR_CPUM_TOO_MANY_CPUID_SUBLEAVES      (-1753)
/** CPUM internal processing error \#1. */
#define VERR_CPUM_IPE_1                         (-1754)
/** CPUM internal processing error \#2. */
#define VERR_CPUM_IPE_2                         (-1755)
/** The specified CPU cannot be found in the CPU database. */
#define VERR_CPUM_DB_CPU_NOT_FOUND              (-1756)
/** Invalid CPUMCPU offset in MSR range. */
#define VERR_CPUM_MSR_BAD_CPUMCPU_OFFSET        (-1757)
/** Return to ring-3 to read the MSR there. */
#define VINF_CPUM_R3_MSR_READ                   (1758)
/** Return to ring-3 to write the MSR there. */
#define VINF_CPUM_R3_MSR_WRITE                  (1759)
/** Too many CPUID leaves. */
#define VERR_TOO_MANY_CPUID_LEAVES              (-1760)
/** Invalid config value. */
#define VERR_CPUM_INVALID_CONFIG_VALUE          (-1761)
/** The loaded XSAVE component mask is not compatible with the host CPU
 * or/and VM config. */
#define VERR_CPUM_INCOMPATIBLE_XSAVE_COMP_MASK  (-1762)
/** The loaded XSAVE component mask is not valid. */
#define VERR_CPUM_INVALID_XSAVE_COMP_MASK       (-1763)
/** The loaded XSAVE header is not valid. */
#define VERR_CPUM_INVALID_XSAVE_HDR             (-1764)
/** The loaded XCR0 register value is not valid. */
#define VERR_CPUM_INVALID_XCR0                  (-1765)
/** Indicates that we modified the host CR0 (FPU related). */
#define VINF_CPUM_HOST_CR0_MODIFIED             (1766)
/** Invalid/unsupported nested hardware virtualization configuration. */
#define VERR_CPUM_INVALID_HWVIRT_CONFIG         (-1767)
/** Invalid nested hardware virtualization feature combination. */
#define VERR_CPUM_INVALID_HWVIRT_FEAT_COMBO     (-1768)
/** @} */


/** @name Save State Manager (SSM) Status Codes
 * @{
 */
/** The specified data unit already exist. */
#define VERR_SSM_UNIT_EXISTS                    (-1800)
/** The specified data unit wasn't found. */
#define VERR_SSM_UNIT_NOT_FOUND                 (-1801)
/** The specified data unit wasn't owned by caller. */
#define VERR_SSM_UNIT_NOT_OWNER                 (-1802)

/** General saved state file integrity error. */
#define VERR_SSM_INTEGRITY                      (-1810)
/** The saved state file magic was not recognized. */
#define VERR_SSM_INTEGRITY_MAGIC                (-1811)
/** The saved state file version is not supported. */
#define VERR_SSM_INTEGRITY_VERSION              (-1812)
/** The saved state file size didn't match the one in the header. */
#define VERR_SSM_INTEGRITY_SIZE                 (-1813)
/** The CRC of the saved state file did not match. */
#define VERR_SSM_INTEGRITY_CRC                  (-1814)
/** The machine uuid field wasn't null. */
#define VERR_SMM_INTEGRITY_MACHINE              (-1815)
/** Saved state header integrity error. */
#define VERR_SSM_INTEGRITY_HEADER               (-1816)
/** Unit header integrity error. */
#define VERR_SSM_INTEGRITY_UNIT                 (-1817)
/** Invalid unit magic (internal data tag). */
#define VERR_SSM_INTEGRITY_UNIT_MAGIC           (-1818)
/** The file contained a data unit which no-one wants. */
#define VERR_SSM_INTEGRITY_UNIT_NOT_FOUND       (-1819)
/** Incorrect version numbers in the header. */
#define VERR_SSM_INTEGRITY_VBOX_VERSION         (-1820)
/** Footer integrity error. */
#define VERR_SSM_INTEGRITY_FOOTER               (-1821)
/** Record header integrity error. */
#define VERR_SSM_INTEGRITY_REC_HDR              (-1822)
/** Termination record integrity error. */
#define VERR_SSM_INTEGRITY_REC_TERM             (-1823)
/** Termination record CRC mismatch. */
#define VERR_SSM_INTEGRITY_REC_TERM_CRC         (-1824)
/** Decompression integrity error.  */
#define VERR_SSM_INTEGRITY_DECOMPRESSION        (-1825)
/** Saved state directory wintertides error.  */
#define VERR_SSM_INTEGRITY_DIR                  (-1826)
/** The saved state directory magic is wrong. */
#define VERR_SSM_INTEGRITY_DIR_MAGIC            (-1827)

/** A data unit in the saved state file was defined but didn't any
 * routine for processing it. */
#define VERR_SSM_NO_LOAD_EXEC                   (-1830)
/** A restore routine attempted to load more data then the unit contained. */
#define VERR_SSM_LOADED_TOO_MUCH                (-1831)
/** Not in the correct state for the attempted operation. */
#define VERR_SSM_INVALID_STATE                  (-1832)
/** Not in the correct state for the attempted operation. */
#define VERR_SSM_LOADED_TOO_LITTLE              (-1833)

/** Unsupported data unit version.
 * A SSM user returns this if it doesn't know the u32Version. */
#define VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION  (-1840)
/** The format of a data unit has changed.
 * A SSM user returns this if it's not able to read the format for
 * other reasons than u32Version. */
#define VERR_SSM_DATA_UNIT_FORMAT_CHANGED       (-1841)
/** The CPUID instruction returns different information when loading than when saved.
 * Normally caused by hardware changes on the host, but could also be caused by
 * changes in the BIOS setup. */
#define VERR_SSM_LOAD_CPUID_MISMATCH            (-1842)
/** The RAM size differs between the saved state and the VM config. */
#define VERR_SSM_LOAD_MEMORY_SIZE_MISMATCH      (-1843)
/** The state doesn't match the VM configuration in one or another way.
 * (There are certain PCI reconfiguration which the OS could potentially
 * do which can cause this problem. Check this out when it happens.) */
#define VERR_SSM_LOAD_CONFIG_MISMATCH           (-1844)
/** The virtual clock frequency differs too much.
 * The clock source for the virtual time isn't reliable or the code have changed. */
#define VERR_SSM_VIRTUAL_CLOCK_HZ               (-1845)
/** A timeout occurred while waiting for async IDE operations to finish. */
#define VERR_SSM_IDE_ASYNC_TIMEOUT              (-1846)
/** One of the structure magics was wrong. */
#define VERR_SSM_STRUCTURE_MAGIC                (-1847)
/** The data in the saved state doesn't conform to expectations. */
#define VERR_SSM_UNEXPECTED_DATA                (-1848)
/** Trying to read a 64-bit guest physical address into a 32-bit variable. */
#define VERR_SSM_GCPHYS_OVERFLOW                (-1849)
/** Trying to read a 64-bit guest virtual address into a 32-bit variable. */
#define VERR_SSM_GCPTR_OVERFLOW                 (-1850)
/** Vote for another pass.  */
#define VINF_SSM_VOTE_FOR_ANOTHER_PASS          1851
/** Vote for done tell SSM not to call again until the final pass. */
#define VINF_SSM_VOTE_DONE_DONT_CALL_AGAIN      1852
/** Vote for giving up.  */
#define VERR_SSM_VOTE_FOR_GIVING_UP             (-1853)
/** Don't call again until the final pass. */
#define VINF_SSM_DONT_CALL_AGAIN                1854
/** Giving up a live snapshot/teleportation attempt because of too many
 * passes. */
#define VERR_SSM_TOO_MANY_PASSES                (-1855)
/** Giving up a live snapshot/teleportation attempt because the state grew to
 * big. */
#define VERR_SSM_STATE_GREW_TOO_BIG             (-1856)
/** Giving up a live snapshot attempt because we're low on disk space.  */
#define VERR_SSM_LOW_ON_DISK_SPACE              (-1857)
/** The operation was cancelled. */
#define VERR_SSM_CANCELLED                      (-1858)
/** Nothing that can be cancelled.  */
#define VERR_SSM_NO_PENDING_OPERATION           (-1859)
/** The operation has already been cancelled. */
#define VERR_SSM_ALREADY_CANCELLED              (-1860)
/** The machine was powered off while saving. */
#define VERR_SSM_LIVE_POWERED_OFF               (-1861)
/** The live snapshot/teleportation operation was aborted because of a guru
 *  meditation. */
#define VERR_SSM_LIVE_GURU_MEDITATION           (-1862)
/** The live snapshot/teleportation operation was aborted because of a fatal
 *  runtime error. */
#define VERR_SSM_LIVE_FATAL_ERROR               (-1863)
/** The VM was suspended before or while saving, don't resume execution. */
#define VINF_SSM_LIVE_SUSPENDED                  1864
/** Complex SSM field fed to SSMR3PutStruct or SSMR3GetStruct.  Use the
 * extended API. */
#define VERR_SSM_FIELD_COMPLEX                  (-1864)
/** Invalid size of a SSM field with the specified transformation. */
#define VERR_SSM_FIELD_INVALID_SIZE             (-1865)
/** The specified field is outside the structure.  */
#define VERR_SSM_FIELD_OUT_OF_BOUNDS            (-1866)
/** The field does not follow immediately the previous one. */
#define VERR_SSM_FIELD_NOT_CONSECUTIVE          (-1867)
/** The field contains an invalid callback or transformation index. */
#define VERR_SSM_FIELD_INVALID_CALLBACK         (-1868)
/** The field contains an invalid padding size. */
#define VERR_SSM_FIELD_INVALID_PADDING_SIZE     (-1869)
/** The field contains a value that is out of range. */
#define VERR_SSM_FIELD_INVALID_VALUE            (-1870)
/** Generic stream error. */
#define VERR_SSM_STREAM_ERROR                   (-1871)
/** SSM did a callback for a pass we didn't expect. */
#define VERR_SSM_UNEXPECTED_PASS                (-1872)
/** Someone is trying to skip backwards in the stream... */
#define VERR_SSM_SKIP_BACKWARDS                 (-1873)
/** Someone is trying to write a memory block which is too big to encode. */
#define VERR_SSM_MEM_TOO_BIG                    (-1874)
/** Encountered an bad (/unknown) record type. */
#define VERR_SSM_BAD_REC_TYPE                   (-1875)
/** Internal processing error \#1 in SSM code.  */
#define VERR_SSM_IPE_1                          (-1876)
/** Internal processing error \#2 in SSM code.  */
#define VERR_SSM_IPE_2                          (-1877)
/** Internal processing error \#3 in SSM code.  */
#define VERR_SSM_IPE_3                          (-1878)
/** A field contained an transformation that should only be used when loading
 * old states. */
#define VERR_SSM_FIELD_LOAD_ONLY_TRANSFORMATION (-1879)
/** @} */


/** @name Virtual Machine (VM) Status Codes
 * @{
 */
/** The specified at reset handler wasn't found. */
#define VERR_VM_ATRESET_NOT_FOUND               (-1900)
/** Invalid VM request type.
 * For the VMR3ReqAlloc() case, the caller just specified an illegal enmType. For
 * all the other occurrences it means indicates corruption, broken logic, or stupid
 * interface user. */
#define VERR_VM_REQUEST_INVALID_TYPE            (-1901)
/** Invalid VM request state.
 * The state of the request packet was not the expected and accepted one(s). Either
 * the interface user screwed up, or we've got corruption/broken logic. */
#define VERR_VM_REQUEST_STATE                   (-1902)
/** Invalid VM request packet.
 * One or more of the VM controlled packet members didn't contain the correct
 * values. Some thing's broken. */
#define VERR_VM_REQUEST_INVALID_PACKAGE         (-1903)
/** The status field has not been updated yet as the request is still
 * pending completion. Someone queried the iStatus field before the request
 * has been fully processed. */
#define VERR_VM_REQUEST_STATUS_STILL_PENDING    (-1904)
/** The request has been freed, don't read the status now.
 * Someone is reading the iStatus field of a freed request packet. */
#define VERR_VM_REQUEST_STATUS_FREED            (-1905)
/** A VM api requiring EMT was called from another thread.
 * Use the VMR3ReqCall() apis to call it! */
#define VERR_VM_THREAD_NOT_EMT                  (-1906)
/** The VM state was invalid for the requested operation.
 * Go check the 'VM Statechart Diagram.gif'. */
#define VERR_VM_INVALID_VM_STATE                (-1907)
/** The support driver is not installed.
 * On linux, open returned ENOENT. */
#define VERR_VM_DRIVER_NOT_INSTALLED            (-1908)
/** The support driver is not accessible.
 * On linux, open returned EPERM. */
#define VERR_VM_DRIVER_NOT_ACCESSIBLE           (-1909)
/** Was not able to load the support driver.
 * On linux, open returned ENODEV. */
#define VERR_VM_DRIVER_LOAD_ERROR               (-1910)
/** Was not able to open the support driver.
 * Generic open error used when none of the other ones fit. */
#define VERR_VM_DRIVER_OPEN_ERROR               (-1911)
/** The installed support driver doesn't match the version of the user. */
#define VERR_VM_DRIVER_VERSION_MISMATCH         (-1912)
/** Saving the VM state is temporarily not allowed. Try again later. */
#define VERR_VM_SAVE_STATE_NOT_ALLOWED          (-1913)
/** An EMT called an API which cannot be called on such a thread. */
#define VERR_VM_THREAD_IS_EMT                   (-1914)
/** Encountered an unexpected VM state.  */
#define VERR_VM_UNEXPECTED_VM_STATE             (-1915)
/** Unexpected unstable VM state. */
#define VERR_VM_UNEXPECTED_UNSTABLE_STATE       (-1916)
/** Too many arguments passed to a VM request / request corruption.  */
#define VERR_VM_REQUEST_TOO_MANY_ARGS_IPE       (-1917)
/** Fatal EMT wait error. */
#define VERR_VM_FATAL_WAIT_ERROR                (-1918)
/** The VM request was killed at VM termination. */
#define VERR_VM_REQUEST_KILLED                  (-1919)
/** @} */


/** @name VBox Remote Desktop Protocol (VRDP) Status Codes
 * @{
 */
/** Successful completion of operation (mapped to generic iprt status code). */
#define VINF_VRDP_SUCCESS                   VINF_SUCCESS
/** VRDP transport operation timed out (mapped to generic iprt status code). */
#define VERR_VRDP_TIMEOUT                   VERR_TIMEOUT

/** Unsupported ISO protocol feature */
#define VERR_VRDP_ISO_UNSUPPORTED           (-2000)
/** Security (en/decryption) engine error */
#define VERR_VRDP_SEC_ENGINE_FAIL           (-2001)
/** VRDP protocol violation */
#define VERR_VRDP_PROTOCOL_ERROR            (-2002)
/** Unsupported VRDP protocol feature */
#define VERR_VRDP_NOT_SUPPORTED             (-2003)
/** VRDP protocol violation, client sends less data than expected */
#define VERR_VRDP_INSUFFICIENT_DATA         (-2004)
/** Internal error, VRDP packet is in wrong operation mode */
#define VERR_VRDP_INVALID_MODE              (-2005)
/** Memory allocation failed */
#define VERR_VRDP_NO_MEMORY                 (-2006)
/** Client has been rejected */
#define VERR_VRDP_ACCESS_DENIED             (-2007)
/** VRPD receives a packet that is not supported */
#define VWRN_VRDP_PDU_NOT_SUPPORTED         2008
/** VRDP script allowed the packet to be processed further */
#define VINF_VRDP_PROCESS_PDU               2009
/** VRDP script has completed its task */
#define VINF_VRDP_OPERATION_COMPLETED       2010
/** VRDP thread has started OK and will run */
#define VINF_VRDP_THREAD_STARTED            2011
/** Framebuffer is resized, terminate send bitmap procedure */
#define VINF_VRDP_RESIZE_REQUESTED          2012
/** Output can be enabled for the client. */
#define VINF_VRDP_OUTPUT_ENABLE             2013
/** @} */


/** @name Configuration Manager (CFGM) Status Codes
 * @{
 */
/** The integer value was too big for the requested representation. */
#define VERR_CFGM_INTEGER_TOO_BIG           (-2100)
/** Child node was not found. */
#define VERR_CFGM_CHILD_NOT_FOUND           (-2101)
/** Path to child node was invalid (i.e. empty). */
#define VERR_CFGM_INVALID_CHILD_PATH        (-2102)
/** Value not found. */
#define VERR_CFGM_VALUE_NOT_FOUND           (-2103)
/** No parent node specified. */
#define VERR_CFGM_NO_PARENT                 (-2104)
/** No node was specified. */
#define VERR_CFGM_NO_NODE                   (-2105)
/** The value is not an integer. */
#define VERR_CFGM_NOT_INTEGER               (-2106)
/** The value is not a zero terminated character string. */
#define VERR_CFGM_NOT_STRING                (-2107)
/** The value is not a byte string. */
#define VERR_CFGM_NOT_BYTES                 (-2108)
/** The specified string / bytes buffer was to small. Specify a larger one and retry. */
#define VERR_CFGM_NOT_ENOUGH_SPACE          (-2109)
/** The path of a new node contained slashes or was empty. */
#define VERR_CFGM_INVALID_NODE_PATH         (-2160)
/** A new node couldn't be inserted because one with the same name exists. */
#define VERR_CFGM_NODE_EXISTS               (-2161)
/** A new leaf couldn't be inserted because one with the same name exists. */
#define VERR_CFGM_LEAF_EXISTS               (-2162)
/** An unknown config value was encountered. */
#define VERR_CFGM_CONFIG_UNKNOWN_VALUE      (-2163)
/** An unknown config node (key) was encountered. */
#define VERR_CFGM_CONFIG_UNKNOWN_NODE       (-2164)
/** Internal processing error \#1 in CFGM. */
#define VERR_CFGM_IPE_1                     (-2165)
/** @} */


/** @name Time Manager (TM) Status Codes
 * @{
 */
/** The loaded timer state was incorrect. */
#define VERR_TM_LOAD_STATE                  (-2200)
/** The timer was not in the correct state for the request operation. */
#define VERR_TM_INVALID_STATE               (-2201)
/** The timer was in a unknown state. Corruption or stupid coding error. */
#define VERR_TM_UNKNOWN_STATE               (-2202)
/** The timer was stuck in an unstable state until we grew impatient and returned. */
#define VERR_TM_UNSTABLE_STATE              (-2203)
/** TM requires GIP. */
#define VERR_TM_GIP_REQUIRED                (-2204)
/** TM does not support the GIP version. */
#define VERR_TM_GIP_VERSION                 (-2205)
/** The GIP update interval is too large. */
#define VERR_TM_GIP_UPDATE_INTERVAL_TOO_BIG (-2206)
/** The timer has a bad clock enum value, probably corruption. */
#define VERR_TM_TIMER_BAD_CLOCK             (-2207)
/** The timer failed to reach a stable state. */
#define VERR_TM_TIMER_UNSTABLE_STATE        (-2208)
/** Attempt to resume a running TSC. */
#define VERR_TM_TSC_ALREADY_TICKING         (-2209)
/** Attempt to pause a paused TSC. */
#define VERR_TM_TSC_ALREADY_PAUSED          (-2210)
/** Invalid value for cVirtualTicking.  */
#define VERR_TM_VIRTUAL_TICKING_IPE         (-2211)
/** @} */


/** @name Recompiled Execution Manager (REM) Status Codes
 * @{
 */
/** Fatal error in virtual hardware. */
#define VERR_REM_VIRTUAL_HARDWARE_ERROR     (-2300)
/** Fatal error in the recompiler cpu. */
#define VERR_REM_VIRTUAL_CPU_ERROR          (-2301)
/** Recompiler execution was interrupted by forced action. */
#define VINF_REM_INTERRUPED_FF              2302
/** Too many similar traps. This is a very useful debug only
 * check (we don't do double/triple faults in REM). */
#define VERR_REM_TOO_MANY_TRAPS             (-2304)
/** The REM is out of breakpoint slots. */
#define VERR_REM_NO_MORE_BP_SLOTS           (-2305)
/** The REM could not find any breakpoint on the specified address. */
#define VERR_REM_BP_NOT_FOUND               (-2306)
/** @} */


/** @name Trap Manager / Monitor (TRPM) Status Codes
 * @{
 */
/** No active trap. Cannot query or reset a non-existing trap. */
#define VERR_TRPM_NO_ACTIVE_TRAP            (-2400)
/** Active trap. Cannot assert a new trap when one is already active. */
#define VERR_TRPM_ACTIVE_TRAP               (-2401)
/** Reason for leaving RC: Guest tried to write to our IDT - fatal.
 * The VM will be terminated assuming the worst, i.e. that the
 * guest has read the idtr register. */
#define VERR_TRPM_SHADOW_IDT_WRITE          (-2402)
/** Reason for leaving RC: Fatal trap in hypervisor. */
#define VERR_TRPM_DONT_PANIC                (-2403)
/** Reason for leaving RC: Double Fault. */
#define VERR_TRPM_PANIC                     (-2404)
/** The exception was dispatched for raw-mode execution. */
#define VINF_TRPM_XCPT_DISPATCHED           2405
/** Bad TRPM_TRAP_IN_OP. */
#define VERR_TRPM_BAD_TRAP_IN_OP            (-2406)
/** Internal processing error \#1 in TRPM. */
#define VERR_TRPM_IPE_1                     (-2407)
/** Internal processing error \#2 in TRPM. */
#define VERR_TRPM_IPE_2                     (-2408)
/** Internal processing error \#3 in TRPM. */
#define VERR_TRPM_IPE_3                     (-2409)
/** Got into a part of TRPM that is not used when HM (VT-x/AMD-V) is enabled. */
#define VERR_TRPM_HM_IPE                    (-2410)
/** @} */


/** @name Selector Manager / Monitor (SELM) Status Code
 * @{
 */
/** Reason for leaving RC: Guest tried to write to our GDT - fatal.
 * The VM will be terminated assuming the worst, i.e. that the
 * guest has read the gdtr register. */
#define VERR_SELM_SHADOW_GDT_WRITE          (-2500)
/** Reason for leaving RC: Guest tried to write to our LDT - fatal.
 * The VM will be terminated assuming the worst, i.e. that the
 * guest has read the ldtr register. */
#define VERR_SELM_SHADOW_LDT_WRITE          (-2501)
/** Reason for leaving RC: Guest tried to write to our TSS - fatal.
 * The VM will be terminated assuming the worst, i.e. that the
 * guest has read the ltr register. */
#define VERR_SELM_SHADOW_TSS_WRITE          (-2502)
/** Reason for leaving RC: Sync the GDT table to solve a conflict. */
#define VINF_SELM_SYNC_GDT                  2503
/** No valid TSS present. */
#define VERR_SELM_NO_TSS                    (-2504)
/** Invalid guest LDT selector. */
#define VERR_SELM_INVALID_LDT               (-2505)
/** The guest LDT selector is out of bounds. */
#define VERR_SELM_LDT_OUT_OF_BOUNDS         (-2506)
/** Unknown error while reading the guest GDT during shadow table updating. */
#define VERR_SELM_GDT_READ_ERROR            (-2507)
/** The guest GDT so full that we cannot find free space for our own
 * selectors. */
#define VERR_SELM_GDT_TOO_FULL              (-2508)
/** Got into a part of SELM that is not used when HM (VT-x/AMD-V) is enabled. */
#define VERR_SELM_HM_IPE                    (-2509)
/** @} */


/** @name I/O Manager / Monitor (IOM) Status Code
 * @{
 */
/** The specified I/O port range was invalid.
 * It was either empty or it was out of bounds. */
#define VERR_IOM_INVALID_IOPORT_RANGE       (-2600)
/** The specified R0 or RC I/O port range didn't have a corresponding R3 range.
 * IOMR3IOPortRegisterR3() must be called first. */
#define VERR_IOM_NO_R3_IOPORT_RANGE         (-2601)
/** The specified I/O port range intruded on an existing range. There is
 * a I/O port conflict between two device, or a device tried to register
 * the same range twice. */
#define VERR_IOM_IOPORT_RANGE_CONFLICT      (-2602)
/** The I/O port range specified for removal wasn't found or it wasn't contiguous. */
#define VERR_IOM_IOPORT_RANGE_NOT_FOUND     (-2603)
/** The specified I/O port range was owned by some other device(s). Both registration
 * and deregistration, but in the first case only RC and R0 ranges. */
#define VERR_IOM_NOT_IOPORT_RANGE_OWNER     (-2604)

/** The specified MMIO range was invalid.
 * It was either empty or it was out of bounds. */
#define VERR_IOM_INVALID_MMIO_RANGE         (-2605)
/** The specified R0 or RC MMIO range didn't have a corresponding R3 range.
 * IOMR3MMIORegisterR3() must be called first. */
#define VERR_IOM_NO_R3_MMIO_RANGE           (-2606)
/** The specified MMIO range was owned by some other device(s). Both registration
 * and deregistration, but in the first case only RC and R0 ranges. */
#define VERR_IOM_NOT_MMIO_RANGE_OWNER       (-2607)
/** The specified MMIO range intruded on an existing range. There is
 * a MMIO conflict between two device, or a device tried to register
 * the same range twice. */
#define VERR_IOM_MMIO_RANGE_CONFLICT        (-2608)
/** The MMIO range specified for removal was not found. */
#define VERR_IOM_MMIO_RANGE_NOT_FOUND       (-2609)
/** The MMIO range specified for removal was invalid. The range didn't match
 * quite match a set of existing ranges. It's not possible to remove parts of
 * a MMIO range, only one or more full ranges. */
#define VERR_IOM_INCOMPLETE_MMIO_RANGE      (-2610)
/** An invalid I/O port size was specified for a read or write operation. */
#define VERR_IOM_INVALID_IOPORT_SIZE        (-2611)
/** The MMIO handler was called for a bogus address! Internal error! */
#define VERR_IOM_MMIO_HANDLER_BOGUS_CALL    (-2612)
/** The MMIO handler experienced a problem with the disassembler. */
#define VERR_IOM_MMIO_HANDLER_DISASM_ERROR  (-2613)
/** The port being read was not present(/unused) and IOM shall return ~0 according to size. */
#define VERR_IOM_IOPORT_UNUSED              (-2614)
/** Unused MMIO register read, fill with 00. */
#define VINF_IOM_MMIO_UNUSED_00             2615
/** Unused MMIO register read, fill with FF. */
#define VINF_IOM_MMIO_UNUSED_FF             2616

/** Reason for leaving RZ: I/O port read. */
#define VINF_IOM_R3_IOPORT_READ             2620
/** Reason for leaving RZ: I/O port write. */
#define VINF_IOM_R3_IOPORT_WRITE            2621
/** Reason for leaving RZ: Pending I/O port write.  Since there is also
 * VMCPU_FF_IOM for this condition, it's ok to drop this status code for
 * some other VINF_EM_XXX statuses. */
#define VINF_IOM_R3_IOPORT_COMMIT_WRITE     2622
/** Reason for leaving RZ: MMIO read. */
#define VINF_IOM_R3_MMIO_READ               2623
/** Reason for leaving RZ: MMIO write. */
#define VINF_IOM_R3_MMIO_WRITE              2624
/** Reason for leaving RZ: MMIO read/write. */
#define VINF_IOM_R3_MMIO_READ_WRITE         2625
/** Reason for leaving RZ: Pending MMIO write.   Since there is also
 * VMCPU_FF_IOM for this condition, it's ok to drop this status code for
 * some other VINF_EM_XXX statuses. */
#define VINF_IOM_R3_MMIO_COMMIT_WRITE       2626

/** IOMGCIOPortHandler was given an unexpected opcode. */
#define VERR_IOM_IOPORT_UNKNOWN_OPCODE      (-2630)
/** Internal processing error \#1 in the I/O port code. */
#define VERR_IOM_IOPORT_IPE_1               (-2631)
/** Internal processing error \#2 in the I/O port code. */
#define VERR_IOM_IOPORT_IPE_2               (-2632)
/** Internal processing error \#3 in the I/O port code. */
#define VERR_IOM_IOPORT_IPE_3               (-2633)
/** Internal processing error \#1 in the MMIO code. */
#define VERR_IOM_MMIO_IPE_1                 (-2634)
/** Internal processing error \#2 in the MMIO code. */
#define VERR_IOM_MMIO_IPE_2                 (-2635)
/** Internal processing error \#3 in the MMIO code. */
#define VERR_IOM_MMIO_IPE_3                 (-2636)
/** Got into a part of IOM that is not used when HM (VT-x/AMD-V) is enabled. */
#define VERR_IOM_HM_IPE                     (-2637)
/** Internal processing error while merging status codes. */
#define VERR_IOM_FF_STATUS_IPE              (-2638)
/** @} */


/** @name Virtual Machine Monitor (VMM) Status Codes
 * @{
 */
/** Reason for leaving RZ: Calling host function. */
#define VINF_VMM_CALL_HOST                  2700
/** Reason for leaving R0: Hit a ring-0 assertion on EMT. */
#define VERR_VMM_RING0_ASSERTION            (-2701)
/** The hyper CR3 differs between PGM and CPUM. */
#define VERR_VMM_HYPER_CR3_MISMATCH         (-2702)
/** Reason for leaving RZ: Illegal call to ring-3. */
#define VERR_VMM_RING3_CALL_DISABLED        (-2703)
/** The VMMR0.r0 module version does not match VBoxVMM.dll/so/dylib.
 * If you just upgraded VirtualBox, please terminate all VMs and make sure
 * that neither VBoxNetDHCP nor VBoxNetNAT is running.  Then try again.
 * If this error persists, try re-installing VirtualBox. */
#define VERR_VMM_R0_VERSION_MISMATCH        (-2704)
/** The VMMRC.rc module version does not match VBoxVMM.dll/so/dylib.
 * Re-install if you are a user.  Developers should make sure the build is
 * complete or try with a clean build. */
#define VERR_VMM_RC_VERSION_MISMATCH        (-2705)
/** VMM set jump error. */
#define VERR_VMM_SET_JMP_ERROR              (-2706)
/** VMM set jump stack overflow error. */
#define VERR_VMM_SET_JMP_STACK_OVERFLOW     (-2707)
/** VMM set jump resume error. */
#define VERR_VMM_SET_JMP_ABORTED_RESUME     (-2708)
/** VMM long jump error. */
#define VERR_VMM_LONG_JMP_ERROR             (-2709)
/** Unknown ring-3 call attempted. */
#define VERR_VMM_UNKNOWN_RING3_CALL         (-2710)
/** The ring-3 call didn't set an RC. */
#define VERR_VMM_RING3_CALL_NO_RC           (-2711)
/** Reason for leaving RC: Caller the tracer in ring-0. */
#define VINF_VMM_CALL_TRACER                (2712)
/** Internal processing error \#1 in the switcher code. */
#define VERR_VMM_SWITCHER_IPE_1             (-2713)
/** Reason for leaving RZ: Unknown call to ring-3. */
#define VINF_VMM_UNKNOWN_RING3_CALL         (2714)
/** Attempted to use stub switcher. */
#define VERR_VMM_SWITCHER_STUB              (-2715)
/** HM returned in the wrong state. */
#define VERR_VMM_WRONG_HM_VMCPU_STATE       (-2716)
/** SMAP enabled, but the AC flag was found to be clear - check the kernel
 * log for details. */
#define VERR_VMM_SMAP_BUT_AC_CLEAR          (-2717)
/** NEM returned in the wrong state. */
#define VERR_VMM_WRONG_NEM_VMCPU_STATE      (-2718)
/** @} */


/** @name Pluggable Device and Driver Manager (PDM) Status Codes
 * @{
 */
/** An invalid LUN specification was given. */
#define VERR_PDM_NO_SUCH_LUN                        (-2800)
/** A device encountered an unknown configuration value.
 * This means that the device is potentially misconfigured and the device
 * construction or unit attachment failed because of this. */
#define VERR_PDM_DEVINS_UNKNOWN_CFG_VALUES          (-2801)
/** The above driver doesn't export a interface required by a driver being
 * attached to it. Typical misconfiguration problem. */
#define VERR_PDM_MISSING_INTERFACE_ABOVE            (-2802)
/** The below driver doesn't export a interface required by the drive
 * having attached it. Typical misconfiguration problem. */
#define VERR_PDM_MISSING_INTERFACE_BELOW            (-2803)
/** A device didn't find a required interface with an attached driver.
 * Typical misconfiguration problem. */
#define VERR_PDM_MISSING_INTERFACE                  (-2804)
/** A driver encountered an unknown configuration value.
 * This means that the driver is potentially misconfigured and the driver
 * construction failed because of this. */
#define VERR_PDM_DRVINS_UNKNOWN_CFG_VALUES          (-2805)
/** The PCI bus assigned to a device didn't have room for it.
 * Either too many devices are configured on the same PCI bus, or there are
 * some internal problem where PDM/PCI doesn't free up slots when unplugging devices. */
#define VERR_PDM_TOO_PCI_MANY_DEVICES               (-2806)
/** A queue is out of free items, the queueing operation failed. */
#define VERR_PDM_NO_QUEUE_ITEMS                     (-2807)
/** Not possible to attach further drivers to the driver.
 * A driver which doesn't support attachments (below of course) will
 * return this status code if it found that further drivers were configured
 * to be attached to it. */
#define VERR_PDM_DRVINS_NO_ATTACH                   (-2808)
/** Not possible to attach drivers to the device.
 * A device which doesn't support attachments (below of course) will
 * return this status code if it found that drivers were configured
 * to be attached to it. */
#define VERR_PDM_DEVINS_NO_ATTACH                   (-2809)
/** No attached driver.
 * The PDMDRVHLP::pfnAttach and PDMDEVHLP::pfnDriverAttach will return
 * this error when no driver was configured to be attached. */
#define VERR_PDM_NO_ATTACHED_DRIVER                 (-2810)
/** The media geometry hasn't been set yet, so it cannot be obtained.
 * The caller should then calculate the geometry from the media size. */
#define VERR_PDM_GEOMETRY_NOT_SET                   (-2811)
/** The media translation hasn't been set yet, so it cannot be obtained.
 * The caller should then guess the translation. */
#define VERR_PDM_TRANSLATION_NOT_SET                (-2812)
/** The media is not mounted, operation requires a mounted media. */
#define VERR_PDM_MEDIA_NOT_MOUNTED                  (-2813)
/** Mount failed because a media was already mounted. Unmount the media
 * and retry the mount. */
#define VERR_PDM_MEDIA_MOUNTED                      (-2814)
/** The media is locked and cannot be unmounted. */
#define VERR_PDM_MEDIA_LOCKED                       (-2815)
/** No 'Type' attribute in the DrvBlock configuration.
 * Misconfiguration. */
#define VERR_PDM_BLOCK_NO_TYPE                      (-2816)
/** The 'Type' attribute in the DrvBlock configuration had an unknown value.
 * Misconfiguration. */
#define VERR_PDM_BLOCK_UNKNOWN_TYPE                 (-2817)
/** The 'Translation' attribute in the DrvBlock configuration had an unknown value.
 * Misconfiguration. */
#define VERR_PDM_BLOCK_UNKNOWN_TRANSLATION          (-2818)
/** The block driver type wasn't supported.
 * Misconfiguration of the kind you get when attaching a floppy to an IDE controller. */
#define VERR_PDM_UNSUPPORTED_BLOCK_TYPE             (-2819)
/** A attach or prepare mount call failed because the driver already
 * had a driver attached. */
#define VERR_PDM_DRIVER_ALREADY_ATTACHED            (-2820)
/** An attempt on detaching a driver without anyone actually being attached, or
 * performing any other operation on an attached driver. */
#define VERR_PDM_NO_DRIVER_ATTACHED                 (-2821)
/** The attached driver configuration is missing the 'Driver' attribute. */
#define VERR_PDM_CFG_MISSING_DRIVER_NAME            (-2822)
/** The configured driver wasn't found.
 * Either the necessary driver modules wasn't loaded, the name was
 * misspelled, or it was a misconfiguration. */
#define VERR_PDM_DRIVER_NOT_FOUND                   (-2823)
/** The Ring-3 module was already loaded. */
#define VINF_PDM_ALREADY_LOADED                     (2824)
/** The name of the module clashed with an existing module. */
#define VERR_PDM_MODULE_NAME_CLASH                  (-2825)
/** Couldn't find any export for registration of drivers/devices. */
#define VERR_PDM_NO_REGISTRATION_EXPORT             (-2826)
/** A module name is too long. */
#define VERR_PDM_MODULE_NAME_TOO_LONG               (-2827)
/** Driver name clash. Another driver with the same name as the
 * one being registered exists. */
#define VERR_PDM_DRIVER_NAME_CLASH                  (-2828)
/** The version of the driver registration structure is unknown
 * to this VBox version. Either mixing incompatible versions or
 * the structure isn't correctly initialized. */
#define VERR_PDM_UNKNOWN_DRVREG_VERSION             (-2829)
/** Invalid entry in the driver registration structure. */
#define VERR_PDM_INVALID_DRIVER_REGISTRATION        (-2830)
/** Invalid host bit mask. */
#define VERR_PDM_INVALID_DRIVER_HOST_BITS           (-2831)
/** Not possible to detach a driver because the above driver/device
 * doesn't support it. The above entity doesn't implement the pfnDetach call. */
#define VERR_PDM_DRIVER_DETACH_NOT_POSSIBLE         (-2832)
/** No PCI Bus is available to register the device with. This is usually a
 * misconfiguration or in rare cases a buggy pci device. */
#define VERR_PDM_NO_PCI_BUS                         (-2833)
/** The device is not a registered PCI device and thus cannot
 * perform any PCI operations. The device forgot to register it self. */
#define VERR_PDM_NOT_PCI_DEVICE                     (-2834)

/** The version of the device registration structure is unknown
 * to this VBox version. Either mixing incompatible versions or
 * the structure isn't correctly initialized. */
#define VERR_PDM_UNKNOWN_DEVREG_VERSION             (-2835)
/** Invalid entry in the device registration structure. */
#define VERR_PDM_INVALID_DEVICE_REGISTRATION        (-2836)
/** Invalid host bit mask. */
#define VERR_PDM_INVALID_DEVICE_GUEST_BITS          (-2837)
/** The guest bit mask didn't match the guest being loaded. */
#define VERR_PDM_INVALID_DEVICE_HOST_BITS           (-2838)
/** Device name clash. Another device with the same name as the
 * one being registered exists. */
#define VERR_PDM_DEVICE_NAME_CLASH                  (-2839)
/** The device wasn't found. There was no registered device
 * by that name. */
#define VERR_PDM_DEVICE_NOT_FOUND                   (-2840)
/** The device instance was not found. */
#define VERR_PDM_DEVICE_INSTANCE_NOT_FOUND          (-2841)
/** The device instance have no base interface. */
#define VERR_PDM_DEVICE_INSTANCE_NO_IBASE           (-2842)
/** The device instance have no such logical unit. */
#define VERR_PDM_DEVICE_INSTANCE_LUN_NOT_FOUND      (-2843)
/** The driver instance could not be found. */
#define VERR_PDM_DRIVER_INSTANCE_NOT_FOUND          (-2844)
/** Logical Unit was not found. */
#define VERR_PDM_LUN_NOT_FOUND                      (-2845)
/** The Logical Unit was found, but it had no driver attached to it. */
#define VERR_PDM_NO_DRIVER_ATTACHED_TO_LUN          (-2846)
/** The Logical Unit was found, but it had no driver attached to it. */
#define VINF_PDM_NO_DRIVER_ATTACHED_TO_LUN          2846
/** No PIC device instance is registered with the current VM and thus
 * the PIC operation cannot be performed. */
#define VERR_PDM_NO_PIC_INSTANCE                    (-2847)
/** No APIC device instance is registered with the current VM and thus
 * the APIC operation cannot be performed. */
#define VERR_PDM_NO_APIC_INSTANCE                   (-2848)
/** No DMAC device instance is registered with the current VM and thus
 * the DMA operation cannot be performed. */
#define VERR_PDM_NO_DMAC_INSTANCE                   (-2849)
/** No RTC device instance is registered with the current VM and thus
 * the RTC or CMOS operation cannot be performed. */
#define VERR_PDM_NO_RTC_INSTANCE                    (-2850)
/** Unable to open the host interface due to a sharing violation . */
#define VERR_PDM_HIF_SHARING_VIOLATION              (-2851)
/** Unable to open the host interface. */
#define VERR_PDM_HIF_OPEN_FAILED                    (-2852)
/** The device doesn't support runtime driver attaching.
 * The PDMDEVREG::pfnAttach callback function is NULL. */
#define VERR_PDM_DEVICE_NO_RT_ATTACH                (-2853)
/** The driver doesn't support runtime driver attaching.
 * The PDMDRVREG::pfnAttach callback function is NULL. */
#define VERR_PDM_DRIVER_NO_RT_ATTACH                (-2854)
/** Invalid host interface version. */
#define VERR_PDM_HIF_INVALID_VERSION                (-2855)

/** The version of the USB device registration structure is unknown
 * to this VBox version. Either mixing incompatible versions or
 * the structure isn't correctly initialized. */
#define VERR_PDM_UNKNOWN_USBREG_VERSION             (-2856)
/** Invalid entry in the device registration structure. */
#define VERR_PDM_INVALID_USB_REGISTRATION           (-2857)
/** Driver name clash. Another driver with the same name as the
 * one being registered exists. */
#define VERR_PDM_USB_NAME_CLASH                     (-2858)
/** The USB hub is already registered. */
#define VERR_PDM_USB_HUB_EXISTS                     (-2859)
/** Couldn't find any USB hubs to attach the device to. */
#define VERR_PDM_NO_USB_HUBS                        (-2860)
/** Couldn't find any free USB ports to attach the device to. */
#define VERR_PDM_NO_USB_PORTS                       (-2861)
/** Couldn't find the USB Proxy device. Using OSE? */
#define VERR_PDM_NO_USBPROXY                        (-2862)
/** The async completion template is still used. */
#define VERR_PDM_ASYNC_TEMPLATE_BUSY                (-2863)
/** The async completion task is already suspended. */
#define VERR_PDM_ASYNC_COMPLETION_ALREADY_SUSPENDED (-2864)
/** The async completion task is not suspended. */
#define VERR_PDM_ASYNC_COMPLETION_NOT_SUSPENDED     (-2865)
/** The driver properties were invalid, and as a consequence construction
 * failed. Caused my unusable media or similar problems. */
#define VERR_PDM_DRIVER_INVALID_PROPERTIES          (-2866)
/** Too many instances of a device. */
#define VERR_PDM_TOO_MANY_DEVICE_INSTANCES          (-2867)
/** Too many instances of a driver. */
#define VERR_PDM_TOO_MANY_DRIVER_INSTANCES          (-2868)
/** Too many instances of a usb device. */
#define VERR_PDM_TOO_MANY_USB_DEVICE_INSTANCES      (-2869)
/** The device instance structure version has changed.
 *
 * If you have upgraded VirtualBox recently, please make sure you have
 * terminated all VMs and upgraded any extension packs.  If this error
 * persists, try re-installing VirtualBox. */
#define VERR_PDM_DEVINS_VERSION_MISMATCH            (-2870)
/** The device helper structure version has changed.
 *
 * If you have upgraded VirtualBox recently, please make sure you have
 * terminated all VMs and upgraded any extension packs.  If this error
 * persists, try re-installing VirtualBox. */
#define VERR_PDM_DEVHLPR3_VERSION_MISMATCH          (-2871)
/** The USB device instance structure version has changed.
 *
 * If you have upgraded VirtualBox recently, please make sure you have
 * terminated all VMs and upgraded any extension packs.  If this error
 * persists, try re-installing VirtualBox. */
#define VERR_PDM_USBINS_VERSION_MISMATCH            (-2872)
/** The USB device helper structure version has changed.
 *
 * If you have upgraded VirtualBox recently, please make sure you have
 * terminated all VMs and upgraded any extension packs.  If this error
 * persists, try re-installing VirtualBox. */
#define VERR_PDM_USBHLPR3_VERSION_MISMATCH          (-2873)
/** The driver instance structure version has changed.
 *
 * If you have upgraded VirtualBox recently, please make sure you have
 * terminated all VMs and upgraded any extension packs.  If this error
 * persists, try re-installing VirtualBox. */
#define VERR_PDM_DRVINS_VERSION_MISMATCH            (-2874)
/** The driver helper structure version has changed.
 *
 * If you have upgraded VirtualBox recently, please make sure you have
 * terminated all VMs and upgraded any extension packs.  If this error
 * persists, try re-installing VirtualBox. */
#define VERR_PDM_DRVHLPR3_VERSION_MISMATCH          (-2875)
/** Generic device structure version mismatch.
 *
 * If you have upgraded VirtualBox recently, please make sure you have
 * terminated all VMs and upgraded any extension packs.  If this error
 * persists, try re-installing VirtualBox. */
#define VERR_PDM_DEVICE_VERSION_MISMATCH            (-2876)
/** Generic USB device structure version mismatch.
 *
 * If you have upgraded VirtualBox recently, please make sure you have
 * terminated all VMs and upgraded any extension packs.  If this error
 * persists, try re-installing VirtualBox. */
#define VERR_PDM_USBDEV_VERSION_MISMATCH            (-2877)
/** Generic driver structure version mismatch.
 *
 * If you have upgraded VirtualBox recently, please make sure you have
 * terminated all VMs and upgraded any extension packs.  If this error
 * persists, try re-installing VirtualBox. */
#define VERR_PDM_DRIVER_VERSION_MISMATCH            (-2878)
/** PDMVMMDevHeapR3ToGCPhys failure. */
#define VERR_PDM_DEV_HEAP_R3_TO_GCPHYS              (-2879)
/** A legacy device isn't implementing the HPET notification interface. */
#define VERR_PDM_HPET_LEGACY_NOTIFY_MISSING         (-2880)
/** Internal processing error in the critical section code. */
#define VERR_PDM_CRITSECT_IPE                       (-2881)
/** The critical section being deleted was not found. */
#define VERR_PDM_CRITSECT_NOT_FOUND                 (-2882)
/** A PDMThread API was called by the wrong thread. */
#define VERR_PDM_THREAD_INVALID_CALLER              (-2883)
/** Internal processing error \#1 in the PDM Thread code. */
#define VERR_PDM_THREAD_IPE_1                       (-2884)
/** Internal processing error \#2 in the PDM Thread code. */
#define VERR_PDM_THREAD_IPE_2                       (-2885)
/** Only one PCI function is supported per PDM device. */
#define VERR_PDM_ONE_PCI_FUNCTION_PER_DEVICE        (-2886)
/** Bad PCI configuration. */
#define VERR_PDM_BAD_PCI_CONFIG                     (-2887)
/** Internal processing error # in the PDM device code. */
#define VERR_PDM_DEV_IPE_1                          (-2888)
/** Misconfigured driver chain transformation. */
#define VERR_PDM_MISCONFIGURED_DRV_TRANSFORMATION   (-2889)
/** The driver is already removed, not more transformations possible (at
 *  present). */
#define VERR_PDM_CANNOT_TRANSFORM_REMOVED_DRIVER    (-2890)
/** The PCI device isn't configured as a busmaster, physical memory access
 * rejected. */
#define VERR_PDM_NOT_PCI_BUS_MASTER                 (-2891)
/** Got into a part of PDM that is not used when HM (VT-x/AMD-V) is enabled. */
#define VERR_PDM_HM_IPE                             (-2892)
/** The I/O request was canceled. */
#define VERR_PDM_MEDIAEX_IOREQ_CANCELED             (-2893)
/** There is not enough room to store the data. */
#define VERR_PDM_MEDIAEX_IOBUF_OVERFLOW             (-2894)
/** There is not enough data to satisfy the request. */
#define VERR_PDM_MEDIAEX_IOBUF_UNDERRUN             (-2895)
/** The I/O request ID is already existing. */
#define VERR_PDM_MEDIAEX_IOREQID_CONFLICT           (-2896)
/** The I/O request ID was not found. */
#define VERR_PDM_MEDIAEX_IOREQID_NOT_FOUND          (-2897)
/** The I/O request is in progress. */
#define VINF_PDM_MEDIAEX_IOREQ_IN_PROGRESS          2898
/** The I/O request is in an invalid state for this operation. */
#define VERR_PDM_MEDIAEX_IOREQ_INVALID_STATE        (-2899)
/** @} */


/** @name Host-Guest Communication Manager (HGCM) Status Codes
 * @{
 */
/** Requested service does not exist. */
#define VERR_HGCM_SERVICE_NOT_FOUND                 (-2900)
/** Service rejected client connection */
#define VINF_HGCM_CLIENT_REJECTED                   2901
/** Command address is invalid. */
#define VERR_HGCM_INVALID_CMD_ADDRESS               (-2902)
/** Service will execute the command in background. */
#define VINF_HGCM_ASYNC_EXECUTE                     2903
/** HGCM could not perform requested operation because of an internal error. */
#define VERR_HGCM_INTERNAL                          (-2904)
/** Invalid HGCM client id. */
#define VERR_HGCM_INVALID_CLIENT_ID                 (-2905)
/** The HGCM is saving state. */
#define VINF_HGCM_SAVE_STATE                        (2906)
/** Requested service already exists. */
#define VERR_HGCM_SERVICE_EXISTS                    (-2907)

/** @} */


/** @name Network Address Translation Driver (DrvNAT) Status Codes
 * @{
 */
/** Failed to find the DNS configured for this machine. */
#define VINF_NAT_DNS                                3000
/** Failed to convert the specified Guest IP to a binary IP address.
 * Malformed input. */
#define VERR_NAT_REDIR_GUEST_IP                     (-3001)
/** Failed while setting up a redirector rule.
 * There probably is a conflict between the rule and some existing
 * service on the computer. */
#define VERR_NAT_REDIR_SETUP                        (-3002)
/** @} */


/** @name HostIF Driver (DrvTUN) Status Codes
 * @{
 */
/** The Host Interface Networking init program failed. */
#define VERR_HOSTIF_INIT_FAILED                     (-3100)
/** The Host Interface Networking device name is too long. */
#define VERR_HOSTIF_DEVICE_NAME_TOO_LONG            (-3101)
/** The Host Interface Networking name config IOCTL call failed. */
#define VERR_HOSTIF_IOCTL                           (-3102)
/** Failed to make the Host Interface Networking handle non-blocking. */
#define VERR_HOSTIF_BLOCKING                        (-3103)
/** If a Host Interface Networking filehandle was specified it's not allowed to
 * have any init or term programs. */
#define VERR_HOSTIF_FD_AND_INIT_TERM                (-3104)
/** The Host Interface Networking terminate program failed. */
#define VERR_HOSTIF_TERM_FAILED                     (-3105)
/** @} */


/** @name VBox HDD Container (VD) Status Codes
 * @{
 */
/** Invalid image type. */
#define VERR_VD_INVALID_TYPE                        (-3200)
/** Operation can't be done in current HDD container state. */
#define VERR_VD_INVALID_STATE                       (-3201)
/** Configuration value not found. */
#define VERR_VD_VALUE_NOT_FOUND                     (-3202)
/** Virtual HDD is not opened. */
#define VERR_VD_NOT_OPENED                          (-3203)
/** Requested image is not opened. */
#define VERR_VD_IMAGE_NOT_FOUND                     (-3204)
/** Image is read-only. */
#define VERR_VD_IMAGE_READ_ONLY                     (-3205)
/** Geometry hasn't been set. */
#define VERR_VD_GEOMETRY_NOT_SET                    (-3206)
/** No data for this block in image. */
#define VERR_VD_BLOCK_FREE                          (-3207)
/** Differencing and parent images can't be used together due to UUID. */
#define VERR_VD_UUID_MISMATCH                       (-3208)
/** Asynchronous I/O request finished. */
#define VINF_VD_ASYNC_IO_FINISHED                   3209
/** Asynchronous I/O is not finished yet. */
#define VERR_VD_ASYNC_IO_IN_PROGRESS                (-3210)
/** The image is too small or too large for this format. */
#define VERR_VD_INVALID_SIZE                        (-3211)
/** Configuration value is unknown. This indicates misconfiguration. */
#define VERR_VD_UNKNOWN_CFG_VALUES                  (-3212)
/** Interface is unknown. This indicates misconfiguration. */
#define VERR_VD_UNKNOWN_INTERFACE                   (-3213)
/** The DEK for disk encryption is missing. */
#define VERR_VD_DEK_MISSING                         (-3214)
/** The provided password to decrypt the DEK was incorrect. */
#define VERR_VD_PASSWORD_INCORRECT                  (-3215)
/** Generic: Invalid image file header. Use this for plugins. */
#define VERR_VD_GEN_INVALID_HEADER                  (-3220)
/** VDI: Invalid image file header. */
#define VERR_VD_VDI_INVALID_HEADER                  (-3230)
/** VDI: Invalid image file header: invalid signature. */
#define VERR_VD_VDI_INVALID_SIGNATURE               (-3231)
/** VDI: Invalid image file header: invalid version. */
#define VERR_VD_VDI_UNSUPPORTED_VERSION             (-3232)
/** Comment string is too long. */
#define VERR_VD_VDI_COMMENT_TOO_LONG                (-3233)
/** VMDK: Invalid image file header. */
#define VERR_VD_VMDK_INVALID_HEADER                 (-3240)
/** VMDK: Invalid image file header: invalid version. */
#define VERR_VD_VMDK_UNSUPPORTED_VERSION            (-3241)
/** VMDK: Image property not found. */
#define VERR_VD_VMDK_VALUE_NOT_FOUND                (-3242)
/** VMDK: Operation can't be done in current image state. */
#define VERR_VD_VMDK_INVALID_STATE                  (-3243)
/** VMDK: Format is invalid/inconsistent. */
#define VERR_VD_VMDK_INVALID_FORMAT                 (-3244)
/** VMDK: Invalid write position. */
#define VERR_VD_VMDK_INVALID_WRITE                  (-3245)
/** iSCSI: Invalid header, i.e. dummy for validity check. */
#define VERR_VD_ISCSI_INVALID_HEADER                (-3250)
/** iSCSI: Operation can't be done in current image state. */
#define VERR_VD_ISCSI_INVALID_STATE                 (-3251)
/** iSCSI: Invalid device type (not a disk). */
#define VERR_VD_ISCSI_INVALID_TYPE                  (-3252)
/** iSCSI: Initiator secret not decrypted */
#define VERR_VD_ISCSI_SECRET_ENCRYPTED              (-3253)
/** VHD: Invalid image file header. */
#define VERR_VD_VHD_INVALID_HEADER                  (-3260)
/** Parallels HDD: Invalid image file header. */
#define VERR_VD_PARALLELS_INVALID_HEADER            (-3265)
/** DMG: Invalid image file header. */
#define VERR_VD_DMG_INVALID_HEADER                  (-3267)
/** Raw: Invalid image file header. */
#define VERR_VD_RAW_INVALID_HEADER                  (-3270)
/** Raw: Invalid image file type. */
#define VERR_VD_RAW_INVALID_TYPE                    (-3271)
/** The backend needs more metadata before it can continue. */
#define VERR_VD_NOT_ENOUGH_METADATA                 (-3272)
/** Halt the current I/O context until further notification from the backend. */
#define VERR_VD_IOCTX_HALT                          (-3273)
/** The disk has a cache attached already. */
#define VERR_VD_CACHE_ALREADY_EXISTS                (-3274)
/** There is no cache attached to the disk. */
#define VERR_VD_CACHE_NOT_FOUND                     (-3275)
/** The cache is not up to date with the image. */
#define VERR_VD_CACHE_NOT_UP_TO_DATE                (-3276)
/** The given range does not meet the required alignment. */
#define VERR_VD_DISCARD_ALIGNMENT_NOT_MET           (-3277)
/** The discard operation is not supported for this image. */
#define VERR_VD_DISCARD_NOT_SUPPORTED               (-3278)
/** The image is the correct format but is corrupted. */
#define VERR_VD_IMAGE_CORRUPTED                     (-3279)
/** Repairing the image is not supported. */
#define VERR_VD_IMAGE_REPAIR_NOT_SUPPORTED          (-3280)
/** Repairing the image is not possible because the corruption is to severe. */
#define VERR_VD_IMAGE_REPAIR_IMPOSSIBLE             (-3281)
/** Reading from the image was not possible because the offset is out of the image range.
 * This usually indicates that there is a minor corruption in the image meta data. */
#define VERR_VD_READ_OUT_OF_RANGE                   (-3282)
/** Block read was marked as free in the image and returned as a zero block. */
#define VINF_VD_NEW_ZEROED_BLOCK                    3283
/** Unable to parse the XML in DMG file. */
#define VERR_VD_DMG_XML_PARSE_ERROR                 (-3284)
/** Unable to locate a usable DMG file within the XAR archive. */
#define VERR_VD_DMG_NOT_FOUND_INSIDE_XAR            (-3285)
/** The size of the raw image is not dividable by 512 */
#define VERR_VD_RAW_SIZE_MODULO_512                 (-3286)
/** The size of the raw image is not dividable by 2048 */
#define VERR_VD_RAW_SIZE_MODULO_2048                (-3287)
/** The size of the raw optical image is too small (<= 32K) */
#define VERR_VD_RAW_SIZE_OPTICAL_TOO_SMALL          (-3288)
/** The size of the raw floppy image is too big (>2.88MB) */
#define VERR_VD_RAW_SIZE_FLOPPY_TOO_BIG             (-3289)
/** Reducing the size is not supported */
#define VERR_VD_SHRINK_NOT_SUPPORTED                (-3290)
/** @} */


/** @name VBox Guest Library (VBGL) Status Codes
 * @{
 */
/** Library was not initialized. */
#define VERR_VBGL_NOT_INITIALIZED                   (-3300)
/** Virtual address was not allocated by the library. */
#define VERR_VBGL_INVALID_ADDR                      (-3301)
/** IOCtl to VBoxGuest driver failed. */
#define VERR_VBGL_IOCTL_FAILED                      (-3302)
/** @} */


/** @name VBox USB (VUSB) Status Codes
 * @{
 */
/** No available ports on the hub.
 * This error is returned when a device is attempted created and/or attached
 * to a hub which is out of ports. */
#define VERR_VUSB_NO_PORTS                          (-3400)
/** The requested operation cannot be performed on a detached USB device. */
#define VERR_VUSB_DEVICE_NOT_ATTACHED               (-3401)
/** Failed to allocate memory for a URB. */
#define VERR_VUSB_NO_URB_MEMORY                     (-3402)
/** General failure during URB queuing.
 * This will go away when the queueing gets proper status code handling. */
#define VERR_VUSB_FAILED_TO_QUEUE_URB               (-3403)
/** Device creation failed because the USB device name was not found. */
#define VERR_VUSB_DEVICE_NAME_NOT_FOUND             (-3404)
/** Not permitted to open the USB device.
 * The user doesn't have access to the device in the usbfs, check the mount options. */
#define VERR_VUSB_USBFS_PERMISSION                  (-3405)
/** The requested operation cannot be performed because the device
 * is currently being reset. */
#define VERR_VUSB_DEVICE_IS_RESETTING               (-3406)
/** The requested operation cannot be performed because the device
 * is currently suspended. */
#define VERR_VUSB_DEVICE_IS_SUSPENDED               (-3407)
/** Not permitted to open the USB device.
 * The user doesn't have access to the device node, check group memberships. */
#define VERR_VUSB_USB_DEVICE_PERMISSION             (-3408)
/** @} */


/** @name VBox VGA Status Codes
 * @{
 */
/** One of the custom modes was incorrect.
 * The format or bit count of the custom mode value is invalid. */
#define VERR_VGA_INVALID_CUSTOM_MODE                (-3500)
/** The display connector is resizing. */
#define VINF_VGA_RESIZE_IN_PROGRESS                 (3501)
/** Unexpected PCI region change during VGA saved state loading. */
#define VERR_VGA_UNEXPECTED_PCI_REGION_LOAD_CHANGE  (-3502)
/** Unabled to locate or load the OpenGL library. */
#define VERR_VGA_GL_LOAD_FAILURE                    (-3503)
/** Unabled to locate an OpenGL symbol. */
#define VERR_VGA_GL_SYMBOL_NOT_FOUND                (-3504)
/** @} */


/** @name Internal Networking Status Codes
 * @{
 */
/** The networking interface to filter was not found. */
#define VERR_INTNET_FLT_IF_NOT_FOUND                (-3600)
/** The networking interface to filter was busy (used by someone). */
#define VERR_INTNET_FLT_IF_BUSY                     (-3601)
/** Failed to create or connect to a networking interface filter. */
#define VERR_INTNET_FLT_IF_FAILED                   (-3602)
/** The network already exists with a different trunk configuration. */
#define VERR_INTNET_INCOMPATIBLE_TRUNK              (-3603)
/** The network already exists with a different security profile (restricted / public). */
#define VERR_INTNET_INCOMPATIBLE_FLAGS              (-3604)
/** Failed to create a virtual network interface instance. */
#define VERR_INTNET_FLT_VNIC_CREATE_FAILED          (-3605)
/** Failed to retrieve a virtual network interface link ID. */
#define VERR_INTNET_FLT_VNIC_LINK_ID_NOT_FOUND      (-3606)
/** Failed to initialize a virtual network interface instance. */
#define VERR_INTNET_FLT_VNIC_INIT_FAILED            (-3607)
/** Failed to open a virtual network interface instance. */
#define VERR_INTNET_FLT_VNIC_OPEN_FAILED            (-3608)
/** Failed to retrieve underlying (lower mac) link. */
#define VERR_INTNET_FLT_LOWER_LINK_INFO_NOT_FOUND   (-3609)
/** Failed to open underlying link instance. */
#define VERR_INTNET_FLT_LOWER_LINK_OPEN_FAILED      (-3610)
/** Failed to get underlying link ID. */
#define VERR_INTNET_FLT_LOWER_LINK_ID_NOT_FOUND     (-3611)
/** @} */


/** @name Support Driver Status Codes
 * @{
 */
/** The component factory was not found. */
#define VERR_SUPDRV_COMPONENT_NOT_FOUND             (-3700)
/** The component factories do not support the requested interface. */
#define VERR_SUPDRV_INTERFACE_NOT_SUPPORTED         (-3701)
/** The service module was not found. */
#define VERR_SUPDRV_SERVICE_NOT_FOUND               (-3702)
/** The host kernel is too old. */
#define VERR_SUPDRV_KERNEL_TOO_OLD_FOR_VTX          (-3703)
/** Bad VTG magic value.  */
#define VERR_SUPDRV_VTG_MAGIC                       (-3704)
/** Bad VTG bit count value.  */
#define VERR_SUPDRV_VTG_BITS                        (-3705)
/** Bad VTG header - misc.  */
#define VERR_SUPDRV_VTG_BAD_HDR_MISC                (-3706)
/** Bad VTG header - offset.  */
#define VERR_SUPDRV_VTG_BAD_HDR_OFF                 (-3707)
/** Bad VTG header - offset.  */
#define VERR_SUPDRV_VTG_BAD_HDR_PTR                 (-3708)
/** Bad VTG header - to low value.  */
#define VERR_SUPDRV_VTG_BAD_HDR_TOO_FEW             (-3709)
/** Bad VTG header - to high value.  */
#define VERR_SUPDRV_VTG_BAD_HDR_TOO_MUCH            (-3710)
/** Bad VTG header - size value is not a multiple of the structure size. */
#define VERR_SUPDRV_VTG_BAD_HDR_NOT_MULTIPLE        (-3711)
/** Bad VTG string table offset. */
#define VERR_SUPDRV_VTG_STRTAB_OFF                  (-3712)
/** Bad VTG string. */
#define VERR_SUPDRV_VTG_BAD_STRING                  (-3713)
/** VTG string is too long. */
#define VERR_SUPDRV_VTG_STRING_TOO_LONG             (-3714)
/** Bad VTG attribute value. */
#define VERR_SUPDRV_VTG_BAD_ATTR                    (-3715)
/** Bad VTG provider descriptor. */
#define VERR_SUPDRV_VTG_BAD_PROVIDER                (-3716)
/** Bad VTG probe descriptor. */
#define VERR_SUPDRV_VTG_BAD_PROBE                   (-3717)
/** Bad VTG argument list descriptor. */
#define VERR_SUPDRV_VTG_BAD_ARGLIST                 (-3718)
/** Bad VTG probe enabled data. */
#define VERR_SUPDRV_VTG_BAD_PROBE_ENABLED           (-3719)
/** Bad VTG probe location record. */
#define VERR_SUPDRV_VTG_BAD_PROBE_LOC               (-3720)
/** The VTG object for the session or image has already been registered. */
#define VERR_SUPDRV_VTG_ALREADY_REGISTERED          (-3721)
/** A driver may only register one VTG object per session. */
#define VERR_SUPDRV_VTG_ONLY_ONCE_PER_SESSION       (-3722)
/** A tracer has already been registered. */
#define VERR_SUPDRV_TRACER_ALREADY_REGISTERED       (-3723)
/** The session has no tracer associated with it. */
#define VERR_SUPDRV_TRACER_NOT_REGISTERED           (-3724)
/** The tracer has already been opened in this sesssion. */
#define VERR_SUPDRV_TRACER_ALREADY_OPENED           (-3725)
/** The tracer has not been opened. */
#define VERR_SUPDRV_TRACER_NOT_OPENED               (-3726)
/** There is no tracer present. */
#define VERR_SUPDRV_TRACER_NOT_PRESENT              (-3727)
/** The tracer is unloading. */
#define VERR_SUPDRV_TRACER_UNLOADING                (-3728)
/** Another thread in the session is talking to the tracer.  */
#define VERR_SUPDRV_TRACER_SESSION_BUSY             (-3729)
/** The tracer cannot open it self in the same session. */
#define VERR_SUPDRV_TRACER_CANNOT_OPEN_SELF         (-3730)
/** Bad argument flags. */
#define VERR_SUPDRV_TRACER_BAD_ARG_FLAGS            (-3731)
/** The session has reached the max number of (user mode) providers. */
#define VERR_SUPDRV_TRACER_TOO_MANY_PROVIDERS       (-3732)
/** The tracepoint provider object is too large. */
#define VERR_SUPDRV_TRACER_TOO_LARGE                (-3733)
/** The probe location array isn't adjacent to the probe enable array. */
#define VERR_SUPDRV_TRACER_UMOD_NOT_ADJACENT        (-3734)
/** The user mode tracepoint provider has too many probe locations and
 * probes. */
#define VERR_SUPDRV_TRACER_UMOD_TOO_MANY_PROBES     (-3735)
/** The user mode tracepoint provider string table is too large. */
#define VERR_SUPDRV_TRACER_UMOD_STRTAB_TOO_BIG      (-3736)
/** The user mode tracepoint provider string table offset is bad. */
#define VERR_SUPDRV_TRACER_UMOD_STRTAB_OFF_BAD      (-3737)
/** The VM process was denied access to vboxdrv because someone have managed to
 * open the process or its main thread with too broad access rights. */
#define VERR_SUPDRV_HARDENING_EVIL_HANDLE           (-3738)
/** Error opening the ApiPort LPC object. */
#define VERR_SUPDRV_APIPORT_OPEN_ERROR              (-3739)
/** Error enumerating all processes in the session. */
#define VERR_SUPDRV_SESSION_PROCESS_ENUM_ERROR      (-3740)
/** The CSRSS instance associated with the client process could not be
 * located. */
#define VERR_SUPDRV_CSRSS_NOT_FOUND                 (-3741)
/** Type error opening the ApiPort LPC object. */
#define VERR_SUPDRV_APIPORT_OPEN_ERROR_TYPE         (-3742)
/** Failed to measure the TSC delta between two CPUs. */
#define VERR_SUPDRV_TSC_DELTA_MEASUREMENT_FAILED    (-3743)
/** Failed to calculate the TSC frequency. */
#define VERR_SUPDRV_TSC_FREQ_MEASUREMENT_FAILED     (-3744)
/** Failed to get the delta-adjusted TSC value. */
#define VERR_SUPDRV_TSC_READ_FAILED                 (-3745)
/** Failed to measure the TSC delta between two CPUs, continue without any
 *  TSC-delta. */
#define VWRN_SUPDRV_TSC_DELTA_MEASUREMENT_FAILED     3746
/** A TSC-delta measurement request is currently being serviced. */
#define VERR_SUPDRV_TSC_DELTA_MEASUREMENT_BUSY      (-3747)
/** The process trying to open VBoxDrv is not a budding VM process (1). */
#define VERR_SUPDRV_NOT_BUDDING_VM_PROCESS_1        (-3748)
/** The process trying to open VBoxDrv is not a budding VM process (2). */
#define VERR_SUPDRV_NOT_BUDDING_VM_PROCESS_2        (-3749)

/** Raw-mode is unavailable courtesy of Hyper-V. */
#define VERR_SUPDRV_NO_RAW_MODE_HYPER_V_ROOT        (-7000)
/** @} */


/** @name Support Library Status Codes
 * @{
 */
/** The specified path was not absolute (hardening). */
#define VERR_SUPLIB_PATH_NOT_ABSOLUTE               (-3750)
/** The specified path was not clean (hardening). */
#define VERR_SUPLIB_PATH_NOT_CLEAN                  (-3751)
/** The specified path is too long (hardening). */
#define VERR_SUPLIB_PATH_TOO_LONG                   (-3752)
/** The specified path is too short (hardening). */
#define VERR_SUPLIB_PATH_TOO_SHORT                  (-3753)
/** The specified path has too many components (hardening). */
#define VERR_SUPLIB_PATH_TOO_MANY_COMPONENTS        (-3754)
/** The specified path is a root path (hardening). */
#define VERR_SUPLIB_PATH_IS_ROOT                    (-3755)
/** Failed to enumerate directory (hardening). */
#define VERR_SUPLIB_DIR_ENUM_FAILED                 (-3756)
/** Failed to stat a file/dir during enumeration (hardening). */
#define VERR_SUPLIB_STAT_ENUM_FAILED                (-3757)
/** Failed to stat a file/dir (hardening). */
#define VERR_SUPLIB_STAT_FAILED                     (-3758)
/** Failed to fstat a native handle (hardening). */
#define VERR_SUPLIB_FSTAT_FAILED                    (-3759)
/** Found an illegal symbolic link (hardening). */
#define VERR_SUPLIB_SYMLINKS_ARE_NOT_PERMITTED      (-3760)
/** Found something which isn't a file nor a directory (hardening). */
#define VERR_SUPLIB_NOT_DIR_NOT_FILE                (-3761)
/** The specified path is a directory and not a file (hardening). */
#define VERR_SUPLIB_IS_DIRECTORY                    (-3762)
/** The specified path is a file and not a directory (hardening). */
#define VERR_SUPLIB_IS_FILE                         (-3763)
/** The path is not the same object as the native handle (hardening). */
#define VERR_SUPLIB_NOT_SAME_OBJECT                 (-3764)
/** The owner is not root (hardening). */
#define VERR_SUPLIB_OWNER_NOT_ROOT                  (-3765)
/** The group is a non-system group and it has write access (hardening). */
#define VERR_SUPLIB_WRITE_NON_SYS_GROUP             (-3766)
/** The file or directory is world writable (hardening). */
#define VERR_SUPLIB_WORLD_WRITABLE                  (-3767)
/** The argv[0] of an internal application does not match the executable image
 * path (hardening). */
#define VERR_SUPLIB_INVALID_ARGV0_INTERNAL          (-3768)
/** The internal application does not reside in the correct place (hardening). */
#define VERR_SUPLIB_INVALID_INTERNAL_APP_DIR        (-3769)
/** Unable to establish trusted of VM process (0). */
#define VERR_SUPLIB_NT_PROCESS_UNTRUSTED_0          (-3770)
/** Unable to establish trusted of VM process (1). */
#define VERR_SUPLIB_NT_PROCESS_UNTRUSTED_1          (-3771)
/** Unable to establish trusted of VM process (2). */
#define VERR_SUPLIB_NT_PROCESS_UNTRUSTED_2          (-3772)
/** Unable to establish trusted of VM process (3). */
#define VERR_SUPLIB_NT_PROCESS_UNTRUSTED_3          (-3773)
/** Unable to establish trusted of VM process (4). */
#define VERR_SUPLIB_NT_PROCESS_UNTRUSTED_4          (-3774)
/** Unable to establish trusted of VM process (5). */
#define VERR_SUPLIB_NT_PROCESS_UNTRUSTED_5          (-3775)
/** Unable to make text memory writeable (hardening). */
#define VERR_SUPLIB_TEXT_NOT_WRITEABLE              (-3776)
/** Unable to seal text memory again to protect against write access (hardening). */
#define VERR_SUPLIB_TEXT_NOT_SEALED                 (-3777)
/** Unexpected instruction encountered for which there is no patch strategy
 * implemented (hardening). */
#define VERR_SUPLIB_UNEXPECTED_INSTRUCTION          (-3778)
/** @} */


/** @name VBox GMM Status Codes
 * @{
 */
/** The GMM is out of pages and needs to be give another chunk of user memory that
 * it can lock down and borrow pages from. */
#define VERR_GMM_SEED_ME                            (-3800)
/** Unable to allocate more pages from the host system. */
#define VERR_GMM_OUT_OF_MEMORY                      (-3801)
/** Hit the global allocation limit.
 * If you know there is still sufficient memory available, try raising the limit. */
#define VERR_GMM_HIT_GLOBAL_LIMIT                   (-3802)
/** Hit the a VM account limit. */
#define VERR_GMM_HIT_VM_ACCOUNT_LIMIT               (-3803)
/** Attempt to free more memory than what was previously allocated. */
#define VERR_GMM_ATTEMPT_TO_FREE_TOO_MUCH           (-3804)
/** Attempted to report too many pages as deflated.  */
#define VERR_GMM_ATTEMPT_TO_DEFLATE_TOO_MUCH        (-3805)
/** The page to be freed or updated was not found. */
#define VERR_GMM_PAGE_NOT_FOUND                     (-3806)
/** The specified shared page was not actually private. */
#define VERR_GMM_PAGE_NOT_PRIVATE                   (-3807)
/** The specified shared page was not actually shared. */
#define VERR_GMM_PAGE_NOT_SHARED                    (-3808)
/** The page to be freed was already freed. */
#define VERR_GMM_PAGE_ALREADY_FREE                  (-3809)
/** The page to be updated or freed was noted owned by the caller. */
#define VERR_GMM_NOT_PAGE_OWNER                     (-3810)
/** The specified chunk was not found. */
#define VERR_GMM_CHUNK_NOT_FOUND                    (-3811)
/** The chunk has already been mapped into the process. */
#define VERR_GMM_CHUNK_ALREADY_MAPPED               (-3812)
/** The chunk to be unmapped isn't actually mapped into the process. */
#define VERR_GMM_CHUNK_NOT_MAPPED                   (-3813)
/** The chunk has been mapped too many times already (impossible). */
#define VERR_GMM_TOO_MANY_CHUNK_MAPPINGS            (-3814)
/** The reservation or reservation update was declined - too many VMs, too
 * little memory, and/or too low GMM configuration. */
#define VERR_GMM_MEMORY_RESERVATION_DECLINED        (-3815)
/** A GMM sanity check failed. */
#define VERR_GMM_IS_NOT_SANE                        (-3816)
/** Inserting a new chunk failed. */
#define VERR_GMM_CHUNK_INSERT                       (-3817)
/** Failed to obtain the GMM instance. */
#define VERR_GMM_INSTANCE                           (-3818)
/** Bad mutex semaphore flags. */
#define VERR_GMM_MTX_FLAGS                          (-3819)
/** Internal processing error in the page allocator. */
#define VERR_GMM_ALLOC_PAGES_IPE                    (-3820)
/** Invalid page count given to GMMR3FreePagesPerform.  */
#define VERR_GMM_ACTUAL_PAGES_IPE                   (-3821)
/** The shared module name is too long. */
#define VERR_GMM_MODULE_NAME_TOO_LONG               (-3822)
/** The shared module version string is too long. */
#define VERR_GMM_MODULE_VERSION_TOO_LONG            (-3823)
/** The shared module has too many regions. */
#define VERR_GMM_TOO_MANY_REGIONS                   (-3824)
/** The guest has reported too many modules. */
#define VERR_GMM_TOO_MANY_PER_VM_MODULES            (-3825)
/** The guest has reported too many modules. */
#define VERR_GMM_TOO_MANY_GLOBAL_MODULES            (-3826)
/** The shared module is already registered. */
#define VINF_GMM_SHARED_MODULE_ALREADY_REGISTERED   (3827)
/** The shared module clashed address wise with a previously registered
 * module. */
#define VERR_GMM_SHARED_MODULE_ADDRESS_CLASH        (-3828)
/** The shared module was not found. */
#define VERR_GMM_SHARED_MODULE_NOT_FOUND            (-3829)
/** The size of the shared module was out of range. */
#define VERR_GMM_BAD_SHARED_MODULE_SIZE             (-3830)
/** The size of the one or more regions in the shared module was out of
 * range. */
#define VERR_GMM_SHARED_MODULE_BAD_REGIONS_SIZE     (-3831)
/** @} */


/** @name VBox GVM Status Codes
 * @{
 */
/** The GVM is out of VM handle space. */
#define VERR_GVM_TOO_MANY_VMS                       (-3900)
/** The EMT was not blocked at the time of the call.  */
#define VINF_GVM_NOT_BLOCKED                        3901
/** The EMT was not busy running guest code at the time of the call. */
#define VINF_GVM_NOT_BUSY_IN_GC                     3902
/** RTThreadYield was called during a GVMMR0SchedPoll call. */
#define VINF_GVM_YIELDED                            3903
/** @} */


/** @name VBox VMX Status Codes
 * @{
 */
/** VMXON failed; possibly because it was already run before. */
#define VERR_VMX_VMXON_FAILED                       (-4000)
/** Invalid VMCS pointer.
 * (Can be OR'ed with VERR_VMX_INVALID_VMCS_FIELD.) */
#define VERR_VMX_INVALID_VMCS_PTR                   (-4001)
/** Invalid VMCS index or write to read-only element. */
#define VERR_VMX_INVALID_VMCS_FIELD                 (-4002)
/** Reserved for future status code that we wish to OR with
 *  VERR_VMX_INVALID_VMCS_PTR and VERR_VMX_INVALID_VMCS_FIELD. */
#define VERR_VMX_RESERVED                           (-4003)
/** Invalid VMXON pointer. */
#define VERR_VMX_INVALID_VMXON_PTR                  (-4004)
/** Unable to start VM execution. */
#define VERR_VMX_UNABLE_TO_START_VM                 (-4005)
/** Unable to switch due to invalid host state. */
#define VERR_VMX_INVALID_HOST_STATE                 (-4006)
/** VMX CPU extension not available in hardware. */
#define VERR_VMX_NO_VMX                             (-4009)
/** CPU was incorrectly left in VMX root mode; incompatible with VirtualBox */
#define VERR_VMX_IN_VMX_ROOT_MODE                   (-4011)
/** Somebody cleared X86_CR4_VMXE in the CR4 register. */
#define VERR_VMX_X86_CR4_VMXE_CLEARED               (-4012)
/** Failed to enable and lock VT-x features. */
#define VERR_VMX_MSR_LOCKING_FAILED                 (-4013)
/** Unable to switch due to invalid guest state. */
#define VERR_VMX_INVALID_GUEST_STATE                (-4014)
/** Unexpected VM exit. */
#define VERR_VMX_UNEXPECTED_EXIT                    (-4015)
/** Unexpected VM exception. */
#define VERR_VMX_UNEXPECTED_EXCEPTION               (-4016)
/** Unexpected interruption exit type. */
#define VERR_VMX_UNEXPECTED_INTERRUPTION_EXIT_TYPE  (-4017)
/** CPU is not in VMX root mode; unexpected when leaving VMX root mode. */
#define VERR_VMX_NOT_IN_VMX_ROOT_MODE               (-4018)
/** Undefined VM exit code. */
#define VERR_VMX_UNDEFINED_EXIT_CODE                (-4019)
/** VMPTRLD failed; possibly because of invalid VMCS launch-state. */
#define VERR_VMX_VMPTRLD_FAILED                     (-4021)
/** Invalid VMCS pointer passed to VMLAUNCH/VMRESUME. */
#define VERR_VMX_INVALID_VMCS_PTR_TO_START_VM       (-4022)
/** Internal VMX processing error no 1. */
#define VERR_VMX_IPE_1                              (-4023)
/** Internal VMX processing error no 2. */
#define VERR_VMX_IPE_2                              (-4024)
/** Internal VMX processing error no 3. */
#define VERR_VMX_IPE_3                              (-4025)
/** Internal VMX processing error no 4. */
#define VERR_VMX_IPE_4                              (-4026)
/** Internal VMX processing error no 5. */
#define VERR_VMX_IPE_5                              (-4027)
/** VT-x features for all modes (SMX and non-SMX) disabled by the BIOS. */
#define VERR_VMX_MSR_ALL_VMX_DISABLED               (-4028)
/** VT-x features disabled by the BIOS. */
#define VERR_VMX_MSR_VMX_DISABLED                   (-4029)
/** VT-x VMCS field cache invalid. */
#define VERR_VMX_VMCS_FIELD_CACHE_INVALID           (-4030)
/** Failed to set VMXON enable bit while enabling VT-x through the MSR. */
#define VERR_VMX_MSR_VMX_ENABLE_FAILED              (-4031)
/** Failed to enable VMXON-in-SMX bit while enabling VT-x through the MSR. */
#define VERR_VMX_MSR_SMX_VMX_ENABLE_FAILED          (-4032)
/** An operation caused a nested-guest VM-exit. */
#define VINF_VMX_VMEXIT                             4033
/** Generic VM-entry failure. */
#define VERR_VMX_VMENTRY_FAILED                     (-4033)
/** Generic VM-exit failure. */
#define VERR_VMX_VMEXIT_FAILED                      (-4034)
/** The requested nested-guest VMX intercept is not active or not in
 *  nested-guest execution mode. */
#define VINF_VMX_INTERCEPT_NOT_ACTIVE               4035
/** The behavior of the instruction/operation is modified/needs modification
 *  in VMX non-root mode. */
#define VINF_VMX_MODIFIES_BEHAVIOR                  4036
/** VMLAUNCH/VMRESUME succeeded, can enter nested-guest execution. */
#define VINF_VMX_VMLAUNCH_VMRESUME                  4037
/** VT-x VMCS launch state invalid. */
#define VERR_VMX_INVALID_VMCS_LAUNCH_STATE          (-4038)
/** @} */


/** @name VBox SVM Status Codes
 * @{
 */
/** Unable to start VM execution. */
#define VERR_SVM_UNABLE_TO_START_VM                 (-4050)
/** AMD-V bit not set in K6_EFER MSR */
#define VERR_SVM_ILLEGAL_EFER_MSR                   (-4051)
/** AMD-V CPU extension not available. */
#define VERR_SVM_NO_SVM                             (-4052)
/** AMD-V CPU extension disabled (by BIOS). */
#define VERR_SVM_DISABLED                           (-4053)
/** AMD-V CPU extension in-use. */
#define VERR_SVM_IN_USE                             (-4054)
/** Invalid pVMCB. */
#define VERR_SVM_INVALID_PVMCB                      (-4055)
/** Unexpected SVM exit. */
#define VERR_SVM_UNEXPECTED_EXIT                    (-4056)
/** Unexpected SVM exception exit. */
#define VERR_SVM_UNEXPECTED_XCPT_EXIT               (-4057)
/** Unexpected SVM patch type. */
#define VERR_SVM_UNEXPECTED_PATCH_TYPE              (-4058)
/** Unable to start VM execution due to an invalid guest state. */
#define VERR_SVM_INVALID_GUEST_STATE                (-4059)
/** Unknown or unrecognized SVM exit.  */
#define VERR_SVM_UNKNOWN_EXIT                       (-4060)
/** Internal SVM processing error no 1. */
#define VERR_SVM_IPE_1                              (-4061)
/** Internal SVM processing error no 2. */
#define VERR_SVM_IPE_2                              (-4062)
/** Internal SVM processing error no 3. */
#define VERR_SVM_IPE_3                              (-4063)
/** Internal SVM processing error no 4. */
#define VERR_SVM_IPE_4                              (-4064)
/** Internal SVM processing error no 5. */
#define VERR_SVM_IPE_5                              (-4065)
/** The nested-guest \#VMEXIT processing failed, initiate shutdown. */
#define VERR_SVM_VMEXIT_FAILED                      (-4066)
/** An operation caused a nested-guest SVM \#VMEXIT. */
#define VINF_SVM_VMEXIT                             4067
/** VMRUN emulation succeeded, ready to immediately enter the nested-guest. */
#define VINF_SVM_VMRUN                              4068
/** The requested nested-guest SVM intercept is not active or not in
 *  nested-guest execution mode. */
#define VINF_SVM_INTERCEPT_NOT_ACTIVE               4069
/** @} */


/** @name VBox HM Status Codes
 * @{
 */
/** Host is about to go into suspend mode. */
#define VERR_HM_SUSPEND_PENDING                     (-4100)
/** Conflicting CFGM values. */
#define VERR_HM_CONFIG_MISMATCH                     (-4103)
/** Internal processing error in the HM init code. */
#define VERR_HM_ALREADY_ENABLED_IPE                 (-4104)
/** Unexpected MSR in the auto-load/store area.  */
#define VERR_HM_UNEXPECTED_LD_ST_MSR                (-4105)
/** No 32-bit to 64-bit switcher in place. */
#define VERR_HM_NO_32_TO_64_SWITCHER                (-4106)
/** HMR0Leave was called on the wrong CPU. */
#define VERR_HM_WRONG_CPU                           (-4107)
/** Internal processing error \#1 in the HM code.  */
#define VERR_HM_IPE_1                               (-4108)
/** Internal processing error \#2 in the HM code.  */
#define VERR_HM_IPE_2                               (-4109)
/** Wrong 32/64-bit switcher. */
#define VERR_HM_WRONG_SWITCHER                      (-4110)
/** Unknown I/O instruction. */
#define VERR_HM_UNKNOWN_IO_INSTRUCTION              (-4111)
/** Unsupported CPU feature combination. */
#define VERR_HM_UNSUPPORTED_CPU_FEATURE_COMBO       (-4112)
/** Internal processing error \#3 in the HM code.  */
#define VERR_HM_IPE_3                               (-4113)
/** Internal processing error \#4 in the HM code.  */
#define VERR_HM_IPE_4                               (-4114)
/** Internal processing error \#5 in the HM code.  */
#define VERR_HM_IPE_5                               (-4115)
/** Invalid HM64ON32OP value.  */
#define VERR_HM_INVALID_HM64ON32OP                  (-4116)
/** Resume guest execution after injecting a double-fault. */
#define VINF_HM_DOUBLE_FAULT                        4117
/** Pending exception; continue guest execution. */
#define VINF_HM_PENDING_XCPT                        4118
/** @} */


/** @name VBox Disassembler Status Codes
 * @{
 */
/** Invalid opcode byte(s) */
#define VERR_DIS_INVALID_OPCODE                     (-4200)
/** Generic failure during disassembly. */
#define VERR_DIS_GEN_FAILURE                        (-4201)
/** No read callback. */
#define VERR_DIS_NO_READ_CALLBACK                   (-4202)
/** Invalid Mod/RM. */
#define VERR_DIS_INVALID_MODRM                      (-4203)
/** Invalid parameter index. */
#define VERR_DIS_INVALID_PARAMETER                  (-4204)
/** The instruction is too long. */
#define VERR_DIS_TOO_LONG_INSTR                     (-4206)
/** @} */


/** @name VBox Webservice Status Codes
 * @{
 */
/** Authentication failed (ISessionManager::logon()) */
#define VERR_WEB_NOT_AUTHENTICATED                  (-4300)
/** Invalid format of managed object reference */
#define VERR_WEB_INVALID_MANAGED_OBJECT_REFERENCE   (-4301)
/** Invalid session ID in managed object reference */
#define VERR_WEB_INVALID_SESSION_ID                 (-4302)
/** Invalid object ID in managed object reference */
#define VERR_WEB_INVALID_OBJECT_ID                  (-4303)
/** Unsupported interface for managed object reference */
#define VERR_WEB_UNSUPPORTED_INTERFACE              (-4304)
/** @} */


/** @name VBox PARAV Status Codes
 * @{
 */
/** Switch back to host */
#define VINF_PARAV_SWITCH_TO_HOST                   4400

/** @} */

/** @name VBox Video HW Acceleration command status
 * @{
 */
/** command processing is pending, a completion handler will be called */
#define VINF_VHWA_CMD_PENDING                       4500

/** @} */


/** @name VBox COM error codes
 *
 * @remarks Global::vboxStatusCodeToCOM and Global::vboxStatusCodeFromCOM uses
 *          these for conversion that is lossless with respect to important COM
 *          status codes.  These methods should be moved to the glue library.
 * @{  */
/** Unexpected turn of events. */
#define VERR_COM_UNEXPECTED                         (-4600)
/** The base of the VirtualBox COM status codes (the lower value)
 * corresponding 1:1 to VBOX_E_XXX.  This is the lowest value. */
#define VERR_COM_VBOX_LOWEST                        (-4699)
/** Object corresponding to the supplied arguments does not exist. */
#define VERR_COM_OBJECT_NOT_FOUND                   (VERR_COM_VBOX_LOWEST + 1)
/** Current virtual machine state prevents the operation. */
#define VERR_COM_INVALID_VM_STATE                   (VERR_COM_VBOX_LOWEST + 2)
/** Virtual machine error occurred attempting the operation. */
#define VERR_COM_VM_ERROR                           (VERR_COM_VBOX_LOWEST + 3)
/** File not accessible or erroneous file contents. */
#define VERR_COM_FILE_ERROR                         (VERR_COM_VBOX_LOWEST + 4)
/** IPRT error. */
#define VERR_COM_IPRT_ERROR                         (VERR_COM_VBOX_LOWEST + 5)
/** Pluggable Device Manager error. */
#define VERR_COM_PDM_ERROR                          (VERR_COM_VBOX_LOWEST + 6)
/** Current object state prohibits operation. */
#define VERR_COM_INVALID_OBJECT_STATE               (VERR_COM_VBOX_LOWEST + 7)
/** Host operating system related error. */
#define VERR_COM_HOST_ERROR                         (VERR_COM_VBOX_LOWEST + 8)
/** Requested operation is not supported. */
#define VERR_COM_NOT_SUPPORTED                      (VERR_COM_VBOX_LOWEST + 9)
/** Invalid XML found. */
#define VERR_COM_XML_ERROR                          (VERR_COM_VBOX_LOWEST + 10)
/** Current session state prohibits operation. */
#define VERR_COM_INVALID_SESSION_STATE              (VERR_COM_VBOX_LOWEST + 11)
/** Object being in use prohibits operation. */
#define VERR_COM_OBJECT_IN_USE                      (VERR_COM_VBOX_LOWEST + 12)
/** Returned by callback methods which does not need to be called
 * again because the client does not actually make use of them. */
#define VERR_COM_DONT_CALL_AGAIN                    (VERR_COM_VBOX_LOWEST + 13)
/** @} */

/** @name VBox VMMDev Status codes
 * @{
 */
/** CPU hotplug events from VMMDev are not monitored by the guest. */
#define VERR_VMMDEV_CPU_HOTPLUG_NOT_MONITORED_BY_GUEST      (-4700)
/** @} */

/** @name VBox async I/O manager Status Codes
 * @{
 */
/** Async I/O task is pending, a completion handler will be called. */
#define VINF_AIO_TASK_PENDING                       4800
/** @} */

/** @name VBox Virtual SCSI Status Codes
 * @{
 */
/** LUN type is not supported. */
#define VERR_VSCSI_LUN_TYPE_NOT_SUPPORTED           (-4900)
/** LUN is already/still attached to a device. */
#define VERR_VSCSI_LUN_ATTACHED_TO_DEVICE           (-4901)
/** The specified LUN is invalid. */
#define VERR_VSCSI_LUN_INVALID                      (-4902)
/** The LUN is not attached to the device. */
#define VERR_VSCSI_LUN_NOT_ATTACHED                 (-4903)
/** The LUN is still busy. */
#define VERR_VSCSI_LUN_BUSY                         (-4904)
/** @} */

/** @name VBox FAM Status Codes
 * @{
 */
/** FAM failed to open a connection. */
#define VERR_FAM_OPEN_FAILED                        (-5000)
/** FAM failed to add a file to the list to be monitored. */
#define VERR_FAM_MONITOR_FILE_FAILED                (-5001)
/** FAM failed to add a directory to the list to be monitored. */
#define VERR_FAM_MONITOR_DIRECTORY_FAILED           (-5002)
/** The connection to the FAM daemon was lost. */
#define VERR_FAM_CONNECTION_LOST                    (-5003)
/** @} */


/** @name PCI Passtrhough Status Codes
 * @{
 */
/** RamPreAlloc not set.
 * RAM pre-allocation is currently a requirement for PCI passthrough. */
#define VERR_PCI_PASSTHROUGH_NO_RAM_PREALLOC        (-5100)
/** VT-x/AMD-V not active.
 * PCI passthrough currently works only if VT-x/AMD-V is active. */
#define VERR_PCI_PASSTHROUGH_NO_HM              (-5101)
/** Nested paging not active.
 * PCI passthrough currently works only if nested paging is active. */
#define VERR_PCI_PASSTHROUGH_NO_NESTED_PAGING       (-5102)
/** @} */


/** @name GVMM Status Codes
 * @{
 */
/** Internal error obtaining the GVMM instance. */
#define VERR_GVMM_INSTANCE                          (-5200)
/** GVMM does not support the range of CPUs present/possible on the host. */
#define VERR_GVMM_HOST_CPU_RANGE                    (-5201)
/** GVMM ran into some broken IPRT code. */
#define VERR_GVMM_BROKEN_IPRT                       (-5202)
/** Internal processing error \#1 in the GVMM code. */
#define VERR_GVMM_IPE_1                             (-5203)
/** Internal processing error \#2 in the GVMM code. */
#define VERR_GVMM_IPE_2                             (-5204)
/** Cannot destroy VM because not all other EMTs have deregistered. */
#define VERR_GVMM_NOT_ALL_EMTS_DEREGISTERED         (-5205)
/** @} */


/** @name IEM Status Codes
 * @{ */
/** The instruction is not yet implemented by IEM. */
#define VERR_IEM_INSTR_NOT_IMPLEMENTED              (-5300)
/** Invalid operand size passed to an IEM function. */
#define VERR_IEM_INVALID_OPERAND_SIZE               (-5301)
/** Invalid address mode passed to an IEM function. */
#define VERR_IEM_INVALID_ADDRESS_MODE               (-5302)
/** Invalid effective segment register number passed to an IEM function. */
#define VERR_IEM_INVALID_EFF_SEG                    (-5303)
/** Invalid instruction length passed to an IEM function. */
#define VERR_IEM_INVALID_INSTR_LENGTH               (-5304)
/** Internal status code for indicating that a selector isn't valid (LAR, LSL,
 *  VERR, VERW).  This is not used outside the instruction implementations. */
#define VINF_IEM_SELECTOR_NOT_OK                    (5305)
/** Restart the current instruction. For testing only. */
#define VERR_IEM_RESTART_INSTRUCTION                (-5389)
/** This particular aspect of the instruction is not yet implemented by IEM. */
#define VERR_IEM_ASPECT_NOT_IMPLEMENTED             (-5390)
/** Internal processing error \#1 in the IEM code. */
#define VERR_IEM_IPE_1                              (-5391)
/** Internal processing error \#2 in the IEM code. */
#define VERR_IEM_IPE_2                              (-5392)
/** Internal processing error \#3 in the IEM code. */
#define VERR_IEM_IPE_3                              (-5393)
/** Internal processing error \#4 in the IEM code. */
#define VERR_IEM_IPE_4                              (-5394)
/** Internal processing error \#5 in the IEM code. */
#define VERR_IEM_IPE_5                              (-5395)
/** Internal processing error \#6 in the IEM code. */
#define VERR_IEM_IPE_6                              (-5396)
/** Internal processing error \#7 in the IEM code. */
#define VERR_IEM_IPE_7                              (-5397)
/** Internal processing error \#8 in the IEM code. */
#define VERR_IEM_IPE_8                              (-5398)
/** Internal processing error \#9 in the IEM code. */
#define VERR_IEM_IPE_9                              (-5399)
/** @} */


/** @name DBGC Status Codes
 *  @{ */
/** Status that causes DBGC to quit. */
#define VERR_DBGC_QUIT                              (-5400)
/** Async command pending. */
#define VWRN_DBGC_CMD_PENDING                       5401
/** The command has already been registered. */
#define VWRN_DBGC_ALREADY_REGISTERED                5402
/** The command cannot be deregistered because has not been registered.  */
#define VERR_DBGC_COMMANDS_NOT_REGISTERED           (-5403)
/** Unknown breakpoint.  */
#define VERR_DBGC_BP_NOT_FOUND                      (-5404)
/** The breakpoint already exists. */
#define VERR_DBGC_BP_EXISTS                         (-5405)
/** The breakpoint has no command. */
#define VINF_DBGC_BP_NO_COMMAND                     5406
/** Generic debugger command failure. */
#define VERR_DBGC_COMMAND_FAILED                    (-5407)
/** Logic bug in the DBGC code. */
#define VERR_DBGC_IPE                               (-5408)

/** The lowest parse status code.   */
#define VERR_DBGC_PARSE_LOWEST                      (-5499)
/** Syntax error - too few arguments. */
#define VERR_DBGC_PARSE_TOO_FEW_ARGUMENTS           (VERR_DBGC_PARSE_LOWEST + 0)
/** Syntax error - too many arguments. */
#define VERR_DBGC_PARSE_TOO_MANY_ARGUMENTS          (VERR_DBGC_PARSE_LOWEST + 1)
/** Syntax error - too many arguments for static storage. */
#define VERR_DBGC_PARSE_ARGUMENT_OVERFLOW           (VERR_DBGC_PARSE_LOWEST + 2)
/** Syntax error - expected binary operator. */
#define VERR_DBGC_PARSE_EXPECTED_BINARY_OP          (VERR_DBGC_PARSE_LOWEST + 3)

/** Syntax error - the argument does not allow a range to be specified. */
#define VERR_DBGC_PARSE_NO_RANGE_ALLOWED            (VERR_DBGC_PARSE_LOWEST + 5)
/** Syntax error - unbalanced quotes. */
#define VERR_DBGC_PARSE_UNBALANCED_QUOTE            (VERR_DBGC_PARSE_LOWEST + 6)
/** Syntax error - unbalanced parenthesis. */
#define VERR_DBGC_PARSE_UNBALANCED_PARENTHESIS      (VERR_DBGC_PARSE_LOWEST + 7)
/** Syntax error - an argument or subargument contains nothing useful. */
#define VERR_DBGC_PARSE_EMPTY_ARGUMENT              (VERR_DBGC_PARSE_LOWEST + 8)
/** Syntax error - invalid operator usage. */
#define VERR_DBGC_PARSE_UNEXPECTED_OPERATOR         (VERR_DBGC_PARSE_LOWEST + 9)
/** Syntax error - invalid numeric value. */
#define VERR_DBGC_PARSE_INVALID_NUMBER              (VERR_DBGC_PARSE_LOWEST + 10)
/** Syntax error - numeric overflow. */
#define VERR_DBGC_PARSE_NUMBER_TOO_BIG              (VERR_DBGC_PARSE_LOWEST + 11)
/** Syntax error - invalid operation attempted. */
#define VERR_DBGC_PARSE_INVALID_OPERATION           (VERR_DBGC_PARSE_LOWEST + 12)
/** Syntax error - function not found. */
#define VERR_DBGC_PARSE_FUNCTION_NOT_FOUND          (VERR_DBGC_PARSE_LOWEST + 13)
/** Syntax error - the specified function is not a function. */
#define VERR_DBGC_PARSE_NOT_A_FUNCTION              (VERR_DBGC_PARSE_LOWEST + 14)
/** Syntax error - out of scratch memory. */
#define VERR_DBGC_PARSE_NO_SCRATCH                  (VERR_DBGC_PARSE_LOWEST + 15)
/** Syntax error - out of regular heap memory. */
#define VERR_DBGC_PARSE_NO_MEMORY                   (VERR_DBGC_PARSE_LOWEST + 16)
/** Syntax error - incorrect argument type. */
#define VERR_DBGC_PARSE_INCORRECT_ARG_TYPE          (VERR_DBGC_PARSE_LOWEST + 17)
/** Syntax error - an undefined variable was referenced. */
#define VERR_DBGC_PARSE_VARIABLE_NOT_FOUND          (VERR_DBGC_PARSE_LOWEST + 18)
/** Syntax error - a type conversion failed. */
#define VERR_DBGC_PARSE_CONVERSION_FAILED           (VERR_DBGC_PARSE_LOWEST + 19)
/** Syntax error - you hit a debugger feature which isn't implemented yet.
 * (Feel free to help implement it.) */
#define VERR_DBGC_PARSE_NOT_IMPLEMENTED             (VERR_DBGC_PARSE_LOWEST + 20)
/** Syntax error - Couldn't satisfy a request for a specific result type. */
#define VERR_DBGC_PARSE_BAD_RESULT_TYPE             (VERR_DBGC_PARSE_LOWEST + 21)
/** Syntax error - Cannot read symbol value, it is a set-only symbol. */
#define VERR_DBGC_PARSE_WRITEONLY_SYMBOL            (VERR_DBGC_PARSE_LOWEST + 22)
/** Syntax error - Invalid command name. */
#define VERR_DBGC_PARSE_INVALD_COMMAND_NAME         (VERR_DBGC_PARSE_LOWEST + 23)
/** Syntax error - Command not found. */
#define VERR_DBGC_PARSE_COMMAND_NOT_FOUND           (VERR_DBGC_PARSE_LOWEST + 24)
/** Syntax error - buggy parser. */
#define VERR_DBGC_PARSE_BUG                         (VERR_DBGC_PARSE_LOWEST + 25)
/** @} */


/** @name Support driver/library shared verification status codes.
 * @{  */
/** Process Verification Failure: The memory content does not match the image
 *  file. */
#define VERR_SUP_VP_MEMORY_VS_FILE_MISMATCH          (-5600)
/** Process Verification Failure: The memory protection of a image file section
 *  does not match what the section header prescribes. */
#define VERR_SUP_VP_SECTION_PROTECTION_MISMATCH      (-5601)
/** Process Verification Failure: One of the section in the image file is not
 *  mapped into memory. */
#define VERR_SUP_VP_SECTION_NOT_MAPPED               (-5602)
/** Process Verification Failure: One of the section in the image file is not
 *  fully mapped into memory. */
#define VERR_SUP_VP_SECTION_NOT_FULLY_MAPPED         (-5603)
/** Process Verification Failure: Bad file alignment value in image header. */
#define VERR_SUP_VP_BAD_FILE_ALIGNMENT_VALUE         (-5604)
/** Process Verification Failure: Bad image base in header. */
#define VERR_SUP_VP_BAD_IMAGE_BASE                   (-5605)
/** Process Verification Failure: Bad image signature. */
#define VERR_SUP_VP_BAD_IMAGE_SIGNATURE              (-5606)
/** Process Verification Failure: Bad image size. */
#define VERR_SUP_VP_BAD_IMAGE_SIZE                   (-5607)
/** Process Verification Failure: Bad new-header offset in the MZ header. */
#define VERR_SUP_VP_BAD_MZ_OFFSET                    (-5608)
/** Process Verification Failure: Bad optional header field. */
#define VERR_SUP_VP_BAD_OPTIONAL_HEADER              (-5609)
/** Process Verification Failure: Bad section alignment value in image
 *  header. */
#define VERR_SUP_VP_BAD_SECTION_ALIGNMENT_VALUE      (-5610)
/** Process Verification Failure: Bad section raw data size. */
#define VERR_SUP_VP_BAD_SECTION_FILE_SIZE            (-5611)
/** Process Verification Failure: Bad virtual section address. */
#define VERR_SUP_VP_BAD_SECTION_RVA                  (-5612)
/** Process Verification Failure: Bad virtual section size. */
#define VERR_SUP_VP_BAD_SECTION_VIRTUAL_SIZE         (-5613)
/** Process Verification Failure: Bad size of image header. */
#define VERR_SUP_VP_BAD_SIZE_OF_HEADERS              (-5614)
/** Process Verification Failure: The process is being debugged. */
#define VERR_SUP_VP_DEBUGGED                         (-5615)
/** Process Verification Failure: A DLL was found more than once. */
#define VERR_SUP_VP_DUPLICATE_DLL_MAPPING            (-5616)
/** Process Verification Failure: Image section region is too large. */
#define VERR_SUP_VP_EMPTY_REGION_TOO_LARGE           (-5617)
/** Process Verification Failure: Executable file name and process image name
 *  does not match up. */
#define VERR_SUP_VP_EXE_VS_PROC_NAME_MISMATCH        (-5618)
/** Process Verification Failure: Found executable memory allocated in the
 *  process.  There is only supposed be executable memory associated with
 *  image file mappings (DLLs & EXE). */
#define VERR_SUP_VP_FOUND_EXEC_MEMORY                (-5619)
/** Process Verification Failure: There is more than one known executable mapped
 *  into the process. */
#define VERR_SUP_VP_FOUND_MORE_THAN_ONE_EXE_MAPPING  (-5620)
/** Process Verification Failure: Error closing image file handle. */
#define VERR_SUP_VP_IMAGE_FILE_CLOSE_ERROR           (-5621)
/** Process Verification Failure: Error opening image file. */
#define VERR_SUP_VP_IMAGE_FILE_OPEN_ERROR            (-5622)
/** Process Verification Failure: Error reading image file header. */
#define VERR_SUP_VP_IMAGE_HDR_READ_ERROR             (-5623)
/** Process Verification Failure: Image mapping is bogus as the first region
 *  has different AllocationBase and BaseAddress values, indicating that a
 *  section was unmapped or otherwise tampered with. */
#define VERR_SUP_VP_IMAGE_MAPPING_BASE_ERROR         (-5624)
/** Process Verification Failure: Error reading process memory for comparing
 *  with disk data. */
#define VERR_SUP_VP_MEMORY_READ_ERROR                (-5625)
/** Process Verification Failure: Found no executable mapped into the process
 *  address space. */
#define VERR_SUP_VP_NO_FOUND_NO_EXE_MAPPING          (-5626)
/** Process Verification Failure: An image mapping failed to report a name. */
#define VERR_SUP_VP_NO_IMAGE_MAPPING_NAME            (-5627)
/** Process Verification Failure: No KERNE32.DLL mapping found.  This is
 *  impossible. */
#define VERR_SUP_VP_NO_KERNEL32_MAPPING              (-5628)
/** Process Verification Failure: Error allocating memory. */
#define VERR_SUP_VP_NO_MEMORY                        (-5629)
/** Process Verification Failure: Error allocating state memory or querying
 *  the system32 path. */
#define VERR_SUP_VP_NO_MEMORY_STATE                  (-5630)
/** Process Verification Failure: No NTDLL.DLL mapping found.  This is
 *  impossible. */
#define VERR_SUP_VP_NO_NTDLL_MAPPING                 (-5631)
/** Process Verification Failure: A DLL residing outside System32 was found
 *  in the process. */
#define VERR_SUP_VP_NON_SYSTEM32_DLL                 (-5632)
/** Process Verification Failure: An unknown and unwanted DLL was found loaded
 *  into the process. */
#define VERR_SUP_VP_NOT_KNOWN_DLL_OR_EXE             (-5633)
/** Process Verification Failure: The name of an image file changes between
 *  mapping regions. */
#define VERR_SUP_VP_NT_MAPPING_NAME_CHANGED          (-5634)
/** Process Verification Failure: Error querying process name. */
#define VERR_SUP_VP_NT_QI_PROCESS_NM_ERROR           (-5635)
/** Process Verification Failure: Error querying thread information. */
#define VERR_SUP_VP_NT_QI_THREAD_ERROR               (-5636)
/** Process Verification Failure: Error query virtual memory information. */
#define VERR_SUP_VP_NT_QI_VIRTUAL_MEMORY_ERROR       (-5637)
/** Process Verification Failure: Error query virtual memory mapping name. */
#define VERR_SUP_VP_NT_QI_VIRTUAL_MEMORY_NM_ERROR    (-5638)
/** Process Verification Failure: Error determining the full path of
 *  System32. */
#define VERR_SUP_VP_SYSTEM32_PATH                    (-5639)
/** Process Verification Failure: The process has more than one thread. */
#define VERR_SUP_VP_THREAD_NOT_ALONE                 (-5640)
/** Process Verification Failure: The image mapping is too large (>= 2GB). */
#define VERR_SUP_VP_TOO_HIGH_REGION_RVA              (-5641)
/** Process Verification Failure: The memory region is too large (>= 2GB). */
#define VERR_SUP_VP_TOO_LARGE_REGION                 (-5642)
/** Process Verification Failure: There are too many DLLs loaded. */
#define VERR_SUP_VP_TOO_MANY_DLLS_LOADED             (-5643)
/** Process Verification Failure: An image has too many regions. */
#define VERR_SUP_VP_TOO_MANY_IMAGE_REGIONS           (-5644)
/** Process Verification Failure: The process has too many virtual memory
 *  regions. */
#define VERR_SUP_VP_TOO_MANY_MEMORY_REGIONS          (-5645)
/** Process Verification Failure: An image has too many sections. */
#define VERR_SUP_VP_TOO_MANY_SECTIONS                (-5646)
/** Process Verification Failure: An image is targeting an unexpected
 *  machine/CPU. */
#define VERR_SUP_VP_UNEXPECTED_IMAGE_MACHINE         (-5647)
/** Process Verification Failure: Unexpected section protection flag
 *  combination. */
#define VERR_SUP_VP_UNEXPECTED_SECTION_FLAGS         (-5648)
/** Process Verification Failure: Expected the process and exe to have forced
 * integrity checking enabled (verifying signatures). */
#define VERR_SUP_VP_EXE_MISSING_FORCE_INTEGRITY     (-5649)
/** Process Verification Failure: Expected the process and exe to have dynamic
 * base enabled. */
#define VERR_SUP_VP_EXE_MISSING_DYNAMIC_BASE        (-5650)
/** Process Verification Failure: Expected the process and exe to advertise
 * NX compatibility. */
#define VERR_SUP_VP_EXE_MISSING_NX_COMPAT           (-5651)
/** Process Verification Failure: The DllCharacteristics of the process
 * does not match the value in the optional header in the exe file. */
#define VERR_SUP_VP_DLL_CHARECTERISTICS_MISMATCH    (-5652)
/** Process Verification Failure: The ImageCharacteristics of the process
 * does not match the value in the file header in the exe file. */
#define VERR_SUP_VP_IMAGE_CHARECTERISTICS_MISMATCH  (-5653)
/** Process Verification Failure: Error querying image information. */
#define VERR_SUP_VP_NT_QI_PROCESS_IMG_INFO_ERROR    (-5654)
/** Process Verification Failure: Error querying debug port. */
#define VERR_SUP_VP_NT_QI_PROCESS_DBG_PORT_ERROR    (-5655)
/** WinVerifyTrust failed with an unexpected status code when using the
 * catalog-file approach. */
#define VERR_SUP_VP_WINTRUST_CAT_FAILURE            (-5656)
/** The image is required to be signed with the same certificate as the rest
 * of VirtualBox. */
#define VERR_SUP_VP_NOT_SIGNED_WITH_BUILD_CERT      (-5657)
/** Internal processing error: Not build certificate. */
#define VERR_SUP_VP_NOT_BUILD_CERT_IPE              (-5658)
/** The image requires to be signed using the kernel-code signing process. */
#define VERR_SUP_VP_NOT_VALID_KERNEL_CODE_SIGNATURE (-5659)
/** Unexpected number of valid paths. */
#define VERR_SUP_VP_UNEXPECTED_VALID_PATH_COUNT     (-5660)
/** The image is required to force integrity checks. */
#define VERR_SUP_VP_SIGNATURE_CHECKS_NOT_ENFORCED   (-5661)
/** Process Verification Failure: Symantec Endpoint Protection must be
 * disabled for the VirtualBox VM processes.
 * http://www.symantec.com/connect/articles/creating-application-control-exclusions-symantec-endpoint-protection-121 */
#define VERR_SUP_VP_SYSFER_DLL                      (-5662)
/** Process Purification Failure: KERNE32.DLL already mapped into the initial
 *  process (suspended). */
#define VERR_SUP_VP_KERNEL32_ALREADY_MAPPED         (-5663)
/** Process Purification Failure: NtFreeVirtualMemory failed on a chunk of
 *  executable memory which shouldn't be present in the process. */
#define VERR_SUP_VP_FREE_VIRTUAL_MEMORY_FAILED      (-5664)
/** Process Purification Failure: Both NtUnmapViewOfSetion and
 *  NtProtectVirtualMemory failed to get rid of or passify an non-image
 *  executable mapping. */
#define VERR_SUP_VP_UNMAP_AND_PROTECT_FAILED        (-5665)
/** Process Purification Failure: Unknown memory type of executable memory.   */
#define VERR_SUP_VP_UNKOWN_MEM_TYPE                 (-5666)
/** The image file is not owned by TrustedInstaller is it should be. */
#define VERR_SUP_VP_NOT_OWNED_BY_TRUSTED_INSTALLER  (-5667)
/** The image is outside the expected range. */
#define VERR_SUP_VP_IMAGE_TOO_BIG                   (-5668)
/** Stub process not found so it cannot be revalidated when vboxdrv is opened
 * by the VM process. */
#define VERR_SUP_VP_STUB_NOT_FOUND                  (-5669)
/** Error opening the stub process for revalidation when vboxdrv is opened by
 *  the VM process. */
#define VERR_SUP_VP_STUB_OPEN_ERROR                 (-5670)
/** Stub process thread not found during revalidation upon vboxdrv opening by
 * the VM process. */
#define VERR_SUP_VP_STUB_THREAD_NOT_FOUND           (-5671)
/** Error opening the stub process thread for revalidation when vboxdrv is
 * opened by the VM process. */
#define VERR_SUP_VP_STUB_THREAD_OPEN_ERROR          (-5672)
/** Process Purification Failure: NtAllocateVirtualMemory failed to get us
 * suitable replacement memory for a chunk of executable memory that
 * shouldn't be present in our process.  (You will only see this message if you
 * got potentially fatally buggy anti-virus software installed.) */
#define VERR_SUP_VP_REPLACE_VIRTUAL_MEMORY_FAILED   (-5673)
/** Error getting the file mode. */
#define VERR_SUP_VP_FILE_MODE_ERROR                 (-5674)
/** Error creating an event semaphore for used with asynchronous reads. */
#define VERR_SUP_VP_CREATE_READ_EVT_SEM_FAILED      (-5675)
/** Undesirable module. */
#define VERR_SUP_VP_UNDESIRABLE_MODULE              (-5676)

/** @} */

/** @name VBox Extension Pack Status Codes
 * @{
 */
/** The host is not supported. Uninstall the extension pack.
 * Returned by the VBOXEXTPACKREG::pfnInstalled. */
#define VERR_EXTPACK_UNSUPPORTED_HOST_UNINSTALL     (-6000)
/** The VirtualBox version is not supported by one of the extension packs.
 *
 * You have probably upgraded VirtualBox recently.  Please upgrade the
 * extension packs to versions compatible with this VirtualBox release.
 */
#define VERR_EXTPACK_VBOX_VERSION_MISMATCH          (-6001)
/** @} */


/** @name VBox Guest Control Status Codes
 * @{
 */
/** Guest side reported an error. */
#define VERR_GSTCTL_GUEST_ERROR                     (-6200)
/** A guest control object has changed its overall status. */
#define VWRN_GSTCTL_OBJECTSTATE_CHANGED             6220
/** Guest process is in a wrong state. */
#define VERR_GSTCTL_PROCESS_WRONG_STATE             (-6221)
/** Maximum (context ID) sessions have been reached. */
#define VERR_GSTCTL_MAX_CID_SESSIONS_REACHED        (-6222)
/** Maximum (context ID) objects have been reached. */
#define VERR_GSTCTL_MAX_CID_OBJECTS_REACHED         (-6223)
/** Maximum (context ID object) count has been reached. */
#define VERR_GSTCTL_MAX_CID_COUNT_REACHED           (-6224)
/** Started guest process terminated with an exit code <> 0. */
#define VERR_GSTCTL_PROCESS_EXIT_CODE               (-6225)
/** @} */


/** @name GIM Status Codes
 * @{
 */
/** No GIM provider is configured for this VM. */
#define VERR_GIM_NOT_ENABLED                        (-6300)
/** GIM internal processing error \#1. */
#define VERR_GIM_IPE_1                              (-6301)
/** GIM internal processing error \#2. */
#define VERR_GIM_IPE_2                              (-6302)
/** GIM internal processing error \#3. */
#define VERR_GIM_IPE_3                              (-6303)
/** The GIM provider does not support any paravirtualized TSC. */
#define VERR_GIM_PVTSC_NOT_AVAILABLE                (-6304)
/** The guest has not setup use of the paravirtualized TSC. */
#define VERR_GIM_PVTSC_NOT_ENABLED                  (-6305)
/** Unknown or invalid GIM provider. */
#define VERR_GIM_INVALID_PROVIDER                   (-6306)
/** GIM generic operation failed. */
#define VERR_GIM_OPERATION_FAILED                   (-6307)
/** The GIM provider does not support any hypercalls. */
#define VERR_GIM_HYPERCALLS_NOT_AVAILABLE           (-6308)
/** The guest has not setup use of the hypercalls. */
#define VERR_GIM_HYPERCALLS_NOT_ENABLED             (-6309)
/** The GIM device is not registered with GIM when it ought to be. */
#define VERR_GIM_DEVICE_NOT_REGISTERED              (-6310)
/** Hypercall cannot be enabled/performed due to access/permissions/CPL. */
#define VERR_GIM_HYPERCALL_ACCESS_DENIED            (-6311)
/** Failed to read to a memory region while performing a hypercall. */
#define VERR_GIM_HYPERCALL_MEMORY_READ_FAILED       (-6312)
/** Failed to write to a memory region while performing a hypercall. */
#define VERR_GIM_HYPERCALL_MEMORY_WRITE_FAILED      (-6313)
/** Generic hypercall operation failure. */
#define VERR_GIM_HYPERCALL_FAILED                   (-6314)
/** No debug connection configured. */
#define VERR_GIM_NO_DEBUG_CONNECTION                (-6315)
/** Return to ring-3 to perform the hypercall there. */
#define VINF_GIM_R3_HYPERCALL                       6316
/** Continuing hypercall at the same RIP, continue guest execution. */
#define VINF_GIM_HYPERCALL_CONTINUING               6317
/** Instruction that triggers the hypercall is invalid/unrecognized. */
#define VERR_GIM_INVALID_HYPERCALL_INSTR            (-6318)
/** @} */


/** @name Main API Status Codes
 * @{
 */
/** The configuration constructor in main failed due to a COM error.  Check
 * the release log of the VM for further details. */
#define VERR_MAIN_CONFIG_CONSTRUCTOR_COM_ERROR      (-6400)
/** The configuration constructor in main failed due to an internal consistency
 *  error. Consult the release log of the VM for further details. */
#define VERR_MAIN_CONFIG_CONSTRUCTOR_IPE            (-6401)
/** @} */


/** @name VBox Drag and Drop Status Codes
 * @{
 */
/** Guest side reported an error. */
#define VERR_GSTDND_GUEST_ERROR                     (-6500)
/** @} */


/** @name Audio Status Codes
 * @{
 */
/** Host backend couldn't be initialized.  Happen if the audio server is not
 *  reachable, audio hardware is not available or similar.  We should use the
 *  NULL audio driver. */
#define VERR_AUDIO_BACKEND_INIT_FAILED              (-6600)
/** No host backend attached / available. */
#define VERR_AUDIO_BACKEND_NOT_ATTACHED             (-6601)
/** No free input streams.  */
#define VERR_AUDIO_NO_FREE_INPUT_STREAMS            (-6602)
/** No free output streams.  */
#define VERR_AUDIO_NO_FREE_OUTPUT_STREAMS           (-6603)
/** Pending stream disable operation in progress.  */
#define VERR_AUDIO_STREAM_PENDING_DISABLE           (-6604)
/** There is more data available.
 *  This can happen due to a buffer wraparound of a buffer read/write operation. */
#define VINF_AUDIO_MORE_DATA_AVAILABLE              (6605)
/** Stream is not ready for requested operation.  */
#define VERR_AUDIO_STREAM_NOT_READY                 (-6605)
/** Stream could not be created.
 *  This might due to missing host (backend) drivers or a host not having the
 *  required hardware, or that the requested stream configuration
 *  is not supported by the host backend. */
#define VERR_AUDIO_STREAM_COULD_NOT_CREATE          (-6606)
/** @} */


/** @name APIC Status Codes
 * @{
 */
/** No pending interrupt. */
#define VERR_APIC_INTR_NOT_PENDING                  (-6700)
/** Pending interrupt is masked by TPR. */
#define VERR_APIC_INTR_MASKED_BY_TPR                (-6701)
/** APIC did not accept the interrupt. */
#define VERR_APIC_INTR_DISCARDED                    (-6702)
/** @} */

/** @name NEM Status Codes
 * @{
 */
/** NEM is not enabled. */
#define VERR_NEM_NOT_ENABLED                        (-6800)
/** NEM is not available. */
#define VERR_NEM_NOT_AVAILABLE                      (-6801)
/** NEM init failed. */
#define VERR_NEM_INIT_FAILED                        (-6802)
/** NEM init failed because of missing kernel API. */
#define VERR_NEM_MISSING_KERNEL_API                 (-6803)
/** NEM can only operate from ring-3. */
#define VERR_NEM_RING3_ONLY                         (-6804)
/** NEM failed to create a native VM instance. */
#define VERR_NEM_VM_CREATE_FAILED                   (-6805)
/** NEM failed to map page(s) into the VM. */
#define VERR_NEM_MAP_PAGES_FAILED                   (-6806)
/** NEM failed to unmap page(s) into the VM. */
#define VERR_NEM_UNMAP_PAGES_FAILED                 (-6807)
/** NEM failed to get registers. */
#define VERR_NEM_GET_REGISTERS_FAILED               (-6808)
/** NEM failed to set registers. */
#define VERR_NEM_SET_REGISTERS_FAILED               (-6809)
/** Get register caller must flush the TLB (not an error). */
#define VERR_NEM_FLUSH_TLB                          (-6810)
/** Get register caller must flush the TLB. */
#define VINF_NEM_FLUSH_TLB                          (6810)
/** NEM failed to set TSC. */
#define VERR_NEM_SET_TSC                            (-6811)

/** NEM internal processing error \#0. */
#define VERR_NEM_IPE_0                              (-6890)
/** NEM internal processing error \#1. */
#define VERR_NEM_IPE_1                              (-6891)
/** NEM internal processing error \#2. */
#define VERR_NEM_IPE_2                              (-6892)
/** NEM internal processing error \#3. */
#define VERR_NEM_IPE_3                              (-6893)
/** NEM internal processing error \#4. */
#define VERR_NEM_IPE_4                              (-6894)
/** NEM internal processing error \#5. */
#define VERR_NEM_IPE_5                              (-6895)
/** NEM internal processing error \#6. */
#define VERR_NEM_IPE_6                              (-6896)
/** NEM internal processing error \#7. */
#define VERR_NEM_IPE_7                              (-6897)
/** NEM internal processing error \#8. */
#define VERR_NEM_IPE_8                              (-6898)
/** NEM internal processing error \#9. */
#define VERR_NEM_IPE_9                              (-6899)
/** @} */

/** @name Recording Status Codes
 * @{
 */
/** Codec was not found. */
#define VERR_RECORDING_CODEC_NOT_FOUND              (-6900)
/** Codec initialization failed. */
#define VERR_RECORDING_CODEC_INIT_FAILED            (-6902)
/** Codec is not supported. */
#define VERR_RECORDING_CODEC_NOT_SUPPORTED          (-6903)
/** Format not supported by the codec. */
#define VERR_RECORDING_FORMAT_NOT_SUPPORTED         (-6904)
/** Recording is not possible due to a set restriction. */
#define VERR_RECORDING_RESTRICTED                   (-6905)
/** Recording limit (time, size, ...) has been reached. */
#define VINF_RECORDING_LIMIT_REACHED                (6906)
/** Recording limit (time, size, ...) has been reached. */
#define VERR_RECORDING_LIMIT_REACHED                (-6906)
/** Recording has been throttled due to current settings.
 *  This e.g. can happen when submitting more video frames than
 *  the current FPS setting allows. */
#define VINF_RECORDING_THROTTLED                    (6907)
/** Recording has been throttled due to current settings.
 *  This e.g. can happen when submitting more video frames than
 *  the current FPS setting allows. */
#define VERR_RECORDING_THROTTLED                    (-6907)
/** @} */
/* SED-END */

/** @} */


#endif /* !VBOX_INCLUDED_err_h */

