/*-
 * Copyright 2013-2015 John Wehle <john@feith.com>
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

#include "opt_global.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_platform.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/devmap.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/machdep.h>
#include <machine/platform.h>

#include <dev/fdt/fdt_common.h>

#include <arm/amlogic/aml8726/aml8726_soc.h>
#include <arm/amlogic/aml8726/aml8726_clkmsr.h>

#if defined(SOCDEV_PA) && defined(SOCDEV_VA)
vm_offset_t aml8726_aobus_kva_base = SOCDEV_VA;
#else
vm_offset_t aml8726_aobus_kva_base;
#endif

static void
aml8726_fixup_busfreq(void)
{
	phandle_t node;
	pcell_t freq, prop;
	ssize_t len;

	/*
	 * Set the bus-frequency for the SoC simple-bus if it
	 * needs updating (meaning the current frequency is zero).
	 */

	if ((freq = aml8726_clkmsr_bus_frequency()) == 0 ||
	    (node = OF_finddevice("/soc")) == 0 ||
	    fdt_is_compatible_strict(node, "simple-bus") == 0)
		while (1);

	freq = cpu_to_fdt32(freq);

	len = OF_getencprop(node, "bus-frequency", &prop, sizeof(prop));
	if ((len / sizeof(prop)) == 1 && prop == 0)
		OF_setprop(node, "bus-frequency", (void *)&freq, sizeof(freq));
}

vm_offset_t
platform_lastaddr(void)
{

	return (devmap_lastaddr());
}

void
platform_probe_and_attach(void)
{
}

void
platform_gpio_init(void)
{

	/*
	 * The UART console driver used for debugging early boot code
	 * needs to know the virtual base address of the aobus.  It's
	 * expected to equal SOCDEV_VA prior to initarm calling setttb
	 * ... afterwards it needs to be updated due to the new page
	 * tables.
	 *
	 * This means there's a deadzone in initarm between setttb
	 * and platform_gpio_init during which printf can't be used.
	 */
	aml8726_aobus_kva_base =
	    (vm_offset_t)devmap_ptov(0xc8100000, 0x100000);

	/*
	 * The hardware mux used by clkmsr is unique to the SoC (though
	 * currently clk81 is at a fixed location, however that might
	 * change in the future).
	 */
	aml8726_identify_soc();

	/*
	 * My aml8726-m3 development box which identifies the CPU as
	 * a Cortex A9-r2 rev 4 randomly locks up during boot when WFI
	 * is used.
	 */
	switch (aml8726_soc_hw_rev) {
	case AML_SOC_HW_REV_M3:
		cpufuncs.cf_sleep = (void *)cpufunc_nullop;
		break;
	default:
		break;
	}

	/*
	 * This FDT fixup should arguably be called through fdt_fixup_table,
	 * however currently there's no mechanism to specify a fixup which
	 * should always be invoked.
	 *
	 * It needs to be called prior to the console being initialized which
	 * is why it's called here, rather than from platform_late_init.
	 */
	aml8726_fixup_busfreq();
}

void
platform_late_init(void)
{
}

/*
 * Construct static devmap entries to map out the core
 * peripherals using 1mb section mappings.
 */
int
platform_devmap_init(void)
{

	devmap_add_entry(0xc1100000, 0x200000); /* cbus */
	devmap_add_entry(0xc4200000, 0x100000); /* pl310 */
	devmap_add_entry(0xc4300000, 0x100000); /* periph */
	devmap_add_entry(0xc8000000, 0x100000); /* apbbus */
	devmap_add_entry(0xc8100000, 0x100000); /* aobus */
	devmap_add_entry(0xc9000000, 0x800000); /* ahbbus */
	devmap_add_entry(0xd9000000, 0x100000); /* ahb */
	devmap_add_entry(0xda000000, 0x100000); /* secbus */

	return (0);
}

#ifndef INTRNG
#ifndef DEV_GIC
static int
fdt_pic_decode_ic(phandle_t node, pcell_t *intr, int *interrupt, int *trig,
    int *pol)
{

	/*
	 * The single core chips have just an Amlogic PIC.
	 */
	if (!fdt_is_compatible_strict(node, "amlogic,aml8726-pic"))
		return (ENXIO);

	*interrupt = fdt32_to_cpu(intr[1]);
	*trig = INTR_TRIGGER_EDGE;
	*pol = INTR_POLARITY_HIGH;

	return (0);
}
#endif

fdt_pic_decode_t fdt_pic_table[] = {
#ifdef DEV_GIC
	&gic_decode_fdt,
#else
	&fdt_pic_decode_ic,
#endif
	NULL
};
#endif /* INTRNG */
