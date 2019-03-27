/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Ian Lepore <ian@freebsd.org>
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

#include "opt_platform.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/reboot.h>
#include <sys/devmap.h>

#include <vm/vm.h>

#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/machdep.h>
#include <machine/platformvar.h>

#include <arm/arm/mpcore_timervar.h>
#include <arm/freescale/imx/imx6_anatopreg.h>
#include <arm/freescale/imx/imx6_anatopvar.h>
#include <arm/freescale/imx/imx_machdep.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>

#include <arm/freescale/imx/imx6_machdep.h>

#include "platform_if.h"
#include "platform_pl310_if.h"

static platform_attach_t imx6_attach;
static platform_devmap_init_t imx6_devmap_init;
static platform_late_init_t imx6_late_init;
static platform_cpu_reset_t imx6_cpu_reset;

/*
 * Fix FDT data related to interrupts.
 *
 * Driven by the needs of linux and its drivers (as always), the published FDT
 * data for imx6 now sets the interrupt parent for most devices to the GPC
 * interrupt controller, which is for use when the chip is in deep-sleep mode.
 * We don't support deep sleep or have a GPC-PIC driver; we need all interrupts
 * to be handled by the GIC.
 *
 * Luckily, the change to the FDT data was to assign the GPC as the interrupt
 * parent for the soc node and letting that get inherited by all other devices
 * (except a few that directly name GIC as their interrupt parent).  So we can
 * set the world right by just changing the interrupt-parent property of the soc
 * node to refer to GIC instead of GPC.  This will get us by until we write our
 * own GPC driver (or until linux changes its mind and the FDT data again).
 *
 * We validate that we have data that looks like we expect before changing it:
 *  - SOC node exists and has GPC as its interrupt parent.
 *  - GPC node exists and has GIC as its interrupt parent.
 *  - GIC node exists and is its own interrupt parent or has no parent.
 *
 * This applies to all models of imx6.  Luckily all of them have the devices
 * involved at the same addresses on the same buses, so we don't need any
 * per-soc logic.  We handle this at platform attach time rather than via the
 * fdt_fixup_table, because the latter requires matching on the FDT "model"
 * property, and this applies to all boards including those not yet invented.
 *
 * This just in:  as of the import of dts files from linux 4.15 on 2018-02-10,
 * they appear to have applied a new style rule to the dts which forbids leading
 * zeroes in the @address qualifiers on node names.  Since we have to find those
 * nodes by string matching we now have to search for both flavors of each node
 * name involved.
 */
static void
fix_fdt_interrupt_data(void)
{
	phandle_t gicipar, gicnode, gicxref;
	phandle_t gpcipar, gpcnode, gpcxref;
	phandle_t socipar, socnode;
	int result;

	socnode = OF_finddevice("/soc");
	if (socnode == -1)
	    return;
	result = OF_getencprop(socnode, "interrupt-parent", &socipar,
	    sizeof(socipar));
	if (result <= 0)
		return;

	/* GIC node may be child of soc node, or appear directly at root. */
	gicnode = OF_finddevice("/soc/interrupt-controller@00a01000");
	if (gicnode == -1)
		gicnode = OF_finddevice("/soc/interrupt-controller@a01000");
	if (gicnode == -1) {
		gicnode = OF_finddevice("/interrupt-controller@00a01000");
		if (gicnode == -1)
			gicnode = OF_finddevice("/interrupt-controller@a01000");
		if (gicnode == -1)
			return;
	}
	gicxref = OF_xref_from_node(gicnode);

	/* If gic node has no parent, pretend it is its own parent. */
	result = OF_getencprop(gicnode, "interrupt-parent", &gicipar,
	    sizeof(gicipar));
	if (result <= 0)
		gicipar = gicxref;

	gpcnode = OF_finddevice("/soc/aips-bus@02000000/gpc@020dc000");
	if (gpcnode == -1)
		gpcnode = OF_finddevice("/soc/aips-bus@2000000/gpc@20dc000");
	if (gpcnode == -1)
		return;
	result = OF_getencprop(gpcnode, "interrupt-parent", &gpcipar,
	    sizeof(gpcipar));
	if (result <= 0)
		return;
	gpcxref = OF_xref_from_node(gpcnode);

	if (socipar != gpcxref || gpcipar != gicxref || gicipar != gicxref)
		return;

	gicxref = cpu_to_fdt32(gicxref);
	OF_setprop(socnode, "interrupt-parent", &gicxref, sizeof(gicxref));
}

static int
imx6_attach(platform_t plat)
{

	/* Fix soc interrupt-parent property. */
	fix_fdt_interrupt_data();

	/* Inform the MPCore timer driver that its clock is variable. */
	arm_tmr_change_frequency(ARM_TMR_FREQUENCY_VARIES);

	return (0);
}

static void
imx6_late_init(platform_t plat)
{
	const uint32_t IMX6_WDOG_SR_PHYS = 0x020bc004;

	imx_wdog_init_last_reset(IMX6_WDOG_SR_PHYS);
}

/*
 * Set up static device mappings.
 *
 * This attempts to cover the most-used devices with 1MB section mappings, which
 * is good for performance (uses fewer TLB entries for device access).
 *
 * ARMMP covers the interrupt controller, MPCore timers, global timer, and the
 * L2 cache controller.  Most of the 1MB range is unused reserved space.
 *
 * AIPS1/AIPS2 cover most of the on-chip devices such as uart, spi, i2c, etc.
 *
 * Notably not mapped right now are HDMI, GPU, and other devices below ARMMP in
 * the memory map.  When we get support for graphics it might make sense to
 * static map some of that area.  Be careful with other things in that area such
 * as OCRAM that probably shouldn't be mapped as VM_MEMATTR_DEVICE memory.
 */
static int
imx6_devmap_init(platform_t plat)
{
	const uint32_t IMX6_ARMMP_PHYS = 0x00a00000;
	const uint32_t IMX6_ARMMP_SIZE = 0x00100000;
	const uint32_t IMX6_AIPS1_PHYS = 0x02000000;
	const uint32_t IMX6_AIPS1_SIZE = 0x00100000;
	const uint32_t IMX6_AIPS2_PHYS = 0x02100000;
	const uint32_t IMX6_AIPS2_SIZE = 0x00100000;

	devmap_add_entry(IMX6_ARMMP_PHYS, IMX6_ARMMP_SIZE);
	devmap_add_entry(IMX6_AIPS1_PHYS, IMX6_AIPS1_SIZE);
	devmap_add_entry(IMX6_AIPS2_PHYS, IMX6_AIPS2_SIZE);

	return (0);
}

static void
imx6_cpu_reset(platform_t plat)
{
	const uint32_t IMX6_WDOG_CR_PHYS = 0x020bc000;

	imx_wdog_cpu_reset(IMX6_WDOG_CR_PHYS);
}

/*
 * Determine what flavor of imx6 we're running on.
 *
 * This code is based on the way u-boot does it.  Information found on the web
 * indicates that Freescale themselves were the original source of this logic,
 * including the strange check for number of CPUs in the SCU configuration
 * register, which is apparently needed on some revisions of the SOLO.
 *
 * According to the documentation, there is such a thing as an i.MX6 Dual
 * (non-lite flavor).  However, Freescale doesn't seem to have assigned it a
 * number or provided any logic to handle it in their detection code.
 *
 * Note that the ANALOG_DIGPROG and SCU configuration registers are not
 * documented in the chip reference manual.  (SCU configuration is mentioned,
 * but not mapped out in detail.)  I think the bottom two bits of the scu config
 * register may be ncpu-1.
 *
 * This hasn't been tested yet on a dual[-lite].
 *
 * On a solo:
 *      digprog    = 0x00610001
 *      hwsoc      = 0x00000062
 *      scu config = 0x00000500
 * On a quad:
 *      digprog    = 0x00630002
 *      hwsoc      = 0x00000063
 *      scu config = 0x00005503
 */
u_int
imx_soc_type(void)
{
	uint32_t digprog, hwsoc;
	uint32_t *pcr;
	static u_int soctype;
	const vm_offset_t SCU_CONFIG_PHYSADDR = 0x00a00004;
#define	HWSOC_MX6SL	0x60
#define	HWSOC_MX6DL	0x61
#define	HWSOC_MX6SOLO	0x62
#define	HWSOC_MX6Q	0x63
#define	HWSOC_MX6UL	0x64

	if (soctype != 0)
		return (soctype);

	digprog = imx6_anatop_read_4(IMX6_ANALOG_DIGPROG_SL);
	hwsoc = (digprog >> IMX6_ANALOG_DIGPROG_SOCTYPE_SHIFT) & 
	    IMX6_ANALOG_DIGPROG_SOCTYPE_MASK;

	if (hwsoc != HWSOC_MX6SL) {
		digprog = imx6_anatop_read_4(IMX6_ANALOG_DIGPROG);
		hwsoc = (digprog & IMX6_ANALOG_DIGPROG_SOCTYPE_MASK) >>
		    IMX6_ANALOG_DIGPROG_SOCTYPE_SHIFT;
		/*printf("digprog = 0x%08x\n", digprog);*/
		if (hwsoc == HWSOC_MX6DL) {
			pcr = devmap_ptov(SCU_CONFIG_PHYSADDR, 4);
			if (pcr != NULL) {
				/*printf("scu config = 0x%08x\n", *pcr);*/
				if ((*pcr & 0x03) == 0) {
					hwsoc = HWSOC_MX6SOLO;
				}
			}
		}
	}
	/* printf("hwsoc 0x%08x\n", hwsoc); */

	switch (hwsoc) {
	case HWSOC_MX6SL:
		soctype = IMXSOC_6SL;
		break;
	case HWSOC_MX6SOLO:
		soctype = IMXSOC_6S;
		break;
	case HWSOC_MX6DL:
		soctype = IMXSOC_6DL;
		break;
	case HWSOC_MX6Q :
		soctype = IMXSOC_6Q;
		break;
	case HWSOC_MX6UL:
		soctype = IMXSOC_6UL;
		break;
	default:
		printf("imx_soc_type: Don't understand hwsoc 0x%02x, "
		    "digprog 0x%08x; assuming IMXSOC_6Q\n", hwsoc, digprog);
		soctype = IMXSOC_6Q;
		break;
	}

	return (soctype);
}

/*
 * Early putc routine for EARLY_PRINTF support.  To use, add to kernel config:
 *   option SOCDEV_PA=0x02000000
 *   option SOCDEV_VA=0x02000000
 *   option EARLY_PRINTF
 * Resist the temptation to change the #if 0 to #ifdef EARLY_PRINTF here. It
 * makes sense now, but if multiple SOCs do that it will make early_putc another
 * duplicate symbol to be eliminated on the path to a generic kernel.
 */
#if 0 
static void 
imx6_early_putc(int c)
{
	volatile uint32_t * UART_STAT_REG = (uint32_t *)0x02020098;
	volatile uint32_t * UART_TX_REG   = (uint32_t *)0x02020040;
	const uint32_t      UART_TXRDY    = (1 << 3);

	while ((*UART_STAT_REG & UART_TXRDY) == 0)
		continue;
	*UART_TX_REG = c;
}
early_putc_t *early_putc = imx6_early_putc;
#endif

static platform_method_t imx6_methods[] = {
	PLATFORMMETHOD(platform_attach,		imx6_attach),
	PLATFORMMETHOD(platform_devmap_init,	imx6_devmap_init),
	PLATFORMMETHOD(platform_late_init,	imx6_late_init),
	PLATFORMMETHOD(platform_cpu_reset,	imx6_cpu_reset),

#ifdef SMP
	PLATFORMMETHOD(platform_mp_start_ap,	imx6_mp_start_ap),
	PLATFORMMETHOD(platform_mp_setmaxid,	imx6_mp_setmaxid),
#endif

	PLATFORMMETHOD(platform_pl310_init,	imx6_pl310_init),

	PLATFORMMETHOD_END,
};

FDT_PLATFORM_DEF2(imx6, imx6s, "i.MX6 Solo", 0, "fsl,imx6s", 80);
FDT_PLATFORM_DEF2(imx6, imx6d, "i.MX6 Dual", 0, "fsl,imx6dl", 80);
FDT_PLATFORM_DEF2(imx6, imx6q, "i.MX6 Quad", 0, "fsl,imx6q", 80);
FDT_PLATFORM_DEF2(imx6, imx6ul, "i.MX6 UltraLite", 0, "fsl,imx6ul", 67);
