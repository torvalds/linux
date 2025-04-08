/* SPDX-License-Identifier: MIT */
/******************************************************************************
 * arch-x86/mca.h
 * Guest OS machine check interface to x86 Xen.
 *
 * Contributed by Advanced Micro Devices, Inc.
 * Author: Christoph Egger <Christoph.Egger@amd.com>
 *
 * Updated by Intel Corporation
 * Author: Liu, Jinsong <jinsong.liu@intel.com>
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
 */

#ifndef __XEN_PUBLIC_ARCH_X86_MCA_H__
#define __XEN_PUBLIC_ARCH_X86_MCA_H__

/* Hypercall */
#define __HYPERVISOR_mca __HYPERVISOR_arch_0

#define XEN_MCA_INTERFACE_VERSION	0x01ecc003

/* IN: Dom0 calls hypercall to retrieve nonurgent error log entry */
#define XEN_MC_NONURGENT	0x1
/* IN: Dom0 calls hypercall to retrieve urgent error log entry */
#define XEN_MC_URGENT		0x2
/* IN: Dom0 acknowledges previosly-fetched error log entry */
#define XEN_MC_ACK		0x4

/* OUT: All is ok */
#define XEN_MC_OK		0x0
/* OUT: Domain could not fetch data. */
#define XEN_MC_FETCHFAILED	0x1
/* OUT: There was no machine check data to fetch. */
#define XEN_MC_NODATA		0x2

#ifndef __ASSEMBLY__
/* vIRQ injected to Dom0 */
#define VIRQ_MCA VIRQ_ARCH_0

/*
 * mc_info entry types
 * mca machine check info are recorded in mc_info entries.
 * when fetch mca info, it can use MC_TYPE_... to distinguish
 * different mca info.
 */
#define MC_TYPE_GLOBAL		0
#define MC_TYPE_BANK		1
#define MC_TYPE_EXTENDED	2
#define MC_TYPE_RECOVERY	3

struct mcinfo_common {
	uint16_t type; /* structure type */
	uint16_t size; /* size of this struct in bytes */
};

#define MC_FLAG_CORRECTABLE	(1 << 0)
#define MC_FLAG_UNCORRECTABLE	(1 << 1)
#define MC_FLAG_RECOVERABLE	(1 << 2)
#define MC_FLAG_POLLED		(1 << 3)
#define MC_FLAG_RESET		(1 << 4)
#define MC_FLAG_CMCI		(1 << 5)
#define MC_FLAG_MCE		(1 << 6)

/* contains x86 global mc information */
struct mcinfo_global {
	struct mcinfo_common common;

	uint16_t mc_domid; /* running domain at the time in error */
	uint16_t mc_vcpuid; /* virtual cpu scheduled for mc_domid */
	uint32_t mc_socketid; /* physical socket of the physical core */
	uint16_t mc_coreid; /* physical impacted core */
	uint16_t mc_core_threadid; /* core thread of physical core */
	uint32_t mc_apicid;
	uint32_t mc_flags;
	uint64_t mc_gstatus; /* global status */
};

/* contains x86 bank mc information */
struct mcinfo_bank {
	struct mcinfo_common common;

	uint16_t mc_bank; /* bank nr */
	uint16_t mc_domid; /* domain referenced by mc_addr if valid */
	uint64_t mc_status; /* bank status */
	uint64_t mc_addr; /* bank address */
	uint64_t mc_misc;
	uint64_t mc_ctrl2;
	uint64_t mc_tsc;
};

struct mcinfo_msr {
	uint64_t reg; /* MSR */
	uint64_t value; /* MSR value */
};

/* contains mc information from other or additional mc MSRs */
struct mcinfo_extended {
	struct mcinfo_common common;
	uint32_t mc_msrs; /* Number of msr with valid values. */
	/*
	 * Currently Intel extended MSR (32/64) include all gp registers
	 * and E(R)FLAGS, E(R)IP, E(R)MISC, up to 11/19 of them might be
	 * useful at present. So expand this array to 16/32 to leave room.
	 */
	struct mcinfo_msr mc_msr[sizeof(void *) * 4];
};

/* Recovery Action flags. Giving recovery result information to DOM0 */

/* Xen takes successful recovery action, the error is recovered */
#define REC_ACTION_RECOVERED (0x1 << 0)
/* No action is performed by XEN */
#define REC_ACTION_NONE (0x1 << 1)
/* It's possible DOM0 might take action ownership in some case */
#define REC_ACTION_NEED_RESET (0x1 << 2)

/*
 * Different Recovery Action types, if the action is performed successfully,
 * REC_ACTION_RECOVERED flag will be returned.
 */

/* Page Offline Action */
#define MC_ACTION_PAGE_OFFLINE (0x1 << 0)
/* CPU offline Action */
#define MC_ACTION_CPU_OFFLINE (0x1 << 1)
/* L3 cache disable Action */
#define MC_ACTION_CACHE_SHRINK (0x1 << 2)

/*
 * Below interface used between XEN/DOM0 for passing XEN's recovery action
 * information to DOM0.
 */
struct page_offline_action {
	/* Params for passing the offlined page number to DOM0 */
	uint64_t mfn;
	uint64_t status;
};

struct cpu_offline_action {
	/* Params for passing the identity of the offlined CPU to DOM0 */
	uint32_t mc_socketid;
	uint16_t mc_coreid;
	uint16_t mc_core_threadid;
};

#define MAX_UNION_SIZE 16
struct mcinfo_recovery {
	struct mcinfo_common common;
	uint16_t mc_bank; /* bank nr */
	uint8_t action_flags;
	uint8_t action_types;
	union {
		struct page_offline_action page_retire;
		struct cpu_offline_action cpu_offline;
		uint8_t pad[MAX_UNION_SIZE];
	} action_info;
};


#define MCINFO_MAXSIZE 768
struct mc_info {
	/* Number of mcinfo_* entries in mi_data */
	uint32_t mi_nentries;
	uint32_t flags;
	uint64_t mi_data[(MCINFO_MAXSIZE - 1) / 8];
};
DEFINE_GUEST_HANDLE_STRUCT(mc_info);

#define __MC_MSR_ARRAYSIZE 8
#define __MC_NMSRS 1
#define MC_NCAPS 7
struct mcinfo_logical_cpu {
	uint32_t mc_cpunr;
	uint32_t mc_chipid;
	uint16_t mc_coreid;
	uint16_t mc_threadid;
	uint32_t mc_apicid;
	uint32_t mc_clusterid;
	uint32_t mc_ncores;
	uint32_t mc_ncores_active;
	uint32_t mc_nthreads;
	uint32_t mc_cpuid_level;
	uint32_t mc_family;
	uint32_t mc_vendor;
	uint32_t mc_model;
	uint32_t mc_step;
	char mc_vendorid[16];
	char mc_brandid[64];
	uint32_t mc_cpu_caps[MC_NCAPS];
	uint32_t mc_cache_size;
	uint32_t mc_cache_alignment;
	uint32_t mc_nmsrvals;
	struct mcinfo_msr mc_msrvalues[__MC_MSR_ARRAYSIZE];
};
DEFINE_GUEST_HANDLE_STRUCT(mcinfo_logical_cpu);

/*
 * Prototype:
 *    uint32_t x86_mcinfo_nentries(struct mc_info *mi);
 */
#define x86_mcinfo_nentries(_mi)    \
	((_mi)->mi_nentries)
/*
 * Prototype:
 *    struct mcinfo_common *x86_mcinfo_first(struct mc_info *mi);
 */
#define x86_mcinfo_first(_mi)       \
	((struct mcinfo_common *)(_mi)->mi_data)
/*
 * Prototype:
 *    struct mcinfo_common *x86_mcinfo_next(struct mcinfo_common *mic);
 */
#define x86_mcinfo_next(_mic)       \
	((struct mcinfo_common *)((uint8_t *)(_mic) + (_mic)->size))

/*
 * Prototype:
 *    void x86_mcinfo_lookup(void *ret, struct mc_info *mi, uint16_t type);
 */
static inline void x86_mcinfo_lookup(struct mcinfo_common **ret,
				     struct mc_info *mi, uint16_t type)
{
	uint32_t i;
	struct mcinfo_common *mic;
	bool found = 0;

	if (!ret || !mi)
		return;

	mic = x86_mcinfo_first(mi);
	for (i = 0; i < x86_mcinfo_nentries(mi); i++) {
		if (mic->type == type) {
			found = 1;
			break;
		}
		mic = x86_mcinfo_next(mic);
	}

	*ret = found ? mic : NULL;
}

/*
 * Fetch machine check data from hypervisor.
 */
#define XEN_MC_fetch		1
struct xen_mc_fetch {
	/*
	 * IN: XEN_MC_NONURGENT, XEN_MC_URGENT,
	 * XEN_MC_ACK if ack'king an earlier fetch
	 * OUT: XEN_MC_OK, XEN_MC_FETCHAILED, XEN_MC_NODATA
	 */
	uint32_t flags;
	uint32_t _pad0;
	/* OUT: id for ack, IN: id we are ack'ing */
	uint64_t fetch_id;

	/* OUT variables. */
	GUEST_HANDLE(mc_info) data;
};
DEFINE_GUEST_HANDLE_STRUCT(xen_mc_fetch);


/*
 * This tells the hypervisor to notify a DomU about the machine check error
 */
#define XEN_MC_notifydomain	2
struct xen_mc_notifydomain {
	/* IN variables */
	uint16_t mc_domid; /* The unprivileged domain to notify */
	uint16_t mc_vcpuid; /* The vcpu in mc_domid to notify */

	/* IN/OUT variables */
	uint32_t flags;
};
DEFINE_GUEST_HANDLE_STRUCT(xen_mc_notifydomain);

#define XEN_MC_physcpuinfo	3
struct xen_mc_physcpuinfo {
	/* IN/OUT */
	uint32_t ncpus;
	uint32_t _pad0;
	/* OUT */
	GUEST_HANDLE(mcinfo_logical_cpu) info;
};

#define XEN_MC_msrinject	4
#define MC_MSRINJ_MAXMSRS	8
struct xen_mc_msrinject {
	/* IN */
	uint32_t mcinj_cpunr; /* target processor id */
	uint32_t mcinj_flags; /* see MC_MSRINJ_F_* below */
	uint32_t mcinj_count; /* 0 .. count-1 in array are valid */
	uint32_t _pad0;
	struct mcinfo_msr mcinj_msr[MC_MSRINJ_MAXMSRS];
};

/* Flags for mcinj_flags above; bits 16-31 are reserved */
#define MC_MSRINJ_F_INTERPOSE	0x1

#define XEN_MC_mceinject	5
struct xen_mc_mceinject {
	unsigned int mceinj_cpunr; /* target processor id */
};

struct xen_mc {
	uint32_t cmd;
	uint32_t interface_version; /* XEN_MCA_INTERFACE_VERSION */
	union {
		struct xen_mc_fetch        mc_fetch;
		struct xen_mc_notifydomain mc_notifydomain;
		struct xen_mc_physcpuinfo  mc_physcpuinfo;
		struct xen_mc_msrinject    mc_msrinject;
		struct xen_mc_mceinject    mc_mceinject;
	} u;
};
DEFINE_GUEST_HANDLE_STRUCT(xen_mc);

/*
 * Fields are zero when not available. Also, this struct is shared with
 * userspace mcelog and thus must keep existing fields at current offsets.
 * Only add new fields to the end of the structure
 */
struct xen_mce {
	__u64 status;
	__u64 misc;
	__u64 addr;
	__u64 mcgstatus;
	__u64 ip;
	__u64 tsc;	/* cpu time stamp counter */
	__u64 time;	/* wall time_t when error was detected */
	__u8  cpuvendor;	/* cpu vendor as encoded in system.h */
	__u8  inject_flags;	/* software inject flags */
	__u16  pad;
	__u32 cpuid;	/* CPUID 1 EAX */
	__u8  cs;		/* code segment */
	__u8  bank;	/* machine check bank */
	__u8  cpu;	/* cpu number; obsolete; use extcpu now */
	__u8  finished;   /* entry is valid */
	__u32 extcpu;	/* linux cpu number that detected the error */
	__u32 socketid;	/* CPU socket ID */
	__u32 apicid;	/* CPU initial apic ID */
	__u64 mcgcap;	/* MCGCAP MSR: machine check capabilities of CPU */
	__u64 synd;	/* MCA_SYND MSR: only valid on SMCA systems */
	__u64 ipid;	/* MCA_IPID MSR: only valid on SMCA systems */
	__u64 ppin;	/* Protected Processor Inventory Number */
};

/*
 * This structure contains all data related to the MCE log.  Also
 * carries a signature to make it easier to find from external
 * debugging tools.  Each entry is only valid when its finished flag
 * is set.
 */

#define XEN_MCE_LOG_LEN 32

struct xen_mce_log {
	char signature[12] __nonstring; /* "MACHINECHECK" */
	unsigned len;	    /* = XEN_MCE_LOG_LEN */
	unsigned next;
	unsigned flags;
	unsigned recordlen;	/* length of struct xen_mce */
	struct xen_mce entry[XEN_MCE_LOG_LEN];
};

#define XEN_MCE_OVERFLOW 0		/* bit 0 in flags means overflow */

#define XEN_MCE_LOG_SIGNATURE	"MACHINECHECK"

#define MCE_GET_RECORD_LEN   _IOR('M', 1, int)
#define MCE_GET_LOG_LEN      _IOR('M', 2, int)
#define MCE_GETCLEAR_FLAGS   _IOR('M', 3, int)

#endif /* __ASSEMBLY__ */
#endif /* __XEN_PUBLIC_ARCH_X86_MCA_H__ */
