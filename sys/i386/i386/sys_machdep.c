/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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
#include "opt_kstack_pages.h"

#include <sys/param.h>
#include <sys/capsicum.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/smp.h>
#include <sys/sysproto.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_extern.h>

#include <machine/atomic.h>
#include <machine/cpu.h>
#include <machine/pcb.h>
#include <machine/pcb_ext.h>
#include <machine/proc.h>
#include <machine/sysarch.h>

#include <security/audit/audit.h>

#include <vm/vm_kern.h>		/* for kernel_map */

#define MAX_LD 8192
#define LD_PER_PAGE 512
#define	NEW_MAX_LD(num)  rounddown2(num + LD_PER_PAGE, LD_PER_PAGE)
#define SIZE_FROM_LARGEST_LD(num) (NEW_MAX_LD(num) << 3)
#define	NULL_LDT_BASE	((caddr_t)NULL)

#ifdef SMP
static void set_user_ldt_rv(void *arg);
#endif
static int i386_set_ldt_data(struct thread *, int start, int num,
    union descriptor *descs);
static int i386_ldt_grow(struct thread *td, int len);

void
fill_based_sd(struct segment_descriptor *sdp, uint32_t base)
{

	sdp->sd_lobase = base & 0xffffff;
	sdp->sd_hibase = (base >> 24) & 0xff;
	sdp->sd_lolimit = 0xffff;	/* 4GB limit, wraps around */
	sdp->sd_hilimit = 0xf;
	sdp->sd_type = SDT_MEMRWA;
	sdp->sd_dpl = SEL_UPL;
	sdp->sd_p = 1;
	sdp->sd_xx = 0;
	sdp->sd_def32 = 1;
	sdp->sd_gran = 1;
}

/*
 * Construct special descriptors for "base" selectors.  Store them in
 * the PCB for later use by cpu_switch().  Store them in the GDT for
 * more immediate use.  The GDT entries are part of the current
 * context.  Callers must load related segment registers to complete
 * setting up the current context.
 */
void
set_fsbase(struct thread *td, uint32_t base)
{
	struct segment_descriptor sd;

	fill_based_sd(&sd, base);
	critical_enter();
	td->td_pcb->pcb_fsd = sd;
	PCPU_GET(fsgs_gdt)[0] = sd;
	critical_exit();
}

void
set_gsbase(struct thread *td, uint32_t base)
{
	struct segment_descriptor sd;

	fill_based_sd(&sd, base);
	critical_enter();
	td->td_pcb->pcb_gsd = sd;
	PCPU_GET(fsgs_gdt)[1] = sd;
	critical_exit();
}

#ifndef _SYS_SYSPROTO_H_
struct sysarch_args {
	int op;
	char *parms;
};
#endif

int
sysarch(struct thread *td, struct sysarch_args *uap)
{
	int error;
	union descriptor *lp;
	union {
		struct i386_ldt_args largs;
		struct i386_ioperm_args iargs;
		struct i386_get_xfpustate xfpu;
	} kargs;
	uint32_t base;
	struct segment_descriptor *sdp;

	AUDIT_ARG_CMD(uap->op);

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

	switch (uap->op) {
	case I386_GET_IOPERM:
	case I386_SET_IOPERM:
		if ((error = copyin(uap->parms, &kargs.iargs,
		    sizeof(struct i386_ioperm_args))) != 0)
			return (error);
		break;
	case I386_GET_LDT:
	case I386_SET_LDT:
		if ((error = copyin(uap->parms, &kargs.largs,
		    sizeof(struct i386_ldt_args))) != 0)
			return (error);
		break;
	case I386_GET_XFPUSTATE:
		if ((error = copyin(uap->parms, &kargs.xfpu,
		    sizeof(struct i386_get_xfpustate))) != 0)
			return (error);
		break;
	default:
		break;
	}

	switch (uap->op) {
	case I386_GET_LDT:
		error = i386_get_ldt(td, &kargs.largs);
		break;
	case I386_SET_LDT:
		if (kargs.largs.descs != NULL) {
			if (kargs.largs.num > MAX_LD)
				return (EINVAL);
			lp = malloc(kargs.largs.num * sizeof(union descriptor),
			    M_TEMP, M_WAITOK);
			error = copyin(kargs.largs.descs, lp,
			    kargs.largs.num * sizeof(union descriptor));
			if (error == 0)
				error = i386_set_ldt(td, &kargs.largs, lp);
			free(lp, M_TEMP);
		} else {
			error = i386_set_ldt(td, &kargs.largs, NULL);
		}
		break;
	case I386_GET_IOPERM:
		error = i386_get_ioperm(td, &kargs.iargs);
		if (error == 0)
			error = copyout(&kargs.iargs, uap->parms,
			    sizeof(struct i386_ioperm_args));
		break;
	case I386_SET_IOPERM:
		error = i386_set_ioperm(td, &kargs.iargs);
		break;
	case I386_VM86:
		error = vm86_sysarch(td, uap->parms);
		break;
	case I386_GET_FSBASE:
		sdp = &td->td_pcb->pcb_fsd;
		base = sdp->sd_hibase << 24 | sdp->sd_lobase;
		error = copyout(&base, uap->parms, sizeof(base));
		break;
	case I386_SET_FSBASE:
		error = copyin(uap->parms, &base, sizeof(base));
		if (error == 0) {
			/*
			 * Construct the special descriptor for fsbase
			 * and arrange for doreti to load its selector
			 * soon enough.
			 */
			set_fsbase(td, base);
			td->td_frame->tf_fs = GSEL(GUFS_SEL, SEL_UPL);
		}
		break;
	case I386_GET_GSBASE:
		sdp = &td->td_pcb->pcb_gsd;
		base = sdp->sd_hibase << 24 | sdp->sd_lobase;
		error = copyout(&base, uap->parms, sizeof(base));
		break;
	case I386_SET_GSBASE:
		error = copyin(uap->parms, &base, sizeof(base));
		if (error == 0) {
			/*
			 * Construct the special descriptor for gsbase.
			 * The selector is loaded immediately, since we
			 * normally only reload %gs on context switches.
			 */
			set_gsbase(td, base);
			load_gs(GSEL(GUGS_SEL, SEL_UPL));
		}
		break;
	case I386_GET_XFPUSTATE:
		if (kargs.xfpu.len > cpu_max_ext_state_size -
		    sizeof(union savefpu))
			return (EINVAL);
		npxgetregs(td);
		error = copyout((char *)(get_pcb_user_save_td(td) + 1),
		    kargs.xfpu.addr, kargs.xfpu.len);
		break;
	default:
		error = EINVAL;
		break;
	}
	return (error);
}

int
i386_extend_pcb(struct thread *td)
{
	int i, offset;
	u_long *addr;
	struct pcb_ext *ext;
	struct soft_segment_descriptor ssd = {
		0,			/* segment base address (overwritten) */
		ctob(IOPAGES + 1) - 1,	/* length */
		SDT_SYS386TSS,		/* segment type */
		0,			/* priority level */
		1,			/* descriptor present */
		0, 0,
		0,			/* default 32 size */
		0			/* granularity */
	};

	ext = pmap_trm_alloc(ctob(IOPAGES + 1), M_WAITOK | M_ZERO);
	/* -16 is so we can convert a trapframe into vm86trapframe inplace */
	ext->ext_tss.tss_ss0 = GSEL(GDATA_SEL, SEL_KPL);
	/*
	 * The last byte of the i/o map must be followed by an 0xff byte.
	 * We arbitrarily allocate 16 bytes here, to keep the starting
	 * address on a doubleword boundary.
	 */
	offset = PAGE_SIZE - 16;
	ext->ext_tss.tss_ioopt = 
	    (offset - ((unsigned)&ext->ext_tss - (unsigned)ext)) << 16;
	ext->ext_iomap = (caddr_t)ext + offset;
	ext->ext_vm86.vm86_intmap = (caddr_t)ext + offset - 32;

	addr = (u_long *)ext->ext_vm86.vm86_intmap;
	for (i = 0; i < (ctob(IOPAGES) + 32 + 16) / sizeof(u_long); i++)
		*addr++ = ~0;

	ssd.ssd_base = (unsigned)&ext->ext_tss;
	ssd.ssd_limit -= ((unsigned)&ext->ext_tss - (unsigned)ext);
	ssdtosd(&ssd, &ext->ext_tssd);

	KASSERT(td == curthread, ("giving TSS to !curthread"));
	KASSERT(td->td_pcb->pcb_ext == 0, ("already have a TSS!"));

	/* Switch to the new TSS. */
	critical_enter();
	ext->ext_tss.tss_esp0 = PCPU_GET(trampstk);
	td->td_pcb->pcb_ext = ext;
	PCPU_SET(private_tss, 1);
	*PCPU_GET(tss_gdt) = ext->ext_tssd;
	ltr(GSEL(GPROC0_SEL, SEL_KPL));
	critical_exit();

	return 0;
}

int
i386_set_ioperm(td, uap)
	struct thread *td;
	struct i386_ioperm_args *uap;
{
	char *iomap;
	u_int i;
	int error;

	if ((error = priv_check(td, PRIV_IO)) != 0)
		return (error);
	if ((error = securelevel_gt(td->td_ucred, 0)) != 0)
		return (error);
	/*
	 * XXX 
	 * While this is restricted to root, we should probably figure out
	 * whether any other driver is using this i/o address, as so not to
	 * cause confusion.  This probably requires a global 'usage registry'.
	 */

	if (td->td_pcb->pcb_ext == 0)
		if ((error = i386_extend_pcb(td)) != 0)
			return (error);
	iomap = (char *)td->td_pcb->pcb_ext->ext_iomap;

	if (uap->start > uap->start + uap->length ||
	    uap->start + uap->length > IOPAGES * PAGE_SIZE * NBBY)
		return (EINVAL);

	for (i = uap->start; i < uap->start + uap->length; i++) {
		if (uap->enable)
			iomap[i >> 3] &= ~(1 << (i & 7));
		else
			iomap[i >> 3] |= (1 << (i & 7));
	}
	return (error);
}

int
i386_get_ioperm(td, uap)
	struct thread *td;
	struct i386_ioperm_args *uap;
{
	int i, state;
	char *iomap;

	if (uap->start >= IOPAGES * PAGE_SIZE * NBBY)
		return (EINVAL);

	if (td->td_pcb->pcb_ext == 0) {
		uap->length = 0;
		goto done;
	}

	iomap = (char *)td->td_pcb->pcb_ext->ext_iomap;

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
 * current process. Manage dt_lock holding/unholding autonomously.
 */   
static void
set_user_ldt_locked(struct mdproc *mdp)
{
	struct proc_ldt *pldt;
	int gdt_idx;

	mtx_assert(&dt_lock, MA_OWNED);

	pldt = mdp->md_ldt;
	gdt_idx = GUSERLDT_SEL;
	gdt_idx += PCPU_GET(cpuid) * NGDT;	/* always 0 on UP */
	gdt[gdt_idx].sd = pldt->ldt_sd;
	lldt(GSEL(GUSERLDT_SEL, SEL_KPL));
	PCPU_SET(currentldt, GSEL(GUSERLDT_SEL, SEL_KPL));
}

void
set_user_ldt(struct mdproc *mdp)
{

	mtx_lock_spin(&dt_lock);
	set_user_ldt_locked(mdp);
	mtx_unlock_spin(&dt_lock);
}

#ifdef SMP
static void
set_user_ldt_rv(void *arg)
{
	struct proc *p;

	p = curproc;
	if (arg == p->p_vmspace)
		set_user_ldt(&p->p_md);
}
#endif

/*
 * dt_lock must be held. Returns with dt_lock held.
 */
struct proc_ldt *
user_ldt_alloc(struct mdproc *mdp, int len)
{
	struct proc_ldt *pldt, *new_ldt;

	mtx_assert(&dt_lock, MA_OWNED);
	mtx_unlock_spin(&dt_lock);
	new_ldt = malloc(sizeof(struct proc_ldt), M_SUBPROC, M_WAITOK);

	new_ldt->ldt_len = len = NEW_MAX_LD(len);
	new_ldt->ldt_base = pmap_trm_alloc(len * sizeof(union descriptor),
	    M_WAITOK | M_ZERO);
	new_ldt->ldt_refcnt = 1;
	new_ldt->ldt_active = 0;

	mtx_lock_spin(&dt_lock);
	gdt_segs[GUSERLDT_SEL].ssd_base = (unsigned)new_ldt->ldt_base;
	gdt_segs[GUSERLDT_SEL].ssd_limit = len * sizeof(union descriptor) - 1;
	ssdtosd(&gdt_segs[GUSERLDT_SEL], &new_ldt->ldt_sd);

	if ((pldt = mdp->md_ldt) != NULL) {
		if (len > pldt->ldt_len)
			len = pldt->ldt_len;
		bcopy(pldt->ldt_base, new_ldt->ldt_base,
		    len * sizeof(union descriptor));
	} else
		bcopy(ldt, new_ldt->ldt_base, sizeof(union descriptor) * NLDT);
	
	return (new_ldt);
}

/*
 * Must be called with dt_lock held.  Returns with dt_lock unheld.
 */
void
user_ldt_free(struct thread *td)
{
	struct mdproc *mdp;
	struct proc_ldt *pldt;

	mtx_assert(&dt_lock, MA_OWNED);
	mdp = &td->td_proc->p_md;
	if ((pldt = mdp->md_ldt) == NULL) {
		mtx_unlock_spin(&dt_lock);
		return;
	}

	if (td == curthread) {
		lldt(_default_ldt);
		PCPU_SET(currentldt, _default_ldt);
	}

	mdp->md_ldt = NULL;
	user_ldt_deref(pldt);
}

void
user_ldt_deref(struct proc_ldt *pldt)
{

	mtx_assert(&dt_lock, MA_OWNED);
	if (--pldt->ldt_refcnt == 0) {
		mtx_unlock_spin(&dt_lock);
		pmap_trm_free(pldt->ldt_base, pldt->ldt_len *
		    sizeof(union descriptor));
		free(pldt, M_SUBPROC);
	} else
		mtx_unlock_spin(&dt_lock);
}

/*
 * Note for the authors of compat layers (linux, etc): copyout() in
 * the function below is not a problem since it presents data in
 * arch-specific format (i.e. i386-specific in this case), not in
 * the OS-specific one.
 */
int
i386_get_ldt(struct thread *td, struct i386_ldt_args *uap)
{
	struct proc_ldt *pldt;
	char *data;
	u_int nldt, num;
	int error;

#ifdef DEBUG
	printf("i386_get_ldt: start=%u num=%u descs=%p\n",
	    uap->start, uap->num, (void *)uap->descs);
#endif

	num = min(uap->num, MAX_LD);
	data = malloc(num * sizeof(union descriptor), M_TEMP, M_WAITOK);
	mtx_lock_spin(&dt_lock);
	pldt = td->td_proc->p_md.md_ldt;
	nldt = pldt != NULL ? pldt->ldt_len : NLDT;
	if (uap->start >= nldt) {
		num = 0;
	} else {
		num = min(num, nldt - uap->start);
		bcopy(pldt != NULL ?
		    &((union descriptor *)(pldt->ldt_base))[uap->start] :
		    &ldt[uap->start], data, num * sizeof(union descriptor));
	}
	mtx_unlock_spin(&dt_lock);
	error = copyout(data, uap->descs, num * sizeof(union descriptor));
	if (error == 0)
		td->td_retval[0] = num;
	free(data, M_TEMP);
	return (error);
}

int
i386_set_ldt(struct thread *td, struct i386_ldt_args *uap,
    union descriptor *descs)
{
	struct mdproc *mdp;
	struct proc_ldt *pldt;
	union descriptor *dp;
	u_int largest_ld, i;
	int error;

#ifdef DEBUG
	printf("i386_set_ldt: start=%u num=%u descs=%p\n",
	    uap->start, uap->num, (void *)uap->descs);
#endif
	error = 0;
	mdp = &td->td_proc->p_md;

	if (descs == NULL) {
		/* Free descriptors */
		if (uap->start == 0 && uap->num == 0) {
			/*
			 * Treat this as a special case, so userland needn't
			 * know magic number NLDT.
			 */
			uap->start = NLDT;
			uap->num = MAX_LD - NLDT;
		}
		mtx_lock_spin(&dt_lock);
		if ((pldt = mdp->md_ldt) == NULL ||
		    uap->start >= pldt->ldt_len) {
			mtx_unlock_spin(&dt_lock);
			return (0);
		}
		largest_ld = uap->start + uap->num;
		if (largest_ld > pldt->ldt_len)
			largest_ld = pldt->ldt_len;
		for (i = uap->start; i < largest_ld; i++)
			atomic_store_rel_64(&((uint64_t *)(pldt->ldt_base))[i],
			    0);
		mtx_unlock_spin(&dt_lock);
		return (0);
	}

	if (uap->start != LDT_AUTO_ALLOC || uap->num != 1) {
		/* verify range of descriptors to modify */
		largest_ld = uap->start + uap->num;
		if (uap->start >= MAX_LD || largest_ld > MAX_LD)
			return (EINVAL);
	}

	/* Check descriptors for access violations */
	for (i = 0; i < uap->num; i++) {
		dp = &descs[i];

		switch (dp->sd.sd_type) {
		case SDT_SYSNULL:	/* system null */ 
			dp->sd.sd_p = 0;
			break;
		case SDT_SYS286TSS: /* system 286 TSS available */
		case SDT_SYSLDT:    /* system local descriptor table */
		case SDT_SYS286BSY: /* system 286 TSS busy */
		case SDT_SYSTASKGT: /* system task gate */
		case SDT_SYS286IGT: /* system 286 interrupt gate */
		case SDT_SYS286TGT: /* system 286 trap gate */
		case SDT_SYSNULL2:  /* undefined by Intel */ 
		case SDT_SYS386TSS: /* system 386 TSS available */
		case SDT_SYSNULL3:  /* undefined by Intel */
		case SDT_SYS386BSY: /* system 386 TSS busy */
		case SDT_SYSNULL4:  /* undefined by Intel */ 
		case SDT_SYS386IGT: /* system 386 interrupt gate */
		case SDT_SYS386TGT: /* system 386 trap gate */
		case SDT_SYS286CGT: /* system 286 call gate */ 
		case SDT_SYS386CGT: /* system 386 call gate */
			return (EACCES);

		/* memory segment types */
		case SDT_MEMEC:   /* memory execute only conforming */
		case SDT_MEMEAC:  /* memory execute only accessed conforming */
		case SDT_MEMERC:  /* memory execute read conforming */
		case SDT_MEMERAC: /* memory execute read accessed conforming */
			 /* Must be "present" if executable and conforming. */
			if (dp->sd.sd_p == 0)
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
			return (EINVAL);
		}

		/* Only user (ring-3) descriptors may be present. */
		if (dp->sd.sd_p != 0 && dp->sd.sd_dpl != SEL_UPL)
			return (EACCES);
	}

	if (uap->start == LDT_AUTO_ALLOC && uap->num == 1) {
		/* Allocate a free slot */
		mtx_lock_spin(&dt_lock);
		if ((pldt = mdp->md_ldt) == NULL) {
			if ((error = i386_ldt_grow(td, NLDT + 1))) {
				mtx_unlock_spin(&dt_lock);
				return (error);
			}
			pldt = mdp->md_ldt;
		}
again:
		/*
		 * start scanning a bit up to leave room for NVidia and
		 * Wine, which still user the "Blat" method of allocation.
		 */
		dp = &((union descriptor *)(pldt->ldt_base))[NLDT];
		for (i = NLDT; i < pldt->ldt_len; ++i) {
			if (dp->sd.sd_type == SDT_SYSNULL)
				break;
			dp++;
		}
		if (i >= pldt->ldt_len) {
			if ((error = i386_ldt_grow(td, pldt->ldt_len+1))) {
				mtx_unlock_spin(&dt_lock);
				return (error);
			}
			goto again;
		}
		uap->start = i;
		error = i386_set_ldt_data(td, i, 1, descs);
		mtx_unlock_spin(&dt_lock);
	} else {
		largest_ld = uap->start + uap->num;
		mtx_lock_spin(&dt_lock);
		if (!(error = i386_ldt_grow(td, largest_ld))) {
			error = i386_set_ldt_data(td, uap->start, uap->num,
			    descs);
		}
		mtx_unlock_spin(&dt_lock);
	}
	if (error == 0)
		td->td_retval[0] = uap->start;
	return (error);
}

static int
i386_set_ldt_data(struct thread *td, int start, int num,
    union descriptor *descs)
{
	struct mdproc *mdp;
	struct proc_ldt *pldt;
	uint64_t *dst, *src;
	int i;

	mtx_assert(&dt_lock, MA_OWNED);

	mdp = &td->td_proc->p_md;
	pldt = mdp->md_ldt;
	dst = (uint64_t *)(pldt->ldt_base);
	src = (uint64_t *)descs;

	/*
	 * Atomic(9) is used only to get 64bit atomic store with
	 * cmpxchg8b when available.  There is no op without release
	 * semantic.
	 */
	for (i = 0; i < num; i++)
		atomic_store_rel_64(&dst[start + i], src[i]);
	return (0);
}

static int
i386_ldt_grow(struct thread *td, int len) 
{
	struct mdproc *mdp;
	struct proc_ldt *new_ldt, *pldt;
	caddr_t old_ldt_base;
	int old_ldt_len;

	mtx_assert(&dt_lock, MA_OWNED);

	if (len > MAX_LD)
		return (ENOMEM);
	if (len < NLDT + 1)
		len = NLDT + 1;

	mdp = &td->td_proc->p_md;
	old_ldt_base = NULL_LDT_BASE;
	old_ldt_len = 0;

	/* Allocate a user ldt. */
	if ((pldt = mdp->md_ldt) == NULL || len > pldt->ldt_len) {
		new_ldt = user_ldt_alloc(mdp, len);
		if (new_ldt == NULL)
			return (ENOMEM);
		pldt = mdp->md_ldt;

		if (pldt != NULL) {
			if (new_ldt->ldt_len <= pldt->ldt_len) {
				/*
				 * We just lost the race for allocation, so
				 * free the new object and return.
				 */
				mtx_unlock_spin(&dt_lock);
				pmap_trm_free(new_ldt->ldt_base,
				   new_ldt->ldt_len * sizeof(union descriptor));
				free(new_ldt, M_SUBPROC);
				mtx_lock_spin(&dt_lock);
				return (0);
			}

			/*
			 * We have to substitute the current LDT entry for
			 * curproc with the new one since its size grew.
			 */
			old_ldt_base = pldt->ldt_base;
			old_ldt_len = pldt->ldt_len;
			pldt->ldt_sd = new_ldt->ldt_sd;
			pldt->ldt_base = new_ldt->ldt_base;
			pldt->ldt_len = new_ldt->ldt_len;
		} else
			mdp->md_ldt = pldt = new_ldt;
#ifdef SMP
		/*
		 * Signal other cpus to reload ldt.  We need to unlock dt_lock
		 * here because other CPU will contest on it since their
		 * curthreads won't hold the lock and will block when trying
		 * to acquire it.
		 */
		mtx_unlock_spin(&dt_lock);
		smp_rendezvous(NULL, set_user_ldt_rv, NULL,
		    td->td_proc->p_vmspace);
#else
		set_user_ldt_locked(&td->td_proc->p_md);
		mtx_unlock_spin(&dt_lock);
#endif
		if (old_ldt_base != NULL_LDT_BASE) {
			pmap_trm_free(old_ldt_base, old_ldt_len *
			    sizeof(union descriptor));
			free(new_ldt, M_SUBPROC);
		}
		mtx_lock_spin(&dt_lock);
	}
	return (0);
}
