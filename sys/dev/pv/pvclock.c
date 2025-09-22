/*	$OpenBSD: pvclock.c,v 1.15 2025/09/16 12:18:10 hshoexer Exp $	*/

/*
 * Copyright (c) 2018 Reyk Floeter <reyk@openbsd.org>
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

#if !defined(__i386__) && !defined(__amd64__)
#error pvclock(4) is only supported on i386 and amd64
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/timeout.h>
#include <sys/timetc.h>

#include <machine/cpu.h>
#include <machine/atomic.h>
#include <uvm/uvm_extern.h>

#include <dev/pv/pvvar.h>
#include <dev/pv/pvreg.h>

#ifndef PMAP_NOCRYPT
#define	PMAP_NOCRYPT	0
#endif

#if defined(__amd64__)

static inline uint64_t
pvclock_atomic_load(volatile uint64_t *ptr)
{
	return *ptr;
}

static inline uint64_t
pvclock_atomic_cas(volatile uint64_t *p, uint64_t e,
    uint64_t n)
{
	return atomic_cas_ulong((volatile unsigned long *)p, e, n);
}

#elif defined(__i386__)

/*
 * We are running on virtualization. Therefore we can assume that we
 * have cmpxchg8b, available on pentium and newer.
 */
static inline uint64_t
pvclock_atomic_load(volatile uint64_t *ptr)
{
	uint64_t val;
	__asm__ volatile ("movl %%ebx,%%eax; movl %%ecx, %%edx; "
	    "lock cmpxchg8b %1" : "=&A" (val) : "m" (*ptr));
	return val;
}

static inline uint64_t
pvclock_atomic_cas(volatile uint64_t *p, uint64_t e,
    uint64_t n)
{
	__asm volatile("lock cmpxchg8b %1" : "+A" (e), "+m" (*p)
	: "b" ((uint32_t)n), "c" ((uint32_t)(n >> 32)));
	return (e);
}

#else
#error "pvclock: unsupported x86 architecture?"
#endif


uint64_t pvclock_lastcount;

struct pvpage {
	struct pvclock_time_info	ti;
	struct pvclock_wall_clock	wc;
};

struct pvclock_softc {
	struct device		 sc_dev;
	struct pvpage		*sc_page;
	paddr_t			 sc_paddr;
	struct timecounter	*sc_tc;
	struct ksensordev	 sc_sensordev;
	struct ksensor		 sc_sensor;
	struct timeout		 sc_tick;
};

#define DEVNAME(_s)			((_s)->sc_dev.dv_xname)

int	 pvclock_match(struct device *, void *, void *);
void	 pvclock_attach(struct device *, struct device *, void *);
int	 pvclock_activate(struct device *, int);

uint64_t pvclock_get(struct timecounter *);
uint	 pvclock_get_timecount(struct timecounter *);
void	 pvclock_tick_hook(struct device *);

static inline uint32_t
	 pvclock_read_begin(const struct pvclock_time_info *);
static inline int
	 pvclock_read_done(const struct pvclock_time_info *, uint32_t);
static inline uint64_t
	 pvclock_scale_delta(uint64_t, uint32_t, int);

const struct cfattach pvclock_ca = {
	sizeof(struct pvclock_softc),
	pvclock_match,
	pvclock_attach,
	NULL,
	pvclock_activate
};

struct cfdriver pvclock_cd = {
	NULL,
	"pvclock",
	DV_DULL,
	CD_COCOVM
};

struct timecounter pvclock_timecounter = {
	.tc_get_timecount = pvclock_get_timecount,
	.tc_counter_mask = ~0u,
	.tc_frequency = 0,
	.tc_name = NULL,
	.tc_quality = -2000,
	.tc_priv = NULL,
	.tc_user = 0,
};

int
pvclock_match(struct device *parent, void *match, void *aux)
{
	struct pv_attach_args	*pva = aux;
	struct pvbus_hv		*hv;

	/*
	 * pvclock is provided by different hypervisors, we currently
	 * only support the "kvmclock".
	 */
	hv = &pva->pva_hv[PVBUS_KVM];
	if (hv->hv_base == 0)
		hv = &pva->pva_hv[PVBUS_OPENBSD];
	if (hv->hv_base != 0) {
		/*
		 * We only implement support for the 2nd version of pvclock.
		 * The first version is basically the same but with different
		 * non-standard MSRs and it is deprecated.
		 */
		if ((hv->hv_features & (1 << KVM_FEATURE_CLOCKSOURCE2)) == 0)
			return (0);

		/*
		 * Only the "stable" clock with a sync'ed TSC is supported.
		 * In this case the host guarantees that the TSC is constant
		 * and invariant, either by the underlying TSC or by passing
		 * on a synchronized value.
		 */
		if ((hv->hv_features &
		    (1 << KVM_FEATURE_CLOCKSOURCE_STABLE_BIT)) == 0)
			return (0);

		return (1);
	}

	return (0);
}

void
pvclock_attach(struct device *parent, struct device *self, void *aux)
{
	struct pvclock_softc		*sc = (struct pvclock_softc *)self;
	struct pv_attach_args		*pva = aux;
	struct pvclock_time_info	*ti;
	paddr_t				 pa;
	uint32_t			 version;
	uint8_t				 flags;
	struct vm_page			*page;
	struct pvbus_hv                 *kvm;

	page = uvm_pagealloc(NULL, 0, NULL, 0);
	if (page == NULL)
		goto err;
	sc->sc_page = km_alloc(PAGE_SIZE, &kv_any, &kp_none, &kd_nowait);
	if (sc->sc_page == NULL)
		goto err;

	pa = VM_PAGE_TO_PHYS(page);
	pmap_kenter_pa((vaddr_t)sc->sc_page, pa | PMAP_NOCRYPT,
		PROT_READ | PROT_WRITE);
	pmap_update(pmap_kernel());
	memset(sc->sc_page, 0, PAGE_SIZE);

	wrmsr(KVM_MSR_SYSTEM_TIME, pa | PVCLOCK_SYSTEM_TIME_ENABLE);
	sc->sc_paddr = pa;

	ti = &sc->sc_page->ti;
	do {
		version = pvclock_read_begin(ti);
		flags = ti->ti_flags;
	} while (!pvclock_read_done(ti, version));

	sc->sc_tc = &pvclock_timecounter;
	sc->sc_tc->tc_name = DEVNAME(sc);
	sc->sc_tc->tc_frequency = 1000000000ULL;
	sc->sc_tc->tc_priv = sc;

	pvclock_lastcount = 0;

	/* Better than HPET but below TSC */
	sc->sc_tc->tc_quality = 1500;

	if ((flags & PVCLOCK_FLAG_TSC_STABLE) == 0) {
		/* if tsc is not stable, set a lower priority */
		/* Better than i8254 but below HPET */
		sc->sc_tc->tc_quality = 500;
	}

	tc_init(sc->sc_tc);

	/*
	 * The openbsd vmm pvclock does not support the WALL_CLOCK msr,
	 * therefore we look only for kvm.
	 */
	kvm = &pva->pva_hv[PVBUS_KVM];
	if (kvm->hv_features & (1 << KVM_FEATURE_CLOCKSOURCE2)) {
		strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
		    sizeof(sc->sc_sensordev.xname));
		sc->sc_sensor.type = SENSOR_TIMEDELTA;
		sc->sc_sensor.status = SENSOR_S_UNKNOWN;
		sensor_attach(&sc->sc_sensordev, &sc->sc_sensor);
		sensordev_install(&sc->sc_sensordev);

		config_mountroot(self, pvclock_tick_hook);
	}

	printf("\n");
	return;
err:
	if (page)
		uvm_pagefree(page);
	printf(": time page allocation failed\n");
}

int
pvclock_activate(struct device *self, int act)
{
	struct pvclock_softc	*sc = (struct pvclock_softc *)self;
	int			 rv = 0;
	paddr_t			 pa = sc->sc_paddr;

	switch (act) {
	case DVACT_POWERDOWN:
		wrmsr(KVM_MSR_SYSTEM_TIME, pa & ~PVCLOCK_SYSTEM_TIME_ENABLE);
		break;
	case DVACT_RESUME:
		wrmsr(KVM_MSR_SYSTEM_TIME, pa | PVCLOCK_SYSTEM_TIME_ENABLE);
		break;
	}

	return (rv);
}

static inline uint32_t
pvclock_read_begin(const struct pvclock_time_info *ti)
{
	uint32_t version = ti->ti_version & ~0x1;
	virtio_membar_sync();
	return (version);
}

static inline int
pvclock_read_done(const struct pvclock_time_info *ti,
    uint32_t version)
{
	virtio_membar_sync();
	return (ti->ti_version == version);
}

static inline uint64_t
pvclock_scale_delta(uint64_t delta, uint32_t mul_frac, int shift)
{
	uint64_t lower, upper;

	if (shift < 0)
		delta >>= -shift;
	else
		delta <<= shift;

	lower = ((uint64_t)mul_frac * ((uint32_t)delta)) >> 32;
	upper = (uint64_t)mul_frac * (delta >> 32);
	return lower + upper;
}

static uint64_t
pvclock_cmp_last(uint64_t ctr)
{
	uint64_t last;

	do {
		last = pvclock_atomic_load(&pvclock_lastcount);
		if (ctr < last)
			return (last);
	} while (pvclock_atomic_cas(&pvclock_lastcount, last, ctr) != last);
	return (ctr);
}

uint64_t
pvclock_get(struct timecounter *tc)
{
	struct pvclock_softc		*sc = tc->tc_priv;
	struct pvclock_time_info	*ti;
	uint64_t			 tsc_timestamp, system_time, delta, ctr;
	uint32_t			 version, mul_frac;
	int8_t				 shift;
	uint8_t				 flags;
	int				 s;

	ti = &sc->sc_page->ti;
	s = splhigh();
	do {
		version = pvclock_read_begin(ti);
		system_time = ti->ti_system_time;
		tsc_timestamp = ti->ti_tsc_timestamp;
		mul_frac = ti->ti_tsc_to_system_mul;
		shift = ti->ti_tsc_shift;
		flags = ti->ti_flags;
		delta = rdtsc_lfence();
	} while (!pvclock_read_done(ti, version));
	splx(s);

	/*
	 * The algorithm is described in
	 * linux/Documentation/virt/kvm/x86/msr.rst
	 */
	if (delta > tsc_timestamp)
		delta -= tsc_timestamp;
	else
		delta = 0;
	ctr = pvclock_scale_delta(delta, mul_frac, shift) + system_time;

	if ((flags & PVCLOCK_FLAG_TSC_STABLE) != 0)
		return (ctr);

	return pvclock_cmp_last(ctr);
}

uint
pvclock_get_timecount(struct timecounter *tc)
{
	return (pvclock_get(tc));
}

void
pvclock_tick(void *arg)
{
	struct pvclock_softc		*sc = arg;
	struct timespec			 ts;
	struct pvclock_wall_clock	*wc = &sc->sc_page->wc;
	int64_t				 value;

	wrmsr(KVM_MSR_WALL_CLOCK, sc->sc_paddr + offsetof(struct pvpage, wc));
	while (wc->wc_version & 0x1)
		virtio_membar_sync();
	if (wc->wc_sec) {
		nanotime(&ts);
		value = TIMESPEC_TO_NSEC(&ts) -
		    SEC_TO_NSEC(wc->wc_sec) - wc->wc_nsec -
		    pvclock_get(&pvclock_timecounter);

		TIMESPEC_TO_TIMEVAL(&sc->sc_sensor.tv, &ts);
		sc->sc_sensor.value = value;
		sc->sc_sensor.status = SENSOR_S_OK;
	} else
		sc->sc_sensor.status = SENSOR_S_UNKNOWN;

	timeout_add_sec(&sc->sc_tick, 15);
}

void
pvclock_tick_hook(struct device *self)
{
	struct pvclock_softc	*sc = (struct pvclock_softc *)self;

	timeout_set(&sc->sc_tick, pvclock_tick, sc);
	pvclock_tick(sc);
}
