/******************************************************************************
 * include/public/trace.h
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Mark Williamson, (C) 2004 Intel Research Cambridge
 * Copyright (C) 2005 Bin Ren
 */

#ifndef __XEN_PUBLIC_TRACE_H__
#define __XEN_PUBLIC_TRACE_H__

#define TRACE_EXTRA_MAX    7
#define TRACE_EXTRA_SHIFT 28

/* Trace classes */
#define TRC_CLS_SHIFT 16
#define TRC_GEN      0x0001f000    /* General trace            */
#define TRC_SCHED    0x0002f000    /* Xen Scheduler trace      */
#define TRC_DOM0OP   0x0004f000    /* Xen DOM0 operation trace */
#define TRC_HVM      0x0008f000    /* Xen HVM trace            */
#define TRC_MEM      0x0010f000    /* Xen memory trace         */
#define TRC_PV       0x0020f000    /* Xen PV traces            */
#define TRC_SHADOW   0x0040f000    /* Xen shadow tracing       */
#define TRC_HW       0x0080f000    /* Xen hardware-related traces */
#define TRC_GUEST    0x0800f000    /* Guest-generated traces   */
#define TRC_ALL      0x0ffff000
#define TRC_HD_TO_EVENT(x) ((x)&0x0fffffff)
#define TRC_HD_CYCLE_FLAG (1UL<<31)
#define TRC_HD_INCLUDES_CYCLE_COUNT(x) ( !!( (x) & TRC_HD_CYCLE_FLAG ) )
#define TRC_HD_EXTRA(x)    (((x)>>TRACE_EXTRA_SHIFT)&TRACE_EXTRA_MAX)

/* Trace subclasses */
#define TRC_SUBCLS_SHIFT 12

/* trace subclasses for SVM */
#define TRC_HVM_ENTRYEXIT   0x00081000   /* VMENTRY and #VMEXIT       */
#define TRC_HVM_HANDLER     0x00082000   /* various HVM handlers      */
#define TRC_HVM_EMUL        0x00084000   /* emulated devices */

#define TRC_SCHED_MIN       0x00021000   /* Just runstate changes */
#define TRC_SCHED_CLASS     0x00022000   /* Scheduler-specific    */
#define TRC_SCHED_VERBOSE   0x00028000   /* More inclusive scheduling */

/*
 * The highest 3 bits of the last 12 bits of TRC_SCHED_CLASS above are
 * reserved for encoding what scheduler produced the information. The
 * actual event is encoded in the last 9 bits.
 *
 * This means we have 8 scheduling IDs available (which means at most 8
 * schedulers generating events) and, in each scheduler, up to 512
 * different events.
 */
#define TRC_SCHED_ID_BITS 3
#define TRC_SCHED_ID_SHIFT (TRC_SUBCLS_SHIFT - TRC_SCHED_ID_BITS)
#define TRC_SCHED_ID_MASK (((1UL<<TRC_SCHED_ID_BITS) - 1) << TRC_SCHED_ID_SHIFT)
#define TRC_SCHED_EVT_MASK (~(TRC_SCHED_ID_MASK))

/* Per-scheduler IDs, to identify scheduler specific events */
#define TRC_SCHED_CSCHED   0
#define TRC_SCHED_CSCHED2  1
/* #define XEN_SCHEDULER_SEDF 2 (Removed) */
#define TRC_SCHED_ARINC653 3
#define TRC_SCHED_RTDS     4

/* Per-scheduler tracing */
#define TRC_SCHED_CLASS_EVT(_c, _e) \
  ( ( TRC_SCHED_CLASS | \
      ((TRC_SCHED_##_c << TRC_SCHED_ID_SHIFT) & TRC_SCHED_ID_MASK) ) + \
    (_e & TRC_SCHED_EVT_MASK) )

/* Trace classes for Hardware */
#define TRC_HW_PM           0x00801000   /* Power management traces */
#define TRC_HW_IRQ          0x00802000   /* Traces relating to the handling of IRQs */

/* Trace events per class */
#define TRC_LOST_RECORDS        (TRC_GEN + 1)
#define TRC_TRACE_WRAP_BUFFER  (TRC_GEN + 2)
#define TRC_TRACE_CPU_CHANGE    (TRC_GEN + 3)

#define TRC_SCHED_RUNSTATE_CHANGE   (TRC_SCHED_MIN + 1)
#define TRC_SCHED_CONTINUE_RUNNING  (TRC_SCHED_MIN + 2)
#define TRC_SCHED_DOM_ADD        (TRC_SCHED_VERBOSE +  1)
#define TRC_SCHED_DOM_REM        (TRC_SCHED_VERBOSE +  2)
#define TRC_SCHED_SLEEP          (TRC_SCHED_VERBOSE +  3)
#define TRC_SCHED_WAKE           (TRC_SCHED_VERBOSE +  4)
#define TRC_SCHED_YIELD          (TRC_SCHED_VERBOSE +  5)
#define TRC_SCHED_BLOCK          (TRC_SCHED_VERBOSE +  6)
#define TRC_SCHED_SHUTDOWN       (TRC_SCHED_VERBOSE +  7)
#define TRC_SCHED_CTL            (TRC_SCHED_VERBOSE +  8)
#define TRC_SCHED_ADJDOM         (TRC_SCHED_VERBOSE +  9)
#define TRC_SCHED_SWITCH         (TRC_SCHED_VERBOSE + 10)
#define TRC_SCHED_S_TIMER_FN     (TRC_SCHED_VERBOSE + 11)
#define TRC_SCHED_T_TIMER_FN     (TRC_SCHED_VERBOSE + 12)
#define TRC_SCHED_DOM_TIMER_FN   (TRC_SCHED_VERBOSE + 13)
#define TRC_SCHED_SWITCH_INFPREV (TRC_SCHED_VERBOSE + 14)
#define TRC_SCHED_SWITCH_INFNEXT (TRC_SCHED_VERBOSE + 15)
#define TRC_SCHED_SHUTDOWN_CODE  (TRC_SCHED_VERBOSE + 16)

#define TRC_MEM_PAGE_GRANT_MAP      (TRC_MEM + 1)
#define TRC_MEM_PAGE_GRANT_UNMAP    (TRC_MEM + 2)
#define TRC_MEM_PAGE_GRANT_TRANSFER (TRC_MEM + 3)
#define TRC_MEM_SET_P2M_ENTRY       (TRC_MEM + 4)
#define TRC_MEM_DECREASE_RESERVATION (TRC_MEM + 5)
#define TRC_MEM_POD_POPULATE        (TRC_MEM + 16)
#define TRC_MEM_POD_ZERO_RECLAIM    (TRC_MEM + 17)
#define TRC_MEM_POD_SUPERPAGE_SPLINTER (TRC_MEM + 18)

#define TRC_PV_ENTRY   0x00201000 /* Hypervisor entry points for PV guests. */
#define TRC_PV_SUBCALL 0x00202000 /* Sub-call in a multicall hypercall */

#define TRC_PV_HYPERCALL             (TRC_PV_ENTRY +  1)
#define TRC_PV_TRAP                  (TRC_PV_ENTRY +  3)
#define TRC_PV_PAGE_FAULT            (TRC_PV_ENTRY +  4)
#define TRC_PV_FORCED_INVALID_OP     (TRC_PV_ENTRY +  5)
#define TRC_PV_EMULATE_PRIVOP        (TRC_PV_ENTRY +  6)
#define TRC_PV_EMULATE_4GB           (TRC_PV_ENTRY +  7)
#define TRC_PV_MATH_STATE_RESTORE    (TRC_PV_ENTRY +  8)
#define TRC_PV_PAGING_FIXUP          (TRC_PV_ENTRY +  9)
#define TRC_PV_GDT_LDT_MAPPING_FAULT (TRC_PV_ENTRY + 10)
#define TRC_PV_PTWR_EMULATION        (TRC_PV_ENTRY + 11)
#define TRC_PV_PTWR_EMULATION_PAE    (TRC_PV_ENTRY + 12)
#define TRC_PV_HYPERCALL_V2          (TRC_PV_ENTRY + 13)
#define TRC_PV_HYPERCALL_SUBCALL     (TRC_PV_SUBCALL + 14)

/*
 * TRC_PV_HYPERCALL_V2 format
 *
 * Only some of the hypercall argument are recorded. Bit fields A0 to
 * A5 in the first extra word are set if the argument is present and
 * the arguments themselves are packed sequentially in the following
 * words.
 *
 * The TRC_64_FLAG bit is not set for these events (even if there are
 * 64-bit arguments in the record).
 *
 * Word
 * 0    bit 31 30|29 28|27 26|25 24|23 22|21 20|19 ... 0
 *          A5   |A4   |A3   |A2   |A1   |A0   |Hypercall op
 * 1    First 32 bit (or low word of first 64 bit) arg in record
 * 2    Second 32 bit (or high word of first 64 bit) arg in record
 * ...
 *
 * A0-A5 bitfield values:
 *
 *   00b  Argument not present
 *   01b  32-bit argument present
 *   10b  64-bit argument present
 *   11b  Reserved
 */
#define TRC_PV_HYPERCALL_V2_ARG_32(i) (0x1 << (20 + 2*(i)))
#define TRC_PV_HYPERCALL_V2_ARG_64(i) (0x2 << (20 + 2*(i)))
#define TRC_PV_HYPERCALL_V2_ARG_MASK  (0xfff00000)

#define TRC_SHADOW_NOT_SHADOW                 (TRC_SHADOW +  1)
#define TRC_SHADOW_FAST_PROPAGATE             (TRC_SHADOW +  2)
#define TRC_SHADOW_FAST_MMIO                  (TRC_SHADOW +  3)
#define TRC_SHADOW_FALSE_FAST_PATH            (TRC_SHADOW +  4)
#define TRC_SHADOW_MMIO                       (TRC_SHADOW +  5)
#define TRC_SHADOW_FIXUP                      (TRC_SHADOW +  6)
#define TRC_SHADOW_DOMF_DYING                 (TRC_SHADOW +  7)
#define TRC_SHADOW_EMULATE                    (TRC_SHADOW +  8)
#define TRC_SHADOW_EMULATE_UNSHADOW_USER      (TRC_SHADOW +  9)
#define TRC_SHADOW_EMULATE_UNSHADOW_EVTINJ    (TRC_SHADOW + 10)
#define TRC_SHADOW_EMULATE_UNSHADOW_UNHANDLED (TRC_SHADOW + 11)
#define TRC_SHADOW_WRMAP_BF                   (TRC_SHADOW + 12)
#define TRC_SHADOW_PREALLOC_UNPIN             (TRC_SHADOW + 13)
#define TRC_SHADOW_RESYNC_FULL                (TRC_SHADOW + 14)
#define TRC_SHADOW_RESYNC_ONLY                (TRC_SHADOW + 15)

/* trace events per subclass */
#define TRC_HVM_NESTEDFLAG      (0x400)
#define TRC_HVM_VMENTRY         (TRC_HVM_ENTRYEXIT + 0x01)
#define TRC_HVM_VMEXIT          (TRC_HVM_ENTRYEXIT + 0x02)
#define TRC_HVM_VMEXIT64        (TRC_HVM_ENTRYEXIT + TRC_64_FLAG + 0x02)
#define TRC_HVM_PF_XEN          (TRC_HVM_HANDLER + 0x01)
#define TRC_HVM_PF_XEN64        (TRC_HVM_HANDLER + TRC_64_FLAG + 0x01)
#define TRC_HVM_PF_INJECT       (TRC_HVM_HANDLER + 0x02)
#define TRC_HVM_PF_INJECT64     (TRC_HVM_HANDLER + TRC_64_FLAG + 0x02)
#define TRC_HVM_INJ_EXC         (TRC_HVM_HANDLER + 0x03)
#define TRC_HVM_INJ_VIRQ        (TRC_HVM_HANDLER + 0x04)
#define TRC_HVM_REINJ_VIRQ      (TRC_HVM_HANDLER + 0x05)
#define TRC_HVM_IO_READ         (TRC_HVM_HANDLER + 0x06)
#define TRC_HVM_IO_WRITE        (TRC_HVM_HANDLER + 0x07)
#define TRC_HVM_CR_READ         (TRC_HVM_HANDLER + 0x08)
#define TRC_HVM_CR_READ64       (TRC_HVM_HANDLER + TRC_64_FLAG + 0x08)
#define TRC_HVM_CR_WRITE        (TRC_HVM_HANDLER + 0x09)
#define TRC_HVM_CR_WRITE64      (TRC_HVM_HANDLER + TRC_64_FLAG + 0x09)
#define TRC_HVM_DR_READ         (TRC_HVM_HANDLER + 0x0A)
#define TRC_HVM_DR_WRITE        (TRC_HVM_HANDLER + 0x0B)
#define TRC_HVM_MSR_READ        (TRC_HVM_HANDLER + 0x0C)
#define TRC_HVM_MSR_WRITE       (TRC_HVM_HANDLER + 0x0D)
#define TRC_HVM_CPUID           (TRC_HVM_HANDLER + 0x0E)
#define TRC_HVM_INTR            (TRC_HVM_HANDLER + 0x0F)
#define TRC_HVM_NMI             (TRC_HVM_HANDLER + 0x10)
#define TRC_HVM_SMI             (TRC_HVM_HANDLER + 0x11)
#define TRC_HVM_VMMCALL         (TRC_HVM_HANDLER + 0x12)
#define TRC_HVM_HLT             (TRC_HVM_HANDLER + 0x13)
#define TRC_HVM_INVLPG          (TRC_HVM_HANDLER + 0x14)
#define TRC_HVM_INVLPG64        (TRC_HVM_HANDLER + TRC_64_FLAG + 0x14)
#define TRC_HVM_MCE             (TRC_HVM_HANDLER + 0x15)
#define TRC_HVM_IOPORT_READ     (TRC_HVM_HANDLER + 0x16)
#define TRC_HVM_IOMEM_READ      (TRC_HVM_HANDLER + 0x17)
#define TRC_HVM_CLTS            (TRC_HVM_HANDLER + 0x18)
#define TRC_HVM_LMSW            (TRC_HVM_HANDLER + 0x19)
#define TRC_HVM_LMSW64          (TRC_HVM_HANDLER + TRC_64_FLAG + 0x19)
#define TRC_HVM_RDTSC           (TRC_HVM_HANDLER + 0x1a)
#define TRC_HVM_INTR_WINDOW     (TRC_HVM_HANDLER + 0x20)
#define TRC_HVM_NPF             (TRC_HVM_HANDLER + 0x21)
#define TRC_HVM_REALMODE_EMULATE (TRC_HVM_HANDLER + 0x22)
#define TRC_HVM_TRAP             (TRC_HVM_HANDLER + 0x23)
#define TRC_HVM_TRAP_DEBUG       (TRC_HVM_HANDLER + 0x24)
#define TRC_HVM_VLAPIC           (TRC_HVM_HANDLER + 0x25)

#define TRC_HVM_IOPORT_WRITE    (TRC_HVM_HANDLER + 0x216)
#define TRC_HVM_IOMEM_WRITE     (TRC_HVM_HANDLER + 0x217)

/* Trace events for emulated devices */
#define TRC_HVM_EMUL_HPET_START_TIMER  (TRC_HVM_EMUL + 0x1)
#define TRC_HVM_EMUL_PIT_START_TIMER   (TRC_HVM_EMUL + 0x2)
#define TRC_HVM_EMUL_RTC_START_TIMER   (TRC_HVM_EMUL + 0x3)
#define TRC_HVM_EMUL_LAPIC_START_TIMER (TRC_HVM_EMUL + 0x4)
#define TRC_HVM_EMUL_HPET_STOP_TIMER   (TRC_HVM_EMUL + 0x5)
#define TRC_HVM_EMUL_PIT_STOP_TIMER    (TRC_HVM_EMUL + 0x6)
#define TRC_HVM_EMUL_RTC_STOP_TIMER    (TRC_HVM_EMUL + 0x7)
#define TRC_HVM_EMUL_LAPIC_STOP_TIMER  (TRC_HVM_EMUL + 0x8)
#define TRC_HVM_EMUL_PIT_TIMER_CB      (TRC_HVM_EMUL + 0x9)
#define TRC_HVM_EMUL_LAPIC_TIMER_CB    (TRC_HVM_EMUL + 0xA)
#define TRC_HVM_EMUL_PIC_INT_OUTPUT    (TRC_HVM_EMUL + 0xB)
#define TRC_HVM_EMUL_PIC_KICK          (TRC_HVM_EMUL + 0xC)
#define TRC_HVM_EMUL_PIC_INTACK        (TRC_HVM_EMUL + 0xD)
#define TRC_HVM_EMUL_PIC_POSEDGE       (TRC_HVM_EMUL + 0xE)
#define TRC_HVM_EMUL_PIC_NEGEDGE       (TRC_HVM_EMUL + 0xF)
#define TRC_HVM_EMUL_PIC_PEND_IRQ_CALL (TRC_HVM_EMUL + 0x10)
#define TRC_HVM_EMUL_LAPIC_PIC_INTR    (TRC_HVM_EMUL + 0x11)

/* trace events for per class */
#define TRC_PM_FREQ_CHANGE      (TRC_HW_PM + 0x01)
#define TRC_PM_IDLE_ENTRY       (TRC_HW_PM + 0x02)
#define TRC_PM_IDLE_EXIT        (TRC_HW_PM + 0x03)

/* Trace events for IRQs */
#define TRC_HW_IRQ_MOVE_CLEANUP_DELAY (TRC_HW_IRQ + 0x1)
#define TRC_HW_IRQ_MOVE_CLEANUP       (TRC_HW_IRQ + 0x2)
#define TRC_HW_IRQ_BIND_VECTOR        (TRC_HW_IRQ + 0x3)
#define TRC_HW_IRQ_CLEAR_VECTOR       (TRC_HW_IRQ + 0x4)
#define TRC_HW_IRQ_MOVE_FINISH        (TRC_HW_IRQ + 0x5)
#define TRC_HW_IRQ_ASSIGN_VECTOR      (TRC_HW_IRQ + 0x6)
#define TRC_HW_IRQ_UNMAPPED_VECTOR    (TRC_HW_IRQ + 0x7)
#define TRC_HW_IRQ_HANDLED            (TRC_HW_IRQ + 0x8)

/*
 * Event Flags
 *
 * Some events (e.g, TRC_PV_TRAP and TRC_HVM_IOMEM_READ) have multiple
 * record formats.  These event flags distinguish between the
 * different formats.
 */
#define TRC_64_FLAG 0x100 /* Addresses are 64 bits (instead of 32 bits) */

/* This structure represents a single trace buffer record. */
struct t_rec {
    uint32_t event:28;
    uint32_t extra_u32:3;         /* # entries in trailing extra_u32[] array */
    uint32_t cycles_included:1;   /* u.cycles or u.no_cycles? */
    union {
        struct {
            uint32_t cycles_lo, cycles_hi; /* cycle counter timestamp */
            uint32_t extra_u32[7];         /* event data items */
        } cycles;
        struct {
            uint32_t extra_u32[7];         /* event data items */
        } nocycles;
    } u;
};

/*
 * This structure contains the metadata for a single trace buffer.  The head
 * field, indexes into an array of struct t_rec's.
 */
struct t_buf {
    /* Assume the data buffer size is X.  X is generally not a power of 2.
     * CONS and PROD are incremented modulo (2*X):
     *     0 <= cons < 2*X
     *     0 <= prod < 2*X
     * This is done because addition modulo X breaks at 2^32 when X is not a
     * power of 2:
     *     (((2^32 - 1) % X) + 1) % X != (2^32) % X
     */
    uint32_t cons;   /* Offset of next item to be consumed by control tools. */
    uint32_t prod;   /* Offset of next item to be produced by Xen.           */
    /*  Records follow immediately after the meta-data header.    */
};

/* Structure used to pass MFNs to the trace buffers back to trace consumers.
 * Offset is an offset into the mapped structure where the mfn list will be held.
 * MFNs will be at ((unsigned long *)(t_info))+(t_info->cpu_offset[cpu]).
 */
struct t_info {
    uint16_t tbuf_size; /* Size in pages of each trace buffer */
    uint16_t mfn_offset[];  /* Offset within t_info structure of the page list per cpu */
    /* MFN lists immediately after the header */
};

#endif /* __XEN_PUBLIC_TRACE_H__ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
