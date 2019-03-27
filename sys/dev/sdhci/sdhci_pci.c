/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Alexander Motin <mav@FreeBSD.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_mmccam.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/resource.h>
#include <sys/rman.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/mmc/bridge.h>

#include <dev/sdhci/sdhci.h>

#include "mmcbr_if.h"
#include "sdhci_if.h"

/*
 * PCI registers
 */
#define	PCI_SDHCI_IFPIO			0x00
#define	PCI_SDHCI_IFDMA			0x01
#define	PCI_SDHCI_IFVENDOR		0x02

#define	PCI_SLOT_INFO			0x40	/* 8 bits */
#define	PCI_SLOT_INFO_SLOTS(x)		(((x >> 4) & 7) + 1)
#define	PCI_SLOT_INFO_FIRST_BAR(x)	((x) & 7)

/*
 * RICOH specific PCI registers
 */
#define	SDHC_PCI_MODE_KEY		0xf9
#define	SDHC_PCI_MODE			0x150
#define	SDHC_PCI_MODE_SD20		0x10
#define	SDHC_PCI_BASE_FREQ_KEY		0xfc
#define	SDHC_PCI_BASE_FREQ		0xe1

static const struct sdhci_device {
	uint32_t	model;
	uint16_t	subvendor;
	const char	*desc;
	u_int		quirks;
} sdhci_devices[] = {
	{ 0x08221180,	0xffff,	"RICOH R5C822 SD",
	    SDHCI_QUIRK_FORCE_DMA },
	{ 0xe8221180,	0xffff,	"RICOH R5CE822 SD",
	    SDHCI_QUIRK_FORCE_DMA |
	    SDHCI_QUIRK_LOWER_FREQUENCY },
	{ 0xe8231180,	0xffff,	"RICOH R5CE823 SD",
	    SDHCI_QUIRK_LOWER_FREQUENCY },
	{ 0x8034104c,	0xffff, "TI XX21/XX11 SD",
	    SDHCI_QUIRK_FORCE_DMA },
	{ 0x05501524,	0xffff, "ENE CB712 SD",
	    SDHCI_QUIRK_BROKEN_TIMINGS },
	{ 0x05511524,	0xffff, "ENE CB712 SD 2",
	    SDHCI_QUIRK_BROKEN_TIMINGS },
	{ 0x07501524,	0xffff, "ENE CB714 SD",
	    SDHCI_QUIRK_RESET_ON_IOS |
	    SDHCI_QUIRK_BROKEN_TIMINGS },
	{ 0x07511524,	0xffff, "ENE CB714 SD 2",
	    SDHCI_QUIRK_RESET_ON_IOS |
	    SDHCI_QUIRK_BROKEN_TIMINGS },
	{ 0x410111ab,	0xffff, "Marvell CaFe SD",
	    SDHCI_QUIRK_INCR_TIMEOUT_CONTROL },
	{ 0x2381197B,	0xffff,	"JMicron JMB38X SD",
	    SDHCI_QUIRK_32BIT_DMA_SIZE |
	    SDHCI_QUIRK_RESET_AFTER_REQUEST },
	{ 0x16bc14e4,	0xffff,	"Broadcom BCM577xx SDXC/MMC Card Reader",
	    SDHCI_QUIRK_BCM577XX_400KHZ_CLKSRC },
	{ 0x0f148086,	0xffff,	"Intel Bay Trail eMMC 4.5 Controller",
	    /* DDR52 is supported but affected by the VLI54 erratum */
	    SDHCI_QUIRK_INTEL_POWER_UP_RESET |
	    SDHCI_QUIRK_WAIT_WHILE_BUSY |
	    SDHCI_QUIRK_CAPS_BIT63_FOR_MMC_HS400 |
	    SDHCI_QUIRK_PRESET_VALUE_BROKEN},
	{ 0x0f158086,	0xffff,	"Intel Bay Trail SDXC Controller",
	    SDHCI_QUIRK_WAIT_WHILE_BUSY |
	    SDHCI_QUIRK_PRESET_VALUE_BROKEN },
	{ 0x0f508086,	0xffff,	"Intel Bay Trail eMMC 4.5 Controller",
	    /* DDR52 is supported but affected by the VLI54 erratum */
	    SDHCI_QUIRK_INTEL_POWER_UP_RESET |
	    SDHCI_QUIRK_WAIT_WHILE_BUSY |
	    SDHCI_QUIRK_CAPS_BIT63_FOR_MMC_HS400 |
	    SDHCI_QUIRK_PRESET_VALUE_BROKEN },
	{ 0x19db8086,	0xffff,	"Intel Denverton eMMC 5.0 Controller",
	    SDHCI_QUIRK_INTEL_POWER_UP_RESET |
	    SDHCI_QUIRK_WAIT_WHILE_BUSY |
	    SDHCI_QUIRK_MMC_DDR52 |
	    SDHCI_QUIRK_CAPS_BIT63_FOR_MMC_HS400 |
	    SDHCI_QUIRK_PRESET_VALUE_BROKEN },
	{ 0x22948086,	0xffff,	"Intel Braswell eMMC 4.5.1 Controller",
	    SDHCI_QUIRK_DATA_TIMEOUT_1MHZ |
	    SDHCI_QUIRK_INTEL_POWER_UP_RESET |
	    SDHCI_QUIRK_WAIT_WHILE_BUSY |
	    SDHCI_QUIRK_MMC_DDR52 |
	    SDHCI_QUIRK_CAPS_BIT63_FOR_MMC_HS400 |
	    SDHCI_QUIRK_PRESET_VALUE_BROKEN },
	{ 0x22968086,	0xffff,	"Intel Braswell SDXC Controller",
	    SDHCI_QUIRK_WAIT_WHILE_BUSY |
	    SDHCI_QUIRK_PRESET_VALUE_BROKEN },
	{ 0x5aca8086,	0xffff,	"Intel Apollo Lake SDXC Controller",
	    SDHCI_QUIRK_BROKEN_DMA |	/* APL18 erratum */
	    SDHCI_QUIRK_WAIT_WHILE_BUSY |
	    SDHCI_QUIRK_PRESET_VALUE_BROKEN },
	{ 0x5acc8086,	0xffff,	"Intel Apollo Lake eMMC 5.0 Controller",
	    SDHCI_QUIRK_BROKEN_DMA |	/* APL18 erratum */
	    SDHCI_QUIRK_INTEL_POWER_UP_RESET |
	    SDHCI_QUIRK_WAIT_WHILE_BUSY |
	    SDHCI_QUIRK_MMC_DDR52 |
	    SDHCI_QUIRK_CAPS_BIT63_FOR_MMC_HS400 |
	    SDHCI_QUIRK_PRESET_VALUE_BROKEN },
	{ 0,		0xffff,	NULL,
	    0 }
};

struct sdhci_pci_softc {
	u_int		quirks;		/* Chip specific quirks */
	struct resource *irq_res;	/* IRQ resource */
	void		*intrhand;	/* Interrupt handle */

	int		num_slots;	/* Number of slots on this controller */
	struct sdhci_slot slots[6];
	struct resource	*mem_res[6];	/* Memory resource */
	uint8_t		cfg_freq;	/* Saved frequency */
	uint8_t		cfg_mode;	/* Saved mode */
};

static int sdhci_enable_msi = 1;
SYSCTL_INT(_hw_sdhci, OID_AUTO, enable_msi, CTLFLAG_RDTUN, &sdhci_enable_msi,
    0, "Enable MSI interrupts");

static uint8_t
sdhci_pci_read_1(device_t dev, struct sdhci_slot *slot __unused, bus_size_t off)
{
	struct sdhci_pci_softc *sc = device_get_softc(dev);

	bus_barrier(sc->mem_res[slot->num], 0, 0xFF,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	return bus_read_1(sc->mem_res[slot->num], off);
}

static void
sdhci_pci_write_1(device_t dev, struct sdhci_slot *slot __unused,
    bus_size_t off, uint8_t val)
{
	struct sdhci_pci_softc *sc = device_get_softc(dev);

	bus_barrier(sc->mem_res[slot->num], 0, 0xFF,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	bus_write_1(sc->mem_res[slot->num], off, val);
}

static uint16_t
sdhci_pci_read_2(device_t dev, struct sdhci_slot *slot __unused, bus_size_t off)
{
	struct sdhci_pci_softc *sc = device_get_softc(dev);

	bus_barrier(sc->mem_res[slot->num], 0, 0xFF,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	return bus_read_2(sc->mem_res[slot->num], off);
}

static void
sdhci_pci_write_2(device_t dev, struct sdhci_slot *slot __unused,
    bus_size_t off, uint16_t val)
{
	struct sdhci_pci_softc *sc = device_get_softc(dev);

	bus_barrier(sc->mem_res[slot->num], 0, 0xFF,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	bus_write_2(sc->mem_res[slot->num], off, val);
}

static uint32_t
sdhci_pci_read_4(device_t dev, struct sdhci_slot *slot __unused, bus_size_t off)
{
	struct sdhci_pci_softc *sc = device_get_softc(dev);

	bus_barrier(sc->mem_res[slot->num], 0, 0xFF,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	return bus_read_4(sc->mem_res[slot->num], off);
}

static void
sdhci_pci_write_4(device_t dev, struct sdhci_slot *slot __unused,
    bus_size_t off, uint32_t val)
{
	struct sdhci_pci_softc *sc = device_get_softc(dev);

	bus_barrier(sc->mem_res[slot->num], 0, 0xFF,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	bus_write_4(sc->mem_res[slot->num], off, val);
}

static void
sdhci_pci_read_multi_4(device_t dev, struct sdhci_slot *slot __unused,
    bus_size_t off, uint32_t *data, bus_size_t count)
{
	struct sdhci_pci_softc *sc = device_get_softc(dev);

	bus_read_multi_stream_4(sc->mem_res[slot->num], off, data, count);
}

static void
sdhci_pci_write_multi_4(device_t dev, struct sdhci_slot *slot __unused,
    bus_size_t off, uint32_t *data, bus_size_t count)
{
	struct sdhci_pci_softc *sc = device_get_softc(dev);

	bus_write_multi_stream_4(sc->mem_res[slot->num], off, data, count);
}

static void sdhci_pci_intr(void *arg);

static void
sdhci_lower_frequency(device_t dev)
{
	struct sdhci_pci_softc *sc = device_get_softc(dev);

	/*
	 * Enable SD2.0 mode.
	 * NB: for RICOH R5CE823, this changes the PCI device ID to 0xe822.
	 */
	pci_write_config(dev, SDHC_PCI_MODE_KEY, 0xfc, 1);
	sc->cfg_mode = pci_read_config(dev, SDHC_PCI_MODE, 1);
	pci_write_config(dev, SDHC_PCI_MODE, SDHC_PCI_MODE_SD20, 1);
	pci_write_config(dev, SDHC_PCI_MODE_KEY, 0x00, 1);

	/*
	 * Some SD/MMC cards don't work with the default base
	 * clock frequency of 200 MHz.  Lower it to 50 MHz.
	 */
	pci_write_config(dev, SDHC_PCI_BASE_FREQ_KEY, 0x01, 1);
	sc->cfg_freq = pci_read_config(dev, SDHC_PCI_BASE_FREQ, 1);
	pci_write_config(dev, SDHC_PCI_BASE_FREQ, 50, 1);
	pci_write_config(dev, SDHC_PCI_BASE_FREQ_KEY, 0x00, 1);
}

static void
sdhci_restore_frequency(device_t dev)
{
	struct sdhci_pci_softc *sc = device_get_softc(dev);

	/* Restore mode. */
	pci_write_config(dev, SDHC_PCI_MODE_KEY, 0xfc, 1);
	pci_write_config(dev, SDHC_PCI_MODE, sc->cfg_mode, 1);
	pci_write_config(dev, SDHC_PCI_MODE_KEY, 0x00, 1);

	/* Restore frequency. */
	pci_write_config(dev, SDHC_PCI_BASE_FREQ_KEY, 0x01, 1);
	pci_write_config(dev, SDHC_PCI_BASE_FREQ, sc->cfg_freq, 1);
	pci_write_config(dev, SDHC_PCI_BASE_FREQ_KEY, 0x00, 1);
}

static int
sdhci_pci_probe(device_t dev)
{
	uint32_t model;
	uint16_t subvendor;
	uint8_t class, subclass;
	int i, result;

	model = (uint32_t)pci_get_device(dev) << 16;
	model |= (uint32_t)pci_get_vendor(dev) & 0x0000ffff;
	subvendor = pci_get_subvendor(dev);
	class = pci_get_class(dev);
	subclass = pci_get_subclass(dev);

	result = ENXIO;
	for (i = 0; sdhci_devices[i].model != 0; i++) {
		if (sdhci_devices[i].model == model &&
		    (sdhci_devices[i].subvendor == 0xffff ||
		    sdhci_devices[i].subvendor == subvendor)) {
			device_set_desc(dev, sdhci_devices[i].desc);
			result = BUS_PROBE_DEFAULT;
			break;
		}
	}
	if (result == ENXIO && class == PCIC_BASEPERIPH &&
	    subclass == PCIS_BASEPERIPH_SDHC) {
		device_set_desc(dev, "Generic SD HCI");
		result = BUS_PROBE_GENERIC;
	}

	return (result);
}

static int
sdhci_pci_attach(device_t dev)
{
	struct sdhci_pci_softc *sc = device_get_softc(dev);
	struct sdhci_slot *slot;
	uint32_t model;
	uint16_t subvendor;
	int bar, err, rid, slots, i;

	model = (uint32_t)pci_get_device(dev) << 16;
	model |= (uint32_t)pci_get_vendor(dev) & 0x0000ffff;
	subvendor = pci_get_subvendor(dev);
	/* Apply chip specific quirks. */
	for (i = 0; sdhci_devices[i].model != 0; i++) {
		if (sdhci_devices[i].model == model &&
		    (sdhci_devices[i].subvendor == 0xffff ||
		    sdhci_devices[i].subvendor == subvendor)) {
			sc->quirks = sdhci_devices[i].quirks;
			break;
		}
	}
	sc->quirks &= ~sdhci_quirk_clear;
	sc->quirks |= sdhci_quirk_set;

	/* Some controllers need to be bumped into the right mode. */
	if (sc->quirks & SDHCI_QUIRK_LOWER_FREQUENCY)
		sdhci_lower_frequency(dev);
	/* Read slots info from PCI registers. */
	slots = pci_read_config(dev, PCI_SLOT_INFO, 1);
	bar = PCI_SLOT_INFO_FIRST_BAR(slots);
	slots = PCI_SLOT_INFO_SLOTS(slots);
	if (slots > 6 || bar > 5) {
		device_printf(dev, "Incorrect slots information (%d, %d).\n",
		    slots, bar);
		return (EINVAL);
	}
	/* Allocate IRQ. */
	i = 1;
	rid = 0;
	if (sdhci_enable_msi != 0 && pci_alloc_msi(dev, &i) == 0)
		rid = 1;
	sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
		RF_ACTIVE | (rid != 0 ? 0 : RF_SHAREABLE));
	if (sc->irq_res == NULL) {
		device_printf(dev, "Can't allocate IRQ\n");
		pci_release_msi(dev);
		return (ENOMEM);
	}
	/* Scan all slots. */
	for (i = 0; i < slots; i++) {
		slot = &sc->slots[sc->num_slots];

		/* Allocate memory. */
		rid = PCIR_BAR(bar + i);
		sc->mem_res[i] = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
		    &rid, RF_ACTIVE);
		if (sc->mem_res[i] == NULL) {
			device_printf(dev,
			    "Can't allocate memory for slot %d\n", i);
			continue;
		}

		slot->quirks = sc->quirks;

		if (sdhci_init_slot(dev, slot, i) != 0)
			continue;

		sc->num_slots++;
	}
	device_printf(dev, "%d slot(s) allocated\n", sc->num_slots);
	/* Activate the interrupt */
	err = bus_setup_intr(dev, sc->irq_res, INTR_TYPE_MISC | INTR_MPSAFE,
	    NULL, sdhci_pci_intr, sc, &sc->intrhand);
	if (err)
		device_printf(dev, "Can't setup IRQ\n");
	pci_enable_busmaster(dev);
	/* Process cards detection. */
	for (i = 0; i < sc->num_slots; i++) {
		sdhci_start_slot(&sc->slots[i]);
	}

	return (0);
}

static int
sdhci_pci_detach(device_t dev)
{
	struct sdhci_pci_softc *sc = device_get_softc(dev);
	int i;

	bus_teardown_intr(dev, sc->irq_res, sc->intrhand);
	bus_release_resource(dev, SYS_RES_IRQ,
	    rman_get_rid(sc->irq_res), sc->irq_res);
	pci_release_msi(dev);

	for (i = 0; i < sc->num_slots; i++) {
		sdhci_cleanup_slot(&sc->slots[i]);
		bus_release_resource(dev, SYS_RES_MEMORY,
		    rman_get_rid(sc->mem_res[i]), sc->mem_res[i]);
	}
	if (sc->quirks & SDHCI_QUIRK_LOWER_FREQUENCY)
		sdhci_restore_frequency(dev);
	return (0);
}

static int
sdhci_pci_shutdown(device_t dev)
{
	struct sdhci_pci_softc *sc = device_get_softc(dev);

	if (sc->quirks & SDHCI_QUIRK_LOWER_FREQUENCY)
		sdhci_restore_frequency(dev);
	return (0);
}

static int
sdhci_pci_suspend(device_t dev)
{
	struct sdhci_pci_softc *sc = device_get_softc(dev);
	int i, err;

	err = bus_generic_suspend(dev);
	if (err)
		return (err);
	for (i = 0; i < sc->num_slots; i++)
		sdhci_generic_suspend(&sc->slots[i]);
	return (0);
}

static int
sdhci_pci_resume(device_t dev)
{
	struct sdhci_pci_softc *sc = device_get_softc(dev);
	int i, err;

	for (i = 0; i < sc->num_slots; i++)
		sdhci_generic_resume(&sc->slots[i]);
	err = bus_generic_resume(dev);
	if (err)
		return (err);
	if (sc->quirks & SDHCI_QUIRK_LOWER_FREQUENCY)
		sdhci_lower_frequency(dev);
	return (0);
}

static void
sdhci_pci_intr(void *arg)
{
	struct sdhci_pci_softc *sc = (struct sdhci_pci_softc *)arg;
	int i;

	for (i = 0; i < sc->num_slots; i++)
		sdhci_generic_intr(&sc->slots[i]);
}

static device_method_t sdhci_methods[] = {
	/* device_if */
	DEVMETHOD(device_probe,		sdhci_pci_probe),
	DEVMETHOD(device_attach,	sdhci_pci_attach),
	DEVMETHOD(device_detach,	sdhci_pci_detach),
	DEVMETHOD(device_shutdown,	sdhci_pci_shutdown),
	DEVMETHOD(device_suspend,	sdhci_pci_suspend),
	DEVMETHOD(device_resume,	sdhci_pci_resume),

	/* Bus interface */
	DEVMETHOD(bus_read_ivar,	sdhci_generic_read_ivar),
	DEVMETHOD(bus_write_ivar,	sdhci_generic_write_ivar),

	/* mmcbr_if */
	DEVMETHOD(mmcbr_update_ios,	sdhci_generic_update_ios),
	DEVMETHOD(mmcbr_switch_vccq,	sdhci_generic_switch_vccq),
	DEVMETHOD(mmcbr_tune,		sdhci_generic_tune),
	DEVMETHOD(mmcbr_retune,		sdhci_generic_retune),
	DEVMETHOD(mmcbr_request,	sdhci_generic_request),
	DEVMETHOD(mmcbr_get_ro,		sdhci_generic_get_ro),
	DEVMETHOD(mmcbr_acquire_host,   sdhci_generic_acquire_host),
	DEVMETHOD(mmcbr_release_host,   sdhci_generic_release_host),

	/* SDHCI accessors */
	DEVMETHOD(sdhci_read_1,		sdhci_pci_read_1),
	DEVMETHOD(sdhci_read_2,		sdhci_pci_read_2),
	DEVMETHOD(sdhci_read_4,		sdhci_pci_read_4),
	DEVMETHOD(sdhci_read_multi_4,	sdhci_pci_read_multi_4),
	DEVMETHOD(sdhci_write_1,	sdhci_pci_write_1),
	DEVMETHOD(sdhci_write_2,	sdhci_pci_write_2),
	DEVMETHOD(sdhci_write_4,	sdhci_pci_write_4),
	DEVMETHOD(sdhci_write_multi_4,	sdhci_pci_write_multi_4),
	DEVMETHOD(sdhci_set_uhs_timing,	sdhci_generic_set_uhs_timing),

	DEVMETHOD_END
};

static driver_t sdhci_pci_driver = {
	"sdhci_pci",
	sdhci_methods,
	sizeof(struct sdhci_pci_softc),
};
static devclass_t sdhci_pci_devclass;

DRIVER_MODULE(sdhci_pci, pci, sdhci_pci_driver, sdhci_pci_devclass, NULL,
    NULL);
SDHCI_DEPEND(sdhci_pci);

#ifndef MMCCAM
MMC_DECLARE_BRIDGE(sdhci_pci);
#endif
