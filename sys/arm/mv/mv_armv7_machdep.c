/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017 Semihalf.
 * Copyright (c) 1994-1998 Mark Brinicombe.
 * Copyright (c) 1994 Brini.
 * All rights reserved.
 *
 * This code is derived from software written for Brini by Mark Brinicombe
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Brini.
 * 4. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BRINI ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL BRINI OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * from: FreeBSD: //depot/projects/arm/src/sys/arm/at91/kb920x_machdep.c, rev 45
 */

#include "opt_ddb.h"
#include "opt_platform.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#define _ARM32_BUS_DMA_PRIVATE
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/devmap.h>
#include <sys/kernel.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <arm/arm/mpcore_timervar.h>
#include <arm/arm/nexusvar.h>

#include <machine/bus.h>
#include <machine/fdt.h>
#include <machine/machdep.h>
#include <machine/platform.h>
#include <machine/platformvar.h>
#include <machine/pte-v6.h>

#include <arm/mv/mvreg.h>
#include <arm/mv/mvvar.h>
#include <arm/mv/mvwin.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus_subr.h>

#include "opt_platform.h"
#include "platform_if.h"

#if defined(SOC_MV_ARMADA38X)
#include "platform_pl310_if.h"
#include "armada38x/armada38x_pl310.h"
#endif

static int platform_mpp_init(void);
int armada38x_win_set_iosync_barrier(void);
int armada38x_scu_enable(void);
int armada38x_open_bootrom_win(void);
int armada38x_mbus_optimization(void);

static vm_offset_t mv_platform_lastaddr(platform_t plate);
static int mv_platform_probe_and_attach(platform_t plate);
static void mv_platform_gpio_init(platform_t plate);
static void mv_cpu_reset(platform_t plat);

static void mv_a38x_platform_late_init(platform_t plate);
static int mv_a38x_platform_devmap_init(platform_t plate);
static void mv_axp_platform_late_init(platform_t plate);
static int mv_axp_platform_devmap_init(platform_t plate);

void armadaxp_init_coher_fabric(void);
void armadaxp_l2_init(void);

#ifdef SMP
void mv_a38x_platform_mp_setmaxid(platform_t plate);
void mv_a38x_platform_mp_start_ap(platform_t plate);
void mv_axp_platform_mp_setmaxid(platform_t plate);
void mv_axp_platform_mp_start_ap(platform_t plate);
#endif

#define MPP_PIN_MAX		68
#define MPP_PIN_CELLS		2
#define MPP_PINS_PER_REG	8
#define MPP_SEL(pin,func)	(((func) & 0xf) <<		\
    (((pin) % MPP_PINS_PER_REG) * 4))

static void
mv_busdma_tag_init(void *arg __unused)
{
	phandle_t node;
	bus_dma_tag_t dmat;

	/*
	 * If this platform has coherent DMA, create the parent DMA tag to pass
	 * down the coherent flag to all busses and devices on the platform,
	 * otherwise return without doing anything. By default create tag
	 * for all A38x-based platforms only.
	 */
	if ((node = OF_finddevice("/")) == -1){
		printf("no tree\n");
		return;
	}

	if (ofw_bus_node_is_compatible(node, "marvell,armada380") == 0)
		return;

	bus_dma_tag_create(NULL,	/* No parent tag */
	    1, 0,			/* alignment, bounds */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    BUS_SPACE_MAXSIZE,		/* maxsize */
	    BUS_SPACE_UNRESTRICTED,	/* nsegments */
	    BUS_SPACE_MAXSIZE,		/* maxsegsize */
	    BUS_DMA_COHERENT,		/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &dmat);

	nexus_set_dma_tag(dmat);

}
SYSINIT(mv_busdma_tag, SI_SUB_DRIVERS, SI_ORDER_ANY, mv_busdma_tag_init, NULL);

static int
platform_mpp_init(void)
{
	pcell_t pinmap[MPP_PIN_MAX * MPP_PIN_CELLS];
	int mpp[MPP_PIN_MAX];
	uint32_t ctrl_val, ctrl_offset;
	pcell_t reg[4];
	u_long start, size;
	phandle_t node;
	pcell_t pin_cells, *pinmap_ptr, pin_count;
	ssize_t len;
	int par_addr_cells, par_size_cells;
	int tuple_size, tuples, rv, pins, i, j;
	int mpp_pin, mpp_function;

	/*
	 * Try to access the MPP node directly i.e. through /aliases/mpp.
	 */
	if ((node = OF_finddevice("mpp")) != -1)
		if (ofw_bus_node_is_compatible(node, "mrvl,mpp"))
			goto moveon;
	/*
	 * Find the node the long way.
	 */
	if ((node = OF_finddevice("/")) == -1)
		return (ENXIO);

	if ((node = fdt_find_compatible(node, "simple-bus", 0)) == 0)
		return (ENXIO);

	if ((node = fdt_find_compatible(node, "mrvl,mpp", 0)) == 0)
		/*
		 * No MPP node. Fall back to how MPP got set by the
		 * first-stage loader and try to continue booting.
		 */
		return (0);
moveon:
	/*
	 * Process 'reg' prop.
	 */
	if ((rv = fdt_addrsize_cells(OF_parent(node), &par_addr_cells,
	    &par_size_cells)) != 0)
		return(ENXIO);

	tuple_size = sizeof(pcell_t) * (par_addr_cells + par_size_cells);
	len = OF_getprop(node, "reg", reg, sizeof(reg));
	tuples = len / tuple_size;
	if (tuple_size <= 0)
		return (EINVAL);

	rv = fdt_data_to_res(reg, par_addr_cells, par_size_cells,
	    &start, &size);
	if (rv != 0)
		return (rv);
	start += fdt_immr_va;

	/*
	 * Process 'pin-count' and 'pin-map' props.
	 */
	if (OF_getencprop(node, "pin-count", &pin_count, sizeof(pin_count)) <= 0)
		return (ENXIO);
	if (pin_count > MPP_PIN_MAX)
		return (ERANGE);

	if (OF_getencprop(node, "#pin-cells", &pin_cells, sizeof(pin_cells)) <= 0)
		pin_cells = MPP_PIN_CELLS;
	if (pin_cells > MPP_PIN_CELLS)
		return (ERANGE);
	tuple_size = sizeof(pcell_t) * pin_cells;

	bzero(pinmap, sizeof(pinmap));
	len = OF_getencprop(node, "pin-map", pinmap, sizeof(pinmap));
	if (len <= 0)
		return (ERANGE);
	if (len % tuple_size)
		return (ERANGE);
	pins = len / tuple_size;
	if (pins > pin_count)
		return (ERANGE);
	/*
	 * Fill out a "mpp[pin] => function" table. All pins unspecified in
	 * the 'pin-map' property are defaulted to 0 function i.e. GPIO.
	 */
	bzero(mpp, sizeof(mpp));
	pinmap_ptr = pinmap;
	for (i = 0; i < pins; i++) {
		mpp_pin = *pinmap_ptr;
		mpp_function = *(pinmap_ptr + 1);
		mpp[mpp_pin] = mpp_function;
		pinmap_ptr += pin_cells;
	}

	/*
	 * Prepare and program MPP control register values.
	 */
	ctrl_offset = 0;
	for (i = 0; i < pin_count;) {
		ctrl_val = 0;

		for (j = 0; j < MPP_PINS_PER_REG; j++) {
			if (i + j == pin_count - 1)
				break;
			ctrl_val |= MPP_SEL(i + j, mpp[i + j]);
		}
		i += MPP_PINS_PER_REG;
		bus_space_write_4(fdtbus_bs_tag, start, ctrl_offset,
		    ctrl_val);

		ctrl_offset += 4;
	}

	return (0);
}

static vm_offset_t
mv_platform_lastaddr(platform_t plat)
{

	return (fdt_immr_va);
}

static int
mv_platform_probe_and_attach(platform_t plate)
{

	if (fdt_immr_addr(MV_BASE) != 0)
		while (1);
	return (0);
}

static void
mv_platform_gpio_init(platform_t plate)
{

	/*
	 * Re-initialise MPP. It is important to call this prior to using
	 * console as the physical connection can be routed via MPP.
	 */
	if (platform_mpp_init() != 0)
		while (1);
}

static void
mv_a38x_platform_late_init(platform_t plate)
{

	/*
	 * Re-initialise decode windows
	 */
	if (mv_check_soc_family() == MV_SOC_UNSUPPORTED)
		panic("Unsupported SoC family\n");

	if (soc_decode_win() != 0)
		printf("WARNING: could not re-initialise decode windows! "
		    "Running with existing settings...\n");

	/* Configure timers' base frequency */
	arm_tmr_change_frequency(get_cpu_freq() / 2);

	/*
	 * Workaround for Marvell Armada38X family HW issue
	 * between Cortex-A9 CPUs and on-chip devices that may
	 * cause hang on heavy load.
	 * To avoid that, map all registers including PCIe IO
	 * as strongly ordered instead of device memory.
	 */
	pmap_remap_vm_attr(VM_MEMATTR_DEVICE, VM_MEMATTR_SO);

	/* Set IO Sync Barrier bit for all Mbus devices */
	if (armada38x_win_set_iosync_barrier() != 0)
		printf("WARNING: could not map CPU Subsystem registers\n");
	if (armada38x_mbus_optimization() != 0)
		printf("WARNING: could not enable mbus optimization\n");
	if (armada38x_scu_enable() != 0)
		printf("WARNING: could not enable SCU\n");
#ifdef SMP
	/* Open window to bootROM memory - needed for SMP */
	if (armada38x_open_bootrom_win() != 0)
		printf("WARNING: could not open window to bootROM\n");
#endif
}

static void
mv_axp_platform_late_init(platform_t plate)
{
	phandle_t node;
	/*
	 * Re-initialise decode windows
	 */
	if (soc_decode_win() != 0)
		printf("WARNING: could not re-initialise decode windows! "
		    "Running with existing settings...\n");
	if ((node = OF_finddevice("/")) == -1)
		return;

#if !defined(SMP)
	/* For SMP case it should be initialized after APs are booted */
	armadaxp_init_coher_fabric();
#endif
	armadaxp_l2_init();
}

#define FDT_DEVMAP_MAX	(MV_WIN_CPU_MAX_ARMV7 + 2)
static struct devmap_entry fdt_devmap[FDT_DEVMAP_MAX] = {
	{ 0, 0, 0, }
};

static int
platform_sram_devmap(struct devmap_entry *map)
{

	return (ENOENT);
}

/*
 * Construct devmap table with DT-derived config data.
 */
static int
mv_a38x_platform_devmap_init(platform_t plat)
{
	phandle_t root, child;
	int i;

	i = 0;
	devmap_register_table(&fdt_devmap[0]);

	if ((root = OF_finddevice("/")) == -1)
		return (ENXIO);

	/*
	 * IMMR range.
	 */
	fdt_devmap[i].pd_va = fdt_immr_va;
	fdt_devmap[i].pd_pa = fdt_immr_pa;
	fdt_devmap[i].pd_size = fdt_immr_size;
	i++;

	/*
	 * SRAM range.
	 */
	if (i < FDT_DEVMAP_MAX)
		if (platform_sram_devmap(&fdt_devmap[i]) == 0)
			i++;

	/*
	 * PCI range(s).
	 * PCI range(s) and localbus.
	 */
	for (child = OF_child(root); child != 0; child = OF_peer(child)) {
		if (mv_fdt_is_type(child, "pci") ||
		    mv_fdt_is_type(child, "pciep")) {
			/*
			 * Check space: each PCI node will consume 2 devmap
			 * entries.
			 */
			if (i + 1 >= FDT_DEVMAP_MAX)
				return (ENOMEM);

			if (mv_pci_devmap(child, &fdt_devmap[i], MV_PCI_VA_IO_BASE,
				    MV_PCI_VA_MEM_BASE) != 0)
				return (ENXIO);
			i += 2;
		}
	}

	return (0);
}

static int
mv_axp_platform_devmap_init(platform_t plate)
{
	vm_paddr_t cur_immr_pa;

	/*
	 * Acquire SoC registers' base passed by u-boot and fill devmap
	 * accordingly. DTB is going to be modified basing on this data
	 * later.
	 */
	__asm __volatile("mrc p15, 4, %0, c15, c0, 0" : "=r" (cur_immr_pa));
	cur_immr_pa = (cur_immr_pa << 13) & 0xff000000;
	if (cur_immr_pa != 0)
		fdt_immr_pa = cur_immr_pa;

	mv_a38x_platform_devmap_init(plate);

	return (0);
}

static void
mv_cpu_reset(platform_t plat)
{

	write_cpu_misc(RSTOUTn_MASK_ARMV7, SOFT_RST_OUT_EN_ARMV7);
	write_cpu_misc(SYSTEM_SOFT_RESET_ARMV7, SYS_SOFT_RST_ARMV7);
}

#if defined(SOC_MV_ARMADA38X)
static platform_method_t mv_a38x_methods[] = {
	PLATFORMMETHOD(platform_devmap_init, mv_a38x_platform_devmap_init),
	PLATFORMMETHOD(platform_cpu_reset, mv_cpu_reset),
	PLATFORMMETHOD(platform_lastaddr, mv_platform_lastaddr),
	PLATFORMMETHOD(platform_attach, mv_platform_probe_and_attach),
	PLATFORMMETHOD(platform_gpio_init, mv_platform_gpio_init),
	PLATFORMMETHOD(platform_late_init, mv_a38x_platform_late_init),
	PLATFORMMETHOD(platform_pl310_init, mv_a38x_platform_pl310_init),
	PLATFORMMETHOD(platform_pl310_write_ctrl, mv_a38x_platform_pl310_write_ctrl),
	PLATFORMMETHOD(platform_pl310_write_debug, mv_a38x_platform_pl310_write_debug),
#ifdef SMP
	PLATFORMMETHOD(platform_mp_start_ap, mv_a38x_platform_mp_start_ap),
	PLATFORMMETHOD(platform_mp_setmaxid, mv_a38x_platform_mp_setmaxid),
#endif

	PLATFORMMETHOD_END,
};
FDT_PLATFORM_DEF(mv_a38x, "mv_a38x", 0, "marvell,armada380", 100);
#endif

static platform_method_t mv_axp_methods[] = {
	PLATFORMMETHOD(platform_devmap_init, mv_axp_platform_devmap_init),
	PLATFORMMETHOD(platform_cpu_reset, mv_cpu_reset),
	PLATFORMMETHOD(platform_lastaddr, mv_platform_lastaddr),
	PLATFORMMETHOD(platform_attach, mv_platform_probe_and_attach),
	PLATFORMMETHOD(platform_gpio_init, mv_platform_gpio_init),
	PLATFORMMETHOD(platform_late_init, mv_axp_platform_late_init),
#ifdef SMP
	PLATFORMMETHOD(platform_mp_start_ap, mv_axp_platform_mp_start_ap),
	PLATFORMMETHOD(platform_mp_setmaxid, mv_axp_platform_mp_setmaxid),
#endif

	PLATFORMMETHOD_END,
};
FDT_PLATFORM_DEF(mv_axp, "mv_axp", 0, "marvell,armadaxp", 100);

