/*	$OpenBSD: dt_dev.c,v 1.46 2025/09/22 07:49:43 sashan Exp $ */

/*
 * Copyright (c) 2019 Martin Pieuchot <mpi@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/param.h>
#include <sys/clockintr.h>
#include <sys/device.h>
#include <sys/exec_elf.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/ptrace.h>
#include <sys/vnode.h>
#include <uvm/uvm.h>
#include <uvm/uvm_map.h>
#include <uvm/uvm_vnode.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/fcntl.h>

#include <machine/intr.h>

#include <dev/dt/dtvar.h>

/*
 * Number of frames to skip in stack traces.
 *
 * The number of frames required to execute dt(4) profiling code
 * depends on the probe, context, architecture and possibly the
 * compiler.
 *
 * Static probes (tracepoints) are executed in the context of the
 * current thread and only need to skip frames up to the recording
 * function.  For example the syscall provider:
 *
 *	dt_prov_syscall_entry+0x141
 *	syscall+0x205		<--- start here
 *	Xsyscall+0x128
 *
 * Probes executed in their own context, like the profile provider,
 * need to skip the frames of that context which are different for
 * every architecture.  For example the profile provider executed
 * from hardclock(9) on amd64:
 *
 *	dt_prov_profile_enter+0x6e
 *	hardclock+0x1a9
 *	lapic_clockintr+0x3f
 *	Xresume_lapic_ltimer+0x26
 *	acpicpu_idle+0x1d2	<---- start here.
 *	sched_idle+0x225
 *	proc_trampoline+0x1c
 */
#if defined(__amd64__)
#define DT_FA_PROFILE	5
#define DT_FA_STATIC	2
#elif defined(__i386__)
#define DT_FA_PROFILE	5
#define DT_FA_STATIC	2
#elif defined(__macppc__)
#define DT_FA_PROFILE  5
#define DT_FA_STATIC   2
#elif defined(__octeon__)
#define DT_FA_PROFILE	6
#define DT_FA_STATIC	2
#elif defined(__powerpc64__)
#define DT_FA_PROFILE	6
#define DT_FA_STATIC	2
#elif defined(__sparc64__)
#define DT_FA_PROFILE	7
#define DT_FA_STATIC	1
#else
#define DT_FA_STATIC	0
#define DT_FA_PROFILE	0
#endif

#define DT_EVTRING_SIZE	16	/* # of slots in per PCB event ring */

#define DPRINTF(x...) /* nothing */

/*
 *  Locks used to protect struct members and variables in this file:
 *	a	atomic
 *	I	invariant after initialization
 *	K	kernel lock
 *	D	dtrace rw-lock dt_lock
 *	r	owned by thread doing read(2)
 *	c	owned by CPU
 *	s	sliced ownership, based on read/write indexes
 *	p	written by CPU, read by thread doing read(2)
 */

/*
 * Per-CPU Event States
 */
struct dt_cpubuf {
	unsigned int		 dc_prod;	/* [r] read index */
	unsigned int		 dc_cons;	/* [c] write index */
	struct dt_evt		*dc_ring;	/* [s] ring of event states */
	unsigned int	 	 dc_inevt;	/* [c] in event already? */

	/* Counters */
	unsigned int		 dc_dropevt;	/* [p] # of events dropped */
	unsigned int		 dc_skiptick;	/* [p] # of ticks skipped */
	unsigned int		 dc_recurevt;	/* [p] # of recursive events */
	unsigned int		 dc_readevt;	/* [r] # of events read */
};

/*
 * Descriptor associated with each program opening /dev/dt.  It is used
 * to keep track of enabled PCBs.
 */
struct dt_softc {
	SLIST_ENTRY(dt_softc)	 ds_next;	/* [K] descriptor list */
	int			 ds_unit;	/* [I] D_CLONE unique unit */
	pid_t			 ds_pid;	/* [I] PID of tracing program */
	void			*ds_si;		/* [I] to defer wakeup(9) */

	struct dt_pcb_list	 ds_pcbs;	/* [K] list of enabled PCBs */
	int			 ds_recording;	/* [D] currently recording? */
	unsigned int		 ds_evtcnt;	/* [a] # of readable evts */

	struct dt_cpubuf	 ds_cpu[MAXCPUS]; /* [I] Per-cpu event states */
	unsigned int		 ds_lastcpu;	/* [r] last CPU ring read(2). */
};

SLIST_HEAD(, dt_softc) dtdev_list;	/* [K] list of open /dev/dt nodes */

/*
 * Probes are created during dt_attach() and never modified/freed during
 * the lifetime of the system.  That's why we consider them as [I]mmutable.
 */
unsigned int			dt_nprobes;	/* [I] # of probes available */
SIMPLEQ_HEAD(, dt_probe)	dt_probe_list;	/* [I] list of probes */

struct rwlock			dt_lock = RWLOCK_INITIALIZER("dtlk");
volatile uint32_t		dt_tracing = 0;	/* [D] # of processes tracing */

int allowdt;					/* [a] */

void	dtattach(struct device *, struct device *, void *);
int	dtopen(dev_t, int, int, struct proc *);
int	dtclose(dev_t, int, int, struct proc *);
int	dtread(dev_t, struct uio *, int);
int	dtioctl(dev_t, u_long, caddr_t, int, struct proc *);

struct	dt_softc *dtlookup(int);
struct	dt_softc *dtalloc(void);
void	dtfree(struct dt_softc *);

int	dt_ioctl_list_probes(struct dt_softc *, struct dtioc_probe *);
int	dt_ioctl_get_args(struct dt_softc *, struct dtioc_arg *);
int	dt_ioctl_get_stats(struct dt_softc *, struct dtioc_stat *);
int	dt_ioctl_record_start(struct dt_softc *);
void	dt_ioctl_record_stop(struct dt_softc *);
int	dt_ioctl_probe_enable(struct dt_softc *, struct dtioc_req *);
int	dt_ioctl_probe_disable(struct dt_softc *, struct dtioc_req *);
int	dt_ioctl_rd_vnode(struct dt_softc *, struct dtioc_rdvn *);

int	dt_ring_copy(struct dt_cpubuf *, struct uio *, size_t, size_t *);

void	dt_wakeup(struct dt_softc *);
void	dt_deferred_wakeup(void *);

void
dtattach(struct device *parent, struct device *self, void *aux)
{
	SLIST_INIT(&dtdev_list);
	SIMPLEQ_INIT(&dt_probe_list);

	/* Init providers */
	dt_nprobes += dt_prov_profile_init();
	dt_nprobes += dt_prov_syscall_init();
	dt_nprobes += dt_prov_static_init();
#ifdef DDBPROF
	dt_nprobes += dt_prov_kprobe_init();
#endif
}

int
dtopen(dev_t dev, int flags, int mode, struct proc *p)
{
	struct dt_softc *sc;
	int unit = minor(dev);

	if (atomic_load_int(&allowdt) == 0)
		return EPERM;

	sc = dtalloc();
	if (sc == NULL)
		return ENOMEM;

	/* no sleep after this point */
	if (dtlookup(unit) != NULL) {
		dtfree(sc);
		return EBUSY;
	}

	sc->ds_unit = unit;
	sc->ds_pid = p->p_p->ps_pid;
	TAILQ_INIT(&sc->ds_pcbs);
	sc->ds_lastcpu = 0;
	sc->ds_evtcnt = 0;

	SLIST_INSERT_HEAD(&dtdev_list, sc, ds_next);

	DPRINTF("dt%d: pid %d open\n", sc->ds_unit, sc->ds_pid);

	return 0;
}

int
dtclose(dev_t dev, int flags, int mode, struct proc *p)
{
	struct dt_softc *sc;
	int unit = minor(dev);

	sc = dtlookup(unit);
	KASSERT(sc != NULL);

	DPRINTF("dt%d: pid %d close\n", sc->ds_unit, sc->ds_pid);

	SLIST_REMOVE(&dtdev_list, sc, dt_softc, ds_next);
	dt_ioctl_record_stop(sc);
	dt_pcb_purge(&sc->ds_pcbs);
	dtfree(sc);

	return 0;
}

int
dtread(dev_t dev, struct uio *uio, int flags)
{
	struct dt_softc *sc;
	struct dt_cpubuf *dc;
	int i, error = 0, unit = minor(dev);
	size_t count, max, read = 0;

	sc = dtlookup(unit);
	KASSERT(sc != NULL);

	max = howmany(uio->uio_resid, sizeof(struct dt_evt));
	if (max < 1)
		return (EMSGSIZE);

	while (!atomic_load_int(&sc->ds_evtcnt)) {
		sleep_setup(sc, PWAIT | PCATCH, "dtread");
		error = sleep_finish(INFSLP, !atomic_load_int(&sc->ds_evtcnt));
		if (error == EINTR || error == ERESTART)
			break;
	}
	if (error)
		return error;

	KERNEL_ASSERT_LOCKED();
	for (i = 0; i < ncpusfound; i++) {
		count = 0;
		dc = &sc->ds_cpu[(sc->ds_lastcpu + i) % ncpusfound];
		error = dt_ring_copy(dc, uio, max, &count);
		if (error && count == 0)
			break;

		read += count;
		max -= count;
		if (max == 0)
			break;
	}
	sc->ds_lastcpu += i % ncpusfound;

	atomic_sub_int(&sc->ds_evtcnt, read);

	return error;
}

int
dtioctl(dev_t dev, u_long cmd, caddr_t addr, int flag, struct proc *p)
{
	struct dt_softc *sc;
	int unit = minor(dev);
	int on, error = 0;

	sc = dtlookup(unit);
	KASSERT(sc != NULL);

	switch (cmd) {
	case DTIOCGPLIST:
		return dt_ioctl_list_probes(sc, (struct dtioc_probe *)addr);
	case DTIOCGARGS:
		return dt_ioctl_get_args(sc, (struct dtioc_arg *)addr);
	case DTIOCGSTATS:
		return dt_ioctl_get_stats(sc, (struct dtioc_stat *)addr);
	case DTIOCRECORD:
	case DTIOCPRBENABLE:
	case DTIOCPRBDISABLE:
	case DTIOCRDVNODE:
		/* root only ioctl(2) */
		break;
	default:
		return ENOTTY;
	}

	if ((error = suser(p)) != 0)
		return error;

	switch (cmd) {
	case DTIOCRECORD:
		on = *(int *)addr;
		if (on)
			error = dt_ioctl_record_start(sc);
		else
			dt_ioctl_record_stop(sc);
		break;
	case DTIOCPRBENABLE:
		error = dt_ioctl_probe_enable(sc, (struct dtioc_req *)addr);
		break;
	case DTIOCPRBDISABLE:
		error = dt_ioctl_probe_disable(sc, (struct dtioc_req *)addr);
		break;
	case DTIOCRDVNODE:
		error = dt_ioctl_rd_vnode(sc, (struct dtioc_rdvn *)addr);
		break;
	default:
		KASSERT(0);
	}

	return error;
}

struct dt_softc *
dtlookup(int unit)
{
	struct dt_softc *sc;

	KERNEL_ASSERT_LOCKED();

	SLIST_FOREACH(sc, &dtdev_list, ds_next) {
		if (sc->ds_unit == unit)
			break;
	}

	return sc;
}

struct dt_softc *
dtalloc(void)
{
	struct dt_softc *sc;
	struct dt_evt *dtev;
	int i;

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK|M_CANFAIL|M_ZERO);
	if (sc == NULL)
		return NULL;

	for (i = 0; i < ncpusfound; i++) {
		dtev = mallocarray(DT_EVTRING_SIZE, sizeof(*dtev), M_DEVBUF,
		    M_WAITOK|M_CANFAIL|M_ZERO);
		if (dtev == NULL)
			break;
		sc->ds_cpu[i].dc_ring = dtev;
	}
	if (i < ncpusfound) {
		dtfree(sc);
		return NULL;
	}

	sc->ds_si = softintr_establish(IPL_SOFTCLOCK | IPL_MPSAFE,
	    dt_deferred_wakeup, sc);
	if (sc->ds_si == NULL) {
		dtfree(sc);
		return NULL;
	}

	return sc;
}

void
dtfree(struct dt_softc *sc)
{
	struct dt_evt *dtev;
	int i;

	if (sc->ds_si != NULL)
		softintr_disestablish(sc->ds_si);

	for (i = 0; i < ncpusfound; i++) {
		dtev = sc->ds_cpu[i].dc_ring;
		free(dtev, M_DEVBUF, DT_EVTRING_SIZE * sizeof(*dtev));
	}
	free(sc, M_DEVBUF, sizeof(*sc));
}

int
dt_ioctl_list_probes(struct dt_softc *sc, struct dtioc_probe *dtpr)
{
	struct dtioc_probe_info info, *dtpi;
	struct dt_probe *dtp;
	size_t size;
	int error = 0;

	size = dtpr->dtpr_size;
	dtpr->dtpr_size = dt_nprobes * sizeof(*dtpi);
	if (size == 0)
		return 0;

	dtpi = dtpr->dtpr_probes;
	SIMPLEQ_FOREACH(dtp, &dt_probe_list, dtp_next) {
		if (size < sizeof(*dtpi)) {
			error = ENOSPC;
			break;
		}
		memset(&info, 0, sizeof(info));
		info.dtpi_pbn = dtp->dtp_pbn;
		info.dtpi_nargs = dtp->dtp_nargs;
		strlcpy(info.dtpi_prov, dtp->dtp_prov->dtpv_name,
		    sizeof(info.dtpi_prov));
		strlcpy(info.dtpi_func, dtp->dtp_func, sizeof(info.dtpi_func));
		strlcpy(info.dtpi_name, dtp->dtp_name, sizeof(info.dtpi_name));
		error = copyout(&info, dtpi, sizeof(*dtpi));
		if (error)
			break;
		size -= sizeof(*dtpi);
		dtpi++;
	}

	return error;
}

int
dt_ioctl_get_args(struct dt_softc *sc, struct dtioc_arg *dtar)
{
	struct dtioc_arg_info info, *dtai;
	struct dt_probe *dtp;
	size_t size, n, t;
	uint32_t pbn;
	int error = 0;

	pbn = dtar->dtar_pbn;
	if (pbn == 0 || pbn > dt_nprobes)
		return EINVAL;

	SIMPLEQ_FOREACH(dtp, &dt_probe_list, dtp_next) {
		if (pbn == dtp->dtp_pbn)
			break;
	}
	if (dtp == NULL)
		return EINVAL;

	if (dtp->dtp_sysnum != 0) {
		/* currently not supported for system calls */
		dtar->dtar_size = 0;
		return 0;
	}

	size = dtar->dtar_size;
	dtar->dtar_size = dtp->dtp_nargs * sizeof(*dtar);
	if (size == 0)
		return 0;

	t = 0;
	dtai = dtar->dtar_args;
	for (n = 0; n < dtp->dtp_nargs; n++) {
		if (size < sizeof(*dtai)) {
			error = ENOSPC;
			break;
		}
		if (n >= DTMAXARGTYPES || dtp->dtp_argtype[n] == NULL)
			continue;
		memset(&info, 0, sizeof(info));
		info.dtai_pbn = dtp->dtp_pbn;
		info.dtai_argn = t++;
		strlcpy(info.dtai_argtype, dtp->dtp_argtype[n],
		    sizeof(info.dtai_argtype));
		error = copyout(&info, dtai, sizeof(*dtai));
		if (error)
			break;
		size -= sizeof(*dtai);
		dtai++;
	}
	dtar->dtar_size = t * sizeof(*dtar);

	return error;
}

int
dt_ioctl_get_stats(struct dt_softc *sc, struct dtioc_stat *dtst)
{
	struct dt_cpubuf *dc;
	uint64_t readevt, dropevt, skiptick, recurevt;
	int i;

	readevt = dropevt = skiptick = 0;
	for (i = 0; i < ncpusfound; i++) {
		dc = &sc->ds_cpu[i];

		membar_consumer();
		dropevt += dc->dc_dropevt;
		skiptick = dc->dc_skiptick;
		recurevt = dc->dc_recurevt;
		readevt += dc->dc_readevt;
	}

	dtst->dtst_readevt = readevt;
	dtst->dtst_dropevt = dropevt;
	dtst->dtst_skiptick = skiptick;
	dtst->dtst_recurevt = recurevt;
	return 0;
}

int
dt_ioctl_record_start(struct dt_softc *sc)
{
	uint64_t now;
	struct dt_pcb *dp;
	int error = 0;

	rw_enter_write(&dt_lock);
	if (sc->ds_recording) {
		error = EBUSY;
		goto out;
	}

	KERNEL_ASSERT_LOCKED();
	if (TAILQ_EMPTY(&sc->ds_pcbs)) {
		error = ENOENT;
		goto out;
	}

	now = nsecuptime();
	TAILQ_FOREACH(dp, &sc->ds_pcbs, dp_snext) {
		struct dt_probe *dtp = dp->dp_dtp;

		SMR_SLIST_INSERT_HEAD_LOCKED(&dtp->dtp_pcbs, dp, dp_pnext);
		dtp->dtp_recording++;
		dtp->dtp_prov->dtpv_recording++;

		if (dp->dp_nsecs != 0) {
			clockintr_bind(&dp->dp_clockintr, dp->dp_cpu, dt_clock,
			    dp);
			clockintr_schedule(&dp->dp_clockintr,
			    now + dp->dp_nsecs);
		}
	}
	sc->ds_recording = 1;
	dt_tracing++;

 out:
	rw_exit_write(&dt_lock);
	return error;
}

void
dt_ioctl_record_stop(struct dt_softc *sc)
{
	struct dt_pcb *dp;

	rw_enter_write(&dt_lock);
	if (!sc->ds_recording) {
		rw_exit_write(&dt_lock);
		return;
	}

	DPRINTF("dt%d: pid %d disable\n", sc->ds_unit, sc->ds_pid);

	dt_tracing--;
	sc->ds_recording = 0;
	TAILQ_FOREACH(dp, &sc->ds_pcbs, dp_snext) {
		struct dt_probe *dtp = dp->dp_dtp;

		/*
		 * Set an execution barrier to ensure the shared
		 * reference to dp is inactive.
		 */
		if (dp->dp_nsecs != 0)
			clockintr_unbind(&dp->dp_clockintr, CL_BARRIER);

		dtp->dtp_recording--;
		dtp->dtp_prov->dtpv_recording--;
		SMR_SLIST_REMOVE_LOCKED(&dtp->dtp_pcbs, dp, dt_pcb, dp_pnext);
	}
	rw_exit_write(&dt_lock);

	/* Wait until readers cannot access the PCBs. */
	smr_barrier();
}

int
dt_ioctl_probe_enable(struct dt_softc *sc, struct dtioc_req *dtrq)
{
	struct dt_pcb_list plist;
	struct dt_probe *dtp;
	struct dt_pcb *dp;
	int error;

	SIMPLEQ_FOREACH(dtp, &dt_probe_list, dtp_next) {
		if (dtp->dtp_pbn == dtrq->dtrq_pbn)
			break;
	}
	if (dtp == NULL)
		return ENOENT;

	/* Only allow one probe of each type. */
	TAILQ_FOREACH(dp, &sc->ds_pcbs, dp_snext) {
		if (dp->dp_dtp->dtp_pbn == dtrq->dtrq_pbn)
			return EEXIST;
	}

	TAILQ_INIT(&plist);
	error = dtp->dtp_prov->dtpv_alloc(dtp, sc, &plist, dtrq);
	if (error)
		return error;

	DPRINTF("dt%d: pid %d enable %u : %b\n", sc->ds_unit, sc->ds_pid,
	    dtrq->dtrq_pbn, (unsigned int)dtrq->dtrq_evtflags, DTEVT_FLAG_BITS);

	/* Append all PCBs to this instance */
	TAILQ_CONCAT(&sc->ds_pcbs, &plist, dp_snext);

	return 0;
}

int
dt_ioctl_probe_disable(struct dt_softc *sc, struct dtioc_req *dtrq)
{
	struct dt_probe *dtp;
	int error;

	SIMPLEQ_FOREACH(dtp, &dt_probe_list, dtp_next) {
		if (dtp->dtp_pbn == dtrq->dtrq_pbn)
			break;
	}
	if (dtp == NULL)
		return ENOENT;

	if (dtp->dtp_prov->dtpv_dealloc) {
		error = dtp->dtp_prov->dtpv_dealloc(dtp, sc, dtrq);
		if (error)
			return error;
	}

	DPRINTF("dt%d: pid %d dealloc\n", sc->ds_unit, sc->ds_pid,
	    dtrq->dtrq_pbn);

	return 0;
}

int
dt_ioctl_rd_vnode(struct dt_softc *sc, struct dtioc_rdvn *dtrv)
{
	struct process *ps;
	struct proc *p = curproc;
	boolean_t ok;
	struct vm_map_entry *e;
	int err = 0;
	int fd;
	struct uvm_vnode *uvn;
	struct vnode *vn;
	struct file *fp;

	if ((ps = prfind(dtrv->dtrv_pid)) == NULL)
		return ESRCH;

	vm_map_lock_read(&ps->ps_vmspace->vm_map);

	ok = uvm_map_lookup_entry(&ps->ps_vmspace->vm_map,
	    (vaddr_t)dtrv->dtrv_va, &e);
	if (ok == 0 || (e->etype & UVM_ET_OBJ) == 0 ||
	    (e->protection & PROT_EXEC) == 0 ||
	    !UVM_OBJ_IS_VNODE(e->object.uvm_obj)) {
		err = ENOENT;
		vn = NULL;
		DPRINTF("%s no mapping for %p\n", __func__, dtrv->dtrv_va);
	} else {
		uvn = (struct uvm_vnode *)e->object.uvm_obj;
		vn = uvn->u_vnode;
		vref(vn);

		dtrv->dtrv_len = (size_t)uvn->u_size;
		dtrv->dtrv_start = (caddr_t)e->start;
		dtrv->dtrv_offset = (caddr_t)e->offset;
	}

	vm_map_unlock_read(&ps->ps_vmspace->vm_map);

	if (vn != NULL) {
		fdplock(p->p_fd);
	        err = falloc(p, &fp, &fd);
		fdpunlock(p->p_fd);
		if (err != 0) {
			vrele(vn);
			DPRINTF("%s fdopen failed (%d)\n", __func__, err);
			return err;
		}
		err = VOP_OPEN(vn, O_RDONLY, p->p_p->ps_ucred, p);
		if (err == 0) {
			fp->f_flag = FREAD;
			fp->f_type = DTYPE_VNODE;
			fp->f_ops = &vnops;
			fp->f_data = vn;
			fp->f_offset = 0;
			dtrv->dtrv_fd = fd;
			fdplock(p->p_fd);
			fdinsert(p->p_fd, fd, UF_EXCLOSE, fp);
			fdpunlock(p->p_fd);
			FRELE(fp, p);
		} else {
			DPRINTF("%s vopen() failed (%d)\n", __func__,
			    err);
			vrele(vn);
			fdplock(p->p_fd);
			fdremove(p->p_fd, fd);
			fdpunlock(p->p_fd);
			FRELE(fp, p);
		}
	}

	return err;
}

struct dt_probe *
dt_dev_alloc_probe(const char *func, const char *name, struct dt_provider *dtpv)
{
	struct dt_probe *dtp;

	dtp = malloc(sizeof(*dtp), M_DT, M_NOWAIT|M_ZERO);
	if (dtp == NULL)
		return NULL;

	SMR_SLIST_INIT(&dtp->dtp_pcbs);
	dtp->dtp_prov = dtpv;
	dtp->dtp_func = func;
	dtp->dtp_name = name;
	dtp->dtp_sysnum = -1;
	dtp->dtp_ref = 0;

	return dtp;
}

void
dt_dev_register_probe(struct dt_probe *dtp)
{
	static uint64_t probe_nb;

	dtp->dtp_pbn = ++probe_nb;
	SIMPLEQ_INSERT_TAIL(&dt_probe_list, dtp, dtp_next);
}

struct dt_pcb *
dt_pcb_alloc(struct dt_probe *dtp, struct dt_softc *sc)
{
	struct dt_pcb *dp;

	dp = malloc(sizeof(*dp), M_DT, M_WAITOK|M_CANFAIL|M_ZERO);
	if (dp == NULL)
		return NULL;

	dp->dp_sc = sc;
	dp->dp_dtp = dtp;
	return dp;
}

void
dt_pcb_free(struct dt_pcb *dp)
{
	free(dp, M_DT, sizeof(*dp));
}

void
dt_pcb_purge(struct dt_pcb_list *plist)
{
	struct dt_pcb *dp;

	while ((dp = TAILQ_FIRST(plist)) != NULL) {
		TAILQ_REMOVE(plist, dp, dp_snext);
		dt_pcb_free(dp);
	}
}

void
dt_pcb_ring_skiptick(struct dt_pcb *dp, unsigned int skip)
{
	struct dt_cpubuf *dc = &dp->dp_sc->ds_cpu[cpu_number()];

	dc->dc_skiptick += skip;
	membar_producer();
}

/*
 * Get a reference to the next free event state from the ring.
 */
struct dt_evt *
dt_pcb_ring_get(struct dt_pcb *dp, int profiling)
{
	struct proc *p = curproc;
	struct dt_evt *dtev;
	int prod, cons, distance;
	struct dt_cpubuf *dc = &dp->dp_sc->ds_cpu[cpu_number()];

	if (dc->dc_inevt == 1) {
		dc->dc_recurevt++;
		membar_producer();
		return NULL;
	}

	dc->dc_inevt = 1;

	membar_consumer();
	prod = dc->dc_prod;
	cons = dc->dc_cons;
	distance = prod - cons;
	if (distance == 1 || distance == (1 - DT_EVTRING_SIZE)) {
		/* read(2) isn't finished */
		dc->dc_dropevt++;
		membar_producer();

		dc->dc_inevt = 0;
		return NULL;
	}

	/*
	 * Save states in next free event slot.
	 */
	dtev = &dc->dc_ring[cons];
	memset(dtev, 0, sizeof(*dtev));

	dtev->dtev_pbn = dp->dp_dtp->dtp_pbn;
	dtev->dtev_cpu = cpu_number();
	dtev->dtev_pid = p->p_p->ps_pid;
	dtev->dtev_tid = p->p_tid + THREAD_PID_OFFSET;
	nanotime(&dtev->dtev_tsp);

	if (ISSET(dp->dp_evtflags, DTEVT_EXECNAME))
		strlcpy(dtev->dtev_comm, p->p_p->ps_comm, sizeof(dtev->dtev_comm));

	if (ISSET(dp->dp_evtflags, DTEVT_KSTACK)) {
		if (profiling)
			stacktrace_save_at(&dtev->dtev_kstack, DT_FA_PROFILE);
		else
			stacktrace_save_at(&dtev->dtev_kstack, DT_FA_STATIC);
	}
	if (ISSET(dp->dp_evtflags, DTEVT_USTACK))
		stacktrace_save_utrace(&dtev->dtev_ustack);

	return dtev;
}

void
dt_pcb_ring_consume(struct dt_pcb *dp, struct dt_evt *dtev)
{
	struct dt_cpubuf *dc = &dp->dp_sc->ds_cpu[cpu_number()];

	KASSERT(dtev == &dc->dc_ring[dc->dc_cons]);

	dc->dc_cons = (dc->dc_cons + 1) % DT_EVTRING_SIZE;
	membar_producer();

	atomic_inc_int(&dp->dp_sc->ds_evtcnt);
	dc->dc_inevt = 0;

	dt_wakeup(dp->dp_sc);
}

/*
 * Copy at most `max' events from `dc', producing the same amount
 * of free slots.
 */
int
dt_ring_copy(struct dt_cpubuf *dc, struct uio *uio, size_t max, size_t *rcvd)
{
	size_t count, copied = 0;
	unsigned int cons, prod;
	int error = 0;

	KASSERT(max > 0);

	membar_consumer();
	cons = dc->dc_cons;
	prod = dc->dc_prod;

	if (cons < prod)
		count = DT_EVTRING_SIZE - prod;
	else
		count = cons - prod;

	if (count == 0)
		return 0;

	count = MIN(count, max);
	error = uiomove(&dc->dc_ring[prod], count * sizeof(struct dt_evt), uio);
	if (error)
		return error;
	copied += count;

	/* Produce */
	prod = (prod + count) % DT_EVTRING_SIZE;

	/* If the ring didn't wrap, stop here. */
	if (max == copied || prod != 0 || cons == 0)
		goto out;

	count = MIN(cons, (max - copied));
	error = uiomove(&dc->dc_ring[0], count * sizeof(struct dt_evt), uio);
	if (error)
		goto out;

	copied += count;
	prod += count;

out:
	dc->dc_readevt += copied;
	dc->dc_prod = prod;
	membar_producer();

	*rcvd = copied;
	return error;
}

void
dt_wakeup(struct dt_softc *sc)
{
	/*
	 * It is not always safe or possible to call wakeup(9) and grab
	 * the SCHED_LOCK() from a given tracepoint.  This is true for
	 * any tracepoint that might trigger inside the scheduler or at
	 * any IPL higher than IPL_SCHED.  For this reason use a soft-
	 * interrupt to defer the wakeup.
	 */
	softintr_schedule(sc->ds_si);
}

void
dt_deferred_wakeup(void *arg)
{
	struct dt_softc *sc = arg;

	wakeup(sc);
}
