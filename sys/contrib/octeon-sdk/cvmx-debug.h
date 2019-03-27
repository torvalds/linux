/***********************license start***************
 * Copyright (c) 2003-2010  Cavium Inc. (support@cavium.com). All rights
 * reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.

 *   * Neither the name of Cavium Inc. nor the names of
 *     its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written
 *     permission.

 * This Software, including technical data, may be subject to U.S. export  control
 * laws, including the U.S. Export Administration Act and its  associated
 * regulations, and may be subject to export or import  regulations in other
 * countries.

 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
 * AND WITH ALL FAULTS AND CAVIUM INC. MAKES NO PROMISES, REPRESENTATIONS OR
 * WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH RESPECT TO
 * THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY REPRESENTATION OR
 * DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT DEFECTS, AND CAVIUM
 * SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY) WARRANTIES OF TITLE,
 * MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF
 * VIRUSES, ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR
 * CORRESPONDENCE TO DESCRIPTION. THE ENTIRE  RISK ARISING OUT OF USE OR
 * PERFORMANCE OF THE SOFTWARE LIES WITH YOU.
 ***********************license end**************************************/


/**
 * @file
 *
 * Interface to debug exception handler
 *
 * <hr>$Revision:  $<hr>
 */

#ifndef __CVMX_DEBUG_H__
#define __CVMX_DEBUG_H__

#include "cvmx-core.h"
#include "cvmx-spinlock.h"


#define CVMX_DEBUG_MAX_REQUEST_SIZE 1024 + 34 /* Enough room for setting memory of 512 bytes. */
#define CVMX_DEBUG_MAX_RESPONSE_SIZE 1024 + 5

#define CVMX_DEBUG_GLOBALS_BLOCK_NAME "cvmx-debug-globals"
#define CVMX_DEBUG_GLOBALS_VERSION 3

#ifdef	__cplusplus
extern "C" {
#endif

void cvmx_debug_init(void);
void cvmx_debug_finish(void);
void cvmx_debug_trigger_exception(void);

#ifdef CVMX_BUILD_FOR_TOOLCHAIN
extern int __octeon_debug_booted;

static inline int cvmx_debug_booted(void)
{
  return __octeon_debug_booted;
}

#else

static inline int cvmx_debug_booted(void)
{
    return cvmx_sysinfo_get()->bootloader_config_flags & CVMX_BOOTINFO_CFG_FLAG_DEBUG;
}
#endif

/* There are 64 TLB entries in CN5XXX and 32 TLB entries in CN3XXX and
   128 TLB entries in CN6XXX. */
#define CVMX_DEBUG_N_TLB_ENTRIES 128

/* Maximium number of hardware breakpoints/watchpoints allowed */
#define CVMX_DEBUG_MAX_OCTEON_HW_BREAKPOINTS 4

typedef struct
{
    volatile uint64_t remote_controlled;
    uint64_t regs[32];
    uint64_t lo;
    uint64_t hi;

#define CVMX_DEBUG_BASIC_CONTEXT                \
    F(remote_controlled);                       \
    {   int i;                                  \
        for (i = 0; i < 32; i++)                \
            F(regs[i]);                         \
    }                                           \
    F(lo);                                      \
    F(hi);

    struct {
        uint64_t index;
        uint64_t entrylo[2];
        uint64_t entryhi;
        uint64_t pagemask;
        uint64_t status;
        uint64_t badvaddr;
        uint64_t cause;
        uint64_t depc;
        uint64_t desave;
        uint64_t debug;
        uint64_t multicoredebug;
        uint64_t perfval[2];
        uint64_t perfctrl[2];
    } cop0;

#define CVMX_DEBUG_COP0_CONTEXT                 \
    F(cop0.index);                              \
    F(cop0.entrylo[0]);                         \
    F(cop0.entrylo[1]);                         \
    F(cop0.entryhi);                            \
    F(cop0.pagemask);                           \
    F(cop0.status);                             \
    F(cop0.badvaddr);                           \
    F(cop0.cause);                              \
    F(cop0.depc);                               \
    F(cop0.desave);                             \
    F(cop0.debug);                              \
    F(cop0.multicoredebug);                     \
    F(cop0.perfval[0]);                         \
    F(cop0.perfval[1]);                         \
    F(cop0.perfctrl[0]);                        \
    F(cop0.perfctrl[1]);

    struct
    {
        uint64_t status;
        uint64_t address[4];
        uint64_t address_mask[4];
        uint64_t asid[4];
        uint64_t control[4];
    } hw_ibp, hw_dbp;

/* Hardware Instruction Break Point */

#define CVMX_DEBUG_HW_IBP_CONTEXT		\
    F(hw_ibp.status);				\
    F(hw_ibp.address[0]);			\
    F(hw_ibp.address[1]);			\
    F(hw_ibp.address[2]);			\
    F(hw_ibp.address[3]);			\
    F(hw_ibp.address_mask[0]);			\
    F(hw_ibp.address_mask[1]);			\
    F(hw_ibp.address_mask[2]);			\
    F(hw_ibp.address_mask[3]);			\
    F(hw_ibp.asid[0]);				\
    F(hw_ibp.asid[1]);				\
    F(hw_ibp.asid[2]);				\
    F(hw_ibp.asid[3]);				\
    F(hw_ibp.control[0]);			\
    F(hw_ibp.control[1]);			\
    F(hw_ibp.control[2]);			\
    F(hw_ibp.control[3]);

/* Hardware Data Break Point */
#define CVMX_DEBUG_HW_DBP_CONTEXT		\
    F(hw_dbp.status);				\
    F(hw_dbp.address[0]);			\
    F(hw_dbp.address[1]);			\
    F(hw_dbp.address[2]);			\
    F(hw_dbp.address[3]);			\
    F(hw_dbp.address_mask[0]);			\
    F(hw_dbp.address_mask[1]);			\
    F(hw_dbp.address_mask[2]);			\
    F(hw_dbp.address_mask[3]);			\
    F(hw_dbp.asid[0]);				\
    F(hw_dbp.asid[1]);				\
    F(hw_dbp.asid[2]);				\
    F(hw_dbp.asid[3]);				\
    F(hw_dbp.control[0]);			\
    F(hw_dbp.control[1]);			\
    F(hw_dbp.control[2]);			\
    F(hw_dbp.control[3]);


    struct cvmx_debug_tlb_t
    {
        uint64_t entryhi;
        uint64_t pagemask;
        uint64_t entrylo[2];
        uint64_t reserved;
    } tlbs[CVMX_DEBUG_N_TLB_ENTRIES];

#define CVMX_DEBUG_TLB_CONTEXT                          \
    {   int i;                                          \
        for (i = 0; i < CVMX_DEBUG_N_TLB_ENTRIES; i++)  \
        {                                               \
            F(tlbs[i].entryhi);                         \
            F(tlbs[i].pagemask);                        \
            F(tlbs[i].entrylo[0]);                      \
            F(tlbs[i].entrylo[1]);                      \
        }                                               \
    }

} cvmx_debug_core_context_t;

typedef struct cvmx_debug_tlb_t cvmx_debug_tlb_t;



typedef enum cvmx_debug_comm_type_e
{
    COMM_UART,
    COMM_REMOTE,
    COMM_SIZE
}cvmx_debug_comm_type_t;

typedef enum
{
    COMMAND_NOP = 0,            /**< Core doesn't need to do anything. Just stay in exception handler */
    COMMAND_STEP,               /**< Core needs to perform a single instruction step */
    COMMAND_CONTINUE            /**< Core need to start running. Doesn't return until some debug event occurs */
} cvmx_debug_command_t;

/* Every field in this struct has to be uint32_t. */
typedef struct 
{
    uint32_t	known_cores;
    uint32_t    step_isr;	/**< True if we are going to step into ISR's. */
    uint32_t    focus_switch;	/**< Focus can be switched. */	
    uint32_t    core_finished;	/**< True if a core has finished and not been processed yet.  */
    uint32_t	command;	/**< Command for all cores (cvmx_debug_command_t) */
    uint32_t    step_all;	/**< True if step and continue should affect all cores. False, only the focus core is affected */
    uint32_t    focus_core;	/**< Core currently under control of the debugger */
    uint32_t    active_cores;	/**< Bitmask of cores that should stop on a breakpoint */
    uint32_t    handler_cores;	/**< Bitmask of cores currently running the exception handler */
    uint32_t	ever_been_in_debug; /**< True if we have been ever been in the debugger stub at all.  */
}__attribute__ ((aligned(sizeof(uint64_t)))) cvmx_debug_state_t;

typedef int cvmx_debug_state_t_should_fit_inside_a_cache_block[sizeof(cvmx_debug_state_t)+sizeof(cvmx_spinlock_t)+4*sizeof(uint64_t) > 128 ? -1 : 1];

typedef struct cvmx_debug_globals_s
{
    uint64_t version; /* This is always the first element of this struct */
    uint64_t comm_type; /* cvmx_debug_comm_type_t */
    volatile uint64_t comm_changed; /* cvmx_debug_comm_type_t+1 when someone wants to change it. */
    volatile uint64_t init_complete;
    uint32_t tlb_entries;
    uint32_t state[sizeof(cvmx_debug_state_t)/sizeof(uint32_t)];
    cvmx_spinlock_t lock;

    volatile cvmx_debug_core_context_t contextes[CVMX_MAX_CORES];
} cvmx_debug_globals_t;

typedef union
{
    uint64_t u64;
    struct
    {
        uint64_t    rsrvd:32;   /**< Unused */
        uint64_t    dbd:1;      /**< Indicates whether the last debug exception or
                                    exception in Debug Mode occurred in a branch or
                                    jump delay slot */
        uint64_t    dm:1;       /**< Indicates that the processor is operating in Debug
                                    Mode: */
        uint64_t    nodcr:1;    /**< Indicates whether the dseg segment is present */
        uint64_t    lsnm:1;     /**< Controls access of loads/stores between the dseg
                                    segment and remaining memory when the dseg
                                    segment is present */
        uint64_t    doze:1;     /**< Indicates that the processor was in a low-power mode
                                    when a debug exception occurred */
        uint64_t    halt:1;     /**< Indicates that the internal processor system bus clock
                                    was stopped when the debug exception occurred */
        uint64_t    countdm:1;  /**< Controls or indicates the Count register behavior in
                                    Debug Mode. Implementations can have fixed
                                    behavior, in which case this bit is read-only (R), or
                                    the implementation can allow this bit to control the
                                    behavior, in which case this bit is read/write (R/W).
                                    The reset value of this bit indicates the behavior after
                                    reset, and depends on the implementation.
                                    Encoding of the bit is:
                                    - 0      Count register stopped in Debug Mode Count register is running in Debug
                                    - 1      Mode
                                    This bit is read-only (R) and reads as zero if not implemented. */
        uint64_t    ibusep:1;   /**< Indicates if a Bus Error exception is pending from an
                                    instruction fetch. Set when an instruction fetch bus
                                    error event occurs or a 1 is written to the bit by
                                    software. Cleared when a Bus Error exception on an
                                    instruction fetch is taken by the processor. If IBusEP
                                    is set when IEXI is cleared, a Bus Error exception on
                                    an instruction fetch is taken by the processor, and
                                    IBusEP is cleared.
                                    In Debug Mode, a Bus Error exception applies to a
                                    Debug Mode Bus Error exception.
                                    This bit is read-only (R) and reads as zero if not
                                    implemented. */
        uint64_t    mcheckp:1;  /**< Indicates if a Machine Check exception is pending.
                                    Set when a machine check event occurs or a 1 is
                                    written to the bit by software. Cleared when a
                                    Machine Check exception is taken by the processor.
                                    If MCheckP is set when IEXI is cleared, a Machine
                                    Check exception is taken by the processor, and
                                    MCheckP is cleared.
                                    In Debug Mode, a Machine Check exception applies
                                    to a Debug Mode Machine Check exception.
                                    This bit is read-only (R) and reads as zero if not
                                    implemented. */
        uint64_t    cacheep:1;  /**< Indicates if a Cache Error is pending. Set when a
                                    cache error event occurs or a 1 is written to the bit by
                                    software. Cleared when a Cache Error exception is
                                    taken by the processor. If CacheEP is set when IEXI
                                    is cleared, a Cache Error exception is taken by the
                                    processor, and CacheEP is cleared.
                                    In Debug Mode, a Cache Error exception applies to a
                                    Debug Mode Cache Error exception.
                                    This bit is read-only (R) and reads as zero if not
                                    implemented. */
        uint64_t    dbusep:1;   /**< Indicates if a Data Access Bus Error exception is
                                    pending. Set when a data access bus error event
                                    occurs or a 1 is written to the bit by software. Cleared
                                    when a Bus Error exception on data access is taken by
                                    the processor. If DBusEP is set when IEXI is cleared,
                                    a Bus Error exception on data access is taken by the
                                    processor, and DBusEP is cleared.
                                    In Debug Mode, a Bus Error exception applies to a
                                    Debug Mode Bus Error exception.
                                    This bit is read-only (R) and reads as zero if not
                                    implemented. */
        uint64_t    iexi:1;     /**< An Imprecise Error eXception Inhibit (IEXI) controls
                                    exceptions taken due to imprecise error indications.
                                    Set when the processor takes a debug exception or an
                                    exception in Debug Mode occurs. Cleared by
                                    execution of the DERET instruction. Otherwise
                                    modifiable by Debug Mode software.
                                    When IEXI is set, then the imprecise error exceptions
                                    from bus errors on instruction fetches or data
                                    accesses, cache errors, or machine checks are
                                    inhibited and deferred until the bit is cleared.
                                    This bit is read-only (R) and reads as zero if not
                                    implemented. */
        uint64_t    ddbsimpr:1; /**< Indicates that a Debug Data Break Store Imprecise
                                    exception due to a store was the cause of the debug
                                    exception, or that an imprecise data hardware break
                                    due to a store was indicated after another debug
                                    exception occurred. Cleared on exception in Debug
                                    Mode.
                                        - 0 No match of an imprecise data hardware breakpoint on store
                                        - 1 Match of imprecise data hardware breakpoint on store
                                    This bit is read-only (R) and reads as zero if not
                                    implemented. */
        uint64_t    ddblimpr:1; /**< Indicates that a Debug Data Break Load Imprecise
                                    exception due to a load was the cause of the debug
                                    exception, or that an imprecise data hardware break
                                    due to a load was indicated after another debug
                                    exception occurred. Cleared on exception in Debug
                                    Mode.
                                        - 0 No match of an imprecise data hardware breakpoint on load
                                        - 1 Match of imprecise data hardware breakpoint on load
                                    This bit is read-only (R) and reads as zero if not
                                    implemented. */
        uint64_t    ejtagver:3; /**< Provides the EJTAG version.
                                        - 0      Version 1 and 2.0
                                        - 1      Version 2.5
                                        - 2      Version 2.6
                                        - 3-7    Reserved */
        uint64_t    dexccode:5; /**< Indicates the cause of the latest exception in Debug
                                    Mode.
                                    The field is encoded as the ExcCode field in the
                                    Cause register for those exceptions that can occur in
                                    Debug Mode (the encoding is shown in MIPS32 and
                                    MIPS64 specifications), with addition of code 30
                                    with the mnemonic CacheErr for cache errors and the
                                    use of code 9 with mnemonic Bp for the SDBBP
                                    instruction.
                                    This value is undefined after a debug exception. */
        uint64_t    nosst:1;    /**< Indicates whether the single-step feature controllable
                                    by the SSt bit is available in this implementation:
                                          - 0      Single-step feature available
                                          - 1      No single-step feature available
                                    A minimum number of hardware instruction
                                    breakpoints must be available if no single-step
                                    feature is implemented in hardware. Refer to Section
                                    4.8.1 on page 69 for more information. */
        uint64_t    sst:1;      /**< Controls whether single-step feature is enabled:
                                          - 0       No enable of single-step feature
                                          - 1       Single-step feature enabled
                                    This bit is read-only (R) and reads as zero if not
                                    implemented due to no single-step feature (NoSSt is
                                    1). */
        uint64_t    rsrvd2:2;   /**< Must be zero */
        uint64_t    dint:1;     /**< Indicates that a Debug Interrupt exception occurred.
                                    Cleared on exception in Debug Mode.
                                          - 0       No Debug Interrupt exception
                                          - 1       Debug Interrupt exception
                                    This bit is read-only (R) and reads as zero if not
                                    implemented. */
        uint64_t    dib:1;      /**< Indicates that a Debug Instruction Break exception
                                    occurred. Cleared on exception in Debug Mode.
                                          - 0       No Debug Instruction Break exception
                                          - 1       Debug Instruction Break exception
                                    This bit is read-only (R) and reads as zero if not
                                    implemented. */
        uint64_t    ddbs:1;     /**< Indicates that a Debug Data Break Store exception
                                    occurred on a store due to a precise data hardware
                                    break. Cleared on exception in Debug Mode.
                                          - 0       No Debug Data Break Store Exception
                                          - 1       Debug Data Break Store Exception
                                    This bit is read-only (R) and reads as zero if not
                                    implemented. */
        uint64_t    ddbl:1;     /**< Indicates that a Debug Data Break Load exception
                                    occurred on a load due to a precise data hardware
                                    break. Cleared on exception in Debug Mode.
                                          - 0       No Debug Data Break Store Exception
                                          - 1       Debug Data Break Store Exception
                                    This bit is read-only (R) and reads as zero if not
                                    implemented. */
        uint64_t    dbp:1;      /**< Indicates that a Debug Breakpoint exception
                                    occurred. Cleared on exception in Debug Mode.
                                          - 0      No Debug Breakpoint exception
                                          - 1      Debug Breakpoint exception */
        uint64_t    dss:1;      /**< Indicates that a Debug Single Step exception
                                    occurred. Cleared on exception in Debug Mode.
                                          - 0       No debug single-step exception
                                          - 1       Debug single-step exception
                                    This bit is read-only (R) and reads as zero if not
                                    implemented. */
    } s;
} cvmx_debug_register_t;


typedef struct
{
    void (*init)(void);
    void (*install_break_handler)(void);
    int needs_proxy;
    int (*getpacket)(char *, size_t);
    int (*putpacket)(char *);
    void (*wait_for_resume)(volatile cvmx_debug_core_context_t *, cvmx_debug_state_t);
    void (*change_core)(int, int);
} cvmx_debug_comm_t;

#ifdef	__cplusplus
}
#endif

#endif  /* __CVMX_DEBUG_H__ */
