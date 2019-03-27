/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2008-2011 MARVELL INTERNATIONAL LTD.
 * All rights reserved.
 *
 * Developed by Semihalf.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of MARVELL nor the names of contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/kdb.h>
#include <sys/reboot.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>
#include <machine/fdt.h>
#include <machine/vmparam.h>
#include <machine/intr.h>

#include <arm/mv/mvreg.h>
#include <arm/mv/mvvar.h>
#include <arm/mv/mvwin.h>


MALLOC_DEFINE(M_IDMA, "idma", "idma dma test memory");

#define IDMA_DEBUG
#undef IDMA_DEBUG

#define MAX_CPU_WIN	5

#ifdef DEBUG
#define debugf(fmt, args...) do { printf("%s(): ", __func__);	\
    printf(fmt,##args); } while (0)
#else
#define debugf(fmt, args...)
#endif

#ifdef DEBUG
#define MV_DUMP_WIN	1
#else
#define MV_DUMP_WIN	0
#endif

struct soc_node_spec;

static enum soc_family soc_family;

static int mv_win_cesa_attr(int wng_sel);
static int mv_win_cesa_attr_armv5(int eng_sel);
static int mv_win_cesa_attr_armada38x(int eng_sel);
static int mv_win_cesa_attr_armadaxp(int eng_sel);

uint32_t read_cpu_ctrl_armv5(uint32_t reg);
uint32_t read_cpu_ctrl_armv7(uint32_t reg);

void write_cpu_ctrl_armv5(uint32_t reg, uint32_t val);
void write_cpu_ctrl_armv7(uint32_t reg, uint32_t val);

static int win_eth_can_remap(int i);

static int decode_win_cesa_valid(void);
static int decode_win_cpu_valid(void);
static int decode_win_usb_valid(void);
static int decode_win_usb3_valid(void);
static int decode_win_eth_valid(void);
static int decode_win_pcie_valid(void);
static int decode_win_sata_valid(void);
static int decode_win_sdhci_valid(void);

static int decode_win_idma_valid(void);
static int decode_win_xor_valid(void);

static void decode_win_cpu_setup(void);
static int decode_win_sdram_fixup(void);
static void decode_win_cesa_setup(u_long);
static void decode_win_a38x_cesa_setup(u_long);
static void decode_win_usb_setup(u_long);
static void decode_win_usb3_setup(u_long);
static void decode_win_eth_setup(u_long);
static void decode_win_neta_setup(u_long);
static void decode_win_sata_setup(u_long);
static void decode_win_ahci_setup(u_long);
static void decode_win_sdhci_setup(u_long);

static void decode_win_idma_setup(u_long);
static void decode_win_xor_setup(u_long);

static void decode_win_cesa_dump(u_long);
static void decode_win_a38x_cesa_dump(u_long);
static void decode_win_usb_dump(u_long);
static void decode_win_usb3_dump(u_long);
static void decode_win_eth_dump(u_long base);
static void decode_win_neta_dump(u_long base);
static void decode_win_idma_dump(u_long base);
static void decode_win_xor_dump(u_long base);
static void decode_win_ahci_dump(u_long base);
static void decode_win_sdhci_dump(u_long);
static void decode_win_pcie_dump(u_long);

static uint32_t win_cpu_cr_read(int);
static uint32_t win_cpu_armv5_cr_read(int);
static uint32_t win_cpu_armv7_cr_read(int);
static uint32_t win_cpu_br_read(int);
static uint32_t win_cpu_armv5_br_read(int);
static uint32_t win_cpu_armv7_br_read(int);
static uint32_t win_cpu_remap_l_read(int);
static uint32_t win_cpu_armv5_remap_l_read(int);
static uint32_t win_cpu_armv7_remap_l_read(int);
static uint32_t win_cpu_remap_h_read(int);
static uint32_t win_cpu_armv5_remap_h_read(int);
static uint32_t win_cpu_armv7_remap_h_read(int);

static void win_cpu_cr_write(int, uint32_t);
static void win_cpu_armv5_cr_write(int, uint32_t);
static void win_cpu_armv7_cr_write(int, uint32_t);
static void win_cpu_br_write(int, uint32_t);
static void win_cpu_armv5_br_write(int, uint32_t);
static void win_cpu_armv7_br_write(int, uint32_t);
static void win_cpu_remap_l_write(int, uint32_t);
static void win_cpu_armv5_remap_l_write(int, uint32_t);
static void win_cpu_armv7_remap_l_write(int, uint32_t);
static void win_cpu_remap_h_write(int, uint32_t);
static void win_cpu_armv5_remap_h_write(int, uint32_t);
static void win_cpu_armv7_remap_h_write(int, uint32_t);

static uint32_t ddr_br_read(int);
static uint32_t ddr_sz_read(int);
static uint32_t ddr_armv5_br_read(int);
static uint32_t ddr_armv5_sz_read(int);
static uint32_t ddr_armv7_br_read(int);
static uint32_t ddr_armv7_sz_read(int);
static void ddr_br_write(int, uint32_t);
static void ddr_sz_write(int, uint32_t);
static void ddr_armv5_br_write(int, uint32_t);
static void ddr_armv5_sz_write(int, uint32_t);
static void ddr_armv7_br_write(int, uint32_t);
static void ddr_armv7_sz_write(int, uint32_t);

static int fdt_get_ranges(const char *, void *, int, int *, int *);
int gic_decode_fdt(phandle_t iparent, pcell_t *intr, int *interrupt,
    int *trig, int *pol);

static int win_cpu_from_dt(void);
static int fdt_win_setup(void);

static int fdt_win_process_child(phandle_t, struct soc_node_spec *, const char*);

static void soc_identify(uint32_t, uint32_t);

static uint32_t dev_mask = 0;
static int cpu_wins_no = 0;
static int eth_port = 0;
static int usb_port = 0;
static boolean_t platform_io_coherent = false;

static struct decode_win cpu_win_tbl[MAX_CPU_WIN];

const struct decode_win *cpu_wins = cpu_win_tbl;

typedef void (*decode_win_setup_t)(u_long);
typedef void (*dump_win_t)(u_long);
typedef int (*valid_t)(void);

/*
 * The power status of device feature is only supported on
 * Kirkwood and Discovery SoCs.
 */
#if defined(SOC_MV_KIRKWOOD) || defined(SOC_MV_DISCOVERY)
#define	SOC_MV_POWER_STAT_SUPPORTED		1
#else
#define	SOC_MV_POWER_STAT_SUPPORTED		0
#endif

struct soc_node_spec {
	const char		*compat;
	decode_win_setup_t	decode_handler;
	dump_win_t		dump_handler;
	valid_t			valid_handler;
};

static struct soc_node_spec soc_nodes[] = {
	{ "mrvl,ge", &decode_win_eth_setup, &decode_win_eth_dump, &decode_win_eth_valid},
	{ "marvell,armada-370-neta", &decode_win_neta_setup,
	    &decode_win_neta_dump, NULL },
	{ "mrvl,usb-ehci", &decode_win_usb_setup, &decode_win_usb_dump, &decode_win_usb_valid},
	{ "marvell,orion-ehci", &decode_win_usb_setup, &decode_win_usb_dump, &decode_win_usb_valid },
	{ "marvell,armada-380-xhci", &decode_win_usb3_setup,
	    &decode_win_usb3_dump, &decode_win_usb3_valid },
	{ "marvell,armada-380-ahci", &decode_win_ahci_setup,
	    &decode_win_ahci_dump, NULL },
	{ "marvell,armada-380-sdhci", &decode_win_sdhci_setup,
	    &decode_win_sdhci_dump, &decode_win_sdhci_valid},
	{ "mrvl,sata", &decode_win_sata_setup, NULL, &decode_win_sata_valid},
	{ "mrvl,xor", &decode_win_xor_setup, &decode_win_xor_dump, &decode_win_xor_valid},
	{ "mrvl,idma", &decode_win_idma_setup, &decode_win_idma_dump, &decode_win_idma_valid},
	{ "mrvl,cesa", &decode_win_cesa_setup, &decode_win_cesa_dump, &decode_win_cesa_valid},
	{ "mrvl,pcie", &decode_win_pcie_setup, &decode_win_pcie_dump, &decode_win_pcie_valid},
	{ "marvell,armada-38x-crypto", &decode_win_a38x_cesa_setup,
	    &decode_win_a38x_cesa_dump, &decode_win_cesa_valid},
	{ NULL, NULL, NULL, NULL },
};

#define	SOC_NODE_PCIE_ENTRY_IDX		11

typedef uint32_t(*read_cpu_ctrl_t)(uint32_t);
typedef void(*write_cpu_ctrl_t)(uint32_t, uint32_t);
typedef uint32_t (*win_read_t)(int);
typedef void (*win_write_t)(int, uint32_t);
typedef int (*win_cesa_attr_t)(int);
typedef uint32_t (*get_t)(void);

struct decode_win_spec {
	read_cpu_ctrl_t  read_cpu_ctrl;
	write_cpu_ctrl_t write_cpu_ctrl;
	win_read_t	cr_read;
	win_read_t	br_read;
	win_read_t	remap_l_read;
	win_read_t	remap_h_read;
	win_write_t	cr_write;
	win_write_t	br_write;
	win_write_t	remap_l_write;
	win_write_t	remap_h_write;
	uint32_t	mv_win_cpu_max;
	win_cesa_attr_t win_cesa_attr;
	int 		win_cesa_target;
	win_read_t	ddr_br_read;
	win_read_t	ddr_sz_read;
	win_write_t	ddr_br_write;
	win_write_t	ddr_sz_write;
#if __ARM_ARCH >= 6
	get_t		get_tclk;
	get_t		get_cpu_freq;
#endif
};

struct decode_win_spec *soc_decode_win_spec;

static struct decode_win_spec decode_win_specs[] =
{
	{
		&read_cpu_ctrl_armv7,
		&write_cpu_ctrl_armv7,
		&win_cpu_armv7_cr_read,
		&win_cpu_armv7_br_read,
		&win_cpu_armv7_remap_l_read,
		&win_cpu_armv7_remap_h_read,
		&win_cpu_armv7_cr_write,
		&win_cpu_armv7_br_write,
		&win_cpu_armv7_remap_l_write,
		&win_cpu_armv7_remap_h_write,
		MV_WIN_CPU_MAX_ARMV7,
		&mv_win_cesa_attr_armada38x,
		MV_WIN_CESA_TARGET_ARMADA38X,
		&ddr_armv7_br_read,
		&ddr_armv7_sz_read,
		&ddr_armv7_br_write,
		&ddr_armv7_sz_write,
#if __ARM_ARCH >= 6
		&get_tclk_armada38x,
		&get_cpu_freq_armada38x,
#endif
	},
	{
		&read_cpu_ctrl_armv7,
		&write_cpu_ctrl_armv7,
		&win_cpu_armv7_cr_read,
		&win_cpu_armv7_br_read,
		&win_cpu_armv7_remap_l_read,
		&win_cpu_armv7_remap_h_read,
		&win_cpu_armv7_cr_write,
		&win_cpu_armv7_br_write,
		&win_cpu_armv7_remap_l_write,
		&win_cpu_armv7_remap_h_write,
		MV_WIN_CPU_MAX_ARMV7,
		&mv_win_cesa_attr_armadaxp,
		MV_WIN_CESA_TARGET_ARMADAXP,
		&ddr_armv7_br_read,
		&ddr_armv7_sz_read,
		&ddr_armv7_br_write,
		&ddr_armv7_sz_write,
#if __ARM_ARCH >= 6
		&get_tclk_armadaxp,
		&get_cpu_freq_armadaxp,
#endif
	},
	{
		&read_cpu_ctrl_armv5,
		&write_cpu_ctrl_armv5,
		&win_cpu_armv5_cr_read,
		&win_cpu_armv5_br_read,
		&win_cpu_armv5_remap_l_read,
		&win_cpu_armv5_remap_h_read,
		&win_cpu_armv5_cr_write,
		&win_cpu_armv5_br_write,
		&win_cpu_armv5_remap_l_write,
		&win_cpu_armv5_remap_h_write,
		MV_WIN_CPU_MAX,
		&mv_win_cesa_attr_armv5,
		MV_WIN_CESA_TARGET,
		&ddr_armv5_br_read,
		&ddr_armv5_sz_read,
		&ddr_armv5_br_write,
		&ddr_armv5_sz_write,
#if __ARM_ARCH >= 6
		NULL,
		NULL,
#endif
	},
};

struct fdt_pm_mask_entry {
	char		*compat;
	uint32_t	mask;
};

static struct fdt_pm_mask_entry fdt_pm_mask_table[] = {
	{ "mrvl,ge",		CPU_PM_CTRL_GE(0) },
	{ "mrvl,ge",		CPU_PM_CTRL_GE(1) },
	{ "mrvl,usb-ehci",	CPU_PM_CTRL_USB(0) },
	{ "mrvl,usb-ehci",	CPU_PM_CTRL_USB(1) },
	{ "mrvl,usb-ehci",	CPU_PM_CTRL_USB(2) },
	{ "mrvl,xor",		CPU_PM_CTRL_XOR },
	{ "mrvl,sata",		CPU_PM_CTRL_SATA },

	{ NULL, 0 }
};

static __inline int
pm_is_disabled(uint32_t mask)
{
#if SOC_MV_POWER_STAT_SUPPORTED
	return (soc_power_ctrl_get(mask) == mask ? 0 : 1);
#else
	return (0);
#endif
}

/*
 * Disable device using power management register.
 * 1 - Device Power On
 * 0 - Device Power Off
 * Mask can be set in loader.
 * EXAMPLE:
 * loader> set hw.pm-disable-mask=0x2
 *
 * Common mask:
 * |-------------------------------|
 * | Device | Kirkwood | Discovery |
 * |-------------------------------|
 * | USB0   | 0x00008  | 0x020000  |
 * |-------------------------------|
 * | USB1   |     -    | 0x040000  |
 * |-------------------------------|
 * | USB2   |     -    | 0x080000  |
 * |-------------------------------|
 * | GE0    | 0x00001  | 0x000002  |
 * |-------------------------------|
 * | GE1    |     -    | 0x000004  |
 * |-------------------------------|
 * | IDMA   |     -    | 0x100000  |
 * |-------------------------------|
 * | XOR    | 0x10000  | 0x200000  |
 * |-------------------------------|
 * | CESA   | 0x20000  | 0x400000  |
 * |-------------------------------|
 * | SATA   | 0x04000  | 0x004000  |
 * --------------------------------|
 * This feature can be used only on Kirkwood and Discovery
 * machines.
 */

static int mv_win_cesa_attr(int eng_sel)
{

	if (soc_decode_win_spec->win_cesa_attr != NULL)
		return (soc_decode_win_spec->win_cesa_attr(eng_sel));

	return (-1);
}

static int mv_win_cesa_attr_armv5(int eng_sel)
{

	return MV_WIN_CESA_ATTR(eng_sel);
}

static int mv_win_cesa_attr_armada38x(int eng_sel)
{

	return MV_WIN_CESA_ATTR_ARMADA38X(eng_sel);
}

static int mv_win_cesa_attr_armadaxp(int eng_sel)
{

	return MV_WIN_CESA_ATTR_ARMADAXP(eng_sel);
}

enum soc_family
mv_check_soc_family()
{
	uint32_t dev, rev;

	soc_id(&dev, &rev);
	switch (dev) {
	case MV_DEV_MV78230:
	case MV_DEV_MV78260:
	case MV_DEV_MV78460:
		soc_decode_win_spec = &decode_win_specs[MV_SOC_ARMADA_XP];
		soc_family = MV_SOC_ARMADA_XP;
		break;
	case MV_DEV_88F6828:
	case MV_DEV_88F6820:
	case MV_DEV_88F6810:
		soc_decode_win_spec = &decode_win_specs[MV_SOC_ARMADA_38X];
		soc_family = MV_SOC_ARMADA_38X;
		break;
	case MV_DEV_88F5181:
	case MV_DEV_88F5182:
	case MV_DEV_88F5281:
	case MV_DEV_88F6281:
	case MV_DEV_88RC8180:
	case MV_DEV_88RC9480:
	case MV_DEV_88RC9580:
	case MV_DEV_88F6781:
	case MV_DEV_88F6282:
	case MV_DEV_MV78100_Z0:
	case MV_DEV_MV78100:
	case MV_DEV_MV78160:
		soc_decode_win_spec = &decode_win_specs[MV_SOC_ARMV5];
		soc_family = MV_SOC_ARMV5;
		break;
	default:
		soc_family = MV_SOC_UNSUPPORTED;
		return (MV_SOC_UNSUPPORTED);
	}

	soc_identify(dev, rev);

	return (soc_family);
}

static __inline void
pm_disable_device(int mask)
{
#ifdef DIAGNOSTIC
	uint32_t reg;

	reg = soc_power_ctrl_get(CPU_PM_CTRL_ALL);
	printf("Power Management Register: 0%x\n", reg);

	reg &= ~mask;
	soc_power_ctrl_set(reg);
	printf("Device %x is disabled\n", mask);

	reg = soc_power_ctrl_get(CPU_PM_CTRL_ALL);
	printf("Power Management Register: 0%x\n", reg);
#endif
}

int
mv_fdt_is_type(phandle_t node, const char *typestr)
{
#define FDT_TYPE_LEN	64
	char type[FDT_TYPE_LEN];

	if (OF_getproplen(node, "device_type") <= 0)
		return (0);

	if (OF_getprop(node, "device_type", type, FDT_TYPE_LEN) < 0)
		return (0);

	if (strncasecmp(type, typestr, FDT_TYPE_LEN) == 0)
		/* This fits. */
		return (1);

	return (0);
#undef FDT_TYPE_LEN
}

int
mv_fdt_pm(phandle_t node)
{
	uint32_t cpu_pm_ctrl;
	int i, ena, compat;

	ena = 1;
	cpu_pm_ctrl = read_cpu_ctrl(CPU_PM_CTRL);
	for (i = 0; fdt_pm_mask_table[i].compat != NULL; i++) {
		if (dev_mask & (1 << i))
			continue;

		compat = ofw_bus_node_is_compatible(node,
		    fdt_pm_mask_table[i].compat);
#if defined(SOC_MV_KIRKWOOD)
		if (compat && (cpu_pm_ctrl & fdt_pm_mask_table[i].mask)) {
			dev_mask |= (1 << i);
			ena = 0;
			break;
		} else if (compat) {
			dev_mask |= (1 << i);
			break;
		}
#else
		if (compat && (~cpu_pm_ctrl & fdt_pm_mask_table[i].mask)) {
			dev_mask |= (1 << i);
			ena = 0;
			break;
		} else if (compat) {
			dev_mask |= (1 << i);
			break;
		}
#endif
	}

	return (ena);
}

uint32_t
read_cpu_ctrl(uint32_t reg)
{

	if (soc_decode_win_spec->read_cpu_ctrl != NULL)
		return (soc_decode_win_spec->read_cpu_ctrl(reg));
	return (-1);
}

uint32_t
read_cpu_ctrl_armv5(uint32_t reg)
{

	return (bus_space_read_4(fdtbus_bs_tag, MV_CPU_CONTROL_BASE, reg));
}

uint32_t
read_cpu_ctrl_armv7(uint32_t reg)
{

	return (bus_space_read_4(fdtbus_bs_tag, MV_CPU_CONTROL_BASE_ARMV7, reg));
}

void
write_cpu_ctrl(uint32_t reg, uint32_t val)
{

	if (soc_decode_win_spec->write_cpu_ctrl != NULL)
		soc_decode_win_spec->write_cpu_ctrl(reg, val);
}

void
write_cpu_ctrl_armv5(uint32_t reg, uint32_t val)
{

	bus_space_write_4(fdtbus_bs_tag, MV_CPU_CONTROL_BASE, reg, val);
}

void
write_cpu_ctrl_armv7(uint32_t reg, uint32_t val)
{

	bus_space_write_4(fdtbus_bs_tag, MV_CPU_CONTROL_BASE_ARMV7, reg, val);
}

uint32_t
read_cpu_mp_clocks(uint32_t reg)
{

	return (bus_space_read_4(fdtbus_bs_tag, MV_MP_CLOCKS_BASE, reg));
}

void
write_cpu_mp_clocks(uint32_t reg, uint32_t val)
{

	bus_space_write_4(fdtbus_bs_tag, MV_MP_CLOCKS_BASE, reg, val);
}

uint32_t
read_cpu_misc(uint32_t reg)
{

	return (bus_space_read_4(fdtbus_bs_tag, MV_MISC_BASE, reg));
}

void
write_cpu_misc(uint32_t reg, uint32_t val)
{

	bus_space_write_4(fdtbus_bs_tag, MV_MISC_BASE, reg, val);
}

uint32_t
cpu_extra_feat(void)
{
	uint32_t dev, rev;
	uint32_t ef = 0;

	soc_id(&dev, &rev);

	switch (dev) {
	case MV_DEV_88F6281:
	case MV_DEV_88F6282:
	case MV_DEV_88RC8180:
	case MV_DEV_MV78100_Z0:
	case MV_DEV_MV78100:
		__asm __volatile("mrc p15, 1, %0, c15, c1, 0" : "=r" (ef));
		break;
	case MV_DEV_88F5182:
	case MV_DEV_88F5281:
		__asm __volatile("mrc p15, 0, %0, c14, c0, 0" : "=r" (ef));
		break;
	default:
		if (bootverbose)
			printf("This ARM Core does not support any extra features\n");
	}

	return (ef);
}

/*
 * Get the power status of device. This feature is only supported on
 * Kirkwood and Discovery SoCs.
 */
uint32_t
soc_power_ctrl_get(uint32_t mask)
{

#if SOC_MV_POWER_STAT_SUPPORTED
	if (mask != CPU_PM_CTRL_NONE)
		mask &= read_cpu_ctrl(CPU_PM_CTRL);

	return (mask);
#else
	return (mask);
#endif
}

/*
 * Set the power status of device. This feature is only supported on
 * Kirkwood and Discovery SoCs.
 */
void
soc_power_ctrl_set(uint32_t mask)
{

#if !defined(SOC_MV_ORION)
	if (mask != CPU_PM_CTRL_NONE)
		write_cpu_ctrl(CPU_PM_CTRL, mask);
#endif
}

void
soc_id(uint32_t *dev, uint32_t *rev)
{
	uint64_t mv_pcie_base = MV_PCIE_BASE;
	phandle_t node;

	/*
	 * Notice: system identifiers are available in the registers range of
	 * PCIE controller, so using this function is only allowed (and
	 * possible) after the internal registers range has been mapped in via
	 * devmap_bootstrap().
	 */
	*dev = 0;
	*rev = 0;
	if ((node = OF_finddevice("/")) == -1)
		return;
	if (ofw_bus_node_is_compatible(node, "marvell,armada380"))
		mv_pcie_base = MV_PCIE_BASE_ARMADA38X;

	*dev = bus_space_read_4(fdtbus_bs_tag, mv_pcie_base, 0) >> 16;
	*rev = bus_space_read_4(fdtbus_bs_tag, mv_pcie_base, 8) & 0xff;
}

static void
soc_identify(uint32_t d, uint32_t r)
{
	uint32_t size, mode, freq;
	const char *dev;
	const char *rev;

	printf("SOC: ");
	if (bootverbose)
		printf("(0x%4x:0x%02x) ", d, r);

	rev = "";
	switch (d) {
	case MV_DEV_88F5181:
		dev = "Marvell 88F5181";
		if (r == 3)
			rev = "B1";
		break;
	case MV_DEV_88F5182:
		dev = "Marvell 88F5182";
		if (r == 2)
			rev = "A2";
		break;
	case MV_DEV_88F5281:
		dev = "Marvell 88F5281";
		if (r == 4)
			rev = "D0";
		else if (r == 5)
			rev = "D1";
		else if (r == 6)
			rev = "D2";
		break;
	case MV_DEV_88F6281:
		dev = "Marvell 88F6281";
		if (r == 0)
			rev = "Z0";
		else if (r == 2)
			rev = "A0";
		else if (r == 3)
			rev = "A1";
		break;
	case MV_DEV_88RC8180:
		dev = "Marvell 88RC8180";
		break;
	case MV_DEV_88RC9480:
		dev = "Marvell 88RC9480";
		break;
	case MV_DEV_88RC9580:
		dev = "Marvell 88RC9580";
		break;
	case MV_DEV_88F6781:
		dev = "Marvell 88F6781";
		if (r == 2)
			rev = "Y0";
		break;
	case MV_DEV_88F6282:
		dev = "Marvell 88F6282";
		if (r == 0)
			rev = "A0";
		else if (r == 1)
			rev = "A1";
		break;
	case MV_DEV_88F6828:
		dev = "Marvell 88F6828";
		break;
	case MV_DEV_88F6820:
		dev = "Marvell 88F6820";
		break;
	case MV_DEV_88F6810:
		dev = "Marvell 88F6810";
		break;
	case MV_DEV_MV78100_Z0:
		dev = "Marvell MV78100 Z0";
		break;
	case MV_DEV_MV78100:
		dev = "Marvell MV78100";
		break;
	case MV_DEV_MV78160:
		dev = "Marvell MV78160";
		break;
	case MV_DEV_MV78260:
		dev = "Marvell MV78260";
		break;
	case MV_DEV_MV78460:
		dev = "Marvell MV78460";
		break;
	default:
		dev = "UNKNOWN";
		break;
	}

	printf("%s", dev);
	if (*rev != '\0')
		printf(" rev %s", rev);
	printf(", TClock %dMHz", get_tclk() / 1000 / 1000);
	freq = get_cpu_freq();
	if (freq != 0)
		printf(", Frequency %dMHz", freq / 1000 / 1000);
	printf("\n");

	mode = read_cpu_ctrl(CPU_CONFIG);
	printf("  Instruction cache prefetch %s, data cache prefetch %s\n",
	    (mode & CPU_CONFIG_IC_PREF) ? "enabled" : "disabled",
	    (mode & CPU_CONFIG_DC_PREF) ? "enabled" : "disabled");

	switch (d) {
	case MV_DEV_88F6281:
	case MV_DEV_88F6282:
		mode = read_cpu_ctrl(CPU_L2_CONFIG) & CPU_L2_CONFIG_MODE;
		printf("  256KB 4-way set-associative %s unified L2 cache\n",
		    mode ? "write-through" : "write-back");
		break;
	case MV_DEV_MV78100:
		mode = read_cpu_ctrl(CPU_CONTROL);
		size = mode & CPU_CONTROL_L2_SIZE;
		mode = mode & CPU_CONTROL_L2_MODE;
		printf("  %s set-associative %s unified L2 cache\n",
		    size ? "256KB 4-way" : "512KB 8-way",
		    mode ? "write-through" : "write-back");
		break;
	default:
		break;
	}
}

#ifdef KDB
static void
mv_enter_debugger(void *dummy)
{

	if (boothowto & RB_KDB)
		kdb_enter(KDB_WHY_BOOTFLAGS, "Boot flags requested debugger");
}
SYSINIT(mv_enter_debugger, SI_SUB_CPU, SI_ORDER_ANY, mv_enter_debugger, NULL);
#endif

int
soc_decode_win(void)
{
	uint32_t dev, rev;
	int mask, err;

	mask = 0;
	TUNABLE_INT_FETCH("hw.pm-disable-mask", &mask);

	if (mask != 0)
		pm_disable_device(mask);

	/* Retrieve data about physical addresses from device tree. */
	if ((err = win_cpu_from_dt()) != 0)
		return (err);

	/* Retrieve our ID: some windows facilities vary between SoC models */
	soc_id(&dev, &rev);

	if (soc_family == MV_SOC_ARMADA_XP)
		if ((err = decode_win_sdram_fixup()) != 0)
			return(err);


	decode_win_cpu_setup();
	if (MV_DUMP_WIN)
		soc_dump_decode_win();

	eth_port = 0;
	usb_port = 0;
	if ((err = fdt_win_setup()) != 0)
		return (err);

	return (0);
}

/**************************************************************************
 * Decode windows registers accessors
 **************************************************************************/
WIN_REG_IDX_RD(win_cpu_armv5, cr, MV_WIN_CPU_CTRL_ARMV5, MV_MBUS_BRIDGE_BASE)
WIN_REG_IDX_RD(win_cpu_armv5, br, MV_WIN_CPU_BASE_ARMV5, MV_MBUS_BRIDGE_BASE)
WIN_REG_IDX_RD(win_cpu_armv5, remap_l, MV_WIN_CPU_REMAP_LO_ARMV5, MV_MBUS_BRIDGE_BASE)
WIN_REG_IDX_RD(win_cpu_armv5, remap_h, MV_WIN_CPU_REMAP_HI_ARMV5, MV_MBUS_BRIDGE_BASE)
WIN_REG_IDX_WR(win_cpu_armv5, cr, MV_WIN_CPU_CTRL_ARMV5, MV_MBUS_BRIDGE_BASE)
WIN_REG_IDX_WR(win_cpu_armv5, br, MV_WIN_CPU_BASE_ARMV5, MV_MBUS_BRIDGE_BASE)
WIN_REG_IDX_WR(win_cpu_armv5, remap_l, MV_WIN_CPU_REMAP_LO_ARMV5, MV_MBUS_BRIDGE_BASE)
WIN_REG_IDX_WR(win_cpu_armv5, remap_h, MV_WIN_CPU_REMAP_HI_ARMV5, MV_MBUS_BRIDGE_BASE)

WIN_REG_IDX_RD(win_cpu_armv7, cr, MV_WIN_CPU_CTRL_ARMV7, MV_MBUS_BRIDGE_BASE)
WIN_REG_IDX_RD(win_cpu_armv7, br, MV_WIN_CPU_BASE_ARMV7, MV_MBUS_BRIDGE_BASE)
WIN_REG_IDX_RD(win_cpu_armv7, remap_l, MV_WIN_CPU_REMAP_LO_ARMV7, MV_MBUS_BRIDGE_BASE)
WIN_REG_IDX_RD(win_cpu_armv7, remap_h, MV_WIN_CPU_REMAP_HI_ARMV7, MV_MBUS_BRIDGE_BASE)
WIN_REG_IDX_WR(win_cpu_armv7, cr, MV_WIN_CPU_CTRL_ARMV7, MV_MBUS_BRIDGE_BASE)
WIN_REG_IDX_WR(win_cpu_armv7, br, MV_WIN_CPU_BASE_ARMV7, MV_MBUS_BRIDGE_BASE)
WIN_REG_IDX_WR(win_cpu_armv7, remap_l, MV_WIN_CPU_REMAP_LO_ARMV7, MV_MBUS_BRIDGE_BASE)
WIN_REG_IDX_WR(win_cpu_armv7, remap_h, MV_WIN_CPU_REMAP_HI_ARMV7, MV_MBUS_BRIDGE_BASE)

static uint32_t
win_cpu_cr_read(int i)
{

	if (soc_decode_win_spec->cr_read != NULL)
		return (soc_decode_win_spec->cr_read(i));
	return (-1);
}

static uint32_t
win_cpu_br_read(int i)
{

	if (soc_decode_win_spec->br_read != NULL)
		return (soc_decode_win_spec->br_read(i));
	return (-1);
}

static uint32_t
win_cpu_remap_l_read(int i)
{

	if (soc_decode_win_spec->remap_l_read != NULL)
		return (soc_decode_win_spec->remap_l_read(i));
	return (-1);
}

static uint32_t
win_cpu_remap_h_read(int i)
{

	if (soc_decode_win_spec->remap_h_read != NULL)
		return soc_decode_win_spec->remap_h_read(i);
	return (-1);
}

static void
win_cpu_cr_write(int i, uint32_t val)
{

	if (soc_decode_win_spec->cr_write != NULL)
		soc_decode_win_spec->cr_write(i, val);
}

static void
win_cpu_br_write(int i, uint32_t val)
{

	if (soc_decode_win_spec->br_write != NULL)
		soc_decode_win_spec->br_write(i, val);
}

static void
win_cpu_remap_l_write(int i, uint32_t val)
{

	if (soc_decode_win_spec->remap_l_write != NULL)
		soc_decode_win_spec->remap_l_write(i, val);
}

static void
win_cpu_remap_h_write(int i, uint32_t val)
{

	if (soc_decode_win_spec->remap_h_write != NULL)
		soc_decode_win_spec->remap_h_write(i, val);
}

WIN_REG_BASE_IDX_RD(win_cesa, cr, MV_WIN_CESA_CTRL)
WIN_REG_BASE_IDX_RD(win_cesa, br, MV_WIN_CESA_BASE)
WIN_REG_BASE_IDX_WR(win_cesa, cr, MV_WIN_CESA_CTRL)
WIN_REG_BASE_IDX_WR(win_cesa, br, MV_WIN_CESA_BASE)

WIN_REG_BASE_IDX_RD(win_usb, cr, MV_WIN_USB_CTRL)
WIN_REG_BASE_IDX_RD(win_usb, br, MV_WIN_USB_BASE)
WIN_REG_BASE_IDX_WR(win_usb, cr, MV_WIN_USB_CTRL)
WIN_REG_BASE_IDX_WR(win_usb, br, MV_WIN_USB_BASE)

WIN_REG_BASE_IDX_RD(win_usb3, cr, MV_WIN_USB3_CTRL)
WIN_REG_BASE_IDX_RD(win_usb3, br, MV_WIN_USB3_BASE)
WIN_REG_BASE_IDX_WR(win_usb3, cr, MV_WIN_USB3_CTRL)
WIN_REG_BASE_IDX_WR(win_usb3, br, MV_WIN_USB3_BASE)

WIN_REG_BASE_IDX_RD(win_eth, br, MV_WIN_ETH_BASE)
WIN_REG_BASE_IDX_RD(win_eth, sz, MV_WIN_ETH_SIZE)
WIN_REG_BASE_IDX_RD(win_eth, har, MV_WIN_ETH_REMAP)
WIN_REG_BASE_IDX_WR(win_eth, br, MV_WIN_ETH_BASE)
WIN_REG_BASE_IDX_WR(win_eth, sz, MV_WIN_ETH_SIZE)
WIN_REG_BASE_IDX_WR(win_eth, har, MV_WIN_ETH_REMAP)

WIN_REG_BASE_IDX_RD2(win_xor, br, MV_WIN_XOR_BASE)
WIN_REG_BASE_IDX_RD2(win_xor, sz, MV_WIN_XOR_SIZE)
WIN_REG_BASE_IDX_RD2(win_xor, har, MV_WIN_XOR_REMAP)
WIN_REG_BASE_IDX_RD2(win_xor, ctrl, MV_WIN_XOR_CTRL)
WIN_REG_BASE_IDX_WR2(win_xor, br, MV_WIN_XOR_BASE)
WIN_REG_BASE_IDX_WR2(win_xor, sz, MV_WIN_XOR_SIZE)
WIN_REG_BASE_IDX_WR2(win_xor, har, MV_WIN_XOR_REMAP)
WIN_REG_BASE_IDX_WR2(win_xor, ctrl, MV_WIN_XOR_CTRL)

WIN_REG_BASE_RD(win_eth, bare, 0x290)
WIN_REG_BASE_RD(win_eth, epap, 0x294)
WIN_REG_BASE_WR(win_eth, bare, 0x290)
WIN_REG_BASE_WR(win_eth, epap, 0x294)

WIN_REG_BASE_IDX_RD(win_pcie, cr, MV_WIN_PCIE_CTRL);
WIN_REG_BASE_IDX_RD(win_pcie, br, MV_WIN_PCIE_BASE);
WIN_REG_BASE_IDX_RD(win_pcie, remap, MV_WIN_PCIE_REMAP);
WIN_REG_BASE_IDX_WR(win_pcie, cr, MV_WIN_PCIE_CTRL);
WIN_REG_BASE_IDX_WR(win_pcie, br, MV_WIN_PCIE_BASE);
WIN_REG_BASE_IDX_WR(win_pcie, remap, MV_WIN_PCIE_REMAP);
WIN_REG_BASE_IDX_RD(pcie_bar, br, MV_PCIE_BAR_BASE);
WIN_REG_BASE_IDX_RD(pcie_bar, brh, MV_PCIE_BAR_BASE_H);
WIN_REG_BASE_IDX_RD(pcie_bar, cr, MV_PCIE_BAR_CTRL);
WIN_REG_BASE_IDX_WR(pcie_bar, br, MV_PCIE_BAR_BASE);
WIN_REG_BASE_IDX_WR(pcie_bar, brh, MV_PCIE_BAR_BASE_H);
WIN_REG_BASE_IDX_WR(pcie_bar, cr, MV_PCIE_BAR_CTRL);

WIN_REG_BASE_IDX_RD(win_idma, br, MV_WIN_IDMA_BASE)
WIN_REG_BASE_IDX_RD(win_idma, sz, MV_WIN_IDMA_SIZE)
WIN_REG_BASE_IDX_RD(win_idma, har, MV_WIN_IDMA_REMAP)
WIN_REG_BASE_IDX_RD(win_idma, cap, MV_WIN_IDMA_CAP)
WIN_REG_BASE_IDX_WR(win_idma, br, MV_WIN_IDMA_BASE)
WIN_REG_BASE_IDX_WR(win_idma, sz, MV_WIN_IDMA_SIZE)
WIN_REG_BASE_IDX_WR(win_idma, har, MV_WIN_IDMA_REMAP)
WIN_REG_BASE_IDX_WR(win_idma, cap, MV_WIN_IDMA_CAP)
WIN_REG_BASE_RD(win_idma, bare, 0xa80)
WIN_REG_BASE_WR(win_idma, bare, 0xa80)

WIN_REG_BASE_IDX_RD(win_sata, cr, MV_WIN_SATA_CTRL);
WIN_REG_BASE_IDX_RD(win_sata, br, MV_WIN_SATA_BASE);
WIN_REG_BASE_IDX_WR(win_sata, cr, MV_WIN_SATA_CTRL);
WIN_REG_BASE_IDX_WR(win_sata, br, MV_WIN_SATA_BASE);

WIN_REG_BASE_IDX_RD(win_sata_armada38x, sz, MV_WIN_SATA_SIZE_ARMADA38X);
WIN_REG_BASE_IDX_WR(win_sata_armada38x, sz, MV_WIN_SATA_SIZE_ARMADA38X);
WIN_REG_BASE_IDX_RD(win_sata_armada38x, cr, MV_WIN_SATA_CTRL_ARMADA38X);
WIN_REG_BASE_IDX_RD(win_sata_armada38x, br, MV_WIN_SATA_BASE_ARMADA38X);
WIN_REG_BASE_IDX_WR(win_sata_armada38x, cr, MV_WIN_SATA_CTRL_ARMADA38X);
WIN_REG_BASE_IDX_WR(win_sata_armada38x, br, MV_WIN_SATA_BASE_ARMADA38X);

WIN_REG_BASE_IDX_RD(win_sdhci, cr, MV_WIN_SDHCI_CTRL);
WIN_REG_BASE_IDX_RD(win_sdhci, br, MV_WIN_SDHCI_BASE);
WIN_REG_BASE_IDX_WR(win_sdhci, cr, MV_WIN_SDHCI_CTRL);
WIN_REG_BASE_IDX_WR(win_sdhci, br, MV_WIN_SDHCI_BASE);

#ifndef SOC_MV_DOVE
WIN_REG_IDX_RD(ddr_armv5, br, MV_WIN_DDR_BASE, MV_DDR_CADR_BASE)
WIN_REG_IDX_RD(ddr_armv5, sz, MV_WIN_DDR_SIZE, MV_DDR_CADR_BASE)
WIN_REG_IDX_WR(ddr_armv5, br, MV_WIN_DDR_BASE, MV_DDR_CADR_BASE)
WIN_REG_IDX_WR(ddr_armv5, sz, MV_WIN_DDR_SIZE, MV_DDR_CADR_BASE)

WIN_REG_IDX_RD(ddr_armv7, br, MV_WIN_DDR_BASE, MV_DDR_CADR_BASE_ARMV7)
WIN_REG_IDX_RD(ddr_armv7, sz, MV_WIN_DDR_SIZE, MV_DDR_CADR_BASE_ARMV7)
WIN_REG_IDX_WR(ddr_armv7, br, MV_WIN_DDR_BASE, MV_DDR_CADR_BASE_ARMV7)
WIN_REG_IDX_WR(ddr_armv7, sz, MV_WIN_DDR_SIZE, MV_DDR_CADR_BASE_ARMV7)

static inline uint32_t
ddr_br_read(int i)
{

	if (soc_decode_win_spec->ddr_br_read != NULL)
		return (soc_decode_win_spec->ddr_br_read(i));
	return (-1);
}

static inline uint32_t
ddr_sz_read(int i)
{

	if (soc_decode_win_spec->ddr_sz_read != NULL)
		return (soc_decode_win_spec->ddr_sz_read(i));
	return (-1);
}

static inline void
ddr_br_write(int i, uint32_t val)
{

	if (soc_decode_win_spec->ddr_br_write != NULL)
		soc_decode_win_spec->ddr_br_write(i, val);
}

static inline void
ddr_sz_write(int i, uint32_t val)
{

	if (soc_decode_win_spec->ddr_sz_write != NULL)
		soc_decode_win_spec->ddr_sz_write(i, val);
}
#else
/*
 * On 88F6781 (Dove) SoC DDR Controller is accessed through
 * single MBUS <-> AXI bridge. In this case we provide emulated
 * ddr_br_read() and ddr_sz_read() functions to keep compatibility
 * with common decoding windows setup code.
 */

static inline uint32_t ddr_br_read(int i)
{
	uint32_t mmap;

	/* Read Memory Address Map Register for CS i */
	mmap = bus_space_read_4(fdtbus_bs_tag, MV_DDR_CADR_BASE + (i * 0x10), 0);

	/* Return CS i base address */
	return (mmap & 0xFF000000);
}

static inline uint32_t ddr_sz_read(int i)
{
	uint32_t mmap, size;

	/* Read Memory Address Map Register for CS i */
	mmap = bus_space_read_4(fdtbus_bs_tag, MV_DDR_CADR_BASE + (i * 0x10), 0);

	/* Extract size of CS space in 64kB units */
	size = (1 << ((mmap >> 16) & 0x0F));

	/* Return CS size and enable/disable status */
	return (((size - 1) << 16) | (mmap & 0x01));
}
#endif

/**************************************************************************
 * Decode windows helper routines
 **************************************************************************/
void
soc_dump_decode_win(void)
{
	int i;

	for (i = 0; i < soc_decode_win_spec->mv_win_cpu_max; i++) {
		printf("CPU window#%d: c 0x%08x, b 0x%08x", i,
		    win_cpu_cr_read(i),
		    win_cpu_br_read(i));

		if (win_cpu_can_remap(i))
			printf(", rl 0x%08x, rh 0x%08x",
			    win_cpu_remap_l_read(i),
			    win_cpu_remap_h_read(i));

		printf("\n");
	}
	printf("Internal regs base: 0x%08x\n",
	    bus_space_read_4(fdtbus_bs_tag, MV_INTREGS_BASE, 0));

	for (i = 0; i < MV_WIN_DDR_MAX; i++)
		printf("DDR CS#%d: b 0x%08x, s 0x%08x\n", i,
		    ddr_br_read(i), ddr_sz_read(i));
}

/**************************************************************************
 * CPU windows routines
 **************************************************************************/
int
win_cpu_can_remap(int i)
{
	uint32_t dev, rev;

	soc_id(&dev, &rev);

	/* Depending on the SoC certain windows have remap capability */
	if ((dev == MV_DEV_88F5182 && i < 2) ||
	    (dev == MV_DEV_88F5281 && i < 4) ||
	    (dev == MV_DEV_88F6281 && i < 4) ||
	    (dev == MV_DEV_88F6282 && i < 4) ||
	    (dev == MV_DEV_88F6828 && i < 20) ||
	    (dev == MV_DEV_88F6820 && i < 20) ||
	    (dev == MV_DEV_88F6810 && i < 20) ||
	    (dev == MV_DEV_88RC8180 && i < 2) ||
	    (dev == MV_DEV_88F6781 && i < 4) ||
	    (dev == MV_DEV_MV78100_Z0 && i < 8) ||
	    ((dev & MV_DEV_FAMILY_MASK) == MV_DEV_DISCOVERY && i < 8))
		return (1);

	return (0);
}

/* XXX This should check for overlapping remap fields too.. */
int
decode_win_overlap(int win, int win_no, const struct decode_win *wintab)
{
	const struct decode_win *tab;
	int i;

	tab = wintab;

	for (i = 0; i < win_no; i++, tab++) {
		if (i == win)
			/* Skip self */
			continue;

		if ((tab->base + tab->size - 1) < (wintab + win)->base)
			continue;

		else if (((wintab + win)->base + (wintab + win)->size - 1) <
		    tab->base)
			continue;
		else
			return (i);
	}

	return (-1);
}

static int
decode_win_cpu_valid(void)
{
	int i, j, rv;
	uint32_t b, e, s;

	if (cpu_wins_no > soc_decode_win_spec->mv_win_cpu_max) {
		printf("CPU windows: too many entries: %d\n", cpu_wins_no);
		return (0);
	}

	rv = 1;
	for (i = 0; i < cpu_wins_no; i++) {

		if (cpu_wins[i].target == 0) {
			printf("CPU window#%d: DDR target window is not "
			    "supposed to be reprogrammed!\n", i);
			rv = 0;
		}

		if (cpu_wins[i].remap != ~0 && win_cpu_can_remap(i) != 1) {
			printf("CPU window#%d: not capable of remapping, but "
			    "val 0x%08x defined\n", i, cpu_wins[i].remap);
			rv = 0;
		}

		s = cpu_wins[i].size;
		b = cpu_wins[i].base;
		e = b + s - 1;
		if (s > (0xFFFFFFFF - b + 1)) {
			/*
			 * XXX this boundary check should account for 64bit
			 * and remapping..
			 */
			printf("CPU window#%d: no space for size 0x%08x at "
			    "0x%08x\n", i, s, b);
			rv = 0;
			continue;
		}

		if (b != rounddown2(b, s)) {
			printf("CPU window#%d: address 0x%08x is not aligned "
			    "to 0x%08x\n", i, b, s);
			rv = 0;
			continue;
		}

		j = decode_win_overlap(i, cpu_wins_no, &cpu_wins[0]);
		if (j >= 0) {
			printf("CPU window#%d: (0x%08x - 0x%08x) overlaps "
			    "with #%d (0x%08x - 0x%08x)\n", i, b, e, j,
			    cpu_wins[j].base,
			    cpu_wins[j].base + cpu_wins[j].size - 1);
			rv = 0;
		}
	}

	return (rv);
}

int
decode_win_cpu_set(int target, int attr, vm_paddr_t base, uint32_t size,
    vm_paddr_t remap)
{
	uint32_t br, cr;
	int win, i;

	if (remap == ~0) {
		win = soc_decode_win_spec->mv_win_cpu_max - 1;
		i = -1;
	} else {
		win = 0;
		i = 1;
	}

	while ((win >= 0) && (win < soc_decode_win_spec->mv_win_cpu_max)) {
		cr = win_cpu_cr_read(win);
		if ((cr & MV_WIN_CPU_ENABLE_BIT) == 0)
			break;
		if ((cr & ((0xff << MV_WIN_CPU_ATTR_SHIFT) |
		    (0x1f << MV_WIN_CPU_TARGET_SHIFT))) ==
		    ((attr << MV_WIN_CPU_ATTR_SHIFT) |
		    (target << MV_WIN_CPU_TARGET_SHIFT)))
			break;
		win += i;
	}
	if ((win < 0) || (win >= soc_decode_win_spec->mv_win_cpu_max) ||
	    ((remap != ~0) && (win_cpu_can_remap(win) == 0)))
		return (-1);

	br = base & 0xffff0000;
	win_cpu_br_write(win, br);

	if (win_cpu_can_remap(win)) {
		if (remap != ~0) {
			win_cpu_remap_l_write(win, remap & 0xffff0000);
			win_cpu_remap_h_write(win, 0);
		} else {
			/*
			 * Remap function is not used for a given window
			 * (capable of remapping) - set remap field with the
			 * same value as base.
			 */
			win_cpu_remap_l_write(win, base & 0xffff0000);
			win_cpu_remap_h_write(win, 0);
		}
	}

	cr = ((size - 1) & 0xffff0000) | (attr << MV_WIN_CPU_ATTR_SHIFT) |
	    (target << MV_WIN_CPU_TARGET_SHIFT) | MV_WIN_CPU_ENABLE_BIT;
	win_cpu_cr_write(win, cr);

	return (0);
}

static void
decode_win_cpu_setup(void)
{
	int i;

	/* Disable all CPU windows */
	for (i = 0; i < soc_decode_win_spec->mv_win_cpu_max; i++) {
		win_cpu_cr_write(i, 0);
		win_cpu_br_write(i, 0);
		if (win_cpu_can_remap(i)) {
			win_cpu_remap_l_write(i, 0);
			win_cpu_remap_h_write(i, 0);
		}
	}

	for (i = 0; i < cpu_wins_no; i++)
		if (cpu_wins[i].target > 0)
			decode_win_cpu_set(cpu_wins[i].target,
			    cpu_wins[i].attr, cpu_wins[i].base,
			    cpu_wins[i].size, cpu_wins[i].remap);

}

static int
decode_win_sdram_fixup(void)
{
	struct mem_region mr[FDT_MEM_REGIONS];
	uint8_t window_valid[MV_WIN_DDR_MAX];
	int mr_cnt, err, i, j;
	uint32_t valid_win_num = 0;

	/* Grab physical memory regions information from device tree. */
	err = fdt_get_mem_regions(mr, &mr_cnt, NULL);
	if (err != 0)
		return (err);

	for (i = 0; i < MV_WIN_DDR_MAX; i++)
		window_valid[i] = 0;

	/* Try to match entries from device tree with settings from u-boot */
	for (i = 0; i < mr_cnt; i++) {
		for (j = 0; j < MV_WIN_DDR_MAX; j++) {
			if (ddr_is_active(j) &&
			    (ddr_base(j) == mr[i].mr_start) &&
			    (ddr_size(j) == mr[i].mr_size)) {
				window_valid[j] = 1;
				valid_win_num++;
			}
		}
	}

	if (mr_cnt != valid_win_num)
		return (EINVAL);

	/* Destroy windows without corresponding device tree entry */
	for (j = 0; j < MV_WIN_DDR_MAX; j++) {
		if (ddr_is_active(j) && (window_valid[j] != 1)) {
			printf("Disabling SDRAM decoding window: %d\n", j);
			ddr_disable(j);
		}
	}

	return (0);
}
/*
 * Check if we're able to cover all active DDR banks.
 */
static int
decode_win_can_cover_ddr(int max)
{
	int i, c;

	c = 0;
	for (i = 0; i < MV_WIN_DDR_MAX; i++)
		if (ddr_is_active(i))
			c++;

	if (c > max) {
		printf("Unable to cover all active DDR banks: "
		    "%d, available windows: %d\n", c, max);
		return (0);
	}

	return (1);
}

/**************************************************************************
 * DDR windows routines
 **************************************************************************/
int
ddr_is_active(int i)
{

	if (ddr_sz_read(i) & 0x1)
		return (1);

	return (0);
}

void
ddr_disable(int i)
{

	ddr_sz_write(i, 0);
	ddr_br_write(i, 0);
}

uint32_t
ddr_base(int i)
{

	return (ddr_br_read(i) & 0xff000000);
}

uint32_t
ddr_size(int i)
{

	return ((ddr_sz_read(i) | 0x00ffffff) + 1);
}

uint32_t
ddr_attr(int i)
{
	uint32_t dev, rev, attr;

	soc_id(&dev, &rev);
	if (dev == MV_DEV_88RC8180)
		return ((ddr_sz_read(i) & 0xf0) >> 4);
	if (dev == MV_DEV_88F6781)
		return (0);

	attr = (i == 0 ? 0xe :
	    (i == 1 ? 0xd :
	    (i == 2 ? 0xb :
	    (i == 3 ? 0x7 : 0xff))));
	if (platform_io_coherent)
		attr |= 0x10;

	return (attr);
}

uint32_t
ddr_target(int i)
{
	uint32_t dev, rev;

	soc_id(&dev, &rev);
	if (dev == MV_DEV_88RC8180) {
		i = (ddr_sz_read(i) & 0xf0) >> 4;
		return (i == 0xe ? 0xc :
		    (i == 0xd ? 0xd :
		    (i == 0xb ? 0xe :
		    (i == 0x7 ? 0xf : 0xc))));
	}

	/*
	 * On SOCs other than 88RC8180 Mbus unit ID for
	 * DDR SDRAM controller is always 0x0.
	 */
	return (0);
}

/**************************************************************************
 * CESA windows routines
 **************************************************************************/
static int
decode_win_cesa_valid(void)
{

	return (decode_win_can_cover_ddr(MV_WIN_CESA_MAX));
}

static void
decode_win_cesa_dump(u_long base)
{
	int i;

	for (i = 0; i < MV_WIN_CESA_MAX; i++)
		printf("CESA window#%d: c 0x%08x, b 0x%08x\n", i,
		    win_cesa_cr_read(base, i), win_cesa_br_read(base, i));
}

/*
 * Set CESA decode windows.
 */
static void
decode_win_cesa_setup(u_long base)
{
	uint32_t br, cr;
	uint64_t size;
	int i, j;

	for (i = 0; i < MV_WIN_CESA_MAX; i++) {
		win_cesa_cr_write(base, i, 0);
		win_cesa_br_write(base, i, 0);
	}

	/* Only access to active DRAM banks is required */
	for (i = 0; i < MV_WIN_DDR_MAX; i++) {
		if (ddr_is_active(i)) {
			br = ddr_base(i);

			size = ddr_size(i);
			/*
			 * Armada 38x SoC's equipped with 4GB DRAM
			 * suffer freeze during CESA operation, if
			 * MBUS window opened at given DRAM CS reaches
			 * end of the address space. Apply a workaround
			 * by setting the window size to the closest possible
			 * value, i.e. divide it by 2.
			 */
			if ((soc_family == MV_SOC_ARMADA_38X) &&
			    (size + ddr_base(i) == 0x100000000ULL))
				size /= 2;

			cr = (((size - 1) & 0xffff0000) |
			    (ddr_attr(i) << IO_WIN_ATTR_SHIFT) |
			    (ddr_target(i) << IO_WIN_TGT_SHIFT) |
			    IO_WIN_ENA_MASK);

			/* Set the first free CESA window */
			for (j = 0; j < MV_WIN_CESA_MAX; j++) {
				if (win_cesa_cr_read(base, j) & 0x1)
					continue;

				win_cesa_br_write(base, j, br);
				win_cesa_cr_write(base, j, cr);
				break;
			}
		}
	}
}

static void
decode_win_a38x_cesa_setup(u_long base)
{
	decode_win_cesa_setup(base);
	decode_win_cesa_setup(base + MV_WIN_CESA_OFFSET);
}

static void
decode_win_a38x_cesa_dump(u_long base)
{
	decode_win_cesa_dump(base);
	decode_win_cesa_dump(base + MV_WIN_CESA_OFFSET);
}

/**************************************************************************
 * USB windows routines
 **************************************************************************/
static int
decode_win_usb_valid(void)
{

	return (decode_win_can_cover_ddr(MV_WIN_USB_MAX));
}

static void
decode_win_usb_dump(u_long base)
{
	int i;

	if (pm_is_disabled(CPU_PM_CTRL_USB(usb_port - 1)))
		return;

	for (i = 0; i < MV_WIN_USB_MAX; i++)
		printf("USB window#%d: c 0x%08x, b 0x%08x\n", i,
		    win_usb_cr_read(base, i), win_usb_br_read(base, i));
}

/*
 * Set USB decode windows.
 */
static void
decode_win_usb_setup(u_long base)
{
	uint32_t br, cr;
	int i, j;

	if (pm_is_disabled(CPU_PM_CTRL_USB(usb_port)))
		return;

	usb_port++;

	for (i = 0; i < MV_WIN_USB_MAX; i++) {
		win_usb_cr_write(base, i, 0);
		win_usb_br_write(base, i, 0);
	}

	/* Only access to active DRAM banks is required */
	for (i = 0; i < MV_WIN_DDR_MAX; i++) {
		if (ddr_is_active(i)) {
			br = ddr_base(i);
			/*
			 * XXX for 6281 we should handle Mbus write
			 * burst limit field in the ctrl reg
			 */
			cr = (((ddr_size(i) - 1) & 0xffff0000) |
			    (ddr_attr(i) << 8) |
			    (ddr_target(i) << 4) | 1);

			/* Set the first free USB window */
			for (j = 0; j < MV_WIN_USB_MAX; j++) {
				if (win_usb_cr_read(base, j) & 0x1)
					continue;

				win_usb_br_write(base, j, br);
				win_usb_cr_write(base, j, cr);
				break;
			}
		}
	}
}

/**************************************************************************
 * USB3 windows routines
 **************************************************************************/
static int
decode_win_usb3_valid(void)
{

	return (decode_win_can_cover_ddr(MV_WIN_USB3_MAX));
}

static void
decode_win_usb3_dump(u_long base)
{
	int i;

	for (i = 0; i < MV_WIN_USB3_MAX; i++)
		printf("USB3.0 window#%d: c 0x%08x, b 0x%08x\n", i,
		    win_usb3_cr_read(base, i), win_usb3_br_read(base, i));
}

/*
 * Set USB3 decode windows
 */
static void
decode_win_usb3_setup(u_long base)
{
	uint32_t br, cr;
	int i, j;

	for (i = 0; i < MV_WIN_USB3_MAX; i++) {
		win_usb3_cr_write(base, i, 0);
		win_usb3_br_write(base, i, 0);
	}

	/* Only access to active DRAM banks is required */
	for (i = 0; i < MV_WIN_DDR_MAX; i++) {
		if (ddr_is_active(i)) {
			br = ddr_base(i);
			cr = (((ddr_size(i) - 1) &
			    (IO_WIN_SIZE_MASK << IO_WIN_SIZE_SHIFT)) |
			    (ddr_attr(i) << IO_WIN_ATTR_SHIFT) |
			    (ddr_target(i) << IO_WIN_TGT_SHIFT) |
			    IO_WIN_ENA_MASK);

			/* Set the first free USB3.0 window */
			for (j = 0; j < MV_WIN_USB3_MAX; j++) {
				if (win_usb3_cr_read(base, j) & IO_WIN_ENA_MASK)
					continue;

				win_usb3_br_write(base, j, br);
				win_usb3_cr_write(base, j, cr);
				break;
			}
		}
	}
}


/**************************************************************************
 * ETH windows routines
 **************************************************************************/

static int
win_eth_can_remap(int i)
{

	/* ETH encode windows 0-3 have remap capability */
	if (i < 4)
		return (1);

	return (0);
}

static int
eth_bare_read(uint32_t base, int i)
{
	uint32_t v;

	v = win_eth_bare_read(base);
	v &= (1 << i);

	return (v >> i);
}

static void
eth_bare_write(uint32_t base, int i, int val)
{
	uint32_t v;

	v = win_eth_bare_read(base);
	v &= ~(1 << i);
	v |= (val << i);
	win_eth_bare_write(base, v);
}

static void
eth_epap_write(uint32_t base, int i, int val)
{
	uint32_t v;

	v = win_eth_epap_read(base);
	v &= ~(0x3 << (i * 2));
	v |= (val << (i * 2));
	win_eth_epap_write(base, v);
}

static void
decode_win_eth_dump(u_long base)
{
	int i;

	if (pm_is_disabled(CPU_PM_CTRL_GE(eth_port - 1)))
		return;

	for (i = 0; i < MV_WIN_ETH_MAX; i++) {
		printf("ETH window#%d: b 0x%08x, s 0x%08x", i,
		    win_eth_br_read(base, i),
		    win_eth_sz_read(base, i));

		if (win_eth_can_remap(i))
			printf(", ha 0x%08x",
			    win_eth_har_read(base, i));

		printf("\n");
	}
	printf("ETH windows: bare 0x%08x, epap 0x%08x\n",
	    win_eth_bare_read(base),
	    win_eth_epap_read(base));
}

#define MV_WIN_ETH_DDR_TRGT(n)	ddr_target(n)

static void
decode_win_eth_setup(u_long base)
{
	uint32_t br, sz;
	int i, j;

	if (pm_is_disabled(CPU_PM_CTRL_GE(eth_port)))
		return;

	eth_port++;

	/* Disable, clear and revoke protection for all ETH windows */
	for (i = 0; i < MV_WIN_ETH_MAX; i++) {

		eth_bare_write(base, i, 1);
		eth_epap_write(base, i, 0);
		win_eth_br_write(base, i, 0);
		win_eth_sz_write(base, i, 0);
		if (win_eth_can_remap(i))
			win_eth_har_write(base, i, 0);
	}

	/* Only access to active DRAM banks is required */
	for (i = 0; i < MV_WIN_DDR_MAX; i++)
		if (ddr_is_active(i)) {

			br = ddr_base(i) | (ddr_attr(i) << 8) | MV_WIN_ETH_DDR_TRGT(i);
			sz = ((ddr_size(i) - 1) & 0xffff0000);

			/* Set the first free ETH window */
			for (j = 0; j < MV_WIN_ETH_MAX; j++) {
				if (eth_bare_read(base, j) == 0)
					continue;

				win_eth_br_write(base, j, br);
				win_eth_sz_write(base, j, sz);

				/* XXX remapping ETH windows not supported */

				/* Set protection RW */
				eth_epap_write(base, j, 0x3);

				/* Enable window */
				eth_bare_write(base, j, 0);
				break;
			}
		}
}

static void
decode_win_neta_dump(u_long base)
{

	decode_win_eth_dump(base + MV_WIN_NETA_OFFSET);
}

static void
decode_win_neta_setup(u_long base)
{

	decode_win_eth_setup(base + MV_WIN_NETA_OFFSET);
}

static int
decode_win_eth_valid(void)
{

	return (decode_win_can_cover_ddr(MV_WIN_ETH_MAX));
}

/**************************************************************************
 * PCIE windows routines
 **************************************************************************/
static void
decode_win_pcie_dump(u_long base)
{
	int i;

	printf("PCIE windows base 0x%08lx\n", base);
	for (i = 0; i < MV_WIN_PCIE_MAX; i++)
		printf("PCIE window#%d: cr 0x%08x br 0x%08x remap 0x%08x\n",
		    i, win_pcie_cr_read(base, i),
		    win_pcie_br_read(base, i), win_pcie_remap_read(base, i));

	for (i = 0; i < MV_PCIE_BAR_MAX; i++)
		printf("PCIE bar#%d: cr 0x%08x br 0x%08x brh 0x%08x\n",
		    i, pcie_bar_cr_read(base, i),
		    pcie_bar_br_read(base, i), pcie_bar_brh_read(base, i));
}

void
decode_win_pcie_setup(u_long base)
{
	uint32_t size = 0, ddrbase = ~0;
	uint32_t cr, br;
	int i, j;

	for (i = 0; i < MV_PCIE_BAR_MAX; i++) {
		pcie_bar_br_write(base, i,
		    MV_PCIE_BAR_64BIT | MV_PCIE_BAR_PREFETCH_EN);
		if (i < 3)
			pcie_bar_brh_write(base, i, 0);
		if (i > 0)
			pcie_bar_cr_write(base, i, 0);
	}

	for (i = 0; i < MV_WIN_PCIE_MAX; i++) {
		win_pcie_cr_write(base, i, 0);
		win_pcie_br_write(base, i, 0);
		win_pcie_remap_write(base, i, 0);
	}

	/* On End-Point only set BAR size to 1MB regardless of DDR size */
	if ((bus_space_read_4(fdtbus_bs_tag, base, MV_PCIE_CONTROL)
	    & MV_PCIE_ROOT_CMPLX) == 0) {
		pcie_bar_cr_write(base, 1, 0xf0000 | 1);
		return;
	}

	for (i = 0; i < MV_WIN_DDR_MAX; i++) {
		if (ddr_is_active(i)) {
			/* Map DDR to BAR 1 */
			cr = (ddr_size(i) - 1) & 0xffff0000;
			size += ddr_size(i) & 0xffff0000;
			cr |= (ddr_attr(i) << 8) | (ddr_target(i) << 4) | 1;
			br = ddr_base(i);
			if (br < ddrbase)
				ddrbase = br;

			/* Use the first available PCIE window */
			for (j = 0; j < MV_WIN_PCIE_MAX; j++) {
				if (win_pcie_cr_read(base, j) != 0)
					continue;

				win_pcie_br_write(base, j, br);
				win_pcie_cr_write(base, j, cr);
				break;
			}
		}
	}

	/*
	 * Upper 16 bits in BAR register is interpreted as BAR size
	 * (in 64 kB units) plus 64kB, so subtract 0x10000
	 * form value passed to register to get correct value.
	 */
	size -= 0x10000;
	pcie_bar_cr_write(base, 1, size | 1);
	pcie_bar_br_write(base, 1, ddrbase |
	    MV_PCIE_BAR_64BIT | MV_PCIE_BAR_PREFETCH_EN);
	pcie_bar_br_write(base, 0, fdt_immr_pa |
	    MV_PCIE_BAR_64BIT | MV_PCIE_BAR_PREFETCH_EN);
}

static int
decode_win_pcie_valid(void)
{

	return (decode_win_can_cover_ddr(MV_WIN_PCIE_MAX));
}

/**************************************************************************
 * IDMA windows routines
 **************************************************************************/
#if defined(SOC_MV_ORION) || defined(SOC_MV_DISCOVERY)
static int
idma_bare_read(u_long base, int i)
{
	uint32_t v;

	v = win_idma_bare_read(base);
	v &= (1 << i);

	return (v >> i);
}

static void
idma_bare_write(u_long base, int i, int val)
{
	uint32_t v;

	v = win_idma_bare_read(base);
	v &= ~(1 << i);
	v |= (val << i);
	win_idma_bare_write(base, v);
}

/*
 * Sets channel protection 'val' for window 'w' on channel 'c'
 */
static void
idma_cap_write(u_long base, int c, int w, int val)
{
	uint32_t v;

	v = win_idma_cap_read(base, c);
	v &= ~(0x3 << (w * 2));
	v |= (val << (w * 2));
	win_idma_cap_write(base, c, v);
}

/*
 * Set protection 'val' on all channels for window 'w'
 */
static void
idma_set_prot(u_long base, int w, int val)
{
	int c;

	for (c = 0; c < MV_IDMA_CHAN_MAX; c++)
		idma_cap_write(base, c, w, val);
}

static int
win_idma_can_remap(int i)
{

	/* IDMA decode windows 0-3 have remap capability */
	if (i < 4)
		return (1);

	return (0);
}

void
decode_win_idma_setup(u_long base)
{
	uint32_t br, sz;
	int i, j;

	if (pm_is_disabled(CPU_PM_CTRL_IDMA))
		return;
	/*
	 * Disable and clear all IDMA windows, revoke protection for all channels
	 */
	for (i = 0; i < MV_WIN_IDMA_MAX; i++) {

		idma_bare_write(base, i, 1);
		win_idma_br_write(base, i, 0);
		win_idma_sz_write(base, i, 0);
		if (win_idma_can_remap(i) == 1)
			win_idma_har_write(base, i, 0);
	}
	for (i = 0; i < MV_IDMA_CHAN_MAX; i++)
		win_idma_cap_write(base, i, 0);

	/*
	 * Set up access to all active DRAM banks
	 */
	for (i = 0; i < MV_WIN_DDR_MAX; i++)
		if (ddr_is_active(i)) {
			br = ddr_base(i) | (ddr_attr(i) << 8) | ddr_target(i);
			sz = ((ddr_size(i) - 1) & 0xffff0000);

			/* Place DDR entries in non-remapped windows */
			for (j = 0; j < MV_WIN_IDMA_MAX; j++)
				if (win_idma_can_remap(j) != 1 &&
				    idma_bare_read(base, j) == 1) {

					/* Configure window */
					win_idma_br_write(base, j, br);
					win_idma_sz_write(base, j, sz);

					/* Set protection RW on all channels */
					idma_set_prot(base, j, 0x3);

					/* Enable window */
					idma_bare_write(base, j, 0);
					break;
				}
		}

	/*
	 * Remaining targets -- from statically defined table
	 */
	for (i = 0; i < idma_wins_no; i++)
		if (idma_wins[i].target > 0) {
			br = (idma_wins[i].base & 0xffff0000) |
			    (idma_wins[i].attr << 8) | idma_wins[i].target;
			sz = ((idma_wins[i].size - 1) & 0xffff0000);

			/* Set the first free IDMA window */
			for (j = 0; j < MV_WIN_IDMA_MAX; j++) {
				if (idma_bare_read(base, j) == 0)
					continue;

				/* Configure window */
				win_idma_br_write(base, j, br);
				win_idma_sz_write(base, j, sz);
				if (win_idma_can_remap(j) &&
				    idma_wins[j].remap >= 0)
					win_idma_har_write(base, j,
					    idma_wins[j].remap);

				/* Set protection RW on all channels */
				idma_set_prot(base, j, 0x3);

				/* Enable window */
				idma_bare_write(base, j, 0);
				break;
			}
		}
}

int
decode_win_idma_valid(void)
{
	const struct decode_win *wintab;
	int c, i, j, rv;
	uint32_t b, e, s;

	if (idma_wins_no > MV_WIN_IDMA_MAX) {
		printf("IDMA windows: too many entries: %d\n", idma_wins_no);
		return (0);
	}
	for (i = 0, c = 0; i < MV_WIN_DDR_MAX; i++)
		if (ddr_is_active(i))
			c++;

	if (idma_wins_no > (MV_WIN_IDMA_MAX - c)) {
		printf("IDMA windows: too many entries: %d, available: %d\n",
		    idma_wins_no, MV_WIN_IDMA_MAX - c);
		return (0);
	}

	wintab = idma_wins;
	rv = 1;
	for (i = 0; i < idma_wins_no; i++, wintab++) {

		if (wintab->target == 0) {
			printf("IDMA window#%d: DDR target window is not "
			    "supposed to be reprogrammed!\n", i);
			rv = 0;
		}

		if (wintab->remap >= 0 && win_cpu_can_remap(i) != 1) {
			printf("IDMA window#%d: not capable of remapping, but "
			    "val 0x%08x defined\n", i, wintab->remap);
			rv = 0;
		}

		s = wintab->size;
		b = wintab->base;
		e = b + s - 1;
		if (s > (0xFFFFFFFF - b + 1)) {
			/* XXX this boundary check should account for 64bit and
			 * remapping.. */
			printf("IDMA window#%d: no space for size 0x%08x at "
			    "0x%08x\n", i, s, b);
			rv = 0;
			continue;
		}

		j = decode_win_overlap(i, idma_wins_no, &idma_wins[0]);
		if (j >= 0) {
			printf("IDMA window#%d: (0x%08x - 0x%08x) overlaps "
			    "with #%d (0x%08x - 0x%08x)\n", i, b, e, j,
			    idma_wins[j].base,
			    idma_wins[j].base + idma_wins[j].size - 1);
			rv = 0;
		}
	}

	return (rv);
}

void
decode_win_idma_dump(u_long base)
{
	int i;

	if (pm_is_disabled(CPU_PM_CTRL_IDMA))
		return;

	for (i = 0; i < MV_WIN_IDMA_MAX; i++) {
		printf("IDMA window#%d: b 0x%08x, s 0x%08x", i,
		    win_idma_br_read(base, i), win_idma_sz_read(base, i));
		
		if (win_idma_can_remap(i))
			printf(", ha 0x%08x", win_idma_har_read(base, i));

		printf("\n");
	}
	for (i = 0; i < MV_IDMA_CHAN_MAX; i++)
		printf("IDMA channel#%d: ap 0x%08x\n", i,
		    win_idma_cap_read(base, i));
	printf("IDMA windows: bare 0x%08x\n", win_idma_bare_read(base));
}
#else

/* Provide dummy functions to satisfy the build for SoCs not equipped with IDMA */
int
decode_win_idma_valid(void)
{

	return (1);
}

void
decode_win_idma_setup(u_long base)
{
}

void
decode_win_idma_dump(u_long base)
{
}
#endif

/**************************************************************************
 * XOR windows routines
 **************************************************************************/
#if defined(SOC_MV_KIRKWOOD) || defined(SOC_MV_DISCOVERY)
static int
xor_ctrl_read(u_long base, int i, int c, int e)
{
	uint32_t v;
	v = win_xor_ctrl_read(base, c, e);
	v &= (1 << i);

	return (v >> i);
}

static void
xor_ctrl_write(u_long base, int i, int c, int e, int val)
{
	uint32_t v;

	v = win_xor_ctrl_read(base, c, e);
	v &= ~(1 << i);
	v |= (val << i);
	win_xor_ctrl_write(base, c, e, v);
}

/*
 * Set channel protection 'val' for window 'w' on channel 'c'
 */
static void
xor_chan_write(u_long base, int c, int e, int w, int val)
{
	uint32_t v;

	v = win_xor_ctrl_read(base, c, e);
	v &= ~(0x3 << (w * 2 + 16));
	v |= (val << (w * 2 + 16));
	win_xor_ctrl_write(base, c, e, v);
}

/*
 * Set protection 'val' on all channels for window 'w' on engine 'e'
 */
static void
xor_set_prot(u_long base, int w, int e, int val)
{
	int c;

	for (c = 0; c < MV_XOR_CHAN_MAX; c++)
		xor_chan_write(base, c, e, w, val);
}

static int
win_xor_can_remap(int i)
{

	/* XOR decode windows 0-3 have remap capability */
	if (i < 4)
		return (1);

	return (0);
}

static int
xor_max_eng(void)
{
	uint32_t dev, rev;

	soc_id(&dev, &rev);
	switch (dev) {
	case MV_DEV_88F6281:
	case MV_DEV_88F6282:
	case MV_DEV_MV78130:
	case MV_DEV_MV78160:
	case MV_DEV_MV78230:
	case MV_DEV_MV78260:
	case MV_DEV_MV78460:
		return (2);
	case MV_DEV_MV78100:
	case MV_DEV_MV78100_Z0:
		return (1);
	default:
		return (0);
	}
}

static void
xor_active_dram(u_long base, int c, int e, int *window)
{
	uint32_t br, sz;
	int i, m, w;

	/*
	 * Set up access to all active DRAM banks
	 */
	m = xor_max_eng();
	for (i = 0; i < m; i++)
		if (ddr_is_active(i)) {
			br = ddr_base(i) | (ddr_attr(i) << 8) |
			    ddr_target(i);
			sz = ((ddr_size(i) - 1) & 0xffff0000);

			/* Place DDR entries in non-remapped windows */
			for (w = 0; w < MV_WIN_XOR_MAX; w++)
				if (win_xor_can_remap(w) != 1 &&
				    (xor_ctrl_read(base, w, c, e) == 0) &&
				    w > *window) {
					/* Configure window */
					win_xor_br_write(base, w, e, br);
					win_xor_sz_write(base, w, e, sz);

					/* Set protection RW on all channels */
					xor_set_prot(base, w, e, 0x3);

					/* Enable window */
					xor_ctrl_write(base, w, c, e, 1);
					(*window)++;
					break;
				}
		}
}

void
decode_win_xor_setup(u_long base)
{
	uint32_t br, sz;
	int i, j, z, e = 1, m, window;

	if (pm_is_disabled(CPU_PM_CTRL_XOR))
		return;

	/*
	 * Disable and clear all XOR windows, revoke protection for all
	 * channels
	 */
	m = xor_max_eng();
	for (j = 0; j < m; j++, e--) {

		/* Number of non-remaped windows */
		window = MV_XOR_NON_REMAP - 1;

		for (i = 0; i < MV_WIN_XOR_MAX; i++) {
			win_xor_br_write(base, i, e, 0);
			win_xor_sz_write(base, i, e, 0);
		}

		if (win_xor_can_remap(i) == 1)
			win_xor_har_write(base, i, e, 0);

		for (i = 0; i < MV_XOR_CHAN_MAX; i++) {
			win_xor_ctrl_write(base, i, e, 0);
			xor_active_dram(base, i, e, &window);
		}

		/*
		 * Remaining targets -- from a statically defined table
		 */
		for (i = 0; i < xor_wins_no; i++)
			if (xor_wins[i].target > 0) {
				br = (xor_wins[i].base & 0xffff0000) |
				    (xor_wins[i].attr << 8) |
				    xor_wins[i].target;
				sz = ((xor_wins[i].size - 1) & 0xffff0000);

				/* Set the first free XOR window */
				for (z = 0; z < MV_WIN_XOR_MAX; z++) {
					if (xor_ctrl_read(base, z, 0, e) &&
					    xor_ctrl_read(base, z, 1, e))
						continue;

					/* Configure window */
					win_xor_br_write(base, z, e, br);
					win_xor_sz_write(base, z, e, sz);
					if (win_xor_can_remap(z) &&
					    xor_wins[z].remap >= 0)
						win_xor_har_write(base, z, e,
						    xor_wins[z].remap);

					/* Set protection RW on all channels */
					xor_set_prot(base, z, e, 0x3);

					/* Enable window */
					xor_ctrl_write(base, z, 0, e, 1);
					xor_ctrl_write(base, z, 1, e, 1);
					break;
				}
			}
	}
}

int
decode_win_xor_valid(void)
{
	const struct decode_win *wintab;
	int c, i, j, rv;
	uint32_t b, e, s;

	if (xor_wins_no > MV_WIN_XOR_MAX) {
		printf("XOR windows: too many entries: %d\n", xor_wins_no);
		return (0);
	}
	for (i = 0, c = 0; i < MV_WIN_DDR_MAX; i++)
		if (ddr_is_active(i))
			c++;

	if (xor_wins_no > (MV_WIN_XOR_MAX - c)) {
		printf("XOR windows: too many entries: %d, available: %d\n",
		    xor_wins_no, MV_WIN_IDMA_MAX - c);
		return (0);
	}

	wintab = xor_wins;
	rv = 1;
	for (i = 0; i < xor_wins_no; i++, wintab++) {

		if (wintab->target == 0) {
			printf("XOR window#%d: DDR target window is not "
			    "supposed to be reprogrammed!\n", i);
			rv = 0;
		}

		if (wintab->remap >= 0 && win_cpu_can_remap(i) != 1) {
			printf("XOR window#%d: not capable of remapping, but "
			    "val 0x%08x defined\n", i, wintab->remap);
			rv = 0;
		}

		s = wintab->size;
		b = wintab->base;
		e = b + s - 1;
		if (s > (0xFFFFFFFF - b + 1)) {
			/*
			 * XXX this boundary check should account for 64bit
			 * and remapping..
			 */
			printf("XOR window#%d: no space for size 0x%08x at "
			    "0x%08x\n", i, s, b);
			rv = 0;
			continue;
		}

		j = decode_win_overlap(i, xor_wins_no, &xor_wins[0]);
		if (j >= 0) {
			printf("XOR window#%d: (0x%08x - 0x%08x) overlaps "
			    "with #%d (0x%08x - 0x%08x)\n", i, b, e, j,
			    xor_wins[j].base,
			    xor_wins[j].base + xor_wins[j].size - 1);
			rv = 0;
		}
	}

	return (rv);
}

void
decode_win_xor_dump(u_long base)
{
	int i, j;
	int e = 1;

	if (pm_is_disabled(CPU_PM_CTRL_XOR))
		return;

	for (j = 0; j < xor_max_eng(); j++, e--) {
		for (i = 0; i < MV_WIN_XOR_MAX; i++) {
			printf("XOR window#%d: b 0x%08x, s 0x%08x", i,
			    win_xor_br_read(base, i, e), win_xor_sz_read(base, i, e));

			if (win_xor_can_remap(i))
				printf(", ha 0x%08x", win_xor_har_read(base, i, e));

			printf("\n");
		}
		for (i = 0; i < MV_XOR_CHAN_MAX; i++)
			printf("XOR control#%d: 0x%08x\n", i,
			    win_xor_ctrl_read(base, i, e));
	}
}

#else
/* Provide dummy functions to satisfy the build for SoCs not equipped with XOR */
static int
decode_win_xor_valid(void)
{

	return (1);
}

static void
decode_win_xor_setup(u_long base)
{
}

static void
decode_win_xor_dump(u_long base)
{
}
#endif

/**************************************************************************
 * SATA windows routines
 **************************************************************************/
static void
decode_win_sata_setup(u_long base)
{
	uint32_t cr, br;
	int i, j;

	if (pm_is_disabled(CPU_PM_CTRL_SATA))
		return;

	for (i = 0; i < MV_WIN_SATA_MAX; i++) {
		win_sata_cr_write(base, i, 0);
		win_sata_br_write(base, i, 0);
	}

	for (i = 0; i < MV_WIN_DDR_MAX; i++)
		if (ddr_is_active(i)) {
			cr = ((ddr_size(i) - 1) & 0xffff0000) |
			    (ddr_attr(i) << 8) | (ddr_target(i) << 4) | 1;
			br = ddr_base(i);

			/* Use the first available SATA window */
			for (j = 0; j < MV_WIN_SATA_MAX; j++) {
				if ((win_sata_cr_read(base, j) & 1) != 0)
					continue;

				win_sata_br_write(base, j, br);
				win_sata_cr_write(base, j, cr);
				break;
			}
		}
}

/*
 * Configure AHCI decoding windows
 */
static void
decode_win_ahci_setup(u_long base)
{
	uint32_t br, cr, sz;
	int i, j;

	for (i = 0; i < MV_WIN_SATA_MAX_ARMADA38X; i++) {
		win_sata_armada38x_cr_write(base, i, 0);
		win_sata_armada38x_br_write(base, i, 0);
		win_sata_armada38x_sz_write(base, i, 0);
	}

	for (i = 0; i < MV_WIN_DDR_MAX; i++) {
		if (ddr_is_active(i)) {
			cr = (ddr_attr(i) << IO_WIN_ATTR_SHIFT) |
			    (ddr_target(i) << IO_WIN_TGT_SHIFT) |
			    IO_WIN_ENA_MASK;
			br = ddr_base(i);
			sz = (ddr_size(i) - 1) &
			    (IO_WIN_SIZE_MASK << IO_WIN_SIZE_SHIFT);

			/* Use first available SATA window */
			for (j = 0; j < MV_WIN_SATA_MAX_ARMADA38X; j++) {
				if (win_sata_armada38x_cr_read(base, j) & IO_WIN_ENA_MASK)
					continue;

				/* BASE is set to DRAM base (0x00000000) */
				win_sata_armada38x_br_write(base, j, br);
				/* CTRL targets DRAM ctrl with 0x0E or 0x0D */
				win_sata_armada38x_cr_write(base, j, cr);
				/* SIZE is set to 16MB - max value */
				win_sata_armada38x_sz_write(base, j, sz);
				break;
			}
		}
	}
}

static void
decode_win_ahci_dump(u_long base)
{
	int i;

	for (i = 0; i < MV_WIN_SATA_MAX_ARMADA38X; i++)
		printf("SATA window#%d: cr 0x%08x, br 0x%08x, sz 0x%08x\n", i,
		    win_sata_armada38x_cr_read(base, i), win_sata_br_read(base, i),
		    win_sata_armada38x_sz_read(base,i));
}

static int
decode_win_sata_valid(void)
{
	uint32_t dev, rev;

	soc_id(&dev, &rev);
	if (dev == MV_DEV_88F5281)
		return (1);

	return (decode_win_can_cover_ddr(MV_WIN_SATA_MAX));
}

static void
decode_win_sdhci_setup(u_long base)
{
	uint32_t cr, br;
	int i, j;

	for (i = 0; i < MV_WIN_SDHCI_MAX; i++) {
		win_sdhci_cr_write(base, i, 0);
		win_sdhci_br_write(base, i, 0);
	}

	for (i = 0; i < MV_WIN_DDR_MAX; i++)
		if (ddr_is_active(i)) {
			br = ddr_base(i);
			cr = (((ddr_size(i) - 1) &
			    (IO_WIN_SIZE_MASK << IO_WIN_SIZE_SHIFT)) |
			    (ddr_attr(i) << IO_WIN_ATTR_SHIFT) |
			    (ddr_target(i) << IO_WIN_TGT_SHIFT) |
			    IO_WIN_ENA_MASK);

			/* Use the first available SDHCI window */
			for (j = 0; j < MV_WIN_SDHCI_MAX; j++) {
				if (win_sdhci_cr_read(base, j) & IO_WIN_ENA_MASK)
					continue;

				win_sdhci_cr_write(base, j, cr);
				win_sdhci_br_write(base, j, br);
				break;
			}
		}
}

static void
decode_win_sdhci_dump(u_long base)
{
	int i;

	for (i = 0; i < MV_WIN_SDHCI_MAX; i++)
		printf("SDHCI window#%d: c 0x%08x, b 0x%08x\n", i,
		    win_sdhci_cr_read(base, i), win_sdhci_br_read(base, i));
}

static int
decode_win_sdhci_valid(void)
{

	return (decode_win_can_cover_ddr(MV_WIN_SDHCI_MAX));
}

/**************************************************************************
 * FDT parsing routines.
 **************************************************************************/

static int
fdt_get_ranges(const char *nodename, void *buf, int size, int *tuples,
    int *tuplesize)
{
	phandle_t node;
	pcell_t addr_cells, par_addr_cells, size_cells;
	int len, tuple_size, tuples_count;

	node = OF_finddevice(nodename);
	if (node == -1)
		return (EINVAL);

	if ((fdt_addrsize_cells(node, &addr_cells, &size_cells)) != 0)
		return (ENXIO);

	par_addr_cells = fdt_parent_addr_cells(node);
	if (par_addr_cells > 2)
		return (ERANGE);

	tuple_size = sizeof(pcell_t) * (addr_cells + par_addr_cells +
	    size_cells);

	/* Note the OF_getprop_alloc() cannot be used at this early stage. */
	len = OF_getprop(node, "ranges", buf, size);

	/*
	 * XXX this does not handle the empty 'ranges;' case, which is
	 * legitimate and should be allowed.
	 */
	tuples_count = len / tuple_size;
	if (tuples_count <= 0)
		return (ERANGE);

	if (par_addr_cells > 2 || addr_cells > 2 || size_cells > 2)
		return (ERANGE);

	*tuples = tuples_count;
	*tuplesize = tuple_size;
	return (0);
}

static int
win_cpu_from_dt(void)
{
	pcell_t ranges[48];
	phandle_t node;
	int i, entry_size, err, t, tuple_size, tuples;
	u_long sram_base, sram_size;

	t = 0;
	/* Retrieve 'ranges' property of '/localbus' node. */
	if ((err = fdt_get_ranges("/localbus", ranges, sizeof(ranges),
	    &tuples, &tuple_size)) == 0) {
		/*
		 * Fill CPU decode windows table.
		 */
		bzero((void *)&cpu_win_tbl, sizeof(cpu_win_tbl));

		entry_size = tuple_size / sizeof(pcell_t);
		cpu_wins_no = tuples;

		/* Check range */
		if (tuples > nitems(cpu_win_tbl)) {
			debugf("too many tuples to fit into cpu_win_tbl\n");
			return (ENOMEM);
		}

		for (i = 0, t = 0; t < tuples; i += entry_size, t++) {
			cpu_win_tbl[t].target = 1;
			cpu_win_tbl[t].attr = fdt32_to_cpu(ranges[i + 1]);
			cpu_win_tbl[t].base = fdt32_to_cpu(ranges[i + 2]);
			cpu_win_tbl[t].size = fdt32_to_cpu(ranges[i + 3]);
			cpu_win_tbl[t].remap = ~0;
			debugf("target = 0x%0x attr = 0x%0x base = 0x%0x "
			    "size = 0x%0x remap = 0x%0x\n",
			    cpu_win_tbl[t].target,
			    cpu_win_tbl[t].attr, cpu_win_tbl[t].base,
			    cpu_win_tbl[t].size, cpu_win_tbl[t].remap);
		}
	}

	/*
	 * Retrieve CESA SRAM data.
	 */
	if ((node = OF_finddevice("sram")) != -1)
		if (ofw_bus_node_is_compatible(node, "mrvl,cesa-sram"))
			goto moveon;

	if ((node = OF_finddevice("/")) == -1)
		return (ENXIO);

	if ((node = fdt_find_compatible(node, "mrvl,cesa-sram", 0)) == 0)
		/* SRAM block is not always present. */
		return (0);
moveon:
	sram_base = sram_size = 0;
	if (fdt_regsize(node, &sram_base, &sram_size) != 0)
		return (EINVAL);

	/* Check range */
	if (t >= nitems(cpu_win_tbl)) {
		debugf("cannot fit CESA tuple into cpu_win_tbl\n");
		return (ENOMEM);
	}

	cpu_win_tbl[t].target = soc_decode_win_spec->win_cesa_target;
	if (soc_family == MV_SOC_ARMADA_38X)
		cpu_win_tbl[t].attr = soc_decode_win_spec->win_cesa_attr(0);
	else
		cpu_win_tbl[t].attr = soc_decode_win_spec->win_cesa_attr(1);
	cpu_win_tbl[t].base = sram_base;
	cpu_win_tbl[t].size = sram_size;
	cpu_win_tbl[t].remap = ~0;
	cpu_wins_no++;
	debugf("sram: base = 0x%0lx size = 0x%0lx\n", sram_base, sram_size);

	/* Check if there is a second CESA node */
	while ((node = OF_peer(node)) != 0) {
		if (ofw_bus_node_is_compatible(node, "mrvl,cesa-sram")) {
			if (fdt_regsize(node, &sram_base, &sram_size) != 0)
				return (EINVAL);
			break;
		}
	}

	if (node == 0)
		return (0);

	t++;
	if (t >= nitems(cpu_win_tbl)) {
		debugf("cannot fit CESA tuple into cpu_win_tbl\n");
		return (ENOMEM);
	}

	/* Configure window for CESA1 */
	cpu_win_tbl[t].target = soc_decode_win_spec->win_cesa_target;
	cpu_win_tbl[t].attr = soc_decode_win_spec->win_cesa_attr(1);
	cpu_win_tbl[t].base = sram_base;
	cpu_win_tbl[t].size = sram_size;
	cpu_win_tbl[t].remap = ~0;
	cpu_wins_no++;
	debugf("sram: base = 0x%0lx size = 0x%0lx\n", sram_base, sram_size);

	return (0);
}

static int
fdt_win_process(phandle_t child)
{
	int i, ret;

	for (i = 0; soc_nodes[i].compat != NULL; i++) {
		/* Setup only for enabled devices */
		if (ofw_bus_node_status_okay(child) == 0)
			continue;

		if (!ofw_bus_node_is_compatible(child, soc_nodes[i].compat))
			continue;

		ret = fdt_win_process_child(child, &soc_nodes[i], "reg");
		if (ret != 0)
			return (ret);
	}

	return (0);
}

static int
fdt_win_process_child(phandle_t child, struct soc_node_spec *soc_node,
    const char* mimo_reg_source)
{
	int addr_cells, size_cells;
	pcell_t reg[8];
	u_long size, base;

	if (fdt_addrsize_cells(OF_parent(child), &addr_cells,
	    &size_cells))
		return (ENXIO);

	if ((sizeof(pcell_t) * (addr_cells + size_cells)) > sizeof(reg))
		return (ENOMEM);
	if (OF_getprop(child, mimo_reg_source, &reg, sizeof(reg)) <= 0)
		return (EINVAL);

	if (addr_cells <= 2)
		base = fdt_data_get(&reg[0], addr_cells);
	else
		base = fdt_data_get(&reg[addr_cells - 2], 2);
	size = fdt_data_get(&reg[addr_cells], size_cells);

	if (soc_node->valid_handler != NULL)
		if (!soc_node->valid_handler())
			return (EINVAL);

	base = (base & 0x000fffff) | fdt_immr_va;
	if (soc_node->decode_handler != NULL)
		soc_node->decode_handler(base);
	else
		return (ENXIO);

	if (MV_DUMP_WIN && (soc_node->dump_handler != NULL))
		soc_node->dump_handler(base);

	return (0);
}

static int
fdt_win_setup(void)
{
	phandle_t node, child, sb;
	phandle_t child_pci;
	int err;

	sb = 0;
	node = OF_finddevice("/");
	if (node == -1)
		panic("fdt_win_setup: no root node");

	/* Allow for coherent transactions on the A38x MBUS */
	if (ofw_bus_node_is_compatible(node, "marvell,armada380"))
		platform_io_coherent = true;

	/*
	 * Traverse through all children of root and simple-bus nodes.
	 * For each found device retrieve decode windows data (if applicable).
	 */
	child = OF_child(node);
	while (child != 0) {
		/* Lookup for callback and run */
		err = fdt_win_process(child);
		if (err != 0)
			return (err);

		/* Process Marvell Armada-XP/38x PCIe controllers */
		if (ofw_bus_node_is_compatible(child, "marvell,armada-370-pcie")) {
			child_pci = OF_child(child);
			while (child_pci != 0) {
				err = fdt_win_process_child(child_pci,
				    &soc_nodes[SOC_NODE_PCIE_ENTRY_IDX],
				    "assigned-addresses");
				if (err != 0)
					return (err);

				child_pci = OF_peer(child_pci);
			}
		}

		/*
		 * Once done with root-level children let's move down to
		 * simple-bus and its children.
		 */
		child = OF_peer(child);
		if ((child == 0) && (node == OF_finddevice("/"))) {
			sb = node = fdt_find_compatible(node, "simple-bus", 0);
			if (node == 0)
				return (ENXIO);
			child = OF_child(node);
		}
		/*
		 * Next, move one more level down to internal-regs node (if
		 * it is present) and its children. This node also have
		 * "simple-bus" compatible.
		 */
		if ((child == 0) && (node == sb)) {
			node = fdt_find_compatible(node, "simple-bus", 0);
			if (node == 0)
				return (0);
			child = OF_child(node);
		}
	}

	return (0);
}

static void
fdt_fixup_busfreq(phandle_t root)
{
	phandle_t sb;
	pcell_t freq;

	freq = cpu_to_fdt32(get_tclk());

	/*
	 * Fix bus speed in cpu node
	 */
	if ((sb = OF_finddevice("cpu")) != -1)
		if (fdt_is_compatible_strict(sb, "ARM,88VS584"))
			OF_setprop(sb, "bus-frequency", (void *)&freq,
			    sizeof(freq));

	/*
	 * This fixup sets the simple-bus bus-frequency property.
	 */
	if ((sb = fdt_find_compatible(root, "simple-bus", 1)) != 0)
		OF_setprop(sb, "bus-frequency", (void *)&freq, sizeof(freq));
}

static void
fdt_fixup_ranges(phandle_t root)
{
	phandle_t node;
	pcell_t par_addr_cells, addr_cells, size_cells;
	pcell_t ranges[3], reg[2], *rangesptr;
	int len, tuple_size, tuples_count;
	uint32_t base;

	/* Fix-up SoC ranges according to real fdt_immr_pa */
	if ((node = fdt_find_compatible(root, "simple-bus", 1)) != 0) {
		if (fdt_addrsize_cells(node, &addr_cells, &size_cells) == 0 &&
		    ((par_addr_cells = fdt_parent_addr_cells(node)) <= 2)) {
			tuple_size = sizeof(pcell_t) * (par_addr_cells +
			   addr_cells + size_cells);
			len = OF_getprop(node, "ranges", ranges,
			    sizeof(ranges));
			tuples_count = len / tuple_size;
			/* Unexpected settings are not supported */
			if (tuples_count != 1)
				goto fixup_failed;

			rangesptr = &ranges[0];
			rangesptr += par_addr_cells;
			base = fdt_data_get((void *)rangesptr, addr_cells);
			*rangesptr = cpu_to_fdt32(fdt_immr_pa);
			if (OF_setprop(node, "ranges", (void *)&ranges[0],
			    sizeof(ranges)) < 0)
				goto fixup_failed;
		}
	}

	/* Fix-up PCIe reg according to real PCIe registers' PA */
	if ((node = fdt_find_compatible(root, "mrvl,pcie", 1)) != 0) {
		if (fdt_addrsize_cells(OF_parent(node), &par_addr_cells,
		    &size_cells) == 0) {
			tuple_size = sizeof(pcell_t) * (par_addr_cells +
			    size_cells);
			len = OF_getprop(node, "reg", reg, sizeof(reg));
			tuples_count = len / tuple_size;
			/* Unexpected settings are not supported */
			if (tuples_count != 1)
				goto fixup_failed;

			base = fdt_data_get((void *)&reg[0], par_addr_cells);
			base &= ~0xFF000000;
			base |= fdt_immr_pa;
			reg[0] = cpu_to_fdt32(base);
			if (OF_setprop(node, "reg", (void *)&reg[0],
			    sizeof(reg)) < 0)
				goto fixup_failed;
		}
	}
	/* Fix-up succeeded. May return and continue */
	return;

fixup_failed:
	while (1) {
		/*
		 * In case of any error while fixing ranges just hang.
		 *	1. No message can be displayed yet since console
		 *	   is not initialized.
		 *	2. Going further will cause failure on bus_space_map()
		 *	   relying on the wrong ranges or data abort when
		 *	   accessing PCIe registers.
		 */
	}
}

struct fdt_fixup_entry fdt_fixup_table[] = {
	{ "mrvl,DB-88F6281", &fdt_fixup_busfreq },
	{ "mrvl,DB-78460", &fdt_fixup_busfreq },
	{ "mrvl,DB-78460", &fdt_fixup_ranges },
	{ NULL, NULL }
};

#if __ARM_ARCH >= 6
uint32_t
get_tclk(void)
{

	if (soc_decode_win_spec->get_tclk != NULL)
		return soc_decode_win_spec->get_tclk();
	else
		return -1;
}

uint32_t
get_cpu_freq(void)
{

	if (soc_decode_win_spec->get_cpu_freq != NULL)
		return soc_decode_win_spec->get_cpu_freq();
	else
		return -1;
}
#endif

#ifndef INTRNG
static int
fdt_pic_decode_ic(phandle_t node, pcell_t *intr, int *interrupt, int *trig,
    int *pol)
{

	if (!ofw_bus_node_is_compatible(node, "mrvl,pic") &&
	    !ofw_bus_node_is_compatible(node, "mrvl,mpic"))
		return (ENXIO);

	*interrupt = fdt32_to_cpu(intr[0]);
	*trig = INTR_TRIGGER_CONFORM;
	*pol = INTR_POLARITY_CONFORM;

	return (0);
}

fdt_pic_decode_t fdt_pic_table[] = {
	&fdt_pic_decode_ic,
	NULL
};
#endif
