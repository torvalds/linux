/*	$OpenBSD: kcov.c,v 1.51 2025/02/02 21:05:12 gnezdo Exp $	*/

/*
 * Copyright (c) 2018 Anton Lindqvist <anton@openbsd.org>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/kcov.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/pool.h>
#include <sys/stdint.h>
#include <sys/queue.h>

/* kcov_vnode() */
#include <sys/conf.h>
#include <sys/vnode.h>
#include <sys/specdev.h>

#include <uvm/uvm_extern.h>

#define KCOV_BUF_MEMB_SIZE	sizeof(uintptr_t)
#define KCOV_BUF_MAX_NMEMB	(512 << 10)

#define KCOV_CMP_CONST		0x1
#define KCOV_CMP_SIZE(x)	((x) << 1)

#define KCOV_STATE_NONE		0
#define KCOV_STATE_READY	1
#define KCOV_STATE_TRACE	2
#define KCOV_STATE_DYING	3

#define KCOV_STRIDE_TRACE_PC	1
#define KCOV_STRIDE_TRACE_CMP	4

/*
 * Coverage structure.
 *
 * Locking:
 * 	I	immutable after creation
 *	M	kcov_mtx
 *	a	atomic operations
 */
struct kcov_dev {
	int		 kd_state;	/* [M] */
	int		 kd_mode;	/* [M] */
	int		 kd_unit;	/* [I] D_CLONE unique device minor */
	int		 kd_intr;	/* [M] currently used in interrupt */
	uintptr_t	*kd_buf;	/* [a] traced coverage */
	size_t		 kd_nmemb;	/* [I] */
	size_t		 kd_size;	/* [I] */

	struct kcov_remote *kd_kr;	/* [M] */

	TAILQ_ENTRY(kcov_dev)	kd_entry;	/* [M] */
};

/*
 * Remote coverage structure.
 *
 * Locking:
 * 	I	immutable after creation
 *	M	kcov_mtx
 */
struct kcov_remote {
	struct kcov_dev *kr_kd;	/* [M] */
	void *kr_id;		/* [I] */
	int kr_subsystem;	/* [I] */
	int kr_nsections;	/* [M] # threads in remote section */
	int kr_state;		/* [M] */

	TAILQ_ENTRY(kcov_remote) kr_entry;	/* [M] */
};

/*
 * Per CPU coverage structure used to track coverage when executing in a remote
 * interrupt context.
 *
 * Locking:
 * 	I	immutable after creation
 *	M	kcov_mtx
 */
struct kcov_cpu {
	struct kcov_dev  kc_kd;
	struct kcov_dev *kc_kd_save;	/* [M] previous kcov_dev */
	int kc_cpuid;			/* [I] cpu number */

	TAILQ_ENTRY(kcov_cpu) kc_entry;	/* [I] */
};

void kcovattach(int);

int kd_init(struct kcov_dev *, unsigned long);
void kd_free(struct kcov_dev *);
struct kcov_dev *kd_lookup(int);
void kd_copy(struct kcov_dev *, struct kcov_dev *);

struct kcov_remote *kcov_remote_register_locked(int, void *);
int kcov_remote_attach(struct kcov_dev *, struct kio_remote_attach *);
void kcov_remote_detach(struct kcov_dev *, struct kcov_remote *);
void kr_free(struct kcov_remote *);
void kr_barrier(struct kcov_remote *);
struct kcov_remote *kr_lookup(int, void *);

static struct kcov_dev *kd_curproc(int);
static struct kcov_cpu *kd_curcpu(void);
static uint64_t kd_claim(struct kcov_dev *, int, int);

TAILQ_HEAD(, kcov_dev) kd_list = TAILQ_HEAD_INITIALIZER(kd_list);
TAILQ_HEAD(, kcov_remote) kr_list = TAILQ_HEAD_INITIALIZER(kr_list);
TAILQ_HEAD(, kcov_cpu) kc_list = TAILQ_HEAD_INITIALIZER(kc_list);

int kcov_cold = 1;
int kr_cold = 1;
struct mutex kcov_mtx = MUTEX_INITIALIZER(IPL_MPFLOOR);
struct pool kr_pool;

static inline int
inintr(struct cpu_info *ci)
{
	return (ci->ci_idepth > 0);
}

/*
 * Compiling the kernel with the `-fsanitize-coverage=trace-pc' option will
 * cause the following function to be called upon function entry and before
 * each block of instructions that maps to a single line in the original source
 * code.
 *
 * If kcov is enabled for the current thread, the kernel program counter will
 * be stored in its corresponding coverage buffer.
 */
void
__sanitizer_cov_trace_pc(void)
{
	struct kcov_dev *kd;
	uint64_t idx;

	kd = kd_curproc(KCOV_MODE_TRACE_PC);
	if (kd == NULL)
		return;

	if ((idx = kd_claim(kd, KCOV_STRIDE_TRACE_PC, 1)))
		kd->kd_buf[idx] = (uintptr_t)__builtin_return_address(0);
}

/*
 * Compiling the kernel with the `-fsanitize-coverage=trace-cmp' option will
 * cause the following function to be called upon integer comparisons and switch
 * statements.
 *
 * If kcov is enabled for the current thread, the comparison will be stored in
 * its corresponding coverage buffer.
 */
void
trace_cmp(struct kcov_dev *kd, uint64_t type, uint64_t arg1, uint64_t arg2,
    uintptr_t pc)
{
	uint64_t idx;

	if ((idx = kd_claim(kd, KCOV_STRIDE_TRACE_CMP, 1))) {
		kd->kd_buf[idx] = type;
		kd->kd_buf[idx + 1] = arg1;
		kd->kd_buf[idx + 2] = arg2;
		kd->kd_buf[idx + 3] = pc;
	}
}

#define TRACE_CMP(type, arg1, arg2) do {				\
	struct kcov_dev *kd;						\
	if ((kd = kd_curproc(KCOV_MODE_TRACE_CMP)) == NULL)		\
		return;							\
	trace_cmp(kd, (type), (arg1), (arg2),				\
	    (uintptr_t)__builtin_return_address(0));			\
} while (0)

void
__sanitizer_cov_trace_cmp1(uint8_t arg1, uint8_t arg2)
{
	TRACE_CMP(KCOV_CMP_SIZE(0), arg1, arg2);
}

void
__sanitizer_cov_trace_cmp2(uint16_t arg1, uint16_t arg2)
{
	TRACE_CMP(KCOV_CMP_SIZE(1), arg1, arg2);
}

void
__sanitizer_cov_trace_cmp4(uint32_t arg1, uint32_t arg2)
{
	TRACE_CMP(KCOV_CMP_SIZE(2), arg1, arg2);
}

void
__sanitizer_cov_trace_cmp8(uint64_t arg1, uint64_t arg2)
{
	TRACE_CMP(KCOV_CMP_SIZE(3), arg1, arg2);
}

void
__sanitizer_cov_trace_const_cmp1(uint8_t arg1, uint8_t arg2)
{
	TRACE_CMP(KCOV_CMP_SIZE(0) | KCOV_CMP_CONST, arg1, arg2);
}

void
__sanitizer_cov_trace_const_cmp2(uint16_t arg1, uint16_t arg2)
{
	TRACE_CMP(KCOV_CMP_SIZE(1) | KCOV_CMP_CONST, arg1, arg2);
}

void
__sanitizer_cov_trace_const_cmp4(uint32_t arg1, uint32_t arg2)
{
	TRACE_CMP(KCOV_CMP_SIZE(2) | KCOV_CMP_CONST, arg1, arg2);
}

void
__sanitizer_cov_trace_const_cmp8(uint64_t arg1, uint64_t arg2)
{
	TRACE_CMP(KCOV_CMP_SIZE(3) | KCOV_CMP_CONST, arg1, arg2);
}

void
__sanitizer_cov_trace_switch(uint64_t val, uint64_t *cases)
{
	struct kcov_dev *kd;
	uint64_t i, nbits, ncases, type;
	uintptr_t pc;

	kd = kd_curproc(KCOV_MODE_TRACE_CMP);
	if (kd == NULL)
		return;

	pc = (uintptr_t)__builtin_return_address(0);
	ncases = cases[0];
	nbits = cases[1];

	switch (nbits) {
	case 8:
		type = KCOV_CMP_SIZE(0);
		break;
	case 16:
		type = KCOV_CMP_SIZE(1);
		break;
	case 32:
		type = KCOV_CMP_SIZE(2);
		break;
	case 64:
		type = KCOV_CMP_SIZE(3);
		break;
	default:
		return;
	}
	type |= KCOV_CMP_CONST;

	for (i = 0; i < ncases; i++)
		trace_cmp(kd, type, cases[i + 2], val, pc);
}

void
kcovattach(int count)
{
	struct kcov_cpu *kc;
	int error, i;

	pool_init(&kr_pool, sizeof(struct kcov_remote), 0, IPL_MPFLOOR, PR_WAITOK,
	    "kcovpl", NULL);

	kc = mallocarray(ncpusfound, sizeof(*kc), M_DEVBUF, M_WAITOK | M_ZERO);
	mtx_enter(&kcov_mtx);
	for (i = 0; i < ncpusfound; i++) {
		kc[i].kc_cpuid = i;
		error = kd_init(&kc[i].kc_kd, KCOV_BUF_MAX_NMEMB);
		KASSERT(error == 0);
		TAILQ_INSERT_TAIL(&kc_list, &kc[i], kc_entry);
	}
	mtx_leave(&kcov_mtx);

	kr_cold = 0;
}

int
kcovopen(dev_t dev, int flag, int mode, struct proc *p)
{
	struct kcov_dev *kd;

	kd = malloc(sizeof(*kd), M_SUBPROC, M_WAITOK | M_ZERO);
	kd->kd_unit = minor(dev);
	mtx_enter(&kcov_mtx);
	KASSERT(kd_lookup(kd->kd_unit) == NULL);
	TAILQ_INSERT_TAIL(&kd_list, kd, kd_entry);
	if (kcov_cold)
		kcov_cold = 0;
	mtx_leave(&kcov_mtx);
	return (0);
}

int
kcovclose(dev_t dev, int flag, int mode, struct proc *p)
{
	struct kcov_dev *kd;

	mtx_enter(&kcov_mtx);

	kd = kd_lookup(minor(dev));
	if (kd == NULL) {
		mtx_leave(&kcov_mtx);
		return (ENXIO);
	}

	TAILQ_REMOVE(&kd_list, kd, kd_entry);
	if (kd->kd_state == KCOV_STATE_TRACE && kd->kd_kr == NULL) {
		/*
		 * Another thread is currently using the kcov descriptor,
		 * postpone freeing to kcov_exit().
		 */
		kd->kd_state = KCOV_STATE_DYING;
		kd->kd_mode = KCOV_MODE_NONE;
	} else {
		kd_free(kd);
	}

	mtx_leave(&kcov_mtx);
	return (0);
}

int
kcovioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct kcov_dev *kd;
	int mode;
	int error = 0;

	mtx_enter(&kcov_mtx);

	kd = kd_lookup(minor(dev));
	if (kd == NULL) {
		mtx_leave(&kcov_mtx);
		return (ENXIO);
	}

	switch (cmd) {
	case KIOSETBUFSIZE:
		error = kd_init(kd, *((unsigned long *)data));
		break;
	case KIOENABLE:
		/* Only one kcov descriptor can be enabled per thread. */
		if (p->p_kd != NULL) {
			error = EBUSY;
			break;
		}
		if (kd->kd_state != KCOV_STATE_READY) {
			error = ENXIO;
			break;
		}
		mode = *((int *)data);
		if (mode != KCOV_MODE_TRACE_PC && mode != KCOV_MODE_TRACE_CMP) {
			error = EINVAL;
			break;
		}
		kd->kd_state = KCOV_STATE_TRACE;
		kd->kd_mode = mode;
		/* Remote coverage is mutually exclusive. */
		if (kd->kd_kr == NULL)
			p->p_kd = kd;
		break;
	case KIODISABLE:
		/* Only the enabled thread may disable itself. */
		if ((p->p_kd != kd && kd->kd_kr == NULL)) {
			error = EPERM;
			break;
		}
		if (kd->kd_state != KCOV_STATE_TRACE) {
			error = ENXIO;
			break;
		}
		kd->kd_state = KCOV_STATE_READY;
		kd->kd_mode = KCOV_MODE_NONE;
		if (kd->kd_kr != NULL)
			kr_barrier(kd->kd_kr);
		p->p_kd = NULL;
		break;
	case KIOREMOTEATTACH:
		error = kcov_remote_attach(kd,
		    (struct kio_remote_attach *)data);
		break;
	default:
		error = ENOTTY;
	}
	mtx_leave(&kcov_mtx);

	return (error);
}

paddr_t
kcovmmap(dev_t dev, off_t offset, int prot)
{
	struct kcov_dev *kd;
	paddr_t pa = -1;
	vaddr_t va;

	mtx_enter(&kcov_mtx);

	kd = kd_lookup(minor(dev));
	if (kd == NULL)
		goto out;

	if (offset < 0 || offset >= kd->kd_nmemb * KCOV_BUF_MEMB_SIZE)
		goto out;

	va = (vaddr_t)kd->kd_buf + offset;
	if (pmap_extract(pmap_kernel(), va, &pa) == FALSE)
		pa = -1;

out:
	mtx_leave(&kcov_mtx);
	return (pa);
}

void
kcov_exit(struct proc *p)
{
	struct kcov_dev *kd;

	mtx_enter(&kcov_mtx);

	kd = p->p_kd;
	if (kd == NULL) {
		mtx_leave(&kcov_mtx);
		return;
	}

	if (kd->kd_state == KCOV_STATE_DYING) {
		p->p_kd = NULL;
		kd_free(kd);
	} else {
		kd->kd_state = KCOV_STATE_READY;
		kd->kd_mode = KCOV_MODE_NONE;
		if (kd->kd_kr != NULL)
			kr_barrier(kd->kd_kr);
		p->p_kd = NULL;
	}

	mtx_leave(&kcov_mtx);
}

/*
 * Returns non-zero if the given vnode refers to a kcov device.
 */
int
kcov_vnode(struct vnode *vp)
{
	return (vp->v_type == VCHR &&
	    cdevsw[major(vp->v_rdev)].d_open == kcovopen);
}

struct kcov_dev *
kd_lookup(int unit)
{
	struct kcov_dev *kd;

	MUTEX_ASSERT_LOCKED(&kcov_mtx);

	TAILQ_FOREACH(kd, &kd_list, kd_entry) {
		if (kd->kd_unit == unit)
			return (kd);
	}
	return (NULL);
}

void
kd_copy(struct kcov_dev *dst, struct kcov_dev *src)
{
	uint64_t idx, nmemb;
	int stride;

	MUTEX_ASSERT_LOCKED(&kcov_mtx);
	KASSERT(dst->kd_mode == src->kd_mode);

	nmemb = src->kd_buf[0];
	if (nmemb == 0)
		return;
	stride = src->kd_mode == KCOV_MODE_TRACE_CMP ? KCOV_STRIDE_TRACE_CMP :
	    KCOV_STRIDE_TRACE_PC;
	idx = kd_claim(dst, stride, nmemb);
	if (idx == 0)
		return;
	memcpy(&dst->kd_buf[idx], &src->kd_buf[1],
	    stride * nmemb * KCOV_BUF_MEMB_SIZE);
}

int
kd_init(struct kcov_dev *kd, unsigned long nmemb)
{
	void *buf;
	size_t size;
	int error;

	KASSERT(kd->kd_buf == NULL);

	if (kd->kd_state != KCOV_STATE_NONE)
		return (EBUSY);

	if (nmemb == 0 || nmemb > KCOV_BUF_MAX_NMEMB)
		return (EINVAL);

	size = roundup(nmemb * KCOV_BUF_MEMB_SIZE, PAGE_SIZE);
	mtx_leave(&kcov_mtx);
	buf = km_alloc(size, &kv_any, &kp_zero, &kd_waitok);
	if (buf == NULL) {
		error = ENOMEM;
		goto err;
	}
	/* km_malloc() can sleep, ensure the race was won. */
	if (kd->kd_state != KCOV_STATE_NONE) {
		error = EBUSY;
		goto err;
	}
	mtx_enter(&kcov_mtx);
	kd->kd_buf = buf;
	/* The first element is reserved to hold the number of used elements. */
	kd->kd_nmemb = nmemb - 1;
	kd->kd_size = size;
	kd->kd_state = KCOV_STATE_READY;
	return (0);

err:
	if (buf != NULL)
		km_free(buf, size, &kv_any, &kp_zero);
	mtx_enter(&kcov_mtx);
	return (error);
}

void
kd_free(struct kcov_dev *kd)
{
	struct kcov_remote *kr;

	MUTEX_ASSERT_LOCKED(&kcov_mtx);

	kr = kd->kd_kr;
	if (kr != NULL)
		kcov_remote_detach(kd, kr);

	if (kd->kd_buf != NULL) {
		mtx_leave(&kcov_mtx);
		km_free(kd->kd_buf, kd->kd_size, &kv_any, &kp_zero);
		mtx_enter(&kcov_mtx);
	}
	free(kd, M_SUBPROC, sizeof(*kd));
}

static struct kcov_dev *
kd_curproc(int mode)
{
	struct cpu_info *ci;
	struct kcov_dev *kd;

	/*
	 * Do not trace before kcovopen() has been called at least once.
	 * At this point, all secondary CPUs have booted and accessing curcpu()
	 * is safe.
	 */
	if (__predict_false(kcov_cold))
		return (NULL);

	ci = curcpu();
	kd = ci->ci_curproc->p_kd;
	if (__predict_true(kd == NULL) || kd->kd_mode != mode)
		return (NULL);

	/*
	 * Do not trace if the kernel has panicked. This could happen if curproc
	 * had kcov enabled while panicking.
	 */
	if (__predict_false(panicstr || db_active))
		return (NULL);

	/* Do not trace in interrupt context unless this is a remote section. */
	if (inintr(ci) && kd->kd_intr == 0)
		return (NULL);

	return (kd);

}

static struct kcov_cpu *
kd_curcpu(void)
{
	struct kcov_cpu *kc;
	unsigned int cpuid = cpu_number();

	TAILQ_FOREACH(kc, &kc_list, kc_entry) {
		if (kc->kc_cpuid == cpuid)
			return (kc);
	}
	return (NULL);
}

/*
 * Claim stride times nmemb number of elements in the coverage buffer. Returns
 * the index of the first claimed element. If the claim cannot be fulfilled,
 * zero is returned.
 */
static uint64_t
kd_claim(struct kcov_dev *kd, int stride, int nmemb)
{
	uint64_t idx, was;

	idx = kd->kd_buf[0];
	for (;;) {
		if (stride * (idx + nmemb) > kd->kd_nmemb)
			return (0);

		was = atomic_cas_ulong(&kd->kd_buf[0], idx, idx + nmemb);
		if (was == idx)
			return (idx * stride + 1);
		idx = was;
	}
}

void
kcov_remote_enter(int subsystem, void *id)
{
	struct cpu_info *ci;
	struct kcov_cpu *kc;
	struct kcov_dev *kd;
	struct kcov_remote *kr;
	struct proc *p;

	mtx_enter(&kcov_mtx);
	kr = kr_lookup(subsystem, id);
	if (kr == NULL || kr->kr_state != KCOV_STATE_READY)
		goto out;
	kd = kr->kr_kd;
	if (kd == NULL || kd->kd_state != KCOV_STATE_TRACE)
		goto out;
	ci = curcpu();
	p = ci->ci_curproc;
	if (inintr(ci)) {
		/*
		 * XXX we only expect to be called from softclock interrupts at
		 * this point.
		 */
		kc = kd_curcpu();
		if (kc == NULL || kc->kc_kd.kd_intr == 1)
			goto out;
		kc->kc_kd.kd_state = KCOV_STATE_TRACE;
		kc->kc_kd.kd_mode = kd->kd_mode;
		kc->kc_kd.kd_intr = 1;
		kc->kc_kd_save = p->p_kd;
		kd = &kc->kc_kd;
		/* Reset coverage buffer. */
		kd->kd_buf[0] = 0;
	} else {
		KASSERT(p->p_kd == NULL);
	}
	kr->kr_nsections++;
	p->p_kd = kd;

out:
	mtx_leave(&kcov_mtx);
}

void
kcov_remote_leave(int subsystem, void *id)
{
	struct cpu_info *ci;
	struct kcov_cpu *kc;
	struct kcov_remote *kr;
	struct proc *p;

	mtx_enter(&kcov_mtx);
	ci = curcpu();
	p = ci->ci_curproc;
	if (p->p_kd == NULL)
		goto out;
	kr = kr_lookup(subsystem, id);
	if (kr == NULL)
		goto out;
	if (inintr(ci)) {
		kc = kd_curcpu();
		if (kc == NULL || kc->kc_kd.kd_intr == 0)
			goto out;

		/*
		 * Stop writing to the coverage buffer associated with this CPU
		 * before copying its contents.
		 */
		p->p_kd = kc->kc_kd_save;
		kc->kc_kd_save = NULL;

		kd_copy(kr->kr_kd, &kc->kc_kd);
		kc->kc_kd.kd_state = KCOV_STATE_READY;
		kc->kc_kd.kd_mode = KCOV_MODE_NONE;
		kc->kc_kd.kd_intr = 0;
	} else {
		KASSERT(p->p_kd == kr->kr_kd);
		p->p_kd = NULL;
	}
	if (--kr->kr_nsections == 0)
		wakeup(kr);
out:
	mtx_leave(&kcov_mtx);
}

void
kcov_remote_register(int subsystem, void *id)
{
	mtx_enter(&kcov_mtx);
	kcov_remote_register_locked(subsystem, id);
	mtx_leave(&kcov_mtx);
}

void
kcov_remote_unregister(int subsystem, void *id)
{
	struct kcov_remote *kr;

	mtx_enter(&kcov_mtx);
	kr = kr_lookup(subsystem, id);
	if (kr != NULL)
		kr_free(kr);
	mtx_leave(&kcov_mtx);
}

struct kcov_remote *
kcov_remote_register_locked(int subsystem, void *id)
{
	struct kcov_remote *kr, *tmp;

	/* Do not allow registrations before the pool is initialized. */
	KASSERT(kr_cold == 0);

	/*
	 * Temporarily release the mutex since the allocation could end up
	 * sleeping.
	 */
	mtx_leave(&kcov_mtx);
	kr = pool_get(&kr_pool, PR_WAITOK | PR_ZERO);
	kr->kr_subsystem = subsystem;
	kr->kr_id = id;
	kr->kr_state = KCOV_STATE_NONE;
	mtx_enter(&kcov_mtx);

	for (;;) {
		tmp = kr_lookup(subsystem, id);
		if (tmp == NULL)
			break;
		if (tmp->kr_state != KCOV_STATE_DYING) {
			pool_put(&kr_pool, kr);
			return (NULL);
		}
		/*
		 * The remote could already be deregistered while another
		 * thread is currently inside a kcov remote section.
		 */
		msleep_nsec(tmp, &kcov_mtx, PWAIT, "kcov", INFSLP);
	}
	TAILQ_INSERT_TAIL(&kr_list, kr, kr_entry);
	return (kr);
}

int
kcov_remote_attach(struct kcov_dev *kd, struct kio_remote_attach *arg)
{
	struct kcov_remote *kr = NULL;

	MUTEX_ASSERT_LOCKED(&kcov_mtx);

	if (kd->kd_state != KCOV_STATE_READY)
		return (ENXIO);

	if (arg->subsystem == KCOV_REMOTE_COMMON) {
		kr = kcov_remote_register_locked(KCOV_REMOTE_COMMON,
		    curproc->p_p);
		if (kr == NULL)
			return (EBUSY);
	} else {
		return (EINVAL);
	}

	kr->kr_state = KCOV_STATE_READY;
	kr->kr_kd = kd;
	kd->kd_kr = kr;
	return (0);
}

void
kcov_remote_detach(struct kcov_dev *kd, struct kcov_remote *kr)
{
	MUTEX_ASSERT_LOCKED(&kcov_mtx);

	KASSERT(kd == kr->kr_kd);
	if (kr->kr_subsystem == KCOV_REMOTE_COMMON) {
		kr_free(kr);
	} else {
		kr->kr_state = KCOV_STATE_NONE;
		kr_barrier(kr);
		kd->kd_kr = NULL;
		kr->kr_kd = NULL;
	}
}

void
kr_free(struct kcov_remote *kr)
{
	MUTEX_ASSERT_LOCKED(&kcov_mtx);

	kr->kr_state = KCOV_STATE_DYING;
	kr_barrier(kr);
	if (kr->kr_kd != NULL)
		kr->kr_kd->kd_kr = NULL;
	kr->kr_kd = NULL;
	TAILQ_REMOVE(&kr_list, kr, kr_entry);
	/* Notify thread(s) waiting in kcov_remote_register(). */
	wakeup(kr);
	pool_put(&kr_pool, kr);
}

void
kr_barrier(struct kcov_remote *kr)
{
	MUTEX_ASSERT_LOCKED(&kcov_mtx);

	while (kr->kr_nsections > 0)
		msleep_nsec(kr, &kcov_mtx, PWAIT, "kcovbar", INFSLP);
}

struct kcov_remote *
kr_lookup(int subsystem, void *id)
{
	struct kcov_remote *kr;

	MUTEX_ASSERT_LOCKED(&kcov_mtx);

	TAILQ_FOREACH(kr, &kr_list, kr_entry) {
		if (kr->kr_subsystem == subsystem && kr->kr_id == id)
			return (kr);
	}
	return (NULL);
}
