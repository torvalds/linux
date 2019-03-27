/*-
 * Copyright (c) 2017 Oleksandr Tymoshenko <gonzo@FreeBSD.org>
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

#include <machine/bus.h>
#include <machine/resource.h>

#include <contrib/dev/acpica/include/acpi.h>
#include <dev/acpica/acpivar.h>

#include <dev/mmc/bridge.h>
#include <dev/mmc/mmcreg.h>

#include <dev/sdhci/sdhci.h>

#include "mmcbr_if.h"
#include "sdhci_if.h"

#define	SDHCI_AMD_RESET_DLL_REG	0x908

static const struct sdhci_acpi_device {
	const char*	hid;
	int		uid;
	const char	*desc;
	u_int		quirks;
} sdhci_acpi_devices[] = {
	{ "80860F14",	1, "Intel Bay Trail/Braswell eMMC 4.5/4.5.1 Controller",
	    SDHCI_QUIRK_INTEL_POWER_UP_RESET |
	    SDHCI_QUIRK_WAIT_WHILE_BUSY |
	    SDHCI_QUIRK_CAPS_BIT63_FOR_MMC_HS400 |
	    SDHCI_QUIRK_PRESET_VALUE_BROKEN },
	{ "80860F14",	3, "Intel Bay Trail/Braswell SDXC Controller",
	    SDHCI_QUIRK_WAIT_WHILE_BUSY |
	    SDHCI_QUIRK_PRESET_VALUE_BROKEN },
	{ "80860F16",	0, "Intel Bay Trail/Braswell SDXC Controller",
	    SDHCI_QUIRK_WAIT_WHILE_BUSY |
	    SDHCI_QUIRK_PRESET_VALUE_BROKEN },
	{ "80865ACA",	0, "Intel Apollo Lake SDXC Controller",
	    SDHCI_QUIRK_BROKEN_DMA |	/* APL18 erratum */
	    SDHCI_QUIRK_WAIT_WHILE_BUSY |
	    SDHCI_QUIRK_PRESET_VALUE_BROKEN },
	{ "80865ACC",	0, "Intel Apollo Lake eMMC 5.0 Controller",
	    SDHCI_QUIRK_BROKEN_DMA |	/* APL18 erratum */
	    SDHCI_QUIRK_INTEL_POWER_UP_RESET |
	    SDHCI_QUIRK_WAIT_WHILE_BUSY |
	    SDHCI_QUIRK_MMC_DDR52 |
	    SDHCI_QUIRK_CAPS_BIT63_FOR_MMC_HS400 |
	    SDHCI_QUIRK_PRESET_VALUE_BROKEN },
	{ "AMDI0040",	0, "AMD eMMC 5.0 Controller",
	    SDHCI_QUIRK_32BIT_DMA_SIZE |
	    SDHCI_QUIRK_MMC_HS400_IF_CAN_SDR104 },
	{ NULL, 0, NULL, 0}
};

static char *sdhci_ids[] = {
	"80860F14",
	"80860F16",
	"80865ACA",
	"80865ACC",
	"AMDI0040",
	NULL
};

struct sdhci_acpi_softc {
	struct sdhci_slot slot;
	struct resource	*mem_res;	/* Memory resource */
	struct resource *irq_res;	/* IRQ resource */
	void		*intrhand;	/* Interrupt handle */
	const struct sdhci_acpi_device *acpi_dev;
};

static void sdhci_acpi_intr(void *arg);
static int sdhci_acpi_detach(device_t dev);

static uint8_t
sdhci_acpi_read_1(device_t dev, struct sdhci_slot *slot __unused,
    bus_size_t off)
{
	struct sdhci_acpi_softc *sc = device_get_softc(dev);

	bus_barrier(sc->mem_res, 0, 0xFF,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	return bus_read_1(sc->mem_res, off);
}

static void
sdhci_acpi_write_1(device_t dev, struct sdhci_slot *slot __unused,
    bus_size_t off, uint8_t val)
{
	struct sdhci_acpi_softc *sc = device_get_softc(dev);

	bus_barrier(sc->mem_res, 0, 0xFF,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	bus_write_1(sc->mem_res, off, val);
}

static uint16_t
sdhci_acpi_read_2(device_t dev, struct sdhci_slot *slot __unused,
    bus_size_t off)
{
	struct sdhci_acpi_softc *sc = device_get_softc(dev);

	bus_barrier(sc->mem_res, 0, 0xFF,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	return bus_read_2(sc->mem_res, off);
}

static void
sdhci_acpi_write_2(device_t dev, struct sdhci_slot *slot __unused,
    bus_size_t off, uint16_t val)
{
	struct sdhci_acpi_softc *sc = device_get_softc(dev);

	bus_barrier(sc->mem_res, 0, 0xFF,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	bus_write_2(sc->mem_res, off, val);
}

static uint32_t
sdhci_acpi_read_4(device_t dev, struct sdhci_slot *slot __unused,
    bus_size_t off)
{
	struct sdhci_acpi_softc *sc = device_get_softc(dev);

	bus_barrier(sc->mem_res, 0, 0xFF,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	return bus_read_4(sc->mem_res, off);
}

static void
sdhci_acpi_write_4(device_t dev, struct sdhci_slot *slot __unused,
    bus_size_t off, uint32_t val)
{
	struct sdhci_acpi_softc *sc = device_get_softc(dev);

	bus_barrier(sc->mem_res, 0, 0xFF,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	bus_write_4(sc->mem_res, off, val);
}

static void
sdhci_acpi_read_multi_4(device_t dev, struct sdhci_slot *slot __unused,
    bus_size_t off, uint32_t *data, bus_size_t count)
{
	struct sdhci_acpi_softc *sc = device_get_softc(dev);

	bus_read_multi_stream_4(sc->mem_res, off, data, count);
}

static void
sdhci_acpi_write_multi_4(device_t dev, struct sdhci_slot *slot __unused,
    bus_size_t off, uint32_t *data, bus_size_t count)
{
	struct sdhci_acpi_softc *sc = device_get_softc(dev);

	bus_write_multi_stream_4(sc->mem_res, off, data, count);
}

static void
sdhci_acpi_set_uhs_timing(device_t dev, struct sdhci_slot *slot)
{
	const struct sdhci_acpi_softc *sc;
	const struct sdhci_acpi_device *acpi_dev;
	const struct mmc_ios *ios;
	device_t bus;
	uint16_t old_timing;
	enum mmc_bus_timing timing;

	bus = slot->bus;
	old_timing = SDHCI_READ_2(bus, slot, SDHCI_HOST_CONTROL2);
	old_timing &= SDHCI_CTRL2_UHS_MASK;
	sdhci_generic_set_uhs_timing(dev, slot);

	sc = device_get_softc(dev);
	acpi_dev = sc->acpi_dev;
	/*
	 * AMDI0040 controllers require SDHCI_CTRL2_SAMPLING_CLOCK to be
	 * disabled when switching from HS200 to high speed and to always
	 * be turned on again when tuning for HS400.  In the later case,
	 * an AMD-specific DLL reset additionally is needed.
	 */
	if (strcmp(acpi_dev->hid, "AMDI0040") == 0 && acpi_dev->uid == 0) {
		ios = &slot->host.ios;
		timing = ios->timing;
		if (old_timing == SDHCI_CTRL2_UHS_SDR104 &&
		    timing == bus_timing_hs)
			SDHCI_WRITE_2(bus, slot, SDHCI_HOST_CONTROL2,
			    SDHCI_READ_2(bus, slot, SDHCI_HOST_CONTROL2) &
			    ~SDHCI_CTRL2_SAMPLING_CLOCK);
		if (ios->clock > SD_SDR50_MAX &&
		    old_timing != SDHCI_CTRL2_MMC_HS400 &&
		    timing == bus_timing_mmc_hs400) {
			SDHCI_WRITE_2(bus, slot, SDHCI_HOST_CONTROL2,
			    SDHCI_READ_2(bus, slot, SDHCI_HOST_CONTROL2) |
			    SDHCI_CTRL2_SAMPLING_CLOCK);
			SDHCI_WRITE_4(bus, slot, SDHCI_AMD_RESET_DLL_REG,
			    0x40003210);
			DELAY(20);
			SDHCI_WRITE_4(bus, slot, SDHCI_AMD_RESET_DLL_REG,
			    0x40033210);
		}
	}
}

static const struct sdhci_acpi_device *
sdhci_acpi_find_device(device_t dev)
{
	char *hid;
	int i, uid;
	ACPI_HANDLE handle;
	ACPI_STATUS status;
	int rv;

	rv = ACPI_ID_PROBE(device_get_parent(dev), dev, sdhci_ids, &hid);
	if (rv > 0)
		return (NULL);

	handle = acpi_get_handle(dev);
	status = acpi_GetInteger(handle, "_UID", &uid);
	if (ACPI_FAILURE(status))
		uid = 0;

	for (i = 0; sdhci_acpi_devices[i].hid != NULL; i++) {
		if (strcmp(sdhci_acpi_devices[i].hid, hid) != 0)
			continue;
		if ((sdhci_acpi_devices[i].uid != 0) &&
		    (sdhci_acpi_devices[i].uid != uid))
			continue;
		return (&sdhci_acpi_devices[i]);
	}

	return (NULL);
}

static int
sdhci_acpi_probe(device_t dev)
{
	const struct sdhci_acpi_device *acpi_dev;

	acpi_dev = sdhci_acpi_find_device(dev);
	if (acpi_dev == NULL)
		return (ENXIO);

	device_set_desc(dev, acpi_dev->desc);

	return (BUS_PROBE_DEFAULT);
}

static int
sdhci_acpi_attach(device_t dev)
{
	struct sdhci_acpi_softc *sc = device_get_softc(dev);
	int rid, err;
	u_int quirks;
	const struct sdhci_acpi_device *acpi_dev;

	acpi_dev = sdhci_acpi_find_device(dev);
	if (acpi_dev == NULL)
		return (ENXIO);

	sc->acpi_dev = acpi_dev;
	quirks = acpi_dev->quirks;

	/* Allocate IRQ. */
	rid = 0;
	sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
		RF_ACTIVE);
	if (sc->irq_res == NULL) {
		device_printf(dev, "can't allocate IRQ\n");
		return (ENOMEM);
	}

	rid = 0;
	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &rid, RF_ACTIVE);
	if (sc->mem_res == NULL) {
		device_printf(dev, "can't allocate memory resource for slot\n");
		sdhci_acpi_detach(dev);
		return (ENOMEM);
	}

	/*
	 * Intel Bay Trail and Braswell eMMC controllers share the same IDs,
	 * but while with these former DDR52 is affected by the VLI54 erratum,
	 * these latter require the timeout clock to be hardcoded to 1 MHz.
	 */
	if (strcmp(acpi_dev->hid, "80860F14") == 0 && acpi_dev->uid == 1 &&
	    SDHCI_READ_4(dev, &sc->slot, SDHCI_CAPABILITIES) == 0x446cc8b2 &&
	    SDHCI_READ_4(dev, &sc->slot, SDHCI_CAPABILITIES2) == 0x00000807)
		quirks |= SDHCI_QUIRK_MMC_DDR52 | SDHCI_QUIRK_DATA_TIMEOUT_1MHZ;
	quirks &= ~sdhci_quirk_clear;
	quirks |= sdhci_quirk_set;
	sc->slot.quirks = quirks;

	err = sdhci_init_slot(dev, &sc->slot, 0);
	if (err) {
		device_printf(dev, "failed to init slot\n");
		sdhci_acpi_detach(dev);
		return (err);
	}

	/* Activate the interrupt */
	err = bus_setup_intr(dev, sc->irq_res, INTR_TYPE_MISC | INTR_MPSAFE,
	    NULL, sdhci_acpi_intr, sc, &sc->intrhand);
	if (err) {
		device_printf(dev, "can't setup IRQ\n");
		sdhci_acpi_detach(dev);
		return (err);
	}

	/* Process cards detection. */
	sdhci_start_slot(&sc->slot);

	return (0);
}

static int
sdhci_acpi_detach(device_t dev)
{
	struct sdhci_acpi_softc *sc = device_get_softc(dev);

	if (sc->intrhand)
		bus_teardown_intr(dev, sc->irq_res, sc->intrhand);
	if (sc->irq_res)
		bus_release_resource(dev, SYS_RES_IRQ,
		    rman_get_rid(sc->irq_res), sc->irq_res);

	if (sc->mem_res) {
		sdhci_cleanup_slot(&sc->slot);
		bus_release_resource(dev, SYS_RES_MEMORY,
		    rman_get_rid(sc->mem_res), sc->mem_res);
	}

	return (0);
}

static int
sdhci_acpi_shutdown(device_t dev)
{

	return (0);
}

static int
sdhci_acpi_suspend(device_t dev)
{
	struct sdhci_acpi_softc *sc = device_get_softc(dev);
	int err;

	err = bus_generic_suspend(dev);
	if (err)
		return (err);
	sdhci_generic_suspend(&sc->slot);
	return (0);
}

static int
sdhci_acpi_resume(device_t dev)
{
	struct sdhci_acpi_softc *sc = device_get_softc(dev);
	int err;

	sdhci_generic_resume(&sc->slot);
	err = bus_generic_resume(dev);
	if (err)
		return (err);
	return (0);
}

static void
sdhci_acpi_intr(void *arg)
{
	struct sdhci_acpi_softc *sc = (struct sdhci_acpi_softc *)arg;

	sdhci_generic_intr(&sc->slot);
}

static device_method_t sdhci_methods[] = {
	/* device_if */
	DEVMETHOD(device_probe, sdhci_acpi_probe),
	DEVMETHOD(device_attach, sdhci_acpi_attach),
	DEVMETHOD(device_detach, sdhci_acpi_detach),
	DEVMETHOD(device_shutdown, sdhci_acpi_shutdown),
	DEVMETHOD(device_suspend, sdhci_acpi_suspend),
	DEVMETHOD(device_resume, sdhci_acpi_resume),

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
	DEVMETHOD(sdhci_read_1,		sdhci_acpi_read_1),
	DEVMETHOD(sdhci_read_2,		sdhci_acpi_read_2),
	DEVMETHOD(sdhci_read_4,		sdhci_acpi_read_4),
	DEVMETHOD(sdhci_read_multi_4,	sdhci_acpi_read_multi_4),
	DEVMETHOD(sdhci_write_1,	sdhci_acpi_write_1),
	DEVMETHOD(sdhci_write_2,	sdhci_acpi_write_2),
	DEVMETHOD(sdhci_write_4,	sdhci_acpi_write_4),
	DEVMETHOD(sdhci_write_multi_4,	sdhci_acpi_write_multi_4),
	DEVMETHOD(sdhci_set_uhs_timing,	sdhci_acpi_set_uhs_timing),

	DEVMETHOD_END
};

static driver_t sdhci_acpi_driver = {
	"sdhci_acpi",
	sdhci_methods,
	sizeof(struct sdhci_acpi_softc),
};
static devclass_t sdhci_acpi_devclass;

DRIVER_MODULE(sdhci_acpi, acpi, sdhci_acpi_driver, sdhci_acpi_devclass, NULL,
    NULL);
SDHCI_DEPEND(sdhci_acpi);

#ifndef MMCCAM
MMC_DECLARE_BRIDGE(sdhci_acpi);
#endif
