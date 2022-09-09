/* SPDX-License-Identifier: MIT */
/******************************************************************************
 * memory.h
 *
 * Memory reservation and information.
 *
 * Copyright (c) 2005, Keir Fraser <keir@xensource.com>
 */

#ifndef __XEN_PUBLIC_MEMORY_H__
#define __XEN_PUBLIC_MEMORY_H__

#include <linux/spinlock.h>

/*
 * Increase or decrease the specified domain's memory reservation. Returns a
 * -ve errcode on failure, or the # extents successfully allocated or freed.
 * arg == addr of struct xen_memory_reservation.
 */
#define XENMEM_increase_reservation 0
#define XENMEM_decrease_reservation 1
#define XENMEM_populate_physmap     6
struct xen_memory_reservation {

    /*
     * XENMEM_increase_reservation:
     *   OUT: MFN (*not* GMFN) bases of extents that were allocated
     * XENMEM_decrease_reservation:
     *   IN:  GMFN bases of extents to free
     * XENMEM_populate_physmap:
     *   IN:  GPFN bases of extents to populate with memory
     *   OUT: GMFN bases of extents that were allocated
     *   (NB. This command also updates the mach_to_phys translation table)
     */
    GUEST_HANDLE(xen_pfn_t) extent_start;

    /* Number of extents, and size/alignment of each (2^extent_order pages). */
    xen_ulong_t  nr_extents;
    unsigned int   extent_order;

    /*
     * Maximum # bits addressable by the user of the allocated region (e.g.,
     * I/O devices often have a 32-bit limitation even in 64-bit systems). If
     * zero then the user has no addressing restriction.
     * This field is not used by XENMEM_decrease_reservation.
     */
    unsigned int   address_bits;

    /*
     * Domain whose reservation is being changed.
     * Unprivileged domains can specify only DOMID_SELF.
     */
    domid_t        domid;

};
DEFINE_GUEST_HANDLE_STRUCT(xen_memory_reservation);

/*
 * An atomic exchange of memory pages. If return code is zero then
 * @out.extent_list provides GMFNs of the newly-allocated memory.
 * Returns zero on complete success, otherwise a negative error code.
 * On complete success then always @nr_exchanged == @in.nr_extents.
 * On partial success @nr_exchanged indicates how much work was done.
 */
#define XENMEM_exchange             11
struct xen_memory_exchange {
    /*
     * [IN] Details of memory extents to be exchanged (GMFN bases).
     * Note that @in.address_bits is ignored and unused.
     */
    struct xen_memory_reservation in;

    /*
     * [IN/OUT] Details of new memory extents.
     * We require that:
     *  1. @in.domid == @out.domid
     *  2. @in.nr_extents  << @in.extent_order ==
     *     @out.nr_extents << @out.extent_order
     *  3. @in.extent_start and @out.extent_start lists must not overlap
     *  4. @out.extent_start lists GPFN bases to be populated
     *  5. @out.extent_start is overwritten with allocated GMFN bases
     */
    struct xen_memory_reservation out;

    /*
     * [OUT] Number of input extents that were successfully exchanged:
     *  1. The first @nr_exchanged input extents were successfully
     *     deallocated.
     *  2. The corresponding first entries in the output extent list correctly
     *     indicate the GMFNs that were successfully exchanged.
     *  3. All other input and output extents are untouched.
     *  4. If not all input exents are exchanged then the return code of this
     *     command will be non-zero.
     *  5. THIS FIELD MUST BE INITIALISED TO ZERO BY THE CALLER!
     */
    xen_ulong_t nr_exchanged;
};

DEFINE_GUEST_HANDLE_STRUCT(xen_memory_exchange);
/*
 * Returns the maximum machine frame number of mapped RAM in this system.
 * This command always succeeds (it never returns an error code).
 * arg == NULL.
 */
#define XENMEM_maximum_ram_page     2

/*
 * Returns the current or maximum memory reservation, in pages, of the
 * specified domain (may be DOMID_SELF). Returns -ve errcode on failure.
 * arg == addr of domid_t.
 */
#define XENMEM_current_reservation  3
#define XENMEM_maximum_reservation  4

/*
 * Returns a list of MFN bases of 2MB extents comprising the machine_to_phys
 * mapping table. Architectures which do not have a m2p table do not implement
 * this command.
 * arg == addr of xen_machphys_mfn_list_t.
 */
#define XENMEM_machphys_mfn_list    5
struct xen_machphys_mfn_list {
    /*
     * Size of the 'extent_start' array. Fewer entries will be filled if the
     * machphys table is smaller than max_extents * 2MB.
     */
    unsigned int max_extents;

    /*
     * Pointer to buffer to fill with list of extent starts. If there are
     * any large discontiguities in the machine address space, 2MB gaps in
     * the machphys table will be represented by an MFN base of zero.
     */
    GUEST_HANDLE(xen_pfn_t) extent_start;

    /*
     * Number of extents written to the above array. This will be smaller
     * than 'max_extents' if the machphys table is smaller than max_e * 2MB.
     */
    unsigned int nr_extents;
};
DEFINE_GUEST_HANDLE_STRUCT(xen_machphys_mfn_list);

/*
 * Returns the location in virtual address space of the machine_to_phys
 * mapping table. Architectures which do not have a m2p table, or which do not
 * map it by default into guest address space, do not implement this command.
 * arg == addr of xen_machphys_mapping_t.
 */
#define XENMEM_machphys_mapping     12
struct xen_machphys_mapping {
    xen_ulong_t v_start, v_end; /* Start and end virtual addresses.   */
    xen_ulong_t max_mfn;        /* Maximum MFN that can be looked up. */
};
DEFINE_GUEST_HANDLE_STRUCT(xen_machphys_mapping_t);

#define XENMAPSPACE_shared_info  0 /* shared info page */
#define XENMAPSPACE_grant_table  1 /* grant table page */
#define XENMAPSPACE_gmfn         2 /* GMFN */
#define XENMAPSPACE_gmfn_range   3 /* GMFN range, XENMEM_add_to_physmap only. */
#define XENMAPSPACE_gmfn_foreign 4 /* GMFN from another dom,
				    * XENMEM_add_to_physmap_range only.
				    */
#define XENMAPSPACE_dev_mmio     5 /* device mmio region */

/*
 * Sets the GPFN at which a particular page appears in the specified guest's
 * pseudophysical address space.
 * arg == addr of xen_add_to_physmap_t.
 */
#define XENMEM_add_to_physmap      7
struct xen_add_to_physmap {
    /* Which domain to change the mapping for. */
    domid_t domid;

    /* Number of pages to go through for gmfn_range */
    uint16_t    size;

    /* Source mapping space. */
    unsigned int space;

    /* Index into source mapping space. */
    xen_ulong_t idx;

    /* GPFN where the source mapping page should appear. */
    xen_pfn_t gpfn;
};
DEFINE_GUEST_HANDLE_STRUCT(xen_add_to_physmap);

/*** REMOVED ***/
/*#define XENMEM_translate_gpfn_list  8*/

#define XENMEM_add_to_physmap_range 23
struct xen_add_to_physmap_range {
    /* IN */
    /* Which domain to change the mapping for. */
    domid_t domid;
    uint16_t space; /* => enum phys_map_space */

    /* Number of pages to go through */
    uint16_t size;
    domid_t foreign_domid; /* IFF gmfn_foreign */

    /* Indexes into space being mapped. */
    GUEST_HANDLE(xen_ulong_t) idxs;

    /* GPFN in domid where the source mapping page should appear. */
    GUEST_HANDLE(xen_pfn_t) gpfns;

    /* OUT */

    /* Per index error code. */
    GUEST_HANDLE(int) errs;
};
DEFINE_GUEST_HANDLE_STRUCT(xen_add_to_physmap_range);

/*
 * Returns the pseudo-physical memory map as it was when the domain
 * was started (specified by XENMEM_set_memory_map).
 * arg == addr of struct xen_memory_map.
 */
#define XENMEM_memory_map           9
struct xen_memory_map {
    /*
     * On call the number of entries which can be stored in buffer. On
     * return the number of entries which have been stored in
     * buffer.
     */
    unsigned int nr_entries;

    /*
     * Entries in the buffer are in the same format as returned by the
     * BIOS INT 0x15 EAX=0xE820 call.
     */
    GUEST_HANDLE(void) buffer;
};
DEFINE_GUEST_HANDLE_STRUCT(xen_memory_map);

/*
 * Returns the real physical memory map. Passes the same structure as
 * XENMEM_memory_map.
 * arg == addr of struct xen_memory_map.
 */
#define XENMEM_machine_memory_map   10


/*
 * Unmaps the page appearing at a particular GPFN from the specified guest's
 * pseudophysical address space.
 * arg == addr of xen_remove_from_physmap_t.
 */
#define XENMEM_remove_from_physmap      15
struct xen_remove_from_physmap {
    /* Which domain to change the mapping for. */
    domid_t domid;

    /* GPFN of the current mapping of the page. */
    xen_pfn_t gpfn;
};
DEFINE_GUEST_HANDLE_STRUCT(xen_remove_from_physmap);

/*
 * Get the pages for a particular guest resource, so that they can be
 * mapped directly by a tools domain.
 */
#define XENMEM_acquire_resource 28
struct xen_mem_acquire_resource {
    /* IN - The domain whose resource is to be mapped */
    domid_t domid;
    /* IN - the type of resource */
    uint16_t type;

#define XENMEM_resource_ioreq_server 0
#define XENMEM_resource_grant_table 1

    /*
     * IN - a type-specific resource identifier, which must be zero
     *      unless stated otherwise.
     *
     * type == XENMEM_resource_ioreq_server -> id == ioreq server id
     * type == XENMEM_resource_grant_table -> id defined below
     */
    uint32_t id;

#define XENMEM_resource_grant_table_id_shared 0
#define XENMEM_resource_grant_table_id_status 1

    /* IN/OUT - As an IN parameter number of frames of the resource
     *          to be mapped. However, if the specified value is 0 and
     *          frame_list is NULL then this field will be set to the
     *          maximum value supported by the implementation on return.
     */
    uint32_t nr_frames;
    /*
     * OUT - Must be zero on entry. On return this may contain a bitwise
     *       OR of the following values.
     */
    uint32_t flags;

    /* The resource pages have been assigned to the calling domain */
#define _XENMEM_rsrc_acq_caller_owned 0
#define XENMEM_rsrc_acq_caller_owned (1u << _XENMEM_rsrc_acq_caller_owned)

    /*
     * IN - the index of the initial frame to be mapped. This parameter
     *      is ignored if nr_frames is 0.
     */
    uint64_t frame;

#define XENMEM_resource_ioreq_server_frame_bufioreq 0
#define XENMEM_resource_ioreq_server_frame_ioreq(n) (1 + (n))

    /*
     * IN/OUT - If the tools domain is PV then, upon return, frame_list
     *          will be populated with the MFNs of the resource.
     *          If the tools domain is HVM then it is expected that, on
     *          entry, frame_list will be populated with a list of GFNs
     *          that will be mapped to the MFNs of the resource.
     *          If -EIO is returned then the frame_list has only been
     *          partially mapped and it is up to the caller to unmap all
     *          the GFNs.
     *          This parameter may be NULL if nr_frames is 0.
     */
    GUEST_HANDLE(xen_pfn_t) frame_list;
};
DEFINE_GUEST_HANDLE_STRUCT(xen_mem_acquire_resource);

#endif /* __XEN_PUBLIC_MEMORY_H__ */
