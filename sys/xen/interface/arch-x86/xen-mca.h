/******************************************************************************
 * arch-x86/mca.h
 * 
 * Contributed by Advanced Micro Devices, Inc.
 * Author: Christoph Egger <Christoph.Egger@amd.com>
 *
 * Guest OS machine check interface to x86 Xen.
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

/* Full MCA functionality has the following Usecases from the guest side:
 *
 * Must have's:
 * 1. Dom0 and DomU register machine check trap callback handlers
 *    (already done via "set_trap_table" hypercall)
 * 2. Dom0 registers machine check event callback handler
 *    (doable via EVTCHNOP_bind_virq)
 * 3. Dom0 and DomU fetches machine check data
 * 4. Dom0 wants Xen to notify a DomU
 * 5. Dom0 gets DomU ID from physical address
 * 6. Dom0 wants Xen to kill DomU (already done for "xm destroy")
 *
 * Nice to have's:
 * 7. Dom0 wants Xen to deactivate a physical CPU
 *    This is better done as separate task, physical CPU hotplugging,
 *    and hypercall(s) should be sysctl's
 * 8. Page migration proposed from Xen NUMA work, where Dom0 can tell Xen to
 *    move a DomU (or Dom0 itself) away from a malicious page
 *    producing correctable errors.
 * 9. offlining physical page:
 *    Xen free's and never re-uses a certain physical page.
 * 10. Testfacility: Allow Dom0 to write values into machine check MSR's
 *     and tell Xen to trigger a machine check
 */

#ifndef __XEN_PUBLIC_ARCH_X86_MCA_H__
#define __XEN_PUBLIC_ARCH_X86_MCA_H__

/* Hypercall */
#define __HYPERVISOR_mca __HYPERVISOR_arch_0

/*
 * The xen-unstable repo has interface version 0x03000001; out interface
 * is incompatible with that and any future minor revisions, so we
 * choose a different version number range that is numerically less
 * than that used in xen-unstable.
 */
#define XEN_MCA_INTERFACE_VERSION 0x01ecc003

/* IN: Dom0 calls hypercall to retrieve nonurgent telemetry */
#define XEN_MC_NONURGENT  0x0001
/* IN: Dom0/DomU calls hypercall to retrieve urgent telemetry */
#define XEN_MC_URGENT     0x0002
/* IN: Dom0 acknowledges previosly-fetched telemetry */
#define XEN_MC_ACK        0x0004

/* OUT: All is ok */
#define XEN_MC_OK           0x0
/* OUT: Domain could not fetch data. */
#define XEN_MC_FETCHFAILED  0x1
/* OUT: There was no machine check data to fetch. */
#define XEN_MC_NODATA       0x2
/* OUT: Between notification time and this hypercall an other
 *  (most likely) correctable error happened. The fetched data,
 *  does not match the original machine check data. */
#define XEN_MC_NOMATCH      0x4

/* OUT: DomU did not register MC NMI handler. Try something else. */
#define XEN_MC_CANNOTHANDLE 0x8
/* OUT: Notifying DomU failed. Retry later or try something else. */
#define XEN_MC_NOTDELIVERED 0x10
/* Note, XEN_MC_CANNOTHANDLE and XEN_MC_NOTDELIVERED are mutually exclusive. */


#ifndef __ASSEMBLY__

#define VIRQ_MCA VIRQ_ARCH_0 /* G. (DOM0) Machine Check Architecture */

/*
 * Machine Check Architecure:
 * structs are read-only and used to report all kinds of
 * correctable and uncorrectable errors detected by the HW.
 * Dom0 and DomU: register a handler to get notified.
 * Dom0 only: Correctable errors are reported via VIRQ_MCA
 * Dom0 and DomU: Uncorrectable errors are reported via nmi handlers
 */
#define MC_TYPE_GLOBAL          0
#define MC_TYPE_BANK            1
#define MC_TYPE_EXTENDED        2
#define MC_TYPE_RECOVERY        3

struct mcinfo_common {
    uint16_t type;      /* structure type */
    uint16_t size;      /* size of this struct in bytes */
};


#define MC_FLAG_CORRECTABLE     (1 << 0)
#define MC_FLAG_UNCORRECTABLE   (1 << 1)
#define MC_FLAG_RECOVERABLE	(1 << 2)
#define MC_FLAG_POLLED		(1 << 3)
#define MC_FLAG_RESET		(1 << 4)
#define MC_FLAG_CMCI		(1 << 5)
#define MC_FLAG_MCE		(1 << 6)
/* contains global x86 mc information */
struct mcinfo_global {
    struct mcinfo_common common;

    /* running domain at the time in error (most likely the impacted one) */
    uint16_t mc_domid;
    uint16_t mc_vcpuid; /* virtual cpu scheduled for mc_domid */
    uint32_t mc_socketid; /* physical socket of the physical core */
    uint16_t mc_coreid; /* physical impacted core */
    uint16_t mc_core_threadid; /* core thread of physical core */
    uint32_t mc_apicid;
    uint32_t mc_flags;
    uint64_t mc_gstatus; /* global status */
};

/* contains bank local x86 mc information */
struct mcinfo_bank {
    struct mcinfo_common common;

    uint16_t mc_bank; /* bank nr */
    uint16_t mc_domid; /* Usecase 5: domain referenced by mc_addr on dom0
                        * and if mc_addr is valid. Never valid on DomU. */
    uint64_t mc_status; /* bank status */
    uint64_t mc_addr;   /* bank address, only valid
                         * if addr bit is set in mc_status */
    uint64_t mc_misc;
    uint64_t mc_ctrl2;
    uint64_t mc_tsc;
};


struct mcinfo_msr {
    uint64_t reg;   /* MSR */
    uint64_t value; /* MSR value */
};

/* contains mc information from other
 * or additional mc MSRs */ 
struct mcinfo_extended {
    struct mcinfo_common common;

    /* You can fill up to five registers.
     * If you need more, then use this structure
     * multiple times. */

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

/* Different Recovery Action types, if the action is performed successfully,
 * REC_ACTION_RECOVERED flag will be returned.
 */

/* Page Offline Action */
#define MC_ACTION_PAGE_OFFLINE (0x1 << 0)
/* CPU offline Action */
#define MC_ACTION_CPU_OFFLINE (0x1 << 1)
/* L3 cache disable Action */
#define MC_ACTION_CACHE_SHRINK (0x1 << 2)

/* Below interface used between XEN/DOM0 for passing XEN's recovery action 
 * information to DOM0. 
 * usage Senario: After offlining broken page, XEN might pass its page offline
 * recovery action result to DOM0. DOM0 will save the information in 
 * non-volatile memory for further proactive actions, such as offlining the
 * easy broken page earlier when doing next reboot.
*/
struct page_offline_action
{
    /* Params for passing the offlined page number to DOM0 */
    uint64_t mfn;
    uint64_t status;
};

struct cpu_offline_action
{
    /* Params for passing the identity of the offlined CPU to DOM0 */
    uint32_t mc_socketid;
    uint16_t mc_coreid;
    uint16_t mc_core_threadid;
};

#define MAX_UNION_SIZE 16
struct mcinfo_recovery
{
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


#define MCINFO_HYPERCALLSIZE	1024
#define MCINFO_MAXSIZE		768

#define MCINFO_FLAGS_UNCOMPLETE 0x1
struct mc_info {
    /* Number of mcinfo_* entries in mi_data */
    uint32_t mi_nentries;
    uint32_t flags;
    uint64_t mi_data[(MCINFO_MAXSIZE - 1) / 8];
};
typedef struct mc_info mc_info_t;
DEFINE_XEN_GUEST_HANDLE(mc_info_t);

#define __MC_MSR_ARRAYSIZE 8
#define __MC_NMSRS 1
#define MC_NCAPS	7	/* 7 CPU feature flag words */
#define MC_CAPS_STD_EDX	0	/* cpuid level 0x00000001 (%edx) */
#define MC_CAPS_AMD_EDX	1	/* cpuid level 0x80000001 (%edx) */
#define MC_CAPS_TM	2	/* cpuid level 0x80860001 (TransMeta) */
#define MC_CAPS_LINUX	3	/* Linux-defined */
#define MC_CAPS_STD_ECX	4	/* cpuid level 0x00000001 (%ecx) */
#define MC_CAPS_VIA	5	/* cpuid level 0xc0000001 */
#define MC_CAPS_AMD_ECX	6	/* cpuid level 0x80000001 (%ecx) */

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
    int32_t mc_cpuid_level;
    uint32_t mc_family;
    uint32_t mc_vendor;
    uint32_t mc_model;
    uint32_t mc_step;
    char mc_vendorid[16];
    char mc_brandid[64];
    uint32_t mc_cpu_caps[MC_NCAPS];
    uint32_t mc_cache_size;
    uint32_t mc_cache_alignment;
    int32_t mc_nmsrvals;
    struct mcinfo_msr mc_msrvalues[__MC_MSR_ARRAYSIZE];
};
typedef struct mcinfo_logical_cpu xen_mc_logical_cpu_t;
DEFINE_XEN_GUEST_HANDLE(xen_mc_logical_cpu_t);


/* 
 * OS's should use these instead of writing their own lookup function
 * each with its own bugs and drawbacks.
 * We use macros instead of static inline functions to allow guests
 * to include this header in assembly files (*.S).
 */
/* Prototype:
 *    uint32_t x86_mcinfo_nentries(struct mc_info *mi);
 */
#define x86_mcinfo_nentries(_mi)    \
    (_mi)->mi_nentries
/* Prototype:
 *    struct mcinfo_common *x86_mcinfo_first(struct mc_info *mi);
 */
#define x86_mcinfo_first(_mi)       \
    ((struct mcinfo_common *)(_mi)->mi_data)
/* Prototype:
 *    struct mcinfo_common *x86_mcinfo_next(struct mcinfo_common *mic);
 */
#define x86_mcinfo_next(_mic)       \
    ((struct mcinfo_common *)((uint8_t *)(_mic) + (_mic)->size))

/* Prototype:
 *    void x86_mcinfo_lookup(void *ret, struct mc_info *mi, uint16_t type);
 */
#define x86_mcinfo_lookup(_ret, _mi, _type)    \
    do {                                                        \
        uint32_t found, i;                                      \
        struct mcinfo_common *_mic;                             \
                                                                \
        found = 0;                                              \
	(_ret) = NULL;						\
	if (_mi == NULL) break;					\
        _mic = x86_mcinfo_first(_mi);                           \
        for (i = 0; i < x86_mcinfo_nentries(_mi); i++) {        \
            if (_mic->type == (_type)) {                        \
                found = 1;                                      \
                break;                                          \
            }                                                   \
            _mic = x86_mcinfo_next(_mic);                       \
        }                                                       \
        (_ret) = found ? _mic : NULL;                           \
    } while (0)


/* Usecase 1
 * Register machine check trap callback handler
 *    (already done via "set_trap_table" hypercall)
 */

/* Usecase 2
 * Dom0 registers machine check event callback handler
 * done by EVTCHNOP_bind_virq
 */

/* Usecase 3
 * Fetch machine check data from hypervisor.
 * Note, this hypercall is special, because both Dom0 and DomU must use this.
 */
#define XEN_MC_fetch            1
struct xen_mc_fetch {
    /* IN/OUT variables. */
    uint32_t flags;	/* IN: XEN_MC_NONURGENT, XEN_MC_URGENT,
                           XEN_MC_ACK if ack'ing an earlier fetch */
			/* OUT: XEN_MC_OK, XEN_MC_FETCHFAILED,
			   XEN_MC_NODATA, XEN_MC_NOMATCH */
    uint32_t _pad0;
    uint64_t fetch_id;	/* OUT: id for ack, IN: id we are ack'ing */

    /* OUT variables. */
    XEN_GUEST_HANDLE(mc_info_t) data;
};
typedef struct xen_mc_fetch xen_mc_fetch_t;
DEFINE_XEN_GUEST_HANDLE(xen_mc_fetch_t);


/* Usecase 4
 * This tells the hypervisor to notify a DomU about the machine check error
 */
#define XEN_MC_notifydomain     2
struct xen_mc_notifydomain {
    /* IN variables. */
    uint16_t mc_domid;    /* The unprivileged domain to notify. */
    uint16_t mc_vcpuid;   /* The vcpu in mc_domid to notify.
                           * Usually echo'd value from the fetch hypercall. */

    /* IN/OUT variables. */
    uint32_t flags;

/* IN: XEN_MC_CORRECTABLE, XEN_MC_TRAP */
/* OUT: XEN_MC_OK, XEN_MC_CANNOTHANDLE, XEN_MC_NOTDELIVERED, XEN_MC_NOMATCH */
};
typedef struct xen_mc_notifydomain xen_mc_notifydomain_t;
DEFINE_XEN_GUEST_HANDLE(xen_mc_notifydomain_t);

#define XEN_MC_physcpuinfo 3
struct xen_mc_physcpuinfo {
	/* IN/OUT */
	uint32_t ncpus;
	uint32_t _pad0;
	/* OUT */
	XEN_GUEST_HANDLE(xen_mc_logical_cpu_t) info;
};

#define XEN_MC_msrinject    4
#define MC_MSRINJ_MAXMSRS       8
struct xen_mc_msrinject {
       /* IN */
	uint32_t mcinj_cpunr;           /* target processor id */
	uint32_t mcinj_flags;           /* see MC_MSRINJ_F_* below */
	uint32_t mcinj_count;           /* 0 .. count-1 in array are valid */
	uint32_t _pad0;
	struct mcinfo_msr mcinj_msr[MC_MSRINJ_MAXMSRS];
};

/* Flags for mcinj_flags above; bits 16-31 are reserved */
#define MC_MSRINJ_F_INTERPOSE   0x1

#define XEN_MC_mceinject    5
struct xen_mc_mceinject {
	unsigned int mceinj_cpunr;      /* target processor id */
};

#if defined(__XEN__) || defined(__XEN_TOOLS__)
#define XEN_MC_inject_v2        6
#define XEN_MC_INJECT_TYPE_MASK     0x7
#define XEN_MC_INJECT_TYPE_MCE      0x0
#define XEN_MC_INJECT_TYPE_CMCI     0x1

#define XEN_MC_INJECT_CPU_BROADCAST 0x8

struct xen_mc_inject_v2 {
	uint32_t flags;
	struct xenctl_bitmap cpumap;
};
#endif

struct xen_mc {
    uint32_t cmd;
    uint32_t interface_version; /* XEN_MCA_INTERFACE_VERSION */
    union {
        struct xen_mc_fetch        mc_fetch;
        struct xen_mc_notifydomain mc_notifydomain;
        struct xen_mc_physcpuinfo  mc_physcpuinfo;
        struct xen_mc_msrinject    mc_msrinject;
        struct xen_mc_mceinject    mc_mceinject;
#if defined(__XEN__) || defined(__XEN_TOOLS__)
        struct xen_mc_inject_v2    mc_inject_v2;
#endif
    } u;
};
typedef struct xen_mc xen_mc_t;
DEFINE_XEN_GUEST_HANDLE(xen_mc_t);

#endif /* __ASSEMBLY__ */

#endif /* __XEN_PUBLIC_ARCH_X86_MCA_H__ */
