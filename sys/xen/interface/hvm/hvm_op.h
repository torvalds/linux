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
 * Copyright (c) 2007, Keir Fraser
 */

#ifndef __XEN_PUBLIC_HVM_HVM_OP_H__
#define __XEN_PUBLIC_HVM_HVM_OP_H__

#include "../xen.h"
#include "../trace.h"
#include "../event_channel.h"

/* Get/set subcommands: extra argument == pointer to xen_hvm_param struct. */
#define HVMOP_set_param           0
#define HVMOP_get_param           1
struct xen_hvm_param {
    domid_t  domid;    /* IN */
    uint32_t index;    /* IN */
    uint64_t value;    /* IN/OUT */
};
typedef struct xen_hvm_param xen_hvm_param_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_param_t);

/* Set the logical level of one of a domain's PCI INTx wires. */
#define HVMOP_set_pci_intx_level  2
struct xen_hvm_set_pci_intx_level {
    /* Domain to be updated. */
    domid_t  domid;
    /* PCI INTx identification in PCI topology (domain:bus:device:intx). */
    uint8_t  domain, bus, device, intx;
    /* Assertion level (0 = unasserted, 1 = asserted). */
    uint8_t  level;
};
typedef struct xen_hvm_set_pci_intx_level xen_hvm_set_pci_intx_level_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_set_pci_intx_level_t);

/* Set the logical level of one of a domain's ISA IRQ wires. */
#define HVMOP_set_isa_irq_level   3
struct xen_hvm_set_isa_irq_level {
    /* Domain to be updated. */
    domid_t  domid;
    /* ISA device identification, by ISA IRQ (0-15). */
    uint8_t  isa_irq;
    /* Assertion level (0 = unasserted, 1 = asserted). */
    uint8_t  level;
};
typedef struct xen_hvm_set_isa_irq_level xen_hvm_set_isa_irq_level_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_set_isa_irq_level_t);

#define HVMOP_set_pci_link_route  4
struct xen_hvm_set_pci_link_route {
    /* Domain to be updated. */
    domid_t  domid;
    /* PCI link identifier (0-3). */
    uint8_t  link;
    /* ISA IRQ (1-15), or 0 (disable link). */
    uint8_t  isa_irq;
};
typedef struct xen_hvm_set_pci_link_route xen_hvm_set_pci_link_route_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_set_pci_link_route_t);

/* Flushes all VCPU TLBs: @arg must be NULL. */
#define HVMOP_flush_tlbs          5

typedef enum {
    HVMMEM_ram_rw,             /* Normal read/write guest RAM */
    HVMMEM_ram_ro,             /* Read-only; writes are discarded */
    HVMMEM_mmio_dm,            /* Reads and write go to the device model */
    HVMMEM_mmio_write_dm       /* Read-only; writes go to the device model */
} hvmmem_type_t;

/* Following tools-only interfaces may change in future. */
#if defined(__XEN__) || defined(__XEN_TOOLS__)

/* Track dirty VRAM. */
#define HVMOP_track_dirty_vram    6
struct xen_hvm_track_dirty_vram {
    /* Domain to be tracked. */
    domid_t  domid;
    /* Number of pages to track. */
    uint32_t nr;
    /* First pfn to track. */
    uint64_aligned_t first_pfn;
    /* OUT variable. */
    /* Dirty bitmap buffer. */
    XEN_GUEST_HANDLE_64(uint8) dirty_bitmap;
};
typedef struct xen_hvm_track_dirty_vram xen_hvm_track_dirty_vram_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_track_dirty_vram_t);

/* Notify that some pages got modified by the Device Model. */
#define HVMOP_modified_memory    7
struct xen_hvm_modified_memory {
    /* Domain to be updated. */
    domid_t  domid;
    /* Number of pages. */
    uint32_t nr;
    /* First pfn. */
    uint64_aligned_t first_pfn;
};
typedef struct xen_hvm_modified_memory xen_hvm_modified_memory_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_modified_memory_t);

#define HVMOP_set_mem_type    8
/* Notify that a region of memory is to be treated in a specific way. */
struct xen_hvm_set_mem_type {
    /* Domain to be updated. */
    domid_t domid;
    /* Memory type */
    uint16_t hvmmem_type;
    /* Number of pages. */
    uint32_t nr;
    /* First pfn. */
    uint64_aligned_t first_pfn;
};
typedef struct xen_hvm_set_mem_type xen_hvm_set_mem_type_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_set_mem_type_t);

#endif /* defined(__XEN__) || defined(__XEN_TOOLS__) */

/* Hint from PV drivers for pagetable destruction. */
#define HVMOP_pagetable_dying        9
struct xen_hvm_pagetable_dying {
    /* Domain with a pagetable about to be destroyed. */
    domid_t  domid;
    uint16_t pad[3]; /* align next field on 8-byte boundary */
    /* guest physical address of the toplevel pagetable dying */
    uint64_t gpa;
};
typedef struct xen_hvm_pagetable_dying xen_hvm_pagetable_dying_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_pagetable_dying_t);

/* Get the current Xen time, in nanoseconds since system boot. */
#define HVMOP_get_time              10
struct xen_hvm_get_time {
    uint64_t now;      /* OUT */
};
typedef struct xen_hvm_get_time xen_hvm_get_time_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_get_time_t);

#define HVMOP_xentrace              11
struct xen_hvm_xentrace {
    uint16_t event, extra_bytes;
    uint8_t extra[TRACE_EXTRA_MAX * sizeof(uint32_t)];
};
typedef struct xen_hvm_xentrace xen_hvm_xentrace_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_xentrace_t);

/* Following tools-only interfaces may change in future. */
#if defined(__XEN__) || defined(__XEN_TOOLS__)

/* Deprecated by XENMEM_access_op_set_access */
#define HVMOP_set_mem_access        12

/* Deprecated by XENMEM_access_op_get_access */
#define HVMOP_get_mem_access        13

#define HVMOP_inject_trap            14
/* Inject a trap into a VCPU, which will get taken up on the next
 * scheduling of it. Note that the caller should know enough of the
 * state of the CPU before injecting, to know what the effect of
 * injecting the trap will be.
 */
struct xen_hvm_inject_trap {
    /* Domain to be queried. */
    domid_t domid;
    /* VCPU */
    uint32_t vcpuid;
    /* Vector number */
    uint32_t vector;
    /* Trap type (HVMOP_TRAP_*) */
    uint32_t type;
/* NB. This enumeration precisely matches hvm.h:X86_EVENTTYPE_* */
# define HVMOP_TRAP_ext_int    0 /* external interrupt */
# define HVMOP_TRAP_nmi        2 /* nmi */
# define HVMOP_TRAP_hw_exc     3 /* hardware exception */
# define HVMOP_TRAP_sw_int     4 /* software interrupt (CD nn) */
# define HVMOP_TRAP_pri_sw_exc 5 /* ICEBP (F1) */
# define HVMOP_TRAP_sw_exc     6 /* INT3 (CC), INTO (CE) */
    /* Error code, or ~0u to skip */
    uint32_t error_code;
    /* Intruction length */
    uint32_t insn_len;
    /* CR2 for page faults */
    uint64_aligned_t cr2;
};
typedef struct xen_hvm_inject_trap xen_hvm_inject_trap_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_inject_trap_t);

#endif /* defined(__XEN__) || defined(__XEN_TOOLS__) */

#define HVMOP_get_mem_type    15
/* Return hvmmem_type_t for the specified pfn. */
struct xen_hvm_get_mem_type {
    /* Domain to be queried. */
    domid_t domid;
    /* OUT variable. */
    uint16_t mem_type;
    uint16_t pad[2]; /* align next field on 8-byte boundary */
    /* IN variable. */
    uint64_t pfn;
};
typedef struct xen_hvm_get_mem_type xen_hvm_get_mem_type_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_get_mem_type_t);

/* Following tools-only interfaces may change in future. */
#if defined(__XEN__) || defined(__XEN_TOOLS__)

/* MSI injection for emulated devices */
#define HVMOP_inject_msi         16
struct xen_hvm_inject_msi {
    /* Domain to be injected */
    domid_t   domid;
    /* Data -- lower 32 bits */
    uint32_t  data;
    /* Address (0xfeexxxxx) */
    uint64_t  addr;
};
typedef struct xen_hvm_inject_msi xen_hvm_inject_msi_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_inject_msi_t);

/*
 * IOREQ Servers
 *
 * The interface between an I/O emulator an Xen is called an IOREQ Server.
 * A domain supports a single 'legacy' IOREQ Server which is instantiated if
 * parameter...
 *
 * HVM_PARAM_IOREQ_PFN is read (to get the gmfn containing the synchronous
 * ioreq structures), or...
 * HVM_PARAM_BUFIOREQ_PFN is read (to get the gmfn containing the buffered
 * ioreq ring), or...
 * HVM_PARAM_BUFIOREQ_EVTCHN is read (to get the event channel that Xen uses
 * to request buffered I/O emulation).
 * 
 * The following hypercalls facilitate the creation of IOREQ Servers for
 * 'secondary' emulators which are invoked to implement port I/O, memory, or
 * PCI config space ranges which they explicitly register.
 */

typedef uint16_t ioservid_t;

/*
 * HVMOP_create_ioreq_server: Instantiate a new IOREQ Server for a secondary
 *                            emulator servicing domain <domid>.
 *
 * The <id> handed back is unique for <domid>. If <handle_bufioreq> is zero
 * the buffered ioreq ring will not be allocated and hence all emulation
 * requestes to this server will be synchronous.
 */
#define HVMOP_create_ioreq_server 17
struct xen_hvm_create_ioreq_server {
    domid_t domid;           /* IN - domain to be serviced */
#define HVM_IOREQSRV_BUFIOREQ_OFF    0
#define HVM_IOREQSRV_BUFIOREQ_LEGACY 1
/*
 * Use this when read_pointer gets updated atomically and
 * the pointer pair gets read atomically:
 */
#define HVM_IOREQSRV_BUFIOREQ_ATOMIC 2
    uint8_t handle_bufioreq; /* IN - should server handle buffered ioreqs */
    ioservid_t id;           /* OUT - server id */
};
typedef struct xen_hvm_create_ioreq_server xen_hvm_create_ioreq_server_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_create_ioreq_server_t);

/*
 * HVMOP_get_ioreq_server_info: Get all the information necessary to access
 *                              IOREQ Server <id>. 
 *
 * The emulator needs to map the synchronous ioreq structures and buffered
 * ioreq ring (if it exists) that Xen uses to request emulation. These are
 * hosted in domain <domid>'s gmfns <ioreq_pfn> and <bufioreq_pfn>
 * respectively. In addition, if the IOREQ Server is handling buffered
 * emulation requests, the emulator needs to bind to event channel
 * <bufioreq_port> to listen for them. (The event channels used for
 * synchronous emulation requests are specified in the per-CPU ioreq
 * structures in <ioreq_pfn>).
 * If the IOREQ Server is not handling buffered emulation requests then the
 * values handed back in <bufioreq_pfn> and <bufioreq_port> will both be 0.
 */
#define HVMOP_get_ioreq_server_info 18
struct xen_hvm_get_ioreq_server_info {
    domid_t domid;                 /* IN - domain to be serviced */
    ioservid_t id;                 /* IN - server id */
    evtchn_port_t bufioreq_port;   /* OUT - buffered ioreq port */
    uint64_aligned_t ioreq_pfn;    /* OUT - sync ioreq pfn */
    uint64_aligned_t bufioreq_pfn; /* OUT - buffered ioreq pfn */
};
typedef struct xen_hvm_get_ioreq_server_info xen_hvm_get_ioreq_server_info_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_get_ioreq_server_info_t);

/*
 * HVM_map_io_range_to_ioreq_server: Register an I/O range of domain <domid>
 *                                   for emulation by the client of IOREQ
 *                                   Server <id>
 * HVM_unmap_io_range_from_ioreq_server: Deregister an I/O range of <domid>
 *                                       for emulation by the client of IOREQ
 *                                       Server <id>
 *
 * There are three types of I/O that can be emulated: port I/O, memory accesses
 * and PCI config space accesses. The <type> field denotes which type of range
 * the <start> and <end> (inclusive) fields are specifying.
 * PCI config space ranges are specified by segment/bus/device/function values
 * which should be encoded using the HVMOP_PCI_SBDF helper macro below.
 *
 * NOTE: unless an emulation request falls entirely within a range mapped
 * by a secondary emulator, it will not be passed to that emulator.
 */
#define HVMOP_map_io_range_to_ioreq_server 19
#define HVMOP_unmap_io_range_from_ioreq_server 20
struct xen_hvm_io_range {
    domid_t domid;               /* IN - domain to be serviced */
    ioservid_t id;               /* IN - server id */
    uint32_t type;               /* IN - type of range */
# define HVMOP_IO_RANGE_PORT   0 /* I/O port range */
# define HVMOP_IO_RANGE_MEMORY 1 /* MMIO range */
# define HVMOP_IO_RANGE_PCI    2 /* PCI segment/bus/dev/func range */
    uint64_aligned_t start, end; /* IN - inclusive start and end of range */
};
typedef struct xen_hvm_io_range xen_hvm_io_range_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_io_range_t);

#define HVMOP_PCI_SBDF(s,b,d,f)                 \
	((((s) & 0xffff) << 16) |                   \
	 (((b) & 0xff) << 8) |                      \
	 (((d) & 0x1f) << 3) |                      \
	 ((f) & 0x07))

/*
 * HVMOP_destroy_ioreq_server: Destroy the IOREQ Server <id> servicing domain
 *                             <domid>.
 *
 * Any registered I/O ranges will be automatically deregistered.
 */
#define HVMOP_destroy_ioreq_server 21
struct xen_hvm_destroy_ioreq_server {
    domid_t domid; /* IN - domain to be serviced */
    ioservid_t id; /* IN - server id */
};
typedef struct xen_hvm_destroy_ioreq_server xen_hvm_destroy_ioreq_server_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_destroy_ioreq_server_t);

/*
 * HVMOP_set_ioreq_server_state: Enable or disable the IOREQ Server <id> servicing
 *                               domain <domid>.
 *
 * The IOREQ Server will not be passed any emulation requests until it is in the
 * enabled state.
 * Note that the contents of the ioreq_pfn and bufioreq_fn (see
 * HVMOP_get_ioreq_server_info) are not meaningful until the IOREQ Server is in
 * the enabled state.
 */
#define HVMOP_set_ioreq_server_state 22
struct xen_hvm_set_ioreq_server_state {
    domid_t domid;   /* IN - domain to be serviced */
    ioservid_t id;   /* IN - server id */
    uint8_t enabled; /* IN - enabled? */    
};
typedef struct xen_hvm_set_ioreq_server_state xen_hvm_set_ioreq_server_state_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_set_ioreq_server_state_t);

#endif /* defined(__XEN__) || defined(__XEN_TOOLS__) */

#if defined(__i386__) || defined(__x86_64__)

/*
 * HVMOP_set_evtchn_upcall_vector: Set a <vector> that should be used for event
 *                                 channel upcalls on the specified <vcpu>. If set,
 *                                 this vector will be used in preference to the
 *                                 domain global callback via (see
 *                                 HVM_PARAM_CALLBACK_IRQ).
 */
#define HVMOP_set_evtchn_upcall_vector 23
struct xen_hvm_evtchn_upcall_vector {
    uint32_t vcpu;
    uint8_t vector;
};
typedef struct xen_hvm_evtchn_upcall_vector xen_hvm_evtchn_upcall_vector_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_evtchn_upcall_vector_t);

#endif /* defined(__i386__) || defined(__x86_64__) */

#define HVMOP_guest_request_vm_event 24

/* HVMOP_altp2m: perform altp2m state operations */
#define HVMOP_altp2m 25

#define HVMOP_ALTP2M_INTERFACE_VERSION 0x00000001

struct xen_hvm_altp2m_domain_state {
    /* IN or OUT variable on/off */
    uint8_t state;
};
typedef struct xen_hvm_altp2m_domain_state xen_hvm_altp2m_domain_state_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_altp2m_domain_state_t);

struct xen_hvm_altp2m_vcpu_enable_notify {
    uint32_t vcpu_id;
    uint32_t pad;
    /* #VE info area gfn */
    uint64_t gfn;
};
typedef struct xen_hvm_altp2m_vcpu_enable_notify xen_hvm_altp2m_vcpu_enable_notify_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_altp2m_vcpu_enable_notify_t);

struct xen_hvm_altp2m_view {
    /* IN/OUT variable */
    uint16_t view;
    /* Create view only: default access type
     * NOTE: currently ignored */
    uint16_t hvmmem_default_access; /* xenmem_access_t */
};
typedef struct xen_hvm_altp2m_view xen_hvm_altp2m_view_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_altp2m_view_t);

struct xen_hvm_altp2m_set_mem_access {
    /* view */
    uint16_t view;
    /* Memory type */
    uint16_t hvmmem_access; /* xenmem_access_t */
    uint32_t pad;
    /* gfn */
    uint64_t gfn;
};
typedef struct xen_hvm_altp2m_set_mem_access xen_hvm_altp2m_set_mem_access_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_altp2m_set_mem_access_t);

struct xen_hvm_altp2m_change_gfn {
    /* view */
    uint16_t view;
    uint16_t pad1;
    uint32_t pad2;
    /* old gfn */
    uint64_t old_gfn;
    /* new gfn, INVALID_GFN (~0UL) means revert */
    uint64_t new_gfn;
};
typedef struct xen_hvm_altp2m_change_gfn xen_hvm_altp2m_change_gfn_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_altp2m_change_gfn_t);

struct xen_hvm_altp2m_op {
    uint32_t version;   /* HVMOP_ALTP2M_INTERFACE_VERSION */
    uint32_t cmd;
/* Get/set the altp2m state for a domain */
#define HVMOP_altp2m_get_domain_state     1
#define HVMOP_altp2m_set_domain_state     2
/* Set the current VCPU to receive altp2m event notifications */
#define HVMOP_altp2m_vcpu_enable_notify   3
/* Create a new view */
#define HVMOP_altp2m_create_p2m           4
/* Destroy a view */
#define HVMOP_altp2m_destroy_p2m          5
/* Switch view for an entire domain */
#define HVMOP_altp2m_switch_p2m           6
/* Notify that a page of memory is to have specific access types */
#define HVMOP_altp2m_set_mem_access       7
/* Change a p2m entry to have a different gfn->mfn mapping */
#define HVMOP_altp2m_change_gfn           8
    domid_t domain;
    uint16_t pad1;
    uint32_t pad2;
    union {
        struct xen_hvm_altp2m_domain_state       domain_state;
        struct xen_hvm_altp2m_vcpu_enable_notify enable_notify;
        struct xen_hvm_altp2m_view               view;
        struct xen_hvm_altp2m_set_mem_access     set_mem_access;
        struct xen_hvm_altp2m_change_gfn         change_gfn;
        uint8_t pad[64];
    } u;
};
typedef struct xen_hvm_altp2m_op xen_hvm_altp2m_op_t;
DEFINE_XEN_GUEST_HANDLE(xen_hvm_altp2m_op_t);

#endif /* __XEN_PUBLIC_HVM_HVM_OP_H__ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
