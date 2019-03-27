/*-
 * Copyright (c) 2017, Adrian Chadd <adrian@FreeBSD.org>.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ar71xx.h"

#include <sys/param.h>
#include <sys/systm.h>

#include <sys/bus.h>
#include <sys/interrupt.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/lock.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr_machdep.h>

#include <sys/linker.h>
#include <sys/firmware.h>

struct ar71xx_caldata_softc {
	device_t		sc_dev;
};

static int
ar71xx_caldata_probe(device_t dev)
{

	return (BUS_PROBE_NOWILDCARD);
}

/* XXX TODO: unify with what's in ar71xx_fixup.c */

/*
 * Create a calibration block from memory mapped SPI data for use by
 * various drivers.  Right now it's just ath(4) but later board support
 * will include 802.11ac NICs with calibration data in NOR flash.
 *
 * (Yes, there are a handful of QCA MIPS boards with QCA9880v2 802.11ac chips
 * with calibration data in flash..)
 */
static void
ar71xx_platform_create_cal_data(device_t dev, int id, long int flash_addr,
    int size)
{
	char buf[64];
	uint16_t *cal_data = (uint16_t *) MIPS_PHYS_TO_KSEG1(flash_addr);
	void *eeprom = NULL;
	const struct firmware *fw = NULL;

	device_printf(dev, "EEPROM firmware: 0x%lx @ %d bytes\n",
	    flash_addr, size);

	eeprom = malloc(size, M_DEVBUF, M_WAITOK | M_ZERO);
	if (! eeprom) {
		device_printf(dev, "%s: malloc failed for '%s', aborting EEPROM\n",
		__func__, buf);
		return;
	}

	memcpy(eeprom, cal_data, size);

	/*
	 * Generate a flash EEPROM 'firmware' from the given memory
	 * region.  Since the SPI controller will eventually
	 * go into port-IO mode instead of memory-mapped IO
	 * mode, a copy of the EEPROM contents is required.
	 */

	snprintf(buf, sizeof(buf), "%s.%d.map.%d.eeprom_firmware",
	    device_get_name(dev),
	    device_get_unit(dev),
	    id);

	fw = firmware_register(buf, eeprom, size, 1, NULL);
	if (fw == NULL) {
		device_printf(dev, "%s: firmware_register (%s) failed\n",
		    __func__, buf);
		free(eeprom, M_DEVBUF);
		return;
	}
	device_printf(dev, "device EEPROM '%s' registered\n", buf);
}

/*
 * Iterate through a list of early-boot hints creating calibration
 * data firmware chunks for AHB (ie, non-PCI) devices with calibration
 * data.
 */
static int
ar71xx_platform_check_eeprom_hints(device_t dev)
{
	char buf[64];
	long int addr;
	int size;
	int i;

	for (i = 0; i < 8; i++) {
		snprintf(buf, sizeof(buf), "map.%d.ath_fixup_addr", i);
		if (resource_long_value(device_get_name(dev),
		    device_get_unit(dev), buf, &addr) != 0)
			break;
		snprintf(buf, sizeof(buf), "map.%d.ath_fixup_size", i);
		if (resource_int_value(device_get_name(dev),
		    device_get_unit(dev), buf, &size) != 0)
			break;
		device_printf(dev, "map.%d.ath_fixup_addr=0x%08x; size=%d\n",
		    i, (int) addr, size);
		(void) ar71xx_platform_create_cal_data(dev, i, addr, size);
        }

        return (0);
}

static int
ar71xx_caldata_attach(device_t dev)
{

	device_add_child(dev, "nexus", -1);
	ar71xx_platform_check_eeprom_hints(dev);
	return (bus_generic_attach(dev));
}

static device_method_t ar71xx_caldata_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ar71xx_caldata_probe),
	DEVMETHOD(device_attach,	ar71xx_caldata_attach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),
	DEVMETHOD_END
};

static driver_t ar71xx_caldata_driver = {
	"ar71xx_caldata",
	ar71xx_caldata_methods,
	sizeof(struct ar71xx_caldata_softc),
};

static devclass_t ar71xx_caldata_devclass;

DRIVER_MODULE(ar71xx_caldata, nexus, ar71xx_caldata_driver, ar71xx_caldata_devclass, 0, 0);
