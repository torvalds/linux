/*-
 * Copyright (c) 2016-2017 Microsoft Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/timetc.h>
#include <sys/vdso.h>

#include <machine/cpufunc.h>
#include <machine/cputypes.h>
#include <machine/md_var.h>
#include <machine/specialreg.h>

#include <vm/vm.h>

#include <dev/hyperv/include/hyperv.h>
#include <dev/hyperv/include/hyperv_busdma.h>
#include <dev/hyperv/vmbus/hyperv_machdep.h>
#include <dev/hyperv/vmbus/hyperv_reg.h>
#include <dev/hyperv/vmbus/hyperv_var.h>

struct hyperv_reftsc_ctx {
	struct hyperv_reftsc	*tsc_ref;
	struct hyperv_dma	tsc_ref_dma;
};

static uint32_t			hyperv_tsc_vdso_timehands(
				    struct vdso_timehands *,
				    struct timecounter *);

static d_open_t			hyperv_tsc_open;
static d_mmap_t			hyperv_tsc_mmap;

static struct timecounter	hyperv_tsc_timecounter = {
	.tc_get_timecount	= NULL,	/* based on CPU vendor. */
	.tc_counter_mask	= 0xffffffff,
	.tc_frequency		= HYPERV_TIMER_FREQ,
	.tc_name		= "Hyper-V-TSC",
	.tc_quality		= 3000,
	.tc_fill_vdso_timehands = hyperv_tsc_vdso_timehands,
};

static struct cdevsw		hyperv_tsc_cdevsw = {
	.d_version		= D_VERSION,
	.d_open			= hyperv_tsc_open,
	.d_mmap			= hyperv_tsc_mmap,
	.d_name			= HYPERV_REFTSC_DEVNAME
};

static struct hyperv_reftsc_ctx	hyperv_ref_tsc;

uint64_t
hypercall_md(volatile void *hc_addr, uint64_t in_val,
    uint64_t in_paddr, uint64_t out_paddr)
{
	uint64_t status;

	__asm__ __volatile__ ("mov %0, %%r8" : : "r" (out_paddr): "r8");
	__asm__ __volatile__ ("call *%3" : "=a" (status) :
	    "c" (in_val), "d" (in_paddr), "m" (hc_addr));
	return (status);
}

static int
hyperv_tsc_open(struct cdev *dev __unused, int oflags, int devtype __unused,
    struct thread *td __unused)
{

	if (oflags & FWRITE)
		return (EPERM);
	return (0);
}

static int
hyperv_tsc_mmap(struct cdev *dev __unused, vm_ooffset_t offset,
    vm_paddr_t *paddr, int nprot __unused, vm_memattr_t *memattr __unused)
{

	KASSERT(hyperv_ref_tsc.tsc_ref != NULL, ("reftsc has not been setup"));

	/*
	 * NOTE:
	 * 'nprot' does not contain information interested to us;
	 * WR-open is blocked by d_open.
	 */

	if (offset != 0)
		return (EOPNOTSUPP);

	*paddr = hyperv_ref_tsc.tsc_ref_dma.hv_paddr;
	return (0);
}

static uint32_t
hyperv_tsc_vdso_timehands(struct vdso_timehands *vdso_th,
    struct timecounter *tc __unused)
{

	vdso_th->th_algo = VDSO_TH_ALGO_X86_HVTSC;
	vdso_th->th_x86_shift = 0;
	vdso_th->th_x86_hpet_idx = 0;
	bzero(vdso_th->th_res, sizeof(vdso_th->th_res));
	return (1);
}

#define HYPERV_TSC_TIMECOUNT(fence)					\
static uint64_t								\
hyperv_tc64_tsc_##fence(void)						\
{									\
	struct hyperv_reftsc *tsc_ref = hyperv_ref_tsc.tsc_ref;		\
	uint32_t seq;							\
									\
	while ((seq = atomic_load_acq_int(&tsc_ref->tsc_seq)) != 0) {	\
		uint64_t disc, ret, tsc;				\
		uint64_t scale = tsc_ref->tsc_scale;			\
		int64_t ofs = tsc_ref->tsc_ofs;				\
									\
		fence();						\
		tsc = rdtsc();						\
									\
		/* ret = ((tsc * scale) >> 64) + ofs */			\
		__asm__ __volatile__ ("mulq %3" :			\
		    "=d" (ret), "=a" (disc) :				\
		    "a" (tsc), "r" (scale));				\
		ret += ofs;						\
									\
		atomic_thread_fence_acq();				\
		if (tsc_ref->tsc_seq == seq)				\
			return (ret);					\
									\
		/* Sequence changed; re-sync. */			\
	}								\
	/* Fallback to the generic timecounter, i.e. rdmsr. */		\
	return (rdmsr(MSR_HV_TIME_REF_COUNT));				\
}									\
									\
static u_int								\
hyperv_tsc_timecount_##fence(struct timecounter *tc __unused)		\
{									\
									\
	return (hyperv_tc64_tsc_##fence());				\
}									\
struct __hack

HYPERV_TSC_TIMECOUNT(lfence);
HYPERV_TSC_TIMECOUNT(mfence);

static void
hyperv_tsc_tcinit(void *dummy __unused)
{
	hyperv_tc64_t tc64 = NULL;
	uint64_t val, orig;

	if ((hyperv_features &
	     (CPUID_HV_MSR_TIME_REFCNT | CPUID_HV_MSR_REFERENCE_TSC)) !=
	    (CPUID_HV_MSR_TIME_REFCNT | CPUID_HV_MSR_REFERENCE_TSC) ||
	    (cpu_feature & CPUID_SSE2) == 0)	/* SSE2 for mfence/lfence */
		return;

	switch (cpu_vendor_id) {
	case CPU_VENDOR_AMD:
		hyperv_tsc_timecounter.tc_get_timecount =
		    hyperv_tsc_timecount_mfence;
		tc64 = hyperv_tc64_tsc_mfence;
		break;

	case CPU_VENDOR_INTEL:
		hyperv_tsc_timecounter.tc_get_timecount =
		    hyperv_tsc_timecount_lfence;
		tc64 = hyperv_tc64_tsc_lfence;
		break;

	default:
		/* Unsupport CPU vendors. */
		return;
	}

	hyperv_ref_tsc.tsc_ref = hyperv_dmamem_alloc(NULL, PAGE_SIZE, 0,
	    sizeof(struct hyperv_reftsc), &hyperv_ref_tsc.tsc_ref_dma,
	    BUS_DMA_WAITOK | BUS_DMA_ZERO);
	if (hyperv_ref_tsc.tsc_ref == NULL) {
		printf("hyperv: reftsc page allocation failed\n");
		return;
	}

	orig = rdmsr(MSR_HV_REFERENCE_TSC);
	val = MSR_HV_REFTSC_ENABLE | (orig & MSR_HV_REFTSC_RSVD_MASK) |
	    ((hyperv_ref_tsc.tsc_ref_dma.hv_paddr >> PAGE_SHIFT) <<
	     MSR_HV_REFTSC_PGSHIFT);
	wrmsr(MSR_HV_REFERENCE_TSC, val);

	/* Register "enlightened" timecounter. */
	tc_init(&hyperv_tsc_timecounter);

	/* Install 64 bits timecounter method for other modules to use. */
	KASSERT(tc64 != NULL, ("tc64 is not set"));
	hyperv_tc64 = tc64;

	/* Add device for mmap(2). */
	make_dev(&hyperv_tsc_cdevsw, 0, UID_ROOT, GID_WHEEL, 0444,
	    HYPERV_REFTSC_DEVNAME);
}
SYSINIT(hyperv_tsc_init, SI_SUB_DRIVERS, SI_ORDER_FIRST, hyperv_tsc_tcinit,
    NULL);
