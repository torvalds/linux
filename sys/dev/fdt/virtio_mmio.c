/*	$OpenBSD: virtio_mmio.c,v 1.23 2025/01/14 14:28:38 sf Exp $	*/
/*	$NetBSD: virtio.c,v 1.3 2011/11/02 23:05:52 njoly Exp $	*/

/*
 * Copyright (c) 2014 Patrick Wildt <patrick@blueri.se>
 * Copyright (c) 2012 Stefan Fritsch.
 * Copyright (c) 2010 Minoura Makoto.
 * All rights reserved.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/mutex.h>

#include <dev/pv/virtioreg.h>
#include <dev/pv/virtiovar.h>

#include <machine/fdt.h>

#include <dev/ofw/fdt.h>
#include <dev/ofw/openfirm.h>

#define VIRTIO_MMIO_MAGIC		('v' | 'i' << 8 | 'r' << 16 | 't' << 24)

#define VIRTIO_MMIO_MAGIC_VALUE		0x000
#define VIRTIO_MMIO_VERSION		0x004
#define VIRTIO_MMIO_DEVICE_ID		0x008
#define VIRTIO_MMIO_VENDOR_ID		0x00c
#define VIRTIO_MMIO_HOST_FEATURES	0x010
#define VIRTIO_MMIO_HOST_FEATURES_SEL	0x014
#define VIRTIO_MMIO_GUEST_FEATURES	0x020
#define VIRTIO_MMIO_GUEST_FEATURES_SEL	0x024
#define VIRTIO_MMIO_GUEST_PAGE_SIZE	0x028
#define VIRTIO_MMIO_QUEUE_SEL		0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX	0x034
#define VIRTIO_MMIO_QUEUE_NUM		0x038
#define VIRTIO_MMIO_QUEUE_ALIGN		0x03c
#define VIRTIO_MMIO_QUEUE_PFN		0x040
#define VIRTIO_MMIO_QUEUE_READY		0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY	0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS	0x060
#define VIRTIO_MMIO_INTERRUPT_ACK	0x064
#define VIRTIO_MMIO_STATUS		0x070
#define VIRTIO_MMIO_QUEUE_DESC_LOW	0x080
#define VIRTIO_MMIO_QUEUE_DESC_HIGH	0x084
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW	0x090
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH	0x094
#define VIRTIO_MMIO_QUEUE_USED_LOW	0x0a0
#define VIRTIO_MMIO_QUEUE_USED_HIGH	0x0a4
#define VIRTIO_MMIO_CONFIG		0x100

#define VIRTIO_MMIO_INT_VRING		(1 << 0)
#define VIRTIO_MMIO_INT_CONFIG		(1 << 1)

#define DEVNAME(sc) (sc)->sc_dev.dv_xname

/*
 * XXX: Before being used on big endian arches, the access to config registers
 * XXX: needs to be reviewed/fixed. The non-device specific registers are
 * XXX: PCI-endian while the device specific registers are native endian.
 */

int		virtio_mmio_match(struct device *, void *, void *);
void		virtio_mmio_attach(struct device *, struct device *, void *);
int		virtio_mmio_detach(struct device *, int);

void		virtio_mmio_kick(struct virtio_softc *, uint16_t);
uint8_t		virtio_mmio_read_device_config_1(struct virtio_softc *, int);
uint16_t	virtio_mmio_read_device_config_2(struct virtio_softc *, int);
uint32_t	virtio_mmio_read_device_config_4(struct virtio_softc *, int);
uint64_t	virtio_mmio_read_device_config_8(struct virtio_softc *, int);
void		virtio_mmio_write_device_config_1(struct virtio_softc *, int, uint8_t);
void		virtio_mmio_write_device_config_2(struct virtio_softc *, int, uint16_t);
void		virtio_mmio_write_device_config_4(struct virtio_softc *, int, uint32_t);
void		virtio_mmio_write_device_config_8(struct virtio_softc *, int, uint64_t);
uint16_t	virtio_mmio_read_queue_size(struct virtio_softc *, uint16_t);
void		virtio_mmio_setup_queue(struct virtio_softc *, struct virtqueue *, uint64_t);
void		virtio_mmio_setup_intrs(struct virtio_softc *);
int		virtio_mmio_attach_finish(struct virtio_softc *, struct virtio_attach_args *);
int		virtio_mmio_get_status(struct virtio_softc *);
void		virtio_mmio_set_status(struct virtio_softc *, int);
int		virtio_mmio_negotiate_features(struct virtio_softc *,
    const struct virtio_feature_name *);
int		virtio_mmio_intr(void *);
void		virtio_mmio_intr_barrier(struct virtio_softc *);
int		virtio_mmio_intr_establish(struct virtio_softc *, struct virtio_attach_args *,
    int, struct cpu_info *, int (*)(void *), void *);

struct virtio_mmio_softc {
	struct virtio_softc	sc_sc;

	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_size_t		sc_iosize;
	bus_dma_tag_t		sc_dmat;

	void			*sc_ih;

	int			sc_config_offset;
	uint32_t		sc_version;
};

struct virtio_mmio_attach_args {
	struct virtio_attach_args	 vma_va;
	struct fdt_attach_args		*vma_fa;
};

const struct cfattach virtio_mmio_ca = {
	sizeof(struct virtio_mmio_softc),
	virtio_mmio_match,
	virtio_mmio_attach,
	virtio_mmio_detach,
	NULL
};

const struct cfattach virtio_mmio_fdt_ca = {
	sizeof(struct virtio_mmio_softc),
	NULL,
	virtio_mmio_attach,
	virtio_mmio_detach,
	NULL
};

const struct virtio_ops virtio_mmio_ops = {
	virtio_mmio_kick,
	virtio_mmio_read_device_config_1,
	virtio_mmio_read_device_config_2,
	virtio_mmio_read_device_config_4,
	virtio_mmio_read_device_config_8,
	virtio_mmio_write_device_config_1,
	virtio_mmio_write_device_config_2,
	virtio_mmio_write_device_config_4,
	virtio_mmio_write_device_config_8,
	virtio_mmio_read_queue_size,
	virtio_mmio_setup_queue,
	virtio_mmio_setup_intrs,
	virtio_mmio_get_status,
	virtio_mmio_set_status,
	virtio_mmio_negotiate_features,
	virtio_mmio_attach_finish,
	virtio_mmio_intr,
	virtio_mmio_intr_barrier,
	virtio_mmio_intr_establish,
};

uint16_t
virtio_mmio_read_queue_size(struct virtio_softc *vsc, uint16_t idx)
{
	struct virtio_mmio_softc *sc = (struct virtio_mmio_softc *)vsc;
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, VIRTIO_MMIO_QUEUE_SEL, idx);
	return bus_space_read_4(sc->sc_iot, sc->sc_ioh,
	    VIRTIO_MMIO_QUEUE_NUM_MAX);
}

void
virtio_mmio_setup_queue(struct virtio_softc *vsc, struct virtqueue *vq,
    uint64_t addr)
{
	struct virtio_mmio_softc *sc = (struct virtio_mmio_softc *)vsc;
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, VIRTIO_MMIO_QUEUE_SEL,
	    vq->vq_index);
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, VIRTIO_MMIO_QUEUE_NUM,
	    bus_space_read_4(sc->sc_iot, sc->sc_ioh, VIRTIO_MMIO_QUEUE_NUM_MAX));
	if (sc->sc_version == 1) {
		bus_space_write_4(sc->sc_iot, sc->sc_ioh,
		    VIRTIO_MMIO_QUEUE_ALIGN, PAGE_SIZE);
		bus_space_write_4(sc->sc_iot, sc->sc_ioh,
		    VIRTIO_MMIO_QUEUE_PFN, addr / VIRTIO_PAGE_SIZE);
	} else {
		bus_space_write_4(sc->sc_iot, sc->sc_ioh,
		    VIRTIO_MMIO_QUEUE_DESC_LOW, addr);
		bus_space_write_4(sc->sc_iot, sc->sc_ioh,
		    VIRTIO_MMIO_QUEUE_DESC_HIGH, addr >> 32);
		bus_space_write_4(sc->sc_iot, sc->sc_ioh,
		    VIRTIO_MMIO_QUEUE_AVAIL_LOW,
		    addr + vq->vq_availoffset);
		bus_space_write_4(sc->sc_iot, sc->sc_ioh,
		    VIRTIO_MMIO_QUEUE_AVAIL_HIGH,
		    (addr + vq->vq_availoffset) >> 32);
		bus_space_write_4(sc->sc_iot, sc->sc_ioh,
		    VIRTIO_MMIO_QUEUE_USED_LOW,
		    addr + vq->vq_usedoffset);
		bus_space_write_4(sc->sc_iot, sc->sc_ioh,
		    VIRTIO_MMIO_QUEUE_USED_HIGH,
		    (addr + vq->vq_usedoffset) >> 32);
		bus_space_write_4(sc->sc_iot, sc->sc_ioh,
		    VIRTIO_MMIO_QUEUE_READY, 1);
	}
}

void
virtio_mmio_setup_intrs(struct virtio_softc *vsc)
{
}

int
virtio_mmio_get_status(struct virtio_softc *vsc)
{
	struct virtio_mmio_softc *sc = (struct virtio_mmio_softc *)vsc;

	return bus_space_read_4(sc->sc_iot, sc->sc_ioh,
	    VIRTIO_MMIO_STATUS);
}

void
virtio_mmio_set_status(struct virtio_softc *vsc, int status)
{
	struct virtio_mmio_softc *sc = (struct virtio_mmio_softc *)vsc;
	int old = 0;

	if (status == 0) {
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, VIRTIO_MMIO_STATUS,
		    0);
		while (bus_space_read_4(sc->sc_iot, sc->sc_ioh,
		    VIRTIO_MMIO_STATUS) != 0) {
			CPU_BUSY_CYCLE();
		}
	} else {
		old = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
		    VIRTIO_MMIO_STATUS);
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, VIRTIO_MMIO_STATUS,
		    status|old);
	}
}

int
virtio_mmio_match(struct device *parent, void *cfdata, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "virtio,mmio");
}

void
virtio_mmio_attach(struct device *parent, struct device *self, void *aux)
{
	struct fdt_attach_args *faa = aux;
	struct virtio_mmio_softc *sc = (struct virtio_mmio_softc *)self;
	struct virtio_softc *vsc = &sc->sc_sc;
	uint32_t id, magic;
	struct virtio_mmio_attach_args vma = { { 0 }, faa };

	if (faa->fa_nreg < 1) {
		printf(": no register data\n");
		return;
	}

	sc->sc_iosize = faa->fa_reg[0].size;
	sc->sc_iot = faa->fa_iot;
	sc->sc_dmat = faa->fa_dmat;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr, faa->fa_reg[0].size,
	    0, &sc->sc_ioh))
		panic("%s: bus_space_map failed!", __func__);

	magic = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
	    VIRTIO_MMIO_MAGIC_VALUE);
	if (magic != VIRTIO_MMIO_MAGIC) {
		printf(": wrong magic value 0x%08x; giving up\n", magic);
		return;
	}

	sc->sc_version = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
	    VIRTIO_MMIO_VERSION);
	if (sc->sc_version < 1 || sc->sc_version > 2) {
		printf(": unknown version 0x%02x; giving up\n", sc->sc_version);
		return;
	}

	id = bus_space_read_4(sc->sc_iot, sc->sc_ioh, VIRTIO_MMIO_DEVICE_ID);
	printf(": Virtio %s Device", virtio_device_string(id));

	printf("\n");

	/* No device connected. */
	if (id == 0)
		return;

	if (sc->sc_version == 1)
		bus_space_write_4(sc->sc_iot, sc->sc_ioh,
		    VIRTIO_MMIO_GUEST_PAGE_SIZE, PAGE_SIZE);

	vsc->sc_ops = &virtio_mmio_ops;
	vsc->sc_dmat = sc->sc_dmat;
	sc->sc_config_offset = VIRTIO_MMIO_CONFIG;

	virtio_device_reset(vsc);
	virtio_mmio_set_status(vsc, VIRTIO_CONFIG_DEVICE_STATUS_ACK);
	virtio_mmio_set_status(vsc, VIRTIO_CONFIG_DEVICE_STATUS_DRIVER);

	vma.vma_va.va_devid = id;
	vma.vma_va.va_nintr = 1;
	vsc->sc_child = NULL;
	config_found(self, &vma, NULL);
	if (vsc->sc_child == NULL) {
		printf("%s: no matching child driver; not configured\n",
		    vsc->sc_dev.dv_xname);
		goto fail;
	}
	if (vsc->sc_child == VIRTIO_CHILD_ERROR) {
		printf("%s: virtio configuration failed\n",
		    vsc->sc_dev.dv_xname);
		goto fail;
	}

	return;

fail:
	virtio_set_status(vsc, VIRTIO_CONFIG_DEVICE_STATUS_FAILED);
}

int
virtio_mmio_attach_finish(struct virtio_softc *vsc,
    struct virtio_attach_args *va)
{
	struct virtio_mmio_softc *sc = (struct virtio_mmio_softc *)vsc;
	struct virtio_mmio_attach_args *vma =
	    (struct virtio_mmio_attach_args *)va;

	sc->sc_ih = fdt_intr_establish(vma->vma_fa->fa_node, vsc->sc_ipl,
	    virtio_mmio_intr, sc, vsc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf("%s: couldn't establish interrupt\n",
		    vsc->sc_dev.dv_xname);
		return -EIO;
	}
	return 0;
}

int
virtio_mmio_detach(struct device *self, int flags)
{
	struct virtio_mmio_softc *sc = (struct virtio_mmio_softc *)self;
	struct virtio_softc *vsc = &sc->sc_sc;
	int r;

	if (vsc->sc_child != 0 && vsc->sc_child != VIRTIO_CHILD_ERROR) {
		r = config_detach(vsc->sc_child, flags);
		if (r)
			return r;
	}
	KASSERT(vsc->sc_child == 0 || vsc->sc_child == VIRTIO_CHILD_ERROR);
	KASSERT(vsc->sc_vqs == 0);
	fdt_intr_disestablish(sc->sc_ih);
	sc->sc_ih = 0;
	if (sc->sc_iosize)
		bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_iosize);
	sc->sc_iosize = 0;

	return 0;
}

/*
 * Feature negotiation.
 * Prints available / negotiated features if guest_feature_names != NULL and
 * VIRTIO_DEBUG is 1
 */
int
virtio_mmio_negotiate_features(struct virtio_softc *vsc,
    const struct virtio_feature_name *guest_feature_names)
{
	struct virtio_mmio_softc *sc = (struct virtio_mmio_softc *)vsc;
	uint64_t host, neg;

	vsc->sc_active_features = 0;

	/*
	 * We enable indirect descriptors by default. They can be switched
	 * off by setting bit 1 in the driver flags, see config(8).
	 */
	if (!(vsc->sc_dev.dv_cfdata->cf_flags & VIRTIO_CF_NO_INDIRECT) &&
	    !(vsc->sc_child->dv_cfdata->cf_flags & VIRTIO_CF_NO_INDIRECT)) {
		vsc->sc_driver_features |= VIRTIO_F_RING_INDIRECT_DESC;
	} else if (guest_feature_names != NULL) {
		printf("RingIndirectDesc disabled by UKC\n");
	}
	/*
	 * The driver must add VIRTIO_F_RING_EVENT_IDX if it supports it.
	 * If it did, check if it is disabled by bit 2 in the driver flags.
	 */
	if ((vsc->sc_driver_features & VIRTIO_F_RING_EVENT_IDX) &&
	    ((vsc->sc_dev.dv_cfdata->cf_flags & VIRTIO_CF_NO_EVENT_IDX) ||
	    (vsc->sc_child->dv_cfdata->cf_flags & VIRTIO_CF_NO_EVENT_IDX))) {
		if (guest_feature_names != NULL)
			printf(" RingEventIdx disabled by UKC");
		vsc->sc_driver_features &= ~(VIRTIO_F_RING_EVENT_IDX);
	}

	bus_space_write_4(sc->sc_iot, sc->sc_ioh,
	    VIRTIO_MMIO_HOST_FEATURES_SEL, 0);
	host = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
				VIRTIO_MMIO_HOST_FEATURES);
	neg = host & vsc->sc_driver_features;
#if VIRTIO_DEBUG
	if (guest_feature_names)
		virtio_log_features(host, neg, guest_feature_names);
#endif
	bus_space_write_4(sc->sc_iot, sc->sc_ioh,
	    VIRTIO_MMIO_GUEST_FEATURES_SEL, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_ioh,
			  VIRTIO_MMIO_GUEST_FEATURES, neg);
	vsc->sc_active_features = neg;
	if (neg & VIRTIO_F_RING_INDIRECT_DESC)
		vsc->sc_indirect = 1;
	else
		vsc->sc_indirect = 0;

	return 0;
}

/*
 * Device configuration registers.
 */
uint8_t
virtio_mmio_read_device_config_1(struct virtio_softc *vsc, int index)
{
	struct virtio_mmio_softc *sc = (struct virtio_mmio_softc *)vsc;
	return bus_space_read_1(sc->sc_iot, sc->sc_ioh,
				sc->sc_config_offset + index);
}

uint16_t
virtio_mmio_read_device_config_2(struct virtio_softc *vsc, int index)
{
	struct virtio_mmio_softc *sc = (struct virtio_mmio_softc *)vsc;
	return bus_space_read_2(sc->sc_iot, sc->sc_ioh,
				sc->sc_config_offset + index);
}

uint32_t
virtio_mmio_read_device_config_4(struct virtio_softc *vsc, int index)
{
	struct virtio_mmio_softc *sc = (struct virtio_mmio_softc *)vsc;
	return bus_space_read_4(sc->sc_iot, sc->sc_ioh,
				sc->sc_config_offset + index);
}

uint64_t
virtio_mmio_read_device_config_8(struct virtio_softc *vsc, int index)
{
	struct virtio_mmio_softc *sc = (struct virtio_mmio_softc *)vsc;
	uint64_t r;

	r = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
			     sc->sc_config_offset + index + sizeof(uint32_t));
	r <<= 32;
	r += bus_space_read_4(sc->sc_iot, sc->sc_ioh,
			      sc->sc_config_offset + index);
	return r;
}

void
virtio_mmio_write_device_config_1(struct virtio_softc *vsc,
			     int index, uint8_t value)
{
	struct virtio_mmio_softc *sc = (struct virtio_mmio_softc *)vsc;
	bus_space_write_1(sc->sc_iot, sc->sc_ioh,
			  sc->sc_config_offset + index, value);
}

void
virtio_mmio_write_device_config_2(struct virtio_softc *vsc,
			     int index, uint16_t value)
{
	struct virtio_mmio_softc *sc = (struct virtio_mmio_softc *)vsc;
	bus_space_write_2(sc->sc_iot, sc->sc_ioh,
			  sc->sc_config_offset + index, value);
}

void
virtio_mmio_write_device_config_4(struct virtio_softc *vsc,
			     int index, uint32_t value)
{
	struct virtio_mmio_softc *sc = (struct virtio_mmio_softc *)vsc;
	bus_space_write_4(sc->sc_iot, sc->sc_ioh,
			  sc->sc_config_offset + index, value);
}

void
virtio_mmio_write_device_config_8(struct virtio_softc *vsc,
			     int index, uint64_t value)
{
	struct virtio_mmio_softc *sc = (struct virtio_mmio_softc *)vsc;
	bus_space_write_4(sc->sc_iot, sc->sc_ioh,
			  sc->sc_config_offset + index,
			  value & 0xffffffff);
	bus_space_write_4(sc->sc_iot, sc->sc_ioh,
			  sc->sc_config_offset + index + sizeof(uint32_t),
			  value >> 32);
}

/*
 * Interrupt handler.
 */
int
virtio_mmio_intr(void *arg)
{
	struct virtio_mmio_softc *sc = arg;
	struct virtio_softc *vsc = &sc->sc_sc;
	int isr, r = 0;

	/* check and ack the interrupt */
	isr = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
			       VIRTIO_MMIO_INTERRUPT_STATUS);
	bus_space_write_4(sc->sc_iot, sc->sc_ioh,
			  VIRTIO_MMIO_INTERRUPT_ACK, isr);
	if ((isr & VIRTIO_MMIO_INT_CONFIG) &&
	    (vsc->sc_config_change != NULL))
		r = (vsc->sc_config_change)(vsc);
	if ((isr & VIRTIO_MMIO_INT_VRING))
		r |= virtio_check_vqs(vsc);

	return r;
}

void
virtio_mmio_kick(struct virtio_softc *vsc, uint16_t idx)
{
	struct virtio_mmio_softc *sc = (struct virtio_mmio_softc *)vsc;
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, VIRTIO_MMIO_QUEUE_NOTIFY,
	    idx);
}

void
virtio_mmio_intr_barrier(struct virtio_softc *vsc)
{
	struct virtio_mmio_softc *sc = (struct virtio_mmio_softc *)vsc;
	if (sc->sc_ih)
		intr_barrier(sc->sc_ih);
}

int
virtio_mmio_intr_establish(struct virtio_softc *vsc,
    struct virtio_attach_args *va, int vec, struct cpu_info *ci,
    int (*func)(void *), void *arg)
{
	return ENXIO;
}
