/*-
 * Copyright (c) 2017 Alexander Motin <mav@FreeBSD.org>
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
 * The Non-Transparent Bridge (NTB) is a device that allows you to connect
 * two or more systems using a PCI-e links, providing remote memory access.
 *
 * This module contains a driver for NTBs in PLX/Avago/Broadcom PCIe bridges.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/interrupt.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/sysctl.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#include <machine/bus.h>
#include <machine/intr_machdep.h>
#include <machine/resource.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include "../ntb.h"

#define PLX_MAX_BARS		4	/* There are at most 4 data BARs. */
#define PLX_NUM_SPAD		8	/* There are 8 scratchpads. */
#define PLX_NUM_SPAD_PATT	4	/* Use test pattern as 4 more. */
#define PLX_NUM_DB		16	/* There are 16 doorbells. */

struct ntb_plx_mw_info {
	int			 mw_bar;
	int			 mw_64bit;
	int			 mw_rid;
	struct resource		*mw_res;
	vm_paddr_t		 mw_pbase;
	caddr_t			 mw_vbase;
	vm_size_t		 mw_size;
	vm_memattr_t		 mw_map_mode;
	bus_addr_t		 mw_xlat_addr;
	size_t			 mw_xlat_size;
};

struct ntb_plx_softc {
	/* ntb.c context. Do not move! Must go first! */
	void			*ntb_store;

	device_t		 dev;
	struct resource		*conf_res;
	int			 conf_rid;
	u_int			 ntx;		/* NTx number within chip. */
	u_int			 link;		/* Link v/s Virtual side. */
	u_int			 port;		/* Port number within chip. */
	u_int			 alut;		/* A-LUT is enabled for NTx */

	int			 int_rid;
	struct resource		*int_res;
	void			*int_tag;

	struct ntb_plx_mw_info	 mw_info[PLX_MAX_BARS];
	int			 mw_count;	/* Number of memory windows. */

	int			 spad_count1;	/* Number of standard spads. */
	int			 spad_count2;	/* Number of extra spads. */
	uint32_t		 spad_off1;	/* Offset of our spads. */
	uint32_t		 spad_off2;	/* Offset of our extra spads. */
	uint32_t		 spad_offp1;	/* Offset of peer spads. */
	uint32_t		 spad_offp2;	/* Offset of peer extra spads. */

	/* Parameters of window shared with peer config access in B2B mode. */
	int			 b2b_mw;	/* Shared window number. */
	uint64_t		 b2b_off;	/* Offset in shared window. */
};

#define	PLX_NT0_BASE		0x3E000
#define	PLX_NT1_BASE		0x3C000
#define	PLX_NTX_BASE(sc)	((sc)->ntx ? PLX_NT1_BASE : PLX_NT0_BASE)
#define	PLX_NTX_LINK_OFFSET	0x01000

/* Bases of NTx our/peer interface registers */
#define	PLX_NTX_OUR_BASE(sc)				\
    (PLX_NTX_BASE(sc) + ((sc)->link ? PLX_NTX_LINK_OFFSET : 0))
#define	PLX_NTX_PEER_BASE(sc)				\
    (PLX_NTX_BASE(sc) + ((sc)->link ? 0 : PLX_NTX_LINK_OFFSET))

/* Read/write NTx our interface registers */
#define	NTX_READ(sc, reg)				\
    bus_read_4((sc)->conf_res, PLX_NTX_OUR_BASE(sc) + (reg))
#define	NTX_WRITE(sc, reg, val)				\
    bus_write_4((sc)->conf_res, PLX_NTX_OUR_BASE(sc) + (reg), (val))

/* Read/write NTx peer interface registers */
#define	PNTX_READ(sc, reg)				\
    bus_read_4((sc)->conf_res, PLX_NTX_PEER_BASE(sc) + (reg))
#define	PNTX_WRITE(sc, reg, val)			\
    bus_write_4((sc)->conf_res, PLX_NTX_PEER_BASE(sc) + (reg), (val))

/* Read/write B2B NTx registers */
#define	BNTX_READ(sc, reg)				\
    bus_read_4((sc)->mw_info[(sc)->b2b_mw].mw_res,	\
    PLX_NTX_BASE(sc) + (reg))
#define	BNTX_WRITE(sc, reg, val)			\
    bus_write_4((sc)->mw_info[(sc)->b2b_mw].mw_res,	\
    PLX_NTX_BASE(sc) + (reg), (val))

#define	PLX_PORT_BASE(p)		((p) << 12)
#define	PLX_STATION_PORT_BASE(sc)	PLX_PORT_BASE((sc)->port & ~7)

#define	PLX_PORT_CONTROL(sc)		(PLX_STATION_PORT_BASE(sc) + 0x208)

static int ntb_plx_init(device_t dev);
static int ntb_plx_detach(device_t dev);
static int ntb_plx_mw_set_trans_internal(device_t dev, unsigned mw_idx);

static int
ntb_plx_probe(device_t dev)
{

	switch (pci_get_devid(dev)) {
	case 0x87a010b5:
		device_set_desc(dev, "PLX Non-Transparent Bridge NT0 Link");
		return (BUS_PROBE_DEFAULT);
	case 0x87a110b5:
		device_set_desc(dev, "PLX Non-Transparent Bridge NT1 Link");
		return (BUS_PROBE_DEFAULT);
	case 0x87b010b5:
		device_set_desc(dev, "PLX Non-Transparent Bridge NT0 Virtual");
		return (BUS_PROBE_DEFAULT);
	case 0x87b110b5:
		device_set_desc(dev, "PLX Non-Transparent Bridge NT1 Virtual");
		return (BUS_PROBE_DEFAULT);
	}
	return (ENXIO);
}

static int
ntb_plx_init(device_t dev)
{
	struct ntb_plx_softc *sc = device_get_softc(dev);
	struct ntb_plx_mw_info *mw;
	uint64_t val64;
	int i;
	uint32_t val;

	if (sc->b2b_mw >= 0) {
		/* Set peer BAR0/1 size and address for B2B NTx access. */
		mw = &sc->mw_info[sc->b2b_mw];
		if (mw->mw_64bit) {
			PNTX_WRITE(sc, 0xe4, 0x3);	/* 64-bit */
			val64 = 0x2000000000000000 * mw->mw_bar | 0x4;
			PNTX_WRITE(sc, PCIR_BAR(0), val64);
			PNTX_WRITE(sc, PCIR_BAR(0) + 4, val64 >> 32);
		} else {
			PNTX_WRITE(sc, 0xe4, 0x2);	/* 32-bit */
			val = 0x20000000 * mw->mw_bar;
			PNTX_WRITE(sc, PCIR_BAR(0), val);
		}

		/* Set Virtual to Link address translation for B2B. */
		for (i = 0; i < sc->mw_count; i++) {
			mw = &sc->mw_info[i];
			if (mw->mw_64bit) {
				val64 = 0x2000000000000000 * mw->mw_bar;
				NTX_WRITE(sc, 0xc3c + (mw->mw_bar - 2) * 4, val64);
				NTX_WRITE(sc, 0xc3c + (mw->mw_bar - 2) * 4 + 4, val64 >> 32);
			} else {
				val = 0x20000000 * mw->mw_bar;
				NTX_WRITE(sc, 0xc3c + (mw->mw_bar - 2) * 4, val);
			}
		}

		/* Make sure Virtual to Link A-LUT is disabled. */
		if (sc->alut)
			PNTX_WRITE(sc, 0xc94, 0);

		/* Enable all Link Interface LUT entries for peer. */
		for (i = 0; i < 32; i += 2) {
			PNTX_WRITE(sc, 0xdb4 + i * 2,
			    0x00010001 | ((i + 1) << 19) | (i << 3));
		}
	}

	/*
	 * Enable Virtual Interface LUT entry 0 for 0:0.*.
	 * entry 1 for our Requester ID reported by the chip,
	 * entries 2-5 for 0/64/128/192:4.* of I/OAT DMA engines.
	 * XXX: Its a hack, we can't know all DMA engines, but this covers all
	 * I/OAT of Xeon E5/E7 at least from Sandy Bridge till Skylake I saw.
	 */
	val = (NTX_READ(sc, 0xc90) << 16) | 0x00010001;
	NTX_WRITE(sc, sc->link ? 0xdb4 : 0xd94, val);
	NTX_WRITE(sc, sc->link ? 0xdb8 : 0xd98, 0x40210021);
	NTX_WRITE(sc, sc->link ? 0xdbc : 0xd9c, 0xc0218021);

	/* Set Link to Virtual address translation. */
	for (i = 0; i < sc->mw_count; i++) {
		mw = &sc->mw_info[i];
		if (mw->mw_xlat_size != 0)
			ntb_plx_mw_set_trans_internal(dev, i);
	}

	pci_enable_busmaster(dev);
	if (sc->b2b_mw >= 0)
		PNTX_WRITE(sc, PCIR_COMMAND, PCIM_CMD_MEMEN | PCIM_CMD_BUSMASTEREN);

	return (0);
}

static void
ntb_plx_isr(void *arg)
{
	device_t dev = arg;
	struct ntb_plx_softc *sc = device_get_softc(dev);
	uint32_t val;

	ntb_db_event((device_t)arg, 0);

	if (sc->link)	/* Link Interface has no Link Error registers. */
		return;

	val = NTX_READ(sc, 0xfe0);
	if (val == 0)
		return;
	NTX_WRITE(sc, 0xfe0, val);
	if (val & 1)
		device_printf(dev, "Correctable Error\n");
	if (val & 2)
		device_printf(dev, "Uncorrectable Error\n");
	if (val & 4) {
		/* DL_Down resets link side registers, have to reinit. */
		ntb_plx_init(dev);
		ntb_link_event(dev);
	}
	if (val & 8)
		device_printf(dev, "Uncorrectable Error Message Drop\n");
}

static int
ntb_plx_setup_intr(device_t dev)
{
	struct ntb_plx_softc *sc = device_get_softc(dev);
	int error;

	/*
	 * XXX: This hardware supports MSI, but I found it unusable.
	 * It generates new MSI only when doorbell register goes from
	 * zero, but does not generate it when another bit is set or on
	 * partial clear.  It makes operation very racy and unreliable.
	 * The data book mentions some mask juggling magic to workaround
	 * that, but I failed to make it work.
	 */
	sc->int_rid = 0;
	sc->int_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
	    &sc->int_rid, RF_SHAREABLE|RF_ACTIVE);
	if (sc->int_res == NULL) {
		device_printf(dev, "bus_alloc_resource failed\n");
		return (ENOMEM);
	}
	error = bus_setup_intr(dev, sc->int_res, INTR_MPSAFE | INTR_TYPE_MISC,
	    NULL, ntb_plx_isr, dev, &sc->int_tag);
	if (error != 0) {
		device_printf(dev, "bus_setup_intr failed: %d\n", error);
		return (error);
	}

	if (!sc->link) { /* Link Interface has no Link Error registers. */
		NTX_WRITE(sc, 0xfe0, 0xf);	/* Clear link interrupts. */
		NTX_WRITE(sc, 0xfe4, 0x0);	/* Unmask link interrupts. */
	}
	return (0);
}

static void
ntb_plx_teardown_intr(device_t dev)
{
	struct ntb_plx_softc *sc = device_get_softc(dev);

	if (!sc->link)	/* Link Interface has no Link Error registers. */
		NTX_WRITE(sc, 0xfe4, 0xf);	/* Mask link interrupts. */

	if (sc->int_res) {
		bus_teardown_intr(dev, sc->int_res, sc->int_tag);
		bus_release_resource(dev, SYS_RES_IRQ, sc->int_rid,
		    sc->int_res);
	}
}

static int
ntb_plx_attach(device_t dev)
{
	struct ntb_plx_softc *sc = device_get_softc(dev);
	struct ntb_plx_mw_info *mw;
	int error = 0, i;
	uint32_t val;
	char buf[32];

	/* Identify what we are (what side of what NTx). */
	sc->dev = dev;
	val = pci_read_config(dev, 0xc8c, 4);
	sc->ntx = (val & 1) != 0;
	sc->link = (val & 0x80000000) != 0;

	/* Get access to whole 256KB of chip configuration space via BAR0/1. */
	sc->conf_rid = PCIR_BAR(0);
	sc->conf_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->conf_rid, RF_ACTIVE);
	if (sc->conf_res == NULL) {
		device_printf(dev, "Can't allocate configuration BAR.\n");
		return (ENXIO);
	}

	/* Identify chip port we are connected to. */
	val = bus_read_4(sc->conf_res, 0x360);
	sc->port = (val >> ((sc->ntx == 0) ? 8 : 16)) & 0x1f;

	/* Detect A-LUT enable and size. */
	val >>= 30;
	sc->alut = (val == 0x3) ? 1 : ((val & (1 << sc->ntx)) ? 2 : 0);
	if (sc->alut)
		device_printf(dev, "%u A-LUT entries\n", 128 * sc->alut);

	/* Find configured memory windows at BAR2-5. */
	sc->mw_count = 0;
	for (i = 2; i <= 5; i++) {
		mw = &sc->mw_info[sc->mw_count];
		mw->mw_bar = i;
		mw->mw_rid = PCIR_BAR(mw->mw_bar);
		mw->mw_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
		    &mw->mw_rid, RF_ACTIVE);
		if (mw->mw_res == NULL)
			continue;
		mw->mw_pbase = rman_get_start(mw->mw_res);
		mw->mw_size = rman_get_size(mw->mw_res);
		mw->mw_vbase = rman_get_virtual(mw->mw_res);
		mw->mw_map_mode = VM_MEMATTR_UNCACHEABLE;
		sc->mw_count++;

		/* Skip over adjacent BAR for 64-bit BARs. */
		val = pci_read_config(dev, PCIR_BAR(mw->mw_bar), 4);
		if ((val & PCIM_BAR_MEM_TYPE) == PCIM_BAR_MEM_64) {
			mw->mw_64bit = 1;
			i++;
		}
	}

	/* Try to identify B2B mode. */
	i = 1;
	snprintf(buf, sizeof(buf), "hint.%s.%d.b2b", device_get_name(dev),
	    device_get_unit(dev));
	TUNABLE_INT_FETCH(buf, &i);
	if (sc->link) {
		device_printf(dev, "NTB-to-Root Port mode (Link Interface)\n");
		sc->b2b_mw = -1;
	} else if (i == 0) {
		device_printf(dev, "NTB-to-Root Port mode (Virtual Interface)\n");
		sc->b2b_mw = -1;
	} else {
		device_printf(dev, "NTB-to-NTB (back-to-back) mode\n");

		/* We need at least one memory window for B2B peer access. */
		if (sc->mw_count == 0) {
			device_printf(dev, "No memory window BARs enabled.\n");
			error = ENXIO;
			goto out;
		}
		sc->b2b_mw = sc->mw_count - 1;

		/* Use half of the window for B2B, but no less then 1MB. */
		mw = &sc->mw_info[sc->b2b_mw];
		if (mw->mw_size >= 2 * 1024 * 1024)
			sc->b2b_off = mw->mw_size / 2;
		else
			sc->b2b_off = 0;
	}

	/*
	 * Use Physical Layer User Test Pattern as additional scratchpad.
	 * Make sure they are present and enabled by writing to them.
	 * XXX: Its a hack, but standard 8 registers are not enough.
	 */
	sc->spad_offp1 = sc->spad_off1 = PLX_NTX_OUR_BASE(sc) + 0xc6c;
	sc->spad_offp2 = sc->spad_off2 = PLX_PORT_BASE(sc->ntx * 8) + 0x20c;
	if (sc->b2b_mw >= 0) {
		/* In NTB-to-NTB mode each side has own scratchpads. */
		sc->spad_count1 = PLX_NUM_SPAD;
		bus_write_4(sc->conf_res, sc->spad_off2, 0x12345678);
		if (bus_read_4(sc->conf_res, sc->spad_off2) == 0x12345678)
			sc->spad_count2 = PLX_NUM_SPAD_PATT;
	} else {
		/* Otherwise we have share scratchpads with the peer. */
		if (sc->link) {
			sc->spad_off1 += PLX_NUM_SPAD / 2 * 4;
			sc->spad_off2 += PLX_NUM_SPAD_PATT / 2 * 4;
		} else {
			sc->spad_offp1 += PLX_NUM_SPAD / 2 * 4;
			sc->spad_offp2 += PLX_NUM_SPAD_PATT / 2 * 4;
		}
		sc->spad_count1 = PLX_NUM_SPAD / 2;
		bus_write_4(sc->conf_res, sc->spad_off2, 0x12345678);
		if (bus_read_4(sc->conf_res, sc->spad_off2) == 0x12345678)
			sc->spad_count2 = PLX_NUM_SPAD_PATT / 2;
	}

	/* Apply static part of NTB configuration. */
	ntb_plx_init(dev);

	/* Allocate and setup interrupts. */
	error = ntb_plx_setup_intr(dev);
	if (error)
		goto out;

	/* Attach children to this controller */
	error = ntb_register_device(dev);

out:
	if (error != 0)
		ntb_plx_detach(dev);
	return (error);
}

static int
ntb_plx_detach(device_t dev)
{
	struct ntb_plx_softc *sc = device_get_softc(dev);
	struct ntb_plx_mw_info *mw;
	int i;

	/* Detach & delete all children */
	ntb_unregister_device(dev);

	/* Disable and free interrupts. */
	ntb_plx_teardown_intr(dev);

	/* Free memory resources. */
	for (i = 0; i < sc->mw_count; i++) {
		mw = &sc->mw_info[i];
		bus_release_resource(dev, SYS_RES_MEMORY, mw->mw_rid,
		    mw->mw_res);
	}
	bus_release_resource(dev, SYS_RES_MEMORY, sc->conf_rid, sc->conf_res);
	return (0);
}


static bool
ntb_plx_link_is_up(device_t dev, enum ntb_speed *speed, enum ntb_width *width)
{
	uint16_t link;

	link = pcie_read_config(dev, PCIER_LINK_STA, 2);
	if (speed != NULL)
		*speed = (link & PCIEM_LINK_STA_SPEED);
	if (width != NULL)
		*width = (link & PCIEM_LINK_STA_WIDTH) >> 4;
	return ((link & PCIEM_LINK_STA_WIDTH) != 0);
}

static int
ntb_plx_link_enable(device_t dev, enum ntb_speed speed __unused,
    enum ntb_width width __unused)
{
	struct ntb_plx_softc *sc = device_get_softc(dev);
	uint32_t reg, val;

	/* The fact that we see the Link Interface means link is enabled. */
	if (sc->link) {
		ntb_link_event(dev);
		return (0);
	}

	reg = PLX_PORT_CONTROL(sc);
	val = bus_read_4(sc->conf_res, reg);
	if ((val & (1 << (sc->port & 7))) == 0) {
		/* If already enabled, generate fake link event and exit. */
		ntb_link_event(dev);
		return (0);
	}
	val &= ~(1 << (sc->port & 7));
	bus_write_4(sc->conf_res, reg, val);
	return (0);
}

static int
ntb_plx_link_disable(device_t dev)
{
	struct ntb_plx_softc *sc = device_get_softc(dev);
	uint32_t reg, val;

	/* Link disable for Link Interface would be suicidal. */
	if (sc->link)
		return (0);

	reg = PLX_PORT_CONTROL(sc);
	val = bus_read_4(sc->conf_res, reg);
	val |= (1 << (sc->port & 7));
	bus_write_4(sc->conf_res, reg, val);
	return (0);
}

static bool
ntb_plx_link_enabled(device_t dev)
{
	struct ntb_plx_softc *sc = device_get_softc(dev);
	uint32_t reg, val;

	/* The fact that we see the Link Interface means link is enabled. */
	if (sc->link)
		return (TRUE);

	reg = PLX_PORT_CONTROL(sc);
	val = bus_read_4(sc->conf_res, reg);
	return ((val & (1 << (sc->port & 7))) == 0);
}

static uint8_t
ntb_plx_mw_count(device_t dev)
{
	struct ntb_plx_softc *sc = device_get_softc(dev);

	if (sc->b2b_mw >= 0 && sc->b2b_off == 0)
		return (sc->mw_count - 1); /* B2B consumed whole window. */
	return (sc->mw_count);
}

static int
ntb_plx_mw_get_range(device_t dev, unsigned mw_idx, vm_paddr_t *base,
    caddr_t *vbase, size_t *size, size_t *align, size_t *align_size,
    bus_addr_t *plimit)
{
	struct ntb_plx_softc *sc = device_get_softc(dev);
	struct ntb_plx_mw_info *mw;
	size_t off;

	if (mw_idx >= sc->mw_count)
		return (EINVAL);
	off = 0;
	if (mw_idx == sc->b2b_mw) {
		KASSERT(sc->b2b_off != 0,
		    ("user shouldn't get non-shared b2b mw"));
		off = sc->b2b_off;
	}
	mw = &sc->mw_info[mw_idx];

	/* Local to remote memory window parameters. */
	if (base != NULL)
		*base = mw->mw_pbase + off;
	if (vbase != NULL)
		*vbase = mw->mw_vbase + off;
	if (size != NULL)
		*size = mw->mw_size - off;

	/*
	 * Remote to local memory window translation address alignment.
	 * Translation address has to be aligned to the BAR size, but A-LUT
	 * entries re-map addresses can be aligned to 1/128 or 1/256 of it.
	 * XXX: In B2B mode we can change BAR size (and so alignmet) live,
	 * but there is no way to report it here, so report safe value.
	 */
	if (align != NULL) {
		if (sc->alut && mw->mw_bar == 2)
			*align = (mw->mw_size - off) / 128 / sc->alut;
		else
			*align = mw->mw_size - off;
	}

	/*
	 * Remote to local memory window size alignment.
	 * The chip has no limit registers, but A-LUT, when available, allows
	 * access control with granularity of 1/128 or 1/256 of the BAR size.
	 * XXX: In B2B case we can change BAR size live, but there is no way
	 * to report it, so report half of the BAR size, that should be safe.
	 * In non-B2B case there is no control at all, so report the BAR size.
	 */
	if (align_size != NULL) {
		if (sc->alut && mw->mw_bar == 2)
			*align_size = (mw->mw_size - off) / 128 / sc->alut;
		else if (sc->b2b_mw >= 0)
			*align_size = (mw->mw_size - off) / 2;
		else
			*align_size = mw->mw_size - off;
	}

	/* Remote to local memory window translation address upper limit. */
	if (plimit != NULL)
		*plimit = mw->mw_64bit ? BUS_SPACE_MAXADDR :
		    BUS_SPACE_MAXADDR_32BIT;
	return (0);
}

static int
ntb_plx_mw_set_trans_internal(device_t dev, unsigned mw_idx)
{
	struct ntb_plx_softc *sc = device_get_softc(dev);
	struct ntb_plx_mw_info *mw;
	uint64_t addr, eaddr, off, size, bsize, esize, val64;
	uint32_t val;
	int i;

	mw = &sc->mw_info[mw_idx];
	addr = mw->mw_xlat_addr;
	size = mw->mw_xlat_size;
	off = 0;
	if (mw_idx == sc->b2b_mw) {
		off = sc->b2b_off;
		KASSERT(off != 0, ("user shouldn't get non-shared b2b mw"));

		/*
		 * While generally we can set any BAR size on link side,
		 * for B2B shared window we can't go above preconfigured
		 * size due to BAR address alignment requirements.
		 */
		if (size > mw->mw_size - off)
			return (EINVAL);
	}

	if (size > 0) {
		/* Round BAR size to next power of 2 or at least 1MB. */
		bsize = size;
		if (!powerof2(bsize))
			bsize = 1LL << flsll(bsize);
		if (bsize < 1024 * 1024)
			bsize = 1024 * 1024;

		/* A-LUT has 128 or 256 times better granularity. */
		esize = bsize;
		if (sc->alut && mw->mw_bar == 2)
			esize /= 128 * sc->alut;

		/* addr should be aligned to BAR or A-LUT element size. */
		if ((addr & (esize - 1)) != 0)
			return (EINVAL);
	} else
		esize = bsize = 0;

	if (mw->mw_64bit) {
		if (sc->b2b_mw >= 0) {
			/* Set Link Interface BAR size and enable/disable it. */
			val64 = 0;
			if (bsize > 0)
				val64 = (~(bsize - 1) & ~0xfffff);
			val64 |= 0xc;
			PNTX_WRITE(sc, 0xe8 + (mw->mw_bar - 2) * 4, val64);
			PNTX_WRITE(sc, 0xe8 + (mw->mw_bar - 2) * 4 + 4, val64 >> 32);

			/* Set Link Interface BAR address. */
			val64 = 0x2000000000000000 * mw->mw_bar + off;
			PNTX_WRITE(sc, PCIR_BAR(mw->mw_bar), val64);
			PNTX_WRITE(sc, PCIR_BAR(mw->mw_bar) + 4, val64 >> 32);
		}

		/* Set Virtual Interface BARs address translation */
		PNTX_WRITE(sc, 0xc3c + (mw->mw_bar - 2) * 4, addr);
		PNTX_WRITE(sc, 0xc3c + (mw->mw_bar - 2) * 4 + 4, addr >> 32);
	} else {
		/* Make sure we fit into 32-bit address space. */
		if ((addr & UINT32_MAX) != addr)
			return (ERANGE);
		if (((addr + bsize) & UINT32_MAX) != (addr + bsize))
			return (ERANGE);

		if (sc->b2b_mw >= 0) {
			/* Set Link Interface BAR size and enable/disable it. */
			val = 0;
			if (bsize > 0)
				val = (~(bsize - 1) & ~0xfffff);
			PNTX_WRITE(sc, 0xe8 + (mw->mw_bar - 2) * 4, val);

			/* Set Link Interface BAR address. */
			val64 = 0x20000000 * mw->mw_bar + off;
			PNTX_WRITE(sc, PCIR_BAR(mw->mw_bar), val64);
		}

		/* Set Virtual Interface BARs address translation */
		PNTX_WRITE(sc, 0xc3c + (mw->mw_bar - 2) * 4, addr);
	}

	/* Configure and enable Link to Virtual A-LUT if we need it. */
	if (sc->alut && mw->mw_bar == 2 &&
	    ((addr & (bsize - 1)) != 0 || size != bsize)) {
		eaddr = addr;
		for (i = 0; i < 128 * sc->alut; i++) {
			val = sc->link ? 0 : 1;
			if (sc->alut == 1)
				val += 2 * sc->ntx;
			val *= 0x1000 * sc->alut;
			val += 0x38000 + i * 4 + (i >= 128 ? 0x0e00 : 0);
			bus_write_4(sc->conf_res, val, eaddr);
			bus_write_4(sc->conf_res, val + 0x400, eaddr >> 32);
			bus_write_4(sc->conf_res, val + 0x800,
			    (eaddr < addr + size) ? 0x3 : 0);
			eaddr += esize;
		}
		NTX_WRITE(sc, 0xc94, 0x10000000);
	} else if (sc->alut && mw->mw_bar == 2)
		NTX_WRITE(sc, 0xc94, 0);

	return (0);
}

static int
ntb_plx_mw_set_trans(device_t dev, unsigned mw_idx, bus_addr_t addr, size_t size)
{
	struct ntb_plx_softc *sc = device_get_softc(dev);
	struct ntb_plx_mw_info *mw;

	if (mw_idx >= sc->mw_count)
		return (EINVAL);
	mw = &sc->mw_info[mw_idx];
	mw->mw_xlat_addr = addr;
	mw->mw_xlat_size = size;
	return (ntb_plx_mw_set_trans_internal(dev, mw_idx));
}

static int
ntb_plx_mw_clear_trans(device_t dev, unsigned mw_idx)
{

	return (ntb_plx_mw_set_trans(dev, mw_idx, 0, 0));
}

static int
ntb_plx_mw_get_wc(device_t dev, unsigned idx, vm_memattr_t *mode)
{
	struct ntb_plx_softc *sc = device_get_softc(dev);
	struct ntb_plx_mw_info *mw;

	if (idx >= sc->mw_count)
		return (EINVAL);
	mw = &sc->mw_info[idx];
	*mode = mw->mw_map_mode;
	return (0);
}

static int
ntb_plx_mw_set_wc(device_t dev, unsigned idx, vm_memattr_t mode)
{
	struct ntb_plx_softc *sc = device_get_softc(dev);
	struct ntb_plx_mw_info *mw;
	uint64_t off;
	int rc;

	if (idx >= sc->mw_count)
		return (EINVAL);
	mw = &sc->mw_info[idx];
	if (mw->mw_map_mode == mode)
		return (0);

	off = 0;
	if (idx == sc->b2b_mw) {
		KASSERT(sc->b2b_off != 0,
		    ("user shouldn't get non-shared b2b mw"));
		off = sc->b2b_off;
	}

	rc = pmap_change_attr((vm_offset_t)mw->mw_vbase + off,
	    mw->mw_size - off, mode);
	if (rc == 0)
		mw->mw_map_mode = mode;
	return (rc);
}

static uint8_t
ntb_plx_spad_count(device_t dev)
{
	struct ntb_plx_softc *sc = device_get_softc(dev);

	return (sc->spad_count1 + sc->spad_count2);
}

static int
ntb_plx_spad_write(device_t dev, unsigned int idx, uint32_t val)
{
	struct ntb_plx_softc *sc = device_get_softc(dev);
	u_int off;

	if (idx >= sc->spad_count1 + sc->spad_count2)
		return (EINVAL);

	if (idx < sc->spad_count1)
		off = sc->spad_off1 + idx * 4;
	else
		off = sc->spad_off2 + (idx - sc->spad_count1) * 4;
	bus_write_4(sc->conf_res, off, val);
	return (0);
}

static void
ntb_plx_spad_clear(device_t dev)
{
	struct ntb_plx_softc *sc = device_get_softc(dev);
	int i;

	for (i = 0; i < sc->spad_count1 + sc->spad_count2; i++)
		ntb_plx_spad_write(dev, i, 0);
}

static int
ntb_plx_spad_read(device_t dev, unsigned int idx, uint32_t *val)
{
	struct ntb_plx_softc *sc = device_get_softc(dev);
	u_int off;

	if (idx >= sc->spad_count1 + sc->spad_count2)
		return (EINVAL);

	if (idx < sc->spad_count1)
		off = sc->spad_off1 + idx * 4;
	else
		off = sc->spad_off2 + (idx - sc->spad_count1) * 4;
	*val = bus_read_4(sc->conf_res, off);
	return (0);
}

static int
ntb_plx_peer_spad_write(device_t dev, unsigned int idx, uint32_t val)
{
	struct ntb_plx_softc *sc = device_get_softc(dev);
	u_int off;

	if (idx >= sc->spad_count1 + sc->spad_count2)
		return (EINVAL);

	if (idx < sc->spad_count1)
		off = sc->spad_offp1 + idx * 4;
	else
		off = sc->spad_offp2 + (idx - sc->spad_count1) * 4;
	if (sc->b2b_mw >= 0)
		bus_write_4(sc->mw_info[sc->b2b_mw].mw_res, off, val);
	else
		bus_write_4(sc->conf_res, off, val);
	return (0);
}

static int
ntb_plx_peer_spad_read(device_t dev, unsigned int idx, uint32_t *val)
{
	struct ntb_plx_softc *sc = device_get_softc(dev);
	u_int off;

	if (idx >= sc->spad_count1 + sc->spad_count2)
		return (EINVAL);

	if (idx < sc->spad_count1)
		off = sc->spad_offp1 + idx * 4;
	else
		off = sc->spad_offp2 + (idx - sc->spad_count1) * 4;
	if (sc->b2b_mw >= 0)
		*val = bus_read_4(sc->mw_info[sc->b2b_mw].mw_res, off);
	else
		*val = bus_read_4(sc->conf_res, off);
	return (0);
}

static uint64_t
ntb_plx_db_valid_mask(device_t dev)
{

	return ((1LL << PLX_NUM_DB) - 1);
}

static int
ntb_plx_db_vector_count(device_t dev)
{

	return (1);
}

static uint64_t
ntb_plx_db_vector_mask(device_t dev, uint32_t vector)
{

	if (vector > 0)
		return (0);
	return ((1LL << PLX_NUM_DB) - 1);
}

static void
ntb_plx_db_clear(device_t dev, uint64_t bits)
{
	struct ntb_plx_softc *sc = device_get_softc(dev);

	NTX_WRITE(sc, sc->link ? 0xc60 : 0xc50, bits);
}

static void
ntb_plx_db_clear_mask(device_t dev, uint64_t bits)
{
	struct ntb_plx_softc *sc = device_get_softc(dev);

	NTX_WRITE(sc, sc->link ? 0xc68 : 0xc58, bits);
}

static uint64_t
ntb_plx_db_read(device_t dev)
{
	struct ntb_plx_softc *sc = device_get_softc(dev);

	return (NTX_READ(sc, sc->link ? 0xc5c : 0xc4c));
}

static void
ntb_plx_db_set_mask(device_t dev, uint64_t bits)
{
	struct ntb_plx_softc *sc = device_get_softc(dev);

	NTX_WRITE(sc, sc->link ? 0xc64 : 0xc54, bits);
}

static int
ntb_plx_peer_db_addr(device_t dev, bus_addr_t *db_addr, vm_size_t *db_size)
{
	struct ntb_plx_softc *sc = device_get_softc(dev);
	struct ntb_plx_mw_info *mw;

	KASSERT((db_addr != NULL && db_size != NULL), ("must be non-NULL"));

	if (sc->b2b_mw >= 0) {
		mw = &sc->mw_info[sc->b2b_mw];
		*db_addr = (uint64_t)mw->mw_pbase + PLX_NTX_BASE(sc) + 0xc4c;
	} else {
		*db_addr = rman_get_start(sc->conf_res) + PLX_NTX_BASE(sc);
		*db_addr += sc->link ? 0xc4c : 0xc5c;
	}
	*db_size = 4;
	return (0);
}

static void
ntb_plx_peer_db_set(device_t dev, uint64_t bit)
{
	struct ntb_plx_softc *sc = device_get_softc(dev);

	if (sc->b2b_mw >= 0)
		BNTX_WRITE(sc, 0xc4c, bit);
	else
		NTX_WRITE(sc, sc->link ? 0xc4c : 0xc5c, bit);
}

static device_method_t ntb_plx_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ntb_plx_probe),
	DEVMETHOD(device_attach,	ntb_plx_attach),
	DEVMETHOD(device_detach,	ntb_plx_detach),
	/* Bus interface */
	DEVMETHOD(bus_child_location_str, ntb_child_location_str),
	DEVMETHOD(bus_print_child,	ntb_print_child),
	/* NTB interface */
	DEVMETHOD(ntb_link_is_up,	ntb_plx_link_is_up),
	DEVMETHOD(ntb_link_enable,	ntb_plx_link_enable),
	DEVMETHOD(ntb_link_disable,	ntb_plx_link_disable),
	DEVMETHOD(ntb_link_enabled,	ntb_plx_link_enabled),
	DEVMETHOD(ntb_mw_count,		ntb_plx_mw_count),
	DEVMETHOD(ntb_mw_get_range,	ntb_plx_mw_get_range),
	DEVMETHOD(ntb_mw_set_trans,	ntb_plx_mw_set_trans),
	DEVMETHOD(ntb_mw_clear_trans,	ntb_plx_mw_clear_trans),
	DEVMETHOD(ntb_mw_get_wc,	ntb_plx_mw_get_wc),
	DEVMETHOD(ntb_mw_set_wc,	ntb_plx_mw_set_wc),
	DEVMETHOD(ntb_spad_count,	ntb_plx_spad_count),
	DEVMETHOD(ntb_spad_clear,	ntb_plx_spad_clear),
	DEVMETHOD(ntb_spad_write,	ntb_plx_spad_write),
	DEVMETHOD(ntb_spad_read,	ntb_plx_spad_read),
	DEVMETHOD(ntb_peer_spad_write,	ntb_plx_peer_spad_write),
	DEVMETHOD(ntb_peer_spad_read,	ntb_plx_peer_spad_read),
	DEVMETHOD(ntb_db_valid_mask,	ntb_plx_db_valid_mask),
	DEVMETHOD(ntb_db_vector_count,	ntb_plx_db_vector_count),
	DEVMETHOD(ntb_db_vector_mask,	ntb_plx_db_vector_mask),
	DEVMETHOD(ntb_db_clear,		ntb_plx_db_clear),
	DEVMETHOD(ntb_db_clear_mask,	ntb_plx_db_clear_mask),
	DEVMETHOD(ntb_db_read,		ntb_plx_db_read),
	DEVMETHOD(ntb_db_set_mask,	ntb_plx_db_set_mask),
	DEVMETHOD(ntb_peer_db_addr,	ntb_plx_peer_db_addr),
	DEVMETHOD(ntb_peer_db_set,	ntb_plx_peer_db_set),
	DEVMETHOD_END
};

static DEFINE_CLASS_0(ntb_hw, ntb_plx_driver, ntb_plx_methods,
    sizeof(struct ntb_plx_softc));
DRIVER_MODULE(ntb_hw_plx, pci, ntb_plx_driver, ntb_hw_devclass, NULL, NULL);
MODULE_DEPEND(ntb_hw_plx, ntb, 1, 1, 1);
MODULE_VERSION(ntb_hw_plx, 1);
