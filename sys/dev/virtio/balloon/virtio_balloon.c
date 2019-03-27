/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011, Bryan Venteicher <bryanv@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Driver for VirtIO memory balloon devices. */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/endian.h>
#include <sys/kthread.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/sglist.h>
#include <sys/sysctl.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/queue.h>

#include <vm/vm.h>
#include <vm/vm_page.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <dev/virtio/virtio.h>
#include <dev/virtio/virtqueue.h>
#include <dev/virtio/balloon/virtio_balloon.h>

#include "virtio_if.h"

struct vtballoon_softc {
	device_t		 vtballoon_dev;
	struct mtx		 vtballoon_mtx;
	uint64_t		 vtballoon_features;
	uint32_t		 vtballoon_flags;
#define VTBALLOON_FLAG_DETACH	 0x01

	struct virtqueue	*vtballoon_inflate_vq;
	struct virtqueue	*vtballoon_deflate_vq;

	uint32_t		 vtballoon_desired_npages;
	uint32_t		 vtballoon_current_npages;
	TAILQ_HEAD(,vm_page)	 vtballoon_pages;

	struct thread		*vtballoon_td;
	uint32_t		*vtballoon_page_frames;
	int			 vtballoon_timeout;
};

static struct virtio_feature_desc vtballoon_feature_desc[] = {
	{ VIRTIO_BALLOON_F_MUST_TELL_HOST,	"MustTellHost"	},
	{ VIRTIO_BALLOON_F_STATS_VQ,		"StatsVq"	},

	{ 0, NULL }
};

static int	vtballoon_probe(device_t);
static int	vtballoon_attach(device_t);
static int	vtballoon_detach(device_t);
static int	vtballoon_config_change(device_t);

static void	vtballoon_negotiate_features(struct vtballoon_softc *);
static int	vtballoon_alloc_virtqueues(struct vtballoon_softc *);

static void	vtballoon_vq_intr(void *);

static void	vtballoon_inflate(struct vtballoon_softc *, int);
static void	vtballoon_deflate(struct vtballoon_softc *, int);

static void	vtballoon_send_page_frames(struct vtballoon_softc *,
		    struct virtqueue *, int);

static void	vtballoon_pop(struct vtballoon_softc *);
static void	vtballoon_stop(struct vtballoon_softc *);

static vm_page_t
		vtballoon_alloc_page(struct vtballoon_softc *);
static void	vtballoon_free_page(struct vtballoon_softc *, vm_page_t);

static int	vtballoon_sleep(struct vtballoon_softc *);
static void	vtballoon_thread(void *);
static void	vtballoon_add_sysctl(struct vtballoon_softc *);

/* Features desired/implemented by this driver. */
#define VTBALLOON_FEATURES		0

/* Timeout between retries when the balloon needs inflating. */
#define VTBALLOON_LOWMEM_TIMEOUT	hz

/*
 * Maximum number of pages we'll request to inflate or deflate
 * the balloon in one virtqueue request. Both Linux and NetBSD
 * have settled on 256, doing up to 1MB at a time.
 */
#define VTBALLOON_PAGES_PER_REQUEST	256

/* Must be able to fix all pages frames in one page (segment). */
CTASSERT(VTBALLOON_PAGES_PER_REQUEST * sizeof(uint32_t) <= PAGE_SIZE);

#define VTBALLOON_MTX(_sc)		&(_sc)->vtballoon_mtx
#define VTBALLOON_LOCK_INIT(_sc, _name)	mtx_init(VTBALLOON_MTX((_sc)), _name, \
					    "VirtIO Balloon Lock", MTX_DEF)
#define VTBALLOON_LOCK(_sc)		mtx_lock(VTBALLOON_MTX((_sc)))
#define VTBALLOON_UNLOCK(_sc)		mtx_unlock(VTBALLOON_MTX((_sc)))
#define VTBALLOON_LOCK_DESTROY(_sc)	mtx_destroy(VTBALLOON_MTX((_sc)))

static device_method_t vtballoon_methods[] = {
	/* Device methods. */
	DEVMETHOD(device_probe,		vtballoon_probe),
	DEVMETHOD(device_attach,	vtballoon_attach),
	DEVMETHOD(device_detach,	vtballoon_detach),

	/* VirtIO methods. */
	DEVMETHOD(virtio_config_change, vtballoon_config_change),

	DEVMETHOD_END
};

static driver_t vtballoon_driver = {
	"vtballoon",
	vtballoon_methods,
	sizeof(struct vtballoon_softc)
};
static devclass_t vtballoon_devclass;

DRIVER_MODULE(virtio_balloon, virtio_pci, vtballoon_driver,
    vtballoon_devclass, 0, 0);
MODULE_VERSION(virtio_balloon, 1);
MODULE_DEPEND(virtio_balloon, virtio, 1, 1, 1);

static int
vtballoon_probe(device_t dev)
{

	if (virtio_get_device_type(dev) != VIRTIO_ID_BALLOON)
		return (ENXIO);

	device_set_desc(dev, "VirtIO Balloon Adapter");

	return (BUS_PROBE_DEFAULT);
}

static int
vtballoon_attach(device_t dev)
{
	struct vtballoon_softc *sc;
	int error;

	sc = device_get_softc(dev);
	sc->vtballoon_dev = dev;

	VTBALLOON_LOCK_INIT(sc, device_get_nameunit(dev));
	TAILQ_INIT(&sc->vtballoon_pages);

	vtballoon_add_sysctl(sc);

	virtio_set_feature_desc(dev, vtballoon_feature_desc);
	vtballoon_negotiate_features(sc);

	sc->vtballoon_page_frames = malloc(VTBALLOON_PAGES_PER_REQUEST *
	    sizeof(uint32_t), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (sc->vtballoon_page_frames == NULL) {
		error = ENOMEM;
		device_printf(dev,
		    "cannot allocate page frame request array\n");
		goto fail;
	}

	error = vtballoon_alloc_virtqueues(sc);
	if (error) {
		device_printf(dev, "cannot allocate virtqueues\n");
		goto fail;
	}

	error = virtio_setup_intr(dev, INTR_TYPE_MISC);
	if (error) {
		device_printf(dev, "cannot setup virtqueue interrupts\n");
		goto fail;
	}

	error = kthread_add(vtballoon_thread, sc, NULL, &sc->vtballoon_td,
	    0, 0, "virtio_balloon");
	if (error) {
		device_printf(dev, "cannot create balloon kthread\n");
		goto fail;
	}

	virtqueue_enable_intr(sc->vtballoon_inflate_vq);
	virtqueue_enable_intr(sc->vtballoon_deflate_vq);

fail:
	if (error)
		vtballoon_detach(dev);

	return (error);
}

static int
vtballoon_detach(device_t dev)
{
	struct vtballoon_softc *sc;

	sc = device_get_softc(dev);

	if (sc->vtballoon_td != NULL) {
		VTBALLOON_LOCK(sc);
		sc->vtballoon_flags |= VTBALLOON_FLAG_DETACH;
		wakeup_one(sc);
		msleep(sc->vtballoon_td, VTBALLOON_MTX(sc), 0, "vtbdth", 0);
		VTBALLOON_UNLOCK(sc);

		sc->vtballoon_td = NULL;
	}

	if (device_is_attached(dev)) {
		vtballoon_pop(sc);
		vtballoon_stop(sc);
	}

	if (sc->vtballoon_page_frames != NULL) {
		free(sc->vtballoon_page_frames, M_DEVBUF);
		sc->vtballoon_page_frames = NULL;
	}

	VTBALLOON_LOCK_DESTROY(sc);

	return (0);
}

static int
vtballoon_config_change(device_t dev)
{
	struct vtballoon_softc *sc;

	sc = device_get_softc(dev);

	VTBALLOON_LOCK(sc);
	wakeup_one(sc);
	VTBALLOON_UNLOCK(sc);

	return (1);
}

static void
vtballoon_negotiate_features(struct vtballoon_softc *sc)
{
	device_t dev;
	uint64_t features;

	dev = sc->vtballoon_dev;
	features = virtio_negotiate_features(dev, VTBALLOON_FEATURES);
	sc->vtballoon_features = features;
}

static int
vtballoon_alloc_virtqueues(struct vtballoon_softc *sc)
{
	device_t dev;
	struct vq_alloc_info vq_info[2];
	int nvqs;

	dev = sc->vtballoon_dev;
	nvqs = 2;

	VQ_ALLOC_INFO_INIT(&vq_info[0], 0, vtballoon_vq_intr, sc,
	    &sc->vtballoon_inflate_vq, "%s inflate", device_get_nameunit(dev));

	VQ_ALLOC_INFO_INIT(&vq_info[1], 0, vtballoon_vq_intr, sc,
	    &sc->vtballoon_deflate_vq, "%s deflate", device_get_nameunit(dev));

	return (virtio_alloc_virtqueues(dev, 0, nvqs, vq_info));
}

static void
vtballoon_vq_intr(void *xsc)
{
	struct vtballoon_softc *sc;

	sc = xsc;

	VTBALLOON_LOCK(sc);
	wakeup_one(sc);
	VTBALLOON_UNLOCK(sc);
}

static void
vtballoon_inflate(struct vtballoon_softc *sc, int npages)
{
	struct virtqueue *vq;
	vm_page_t m;
	int i;

	vq = sc->vtballoon_inflate_vq;

	if (npages > VTBALLOON_PAGES_PER_REQUEST)
		npages = VTBALLOON_PAGES_PER_REQUEST;

	for (i = 0; i < npages; i++) {
		if ((m = vtballoon_alloc_page(sc)) == NULL) {
			sc->vtballoon_timeout = VTBALLOON_LOWMEM_TIMEOUT;
			break;
		}

		sc->vtballoon_page_frames[i] =
		    VM_PAGE_TO_PHYS(m) >> VIRTIO_BALLOON_PFN_SHIFT;

		KASSERT(m->queue == PQ_NONE,
		    ("%s: allocated page %p on queue", __func__, m));
		TAILQ_INSERT_TAIL(&sc->vtballoon_pages, m, plinks.q);
	}

	if (i > 0)
		vtballoon_send_page_frames(sc, vq, i);
}

static void
vtballoon_deflate(struct vtballoon_softc *sc, int npages)
{
	TAILQ_HEAD(, vm_page) free_pages;
	struct virtqueue *vq;
	vm_page_t m;
	int i;

	vq = sc->vtballoon_deflate_vq;
	TAILQ_INIT(&free_pages);

	if (npages > VTBALLOON_PAGES_PER_REQUEST)
		npages = VTBALLOON_PAGES_PER_REQUEST;

	for (i = 0; i < npages; i++) {
		m = TAILQ_FIRST(&sc->vtballoon_pages);
		KASSERT(m != NULL, ("%s: no more pages to deflate", __func__));

		sc->vtballoon_page_frames[i] =
		    VM_PAGE_TO_PHYS(m) >> VIRTIO_BALLOON_PFN_SHIFT;

		TAILQ_REMOVE(&sc->vtballoon_pages, m, plinks.q);
		TAILQ_INSERT_TAIL(&free_pages, m, plinks.q);
	}

	if (i > 0) {
		/* Always tell host first before freeing the pages. */
		vtballoon_send_page_frames(sc, vq, i);

		while ((m = TAILQ_FIRST(&free_pages)) != NULL) {
			TAILQ_REMOVE(&free_pages, m, plinks.q);
			vtballoon_free_page(sc, m);
		}
	}

	KASSERT((TAILQ_EMPTY(&sc->vtballoon_pages) &&
	    sc->vtballoon_current_npages == 0) ||
	    (!TAILQ_EMPTY(&sc->vtballoon_pages) &&
	    sc->vtballoon_current_npages != 0),
	    ("%s: bogus page count %d", __func__,
	    sc->vtballoon_current_npages));
}

static void
vtballoon_send_page_frames(struct vtballoon_softc *sc, struct virtqueue *vq,
    int npages)
{
	struct sglist sg;
	struct sglist_seg segs[1];
	void *c;
	int error;

	sglist_init(&sg, 1, segs);

	error = sglist_append(&sg, sc->vtballoon_page_frames,
	    npages * sizeof(uint32_t));
	KASSERT(error == 0, ("error adding page frames to sglist"));

	error = virtqueue_enqueue(vq, vq, &sg, 1, 0);
	KASSERT(error == 0, ("error enqueuing page frames to virtqueue"));
	virtqueue_notify(vq);

	/*
	 * Inflate and deflate operations are done synchronously. The
	 * interrupt handler will wake us up.
	 */
	VTBALLOON_LOCK(sc);
	while ((c = virtqueue_dequeue(vq, NULL)) == NULL)
		msleep(sc, VTBALLOON_MTX(sc), 0, "vtbspf", 0);
	VTBALLOON_UNLOCK(sc);

	KASSERT(c == vq, ("unexpected balloon operation response"));
}

static void
vtballoon_pop(struct vtballoon_softc *sc)
{

	while (!TAILQ_EMPTY(&sc->vtballoon_pages))
		vtballoon_deflate(sc, sc->vtballoon_current_npages);
}

static void
vtballoon_stop(struct vtballoon_softc *sc)
{

	virtqueue_disable_intr(sc->vtballoon_inflate_vq);
	virtqueue_disable_intr(sc->vtballoon_deflate_vq);

	virtio_stop(sc->vtballoon_dev);
}

static vm_page_t
vtballoon_alloc_page(struct vtballoon_softc *sc)
{
	vm_page_t m;

	m = vm_page_alloc(NULL, 0, VM_ALLOC_NORMAL | VM_ALLOC_NOOBJ);
	if (m != NULL)
		sc->vtballoon_current_npages++;

	return (m);
}

static void
vtballoon_free_page(struct vtballoon_softc *sc, vm_page_t m)
{

	vm_page_free(m);
	sc->vtballoon_current_npages--;
}

static uint32_t
vtballoon_desired_size(struct vtballoon_softc *sc)
{
	uint32_t desired;

	desired = virtio_read_dev_config_4(sc->vtballoon_dev,
	    offsetof(struct virtio_balloon_config, num_pages));

	return (le32toh(desired));
}

static void
vtballoon_update_size(struct vtballoon_softc *sc)
{

	virtio_write_dev_config_4(sc->vtballoon_dev,
	    offsetof(struct virtio_balloon_config, actual),
	    htole32(sc->vtballoon_current_npages));
}

static int
vtballoon_sleep(struct vtballoon_softc *sc)
{
	int rc, timeout;
	uint32_t current, desired;

	rc = 0;
	current = sc->vtballoon_current_npages;

	VTBALLOON_LOCK(sc);
	for (;;) {
		if (sc->vtballoon_flags & VTBALLOON_FLAG_DETACH) {
			rc = 1;
			break;
		}

		desired = vtballoon_desired_size(sc);
		sc->vtballoon_desired_npages = desired;

		/*
		 * If given, use non-zero timeout on the first time through
		 * the loop. On subsequent times, timeout will be zero so
		 * we will reevaluate the desired size of the balloon and
		 * break out to retry if needed.
		 */
		timeout = sc->vtballoon_timeout;
		sc->vtballoon_timeout = 0;

		if (current > desired)
			break;
		if (current < desired && timeout == 0)
			break;

		msleep(sc, VTBALLOON_MTX(sc), 0, "vtbslp", timeout);
	}
	VTBALLOON_UNLOCK(sc);

	return (rc);
}

static void
vtballoon_thread(void *xsc)
{
	struct vtballoon_softc *sc;
	uint32_t current, desired;

	sc = xsc;

	for (;;) {
		if (vtballoon_sleep(sc) != 0)
			break;

		current = sc->vtballoon_current_npages;
		desired = sc->vtballoon_desired_npages;

		if (desired != current) {
			if (desired > current)
				vtballoon_inflate(sc, desired - current);
			else
				vtballoon_deflate(sc, current - desired);

			vtballoon_update_size(sc);
		}
	}

	kthread_exit();
}

static void
vtballoon_add_sysctl(struct vtballoon_softc *sc)
{
	device_t dev;
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;
	struct sysctl_oid_list *child;

	dev = sc->vtballoon_dev;
	ctx = device_get_sysctl_ctx(dev);
	tree = device_get_sysctl_tree(dev);
	child = SYSCTL_CHILDREN(tree);

	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "desired",
	    CTLFLAG_RD, &sc->vtballoon_desired_npages, sizeof(uint32_t),
	    "Desired balloon size in pages");

	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "current",
	    CTLFLAG_RD, &sc->vtballoon_current_npages, sizeof(uint32_t),
	    "Current balloon size in pages");
}
