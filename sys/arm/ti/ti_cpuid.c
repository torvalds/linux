/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011
 *	Ben Gray <ben.r.gray@gmail.com>.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/resource.h>
#include <sys/rman.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/fdt.h>
#include <machine/resource.h>
#include <machine/intr.h>

#include <dev/fdt/simplebus.h>
#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/ti/tivar.h>
#include <arm/ti/ti_cpuid.h>

#include <arm/ti/omap4/omap4_reg.h>
#include <arm/ti/am335x/am335x_reg.h>

#define OMAP4_STD_FUSE_DIE_ID_0    0x2200
#define OMAP4_ID_CODE              0x2204
#define OMAP4_STD_FUSE_DIE_ID_1    0x2208
#define OMAP4_STD_FUSE_DIE_ID_2    0x220C
#define OMAP4_STD_FUSE_DIE_ID_3    0x2210
#define OMAP4_STD_FUSE_PROD_ID_0   0x2214
#define OMAP4_STD_FUSE_PROD_ID_1   0x2218

#define OMAP3_ID_CODE              0xA204

static uint32_t chip_revision = 0xffffffff;

/**
 *	ti_revision - Returns the revision number of the device
 *
 *	Simply returns an identifier for the revision of the chip we are running
 *	on.
 *
 *	RETURNS
 *	A 32-bit identifier for the current chip
 */
uint32_t
ti_revision(void)
{
	return chip_revision;
}

/**
 *	omap4_get_revision - determines omap4 revision
 *
 *	Reads the registers to determine the revision of the chip we are currently
 *	running on.  Stores the information in global variables.
 *
 *
 */
static void
omap4_get_revision(void)
{
	uint32_t id_code;
	uint32_t revision;
	uint32_t hawkeye;
	bus_space_handle_t bsh;

	/* The chip revsion is read from the device identification registers and
	 * the JTAG (?) tap registers, which are located in address 0x4A00_2200 to
	 * 0x4A00_2218.  This is part of the L4_CORE memory range and should have
	 * been mapped in by the machdep.c code.
	 *
	 *   STD_FUSE_DIE_ID_0    0x4A00 2200
	 *   ID_CODE              0x4A00 2204   (this is the only one we need)
	 *   STD_FUSE_DIE_ID_1    0x4A00 2208
	 *   STD_FUSE_DIE_ID_2    0x4A00 220C
	 *   STD_FUSE_DIE_ID_3    0x4A00 2210
	 *   STD_FUSE_PROD_ID_0   0x4A00 2214
	 *   STD_FUSE_PROD_ID_1   0x4A00 2218
	 */
	/* FIXME Should we map somewhere else? */
	bus_space_map(fdtbus_bs_tag,OMAP44XX_L4_CORE_HWBASE, 0x4000, 0, &bsh);
	id_code = bus_space_read_4(fdtbus_bs_tag, bsh, OMAP4_ID_CODE);
	bus_space_unmap(fdtbus_bs_tag, bsh, 0x4000);

	hawkeye = ((id_code >> 12) & 0xffff);
	revision = ((id_code >> 28) & 0xf);

	/* Apparently according to the linux code there were some ES2.0 samples that
	 * have the wrong id code and report themselves as ES1.0 silicon.  So used
	 * the ARM cpuid to get the correct revision.
	 */
	if (revision == 0) {
		id_code = cp15_midr_get();
		revision = (id_code & 0xf) - 1;
	}

	switch (hawkeye) {
	case 0xB852:
		switch (revision) {
		case 0:
			chip_revision = OMAP4430_REV_ES1_0;
			break;
		case 1:
			chip_revision = OMAP4430_REV_ES2_1;
			break;
		default:
			chip_revision = OMAP4430_REV_UNKNOWN;
			break;
		}
		break;

	case 0xB95C:
		switch (revision) {
		case 3:
			chip_revision = OMAP4430_REV_ES2_1;
			break;
		case 4:
			chip_revision = OMAP4430_REV_ES2_2;
			break;
		case 6:
			chip_revision = OMAP4430_REV_ES2_3;
			break;
		default:
			chip_revision = OMAP4430_REV_UNKNOWN;
			break;
		}
		break;

	case 0xB94E:
		switch (revision) {
		case 0:
			chip_revision = OMAP4460_REV_ES1_0;
			break;
		case 2:
			chip_revision = OMAP4460_REV_ES1_1;
			break;
		default:
			chip_revision = OMAP4460_REV_UNKNOWN;
			break;
		}
		break;

	case 0xB975:
		switch (revision) {
		case 0:
			chip_revision = OMAP4470_REV_ES1_0;
			break;
		default:
			chip_revision = OMAP4470_REV_UNKNOWN;
			break;
		}
		break;

	default:
		/* Default to the latest revision if we can't determine type */
		chip_revision = OMAP_UNKNOWN_DEV;
		break;
	}
	if (chip_revision != OMAP_UNKNOWN_DEV) {
		printf("Texas Instruments OMAP%04x Processor, Revision ES%u.%u\n",
		    OMAP_REV_DEVICE(chip_revision), OMAP_REV_MAJOR(chip_revision), 
		    OMAP_REV_MINOR(chip_revision));
	}
	else {
		printf("Texas Instruments unknown OMAP chip: %04x, rev %d\n",
		    hawkeye, revision); 
	}
}

static void
am335x_get_revision(void)
{
	uint32_t dev_feature;
	char cpu_last_char;
	bus_space_handle_t bsh;
	int major;
	int minor;

	bus_space_map(fdtbus_bs_tag, AM335X_CONTROL_BASE, AM335X_CONTROL_SIZE, 0, &bsh);
	chip_revision = bus_space_read_4(fdtbus_bs_tag, bsh, AM335X_CONTROL_DEVICE_ID);
	dev_feature = bus_space_read_4(fdtbus_bs_tag, bsh, AM335X_CONTROL_DEV_FEATURE);
	bus_space_unmap(fdtbus_bs_tag, bsh, AM335X_CONTROL_SIZE);

	switch (dev_feature) {
		case 0x00FF0382:
			cpu_last_char='2';
			break;
		case 0x20FF0382:
			cpu_last_char='4';
			break;
		case 0x00FF0383:
			cpu_last_char='6';
			break;
		case 0x00FE0383:
			cpu_last_char='7';
			break;
		case 0x20FF0383:
			cpu_last_char='8';
			break;
		case 0x20FE0383:
			cpu_last_char='9';
			break;
		default:
			cpu_last_char='x';
	}

	switch(AM335X_DEVREV(chip_revision)) {
		case 0:
			major = 1;
			minor = 0;
			break;
		case 1:
			major = 2;
			minor = 0;
			break;
		case 2:
			major = 2;
			minor = 1;
			break;
		default:
			major = 0;
			minor = AM335X_DEVREV(chip_revision);
			break;
	}
	printf("Texas Instruments AM335%c Processor, Revision ES%u.%u\n",
		cpu_last_char, major, minor);
}

/**
 *	ti_cpu_ident - attempts to identify the chip we are running on
 *	@dummy: ignored
 *
 *	This function is called before any of the driver are initialised, however
 *	the basic virt to phys maps have been setup in machdep.c so we can still
 *	access the required registers, we just have to use direct register reads
 *	and writes rather than going through the bus stuff.
 *
 *
 */
static void
ti_cpu_ident(void *dummy)
{
	if (!ti_soc_is_supported())
		return;
	switch(ti_chip()) {
	case CHIP_OMAP_4:
		omap4_get_revision();
		break;
	case CHIP_AM335X:
		am335x_get_revision();
		break;
	default:
		panic("Unknown chip type, fixme!\n");
	}
}

SYSINIT(ti_cpu_ident, SI_SUB_CPU, SI_ORDER_SECOND, ti_cpu_ident, NULL);
