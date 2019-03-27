/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013, 2015 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Konstantin Belousov <kib@FreeBSD.org>
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/memdesc.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/rwlock.h>
#include <sys/smp.h>
#include <sys/taskqueue.h>
#include <sys/tree.h>
#include <sys/vmem.h>
#include <machine/bus.h>
#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>
#include <dev/acpica/acpivar.h>
#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/vm_map.h>
#include <x86/include/busdma_impl.h>
#include <x86/iommu/intel_reg.h>
#include <x86/iommu/busdma_dmar.h>
#include <x86/iommu/intel_dmar.h>
#include <dev/pci/pcivar.h>

typedef void (*dmar_quirk_cpu_fun)(struct dmar_unit *);

struct intel_dmar_quirk_cpu {
	u_int ext_family;
	u_int ext_model;
	u_int family_code;
	u_int model;
	u_int stepping;
	dmar_quirk_cpu_fun quirk;
	const char *descr;
};

typedef void (*dmar_quirk_nb_fun)(struct dmar_unit *, device_t nb);

struct intel_dmar_quirk_nb {
	u_int dev_id;
	u_int rev_no;
	dmar_quirk_nb_fun quirk;
	const char *descr;
};

#define	QUIRK_NB_ALL_REV	0xffffffff

static void
dmar_match_quirks(struct dmar_unit *dmar,
    const struct intel_dmar_quirk_nb *nb_quirks, int nb_quirks_len,
    const struct intel_dmar_quirk_cpu *cpu_quirks, int cpu_quirks_len)
{
	device_t nb;
	const struct intel_dmar_quirk_nb *nb_quirk;
	const struct intel_dmar_quirk_cpu *cpu_quirk;
	u_int p[4];
	u_int dev_id, rev_no;
	u_int ext_family, ext_model, family_code, model, stepping;
	int i;

	if (nb_quirks != NULL) {
		nb = pci_find_bsf(0, 0, 0);
		if (nb != NULL) {
			dev_id = pci_get_device(nb);
			rev_no = pci_get_revid(nb);
			for (i = 0; i < nb_quirks_len; i++) {
				nb_quirk = &nb_quirks[i];
				if (nb_quirk->dev_id == dev_id &&
				    (nb_quirk->rev_no == rev_no ||
				    nb_quirk->rev_no == QUIRK_NB_ALL_REV)) {
					if (bootverbose) {
						device_printf(dmar->dev,
						    "NB IOMMU quirk %s\n",
						    nb_quirk->descr);
					}
					nb_quirk->quirk(dmar, nb);
				}
			}
		} else {
			device_printf(dmar->dev, "cannot find northbridge\n");
		}
	}
	if (cpu_quirks != NULL) {
		do_cpuid(1, p);
		ext_family = (p[0] & CPUID_EXT_FAMILY) >> 20;
		ext_model = (p[0] & CPUID_EXT_MODEL) >> 16;
		family_code = (p[0] & CPUID_FAMILY) >> 8;
		model = (p[0] & CPUID_MODEL) >> 4;
		stepping = p[0] & CPUID_STEPPING;
		for (i = 0; i < cpu_quirks_len; i++) {
			cpu_quirk = &cpu_quirks[i];
			if (cpu_quirk->ext_family == ext_family &&
			    cpu_quirk->ext_model == ext_model &&
			    cpu_quirk->family_code == family_code &&
			    cpu_quirk->model == model &&
			    (cpu_quirk->stepping == -1 ||
			    cpu_quirk->stepping == stepping)) {
				if (bootverbose) {
					device_printf(dmar->dev,
					    "CPU IOMMU quirk %s\n",
					    cpu_quirk->descr);
				}
				cpu_quirk->quirk(dmar);
			}
		}
	}
}

static void
nb_5400_no_low_high_prot_mem(struct dmar_unit *unit, device_t nb __unused)
{

	unit->hw_cap &= ~(DMAR_CAP_PHMR | DMAR_CAP_PLMR);
}

static void
nb_no_ir(struct dmar_unit *unit, device_t nb __unused)
{

	unit->hw_ecap &= ~(DMAR_ECAP_IR | DMAR_ECAP_EIM);
}

static void
nb_5500_no_ir_rev13(struct dmar_unit *unit, device_t nb)
{
	u_int rev_no;

	rev_no = pci_get_revid(nb);
	if (rev_no <= 0x13)
		nb_no_ir(unit, nb);
}

static const struct intel_dmar_quirk_nb pre_use_nb[] = {
	{
	    .dev_id = 0x4001, .rev_no = 0x20,
	    .quirk = nb_5400_no_low_high_prot_mem,
	    .descr = "5400 E23" /* no low/high protected memory */
	},
	{
	    .dev_id = 0x4003, .rev_no = 0x20,
	    .quirk = nb_5400_no_low_high_prot_mem,
	    .descr = "5400 E23" /* no low/high protected memory */
	},
	{
	    .dev_id = 0x3403, .rev_no = QUIRK_NB_ALL_REV,
	    .quirk = nb_5500_no_ir_rev13,
	    .descr = "5500 E47, E53" /* interrupt remapping does not work */
	},
	{
	    .dev_id = 0x3405, .rev_no = QUIRK_NB_ALL_REV,
	    .quirk = nb_5500_no_ir_rev13,
	    .descr = "5500 E47, E53" /* interrupt remapping does not work */
	},
	{
	    .dev_id = 0x3405, .rev_no = 0x22,
	    .quirk = nb_no_ir,
	    .descr = "5500 E47, E53" /* interrupt remapping does not work */
	},
	{
	    .dev_id = 0x3406, .rev_no = QUIRK_NB_ALL_REV,
	    .quirk = nb_5500_no_ir_rev13,
	    .descr = "5500 E47, E53" /* interrupt remapping does not work */
	},
};

static void
cpu_e5_am9(struct dmar_unit *unit)
{

	unit->hw_cap &= ~(0x3fULL << 48);
	unit->hw_cap |= (9ULL << 48);
}

static const struct intel_dmar_quirk_cpu post_ident_cpu[] = {
	{
	    .ext_family = 0, .ext_model = 2, .family_code = 6, .model = 13,
	    .stepping = 6, .quirk = cpu_e5_am9,
	    .descr = "E5 BT176" /* AM should be at most 9 */
	},
};

void
dmar_quirks_pre_use(struct dmar_unit *dmar)
{

	if (!dmar_barrier_enter(dmar, DMAR_BARRIER_USEQ))
		return;
	DMAR_LOCK(dmar);
	dmar_match_quirks(dmar, pre_use_nb, nitems(pre_use_nb),
	    NULL, 0);
	dmar_barrier_exit(dmar, DMAR_BARRIER_USEQ);
}

void
dmar_quirks_post_ident(struct dmar_unit *dmar)
{

	dmar_match_quirks(dmar, NULL, 0, post_ident_cpu,
	    nitems(post_ident_cpu));
}
