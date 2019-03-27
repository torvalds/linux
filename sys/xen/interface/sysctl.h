/******************************************************************************
 * sysctl.h
 * 
 * System management operations. For use by node control stack.
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
 * Copyright (c) 2002-2006, K Fraser
 */

#ifndef __XEN_PUBLIC_SYSCTL_H__
#define __XEN_PUBLIC_SYSCTL_H__

#if !defined(__XEN__) && !defined(__XEN_TOOLS__)
#error "sysctl operations are intended for use by node control tools only"
#endif

#include "xen.h"
#include "domctl.h"
#include "physdev.h"
#include "tmem.h"

#define XEN_SYSCTL_INTERFACE_VERSION 0x0000000C

/*
 * Read console content from Xen buffer ring.
 */
/* XEN_SYSCTL_readconsole */
struct xen_sysctl_readconsole {
    /* IN: Non-zero -> clear after reading. */
    uint8_t clear;
    /* IN: Non-zero -> start index specified by @index field. */
    uint8_t incremental;
    uint8_t pad0, pad1;
    /*
     * IN:  Start index for consuming from ring buffer (if @incremental);
     * OUT: End index after consuming from ring buffer.
     */
    uint32_t index; 
    /* IN: Virtual address to write console data. */
    XEN_GUEST_HANDLE_64(char) buffer;
    /* IN: Size of buffer; OUT: Bytes written to buffer. */
    uint32_t count;
};
typedef struct xen_sysctl_readconsole xen_sysctl_readconsole_t;
DEFINE_XEN_GUEST_HANDLE(xen_sysctl_readconsole_t);

/* Get trace buffers machine base address */
/* XEN_SYSCTL_tbuf_op */
struct xen_sysctl_tbuf_op {
    /* IN variables */
#define XEN_SYSCTL_TBUFOP_get_info     0
#define XEN_SYSCTL_TBUFOP_set_cpu_mask 1
#define XEN_SYSCTL_TBUFOP_set_evt_mask 2
#define XEN_SYSCTL_TBUFOP_set_size     3
#define XEN_SYSCTL_TBUFOP_enable       4
#define XEN_SYSCTL_TBUFOP_disable      5
    uint32_t cmd;
    /* IN/OUT variables */
    struct xenctl_bitmap cpu_mask;
    uint32_t             evt_mask;
    /* OUT variables */
    uint64_aligned_t buffer_mfn;
    uint32_t size;  /* Also an IN variable! */
};
typedef struct xen_sysctl_tbuf_op xen_sysctl_tbuf_op_t;
DEFINE_XEN_GUEST_HANDLE(xen_sysctl_tbuf_op_t);

/*
 * Get physical information about the host machine
 */
/* XEN_SYSCTL_physinfo */
 /* (x86) The platform supports HVM guests. */
#define _XEN_SYSCTL_PHYSCAP_hvm          0
#define XEN_SYSCTL_PHYSCAP_hvm           (1u<<_XEN_SYSCTL_PHYSCAP_hvm)
 /* (x86) The platform supports HVM-guest direct access to I/O devices. */
#define _XEN_SYSCTL_PHYSCAP_hvm_directio 1
#define XEN_SYSCTL_PHYSCAP_hvm_directio  (1u<<_XEN_SYSCTL_PHYSCAP_hvm_directio)
struct xen_sysctl_physinfo {
    uint32_t threads_per_core;
    uint32_t cores_per_socket;
    uint32_t nr_cpus;     /* # CPUs currently online */
    uint32_t max_cpu_id;  /* Largest possible CPU ID on this host */
    uint32_t nr_nodes;    /* # nodes currently online */
    uint32_t max_node_id; /* Largest possible node ID on this host */
    uint32_t cpu_khz;
    uint64_aligned_t total_pages;
    uint64_aligned_t free_pages;
    uint64_aligned_t scrub_pages;
    uint64_aligned_t outstanding_pages;
    uint32_t hw_cap[8];

    /* XEN_SYSCTL_PHYSCAP_??? */
    uint32_t capabilities;
};
typedef struct xen_sysctl_physinfo xen_sysctl_physinfo_t;
DEFINE_XEN_GUEST_HANDLE(xen_sysctl_physinfo_t);

/*
 * Get the ID of the current scheduler.
 */
/* XEN_SYSCTL_sched_id */
struct xen_sysctl_sched_id {
    /* OUT variable */
    uint32_t sched_id;
};
typedef struct xen_sysctl_sched_id xen_sysctl_sched_id_t;
DEFINE_XEN_GUEST_HANDLE(xen_sysctl_sched_id_t);

/* Interface for controlling Xen software performance counters. */
/* XEN_SYSCTL_perfc_op */
/* Sub-operations: */
#define XEN_SYSCTL_PERFCOP_reset 1   /* Reset all counters to zero. */
#define XEN_SYSCTL_PERFCOP_query 2   /* Get perfctr information. */
struct xen_sysctl_perfc_desc {
    char         name[80];             /* name of perf counter */
    uint32_t     nr_vals;              /* number of values for this counter */
};
typedef struct xen_sysctl_perfc_desc xen_sysctl_perfc_desc_t;
DEFINE_XEN_GUEST_HANDLE(xen_sysctl_perfc_desc_t);
typedef uint32_t xen_sysctl_perfc_val_t;
DEFINE_XEN_GUEST_HANDLE(xen_sysctl_perfc_val_t);

struct xen_sysctl_perfc_op {
    /* IN variables. */
    uint32_t       cmd;                /*  XEN_SYSCTL_PERFCOP_??? */
    /* OUT variables. */
    uint32_t       nr_counters;       /*  number of counters description  */
    uint32_t       nr_vals;           /*  number of values  */
    /* counter information (or NULL) */
    XEN_GUEST_HANDLE_64(xen_sysctl_perfc_desc_t) desc;
    /* counter values (or NULL) */
    XEN_GUEST_HANDLE_64(xen_sysctl_perfc_val_t) val;
};
typedef struct xen_sysctl_perfc_op xen_sysctl_perfc_op_t;
DEFINE_XEN_GUEST_HANDLE(xen_sysctl_perfc_op_t);

/* XEN_SYSCTL_getdomaininfolist */
struct xen_sysctl_getdomaininfolist {
    /* IN variables. */
    domid_t               first_domain;
    uint32_t              max_domains;
    XEN_GUEST_HANDLE_64(xen_domctl_getdomaininfo_t) buffer;
    /* OUT variables. */
    uint32_t              num_domains;
};
typedef struct xen_sysctl_getdomaininfolist xen_sysctl_getdomaininfolist_t;
DEFINE_XEN_GUEST_HANDLE(xen_sysctl_getdomaininfolist_t);

/* Inject debug keys into Xen. */
/* XEN_SYSCTL_debug_keys */
struct xen_sysctl_debug_keys {
    /* IN variables. */
    XEN_GUEST_HANDLE_64(char) keys;
    uint32_t nr_keys;
};
typedef struct xen_sysctl_debug_keys xen_sysctl_debug_keys_t;
DEFINE_XEN_GUEST_HANDLE(xen_sysctl_debug_keys_t);

/* Get physical CPU information. */
/* XEN_SYSCTL_getcpuinfo */
struct xen_sysctl_cpuinfo {
    uint64_aligned_t idletime;
};
typedef struct xen_sysctl_cpuinfo xen_sysctl_cpuinfo_t;
DEFINE_XEN_GUEST_HANDLE(xen_sysctl_cpuinfo_t); 
struct xen_sysctl_getcpuinfo {
    /* IN variables. */
    uint32_t max_cpus;
    XEN_GUEST_HANDLE_64(xen_sysctl_cpuinfo_t) info;
    /* OUT variables. */
    uint32_t nr_cpus;
}; 
typedef struct xen_sysctl_getcpuinfo xen_sysctl_getcpuinfo_t;
DEFINE_XEN_GUEST_HANDLE(xen_sysctl_getcpuinfo_t); 

/* XEN_SYSCTL_availheap */
struct xen_sysctl_availheap {
    /* IN variables. */
    uint32_t min_bitwidth;  /* Smallest address width (zero if don't care). */
    uint32_t max_bitwidth;  /* Largest address width (zero if don't care). */
    int32_t  node;          /* NUMA node of interest (-1 for all nodes). */
    /* OUT variables. */
    uint64_aligned_t avail_bytes;/* Bytes available in the specified region. */
};
typedef struct xen_sysctl_availheap xen_sysctl_availheap_t;
DEFINE_XEN_GUEST_HANDLE(xen_sysctl_availheap_t);

/* XEN_SYSCTL_get_pmstat */
struct pm_px_val {
    uint64_aligned_t freq;        /* Px core frequency */
    uint64_aligned_t residency;   /* Px residency time */
    uint64_aligned_t count;       /* Px transition count */
};
typedef struct pm_px_val pm_px_val_t;
DEFINE_XEN_GUEST_HANDLE(pm_px_val_t);

struct pm_px_stat {
    uint8_t total;        /* total Px states */
    uint8_t usable;       /* usable Px states */
    uint8_t last;         /* last Px state */
    uint8_t cur;          /* current Px state */
    XEN_GUEST_HANDLE_64(uint64) trans_pt;   /* Px transition table */
    XEN_GUEST_HANDLE_64(pm_px_val_t) pt;
};
typedef struct pm_px_stat pm_px_stat_t;
DEFINE_XEN_GUEST_HANDLE(pm_px_stat_t);

struct pm_cx_stat {
    uint32_t nr;    /* entry nr in triggers & residencies, including C0 */
    uint32_t last;  /* last Cx state */
    uint64_aligned_t idle_time;                 /* idle time from boot */
    XEN_GUEST_HANDLE_64(uint64) triggers;    /* Cx trigger counts */
    XEN_GUEST_HANDLE_64(uint64) residencies; /* Cx residencies */
    uint32_t nr_pc;                          /* entry nr in pc[] */
    uint32_t nr_cc;                          /* entry nr in cc[] */
    /*
     * These two arrays may (and generally will) have unused slots; slots not
     * having a corresponding hardware register will not be written by the
     * hypervisor. It is therefore up to the caller to put a suitable sentinel
     * into all slots before invoking the function.
     * Indexing is 1-biased (PC1/CC1 being at index 0).
     */
    XEN_GUEST_HANDLE_64(uint64) pc;
    XEN_GUEST_HANDLE_64(uint64) cc;
};

struct xen_sysctl_get_pmstat {
#define PMSTAT_CATEGORY_MASK 0xf0
#define PMSTAT_PX            0x10
#define PMSTAT_CX            0x20
#define PMSTAT_get_max_px    (PMSTAT_PX | 0x1)
#define PMSTAT_get_pxstat    (PMSTAT_PX | 0x2)
#define PMSTAT_reset_pxstat  (PMSTAT_PX | 0x3)
#define PMSTAT_get_max_cx    (PMSTAT_CX | 0x1)
#define PMSTAT_get_cxstat    (PMSTAT_CX | 0x2)
#define PMSTAT_reset_cxstat  (PMSTAT_CX | 0x3)
    uint32_t type;
    uint32_t cpuid;
    union {
        struct pm_px_stat getpx;
        struct pm_cx_stat getcx;
        /* other struct for tx, etc */
    } u;
};
typedef struct xen_sysctl_get_pmstat xen_sysctl_get_pmstat_t;
DEFINE_XEN_GUEST_HANDLE(xen_sysctl_get_pmstat_t);

/* XEN_SYSCTL_cpu_hotplug */
struct xen_sysctl_cpu_hotplug {
    /* IN variables */
    uint32_t cpu;   /* Physical cpu. */
#define XEN_SYSCTL_CPU_HOTPLUG_ONLINE  0
#define XEN_SYSCTL_CPU_HOTPLUG_OFFLINE 1
    uint32_t op;    /* hotplug opcode */
};
typedef struct xen_sysctl_cpu_hotplug xen_sysctl_cpu_hotplug_t;
DEFINE_XEN_GUEST_HANDLE(xen_sysctl_cpu_hotplug_t);

/*
 * Get/set xen power management, include 
 * 1. cpufreq governors and related parameters
 */
/* XEN_SYSCTL_pm_op */
struct xen_userspace {
    uint32_t scaling_setspeed;
};
typedef struct xen_userspace xen_userspace_t;

struct xen_ondemand {
    uint32_t sampling_rate_max;
    uint32_t sampling_rate_min;

    uint32_t sampling_rate;
    uint32_t up_threshold;
};
typedef struct xen_ondemand xen_ondemand_t;

/* 
 * cpufreq para name of this structure named 
 * same as sysfs file name of native linux
 */
#define CPUFREQ_NAME_LEN 16
struct xen_get_cpufreq_para {
    /* IN/OUT variable */
    uint32_t cpu_num;
    uint32_t freq_num;
    uint32_t gov_num;

    /* for all governors */
    /* OUT variable */
    XEN_GUEST_HANDLE_64(uint32) affected_cpus;
    XEN_GUEST_HANDLE_64(uint32) scaling_available_frequencies;
    XEN_GUEST_HANDLE_64(char)   scaling_available_governors;
    char scaling_driver[CPUFREQ_NAME_LEN];

    uint32_t cpuinfo_cur_freq;
    uint32_t cpuinfo_max_freq;
    uint32_t cpuinfo_min_freq;
    uint32_t scaling_cur_freq;

    char scaling_governor[CPUFREQ_NAME_LEN];
    uint32_t scaling_max_freq;
    uint32_t scaling_min_freq;

    /* for specific governor */
    union {
        struct  xen_userspace userspace;
        struct  xen_ondemand ondemand;
    } u;

    int32_t turbo_enabled;
};

struct xen_set_cpufreq_gov {
    char scaling_governor[CPUFREQ_NAME_LEN];
};

struct xen_set_cpufreq_para {
    #define SCALING_MAX_FREQ           1
    #define SCALING_MIN_FREQ           2
    #define SCALING_SETSPEED           3
    #define SAMPLING_RATE              4
    #define UP_THRESHOLD               5

    uint32_t ctrl_type;
    uint32_t ctrl_value;
};

struct xen_sysctl_pm_op {
    #define PM_PARA_CATEGORY_MASK      0xf0
    #define CPUFREQ_PARA               0x10

    /* cpufreq command type */
    #define GET_CPUFREQ_PARA           (CPUFREQ_PARA | 0x01)
    #define SET_CPUFREQ_GOV            (CPUFREQ_PARA | 0x02)
    #define SET_CPUFREQ_PARA           (CPUFREQ_PARA | 0x03)
    #define GET_CPUFREQ_AVGFREQ        (CPUFREQ_PARA | 0x04)

    /* set/reset scheduler power saving option */
    #define XEN_SYSCTL_pm_op_set_sched_opt_smt    0x21

    /* cpuidle max_cstate access command */
    #define XEN_SYSCTL_pm_op_get_max_cstate       0x22
    #define XEN_SYSCTL_pm_op_set_max_cstate       0x23

    /* set scheduler migration cost value */
    #define XEN_SYSCTL_pm_op_set_vcpu_migration_delay   0x24
    #define XEN_SYSCTL_pm_op_get_vcpu_migration_delay   0x25

    /* enable/disable turbo mode when in dbs governor */
    #define XEN_SYSCTL_pm_op_enable_turbo               0x26
    #define XEN_SYSCTL_pm_op_disable_turbo              0x27

    uint32_t cmd;
    uint32_t cpuid;
    union {
        struct xen_get_cpufreq_para get_para;
        struct xen_set_cpufreq_gov  set_gov;
        struct xen_set_cpufreq_para set_para;
        uint64_aligned_t get_avgfreq;
        uint32_t                    set_sched_opt_smt;
        uint32_t                    get_max_cstate;
        uint32_t                    set_max_cstate;
        uint32_t                    get_vcpu_migration_delay;
        uint32_t                    set_vcpu_migration_delay;
    } u;
};

/* XEN_SYSCTL_page_offline_op */
struct xen_sysctl_page_offline_op {
    /* IN: range of page to be offlined */
#define sysctl_page_offline     1
#define sysctl_page_online      2
#define sysctl_query_page_offline  3
    uint32_t cmd;
    uint32_t start;
    uint32_t end;
    /* OUT: result of page offline request */
    /*
     * bit 0~15: result flags
     * bit 16~31: owner
     */
    XEN_GUEST_HANDLE(uint32) status;
};

#define PG_OFFLINE_STATUS_MASK    (0xFFUL)

/* The result is invalid, i.e. HV does not handle it */
#define PG_OFFLINE_INVALID   (0x1UL << 0)

#define PG_OFFLINE_OFFLINED  (0x1UL << 1)
#define PG_OFFLINE_PENDING   (0x1UL << 2)
#define PG_OFFLINE_FAILED    (0x1UL << 3)
#define PG_OFFLINE_AGAIN     (0x1UL << 4)

#define PG_ONLINE_FAILED     PG_OFFLINE_FAILED
#define PG_ONLINE_ONLINED    PG_OFFLINE_OFFLINED

#define PG_OFFLINE_STATUS_OFFLINED              (0x1UL << 1)
#define PG_OFFLINE_STATUS_ONLINE                (0x1UL << 2)
#define PG_OFFLINE_STATUS_OFFLINE_PENDING       (0x1UL << 3)
#define PG_OFFLINE_STATUS_BROKEN                (0x1UL << 4)

#define PG_OFFLINE_MISC_MASK    (0xFFUL << 4)

/* valid when PG_OFFLINE_FAILED or PG_OFFLINE_PENDING */
#define PG_OFFLINE_XENPAGE   (0x1UL << 8)
#define PG_OFFLINE_DOM0PAGE  (0x1UL << 9)
#define PG_OFFLINE_ANONYMOUS (0x1UL << 10)
#define PG_OFFLINE_NOT_CONV_RAM   (0x1UL << 11)
#define PG_OFFLINE_OWNED     (0x1UL << 12)

#define PG_OFFLINE_BROKEN    (0x1UL << 13)
#define PG_ONLINE_BROKEN     PG_OFFLINE_BROKEN

#define PG_OFFLINE_OWNER_SHIFT 16

/* XEN_SYSCTL_lockprof_op */
/* Sub-operations: */
#define XEN_SYSCTL_LOCKPROF_reset 1   /* Reset all profile data to zero. */
#define XEN_SYSCTL_LOCKPROF_query 2   /* Get lock profile information. */
/* Record-type: */
#define LOCKPROF_TYPE_GLOBAL      0   /* global lock, idx meaningless */
#define LOCKPROF_TYPE_PERDOM      1   /* per-domain lock, idx is domid */
#define LOCKPROF_TYPE_N           2   /* number of types */
struct xen_sysctl_lockprof_data {
    char     name[40];     /* lock name (may include up to 2 %d specifiers) */
    int32_t  type;         /* LOCKPROF_TYPE_??? */
    int32_t  idx;          /* index (e.g. domain id) */
    uint64_aligned_t lock_cnt;     /* # of locking succeeded */
    uint64_aligned_t block_cnt;    /* # of wait for lock */
    uint64_aligned_t lock_time;    /* nsecs lock held */
    uint64_aligned_t block_time;   /* nsecs waited for lock */
};
typedef struct xen_sysctl_lockprof_data xen_sysctl_lockprof_data_t;
DEFINE_XEN_GUEST_HANDLE(xen_sysctl_lockprof_data_t);
struct xen_sysctl_lockprof_op {
    /* IN variables. */
    uint32_t       cmd;               /* XEN_SYSCTL_LOCKPROF_??? */
    uint32_t       max_elem;          /* size of output buffer */
    /* OUT variables (query only). */
    uint32_t       nr_elem;           /* number of elements available */
    uint64_aligned_t time;            /* nsecs of profile measurement */
    /* profile information (or NULL) */
    XEN_GUEST_HANDLE_64(xen_sysctl_lockprof_data_t) data;
};
typedef struct xen_sysctl_lockprof_op xen_sysctl_lockprof_op_t;
DEFINE_XEN_GUEST_HANDLE(xen_sysctl_lockprof_op_t);

/* XEN_SYSCTL_cputopoinfo */
#define XEN_INVALID_CORE_ID     (~0U)
#define XEN_INVALID_SOCKET_ID   (~0U)
#define XEN_INVALID_NODE_ID     (~0U)

struct xen_sysctl_cputopo {
    uint32_t core;
    uint32_t socket;
    uint32_t node;
};
typedef struct xen_sysctl_cputopo xen_sysctl_cputopo_t;
DEFINE_XEN_GUEST_HANDLE(xen_sysctl_cputopo_t);

/*
 * IN:
 *  - a NULL 'cputopo' handle is a request for maximun 'num_cpus'.
 *  - otherwise it's the number of entries in 'cputopo'
 *
 * OUT:
 *  - If 'num_cpus' is less than the number Xen wants to write but the handle
 *    handle is not a NULL one, partial data gets returned and 'num_cpus' gets
 *    updated to reflect the intended number.
 *  - Otherwise, 'num_cpus' shall indicate the number of entries written, which
 *    may be less than the input value.
 */
struct xen_sysctl_cputopoinfo {
    uint32_t num_cpus;
    XEN_GUEST_HANDLE_64(xen_sysctl_cputopo_t) cputopo;
};
typedef struct xen_sysctl_cputopoinfo xen_sysctl_cputopoinfo_t;
DEFINE_XEN_GUEST_HANDLE(xen_sysctl_cputopoinfo_t);

/* XEN_SYSCTL_numainfo */
#define XEN_INVALID_MEM_SZ     (~0U)
#define XEN_INVALID_NODE_DIST  (~0U)

struct xen_sysctl_meminfo {
    uint64_t memsize;
    uint64_t memfree;
};
typedef struct xen_sysctl_meminfo xen_sysctl_meminfo_t;
DEFINE_XEN_GUEST_HANDLE(xen_sysctl_meminfo_t);

/*
 * IN:
 *  - Both 'meminfo' and 'distance' handles being null is a request
 *    for maximum value of 'num_nodes'.
 *  - Otherwise it's the number of entries in 'meminfo' and square root
 *    of number of entries in 'distance' (when corresponding handle is
 *    non-null)
 *
 * OUT:
 *  - If 'num_nodes' is less than the number Xen wants to write but either
 *    handle is not a NULL one, partial data gets returned and 'num_nodes'
 *    gets updated to reflect the intended number.
 *  - Otherwise, 'num_nodes' shall indicate the number of entries written, which
 *    may be less than the input value.
 */

struct xen_sysctl_numainfo {
    uint32_t num_nodes;

    XEN_GUEST_HANDLE_64(xen_sysctl_meminfo_t) meminfo;

    /*
     * Distance between nodes 'i' and 'j' is stored in index 'i*N + j',
     * where N is the number of nodes that will be returned in 'num_nodes'
     * (i.e. not 'num_nodes' provided by the caller)
     */
    XEN_GUEST_HANDLE_64(uint32) distance;
};
typedef struct xen_sysctl_numainfo xen_sysctl_numainfo_t;
DEFINE_XEN_GUEST_HANDLE(xen_sysctl_numainfo_t);

/* XEN_SYSCTL_cpupool_op */
#define XEN_SYSCTL_CPUPOOL_OP_CREATE                1  /* C */
#define XEN_SYSCTL_CPUPOOL_OP_DESTROY               2  /* D */
#define XEN_SYSCTL_CPUPOOL_OP_INFO                  3  /* I */
#define XEN_SYSCTL_CPUPOOL_OP_ADDCPU                4  /* A */
#define XEN_SYSCTL_CPUPOOL_OP_RMCPU                 5  /* R */
#define XEN_SYSCTL_CPUPOOL_OP_MOVEDOMAIN            6  /* M */
#define XEN_SYSCTL_CPUPOOL_OP_FREEINFO              7  /* F */
#define XEN_SYSCTL_CPUPOOL_PAR_ANY     0xFFFFFFFF
struct xen_sysctl_cpupool_op {
    uint32_t op;          /* IN */
    uint32_t cpupool_id;  /* IN: CDIARM OUT: CI */
    uint32_t sched_id;    /* IN: C      OUT: I  */
    uint32_t domid;       /* IN: M              */
    uint32_t cpu;         /* IN: AR             */
    uint32_t n_dom;       /*            OUT: I  */
    struct xenctl_bitmap cpumap; /*     OUT: IF */
};
typedef struct xen_sysctl_cpupool_op xen_sysctl_cpupool_op_t;
DEFINE_XEN_GUEST_HANDLE(xen_sysctl_cpupool_op_t);

#define ARINC653_MAX_DOMAINS_PER_SCHEDULE   64
/*
 * This structure is used to pass a new ARINC653 schedule from a
 * privileged domain (ie dom0) to Xen.
 */
struct xen_sysctl_arinc653_schedule {
    /* major_frame holds the time for the new schedule's major frame
     * in nanoseconds. */
    uint64_aligned_t     major_frame;
    /* num_sched_entries holds how many of the entries in the
     * sched_entries[] array are valid. */
    uint8_t     num_sched_entries;
    /* The sched_entries array holds the actual schedule entries. */
    struct {
        /* dom_handle must match a domain's UUID */
        xen_domain_handle_t dom_handle;
        /* If a domain has multiple VCPUs, vcpu_id specifies which one
         * this schedule entry applies to. It should be set to 0 if
         * there is only one VCPU for the domain. */
        unsigned int vcpu_id;
        /* runtime specifies the amount of time that should be allocated
         * to this VCPU per major frame. It is specified in nanoseconds */
        uint64_aligned_t runtime;
    } sched_entries[ARINC653_MAX_DOMAINS_PER_SCHEDULE];
};
typedef struct xen_sysctl_arinc653_schedule xen_sysctl_arinc653_schedule_t;
DEFINE_XEN_GUEST_HANDLE(xen_sysctl_arinc653_schedule_t);

struct xen_sysctl_credit_schedule {
    /* Length of timeslice in milliseconds */
#define XEN_SYSCTL_CSCHED_TSLICE_MAX 1000
#define XEN_SYSCTL_CSCHED_TSLICE_MIN 1
    unsigned tslice_ms;
    /* Rate limit (minimum timeslice) in microseconds */
#define XEN_SYSCTL_SCHED_RATELIMIT_MAX 500000
#define XEN_SYSCTL_SCHED_RATELIMIT_MIN 100
    unsigned ratelimit_us;
};
typedef struct xen_sysctl_credit_schedule xen_sysctl_credit_schedule_t;
DEFINE_XEN_GUEST_HANDLE(xen_sysctl_credit_schedule_t);

/* XEN_SYSCTL_scheduler_op */
/* Set or get info? */
#define XEN_SYSCTL_SCHEDOP_putinfo 0
#define XEN_SYSCTL_SCHEDOP_getinfo 1
struct xen_sysctl_scheduler_op {
    uint32_t cpupool_id; /* Cpupool whose scheduler is to be targetted. */
    uint32_t sched_id;   /* XEN_SCHEDULER_* (domctl.h) */
    uint32_t cmd;        /* XEN_SYSCTL_SCHEDOP_* */
    union {
        struct xen_sysctl_sched_arinc653 {
            XEN_GUEST_HANDLE_64(xen_sysctl_arinc653_schedule_t) schedule;
        } sched_arinc653;
        struct xen_sysctl_credit_schedule sched_credit;
    } u;
};
typedef struct xen_sysctl_scheduler_op xen_sysctl_scheduler_op_t;
DEFINE_XEN_GUEST_HANDLE(xen_sysctl_scheduler_op_t);

/* XEN_SYSCTL_coverage_op */
/*
 * Get total size of information, to help allocate
 * the buffer. The pointer points to a 32 bit value.
 */
#define XEN_SYSCTL_COVERAGE_get_total_size 0

/*
 * Read coverage information in a single run
 * You must use a tool to split them.
 */
#define XEN_SYSCTL_COVERAGE_read           1

/*
 * Reset all the coverage counters to 0
 * No parameters.
 */
#define XEN_SYSCTL_COVERAGE_reset          2

/*
 * Like XEN_SYSCTL_COVERAGE_read but reset also
 * counters to 0 in a single call.
 */
#define XEN_SYSCTL_COVERAGE_read_and_reset 3

struct xen_sysctl_coverage_op {
    uint32_t cmd;        /* XEN_SYSCTL_COVERAGE_* */
    union {
        uint32_t total_size; /* OUT */
        XEN_GUEST_HANDLE_64(uint8)  raw_info;   /* OUT */
    } u;
};
typedef struct xen_sysctl_coverage_op xen_sysctl_coverage_op_t;
DEFINE_XEN_GUEST_HANDLE(xen_sysctl_coverage_op_t);

#define XEN_SYSCTL_PSR_CMT_get_total_rmid            0
#define XEN_SYSCTL_PSR_CMT_get_l3_upscaling_factor   1
/* The L3 cache size is returned in KB unit */
#define XEN_SYSCTL_PSR_CMT_get_l3_cache_size         2
#define XEN_SYSCTL_PSR_CMT_enabled                   3
#define XEN_SYSCTL_PSR_CMT_get_l3_event_mask         4
struct xen_sysctl_psr_cmt_op {
    uint32_t cmd;       /* IN: XEN_SYSCTL_PSR_CMT_* */
    uint32_t flags;     /* padding variable, may be extended for future use */
    union {
        uint64_t data;  /* OUT */
        struct {
            uint32_t cpu;   /* IN */
            uint32_t rsvd;
        } l3_cache;
    } u;
};
typedef struct xen_sysctl_psr_cmt_op xen_sysctl_psr_cmt_op_t;
DEFINE_XEN_GUEST_HANDLE(xen_sysctl_psr_cmt_op_t);

/* XEN_SYSCTL_pcitopoinfo */
#define XEN_INVALID_DEV (XEN_INVALID_NODE_ID - 1)
struct xen_sysctl_pcitopoinfo {
    /*
     * IN: Number of elements in 'pcitopo' and 'nodes' arrays.
     * OUT: Number of processed elements of those arrays.
     */
    uint32_t num_devs;

    /* IN: list of devices for which node IDs are requested. */
    XEN_GUEST_HANDLE_64(physdev_pci_device_t) devs;

    /*
     * OUT: node identifier for each device.
     * If information for a particular device is not available then
     * corresponding entry will be set to XEN_INVALID_NODE_ID. If
     * device is not known to the hypervisor then XEN_INVALID_DEV
     * will be provided.
     */
    XEN_GUEST_HANDLE_64(uint32) nodes;
};
typedef struct xen_sysctl_pcitopoinfo xen_sysctl_pcitopoinfo_t;
DEFINE_XEN_GUEST_HANDLE(xen_sysctl_pcitopoinfo_t);

#define XEN_SYSCTL_PSR_CAT_get_l3_info               0
struct xen_sysctl_psr_cat_op {
    uint32_t cmd;       /* IN: XEN_SYSCTL_PSR_CAT_* */
    uint32_t target;    /* IN */
    union {
        struct {
            uint32_t cbm_len;   /* OUT: CBM length */
            uint32_t cos_max;   /* OUT: Maximum COS */
        } l3_info;
    } u;
};
typedef struct xen_sysctl_psr_cat_op xen_sysctl_psr_cat_op_t;
DEFINE_XEN_GUEST_HANDLE(xen_sysctl_psr_cat_op_t);

#define XEN_SYSCTL_TMEM_OP_ALL_CLIENTS 0xFFFFU

#define XEN_SYSCTL_TMEM_OP_THAW                   0
#define XEN_SYSCTL_TMEM_OP_FREEZE                 1
#define XEN_SYSCTL_TMEM_OP_FLUSH                  2
#define XEN_SYSCTL_TMEM_OP_DESTROY                3
#define XEN_SYSCTL_TMEM_OP_LIST                   4
#define XEN_SYSCTL_TMEM_OP_SET_WEIGHT             5
#define XEN_SYSCTL_TMEM_OP_SET_CAP                6
#define XEN_SYSCTL_TMEM_OP_SET_COMPRESS           7
#define XEN_SYSCTL_TMEM_OP_QUERY_FREEABLE_MB      8
#define XEN_SYSCTL_TMEM_OP_SAVE_BEGIN             10
#define XEN_SYSCTL_TMEM_OP_SAVE_GET_VERSION       11
#define XEN_SYSCTL_TMEM_OP_SAVE_GET_MAXPOOLS      12
#define XEN_SYSCTL_TMEM_OP_SAVE_GET_CLIENT_WEIGHT 13
#define XEN_SYSCTL_TMEM_OP_SAVE_GET_CLIENT_CAP    14
#define XEN_SYSCTL_TMEM_OP_SAVE_GET_CLIENT_FLAGS  15
#define XEN_SYSCTL_TMEM_OP_SAVE_GET_POOL_FLAGS    16
#define XEN_SYSCTL_TMEM_OP_SAVE_GET_POOL_NPAGES   17
#define XEN_SYSCTL_TMEM_OP_SAVE_GET_POOL_UUID     18
#define XEN_SYSCTL_TMEM_OP_SAVE_GET_NEXT_PAGE     19
#define XEN_SYSCTL_TMEM_OP_SAVE_GET_NEXT_INV      20
#define XEN_SYSCTL_TMEM_OP_SAVE_END               21
#define XEN_SYSCTL_TMEM_OP_RESTORE_BEGIN          30
#define XEN_SYSCTL_TMEM_OP_RESTORE_PUT_PAGE       32
#define XEN_SYSCTL_TMEM_OP_RESTORE_FLUSH_PAGE     33

/*
 * XEN_SYSCTL_TMEM_OP_SAVE_GET_NEXT_[PAGE|INV] override the 'buf' in
 * xen_sysctl_tmem_op with this structure - sometimes with an extra
 * page tackled on.
 */
struct tmem_handle {
    uint32_t pool_id;
    uint32_t index;
    xen_tmem_oid_t oid;
};

struct xen_sysctl_tmem_op {
    uint32_t cmd;       /* IN: XEN_SYSCTL_TMEM_OP_* . */
    int32_t pool_id;    /* IN: 0 by default unless _SAVE_*, RESTORE_* .*/
    uint32_t cli_id;    /* IN: client id, 0 for XEN_SYSCTL_TMEM_QUERY_FREEABLE_MB
                           for all others can be the domain id or
                           XEN_SYSCTL_TMEM_OP_ALL_CLIENTS for all. */
    uint32_t arg1;      /* IN: If not applicable to command use 0. */
    uint32_t arg2;      /* IN: If not applicable to command use 0. */
    uint32_t pad;       /* Padding so structure is the same under 32 and 64. */
    xen_tmem_oid_t oid; /* IN: If not applicable to command use 0s. */
    XEN_GUEST_HANDLE_64(char) buf; /* IN/OUT: Buffer to save and restore ops. */
};
typedef struct xen_sysctl_tmem_op xen_sysctl_tmem_op_t;
DEFINE_XEN_GUEST_HANDLE(xen_sysctl_tmem_op_t);

struct xen_sysctl {
    uint32_t cmd;
#define XEN_SYSCTL_readconsole                    1
#define XEN_SYSCTL_tbuf_op                        2
#define XEN_SYSCTL_physinfo                       3
#define XEN_SYSCTL_sched_id                       4
#define XEN_SYSCTL_perfc_op                       5
#define XEN_SYSCTL_getdomaininfolist              6
#define XEN_SYSCTL_debug_keys                     7
#define XEN_SYSCTL_getcpuinfo                     8
#define XEN_SYSCTL_availheap                      9
#define XEN_SYSCTL_get_pmstat                    10
#define XEN_SYSCTL_cpu_hotplug                   11
#define XEN_SYSCTL_pm_op                         12
#define XEN_SYSCTL_page_offline_op               14
#define XEN_SYSCTL_lockprof_op                   15
#define XEN_SYSCTL_cputopoinfo                   16
#define XEN_SYSCTL_numainfo                      17
#define XEN_SYSCTL_cpupool_op                    18
#define XEN_SYSCTL_scheduler_op                  19
#define XEN_SYSCTL_coverage_op                   20
#define XEN_SYSCTL_psr_cmt_op                    21
#define XEN_SYSCTL_pcitopoinfo                   22
#define XEN_SYSCTL_psr_cat_op                    23
#define XEN_SYSCTL_tmem_op                       24
    uint32_t interface_version; /* XEN_SYSCTL_INTERFACE_VERSION */
    union {
        struct xen_sysctl_readconsole       readconsole;
        struct xen_sysctl_tbuf_op           tbuf_op;
        struct xen_sysctl_physinfo          physinfo;
        struct xen_sysctl_cputopoinfo       cputopoinfo;
        struct xen_sysctl_pcitopoinfo       pcitopoinfo;
        struct xen_sysctl_numainfo          numainfo;
        struct xen_sysctl_sched_id          sched_id;
        struct xen_sysctl_perfc_op          perfc_op;
        struct xen_sysctl_getdomaininfolist getdomaininfolist;
        struct xen_sysctl_debug_keys        debug_keys;
        struct xen_sysctl_getcpuinfo        getcpuinfo;
        struct xen_sysctl_availheap         availheap;
        struct xen_sysctl_get_pmstat        get_pmstat;
        struct xen_sysctl_cpu_hotplug       cpu_hotplug;
        struct xen_sysctl_pm_op             pm_op;
        struct xen_sysctl_page_offline_op   page_offline;
        struct xen_sysctl_lockprof_op       lockprof_op;
        struct xen_sysctl_cpupool_op        cpupool_op;
        struct xen_sysctl_scheduler_op      scheduler_op;
        struct xen_sysctl_coverage_op       coverage_op;
        struct xen_sysctl_psr_cmt_op        psr_cmt_op;
        struct xen_sysctl_psr_cat_op        psr_cat_op;
        struct xen_sysctl_tmem_op           tmem_op;
        uint8_t                             pad[128];
    } u;
};
typedef struct xen_sysctl xen_sysctl_t;
DEFINE_XEN_GUEST_HANDLE(xen_sysctl_t);

#endif /* __XEN_PUBLIC_SYSCTL_H__ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
