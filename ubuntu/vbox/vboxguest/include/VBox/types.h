/** @file
 * VirtualBox - Types.
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

#ifndef VBOX_INCLUDED_types_h
#define VBOX_INCLUDED_types_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/cdefs.h>
#include <iprt/types.h>


/** @defgroup grp_types     VBox Basic Types
 * @{
 */


/** @defgroup grp_types_both  Common Guest and Host Context Basic Types
 * @{
 */


/** @defgroup grp_types_hc  Host Context Basic Types
 * @{
 */

/** @} */


/** @defgroup grp_types_gc  Guest Context Basic Types
 * @{
 */

/** @} */


/** Pointer to per support driver session data.
 * (The data is a R0 entity and private to the the R0 SUP part. All
 * other should consider this a sort of handle.) */
typedef R0PTRTYPE(struct SUPDRVSESSION *)           PSUPDRVSESSION;

/** Event semaphore handle. Ring-0 / ring-3. */
typedef R0PTRTYPE(struct SUPSEMEVENTHANDLE *)       SUPSEMEVENT;
/** Pointer to an event semaphore handle. */
typedef SUPSEMEVENT                                *PSUPSEMEVENT;
/** Nil event semaphore handle. */
#define NIL_SUPSEMEVENT                             ((SUPSEMEVENT)0)

/** Multiple release event semaphore handle. Ring-0 / ring-3. */
typedef R0PTRTYPE(struct SUPSEMEVENTMULTIHANDLE *)  SUPSEMEVENTMULTI;
/** Pointer to an multiple release event semaphore handle. */
typedef SUPSEMEVENTMULTI                           *PSUPSEMEVENTMULTI;
/** Nil multiple release event semaphore handle. */
#define NIL_SUPSEMEVENTMULTI                        ((SUPSEMEVENTMULTI)0)


/** Pointer to a VM. */
typedef struct VM                  *PVM;
/** Pointer to a VM - Ring-0 Ptr. */
typedef R0PTRTYPE(struct VM *)      PVMR0;
/** Pointer to a VM - Ring-3 Ptr. */
typedef R3PTRTYPE(struct VM *)      PVMR3;
/** Pointer to a VM - RC Ptr. */
typedef RCPTRTYPE(struct VM *)      PVMRC;

/** Pointer to a virtual CPU structure. */
typedef struct VMCPU *              PVMCPU;
/** Pointer to a const virtual CPU structure. */
typedef const struct VMCPU *        PCVMCPU;
/** Pointer to a virtual CPU structure - Ring-3 Ptr. */
typedef R3PTRTYPE(struct VMCPU *)   PVMCPUR3;
/** Pointer to a virtual CPU structure - Ring-0 Ptr. */
typedef R0PTRTYPE(struct VMCPU *)   PVMCPUR0;
/** Pointer to a virtual CPU structure - RC Ptr. */
typedef RCPTRTYPE(struct VMCPU *)   PVMCPURC;

/** Pointer to a ring-0 (global) VM structure. */
typedef R0PTRTYPE(struct GVM *)     PGVM;
/** Pointer to the GVMCPU data. */
typedef R0PTRTYPE(struct GVMCPU *)  PGVMCPU;

/** Pointer to a ring-3 (user mode) VM structure. */
typedef R3PTRTYPE(struct UVM *)     PUVM;

/** Pointer to a ring-3 (user mode) VMCPU structure. */
typedef R3PTRTYPE(struct UVMCPU *)  PUVMCPU;

/** Virtual CPU ID. */
typedef uint32_t                    VMCPUID;
/** Pointer to a virtual CPU ID. */
typedef VMCPUID                    *PVMCPUID;
/** @name Special CPU ID values.
 * Most of these are for request scheduling.
 *
 * @{ */
/** All virtual CPUs. */
#define VMCPUID_ALL         UINT32_C(0xfffffff2)
/** All virtual CPUs, descending order. */
#define VMCPUID_ALL_REVERSE UINT32_C(0xfffffff3)
/** Any virtual CPU.
 * Intended for scheduling a VM request or some other task. */
#define VMCPUID_ANY         UINT32_C(0xfffffff4)
/** Any virtual CPU; always queue for future execution.
 * Intended for scheduling a VM request or some other task. */
#define VMCPUID_ANY_QUEUE   UINT32_C(0xfffffff5)
/** The NIL value. */
#define NIL_VMCPUID         UINT32_C(0xfffffffd)
/** @} */

/**
 * Virtual CPU set.
 */
typedef struct VMCPUSET
{
    /** The bitmap data.  */
    uint32_t    au32Bitmap[8 /*256/32*/];
} VMCPUSET;
/** Pointer to a Virtual CPU set. */
typedef VMCPUSET *PVMCPUSET;
/** Pointer to a const Virtual CPU set. */
typedef VMCPUSET const *PCVMCPUSET;


/**
 * VM State
 */
typedef enum VMSTATE
{
    /** The VM is being created. */
    VMSTATE_CREATING = 0,
    /** The VM is created. */
    VMSTATE_CREATED,
    /** The VM state is being loaded from file. */
    VMSTATE_LOADING,
    /** The VM is being powered on */
    VMSTATE_POWERING_ON,
    /** The VM is being resumed. */
    VMSTATE_RESUMING,
    /** The VM is runnning. */
    VMSTATE_RUNNING,
    /** Live save: The VM is running and the state is being saved. */
    VMSTATE_RUNNING_LS,
    /** Fault Tolerance: The VM is running and the state is being synced. */
    VMSTATE_RUNNING_FT,
    /** The VM is being reset. */
    VMSTATE_RESETTING,
    /** Live save: The VM is being reset and immediately suspended. */
    VMSTATE_RESETTING_LS,
    /** The VM is being soft/warm reset. */
    VMSTATE_SOFT_RESETTING,
    /** Live save: The VM is being soft/warm reset (not suspended afterwards). */
    VMSTATE_SOFT_RESETTING_LS,
    /** The VM is being suspended. */
    VMSTATE_SUSPENDING,
    /** Live save: The VM is being suspended during a live save operation, either as
     * part of the normal flow or VMR3Reset. */
    VMSTATE_SUSPENDING_LS,
    /** Live save: The VM is being suspended by VMR3Suspend during live save. */
    VMSTATE_SUSPENDING_EXT_LS,
    /** The VM is suspended. */
    VMSTATE_SUSPENDED,
    /** Live save: The VM has been suspended and is waiting for the live save
     * operation to move on. */
    VMSTATE_SUSPENDED_LS,
    /** Live save: The VM has been suspended by VMR3Suspend during a live save. */
    VMSTATE_SUSPENDED_EXT_LS,
    /** The VM is suspended and its state is being saved by EMT(0). (See SSM) */
    VMSTATE_SAVING,
    /** The VM is being debugged. (See DBGF.) */
    VMSTATE_DEBUGGING,
    /** Live save: The VM is being debugged while the live phase is going on. */
    VMSTATE_DEBUGGING_LS,
    /** The VM is being powered off. */
    VMSTATE_POWERING_OFF,
    /** Live save: The VM is being powered off and the save cancelled. */
    VMSTATE_POWERING_OFF_LS,
    /** The VM is switched off, awaiting destruction. */
    VMSTATE_OFF,
    /** Live save: Waiting for cancellation and transition to VMSTATE_OFF. */
    VMSTATE_OFF_LS,
    /** The VM is powered off because of a fatal error. */
    VMSTATE_FATAL_ERROR,
    /** Live save: Waiting for cancellation and transition to FatalError. */
    VMSTATE_FATAL_ERROR_LS,
    /** The VM is in guru meditation over a fatal failure. */
    VMSTATE_GURU_MEDITATION,
    /** Live save: Waiting for cancellation and transition to GuruMeditation. */
    VMSTATE_GURU_MEDITATION_LS,
    /** The VM is screwed because of a failed state loading. */
    VMSTATE_LOAD_FAILURE,
    /** The VM is being destroyed. */
    VMSTATE_DESTROYING,
    /** Terminated. */
    VMSTATE_TERMINATED,
    /** hack forcing the size of the enum to 32-bits. */
    VMSTATE_MAKE_32BIT_HACK = 0x7fffffff
} VMSTATE;

/** @def VBOXSTRICTRC_STRICT_ENABLED
 * Indicates that VBOXSTRICTRC is in strict mode.
 */
#if defined(__cplusplus) \
 && ARCH_BITS == 64    /* cdecl requires classes and structs as hidden params. */ \
 && !defined(_MSC_VER) /* trouble similar to 32-bit gcc. */ \
 &&  (   defined(RT_STRICT) \
      || defined(VBOX_STRICT) \
      || defined(DEBUG) \
      || defined(DOXYGEN_RUNNING) )
# define VBOXSTRICTRC_STRICT_ENABLED 1
#endif

/** We need RTERR_STRICT_RC.  */
#if defined(VBOXSTRICTRC_STRICT_ENABLED) && !defined(RTERR_STRICT_RC)
# define RTERR_STRICT_RC 1
#endif

/**
 * Strict VirtualBox status code.
 *
 * This is normally an 32-bit integer and the only purpose of the type is to
 * highlight the special handling that is required.  But in strict build it is a
 * class that causes compilation and runtime errors for some of the incorrect
 * handling.
 */
#ifdef VBOXSTRICTRC_STRICT_ENABLED
struct VBOXSTRICTRC
{
protected:
    /** The status code. */
    int32_t m_rc;

public:
    /** Default constructor setting the status to VERR_IPE_UNINITIALIZED_STATUS. */
    VBOXSTRICTRC()
#ifdef VERR_IPE_UNINITIALIZED_STATUS
        : m_rc(VERR_IPE_UNINITIALIZED_STATUS)
#else
        : m_rc(-233 /*VERR_IPE_UNINITIALIZED_STATUS*/)
#endif
    {
    }

    /** Constructor for normal integer status codes. */
    VBOXSTRICTRC(int32_t const rc)
        : m_rc(rc)
    {
    }

    /** Getter that VBOXSTRICTRC_VAL can use. */
    int32_t getValue() const                        { return m_rc; }

    /** @name Comparison operators
     * @{ */
    bool operator==(int32_t rc) const               { return m_rc == rc; }
    bool operator!=(int32_t rc) const               { return m_rc != rc; }
    bool operator<=(int32_t rc) const               { return m_rc <= rc; }
    bool operator>=(int32_t rc) const               { return m_rc >= rc; }
    bool operator<(int32_t rc) const                { return m_rc <  rc; }
    bool operator>(int32_t rc) const                { return m_rc >  rc; }

    bool operator==(const VBOXSTRICTRC &rRc) const  { return m_rc == rRc.m_rc; }
    bool operator!=(const VBOXSTRICTRC &rRc) const  { return m_rc != rRc.m_rc; }
    bool operator<=(const VBOXSTRICTRC &rRc) const  { return m_rc <= rRc.m_rc; }
    bool operator>=(const VBOXSTRICTRC &rRc) const  { return m_rc >= rRc.m_rc; }
    bool operator<(const VBOXSTRICTRC &rRc) const   { return m_rc <  rRc.m_rc; }
    bool operator>(const VBOXSTRICTRC &rRc) const   { return m_rc >  rRc.m_rc; }
    /** @} */

    /** Special automatic cast for RT_SUCCESS_NP. */
    operator RTErrStrictType2() const               { return RTErrStrictType2(m_rc); }

private:
    /** @name Constructors that will prevent some of the bad types.
     * @{ */
    VBOXSTRICTRC(uint8_t  rc) : m_rc(-999)          { NOREF(rc); }
    VBOXSTRICTRC(uint16_t rc) : m_rc(-999)          { NOREF(rc); }
    VBOXSTRICTRC(uint32_t rc) : m_rc(-999)          { NOREF(rc); }
    VBOXSTRICTRC(uint64_t rc) : m_rc(-999)          { NOREF(rc); }

    VBOXSTRICTRC(int8_t rc)   : m_rc(-999)          { NOREF(rc); }
    VBOXSTRICTRC(int16_t rc)  : m_rc(-999)          { NOREF(rc); }
    VBOXSTRICTRC(int64_t rc)  : m_rc(-999)          { NOREF(rc); }
    /** @} */
};
# ifdef _MSC_VER
#  pragma warning(disable:4190)
# endif
#else
typedef int32_t VBOXSTRICTRC;
#endif

/** @def VBOXSTRICTRC_VAL
 * Explicit getter.
 * @param rcStrict  The strict VirtualBox status code.
 */
#ifdef VBOXSTRICTRC_STRICT_ENABLED
# define VBOXSTRICTRC_VAL(rcStrict) ( (rcStrict).getValue() )
#else
# define VBOXSTRICTRC_VAL(rcStrict) (rcStrict)
#endif

/** @def VBOXSTRICTRC_TODO
 * Returns that needs dealing with.
 * @param rcStrict  The strict VirtualBox status code.
 */
#define VBOXSTRICTRC_TODO(rcStrict) VBOXSTRICTRC_VAL(rcStrict)


/** Pointer to a PDM Base Interface. */
typedef struct PDMIBASE *PPDMIBASE;
/** Pointer to a pointer to a PDM Base Interface. */
typedef PPDMIBASE *PPPDMIBASE;

/** Pointer to a PDM Device Instance. */
typedef struct PDMDEVINS *PPDMDEVINS;
/** Pointer to a pointer to a PDM Device Instance. */
typedef PPDMDEVINS *PPPDMDEVINS;
/** R3 pointer to a PDM Device Instance. */
typedef R3PTRTYPE(PPDMDEVINS) PPDMDEVINSR3;
/** R0 pointer to a PDM Device Instance. */
typedef R0PTRTYPE(PPDMDEVINS) PPDMDEVINSR0;
/** RC pointer to a PDM Device Instance. */
typedef RCPTRTYPE(PPDMDEVINS) PPDMDEVINSRC;

/** Pointer to a PDM PCI device structure. */
typedef struct PDMPCIDEV *PPDMPCIDEV;

/** Pointer to a PDM USB Device Instance. */
typedef struct PDMUSBINS *PPDMUSBINS;
/** Pointer to a pointer to a PDM USB Device Instance. */
typedef PPDMUSBINS *PPPDMUSBINS;

/** Pointer to a PDM Driver Instance. */
typedef struct PDMDRVINS *PPDMDRVINS;
/** Pointer to a pointer to a PDM Driver Instance. */
typedef PPDMDRVINS *PPPDMDRVINS;
/** R3 pointer to a PDM Driver Instance. */
typedef R3PTRTYPE(PPDMDRVINS) PPDMDRVINSR3;
/** R0 pointer to a PDM Driver Instance. */
typedef R0PTRTYPE(PPDMDRVINS) PPDMDRVINSR0;
/** RC pointer to a PDM Driver Instance. */
typedef RCPTRTYPE(PPDMDRVINS) PPDMDRVINSRC;

/** Pointer to a PDM Service Instance. */
typedef struct PDMSRVINS *PPDMSRVINS;
/** Pointer to a pointer to a PDM Service Instance. */
typedef PPDMSRVINS *PPPDMSRVINS;

/** Pointer to a PDM critical section. */
typedef union PDMCRITSECT *PPDMCRITSECT;
/** Pointer to a const PDM critical section. */
typedef const union PDMCRITSECT *PCPDMCRITSECT;

/** Pointer to a PDM read/write critical section. */
typedef union PDMCRITSECTRW *PPDMCRITSECTRW;
/** Pointer to a const PDM read/write critical section. */
typedef union PDMCRITSECTRW const *PCPDMCRITSECTRW;

/** R3 pointer to a timer. */
typedef R3PTRTYPE(struct TMTIMER *) PTMTIMERR3;
/** Pointer to a R3 pointer to a timer. */
typedef PTMTIMERR3 *PPTMTIMERR3;

/** R0 pointer to a timer. */
typedef R0PTRTYPE(struct TMTIMER *) PTMTIMERR0;
/** Pointer to a R3 pointer to a timer. */
typedef PTMTIMERR0 *PPTMTIMERR0;

/** RC pointer to a timer. */
typedef RCPTRTYPE(struct TMTIMER *) PTMTIMERRC;
/** Pointer to a RC pointer to a timer. */
typedef PTMTIMERRC *PPTMTIMERRC;

/** Pointer to a timer. */
typedef CTX_SUFF(PTMTIMER)     PTMTIMER;
/** Pointer to a pointer to a timer. */
typedef PTMTIMER              *PPTMTIMER;

/** SSM Operation handle. */
typedef struct SSMHANDLE *PSSMHANDLE;
/** Pointer to a const SSM stream method table. */
typedef struct SSMSTRMOPS const *PCSSMSTRMOPS;

/** Pointer to a CPUMCTX. */
typedef struct CPUMCTX *PCPUMCTX;
/** Pointer to a const CPUMCTX. */
typedef const struct CPUMCTX *PCCPUMCTX;

/** Pointer to a CPU context core. */
typedef struct CPUMCTXCORE *PCPUMCTXCORE;
/** Pointer to a const CPU context core. */
typedef const struct CPUMCTXCORE *PCCPUMCTXCORE;

/** Pointer to a selector register. */
typedef struct CPUMSELREG *PCPUMSELREG;
/** Pointer to a const selector register. */
typedef const struct CPUMSELREG *PCCPUMSELREG;

/** Pointer to selector hidden registers.
 * @deprecated Replaced by PCPUMSELREG  */
typedef struct CPUMSELREG *PCPUMSELREGHID;
/** Pointer to const selector hidden registers.
 * @deprecated Replaced by PCCPUMSELREG  */
typedef const struct CPUMSELREG *PCCPUMSELREGHID;

/** @} */


/** @defgroup grp_types_idt     Interrupt Descriptor Table Entry.
 * @todo This all belongs in x86.h!
 * @{ */

/** @todo VBOXIDT -> VBOXDESCIDT, skip the complex variations. We'll never use them. */

/** IDT Entry, Task Gate view. */
#pragma pack(1)                         /* paranoia */
typedef struct VBOXIDTE_TASKGATE
{
    /** Reserved. */
    unsigned    u16Reserved1 : 16;
    /** Task Segment Selector. */
    unsigned    u16TSS : 16;
    /** More reserved. */
    unsigned    u8Reserved2 : 8;
    /** Fixed value bit 0 - Set to 1. */
    unsigned    u1Fixed0 : 1;
    /** Busy bit. */
    unsigned    u1Busy : 1;
    /** Fixed value bit 2 - Set to 1. */
    unsigned    u1Fixed1 : 1;
    /** Fixed value bit 3 - Set to 0. */
    unsigned    u1Fixed2 : 1;
    /** Fixed value bit 4 - Set to 0. */
    unsigned    u1Fixed3 : 1;
    /** Descriptor Privilege level. */
    unsigned    u2DPL : 2;
    /** Present flag. */
    unsigned    u1Present : 1;
    /** Reserved. */
    unsigned    u16Reserved3 : 16;
} VBOXIDTE_TASKGATE;
#pragma pack()
/** Pointer to IDT Entry, Task gate view. */
typedef VBOXIDTE_TASKGATE *PVBOXIDTE_TASKGATE;


/** IDT Entry, Intertupt gate view. */
#pragma pack(1)                         /* paranoia */
typedef struct VBOXIDTE_INTERRUPTGATE
{
    /** Low offset word. */
    unsigned    u16OffsetLow : 16;
    /** Segment Selector. */
    unsigned    u16SegSel : 16;
    /** Reserved. */
    unsigned    u5Reserved2 : 5;
    /** Fixed value bit 0 - Set to 0. */
    unsigned    u1Fixed0 : 1;
    /** Fixed value bit 1 - Set to 0. */
    unsigned    u1Fixed1 : 1;
    /** Fixed value bit 2 - Set to 0. */
    unsigned    u1Fixed2 : 1;
    /** Fixed value bit 3 - Set to 0. */
    unsigned    u1Fixed3 : 1;
    /** Fixed value bit 4 - Set to 1. */
    unsigned    u1Fixed4 : 1;
    /** Fixed value bit 5 - Set to 1. */
    unsigned    u1Fixed5 : 1;
    /** Gate size, 1 = 32 bits, 0 = 16 bits. */
    unsigned    u132BitGate : 1;
    /** Fixed value bit 5 - Set to 0. */
    unsigned    u1Fixed6 : 1;
    /** Descriptor Privilege level. */
    unsigned    u2DPL : 2;
    /** Present flag. */
    unsigned    u1Present : 1;
    /** High offset word. */
    unsigned    u16OffsetHigh : 16;
} VBOXIDTE_INTERRUPTGATE;
#pragma pack()
/** Pointer to IDT Entry, Interrupt gate view. */
typedef  VBOXIDTE_INTERRUPTGATE *PVBOXIDTE_INTERRUPTGATE;

/** IDT Entry, Trap Gate view. */
#pragma pack(1)                         /* paranoia */
typedef struct VBOXIDTE_TRAPGATE
{
    /** Low offset word. */
    unsigned    u16OffsetLow : 16;
    /** Segment Selector. */
    unsigned    u16SegSel : 16;
    /** Reserved. */
    unsigned    u5Reserved2 : 5;
    /** Fixed value bit 0 - Set to 0. */
    unsigned    u1Fixed0 : 1;
    /** Fixed value bit 1 - Set to 0. */
    unsigned    u1Fixed1 : 1;
    /** Fixed value bit 2 - Set to 0. */
    unsigned    u1Fixed2 : 1;
    /** Fixed value bit 3 - Set to 1. */
    unsigned    u1Fixed3 : 1;
    /** Fixed value bit 4 - Set to 1. */
    unsigned    u1Fixed4 : 1;
    /** Fixed value bit 5 - Set to 1. */
    unsigned    u1Fixed5 : 1;
    /** Gate size, 1 = 32 bits, 0 = 16 bits. */
    unsigned    u132BitGate : 1;
    /** Fixed value bit 5 - Set to 0. */
    unsigned    u1Fixed6 : 1;
    /** Descriptor Privilege level. */
    unsigned    u2DPL : 2;
    /** Present flag. */
    unsigned    u1Present : 1;
    /** High offset word. */
    unsigned    u16OffsetHigh : 16;
} VBOXIDTE_TRAPGATE;
#pragma pack()
/** Pointer to IDT Entry, Trap Gate view. */
typedef VBOXIDTE_TRAPGATE *PVBOXIDTE_TRAPGATE;

/** IDT Entry Generic view. */
#pragma pack(1)                         /* paranoia */
typedef struct VBOXIDTE_GENERIC
{
    /** Low offset word. */
    unsigned    u16OffsetLow : 16;
    /** Segment Selector. */
    unsigned    u16SegSel : 16;
    /** Reserved. */
    unsigned    u5Reserved : 5;
    /** IDT Type part one (not used for task gate). */
    unsigned    u3Type1 : 3;
    /** IDT Type part two. */
    unsigned    u5Type2 : 5;
    /** Descriptor Privilege level. */
    unsigned    u2DPL : 2;
    /** Present flag. */
    unsigned    u1Present : 1;
    /** High offset word. */
    unsigned    u16OffsetHigh : 16;
} VBOXIDTE_GENERIC;
#pragma pack()
/** Pointer to IDT Entry Generic view. */
typedef VBOXIDTE_GENERIC *PVBOXIDTE_GENERIC;

/** IDT Type1 value. (Reserved for task gate!) */
#define VBOX_IDTE_TYPE1             0
/** IDT Type2 value - Task gate. */
#define VBOX_IDTE_TYPE2_TASK        0x5
/** IDT Type2 value - 16 bit interrupt gate. */
#define VBOX_IDTE_TYPE2_INT_16      0x6
/** IDT Type2 value - 32 bit interrupt gate. */
#define VBOX_IDTE_TYPE2_INT_32      0xe
/** IDT Type2 value - 16 bit trap gate. */
#define VBOX_IDTE_TYPE2_TRAP_16     0x7
/** IDT Type2 value - 32 bit trap gate. */
#define VBOX_IDTE_TYPE2_TRAP_32     0xf

/** IDT Entry. */
#pragma pack(1)                         /* paranoia */
typedef union VBOXIDTE
{
    /** Task gate view. */
    VBOXIDTE_TASKGATE       Task;
    /** Trap gate view. */
    VBOXIDTE_TRAPGATE       Trap;
    /** Interrupt gate view. */
    VBOXIDTE_INTERRUPTGATE  Int;
    /** Generic IDT view. */
    VBOXIDTE_GENERIC        Gen;

    /** 8 bit unsigned integer view. */
    uint8_t     au8[8];
    /** 16 bit unsigned integer view. */
    uint16_t    au16[4];
    /** 32 bit unsigned integer view. */
    uint32_t    au32[2];
    /** 64 bit unsigned integer view. */
    uint64_t    au64;
} VBOXIDTE;
#pragma pack()
/** Pointer to IDT Entry. */
typedef VBOXIDTE *PVBOXIDTE;
/** Pointer to IDT Entry. */
typedef VBOXIDTE const *PCVBOXIDTE;

/** IDT Entry, 64-bit mode, Intertupt gate view. */
#pragma pack(1)                         /* paranoia */
typedef struct VBOXIDTE64_INTERRUPTGATE
{
    /** Low offset word. */
    unsigned    u16OffsetLow : 16;
    /** Segment Selector. */
    unsigned    u16SegSel : 16;
    /** Interrupt Stack Table Index. */
    unsigned    u3Ist : 3;
    /** Fixed value bit 0 - Set to 0. */
    unsigned    u1Fixed0 : 1;
    /** Fixed value bit 1 - Set to 0. */
    unsigned    u1Fixed1 : 1;
    /** Fixed value bit 2 - Set to 0. */
    unsigned    u1Fixed2 : 1;
    /** Fixed value bit 3 - Set to 0. */
    unsigned    u1Fixed3 : 1;
    /** Fixed value bit 4 - Set to 0. */
    unsigned    u1Fixed4 : 1;
    /** Fixed value bit 5 - Set to 0. */
    unsigned    u1Fixed5 : 1;
    /** Fixed value bit 6 - Set to 1. */
    unsigned    u1Fixed6 : 1;
    /** Fixed value bit 7 - Set to 1. */
    unsigned    u1Fixed7 : 1;
    /** Gate size, 1 = 32 bits, 0 = 16 bits. */
    unsigned    u132BitGate : 1;
    /** Fixed value bit 5 - Set to 0. */
    unsigned    u1Fixed8 : 1;
    /** Descriptor Privilege level. */
    unsigned    u2DPL : 2;
    /** Present flag. */
    unsigned    u1Present : 1;
    /** High offset word. */
    unsigned    u16OffsetHigh : 16;
    /** Offset bits 32..63. */
    unsigned    u32OffsetHigh64;
    /** Reserved. */
    unsigned    u32Reserved;
} VBOXIDTE64_INTERRUPTGATE;
#pragma pack()
/** Pointer to IDT Entry, 64-bit mode, Interrupt gate view. */
typedef  VBOXIDTE64_INTERRUPTGATE *PVBOXIDTE64_INTERRUPTGATE;

/** IDT Entry, 64-bit mode, Trap gate view. */
#pragma pack(1)                         /* paranoia */
typedef struct VBOXIDTE64_TRAPGATE
{
    /** Low offset word. */
    unsigned    u16OffsetLow : 16;
    /** Segment Selector. */
    unsigned    u16SegSel : 16;
    /** Interrupt Stack Table Index. */
    unsigned    u3Ist : 3;
    /** Fixed value bit 0 - Set to 0. */
    unsigned    u1Fixed0 : 1;
    /** Fixed value bit 1 - Set to 0. */
    unsigned    u1Fixed1 : 1;
    /** Fixed value bit 2 - Set to 0. */
    unsigned    u1Fixed2 : 1;
    /** Fixed value bit 3 - Set to 0. */
    unsigned    u1Fixed3 : 1;
    /** Fixed value bit 4 - Set to 0. */
    unsigned    u1Fixed4 : 1;
    /** Fixed value bit 5 - Set to 1. */
    unsigned    u1Fixed5 : 1;
    /** Fixed value bit 6 - Set to 1. */
    unsigned    u1Fixed6 : 1;
    /** Fixed value bit 7 - Set to 1. */
    unsigned    u1Fixed7 : 1;
    /** Gate size, 1 = 32 bits, 0 = 16 bits. */
    unsigned    u132BitGate : 1;
    /** Fixed value bit 5 - Set to 0. */
    unsigned    u1Fixed8 : 1;
    /** Descriptor Privilege level. */
    unsigned    u2DPL : 2;
    /** Present flag. */
    unsigned    u1Present : 1;
    /** High offset word. */
    unsigned    u16OffsetHigh : 16;
    /** Offset bits 32..63. */
    unsigned    u32OffsetHigh64;
    /** Reserved. */
    unsigned    u32Reserved;
} VBOXIDTE64_TRAPGATE;
#pragma pack()
/** Pointer to IDT Entry, 64-bit mode, Trap gate view. */
typedef  VBOXIDTE64_TRAPGATE *PVBOXIDTE64_TRAPGATE;

/** IDT Entry, 64-bit mode, Generic view. */
#pragma pack(1)                         /* paranoia */
typedef struct VBOXIDTE64_GENERIC
{
    /** Low offset word. */
    unsigned    u16OffsetLow : 16;
    /** Segment Selector. */
    unsigned    u16SegSel : 16;
    /** Reserved. */
    unsigned    u3Ist : 3;
    /** Fixed value bit 0 - Set to 0. */
    unsigned    u1Fixed0 : 1;
    /** Fixed value bit 1 - Set to 0. */
    unsigned    u1Fixed1 : 1;
    /** IDT Type part one (not used for task gate). */
    unsigned    u3Type1 : 3;
    /** IDT Type part two. */
    unsigned    u5Type2 : 5;
    /** Descriptor Privilege level. */
    unsigned    u2DPL : 2;
    /** Present flag. */
    unsigned    u1Present : 1;
    /** High offset word. */
    unsigned    u16OffsetHigh : 16;
    /** Offset bits 32..63. */
    unsigned    u32OffsetHigh64;
    /** Reserved. */
    unsigned    u32Reserved;
} VBOXIDTE64_GENERIC;
#pragma pack()
/** Pointer to IDT Entry, 64-bit mode, Generic view. */
typedef VBOXIDTE64_GENERIC *PVBOXIDTE64_GENERIC;

/** IDT Entry, 64-bit mode. */
#pragma pack(1)                         /* paranoia */
typedef union VBOXIDTE64
{
    /** Trap gate view. */
    VBOXIDTE64_TRAPGATE       Trap;
    /** Interrupt gate view. */
    VBOXIDTE64_INTERRUPTGATE  Int;
    /** Generic IDT view. */
    VBOXIDTE64_GENERIC        Gen;

    /** 8 bit unsigned integer view. */
    uint8_t     au8[16];
    /** 16 bit unsigned integer view. */
    uint16_t    au16[8];
    /** 32 bit unsigned integer view. */
    uint32_t    au32[4];
    /** 64 bit unsigned integer view. */
    uint64_t    au64[2];
} VBOXIDTE64;
#pragma pack()
/** Pointer to IDT Entry. */
typedef VBOXIDTE64 *PVBOXIDTE64;
/** Pointer to IDT Entry. */
typedef VBOXIDTE64 const *PCVBOXIDTE64;

#pragma pack(1)
/** IDTR */
typedef struct VBOXIDTR
{
    /** Size of the IDT. */
    uint16_t    cbIdt;
    /** Address of the IDT. */
    uint64_t     pIdt;
} VBOXIDTR, *PVBOXIDTR;
#pragma pack()

/** @} */


/** @def VBOXIDTE_OFFSET
 * Return the offset of an IDT entry.
 */
#define VBOXIDTE_OFFSET(desc) \
        (  ((uint32_t)((desc).Gen.u16OffsetHigh) << 16) \
         | (           (desc).Gen.u16OffsetLow        ) )

/** @def VBOXIDTE64_OFFSET
 * Return the offset of an IDT entry.
 */
#define VBOXIDTE64_OFFSET(desc) \
        (  ((uint64_t)((desc).Gen.u32OffsetHigh64) << 32) \
         | ((uint32_t)((desc).Gen.u16OffsetHigh)   << 16) \
         | (           (desc).Gen.u16OffsetLow          ) )

#pragma pack(1)
/** GDTR */
typedef struct VBOXGDTR
{
    /** Size of the GDT. */
    uint16_t    cbGdt;
    /** Address of the GDT. */
    uint64_t    pGdt;
} VBOXGDTR;
#pragma pack()
/** Pointer to GDTR. */
typedef VBOXGDTR *PVBOXGDTR;

/** @} */


/**
 * 32-bit Task Segment used in raw mode.
 * @todo Move this to SELM! Use X86TSS32 instead.
 */
#pragma pack(1)
typedef struct VBOXTSS
{
    /** 0x00 - Back link to previous task. (static) */
    RTSEL       selPrev;
    uint16_t    padding1;
    /** 0x04 - Ring-0 stack pointer. (static) */
    uint32_t    esp0;
    /** 0x08 - Ring-0 stack segment. (static) */
    RTSEL       ss0;
    uint16_t    padding_ss0;
    /** 0x0c - Ring-1 stack pointer. (static) */
    uint32_t    esp1;
    /** 0x10 - Ring-1 stack segment. (static) */
    RTSEL       ss1;
    uint16_t    padding_ss1;
    /** 0x14 - Ring-2 stack pointer. (static) */
    uint32_t    esp2;
    /** 0x18 - Ring-2 stack segment. (static) */
    RTSEL       ss2;
    uint16_t    padding_ss2;
    /** 0x1c - Page directory for the task. (static) */
    uint32_t    cr3;
    /** 0x20 - EIP before task switch. */
    uint32_t    eip;
    /** 0x24 - EFLAGS before task switch. */
    uint32_t    eflags;
    /** 0x28 - EAX before task switch. */
    uint32_t    eax;
    /** 0x2c - ECX before task switch. */
    uint32_t    ecx;
    /** 0x30 - EDX before task switch. */
    uint32_t    edx;
    /** 0x34 - EBX before task switch. */
    uint32_t    ebx;
    /** 0x38 - ESP before task switch. */
    uint32_t    esp;
    /** 0x3c - EBP before task switch. */
    uint32_t    ebp;
    /** 0x40 - ESI before task switch. */
    uint32_t    esi;
    /** 0x44 - EDI before task switch. */
    uint32_t    edi;
    /** 0x48 - ES before task switch. */
    RTSEL       es;
    uint16_t    padding_es;
    /** 0x4c - CS before task switch. */
    RTSEL       cs;
    uint16_t    padding_cs;
    /** 0x50 - SS before task switch. */
    RTSEL       ss;
    uint16_t    padding_ss;
    /** 0x54 - DS before task switch. */
    RTSEL       ds;
    uint16_t    padding_ds;
    /** 0x58 - FS before task switch. */
    RTSEL       fs;
    uint16_t    padding_fs;
    /** 0x5c - GS before task switch. */
    RTSEL       gs;
    uint16_t    padding_gs;
    /** 0x60 - LDTR before task switch. */
    RTSEL       selLdt;
    uint16_t    padding_ldt;
    /** 0x64 - Debug trap flag */
    uint16_t    fDebugTrap;
    /** 0x66 -  Offset relative to the TSS of the start of the I/O Bitmap
     * and the end of the interrupt redirection bitmap. */
    uint16_t    offIoBitmap;
    /** 0x68 -  32 bytes for the virtual interrupt redirection bitmap. (VME) */
    uint8_t     IntRedirBitmap[32];
} VBOXTSS;
#pragma pack()
/** Pointer to task segment. */
typedef VBOXTSS *PVBOXTSS;
/** Pointer to const task segment. */
typedef const VBOXTSS *PCVBOXTSS;


/** Pointer to a callback method table provided by the VM API user. */
typedef struct VMM2USERMETHODS const *PCVMM2USERMETHODS;


/**
 * Data transport buffer (scatter/gather)
 */
typedef struct PDMDATASEG
{
    /** Length of buffer in entry. */
    size_t  cbSeg;
    /** Pointer to the start of the buffer. */
    void   *pvSeg;
} PDMDATASEG;
/** Pointer to a data transport segment. */
typedef PDMDATASEG *PPDMDATASEG;
/** Pointer to a const data transport segment. */
typedef PDMDATASEG const *PCPDMDATASEG;


/**
 * Forms of generic segment offloading.
 */
typedef enum PDMNETWORKGSOTYPE
{
    /** Invalid zero value. */
    PDMNETWORKGSOTYPE_INVALID = 0,
    /** TCP/IPv4 - no CWR/ECE encoding. */
    PDMNETWORKGSOTYPE_IPV4_TCP,
    /** TCP/IPv6 - no CWR/ECE encoding. */
    PDMNETWORKGSOTYPE_IPV6_TCP,
    /** UDP/IPv4. */
    PDMNETWORKGSOTYPE_IPV4_UDP,
    /** UDP/IPv6. */
    PDMNETWORKGSOTYPE_IPV6_UDP,
    /** TCP/IPv6 over IPv4 tunneling - no CWR/ECE encoding.
     * The header offsets and sizes relates to IPv4 and TCP, the IPv6 header is
     * figured out as needed.
     * @todo Needs checking against facts, this is just an outline of the idea. */
    PDMNETWORKGSOTYPE_IPV4_IPV6_TCP,
    /** UDP/IPv6 over IPv4 tunneling.
     * The header offsets and sizes relates to IPv4 and UDP, the IPv6 header is
     * figured out as needed.
     * @todo Needs checking against facts, this is just an outline of the idea. */
    PDMNETWORKGSOTYPE_IPV4_IPV6_UDP,
    /** The end of valid GSO types. */
    PDMNETWORKGSOTYPE_END
} PDMNETWORKGSOTYPE;


/**
 * Generic segment offloading context.
 *
 * We generally follow the E1000 specs wrt to which header fields we change.
 * However the GSO type implies where the checksum fields are and that they are
 * always updated from scratch (no half done pseudo checksums).
 *
 * @remarks This is part of the internal network GSO packets.  Take great care
 *          when making changes.  The size is expected to be exactly 8 bytes.
 *
 * @ingroup grp_pdm
 */
typedef struct PDMNETWORKGSO
{
    /** The type of segmentation offloading we're performing (PDMNETWORKGSOTYPE). */
    uint8_t             u8Type;
    /** The total header size. */
    uint8_t             cbHdrsTotal;
    /** The max segment size (MSS) to apply. */
    uint16_t            cbMaxSeg;

    /** Offset of the first header (IPv4 / IPv6).  0 if not not needed. */
    uint8_t             offHdr1;
    /** Offset of the second header (TCP / UDP).  0 if not not needed. */
    uint8_t             offHdr2;
    /** The header size used for segmentation (equal to offHdr2 in UFO). */
    uint8_t             cbHdrsSeg;
    /** Unused. */
    uint8_t             u8Unused;
} PDMNETWORKGSO;
/** Pointer to a GSO context.
 * @ingroup grp_pdm */
typedef PDMNETWORKGSO *PPDMNETWORKGSO;
/** Pointer to a const GSO context.
 * @ingroup grp_pdm */
typedef PDMNETWORKGSO const *PCPDMNETWORKGSO;

/** Pointer to a PDM filter handle.
 * @ingroup grp_pdm_net_shaper  */
typedef struct PDMNSFILTER *PPDMNSFILTER;
/** Pointer to a network shaper.
 * @ingroup grp_pdm_net_shaper */
typedef struct PDMNETSHAPER *PPDMNETSHAPER;


/**
 * The current ROM page protection.
 *
 * @remarks This is part of the saved state.
 * @ingroup grp_pgm
 */
typedef enum PGMROMPROT
{
    /** The customary invalid value. */
    PGMROMPROT_INVALID = 0,
    /** Read from the virgin ROM page, ignore writes.
     * Map the virgin page, use write access handler to ignore writes. */
    PGMROMPROT_READ_ROM_WRITE_IGNORE,
    /** Read from the virgin ROM page, write to the shadow RAM.
     * Map the virgin page, use write access handler to change the shadow RAM. */
    PGMROMPROT_READ_ROM_WRITE_RAM,
    /** Read from the shadow ROM page, ignore writes.
     * Map the shadow page read-only, use write access handler to ignore writes. */
    PGMROMPROT_READ_RAM_WRITE_IGNORE,
    /** Read from the shadow ROM page, ignore writes.
     * Map the shadow page read-write, disabled write access handler. */
    PGMROMPROT_READ_RAM_WRITE_RAM,
    /** The end of valid values. */
    PGMROMPROT_END,
    /** The usual 32-bit type size hack. */
    PGMROMPROT_32BIT_HACK = 0x7fffffff
} PGMROMPROT;


/**
 * Page mapping lock.
 * @ingroup grp_pgm
 */
typedef struct PGMPAGEMAPLOCK
{
#if defined(IN_RC) || defined(VBOX_WITH_2X_4GB_ADDR_SPACE_IN_R0)
    /** The locked page. */
    void       *pvPage;
    /** Pointer to the CPU that made the mapping.
     * In ring-0 and raw-mode context we don't intend to ever allow long term
     * locking and this is a way of making sure we're still on the same CPU. */
    PVMCPU      pVCpu;
#else
    /** Pointer to the PGMPAGE and lock type.
     * bit-0 abuse: set=write, clear=read. */
    uintptr_t   uPageAndType;
/** Read lock type value. */
# define PGMPAGEMAPLOCK_TYPE_READ    ((uintptr_t)0)
/** Write lock type value. */
# define PGMPAGEMAPLOCK_TYPE_WRITE   ((uintptr_t)1)
/** Lock type mask. */
# define PGMPAGEMAPLOCK_TYPE_MASK    ((uintptr_t)1)
    /** Pointer to the PGMCHUNKR3MAP. */
    void       *pvMap;
#endif
} PGMPAGEMAPLOCK;
/** Pointer to a page mapping lock.
 * @ingroup grp_pgm */
typedef PGMPAGEMAPLOCK *PPGMPAGEMAPLOCK;


/** Pointer to a info helper callback structure. */
typedef struct DBGFINFOHLP *PDBGFINFOHLP;
/** Pointer to a const info helper callback structure. */
typedef const struct DBGFINFOHLP *PCDBGFINFOHLP;

/** Pointer to a const register descriptor. */
typedef struct DBGFREGDESC const *PCDBGFREGDESC;


/** Configuration manager tree node - A key. */
typedef struct CFGMNODE *PCFGMNODE;

/** Configuration manager tree leaf - A value. */
typedef struct CFGMLEAF *PCFGMLEAF;


/**
 * CPU modes.
 */
typedef enum CPUMMODE
{
    /** The usual invalid zero entry. */
    CPUMMODE_INVALID = 0,
    /** Real mode. */
    CPUMMODE_REAL,
    /** Protected mode (32-bit). */
    CPUMMODE_PROTECTED,
    /** Long mode (64-bit). */
    CPUMMODE_LONG
} CPUMMODE;


/**
 * CPU mode flags (DISSTATE::mode).
 */
typedef enum DISCPUMODE
{
    DISCPUMODE_INVALID = 0,
    DISCPUMODE_16BIT,
    DISCPUMODE_32BIT,
    DISCPUMODE_64BIT,
    /** hack forcing the size of the enum to 32-bits. */
    DISCPUMODE_MAKE_32BIT_HACK = 0x7fffffff
} DISCPUMODE;

/** Pointer to the disassembler state. */
typedef struct DISSTATE *PDISSTATE;
/** Pointer to a const disassembler state. */
typedef struct DISSTATE const *PCDISSTATE;

/** @deprecated  PDISSTATE and change pCpu and pDisState to pDis. */
typedef PDISSTATE PDISCPUSTATE;
/** @deprecated  PCDISSTATE and change pCpu and pDisState to pDis. */
typedef PCDISSTATE PCDISCPUSTATE;


/**
 * Shared region description (needed by GMM and others, thus global).
 * @ingroup grp_vmmdev
 */
typedef struct VMMDEVSHAREDREGIONDESC
{
    RTGCPTR64           GCRegionAddr;
    uint32_t            cbRegion;
    uint32_t            u32Alignment;
} VMMDEVSHAREDREGIONDESC;


/** @} */

#endif /* !VBOX_INCLUDED_types_h */
