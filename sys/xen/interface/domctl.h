/******************************************************************************
 * domctl.h
 * 
 * Domain management operations. For use by node control stack.
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
 * Copyright (c) 2002-2003, B Dragovic
 * Copyright (c) 2002-2006, K Fraser
 */

#ifndef __XEN_PUBLIC_DOMCTL_H__
#define __XEN_PUBLIC_DOMCTL_H__

#if !defined(__XEN__) && !defined(__XEN_TOOLS__)
#error "domctl operations are intended for use by node control tools only"
#endif

#include "xen.h"
#include "grant_table.h"
#include "hvm/save.h"
#include "memory.h"

#define XEN_DOMCTL_INTERFACE_VERSION 0x0000000b

/*
 * NB. xen_domctl.domain is an IN/OUT parameter for this operation.
 * If it is specified as zero, an id is auto-allocated and returned.
 */
/* XEN_DOMCTL_createdomain */
struct xen_domctl_createdomain {
    /* IN parameters */
    uint32_t ssidref;
    xen_domain_handle_t handle;
 /* Is this an HVM guest (as opposed to a PVH or PV guest)? */
#define _XEN_DOMCTL_CDF_hvm_guest     0
#define XEN_DOMCTL_CDF_hvm_guest      (1U<<_XEN_DOMCTL_CDF_hvm_guest)
 /* Use hardware-assisted paging if available? */
#define _XEN_DOMCTL_CDF_hap           1
#define XEN_DOMCTL_CDF_hap            (1U<<_XEN_DOMCTL_CDF_hap)
 /* Should domain memory integrity be verifed by tboot during Sx? */
#define _XEN_DOMCTL_CDF_s3_integrity  2
#define XEN_DOMCTL_CDF_s3_integrity   (1U<<_XEN_DOMCTL_CDF_s3_integrity)
 /* Disable out-of-sync shadow page tables? */
#define _XEN_DOMCTL_CDF_oos_off       3
#define XEN_DOMCTL_CDF_oos_off        (1U<<_XEN_DOMCTL_CDF_oos_off)
 /* Is this a PVH guest (as opposed to an HVM or PV guest)? */
#define _XEN_DOMCTL_CDF_pvh_guest     4
#define XEN_DOMCTL_CDF_pvh_guest      (1U<<_XEN_DOMCTL_CDF_pvh_guest)
    uint32_t flags;
    struct xen_arch_domainconfig config;
};
typedef struct xen_domctl_createdomain xen_domctl_createdomain_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_createdomain_t);

/* XEN_DOMCTL_getdomaininfo */
struct xen_domctl_getdomaininfo {
    /* OUT variables. */
    domid_t  domain;              /* Also echoed in domctl.domain */
 /* Domain is scheduled to die. */
#define _XEN_DOMINF_dying     0
#define XEN_DOMINF_dying      (1U<<_XEN_DOMINF_dying)
 /* Domain is an HVM guest (as opposed to a PV guest). */
#define _XEN_DOMINF_hvm_guest 1
#define XEN_DOMINF_hvm_guest  (1U<<_XEN_DOMINF_hvm_guest)
 /* The guest OS has shut down. */
#define _XEN_DOMINF_shutdown  2
#define XEN_DOMINF_shutdown   (1U<<_XEN_DOMINF_shutdown)
 /* Currently paused by control software. */
#define _XEN_DOMINF_paused    3
#define XEN_DOMINF_paused     (1U<<_XEN_DOMINF_paused)
 /* Currently blocked pending an event.     */
#define _XEN_DOMINF_blocked   4
#define XEN_DOMINF_blocked    (1U<<_XEN_DOMINF_blocked)
 /* Domain is currently running.            */
#define _XEN_DOMINF_running   5
#define XEN_DOMINF_running    (1U<<_XEN_DOMINF_running)
 /* Being debugged.  */
#define _XEN_DOMINF_debugged  6
#define XEN_DOMINF_debugged   (1U<<_XEN_DOMINF_debugged)
/* domain is PVH */
#define _XEN_DOMINF_pvh_guest 7
#define XEN_DOMINF_pvh_guest  (1U<<_XEN_DOMINF_pvh_guest)
 /* XEN_DOMINF_shutdown guest-supplied code.  */
#define XEN_DOMINF_shutdownmask 255
#define XEN_DOMINF_shutdownshift 16
    uint32_t flags;              /* XEN_DOMINF_* */
    uint64_aligned_t tot_pages;
    uint64_aligned_t max_pages;
    uint64_aligned_t outstanding_pages;
    uint64_aligned_t shr_pages;
    uint64_aligned_t paged_pages;
    uint64_aligned_t shared_info_frame; /* GMFN of shared_info struct */
    uint64_aligned_t cpu_time;
    uint32_t nr_online_vcpus;    /* Number of VCPUs currently online. */
#define XEN_INVALID_MAX_VCPU_ID (~0U) /* Domain has no vcpus? */
    uint32_t max_vcpu_id;        /* Maximum VCPUID in use by this domain. */
    uint32_t ssidref;
    xen_domain_handle_t handle;
    uint32_t cpupool;
};
typedef struct xen_domctl_getdomaininfo xen_domctl_getdomaininfo_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_getdomaininfo_t);


/* XEN_DOMCTL_getmemlist */
struct xen_domctl_getmemlist {
    /* IN variables. */
    /* Max entries to write to output buffer. */
    uint64_aligned_t max_pfns;
    /* Start index in guest's page list. */
    uint64_aligned_t start_pfn;
    XEN_GUEST_HANDLE_64(uint64) buffer;
    /* OUT variables. */
    uint64_aligned_t num_pfns;
};
typedef struct xen_domctl_getmemlist xen_domctl_getmemlist_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_getmemlist_t);


/* XEN_DOMCTL_getpageframeinfo */

#define XEN_DOMCTL_PFINFO_LTAB_SHIFT 28
#define XEN_DOMCTL_PFINFO_NOTAB   (0x0U<<28)
#define XEN_DOMCTL_PFINFO_L1TAB   (0x1U<<28)
#define XEN_DOMCTL_PFINFO_L2TAB   (0x2U<<28)
#define XEN_DOMCTL_PFINFO_L3TAB   (0x3U<<28)
#define XEN_DOMCTL_PFINFO_L4TAB   (0x4U<<28)
#define XEN_DOMCTL_PFINFO_LTABTYPE_MASK (0x7U<<28)
#define XEN_DOMCTL_PFINFO_LPINTAB (0x1U<<31)
#define XEN_DOMCTL_PFINFO_XTAB    (0xfU<<28) /* invalid page */
#define XEN_DOMCTL_PFINFO_XALLOC  (0xeU<<28) /* allocate-only page */
#define XEN_DOMCTL_PFINFO_BROKEN  (0xdU<<28) /* broken page */
#define XEN_DOMCTL_PFINFO_LTAB_MASK (0xfU<<28)

/* XEN_DOMCTL_getpageframeinfo3 */
struct xen_domctl_getpageframeinfo3 {
    /* IN variables. */
    uint64_aligned_t num;
    /* IN/OUT variables. */
    XEN_GUEST_HANDLE_64(xen_pfn_t) array;
};


/*
 * Control shadow pagetables operation
 */
/* XEN_DOMCTL_shadow_op */

/* Disable shadow mode. */
#define XEN_DOMCTL_SHADOW_OP_OFF         0

/* Enable shadow mode (mode contains ORed XEN_DOMCTL_SHADOW_ENABLE_* flags). */
#define XEN_DOMCTL_SHADOW_OP_ENABLE      32

/* Log-dirty bitmap operations. */
 /* Return the bitmap and clean internal copy for next round. */
#define XEN_DOMCTL_SHADOW_OP_CLEAN       11
 /* Return the bitmap but do not modify internal copy. */
#define XEN_DOMCTL_SHADOW_OP_PEEK        12

/* Memory allocation accessors. */
#define XEN_DOMCTL_SHADOW_OP_GET_ALLOCATION   30
#define XEN_DOMCTL_SHADOW_OP_SET_ALLOCATION   31

/* Legacy enable operations. */
 /* Equiv. to ENABLE with no mode flags. */
#define XEN_DOMCTL_SHADOW_OP_ENABLE_TEST       1
 /* Equiv. to ENABLE with mode flag ENABLE_LOG_DIRTY. */
#define XEN_DOMCTL_SHADOW_OP_ENABLE_LOGDIRTY   2
 /* Equiv. to ENABLE with mode flags ENABLE_REFCOUNT and ENABLE_TRANSLATE. */
#define XEN_DOMCTL_SHADOW_OP_ENABLE_TRANSLATE  3

/* Mode flags for XEN_DOMCTL_SHADOW_OP_ENABLE. */
 /*
  * Shadow pagetables are refcounted: guest does not use explicit mmu
  * operations nor write-protect its pagetables.
  */
#define XEN_DOMCTL_SHADOW_ENABLE_REFCOUNT  (1 << 1)
 /*
  * Log pages in a bitmap as they are dirtied.
  * Used for live relocation to determine which pages must be re-sent.
  */
#define XEN_DOMCTL_SHADOW_ENABLE_LOG_DIRTY (1 << 2)
 /*
  * Automatically translate GPFNs into MFNs.
  */
#define XEN_DOMCTL_SHADOW_ENABLE_TRANSLATE (1 << 3)
 /*
  * Xen does not steal virtual address space from the guest.
  * Requires HVM support.
  */
#define XEN_DOMCTL_SHADOW_ENABLE_EXTERNAL  (1 << 4)

struct xen_domctl_shadow_op_stats {
    uint32_t fault_count;
    uint32_t dirty_count;
};
typedef struct xen_domctl_shadow_op_stats xen_domctl_shadow_op_stats_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_shadow_op_stats_t);

struct xen_domctl_shadow_op {
    /* IN variables. */
    uint32_t       op;       /* XEN_DOMCTL_SHADOW_OP_* */

    /* OP_ENABLE */
    uint32_t       mode;     /* XEN_DOMCTL_SHADOW_ENABLE_* */

    /* OP_GET_ALLOCATION / OP_SET_ALLOCATION */
    uint32_t       mb;       /* Shadow memory allocation in MB */

    /* OP_PEEK / OP_CLEAN */
    XEN_GUEST_HANDLE_64(uint8) dirty_bitmap;
    uint64_aligned_t pages; /* Size of buffer. Updated with actual size. */
    struct xen_domctl_shadow_op_stats stats;
};
typedef struct xen_domctl_shadow_op xen_domctl_shadow_op_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_shadow_op_t);


/* XEN_DOMCTL_max_mem */
struct xen_domctl_max_mem {
    /* IN variables. */
    uint64_aligned_t max_memkb;
};
typedef struct xen_domctl_max_mem xen_domctl_max_mem_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_max_mem_t);


/* XEN_DOMCTL_setvcpucontext */
/* XEN_DOMCTL_getvcpucontext */
struct xen_domctl_vcpucontext {
    uint32_t              vcpu;                  /* IN */
    XEN_GUEST_HANDLE_64(vcpu_guest_context_t) ctxt; /* IN/OUT */
};
typedef struct xen_domctl_vcpucontext xen_domctl_vcpucontext_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_vcpucontext_t);


/* XEN_DOMCTL_getvcpuinfo */
struct xen_domctl_getvcpuinfo {
    /* IN variables. */
    uint32_t vcpu;
    /* OUT variables. */
    uint8_t  online;                  /* currently online (not hotplugged)? */
    uint8_t  blocked;                 /* blocked waiting for an event? */
    uint8_t  running;                 /* currently scheduled on its CPU? */
    uint64_aligned_t cpu_time;        /* total cpu time consumed (ns) */
    uint32_t cpu;                     /* current mapping   */
};
typedef struct xen_domctl_getvcpuinfo xen_domctl_getvcpuinfo_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_getvcpuinfo_t);


/* Get/set the NUMA node(s) with which the guest has affinity with. */
/* XEN_DOMCTL_setnodeaffinity */
/* XEN_DOMCTL_getnodeaffinity */
struct xen_domctl_nodeaffinity {
    struct xenctl_bitmap nodemap;/* IN */
};
typedef struct xen_domctl_nodeaffinity xen_domctl_nodeaffinity_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_nodeaffinity_t);


/* Get/set which physical cpus a vcpu can execute on. */
/* XEN_DOMCTL_setvcpuaffinity */
/* XEN_DOMCTL_getvcpuaffinity */
struct xen_domctl_vcpuaffinity {
    /* IN variables. */
    uint32_t  vcpu;
 /* Set/get the hard affinity for vcpu */
#define _XEN_VCPUAFFINITY_HARD  0
#define XEN_VCPUAFFINITY_HARD   (1U<<_XEN_VCPUAFFINITY_HARD)
 /* Set/get the soft affinity for vcpu */
#define _XEN_VCPUAFFINITY_SOFT  1
#define XEN_VCPUAFFINITY_SOFT   (1U<<_XEN_VCPUAFFINITY_SOFT)
    uint32_t flags;
    /*
     * IN/OUT variables.
     *
     * Both are IN/OUT for XEN_DOMCTL_setvcpuaffinity, in which case they
     * contain effective hard or/and soft affinity. That is, upon successful
     * return, cpumap_soft, contains the intersection of the soft affinity,
     * hard affinity and the cpupool's online CPUs for the domain (if
     * XEN_VCPUAFFINITY_SOFT was set in flags). cpumap_hard contains the
     * intersection between hard affinity and the cpupool's online CPUs (if
     * XEN_VCPUAFFINITY_HARD was set in flags).
     *
     * Both are OUT-only for XEN_DOMCTL_getvcpuaffinity, in which case they
     * contain the plain hard and/or soft affinity masks that were set during
     * previous successful calls to XEN_DOMCTL_setvcpuaffinity (or the
     * default values), without intersecting or altering them in any way.
     */
    struct xenctl_bitmap cpumap_hard;
    struct xenctl_bitmap cpumap_soft;
};
typedef struct xen_domctl_vcpuaffinity xen_domctl_vcpuaffinity_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_vcpuaffinity_t);


/* XEN_DOMCTL_max_vcpus */
struct xen_domctl_max_vcpus {
    uint32_t max;           /* maximum number of vcpus */
};
typedef struct xen_domctl_max_vcpus xen_domctl_max_vcpus_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_max_vcpus_t);


/* XEN_DOMCTL_scheduler_op */
/* Scheduler types. */
/* #define XEN_SCHEDULER_SEDF  4 (Removed) */
#define XEN_SCHEDULER_CREDIT   5
#define XEN_SCHEDULER_CREDIT2  6
#define XEN_SCHEDULER_ARINC653 7
#define XEN_SCHEDULER_RTDS     8

/* Set or get info? */
#define XEN_DOMCTL_SCHEDOP_putinfo 0
#define XEN_DOMCTL_SCHEDOP_getinfo 1
struct xen_domctl_scheduler_op {
    uint32_t sched_id;  /* XEN_SCHEDULER_* */
    uint32_t cmd;       /* XEN_DOMCTL_SCHEDOP_* */
    union {
        struct xen_domctl_sched_credit {
            uint16_t weight;
            uint16_t cap;
        } credit;
        struct xen_domctl_sched_credit2 {
            uint16_t weight;
        } credit2;
        struct xen_domctl_sched_rtds {
            uint32_t period;
            uint32_t budget;
        } rtds;
    } u;
};
typedef struct xen_domctl_scheduler_op xen_domctl_scheduler_op_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_scheduler_op_t);


/* XEN_DOMCTL_setdomainhandle */
struct xen_domctl_setdomainhandle {
    xen_domain_handle_t handle;
};
typedef struct xen_domctl_setdomainhandle xen_domctl_setdomainhandle_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_setdomainhandle_t);


/* XEN_DOMCTL_setdebugging */
struct xen_domctl_setdebugging {
    uint8_t enable;
};
typedef struct xen_domctl_setdebugging xen_domctl_setdebugging_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_setdebugging_t);


/* XEN_DOMCTL_irq_permission */
struct xen_domctl_irq_permission {
    uint8_t pirq;
    uint8_t allow_access;    /* flag to specify enable/disable of IRQ access */
};
typedef struct xen_domctl_irq_permission xen_domctl_irq_permission_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_irq_permission_t);


/* XEN_DOMCTL_iomem_permission */
struct xen_domctl_iomem_permission {
    uint64_aligned_t first_mfn;/* first page (physical page number) in range */
    uint64_aligned_t nr_mfns;  /* number of pages in range (>0) */
    uint8_t  allow_access;     /* allow (!0) or deny (0) access to range? */
};
typedef struct xen_domctl_iomem_permission xen_domctl_iomem_permission_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_iomem_permission_t);


/* XEN_DOMCTL_ioport_permission */
struct xen_domctl_ioport_permission {
    uint32_t first_port;              /* first port int range */
    uint32_t nr_ports;                /* size of port range */
    uint8_t  allow_access;            /* allow or deny access to range? */
};
typedef struct xen_domctl_ioport_permission xen_domctl_ioport_permission_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_ioport_permission_t);


/* XEN_DOMCTL_hypercall_init */
struct xen_domctl_hypercall_init {
    uint64_aligned_t  gmfn;           /* GMFN to be initialised */
};
typedef struct xen_domctl_hypercall_init xen_domctl_hypercall_init_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_hypercall_init_t);


/* XEN_DOMCTL_settimeoffset */
struct xen_domctl_settimeoffset {
    int64_aligned_t time_offset_seconds; /* applied to domain wallclock time */
};
typedef struct xen_domctl_settimeoffset xen_domctl_settimeoffset_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_settimeoffset_t);

/* XEN_DOMCTL_gethvmcontext */
/* XEN_DOMCTL_sethvmcontext */
typedef struct xen_domctl_hvmcontext {
    uint32_t size; /* IN/OUT: size of buffer / bytes filled */
    XEN_GUEST_HANDLE_64(uint8) buffer; /* IN/OUT: data, or call
                                        * gethvmcontext with NULL
                                        * buffer to get size req'd */
} xen_domctl_hvmcontext_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_hvmcontext_t);


/* XEN_DOMCTL_set_address_size */
/* XEN_DOMCTL_get_address_size */
typedef struct xen_domctl_address_size {
    uint32_t size;
} xen_domctl_address_size_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_address_size_t);


/* XEN_DOMCTL_sendtrigger */
#define XEN_DOMCTL_SENDTRIGGER_NMI    0
#define XEN_DOMCTL_SENDTRIGGER_RESET  1
#define XEN_DOMCTL_SENDTRIGGER_INIT   2
#define XEN_DOMCTL_SENDTRIGGER_POWER  3
#define XEN_DOMCTL_SENDTRIGGER_SLEEP  4
struct xen_domctl_sendtrigger {
    uint32_t  trigger;  /* IN */
    uint32_t  vcpu;     /* IN */
};
typedef struct xen_domctl_sendtrigger xen_domctl_sendtrigger_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_sendtrigger_t);


/* Assign a device to a guest. Sets up IOMMU structures. */
/* XEN_DOMCTL_assign_device */
/* XEN_DOMCTL_test_assign_device */
/*
 * XEN_DOMCTL_deassign_device: The behavior of this DOMCTL differs
 * between the different type of device:
 *  - PCI device (XEN_DOMCTL_DEV_PCI) will be reassigned to DOM0
 *  - DT device (XEN_DOMCTL_DT_PCI) will left unassigned. DOM0
 *  will have to call XEN_DOMCTL_assign_device in order to use the
 *  device.
 */
#define XEN_DOMCTL_DEV_PCI      0
#define XEN_DOMCTL_DEV_DT       1
struct xen_domctl_assign_device {
    uint32_t dev;   /* XEN_DOMCTL_DEV_* */
    union {
        struct {
            uint32_t machine_sbdf;   /* machine PCI ID of assigned device */
        } pci;
        struct {
            uint32_t size; /* Length of the path */
            XEN_GUEST_HANDLE_64(char) path; /* path to the device tree node */
        } dt;
    } u;
    /* IN */
#define XEN_DOMCTL_DEV_RDM_RELAXED      1
    uint32_t  flag;   /* flag of assigned device */
};
typedef struct xen_domctl_assign_device xen_domctl_assign_device_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_assign_device_t);

/* Retrieve sibling devices infomation of machine_sbdf */
/* XEN_DOMCTL_get_device_group */
struct xen_domctl_get_device_group {
    uint32_t  machine_sbdf;     /* IN */
    uint32_t  max_sdevs;        /* IN */
    uint32_t  num_sdevs;        /* OUT */
    XEN_GUEST_HANDLE_64(uint32)  sdev_array;   /* OUT */
};
typedef struct xen_domctl_get_device_group xen_domctl_get_device_group_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_get_device_group_t);

/* Pass-through interrupts: bind real irq -> hvm devfn. */
/* XEN_DOMCTL_bind_pt_irq */
/* XEN_DOMCTL_unbind_pt_irq */
typedef enum pt_irq_type_e {
    PT_IRQ_TYPE_PCI,
    PT_IRQ_TYPE_ISA,
    PT_IRQ_TYPE_MSI,
    PT_IRQ_TYPE_MSI_TRANSLATE,
    PT_IRQ_TYPE_SPI,    /* ARM: valid range 32-1019 */
} pt_irq_type_t;
struct xen_domctl_bind_pt_irq {
    uint32_t machine_irq;
    pt_irq_type_t irq_type;
    uint32_t hvm_domid;

    union {
        struct {
            uint8_t isa_irq;
        } isa;
        struct {
            uint8_t bus;
            uint8_t device;
            uint8_t intx;
        } pci;
        struct {
            uint8_t gvec;
            uint32_t gflags;
            uint64_aligned_t gtable;
        } msi;
        struct {
            uint16_t spi;
        } spi;
    } u;
};
typedef struct xen_domctl_bind_pt_irq xen_domctl_bind_pt_irq_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_bind_pt_irq_t);


/* Bind machine I/O address range -> HVM address range. */
/* If this returns -E2BIG lower nr_mfns value. */
/* XEN_DOMCTL_memory_mapping */
#define DPCI_ADD_MAPPING         1
#define DPCI_REMOVE_MAPPING      0
struct xen_domctl_memory_mapping {
    uint64_aligned_t first_gfn; /* first page (hvm guest phys page) in range */
    uint64_aligned_t first_mfn; /* first page (machine page) in range */
    uint64_aligned_t nr_mfns;   /* number of pages in range (>0) */
    uint32_t add_mapping;       /* add or remove mapping */
    uint32_t padding;           /* padding for 64-bit aligned structure */
};
typedef struct xen_domctl_memory_mapping xen_domctl_memory_mapping_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_memory_mapping_t);


/* Bind machine I/O port range -> HVM I/O port range. */
/* XEN_DOMCTL_ioport_mapping */
struct xen_domctl_ioport_mapping {
    uint32_t first_gport;     /* first guest IO port*/
    uint32_t first_mport;     /* first machine IO port */
    uint32_t nr_ports;        /* size of port range */
    uint32_t add_mapping;     /* add or remove mapping */
};
typedef struct xen_domctl_ioport_mapping xen_domctl_ioport_mapping_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_ioport_mapping_t);


/*
 * Pin caching type of RAM space for x86 HVM domU.
 */
/* XEN_DOMCTL_pin_mem_cacheattr */
/* Caching types: these happen to be the same as x86 MTRR/PAT type codes. */
#define XEN_DOMCTL_MEM_CACHEATTR_UC  0
#define XEN_DOMCTL_MEM_CACHEATTR_WC  1
#define XEN_DOMCTL_MEM_CACHEATTR_WT  4
#define XEN_DOMCTL_MEM_CACHEATTR_WP  5
#define XEN_DOMCTL_MEM_CACHEATTR_WB  6
#define XEN_DOMCTL_MEM_CACHEATTR_UCM 7
#define XEN_DOMCTL_DELETE_MEM_CACHEATTR (~(uint32_t)0)
struct xen_domctl_pin_mem_cacheattr {
    uint64_aligned_t start, end;
    uint32_t type; /* XEN_DOMCTL_MEM_CACHEATTR_* */
};
typedef struct xen_domctl_pin_mem_cacheattr xen_domctl_pin_mem_cacheattr_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_pin_mem_cacheattr_t);


/* XEN_DOMCTL_set_ext_vcpucontext */
/* XEN_DOMCTL_get_ext_vcpucontext */
struct xen_domctl_ext_vcpucontext {
    /* IN: VCPU that this call applies to. */
    uint32_t         vcpu;
    /*
     * SET: Size of struct (IN)
     * GET: Size of struct (OUT, up to 128 bytes)
     */
    uint32_t         size;
#if defined(__i386__) || defined(__x86_64__)
    /* SYSCALL from 32-bit mode and SYSENTER callback information. */
    /* NB. SYSCALL from 64-bit mode is contained in vcpu_guest_context_t */
    uint64_aligned_t syscall32_callback_eip;
    uint64_aligned_t sysenter_callback_eip;
    uint16_t         syscall32_callback_cs;
    uint16_t         sysenter_callback_cs;
    uint8_t          syscall32_disables_events;
    uint8_t          sysenter_disables_events;
#if defined(__GNUC__)
    union {
        uint64_aligned_t mcg_cap;
        struct hvm_vmce_vcpu vmce;
    };
#else
    struct hvm_vmce_vcpu vmce;
#endif
#endif
};
typedef struct xen_domctl_ext_vcpucontext xen_domctl_ext_vcpucontext_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_ext_vcpucontext_t);

/*
 * Set the target domain for a domain
 */
/* XEN_DOMCTL_set_target */
struct xen_domctl_set_target {
    domid_t target;
};
typedef struct xen_domctl_set_target xen_domctl_set_target_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_set_target_t);

#if defined(__i386__) || defined(__x86_64__)
# define XEN_CPUID_INPUT_UNUSED  0xFFFFFFFF
/* XEN_DOMCTL_set_cpuid */
struct xen_domctl_cpuid {
  uint32_t input[2];
  uint32_t eax;
  uint32_t ebx;
  uint32_t ecx;
  uint32_t edx;
};
typedef struct xen_domctl_cpuid xen_domctl_cpuid_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_cpuid_t);
#endif

/*
 * Arranges that if the domain suspends (specifically, if it shuts
 * down with code SHUTDOWN_suspend), this event channel will be
 * notified.
 *
 * This is _instead of_ the usual notification to the global
 * VIRQ_DOM_EXC.  (In most systems that pirq is owned by xenstored.)
 *
 * Only one subscription per domain is possible.  Last subscriber
 * wins; others are silently displaced.
 *
 * NB that contrary to the rather general name, it only applies to
 * domain shutdown with code suspend.  Shutdown for other reasons
 * (including crash), and domain death, are notified to VIRQ_DOM_EXC
 * regardless.
 */
/* XEN_DOMCTL_subscribe */
struct xen_domctl_subscribe {
    uint32_t port; /* IN */
};
typedef struct xen_domctl_subscribe xen_domctl_subscribe_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_subscribe_t);

/*
 * Define the maximum machine address size which should be allocated
 * to a guest.
 */
/* XEN_DOMCTL_set_machine_address_size */
/* XEN_DOMCTL_get_machine_address_size */

/*
 * Do not inject spurious page faults into this domain.
 */
/* XEN_DOMCTL_suppress_spurious_page_faults */

/* XEN_DOMCTL_debug_op */
#define XEN_DOMCTL_DEBUG_OP_SINGLE_STEP_OFF         0
#define XEN_DOMCTL_DEBUG_OP_SINGLE_STEP_ON          1
struct xen_domctl_debug_op {
    uint32_t op;   /* IN */
    uint32_t vcpu; /* IN */
};
typedef struct xen_domctl_debug_op xen_domctl_debug_op_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_debug_op_t);

/*
 * Request a particular record from the HVM context
 */
/* XEN_DOMCTL_gethvmcontext_partial */
typedef struct xen_domctl_hvmcontext_partial {
    uint32_t type;                      /* IN: Type of record required */
    uint32_t instance;                  /* IN: Instance of that type */
    XEN_GUEST_HANDLE_64(uint8) buffer;  /* OUT: buffer to write record into */
} xen_domctl_hvmcontext_partial_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_hvmcontext_partial_t);

/* XEN_DOMCTL_disable_migrate */
typedef struct xen_domctl_disable_migrate {
    uint32_t disable; /* IN: 1: disable migration and restore */
} xen_domctl_disable_migrate_t;


/* XEN_DOMCTL_gettscinfo */
/* XEN_DOMCTL_settscinfo */
typedef struct xen_domctl_tsc_info {
    /* IN/OUT */
    uint32_t tsc_mode;
    uint32_t gtsc_khz;
    uint32_t incarnation;
    uint32_t pad;
    uint64_aligned_t elapsed_nsec;
} xen_domctl_tsc_info_t;

/* XEN_DOMCTL_gdbsx_guestmemio      guest mem io */
struct xen_domctl_gdbsx_memio {
    /* IN */
    uint64_aligned_t pgd3val;/* optional: init_mm.pgd[3] value */
    uint64_aligned_t gva;    /* guest virtual address */
    uint64_aligned_t uva;    /* user buffer virtual address */
    uint32_t         len;    /* number of bytes to read/write */
    uint8_t          gwr;    /* 0 = read from guest. 1 = write to guest */
    /* OUT */
    uint32_t         remain; /* bytes remaining to be copied */
};

/* XEN_DOMCTL_gdbsx_pausevcpu */
/* XEN_DOMCTL_gdbsx_unpausevcpu */
struct xen_domctl_gdbsx_pauseunp_vcpu { /* pause/unpause a vcpu */
    uint32_t         vcpu;         /* which vcpu */
};

/* XEN_DOMCTL_gdbsx_domstatus */
struct xen_domctl_gdbsx_domstatus {
    /* OUT */
    uint8_t          paused;     /* is the domain paused */
    uint32_t         vcpu_id;    /* any vcpu in an event? */
    uint32_t         vcpu_ev;    /* if yes, what event? */
};

/*
 * VM event operations
 */

/* XEN_DOMCTL_vm_event_op */

/*
 * There are currently three rings available for VM events:
 * sharing, monitor and paging. This hypercall allows one to
 * control these rings (enable/disable), as well as to signal
 * to the hypervisor to pull responses (resume) from the given
 * ring.
 */
#define XEN_VM_EVENT_ENABLE               0
#define XEN_VM_EVENT_DISABLE              1
#define XEN_VM_EVENT_RESUME               2

/*
 * Domain memory paging
 * Page memory in and out.
 * Domctl interface to set up and tear down the 
 * pager<->hypervisor interface. Use XENMEM_paging_op*
 * to perform per-page operations.
 *
 * The XEN_VM_EVENT_PAGING_ENABLE domctl returns several
 * non-standard error codes to indicate why paging could not be enabled:
 * ENODEV - host lacks HAP support (EPT/NPT) or HAP is disabled in guest
 * EMLINK - guest has iommu passthrough enabled
 * EXDEV  - guest has PoD enabled
 * EBUSY  - guest has or had paging enabled, ring buffer still active
 */
#define XEN_DOMCTL_VM_EVENT_OP_PAGING            1

/*
 * Monitor helper.
 *
 * As with paging, use the domctl for teardown/setup of the
 * helper<->hypervisor interface.
 *
 * The monitor interface can be used to register for various VM events. For
 * example, there are HVM hypercalls to set the per-page access permissions
 * of every page in a domain.  When one of these permissions--independent,
 * read, write, and execute--is violated, the VCPU is paused and a memory event
 * is sent with what happened. The memory event handler can then resume the
 * VCPU and redo the access with a XEN_VM_EVENT_RESUME option.
 *
 * See public/vm_event.h for the list of available events that can be
 * subscribed to via the monitor interface.
 *
 * The XEN_VM_EVENT_MONITOR_* domctls returns
 * non-standard error codes to indicate why access could not be enabled:
 * ENODEV - host lacks HAP support (EPT/NPT) or HAP is disabled in guest
 * EBUSY  - guest has or had access enabled, ring buffer still active
 *
 */
#define XEN_DOMCTL_VM_EVENT_OP_MONITOR           2

/*
 * Sharing ENOMEM helper.
 *
 * As with paging, use the domctl for teardown/setup of the
 * helper<->hypervisor interface.
 *
 * If setup, this ring is used to communicate failed allocations
 * in the unshare path. XENMEM_sharing_op_resume is used to wake up
 * vcpus that could not unshare.
 *
 * Note that shring can be turned on (as per the domctl below)
 * *without* this ring being setup.
 */
#define XEN_DOMCTL_VM_EVENT_OP_SHARING           3

/* Use for teardown/setup of helper<->hypervisor interface for paging, 
 * access and sharing.*/
struct xen_domctl_vm_event_op {
    uint32_t       op;           /* XEN_VM_EVENT_* */
    uint32_t       mode;         /* XEN_DOMCTL_VM_EVENT_OP_* */

    uint32_t port;              /* OUT: event channel for ring */
};
typedef struct xen_domctl_vm_event_op xen_domctl_vm_event_op_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_vm_event_op_t);

/*
 * Memory sharing operations
 */
/* XEN_DOMCTL_mem_sharing_op.
 * The CONTROL sub-domctl is used for bringup/teardown. */
#define XEN_DOMCTL_MEM_SHARING_CONTROL          0

struct xen_domctl_mem_sharing_op {
    uint8_t op; /* XEN_DOMCTL_MEM_SHARING_* */

    union {
        uint8_t enable;                   /* CONTROL */
    } u;
};
typedef struct xen_domctl_mem_sharing_op xen_domctl_mem_sharing_op_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_mem_sharing_op_t);

struct xen_domctl_audit_p2m {
    /* OUT error counts */
    uint64_t orphans;
    uint64_t m2p_bad;
    uint64_t p2m_bad;
};
typedef struct xen_domctl_audit_p2m xen_domctl_audit_p2m_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_audit_p2m_t);

struct xen_domctl_set_virq_handler {
    uint32_t virq; /* IN */
};
typedef struct xen_domctl_set_virq_handler xen_domctl_set_virq_handler_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_set_virq_handler_t);

#if defined(__i386__) || defined(__x86_64__)
/* XEN_DOMCTL_setvcpuextstate */
/* XEN_DOMCTL_getvcpuextstate */
struct xen_domctl_vcpuextstate {
    /* IN: VCPU that this call applies to. */
    uint32_t         vcpu;
    /*
     * SET: Ignored.
     * GET: xfeature support mask of struct (IN/OUT)
     * xfeature mask is served as identifications of the saving format
     * so that compatible CPUs can have a check on format to decide
     * whether it can restore.
     */
    uint64_aligned_t         xfeature_mask;
    /*
     * SET: Size of struct (IN)
     * GET: Size of struct (IN/OUT)
     */
    uint64_aligned_t         size;
    XEN_GUEST_HANDLE_64(uint64) buffer;
};
typedef struct xen_domctl_vcpuextstate xen_domctl_vcpuextstate_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_vcpuextstate_t);
#endif

/* XEN_DOMCTL_set_access_required: sets whether a memory event listener
 * must be present to handle page access events: if false, the page
 * access will revert to full permissions if no one is listening;
 *  */
struct xen_domctl_set_access_required {
    uint8_t access_required;
};
typedef struct xen_domctl_set_access_required xen_domctl_set_access_required_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_set_access_required_t);

struct xen_domctl_set_broken_page_p2m {
    uint64_aligned_t pfn;
};
typedef struct xen_domctl_set_broken_page_p2m xen_domctl_set_broken_page_p2m_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_set_broken_page_p2m_t);

/*
 * XEN_DOMCTL_set_max_evtchn: sets the maximum event channel port
 * number the guest may use.  Use this limit the amount of resources
 * (global mapping space, xenheap) a guest may use for event channels.
 */
struct xen_domctl_set_max_evtchn {
    uint32_t max_port;
};
typedef struct xen_domctl_set_max_evtchn xen_domctl_set_max_evtchn_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_set_max_evtchn_t);

/*
 * ARM: Clean and invalidate caches associated with given region of
 * guest memory.
 */
struct xen_domctl_cacheflush {
    /* IN: page range to flush. */
    xen_pfn_t start_pfn, nr_pfns;
};
typedef struct xen_domctl_cacheflush xen_domctl_cacheflush_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_cacheflush_t);

#if defined(__i386__) || defined(__x86_64__)
struct xen_domctl_vcpu_msr {
    uint32_t         index;
    uint32_t         reserved;
    uint64_aligned_t value;
};
typedef struct xen_domctl_vcpu_msr xen_domctl_vcpu_msr_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_vcpu_msr_t);

/*
 * XEN_DOMCTL_set_vcpu_msrs / XEN_DOMCTL_get_vcpu_msrs.
 *
 * Input:
 * - A NULL 'msrs' guest handle is a request for the maximum 'msr_count'.
 * - Otherwise, 'msr_count' is the number of entries in 'msrs'.
 *
 * Output for get:
 * - If 'msr_count' is less than the number Xen needs to write, -ENOBUFS shall
 *   be returned and 'msr_count' updated to reflect the intended number.
 * - On success, 'msr_count' shall indicate the number of MSRs written, which
 *   may be less than the maximum if some are not currently used by the vcpu.
 *
 * Output for set:
 * - If Xen encounters an error with a specific MSR, -EINVAL shall be returned
 *   and 'msr_count' shall be set to the offending index, to aid debugging.
 */
struct xen_domctl_vcpu_msrs {
    uint32_t vcpu;                                   /* IN     */
    uint32_t msr_count;                              /* IN/OUT */
    XEN_GUEST_HANDLE_64(xen_domctl_vcpu_msr_t) msrs; /* IN/OUT */
};
typedef struct xen_domctl_vcpu_msrs xen_domctl_vcpu_msrs_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_vcpu_msrs_t);
#endif

/* XEN_DOMCTL_setvnumainfo: specifies a virtual NUMA topology for the guest */
struct xen_domctl_vnuma {
    /* IN: number of vNUMA nodes to setup. Shall be greater than 0 */
    uint32_t nr_vnodes;
    /* IN: number of memory ranges to setup */
    uint32_t nr_vmemranges;
    /*
     * IN: number of vCPUs of the domain (used as size of the vcpu_to_vnode
     * array declared below). Shall be equal to the domain's max_vcpus.
     */
    uint32_t nr_vcpus;
    uint32_t pad;                                  /* must be zero */

    /*
     * IN: array for specifying the distances of the vNUMA nodes
     * between each others. Shall have nr_vnodes*nr_vnodes elements.
     */
    XEN_GUEST_HANDLE_64(uint) vdistance;
    /*
     * IN: array for specifying to what vNUMA node each vCPU belongs.
     * Shall have nr_vcpus elements.
     */
    XEN_GUEST_HANDLE_64(uint) vcpu_to_vnode;
    /*
     * IN: array for specifying on what physical NUMA node each vNUMA
     * node is placed. Shall have nr_vnodes elements.
     */
    XEN_GUEST_HANDLE_64(uint) vnode_to_pnode;
    /*
     * IN: array for specifying the memory ranges. Shall have
     * nr_vmemranges elements.
     */
    XEN_GUEST_HANDLE_64(xen_vmemrange_t) vmemrange;
};
typedef struct xen_domctl_vnuma xen_domctl_vnuma_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_vnuma_t);

struct xen_domctl_psr_cmt_op {
#define XEN_DOMCTL_PSR_CMT_OP_DETACH         0
#define XEN_DOMCTL_PSR_CMT_OP_ATTACH         1
#define XEN_DOMCTL_PSR_CMT_OP_QUERY_RMID     2
    uint32_t cmd;
    uint32_t data;
};
typedef struct xen_domctl_psr_cmt_op xen_domctl_psr_cmt_op_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_psr_cmt_op_t);

/*  XEN_DOMCTL_MONITOR_*
 *
 * Enable/disable monitoring various VM events.
 * This domctl configures what events will be reported to helper apps
 * via the ring buffer "MONITOR". The ring has to be first enabled
 * with the domctl XEN_DOMCTL_VM_EVENT_OP_MONITOR.
 *
 * GET_CAPABILITIES can be used to determine which of these features is
 * available on a given platform.
 *
 * NOTICE: mem_access events are also delivered via the "MONITOR" ring buffer;
 * however, enabling/disabling those events is performed with the use of
 * memory_op hypercalls!
 */
#define XEN_DOMCTL_MONITOR_OP_ENABLE            0
#define XEN_DOMCTL_MONITOR_OP_DISABLE           1
#define XEN_DOMCTL_MONITOR_OP_GET_CAPABILITIES  2

#define XEN_DOMCTL_MONITOR_EVENT_WRITE_CTRLREG         0
#define XEN_DOMCTL_MONITOR_EVENT_MOV_TO_MSR            1
#define XEN_DOMCTL_MONITOR_EVENT_SINGLESTEP            2
#define XEN_DOMCTL_MONITOR_EVENT_SOFTWARE_BREAKPOINT   3
#define XEN_DOMCTL_MONITOR_EVENT_GUEST_REQUEST         4

struct xen_domctl_monitor_op {
    uint32_t op; /* XEN_DOMCTL_MONITOR_OP_* */

    /*
     * When used with ENABLE/DISABLE this has to be set to
     * the requested XEN_DOMCTL_MONITOR_EVENT_* value.
     * With GET_CAPABILITIES this field returns a bitmap of
     * events supported by the platform, in the format
     * (1 << XEN_DOMCTL_MONITOR_EVENT_*).
     */
    uint32_t event;

    /*
     * Further options when issuing XEN_DOMCTL_MONITOR_OP_ENABLE.
     */
    union {
        struct {
            /* Which control register */
            uint8_t index;
            /* Pause vCPU until response */
            uint8_t sync;
            /* Send event only on a change of value */
            uint8_t onchangeonly;
        } mov_to_cr;

        struct {
            /* Enable the capture of an extended set of MSRs */
            uint8_t extended_capture;
        } mov_to_msr;

        struct {
            /* Pause vCPU until response */
            uint8_t sync;
        } guest_request;
    } u;
};
typedef struct xen_domctl_monitor_op xen_domctl_monitor_op_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_monitor_op_t);

struct xen_domctl_psr_cat_op {
#define XEN_DOMCTL_PSR_CAT_OP_SET_L3_CBM     0
#define XEN_DOMCTL_PSR_CAT_OP_GET_L3_CBM     1
    uint32_t cmd;       /* IN: XEN_DOMCTL_PSR_CAT_OP_* */
    uint32_t target;    /* IN */
    uint64_t data;      /* IN/OUT */
};
typedef struct xen_domctl_psr_cat_op xen_domctl_psr_cat_op_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_psr_cat_op_t);

struct xen_domctl {
    uint32_t cmd;
#define XEN_DOMCTL_createdomain                   1
#define XEN_DOMCTL_destroydomain                  2
#define XEN_DOMCTL_pausedomain                    3
#define XEN_DOMCTL_unpausedomain                  4
#define XEN_DOMCTL_getdomaininfo                  5
#define XEN_DOMCTL_getmemlist                     6
/* #define XEN_DOMCTL_getpageframeinfo            7 Obsolete - use getpageframeinfo3 */
/* #define XEN_DOMCTL_getpageframeinfo2           8 Obsolete - use getpageframeinfo3 */
#define XEN_DOMCTL_setvcpuaffinity                9
#define XEN_DOMCTL_shadow_op                     10
#define XEN_DOMCTL_max_mem                       11
#define XEN_DOMCTL_setvcpucontext                12
#define XEN_DOMCTL_getvcpucontext                13
#define XEN_DOMCTL_getvcpuinfo                   14
#define XEN_DOMCTL_max_vcpus                     15
#define XEN_DOMCTL_scheduler_op                  16
#define XEN_DOMCTL_setdomainhandle               17
#define XEN_DOMCTL_setdebugging                  18
#define XEN_DOMCTL_irq_permission                19
#define XEN_DOMCTL_iomem_permission              20
#define XEN_DOMCTL_ioport_permission             21
#define XEN_DOMCTL_hypercall_init                22
#define XEN_DOMCTL_arch_setup                    23 /* Obsolete IA64 only */
#define XEN_DOMCTL_settimeoffset                 24
#define XEN_DOMCTL_getvcpuaffinity               25
#define XEN_DOMCTL_real_mode_area                26 /* Obsolete PPC only */
#define XEN_DOMCTL_resumedomain                  27
#define XEN_DOMCTL_sendtrigger                   28
#define XEN_DOMCTL_subscribe                     29
#define XEN_DOMCTL_gethvmcontext                 33
#define XEN_DOMCTL_sethvmcontext                 34
#define XEN_DOMCTL_set_address_size              35
#define XEN_DOMCTL_get_address_size              36
#define XEN_DOMCTL_assign_device                 37
#define XEN_DOMCTL_bind_pt_irq                   38
#define XEN_DOMCTL_memory_mapping                39
#define XEN_DOMCTL_ioport_mapping                40
#define XEN_DOMCTL_pin_mem_cacheattr             41
#define XEN_DOMCTL_set_ext_vcpucontext           42
#define XEN_DOMCTL_get_ext_vcpucontext           43
#define XEN_DOMCTL_set_opt_feature               44 /* Obsolete IA64 only */
#define XEN_DOMCTL_test_assign_device            45
#define XEN_DOMCTL_set_target                    46
#define XEN_DOMCTL_deassign_device               47
#define XEN_DOMCTL_unbind_pt_irq                 48
#define XEN_DOMCTL_set_cpuid                     49
#define XEN_DOMCTL_get_device_group              50
#define XEN_DOMCTL_set_machine_address_size      51
#define XEN_DOMCTL_get_machine_address_size      52
#define XEN_DOMCTL_suppress_spurious_page_faults 53
#define XEN_DOMCTL_debug_op                      54
#define XEN_DOMCTL_gethvmcontext_partial         55
#define XEN_DOMCTL_vm_event_op                   56
#define XEN_DOMCTL_mem_sharing_op                57
#define XEN_DOMCTL_disable_migrate               58
#define XEN_DOMCTL_gettscinfo                    59
#define XEN_DOMCTL_settscinfo                    60
#define XEN_DOMCTL_getpageframeinfo3             61
#define XEN_DOMCTL_setvcpuextstate               62
#define XEN_DOMCTL_getvcpuextstate               63
#define XEN_DOMCTL_set_access_required           64
#define XEN_DOMCTL_audit_p2m                     65
#define XEN_DOMCTL_set_virq_handler              66
#define XEN_DOMCTL_set_broken_page_p2m           67
#define XEN_DOMCTL_setnodeaffinity               68
#define XEN_DOMCTL_getnodeaffinity               69
#define XEN_DOMCTL_set_max_evtchn                70
#define XEN_DOMCTL_cacheflush                    71
#define XEN_DOMCTL_get_vcpu_msrs                 72
#define XEN_DOMCTL_set_vcpu_msrs                 73
#define XEN_DOMCTL_setvnumainfo                  74
#define XEN_DOMCTL_psr_cmt_op                    75
#define XEN_DOMCTL_monitor_op                    77
#define XEN_DOMCTL_psr_cat_op                    78
#define XEN_DOMCTL_gdbsx_guestmemio            1000
#define XEN_DOMCTL_gdbsx_pausevcpu             1001
#define XEN_DOMCTL_gdbsx_unpausevcpu           1002
#define XEN_DOMCTL_gdbsx_domstatus             1003
    uint32_t interface_version; /* XEN_DOMCTL_INTERFACE_VERSION */
    domid_t  domain;
    union {
        struct xen_domctl_createdomain      createdomain;
        struct xen_domctl_getdomaininfo     getdomaininfo;
        struct xen_domctl_getmemlist        getmemlist;
        struct xen_domctl_getpageframeinfo3 getpageframeinfo3;
        struct xen_domctl_nodeaffinity      nodeaffinity;
        struct xen_domctl_vcpuaffinity      vcpuaffinity;
        struct xen_domctl_shadow_op         shadow_op;
        struct xen_domctl_max_mem           max_mem;
        struct xen_domctl_vcpucontext       vcpucontext;
        struct xen_domctl_getvcpuinfo       getvcpuinfo;
        struct xen_domctl_max_vcpus         max_vcpus;
        struct xen_domctl_scheduler_op      scheduler_op;
        struct xen_domctl_setdomainhandle   setdomainhandle;
        struct xen_domctl_setdebugging      setdebugging;
        struct xen_domctl_irq_permission    irq_permission;
        struct xen_domctl_iomem_permission  iomem_permission;
        struct xen_domctl_ioport_permission ioport_permission;
        struct xen_domctl_hypercall_init    hypercall_init;
        struct xen_domctl_settimeoffset     settimeoffset;
        struct xen_domctl_disable_migrate   disable_migrate;
        struct xen_domctl_tsc_info          tsc_info;
        struct xen_domctl_hvmcontext        hvmcontext;
        struct xen_domctl_hvmcontext_partial hvmcontext_partial;
        struct xen_domctl_address_size      address_size;
        struct xen_domctl_sendtrigger       sendtrigger;
        struct xen_domctl_get_device_group  get_device_group;
        struct xen_domctl_assign_device     assign_device;
        struct xen_domctl_bind_pt_irq       bind_pt_irq;
        struct xen_domctl_memory_mapping    memory_mapping;
        struct xen_domctl_ioport_mapping    ioport_mapping;
        struct xen_domctl_pin_mem_cacheattr pin_mem_cacheattr;
        struct xen_domctl_ext_vcpucontext   ext_vcpucontext;
        struct xen_domctl_set_target        set_target;
        struct xen_domctl_subscribe         subscribe;
        struct xen_domctl_debug_op          debug_op;
        struct xen_domctl_vm_event_op       vm_event_op;
        struct xen_domctl_mem_sharing_op    mem_sharing_op;
#if defined(__i386__) || defined(__x86_64__)
        struct xen_domctl_cpuid             cpuid;
        struct xen_domctl_vcpuextstate      vcpuextstate;
        struct xen_domctl_vcpu_msrs         vcpu_msrs;
#endif
        struct xen_domctl_set_access_required access_required;
        struct xen_domctl_audit_p2m         audit_p2m;
        struct xen_domctl_set_virq_handler  set_virq_handler;
        struct xen_domctl_set_max_evtchn    set_max_evtchn;
        struct xen_domctl_gdbsx_memio       gdbsx_guest_memio;
        struct xen_domctl_set_broken_page_p2m set_broken_page_p2m;
        struct xen_domctl_cacheflush        cacheflush;
        struct xen_domctl_gdbsx_pauseunp_vcpu gdbsx_pauseunp_vcpu;
        struct xen_domctl_gdbsx_domstatus   gdbsx_domstatus;
        struct xen_domctl_vnuma             vnuma;
        struct xen_domctl_psr_cmt_op        psr_cmt_op;
        struct xen_domctl_monitor_op        monitor_op;
        struct xen_domctl_psr_cat_op        psr_cat_op;
        uint8_t                             pad[128];
    } u;
};
typedef struct xen_domctl xen_domctl_t;
DEFINE_XEN_GUEST_HANDLE(xen_domctl_t);

#endif /* __XEN_PUBLIC_DOMCTL_H__ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
