/*	$OpenBSD: subr_prof.c,v 1.43 2025/08/15 04:21:00 guenther Exp $	*/
/*	$NetBSD: subr_prof.c,v 1.12 1996/04/22 01:38:50 christos Exp $	*/

/*-
 * Copyright (c) 1982, 1986, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)subr_prof.c	8.3 (Berkeley) 9/23/93
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/atomic.h>
#include <sys/clockintr.h>
#include <sys/namei.h>
#include <sys/vnode.h>
#include <sys/pledge.h>
#include <sys/stat.h>
#include <sys/proc.h>
#include <sys/pool.h>
#include <sys/file.h>
#include <sys/fcntl.h>
#include <sys/filedesc.h>
#include <sys/resourcevar.h>
#include <sys/mount.h>
#include <sys/sysctl.h>
#include <sys/syscallargs.h>
#include <sys/user.h>
#include <sys/gmon.h>

uint64_t profclock_period;

#if defined(GPROF) || defined(DDBPROF)
#include <sys/malloc.h>

#include <uvm/uvm_extern.h>

#include <machine/db_machdep.h>
#include <ddb/db_extern.h>

/*
 * Flag to prevent CPUs from executing the mcount() monitor function
 * until we're sure they are in a sane state.
 */
int gmoninit = 0;
u_int gmon_cpu_count;		/* [K] number of CPUs with profiling enabled */

extern char etext[];

void gmonclock(struct clockrequest *, void *, void *);

void
prof_init(void)
{
	CPU_INFO_ITERATOR cii;
	struct cpu_info *ci;
	struct gmonparam *p;
	u_long lowpc, highpc, textsize;
	u_long kcountsize, fromssize, tossize;
	long tolimit;
	char *cp;
	int size;

	/*
	 * Round lowpc and highpc to multiples of the density we're using
	 * so the rest of the scaling (here and in gprof) stays in ints.
	 */
	lowpc = ROUNDDOWN(KERNBASE, HISTFRACTION * sizeof(HISTCOUNTER));
	highpc = ROUNDUP((u_long)etext, HISTFRACTION * sizeof(HISTCOUNTER));
	textsize = highpc - lowpc;
#ifdef GPROF
	printf("Profiling kernel, textsize=%ld [%lx..%lx]\n",
	    textsize, lowpc, highpc);
#endif
	kcountsize = textsize / HISTFRACTION;
	fromssize = textsize / HASHFRACTION;
	tolimit = textsize * ARCDENSITY / 100;
	if (tolimit < MINARCS)
		tolimit = MINARCS;
	else if (tolimit > MAXARCS)
		tolimit = MAXARCS;
	tossize = tolimit * sizeof(struct tostruct);
	size = sizeof(*p) + kcountsize + fromssize + tossize;

	/* Allocate and initialize one profiling buffer per CPU. */
	CPU_INFO_FOREACH(cii, ci) {
		cp = km_alloc(round_page(size), &kv_any, &kp_zero, &kd_nowait);
		if (cp == NULL) {
			printf("No memory for profiling.\n");
			return;
		}

		clockintr_bind(&ci->ci_gmonclock, ci, gmonclock, NULL);
		clockintr_stagger(&ci->ci_gmonclock, profclock_period,
		    CPU_INFO_UNIT(ci), MAXCPUS);

		p = (struct gmonparam *)cp;
		cp += sizeof(*p);
		p->tos = (struct tostruct *)cp;
		cp += tossize;
		p->kcount = (u_short *)cp;
		cp += kcountsize;
		p->froms = (u_short *)cp;

		p->state = GMON_PROF_OFF;
		p->lowpc = lowpc;
		p->highpc = highpc;
		p->textsize = textsize;
		p->hashfraction = HASHFRACTION;
		p->kcountsize = kcountsize;
		p->fromssize = fromssize;
		p->tolimit = tolimit;
		p->tossize = tossize;

		ci->ci_gmon = p;
	}
}

int
prof_state_toggle(struct cpu_info *ci, int oldstate)
{
	struct gmonparam *gp = ci->ci_gmon;
	int error = 0;

	KERNEL_ASSERT_LOCKED();

	if (gp->state == oldstate)
		return (0);

	switch (gp->state) {
	case GMON_PROF_ON:
#if !defined(GPROF)
		/*
		 * If this is not a profiling kernel, we need to patch
		 * all symbols that can be instrumented.
		 */
		error = db_prof_enable();
#endif
		if (error == 0) {
			if (++gmon_cpu_count == 1)
				startprofclock(&process0);
			clockintr_advance(&ci->ci_gmonclock, profclock_period);
		}
		break;
	default:
		error = EINVAL;
		gp->state = GMON_PROF_OFF;
		/* FALLTHROUGH */
	case GMON_PROF_OFF:
		clockintr_cancel(&ci->ci_gmonclock);
		if (--gmon_cpu_count == 0)
			stopprofclock(&process0);
#if !defined(GPROF)
		db_prof_disable();
#endif
		break;
	}

	return (error);
}

/*
 * Return kernel profiling information.
 */
int
sysctl_doprof(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen)
{
	CPU_INFO_ITERATOR cii;
	struct cpu_info *ci;
	struct gmonparam *gp = NULL;
	int error, cpuid, op, state;

	/* all sysctl names at this level are name and field */
	if (namelen != 2)
		return (ENOTDIR);		/* overloaded */

	op = name[0];
	cpuid = name[1];

	CPU_INFO_FOREACH(cii, ci) {
		if (cpuid == CPU_INFO_UNIT(ci)) {
			gp = ci->ci_gmon;
			break;
		}
	}

	if (gp == NULL)
		return (EOPNOTSUPP);

	/* Assume that if we're here it is safe to execute profiling. */
	gmoninit = 1;

	switch (op) {
	case GPROF_STATE:
		state = gp->state;
		error = sysctl_int(oldp, oldlenp, newp, newlen, &gp->state);
		if (error)
			return (error);
		return prof_state_toggle(ci, state);
	case GPROF_COUNT:
		return (sysctl_struct(oldp, oldlenp, newp, newlen,
		    gp->kcount, gp->kcountsize));
	case GPROF_FROMS:
		return (sysctl_struct(oldp, oldlenp, newp, newlen,
		    gp->froms, gp->fromssize));
	case GPROF_TOS:
		return (sysctl_struct(oldp, oldlenp, newp, newlen,
		    gp->tos, gp->tossize));
	case GPROF_GMONPARAM:
		return (sysctl_rdstruct(oldp, oldlenp, newp, gp, sizeof *gp));
	default:
		return (EOPNOTSUPP);
	}
	/* NOTREACHED */
}

void
gmonclock(struct clockrequest *cr, void *cf, void *arg)
{
	uint64_t count;
	struct clockframe *frame = cf;
	struct gmonparam *g = curcpu()->ci_gmon;
	u_long i;

	count = clockrequest_advance(cr, profclock_period);
	if (count > ULONG_MAX)
		count = ULONG_MAX;

	/*
	 * Kernel statistics are just like addupc_intr(), only easier.
	 */
	if (!CLKF_USERMODE(frame) && g != NULL && g->state == GMON_PROF_ON) {
		i = CLKF_PC(frame) - g->lowpc;
		if (i < g->textsize) {
			i /= HISTFRACTION * sizeof(*g->kcount);
			g->kcount[i] += (u_long)count;
		}
	}
}

#endif /* GPROF || DDBPROF */

/*
 * Profiling system call.
 *
 * The scale factor is a fixed point number with 16 bits of fraction, so that
 * 1.0 is represented as 0x10000.  A scale factor of 0 turns off profiling.
 */
int
sys_profil(struct proc *p, void *v, register_t *retval)
{
	struct sys_profil_args /* {
		syscallarg(void *) buf;
		syscallarg(size_t) buflen;
		syscallarg(size_t) samplesize;
		syscallarg(u_long) offset;
		syscallarg(u_int) scale;
		syscallarg(int) dirfd;
	} */ *uap = v;
	struct process *pr = p->p_p;
	struct uprof *upp;
	int s;

	/* Only binaries linked for profiling can do profil() */
	if ((pr->ps_iflags & PSI_PROFILE) == 0)
		return (EPERM);

	if (SCARG(uap, scale) > (1 << 16))
		return (EINVAL);
	if (SCARG(uap, scale) == 0) {
		stopprofclock(pr);
		need_resched(curcpu());
		return (0);
	}
	upp = &pr->ps_prof;

	/* May not change to a new buffer */
	if (upp->pr_buf &&
	    (SCARG(uap, buf) != upp->pr_buf ||
	    SCARG(uap, buflen) != upp->pr_buflen))
		return (EALREADY);

	/* First call determines where the output files will go */
	if (upp->pr_cdir == NULL) {
		struct vnode *dirvp;

		upp->pr_ucred = crhold(p->p_ucred);
		if (SCARG(uap, dirfd) != -1) {
			struct filedesc *fdp = p->p_fd;
			struct file *fp;

			if ((fp = fd_getfile(fdp, SCARG(uap, dirfd))) == NULL)
				return (EBADF);
			dirvp = fp->f_data;
			if (fp->f_type != DTYPE_VNODE || dirvp->v_type != VDIR) {
				FRELE(fp, p);
				return (ENOTDIR);
			}
		} else
			dirvp = p->p_fd->fd_cdir;
		upp->pr_cdir = dirvp;
		vref(upp->pr_cdir);
	}

#if 0
	if (upp->pr_buf == NULL) {
		/* XXX In the future, consider forcing the buffer immutable */
		struct sys_mimmutable_args ua;
		SCARG(&ua, addr) = SCARG(uap, buf);
		SCARG(&ua, len) = SCARG(uap, buflen);
		(void) sys_mimmutable(p, &ua, retval);
	}
#endif

	/* Block profile interrupts while changing state. */
	s = splstatclock();
	upp->pr_off = SCARG(uap, offset);
	upp->pr_scale = SCARG(uap, scale);
	upp->pr_base = (caddr_t)SCARG(uap, buf) + sizeof(struct gmonhdr);
	upp->pr_size = SCARG(uap, samplesize);
	upp->pr_buf = SCARG(uap, buf);
	upp->pr_buflen = SCARG(uap, buflen);
	startprofclock(pr);
	splx(s);
	need_resched(curcpu());

	return (0);
}

void
prof_fork(struct process *pr)
{
	struct uprof *upp = &pr->ps_prof;

	if (upp->pr_cdir)
		vref(upp->pr_cdir);
	if (upp->pr_ucred)
		crhold(upp->pr_ucred);
}

void
prof_exec(struct process *pr)
{
	struct uprof *upp = &pr->ps_prof;

	if (upp->pr_cdir)
		vrele(upp->pr_cdir);
	upp->pr_cdir = NULL;
	if (upp->pr_ucred)
		crfree(upp->pr_ucred);
	upp->pr_ucred = NULL;
	upp->pr_buf = NULL;
}

void
prof_write(struct proc *p)
{
	struct process *pr = p->p_p;
	struct uprof *upp = &pr->ps_prof;
	struct filedesc *fdp = p->p_fd;
	struct vnode *cdir = NULL, *old_cdir, *vp;
	struct ucred *cred = NULL, *old_cred;
	struct nameidata nd;
	struct gmonhdr hdr;
	struct vattr vattr;
	int error;
	size_t size;
	char *name = NULL;

	/* Is this process profiling? */
	if (upp->pr_buf == NULL)
		return;

	cdir = upp->pr_cdir;
	cred = upp->pr_ucred;

	/* Process sets hrd.totarc to indicate space used by the arc tables */
	error = copyin(upp->pr_buf, &hdr, sizeof hdr);
	if (error)
		goto out;
	if (hdr.totarc <= 0)
		goto out;
	size = sizeof(hdr) + upp->pr_size + hdr.totarc;
	if (size > upp->pr_buflen)
		goto out;

	name = pool_get(&namei_pool, PR_WAITOK);
	if (snprintf(name, MAXPATHLEN, "gmon.%s.%d.out",
	    pr->ps_comm, pr->ps_pid) >= MAXPATHLEN)
		goto out;

	/* Swap back to the original directory */
	old_cdir = fdp->fd_cdir;
	fdp->fd_cdir = cdir;
	vrele(old_cdir);
	cdir = NULL;	/* will be released as fdp->fd_cdir */

	/* Swap back to the original cred */
	old_cred = p->p_ucred;
	p->p_ucred = crdup(cred);
	crfree(old_cred);

	NDINIT(&nd, 0, BYPASSUNVEIL | KERNELPATH, UIO_SYSSPACE, name, p);
	error = vn_open(&nd, O_CREAT | FWRITE | O_NOFOLLOW | O_NONBLOCK,
	    S_IRUSR | S_IWUSR);
	if (error)
		goto out;

	/*
	 * Don't dump to non-regular files, files with links, or files
	 * owned by someone else.
	 */
	vp = nd.ni_vp;
	if ((error = VOP_GETATTR(vp, &vattr, cred, p)) != 0) {
		VOP_UNLOCK(vp);
		vn_close(vp, FWRITE, cred, p);
		goto out;
	}
	if (vp->v_type != VREG || vattr.va_nlink != 1 ||
	    vattr.va_mode & ((VREAD | VWRITE) >> 3 | (VREAD | VWRITE) >> 6) ||
	    vattr.va_uid != cred->cr_uid) {
		error = EACCES;
		VOP_UNLOCK(vp);
		vn_close(vp, FWRITE, cred, p);
		goto out;
	}
	vattr_null(&vattr);
	vattr.va_size = 0;
	VOP_SETATTR(vp, &vattr, cred, p);

	VOP_UNLOCK(vp);
	vref(vp);
	error = vn_close(vp, FWRITE, cred, p);
	if (error == 0)
		error = vn_rdwr(UIO_WRITE, vp, upp->pr_buf, size,
		    0, UIO_USERSPACE, IO_UNIT, cred, NULL, p);
	vrele(vp);
out:
	if (name)
		pool_put(&namei_pool, name);
	if (cred)
		crfree(cred);
	if (cdir)
		vrele(cdir);
}

void
profclock(struct clockrequest *cr, void *cf, void *arg)
{
	uint64_t count;
	struct clockframe *frame = cf;
	struct proc *p = curproc;

	count = clockrequest_advance(cr, profclock_period);
	if (count > ULONG_MAX)
		count = ULONG_MAX;

	if (CLKF_USERMODE(frame)) {
		if (ISSET(p->p_p->ps_flags, PS_PROFIL))
			addupc_intr(p, CLKF_PC(frame), (u_long)count);
	} else {
		if (p != NULL && ISSET(p->p_p->ps_flags, PS_PROFIL))
			addupc_intr(p, PROC_PC(p), (u_long)count);
	}
}

/*
 * Scale is a fixed-point number with the binary point 16 bits
 * into the value, and is <= 1.0.  pc is at most 32 bits, so the
 * intermediate result is at most 48 bits.
 */
#define	PC_TO_INDEX(pc, prof) \
	((int)(((u_quad_t)((pc) - (prof)->pr_off) * \
	    (u_quad_t)((prof)->pr_scale)) >> 16) & ~1)

/*
 * Collect user-level profiling statistics; called on a profiling tick,
 * when a process is running in user-mode.  This routine may be called
 * from an interrupt context. Schedule an AST that will vector us to
 * trap() with a context in which copyin and copyout will work.
 * Trap will then call addupc_task().
 */
void
addupc_intr(struct proc *p, u_long pc, u_long nticks)
{
	struct uprof *prof;

	prof = &p->p_p->ps_prof;
	if (pc < prof->pr_off || PC_TO_INDEX(pc, prof) >= prof->pr_size)
		return;			/* out of range; ignore */

	p->p_prof_addr = pc;
	p->p_prof_ticks += nticks;
	atomic_setbits_int(&p->p_flag, P_OWEUPC);
	need_proftick(p);
}


/*
 * Much like before, but we can afford to take faults here.  If the
 * update fails, we simply turn off profiling.
 */
void
addupc_task(struct proc *p, u_long pc, u_int nticks)
{
	struct process *pr = p->p_p;
	struct uprof *prof;
	caddr_t addr;
	u_int i;
	u_short v;

	/* Testing PS_PROFIL may be unnecessary, but is certainly safe. */
	if ((pr->ps_flags & PS_PROFIL) == 0 || nticks == 0)
		return;

	prof = &pr->ps_prof;
	if (pc < prof->pr_off ||
	    (i = PC_TO_INDEX(pc, prof)) >= prof->pr_size)
		return;

	addr = prof->pr_base + i;
	if (copyin(addr, (caddr_t)&v, sizeof(v)) == 0) {
		v += nticks;
		if (copyout((caddr_t)&v, addr, sizeof(v)) == 0)
			return;
	}
	stopprofclock(pr);
}
