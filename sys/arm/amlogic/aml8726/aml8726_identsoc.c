/*-
 * Copyright 2015 John Wehle <john@feith.com>
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
 *
 */

/*
 * Amlogic aml8726 SoC identification.
 *
 * The SoC identification is used by some of the drivers in order to
 * handle hardware differences so the identification needs to happen
 * early in the boot process (e.g. before SMP startup).
 *
 * It's expected that the register addresses for identifying the SoC
 * are set in stone.
 *
 * Currently missing an entry for the aml8726-m and doesn't distinguish
 * between the m801, m802, m805, s802, s805, and s812 which are all
 * variations of the aml8726-m8.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/clock.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/resource.h>
#include <sys/rman.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <arm/amlogic/aml8726/aml8726_soc.h>

uint32_t aml8726_soc_hw_rev = AML_SOC_HW_REV_UNKNOWN;
uint32_t aml8726_soc_metal_rev = AML_SOC_METAL_REV_UNKNOWN;

static const struct {
	uint32_t hw_rev;
	char *desc;
} aml8726_soc_desc[] = {
	{ AML_SOC_HW_REV_M3,	"aml8726-m3" },
	{ AML_SOC_HW_REV_M6,	"aml8726-m6" },
	{ AML_SOC_HW_REV_M6TV,	"aml8726-m6tv" },
	{ AML_SOC_HW_REV_M6TVL,	"aml8726-m6tvl" },
	{ AML_SOC_HW_REV_M8,	"aml8726-m8" },
	{ AML_SOC_HW_REV_M8B,	"aml8726-m8b" },
	{ 0xff, NULL }
};

static const struct {
	uint32_t metal_rev;
	char *desc;
} aml8726_m8_soc_rev[] = {
	{ AML_SOC_M8_METAL_REV_A,	"A" },
	{ AML_SOC_M8_METAL_REV_M2_A,	"MarkII A" },
	{ AML_SOC_M8_METAL_REV_B,	"B" },
	{ AML_SOC_M8_METAL_REV_C,	"C" },
	{ 0xff, NULL }
};

void
aml8726_identify_soc(void)
{
	int err;
	struct resource res;

	memset(&res, 0, sizeof(res));

	res.r_bustag = fdtbus_bs_tag;

	err = bus_space_map(res.r_bustag, AML_SOC_CBUS_BASE_ADDR, 0x100000,
	    0, &res.r_bushandle);

	if (err)
		panic("Could not allocate resource for SoC identification\n");

	aml8726_soc_hw_rev = bus_read_4(&res, AML_SOC_HW_REV_REG);

	aml8726_soc_metal_rev = bus_read_4(&res, AML_SOC_METAL_REV_REG);

	bus_space_unmap(res.r_bustag, res.r_bushandle, 0x100000);
}

static void
aml8726_identify_announce_soc(void *dummy)
{
	int i;

	for (i = 0; aml8726_soc_desc[i].desc; i++)
		if (aml8726_soc_desc[i].hw_rev == aml8726_soc_hw_rev)
			break;

	if (aml8726_soc_desc[i].desc == NULL)
		panic("Amlogic unknown aml8726 SoC %#x\n", aml8726_soc_hw_rev);

	printf("Amlogic %s SoC", aml8726_soc_desc[i].desc);

	if (aml8726_soc_hw_rev == AML_SOC_HW_REV_M8) {
		for (i = 0; aml8726_m8_soc_rev[i].desc; i++)
			if (aml8726_m8_soc_rev[i].metal_rev ==
			    aml8726_soc_metal_rev)
				break;

		if (aml8726_m8_soc_rev[i].desc == NULL)
			printf(", unknown rev %#x", aml8726_soc_metal_rev);
		else
			printf(", rev %s", aml8726_m8_soc_rev[i].desc);
	}

	printf("\n");
}

SYSINIT(aml8726_identify_announce_soc, SI_SUB_CPU, SI_ORDER_SECOND,
    aml8726_identify_announce_soc, NULL);
