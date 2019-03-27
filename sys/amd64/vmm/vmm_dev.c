/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 NetApp, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/jail.h>
#include <sys/queue.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/malloc.h>
#include <sys/conf.h>
#include <sys/sysctl.h>
#include <sys/libkern.h>
#include <sys/ioccom.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <sys/proc.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>

#include <machine/vmparam.h>
#include <machine/vmm.h>
#include <machine/vmm_instruction_emul.h>
#include <machine/vmm_dev.h>

#include "vmm_lapic.h"
#include "vmm_stat.h"
#include "vmm_mem.h"
#include "io/ppt.h"
#include "io/vatpic.h"
#include "io/vioapic.h"
#include "io/vhpet.h"
#include "io/vrtc.h"

struct devmem_softc {
	int	segid;
	char	*name;
	struct cdev *cdev;
	struct vmmdev_softc *sc;
	SLIST_ENTRY(devmem_softc) link;
};

struct vmmdev_softc {
	struct vm	*vm;		/* vm instance cookie */
	struct cdev	*cdev;
	SLIST_ENTRY(vmmdev_softc) link;
	SLIST_HEAD(, devmem_softc) devmem;
	int		flags;
};
#define	VSC_LINKED		0x01

static SLIST_HEAD(, vmmdev_softc) head;

static unsigned pr_allow_flag;
static struct mtx vmmdev_mtx;

static MALLOC_DEFINE(M_VMMDEV, "vmmdev", "vmmdev");

SYSCTL_DECL(_hw_vmm);

static int vmm_priv_check(struct ucred *ucred);
static int devmem_create_cdev(const char *vmname, int id, char *devmem);
static void devmem_destroy(void *arg);

static int
vmm_priv_check(struct ucred *ucred)
{

	if (jailed(ucred) &&
	    !(ucred->cr_prison->pr_allow & pr_allow_flag))
		return (EPERM);

	return (0);
}

static int
vcpu_lock_one(struct vmmdev_softc *sc, int vcpu)
{
	int error;

	if (vcpu < 0 || vcpu >= VM_MAXCPU)
		return (EINVAL);

	error = vcpu_set_state(sc->vm, vcpu, VCPU_FROZEN, true);
	return (error);
}

static void
vcpu_unlock_one(struct vmmdev_softc *sc, int vcpu)
{
	enum vcpu_state state;

	state = vcpu_get_state(sc->vm, vcpu, NULL);
	if (state != VCPU_FROZEN) {
		panic("vcpu %s(%d) has invalid state %d", vm_name(sc->vm),
		    vcpu, state);
	}

	vcpu_set_state(sc->vm, vcpu, VCPU_IDLE, false);
}

static int
vcpu_lock_all(struct vmmdev_softc *sc)
{
	int error, vcpu;

	for (vcpu = 0; vcpu < VM_MAXCPU; vcpu++) {
		error = vcpu_lock_one(sc, vcpu);
		if (error)
			break;
	}

	if (error) {
		while (--vcpu >= 0)
			vcpu_unlock_one(sc, vcpu);
	}

	return (error);
}

static void
vcpu_unlock_all(struct vmmdev_softc *sc)
{
	int vcpu;

	for (vcpu = 0; vcpu < VM_MAXCPU; vcpu++)
		vcpu_unlock_one(sc, vcpu);
}

static struct vmmdev_softc *
vmmdev_lookup(const char *name)
{
	struct vmmdev_softc *sc;

#ifdef notyet	/* XXX kernel is not compiled with invariants */
	mtx_assert(&vmmdev_mtx, MA_OWNED);
#endif

	SLIST_FOREACH(sc, &head, link) {
		if (strcmp(name, vm_name(sc->vm)) == 0)
			break;
	}

	return (sc);
}

static struct vmmdev_softc *
vmmdev_lookup2(struct cdev *cdev)
{

	return (cdev->si_drv1);
}

static int
vmmdev_rw(struct cdev *cdev, struct uio *uio, int flags)
{
	int error, off, c, prot;
	vm_paddr_t gpa, maxaddr;
	void *hpa, *cookie;
	struct vmmdev_softc *sc;

	error = vmm_priv_check(curthread->td_ucred);
	if (error)
		return (error);

	sc = vmmdev_lookup2(cdev);
	if (sc == NULL)
		return (ENXIO);

	/*
	 * Get a read lock on the guest memory map by freezing any vcpu.
	 */
	error = vcpu_lock_one(sc, VM_MAXCPU - 1);
	if (error)
		return (error);

	prot = (uio->uio_rw == UIO_WRITE ? VM_PROT_WRITE : VM_PROT_READ);
	maxaddr = vmm_sysmem_maxaddr(sc->vm);
	while (uio->uio_resid > 0 && error == 0) {
		gpa = uio->uio_offset;
		off = gpa & PAGE_MASK;
		c = min(uio->uio_resid, PAGE_SIZE - off);

		/*
		 * The VM has a hole in its physical memory map. If we want to
		 * use 'dd' to inspect memory beyond the hole we need to
		 * provide bogus data for memory that lies in the hole.
		 *
		 * Since this device does not support lseek(2), dd(1) will
		 * read(2) blocks of data to simulate the lseek(2).
		 */
		hpa = vm_gpa_hold(sc->vm, VM_MAXCPU - 1, gpa, c, prot, &cookie);
		if (hpa == NULL) {
			if (uio->uio_rw == UIO_READ && gpa < maxaddr)
				error = uiomove(__DECONST(void *, zero_region),
				    c, uio);
			else
				error = EFAULT;
		} else {
			error = uiomove(hpa, c, uio);
			vm_gpa_release(cookie);
		}
	}
	vcpu_unlock_one(sc, VM_MAXCPU - 1);
	return (error);
}

CTASSERT(sizeof(((struct vm_memseg *)0)->name) >= SPECNAMELEN + 1);

static int
get_memseg(struct vmmdev_softc *sc, struct vm_memseg *mseg)
{
	struct devmem_softc *dsc;
	int error;
	bool sysmem;

	error = vm_get_memseg(sc->vm, mseg->segid, &mseg->len, &sysmem, NULL);
	if (error || mseg->len == 0)
		return (error);

	if (!sysmem) {
		SLIST_FOREACH(dsc, &sc->devmem, link) {
			if (dsc->segid == mseg->segid)
				break;
		}
		KASSERT(dsc != NULL, ("%s: devmem segment %d not found",
		    __func__, mseg->segid));
		error = copystr(dsc->name, mseg->name, SPECNAMELEN + 1, NULL);
	} else {
		bzero(mseg->name, sizeof(mseg->name));
	}

	return (error);
}

static int
alloc_memseg(struct vmmdev_softc *sc, struct vm_memseg *mseg)
{
	char *name;
	int error;
	bool sysmem;

	error = 0;
	name = NULL;
	sysmem = true;

	if (VM_MEMSEG_NAME(mseg)) {
		sysmem = false;
		name = malloc(SPECNAMELEN + 1, M_VMMDEV, M_WAITOK);
		error = copystr(mseg->name, name, SPECNAMELEN + 1, 0);
		if (error)
			goto done;
	}

	error = vm_alloc_memseg(sc->vm, mseg->segid, mseg->len, sysmem);
	if (error)
		goto done;

	if (VM_MEMSEG_NAME(mseg)) {
		error = devmem_create_cdev(vm_name(sc->vm), mseg->segid, name);
		if (error)
			vm_free_memseg(sc->vm, mseg->segid);
		else
			name = NULL;	/* freed when 'cdev' is destroyed */
	}
done:
	free(name, M_VMMDEV);
	return (error);
}

static int
vm_get_register_set(struct vm *vm, int vcpu, unsigned int count, int *regnum,
    uint64_t *regval)
{
	int error, i;

	error = 0;
	for (i = 0; i < count; i++) {
		error = vm_get_register(vm, vcpu, regnum[i], &regval[i]);
		if (error)
			break;
	}
	return (error);
}

static int
vm_set_register_set(struct vm *vm, int vcpu, unsigned int count, int *regnum,
    uint64_t *regval)
{
	int error, i;

	error = 0;
	for (i = 0; i < count; i++) {
		error = vm_set_register(vm, vcpu, regnum[i], regval[i]);
		if (error)
			break;
	}
	return (error);
}

static int
vmmdev_ioctl(struct cdev *cdev, u_long cmd, caddr_t data, int fflag,
	     struct thread *td)
{
	int error, vcpu, state_changed, size;
	cpuset_t *cpuset;
	struct vmmdev_softc *sc;
	struct vm_register *vmreg;
	struct vm_seg_desc *vmsegdesc;
	struct vm_register_set *vmregset;
	struct vm_run *vmrun;
	struct vm_exception *vmexc;
	struct vm_lapic_irq *vmirq;
	struct vm_lapic_msi *vmmsi;
	struct vm_ioapic_irq *ioapic_irq;
	struct vm_isa_irq *isa_irq;
	struct vm_isa_irq_trigger *isa_irq_trigger;
	struct vm_capability *vmcap;
	struct vm_pptdev *pptdev;
	struct vm_pptdev_mmio *pptmmio;
	struct vm_pptdev_msi *pptmsi;
	struct vm_pptdev_msix *pptmsix;
	struct vm_nmi *vmnmi;
	struct vm_stats *vmstats;
	struct vm_stat_desc *statdesc;
	struct vm_x2apic *x2apic;
	struct vm_gpa_pte *gpapte;
	struct vm_suspend *vmsuspend;
	struct vm_gla2gpa *gg;
	struct vm_activate_cpu *vac;
	struct vm_cpuset *vm_cpuset;
	struct vm_intinfo *vmii;
	struct vm_rtc_time *rtctime;
	struct vm_rtc_data *rtcdata;
	struct vm_memmap *mm;
	struct vm_cpu_topology *topology;
	uint64_t *regvals;
	int *regnums;

	error = vmm_priv_check(curthread->td_ucred);
	if (error)
		return (error);

	sc = vmmdev_lookup2(cdev);
	if (sc == NULL)
		return (ENXIO);

	vcpu = -1;
	state_changed = 0;

	/*
	 * Some VMM ioctls can operate only on vcpus that are not running.
	 */
	switch (cmd) {
	case VM_RUN:
	case VM_GET_REGISTER:
	case VM_SET_REGISTER:
	case VM_GET_SEGMENT_DESCRIPTOR:
	case VM_SET_SEGMENT_DESCRIPTOR:
	case VM_GET_REGISTER_SET:
	case VM_SET_REGISTER_SET:
	case VM_INJECT_EXCEPTION:
	case VM_GET_CAPABILITY:
	case VM_SET_CAPABILITY:
	case VM_PPTDEV_MSI:
	case VM_PPTDEV_MSIX:
	case VM_SET_X2APIC_STATE:
	case VM_GLA2GPA:
	case VM_GLA2GPA_NOFAULT:
	case VM_ACTIVATE_CPU:
	case VM_SET_INTINFO:
	case VM_GET_INTINFO:
	case VM_RESTART_INSTRUCTION:
		/*
		 * XXX fragile, handle with care
		 * Assumes that the first field of the ioctl data is the vcpu.
		 */
		vcpu = *(int *)data;
		error = vcpu_lock_one(sc, vcpu);
		if (error)
			goto done;
		state_changed = 1;
		break;

	case VM_MAP_PPTDEV_MMIO:
	case VM_BIND_PPTDEV:
	case VM_UNBIND_PPTDEV:
	case VM_ALLOC_MEMSEG:
	case VM_MMAP_MEMSEG:
	case VM_REINIT:
		/*
		 * ioctls that operate on the entire virtual machine must
		 * prevent all vcpus from running.
		 */
		error = vcpu_lock_all(sc);
		if (error)
			goto done;
		state_changed = 2;
		break;

	case VM_GET_MEMSEG:
	case VM_MMAP_GETNEXT:
		/*
		 * Lock a vcpu to make sure that the memory map cannot be
		 * modified while it is being inspected.
		 */
		vcpu = VM_MAXCPU - 1;
		error = vcpu_lock_one(sc, vcpu);
		if (error)
			goto done;
		state_changed = 1;
		break;

	default:
		break;
	}

	switch(cmd) {
	case VM_RUN:
		vmrun = (struct vm_run *)data;
		error = vm_run(sc->vm, vmrun);
		break;
	case VM_SUSPEND:
		vmsuspend = (struct vm_suspend *)data;
		error = vm_suspend(sc->vm, vmsuspend->how);
		break;
	case VM_REINIT:
		error = vm_reinit(sc->vm);
		break;
	case VM_STAT_DESC: {
		statdesc = (struct vm_stat_desc *)data;
		error = vmm_stat_desc_copy(statdesc->index,
					statdesc->desc, sizeof(statdesc->desc));
		break;
	}
	case VM_STATS: {
		CTASSERT(MAX_VM_STATS >= MAX_VMM_STAT_ELEMS);
		vmstats = (struct vm_stats *)data;
		getmicrotime(&vmstats->tv);
		error = vmm_stat_copy(sc->vm, vmstats->cpuid,
				      &vmstats->num_entries, vmstats->statbuf);
		break;
	}
	case VM_PPTDEV_MSI:
		pptmsi = (struct vm_pptdev_msi *)data;
		error = ppt_setup_msi(sc->vm, pptmsi->vcpu,
				      pptmsi->bus, pptmsi->slot, pptmsi->func,
				      pptmsi->addr, pptmsi->msg,
				      pptmsi->numvec);
		break;
	case VM_PPTDEV_MSIX:
		pptmsix = (struct vm_pptdev_msix *)data;
		error = ppt_setup_msix(sc->vm, pptmsix->vcpu,
				       pptmsix->bus, pptmsix->slot, 
				       pptmsix->func, pptmsix->idx,
				       pptmsix->addr, pptmsix->msg,
				       pptmsix->vector_control);
		break;
	case VM_MAP_PPTDEV_MMIO:
		pptmmio = (struct vm_pptdev_mmio *)data;
		error = ppt_map_mmio(sc->vm, pptmmio->bus, pptmmio->slot,
				     pptmmio->func, pptmmio->gpa, pptmmio->len,
				     pptmmio->hpa);
		break;
	case VM_BIND_PPTDEV:
		pptdev = (struct vm_pptdev *)data;
		error = vm_assign_pptdev(sc->vm, pptdev->bus, pptdev->slot,
					 pptdev->func);
		break;
	case VM_UNBIND_PPTDEV:
		pptdev = (struct vm_pptdev *)data;
		error = vm_unassign_pptdev(sc->vm, pptdev->bus, pptdev->slot,
					   pptdev->func);
		break;
	case VM_INJECT_EXCEPTION:
		vmexc = (struct vm_exception *)data;
		error = vm_inject_exception(sc->vm, vmexc->cpuid,
		    vmexc->vector, vmexc->error_code_valid, vmexc->error_code,
		    vmexc->restart_instruction);
		break;
	case VM_INJECT_NMI:
		vmnmi = (struct vm_nmi *)data;
		error = vm_inject_nmi(sc->vm, vmnmi->cpuid);
		break;
	case VM_LAPIC_IRQ:
		vmirq = (struct vm_lapic_irq *)data;
		error = lapic_intr_edge(sc->vm, vmirq->cpuid, vmirq->vector);
		break;
	case VM_LAPIC_LOCAL_IRQ:
		vmirq = (struct vm_lapic_irq *)data;
		error = lapic_set_local_intr(sc->vm, vmirq->cpuid,
		    vmirq->vector);
		break;
	case VM_LAPIC_MSI:
		vmmsi = (struct vm_lapic_msi *)data;
		error = lapic_intr_msi(sc->vm, vmmsi->addr, vmmsi->msg);
		break;
	case VM_IOAPIC_ASSERT_IRQ:
		ioapic_irq = (struct vm_ioapic_irq *)data;
		error = vioapic_assert_irq(sc->vm, ioapic_irq->irq);
		break;
	case VM_IOAPIC_DEASSERT_IRQ:
		ioapic_irq = (struct vm_ioapic_irq *)data;
		error = vioapic_deassert_irq(sc->vm, ioapic_irq->irq);
		break;
	case VM_IOAPIC_PULSE_IRQ:
		ioapic_irq = (struct vm_ioapic_irq *)data;
		error = vioapic_pulse_irq(sc->vm, ioapic_irq->irq);
		break;
	case VM_IOAPIC_PINCOUNT:
		*(int *)data = vioapic_pincount(sc->vm);
		break;
	case VM_ISA_ASSERT_IRQ:
		isa_irq = (struct vm_isa_irq *)data;
		error = vatpic_assert_irq(sc->vm, isa_irq->atpic_irq);
		if (error == 0 && isa_irq->ioapic_irq != -1)
			error = vioapic_assert_irq(sc->vm,
			    isa_irq->ioapic_irq);
		break;
	case VM_ISA_DEASSERT_IRQ:
		isa_irq = (struct vm_isa_irq *)data;
		error = vatpic_deassert_irq(sc->vm, isa_irq->atpic_irq);
		if (error == 0 && isa_irq->ioapic_irq != -1)
			error = vioapic_deassert_irq(sc->vm,
			    isa_irq->ioapic_irq);
		break;
	case VM_ISA_PULSE_IRQ:
		isa_irq = (struct vm_isa_irq *)data;
		error = vatpic_pulse_irq(sc->vm, isa_irq->atpic_irq);
		if (error == 0 && isa_irq->ioapic_irq != -1)
			error = vioapic_pulse_irq(sc->vm, isa_irq->ioapic_irq);
		break;
	case VM_ISA_SET_IRQ_TRIGGER:
		isa_irq_trigger = (struct vm_isa_irq_trigger *)data;
		error = vatpic_set_irq_trigger(sc->vm,
		    isa_irq_trigger->atpic_irq, isa_irq_trigger->trigger);
		break;
	case VM_MMAP_GETNEXT:
		mm = (struct vm_memmap *)data;
		error = vm_mmap_getnext(sc->vm, &mm->gpa, &mm->segid,
		    &mm->segoff, &mm->len, &mm->prot, &mm->flags);
		break;
	case VM_MMAP_MEMSEG:
		mm = (struct vm_memmap *)data;
		error = vm_mmap_memseg(sc->vm, mm->gpa, mm->segid, mm->segoff,
		    mm->len, mm->prot, mm->flags);
		break;
	case VM_ALLOC_MEMSEG:
		error = alloc_memseg(sc, (struct vm_memseg *)data);
		break;
	case VM_GET_MEMSEG:
		error = get_memseg(sc, (struct vm_memseg *)data);
		break;
	case VM_GET_REGISTER:
		vmreg = (struct vm_register *)data;
		error = vm_get_register(sc->vm, vmreg->cpuid, vmreg->regnum,
					&vmreg->regval);
		break;
	case VM_SET_REGISTER:
		vmreg = (struct vm_register *)data;
		error = vm_set_register(sc->vm, vmreg->cpuid, vmreg->regnum,
					vmreg->regval);
		break;
	case VM_SET_SEGMENT_DESCRIPTOR:
		vmsegdesc = (struct vm_seg_desc *)data;
		error = vm_set_seg_desc(sc->vm, vmsegdesc->cpuid,
					vmsegdesc->regnum,
					&vmsegdesc->desc);
		break;
	case VM_GET_SEGMENT_DESCRIPTOR:
		vmsegdesc = (struct vm_seg_desc *)data;
		error = vm_get_seg_desc(sc->vm, vmsegdesc->cpuid,
					vmsegdesc->regnum,
					&vmsegdesc->desc);
		break;
	case VM_GET_REGISTER_SET:
		vmregset = (struct vm_register_set *)data;
		if (vmregset->count > VM_REG_LAST) {
			error = EINVAL;
			break;
		}
		regvals = malloc(sizeof(regvals[0]) * vmregset->count, M_VMMDEV,
		    M_WAITOK);
		regnums = malloc(sizeof(regnums[0]) * vmregset->count, M_VMMDEV,
		    M_WAITOK);
		error = copyin(vmregset->regnums, regnums, sizeof(regnums[0]) *
		    vmregset->count);
		if (error == 0)
			error = vm_get_register_set(sc->vm, vmregset->cpuid,
			    vmregset->count, regnums, regvals);
		if (error == 0)
			error = copyout(regvals, vmregset->regvals,
			    sizeof(regvals[0]) * vmregset->count);
		free(regvals, M_VMMDEV);
		free(regnums, M_VMMDEV);
		break;
	case VM_SET_REGISTER_SET:
		vmregset = (struct vm_register_set *)data;
		if (vmregset->count > VM_REG_LAST) {
			error = EINVAL;
			break;
		}
		regvals = malloc(sizeof(regvals[0]) * vmregset->count, M_VMMDEV,
		    M_WAITOK);
		regnums = malloc(sizeof(regnums[0]) * vmregset->count, M_VMMDEV,
		    M_WAITOK);
		error = copyin(vmregset->regnums, regnums, sizeof(regnums[0]) *
		    vmregset->count);
		if (error == 0)
			error = copyin(vmregset->regvals, regvals,
			    sizeof(regvals[0]) * vmregset->count);
		if (error == 0)
			error = vm_set_register_set(sc->vm, vmregset->cpuid,
			    vmregset->count, regnums, regvals);
		free(regvals, M_VMMDEV);
		free(regnums, M_VMMDEV);
		break;
	case VM_GET_CAPABILITY:
		vmcap = (struct vm_capability *)data;
		error = vm_get_capability(sc->vm, vmcap->cpuid,
					  vmcap->captype,
					  &vmcap->capval);
		break;
	case VM_SET_CAPABILITY:
		vmcap = (struct vm_capability *)data;
		error = vm_set_capability(sc->vm, vmcap->cpuid,
					  vmcap->captype,
					  vmcap->capval);
		break;
	case VM_SET_X2APIC_STATE:
		x2apic = (struct vm_x2apic *)data;
		error = vm_set_x2apic_state(sc->vm,
					    x2apic->cpuid, x2apic->state);
		break;
	case VM_GET_X2APIC_STATE:
		x2apic = (struct vm_x2apic *)data;
		error = vm_get_x2apic_state(sc->vm,
					    x2apic->cpuid, &x2apic->state);
		break;
	case VM_GET_GPA_PMAP:
		gpapte = (struct vm_gpa_pte *)data;
		pmap_get_mapping(vmspace_pmap(vm_get_vmspace(sc->vm)),
				 gpapte->gpa, gpapte->pte, &gpapte->ptenum);
		error = 0;
		break;
	case VM_GET_HPET_CAPABILITIES:
		error = vhpet_getcap((struct vm_hpet_cap *)data);
		break;
	case VM_GLA2GPA: {
		CTASSERT(PROT_READ == VM_PROT_READ);
		CTASSERT(PROT_WRITE == VM_PROT_WRITE);
		CTASSERT(PROT_EXEC == VM_PROT_EXECUTE);
		gg = (struct vm_gla2gpa *)data;
		error = vm_gla2gpa(sc->vm, gg->vcpuid, &gg->paging, gg->gla,
		    gg->prot, &gg->gpa, &gg->fault);
		KASSERT(error == 0 || error == EFAULT,
		    ("%s: vm_gla2gpa unknown error %d", __func__, error));
		break;
	}
	case VM_GLA2GPA_NOFAULT:
		gg = (struct vm_gla2gpa *)data;
		error = vm_gla2gpa_nofault(sc->vm, gg->vcpuid, &gg->paging,
		    gg->gla, gg->prot, &gg->gpa, &gg->fault);
		KASSERT(error == 0 || error == EFAULT,
		    ("%s: vm_gla2gpa unknown error %d", __func__, error));
		break;
	case VM_ACTIVATE_CPU:
		vac = (struct vm_activate_cpu *)data;
		error = vm_activate_cpu(sc->vm, vac->vcpuid);
		break;
	case VM_GET_CPUS:
		error = 0;
		vm_cpuset = (struct vm_cpuset *)data;
		size = vm_cpuset->cpusetsize;
		if (size < sizeof(cpuset_t) || size > CPU_MAXSIZE / NBBY) {
			error = ERANGE;
			break;
		}
		cpuset = malloc(size, M_TEMP, M_WAITOK | M_ZERO);
		if (vm_cpuset->which == VM_ACTIVE_CPUS)
			*cpuset = vm_active_cpus(sc->vm);
		else if (vm_cpuset->which == VM_SUSPENDED_CPUS)
			*cpuset = vm_suspended_cpus(sc->vm);
		else if (vm_cpuset->which == VM_DEBUG_CPUS)
			*cpuset = vm_debug_cpus(sc->vm);
		else
			error = EINVAL;
		if (error == 0)
			error = copyout(cpuset, vm_cpuset->cpus, size);
		free(cpuset, M_TEMP);
		break;
	case VM_SUSPEND_CPU:
		vac = (struct vm_activate_cpu *)data;
		error = vm_suspend_cpu(sc->vm, vac->vcpuid);
		break;
	case VM_RESUME_CPU:
		vac = (struct vm_activate_cpu *)data;
		error = vm_resume_cpu(sc->vm, vac->vcpuid);
		break;
	case VM_SET_INTINFO:
		vmii = (struct vm_intinfo *)data;
		error = vm_exit_intinfo(sc->vm, vmii->vcpuid, vmii->info1);
		break;
	case VM_GET_INTINFO:
		vmii = (struct vm_intinfo *)data;
		error = vm_get_intinfo(sc->vm, vmii->vcpuid, &vmii->info1,
		    &vmii->info2);
		break;
	case VM_RTC_WRITE:
		rtcdata = (struct vm_rtc_data *)data;
		error = vrtc_nvram_write(sc->vm, rtcdata->offset,
		    rtcdata->value);
		break;
	case VM_RTC_READ:
		rtcdata = (struct vm_rtc_data *)data;
		error = vrtc_nvram_read(sc->vm, rtcdata->offset,
		    &rtcdata->value);
		break;
	case VM_RTC_SETTIME:
		rtctime = (struct vm_rtc_time *)data;
		error = vrtc_set_time(sc->vm, rtctime->secs);
		break;
	case VM_RTC_GETTIME:
		error = 0;
		rtctime = (struct vm_rtc_time *)data;
		rtctime->secs = vrtc_get_time(sc->vm);
		break;
	case VM_RESTART_INSTRUCTION:
		error = vm_restart_instruction(sc->vm, vcpu);
		break;
	case VM_SET_TOPOLOGY:
		topology = (struct vm_cpu_topology *)data;
		error = vm_set_topology(sc->vm, topology->sockets,
		    topology->cores, topology->threads, topology->maxcpus);
		break;
	case VM_GET_TOPOLOGY:
		topology = (struct vm_cpu_topology *)data;
		vm_get_topology(sc->vm, &topology->sockets, &topology->cores,
		    &topology->threads, &topology->maxcpus);
		error = 0;
		break;
	default:
		error = ENOTTY;
		break;
	}

	if (state_changed == 1)
		vcpu_unlock_one(sc, vcpu);
	else if (state_changed == 2)
		vcpu_unlock_all(sc);

done:
	/* Make sure that no handler returns a bogus value like ERESTART */
	KASSERT(error >= 0, ("vmmdev_ioctl: invalid error return %d", error));
	return (error);
}

static int
vmmdev_mmap_single(struct cdev *cdev, vm_ooffset_t *offset, vm_size_t mapsize,
    struct vm_object **objp, int nprot)
{
	struct vmmdev_softc *sc;
	vm_paddr_t gpa;
	size_t len;
	vm_ooffset_t segoff, first, last;
	int error, found, segid;
	bool sysmem;

	error = vmm_priv_check(curthread->td_ucred);
	if (error)
		return (error);

	first = *offset;
	last = first + mapsize;
	if ((nprot & PROT_EXEC) || first < 0 || first >= last)
		return (EINVAL);

	sc = vmmdev_lookup2(cdev);
	if (sc == NULL) {
		/* virtual machine is in the process of being created */
		return (EINVAL);
	}

	/*
	 * Get a read lock on the guest memory map by freezing any vcpu.
	 */
	error = vcpu_lock_one(sc, VM_MAXCPU - 1);
	if (error)
		return (error);

	gpa = 0;
	found = 0;
	while (!found) {
		error = vm_mmap_getnext(sc->vm, &gpa, &segid, &segoff, &len,
		    NULL, NULL);
		if (error)
			break;

		if (first >= gpa && last <= gpa + len)
			found = 1;
		else
			gpa += len;
	}

	if (found) {
		error = vm_get_memseg(sc->vm, segid, &len, &sysmem, objp);
		KASSERT(error == 0 && *objp != NULL,
		    ("%s: invalid memory segment %d", __func__, segid));
		if (sysmem) {
			vm_object_reference(*objp);
			*offset = segoff + (first - gpa);
		} else {
			error = EINVAL;
		}
	}
	vcpu_unlock_one(sc, VM_MAXCPU - 1);
	return (error);
}

static void
vmmdev_destroy(void *arg)
{
	struct vmmdev_softc *sc = arg;
	struct devmem_softc *dsc;
	int error;

	error = vcpu_lock_all(sc);
	KASSERT(error == 0, ("%s: error %d freezing vcpus", __func__, error));

	while ((dsc = SLIST_FIRST(&sc->devmem)) != NULL) {
		KASSERT(dsc->cdev == NULL, ("%s: devmem not free", __func__));
		SLIST_REMOVE_HEAD(&sc->devmem, link);
		free(dsc->name, M_VMMDEV);
		free(dsc, M_VMMDEV);
	}

	if (sc->cdev != NULL)
		destroy_dev(sc->cdev);

	if (sc->vm != NULL)
		vm_destroy(sc->vm);

	if ((sc->flags & VSC_LINKED) != 0) {
		mtx_lock(&vmmdev_mtx);
		SLIST_REMOVE(&head, sc, vmmdev_softc, link);
		mtx_unlock(&vmmdev_mtx);
	}

	free(sc, M_VMMDEV);
}

static int
sysctl_vmm_destroy(SYSCTL_HANDLER_ARGS)
{
	int error;
	char buf[VM_MAX_NAMELEN];
	struct devmem_softc *dsc;
	struct vmmdev_softc *sc;
	struct cdev *cdev;

	error = vmm_priv_check(req->td->td_ucred);
	if (error)
		return (error);

	strlcpy(buf, "beavis", sizeof(buf));
	error = sysctl_handle_string(oidp, buf, sizeof(buf), req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	mtx_lock(&vmmdev_mtx);
	sc = vmmdev_lookup(buf);
	if (sc == NULL || sc->cdev == NULL) {
		mtx_unlock(&vmmdev_mtx);
		return (EINVAL);
	}

	/*
	 * The 'cdev' will be destroyed asynchronously when 'si_threadcount'
	 * goes down to 0 so we should not do it again in the callback.
	 *
	 * Setting 'sc->cdev' to NULL is also used to indicate that the VM
	 * is scheduled for destruction.
	 */
	cdev = sc->cdev;
	sc->cdev = NULL;		
	mtx_unlock(&vmmdev_mtx);

	/*
	 * Schedule all cdevs to be destroyed:
	 *
	 * - any new operations on the 'cdev' will return an error (ENXIO).
	 *
	 * - when the 'si_threadcount' dwindles down to zero the 'cdev' will
	 *   be destroyed and the callback will be invoked in a taskqueue
	 *   context.
	 *
	 * - the 'devmem' cdevs are destroyed before the virtual machine 'cdev'
	 */
	SLIST_FOREACH(dsc, &sc->devmem, link) {
		KASSERT(dsc->cdev != NULL, ("devmem cdev already destroyed"));
		destroy_dev_sched_cb(dsc->cdev, devmem_destroy, dsc);
	}
	destroy_dev_sched_cb(cdev, vmmdev_destroy, sc);
	return (0);
}
SYSCTL_PROC(_hw_vmm, OID_AUTO, destroy,
	    CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_PRISON,
	    NULL, 0, sysctl_vmm_destroy, "A", NULL);

static struct cdevsw vmmdevsw = {
	.d_name		= "vmmdev",
	.d_version	= D_VERSION,
	.d_ioctl	= vmmdev_ioctl,
	.d_mmap_single	= vmmdev_mmap_single,
	.d_read		= vmmdev_rw,
	.d_write	= vmmdev_rw,
};

static int
sysctl_vmm_create(SYSCTL_HANDLER_ARGS)
{
	int error;
	struct vm *vm;
	struct cdev *cdev;
	struct vmmdev_softc *sc, *sc2;
	char buf[VM_MAX_NAMELEN];

	error = vmm_priv_check(req->td->td_ucred);
	if (error)
		return (error);

	strlcpy(buf, "beavis", sizeof(buf));
	error = sysctl_handle_string(oidp, buf, sizeof(buf), req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	mtx_lock(&vmmdev_mtx);
	sc = vmmdev_lookup(buf);
	mtx_unlock(&vmmdev_mtx);
	if (sc != NULL)
		return (EEXIST);

	error = vm_create(buf, &vm);
	if (error != 0)
		return (error);

	sc = malloc(sizeof(struct vmmdev_softc), M_VMMDEV, M_WAITOK | M_ZERO);
	sc->vm = vm;
	SLIST_INIT(&sc->devmem);

	/*
	 * Lookup the name again just in case somebody sneaked in when we
	 * dropped the lock.
	 */
	mtx_lock(&vmmdev_mtx);
	sc2 = vmmdev_lookup(buf);
	if (sc2 == NULL) {
		SLIST_INSERT_HEAD(&head, sc, link);
		sc->flags |= VSC_LINKED;
	}
	mtx_unlock(&vmmdev_mtx);

	if (sc2 != NULL) {
		vmmdev_destroy(sc);
		return (EEXIST);
	}

	error = make_dev_p(MAKEDEV_CHECKNAME, &cdev, &vmmdevsw, NULL,
			   UID_ROOT, GID_WHEEL, 0600, "vmm/%s", buf);
	if (error != 0) {
		vmmdev_destroy(sc);
		return (error);
	}

	mtx_lock(&vmmdev_mtx);
	sc->cdev = cdev;
	sc->cdev->si_drv1 = sc;
	mtx_unlock(&vmmdev_mtx);

	return (0);
}
SYSCTL_PROC(_hw_vmm, OID_AUTO, create,
	    CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_PRISON,
	    NULL, 0, sysctl_vmm_create, "A", NULL);

void
vmmdev_init(void)
{
	mtx_init(&vmmdev_mtx, "vmm device mutex", NULL, MTX_DEF);
	pr_allow_flag = prison_add_allow(NULL, "vmm", NULL,
	    "Allow use of vmm in a jail.");
}

int
vmmdev_cleanup(void)
{
	int error;

	if (SLIST_EMPTY(&head))
		error = 0;
	else
		error = EBUSY;

	return (error);
}

static int
devmem_mmap_single(struct cdev *cdev, vm_ooffset_t *offset, vm_size_t len,
    struct vm_object **objp, int nprot)
{
	struct devmem_softc *dsc;
	vm_ooffset_t first, last;
	size_t seglen;
	int error;
	bool sysmem;

	dsc = cdev->si_drv1;
	if (dsc == NULL) {
		/* 'cdev' has been created but is not ready for use */
		return (ENXIO);
	}

	first = *offset;
	last = *offset + len;
	if ((nprot & PROT_EXEC) || first < 0 || first >= last)
		return (EINVAL);

	error = vcpu_lock_one(dsc->sc, VM_MAXCPU - 1);
	if (error)
		return (error);

	error = vm_get_memseg(dsc->sc->vm, dsc->segid, &seglen, &sysmem, objp);
	KASSERT(error == 0 && !sysmem && *objp != NULL,
	    ("%s: invalid devmem segment %d", __func__, dsc->segid));

	vcpu_unlock_one(dsc->sc, VM_MAXCPU - 1);

	if (seglen >= last) {
		vm_object_reference(*objp);
		return (0);
	} else {
		return (EINVAL);
	}
}

static struct cdevsw devmemsw = {
	.d_name		= "devmem",
	.d_version	= D_VERSION,
	.d_mmap_single	= devmem_mmap_single,
};

static int
devmem_create_cdev(const char *vmname, int segid, char *devname)
{
	struct devmem_softc *dsc;
	struct vmmdev_softc *sc;
	struct cdev *cdev;
	int error;

	error = make_dev_p(MAKEDEV_CHECKNAME, &cdev, &devmemsw, NULL,
	    UID_ROOT, GID_WHEEL, 0600, "vmm.io/%s.%s", vmname, devname);
	if (error)
		return (error);

	dsc = malloc(sizeof(struct devmem_softc), M_VMMDEV, M_WAITOK | M_ZERO);

	mtx_lock(&vmmdev_mtx);
	sc = vmmdev_lookup(vmname);
	KASSERT(sc != NULL, ("%s: vm %s softc not found", __func__, vmname));
	if (sc->cdev == NULL) {
		/* virtual machine is being created or destroyed */
		mtx_unlock(&vmmdev_mtx);
		free(dsc, M_VMMDEV);
		destroy_dev_sched_cb(cdev, NULL, 0);
		return (ENODEV);
	}

	dsc->segid = segid;
	dsc->name = devname;
	dsc->cdev = cdev;
	dsc->sc = sc;
	SLIST_INSERT_HEAD(&sc->devmem, dsc, link);
	mtx_unlock(&vmmdev_mtx);

	/* The 'cdev' is ready for use after 'si_drv1' is initialized */
	cdev->si_drv1 = dsc;
	return (0);
}

static void
devmem_destroy(void *arg)
{
	struct devmem_softc *dsc = arg;

	KASSERT(dsc->cdev, ("%s: devmem cdev already destroyed", __func__));
	dsc->cdev = NULL;
	dsc->sc = NULL;
}
