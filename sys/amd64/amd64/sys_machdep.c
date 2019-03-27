/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2003 Peter Wemm.
 * Copyright (c) 1990 The Regents of the University of California.
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
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)sys_machdep.c	5.5 (Berkeley) 1/19/91
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_capsicum.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/capsicum.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/pcpu.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/smp.h>
#include <sys/sysproto.h>
#include <sys/uio.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_kern.h>		/* for kernel_map */
#include <vm/vm_map.h>
#include <vm/vm_extern.h>

#include <machine/frame.h>
#include <machine/md_var.h>
#include <machine/pcb.h>
#include <machine/specialreg.h>
#include <machine/sysarch.h>
#include <machine/tss.h>
#include <machine/vmparam.h>

#include <security/audit/audit.h>

static void user_ldt_deref(struct proc_ldt *pldt);
static void user_ldt_derefl(struct proc_ldt *pldt);

#define	MAX_LD		8192

int max_ldt_segment = 512;
SYSCTL_INT(_machdep, OID_AUTO, max_ldt_segment, CTLFLAG_RDTUN,
    &max_ldt_segment, 0,
    "Maximum number of allowed LDT segments in the single address space");

static void
max_ldt_segment_init(void *arg __unused)
{

	if (max_ldt_segment <= 0)
		max_ldt_segment = 1;
	if (max_ldt_segment > MAX_LD)
		max_ldt_segment = MAX_LD;
}
SYSINIT(maxldt, SI_SUB_VM_CONF, SI_ORDER_ANY, max_ldt_segment_init, NULL);

#ifndef _SYS_SYSPROTO_H_
struct sysarch_args {
	int op;
	char *parms;
};
#endif

int
sysarch_ldt(struct thread *td, struct sysarch_args *uap, int uap_space)
{
	struct i386_ldt_args *largs, la;
	struct user_segment_descriptor *lp;
	int error = 0;

	/*
	 * XXXKIB check that the BSM generation code knows to encode
	 * the op argument.
	 */
	AUDIT_ARG_CMD(uap->op);
	if (uap_space == UIO_USERSPACE) {
		error = copyin(uap->parms, &la, sizeof(struct i386_ldt_args));
		if (error != 0)
			return (error);
		largs = &la;
	} else
		largs = (struct i386_ldt_args *)uap->parms;

	switch (uap->op) {
	case I386_GET_LDT:
		error = amd64_get_ldt(td, largs);
		break;
	case I386_SET_LDT:
		if (largs->descs != NULL && largs->num > max_ldt_segment)
			return (EINVAL);
		set_pcb_flags(td->td_pcb, PCB_FULL_IRET);
		if (largs->descs != NULL) {
			lp = malloc(largs->num * sizeof(struct
			    user_segment_descriptor), M_TEMP, M_WAITOK);
			error = copyin(largs->descs, lp, largs->num *
			    sizeof(struct user_segment_descriptor));
			if (error == 0)
				error = amd64_set_ldt(td, largs, lp);
			free(lp, M_TEMP);
		} else {
			error = amd64_set_ldt(td, largs, NULL);
		}
		break;
	}
	return (error);
}

void
update_gdt_gsbase(struct thread *td, uint32_t base)
{
	struct user_segment_descriptor *sd;

	if (td != curthread)
		return;
	set_pcb_flags(td->td_pcb, PCB_FULL_IRET);
	critical_enter();
	sd = PCPU_GET(gs32p);
	sd->sd_lobase = base & 0xffffff;
	sd->sd_hibase = (base >> 24) & 0xff;
	critical_exit();
}

void
update_gdt_fsbase(struct thread *td, uint32_t base)
{
	struct user_segment_descriptor *sd;

	if (td != curthread)
		return;
	set_pcb_flags(td->td_pcb, PCB_FULL_IRET);
	critical_enter();
	sd = PCPU_GET(fs32p);
	sd->sd_lobase = base & 0xffffff;
	sd->sd_hibase = (base >> 24) & 0xff;
	critical_exit();
}

int
sysarch(struct thread *td, struct sysarch_args *uap)
{
	struct pcb *pcb;
	struct vm_map *map;
	uint32_t i386base;
	uint64_t a64base;
	struct i386_ioperm_args iargs;
	struct i386_get_xfpustate i386xfpu;
	struct i386_set_pkru i386pkru;
	struct amd64_get_xfpustate a64xfpu;
	struct amd64_set_pkru a64pkru;
	int error;

#ifdef CAPABILITY_MODE
	/*
	 * When adding new operations, add a new case statement here to
	 * explicitly indicate whether or not the operation is safe to
	 * perform in capability mode.
	 */
	if (IN_CAPABILITY_MODE(td)) {
		switch (uap->op) {
		case I386_GET_LDT:
		case I386_SET_LDT:
		case I386_GET_IOPERM:
		case I386_GET_FSBASE:
		case I386_SET_FSBASE:
		case I386_GET_GSBASE:
		case I386_SET_GSBASE:
		case I386_GET_XFPUSTATE:
		case I386_SET_PKRU:
		case I386_CLEAR_PKRU:
		case AMD64_GET_FSBASE:
		case AMD64_SET_FSBASE:
		case AMD64_GET_GSBASE:
		case AMD64_SET_GSBASE:
		case AMD64_GET_XFPUSTATE:
		case AMD64_SET_PKRU:
		case AMD64_CLEAR_PKRU:
			break;

		case I386_SET_IOPERM:
		default:
#ifdef KTRACE
			if (KTRPOINT(td, KTR_CAPFAIL))
				ktrcapfail(CAPFAIL_SYSCALL, NULL, NULL);
#endif
			return (ECAPMODE);
		}
	}
#endif

	if (uap->op == I386_GET_LDT || uap->op == I386_SET_LDT)
		return (sysarch_ldt(td, uap, UIO_USERSPACE));

	error = 0;
	pcb = td->td_pcb;

	/*
	 * XXXKIB check that the BSM generation code knows to encode
	 * the op argument.
	 */
	AUDIT_ARG_CMD(uap->op);
	switch (uap->op) {
	case I386_GET_IOPERM:
	case I386_SET_IOPERM:
		if ((error = copyin(uap->parms, &iargs,
		    sizeof(struct i386_ioperm_args))) != 0)
			return (error);
		break;
	case I386_GET_XFPUSTATE:
		if ((error = copyin(uap->parms, &i386xfpu,
		    sizeof(struct i386_get_xfpustate))) != 0)
			return (error);
		a64xfpu.addr = (void *)(uintptr_t)i386xfpu.addr;
		a64xfpu.len = i386xfpu.len;
		break;
	case I386_SET_PKRU:
	case I386_CLEAR_PKRU:
		if ((error = copyin(uap->parms, &i386pkru,
		    sizeof(struct i386_set_pkru))) != 0)
			return (error);
		a64pkru.addr = (void *)(uintptr_t)i386pkru.addr;
		a64pkru.len = i386pkru.len;
		a64pkru.keyidx = i386pkru.keyidx;
		a64pkru.flags = i386pkru.flags;
		break;
	case AMD64_GET_XFPUSTATE:
		if ((error = copyin(uap->parms, &a64xfpu,
		    sizeof(struct amd64_get_xfpustate))) != 0)
			return (error);
		break;
	case AMD64_SET_PKRU:
	case AMD64_CLEAR_PKRU:
		if ((error = copyin(uap->parms, &a64pkru,
		    sizeof(struct amd64_set_pkru))) != 0)
			return (error);
		break;
	default:
		break;
	}

	switch (uap->op) {
	case I386_GET_IOPERM:
		error = amd64_get_ioperm(td, &iargs);
		if (error == 0)
			error = copyout(&iargs, uap->parms,
			    sizeof(struct i386_ioperm_args));
		break;
	case I386_SET_IOPERM:
		error = amd64_set_ioperm(td, &iargs);
		break;
	case I386_GET_FSBASE:
		update_pcb_bases(pcb);
		i386base = pcb->pcb_fsbase;
		error = copyout(&i386base, uap->parms, sizeof(i386base));
		break;
	case I386_SET_FSBASE:
		error = copyin(uap->parms, &i386base, sizeof(i386base));
		if (!error) {
			set_pcb_flags(pcb, PCB_FULL_IRET);
			pcb->pcb_fsbase = i386base;
			td->td_frame->tf_fs = _ufssel;
			update_gdt_fsbase(td, i386base);
		}
		break;
	case I386_GET_GSBASE:
		update_pcb_bases(pcb);
		i386base = pcb->pcb_gsbase;
		error = copyout(&i386base, uap->parms, sizeof(i386base));
		break;
	case I386_SET_GSBASE:
		error = copyin(uap->parms, &i386base, sizeof(i386base));
		if (!error) {
			set_pcb_flags(pcb, PCB_FULL_IRET);
			pcb->pcb_gsbase = i386base;
			td->td_frame->tf_gs = _ugssel;
			update_gdt_gsbase(td, i386base);
		}
		break;
	case AMD64_GET_FSBASE:
		update_pcb_bases(pcb);
		error = copyout(&pcb->pcb_fsbase, uap->parms,
		    sizeof(pcb->pcb_fsbase));
		break;
		
	case AMD64_SET_FSBASE:
		error = copyin(uap->parms, &a64base, sizeof(a64base));
		if (!error) {
			if (a64base < VM_MAXUSER_ADDRESS) {
				set_pcb_flags(pcb, PCB_FULL_IRET);
				pcb->pcb_fsbase = a64base;
				td->td_frame->tf_fs = _ufssel;
			} else
				error = EINVAL;
		}
		break;

	case AMD64_GET_GSBASE:
		update_pcb_bases(pcb);
		error = copyout(&pcb->pcb_gsbase, uap->parms,
		    sizeof(pcb->pcb_gsbase));
		break;

	case AMD64_SET_GSBASE:
		error = copyin(uap->parms, &a64base, sizeof(a64base));
		if (!error) {
			if (a64base < VM_MAXUSER_ADDRESS) {
				set_pcb_flags(pcb, PCB_FULL_IRET);
				pcb->pcb_gsbase = a64base;
				td->td_frame->tf_gs = _ugssel;
			} else
				error = EINVAL;
		}
		break;

	case I386_GET_XFPUSTATE:
	case AMD64_GET_XFPUSTATE:
		if (a64xfpu.len > cpu_max_ext_state_size -
		    sizeof(struct savefpu))
			return (EINVAL);
		fpugetregs(td);
		error = copyout((char *)(get_pcb_user_save_td(td) + 1),
		    a64xfpu.addr, a64xfpu.len);
		break;

	case I386_SET_PKRU:
	case AMD64_SET_PKRU:
		/*
		 * Read-lock the map to synchronize with parallel
		 * pmap_vmspace_copy() on fork.
		 */
		map = &td->td_proc->p_vmspace->vm_map;
		vm_map_lock_read(map);
		error = pmap_pkru_set(PCPU_GET(curpmap),
		    (vm_offset_t)a64pkru.addr, (vm_offset_t)a64pkru.addr +
		    a64pkru.len, a64pkru.keyidx, a64pkru.flags);
		vm_map_unlock_read(map);
		break;

	case I386_CLEAR_PKRU:
	case AMD64_CLEAR_PKRU:
		if (a64pkru.flags != 0 || a64pkru.keyidx != 0) {
			error = EINVAL;
			break;
		}
		map = &td->td_proc->p_vmspace->vm_map;
		vm_map_lock_read(map);
		error = pmap_pkru_clear(PCPU_GET(curpmap),
		    (vm_offset_t)a64pkru.addr,
		    (vm_offset_t)a64pkru.addr + a64pkru.len);
		vm_map_unlock(map);
		break;

	default:
		error = EINVAL;
		break;
	}
	return (error);
}

int
amd64_set_ioperm(td, uap)
	struct thread *td;
	struct i386_ioperm_args *uap;
{
	char *iomap;
	struct amd64tss *tssp;
	struct system_segment_descriptor *tss_sd;
	struct pcb *pcb;
	u_int i;
	int error;

	if ((error = priv_check(td, PRIV_IO)) != 0)
		return (error);
	if ((error = securelevel_gt(td->td_ucred, 0)) != 0)
		return (error);
	if (uap->start > uap->start + uap->length ||
	    uap->start + uap->length > IOPAGES * PAGE_SIZE * NBBY)
		return (EINVAL);

	/*
	 * XXX
	 * While this is restricted to root, we should probably figure out
	 * whether any other driver is using this i/o address, as so not to
	 * cause confusion.  This probably requires a global 'usage registry'.
	 */
	pcb = td->td_pcb;
	if (pcb->pcb_tssp == NULL) {
		tssp = (struct amd64tss *)kmem_malloc(ctob(IOPAGES + 1),
		    M_WAITOK);
		pmap_pti_add_kva((vm_offset_t)tssp, (vm_offset_t)tssp +
		    ctob(IOPAGES + 1), false);
		iomap = (char *)&tssp[1];
		memset(iomap, 0xff, IOPERM_BITMAP_SIZE);
		critical_enter();
		/* Takes care of tss_rsp0. */
		memcpy(tssp, &common_tss[PCPU_GET(cpuid)],
		    sizeof(struct amd64tss));
		tssp->tss_iobase = sizeof(*tssp);
		pcb->pcb_tssp = tssp;
		tss_sd = PCPU_GET(tss);
		tss_sd->sd_lobase = (u_long)tssp & 0xffffff;
		tss_sd->sd_hibase = ((u_long)tssp >> 24) & 0xfffffffffful;
		tss_sd->sd_type = SDT_SYSTSS;
		ltr(GSEL(GPROC0_SEL, SEL_KPL));
		PCPU_SET(tssp, tssp);
		critical_exit();
	} else
		iomap = (char *)&pcb->pcb_tssp[1];
	for (i = uap->start; i < uap->start + uap->length; i++) {
		if (uap->enable)
			iomap[i >> 3] &= ~(1 << (i & 7));
		else
			iomap[i >> 3] |= (1 << (i & 7));
	}
	return (error);
}

int
amd64_get_ioperm(td, uap)
	struct thread *td;
	struct i386_ioperm_args *uap;
{
	int i, state;
	char *iomap;

	if (uap->start >= IOPAGES * PAGE_SIZE * NBBY)
		return (EINVAL);
	if (td->td_pcb->pcb_tssp == NULL) {
		uap->length = 0;
		goto done;
	}

	iomap = (char *)&td->td_pcb->pcb_tssp[1];

	i = uap->start;
	state = (iomap[i >> 3] >> (i & 7)) & 1;
	uap->enable = !state;
	uap->length = 1;

	for (i = uap->start + 1; i < IOPAGES * PAGE_SIZE * NBBY; i++) {
		if (state != ((iomap[i >> 3] >> (i & 7)) & 1))
			break;
		uap->length++;
	}

done:
	return (0);
}

/*
 * Update the GDT entry pointing to the LDT to point to the LDT of the
 * current process.
 */
static void
set_user_ldt(struct mdproc *mdp)
{

	*PCPU_GET(ldt) = mdp->md_ldt_sd;
	lldt(GSEL(GUSERLDT_SEL, SEL_KPL));
}

static void
set_user_ldt_rv(struct vmspace *vmsp)
{
	struct thread *td;

	td = curthread;
	if (vmsp != td->td_proc->p_vmspace)
		return;

	set_user_ldt(&td->td_proc->p_md);
}

struct proc_ldt *
user_ldt_alloc(struct proc *p, int force)
{
	struct proc_ldt *pldt, *new_ldt;
	struct mdproc *mdp;
	struct soft_segment_descriptor sldt;
	vm_offset_t sva;
	vm_size_t sz;

	mtx_assert(&dt_lock, MA_OWNED);
	mdp = &p->p_md;
	if (!force && mdp->md_ldt != NULL)
		return (mdp->md_ldt);
	mtx_unlock(&dt_lock);
	new_ldt = malloc(sizeof(struct proc_ldt), M_SUBPROC, M_WAITOK);
	sz = max_ldt_segment * sizeof(struct user_segment_descriptor);
	sva = kmem_malloc(sz, M_WAITOK | M_ZERO);
	new_ldt->ldt_base = (caddr_t)sva;
	pmap_pti_add_kva(sva, sva + sz, false);
	new_ldt->ldt_refcnt = 1;
	sldt.ssd_base = sva;
	sldt.ssd_limit = sz - 1;
	sldt.ssd_type = SDT_SYSLDT;
	sldt.ssd_dpl = SEL_KPL;
	sldt.ssd_p = 1;
	sldt.ssd_long = 0;
	sldt.ssd_def32 = 0;
	sldt.ssd_gran = 0;
	mtx_lock(&dt_lock);
	pldt = mdp->md_ldt;
	if (pldt != NULL && !force) {
		pmap_pti_remove_kva(sva, sva + sz);
		kmem_free(sva, sz);
		free(new_ldt, M_SUBPROC);
		return (pldt);
	}

	if (pldt != NULL) {
		bcopy(pldt->ldt_base, new_ldt->ldt_base, max_ldt_segment *
		    sizeof(struct user_segment_descriptor));
		user_ldt_derefl(pldt);
	}
	critical_enter();
	ssdtosyssd(&sldt, &p->p_md.md_ldt_sd);
	atomic_thread_fence_rel();
	mdp->md_ldt = new_ldt;
	critical_exit();
	smp_rendezvous(NULL, (void (*)(void *))set_user_ldt_rv, NULL,
	    p->p_vmspace);

	return (mdp->md_ldt);
}

void
user_ldt_free(struct thread *td)
{
	struct proc *p = td->td_proc;
	struct mdproc *mdp = &p->p_md;
	struct proc_ldt *pldt;

	mtx_lock(&dt_lock);
	if ((pldt = mdp->md_ldt) == NULL) {
		mtx_unlock(&dt_lock);
		return;
	}

	critical_enter();
	mdp->md_ldt = NULL;
	atomic_thread_fence_rel();
	bzero(&mdp->md_ldt_sd, sizeof(mdp->md_ldt_sd));
	if (td == curthread)
		lldt(GSEL(GNULL_SEL, SEL_KPL));
	critical_exit();
	user_ldt_deref(pldt);
}

static void
user_ldt_derefl(struct proc_ldt *pldt)
{
	vm_offset_t sva;
	vm_size_t sz;

	if (--pldt->ldt_refcnt == 0) {
		sva = (vm_offset_t)pldt->ldt_base;
		sz = max_ldt_segment * sizeof(struct user_segment_descriptor);
		pmap_pti_remove_kva(sva, sva + sz);
		kmem_free(sva, sz);
		free(pldt, M_SUBPROC);
	}
}

static void
user_ldt_deref(struct proc_ldt *pldt)
{

	mtx_assert(&dt_lock, MA_OWNED);
	user_ldt_derefl(pldt);
	mtx_unlock(&dt_lock);
}

/*
 * Note for the authors of compat layers (linux, etc): copyout() in
 * the function below is not a problem since it presents data in
 * arch-specific format (i.e. i386-specific in this case), not in
 * the OS-specific one.
 */
int
amd64_get_ldt(struct thread *td, struct i386_ldt_args *uap)
{
	struct proc_ldt *pldt;
	struct user_segment_descriptor *lp;
	uint64_t *data;
	u_int i, num;
	int error;

#ifdef	DEBUG
	printf("amd64_get_ldt: start=%u num=%u descs=%p\n",
	    uap->start, uap->num, (void *)uap->descs);
#endif

	pldt = td->td_proc->p_md.md_ldt;
	if (pldt == NULL || uap->start >= max_ldt_segment || uap->num == 0) {
		td->td_retval[0] = 0;
		return (0);
	}
	num = min(uap->num, max_ldt_segment - uap->start);
	lp = &((struct user_segment_descriptor *)(pldt->ldt_base))[uap->start];
	data = malloc(num * sizeof(struct user_segment_descriptor), M_TEMP,
	    M_WAITOK);
	mtx_lock(&dt_lock);
	for (i = 0; i < num; i++)
		data[i] = ((volatile uint64_t *)lp)[i];
	mtx_unlock(&dt_lock);
	error = copyout(data, uap->descs, num *
	    sizeof(struct user_segment_descriptor));
	free(data, M_TEMP);
	if (error == 0)
		td->td_retval[0] = num;
	return (error);
}

int
amd64_set_ldt(struct thread *td, struct i386_ldt_args *uap,
    struct user_segment_descriptor *descs)
{
	struct mdproc *mdp;
	struct proc_ldt *pldt;
	struct user_segment_descriptor *dp;
	struct proc *p;
	u_int largest_ld, i;
	int error;

#ifdef	DEBUG
	printf("amd64_set_ldt: start=%u num=%u descs=%p\n",
	    uap->start, uap->num, (void *)uap->descs);
#endif
	mdp = &td->td_proc->p_md;
	error = 0;

	set_pcb_flags(td->td_pcb, PCB_FULL_IRET);
	p = td->td_proc;
	if (descs == NULL) {
		/* Free descriptors */
		if (uap->start == 0 && uap->num == 0)
			uap->num = max_ldt_segment;
		if (uap->num == 0)
			return (EINVAL);
		if ((pldt = mdp->md_ldt) == NULL ||
		    uap->start >= max_ldt_segment)
			return (0);
		largest_ld = uap->start + uap->num;
		if (largest_ld > max_ldt_segment)
			largest_ld = max_ldt_segment;
		if (largest_ld < uap->start)
			return (EINVAL);
		mtx_lock(&dt_lock);
		for (i = uap->start; i < largest_ld; i++)
			((volatile uint64_t *)(pldt->ldt_base))[i] = 0;
		mtx_unlock(&dt_lock);
		return (0);
	}

	if (!(uap->start == LDT_AUTO_ALLOC && uap->num == 1)) {
		/* verify range of descriptors to modify */
		largest_ld = uap->start + uap->num;
		if (uap->start >= max_ldt_segment ||
		    largest_ld > max_ldt_segment ||
		    largest_ld < uap->start)
			return (EINVAL);
	}

	/* Check descriptors for access violations */
	for (i = 0; i < uap->num; i++) {
		dp = &descs[i];

		switch (dp->sd_type) {
		case SDT_SYSNULL:	/* system null */
			dp->sd_p = 0;
			break;
		case SDT_SYS286TSS:
		case SDT_SYSLDT:
		case SDT_SYS286BSY:
		case SDT_SYS286CGT:
		case SDT_SYSTASKGT:
		case SDT_SYS286IGT:
		case SDT_SYS286TGT:
		case SDT_SYSNULL2:
		case SDT_SYSTSS:
		case SDT_SYSNULL3:
		case SDT_SYSBSY:
		case SDT_SYSCGT:
		case SDT_SYSNULL4:
		case SDT_SYSIGT:
		case SDT_SYSTGT:
			return (EACCES);

		/* memory segment types */
		case SDT_MEMEC:   /* memory execute only conforming */
		case SDT_MEMEAC:  /* memory execute only accessed conforming */
		case SDT_MEMERC:  /* memory execute read conforming */
		case SDT_MEMERAC: /* memory execute read accessed conforming */
			 /* Must be "present" if executable and conforming. */
			if (dp->sd_p == 0)
				return (EACCES);
			break;
		case SDT_MEMRO:   /* memory read only */
		case SDT_MEMROA:  /* memory read only accessed */
		case SDT_MEMRW:   /* memory read write */
		case SDT_MEMRWA:  /* memory read write accessed */
		case SDT_MEMROD:  /* memory read only expand dwn limit */
		case SDT_MEMRODA: /* memory read only expand dwn lim accessed */
		case SDT_MEMRWD:  /* memory read write expand dwn limit */
		case SDT_MEMRWDA: /* memory read write expand dwn lim acessed */
		case SDT_MEME:    /* memory execute only */
		case SDT_MEMEA:   /* memory execute only accessed */
		case SDT_MEMER:   /* memory execute read */
		case SDT_MEMERA:  /* memory execute read accessed */
			break;
		default:
			return(EINVAL);
		}

		/* Only user (ring-3) descriptors may be present. */
		if ((dp->sd_p != 0) && (dp->sd_dpl != SEL_UPL))
			return (EACCES);
	}

	if (uap->start == LDT_AUTO_ALLOC && uap->num == 1) {
		/* Allocate a free slot */
		mtx_lock(&dt_lock);
		pldt = user_ldt_alloc(p, 0);
		if (pldt == NULL) {
			mtx_unlock(&dt_lock);
			return (ENOMEM);
		}

		/*
		 * start scanning a bit up to leave room for NVidia and
		 * Wine, which still user the "Blat" method of allocation.
		 */
		i = 16;
		dp = &((struct user_segment_descriptor *)(pldt->ldt_base))[i];
		for (; i < max_ldt_segment; ++i, ++dp) {
			if (dp->sd_type == SDT_SYSNULL)
				break;
		}
		if (i >= max_ldt_segment) {
			mtx_unlock(&dt_lock);
			return (ENOSPC);
		}
		uap->start = i;
		error = amd64_set_ldt_data(td, i, 1, descs);
		mtx_unlock(&dt_lock);
	} else {
		largest_ld = uap->start + uap->num;
		if (largest_ld > max_ldt_segment)
			return (EINVAL);
		mtx_lock(&dt_lock);
		if (user_ldt_alloc(p, 0) != NULL) {
			error = amd64_set_ldt_data(td, uap->start, uap->num,
			    descs);
		}
		mtx_unlock(&dt_lock);
	}
	if (error == 0)
		td->td_retval[0] = uap->start;
	return (error);
}

int
amd64_set_ldt_data(struct thread *td, int start, int num,
    struct user_segment_descriptor *descs)
{
	struct mdproc *mdp;
	struct proc_ldt *pldt;
	volatile uint64_t *dst, *src;
	int i;

	mtx_assert(&dt_lock, MA_OWNED);

	mdp = &td->td_proc->p_md;
	pldt = mdp->md_ldt;
	dst = (volatile uint64_t *)(pldt->ldt_base);
	src = (volatile uint64_t *)descs;
	for (i = 0; i < num; i++)
		dst[start + i] = src[i];
	return (0);
}
