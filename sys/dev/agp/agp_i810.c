/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Doug Rabson
 * Copyright (c) 2000 Ruslan Ermilov
 * Copyright (c) 2011 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Konstantin Belousov
 * under sponsorship from the FreeBSD Foundation.
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

/*
 * Fixes for 830/845G support: David Dawes <dawes@xfree86.org>
 * 852GM/855GM/865G support added by David Dawes <dawes@xfree86.org>
 *
 * This is generic Intel GTT handling code, morphed from the AGP
 * bridge code.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#if 0
#define	KTR_AGP_I810	KTR_DEV
#else
#define	KTR_AGP_I810	0
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/rwlock.h>

#include <dev/agp/agppriv.h>
#include <dev/agp/agpreg.h>
#include <dev/agp/agp_i810.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pci_private.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/vm_param.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pageout.h>
#include <vm/pmap.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <machine/md_var.h>
#include <sys/rman.h>

MALLOC_DECLARE(M_AGP);

struct agp_i810_match;

static int agp_i810_check_active(device_t bridge_dev);
static int agp_i830_check_active(device_t bridge_dev);
static int agp_i915_check_active(device_t bridge_dev);

static void agp_82852_set_desc(device_t dev,
    const struct agp_i810_match *match);
static void agp_i810_set_desc(device_t dev, const struct agp_i810_match *match);

static void agp_i810_dump_regs(device_t dev);
static void agp_i830_dump_regs(device_t dev);
static void agp_i855_dump_regs(device_t dev);
static void agp_i915_dump_regs(device_t dev);
static void agp_i965_dump_regs(device_t dev);

static int agp_i810_get_stolen_size(device_t dev);
static int agp_i830_get_stolen_size(device_t dev);
static int agp_i915_get_stolen_size(device_t dev);

static int agp_i810_get_gtt_mappable_entries(device_t dev);
static int agp_i830_get_gtt_mappable_entries(device_t dev);
static int agp_i915_get_gtt_mappable_entries(device_t dev);

static int agp_i810_get_gtt_total_entries(device_t dev);
static int agp_i965_get_gtt_total_entries(device_t dev);
static int agp_gen5_get_gtt_total_entries(device_t dev);

static int agp_i810_install_gatt(device_t dev);
static int agp_i830_install_gatt(device_t dev);
static int agp_i965_install_gatt(device_t dev);
static int agp_g4x_install_gatt(device_t dev);

static void agp_i810_deinstall_gatt(device_t dev);
static void agp_i830_deinstall_gatt(device_t dev);

static void agp_i810_install_gtt_pte(device_t dev, u_int index,
    vm_offset_t physical, int flags);
static void agp_i830_install_gtt_pte(device_t dev, u_int index,
    vm_offset_t physical, int flags);
static void agp_i915_install_gtt_pte(device_t dev, u_int index,
    vm_offset_t physical, int flags);
static void agp_i965_install_gtt_pte(device_t dev, u_int index,
    vm_offset_t physical, int flags);
static void agp_g4x_install_gtt_pte(device_t dev, u_int index,
    vm_offset_t physical, int flags);

static void agp_i810_write_gtt(device_t dev, u_int index, uint32_t pte);
static void agp_i915_write_gtt(device_t dev, u_int index, uint32_t pte);
static void agp_i965_write_gtt(device_t dev, u_int index, uint32_t pte);
static void agp_g4x_write_gtt(device_t dev, u_int index, uint32_t pte);

static u_int32_t agp_i810_read_gtt_pte(device_t dev, u_int index);
static u_int32_t agp_i915_read_gtt_pte(device_t dev, u_int index);
static u_int32_t agp_i965_read_gtt_pte(device_t dev, u_int index);
static u_int32_t agp_g4x_read_gtt_pte(device_t dev, u_int index);

static vm_paddr_t agp_i810_read_gtt_pte_paddr(device_t dev, u_int index);
static vm_paddr_t agp_i915_read_gtt_pte_paddr(device_t dev, u_int index);

static int agp_i810_set_aperture(device_t dev, u_int32_t aperture);
static int agp_i830_set_aperture(device_t dev, u_int32_t aperture);
static int agp_i915_set_aperture(device_t dev, u_int32_t aperture);

static int agp_i810_chipset_flush_setup(device_t dev);
static int agp_i915_chipset_flush_setup(device_t dev);
static int agp_i965_chipset_flush_setup(device_t dev);

static void agp_i810_chipset_flush_teardown(device_t dev);
static void agp_i915_chipset_flush_teardown(device_t dev);
static void agp_i965_chipset_flush_teardown(device_t dev);

static void agp_i810_chipset_flush(device_t dev);
static void agp_i830_chipset_flush(device_t dev);
static void agp_i915_chipset_flush(device_t dev);

enum {
	CHIP_I810,	/* i810/i815 */
	CHIP_I830,	/* 830M/845G */
	CHIP_I855,	/* 852GM/855GM/865G */
	CHIP_I915,	/* 915G/915GM */
	CHIP_I965,	/* G965 */
	CHIP_G33,	/* G33/Q33/Q35 */
	CHIP_IGD,	/* Pineview */
	CHIP_G4X,	/* G45/Q45 */
};

/* The i810 through i855 have the registers at BAR 1, and the GATT gets
 * allocated by us.  The i915 has registers in BAR 0 and the GATT is at the
 * start of the stolen memory, and should only be accessed by the OS through
 * BAR 3.  The G965 has registers and GATT in the same BAR (0) -- first 512KB
 * is registers, second 512KB is GATT.
 */
static struct resource_spec agp_i810_res_spec[] = {
	{ SYS_RES_MEMORY, AGP_I810_MMADR, RF_ACTIVE | RF_SHAREABLE },
	{ -1, 0 }
};

static struct resource_spec agp_i915_res_spec[] = {
	{ SYS_RES_MEMORY, AGP_I915_MMADR, RF_ACTIVE | RF_SHAREABLE },
	{ SYS_RES_MEMORY, AGP_I915_GTTADR, RF_ACTIVE | RF_SHAREABLE },
	{ -1, 0 }
};

static struct resource_spec agp_i965_res_spec[] = {
	{ SYS_RES_MEMORY, AGP_I965_GTTMMADR, RF_ACTIVE | RF_SHAREABLE },
	{ SYS_RES_MEMORY, AGP_I965_APBASE, RF_ACTIVE | RF_SHAREABLE },
	{ -1, 0 }
};

struct agp_i810_softc {
	struct agp_softc agp;
	u_int32_t initial_aperture;	/* aperture size at startup */
	struct agp_gatt *gatt;
	u_int32_t dcache_size;		/* i810 only */
	u_int32_t stolen;		/* number of i830/845 gtt
					   entries for stolen memory */
	u_int stolen_size;		/* BIOS-reserved graphics memory */
	u_int gtt_total_entries;	/* Total number of gtt ptes */
	u_int gtt_mappable_entries;	/* Number of gtt ptes mappable by CPU */
	device_t bdev;			/* bridge device */
	void *argb_cursor;		/* contigmalloc area for ARGB cursor */
	struct resource *sc_res[2];
	const struct agp_i810_match *match;
	int sc_flush_page_rid;
	struct resource *sc_flush_page_res;
	void *sc_flush_page_vaddr;
	int sc_bios_allocated_flush_page;
};

static device_t intel_agp;

struct agp_i810_driver {
	int chiptype;
	int gen;
	int busdma_addr_mask_sz;
	struct resource_spec *res_spec;
	int (*check_active)(device_t);
	void (*set_desc)(device_t, const struct agp_i810_match *);
	void (*dump_regs)(device_t);
	int (*get_stolen_size)(device_t);
	int (*get_gtt_total_entries)(device_t);
	int (*get_gtt_mappable_entries)(device_t);
	int (*install_gatt)(device_t);
	void (*deinstall_gatt)(device_t);
	void (*write_gtt)(device_t, u_int, uint32_t);
	void (*install_gtt_pte)(device_t, u_int, vm_offset_t, int);
	u_int32_t (*read_gtt_pte)(device_t, u_int);
	vm_paddr_t (*read_gtt_pte_paddr)(device_t , u_int);
	int (*set_aperture)(device_t, u_int32_t);
	int (*chipset_flush_setup)(device_t);
	void (*chipset_flush_teardown)(device_t);
	void (*chipset_flush)(device_t);
};

static struct {
	struct intel_gtt base;
} intel_private;

static const struct agp_i810_driver agp_i810_i810_driver = {
	.chiptype = CHIP_I810,
	.gen = 1,
	.busdma_addr_mask_sz = 32,
	.res_spec = agp_i810_res_spec,
	.check_active = agp_i810_check_active,
	.set_desc = agp_i810_set_desc,
	.dump_regs = agp_i810_dump_regs,
	.get_stolen_size = agp_i810_get_stolen_size,
	.get_gtt_mappable_entries = agp_i810_get_gtt_mappable_entries,
	.get_gtt_total_entries = agp_i810_get_gtt_total_entries,
	.install_gatt = agp_i810_install_gatt,
	.deinstall_gatt = agp_i810_deinstall_gatt,
	.write_gtt = agp_i810_write_gtt,
	.install_gtt_pte = agp_i810_install_gtt_pte,
	.read_gtt_pte = agp_i810_read_gtt_pte,
	.read_gtt_pte_paddr = agp_i810_read_gtt_pte_paddr,
	.set_aperture = agp_i810_set_aperture,
	.chipset_flush_setup = agp_i810_chipset_flush_setup,
	.chipset_flush_teardown = agp_i810_chipset_flush_teardown,
	.chipset_flush = agp_i810_chipset_flush,
};

static const struct agp_i810_driver agp_i810_i815_driver = {
	.chiptype = CHIP_I810,
	.gen = 2,
	.busdma_addr_mask_sz = 32,
	.res_spec = agp_i810_res_spec,
	.check_active = agp_i810_check_active,
	.set_desc = agp_i810_set_desc,
	.dump_regs = agp_i810_dump_regs,
	.get_stolen_size = agp_i810_get_stolen_size,
	.get_gtt_mappable_entries = agp_i830_get_gtt_mappable_entries,
	.get_gtt_total_entries = agp_i810_get_gtt_total_entries,
	.install_gatt = agp_i810_install_gatt,
	.deinstall_gatt = agp_i810_deinstall_gatt,
	.write_gtt = agp_i810_write_gtt,
	.install_gtt_pte = agp_i810_install_gtt_pte,
	.read_gtt_pte = agp_i810_read_gtt_pte,
	.read_gtt_pte_paddr = agp_i810_read_gtt_pte_paddr,
	.set_aperture = agp_i810_set_aperture,
	.chipset_flush_setup = agp_i810_chipset_flush_setup,
	.chipset_flush_teardown = agp_i810_chipset_flush_teardown,
	.chipset_flush = agp_i830_chipset_flush,
};

static const struct agp_i810_driver agp_i810_i830_driver = {
	.chiptype = CHIP_I830,
	.gen = 2,
	.busdma_addr_mask_sz = 32,
	.res_spec = agp_i810_res_spec,
	.check_active = agp_i830_check_active,
	.set_desc = agp_i810_set_desc,
	.dump_regs = agp_i830_dump_regs,
	.get_stolen_size = agp_i830_get_stolen_size,
	.get_gtt_mappable_entries = agp_i830_get_gtt_mappable_entries,
	.get_gtt_total_entries = agp_i810_get_gtt_total_entries,
	.install_gatt = agp_i830_install_gatt,
	.deinstall_gatt = agp_i830_deinstall_gatt,
	.write_gtt = agp_i810_write_gtt,
	.install_gtt_pte = agp_i830_install_gtt_pte,
	.read_gtt_pte = agp_i810_read_gtt_pte,
	.read_gtt_pte_paddr = agp_i810_read_gtt_pte_paddr,
	.set_aperture = agp_i830_set_aperture,
	.chipset_flush_setup = agp_i810_chipset_flush_setup,
	.chipset_flush_teardown = agp_i810_chipset_flush_teardown,
	.chipset_flush = agp_i830_chipset_flush,
};

static const struct agp_i810_driver agp_i810_i855_driver = {
	.chiptype = CHIP_I855,
	.gen = 2,
	.busdma_addr_mask_sz = 32,
	.res_spec = agp_i810_res_spec,
	.check_active = agp_i830_check_active,
	.set_desc = agp_82852_set_desc,
	.dump_regs = agp_i855_dump_regs,
	.get_stolen_size = agp_i915_get_stolen_size,
	.get_gtt_mappable_entries = agp_i915_get_gtt_mappable_entries,
	.get_gtt_total_entries = agp_i810_get_gtt_total_entries,
	.install_gatt = agp_i830_install_gatt,
	.deinstall_gatt = agp_i830_deinstall_gatt,
	.write_gtt = agp_i810_write_gtt,
	.install_gtt_pte = agp_i830_install_gtt_pte,
	.read_gtt_pte = agp_i810_read_gtt_pte,
	.read_gtt_pte_paddr = agp_i810_read_gtt_pte_paddr,
	.set_aperture = agp_i830_set_aperture,
	.chipset_flush_setup = agp_i810_chipset_flush_setup,
	.chipset_flush_teardown = agp_i810_chipset_flush_teardown,
	.chipset_flush = agp_i830_chipset_flush,
};

static const struct agp_i810_driver agp_i810_i865_driver = {
	.chiptype = CHIP_I855,
	.gen = 2,
	.busdma_addr_mask_sz = 32,
	.res_spec = agp_i810_res_spec,
	.check_active = agp_i830_check_active,
	.set_desc = agp_i810_set_desc,
	.dump_regs = agp_i855_dump_regs,
	.get_stolen_size = agp_i915_get_stolen_size,
	.get_gtt_mappable_entries = agp_i915_get_gtt_mappable_entries,
	.get_gtt_total_entries = agp_i810_get_gtt_total_entries,
	.install_gatt = agp_i830_install_gatt,
	.deinstall_gatt = agp_i830_deinstall_gatt,
	.write_gtt = agp_i810_write_gtt,
	.install_gtt_pte = agp_i830_install_gtt_pte,
	.read_gtt_pte = agp_i810_read_gtt_pte,
	.read_gtt_pte_paddr = agp_i810_read_gtt_pte_paddr,
	.set_aperture = agp_i915_set_aperture,
	.chipset_flush_setup = agp_i810_chipset_flush_setup,
	.chipset_flush_teardown = agp_i810_chipset_flush_teardown,
	.chipset_flush = agp_i830_chipset_flush,
};

static const struct agp_i810_driver agp_i810_i915_driver = {
	.chiptype = CHIP_I915,
	.gen = 3,
	.busdma_addr_mask_sz = 32,
	.res_spec = agp_i915_res_spec,
	.check_active = agp_i915_check_active,
	.set_desc = agp_i810_set_desc,
	.dump_regs = agp_i915_dump_regs,
	.get_stolen_size = agp_i915_get_stolen_size,
	.get_gtt_mappable_entries = agp_i915_get_gtt_mappable_entries,
	.get_gtt_total_entries = agp_i810_get_gtt_total_entries,
	.install_gatt = agp_i830_install_gatt,
	.deinstall_gatt = agp_i830_deinstall_gatt,
	.write_gtt = agp_i915_write_gtt,
	.install_gtt_pte = agp_i915_install_gtt_pte,
	.read_gtt_pte = agp_i915_read_gtt_pte,
	.read_gtt_pte_paddr = agp_i915_read_gtt_pte_paddr,
	.set_aperture = agp_i915_set_aperture,
	.chipset_flush_setup = agp_i915_chipset_flush_setup,
	.chipset_flush_teardown = agp_i915_chipset_flush_teardown,
	.chipset_flush = agp_i915_chipset_flush,
};

static const struct agp_i810_driver agp_i810_g33_driver = {
	.chiptype = CHIP_G33,
	.gen = 3,
	.busdma_addr_mask_sz = 36,
	.res_spec = agp_i915_res_spec,
	.check_active = agp_i915_check_active,
	.set_desc = agp_i810_set_desc,
	.dump_regs = agp_i965_dump_regs,
	.get_stolen_size = agp_i915_get_stolen_size,
	.get_gtt_mappable_entries = agp_i915_get_gtt_mappable_entries,
	.get_gtt_total_entries = agp_i965_get_gtt_total_entries,
	.install_gatt = agp_i830_install_gatt,
	.deinstall_gatt = agp_i830_deinstall_gatt,
	.write_gtt = agp_i915_write_gtt,
	.install_gtt_pte = agp_i915_install_gtt_pte,
	.read_gtt_pte = agp_i915_read_gtt_pte,
	.read_gtt_pte_paddr = agp_i915_read_gtt_pte_paddr,
	.set_aperture = agp_i915_set_aperture,
	.chipset_flush_setup = agp_i965_chipset_flush_setup,
	.chipset_flush_teardown = agp_i965_chipset_flush_teardown,
	.chipset_flush = agp_i915_chipset_flush,
};

static const struct agp_i810_driver agp_i810_igd_driver = {
	.chiptype = CHIP_IGD,
	.gen = 3,
	.busdma_addr_mask_sz = 36,
	.res_spec = agp_i915_res_spec,
	.check_active = agp_i915_check_active,
	.set_desc = agp_i810_set_desc,
	.dump_regs = agp_i915_dump_regs,
	.get_stolen_size = agp_i915_get_stolen_size,
	.get_gtt_mappable_entries = agp_i915_get_gtt_mappable_entries,
	.get_gtt_total_entries = agp_i965_get_gtt_total_entries,
	.install_gatt = agp_i830_install_gatt,
	.deinstall_gatt = agp_i830_deinstall_gatt,
	.write_gtt = agp_i915_write_gtt,
	.install_gtt_pte = agp_i915_install_gtt_pte,
	.read_gtt_pte = agp_i915_read_gtt_pte,
	.read_gtt_pte_paddr = agp_i915_read_gtt_pte_paddr,
	.set_aperture = agp_i915_set_aperture,
	.chipset_flush_setup = agp_i965_chipset_flush_setup,
	.chipset_flush_teardown = agp_i965_chipset_flush_teardown,
	.chipset_flush = agp_i915_chipset_flush,
};

static const struct agp_i810_driver agp_i810_g965_driver = {
	.chiptype = CHIP_I965,
	.gen = 4,
	.busdma_addr_mask_sz = 36,
	.res_spec = agp_i965_res_spec,
	.check_active = agp_i915_check_active,
	.set_desc = agp_i810_set_desc,
	.dump_regs = agp_i965_dump_regs,
	.get_stolen_size = agp_i915_get_stolen_size,
	.get_gtt_mappable_entries = agp_i915_get_gtt_mappable_entries,
	.get_gtt_total_entries = agp_i965_get_gtt_total_entries,
	.install_gatt = agp_i965_install_gatt,
	.deinstall_gatt = agp_i830_deinstall_gatt,
	.write_gtt = agp_i965_write_gtt,
	.install_gtt_pte = agp_i965_install_gtt_pte,
	.read_gtt_pte = agp_i965_read_gtt_pte,
	.read_gtt_pte_paddr = agp_i915_read_gtt_pte_paddr,
	.set_aperture = agp_i915_set_aperture,
	.chipset_flush_setup = agp_i965_chipset_flush_setup,
	.chipset_flush_teardown = agp_i965_chipset_flush_teardown,
	.chipset_flush = agp_i915_chipset_flush,
};

static const struct agp_i810_driver agp_i810_g4x_driver = {
	.chiptype = CHIP_G4X,
	.gen = 5,
	.busdma_addr_mask_sz = 36,
	.res_spec = agp_i965_res_spec,
	.check_active = agp_i915_check_active,
	.set_desc = agp_i810_set_desc,
	.dump_regs = agp_i965_dump_regs,
	.get_stolen_size = agp_i915_get_stolen_size,
	.get_gtt_mappable_entries = agp_i915_get_gtt_mappable_entries,
	.get_gtt_total_entries = agp_gen5_get_gtt_total_entries,
	.install_gatt = agp_g4x_install_gatt,
	.deinstall_gatt = agp_i830_deinstall_gatt,
	.write_gtt = agp_g4x_write_gtt,
	.install_gtt_pte = agp_g4x_install_gtt_pte,
	.read_gtt_pte = agp_g4x_read_gtt_pte,
	.read_gtt_pte_paddr = agp_i915_read_gtt_pte_paddr,
	.set_aperture = agp_i915_set_aperture,
	.chipset_flush_setup = agp_i965_chipset_flush_setup,
	.chipset_flush_teardown = agp_i965_chipset_flush_teardown,
	.chipset_flush = agp_i915_chipset_flush,
};

/* For adding new devices, devid is the id of the graphics controller
 * (pci:0:2:0, for example).  The placeholder (usually at pci:0:2:1) for the
 * second head should never be added.  The bridge_offset is the offset to
 * subtract from devid to get the id of the hostb that the device is on.
 */
static const struct agp_i810_match {
	int devid;
	char *name;
	const struct agp_i810_driver *driver;
} agp_i810_matches[] = {
	{
		.devid = 0x71218086,
		.name = "Intel 82810 (i810 GMCH) SVGA controller",
		.driver = &agp_i810_i810_driver
	},
	{
		.devid = 0x71238086,
		.name = "Intel 82810-DC100 (i810-DC100 GMCH) SVGA controller",
		.driver = &agp_i810_i810_driver
	},
	{
		.devid = 0x71258086,
		.name = "Intel 82810E (i810E GMCH) SVGA controller",
		.driver = &agp_i810_i810_driver
	},
	{
		.devid = 0x11328086,
		.name = "Intel 82815 (i815 GMCH) SVGA controller",
		.driver = &agp_i810_i815_driver
	},
	{
		.devid = 0x35778086,
		.name = "Intel 82830M (830M GMCH) SVGA controller",
		.driver = &agp_i810_i830_driver
	},
	{
		.devid = 0x25628086,
		.name = "Intel 82845M (845M GMCH) SVGA controller",
		.driver = &agp_i810_i830_driver
	},
	{
		.devid = 0x35828086,
		.name = "Intel 82852/855GM SVGA controller",
		.driver = &agp_i810_i855_driver
	},
	{
		.devid = 0x25728086,
		.name = "Intel 82865G (865G GMCH) SVGA controller",
		.driver = &agp_i810_i865_driver
	},
	{
		.devid = 0x25828086,
		.name = "Intel 82915G (915G GMCH) SVGA controller",
		.driver = &agp_i810_i915_driver
	},
	{
		.devid = 0x258A8086,
		.name = "Intel E7221 SVGA controller",
		.driver = &agp_i810_i915_driver
	},
	{
		.devid = 0x25928086,
		.name = "Intel 82915GM (915GM GMCH) SVGA controller",
		.driver = &agp_i810_i915_driver
	},
	{
		.devid = 0x27728086,
		.name = "Intel 82945G (945G GMCH) SVGA controller",
		.driver = &agp_i810_i915_driver
	},
	{
		.devid = 0x27A28086,
		.name = "Intel 82945GM (945GM GMCH) SVGA controller",
		.driver = &agp_i810_i915_driver
	},
	{
		.devid = 0x27AE8086,
		.name = "Intel 945GME SVGA controller",
		.driver = &agp_i810_i915_driver
	},
	{
		.devid = 0x29728086,
		.name = "Intel 946GZ SVGA controller",
		.driver = &agp_i810_g965_driver
	},
	{
		.devid = 0x29828086,
		.name = "Intel G965 SVGA controller",
		.driver = &agp_i810_g965_driver
	},
	{
		.devid = 0x29928086,
		.name = "Intel Q965 SVGA controller",
		.driver = &agp_i810_g965_driver
	},
	{
		.devid = 0x29A28086,
		.name = "Intel G965 SVGA controller",
		.driver = &agp_i810_g965_driver
	},
	{
		.devid = 0x29B28086,
		.name = "Intel Q35 SVGA controller",
		.driver = &agp_i810_g33_driver
	},
	{
		.devid = 0x29C28086,
		.name = "Intel G33 SVGA controller",
		.driver = &agp_i810_g33_driver
	},
	{
		.devid = 0x29D28086,
		.name = "Intel Q33 SVGA controller",
		.driver = &agp_i810_g33_driver
	},
	{
		.devid = 0xA0018086,
		.name = "Intel Pineview SVGA controller",
		.driver = &agp_i810_igd_driver
	},
	{
		.devid = 0xA0118086,
		.name = "Intel Pineview (M) SVGA controller",
		.driver = &agp_i810_igd_driver
	},
	{
		.devid = 0x2A028086,
		.name = "Intel GM965 SVGA controller",
		.driver = &agp_i810_g965_driver
	},
	{
		.devid = 0x2A128086,
		.name = "Intel GME965 SVGA controller",
		.driver = &agp_i810_g965_driver
	},
	{
		.devid = 0x2A428086,
		.name = "Intel GM45 SVGA controller",
		.driver = &agp_i810_g4x_driver
	},
	{
		.devid = 0x2E028086,
		.name = "Intel Eaglelake SVGA controller",
		.driver = &agp_i810_g4x_driver
	},
	{
		.devid = 0x2E128086,
		.name = "Intel Q45 SVGA controller",
		.driver = &agp_i810_g4x_driver
	},
	{
		.devid = 0x2E228086,
		.name = "Intel G45 SVGA controller",
		.driver = &agp_i810_g4x_driver
	},
	{
		.devid = 0x2E328086,
		.name = "Intel G41 SVGA controller",
		.driver = &agp_i810_g4x_driver
	},
	{
		.devid = 0x00428086,
		.name = "Intel Ironlake (D) SVGA controller",
		.driver = &agp_i810_g4x_driver
	},
	{
		.devid = 0x00468086,
		.name = "Intel Ironlake (M) SVGA controller",
		.driver = &agp_i810_g4x_driver
	},
	{
		.devid = 0,
	}
};

static const struct agp_i810_match*
agp_i810_match(device_t dev)
{
	int i, devid;

	if (pci_get_class(dev) != PCIC_DISPLAY
	    || (pci_get_subclass(dev) != PCIS_DISPLAY_VGA &&
	    pci_get_subclass(dev) != PCIS_DISPLAY_OTHER))
		return (NULL);

	devid = pci_get_devid(dev);
	for (i = 0; agp_i810_matches[i].devid != 0; i++) {
		if (agp_i810_matches[i].devid == devid)
			break;
	}
	if (agp_i810_matches[i].devid == 0)
		return (NULL);
	else
		return (&agp_i810_matches[i]);
}

/*
 * Find bridge device.
 */
static device_t
agp_i810_find_bridge(device_t dev)
{

	return (pci_find_dbsf(0, 0, 0, 0));
}

static void
agp_i810_identify(driver_t *driver, device_t parent)
{

	if (device_find_child(parent, "agp", -1) == NULL &&
	    agp_i810_match(parent))
		device_add_child(parent, "agp", -1);
}

static int
agp_i810_check_active(device_t bridge_dev)
{
	u_int8_t smram;

	smram = pci_read_config(bridge_dev, AGP_I810_SMRAM, 1);
	if ((smram & AGP_I810_SMRAM_GMS) == AGP_I810_SMRAM_GMS_DISABLED)
		return (ENXIO);
	return (0);
}

static int
agp_i830_check_active(device_t bridge_dev)
{
	int gcc1;

	gcc1 = pci_read_config(bridge_dev, AGP_I830_GCC1, 1);
	if ((gcc1 & AGP_I830_GCC1_DEV2) == AGP_I830_GCC1_DEV2_DISABLED)
		return (ENXIO);
	return (0);
}

static int
agp_i915_check_active(device_t bridge_dev)
{
	int deven;

	deven = pci_read_config(bridge_dev, AGP_I915_DEVEN, 4);
	if ((deven & AGP_I915_DEVEN_D2F0) == AGP_I915_DEVEN_D2F0_DISABLED)
		return (ENXIO);
	return (0);
}

static void
agp_82852_set_desc(device_t dev, const struct agp_i810_match *match)
{

	switch (pci_read_config(dev, AGP_I85X_CAPID, 1)) {
	case AGP_I855_GME:
		device_set_desc(dev,
		    "Intel 82855GME (855GME GMCH) SVGA controller");
		break;
	case AGP_I855_GM:
		device_set_desc(dev,
		    "Intel 82855GM (855GM GMCH) SVGA controller");
		break;
	case AGP_I852_GME:
		device_set_desc(dev,
		    "Intel 82852GME (852GME GMCH) SVGA controller");
		break;
	case AGP_I852_GM:
		device_set_desc(dev,
		    "Intel 82852GM (852GM GMCH) SVGA controller");
		break;
	default:
		device_set_desc(dev,
		    "Intel 8285xM (85xGM GMCH) SVGA controller");
		break;
	}
}

static void
agp_i810_set_desc(device_t dev, const struct agp_i810_match *match)
{

	device_set_desc(dev, match->name);
}

static int
agp_i810_probe(device_t dev)
{
	device_t bdev;
	const struct agp_i810_match *match;
	int err;

	if (resource_disabled("agp", device_get_unit(dev)))
		return (ENXIO);
	match = agp_i810_match(dev);
	if (match == NULL)
		return (ENXIO);

	bdev = agp_i810_find_bridge(dev);
	if (bdev == NULL) {
		if (bootverbose)
			printf("I810: can't find bridge device\n");
		return (ENXIO);
	}

	/*
	 * checking whether internal graphics device has been activated.
	 */
	err = match->driver->check_active(bdev);
	if (err != 0) {
		if (bootverbose)
			printf("i810: disabled, not probing\n");
		return (err);
	}

	match->driver->set_desc(dev, match);
	return (BUS_PROBE_DEFAULT);
}

static void
agp_i810_dump_regs(device_t dev)
{
	struct agp_i810_softc *sc = device_get_softc(dev);

	device_printf(dev, "AGP_I810_PGTBL_CTL: %08x\n",
	    bus_read_4(sc->sc_res[0], AGP_I810_PGTBL_CTL));
	device_printf(dev, "AGP_I810_MISCC: 0x%04x\n",
	    pci_read_config(sc->bdev, AGP_I810_MISCC, 2));
}

static void
agp_i830_dump_regs(device_t dev)
{
	struct agp_i810_softc *sc = device_get_softc(dev);

	device_printf(dev, "AGP_I810_PGTBL_CTL: %08x\n",
	    bus_read_4(sc->sc_res[0], AGP_I810_PGTBL_CTL));
	device_printf(dev, "AGP_I830_GCC1: 0x%02x\n",
	    pci_read_config(sc->bdev, AGP_I830_GCC1, 1));
}

static void
agp_i855_dump_regs(device_t dev)
{
	struct agp_i810_softc *sc = device_get_softc(dev);

	device_printf(dev, "AGP_I810_PGTBL_CTL: %08x\n",
	    bus_read_4(sc->sc_res[0], AGP_I810_PGTBL_CTL));
	device_printf(dev, "AGP_I855_GCC1: 0x%02x\n",
	    pci_read_config(sc->bdev, AGP_I855_GCC1, 1));
}

static void
agp_i915_dump_regs(device_t dev)
{
	struct agp_i810_softc *sc = device_get_softc(dev);

	device_printf(dev, "AGP_I810_PGTBL_CTL: %08x\n",
	    bus_read_4(sc->sc_res[0], AGP_I810_PGTBL_CTL));
	device_printf(dev, "AGP_I855_GCC1: 0x%02x\n",
	    pci_read_config(sc->bdev, AGP_I855_GCC1, 1));
	device_printf(dev, "AGP_I915_MSAC: 0x%02x\n",
	    pci_read_config(sc->bdev, AGP_I915_MSAC, 1));
}

static void
agp_i965_dump_regs(device_t dev)
{
	struct agp_i810_softc *sc = device_get_softc(dev);

	device_printf(dev, "AGP_I965_PGTBL_CTL2: %08x\n",
	    bus_read_4(sc->sc_res[0], AGP_I965_PGTBL_CTL2));
	device_printf(dev, "AGP_I855_GCC1: 0x%02x\n",
	    pci_read_config(sc->bdev, AGP_I855_GCC1, 1));
	device_printf(dev, "AGP_I965_MSAC: 0x%02x\n",
	    pci_read_config(sc->bdev, AGP_I965_MSAC, 1));
}

static int
agp_i810_get_stolen_size(device_t dev)
{
	struct agp_i810_softc *sc;

	sc = device_get_softc(dev);
	sc->stolen = 0;
	sc->stolen_size = 0;
	return (0);
}

static int
agp_i830_get_stolen_size(device_t dev)
{
	struct agp_i810_softc *sc;
	unsigned int gcc1;

	sc = device_get_softc(dev);

	gcc1 = pci_read_config(sc->bdev, AGP_I830_GCC1, 1);
	switch (gcc1 & AGP_I830_GCC1_GMS) {
	case AGP_I830_GCC1_GMS_STOLEN_512:
		sc->stolen = (512 - 132) * 1024 / 4096;
		sc->stolen_size = 512 * 1024;
		break;
	case AGP_I830_GCC1_GMS_STOLEN_1024:
		sc->stolen = (1024 - 132) * 1024 / 4096;
		sc->stolen_size = 1024 * 1024;
		break;
	case AGP_I830_GCC1_GMS_STOLEN_8192:
		sc->stolen = (8192 - 132) * 1024 / 4096;
		sc->stolen_size = 8192 * 1024;
		break;
	default:
		sc->stolen = 0;
		device_printf(dev,
		    "unknown memory configuration, disabling (GCC1 %x)\n",
		    gcc1);
		return (EINVAL);
	}
	return (0);
}

static int
agp_i915_get_stolen_size(device_t dev)
{
	struct agp_i810_softc *sc;
	unsigned int gcc1, stolen, gtt_size;

	sc = device_get_softc(dev);

	/*
	 * Stolen memory is set up at the beginning of the aperture by
	 * the BIOS, consisting of the GATT followed by 4kb for the
	 * BIOS display.
	 */
	switch (sc->match->driver->chiptype) {
	case CHIP_I855:
		gtt_size = 128;
		break;
	case CHIP_I915:
		gtt_size = 256;
		break;
	case CHIP_I965:
		switch (bus_read_4(sc->sc_res[0], AGP_I810_PGTBL_CTL) &
			AGP_I810_PGTBL_SIZE_MASK) {
		case AGP_I810_PGTBL_SIZE_128KB:
			gtt_size = 128;
			break;
		case AGP_I810_PGTBL_SIZE_256KB:
			gtt_size = 256;
			break;
		case AGP_I810_PGTBL_SIZE_512KB:
			gtt_size = 512;
			break;
		case AGP_I965_PGTBL_SIZE_1MB:
			gtt_size = 1024;
			break;
		case AGP_I965_PGTBL_SIZE_2MB:
			gtt_size = 2048;
			break;
		case AGP_I965_PGTBL_SIZE_1_5MB:
			gtt_size = 1024 + 512;
			break;
		default:
			device_printf(dev, "Bad PGTBL size\n");
			return (EINVAL);
		}
		break;
	case CHIP_G33:
		gcc1 = pci_read_config(sc->bdev, AGP_I855_GCC1, 2);
		switch (gcc1 & AGP_G33_MGGC_GGMS_MASK) {
		case AGP_G33_MGGC_GGMS_SIZE_1M:
			gtt_size = 1024;
			break;
		case AGP_G33_MGGC_GGMS_SIZE_2M:
			gtt_size = 2048;
			break;
		default:
			device_printf(dev, "Bad PGTBL size\n");
			return (EINVAL);
		}
		break;
	case CHIP_IGD:
	case CHIP_G4X:
		gtt_size = 0;
		break;
	default:
		device_printf(dev, "Bad chiptype\n");
		return (EINVAL);
	}

	/* GCC1 is called MGGC on i915+ */
	gcc1 = pci_read_config(sc->bdev, AGP_I855_GCC1, 1);
	switch (gcc1 & AGP_I855_GCC1_GMS) {
	case AGP_I855_GCC1_GMS_STOLEN_1M:
		stolen = 1024;
		break;
	case AGP_I855_GCC1_GMS_STOLEN_4M:
		stolen = 4 * 1024;
		break;
	case AGP_I855_GCC1_GMS_STOLEN_8M:
		stolen = 8 * 1024;
		break;
	case AGP_I855_GCC1_GMS_STOLEN_16M:
		stolen = 16 * 1024;
		break;
	case AGP_I855_GCC1_GMS_STOLEN_32M:
		stolen = 32 * 1024;
		break;
	case AGP_I915_GCC1_GMS_STOLEN_48M:
		stolen = sc->match->driver->gen > 2 ? 48 * 1024 : 0;
		break;
	case AGP_I915_GCC1_GMS_STOLEN_64M:
		stolen = sc->match->driver->gen > 2 ? 64 * 1024 : 0;
		break;
	case AGP_G33_GCC1_GMS_STOLEN_128M:
		stolen = sc->match->driver->gen > 2 ? 128 * 1024 : 0;
		break;
	case AGP_G33_GCC1_GMS_STOLEN_256M:
		stolen = sc->match->driver->gen > 2 ? 256 * 1024 : 0;
		break;
	case AGP_G4X_GCC1_GMS_STOLEN_96M:
		if (sc->match->driver->chiptype == CHIP_I965 ||
		    sc->match->driver->chiptype == CHIP_G4X)
			stolen = 96 * 1024;
		else
			stolen = 0;
		break;
	case AGP_G4X_GCC1_GMS_STOLEN_160M:
		if (sc->match->driver->chiptype == CHIP_I965 ||
		    sc->match->driver->chiptype == CHIP_G4X)
			stolen = 160 * 1024;
		else
			stolen = 0;
		break;
	case AGP_G4X_GCC1_GMS_STOLEN_224M:
		if (sc->match->driver->chiptype == CHIP_I965 ||
		    sc->match->driver->chiptype == CHIP_G4X)
			stolen = 224 * 1024;
		else
			stolen = 0;
		break;
	case AGP_G4X_GCC1_GMS_STOLEN_352M:
		if (sc->match->driver->chiptype == CHIP_I965 ||
		    sc->match->driver->chiptype == CHIP_G4X)
			stolen = 352 * 1024;
		else
			stolen = 0;
		break;
	default:
		device_printf(dev,
		    "unknown memory configuration, disabling (GCC1 %x)\n",
		    gcc1);
		return (EINVAL);
	}

	gtt_size += 4;
	sc->stolen_size = stolen * 1024;
	sc->stolen = (stolen - gtt_size) * 1024 / 4096;

	return (0);
}

static int
agp_i810_get_gtt_mappable_entries(device_t dev)
{
	struct agp_i810_softc *sc;
	uint32_t ap;
	uint16_t miscc;

	sc = device_get_softc(dev);
	miscc = pci_read_config(sc->bdev, AGP_I810_MISCC, 2);
	if ((miscc & AGP_I810_MISCC_WINSIZE) == AGP_I810_MISCC_WINSIZE_32)
		ap = 32;
	else
		ap = 64;
	sc->gtt_mappable_entries = (ap * 1024 * 1024) >> AGP_PAGE_SHIFT;
	return (0);
}

static int
agp_i830_get_gtt_mappable_entries(device_t dev)
{
	struct agp_i810_softc *sc;
	uint32_t ap;
	uint16_t gmch_ctl;

	sc = device_get_softc(dev);
	gmch_ctl = pci_read_config(sc->bdev, AGP_I830_GCC1, 2);
	if ((gmch_ctl & AGP_I830_GCC1_GMASIZE) == AGP_I830_GCC1_GMASIZE_64)
		ap = 64;
	else
		ap = 128;
	sc->gtt_mappable_entries = (ap * 1024 * 1024) >> AGP_PAGE_SHIFT;
	return (0);
}

static int
agp_i915_get_gtt_mappable_entries(device_t dev)
{
	struct agp_i810_softc *sc;
	uint32_t ap;

	sc = device_get_softc(dev);
	ap = AGP_GET_APERTURE(dev);
	sc->gtt_mappable_entries = ap >> AGP_PAGE_SHIFT;
	return (0);
}

static int
agp_i810_get_gtt_total_entries(device_t dev)
{
	struct agp_i810_softc *sc;

	sc = device_get_softc(dev);
	sc->gtt_total_entries = sc->gtt_mappable_entries;
	return (0);
}

static int
agp_i965_get_gtt_total_entries(device_t dev)
{
	struct agp_i810_softc *sc;
	uint32_t pgetbl_ctl;
	int error;

	sc = device_get_softc(dev);
	error = 0;
	pgetbl_ctl = bus_read_4(sc->sc_res[0], AGP_I810_PGTBL_CTL);
	switch (pgetbl_ctl & AGP_I810_PGTBL_SIZE_MASK) {
	case AGP_I810_PGTBL_SIZE_128KB:
		sc->gtt_total_entries = 128 * 1024 / 4;
		break;
	case AGP_I810_PGTBL_SIZE_256KB:
		sc->gtt_total_entries = 256 * 1024 / 4;
		break;
	case AGP_I810_PGTBL_SIZE_512KB:
		sc->gtt_total_entries = 512 * 1024 / 4;
		break;
	/* GTT pagetable sizes bigger than 512KB are not possible on G33! */
	case AGP_I810_PGTBL_SIZE_1MB:
		sc->gtt_total_entries = 1024 * 1024 / 4;
		break;
	case AGP_I810_PGTBL_SIZE_2MB:
		sc->gtt_total_entries = 2 * 1024 * 1024 / 4;
		break;
	case AGP_I810_PGTBL_SIZE_1_5MB:
		sc->gtt_total_entries = (1024 + 512) * 1024 / 4;
		break;
	default:
		device_printf(dev, "Unknown page table size\n");
		error = ENXIO;
	}
	return (error);
}

static void
agp_gen5_adjust_pgtbl_size(device_t dev, uint32_t sz)
{
	struct agp_i810_softc *sc;
	uint32_t pgetbl_ctl, pgetbl_ctl2;

	sc = device_get_softc(dev);

	/* Disable per-process page table. */
	pgetbl_ctl2 = bus_read_4(sc->sc_res[0], AGP_I965_PGTBL_CTL2);
	pgetbl_ctl2 &= ~AGP_I810_PGTBL_ENABLED;
	bus_write_4(sc->sc_res[0], AGP_I965_PGTBL_CTL2, pgetbl_ctl2);

	/* Write the new ggtt size. */
	pgetbl_ctl = bus_read_4(sc->sc_res[0], AGP_I810_PGTBL_CTL);
	pgetbl_ctl &= ~AGP_I810_PGTBL_SIZE_MASK;
	pgetbl_ctl |= sz;
	bus_write_4(sc->sc_res[0], AGP_I810_PGTBL_CTL, pgetbl_ctl);
}

static int
agp_gen5_get_gtt_total_entries(device_t dev)
{
	struct agp_i810_softc *sc;
	uint16_t gcc1;

	sc = device_get_softc(dev);

	gcc1 = pci_read_config(sc->bdev, AGP_I830_GCC1, 2);
	switch (gcc1 & AGP_G4x_GCC1_SIZE_MASK) {
	case AGP_G4x_GCC1_SIZE_1M:
	case AGP_G4x_GCC1_SIZE_VT_1M:
		agp_gen5_adjust_pgtbl_size(dev, AGP_I810_PGTBL_SIZE_1MB);
		break;
	case AGP_G4x_GCC1_SIZE_VT_1_5M:
		agp_gen5_adjust_pgtbl_size(dev, AGP_I810_PGTBL_SIZE_1_5MB);
		break;
	case AGP_G4x_GCC1_SIZE_2M:
	case AGP_G4x_GCC1_SIZE_VT_2M:
		agp_gen5_adjust_pgtbl_size(dev, AGP_I810_PGTBL_SIZE_2MB);
		break;
	default:
		device_printf(dev, "Unknown page table size\n");
		return (ENXIO);
	}

	return (agp_i965_get_gtt_total_entries(dev));
}

static int
agp_i810_install_gatt(device_t dev)
{
	struct agp_i810_softc *sc;

	sc = device_get_softc(dev);

	/* Some i810s have on-chip memory called dcache. */
	if ((bus_read_1(sc->sc_res[0], AGP_I810_DRT) & AGP_I810_DRT_POPULATED)
	    != 0)
		sc->dcache_size = 4 * 1024 * 1024;
	else
		sc->dcache_size = 0;

	/* According to the specs the gatt on the i810 must be 64k. */
	sc->gatt->ag_virtual = (void *)kmem_alloc_contig(64 * 1024, M_NOWAIT |
	    M_ZERO, 0, ~0, PAGE_SIZE, 0, VM_MEMATTR_WRITE_COMBINING);
	if (sc->gatt->ag_virtual == NULL) {
		if (bootverbose)
			device_printf(dev, "contiguous allocation failed\n");
		return (ENOMEM);
	}

	sc->gatt->ag_physical = vtophys((vm_offset_t)sc->gatt->ag_virtual);
	/* Install the GATT. */
	bus_write_4(sc->sc_res[0], AGP_I810_PGTBL_CTL,
	    sc->gatt->ag_physical | 1);
	return (0);
}

static void
agp_i830_install_gatt_init(struct agp_i810_softc *sc)
{
	uint32_t pgtblctl;

	/*
	 * The i830 automatically initializes the 128k gatt on boot.
	 * GATT address is already in there, make sure it's enabled.
	 */
	pgtblctl = bus_read_4(sc->sc_res[0], AGP_I810_PGTBL_CTL);
	pgtblctl |= 1;
	bus_write_4(sc->sc_res[0], AGP_I810_PGTBL_CTL, pgtblctl);
	
	sc->gatt->ag_physical = pgtblctl & ~1;
}

static int
agp_i830_install_gatt(device_t dev)
{
	struct agp_i810_softc *sc;

	sc = device_get_softc(dev);
	agp_i830_install_gatt_init(sc);
	return (0);
}

static int
agp_gen4_install_gatt(device_t dev, const vm_size_t gtt_offset)
{
	struct agp_i810_softc *sc;

	sc = device_get_softc(dev);
	pmap_change_attr((vm_offset_t)rman_get_virtual(sc->sc_res[0]) +
	    gtt_offset, rman_get_size(sc->sc_res[0]) - gtt_offset,
	    VM_MEMATTR_WRITE_COMBINING);
	agp_i830_install_gatt_init(sc);
	return (0);
}

static int
agp_i965_install_gatt(device_t dev)
{

	return (agp_gen4_install_gatt(dev, 512 * 1024));
}

static int
agp_g4x_install_gatt(device_t dev)
{

	return (agp_gen4_install_gatt(dev, 2 * 1024 * 1024));
}

static int
agp_i810_attach(device_t dev)
{
	struct agp_i810_softc *sc;
	int error;

	sc = device_get_softc(dev);
	sc->bdev = agp_i810_find_bridge(dev);
	if (sc->bdev == NULL)
		return (ENOENT);

	sc->match = agp_i810_match(dev);

	agp_set_aperture_resource(dev, sc->match->driver->gen <= 2 ?
	    AGP_APBASE : AGP_I915_GMADR);
	error = agp_generic_attach(dev);
	if (error)
		return (error);

	if (ptoa((vm_paddr_t)Maxmem) >
	    (1ULL << sc->match->driver->busdma_addr_mask_sz) - 1) {
		device_printf(dev, "agp_i810 does not support physical "
		    "memory above %ju.\n", (uintmax_t)(1ULL <<
		    sc->match->driver->busdma_addr_mask_sz) - 1);
		return (ENOENT);
	}

	if (bus_alloc_resources(dev, sc->match->driver->res_spec, sc->sc_res)) {
		agp_generic_detach(dev);
		return (ENODEV);
	}

	sc->initial_aperture = AGP_GET_APERTURE(dev);
	sc->gatt = malloc(sizeof(struct agp_gatt), M_AGP, M_WAITOK);
	sc->gatt->ag_entries = AGP_GET_APERTURE(dev) >> AGP_PAGE_SHIFT;

	if ((error = sc->match->driver->get_stolen_size(dev)) != 0 ||
	    (error = sc->match->driver->install_gatt(dev)) != 0 ||
	    (error = sc->match->driver->get_gtt_mappable_entries(dev)) != 0 ||
	    (error = sc->match->driver->get_gtt_total_entries(dev)) != 0 ||
	    (error = sc->match->driver->chipset_flush_setup(dev)) != 0) {
		bus_release_resources(dev, sc->match->driver->res_spec,
		    sc->sc_res);
		free(sc->gatt, M_AGP);
		agp_generic_detach(dev);
		return (error);
	}

	intel_agp = dev;
	device_printf(dev, "aperture size is %dM",
	    sc->initial_aperture / 1024 / 1024);
	if (sc->stolen > 0)
		printf(", detected %dk stolen memory\n", sc->stolen * 4);
	else
		printf("\n");
	if (bootverbose) {
		sc->match->driver->dump_regs(dev);
		device_printf(dev, "Mappable GTT entries: %d\n",
		    sc->gtt_mappable_entries);
		device_printf(dev, "Total GTT entries: %d\n",
		    sc->gtt_total_entries);
	}
	return (0);
}

static void
agp_i810_deinstall_gatt(device_t dev)
{
	struct agp_i810_softc *sc;

	sc = device_get_softc(dev);
	bus_write_4(sc->sc_res[0], AGP_I810_PGTBL_CTL, 0);
	kmem_free((vm_offset_t)sc->gatt->ag_virtual, 64 * 1024);
}

static void
agp_i830_deinstall_gatt(device_t dev)
{
	struct agp_i810_softc *sc;
	unsigned int pgtblctl;

	sc = device_get_softc(dev);
	pgtblctl = bus_read_4(sc->sc_res[0], AGP_I810_PGTBL_CTL);
	pgtblctl &= ~1;
	bus_write_4(sc->sc_res[0], AGP_I810_PGTBL_CTL, pgtblctl);
}

static int
agp_i810_detach(device_t dev)
{
	struct agp_i810_softc *sc;

	sc = device_get_softc(dev);
	agp_free_cdev(dev);

	/* Clear the GATT base. */
	sc->match->driver->deinstall_gatt(dev);

	sc->match->driver->chipset_flush_teardown(dev);

	/* Put the aperture back the way it started. */
	AGP_SET_APERTURE(dev, sc->initial_aperture);

	free(sc->gatt, M_AGP);
	bus_release_resources(dev, sc->match->driver->res_spec, sc->sc_res);
	agp_free_res(dev);

	return (0);
}

static int
agp_i810_resume(device_t dev)
{
	struct agp_i810_softc *sc;
	sc = device_get_softc(dev);

	AGP_SET_APERTURE(dev, sc->initial_aperture);

	/* Install the GATT. */
	bus_write_4(sc->sc_res[0], AGP_I810_PGTBL_CTL,
	sc->gatt->ag_physical | 1);

	return (bus_generic_resume(dev));
}

/**
 * Sets the PCI resource size of the aperture on i830-class and below chipsets,
 * while returning failure on later chipsets when an actual change is
 * requested.
 *
 * This whole function is likely bogus, as the kernel would probably need to
 * reconfigure the placement of the AGP aperture if a larger size is requested,
 * which doesn't happen currently.
 */
static int
agp_i810_set_aperture(device_t dev, u_int32_t aperture)
{
	struct agp_i810_softc *sc;
	u_int16_t miscc;

	sc = device_get_softc(dev);
	/*
	 * Double check for sanity.
	 */
	if (aperture != 32 * 1024 * 1024 && aperture != 64 * 1024 * 1024) {
		device_printf(dev, "bad aperture size %d\n", aperture);
		return (EINVAL);
	}

	miscc = pci_read_config(sc->bdev, AGP_I810_MISCC, 2);
	miscc &= ~AGP_I810_MISCC_WINSIZE;
	if (aperture == 32 * 1024 * 1024)
		miscc |= AGP_I810_MISCC_WINSIZE_32;
	else
		miscc |= AGP_I810_MISCC_WINSIZE_64;
	
	pci_write_config(sc->bdev, AGP_I810_MISCC, miscc, 2);
	return (0);
}

static int
agp_i830_set_aperture(device_t dev, u_int32_t aperture)
{
	struct agp_i810_softc *sc;
	u_int16_t gcc1;

	sc = device_get_softc(dev);

	if (aperture != 64 * 1024 * 1024 &&
	    aperture != 128 * 1024 * 1024) {
		device_printf(dev, "bad aperture size %d\n", aperture);
		return (EINVAL);
	}
	gcc1 = pci_read_config(sc->bdev, AGP_I830_GCC1, 2);
	gcc1 &= ~AGP_I830_GCC1_GMASIZE;
	if (aperture == 64 * 1024 * 1024)
		gcc1 |= AGP_I830_GCC1_GMASIZE_64;
	else
		gcc1 |= AGP_I830_GCC1_GMASIZE_128;

	pci_write_config(sc->bdev, AGP_I830_GCC1, gcc1, 2);
	return (0);
}

static int
agp_i915_set_aperture(device_t dev, u_int32_t aperture)
{

	return (agp_generic_set_aperture(dev, aperture));
}

static int
agp_i810_method_set_aperture(device_t dev, u_int32_t aperture)
{
	struct agp_i810_softc *sc;

	sc = device_get_softc(dev);
	return (sc->match->driver->set_aperture(dev, aperture));
}

/**
 * Writes a GTT entry mapping the page at the given offset from the
 * beginning of the aperture to the given physical address.  Setup the
 * caching mode according to flags.
 *
 * For gen 1, 2 and 3, GTT start is located at AGP_I810_GTT offset
 * from corresponding BAR start. For gen 4, offset is 512KB +
 * AGP_I810_GTT, for gen 5 and 6 it is 2MB + AGP_I810_GTT.
 *
 * Also, the bits of the physical page address above 4GB needs to be
 * placed into bits 40-32 of PTE.
 */
static void
agp_i810_install_gtt_pte(device_t dev, u_int index, vm_offset_t physical,
    int flags)
{
	uint32_t pte;

	pte = (u_int32_t)physical | I810_PTE_VALID;
	if (flags == AGP_DCACHE_MEMORY)
		pte |= I810_PTE_LOCAL;
	else if (flags == AGP_USER_CACHED_MEMORY)
		pte |= I830_PTE_SYSTEM_CACHED;
	agp_i810_write_gtt(dev, index, pte);
}

static void
agp_i810_write_gtt(device_t dev, u_int index, uint32_t pte)
{
	struct agp_i810_softc *sc;

	sc = device_get_softc(dev);
	bus_write_4(sc->sc_res[0], AGP_I810_GTT + index * 4, pte);
	CTR2(KTR_AGP_I810, "810_pte %x %x", index, pte);
}

static void
agp_i830_install_gtt_pte(device_t dev, u_int index, vm_offset_t physical,
    int flags)
{
	uint32_t pte;

	pte = (u_int32_t)physical | I810_PTE_VALID;
	if (flags == AGP_USER_CACHED_MEMORY)
		pte |= I830_PTE_SYSTEM_CACHED;
	agp_i810_write_gtt(dev, index, pte);
}

static void
agp_i915_install_gtt_pte(device_t dev, u_int index, vm_offset_t physical,
    int flags)
{
	uint32_t pte;

	pte = (u_int32_t)physical | I810_PTE_VALID;
	if (flags == AGP_USER_CACHED_MEMORY)
		pte |= I830_PTE_SYSTEM_CACHED;
	pte |= (physical & 0x0000000f00000000ull) >> 28;
	agp_i915_write_gtt(dev, index, pte);
}

static void
agp_i915_write_gtt(device_t dev, u_int index, uint32_t pte)
{
	struct agp_i810_softc *sc;

	sc = device_get_softc(dev);
	bus_write_4(sc->sc_res[1], index * 4, pte);
	CTR2(KTR_AGP_I810, "915_pte %x %x", index, pte);
}

static void
agp_i965_install_gtt_pte(device_t dev, u_int index, vm_offset_t physical,
    int flags)
{
	uint32_t pte;

	pte = (u_int32_t)physical | I810_PTE_VALID;
	if (flags == AGP_USER_CACHED_MEMORY)
		pte |= I830_PTE_SYSTEM_CACHED;
	pte |= (physical & 0x0000000f00000000ull) >> 28;
	agp_i965_write_gtt(dev, index, pte);
}

static void
agp_i965_write_gtt(device_t dev, u_int index, uint32_t pte)
{
	struct agp_i810_softc *sc;

	sc = device_get_softc(dev);
	bus_write_4(sc->sc_res[0], index * 4 + (512 * 1024), pte);
	CTR2(KTR_AGP_I810, "965_pte %x %x", index, pte);
}

static void
agp_g4x_install_gtt_pte(device_t dev, u_int index, vm_offset_t physical,
    int flags)
{
	uint32_t pte;

	pte = (u_int32_t)physical | I810_PTE_VALID;
	if (flags == AGP_USER_CACHED_MEMORY)
		pte |= I830_PTE_SYSTEM_CACHED;
	pte |= (physical & 0x0000000f00000000ull) >> 28;
	agp_g4x_write_gtt(dev, index, pte);
}

static void
agp_g4x_write_gtt(device_t dev, u_int index, uint32_t pte)
{
	struct agp_i810_softc *sc;

	sc = device_get_softc(dev);
	bus_write_4(sc->sc_res[0], index * 4 + (2 * 1024 * 1024), pte);
	CTR2(KTR_AGP_I810, "g4x_pte %x %x", index, pte);
}

static int
agp_i810_bind_page(device_t dev, vm_offset_t offset, vm_offset_t physical)
{
	struct agp_i810_softc *sc = device_get_softc(dev);
	u_int index;

	if (offset >= (sc->gatt->ag_entries << AGP_PAGE_SHIFT)) {
		device_printf(dev, "failed: offset is 0x%08jx, "
		    "shift is %d, entries is %d\n", (intmax_t)offset,
		    AGP_PAGE_SHIFT, sc->gatt->ag_entries);
		return (EINVAL);
	}
	index = offset >> AGP_PAGE_SHIFT;
	if (sc->stolen != 0 && index < sc->stolen) {
		device_printf(dev, "trying to bind into stolen memory\n");
		return (EINVAL);
	}
	sc->match->driver->install_gtt_pte(dev, index, physical, 0);
	return (0);
}

static int
agp_i810_unbind_page(device_t dev, vm_offset_t offset)
{
	struct agp_i810_softc *sc;
	u_int index;

	sc = device_get_softc(dev);
	if (offset >= (sc->gatt->ag_entries << AGP_PAGE_SHIFT))
		return (EINVAL);
	index = offset >> AGP_PAGE_SHIFT;
	if (sc->stolen != 0 && index < sc->stolen) {
		device_printf(dev, "trying to unbind from stolen memory\n");
		return (EINVAL);
	}
	sc->match->driver->install_gtt_pte(dev, index, 0, 0);
	return (0);
}

static u_int32_t
agp_i810_read_gtt_pte(device_t dev, u_int index)
{
	struct agp_i810_softc *sc;
	u_int32_t pte;

	sc = device_get_softc(dev);
	pte = bus_read_4(sc->sc_res[0], AGP_I810_GTT + index * 4);
	return (pte);
}

static u_int32_t
agp_i915_read_gtt_pte(device_t dev, u_int index)
{
	struct agp_i810_softc *sc;
	u_int32_t pte;

	sc = device_get_softc(dev);
	pte = bus_read_4(sc->sc_res[1], index * 4);
	return (pte);
}

static u_int32_t
agp_i965_read_gtt_pte(device_t dev, u_int index)
{
	struct agp_i810_softc *sc;
	u_int32_t pte;

	sc = device_get_softc(dev);
	pte = bus_read_4(sc->sc_res[0], index * 4 + (512 * 1024));
	return (pte);
}

static u_int32_t
agp_g4x_read_gtt_pte(device_t dev, u_int index)
{
	struct agp_i810_softc *sc;
	u_int32_t pte;

	sc = device_get_softc(dev);
	pte = bus_read_4(sc->sc_res[0], index * 4 + (2 * 1024 * 1024));
	return (pte);
}

static vm_paddr_t
agp_i810_read_gtt_pte_paddr(device_t dev, u_int index)
{
	struct agp_i810_softc *sc;
	u_int32_t pte;
	vm_paddr_t res;

	sc = device_get_softc(dev);
	pte = sc->match->driver->read_gtt_pte(dev, index);
	res = pte & ~PAGE_MASK;
	return (res);
}

static vm_paddr_t
agp_i915_read_gtt_pte_paddr(device_t dev, u_int index)
{
	struct agp_i810_softc *sc;
	u_int32_t pte;
	vm_paddr_t res;

	sc = device_get_softc(dev);
	pte = sc->match->driver->read_gtt_pte(dev, index);
	res = (pte & ~PAGE_MASK) | ((pte & 0xf0) << 28);
	return (res);
}

/*
 * Writing via memory mapped registers already flushes all TLBs.
 */
static void
agp_i810_flush_tlb(device_t dev)
{
}

static int
agp_i810_enable(device_t dev, u_int32_t mode)
{

	return (0);
}

static struct agp_memory *
agp_i810_alloc_memory(device_t dev, int type, vm_size_t size)
{
	struct agp_i810_softc *sc;
	struct agp_memory *mem;
	vm_page_t m;

	sc = device_get_softc(dev);

	if ((size & (AGP_PAGE_SIZE - 1)) != 0 ||
	    sc->agp.as_allocated + size > sc->agp.as_maxmem)
		return (0);

	if (type == 1) {
		/*
		 * Mapping local DRAM into GATT.
		 */
		if (sc->match->driver->chiptype != CHIP_I810)
			return (0);
		if (size != sc->dcache_size)
			return (0);
	} else if (type == 2) {
		/*
		 * Type 2 is the contiguous physical memory type, that hands
		 * back a physical address.  This is used for cursors on i810.
		 * Hand back as many single pages with physical as the user
		 * wants, but only allow one larger allocation (ARGB cursor)
		 * for simplicity.
		 */
		if (size != AGP_PAGE_SIZE) {
			if (sc->argb_cursor != NULL)
				return (0);

			/* Allocate memory for ARGB cursor, if we can. */
			sc->argb_cursor = contigmalloc(size, M_AGP,
			   0, 0, ~0, PAGE_SIZE, 0);
			if (sc->argb_cursor == NULL)
				return (0);
		}
	}

	mem = malloc(sizeof *mem, M_AGP, M_WAITOK);
	mem->am_id = sc->agp.as_nextid++;
	mem->am_size = size;
	mem->am_type = type;
	if (type != 1 && (type != 2 || size == AGP_PAGE_SIZE))
		mem->am_obj = vm_object_allocate(OBJT_DEFAULT,
		    atop(round_page(size)));
	else
		mem->am_obj = 0;

	if (type == 2) {
		if (size == AGP_PAGE_SIZE) {
			/*
			 * Allocate and wire down the page now so that we can
			 * get its physical address.
			 */
			VM_OBJECT_WLOCK(mem->am_obj);
			m = vm_page_grab(mem->am_obj, 0, VM_ALLOC_NOBUSY |
			    VM_ALLOC_WIRED | VM_ALLOC_ZERO);
			VM_OBJECT_WUNLOCK(mem->am_obj);
			mem->am_physical = VM_PAGE_TO_PHYS(m);
		} else {
			/* Our allocation is already nicely wired down for us.
			 * Just grab the physical address.
			 */
			mem->am_physical = vtophys(sc->argb_cursor);
		}
	} else
		mem->am_physical = 0;

	mem->am_offset = 0;
	mem->am_is_bound = 0;
	TAILQ_INSERT_TAIL(&sc->agp.as_memory, mem, am_link);
	sc->agp.as_allocated += size;

	return (mem);
}

static int
agp_i810_free_memory(device_t dev, struct agp_memory *mem)
{
	struct agp_i810_softc *sc;
	vm_page_t m;

	if (mem->am_is_bound)
		return (EBUSY);

	sc = device_get_softc(dev);

	if (mem->am_type == 2) {
		if (mem->am_size == AGP_PAGE_SIZE) {
			/*
			 * Unwire the page which we wired in alloc_memory.
			 */
			VM_OBJECT_WLOCK(mem->am_obj);
			m = vm_page_lookup(mem->am_obj, 0);
			vm_page_lock(m);
			vm_page_unwire(m, PQ_INACTIVE);
			vm_page_unlock(m);
			VM_OBJECT_WUNLOCK(mem->am_obj);
		} else {
			contigfree(sc->argb_cursor, mem->am_size, M_AGP);
			sc->argb_cursor = NULL;
		}
	}

	sc->agp.as_allocated -= mem->am_size;
	TAILQ_REMOVE(&sc->agp.as_memory, mem, am_link);
	if (mem->am_obj)
		vm_object_deallocate(mem->am_obj);
	free(mem, M_AGP);
	return (0);
}

static int
agp_i810_bind_memory(device_t dev, struct agp_memory *mem, vm_offset_t offset)
{
	struct agp_i810_softc *sc;
	vm_offset_t i;

	/* Do some sanity checks first. */
	if ((offset & (AGP_PAGE_SIZE - 1)) != 0 ||
	    offset + mem->am_size > AGP_GET_APERTURE(dev)) {
		device_printf(dev, "binding memory at bad offset %#x\n",
		    (int)offset);
		return (EINVAL);
	}

	sc = device_get_softc(dev);
	if (mem->am_type == 2 && mem->am_size != AGP_PAGE_SIZE) {
		mtx_lock(&sc->agp.as_lock);
		if (mem->am_is_bound) {
			mtx_unlock(&sc->agp.as_lock);
			return (EINVAL);
		}
		/* The memory's already wired down, just stick it in the GTT. */
		for (i = 0; i < mem->am_size; i += AGP_PAGE_SIZE) {
			sc->match->driver->install_gtt_pte(dev, (offset + i) >>
			    AGP_PAGE_SHIFT, mem->am_physical + i, 0);
		}
		mem->am_offset = offset;
		mem->am_is_bound = 1;
		mtx_unlock(&sc->agp.as_lock);
		return (0);
	}

	if (mem->am_type != 1)
		return (agp_generic_bind_memory(dev, mem, offset));

	/*
	 * Mapping local DRAM into GATT.
	 */
	if (sc->match->driver->chiptype != CHIP_I810)
		return (EINVAL);
	for (i = 0; i < mem->am_size; i += AGP_PAGE_SIZE)
		bus_write_4(sc->sc_res[0],
		    AGP_I810_GTT + (i >> AGP_PAGE_SHIFT) * 4, i | 3);

	return (0);
}

static int
agp_i810_unbind_memory(device_t dev, struct agp_memory *mem)
{
	struct agp_i810_softc *sc;
	vm_offset_t i;

	sc = device_get_softc(dev);

	if (mem->am_type == 2 && mem->am_size != AGP_PAGE_SIZE) {
		mtx_lock(&sc->agp.as_lock);
		if (!mem->am_is_bound) {
			mtx_unlock(&sc->agp.as_lock);
			return (EINVAL);
		}

		for (i = 0; i < mem->am_size; i += AGP_PAGE_SIZE) {
			sc->match->driver->install_gtt_pte(dev,
			    (mem->am_offset + i) >> AGP_PAGE_SHIFT, 0, 0);
		}
		mem->am_is_bound = 0;
		mtx_unlock(&sc->agp.as_lock);
		return (0);
	}

	if (mem->am_type != 1)
		return (agp_generic_unbind_memory(dev, mem));

	if (sc->match->driver->chiptype != CHIP_I810)
		return (EINVAL);
	for (i = 0; i < mem->am_size; i += AGP_PAGE_SIZE) {
		sc->match->driver->install_gtt_pte(dev, i >> AGP_PAGE_SHIFT,
		    0, 0);
	}
	return (0);
}

static device_method_t agp_i810_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	agp_i810_identify),
	DEVMETHOD(device_probe,		agp_i810_probe),
	DEVMETHOD(device_attach,	agp_i810_attach),
	DEVMETHOD(device_detach,	agp_i810_detach),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	agp_i810_resume),

	/* AGP interface */
	DEVMETHOD(agp_get_aperture,	agp_generic_get_aperture),
	DEVMETHOD(agp_set_aperture,	agp_i810_method_set_aperture),
	DEVMETHOD(agp_bind_page,	agp_i810_bind_page),
	DEVMETHOD(agp_unbind_page,	agp_i810_unbind_page),
	DEVMETHOD(agp_flush_tlb,	agp_i810_flush_tlb),
	DEVMETHOD(agp_enable,		agp_i810_enable),
	DEVMETHOD(agp_alloc_memory,	agp_i810_alloc_memory),
	DEVMETHOD(agp_free_memory,	agp_i810_free_memory),
	DEVMETHOD(agp_bind_memory,	agp_i810_bind_memory),
	DEVMETHOD(agp_unbind_memory,	agp_i810_unbind_memory),
	DEVMETHOD(agp_chipset_flush,	agp_intel_gtt_chipset_flush),

	{ 0, 0 }
};

static driver_t agp_i810_driver = {
	"agp",
	agp_i810_methods,
	sizeof(struct agp_i810_softc),
};

static devclass_t agp_devclass;

DRIVER_MODULE(agp_i810, vgapci, agp_i810_driver, agp_devclass, 0, 0);
MODULE_DEPEND(agp_i810, agp, 1, 1, 1);
MODULE_DEPEND(agp_i810, pci, 1, 1, 1);

void
agp_intel_gtt_clear_range(device_t dev, u_int first_entry, u_int num_entries)
{
	struct agp_i810_softc *sc;
	u_int i;

	sc = device_get_softc(dev);
	for (i = 0; i < num_entries; i++)
		sc->match->driver->install_gtt_pte(dev, first_entry + i,
		    VM_PAGE_TO_PHYS(bogus_page), 0);
	sc->match->driver->read_gtt_pte(dev, first_entry + num_entries - 1);
}

void
agp_intel_gtt_insert_pages(device_t dev, u_int first_entry, u_int num_entries,
    vm_page_t *pages, u_int flags)
{
	struct agp_i810_softc *sc;
	u_int i;

	sc = device_get_softc(dev);
	for (i = 0; i < num_entries; i++) {
		MPASS(pages[i]->valid == VM_PAGE_BITS_ALL);
		MPASS(pages[i]->wire_count > 0);
		sc->match->driver->install_gtt_pte(dev, first_entry + i,
		    VM_PAGE_TO_PHYS(pages[i]), flags);
	}
	sc->match->driver->read_gtt_pte(dev, first_entry + num_entries - 1);
}

struct intel_gtt
agp_intel_gtt_get(device_t dev)
{
	struct agp_i810_softc *sc;
	struct intel_gtt res;

	sc = device_get_softc(dev);
	res.stolen_size = sc->stolen_size;
	res.gtt_total_entries = sc->gtt_total_entries;
	res.gtt_mappable_entries = sc->gtt_mappable_entries;
	res.do_idle_maps = 0;
	res.scratch_page_dma = VM_PAGE_TO_PHYS(bogus_page);
	if (sc->agp.as_aperture != NULL)
		res.gma_bus_addr = rman_get_start(sc->agp.as_aperture);
	else
		res.gma_bus_addr = 0;
	return (res);
}

static int
agp_i810_chipset_flush_setup(device_t dev)
{

	return (0);
}

static void
agp_i810_chipset_flush_teardown(device_t dev)
{

	/* Nothing to do. */
}

static void
agp_i810_chipset_flush(device_t dev)
{

	/* Nothing to do. */
}

static void
agp_i830_chipset_flush(device_t dev)
{
	struct agp_i810_softc *sc;
	uint32_t hic;
	int i;

	sc = device_get_softc(dev);
	pmap_invalidate_cache();
	hic = bus_read_4(sc->sc_res[0], AGP_I830_HIC);
	bus_write_4(sc->sc_res[0], AGP_I830_HIC, hic | (1U << 31));
	for (i = 0; i < 20000 /* 1 sec */; i++) {
		hic = bus_read_4(sc->sc_res[0], AGP_I830_HIC);
		if ((hic & (1U << 31)) == 0)
			break;
		DELAY(50);
	}
}

static int
agp_i915_chipset_flush_alloc_page(device_t dev, uint64_t start, uint64_t end)
{
	struct agp_i810_softc *sc;
	device_t vga;

	sc = device_get_softc(dev);
	vga = device_get_parent(dev);
	sc->sc_flush_page_rid = 100;
	sc->sc_flush_page_res = BUS_ALLOC_RESOURCE(device_get_parent(vga), dev,
	    SYS_RES_MEMORY, &sc->sc_flush_page_rid, start, end, PAGE_SIZE,
	    RF_ACTIVE);
	if (sc->sc_flush_page_res == NULL) {
		device_printf(dev, "Failed to allocate flush page at 0x%jx\n",
		    (uintmax_t)start);
		return (EINVAL);
	}
	sc->sc_flush_page_vaddr = rman_get_virtual(sc->sc_flush_page_res);
	if (bootverbose) {
		device_printf(dev, "Allocated flush page phys 0x%jx virt %p\n",
		    (uintmax_t)rman_get_start(sc->sc_flush_page_res),
		    sc->sc_flush_page_vaddr);
	}
	return (0);
}

static void
agp_i915_chipset_flush_free_page(device_t dev)
{
	struct agp_i810_softc *sc;
	device_t vga;

	sc = device_get_softc(dev);
	vga = device_get_parent(dev);
	if (sc->sc_flush_page_res == NULL)
		return;
	BUS_DEACTIVATE_RESOURCE(device_get_parent(vga), dev, SYS_RES_MEMORY,
	    sc->sc_flush_page_rid, sc->sc_flush_page_res);
	BUS_RELEASE_RESOURCE(device_get_parent(vga), dev, SYS_RES_MEMORY,
	    sc->sc_flush_page_rid, sc->sc_flush_page_res);
}

static int
agp_i915_chipset_flush_setup(device_t dev)
{
	struct agp_i810_softc *sc;
	uint32_t temp;
	int error;

	sc = device_get_softc(dev);
	temp = pci_read_config(sc->bdev, AGP_I915_IFPADDR, 4);
	if ((temp & 1) != 0) {
		temp &= ~1;
		if (bootverbose)
			device_printf(dev,
			    "Found already configured flush page at 0x%jx\n",
			    (uintmax_t)temp);
		sc->sc_bios_allocated_flush_page = 1;
		/*
		 * In the case BIOS initialized the flush pointer (?)
		 * register, expect that BIOS also set up the resource
		 * for the page.
		 */
		error = agp_i915_chipset_flush_alloc_page(dev, temp,
		    temp + PAGE_SIZE - 1);
		if (error != 0)
			return (error);
	} else {
		sc->sc_bios_allocated_flush_page = 0;
		error = agp_i915_chipset_flush_alloc_page(dev, 0, 0xffffffff);
		if (error != 0)
			return (error);
		temp = rman_get_start(sc->sc_flush_page_res);
		pci_write_config(sc->bdev, AGP_I915_IFPADDR, temp | 1, 4);
	}
	return (0);
}

static void
agp_i915_chipset_flush_teardown(device_t dev)
{
	struct agp_i810_softc *sc;
	uint32_t temp;

	sc = device_get_softc(dev);
	if (sc->sc_flush_page_res == NULL)
		return;
	if (!sc->sc_bios_allocated_flush_page) {
		temp = pci_read_config(sc->bdev, AGP_I915_IFPADDR, 4);
		temp &= ~1;
		pci_write_config(sc->bdev, AGP_I915_IFPADDR, temp, 4);
	}		
	agp_i915_chipset_flush_free_page(dev);
}

static int
agp_i965_chipset_flush_setup(device_t dev)
{
	struct agp_i810_softc *sc;
	uint64_t temp;
	uint32_t temp_hi, temp_lo;
	int error;

	sc = device_get_softc(dev);

	temp_hi = pci_read_config(sc->bdev, AGP_I965_IFPADDR + 4, 4);
	temp_lo = pci_read_config(sc->bdev, AGP_I965_IFPADDR, 4);

	if ((temp_lo & 1) != 0) {
		temp = ((uint64_t)temp_hi << 32) | (temp_lo & ~1);
		if (bootverbose)
			device_printf(dev,
			    "Found already configured flush page at 0x%jx\n",
			    (uintmax_t)temp);
		sc->sc_bios_allocated_flush_page = 1;
		/*
		 * In the case BIOS initialized the flush pointer (?)
		 * register, expect that BIOS also set up the resource
		 * for the page.
		 */
		error = agp_i915_chipset_flush_alloc_page(dev, temp,
		    temp + PAGE_SIZE - 1);
		if (error != 0)
			return (error);
	} else {
		sc->sc_bios_allocated_flush_page = 0;
		error = agp_i915_chipset_flush_alloc_page(dev, 0, ~0);
		if (error != 0)
			return (error);
		temp = rman_get_start(sc->sc_flush_page_res);
		pci_write_config(sc->bdev, AGP_I965_IFPADDR + 4,
		    (temp >> 32) & UINT32_MAX, 4);
		pci_write_config(sc->bdev, AGP_I965_IFPADDR,
		    (temp & UINT32_MAX) | 1, 4);
	}
	return (0);
}

static void
agp_i965_chipset_flush_teardown(device_t dev)
{
	struct agp_i810_softc *sc;
	uint32_t temp_lo;

	sc = device_get_softc(dev);
	if (sc->sc_flush_page_res == NULL)
		return;
	if (!sc->sc_bios_allocated_flush_page) {
		temp_lo = pci_read_config(sc->bdev, AGP_I965_IFPADDR, 4);
		temp_lo &= ~1;
		pci_write_config(sc->bdev, AGP_I965_IFPADDR, temp_lo, 4);
	}
	agp_i915_chipset_flush_free_page(dev);
}

static void
agp_i915_chipset_flush(device_t dev)
{
	struct agp_i810_softc *sc;

	sc = device_get_softc(dev);
	*(uint32_t *)sc->sc_flush_page_vaddr = 1;
}

int
agp_intel_gtt_chipset_flush(device_t dev)
{
	struct agp_i810_softc *sc;

	sc = device_get_softc(dev);
	sc->match->driver->chipset_flush(dev);
	return (0);
}

void
agp_intel_gtt_unmap_memory(device_t dev, struct sglist *sg_list)
{
}

int
agp_intel_gtt_map_memory(device_t dev, vm_page_t *pages, u_int num_entries,
    struct sglist **sg_list)
{
	struct agp_i810_softc *sc;
	struct sglist *sg;
	int i;
#if 0
	int error;
	bus_dma_tag_t dmat;
#endif

	if (*sg_list != NULL)
		return (0);
	sc = device_get_softc(dev);
	sg = sglist_alloc(num_entries, M_WAITOK /* XXXKIB */);
	for (i = 0; i < num_entries; i++) {
		sg->sg_segs[i].ss_paddr = VM_PAGE_TO_PHYS(pages[i]);
		sg->sg_segs[i].ss_len = PAGE_SIZE;
	}

#if 0
	error = bus_dma_tag_create(bus_get_dma_tag(dev),
	    1 /* alignment */, 0 /* boundary */,
	    1ULL << sc->match->busdma_addr_mask_sz /* lowaddr */,
	    BUS_SPACE_MAXADDR /* highaddr */,
            NULL /* filtfunc */, NULL /* filtfuncarg */,
	    BUS_SPACE_MAXADDR /* maxsize */,
	    BUS_SPACE_UNRESTRICTED /* nsegments */,
	    BUS_SPACE_MAXADDR /* maxsegsz */,
	    0 /* flags */, NULL /* lockfunc */, NULL /* lockfuncarg */,
	    &dmat);
	if (error != 0) {
		sglist_free(sg);
		return (error);
	}
	/* XXXKIB */
#endif
	*sg_list = sg;
	return (0);
}

static void
agp_intel_gtt_install_pte(device_t dev, u_int index, vm_paddr_t addr,
    u_int flags)
{
	struct agp_i810_softc *sc;

	sc = device_get_softc(dev);
	sc->match->driver->install_gtt_pte(dev, index, addr, flags);
}

void
agp_intel_gtt_insert_sg_entries(device_t dev, struct sglist *sg_list,
    u_int first_entry, u_int flags)
{
	struct agp_i810_softc *sc;
	vm_paddr_t spaddr;
	size_t slen;
	u_int i, j;

	sc = device_get_softc(dev);
	for (i = j = 0; j < sg_list->sg_nseg; j++) {
		spaddr = sg_list->sg_segs[i].ss_paddr;
		slen = sg_list->sg_segs[i].ss_len;
		for (; slen > 0; i++) {
			sc->match->driver->install_gtt_pte(dev, first_entry + i,
			    spaddr, flags);
			spaddr += AGP_PAGE_SIZE;
			slen -= AGP_PAGE_SIZE;
		}
	}
	sc->match->driver->read_gtt_pte(dev, first_entry + i - 1);
}

void
intel_gtt_clear_range(u_int first_entry, u_int num_entries)
{

	agp_intel_gtt_clear_range(intel_agp, first_entry, num_entries);
}

void
intel_gtt_insert_pages(u_int first_entry, u_int num_entries, vm_page_t *pages,
    u_int flags)
{

	agp_intel_gtt_insert_pages(intel_agp, first_entry, num_entries,
	    pages, flags);
}

struct intel_gtt *
intel_gtt_get(void)
{

	intel_private.base = agp_intel_gtt_get(intel_agp);
	return (&intel_private.base);
}

int
intel_gtt_chipset_flush(void)
{

	return (agp_intel_gtt_chipset_flush(intel_agp));
}

void
intel_gtt_unmap_memory(struct sglist *sg_list)
{

	agp_intel_gtt_unmap_memory(intel_agp, sg_list);
}

int
intel_gtt_map_memory(vm_page_t *pages, u_int num_entries,
    struct sglist **sg_list)
{

	return (agp_intel_gtt_map_memory(intel_agp, pages, num_entries,
	    sg_list));
}

void
intel_gtt_insert_sg_entries(struct sglist *sg_list, u_int first_entry,
    u_int flags)
{

	agp_intel_gtt_insert_sg_entries(intel_agp, sg_list, first_entry, flags);
}

void
intel_gtt_install_pte(u_int index, vm_paddr_t addr, u_int flags)
{

	agp_intel_gtt_install_pte(intel_agp, index, addr, flags);
}

device_t
intel_gtt_get_bridge_device(void)
{
	struct agp_i810_softc *sc;

	sc = device_get_softc(intel_agp);
	return (sc->bdev);
}

vm_paddr_t
intel_gtt_read_pte_paddr(u_int entry)
{
	struct agp_i810_softc *sc;

	sc = device_get_softc(intel_agp);
	return (sc->match->driver->read_gtt_pte_paddr(intel_agp, entry));
}

u_int32_t
intel_gtt_read_pte(u_int entry)
{
	struct agp_i810_softc *sc;

	sc = device_get_softc(intel_agp);
	return (sc->match->driver->read_gtt_pte(intel_agp, entry));
}

void
intel_gtt_write(u_int entry, uint32_t val)
{
	struct agp_i810_softc *sc;

	sc = device_get_softc(intel_agp);
	return (sc->match->driver->write_gtt(intel_agp, entry, val));
}
