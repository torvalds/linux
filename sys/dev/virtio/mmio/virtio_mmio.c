/*-
 * Copyright (c) 2014 Ruslan Bukin <br@bsdpad.com>
 * Copyright (c) 2014 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Portions of this software were developed by Andrew Turner
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * VirtIO MMIO interface.
 * This driver is heavily based on VirtIO PCI interface driver.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/virtio/virtio.h>
#include <dev/virtio/virtqueue.h>
#include <dev/virtio/mmio/virtio_mmio.h>

#include "virtio_mmio_if.h"
#include "virtio_bus_if.h"
#include "virtio_if.h"

#define	PAGE_SHIFT	12

struct vtmmio_virtqueue {
	struct virtqueue	*vtv_vq;
	int			 vtv_no_intr;
};

static int	vtmmio_detach(device_t);
static int	vtmmio_suspend(device_t);
static int	vtmmio_resume(device_t);
static int	vtmmio_shutdown(device_t);
static void	vtmmio_driver_added(device_t, driver_t *);
static void	vtmmio_child_detached(device_t, device_t);
static int	vtmmio_read_ivar(device_t, device_t, int, uintptr_t *);
static int	vtmmio_write_ivar(device_t, device_t, int, uintptr_t);
static uint64_t	vtmmio_negotiate_features(device_t, uint64_t);
static int	vtmmio_with_feature(device_t, uint64_t);
static int	vtmmio_alloc_virtqueues(device_t, int, int,
		    struct vq_alloc_info *);
static int	vtmmio_setup_intr(device_t, enum intr_type);
static void	vtmmio_stop(device_t);
static void	vtmmio_poll(device_t);
static int	vtmmio_reinit(device_t, uint64_t);
static void	vtmmio_reinit_complete(device_t);
static void	vtmmio_notify_virtqueue(device_t, uint16_t);
static uint8_t	vtmmio_get_status(device_t);
static void	vtmmio_set_status(device_t, uint8_t);
static void	vtmmio_read_dev_config(device_t, bus_size_t, void *, int);
static void	vtmmio_write_dev_config(device_t, bus_size_t, void *, int);
static void	vtmmio_describe_features(struct vtmmio_softc *, const char *,
		    uint64_t);
static void	vtmmio_probe_and_attach_child(struct vtmmio_softc *);
static int	vtmmio_reinit_virtqueue(struct vtmmio_softc *, int);
static void	vtmmio_free_interrupts(struct vtmmio_softc *);
static void	vtmmio_free_virtqueues(struct vtmmio_softc *);
static void	vtmmio_release_child_resources(struct vtmmio_softc *);
static void	vtmmio_reset(struct vtmmio_softc *);
static void	vtmmio_select_virtqueue(struct vtmmio_softc *, int);
static void	vtmmio_vq_intr(void *);

/*
 * I/O port read/write wrappers.
 */
#define vtmmio_write_config_1(sc, o, v)				\
do {								\
	if (sc->platform != NULL)				\
		VIRTIO_MMIO_PREWRITE(sc->platform, (o), (v));	\
	bus_write_1((sc)->res[0], (o), (v)); 			\
	if (sc->platform != NULL)				\
		VIRTIO_MMIO_NOTE(sc->platform, (o), (v));	\
} while (0)
#define vtmmio_write_config_2(sc, o, v)				\
do {								\
	if (sc->platform != NULL)				\
		VIRTIO_MMIO_PREWRITE(sc->platform, (o), (v));	\
	bus_write_2((sc)->res[0], (o), (v));			\
	if (sc->platform != NULL)				\
		VIRTIO_MMIO_NOTE(sc->platform, (o), (v));	\
} while (0)
#define vtmmio_write_config_4(sc, o, v)				\
do {								\
	if (sc->platform != NULL)				\
		VIRTIO_MMIO_PREWRITE(sc->platform, (o), (v));	\
	bus_write_4((sc)->res[0], (o), (v));			\
	if (sc->platform != NULL)				\
		VIRTIO_MMIO_NOTE(sc->platform, (o), (v));	\
} while (0)

#define vtmmio_read_config_1(sc, o) \
	bus_read_1((sc)->res[0], (o))
#define vtmmio_read_config_2(sc, o) \
	bus_read_2((sc)->res[0], (o))
#define vtmmio_read_config_4(sc, o) \
	bus_read_4((sc)->res[0], (o))

static device_method_t vtmmio_methods[] = {
	/* Device interface. */
	DEVMETHOD(device_attach,		  vtmmio_attach),
	DEVMETHOD(device_detach,		  vtmmio_detach),
	DEVMETHOD(device_suspend,		  vtmmio_suspend),
	DEVMETHOD(device_resume,		  vtmmio_resume),
	DEVMETHOD(device_shutdown,		  vtmmio_shutdown),

	/* Bus interface. */
	DEVMETHOD(bus_driver_added,		  vtmmio_driver_added),
	DEVMETHOD(bus_child_detached,		  vtmmio_child_detached),
	DEVMETHOD(bus_read_ivar,		  vtmmio_read_ivar),
	DEVMETHOD(bus_write_ivar,		  vtmmio_write_ivar),

	/* VirtIO bus interface. */
	DEVMETHOD(virtio_bus_negotiate_features,  vtmmio_negotiate_features),
	DEVMETHOD(virtio_bus_with_feature,	  vtmmio_with_feature),
	DEVMETHOD(virtio_bus_alloc_virtqueues,	  vtmmio_alloc_virtqueues),
	DEVMETHOD(virtio_bus_setup_intr,	  vtmmio_setup_intr),
	DEVMETHOD(virtio_bus_stop,		  vtmmio_stop),
	DEVMETHOD(virtio_bus_poll,		  vtmmio_poll),
	DEVMETHOD(virtio_bus_reinit,		  vtmmio_reinit),
	DEVMETHOD(virtio_bus_reinit_complete,	  vtmmio_reinit_complete),
	DEVMETHOD(virtio_bus_notify_vq,		  vtmmio_notify_virtqueue),
	DEVMETHOD(virtio_bus_read_device_config,  vtmmio_read_dev_config),
	DEVMETHOD(virtio_bus_write_device_config, vtmmio_write_dev_config),

	DEVMETHOD_END
};

DEFINE_CLASS_0(virtio_mmio, vtmmio_driver, vtmmio_methods,
    sizeof(struct vtmmio_softc));

MODULE_VERSION(virtio_mmio, 1);

static int
vtmmio_setup_intr(device_t dev, enum intr_type type)
{
	struct vtmmio_softc *sc;
	int rid;
	int err;

	sc = device_get_softc(dev);

	if (sc->platform != NULL) {
		err = VIRTIO_MMIO_SETUP_INTR(sc->platform, sc->dev,
					vtmmio_vq_intr, sc);
		if (err == 0) {
			/* Okay we have backend-specific interrupts */
			return (0);
		}
	}

	rid = 0;
	sc->res[1] = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
		RF_ACTIVE);
	if (!sc->res[1]) {
		device_printf(dev, "Can't allocate interrupt\n");
		return (ENXIO);
	}

	if (bus_setup_intr(dev, sc->res[1], INTR_TYPE_MISC | INTR_MPSAFE,
		NULL, vtmmio_vq_intr, sc, &sc->ih)) {
		device_printf(dev, "Can't setup the interrupt\n");
		return (ENXIO);
	}

	return (0);
}

int
vtmmio_attach(device_t dev)
{
	struct vtmmio_softc *sc;
	device_t child;
	int rid;

	sc = device_get_softc(dev);
	sc->dev = dev;

	rid = 0;
	sc->res[0] = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
			RF_ACTIVE);
	if (!sc->res[0]) {
		device_printf(dev, "Cannot allocate memory window.\n");
		return (ENXIO);
	}

	vtmmio_reset(sc);

	/* Tell the host we've noticed this device. */
	vtmmio_set_status(dev, VIRTIO_CONFIG_STATUS_ACK);

	if ((child = device_add_child(dev, NULL, -1)) == NULL) {
		device_printf(dev, "Cannot create child device.\n");
		vtmmio_set_status(dev, VIRTIO_CONFIG_STATUS_FAILED);
		vtmmio_detach(dev);
		return (ENOMEM);
	}

	sc->vtmmio_child_dev = child;
	vtmmio_probe_and_attach_child(sc);

	return (0);
}

static int
vtmmio_detach(device_t dev)
{
	struct vtmmio_softc *sc;
	device_t child;
	int error;

	sc = device_get_softc(dev);

	if ((child = sc->vtmmio_child_dev) != NULL) {
		error = device_delete_child(dev, child);
		if (error)
			return (error);
		sc->vtmmio_child_dev = NULL;
	}

	vtmmio_reset(sc);

	if (sc->res[0] != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, 0,
		    sc->res[0]);
		sc->res[0] = NULL;
	}

	return (0);
}

static int
vtmmio_suspend(device_t dev)
{

	return (bus_generic_suspend(dev));
}

static int
vtmmio_resume(device_t dev)
{

	return (bus_generic_resume(dev));
}

static int
vtmmio_shutdown(device_t dev)
{

	(void) bus_generic_shutdown(dev);

	/* Forcibly stop the host device. */
	vtmmio_stop(dev);

	return (0);
}

static void
vtmmio_driver_added(device_t dev, driver_t *driver)
{
	struct vtmmio_softc *sc;

	sc = device_get_softc(dev);

	vtmmio_probe_and_attach_child(sc);
}

static void
vtmmio_child_detached(device_t dev, device_t child)
{
	struct vtmmio_softc *sc;

	sc = device_get_softc(dev);

	vtmmio_reset(sc);
	vtmmio_release_child_resources(sc);
}

static int
vtmmio_read_ivar(device_t dev, device_t child, int index, uintptr_t *result)
{
	struct vtmmio_softc *sc;

	sc = device_get_softc(dev);

	if (sc->vtmmio_child_dev != child)
		return (ENOENT);

	switch (index) {
	case VIRTIO_IVAR_DEVTYPE:
	case VIRTIO_IVAR_SUBDEVICE:
		*result = vtmmio_read_config_4(sc, VIRTIO_MMIO_DEVICE_ID);
		break;
	case VIRTIO_IVAR_VENDOR:
		*result = vtmmio_read_config_4(sc, VIRTIO_MMIO_VENDOR_ID);
		break;
	default:
		return (ENOENT);
	}

	return (0);
}

static int
vtmmio_write_ivar(device_t dev, device_t child, int index, uintptr_t value)
{
	struct vtmmio_softc *sc;

	sc = device_get_softc(dev);

	if (sc->vtmmio_child_dev != child)
		return (ENOENT);

	switch (index) {
	case VIRTIO_IVAR_FEATURE_DESC:
		sc->vtmmio_child_feat_desc = (void *) value;
		break;
	default:
		return (ENOENT);
	}

	return (0);
}

static uint64_t
vtmmio_negotiate_features(device_t dev, uint64_t child_features)
{
	struct vtmmio_softc *sc;
	uint64_t host_features, features;

	sc = device_get_softc(dev);

	host_features = vtmmio_read_config_4(sc, VIRTIO_MMIO_HOST_FEATURES);
	vtmmio_describe_features(sc, "host", host_features);

	/*
	 * Limit negotiated features to what the driver, virtqueue, and
	 * host all support.
	 */
	features = host_features & child_features;
	features = virtqueue_filter_features(features);
	sc->vtmmio_features = features;

	vtmmio_describe_features(sc, "negotiated", features);
	vtmmio_write_config_4(sc, VIRTIO_MMIO_GUEST_FEATURES, features);

	return (features);
}

static int
vtmmio_with_feature(device_t dev, uint64_t feature)
{
	struct vtmmio_softc *sc;

	sc = device_get_softc(dev);

	return ((sc->vtmmio_features & feature) != 0);
}

static int
vtmmio_alloc_virtqueues(device_t dev, int flags, int nvqs,
    struct vq_alloc_info *vq_info)
{
	struct vtmmio_virtqueue *vqx;
	struct vq_alloc_info *info;
	struct vtmmio_softc *sc;
	struct virtqueue *vq;
	uint32_t size;
	int idx, error;

	sc = device_get_softc(dev);

	if (sc->vtmmio_nvqs != 0)
		return (EALREADY);
	if (nvqs <= 0)
		return (EINVAL);

	sc->vtmmio_vqs = malloc(nvqs * sizeof(struct vtmmio_virtqueue),
	    M_DEVBUF, M_NOWAIT | M_ZERO);
	if (sc->vtmmio_vqs == NULL)
		return (ENOMEM);

	vtmmio_write_config_4(sc, VIRTIO_MMIO_GUEST_PAGE_SIZE,
	    (1 << PAGE_SHIFT));

	for (idx = 0; idx < nvqs; idx++) {
		vqx = &sc->vtmmio_vqs[idx];
		info = &vq_info[idx];

		vtmmio_write_config_4(sc, VIRTIO_MMIO_QUEUE_SEL, idx);

		vtmmio_select_virtqueue(sc, idx);
		size = vtmmio_read_config_4(sc, VIRTIO_MMIO_QUEUE_NUM_MAX);

		error = virtqueue_alloc(dev, idx, size,
		    VIRTIO_MMIO_VRING_ALIGN, 0xFFFFFFFFUL, info, &vq);
		if (error) {
			device_printf(dev,
			    "cannot allocate virtqueue %d: %d\n",
			    idx, error);
			break;
		}

		vtmmio_write_config_4(sc, VIRTIO_MMIO_QUEUE_NUM, size);
		vtmmio_write_config_4(sc, VIRTIO_MMIO_QUEUE_ALIGN,
		    VIRTIO_MMIO_VRING_ALIGN);
#if 0
		device_printf(dev, "virtqueue paddr 0x%08lx\n",
		    (uint64_t)virtqueue_paddr(vq));
#endif
		vtmmio_write_config_4(sc, VIRTIO_MMIO_QUEUE_PFN,
		    virtqueue_paddr(vq) >> PAGE_SHIFT);

		vqx->vtv_vq = *info->vqai_vq = vq;
		vqx->vtv_no_intr = info->vqai_intr == NULL;

		sc->vtmmio_nvqs++;
	}

	if (error)
		vtmmio_free_virtqueues(sc);

	return (error);
}

static void
vtmmio_stop(device_t dev)
{

	vtmmio_reset(device_get_softc(dev));
}

static void
vtmmio_poll(device_t dev)
{
	struct vtmmio_softc *sc;

	sc = device_get_softc(dev);

	if (sc->platform != NULL)
		VIRTIO_MMIO_POLL(sc->platform);
}

static int
vtmmio_reinit(device_t dev, uint64_t features)
{
	struct vtmmio_softc *sc;
	int idx, error;

	sc = device_get_softc(dev);

	if (vtmmio_get_status(dev) != VIRTIO_CONFIG_STATUS_RESET)
		vtmmio_stop(dev);

	/*
	 * Quickly drive the status through ACK and DRIVER. The device
	 * does not become usable again until vtmmio_reinit_complete().
	 */
	vtmmio_set_status(dev, VIRTIO_CONFIG_STATUS_ACK);
	vtmmio_set_status(dev, VIRTIO_CONFIG_STATUS_DRIVER);

	vtmmio_negotiate_features(dev, features);

	vtmmio_write_config_4(sc, VIRTIO_MMIO_GUEST_PAGE_SIZE,
	    (1 << PAGE_SHIFT));

	for (idx = 0; idx < sc->vtmmio_nvqs; idx++) {
		error = vtmmio_reinit_virtqueue(sc, idx);
		if (error)
			return (error);
	}

	return (0);
}

static void
vtmmio_reinit_complete(device_t dev)
{

	vtmmio_set_status(dev, VIRTIO_CONFIG_STATUS_DRIVER_OK);
}

static void
vtmmio_notify_virtqueue(device_t dev, uint16_t queue)
{
	struct vtmmio_softc *sc;

	sc = device_get_softc(dev);

	vtmmio_write_config_4(sc, VIRTIO_MMIO_QUEUE_NOTIFY, queue);
}

static uint8_t
vtmmio_get_status(device_t dev)
{
	struct vtmmio_softc *sc;

	sc = device_get_softc(dev);

	return (vtmmio_read_config_4(sc, VIRTIO_MMIO_STATUS));
}

static void
vtmmio_set_status(device_t dev, uint8_t status)
{
	struct vtmmio_softc *sc;

	sc = device_get_softc(dev);

	if (status != VIRTIO_CONFIG_STATUS_RESET)
		status |= vtmmio_get_status(dev);

	vtmmio_write_config_4(sc, VIRTIO_MMIO_STATUS, status);
}

static void
vtmmio_read_dev_config(device_t dev, bus_size_t offset,
    void *dst, int length)
{
	struct vtmmio_softc *sc;
	bus_size_t off;
	uint8_t *d;
	int size;

	sc = device_get_softc(dev);
	off = VIRTIO_MMIO_CONFIG + offset;

	for (d = dst; length > 0; d += size, off += size, length -= size) {
#ifdef ALLOW_WORD_ALIGNED_ACCESS
		if (length >= 4) {
			size = 4;
			*(uint32_t *)d = vtmmio_read_config_4(sc, off);
		} else if (length >= 2) {
			size = 2;
			*(uint16_t *)d = vtmmio_read_config_2(sc, off);
		} else
#endif
		{
			size = 1;
			*d = vtmmio_read_config_1(sc, off);
		}
	}
}

static void
vtmmio_write_dev_config(device_t dev, bus_size_t offset,
    void *src, int length)
{
	struct vtmmio_softc *sc;
	bus_size_t off;
	uint8_t *s;
	int size;

	sc = device_get_softc(dev);
	off = VIRTIO_MMIO_CONFIG + offset;

	for (s = src; length > 0; s += size, off += size, length -= size) {
#ifdef ALLOW_WORD_ALIGNED_ACCESS
		if (length >= 4) {
			size = 4;
			vtmmio_write_config_4(sc, off, *(uint32_t *)s);
		} else if (length >= 2) {
			size = 2;
			vtmmio_write_config_2(sc, off, *(uint16_t *)s);
		} else
#endif
		{
			size = 1;
			vtmmio_write_config_1(sc, off, *s);
		}
	}
}

static void
vtmmio_describe_features(struct vtmmio_softc *sc, const char *msg,
    uint64_t features)
{
	device_t dev, child;

	dev = sc->dev;
	child = sc->vtmmio_child_dev;

	if (device_is_attached(child) || bootverbose == 0)
		return;

	virtio_describe(dev, msg, features, sc->vtmmio_child_feat_desc);
}

static void
vtmmio_probe_and_attach_child(struct vtmmio_softc *sc)
{
	device_t dev, child;

	dev = sc->dev;
	child = sc->vtmmio_child_dev;

	if (child == NULL)
		return;

	if (device_get_state(child) != DS_NOTPRESENT) {
		return;
	}

	if (device_probe(child) != 0) {
		return;
	}

	vtmmio_set_status(dev, VIRTIO_CONFIG_STATUS_DRIVER);
	if (device_attach(child) != 0) {
		vtmmio_set_status(dev, VIRTIO_CONFIG_STATUS_FAILED);
		vtmmio_reset(sc);
		vtmmio_release_child_resources(sc);
		/* Reset status for future attempt. */
		vtmmio_set_status(dev, VIRTIO_CONFIG_STATUS_ACK);
	} else {
		vtmmio_set_status(dev, VIRTIO_CONFIG_STATUS_DRIVER_OK);
		VIRTIO_ATTACH_COMPLETED(child);
	}
}

static int
vtmmio_reinit_virtqueue(struct vtmmio_softc *sc, int idx)
{
	struct vtmmio_virtqueue *vqx;
	struct virtqueue *vq;
	int error;
	uint16_t size;

	vqx = &sc->vtmmio_vqs[idx];
	vq = vqx->vtv_vq;

	KASSERT(vq != NULL, ("%s: vq %d not allocated", __func__, idx));

	vtmmio_select_virtqueue(sc, idx);
	size = vtmmio_read_config_4(sc, VIRTIO_MMIO_QUEUE_NUM_MAX);

	error = virtqueue_reinit(vq, size);
	if (error)
		return (error);

	vtmmio_write_config_4(sc, VIRTIO_MMIO_QUEUE_NUM, size);
	vtmmio_write_config_4(sc, VIRTIO_MMIO_QUEUE_ALIGN,
	    VIRTIO_MMIO_VRING_ALIGN);
#if 0
	device_printf(sc->dev, "virtqueue paddr 0x%08lx\n",
	    (uint64_t)virtqueue_paddr(vq));
#endif
	vtmmio_write_config_4(sc, VIRTIO_MMIO_QUEUE_PFN,
	    virtqueue_paddr(vq) >> PAGE_SHIFT);

	return (0);
}

static void
vtmmio_free_interrupts(struct vtmmio_softc *sc)
{

	if (sc->ih != NULL)
		bus_teardown_intr(sc->dev, sc->res[1], sc->ih);

	if (sc->res[1] != NULL)
		bus_release_resource(sc->dev, SYS_RES_IRQ, 0, sc->res[1]);
}

static void
vtmmio_free_virtqueues(struct vtmmio_softc *sc)
{
	struct vtmmio_virtqueue *vqx;
	int idx;

	for (idx = 0; idx < sc->vtmmio_nvqs; idx++) {
		vqx = &sc->vtmmio_vqs[idx];

		vtmmio_select_virtqueue(sc, idx);
		vtmmio_write_config_4(sc, VIRTIO_MMIO_QUEUE_PFN, 0);

		virtqueue_free(vqx->vtv_vq);
		vqx->vtv_vq = NULL;
	}

	free(sc->vtmmio_vqs, M_DEVBUF);
	sc->vtmmio_vqs = NULL;
	sc->vtmmio_nvqs = 0;
}

static void
vtmmio_release_child_resources(struct vtmmio_softc *sc)
{

	vtmmio_free_interrupts(sc);
	vtmmio_free_virtqueues(sc);
}

static void
vtmmio_reset(struct vtmmio_softc *sc)
{

	/*
	 * Setting the status to RESET sets the host device to
	 * the original, uninitialized state.
	 */
	vtmmio_set_status(sc->dev, VIRTIO_CONFIG_STATUS_RESET);
}

static void
vtmmio_select_virtqueue(struct vtmmio_softc *sc, int idx)
{

	vtmmio_write_config_4(sc, VIRTIO_MMIO_QUEUE_SEL, idx);
}

static void
vtmmio_vq_intr(void *arg)
{
	struct vtmmio_virtqueue *vqx;
	struct vtmmio_softc *sc;
	struct virtqueue *vq;
	uint32_t status;
	int idx;

	sc = arg;

	status = vtmmio_read_config_4(sc, VIRTIO_MMIO_INTERRUPT_STATUS);
	vtmmio_write_config_4(sc, VIRTIO_MMIO_INTERRUPT_ACK, status);

	/* The config changed */
	if (status & VIRTIO_MMIO_INT_CONFIG)
		if (sc->vtmmio_child_dev != NULL)
			VIRTIO_CONFIG_CHANGE(sc->vtmmio_child_dev);

	/* Notify all virtqueues. */
	if (status & VIRTIO_MMIO_INT_VRING) {
		for (idx = 0; idx < sc->vtmmio_nvqs; idx++) {
			vqx = &sc->vtmmio_vqs[idx];
			if (vqx->vtv_no_intr == 0) {
				vq = vqx->vtv_vq;
				virtqueue_intr(vq);
			}
		}
	}
}
