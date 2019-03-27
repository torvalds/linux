/*-
 * Copyright (c) 2015 The FreeBSD Foundation
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
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/memdesc.h>
#include <sys/rman.h>
#include <sys/rwlock.h>
#include <sys/taskqueue.h>
#include <sys/tree.h>
#include <sys/vmem.h>
#include <machine/bus.h>
#include <machine/intr_machdep.h>
#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <x86/include/apicreg.h>
#include <x86/include/apicvar.h>
#include <x86/include/busdma_impl.h>
#include <x86/iommu/intel_reg.h>
#include <x86/iommu/busdma_dmar.h>
#include <x86/iommu/intel_dmar.h>
#include <dev/pci/pcivar.h>
#include <x86/iommu/iommu_intrmap.h>

static struct dmar_unit *dmar_ir_find(device_t src, uint16_t *rid,
    int *is_dmar);
static void dmar_ir_program_irte(struct dmar_unit *unit, u_int idx,
    uint64_t low, uint16_t rid);
static int dmar_ir_free_irte(struct dmar_unit *unit, u_int cookie);

int
iommu_alloc_msi_intr(device_t src, u_int *cookies, u_int count)
{
	struct dmar_unit *unit;
	vmem_addr_t vmem_res;
	u_int idx, i;
	int error;

	unit = dmar_ir_find(src, NULL, NULL);
	if (unit == NULL || !unit->ir_enabled) {
		for (i = 0; i < count; i++)
			cookies[i] = -1;
		return (EOPNOTSUPP);
	}

	error = vmem_alloc(unit->irtids, count, M_FIRSTFIT | M_NOWAIT,
	    &vmem_res);
	if (error != 0) {
		KASSERT(error != EOPNOTSUPP,
		    ("impossible EOPNOTSUPP from vmem"));
		return (error);
	}
	idx = vmem_res;
	for (i = 0; i < count; i++)
		cookies[i] = idx + i;
	return (0);
}

int
iommu_map_msi_intr(device_t src, u_int cpu, u_int vector, u_int cookie,
    uint64_t *addr, uint32_t *data)
{
	struct dmar_unit *unit;
	uint64_t low;
	uint16_t rid;
	int is_dmar;

	unit = dmar_ir_find(src, &rid, &is_dmar);
	if (is_dmar) {
		KASSERT(unit == NULL, ("DMAR cannot translate itself"));

		/*
		 * See VT-d specification, 5.1.6 Remapping Hardware -
		 * Interrupt Programming.
		 */
		*data = vector;
		*addr = MSI_INTEL_ADDR_BASE | ((cpu & 0xff) << 12);
		if (x2apic_mode)
			*addr |= ((uint64_t)cpu & 0xffffff00) << 32;
		else
			KASSERT(cpu <= 0xff, ("cpu id too big %d", cpu));
		return (0);
	}
	if (unit == NULL || !unit->ir_enabled || cookie == -1)
		return (EOPNOTSUPP);

	low = (DMAR_X2APIC(unit) ? DMAR_IRTE1_DST_x2APIC(cpu) :
	    DMAR_IRTE1_DST_xAPIC(cpu)) | DMAR_IRTE1_V(vector) |
	    DMAR_IRTE1_DLM_FM | DMAR_IRTE1_TM_EDGE | DMAR_IRTE1_RH_DIRECT |
	    DMAR_IRTE1_DM_PHYSICAL | DMAR_IRTE1_P;
	dmar_ir_program_irte(unit, cookie, low, rid);

	if (addr != NULL) {
		/*
		 * See VT-d specification, 5.1.5.2 MSI and MSI-X
		 * Register Programming.
		 */
		*addr = MSI_INTEL_ADDR_BASE | ((cookie & 0x7fff) << 5) |
		    ((cookie & 0x8000) << 2) | 0x18;
		*data = 0;
	}
	return (0);
}

int
iommu_unmap_msi_intr(device_t src, u_int cookie)
{
	struct dmar_unit *unit;

	if (cookie == -1)
		return (0);
	unit = dmar_ir_find(src, NULL, NULL);
	return (dmar_ir_free_irte(unit, cookie));
}

int
iommu_map_ioapic_intr(u_int ioapic_id, u_int cpu, u_int vector, bool edge,
    bool activehi, int irq, u_int *cookie, uint32_t *hi, uint32_t *lo)
{
	struct dmar_unit *unit;
	vmem_addr_t vmem_res;
	uint64_t low, iorte;
	u_int idx;
	int error;
	uint16_t rid;

	unit = dmar_find_ioapic(ioapic_id, &rid);
	if (unit == NULL || !unit->ir_enabled) {
		*cookie = -1;
		return (EOPNOTSUPP);
	}

	error = vmem_alloc(unit->irtids, 1, M_FIRSTFIT | M_NOWAIT, &vmem_res);
	if (error != 0) {
		KASSERT(error != EOPNOTSUPP,
		    ("impossible EOPNOTSUPP from vmem"));
		return (error);
	}
	idx = vmem_res;
	low = 0;
	switch (irq) {
	case IRQ_EXTINT:
		low |= DMAR_IRTE1_DLM_ExtINT;
		break;
	case IRQ_NMI:
		low |= DMAR_IRTE1_DLM_NMI;
		break;
	case IRQ_SMI:
		low |= DMAR_IRTE1_DLM_SMI;
		break;
	default:
		KASSERT(vector != 0, ("No vector for IRQ %u", irq));
		low |= DMAR_IRTE1_DLM_FM | DMAR_IRTE1_V(vector);
		break;
	}
	low |= (DMAR_X2APIC(unit) ? DMAR_IRTE1_DST_x2APIC(cpu) :
	    DMAR_IRTE1_DST_xAPIC(cpu)) |
	    (edge ? DMAR_IRTE1_TM_EDGE : DMAR_IRTE1_TM_LEVEL) |
	    DMAR_IRTE1_RH_DIRECT | DMAR_IRTE1_DM_PHYSICAL | DMAR_IRTE1_P;
	dmar_ir_program_irte(unit, idx, low, rid);

	if (hi != NULL) {
		/*
		 * See VT-d specification, 5.1.5.1 I/OxAPIC
		 * Programming.
		 */
		iorte = (1ULL << 48) | ((uint64_t)(idx & 0x7fff) << 49) |
		    ((idx & 0x8000) != 0 ? (1 << 11) : 0) |
		    (edge ? IOART_TRGREDG : IOART_TRGRLVL) |
		    (activehi ? IOART_INTAHI : IOART_INTALO) |
		    IOART_DELFIXED | vector;
		*hi = iorte >> 32;
		*lo = iorte;
	}
	*cookie = idx;
	return (0);
}

int
iommu_unmap_ioapic_intr(u_int ioapic_id, u_int *cookie)
{
	struct dmar_unit *unit;
	u_int idx;

	idx = *cookie;
	if (idx == -1)
		return (0);
	*cookie = -1;
	unit = dmar_find_ioapic(ioapic_id, NULL);
	KASSERT(unit != NULL && unit->ir_enabled,
	    ("unmap: cookie %d unit %p", idx, unit));
	return (dmar_ir_free_irte(unit, idx));
}

static struct dmar_unit *
dmar_ir_find(device_t src, uint16_t *rid, int *is_dmar)
{
	devclass_t src_class;
	struct dmar_unit *unit;

	/*
	 * We need to determine if the interrupt source generates FSB
	 * interrupts.  If yes, it is either DMAR, in which case
	 * interrupts are not remapped.  Or it is HPET, and interrupts
	 * are remapped.  For HPET, source id is reported by HPET
	 * record in DMAR ACPI table.
	 */
	if (is_dmar != NULL)
		*is_dmar = FALSE;
	src_class = device_get_devclass(src);
	if (src_class == devclass_find("dmar")) {
		unit = NULL;
		if (is_dmar != NULL)
			*is_dmar = TRUE;
	} else if (src_class == devclass_find("hpet")) {
		unit = dmar_find_hpet(src, rid);
	} else {
		unit = dmar_find(src);
		if (unit != NULL && rid != NULL)
			dmar_get_requester(src, rid);
	}
	return (unit);
}

static void
dmar_ir_program_irte(struct dmar_unit *unit, u_int idx, uint64_t low,
    uint16_t rid)
{
	dmar_irte_t *irte;
	uint64_t high;

	KASSERT(idx < unit->irte_cnt,
	    ("bad cookie %d %d", idx, unit->irte_cnt));
	irte = &(unit->irt[idx]);
	high = DMAR_IRTE2_SVT_RID | DMAR_IRTE2_SQ_RID |
	    DMAR_IRTE2_SID_RID(rid);
	device_printf(unit->dev,
	    "programming irte[%d] rid %#x high %#jx low %#jx\n",
	    idx, rid, (uintmax_t)high, (uintmax_t)low);
	DMAR_LOCK(unit);
	if ((irte->irte1 & DMAR_IRTE1_P) != 0) {
		/*
		 * The rte is already valid.  Assume that the request
		 * is to remap the interrupt for balancing.  Only low
		 * word of rte needs to be changed.  Assert that the
		 * high word contains expected value.
		 */
		KASSERT(irte->irte2 == high,
		    ("irte2 mismatch, %jx %jx", (uintmax_t)irte->irte2,
		    (uintmax_t)high));
		dmar_pte_update(&irte->irte1, low);
	} else {
		dmar_pte_store(&irte->irte2, high);
		dmar_pte_store(&irte->irte1, low);
	}
	dmar_qi_invalidate_iec(unit, idx, 1);
	DMAR_UNLOCK(unit);

}

static int
dmar_ir_free_irte(struct dmar_unit *unit, u_int cookie)
{
	dmar_irte_t *irte;

	KASSERT(unit != NULL && unit->ir_enabled,
	    ("unmap: cookie %d unit %p", cookie, unit));
	KASSERT(cookie < unit->irte_cnt,
	    ("bad cookie %u %u", cookie, unit->irte_cnt));
	irte = &(unit->irt[cookie]);
	dmar_pte_clear(&irte->irte1);
	dmar_pte_clear(&irte->irte2);
	DMAR_LOCK(unit);
	dmar_qi_invalidate_iec(unit, cookie, 1);
	DMAR_UNLOCK(unit);
	vmem_free(unit->irtids, cookie, 1);
	return (0);
}

static u_int
clp2(u_int v)
{

	return (powerof2(v) ? v : 1 << fls(v));
}

int
dmar_init_irt(struct dmar_unit *unit)
{

	if ((unit->hw_ecap & DMAR_ECAP_IR) == 0)
		return (0);
	unit->ir_enabled = 1;
	TUNABLE_INT_FETCH("hw.dmar.ir", &unit->ir_enabled);
	if (!unit->ir_enabled)
		return (0);
	if (!unit->qi_enabled) {
		unit->ir_enabled = 0;
		if (bootverbose)
			device_printf(unit->dev,
	     "QI disabled, disabling interrupt remapping\n");
		return (0);
	}
	unit->irte_cnt = clp2(num_io_irqs);
	unit->irt = (dmar_irte_t *)(uintptr_t)kmem_alloc_contig(
	    unit->irte_cnt * sizeof(dmar_irte_t), M_ZERO | M_WAITOK, 0,
	    dmar_high, PAGE_SIZE, 0, DMAR_IS_COHERENT(unit) ?
	    VM_MEMATTR_DEFAULT : VM_MEMATTR_UNCACHEABLE);
	if (unit->irt == NULL)
		return (ENOMEM);
	unit->irt_phys = pmap_kextract((vm_offset_t)unit->irt);
	unit->irtids = vmem_create("dmarirt", 0, unit->irte_cnt, 1, 0,
	    M_FIRSTFIT | M_NOWAIT);
	DMAR_LOCK(unit);
	dmar_load_irt_ptr(unit);
	dmar_qi_invalidate_iec_glob(unit);
	DMAR_UNLOCK(unit);

	/*
	 * Initialize mappings for already configured interrupt pins.
	 * Required, because otherwise the interrupts fault without
	 * irtes.
	 */
	intr_reprogram();

	DMAR_LOCK(unit);
	dmar_enable_ir(unit);
	DMAR_UNLOCK(unit);
	return (0);
}

void
dmar_fini_irt(struct dmar_unit *unit)
{

	unit->ir_enabled = 0;
	if (unit->irt != NULL) {
		dmar_disable_ir(unit);
		dmar_qi_invalidate_iec_glob(unit);
		vmem_destroy(unit->irtids);
		kmem_free((vm_offset_t)unit->irt, unit->irte_cnt *
		    sizeof(dmar_irte_t));
	}
}
