/*	$OpenBSD: uvm_meter.c,v 1.54 2025/06/12 20:37:59 deraadt Exp $	*/
/*	$NetBSD: uvm_meter.c,v 1.21 2001/07/14 06:36:03 matt Exp $	*/

/*
 * Copyright (c) 1997 Charles D. Cranor and Washington University.
 * Copyright (c) 1982, 1986, 1989, 1993
 *      The Regents of the University of California.
 *
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
 *      @(#)vm_meter.c  8.4 (Berkeley) 1/4/94
 * from: Id: uvm_meter.c,v 1.1.2.1 1997/08/14 19:10:35 chuck Exp
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/percpu.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/vmmeter.h>
#include <uvm/uvm.h>
#include <uvm/uvm_ddb.h>

#ifdef UVM_SWAP_ENCRYPT
#include <uvm/uvm_swap.h>
#include <uvm/uvm_swap_encrypt.h>
#endif

/*
 * The time for a process to be blocked before being very swappable.
 * This is a number of seconds which the system takes as being a non-trivial
 * amount of real time.  You probably shouldn't change this;
 * it is used in subtle ways (fractions and multiples of it are, that is, like
 * half of a ``long time'', almost a long time, etc.)
 * It is related to human patience and other factors which don't really
 * change over time.
 */
#define	MAXSLP	20

int maxslp = MAXSLP;	/* patchable ... */

extern struct loadavg averunnable;

void uvm_total(struct vmtotal *);
void uvmexp_read(struct uvmexp *);

char malloc_conf[16];

#ifndef SMALL_KERNEL
/*
 * uvm_sysctl: sysctl hook into UVM system.
 */
int
uvm_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen, struct proc *p)
{
	struct process *pr = p->p_p;
	struct vmtotal vmtotals;
	struct uvmexp uexp;
	int rv, t;

	switch (name[0]) {
	case VM_SWAPENCRYPT:
#ifdef UVM_SWAP_ENCRYPT
		return (swap_encrypt_ctl(name + 1, namelen - 1, oldp, oldlenp,
					 newp, newlen, p));
#else
		return (EOPNOTSUPP);
#endif
	default:
		/* all sysctl names at this level are terminal */
		if (namelen != 1)
			return (ENOTDIR);		/* overloaded */
		break;
	}

	switch (name[0]) {
	case VM_LOADAVG:
		return (sysctl_rdstruct(oldp, oldlenp, newp, &averunnable,
		    sizeof(averunnable)));

	case VM_METER:
		uvm_total(&vmtotals);
		return (sysctl_rdstruct(oldp, oldlenp, newp, &vmtotals,
		    sizeof(vmtotals)));

	case VM_UVMEXP:
		uvmexp_read(&uexp);
		return (sysctl_rdstruct(oldp, oldlenp, newp, &uexp,
		    sizeof(uexp)));

	case VM_NKMEMPAGES:
		return (sysctl_rdint(oldp, oldlenp, newp, nkmempages));

	case VM_PSSTRINGS:
		return (sysctl_rdstruct(oldp, oldlenp, newp, &pr->ps_strings,
		    sizeof(pr->ps_strings)));

	case VM_ANONMIN:
		t = uvmexp.anonminpct;
		rv = sysctl_int(oldp, oldlenp, newp, newlen, &t);
		if (rv) {
			return rv;
		}
		if (t + uvmexp.vtextminpct + uvmexp.vnodeminpct > 95 || t < 0) {
			return EINVAL;
		}
		uvmexp.anonminpct = t;
		uvmexp.anonmin = t * 256 / 100;
		return rv;

	case VM_VTEXTMIN:
		t = uvmexp.vtextminpct;
		rv = sysctl_int(oldp, oldlenp, newp, newlen, &t);
		if (rv) {
			return rv;
		}
		if (uvmexp.anonminpct + t + uvmexp.vnodeminpct > 95 || t < 0) {
			return EINVAL;
		}
		uvmexp.vtextminpct = t;
		uvmexp.vtextmin = t * 256 / 100;
		return rv;

	case VM_VNODEMIN:
		t = uvmexp.vnodeminpct;
		rv = sysctl_int(oldp, oldlenp, newp, newlen, &t);
		if (rv) {
			return rv;
		}
		if (uvmexp.anonminpct + uvmexp.vtextminpct + t > 95 || t < 0) {
			return EINVAL;
		}
		uvmexp.vnodeminpct = t;
		uvmexp.vnodemin = t * 256 / 100;
		return rv;

	case VM_MAXSLP:
		return (sysctl_rdint(oldp, oldlenp, newp, maxslp));

	case VM_USPACE:
		return (sysctl_rdint(oldp, oldlenp, newp, USPACE));

	case VM_MALLOC_CONF:
		return (sysctl_string(oldp, oldlenp, newp, newlen,
		    malloc_conf, sizeof(malloc_conf)));
	default:
		return (EOPNOTSUPP);
	}
	/* NOTREACHED */
}

/*
 * uvm_total: calculate the current state of the system.
 */
void
uvm_total(struct vmtotal *totalp)
{
	struct proc *p;
#if 0
	struct vm_map_entry *	entry;
	struct vm_map *map;
	int paging;
#endif

	memset(totalp, 0, sizeof *totalp);

	/* calculate process statistics */
	LIST_FOREACH(p, &allproc, p_list) {
		switch (p->p_stat) {
		case 0:
			continue;

		case SSLEEP:
		case SSTOP:
			totalp->t_sl++;
			break;
		case SRUN:
		case SONPROC:
			if (p == p->p_cpu->ci_schedstate.spc_idleproc)
				continue;
		/* FALLTHROUGH */
		case SIDL:
			totalp->t_rq++;
			if (p->p_stat == SIDL)
				continue;
			break;
		}
		/*
		 * note active objects
		 */
#if 0
		/*
		 * XXXCDC: BOGUS!  rethink this.   in the mean time
		 * don't do it.
		 */
		paging = 0;
		vm_map_lock(map);
		for (map = &p->p_vmspace->vm_map, entry = map->header.next;
		    entry != &map->header; entry = entry->next) {
			if (entry->is_a_map || entry->is_sub_map ||
			    entry->object.uvm_obj == NULL)
				continue;
			/* XXX how to do this with uvm */
		}
		vm_map_unlock(map);
		if (paging)
			totalp->t_pw++;
#endif
	}
	/*
	 * Calculate object memory usage statistics.
	 */
	totalp->t_free = uvmexp.free;
	totalp->t_vm = uvmexp.npages - uvmexp.free + uvmexp.swpginuse;
	totalp->t_avm = uvmexp.active + uvmexp.swpginuse;	/* XXX */
	totalp->t_rm = uvmexp.npages - uvmexp.free;
	totalp->t_arm = uvmexp.active;
	totalp->t_vmshr = 0;		/* XXX */
	totalp->t_avmshr = 0;		/* XXX */
	totalp->t_rmshr = 0;		/* XXX */
	totalp->t_armshr = 0;		/* XXX */
}

void
uvmexp_read(struct uvmexp *uexp)
{
	uint64_t counters[exp_ncounters], scratch[exp_ncounters];

	memcpy(uexp, &uvmexp, sizeof(*uexp));

	counters_read(uvmexp_counters, counters, exp_ncounters, scratch);

	/* stat counters */
	uexp->faults = (int)counters[faults];
	uexp->pageins = (int)counters[pageins];

	/* fault subcounters */
	uexp->fltnoram = (int)counters[flt_noram];
	uexp->fltnoanon = (int)counters[flt_noanon];
	uexp->fltnoamap = (int)counters[flt_noamap];
	uexp->fltpgwait = (int)counters[flt_pgwait];
	uexp->fltpgrele = (int)counters[flt_pgrele];
	uexp->fltrelck = (int)counters[flt_relck];
	uexp->fltnorelck = (int)counters[flt_norelck];
	uexp->fltanget = (int)counters[flt_anget];
	uexp->fltanretry = (int)counters[flt_anretry];
	uexp->fltamcopy = (int)counters[flt_amcopy];
	uexp->fltnamap = (int)counters[flt_namap];
	uexp->fltnomap = (int)counters[flt_nomap];
	uexp->fltlget = (int)counters[flt_lget];
	uexp->fltget = (int)counters[flt_get];
	uexp->flt_anon = (int)counters[flt_anon];
	uexp->flt_acow = (int)counters[flt_acow];
	uexp->flt_obj = (int)counters[flt_obj];
	uexp->flt_prcopy = (int)counters[flt_prcopy];
	uexp->flt_przero = (int)counters[flt_przero];
	uexp->fltup = (int)counters[flt_up];
	uexp->fltnoup = (int)counters[flt_noup];
}
#endif /* SMALL_KERNEL */

#ifdef DDB

/*
 * uvmexp_print: ddb hook to print interesting uvm counters
 */
void
uvmexp_print(int (*pr)(const char *, ...))
{
	struct uvmexp uexp;

	uvmexp_read(&uexp);

	(*pr)("Current UVM status:\n");
	(*pr)("  pagesize=%d (0x%x), pagemask=0x%x, pageshift=%d\n",
	    uexp.pagesize, uexp.pagesize, uexp.pagemask,
	    uexp.pageshift);
	(*pr)("  %d VM pages: %d active, %d inactive, %d wired, %d free (%d zero)\n",
	    uexp.npages, uexp.active, uexp.inactive, uexp.wired,
	    uexp.free, uexp.zeropages);
	(*pr)("  freemin=%d, free-target=%d, inactive-target=%d, "
	    "wired-max=%d\n", uexp.freemin, uexp.freetarg, uexp.inactarg,
	    uexp.wiredmax);
	(*pr)("  faults=%d, traps=%d, intrs=%d, ctxswitch=%d fpuswitch=%d\n",
	    uexp.faults, uexp.traps, uexp.intrs, uexp.swtch,
	    uexp.fpswtch);
	(*pr)("  softint=%d, syscalls=%d, kmapent=%d\n",
	    uexp.softs, uexp.syscalls, uexp.kmapent);

	(*pr)("  fault counts:\n");
	(*pr)("    noram=%d, noanon=%d, noamap=%d, pgwait=%d, pgrele=%d\n",
	    uexp.fltnoram, uexp.fltnoanon, uexp.fltnoamap,
	    uexp.fltpgwait, uexp.fltpgrele);
	(*pr)("    relocks=%d(%d), upgrades=%d(%d) anget(retries)=%d(%d), "
	    "amapcopy=%d\n", uexp.fltrelck, uexp.fltnorelck, uexp.fltup,
	    uexp.fltnoup, uexp.fltanget, uexp.fltanretry, uexp.fltamcopy);
	(*pr)("    neighbor anon/obj pg=%d/%d, gets(lock/unlock)=%d/%d\n",
	    uexp.fltnamap, uexp.fltnomap, uexp.fltlget, uexp.fltget);
	(*pr)("    cases: anon=%d, anoncow=%d, obj=%d, prcopy=%d, przero=%d\n",
	    uexp.flt_anon, uexp.flt_acow, uexp.flt_obj, uexp.flt_prcopy,
	    uexp.flt_przero);

	(*pr)("  daemon and swap counts:\n");
	(*pr)("    woke=%d, revs=%d, scans=%d, obscans=%d, anscans=%d\n",
	    uexp.pdwoke, uexp.pdrevs, uexp.pdscans, uexp.pdobscan,
	    uexp.pdanscan);
	(*pr)("    busy=%d, freed=%d, reactivate=%d, deactivate=%d\n",
	    uexp.pdbusy, uexp.pdfreed, uexp.pdreact, uexp.pddeact);
	(*pr)("    pageouts=%d, pending=%d, nswget=%d\n", uexp.pdpageouts,
	    uexp.pdpending, uexp.nswget);
	(*pr)("    nswapdev=%d\n",
	    uexp.nswapdev);
	(*pr)("    swpages=%d, swpginuse=%d, swpgonly=%d paging=%d\n",
	    uexp.swpages, uexp.swpginuse, uexp.swpgonly, uexp.paging);

	(*pr)("  kernel pointers:\n");
	(*pr)("    objs(kern)=%p\n", uvm.kernel_object);
}
#endif
