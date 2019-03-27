/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013, Bryan Venteicher <bryanv@FreeBSD.org>
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

/* Driver for VirtIO entropy device. */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/sglist.h>
#include <sys/callout.h>
#include <sys/random.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/bus.h>

#include <dev/virtio/virtio.h>
#include <dev/virtio/virtqueue.h>

struct vtrnd_softc {
	device_t		 vtrnd_dev;
	uint64_t		 vtrnd_features;
	struct callout		 vtrnd_callout;
	struct virtqueue	*vtrnd_vq;
};

static int	vtrnd_modevent(module_t, int, void *);

static int	vtrnd_probe(device_t);
static int	vtrnd_attach(device_t);
static int	vtrnd_detach(device_t);

static void	vtrnd_negotiate_features(struct vtrnd_softc *);
static int	vtrnd_alloc_virtqueue(struct vtrnd_softc *);
static void	vtrnd_harvest(struct vtrnd_softc *);
static void	vtrnd_timer(void *);

#define VTRND_FEATURES	0

static struct virtio_feature_desc vtrnd_feature_desc[] = {
	{ 0, NULL }
};

static device_method_t vtrnd_methods[] = {
	/* Device methods. */
	DEVMETHOD(device_probe,		vtrnd_probe),
	DEVMETHOD(device_attach,	vtrnd_attach),
	DEVMETHOD(device_detach,	vtrnd_detach),

	DEVMETHOD_END
};

static driver_t vtrnd_driver = {
	"vtrnd",
	vtrnd_methods,
	sizeof(struct vtrnd_softc)
};
static devclass_t vtrnd_devclass;

DRIVER_MODULE(virtio_random, virtio_pci, vtrnd_driver, vtrnd_devclass,
    vtrnd_modevent, 0);
MODULE_VERSION(virtio_random, 1);
MODULE_DEPEND(virtio_random, virtio, 1, 1, 1);

static int
vtrnd_modevent(module_t mod, int type, void *unused)
{
	int error;

	switch (type) {
	case MOD_LOAD:
	case MOD_QUIESCE:
	case MOD_UNLOAD:
	case MOD_SHUTDOWN:
		error = 0;
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}

	return (error);
}

static int
vtrnd_probe(device_t dev)
{

	if (virtio_get_device_type(dev) != VIRTIO_ID_ENTROPY)
		return (ENXIO);

	device_set_desc(dev, "VirtIO Entropy Adapter");

	return (BUS_PROBE_DEFAULT);
}

static int
vtrnd_attach(device_t dev)
{
	struct vtrnd_softc *sc;
	int error;

	sc = device_get_softc(dev);
	sc->vtrnd_dev = dev;

	callout_init(&sc->vtrnd_callout, 1);

	virtio_set_feature_desc(dev, vtrnd_feature_desc);
	vtrnd_negotiate_features(sc);

	error = vtrnd_alloc_virtqueue(sc);
	if (error) {
		device_printf(dev, "cannot allocate virtqueue\n");
		goto fail;
	}

	callout_reset(&sc->vtrnd_callout, 5 * hz, vtrnd_timer, sc);

fail:
	if (error)
		vtrnd_detach(dev);

	return (error);
}

static int
vtrnd_detach(device_t dev)
{
	struct vtrnd_softc *sc;

	sc = device_get_softc(dev);

	callout_drain(&sc->vtrnd_callout);

	return (0);
}

static void
vtrnd_negotiate_features(struct vtrnd_softc *sc)
{
	device_t dev;
	uint64_t features;

	dev = sc->vtrnd_dev;
	features = VTRND_FEATURES;

	sc->vtrnd_features = virtio_negotiate_features(dev, features);
}

static int
vtrnd_alloc_virtqueue(struct vtrnd_softc *sc)
{
	device_t dev;
	struct vq_alloc_info vq_info;

	dev = sc->vtrnd_dev;

	VQ_ALLOC_INFO_INIT(&vq_info, 0, NULL, sc, &sc->vtrnd_vq,
	    "%s request", device_get_nameunit(dev));

	return (virtio_alloc_virtqueues(dev, 0, 1, &vq_info));
}

static void
vtrnd_harvest(struct vtrnd_softc *sc)
{
	struct sglist_seg segs[1];
	struct sglist sg;
	struct virtqueue *vq;
	uint32_t value;
	int error;

	vq = sc->vtrnd_vq;

	sglist_init(&sg, 1, segs);
	error = sglist_append(&sg, &value, sizeof(value));
	KASSERT(error == 0 && sg.sg_nseg == 1,
	    ("%s: error %d adding buffer to sglist", __func__, error));

	if (!virtqueue_empty(vq))
		return;
	if (virtqueue_enqueue(vq, &value, &sg, 0, 1) != 0)
		return;

	/*
	 * Poll for the response, but the command is likely already
	 * done when we return from the notify.
	 */
	virtqueue_notify(vq);
	virtqueue_poll(vq, NULL);

	random_harvest_queue(&value, sizeof(value), RANDOM_PURE_VIRTIO);
}

static void
vtrnd_timer(void *xsc)
{
	struct vtrnd_softc *sc;

	sc = xsc;

	vtrnd_harvest(sc);
	callout_schedule(&sc->vtrnd_callout, 5 * hz);
}
