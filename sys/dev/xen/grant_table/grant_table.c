/******************************************************************************
 * gnttab.c
 * 
 * Two sets of functionality:
 * 1. Granting foreign access to our memory reservation.
 * 2. Accessing others' memory reservations via grant references.
 * (i.e., mechanisms for both sender and recipient of grant references)
 * 
 * Copyright (c) 2005, Christopher Clark
 * Copyright (c) 2004, K A Fraser
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mman.h>
#include <sys/limits.h>
#include <sys/rman.h>
#include <machine/resource.h>
#include <machine/cpu.h>

#include <xen/xen-os.h>
#include <xen/hypervisor.h>
#include <machine/xen/synch_bitops.h>

#include <xen/hypervisor.h>
#include <xen/gnttab.h>

#include <vm/vm.h>
#include <vm/vm_kern.h>
#include <vm/vm_extern.h>
#include <vm/pmap.h>

/* External tools reserve first few grant table entries. */
#define NR_RESERVED_ENTRIES 8
#define GREFS_PER_GRANT_FRAME (PAGE_SIZE / sizeof(grant_entry_t))

static grant_ref_t **gnttab_list;
static unsigned int nr_grant_frames;
static unsigned int boot_max_nr_grant_frames;
static int gnttab_free_count;
static grant_ref_t gnttab_free_head;
static struct mtx gnttab_list_lock;

/*
 * Resource representing allocated physical address space
 * for the grant table metainfo
 */
static struct resource *gnttab_pseudo_phys_res;

/* Resource id for allocated physical address space. */
static int gnttab_pseudo_phys_res_id;

static grant_entry_t *shared;

static struct gnttab_free_callback *gnttab_free_callback_list = NULL;

static int gnttab_expand(unsigned int req_entries);

#define RPP (PAGE_SIZE / sizeof(grant_ref_t))
#define gnttab_entry(entry) (gnttab_list[(entry) / RPP][(entry) % RPP])

static int
get_free_entries(int count, int *entries)
{
	int ref, error;
	grant_ref_t head;

	mtx_lock(&gnttab_list_lock);
	if ((gnttab_free_count < count) &&
	    ((error = gnttab_expand(count - gnttab_free_count)) != 0)) {
		mtx_unlock(&gnttab_list_lock);
		return (error);
	}
	ref = head = gnttab_free_head;
	gnttab_free_count -= count;
	while (count-- > 1)
		head = gnttab_entry(head);
	gnttab_free_head = gnttab_entry(head);
	gnttab_entry(head) = GNTTAB_LIST_END;
	mtx_unlock(&gnttab_list_lock);

	*entries = ref;
	return (0);
}

static void
do_free_callbacks(void)
{
	struct gnttab_free_callback *callback, *next;

	callback = gnttab_free_callback_list;
	gnttab_free_callback_list = NULL;

	while (callback != NULL) {
		next = callback->next;
		if (gnttab_free_count >= callback->count) {
			callback->next = NULL;
			callback->fn(callback->arg);
		} else {
			callback->next = gnttab_free_callback_list;
			gnttab_free_callback_list = callback;
		}
		callback = next;
	}
}

static inline void
check_free_callbacks(void)
{
	if (__predict_false(gnttab_free_callback_list != NULL))
		do_free_callbacks();
}

static void
put_free_entry(grant_ref_t ref)
{

	mtx_lock(&gnttab_list_lock);
	gnttab_entry(ref) = gnttab_free_head;
	gnttab_free_head = ref;
	gnttab_free_count++;
	check_free_callbacks();
	mtx_unlock(&gnttab_list_lock);
}

/*
 * Public grant-issuing interface functions
 */

int
gnttab_grant_foreign_access(domid_t domid, unsigned long frame, int readonly,
	grant_ref_t *result)
{
	int error, ref;

	error = get_free_entries(1, &ref);

	if (__predict_false(error))
		return (error);

	shared[ref].frame = frame;
	shared[ref].domid = domid;
	wmb();
	shared[ref].flags = GTF_permit_access | (readonly ? GTF_readonly : 0);

	if (result)
		*result = ref;

	return (0);
}

void
gnttab_grant_foreign_access_ref(grant_ref_t ref, domid_t domid,
				unsigned long frame, int readonly)
{

	shared[ref].frame = frame;
	shared[ref].domid = domid;
	wmb();
	shared[ref].flags = GTF_permit_access | (readonly ? GTF_readonly : 0);
}

int
gnttab_query_foreign_access(grant_ref_t ref)
{
	uint16_t nflags;

	nflags = shared[ref].flags;

	return (nflags & (GTF_reading|GTF_writing));
}

int
gnttab_end_foreign_access_ref(grant_ref_t ref)
{
	uint16_t flags, nflags;

	nflags = shared[ref].flags;
	do {
		if ( (flags = nflags) & (GTF_reading|GTF_writing) ) {
			printf("%s: WARNING: g.e. still in use!\n", __func__);
			return (0);
		}
	} while ((nflags = synch_cmpxchg(&shared[ref].flags, flags, 0)) !=
	       flags);

	return (1);
}

void
gnttab_end_foreign_access(grant_ref_t ref, void *page)
{
	if (gnttab_end_foreign_access_ref(ref)) {
		put_free_entry(ref);
		if (page != NULL) {
			free(page, M_DEVBUF);
		}
	}
	else {
		/* XXX This needs to be fixed so that the ref and page are
		   placed on a list to be freed up later. */
		printf("%s: WARNING: leaking g.e. and page still in use!\n",
		       __func__);
	}
}

void
gnttab_end_foreign_access_references(u_int count, grant_ref_t *refs)
{
	grant_ref_t *last_ref;
	grant_ref_t  head;
	grant_ref_t  tail;

	head = GNTTAB_LIST_END;
	tail = *refs;
	last_ref = refs + count;
	while (refs != last_ref) {

		if (gnttab_end_foreign_access_ref(*refs)) {
			gnttab_entry(*refs) = head;
			head = *refs;
		} else {
			/*
			 * XXX This needs to be fixed so that the ref 
			 * is placed on a list to be freed up later.
			 */
			printf("%s: WARNING: leaking g.e. still in use!\n",
			       __func__);
			count--;
		}
		refs++;
	}

	if (count != 0) {
		mtx_lock(&gnttab_list_lock);
		gnttab_free_count += count;
		gnttab_entry(tail) = gnttab_free_head;
		gnttab_free_head = head;
		check_free_callbacks();
		mtx_unlock(&gnttab_list_lock);
	}
}

int
gnttab_grant_foreign_transfer(domid_t domid, unsigned long pfn,
    grant_ref_t *result)
{
	int error, ref;

	error = get_free_entries(1, &ref);
	if (__predict_false(error))
		return (error);

	gnttab_grant_foreign_transfer_ref(ref, domid, pfn);

	*result = ref;
	return (0);
}

void
gnttab_grant_foreign_transfer_ref(grant_ref_t ref, domid_t domid,
	unsigned long pfn)
{
	shared[ref].frame = pfn;
	shared[ref].domid = domid;
	wmb();
	shared[ref].flags = GTF_accept_transfer;
}

unsigned long
gnttab_end_foreign_transfer_ref(grant_ref_t ref)
{
	unsigned long frame;
	uint16_t      flags;

	/*
         * If a transfer is not even yet started, try to reclaim the grant
         * reference and return failure (== 0).
         */
	while (!((flags = shared[ref].flags) & GTF_transfer_committed)) {
		if ( synch_cmpxchg(&shared[ref].flags, flags, 0) == flags )
			return (0);
		cpu_spinwait();
	}

	/* If a transfer is in progress then wait until it is completed. */
	while (!(flags & GTF_transfer_completed)) {
		flags = shared[ref].flags;
		cpu_spinwait();
	}

	/* Read the frame number /after/ reading completion status. */
	rmb();
	frame = shared[ref].frame;
	KASSERT(frame != 0, ("grant table inconsistent"));

	return (frame);
}

unsigned long
gnttab_end_foreign_transfer(grant_ref_t ref)
{
	unsigned long frame = gnttab_end_foreign_transfer_ref(ref);

	put_free_entry(ref);
	return (frame);
}

void
gnttab_free_grant_reference(grant_ref_t ref)
{

	put_free_entry(ref);
}

void
gnttab_free_grant_references(grant_ref_t head)
{
	grant_ref_t ref;
	int count = 1;

	if (head == GNTTAB_LIST_END)
		return;

	ref = head;
	while (gnttab_entry(ref) != GNTTAB_LIST_END) {
		ref = gnttab_entry(ref);
		count++;
	}
	mtx_lock(&gnttab_list_lock);
	gnttab_entry(ref) = gnttab_free_head;
	gnttab_free_head = head;
	gnttab_free_count += count;
	check_free_callbacks();
	mtx_unlock(&gnttab_list_lock);
}

int
gnttab_alloc_grant_references(uint16_t count, grant_ref_t *head)
{
	int ref, error;

	error = get_free_entries(count, &ref);
	if (__predict_false(error))
		return (error);

	*head = ref;
	return (0);
}

int
gnttab_empty_grant_references(const grant_ref_t *private_head)
{

	return (*private_head == GNTTAB_LIST_END);
}

int
gnttab_claim_grant_reference(grant_ref_t *private_head)
{
	grant_ref_t g = *private_head;

	if (__predict_false(g == GNTTAB_LIST_END))
		return (g);
	*private_head = gnttab_entry(g);
	return (g);
}

void
gnttab_release_grant_reference(grant_ref_t *private_head, grant_ref_t  release)
{

	gnttab_entry(release) = *private_head;
	*private_head = release;
}

void
gnttab_request_free_callback(struct gnttab_free_callback *callback,
    void (*fn)(void *), void *arg, uint16_t count)
{

	mtx_lock(&gnttab_list_lock);
	if (callback->next)
		goto out;
	callback->fn = fn;
	callback->arg = arg;
	callback->count = count;
	callback->next = gnttab_free_callback_list;
	gnttab_free_callback_list = callback;
	check_free_callbacks();
 out:
	mtx_unlock(&gnttab_list_lock);

}

void
gnttab_cancel_free_callback(struct gnttab_free_callback *callback)
{
	struct gnttab_free_callback **pcb;

	mtx_lock(&gnttab_list_lock);
	for (pcb = &gnttab_free_callback_list; *pcb; pcb = &(*pcb)->next) {
		if (*pcb == callback) {
			*pcb = callback->next;
			break;
		}
	}
	mtx_unlock(&gnttab_list_lock);
}


static int
grow_gnttab_list(unsigned int more_frames)
{
	unsigned int new_nr_grant_frames, extra_entries, i;

	new_nr_grant_frames = nr_grant_frames + more_frames;
	extra_entries       = more_frames * GREFS_PER_GRANT_FRAME;

	for (i = nr_grant_frames; i < new_nr_grant_frames; i++)
	{
		gnttab_list[i] = (grant_ref_t *)
			malloc(PAGE_SIZE, M_DEVBUF, M_NOWAIT);

		if (!gnttab_list[i])
			goto grow_nomem;
	}

	for (i = GREFS_PER_GRANT_FRAME * nr_grant_frames;
	     i < GREFS_PER_GRANT_FRAME * new_nr_grant_frames - 1; i++)
		gnttab_entry(i) = i + 1;

	gnttab_entry(i) = gnttab_free_head;
	gnttab_free_head = GREFS_PER_GRANT_FRAME * nr_grant_frames;
	gnttab_free_count += extra_entries;

	nr_grant_frames = new_nr_grant_frames;

	check_free_callbacks();

	return (0);

grow_nomem:
	for ( ; i >= nr_grant_frames; i--)
		free(gnttab_list[i], M_DEVBUF);
	return (ENOMEM);
}

static unsigned int
__max_nr_grant_frames(void)
{
	struct gnttab_query_size query;
	int rc;

	query.dom = DOMID_SELF;

	rc = HYPERVISOR_grant_table_op(GNTTABOP_query_size, &query, 1);
	if ((rc < 0) || (query.status != GNTST_okay))
		return (4); /* Legacy max supported number of frames */

	return (query.max_nr_frames);
}

static inline
unsigned int max_nr_grant_frames(void)
{
	unsigned int xen_max = __max_nr_grant_frames();

	if (xen_max > boot_max_nr_grant_frames)
		return (boot_max_nr_grant_frames);
	return (xen_max);
}

#ifdef notyet
/*
 * XXX needed for backend support
 *
 */
static int
map_pte_fn(pte_t *pte, struct page *pmd_page,
		      unsigned long addr, void *data)
{
	unsigned long **frames = (unsigned long **)data;

	set_pte_at(&init_mm, addr, pte, pfn_pte_ma((*frames)[0], PAGE_KERNEL));
	(*frames)++;
	return 0;
}

static int
unmap_pte_fn(pte_t *pte, struct page *pmd_page,
			unsigned long addr, void *data)
{

	set_pte_at(&init_mm, addr, pte, __pte(0));
	return 0;
}
#endif

static vm_paddr_t resume_frames;

static int
gnttab_map(unsigned int start_idx, unsigned int end_idx)
{
	struct xen_add_to_physmap xatp;
	unsigned int i = end_idx;

	/*
	 * Loop backwards, so that the first hypercall has the largest index,
	 * ensuring that the table will grow only once.
	 */
	do {
		xatp.domid = DOMID_SELF;
		xatp.idx = i;
		xatp.space = XENMAPSPACE_grant_table;
		xatp.gpfn = (resume_frames >> PAGE_SHIFT) + i;
		if (HYPERVISOR_memory_op(XENMEM_add_to_physmap, &xatp))
			panic("HYPERVISOR_memory_op failed to map gnttab");
	} while (i-- > start_idx);

	if (shared == NULL) {
		vm_offset_t area;

		area = kva_alloc(PAGE_SIZE * max_nr_grant_frames());
		KASSERT(area, ("can't allocate VM space for grant table"));
		shared = (grant_entry_t *)area;
	}

	for (i = start_idx; i <= end_idx; i++) {
		pmap_kenter((vm_offset_t) shared + i * PAGE_SIZE,
		    resume_frames + i * PAGE_SIZE);
	}

	return (0);
}

int
gnttab_resume(device_t dev)
{
	unsigned int max_nr_gframes, nr_gframes;

	nr_gframes = nr_grant_frames;
	max_nr_gframes = max_nr_grant_frames();
	if (max_nr_gframes < nr_gframes)
		return (ENOSYS);

	if (!resume_frames) {
		KASSERT(dev != NULL,
		    ("No resume frames and no device provided"));

		gnttab_pseudo_phys_res = xenmem_alloc(dev,
		    &gnttab_pseudo_phys_res_id, PAGE_SIZE * max_nr_gframes);
		if (gnttab_pseudo_phys_res == NULL)
			panic("Unable to reserve physical memory for gnttab");
		resume_frames = rman_get_start(gnttab_pseudo_phys_res);
	}

	return (gnttab_map(0, nr_gframes - 1));
}

static int
gnttab_expand(unsigned int req_entries)
{
	int error;
	unsigned int cur, extra;

	cur = nr_grant_frames;
	extra = howmany(req_entries, GREFS_PER_GRANT_FRAME);
	if (cur + extra > max_nr_grant_frames())
		return (ENOSPC);

	error = gnttab_map(cur, cur + extra - 1);
	if (!error)
		error = grow_gnttab_list(extra);

	return (error);
}

MTX_SYSINIT(gnttab, &gnttab_list_lock, "GNTTAB LOCK", MTX_DEF | MTX_RECURSE);

/*------------------ Private Device Attachment Functions  --------------------*/
/**
 * \brief Identify instances of this device type in the system.
 *
 * \param driver  The driver performing this identify action.
 * \param parent  The NewBus parent device for any devices this method adds.
 */
static void
granttable_identify(driver_t *driver __unused, device_t parent)
{

	KASSERT(xen_domain(),
	    ("Trying to attach grant-table device on non Xen domain"));
	/*
	 * A single device instance for our driver is always present
	 * in a system operating under Xen.
	 */
	if (BUS_ADD_CHILD(parent, 0, driver->name, 0) == NULL)
		panic("unable to attach Xen Grant-table device");
}

/**
 * \brief Probe for the existence of the Xen Grant-table device
 *
 * \param dev  NewBus device_t for this instance.
 *
 * \return  Always returns 0 indicating success.
 */
static int 
granttable_probe(device_t dev)
{

	device_set_desc(dev, "Xen Grant-table Device");
	return (BUS_PROBE_NOWILDCARD);
}

/**
 * \brief Attach the Xen Grant-table device.
 *
 * \param dev  NewBus device_t for this instance.
 *
 * \return  On success, 0. Otherwise an errno value indicating the
 *          type of failure.
 */
static int
granttable_attach(device_t dev)
{
	int i;
	unsigned int max_nr_glist_frames;
	unsigned int nr_init_grefs;

	nr_grant_frames = 1;
	boot_max_nr_grant_frames = __max_nr_grant_frames();

	/* Determine the maximum number of frames required for the
	 * grant reference free list on the current hypervisor.
	 */
	max_nr_glist_frames = (boot_max_nr_grant_frames *
			       GREFS_PER_GRANT_FRAME /
			       (PAGE_SIZE / sizeof(grant_ref_t)));

	gnttab_list = malloc(max_nr_glist_frames * sizeof(grant_ref_t *),
	    M_DEVBUF, M_NOWAIT);

	if (gnttab_list == NULL)
		return (ENOMEM);

	for (i = 0; i < nr_grant_frames; i++) {
		gnttab_list[i] = (grant_ref_t *)
			malloc(PAGE_SIZE, M_DEVBUF, M_NOWAIT);
		if (gnttab_list[i] == NULL)
			goto ini_nomem;
	}

	if (gnttab_resume(dev))
		return (ENODEV);

	nr_init_grefs = nr_grant_frames * GREFS_PER_GRANT_FRAME;

	for (i = NR_RESERVED_ENTRIES; i < nr_init_grefs - 1; i++)
		gnttab_entry(i) = i + 1;

	gnttab_entry(nr_init_grefs - 1) = GNTTAB_LIST_END;
	gnttab_free_count = nr_init_grefs - NR_RESERVED_ENTRIES;
	gnttab_free_head  = NR_RESERVED_ENTRIES;

	if (bootverbose)
		printf("Grant table initialized\n");

	return (0);

ini_nomem:
	for (i--; i >= 0; i--)
		free(gnttab_list[i], M_DEVBUF);
	free(gnttab_list, M_DEVBUF);
	return (ENOMEM);
}

/*-------------------- Private Device Attachment Data  -----------------------*/
static device_method_t granttable_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	granttable_identify),
	DEVMETHOD(device_probe,         granttable_probe),
	DEVMETHOD(device_attach,        granttable_attach),

	DEVMETHOD_END
};

DEFINE_CLASS_0(granttable, granttable_driver, granttable_methods, 0);
devclass_t granttable_devclass;

DRIVER_MODULE_ORDERED(granttable, xenpv, granttable_driver, granttable_devclass,
    NULL, NULL, SI_ORDER_FIRST);
