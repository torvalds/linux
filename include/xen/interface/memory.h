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
    GUEST_HANDLE(ulong) extent_start;

    /* Number of extents, and size/alignment of each (2^extent_order pages). */
    unsigned long  nr_extents;
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
    unsigned long nr_exchanged;
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
    GUEST_HANDLE(ulong) extent_start;

    /*
     * Number of extents written to the above array. This will be smaller
     * than 'max_extents' if the machphys table is smaller than max_e * 2MB.
     */
    unsigned int nr_extents;
};
DEFINE_GUEST_HANDLE_STRUCT(xen_machphys_mfn_list);

/*
 * Sets the GPFN at which a particular page appears in the specified guest's
 * pseudophysical address space.
 * arg == addr of xen_add_to_physmap_t.
 */
#define XENMEM_add_to_physmap      7
struct xen_add_to_physmap {
    /* Which domain to change the mapping for. */
    domid_t domid;

    /* Source mapping space. */
#define XENMAPSPACE_shared_info 0 /* shared info page */
#define XENMAPSPACE_grant_table 1 /* grant table page */
    unsigned int space;

    /* Index into source mapping space. */
    unsigned long idx;

    /* GPFN where the source mapping page should appear. */
    unsigned long gpfn;
};
DEFINE_GUEST_HANDLE_STRUCT(xen_add_to_physmap);

/*
 * Translates a list of domain-specific GPFNs into MFNs. Returns a -ve error
 * code on failure. This call only works for auto-translated guests.
 */
#define XENMEM_translate_gpfn_list  8
struct xen_translate_gpfn_list {
    /* Which domain to translate for? */
    domid_t domid;

    /* Length of list. */
    unsigned long nr_gpfns;

    /* List of GPFNs to translate. */
    GUEST_HANDLE(ulong) gpfn_list;

    /*
     * Output list to contain MFN translations. May be the same as the input
     * list (in which case each input GPFN is overwritten with the output MFN).
     */
    GUEST_HANDLE(ulong) mfn_list;
};
DEFINE_GUEST_HANDLE_STRUCT(xen_translate_gpfn_list);

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
 * Prevent the balloon driver from changing the memory reservation
 * during a driver critical region.
 */
extern spinlock_t xen_reservation_lock;
#endif /* __XEN_PUBLIC_MEMORY_H__ */
