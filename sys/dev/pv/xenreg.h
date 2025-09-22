/*	$OpenBSD: xenreg.h,v 1.12 2024/09/04 07:54:52 mglocker Exp $	*/

/*
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
 * Copyright (c) 2004,2005,2006,2007, Keir Fraser <keir@xensource.com>
 */

#ifndef _DEV_PV_XENREG_H_
#define _DEV_PV_XENREG_H_

/*
 * Hypercall interface defines
 */

#if defined(__amd64__)
# define HYPERCALL_ARG1(_i1)	ulong _a1 = (ulong)(_i1)
# define HYPERCALL_ARG2(_i2)	ulong _a2 = (ulong)(_i2)
# define HYPERCALL_ARG3(_i3)	ulong _a3 = (ulong)(_i3)
# define HYPERCALL_ARG4(_i4)	register ulong _a4 __asm__("r10") = (ulong)(_i4)
# define HYPERCALL_ARG5(_i5)	register ulong _a5 __asm__("r8") = (ulong)(_i5)
# define HYPERCALL_RES1		ulong _r1
# define HYPERCALL_RES2		ulong _r2
# define HYPERCALL_RES3		ulong _r3
# define HYPERCALL_RES4		ulong _r4
# define HYPERCALL_RES5		/* empty */
# define HYPERCALL_RES6		/* empty */
# define HYPERCALL_RET(_rv)	(_rv) = _r1
# define HYPERCALL_LABEL	"call *%[hcall]"
# define HYPERCALL_PTR(_ptr)	[hcall] "a" (_ptr)
# define HYPERCALL_OUT1		"=a" (_r1)
# define HYPERCALL_OUT2		, "=D" (_r2)
# define HYPERCALL_OUT3		, "=S" (_r3)
# define HYPERCALL_OUT4		, "=d" (_r4)
# define HYPERCALL_OUT5		, "+r" (_a4)
# define HYPERCALL_OUT6		, "+r" (_a5)
# define HYPERCALL_IN1		"1" (_a1)
# define HYPERCALL_IN2		, "2" (_a2)
# define HYPERCALL_IN3		, "3" (_a3)
# define HYPERCALL_IN4		/* empty */
# define HYPERCALL_IN5		/* empty */
# define HYPERCALL_CLOBBER	"memory"
#elif defined(__i386__)
# define HYPERCALL_ARG1(_i1)	ulong _a1 = (ulong)(_i1)
# define HYPERCALL_ARG2(_i2)	ulong _a2 = (ulong)(_i2)
# define HYPERCALL_ARG3(_i3)	ulong _a3 = (ulong)(_i3)
# define HYPERCALL_ARG4(_i4)	ulong _a4 = (ulong)(_i4)
# define HYPERCALL_ARG5(_i5)	ulong _a5 = (ulong)(_i5)
# define HYPERCALL_RES1		ulong _r1
# define HYPERCALL_RES2		ulong _r2
# define HYPERCALL_RES3		ulong _r3
# define HYPERCALL_RES4		ulong _r4
# define HYPERCALL_RES5		ulong _r5
# define HYPERCALL_RES6		ulong _r6
# define HYPERCALL_RET(_rv)	(_rv) = _r1
# define HYPERCALL_LABEL	"call *%[hcall]"
# define HYPERCALL_PTR(_ptr)	[hcall] "a" (_ptr)
# define HYPERCALL_OUT1		"=a" (_r1)
# define HYPERCALL_OUT2		, "=b" (_r2)
# define HYPERCALL_OUT3		, "=c" (_r3)
# define HYPERCALL_OUT4		, "=d" (_r4)
# define HYPERCALL_OUT5		, "=S" (_r5)
# define HYPERCALL_OUT6		, "=D" (_r6)
# define HYPERCALL_IN1		"1" (_a1)
# define HYPERCALL_IN2		, "2" (_a2)
# define HYPERCALL_IN3		, "3" (_a3)
# define HYPERCALL_IN4		, "4" (_a4)
# define HYPERCALL_IN5		, "5" (_a5)
# define HYPERCALL_CLOBBER	"memory"
#else
# error "Not implemented"
#endif

/* Hypercall not implemented */
#define ENOXENSYS		38


#if defined(__i386__) || defined(__amd64__)
struct arch_vcpu_info {
	unsigned long cr2;
	unsigned long pad;
} __packed;

typedef unsigned long xen_pfn_t;
typedef unsigned long xen_ulong_t;

/* Maximum number of virtual CPUs in legacy multi-processor guests. */
#define XEN_LEGACY_MAX_VCPUS 32

struct arch_shared_info {
	unsigned long max_pfn;	/* max pfn that appears in table */
	/*
	 * Frame containing list of mfns containing list of mfns containing p2m.
	 */
	xen_pfn_t pfn_to_mfn_frame_list;
	unsigned long nmi_reason;
	uint64_t pad[32];
} __packed;
#else
#error "Not implemented"
#endif	/* __i386__ || __amd64__ */

/*
 * interface/xen.h
 */

typedef uint16_t domid_t;

/* DOMID_SELF is used in certain contexts to refer to oneself. */
#define DOMID_SELF		(0x7FF0U)

/*
 * Event channel endpoints per domain:
 *  1024 if a long is 32 bits; 4096 if a long is 64 bits.
 */
#define NR_EVENT_CHANNELS (sizeof(unsigned long) * sizeof(unsigned long) * 64)

struct vcpu_time_info {
	/*
	 * Updates to the following values are preceded and followed by an
	 * increment of 'version'. The guest can therefore detect updates by
	 * looking for changes to 'version'. If the least-significant bit of
	 * the version number is set then an update is in progress and the
	 * guest must wait to read a consistent set of values.
	 *
	 * The correct way to interact with the version number is similar to
	 * Linux's seqlock: see the implementations of read_seqbegin and
	 * read_seqretry.
	 */
	uint32_t version;
	uint32_t pad0;
	uint64_t tsc_timestamp;	/* TSC at last update of time vals.  */
	uint64_t system_time;	/* Time, in nanosecs, since boot.    */
	/*
	 * Current system time:
	 *   system_time +
	 *   ((((tsc - tsc_timestamp) << tsc_shift) * tsc_to_system_mul) >> 32)
	 * CPU frequency (Hz):
	 *   ((10^9 << 32) / tsc_to_system_mul) >> tsc_shift
	 */
	uint32_t tsc_to_system_mul;
	int8_t tsc_shift;
	int8_t pad1[3];
} __packed; /* 32 bytes */

struct vcpu_info {
	/*
	 * 'evtchn_upcall_pending' is written non-zero by Xen to indicate
	 * a pending notification for a particular VCPU. It is then cleared
	 * by the guest OS /before/ checking for pending work, thus avoiding
	 * a set-and-check race. Note that the mask is only accessed by Xen
	 * on the CPU that is currently hosting the VCPU. This means that the
	 * pending and mask flags can be updated by the guest without special
	 * synchronisation (i.e., no need for the x86 LOCK prefix).
	 * This may seem suboptimal because if the pending flag is set by
	 * a different CPU then an IPI may be scheduled even when the mask
	 * is set. However, note:
	 *  1. The task of 'interrupt holdoff' is covered by the per-event-
	 *     channel mask bits. A 'noisy' event that is continually being
	 *     triggered can be masked at source at this very precise
	 *     granularity.
	 *  2. The main purpose of the per-VCPU mask is therefore to restrict
	 *     reentrant execution: whether for concurrency control, or to
	 *     prevent unbounded stack usage. Whatever the purpose, we expect
	 *     that the mask will be asserted only for short periods at a time,
	 *     and so the likelihood of a 'spurious' IPI is suitably small.
	 * The mask is read before making an event upcall to the guest: a
	 * non-zero mask therefore guarantees that the VCPU will not receive
	 * an upcall activation. The mask is cleared when the VCPU requests
	 * to block: this avoids wakeup-waiting races.
	 */
	uint8_t evtchn_upcall_pending;
	uint8_t pad1[3];
	uint8_t evtchn_upcall_mask;
	uint8_t pad2[3];
	unsigned long evtchn_pending_sel;
	struct arch_vcpu_info arch;
	struct vcpu_time_info time;
} __packed; /* 64 bytes (x86) */

/*
 * Xen/kernel shared data -- pointer provided in start_info.
 *
 * This structure is defined to be both smaller than a page, and the only data
 * on the shared page, but may vary in actual size even within compatible Xen
 * versions; guests should not rely on the size of this structure remaining
 * constant.
 */
struct shared_info {
	struct vcpu_info vcpu_info[XEN_LEGACY_MAX_VCPUS];

	/*
	 * A domain can create "event channels" on which it can send and
	 * receive asynchronous event notifications. There are three classes
	 * of event that are delivered by this mechanism:
	 *  1. Bi-directional inter- and intra-domain connections.  Domains
	 *     must arrange out-of-band to set up a connection (usually by
	 *     allocating an unbound 'listener' port and advertising that via
	 *     a storage service such as xenstore).
	 *  2. Physical interrupts. A domain with suitable hardware-access
	 *     privileges can bind an event-channel port to a physical
	 *     interrupt source.
	 *  3. Virtual interrupts ('events'). A domain can bind an event
	 *     channel port to a virtual interrupt source, such as the
	 *     virtual-timer device or the emergency console.
	 *
	 * Event channels are addressed by a "port index". Each channel is
	 * associated with two bits of information:
	 *  1. PENDING -- notifies the domain that there is a pending
	 *     notification to be processed. This bit is cleared by the guest.
	 *  2. MASK -- if this bit is clear then a 0->1 transition of PENDING
	 *     will cause an asynchronous upcall to be scheduled. This bit is
	 *     only updated by the guest. It is read-only within Xen. If a
	 *     channel becomes pending while the channel is masked then the
	 *     'edge' is lost (i.e., when the channel is unmasked, the guest
	 *     must manually handle pending notifications as no upcall will be
	 *     scheduled by Xen).
	 *
	 * To expedite scanning of pending notifications, any 0->1 pending
	 * transition on an unmasked channel causes a corresponding bit in a
	 * per-vcpu selector word to be set. Each bit in the selector covers a
	 * 'C long' in the PENDING bitfield array.
	 */
	volatile unsigned long evtchn_pending[sizeof(unsigned long) * 8];
	volatile unsigned long evtchn_mask[sizeof(unsigned long) * 8];

	/*
	 * Wallclock time: updated only by control software. Guests should
	 * base their gettimeofday() syscall on this wallclock-base value.
	 */
	uint32_t wc_version;	/* Version counter: see vcpu_time_info_t. */
	uint32_t wc_sec;	/* Secs  00:00:00 UTC, Jan 1, 1970.  */
	uint32_t wc_nsec;	/* Nsecs 00:00:00 UTC, Jan 1, 1970.  */

	struct arch_shared_info arch;
} __packed;


/*
 * interface/hvm/hvm_op.h
 */

/* Get/set subcommands: extra argument == pointer to xen_hvm_param struct. */
#define HVMOP_set_param		0
#define HVMOP_get_param		1
struct xen_hvm_param {
	domid_t  domid;		/* IN */
	uint32_t index;		/* IN */
	uint64_t value;		/* IN/OUT */
};

/*
 * Parameter space for HVMOP_{set,get}_param.
 */

/*
 * How should CPU0 event-channel notifications be delivered?
 * val[63:56] == 0: val[55:0] is a delivery GSI (Global System Interrupt).
 * val[63:56] == 1: val[55:0] is a delivery PCI INTx line, as follows:
 *                  Domain = val[47:32], Bus  = val[31:16],
 *                  DevFn  = val[15: 8], IntX = val[ 1: 0]
 * val[63:56] == 2: val[7:0] is a vector number, check for
 *                  XENFEAT_hvm_callback_vector to know if this delivery
 *                  method is available.
 * If val == 0 then CPU0 event-channel notifications are not delivered.
 */
#define HVM_PARAM_CALLBACK_IRQ			0

/*
 * These are not used by Xen. They are here for convenience of HVM-guest
 * xenbus implementations.
 */
#define HVM_PARAM_STORE_PFN			1
#define HVM_PARAM_STORE_EVTCHN			2

#define HVM_PARAM_PAE_ENABLED			4

#define HVM_PARAM_IOREQ_PFN			5

#define HVM_PARAM_BUFIOREQ_PFN			6
#define HVM_PARAM_BUFIOREQ_EVTCHN		26

/*
 * Set mode for virtual timers (currently x86 only):
 *  delay_for_missed_ticks (default):
 *   Do not advance a vcpu's time beyond the correct delivery time for
 *   interrupts that have been missed due to preemption. Deliver missed
 *   interrupts when the vcpu is rescheduled and advance the vcpu's virtual
 *   time stepwise for each one.
 *  no_delay_for_missed_ticks:
 *   As above, missed interrupts are delivered, but guest time always tracks
 *   wallclock (i.e., real) time while doing so.
 *  no_missed_ticks_pending:
 *   No missed interrupts are held pending. Instead, to ensure ticks are
 *   delivered at some non-zero rate, if we detect missed ticks then the
 *   internal tick alarm is not disabled if the VCPU is preempted during the
 *   next tick period.
 *  one_missed_tick_pending:
 *   Missed interrupts are collapsed together and delivered as one 'late tick'.
 *   Guest time always tracks wallclock (i.e., real) time.
 */
#define HVM_PARAM_TIMER_MODE			10
#define HVMPTM_delay_for_missed_ticks		 0
#define HVMPTM_no_delay_for_missed_ticks	 1
#define HVMPTM_no_missed_ticks_pending		 2
#define HVMPTM_one_missed_tick_pending		 3

/* Boolean: Enable virtual HPET (high-precision event timer)? (x86-only) */
#define HVM_PARAM_HPET_ENABLED			11

/* Identity-map page directory used by Intel EPT when CR0.PG=0. */
#define HVM_PARAM_IDENT_PT			12

/* Device Model domain, defaults to 0. */
#define HVM_PARAM_DM_DOMAIN			13

/* ACPI S state: currently support S0 and S3 on x86. */
#define HVM_PARAM_ACPI_S_STATE			14

/* TSS used on Intel when CR0.PE=0. */
#define HVM_PARAM_VM86_TSS			15

/* Boolean: Enable aligning all periodic vpts to reduce interrupts */
#define HVM_PARAM_VPT_ALIGN			16

/* Console debug shared memory ring and event channel */
#define HVM_PARAM_CONSOLE_PFN			17
#define HVM_PARAM_CONSOLE_EVTCHN		18

/*
 * Select location of ACPI PM1a and TMR control blocks. Currently two locations
 * are supported, specified by version 0 or 1 in this parameter:
 *   - 0: default, use the old addresses
 *        PM1A_EVT == 0x1f40; PM1A_CNT == 0x1f44; PM_TMR == 0x1f48
 *   - 1: use the new default qemu addresses
 *        PM1A_EVT == 0xb000; PM1A_CNT == 0xb004; PM_TMR == 0xb008
 * You can find these address definitions in <hvm/ioreq.h>
 */
#define HVM_PARAM_ACPI_IOPORTS_LOCATION		19

/* Enable blocking memory events, async or sync (pause vcpu until response)
 * onchangeonly indicates messages only on a change of value */
#define HVM_PARAM_MEMORY_EVENT_CR0		20
#define HVM_PARAM_MEMORY_EVENT_CR3		21
#define HVM_PARAM_MEMORY_EVENT_CR4		22
#define HVM_PARAM_MEMORY_EVENT_INT3		23
#define HVM_PARAM_MEMORY_EVENT_SINGLE_STEP	25

#define HVMPME_MODE_MASK			(3 << 0)
#define HVMPME_mode_disabled			 0
#define HVMPME_mode_async			 1
#define HVMPME_mode_sync			 2
#define HVMPME_onchangeonly			(1 << 2)

/* Boolean: Enable nestedhvm (hvm only) */
#define HVM_PARAM_NESTEDHVM			24

/* Params for the mem event rings */
#define HVM_PARAM_PAGING_RING_PFN		27
#define HVM_PARAM_ACCESS_RING_PFN		28
#define HVM_PARAM_SHARING_RING_PFN		29

#define HVM_NR_PARAMS				30

/** The callback method types for Hypervisor event delivery to our domain. */
enum {
	HVM_CB_TYPE_GSI,
	HVM_CB_TYPE_PCI_INTX,
	HVM_CB_TYPE_VECTOR,
	HVM_CB_TYPE_MASK		= 0xFF,
	HVM_CB_TYPE_SHIFT		= 56
};

/** Format for specifying a GSI type callback. */
enum {
	HVM_CB_GSI_GSI_MASK		= 0xFFFFFFFF,
	HVM_CB_GSI_GSI_SHIFT		= 0
};
#define HVM_CALLBACK_GSI(gsi) \
	(((uint64_t)HVM_CB_TYPE_GSI << HVM_CB_TYPE_SHIFT) | \
	 ((gsi) & HVM_CB_GSI_GSI_MASK) << HVM_CB_GSI_GSI_SHIFT)

/** Format for specifying a virtual PCI interrupt line GSI style callback. */
enum {
	HVM_CB_PCI_INTX_INTPIN_MASK	= 0x3,
	HVM_CB_PCI_INTX_INTPIN_SHIFT	= 0,
	HVM_CB_PCI_INTX_SLOT_MASK	= 0x1F,
	HVM_CB_PCI_INTX_SLOT_SHIFT	= 11,
};
#define HVM_CALLBACK_PCI_INTX(slot, pin) \
	(((uint64_t)HVM_CB_TYPE_PCI_INTX << HVM_CB_TYPE_SHIFT) | \
	 (((slot) & HVM_CB_PCI_INTX_SLOT_MASK) << HVM_CB_PCI_INTX_SLOT_SHIFT) | \
	 (((pin) & HVM_CB_PCI_INTX_INTPIN_MASK) << HVM_CB_PCI_INTX_INTPIN_SHIFT))

/** Format for specifying a direct IDT vector injection style callback. */
enum {
	HVM_CB_VECTOR_VECTOR_MASK	= 0xFFFFFFFF,
	HVM_CB_VECTOR_VECTOR_SHIFT	= 0
};
#define HVM_CALLBACK_VECTOR(vector) \
	(((uint64_t)HVM_CB_TYPE_VECTOR << HVM_CB_TYPE_SHIFT) | \
	 (((vector) & HVM_CB_GSI_GSI_MASK) << HVM_CB_GSI_GSI_SHIFT))


/*
 * interface/event_channel.h
 *
 * Event channels between domains.
 */

#define EVTCHNOP_bind_interdomain	0
#define EVTCHNOP_bind_virq		1
#define EVTCHNOP_bind_pirq		2
#define EVTCHNOP_close			3
#define EVTCHNOP_send			4
#define EVTCHNOP_status			5
#define EVTCHNOP_alloc_unbound		6
#define EVTCHNOP_bind_ipi		7
#define EVTCHNOP_bind_vcpu		8
#define EVTCHNOP_unmask			9
#define EVTCHNOP_reset			10

typedef uint32_t evtchn_port_t;

/*
 * EVTCHNOP_alloc_unbound: Allocate a port in domain <dom> and mark as
 * accepting interdomain bindings from domain <remote_dom>. A fresh port
 * is allocated in <dom> and returned as <port>.
 * NOTES:
 *  1. If the caller is unprivileged then <dom> must be DOMID_SELF.
 *  2. <rdom> may be DOMID_SELF, allowing loopback connections.
 */
struct evtchn_alloc_unbound {
	/* IN parameters */
	domid_t dom, remote_dom;
	/* OUT parameters */
	evtchn_port_t port;
};

/*
 * EVTCHNOP_close: Close a local event channel <port>. If the channel is
 * interdomain then the remote end is placed in the unbound state
 * (EVTCHNSTAT_unbound), awaiting a new connection.
 */
struct evtchn_close {
	/* IN parameters. */
	evtchn_port_t port;
};

/*
 * EVTCHNOP_send: Send an event to the remote end of the channel whose local
 * endpoint is <port>.
 */
struct evtchn_send {
	/* IN parameters. */
	evtchn_port_t port;
};

/*
 * EVTCHNOP_status: Get the current status of the communication channel which
 * has an endpoint at <dom, port>.
 * NOTES:
 *  1. <dom> may be specified as DOMID_SELF.
 *  2. Only a sufficiently-privileged domain may obtain the status of an event
 *     channel for which <dom> is not DOMID_SELF.
 */
struct evtchn_status {
	/* IN parameters */
	domid_t  dom;
	evtchn_port_t port;
	/* OUT parameters */
#define EVTCHNSTAT_closed	0  /* Channel is not in use.                 */
#define EVTCHNSTAT_unbound	1  /* Channel is waiting interdom connection.*/
#define EVTCHNSTAT_interdomain	2  /* Channel is connected to remote domain. */
#define EVTCHNSTAT_pirq		3  /* Channel is bound to a phys IRQ line.   */
#define EVTCHNSTAT_virq		4  /* Channel is bound to a virtual IRQ line */
#define EVTCHNSTAT_ipi		5  /* Channel is bound to a virtual IPI line */
	uint32_t status;
	uint32_t vcpu;		   /* VCPU to which this channel is bound.   */
	union {
		struct {
			domid_t dom;
		} unbound;	   /* EVTCHNSTAT_unbound */
		struct {
			domid_t dom;
			evtchn_port_t port;
		} interdomain;	   /* EVTCHNSTAT_interdomain */
		uint32_t pirq;	   /* EVTCHNSTAT_pirq */
		uint32_t virq;	   /* EVTCHNSTAT_virq */
	} u;
};

/*
 * EVTCHNOP_bind_vcpu: Specify which vcpu a channel should notify when an
 * event is pending.
 * NOTES:
 *  1. IPI-bound channels always notify the vcpu specified at bind time.
 *     This binding cannot be changed.
 *  2. Per-VCPU VIRQ channels always notify the vcpu specified at bind time.
 *     This binding cannot be changed.
 *  3. All other channels notify vcpu0 by default. This default is set when
 *     the channel is allocated (a port that is freed and subsequently reused
 *     has its binding reset to vcpu0).
 */
struct evtchn_bind_vcpu {
	/* IN parameters. */
	evtchn_port_t port;
	uint32_t vcpu;
};

/*
 * EVTCHNOP_unmask: Unmask the specified local event-channel port and deliver
 * a notification to the appropriate VCPU if an event is pending.
 */
struct evtchn_unmask {
	/* IN parameters. */
	evtchn_port_t port;
};

/*
 * Superseded by new event_channel_op() hypercall since 0x00030202.
 */
struct evtchn_op {
	uint32_t cmd;		/* EVTCHNOP_* */
	union {
		struct evtchn_alloc_unbound alloc_unbound;
		struct evtchn_close close;
		struct evtchn_send send;
		struct evtchn_status status;
		struct evtchn_bind_vcpu bind_vcpu;
		struct evtchn_unmask unmask;
	} u;
};

/*
 * interface/features.h
 *
 * Feature flags, reported by XENVER_get_features.
 */

/*
 * If set, the guest does not need to write-protect its pagetables, and can
 * update them via direct writes.
 */
#define XENFEAT_writable_page_tables		0
/*
 * If set, the guest does not need to write-protect its segment descriptor
 * tables, and can update them via direct writes.
 */
#define XENFEAT_writable_descriptor_tables	1
/*
 * If set, translation between the guest's 'pseudo-physical' address space
 * and the host's machine address space are handled by the hypervisor. In this
 * mode the guest does not need to perform phys-to/from-machine translations
 * when performing page table operations.
 */
#define XENFEAT_auto_translated_physmap		2
/* If set, the guest is running in supervisor mode (e.g., x86 ring 0). */
#define XENFEAT_supervisor_mode_kernel		3
/*
 * If set, the guest does not need to allocate x86 PAE page directories
 * below 4GB. This flag is usually implied by auto_translated_physmap.
 */
#define XENFEAT_pae_pgdir_above_4gb		4
/* x86: Does this Xen host support the MMU_PT_UPDATE_PRESERVE_AD hypercall? */
#define XENFEAT_mmu_pt_update_preserve_ad	5
/* x86: Does this Xen host support the MMU_{CLEAR,COPY}_PAGE hypercall? */
#define XENFEAT_highmem_assist			6
/*
 * If set, GNTTABOP_map_grant_ref honors flags to be placed into guest kernel
 * available pte bits.
 */
#define XENFEAT_gnttab_map_avail_bits		7
/* x86: Does this Xen host support the HVM callback vector type? */
#define XENFEAT_hvm_callback_vector		8
/* x86: pvclock algorithm is safe to use on HVM */
#define XENFEAT_hvm_safe_pvclock		9
/* x86: pirq can be used by HVM guests */
#define XENFEAT_hvm_pirqs			10
/* operation as Dom0 is supported */
#define XENFEAT_dom0				11


/*
 * interface/grant_table.h
 */

/*
 * Reference to a grant entry in a specified domain's grant table.
 */
typedef uint32_t grant_ref_t;

/*
 * The first few grant table entries will be preserved across grant table
 * version changes and may be pre-populated at domain creation by tools.
 */
#define GNTTAB_NR_RESERVED_ENTRIES		8

/*
 * Type of grant entry.
 *  GTF_invalid: This grant entry grants no privileges.
 *  GTF_permit_access: Allow @domid to map/access @frame.
 *  GTF_accept_transfer: Allow @domid to transfer ownership of one page frame
 *                       to this guest. Xen writes the page number to @frame.
 *  GTF_transitive: Allow @domid to transitively access a subrange of
 *                  @trans_grant in @trans_domid.  No mappings are allowed.
 */
#define GTF_invalid				(0<<0)
#define GTF_permit_access			(1<<0)
#define GTF_accept_transfer			(2<<0)
#define GTF_transitive				(3<<0)
#define GTF_type_mask				(3<<0)

/*
 * Subflags for GTF_permit_access.
 *  GTF_readonly: Restrict @domid to read-only mappings and accesses. [GST]
 *  GTF_reading: Grant entry is currently mapped for reading by @domid. [XEN]
 *  GTF_writing: Grant entry is currently mapped for writing by @domid. [XEN]
 *  GTF_PAT, GTF_PWT, GTF_PCD: (x86) cache attribute flags for the grant [GST]
 */
#define GTF_readonly				(1<<2)
#define GTF_reading				(1<<3)
#define GTF_writing				(1<<4)
#define GTF_PWT					(1<<5)
#define GTF_PCD					(1<<6)
#define GTF_PAT					(1<<7)

typedef struct grant_entry {
	uint16_t flags;
	domid_t domid;
	uint32_t frame;
} __packed grant_entry_t;

/* Number of grant table entries per memory page */
#define GNTTAB_NEPG			(PAGE_SIZE / sizeof(grant_entry_t))

#define GNTTABOP_query_size			6
#define GNTTABOP_set_version			8
#define GNTTABOP_get_version			10

/*
 * GNTTABOP_query_size: Query the current and maximum sizes of the shared
 * grant table.
 * NOTES:
 *  1. <dom> may be specified as DOMID_SELF.
 *  2. Only a sufficiently-privileged domain may specify <dom> != DOMID_SELF.
 */
struct gnttab_query_size {
	/* IN parameters. */
	domid_t dom;
	/* OUT parameters. */
	uint32_t nr_frames;
	uint32_t max_nr_frames;
	int16_t status;		/* => enum grant_status */
};

/*
 * GNTTABOP_set_version: Request a particular version of the grant
 * table shared table structure.  This operation can only be performed
 * once in any given domain.  It must be performed before any grants
 * are activated; otherwise, the domain will be stuck with version 1.
 * The only defined versions are 1 and 2.
 */
struct gnttab_set_version {
	/* IN/OUT parameters */
	uint32_t version;
};

/*
 * GNTTABOP_get_version: Get the grant table version which is in
 * effect for domain <dom>.
 */
struct gnttab_get_version {
	/* IN parameters */
	domid_t dom;
	uint16_t pad;
	/* OUT parameters */
	uint32_t version;
};


/*
 * interface/memory.h
 *
 * Memory reservation and information.
 */

/*
 * Increase or decrease the specified domain's memory reservation.
 * Returns the number of extents successfully allocated or freed.
 * arg == addr of struct xen_memory_reservation.
 */
#define XENMEM_increase_reservation	0
#define XENMEM_decrease_reservation	1
#define XENMEM_populate_physmap		6

#define XENMAPSPACE_shared_info		0	/* shared info page */
#define XENMAPSPACE_grant_table		1	/* grant table page */
#define XENMAPSPACE_gmfn		2	/* GMFN */
#define XENMAPSPACE_gmfn_range		3	/* GMFN range */
#define XENMAPSPACE_gmfn_foreign	4	/* GMFN from another domain */

/*
 * Sets the GPFN at which a particular page appears in the specified guest's
 * pseudophysical address space.
 * arg == addr of xen_add_to_physmap_t.
 */
#define XENMEM_add_to_physmap		7
struct xen_add_to_physmap {
	/* Which domain to change the mapping for. */
	domid_t domid;

	/* Number of pages to go through for gmfn_range */
	uint16_t size;

	/* Source mapping space. */
#define XENMAPSPACE_shared_info	0 /* shared info page */
#define XENMAPSPACE_grant_table	1 /* grant table page */
#define XENMAPSPACE_gmfn	2 /* GMFN */
#define XENMAPSPACE_gmfn_range	3 /* GMFN range */
	unsigned int space;

#define XENMAPIDX_grant_table_status 0x80000000

	/* Index into source mapping space. */
	xen_ulong_t idx;

	/* GPFN where the source mapping page should appear. */
	xen_pfn_t gpfn;
};

/*
 * interface/version.h
 *
 * Xen version, type, and compile information.
 */

/* arg == NULL; returns major:minor (16:16). */
#define XENVER_version		0

/* arg == 16 bytes buffer. */
#define XENVER_extraversion	1

/* arg == xen_compile_info. */
#define XENVER_compile_info	2
struct xen_compile_info {
	char compiler[64];
	char compile_by[16];
	char compile_domain[32];
	char compile_date[32];
};

#define XENVER_get_features	6
struct xen_feature_info {
	unsigned int submap_idx;	/* IN: which 32-bit submap to return */
	uint32_t submap;		/* OUT: 32-bit submap */
};

/* arg == NULL; returns host memory page size. */
#define XENVER_pagesize		7

/* arg == xen_domain_handle_t. */
#define XENVER_guest_handle	8

#define XENVER_commandline	9
typedef char xen_commandline_t[1024];

#endif /* _DEV_PV_XENREG_H_ */
