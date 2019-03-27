/******************************************************************************
 * balloon.c
 *
 * Xen balloon driver - enables returning/claiming memory to/from Xen.
 *
 * Copyright (c) 2003, B Dragovic
 * Copyright (c) 2003-2004, M Williamson, K Fraser
 * Copyright (c) 2005 Dan M. Smith, IBM Corporation
 * 
 * This file may be distributed separately from the Linux kernel, or
 * incorporated into other software packages, subject to the following license:
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <sys/module.h>

#include <vm/vm.h>
#include <vm/vm_page.h>

#include <xen/xen-os.h>
#include <xen/hypervisor.h>
#include <xen/features.h>
#include <xen/xenstore/xenstorevar.h>

static MALLOC_DEFINE(M_BALLOON, "Balloon", "Xen Balloon Driver");

/* Convert from KB (as fetched from xenstore) to number of PAGES */
#define KB_TO_PAGE_SHIFT	(PAGE_SHIFT - 10)

struct mtx balloon_mutex;

/* We increase/decrease in batches which fit in a page */
static xen_pfn_t frame_list[PAGE_SIZE / sizeof(xen_pfn_t)];

struct balloon_stats {
	/* We aim for 'current allocation' == 'target allocation'. */
	unsigned long current_pages;
	unsigned long target_pages;
	/* We may hit the hard limit in Xen. If we do then we remember it. */
	unsigned long hard_limit;
	/*
	 * Drivers may alter the memory reservation independently, but they
	 * must inform the balloon driver so we avoid hitting the hard limit.
	 */
	unsigned long driver_pages;
	/* Number of pages in high- and low-memory balloons. */
	unsigned long balloon_low;
	unsigned long balloon_high;
};

static struct balloon_stats balloon_stats;
#define bs balloon_stats

SYSCTL_DECL(_dev_xen);
static SYSCTL_NODE(_dev_xen, OID_AUTO, balloon, CTLFLAG_RD, NULL, "Balloon");
SYSCTL_ULONG(_dev_xen_balloon, OID_AUTO, current, CTLFLAG_RD,
    &bs.current_pages, 0, "Current allocation");
SYSCTL_ULONG(_dev_xen_balloon, OID_AUTO, target, CTLFLAG_RD,
    &bs.target_pages, 0, "Target allocation");
SYSCTL_ULONG(_dev_xen_balloon, OID_AUTO, driver_pages, CTLFLAG_RD,
    &bs.driver_pages, 0, "Driver pages");
SYSCTL_ULONG(_dev_xen_balloon, OID_AUTO, hard_limit, CTLFLAG_RD,
    &bs.hard_limit, 0, "Xen hard limit");
SYSCTL_ULONG(_dev_xen_balloon, OID_AUTO, low_mem, CTLFLAG_RD,
    &bs.balloon_low, 0, "Low-mem balloon");
SYSCTL_ULONG(_dev_xen_balloon, OID_AUTO, high_mem, CTLFLAG_RD,
    &bs.balloon_high, 0, "High-mem balloon");

/* List of ballooned pages, threaded through the mem_map array. */
static TAILQ_HEAD(,vm_page) ballooned_pages;

/* Main work function, always executed in process context. */
static void balloon_process(void *unused);

#define IPRINTK(fmt, args...) \
	printk(KERN_INFO "xen_mem: " fmt, ##args)
#define WPRINTK(fmt, args...) \
	printk(KERN_WARNING "xen_mem: " fmt, ##args)

static unsigned long 
current_target(void)
{
	unsigned long target = min(bs.target_pages, bs.hard_limit);
	if (target > (bs.current_pages + bs.balloon_low + bs.balloon_high))
		target = bs.current_pages + bs.balloon_low + bs.balloon_high;
	return (target);
}

static unsigned long
minimum_target(void)
{
	unsigned long min_pages, curr_pages = current_target();

#define MB2PAGES(mb) ((mb) << (20 - PAGE_SHIFT))
	/*
	 * Simple continuous piecewiese linear function:
	 *  max MiB -> min MiB	gradient
	 *       0	   0
	 *      16	  16
	 *      32	  24
	 *     128	  72	(1/2)
	 *     512 	 168	(1/4)
	 *    2048	 360	(1/8)
	 *    8192	 552	(1/32)
	 *   32768	1320
	 *  131072	4392
	 */
	if (realmem < MB2PAGES(128))
		min_pages = MB2PAGES(8) + (realmem >> 1);
	else if (realmem < MB2PAGES(512))
		min_pages = MB2PAGES(40) + (realmem >> 2);
	else if (realmem < MB2PAGES(2048))
		min_pages = MB2PAGES(104) + (realmem >> 3);
	else
		min_pages = MB2PAGES(296) + (realmem >> 5);
#undef MB2PAGES

	/* Don't enforce growth */
	return (min(min_pages, curr_pages));
}

static int 
increase_reservation(unsigned long nr_pages)
{
	unsigned long  i;
	vm_page_t      page;
	long           rc;
	struct xen_memory_reservation reservation = {
		.address_bits = 0,
		.extent_order = 0,
		.domid        = DOMID_SELF
	};

	mtx_assert(&balloon_mutex, MA_OWNED);

	if (nr_pages > nitems(frame_list))
		nr_pages = nitems(frame_list);

	for (page = TAILQ_FIRST(&ballooned_pages), i = 0;
	    i < nr_pages; i++, page = TAILQ_NEXT(page, plinks.q)) {
		KASSERT(page != NULL, ("ballooned_pages list corrupt"));
		frame_list[i] = (VM_PAGE_TO_PHYS(page) >> PAGE_SHIFT);
	}

	set_xen_guest_handle(reservation.extent_start, frame_list);
	reservation.nr_extents   = nr_pages;
	rc = HYPERVISOR_memory_op(
		XENMEM_populate_physmap, &reservation);
	if (rc < nr_pages) {
		if (rc > 0) {
			int ret;

			/* We hit the Xen hard limit: reprobe. */
			reservation.nr_extents = rc;
			ret = HYPERVISOR_memory_op(XENMEM_decrease_reservation,
					&reservation);
			KASSERT(ret == rc, ("HYPERVISOR_memory_op failed"));
		}
		if (rc >= 0)
			bs.hard_limit = (bs.current_pages + rc -
					 bs.driver_pages);
		goto out;
	}

	for (i = 0; i < nr_pages; i++) {
		page = TAILQ_FIRST(&ballooned_pages);
		KASSERT(page != NULL, ("Unable to get ballooned page"));
		TAILQ_REMOVE(&ballooned_pages, page, plinks.q);
		bs.balloon_low--;

		KASSERT(xen_feature(XENFEAT_auto_translated_physmap),
		    ("auto translated physmap but mapping is valid"));

		vm_page_free(page);
	}

	bs.current_pages += nr_pages;

 out:
	return (0);
}

static int
decrease_reservation(unsigned long nr_pages)
{
	unsigned long  i;
	vm_page_t      page;
	int            need_sleep = 0;
	int ret;
	struct xen_memory_reservation reservation = {
		.address_bits = 0,
		.extent_order = 0,
		.domid        = DOMID_SELF
	};

	mtx_assert(&balloon_mutex, MA_OWNED);

	if (nr_pages > nitems(frame_list))
		nr_pages = nitems(frame_list);

	for (i = 0; i < nr_pages; i++) {
		if ((page = vm_page_alloc(NULL, 0, 
			    VM_ALLOC_NORMAL | VM_ALLOC_NOOBJ | 
			    VM_ALLOC_ZERO)) == NULL) {
			nr_pages = i;
			need_sleep = 1;
			break;
		}

		if ((page->flags & PG_ZERO) == 0) {
			/*
			 * Zero the page, or else we might be leaking
			 * important data to other domains on the same
			 * host. Xen doesn't scrub ballooned out memory
			 * pages, the guest is in charge of making
			 * sure that no information is leaked.
			 */
			pmap_zero_page(page);
		}

		frame_list[i] = (VM_PAGE_TO_PHYS(page) >> PAGE_SHIFT);

		TAILQ_INSERT_HEAD(&ballooned_pages, page, plinks.q);
		bs.balloon_low++;
	}

	set_xen_guest_handle(reservation.extent_start, frame_list);
	reservation.nr_extents   = nr_pages;
	ret = HYPERVISOR_memory_op(XENMEM_decrease_reservation, &reservation);
	KASSERT(ret == nr_pages, ("HYPERVISOR_memory_op failed"));

	bs.current_pages -= nr_pages;

	return (need_sleep);
}

/*
 * We avoid multiple worker processes conflicting via the balloon mutex.
 * We may of course race updates of the target counts (which are protected
 * by the balloon lock), or with changes to the Xen hard limit, but we will
 * recover from these in time.
 */
static void 
balloon_process(void *unused)
{
	int need_sleep = 0;
	long credit;
	
	mtx_lock(&balloon_mutex);
	for (;;) {
		int sleep_time;

		do {
			credit = current_target() - bs.current_pages;
			if (credit > 0)
				need_sleep = (increase_reservation(credit) != 0);
			if (credit < 0)
				need_sleep = (decrease_reservation(-credit) != 0);
			
		} while ((credit != 0) && !need_sleep);
		
		/* Schedule more work if there is some still to be done. */
		if (current_target() != bs.current_pages)
			sleep_time = hz;
		else
			sleep_time = 0;

		msleep(balloon_process, &balloon_mutex, 0, "balloon",
		       sleep_time);
	}
	mtx_unlock(&balloon_mutex);
}

/* Resets the Xen limit, sets new target, and kicks off processing. */
static void 
set_new_target(unsigned long target)
{
	/* No need for lock. Not read-modify-write updates. */
	bs.hard_limit   = ~0UL;
	bs.target_pages = max(target, minimum_target());
	wakeup(balloon_process);
}

static struct xs_watch target_watch =
{
	.node = "memory/target"
};

/* React to a change in the target key */
static void 
watch_target(struct xs_watch *watch,
	     const char **vec, unsigned int len)
{
	unsigned long long new_target;
	int err;

	err = xs_scanf(XST_NIL, "memory", "target", NULL,
	    "%llu", &new_target);
	if (err) {
		/* This is ok (for domain0 at least) - so just return */
		return;
	} 
        
	/*
	 * The given memory/target value is in KiB, so it needs converting to
	 * pages.  PAGE_SHIFT converts bytes to pages, hence PAGE_SHIFT - 10.
	 */
	set_new_target(new_target >> KB_TO_PAGE_SHIFT);
}

/*------------------ Private Device Attachment Functions  --------------------*/
/**
 * \brief Identify instances of this device type in the system.
 *
 * \param driver  The driver performing this identify action.
 * \param parent  The NewBus parent device for any devices this method adds.
 */
static void
xenballoon_identify(driver_t *driver __unused, device_t parent)
{
	/*
	 * A single device instance for our driver is always present
	 * in a system operating under Xen.
	 */
	BUS_ADD_CHILD(parent, 0, driver->name, 0);
}

/**
 * \brief Probe for the existence of the Xen Balloon device
 *
 * \param dev  NewBus device_t for this Xen control instance.
 *
 * \return  Always returns 0 indicating success.
 */
static int 
xenballoon_probe(device_t dev)
{

	device_set_desc(dev, "Xen Balloon Device");
	return (0);
}

/**
 * \brief Attach the Xen Balloon device.
 *
 * \param dev  NewBus device_t for this Xen control instance.
 *
 * \return  On success, 0. Otherwise an errno value indicating the
 *          type of failure.
 */
static int
xenballoon_attach(device_t dev)
{
	int err;

	mtx_init(&balloon_mutex, "balloon_mutex", NULL, MTX_DEF);

	bs.current_pages = realmem;
	bs.target_pages  = bs.current_pages;
	bs.balloon_low   = 0;
	bs.balloon_high  = 0;
	bs.driver_pages  = 0UL;
	bs.hard_limit    = ~0UL;

	kproc_create(balloon_process, NULL, NULL, 0, 0, "balloon");
    
	target_watch.callback = watch_target;

	err = xs_register_watch(&target_watch);
	if (err)
		device_printf(dev,
		    "xenballon: failed to set balloon watcher\n");

	return (err);
}

/*-------------------- Private Device Attachment Data  -----------------------*/
static device_method_t xenballoon_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	xenballoon_identify),
	DEVMETHOD(device_probe,         xenballoon_probe),
	DEVMETHOD(device_attach,        xenballoon_attach),

	DEVMETHOD_END
};

DEFINE_CLASS_0(xenballoon, xenballoon_driver, xenballoon_methods, 0);
devclass_t xenballoon_devclass;

DRIVER_MODULE(xenballoon, xenstore, xenballoon_driver, xenballoon_devclass,
    NULL, NULL);
