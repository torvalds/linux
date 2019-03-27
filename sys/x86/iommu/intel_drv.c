/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013-2015 The FreeBSD Foundation
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

#include "opt_acpi.h"
#if defined(__amd64__)
#define	DEV_APIC
#else
#include "opt_apic.h"
#endif
#include "opt_ddb.h"

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
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#ifdef DEV_APIC
#include "pcib_if.h"
#include <machine/intr_machdep.h>
#include <x86/apicreg.h>
#include <x86/apicvar.h>
#endif

#define	DMAR_FAULT_IRQ_RID	0
#define	DMAR_QI_IRQ_RID		1
#define	DMAR_REG_RID		2

static devclass_t dmar_devclass;
static device_t *dmar_devs;
static int dmar_devcnt;

typedef int (*dmar_iter_t)(ACPI_DMAR_HEADER *, void *);

static void
dmar_iterate_tbl(dmar_iter_t iter, void *arg)
{
	ACPI_TABLE_DMAR *dmartbl;
	ACPI_DMAR_HEADER *dmarh;
	char *ptr, *ptrend;
	ACPI_STATUS status;

	status = AcpiGetTable(ACPI_SIG_DMAR, 1, (ACPI_TABLE_HEADER **)&dmartbl);
	if (ACPI_FAILURE(status))
		return;
	ptr = (char *)dmartbl + sizeof(*dmartbl);
	ptrend = (char *)dmartbl + dmartbl->Header.Length;
	for (;;) {
		if (ptr >= ptrend)
			break;
		dmarh = (ACPI_DMAR_HEADER *)ptr;
		if (dmarh->Length <= 0) {
			printf("dmar_identify: corrupted DMAR table, l %d\n",
			    dmarh->Length);
			break;
		}
		ptr += dmarh->Length;
		if (!iter(dmarh, arg))
			break;
	}
	AcpiPutTable((ACPI_TABLE_HEADER *)dmartbl);
}

struct find_iter_args {
	int i;
	ACPI_DMAR_HARDWARE_UNIT *res;
};

static int
dmar_find_iter(ACPI_DMAR_HEADER *dmarh, void *arg)
{
	struct find_iter_args *fia;

	if (dmarh->Type != ACPI_DMAR_TYPE_HARDWARE_UNIT)
		return (1);

	fia = arg;
	if (fia->i == 0) {
		fia->res = (ACPI_DMAR_HARDWARE_UNIT *)dmarh;
		return (0);
	}
	fia->i--;
	return (1);
}

static ACPI_DMAR_HARDWARE_UNIT *
dmar_find_by_index(int idx)
{
	struct find_iter_args fia;

	fia.i = idx;
	fia.res = NULL;
	dmar_iterate_tbl(dmar_find_iter, &fia);
	return (fia.res);
}

static int
dmar_count_iter(ACPI_DMAR_HEADER *dmarh, void *arg)
{

	if (dmarh->Type == ACPI_DMAR_TYPE_HARDWARE_UNIT)
		dmar_devcnt++;
	return (1);
}

static int dmar_enable = 0;
static void
dmar_identify(driver_t *driver, device_t parent)
{
	ACPI_TABLE_DMAR *dmartbl;
	ACPI_DMAR_HARDWARE_UNIT *dmarh;
	ACPI_STATUS status;
	int i, error;

	if (acpi_disabled("dmar"))
		return;
	TUNABLE_INT_FETCH("hw.dmar.enable", &dmar_enable);
	if (!dmar_enable)
		return;
#ifdef INVARIANTS
	TUNABLE_INT_FETCH("hw.dmar.check_free", &dmar_check_free);
#endif
	TUNABLE_INT_FETCH("hw.dmar.match_verbose", &dmar_match_verbose);
	status = AcpiGetTable(ACPI_SIG_DMAR, 1, (ACPI_TABLE_HEADER **)&dmartbl);
	if (ACPI_FAILURE(status))
		return;
	haw = dmartbl->Width + 1;
	if ((1ULL << (haw + 1)) > BUS_SPACE_MAXADDR)
		dmar_high = BUS_SPACE_MAXADDR;
	else
		dmar_high = 1ULL << (haw + 1);
	if (bootverbose) {
		printf("DMAR HAW=%d flags=<%b>\n", dmartbl->Width,
		    (unsigned)dmartbl->Flags,
		    "\020\001INTR_REMAP\002X2APIC_OPT_OUT");
	}
	AcpiPutTable((ACPI_TABLE_HEADER *)dmartbl);

	dmar_iterate_tbl(dmar_count_iter, NULL);
	if (dmar_devcnt == 0)
		return;
	dmar_devs = malloc(sizeof(device_t) * dmar_devcnt, M_DEVBUF,
	    M_WAITOK | M_ZERO);
	for (i = 0; i < dmar_devcnt; i++) {
		dmarh = dmar_find_by_index(i);
		if (dmarh == NULL) {
			printf("dmar_identify: cannot find HWUNIT %d\n", i);
			continue;
		}
		dmar_devs[i] = BUS_ADD_CHILD(parent, 1, "dmar", i);
		if (dmar_devs[i] == NULL) {
			printf("dmar_identify: cannot create instance %d\n", i);
			continue;
		}
		error = bus_set_resource(dmar_devs[i], SYS_RES_MEMORY,
		    DMAR_REG_RID, dmarh->Address, PAGE_SIZE);
		if (error != 0) {
			printf(
	"dmar%d: unable to alloc register window at 0x%08jx: error %d\n",
			    i, (uintmax_t)dmarh->Address, error);
			device_delete_child(parent, dmar_devs[i]);
			dmar_devs[i] = NULL;
		}
	}
}

static int
dmar_probe(device_t dev)
{

	if (acpi_get_handle(dev) != NULL)
		return (ENXIO);
	device_set_desc(dev, "DMA remap");
	return (BUS_PROBE_NOWILDCARD);
}

static void
dmar_release_intr(device_t dev, struct dmar_unit *unit, int idx)
{
	struct dmar_msi_data *dmd;

	dmd = &unit->intrs[idx];
	if (dmd->irq == -1)
		return;
	bus_teardown_intr(dev, dmd->irq_res, dmd->intr_handle);
	bus_release_resource(dev, SYS_RES_IRQ, dmd->irq_rid, dmd->irq_res);
	bus_delete_resource(dev, SYS_RES_IRQ, dmd->irq_rid);
	PCIB_RELEASE_MSIX(device_get_parent(device_get_parent(dev)),
	    dev, dmd->irq);
	dmd->irq = -1;
}

static void
dmar_release_resources(device_t dev, struct dmar_unit *unit)
{
	int i;

	dmar_fini_busdma(unit);
	dmar_fini_irt(unit);
	dmar_fini_qi(unit);
	dmar_fini_fault_log(unit);
	for (i = 0; i < DMAR_INTR_TOTAL; i++)
		dmar_release_intr(dev, unit, i);
	if (unit->regs != NULL) {
		bus_deactivate_resource(dev, SYS_RES_MEMORY, unit->reg_rid,
		    unit->regs);
		bus_release_resource(dev, SYS_RES_MEMORY, unit->reg_rid,
		    unit->regs);
		unit->regs = NULL;
	}
	if (unit->domids != NULL) {
		delete_unrhdr(unit->domids);
		unit->domids = NULL;
	}
	if (unit->ctx_obj != NULL) {
		vm_object_deallocate(unit->ctx_obj);
		unit->ctx_obj = NULL;
	}
}

static int
dmar_alloc_irq(device_t dev, struct dmar_unit *unit, int idx)
{
	device_t pcib;
	struct dmar_msi_data *dmd;
	uint64_t msi_addr;
	uint32_t msi_data;
	int error;

	dmd = &unit->intrs[idx];
	pcib = device_get_parent(device_get_parent(dev)); /* Really not pcib */
	error = PCIB_ALLOC_MSIX(pcib, dev, &dmd->irq);
	if (error != 0) {
		device_printf(dev, "cannot allocate %s interrupt, %d\n",
		    dmd->name, error);
		goto err1;
	}
	error = bus_set_resource(dev, SYS_RES_IRQ, dmd->irq_rid,
	    dmd->irq, 1);
	if (error != 0) {
		device_printf(dev, "cannot set %s interrupt resource, %d\n",
		    dmd->name, error);
		goto err2;
	}
	dmd->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
	    &dmd->irq_rid, RF_ACTIVE);
	if (dmd->irq_res == NULL) {
		device_printf(dev,
		    "cannot allocate resource for %s interrupt\n", dmd->name);
		error = ENXIO;
		goto err3;
	}
	error = bus_setup_intr(dev, dmd->irq_res, INTR_TYPE_MISC,
	    dmd->handler, NULL, unit, &dmd->intr_handle);
	if (error != 0) {
		device_printf(dev, "cannot setup %s interrupt, %d\n",
		    dmd->name, error);
		goto err4;
	}
	bus_describe_intr(dev, dmd->irq_res, dmd->intr_handle, "%s", dmd->name);
	error = PCIB_MAP_MSI(pcib, dev, dmd->irq, &msi_addr, &msi_data);
	if (error != 0) {
		device_printf(dev, "cannot map %s interrupt, %d\n",
		    dmd->name, error);
		goto err5;
	}
	dmar_write4(unit, dmd->msi_data_reg, msi_data);
	dmar_write4(unit, dmd->msi_addr_reg, msi_addr);
	/* Only for xAPIC mode */
	dmar_write4(unit, dmd->msi_uaddr_reg, msi_addr >> 32);
	return (0);

err5:
	bus_teardown_intr(dev, dmd->irq_res, dmd->intr_handle);
err4:
	bus_release_resource(dev, SYS_RES_IRQ, dmd->irq_rid, dmd->irq_res);
err3:
	bus_delete_resource(dev, SYS_RES_IRQ, dmd->irq_rid);
err2:
	PCIB_RELEASE_MSIX(pcib, dev, dmd->irq);
	dmd->irq = -1;
err1:
	return (error);
}

#ifdef DEV_APIC
static int
dmar_remap_intr(device_t dev, device_t child, u_int irq)
{
	struct dmar_unit *unit;
	struct dmar_msi_data *dmd;
	uint64_t msi_addr;
	uint32_t msi_data;
	int i, error;

	unit = device_get_softc(dev);
	for (i = 0; i < DMAR_INTR_TOTAL; i++) {
		dmd = &unit->intrs[i];
		if (irq == dmd->irq) {
			error = PCIB_MAP_MSI(device_get_parent(
			    device_get_parent(dev)),
			    dev, irq, &msi_addr, &msi_data);
			if (error != 0)
				return (error);
			DMAR_LOCK(unit);
			(dmd->disable_intr)(unit);
			dmar_write4(unit, dmd->msi_data_reg, msi_data);
			dmar_write4(unit, dmd->msi_addr_reg, msi_addr);
			dmar_write4(unit, dmd->msi_uaddr_reg, msi_addr >> 32);
			(dmd->enable_intr)(unit);
			DMAR_UNLOCK(unit);
			return (0);
		}
	}
	return (ENOENT);
}
#endif

static void
dmar_print_caps(device_t dev, struct dmar_unit *unit,
    ACPI_DMAR_HARDWARE_UNIT *dmaru)
{
	uint32_t caphi, ecaphi;

	device_printf(dev, "regs@0x%08jx, ver=%d.%d, seg=%d, flags=<%b>\n",
	    (uintmax_t)dmaru->Address, DMAR_MAJOR_VER(unit->hw_ver),
	    DMAR_MINOR_VER(unit->hw_ver), dmaru->Segment,
	    dmaru->Flags, "\020\001INCLUDE_ALL_PCI");
	caphi = unit->hw_cap >> 32;
	device_printf(dev, "cap=%b,", (u_int)unit->hw_cap,
	    "\020\004AFL\005WBF\006PLMR\007PHMR\010CM\027ZLR\030ISOCH");
	printf("%b, ", caphi, "\020\010PSI\027DWD\030DRD\031FL1GP\034PSI");
	printf("ndoms=%d, sagaw=%d, mgaw=%d, fro=%d, nfr=%d, superp=%d",
	    DMAR_CAP_ND(unit->hw_cap), DMAR_CAP_SAGAW(unit->hw_cap),
	    DMAR_CAP_MGAW(unit->hw_cap), DMAR_CAP_FRO(unit->hw_cap),
	    DMAR_CAP_NFR(unit->hw_cap), DMAR_CAP_SPS(unit->hw_cap));
	if ((unit->hw_cap & DMAR_CAP_PSI) != 0)
		printf(", mamv=%d", DMAR_CAP_MAMV(unit->hw_cap));
	printf("\n");
	ecaphi = unit->hw_ecap >> 32;
	device_printf(dev, "ecap=%b,", (u_int)unit->hw_ecap,
	    "\020\001C\002QI\003DI\004IR\005EIM\007PT\010SC\031ECS\032MTS"
	    "\033NEST\034DIS\035PASID\036PRS\037ERS\040SRS");
	printf("%b, ", ecaphi, "\020\002NWFS\003EAFS");
	printf("mhmw=%d, iro=%d\n", DMAR_ECAP_MHMV(unit->hw_ecap),
	    DMAR_ECAP_IRO(unit->hw_ecap));
}

static int
dmar_attach(device_t dev)
{
	struct dmar_unit *unit;
	ACPI_DMAR_HARDWARE_UNIT *dmaru;
	uint64_t timeout;
	int i, error;

	unit = device_get_softc(dev);
	unit->dev = dev;
	unit->unit = device_get_unit(dev);
	dmaru = dmar_find_by_index(unit->unit);
	if (dmaru == NULL)
		return (EINVAL);
	unit->segment = dmaru->Segment;
	unit->base = dmaru->Address;
	unit->reg_rid = DMAR_REG_RID;
	unit->regs = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &unit->reg_rid, RF_ACTIVE);
	if (unit->regs == NULL) {
		device_printf(dev, "cannot allocate register window\n");
		return (ENOMEM);
	}
	unit->hw_ver = dmar_read4(unit, DMAR_VER_REG);
	unit->hw_cap = dmar_read8(unit, DMAR_CAP_REG);
	unit->hw_ecap = dmar_read8(unit, DMAR_ECAP_REG);
	if (bootverbose)
		dmar_print_caps(dev, unit, dmaru);
	dmar_quirks_post_ident(unit);

	timeout = dmar_get_timeout();
	TUNABLE_UINT64_FETCH("hw.dmar.timeout", &timeout);
	dmar_update_timeout(timeout);

	for (i = 0; i < DMAR_INTR_TOTAL; i++)
		unit->intrs[i].irq = -1;

	unit->intrs[DMAR_INTR_FAULT].name = "fault";
	unit->intrs[DMAR_INTR_FAULT].irq_rid = DMAR_FAULT_IRQ_RID;
	unit->intrs[DMAR_INTR_FAULT].handler = dmar_fault_intr;
	unit->intrs[DMAR_INTR_FAULT].msi_data_reg = DMAR_FEDATA_REG;
	unit->intrs[DMAR_INTR_FAULT].msi_addr_reg = DMAR_FEADDR_REG;
	unit->intrs[DMAR_INTR_FAULT].msi_uaddr_reg = DMAR_FEUADDR_REG;
	unit->intrs[DMAR_INTR_FAULT].enable_intr = dmar_enable_fault_intr;
	unit->intrs[DMAR_INTR_FAULT].disable_intr = dmar_disable_fault_intr;
	error = dmar_alloc_irq(dev, unit, DMAR_INTR_FAULT);
	if (error != 0) {
		dmar_release_resources(dev, unit);
		return (error);
	}
	if (DMAR_HAS_QI(unit)) {
		unit->intrs[DMAR_INTR_QI].name = "qi";
		unit->intrs[DMAR_INTR_QI].irq_rid = DMAR_QI_IRQ_RID;
		unit->intrs[DMAR_INTR_QI].handler = dmar_qi_intr;
		unit->intrs[DMAR_INTR_QI].msi_data_reg = DMAR_IEDATA_REG;
		unit->intrs[DMAR_INTR_QI].msi_addr_reg = DMAR_IEADDR_REG;
		unit->intrs[DMAR_INTR_QI].msi_uaddr_reg = DMAR_IEUADDR_REG;
		unit->intrs[DMAR_INTR_QI].enable_intr = dmar_enable_qi_intr;
		unit->intrs[DMAR_INTR_QI].disable_intr = dmar_disable_qi_intr;
		error = dmar_alloc_irq(dev, unit, DMAR_INTR_QI);
		if (error != 0) {
			dmar_release_resources(dev, unit);
			return (error);
		}
	}

	mtx_init(&unit->lock, "dmarhw", NULL, MTX_DEF);
	unit->domids = new_unrhdr(0, dmar_nd2mask(DMAR_CAP_ND(unit->hw_cap)),
	    &unit->lock);
	LIST_INIT(&unit->domains);

	/*
	 * 9.2 "Context Entry":
	 * When Caching Mode (CM) field is reported as Set, the
	 * domain-id value of zero is architecturally reserved.
	 * Software must not use domain-id value of zero
	 * when CM is Set.
	 */
	if ((unit->hw_cap & DMAR_CAP_CM) != 0)
		alloc_unr_specific(unit->domids, 0);

	unit->ctx_obj = vm_pager_allocate(OBJT_PHYS, NULL, IDX_TO_OFF(1 +
	    DMAR_CTX_CNT), 0, 0, NULL);

	/*
	 * Allocate and load the root entry table pointer.  Enable the
	 * address translation after the required invalidations are
	 * done.
	 */
	dmar_pgalloc(unit->ctx_obj, 0, DMAR_PGF_WAITOK | DMAR_PGF_ZERO);
	DMAR_LOCK(unit);
	error = dmar_load_root_entry_ptr(unit);
	if (error != 0) {
		DMAR_UNLOCK(unit);
		dmar_release_resources(dev, unit);
		return (error);
	}
	error = dmar_inv_ctx_glob(unit);
	if (error != 0) {
		DMAR_UNLOCK(unit);
		dmar_release_resources(dev, unit);
		return (error);
	}
	if ((unit->hw_ecap & DMAR_ECAP_DI) != 0) {
		error = dmar_inv_iotlb_glob(unit);
		if (error != 0) {
			DMAR_UNLOCK(unit);
			dmar_release_resources(dev, unit);
			return (error);
		}
	}

	DMAR_UNLOCK(unit);
	error = dmar_init_fault_log(unit);
	if (error != 0) {
		dmar_release_resources(dev, unit);
		return (error);
	}
	error = dmar_init_qi(unit);
	if (error != 0) {
		dmar_release_resources(dev, unit);
		return (error);
	}
	error = dmar_init_irt(unit);
	if (error != 0) {
		dmar_release_resources(dev, unit);
		return (error);
	}
	error = dmar_init_busdma(unit);
	if (error != 0) {
		dmar_release_resources(dev, unit);
		return (error);
	}

#ifdef NOTYET
	DMAR_LOCK(unit);
	error = dmar_enable_translation(unit);
	if (error != 0) {
		DMAR_UNLOCK(unit);
		dmar_release_resources(dev, unit);
		return (error);
	}
	DMAR_UNLOCK(unit);
#endif

	return (0);
}

static int
dmar_detach(device_t dev)
{

	return (EBUSY);
}

static int
dmar_suspend(device_t dev)
{

	return (0);
}

static int
dmar_resume(device_t dev)
{

	/* XXXKIB */
	return (0);
}

static device_method_t dmar_methods[] = {
	DEVMETHOD(device_identify, dmar_identify),
	DEVMETHOD(device_probe, dmar_probe),
	DEVMETHOD(device_attach, dmar_attach),
	DEVMETHOD(device_detach, dmar_detach),
	DEVMETHOD(device_suspend, dmar_suspend),
	DEVMETHOD(device_resume, dmar_resume),
#ifdef DEV_APIC
	DEVMETHOD(bus_remap_intr, dmar_remap_intr),
#endif
	DEVMETHOD_END
};

static driver_t	dmar_driver = {
	"dmar",
	dmar_methods,
	sizeof(struct dmar_unit),
};

DRIVER_MODULE(dmar, acpi, dmar_driver, dmar_devclass, 0, 0);
MODULE_DEPEND(dmar, acpi, 1, 1, 1);

static void
dmar_print_path(device_t dev, const char *banner, int busno, int depth,
    const ACPI_DMAR_PCI_PATH *path)
{
	int i;

	device_printf(dev, "%s [%d, ", banner, busno);
	for (i = 0; i < depth; i++) {
		if (i != 0)
			printf(", ");
		printf("(%d, %d)", path[i].Device, path[i].Function);
	}
	printf("]\n");
}

static int
dmar_dev_depth(device_t child)
{
	devclass_t pci_class;
	device_t bus, pcib;
	int depth;

	pci_class = devclass_find("pci");
	for (depth = 1; ; depth++) {
		bus = device_get_parent(child);
		pcib = device_get_parent(bus);
		if (device_get_devclass(device_get_parent(pcib)) !=
		    pci_class)
			return (depth);
		child = pcib;
	}
}

static void
dmar_dev_path(device_t child, int *busno, ACPI_DMAR_PCI_PATH *path, int depth)
{
	devclass_t pci_class;
	device_t bus, pcib;

	pci_class = devclass_find("pci");
	for (depth--; depth != -1; depth--) {
		path[depth].Device = pci_get_slot(child);
		path[depth].Function = pci_get_function(child);
		bus = device_get_parent(child);
		pcib = device_get_parent(bus);
		if (device_get_devclass(device_get_parent(pcib)) !=
		    pci_class) {
			/* reached a host bridge */
			*busno = pcib_get_bus(bus);
			return;
		}
		child = pcib;
	}
	panic("wrong depth");
}

static int
dmar_match_pathes(int busno1, const ACPI_DMAR_PCI_PATH *path1, int depth1,
    int busno2, const ACPI_DMAR_PCI_PATH *path2, int depth2,
    enum AcpiDmarScopeType scope_type)
{
	int i, depth;

	if (busno1 != busno2)
		return (0);
	if (scope_type == ACPI_DMAR_SCOPE_TYPE_ENDPOINT && depth1 != depth2)
		return (0);
	depth = depth1;
	if (depth2 < depth)
		depth = depth2;
	for (i = 0; i < depth; i++) {
		if (path1[i].Device != path2[i].Device ||
		    path1[i].Function != path2[i].Function)
			return (0);
	}
	return (1);
}

static int
dmar_match_devscope(ACPI_DMAR_DEVICE_SCOPE *devscope, device_t dev,
    int dev_busno, const ACPI_DMAR_PCI_PATH *dev_path, int dev_path_len)
{
	ACPI_DMAR_PCI_PATH *path;
	int path_len;

	if (devscope->Length < sizeof(*devscope)) {
		printf("dmar_find: corrupted DMAR table, dl %d\n",
		    devscope->Length);
		return (-1);
	}
	if (devscope->EntryType != ACPI_DMAR_SCOPE_TYPE_ENDPOINT &&
	    devscope->EntryType != ACPI_DMAR_SCOPE_TYPE_BRIDGE)
		return (0);
	path_len = devscope->Length - sizeof(*devscope);
	if (path_len % 2 != 0) {
		printf("dmar_find_bsf: corrupted DMAR table, dl %d\n",
		    devscope->Length);
		return (-1);
	}
	path_len /= 2;
	path = (ACPI_DMAR_PCI_PATH *)(devscope + 1);
	if (path_len == 0) {
		printf("dmar_find: corrupted DMAR table, dl %d\n",
		    devscope->Length);
		return (-1);
	}
	if (dmar_match_verbose)
		dmar_print_path(dev, "DMAR", devscope->Bus, path_len, path);

	return (dmar_match_pathes(devscope->Bus, path, path_len, dev_busno,
	    dev_path, dev_path_len, devscope->EntryType));
}

struct dmar_unit *
dmar_find(device_t dev)
{
	device_t dmar_dev;
	ACPI_DMAR_HARDWARE_UNIT *dmarh;
	ACPI_DMAR_DEVICE_SCOPE *devscope;
	char *ptr, *ptrend;
	int i, match, dev_domain, dev_busno, dev_path_len;

	dmar_dev = NULL;
	dev_domain = pci_get_domain(dev);
	dev_path_len = dmar_dev_depth(dev);
	ACPI_DMAR_PCI_PATH dev_path[dev_path_len];
	dmar_dev_path(dev, &dev_busno, dev_path, dev_path_len);
	if (dmar_match_verbose)
		dmar_print_path(dev, "PCI", dev_busno, dev_path_len, dev_path);

	for (i = 0; i < dmar_devcnt; i++) {
		if (dmar_devs[i] == NULL)
			continue;
		dmarh = dmar_find_by_index(i);
		if (dmarh == NULL)
			continue;
		if (dmarh->Segment != dev_domain)
			continue;
		if ((dmarh->Flags & ACPI_DMAR_INCLUDE_ALL) != 0) {
			dmar_dev = dmar_devs[i];
			if (dmar_match_verbose) {
				device_printf(dev,
				    "pci%d:%d:%d:%d matched dmar%d INCLUDE_ALL\n",
				    dev_domain, pci_get_bus(dev),
				    pci_get_slot(dev),
				    pci_get_function(dev),
				    ((struct dmar_unit *)device_get_softc(
				    dmar_dev))->unit);
			}
			goto found;
		}
		ptr = (char *)dmarh + sizeof(*dmarh);
		ptrend = (char *)dmarh + dmarh->Header.Length;
		for (;;) {
			if (ptr >= ptrend)
				break;
			devscope = (ACPI_DMAR_DEVICE_SCOPE *)ptr;
			ptr += devscope->Length;
			if (dmar_match_verbose) {
				device_printf(dev,
				    "pci%d:%d:%d:%d matching dmar%d\n",
				    dev_domain, pci_get_bus(dev),
				    pci_get_slot(dev),
				    pci_get_function(dev),
				    ((struct dmar_unit *)device_get_softc(
				    dmar_devs[i]))->unit);
			}
			match = dmar_match_devscope(devscope, dev, dev_busno,
			    dev_path, dev_path_len);
			if (dmar_match_verbose) {
				if (match == -1)
					printf("table error\n");
				else if (match == 0)
					printf("not matched\n");
				else
					printf("matched\n");
			}
			if (match == -1)
				return (NULL);
			else if (match == 1) {
				dmar_dev = dmar_devs[i];
				goto found;
			}
		}
	}
	return (NULL);
found:
	return (device_get_softc(dmar_dev));
}

static struct dmar_unit *
dmar_find_nonpci(u_int id, u_int entry_type, uint16_t *rid)
{
	device_t dmar_dev;
	struct dmar_unit *unit;
	ACPI_DMAR_HARDWARE_UNIT *dmarh;
	ACPI_DMAR_DEVICE_SCOPE *devscope;
	ACPI_DMAR_PCI_PATH *path;
	char *ptr, *ptrend;
#ifdef DEV_APIC
	int error;
#endif
	int i;

	for (i = 0; i < dmar_devcnt; i++) {
		dmar_dev = dmar_devs[i];
		if (dmar_dev == NULL)
			continue;
		unit = (struct dmar_unit *)device_get_softc(dmar_dev);
		dmarh = dmar_find_by_index(i);
		if (dmarh == NULL)
			continue;
		ptr = (char *)dmarh + sizeof(*dmarh);
		ptrend = (char *)dmarh + dmarh->Header.Length;
		for (;;) {
			if (ptr >= ptrend)
				break;
			devscope = (ACPI_DMAR_DEVICE_SCOPE *)ptr;
			ptr += devscope->Length;
			if (devscope->EntryType != entry_type)
				continue;
			if (devscope->EnumerationId != id)
				continue;
#ifdef DEV_APIC
			if (entry_type == ACPI_DMAR_SCOPE_TYPE_IOAPIC) {
				error = ioapic_get_rid(id, rid);
				/*
				 * If our IOAPIC has PCI bindings then
				 * use the PCI device rid.
				 */
				if (error == 0)
					return (unit);
			}
#endif
			if (devscope->Length - sizeof(ACPI_DMAR_DEVICE_SCOPE)
			    == 2) {
				if (rid != NULL) {
					path = (ACPI_DMAR_PCI_PATH *)
					    (devscope + 1);
					*rid = PCI_RID(devscope->Bus,
					    path->Device, path->Function);
				}
				return (unit);
			}
			printf(
		           "dmar_find_nonpci: id %d type %d path length != 2\n",
			    id, entry_type);
			break;
		}
	}
	return (NULL);
}


struct dmar_unit *
dmar_find_hpet(device_t dev, uint16_t *rid)
{

	return (dmar_find_nonpci(hpet_get_uid(dev), ACPI_DMAR_SCOPE_TYPE_HPET,
	    rid));
}

struct dmar_unit *
dmar_find_ioapic(u_int apic_id, uint16_t *rid)
{

	return (dmar_find_nonpci(apic_id, ACPI_DMAR_SCOPE_TYPE_IOAPIC, rid));
}

struct rmrr_iter_args {
	struct dmar_domain *domain;
	device_t dev;
	int dev_domain;
	int dev_busno;
	ACPI_DMAR_PCI_PATH *dev_path;
	int dev_path_len;
	struct dmar_map_entries_tailq *rmrr_entries;
};

static int
dmar_rmrr_iter(ACPI_DMAR_HEADER *dmarh, void *arg)
{
	struct rmrr_iter_args *ria;
	ACPI_DMAR_RESERVED_MEMORY *resmem;
	ACPI_DMAR_DEVICE_SCOPE *devscope;
	struct dmar_map_entry *entry;
	char *ptr, *ptrend;
	int match;

	if (dmarh->Type != ACPI_DMAR_TYPE_RESERVED_MEMORY)
		return (1);

	ria = arg;
	resmem = (ACPI_DMAR_RESERVED_MEMORY *)dmarh;
	if (dmar_match_verbose) {
		printf("RMRR [%jx,%jx] segment %d\n",
		    (uintmax_t)resmem->BaseAddress,
		    (uintmax_t)resmem->EndAddress,
		    resmem->Segment);
	}
	if (resmem->Segment != ria->dev_domain)
		return (1);

	ptr = (char *)resmem + sizeof(*resmem);
	ptrend = (char *)resmem + resmem->Header.Length;
	for (;;) {
		if (ptr >= ptrend)
			break;
		devscope = (ACPI_DMAR_DEVICE_SCOPE *)ptr;
		ptr += devscope->Length;
		match = dmar_match_devscope(devscope, ria->dev, ria->dev_busno,
		    ria->dev_path, ria->dev_path_len);
		if (match == 1) {
			if (dmar_match_verbose)
				printf("matched\n");
			entry = dmar_gas_alloc_entry(ria->domain,
			    DMAR_PGF_WAITOK);
			entry->start = resmem->BaseAddress;
			/* The RMRR entry end address is inclusive. */
			entry->end = resmem->EndAddress;
			TAILQ_INSERT_TAIL(ria->rmrr_entries, entry,
			    unroll_link);
		} else if (dmar_match_verbose) {
			printf("not matched, err %d\n", match);
		}
	}

	return (1);
}

void
dmar_dev_parse_rmrr(struct dmar_domain *domain, device_t dev,
    struct dmar_map_entries_tailq *rmrr_entries)
{
	struct rmrr_iter_args ria;

	ria.dev_domain = pci_get_domain(dev);
	ria.dev_path_len = dmar_dev_depth(dev);
	ACPI_DMAR_PCI_PATH dev_path[ria.dev_path_len];
	dmar_dev_path(dev, &ria.dev_busno, dev_path, ria.dev_path_len);

	if (dmar_match_verbose) {
		device_printf(dev, "parsing RMRR entries for ");
		dmar_print_path(dev, "PCI", ria.dev_busno, ria.dev_path_len,
		    dev_path);
	}

	ria.domain = domain;
	ria.dev = dev;
	ria.dev_path = dev_path;
	ria.rmrr_entries = rmrr_entries;
	dmar_iterate_tbl(dmar_rmrr_iter, &ria);
}

struct inst_rmrr_iter_args {
	struct dmar_unit *dmar;
};

static device_t
dmar_path_dev(int segment, int path_len, int busno,
    const ACPI_DMAR_PCI_PATH *path)
{
	devclass_t pci_class;
	device_t bus, pcib, dev;
	int i;

	pci_class = devclass_find("pci");
	dev = NULL;
	for (i = 0; i < path_len; i++, path++) {
		dev = pci_find_dbsf(segment, busno, path->Device,
		    path->Function);
		if (dev == NULL)
			break;
		if (i != path_len - 1) {
			bus = device_get_parent(dev);
			pcib = device_get_parent(bus);
			if (device_get_devclass(device_get_parent(pcib)) !=
			    pci_class)
				return (NULL);
		}
		busno = pcib_get_bus(dev);
	}
	return (dev);
}

static int
dmar_inst_rmrr_iter(ACPI_DMAR_HEADER *dmarh, void *arg)
{
	const ACPI_DMAR_RESERVED_MEMORY *resmem;
	const ACPI_DMAR_DEVICE_SCOPE *devscope;
	struct inst_rmrr_iter_args *iria;
	const char *ptr, *ptrend;
	struct dmar_unit *dev_dmar;
	device_t dev;

	if (dmarh->Type != ACPI_DMAR_TYPE_RESERVED_MEMORY)
		return (1);

	iria = arg;
	resmem = (ACPI_DMAR_RESERVED_MEMORY *)dmarh;
	if (resmem->Segment != iria->dmar->segment)
		return (1);
	if (dmar_match_verbose) {
		printf("dmar%d: RMRR [%jx,%jx]\n", iria->dmar->unit,
		    (uintmax_t)resmem->BaseAddress,
		    (uintmax_t)resmem->EndAddress);
	}

	ptr = (const char *)resmem + sizeof(*resmem);
	ptrend = (const char *)resmem + resmem->Header.Length;
	for (;;) {
		if (ptr >= ptrend)
			break;
		devscope = (const ACPI_DMAR_DEVICE_SCOPE *)ptr;
		ptr += devscope->Length;
		/* XXXKIB bridge */
		if (devscope->EntryType != ACPI_DMAR_SCOPE_TYPE_ENDPOINT)
			continue;
		if (dmar_match_verbose) {
			dmar_print_path(iria->dmar->dev, "RMRR scope",
			    devscope->Bus, (devscope->Length -
			    sizeof(ACPI_DMAR_DEVICE_SCOPE)) / 2,
			    (const ACPI_DMAR_PCI_PATH *)(devscope + 1));
		}
		dev = dmar_path_dev(resmem->Segment, (devscope->Length -
		    sizeof(ACPI_DMAR_DEVICE_SCOPE)) / 2, devscope->Bus,
		    (const ACPI_DMAR_PCI_PATH *)(devscope + 1));
		if (dev == NULL) {
			if (dmar_match_verbose)
				printf("null dev\n");
			continue;
		}
		dev_dmar = dmar_find(dev);
		if (dev_dmar != iria->dmar) {
			if (dmar_match_verbose) {
				printf("dmar%d matched, skipping\n",
				    dev_dmar->unit);
			}
			continue;
		}
		if (dmar_match_verbose)
			printf("matched, instantiating RMRR context\n");
		dmar_instantiate_ctx(iria->dmar, dev, true);
	}

	return (1);

}

/*
 * Pre-create all contexts for the DMAR which have RMRR entries.
 */
int
dmar_instantiate_rmrr_ctxs(struct dmar_unit *dmar)
{
	struct inst_rmrr_iter_args iria;
	int error;

	if (!dmar_barrier_enter(dmar, DMAR_BARRIER_RMRR))
		return (0);

	error = 0;
	iria.dmar = dmar;
	if (dmar_match_verbose)
		printf("dmar%d: instantiating RMRR contexts\n", dmar->unit);
	dmar_iterate_tbl(dmar_inst_rmrr_iter, &iria);
	DMAR_LOCK(dmar);
	if (!LIST_EMPTY(&dmar->domains)) {
		KASSERT((dmar->hw_gcmd & DMAR_GCMD_TE) == 0,
	    ("dmar%d: RMRR not handled but translation is already enabled",
		    dmar->unit));
		error = dmar_enable_translation(dmar);
	}
	dmar_barrier_exit(dmar, DMAR_BARRIER_RMRR);
	return (error);
}

#ifdef DDB
#include <ddb/ddb.h>
#include <ddb/db_lex.h>

static void
dmar_print_domain_entry(const struct dmar_map_entry *entry)
{
	struct dmar_map_entry *l, *r;

	db_printf(
	    "    start %jx end %jx free_after %jx free_down %jx flags %x ",
	    entry->start, entry->end, entry->free_after, entry->free_down,
	    entry->flags);
	db_printf("left ");
	l = RB_LEFT(entry, rb_entry);
	if (l == NULL)
		db_printf("NULL ");
	else
		db_printf("%jx ", l->start);
	db_printf("right ");
	r = RB_RIGHT(entry, rb_entry);
	if (r == NULL)
		db_printf("NULL");
	else
		db_printf("%jx", r->start);
	db_printf("\n");
}

static void
dmar_print_ctx(struct dmar_ctx *ctx)
{

	db_printf(
	    "    @%p pci%d:%d:%d refs %d flags %x loads %lu unloads %lu\n",
	    ctx, pci_get_bus(ctx->ctx_tag.owner),
	    pci_get_slot(ctx->ctx_tag.owner),
	    pci_get_function(ctx->ctx_tag.owner), ctx->refs, ctx->flags,
	    ctx->loads, ctx->unloads);
}

static void
dmar_print_domain(struct dmar_domain *domain, bool show_mappings)
{
	struct dmar_map_entry *entry;
	struct dmar_ctx *ctx;

	db_printf(
	    "  @%p dom %d mgaw %d agaw %d pglvl %d end %jx refs %d\n"
	    "   ctx_cnt %d flags %x pgobj %p map_ents %u\n",
	    domain, domain->domain, domain->mgaw, domain->agaw, domain->pglvl,
	    (uintmax_t)domain->end, domain->refs, domain->ctx_cnt,
	    domain->flags, domain->pgtbl_obj, domain->entries_cnt);
	if (!LIST_EMPTY(&domain->contexts)) {
		db_printf("  Contexts:\n");
		LIST_FOREACH(ctx, &domain->contexts, link)
			dmar_print_ctx(ctx);
	}
	if (!show_mappings)
		return;
	db_printf("    mapped:\n");
	RB_FOREACH(entry, dmar_gas_entries_tree, &domain->rb_root) {
		dmar_print_domain_entry(entry);
		if (db_pager_quit)
			break;
	}
	if (db_pager_quit)
		return;
	db_printf("    unloading:\n");
	TAILQ_FOREACH(entry, &domain->unload_entries, dmamap_link) {
		dmar_print_domain_entry(entry);
		if (db_pager_quit)
			break;
	}
}

DB_FUNC(dmar_domain, db_dmar_print_domain, db_show_table, CS_OWN, NULL)
{
	struct dmar_unit *unit;
	struct dmar_domain *domain;
	struct dmar_ctx *ctx;
	bool show_mappings, valid;
	int pci_domain, bus, device, function, i, t;
	db_expr_t radix;

	valid = false;
	radix = db_radix;
	db_radix = 10;
	t = db_read_token();
	if (t == tSLASH) {
		t = db_read_token();
		if (t != tIDENT) {
			db_printf("Bad modifier\n");
			db_radix = radix;
			db_skip_to_eol();
			return;
		}
		show_mappings = strchr(db_tok_string, 'm') != NULL;
		t = db_read_token();
	} else {
		show_mappings = false;
	}
	if (t == tNUMBER) {
		pci_domain = db_tok_number;
		t = db_read_token();
		if (t == tNUMBER) {
			bus = db_tok_number;
			t = db_read_token();
			if (t == tNUMBER) {
				device = db_tok_number;
				t = db_read_token();
				if (t == tNUMBER) {
					function = db_tok_number;
					valid = true;
				}
			}
		}
	}
			db_radix = radix;
	db_skip_to_eol();
	if (!valid) {
		db_printf("usage: show dmar_domain [/m] "
		    "<domain> <bus> <device> <func>\n");
		return;
	}
	for (i = 0; i < dmar_devcnt; i++) {
		unit = device_get_softc(dmar_devs[i]);
		LIST_FOREACH(domain, &unit->domains, link) {
			LIST_FOREACH(ctx, &domain->contexts, link) {
				if (pci_domain == unit->segment && 
				    bus == pci_get_bus(ctx->ctx_tag.owner) &&
				    device ==
				    pci_get_slot(ctx->ctx_tag.owner) &&
				    function ==
				    pci_get_function(ctx->ctx_tag.owner)) {
					dmar_print_domain(domain,
					    show_mappings);
					goto out;
				}
			}
		}
	}
out:;
}

static void
dmar_print_one(int idx, bool show_domains, bool show_mappings)
{
	struct dmar_unit *unit;
	struct dmar_domain *domain;
	int i, frir;

	unit = device_get_softc(dmar_devs[idx]);
	db_printf("dmar%d at %p, root at 0x%jx, ver 0x%x\n", unit->unit, unit,
	    dmar_read8(unit, DMAR_RTADDR_REG), dmar_read4(unit, DMAR_VER_REG));
	db_printf("cap 0x%jx ecap 0x%jx gsts 0x%x fsts 0x%x fectl 0x%x\n",
	    (uintmax_t)dmar_read8(unit, DMAR_CAP_REG),
	    (uintmax_t)dmar_read8(unit, DMAR_ECAP_REG),
	    dmar_read4(unit, DMAR_GSTS_REG),
	    dmar_read4(unit, DMAR_FSTS_REG),
	    dmar_read4(unit, DMAR_FECTL_REG));
	if (unit->ir_enabled) {
		db_printf("ir is enabled; IRT @%p phys 0x%jx maxcnt %d\n",
		    unit->irt, (uintmax_t)unit->irt_phys, unit->irte_cnt);
	}
	db_printf("fed 0x%x fea 0x%x feua 0x%x\n",
	    dmar_read4(unit, DMAR_FEDATA_REG),
	    dmar_read4(unit, DMAR_FEADDR_REG),
	    dmar_read4(unit, DMAR_FEUADDR_REG));
	db_printf("primary fault log:\n");
	for (i = 0; i < DMAR_CAP_NFR(unit->hw_cap); i++) {
		frir = (DMAR_CAP_FRO(unit->hw_cap) + i) * 16;
		db_printf("  %d at 0x%x: %jx %jx\n", i, frir,
		    (uintmax_t)dmar_read8(unit, frir),
		    (uintmax_t)dmar_read8(unit, frir + 8));
	}
	if (DMAR_HAS_QI(unit)) {
		db_printf("ied 0x%x iea 0x%x ieua 0x%x\n",
		    dmar_read4(unit, DMAR_IEDATA_REG),
		    dmar_read4(unit, DMAR_IEADDR_REG),
		    dmar_read4(unit, DMAR_IEUADDR_REG));
		if (unit->qi_enabled) {
			db_printf("qi is enabled: queue @0x%jx (IQA 0x%jx) "
			    "size 0x%jx\n"
		    "  head 0x%x tail 0x%x avail 0x%x status 0x%x ctrl 0x%x\n"
		    "  hw compl 0x%x@%p/phys@%jx next seq 0x%x gen 0x%x\n",
			    (uintmax_t)unit->inv_queue,
			    (uintmax_t)dmar_read8(unit, DMAR_IQA_REG),
			    (uintmax_t)unit->inv_queue_size,
			    dmar_read4(unit, DMAR_IQH_REG),
			    dmar_read4(unit, DMAR_IQT_REG),
			    unit->inv_queue_avail,
			    dmar_read4(unit, DMAR_ICS_REG),
			    dmar_read4(unit, DMAR_IECTL_REG),
			    unit->inv_waitd_seq_hw,
			    &unit->inv_waitd_seq_hw,
			    (uintmax_t)unit->inv_waitd_seq_hw_phys,
			    unit->inv_waitd_seq,
			    unit->inv_waitd_gen);
		} else {
			db_printf("qi is disabled\n");
		}
	}
	if (show_domains) {
		db_printf("domains:\n");
		LIST_FOREACH(domain, &unit->domains, link) {
			dmar_print_domain(domain, show_mappings);
			if (db_pager_quit)
				break;
		}
	}
}

DB_SHOW_COMMAND(dmar, db_dmar_print)
{
	bool show_domains, show_mappings;

	show_domains = strchr(modif, 'd') != NULL;
	show_mappings = strchr(modif, 'm') != NULL;
	if (!have_addr) {
		db_printf("usage: show dmar [/d] [/m] index\n");
		return;
	}
	dmar_print_one((int)addr, show_domains, show_mappings);
}

DB_SHOW_ALL_COMMAND(dmars, db_show_all_dmars)
{
	int i;
	bool show_domains, show_mappings;

	show_domains = strchr(modif, 'd') != NULL;
	show_mappings = strchr(modif, 'm') != NULL;

	for (i = 0; i < dmar_devcnt; i++) {
		dmar_print_one(i, show_domains, show_mappings);
		if (db_pager_quit)
			break;
	}
}
#endif
