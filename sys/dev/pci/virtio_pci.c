/*	$OpenBSD: virtio_pci.c,v 1.52 2025/08/05 09:48:44 sf Exp $	*/
/*	$NetBSD: virtio.c,v 1.3 2011/11/02 23:05:52 njoly Exp $	*/

/*
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
#include <sys/device.h>
#include <sys/mutex.h>

#include <dev/pci/pcidevs.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/virtio_pcireg.h>

#include <dev/pv/virtioreg.h>
#include <dev/pv/virtiovar.h>

#define DNPRINTF(n,x...)				\
    do { if (VIRTIO_DEBUG >= n) printf(x); } while(0)


/*
 * XXX: Before being used on big endian arches, the access to config registers
 * XXX: needs to be reviewed/fixed. The non-device specific registers are
 * XXX: PCI-endian while the device specific registers are native endian.
 */

#define MAX_MSIX_VECS	16

struct virtio_pci_softc;
struct virtio_pci_attach_args;

int		virtio_pci_match(struct device *, void *, void *);
void		virtio_pci_attach(struct device *, struct device *, void *);
int		virtio_pci_attach_09(struct virtio_pci_softc *sc, struct pci_attach_args *pa);
int		virtio_pci_attach_10(struct virtio_pci_softc *sc, struct pci_attach_args *pa);
int		virtio_pci_detach(struct device *, int);

void		virtio_pci_kick(struct virtio_softc *, uint16_t);
int		virtio_pci_adjust_config_region(struct virtio_pci_softc *, int offset);
uint8_t		virtio_pci_read_device_config_1(struct virtio_softc *, int);
uint16_t	virtio_pci_read_device_config_2(struct virtio_softc *, int);
uint32_t	virtio_pci_read_device_config_4(struct virtio_softc *, int);
uint64_t	virtio_pci_read_device_config_8(struct virtio_softc *, int);
void		virtio_pci_write_device_config_1(struct virtio_softc *, int, uint8_t);
void		virtio_pci_write_device_config_2(struct virtio_softc *, int, uint16_t);
void		virtio_pci_write_device_config_4(struct virtio_softc *, int, uint32_t);
void		virtio_pci_write_device_config_8(struct virtio_softc *, int, uint64_t);
uint16_t	virtio_pci_read_queue_size(struct virtio_softc *, uint16_t);
void		virtio_pci_setup_queue(struct virtio_softc *, struct virtqueue *, uint64_t);
void		virtio_pci_setup_intrs(struct virtio_softc *);
int		virtio_pci_attach_finish(struct virtio_softc *, struct virtio_attach_args *);
int		virtio_pci_get_status(struct virtio_softc *);
void		virtio_pci_set_status(struct virtio_softc *, int);
int		virtio_pci_negotiate_features(struct virtio_softc *, const struct virtio_feature_name *);
int		virtio_pci_negotiate_features_10(struct virtio_softc *, const struct virtio_feature_name *);
void		virtio_pci_set_msix_queue_vector(struct virtio_pci_softc *, uint32_t, uint16_t);
void		virtio_pci_set_msix_config_vector(struct virtio_pci_softc *, uint16_t);
int		virtio_pci_msix_establish(struct virtio_pci_softc *, struct virtio_pci_attach_args *, int, struct cpu_info *, int (*)(void *), void *);
int		virtio_pci_setup_msix(struct virtio_pci_softc *, struct virtio_pci_attach_args *, int);
void		virtio_pci_intr_barrier(struct virtio_softc *);
int		virtio_pci_intr_establish(struct virtio_softc *, struct virtio_attach_args *, int, struct cpu_info *, int (*)(void *), void *);
void		virtio_pci_free_irqs(struct virtio_pci_softc *);
int		virtio_pci_poll_intr(void *);
int		virtio_pci_legacy_intr(void *);
int		virtio_pci_legacy_intr_mpsafe(void *);
int		virtio_pci_config_intr(void *);
int		virtio_pci_queue_intr(void *);
int		virtio_pci_shared_queue_intr(void *);
int		virtio_pci_find_cap(struct virtio_pci_softc *sc, int cfg_type, void *buf, int buflen);
#if VIRTIO_DEBUG
void virtio_pci_dump_caps(struct virtio_pci_softc *sc);
#endif

enum irq_type {
	IRQ_NO_MSIX,
	IRQ_MSIX_SHARED, /* vec 0: config irq, vec 1 shared by all vqs */
	IRQ_MSIX_PER_VQ, /* vec 0: config irq, vec n: irq of vq[n-1] */
	IRQ_MSIX_CHILD,  /* assigned by child driver */
};

struct virtio_pci_intr {
	char	 name[16];
	void	*ih;
};

struct virtio_pci_softc {
	struct virtio_softc	sc_sc;
	pci_chipset_tag_t	sc_pc;
	pcitag_t		sc_ptag;

	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_size_t		sc_iosize;

	bus_space_tag_t		sc_bars_iot[4];
	bus_space_handle_t	sc_bars_ioh[4];
	bus_size_t		sc_bars_iosize[4];

	bus_space_tag_t		sc_notify_iot;
	bus_space_handle_t	sc_notify_ioh;
	bus_size_t		sc_notify_iosize;
	unsigned int		sc_notify_off_multiplier;

	bus_space_tag_t		sc_devcfg_iot;
	bus_space_handle_t	sc_devcfg_ioh;
	bus_size_t		sc_devcfg_iosize;
	/*
	 * With 0.9, the offset of the devcfg region in the io bar changes
	 * depending on MSI-X being enabled or not.
	 * With 1.0, this field is still used to remember if MSI-X is enabled
	 * or not.
	 */
	unsigned int		sc_devcfg_offset;

	bus_space_tag_t		sc_isr_iot;
	bus_space_handle_t	sc_isr_ioh;
	bus_size_t		sc_isr_iosize;

	struct virtio_pci_intr	*sc_intr;
	int			sc_nintr;

	enum irq_type		sc_irq_type;
};

struct virtio_pci_attach_args {
	struct virtio_attach_args	 vpa_va;
	struct pci_attach_args		*vpa_pa;
	int				 vpa_msix;
};


const struct cfattach virtio_pci_ca = {
	sizeof(struct virtio_pci_softc),
	virtio_pci_match,
	virtio_pci_attach,
	virtio_pci_detach,
	NULL
};

const struct virtio_ops virtio_pci_ops = {
	virtio_pci_kick,
	virtio_pci_read_device_config_1,
	virtio_pci_read_device_config_2,
	virtio_pci_read_device_config_4,
	virtio_pci_read_device_config_8,
	virtio_pci_write_device_config_1,
	virtio_pci_write_device_config_2,
	virtio_pci_write_device_config_4,
	virtio_pci_write_device_config_8,
	virtio_pci_read_queue_size,
	virtio_pci_setup_queue,
	virtio_pci_setup_intrs,
	virtio_pci_get_status,
	virtio_pci_set_status,
	virtio_pci_negotiate_features,
	virtio_pci_attach_finish,
	virtio_pci_poll_intr,
	virtio_pci_intr_barrier,
	virtio_pci_intr_establish,
};

static inline uint64_t
_cread(struct virtio_pci_softc *sc, unsigned off, unsigned size)
{
	uint64_t val;
	switch (size) {
	case 1:
		val = bus_space_read_1(sc->sc_iot, sc->sc_ioh, off);
		break;
	case 2:
		val = bus_space_read_2(sc->sc_iot, sc->sc_ioh, off);
		break;
	case 4:
		val = bus_space_read_4(sc->sc_iot, sc->sc_ioh, off);
		break;
	case 8:
		val = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
		    off + sizeof(uint32_t));
		val <<= 32;
		val += bus_space_read_4(sc->sc_iot, sc->sc_ioh, off);
		break;
	}
	return val;
}

#define CREAD(sc, memb)  _cread(sc, offsetof(struct virtio_pci_common_cfg, memb), \
    sizeof(((struct virtio_pci_common_cfg *)0)->memb))

#define CWRITE(sc, memb, val)							\
	do {									\
		struct virtio_pci_common_cfg c;					\
		size_t off = offsetof(struct virtio_pci_common_cfg, memb);	\
		size_t size = sizeof(c.memb);					\
										\
		DNPRINTF(2, "%s: %d: off %#zx size %#zx write %#llx\n",		\
		    __func__, __LINE__, off, size, (unsigned long long)val);	\
		switch (size) {							\
		case 1:								\
			bus_space_write_1(sc->sc_iot, sc->sc_ioh, off, val);	\
			break;							\
		case 2:								\
			bus_space_write_2(sc->sc_iot, sc->sc_ioh, off, val);	\
			break;							\
		case 4:								\
			bus_space_write_4(sc->sc_iot, sc->sc_ioh, off, val);	\
			break;							\
		case 8:								\
			bus_space_write_4(sc->sc_iot, sc->sc_ioh, off,		\
			    (val) & 0xffffffff);				\
			bus_space_write_4(sc->sc_iot, sc->sc_ioh,		\
			    (off) + sizeof(uint32_t), (uint64_t)(val) >> 32);	\
			break;							\
		}								\
	} while (0)

uint16_t
virtio_pci_read_queue_size(struct virtio_softc *vsc, uint16_t idx)
{
	struct virtio_pci_softc *sc = (struct virtio_pci_softc *)vsc;
	uint16_t ret;
	if (sc->sc_sc.sc_version_1) {
		CWRITE(sc, queue_select, idx);
		ret = CREAD(sc, queue_size);
	} else {
		bus_space_write_2(sc->sc_iot, sc->sc_ioh,
		    VIRTIO_CONFIG_QUEUE_SELECT, idx);
		ret = bus_space_read_2(sc->sc_iot, sc->sc_ioh,
		    VIRTIO_CONFIG_QUEUE_SIZE);
	}
	return ret;
}

void
virtio_pci_setup_queue(struct virtio_softc *vsc, struct virtqueue *vq,
    uint64_t addr)
{
	struct virtio_pci_softc *sc = (struct virtio_pci_softc *)vsc;
	if (sc->sc_sc.sc_version_1) {
		CWRITE(sc, queue_select, vq->vq_index);
		if (addr == 0) {
			CWRITE(sc, queue_enable, 0);
			CWRITE(sc, queue_desc, 0);
			CWRITE(sc, queue_avail, 0);
			CWRITE(sc, queue_used, 0);
		} else {
			CWRITE(sc, queue_desc, addr);
			CWRITE(sc, queue_avail, addr + vq->vq_availoffset);
			CWRITE(sc, queue_used, addr + vq->vq_usedoffset);
			CWRITE(sc, queue_enable, 1);
			vq->vq_notify_off = CREAD(sc, queue_notify_off);
		}
	} else {
		bus_space_write_2(sc->sc_iot, sc->sc_ioh,
		    VIRTIO_CONFIG_QUEUE_SELECT, vq->vq_index);
		bus_space_write_4(sc->sc_iot, sc->sc_ioh,
		    VIRTIO_CONFIG_QUEUE_ADDRESS, addr / VIRTIO_PAGE_SIZE);
	}
}

void
virtio_pci_setup_intrs(struct virtio_softc *vsc)
{
	struct virtio_pci_softc *sc = (struct virtio_pci_softc *)vsc;
	int i;

	if (sc->sc_irq_type == IRQ_NO_MSIX)
		return;

	for (i = 0; i < vsc->sc_nvqs; i++) {
		unsigned vec = vsc->sc_vqs[i].vq_intr_vec;
		virtio_pci_set_msix_queue_vector(sc, i, vec);
	}
	if (vsc->sc_config_change)
		virtio_pci_set_msix_config_vector(sc, 0);
}

int
virtio_pci_get_status(struct virtio_softc *vsc)
{
	struct virtio_pci_softc *sc = (struct virtio_pci_softc *)vsc;

	if (sc->sc_sc.sc_version_1)
		return CREAD(sc, device_status);
	else
		return bus_space_read_1(sc->sc_iot, sc->sc_ioh,
		    VIRTIO_CONFIG_DEVICE_STATUS);
}

void
virtio_pci_set_status(struct virtio_softc *vsc, int status)
{
	struct virtio_pci_softc *sc = (struct virtio_pci_softc *)vsc;
	int old = 0;

	if (sc->sc_sc.sc_version_1) {
		if (status == 0) {
			CWRITE(sc, device_status, 0);
			while (CREAD(sc, device_status) != 0) {
				CPU_BUSY_CYCLE();
			}
		} else {
			old = CREAD(sc, device_status);
			CWRITE(sc, device_status, status|old);
		}
	} else {
		if (status == 0) {
			bus_space_write_1(sc->sc_iot, sc->sc_ioh,
			    VIRTIO_CONFIG_DEVICE_STATUS, status|old);
			while (bus_space_read_1(sc->sc_iot, sc->sc_ioh,
			    VIRTIO_CONFIG_DEVICE_STATUS) != 0) {
				CPU_BUSY_CYCLE();
			}
		} else {
			old = bus_space_read_1(sc->sc_iot, sc->sc_ioh,
			    VIRTIO_CONFIG_DEVICE_STATUS);
			bus_space_write_1(sc->sc_iot, sc->sc_ioh,
			    VIRTIO_CONFIG_DEVICE_STATUS, status|old);
		}
	}
}

int
virtio_pci_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa;

	pa = (struct pci_attach_args *)aux;
	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_OPENBSD &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_OPENBSD_CONTROL)
		return 1;
	if (PCI_VENDOR(pa->pa_id) != PCI_VENDOR_QUMRANET)
		return 0;
	/* virtio 0.9 */
	if (PCI_PRODUCT(pa->pa_id) >= 0x1000 &&
	    PCI_PRODUCT(pa->pa_id) <= 0x103f &&
	    PCI_REVISION(pa->pa_class) == 0)
		return 1;
	/* virtio 1.0 */
	if (PCI_PRODUCT(pa->pa_id) >= 0x1040 &&
	    PCI_PRODUCT(pa->pa_id) <= 0x107f)
		return 1;
	return 0;
}

#if VIRTIO_DEBUG
void
virtio_pci_dump_caps(struct virtio_pci_softc *sc)
{
	pci_chipset_tag_t pc = sc->sc_pc;
	pcitag_t tag = sc->sc_ptag;
	int offset;
	union {
		pcireg_t reg[4];
		struct virtio_pci_cap vcap;
	} v;

	if (!pci_get_capability(pc, tag, PCI_CAP_VENDSPEC, &offset, &v.reg[0]))
		return;

	printf("\n");
	do {
		for (int i = 0; i < 4; i++)
			v.reg[i] = pci_conf_read(pc, tag, offset + i * 4);
		printf("%s: cfgoff %#x len %#x type %#x bar %#x: off %#x len %#x\n",
			__func__, offset, v.vcap.cap_len, v.vcap.cfg_type, v.vcap.bar,
			v.vcap.offset, v.vcap.length);
		offset = v.vcap.cap_next;
	} while (offset != 0);
}
#endif

int
virtio_pci_find_cap(struct virtio_pci_softc *sc, int cfg_type, void *buf, int buflen)
{
	pci_chipset_tag_t pc = sc->sc_pc;
	pcitag_t tag = sc->sc_ptag;
	unsigned int offset, i, len;
	union {
		pcireg_t reg[8];
		struct virtio_pci_cap vcap;
	} *v = buf;

	if (buflen < sizeof(struct virtio_pci_cap))
		return ERANGE;

	if (!pci_get_capability(pc, tag, PCI_CAP_VENDSPEC, &offset, &v->reg[0]))
		return ENOENT;

	do {
		for (i = 0; i < 4; i++)
			v->reg[i] = pci_conf_read(pc, tag, offset + i * 4);
		if (v->vcap.cfg_type == cfg_type)
			break;
		offset = v->vcap.cap_next;
	} while (offset != 0);

	if (offset == 0)
		return ENOENT;

	if (v->vcap.cap_len > sizeof(struct virtio_pci_cap)) {
		len = roundup(v->vcap.cap_len, sizeof(pcireg_t));
		if (len > buflen) {
			printf("%s: cap too large\n", __func__);
			return ERANGE;
		}
		for (i = 4; i < len / sizeof(pcireg_t); i++)
			v->reg[i] = pci_conf_read(pc, tag, offset + i * 4);
	}

	return 0;
}


#define NMAPREG		((PCI_MAPREG_END - PCI_MAPREG_START) / \
				sizeof(pcireg_t))

int
virtio_pci_attach_10(struct virtio_pci_softc *sc, struct pci_attach_args *pa)
{
	struct virtio_pci_cap common, isr, device;
	struct virtio_pci_notify_cap notify;
	int have_device_cfg = 0;
	bus_size_t bars[NMAPREG] = { 0 };
	int bars_idx[NMAPREG] = { 0 };
	struct virtio_pci_cap *caps[] = { &common, &isr, &device, &notify.cap };
	int i, j = 0, ret = 0;

	if (virtio_pci_find_cap(sc, VIRTIO_PCI_CAP_COMMON_CFG, &common, sizeof(common)) != 0)
		return ENODEV;

	if (virtio_pci_find_cap(sc, VIRTIO_PCI_CAP_NOTIFY_CFG, &notify, sizeof(notify)) != 0)
		return ENODEV;
	if (virtio_pci_find_cap(sc, VIRTIO_PCI_CAP_ISR_CFG, &isr, sizeof(isr)) != 0)
		return ENODEV;
	if (virtio_pci_find_cap(sc, VIRTIO_PCI_CAP_DEVICE_CFG, &device, sizeof(device)) != 0)
		memset(&device, 0, sizeof(device));
	else
		have_device_cfg = 1;

	/*
	 * XXX Maybe there are devices that offer the pci caps but not the
	 * XXX VERSION_1 feature bit? Then we should check the feature bit
	 * XXX here and fall back to 0.9 out if not present.
	 */

	/* Figure out which bars we need to map */
	for (i = 0; i < nitems(caps); i++) {
		int bar = caps[i]->bar;
		bus_size_t len = caps[i]->offset + caps[i]->length;
		if (caps[i]->length == 0)
			continue;
		if (bars[bar] < len)
			bars[bar] = len;
	}

	for (i = 0; i < nitems(bars); i++) {
		int reg;
		pcireg_t type;
		if (bars[i] == 0)
			continue;
		reg = PCI_MAPREG_START + i * 4;
		type = pci_mapreg_type(sc->sc_pc, sc->sc_ptag, reg);
		if (pci_mapreg_map(pa, reg, type, 0, &sc->sc_bars_iot[j],
		    &sc->sc_bars_ioh[j], NULL, &sc->sc_bars_iosize[j],
		    bars[i])) {
			printf("%s: can't map bar %u \n",
			    sc->sc_sc.sc_dev.dv_xname, i);
			ret = EIO;
			goto err;
		}
		bars_idx[i] = j;
		j++;
	}

	i = bars_idx[notify.cap.bar];
	if (bus_space_subregion(sc->sc_bars_iot[i], sc->sc_bars_ioh[i],
	    notify.cap.offset, notify.cap.length, &sc->sc_notify_ioh) != 0) {
		printf("%s: can't map notify i/o space\n",
		    sc->sc_sc.sc_dev.dv_xname);
		ret = EIO;
		goto err;
	}
	sc->sc_notify_iosize = notify.cap.length;
	sc->sc_notify_iot = sc->sc_bars_iot[i];
	sc->sc_notify_off_multiplier = notify.notify_off_multiplier;

	if (have_device_cfg) {
		i = bars_idx[device.bar];
		if (bus_space_subregion(sc->sc_bars_iot[i], sc->sc_bars_ioh[i],
		    device.offset, device.length, &sc->sc_devcfg_ioh) != 0) {
			printf("%s: can't map devcfg i/o space\n",
			    sc->sc_sc.sc_dev.dv_xname);
			ret = EIO;
			goto err;
		}
		sc->sc_devcfg_iosize = device.length;
		sc->sc_devcfg_iot = sc->sc_bars_iot[i];
	}

	i = bars_idx[isr.bar];
	if (bus_space_subregion(sc->sc_bars_iot[i], sc->sc_bars_ioh[i],
	    isr.offset, isr.length, &sc->sc_isr_ioh) != 0) {
		printf("%s: can't map isr i/o space\n",
		    sc->sc_sc.sc_dev.dv_xname);
		ret = EIO;
		goto err;
	}
	sc->sc_isr_iosize = isr.length;
	sc->sc_isr_iot = sc->sc_bars_iot[i];

	i = bars_idx[common.bar];
	if (bus_space_subregion(sc->sc_bars_iot[i], sc->sc_bars_ioh[i],
	    common.offset, common.length, &sc->sc_ioh) != 0) {
		printf("%s: can't map common i/o space\n",
		    sc->sc_sc.sc_dev.dv_xname);
		ret = EIO;
		goto err;
	}
	sc->sc_iosize = common.length;
	sc->sc_iot = sc->sc_bars_iot[i];

	sc->sc_sc.sc_version_1 = 1;
	return 0;

err:
	/* there is no pci_mapreg_unmap() */
	return ret;
}

int
virtio_pci_attach_09(struct virtio_pci_softc *sc, struct pci_attach_args *pa)
{
	struct virtio_softc *vsc = &sc->sc_sc;
	pcireg_t type;

	type = pci_mapreg_type(pa->pa_pc, pa->pa_tag, PCI_MAPREG_START);
	if (pci_mapreg_map(pa, PCI_MAPREG_START, type, 0,
	    &sc->sc_iot, &sc->sc_ioh, NULL, &sc->sc_iosize, 0)) {
		printf("%s: can't map i/o space\n", vsc->sc_dev.dv_xname);
		return EIO;
	}

	if (bus_space_subregion(sc->sc_iot, sc->sc_ioh,
	    VIRTIO_CONFIG_QUEUE_NOTIFY, 2, &sc->sc_notify_ioh) != 0) {
		printf("%s: can't map notify i/o space\n",
		    vsc->sc_dev.dv_xname);
		return EIO;
	}
	sc->sc_notify_iosize = 2;
	sc->sc_notify_iot = sc->sc_iot;

	if (bus_space_subregion(sc->sc_iot, sc->sc_ioh,
	    VIRTIO_CONFIG_ISR_STATUS, 1, &sc->sc_isr_ioh) != 0) {
		printf("%s: can't map isr i/o space\n",
		    vsc->sc_dev.dv_xname);
		return EIO;
	}
	sc->sc_isr_iosize = 1;
	sc->sc_isr_iot = sc->sc_iot;

	return 0;
}

void
virtio_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct virtio_pci_softc *sc = (struct virtio_pci_softc *)self;
	struct virtio_softc *vsc = &sc->sc_sc;
	struct pci_attach_args *pa = (struct pci_attach_args *)aux;
	pci_chipset_tag_t pc = pa->pa_pc;
	pcitag_t tag = pa->pa_tag;
	int revision, product, vendor, ret = ENODEV, flags;
	pcireg_t id;
	struct virtio_pci_attach_args vpa = { { 0 }, pa, 0 };

	revision = PCI_REVISION(pa->pa_class);
	product = PCI_PRODUCT(pa->pa_id);
	vendor = PCI_VENDOR(pa->pa_id);
	if (vendor == PCI_VENDOR_OPENBSD ||
	    (product >= 0x1000 && product <= 0x103f && revision == 0)) {
		/* OpenBSD VMMCI and virtio 0.9 */
		id = PCI_PRODUCT(pci_conf_read(pc, tag, PCI_SUBSYS_ID_REG));
	} else if (product >= 0x1040 && product <= 0x107f) {
		/* virtio 1.0 */
		id = product - 0x1040;
		revision = 1;
	} else {
		printf("unknown device prod 0x%04x rev 0x%02x; giving up\n",
		    product, revision);
		return;
	}

	sc->sc_pc = pc;
	sc->sc_ptag = pa->pa_tag;
	vsc->sc_dmat = pa->pa_dmat;

#if defined(__i386__) || defined(__amd64__)
	/*
	 * For virtio, ignore normal MSI black/white-listing depending on the
	 * PCI bridge but enable it unconditionally.
	 */
	pa->pa_flags |= PCI_FLAGS_MSI_ENABLED;
#endif

#if VIRTIO_DEBUG
	virtio_pci_dump_caps(sc);
#endif

	sc->sc_nintr = min(MAX_MSIX_VECS, pci_intr_msix_count(pa));
	if (sc->sc_nintr > 0)
		vpa.vpa_msix = 1;
	else
		sc->sc_nintr = 1;
	vpa.vpa_va.va_nintr = sc->sc_nintr;

	sc->sc_intr = mallocarray(sc->sc_nintr, sizeof(*sc->sc_intr),
	    M_DEVBUF, M_WAITOK | M_ZERO);

	vsc->sc_ops = &virtio_pci_ops;
	flags = vsc->sc_dev.dv_cfdata->cf_flags;
	if ((flags & VIRTIO_CF_PREFER_VERSION_09) == 0)
		ret = virtio_pci_attach_10(sc, pa);
	if (ret != 0 && revision == 0) {
		/* revision 0 means 0.9 only or both 0.9 and 1.0 */
		ret = virtio_pci_attach_09(sc, pa);
	}
	if (ret != 0 && (flags & VIRTIO_CF_PREFER_VERSION_09))
		ret = virtio_pci_attach_10(sc, pa);
	if (ret != 0) {
		printf(": Cannot attach (%d)\n", ret);
		goto free;
	}

	sc->sc_irq_type = IRQ_NO_MSIX;
	if (virtio_pci_adjust_config_region(sc,
	    VIRTIO_CONFIG_DEVICE_CONFIG_NOMSI) != 0)
	{
		goto free;
	}

	virtio_device_reset(vsc);
	virtio_set_status(vsc, VIRTIO_CONFIG_DEVICE_STATUS_ACK);
	virtio_set_status(vsc, VIRTIO_CONFIG_DEVICE_STATUS_DRIVER);

	printf("\n");
	vpa.vpa_va.va_devid = id;
	vsc->sc_child = NULL;
	config_found(self, &vpa, NULL);
	if (vsc->sc_child == NULL) {
		printf("%s: no matching child driver; not configured\n",
		    vsc->sc_dev.dv_xname);
		goto err;
	}
	if (vsc->sc_child == VIRTIO_CHILD_ERROR) {
		printf("%s: virtio configuration failed\n",
		    vsc->sc_dev.dv_xname);
		goto err;
	}

	return;

err:
	/* no pci_mapreg_unmap() or pci_intr_unmap() */
	virtio_set_status(vsc, VIRTIO_CONFIG_DEVICE_STATUS_FAILED);
free:
	free(sc->sc_intr, M_DEVBUF, sc->sc_nintr * sizeof(*sc->sc_intr));
}

int
virtio_pci_attach_finish(struct virtio_softc *vsc,
    struct virtio_attach_args *va)
{
	struct virtio_pci_softc *sc = (struct virtio_pci_softc *)vsc;
	struct virtio_pci_attach_args *vpa =
	    (struct virtio_pci_attach_args *)va;
	pci_intr_handle_t ih;
	pci_chipset_tag_t pc = vpa->vpa_pa->pa_pc;
	char const *intrstr;

	if (sc->sc_irq_type == IRQ_MSIX_CHILD) {
		intrstr = "msix";
	} else if (virtio_pci_setup_msix(sc, vpa, 0) == 0) {
		sc->sc_irq_type = IRQ_MSIX_PER_VQ;
		intrstr = "msix per-VQ";
	} else if (virtio_pci_setup_msix(sc, vpa, 1) == 0) {
		sc->sc_irq_type = IRQ_MSIX_SHARED;
		intrstr = "msix shared";
	} else {
		int (*ih_func)(void *) = virtio_pci_legacy_intr;
		if (pci_intr_map_msi(vpa->vpa_pa, &ih) != 0 &&
		    pci_intr_map(vpa->vpa_pa, &ih) != 0) {
			printf("%s: couldn't map interrupt\n",
			    vsc->sc_dev.dv_xname);
			return -EIO;
		}
		intrstr = pci_intr_string(pc, ih);
		/*
		 * We always set the IPL_MPSAFE flag in order to do the relatively
		 * expensive ISR read without lock, and then grab the kernel lock in
		 * the interrupt handler.
		 */
		if (vsc->sc_ipl & IPL_MPSAFE)
			ih_func = virtio_pci_legacy_intr_mpsafe;
		sc->sc_intr[0].ih = pci_intr_establish(pc, ih,
		    vsc->sc_ipl | IPL_MPSAFE, ih_func, sc,
		    vsc->sc_child->dv_xname);
		if (sc->sc_intr[0].ih == NULL) {
			printf("%s: couldn't establish interrupt",
			    vsc->sc_dev.dv_xname);
			if (intrstr != NULL)
				printf(" at %s", intrstr);
			printf("\n");
			return -EIO;
		}
	}

	printf("%s: %s\n", vsc->sc_dev.dv_xname, intrstr);
	return 0;
}

int
virtio_pci_detach(struct device *self, int flags)
{
	struct virtio_pci_softc *sc = (struct virtio_pci_softc *)self;
	struct virtio_softc *vsc = &sc->sc_sc;
	int r;

	if (vsc->sc_child != 0 && vsc->sc_child != VIRTIO_CHILD_ERROR) {
		r = config_detach(vsc->sc_child, flags);
		if (r)
			return r;
	}
	KASSERT(vsc->sc_child == 0 || vsc->sc_child == VIRTIO_CHILD_ERROR);
	KASSERT(vsc->sc_vqs == 0);
	virtio_pci_free_irqs(sc);
	if (sc->sc_iosize)
		bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_iosize);
	sc->sc_iosize = 0;

	return 0;
}

int
virtio_pci_adjust_config_region(struct virtio_pci_softc *sc, int offset)
{
	if (sc->sc_sc.sc_version_1)
		return 0;
	if (sc->sc_devcfg_offset == offset)
		return 0;
	sc->sc_devcfg_offset = offset;
	sc->sc_devcfg_iosize = sc->sc_iosize - offset;
	sc->sc_devcfg_iot = sc->sc_iot;
	if (bus_space_subregion(sc->sc_iot, sc->sc_ioh, sc->sc_devcfg_offset,
	    sc->sc_devcfg_iosize, &sc->sc_devcfg_ioh) != 0) {
		printf("%s: can't map config i/o space\n",
		    sc->sc_sc.sc_dev.dv_xname);
		return 1;
	}
	return 0;
}

/*
 * Feature negotiation.
 * Prints available / negotiated features if guest_feature_names != NULL and
 * VIRTIO_DEBUG is 1
 */
int
virtio_pci_negotiate_features(struct virtio_softc *vsc,
    const struct virtio_feature_name *guest_feature_names)
{
	struct virtio_pci_softc *sc = (struct virtio_pci_softc *)vsc;
	uint64_t host, negotiated;

	vsc->sc_active_features = 0;

	/*
	 * We enable indirect descriptors by default. They can be switched
	 * off by setting bit 1 in the driver flags, see config(8)
	 */
	if (!(vsc->sc_dev.dv_cfdata->cf_flags & VIRTIO_CF_NO_INDIRECT) &&
	    !(vsc->sc_child->dv_cfdata->cf_flags & VIRTIO_CF_NO_INDIRECT)) {
		vsc->sc_driver_features |= VIRTIO_F_RING_INDIRECT_DESC;
	} else if (guest_feature_names != NULL) {
		printf(" RingIndirectDesc disabled by UKC");
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
		vsc->sc_driver_features &= ~VIRTIO_F_RING_EVENT_IDX;
	}

	if (vsc->sc_version_1) {
		return virtio_pci_negotiate_features_10(vsc,
		    guest_feature_names);
	}

	/* virtio 0.9 only */
	host = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
				VIRTIO_CONFIG_DEVICE_FEATURES);
	negotiated = host & vsc->sc_driver_features;
#if VIRTIO_DEBUG
	if (guest_feature_names)
		virtio_log_features(host, negotiated, guest_feature_names);
#endif
	bus_space_write_4(sc->sc_iot, sc->sc_ioh,
			  VIRTIO_CONFIG_GUEST_FEATURES, negotiated);
	vsc->sc_active_features = negotiated;
	if (negotiated & VIRTIO_F_RING_INDIRECT_DESC)
		vsc->sc_indirect = 1;
	else
		vsc->sc_indirect = 0;
	return 0;
}

int
virtio_pci_negotiate_features_10(struct virtio_softc *vsc,
    const struct virtio_feature_name *guest_feature_names)
{
	struct virtio_pci_softc *sc = (struct virtio_pci_softc *)vsc;
	uint64_t host, negotiated;

	vsc->sc_driver_features |= VIRTIO_F_VERSION_1;
	/*
	 * XXX Without this SEV doesn't work with a KVM/qemu hypervisor on
	 * XXX amd64.
	 */
	vsc->sc_driver_features |= VIRTIO_F_ACCESS_PLATFORM;
	/* notify on empty is 0.9 only */
	vsc->sc_driver_features &= ~VIRTIO_F_NOTIFY_ON_EMPTY;
	CWRITE(sc, device_feature_select, 0);
	host = CREAD(sc, device_feature);
	CWRITE(sc, device_feature_select, 1);
	host |= (uint64_t)CREAD(sc, device_feature) << 32;

	negotiated = host & vsc->sc_driver_features;
#if VIRTIO_DEBUG
	if (guest_feature_names)
		virtio_log_features(host, negotiated, guest_feature_names);
#endif
	CWRITE(sc, driver_feature_select, 0);
	CWRITE(sc, driver_feature, negotiated & 0xffffffff);
	CWRITE(sc, driver_feature_select, 1);
	CWRITE(sc, driver_feature, negotiated >> 32);
	virtio_pci_set_status(vsc, VIRTIO_CONFIG_DEVICE_STATUS_FEATURES_OK);

	if ((CREAD(sc, device_status) &
	    VIRTIO_CONFIG_DEVICE_STATUS_FEATURES_OK) == 0) {
		printf("%s: Feature negotiation failed\n",
		    vsc->sc_dev.dv_xname);
		CWRITE(sc, device_status, VIRTIO_CONFIG_DEVICE_STATUS_FAILED);
		return ENXIO;
	}
	vsc->sc_active_features = negotiated;

	if (negotiated & VIRTIO_F_RING_INDIRECT_DESC)
		vsc->sc_indirect = 1;
	else
		vsc->sc_indirect = 0;

	if ((negotiated & VIRTIO_F_VERSION_1) == 0) {
#if VIRTIO_DEBUG
		printf("%s: Host rejected Version_1\n", __func__);
#endif
		CWRITE(sc, device_status, VIRTIO_CONFIG_DEVICE_STATUS_FAILED);
		return EINVAL;
	}
	return 0;
}

/*
 * Device configuration registers.
 */
uint8_t
virtio_pci_read_device_config_1(struct virtio_softc *vsc, int index)
{
	struct virtio_pci_softc *sc = (struct virtio_pci_softc *)vsc;
	return bus_space_read_1(sc->sc_devcfg_iot, sc->sc_devcfg_ioh, index);
}

uint16_t
virtio_pci_read_device_config_2(struct virtio_softc *vsc, int index)
{
	struct virtio_pci_softc *sc = (struct virtio_pci_softc *)vsc;
	return bus_space_read_2(sc->sc_devcfg_iot, sc->sc_devcfg_ioh, index);
}

uint32_t
virtio_pci_read_device_config_4(struct virtio_softc *vsc, int index)
{
	struct virtio_pci_softc *sc = (struct virtio_pci_softc *)vsc;
	return bus_space_read_4(sc->sc_devcfg_iot, sc->sc_devcfg_ioh, index);
}

uint64_t
virtio_pci_read_device_config_8(struct virtio_softc *vsc, int index)
{
	struct virtio_pci_softc *sc = (struct virtio_pci_softc *)vsc;
	uint64_t r;

	r = bus_space_read_4(sc->sc_devcfg_iot, sc->sc_devcfg_ioh,
	    index + sizeof(uint32_t));
	r <<= 32;
	r += bus_space_read_4(sc->sc_devcfg_iot, sc->sc_devcfg_ioh, index);
	return r;
}

void
virtio_pci_write_device_config_1(struct virtio_softc *vsc, int index,
    uint8_t value)
{
	struct virtio_pci_softc *sc = (struct virtio_pci_softc *)vsc;
	bus_space_write_1(sc->sc_devcfg_iot, sc->sc_devcfg_ioh, index, value);
}

void
virtio_pci_write_device_config_2(struct virtio_softc *vsc, int index,
    uint16_t value)
{
	struct virtio_pci_softc *sc = (struct virtio_pci_softc *)vsc;
	bus_space_write_2(sc->sc_devcfg_iot, sc->sc_devcfg_ioh, index, value);
}

void
virtio_pci_write_device_config_4(struct virtio_softc *vsc,
			     int index, uint32_t value)
{
	struct virtio_pci_softc *sc = (struct virtio_pci_softc *)vsc;
	bus_space_write_4(sc->sc_devcfg_iot, sc->sc_devcfg_ioh, index, value);
}

void
virtio_pci_write_device_config_8(struct virtio_softc *vsc,
			     int index, uint64_t value)
{
	struct virtio_pci_softc *sc = (struct virtio_pci_softc *)vsc;
	bus_space_write_4(sc->sc_devcfg_iot, sc->sc_devcfg_ioh,
	    index, value & 0xffffffff);
	bus_space_write_4(sc->sc_devcfg_iot, sc->sc_devcfg_ioh,
	    index + sizeof(uint32_t), value >> 32);
}

int
virtio_pci_msix_establish(struct virtio_pci_softc *sc,
    struct virtio_pci_attach_args *vpa, int idx, struct cpu_info *ci,
    int (*handler)(void *), void *ih_arg)
{
	struct virtio_softc *vsc = &sc->sc_sc;
	pci_intr_handle_t ih;
	int r;

	KASSERT(idx < sc->sc_nintr);

	if (!vpa->vpa_msix)
		return ENXIO;

	r = pci_intr_map_msix(vpa->vpa_pa, idx, &ih);
	if (r != 0) {
#if VIRTIO_DEBUG
		printf("%s[%d]: pci_intr_map_msix failed\n",
		    vsc->sc_dev.dv_xname, idx);
#endif
		return r;
	}
	snprintf(sc->sc_intr[idx].name, sizeof(sc->sc_intr[idx].name), "%s:%d",
	    vsc->sc_child->dv_xname, idx);
	sc->sc_intr[idx].ih = pci_intr_establish_cpu(sc->sc_pc, ih, vsc->sc_ipl,
	    ci, handler, ih_arg, sc->sc_intr[idx].name);
	if (sc->sc_intr[idx].ih == NULL) {
		printf("%s[%d]: couldn't establish msix interrupt\n",
		    vsc->sc_child->dv_xname, idx);
		return ENOMEM;
	}
	virtio_pci_adjust_config_region(sc, VIRTIO_CONFIG_DEVICE_CONFIG_MSI);
	return 0;
}

void
virtio_pci_set_msix_queue_vector(struct virtio_pci_softc *sc, uint32_t idx, uint16_t vector)
{
	if (sc->sc_sc.sc_version_1) {
		CWRITE(sc, queue_select, idx);
		CWRITE(sc, queue_msix_vector, vector);
	} else {
		bus_space_write_2(sc->sc_iot, sc->sc_ioh,
		    VIRTIO_CONFIG_QUEUE_SELECT, idx);
		bus_space_write_2(sc->sc_iot, sc->sc_ioh,
		    VIRTIO_MSI_QUEUE_VECTOR, vector);
	}
}

void
virtio_pci_set_msix_config_vector(struct virtio_pci_softc *sc, uint16_t vector)
{
	if (sc->sc_sc.sc_version_1) {
		CWRITE(sc, config_msix_vector, vector);
	} else {
		bus_space_write_2(sc->sc_iot, sc->sc_ioh,
		    VIRTIO_MSI_CONFIG_VECTOR, vector);
	}
}


void
virtio_pci_free_irqs(struct virtio_pci_softc *sc)
{
	struct virtio_softc *vsc = &sc->sc_sc;
	int i;

	if (sc->sc_devcfg_offset == VIRTIO_CONFIG_DEVICE_CONFIG_MSI) {
		for (i = 0; i < vsc->sc_nvqs; i++) {
			virtio_pci_set_msix_queue_vector(sc, i,
			    VIRTIO_MSI_NO_VECTOR);
		}
	}

	for (i = 0; i < sc->sc_nintr; i++) {
		if (sc->sc_intr[i].ih) {
			pci_intr_disestablish(sc->sc_pc, sc->sc_intr[i].ih);
			sc->sc_intr[i].ih = NULL;
		}
	}

	/* XXX msix_delroute does not unset PCI_MSIX_MC_MSIXE -> leave alone? */
	virtio_pci_adjust_config_region(sc, VIRTIO_CONFIG_DEVICE_CONFIG_NOMSI);
}

int
virtio_pci_setup_msix(struct virtio_pci_softc *sc,
    struct virtio_pci_attach_args *vpa, int shared)
{
	struct virtio_softc *vsc = &sc->sc_sc;
	int i, r = 0;

	/* Shared needs config + queue */
	if (shared && vpa->vpa_va.va_nintr < 1 + 1)
		return ERANGE;
	/* Per VQ needs config + N * queue */
	if (!shared && vpa->vpa_va.va_nintr < 1 + vsc->sc_nvqs)
		return ERANGE;

	r = virtio_pci_msix_establish(sc, vpa, 0, NULL, virtio_pci_config_intr, vsc);
	if (r != 0)
		return r;

	if (shared) {
		r = virtio_pci_msix_establish(sc, vpa, 1, NULL,
		    virtio_pci_shared_queue_intr, vsc);
		if (r != 0)
			goto fail;

		for (i = 0; i < vsc->sc_nvqs; i++)
			vsc->sc_vqs[i].vq_intr_vec = 1;
	} else {
		for (i = 0; i < vsc->sc_nvqs; i++) {
			r = virtio_pci_msix_establish(sc, vpa, i + 1, NULL,
			    virtio_pci_queue_intr, &vsc->sc_vqs[i]);
			if (r != 0)
				goto fail;
			vsc->sc_vqs[i].vq_intr_vec = i + 1;
		}
	}

	return 0;
fail:
	virtio_pci_free_irqs(sc);
	return r;
}

int
virtio_pci_intr_establish(struct virtio_softc *vsc,
    struct virtio_attach_args *va, int vec, struct cpu_info *ci,
    int (*func)(void *), void *arg)
{
	struct virtio_pci_attach_args *vpa;
	struct virtio_pci_softc *sc;

	if (vsc->sc_ops != &virtio_pci_ops)
		return ENXIO;

	vpa = (struct virtio_pci_attach_args *)va;
	sc = (struct virtio_pci_softc *)vsc;

	if (vec >= sc->sc_nintr || sc->sc_nintr <= 1)
		return ERANGE;

	sc->sc_irq_type = IRQ_MSIX_CHILD;
	return virtio_pci_msix_establish(sc, vpa, vec, ci, func, arg);
}

void
virtio_pci_intr_barrier(struct virtio_softc *vsc)
{
	struct virtio_pci_softc *sc = (struct virtio_pci_softc *)vsc;
	int i;

	for (i = 0; i < sc->sc_nintr; i++) {
		if (sc->sc_intr[i].ih != NULL)
			intr_barrier(sc->sc_intr[i].ih);
	}
}

/*
 * Interrupt handler.
 */

/*
 * Only used without MSI-X
 */
int
virtio_pci_legacy_intr(void *arg)
{
	struct virtio_pci_softc *sc = arg;
	struct virtio_softc *vsc = &sc->sc_sc;
	int isr, r = 0;

	/* check and ack the interrupt */
	isr = bus_space_read_1(sc->sc_isr_iot, sc->sc_isr_ioh, 0);
	if (isr == 0)
		return 0;
	KERNEL_LOCK();
	if ((isr & VIRTIO_CONFIG_ISR_CONFIG_CHANGE) &&
	    (vsc->sc_config_change != NULL)) {
		r = (vsc->sc_config_change)(vsc);
	}
	r |= virtio_check_vqs(vsc);
	KERNEL_UNLOCK();

	return r;
}

int
virtio_pci_legacy_intr_mpsafe(void *arg)
{
	struct virtio_pci_softc *sc = arg;
	struct virtio_softc *vsc = &sc->sc_sc;
	int isr, r = 0;

	/* check and ack the interrupt */
	isr = bus_space_read_1(sc->sc_isr_iot, sc->sc_isr_ioh, 0);
	if (isr == 0)
		return 0;
	if ((isr & VIRTIO_CONFIG_ISR_CONFIG_CHANGE) &&
	    (vsc->sc_config_change != NULL)) {
		r = (vsc->sc_config_change)(vsc);
	}
	r |= virtio_check_vqs(vsc);
	return r;
}

/*
 * Only used with MSI-X
 */
int
virtio_pci_config_intr(void *arg)
{
	struct virtio_softc *vsc = arg;

	if (vsc->sc_config_change != NULL)
		return vsc->sc_config_change(vsc);
	return 0;
}

/*
 * Only used with MSI-X
 */
int
virtio_pci_queue_intr(void *arg)
{
	struct virtqueue *vq = arg;
	struct virtio_softc *vsc = vq->vq_owner;

	return virtio_check_vq(vsc, vq);
}

int
virtio_pci_shared_queue_intr(void *arg)
{
	struct virtio_softc *vsc = arg;

	return virtio_check_vqs(vsc);
}

/*
 * Interrupt handler to be used when polling.
 * We cannot use isr here because it is not defined in MSI-X mode.
 */
int
virtio_pci_poll_intr(void *arg)
{
	struct virtio_pci_softc *sc = arg;
	struct virtio_softc *vsc = &sc->sc_sc;
	int r = 0;

	if (vsc->sc_config_change != NULL)
		r = (vsc->sc_config_change)(vsc);

	r |= virtio_check_vqs(vsc);

	return r;
}

void
virtio_pci_kick(struct virtio_softc *vsc, uint16_t idx)
{
	struct virtio_pci_softc *sc = (struct virtio_pci_softc *)vsc;
	unsigned offset = 0;
	if (vsc->sc_version_1) {
		offset = vsc->sc_vqs[idx].vq_notify_off *
		    sc->sc_notify_off_multiplier;
	}
	bus_space_write_2(sc->sc_notify_iot, sc->sc_notify_ioh, offset, idx);
}
