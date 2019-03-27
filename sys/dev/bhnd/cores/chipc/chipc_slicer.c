/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2016 Michael Zhilin <mizhka@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Slicer is required to split firmware images into pieces.
 * The first supported FW is TRX-based used by Asus routers
 * TODO: add NetGear FW (CHK)
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/errno.h>
#include <sys/rman.h>
#include <sys/bus.h>
#include <sys/systm.h>
#include <sys/slicer.h>

#include <machine/bus.h>

#include <dev/bhnd/bhnd_debug.h>

#include "chipc_slicer.h"

#include <dev/cfi/cfi_var.h>
#include "chipc_spi.h"

static int	chipc_slicer_walk(device_t dev, struct resource *res,
		    struct flash_slice *slices, int *nslices);

void
chipc_register_slicer(chipc_flash flash_type)
{
	switch (flash_type) {
	case CHIPC_SFLASH_AT:
	case CHIPC_SFLASH_ST:
		flash_register_slicer(chipc_slicer_spi, FLASH_SLICES_TYPE_SPI,
		   TRUE);
		break;
	case CHIPC_PFLASH_CFI:
		flash_register_slicer(chipc_slicer_cfi, FLASH_SLICES_TYPE_CFI,
		   TRUE);
		break;
	default:
		/* Unsupported */
		break;
	}
}

int
chipc_slicer_cfi(device_t dev, const char *provider __unused,
    struct flash_slice *slices, int *nslices)
{
	struct cfi_softc	*sc;
	device_t		 parent;

	/* must be CFI flash */
	if (device_get_devclass(dev) != devclass_find("cfi"))
		return (ENXIO);

	/* must be attached to chipc */
	if ((parent = device_get_parent(dev)) == NULL) {
		BHND_ERROR_DEV(dev, "no found ChipCommon device");
		return (ENXIO);
	}

	if (device_get_devclass(parent) != devclass_find("bhnd_chipc")) {
		BHND_ERROR_DEV(dev, "no found ChipCommon device");
		return (ENXIO);
	}

	sc = device_get_softc(dev);
	return (chipc_slicer_walk(dev, sc->sc_res, slices, nslices));
}

int
chipc_slicer_spi(device_t dev, const char *provider __unused,
    struct flash_slice *slices, int *nslices)
{
	struct chipc_spi_softc	*sc;
	device_t		 chipc, spi, spibus;

	BHND_DEBUG_DEV(dev, "initting SPI slicer: %s", device_get_name(dev));

	/* must be SPI-attached flash */
	spibus = device_get_parent(dev);
	if (spibus == NULL) {
		BHND_ERROR_DEV(dev, "no found ChipCommon SPI BUS device");
		return (ENXIO);
	}

	spi = device_get_parent(spibus);
	if (spi == NULL) {
		BHND_ERROR_DEV(dev, "no found ChipCommon SPI device");
		return (ENXIO);
	}

	chipc = device_get_parent(spi);
	if (device_get_devclass(chipc) != devclass_find("bhnd_chipc")) {
		BHND_ERROR_DEV(dev, "no found ChipCommon device");
		return (ENXIO);
	}

	sc = device_get_softc(spi);
	return (chipc_slicer_walk(dev, sc->sc_flash_res, slices, nslices));
}

/*
 * Main processing part
 */
static int
chipc_slicer_walk(device_t dev, struct resource *res,
    struct flash_slice *slices, int *nslices)
{
	uint32_t	 fw_len;
	uint32_t	 fs_ofs;
	uint32_t	 val;
	uint32_t	 ofs_trx;
	int		 flash_size;

	*nslices = 0;

	flash_size = rman_get_size(res);
	ofs_trx = flash_size;

	BHND_TRACE_DEV(dev, "slicer: scanning memory [%x bytes] for headers...",
	    flash_size);

	/* Find FW header in flash memory with step=128Kb (0x1000) */
	for(uint32_t ofs = 0; ofs < flash_size; ofs+= 0x1000){
		val = bus_read_4(res, ofs);
		switch (val) {
		case TRX_MAGIC:
			/* check for second TRX */
			if (ofs_trx < ofs) {
				BHND_TRACE_DEV(dev, "stop on 2nd TRX: %x", ofs);
				break;
			}

			BHND_TRACE("TRX found: %x", ofs);
			ofs_trx = ofs;
			/* read last offset of TRX header */
			fs_ofs = bus_read_4(res, ofs + 24);
			BHND_TRACE("FS offset: %x", fs_ofs);

			/*
			 * GEOM IO will panic if offset is not aligned
			 * on sector size, i.e. 512 bytes
			 */
			if (fs_ofs % 0x200 != 0) {
				BHND_WARN("WARNING! filesystem offset should be"
				    " aligned on sector size (%d bytes)", 0x200);
				BHND_WARN("ignoring TRX firmware image");
				break;
			}

			slices[*nslices].base = ofs + fs_ofs;
			//XXX: fully sized? any other partition?
			fw_len = bus_read_4(res, ofs + 4);
			slices[*nslices].size = fw_len - fs_ofs;
			slices[*nslices].label = "rootfs";
			*nslices += 1;
			break;
		case CFE_MAGIC:
			BHND_TRACE("CFE found: %x", ofs);
			break;
		case NVRAM_MAGIC:
			BHND_TRACE("NVRAM found: %x", ofs);
			break;
		default:
			break;
		}
	}

	BHND_TRACE("slicer: done");
	return (0);
}
