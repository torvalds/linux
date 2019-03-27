/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 Johannes Lundberg
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <vm/vm.h>
/* XXX: enable this once the KPI is available */
/* #include <x86/physmem.h> */
#include <machine/pci_cfgreg.h>
#include <machine/md_var.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <x86/pci/pci_early_quirks.h>

#define	MiB(v) ((unsigned long)(v) << 20)

struct pci_device_id {
	uint32_t	vendor;
	uint32_t	device;
	const struct intel_stolen_ops *data;
};

/*
 * These global variables are read by LinuxKPI.
 * LinuxKPI provide this information to the i915 driver.
 */
vm_paddr_t intel_graphics_stolen_base = 0;
vm_paddr_t intel_graphics_stolen_size = 0;

/*
 * Intel early quirks functions
 */
static vm_paddr_t
intel_stolen_base_gen3(int bus, int slot, int func)
{
	uint32_t ctrl;
	vm_paddr_t val;

	ctrl = pci_cfgregread(bus, slot, func, INTEL_BSM, 4);
	val = ctrl & INTEL_BSM_MASK;
	return (val);
}

static vm_paddr_t
intel_stolen_size_gen3(int bus, int slot, int func)
{
	uint32_t ctrl;
	vm_paddr_t val;

	ctrl = pci_cfgregread(0, 0, 0, I830_GMCH_CTRL, 2);
	val = ctrl & I855_GMCH_GMS_MASK;

	switch (val) {
	case I855_GMCH_GMS_STOLEN_1M:
		return (MiB(1));
	case I855_GMCH_GMS_STOLEN_4M:
		return (MiB(4));
	case I855_GMCH_GMS_STOLEN_8M:
		return (MiB(8));
	case I855_GMCH_GMS_STOLEN_16M:
		return (MiB(16));
	case I855_GMCH_GMS_STOLEN_32M:
		return (MiB(32));
	case I915_GMCH_GMS_STOLEN_48M:
		return (MiB(48));
	case I915_GMCH_GMS_STOLEN_64M:
		return (MiB(64));
	case G33_GMCH_GMS_STOLEN_128M:
		return (MiB(128));
	case G33_GMCH_GMS_STOLEN_256M:
		return (MiB(256));
	case INTEL_GMCH_GMS_STOLEN_96M:
		return (MiB(96));
	case INTEL_GMCH_GMS_STOLEN_160M:
		return (MiB(160));
	case INTEL_GMCH_GMS_STOLEN_224M:
		return (MiB(224));
	case INTEL_GMCH_GMS_STOLEN_352M:
		return (MiB(352));
	}
	return (0);
}

static vm_paddr_t
intel_stolen_size_gen6(int bus, int slot, int func)
{
	uint32_t ctrl;
	vm_paddr_t val;

	ctrl = pci_cfgregread(bus, slot, func, SNB_GMCH_CTRL, 2);
	val = (ctrl >> SNB_GMCH_GMS_SHIFT) & SNB_GMCH_GMS_MASK;
	return (val * MiB(32));
}

static vm_paddr_t
intel_stolen_size_gen8(int bus, int slot, int func)
{
	uint32_t ctrl;
	vm_paddr_t val;

	ctrl = pci_cfgregread(bus, slot, func, SNB_GMCH_CTRL, 2);
	val = (ctrl >> BDW_GMCH_GMS_SHIFT) & BDW_GMCH_GMS_MASK;
	return (val * MiB(32));
}

static vm_paddr_t
intel_stolen_size_chv(int bus, int slot, int func)
{
	uint32_t ctrl;
	vm_paddr_t val;

	ctrl = pci_cfgregread(bus, slot, func, SNB_GMCH_CTRL, 2);
	val = (ctrl >> SNB_GMCH_GMS_SHIFT) & SNB_GMCH_GMS_MASK;

	/*
	 * 0x0  to 0x10: 32MB increments starting at 0MB
	 * 0x11 to 0x16: 4MB increments starting at 8MB
	 * 0x17 to 0x1d: 4MB increments start at 36MB
	 */
	if (val < 0x11)
		return (val * MiB(32));
	else if (val < 0x17)
		return ((val - 0x11) * MiB(4) + MiB(8));
	else
		return ((val - 0x17) * MiB(4) + MiB(36));
}

static vm_paddr_t
intel_stolen_size_gen9(int bus, int slot, int func)
{
	uint32_t ctrl;
	vm_paddr_t val;

	ctrl = pci_cfgregread(bus, slot, func, SNB_GMCH_CTRL, 2);
	val = (ctrl >> BDW_GMCH_GMS_SHIFT) & BDW_GMCH_GMS_MASK;

	/* 0x0  to 0xEF: 32MB increments starting at 0MB */
	/* 0xF0 to 0xFE: 4MB increments starting at 4MB */
	if (val < 0xF0)
		return (val * MiB(32));
	return ((val - 0xF0) * MiB(4) + MiB(4));
}

struct intel_stolen_ops {
	vm_paddr_t (*base)(int bus, int slot, int func);
	vm_paddr_t (*size)(int bus, int slot, int func);
};

static const struct intel_stolen_ops intel_stolen_ops_gen3 = {
	.base = intel_stolen_base_gen3,
	.size = intel_stolen_size_gen3,
};

static const struct intel_stolen_ops intel_stolen_ops_gen6 = {
	.base = intel_stolen_base_gen3,
	.size = intel_stolen_size_gen6,
};

static const struct intel_stolen_ops intel_stolen_ops_gen8 = {
	.base = intel_stolen_base_gen3,
	.size = intel_stolen_size_gen8,
};

static const struct intel_stolen_ops intel_stolen_ops_gen9 = {
	.base = intel_stolen_base_gen3,
	.size = intel_stolen_size_gen9,
};

static const struct intel_stolen_ops intel_stolen_ops_chv = {
	.base = intel_stolen_base_gen3,
	.size = intel_stolen_size_chv,
};

static const struct pci_device_id intel_ids[] = {
	INTEL_I915G_IDS(&intel_stolen_ops_gen3),
	INTEL_I915GM_IDS(&intel_stolen_ops_gen3),
	INTEL_I945G_IDS(&intel_stolen_ops_gen3),
	INTEL_I945GM_IDS(&intel_stolen_ops_gen3),
	INTEL_VLV_IDS(&intel_stolen_ops_gen6),
	INTEL_PINEVIEW_IDS(&intel_stolen_ops_gen3),
	INTEL_I965G_IDS(&intel_stolen_ops_gen3),
	INTEL_G33_IDS(&intel_stolen_ops_gen3),
	INTEL_I965GM_IDS(&intel_stolen_ops_gen3),
	INTEL_GM45_IDS(&intel_stolen_ops_gen3),
	INTEL_G45_IDS(&intel_stolen_ops_gen3),
	INTEL_IRONLAKE_D_IDS(&intel_stolen_ops_gen3),
	INTEL_IRONLAKE_M_IDS(&intel_stolen_ops_gen3),
	INTEL_SNB_D_IDS(&intel_stolen_ops_gen6),
	INTEL_SNB_M_IDS(&intel_stolen_ops_gen6),
	INTEL_IVB_M_IDS(&intel_stolen_ops_gen6),
	INTEL_IVB_D_IDS(&intel_stolen_ops_gen6),
	INTEL_HSW_IDS(&intel_stolen_ops_gen6),
	INTEL_BDW_IDS(&intel_stolen_ops_gen8),
	INTEL_CHV_IDS(&intel_stolen_ops_chv),
	INTEL_SKL_IDS(&intel_stolen_ops_gen9),
	INTEL_BXT_IDS(&intel_stolen_ops_gen9),
	INTEL_KBL_IDS(&intel_stolen_ops_gen9),
	INTEL_CFL_IDS(&intel_stolen_ops_gen9),
	INTEL_GLK_IDS(&intel_stolen_ops_gen9),
	INTEL_CNL_IDS(&intel_stolen_ops_gen9),
};

/*
 * Buggy BIOS don't reserve memory for the GPU properly and the OS
 * can claim it before the GPU driver is loaded. This function will
 * check the registers for base and size of this memory and reserve
 * it for the GPU driver.
 * gen3 (2004) and newer devices are supported. Support for older hw
 * can be ported from Linux if needed.
 */
static void
intel_graphics_stolen(void)
{
	const struct intel_stolen_ops *ops;
	uint32_t vendor, device, class;
	int i;

	/* XXX: Scan bus instead of assuming 0:2:0? */
	const int bus = 0;
	const int slot = 2;
	const int func = 0;

	if (pci_cfgregopen() == 0)
		return;

	vendor = pci_cfgregread(bus, slot, func, PCIR_VENDOR, 2);
	if (vendor != PCI_VENDOR_INTEL)
		return;

	class = pci_cfgregread(bus, slot, func, PCIR_SUBCLASS, 2);
	if (class != PCI_CLASS_VGA)
		return;

	device = pci_cfgregread(bus, slot, func, PCIR_DEVICE, 2);
	if (device == 0xFFFF)
		return;

	for (i = 0; i < nitems(intel_ids); i++) {
		if (intel_ids[i].device != device)
			continue;
		ops = intel_ids[i].data;
		intel_graphics_stolen_base = ops->base(bus, slot, func);
		intel_graphics_stolen_size = ops->size(bus, slot, func);
		break;
	}

	/* XXX: enable this once the KPI is available */
	/* phys_avail_reserve(intel_graphics_stolen_base, */
	/*     intel_graphics_stolen_base + intel_graphics_stolen_size); */
}

void
pci_early_quirks(void)
{

	intel_graphics_stolen();
}
