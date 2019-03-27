/*-
 * Copyright (c) 2005 Poul-Henning Kamp
 * Copyright (c) 2010 Alexander Motin <mav@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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
#include <sys/param.h>
#include <sys/conf.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/rman.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/smp.h>
#include <sys/sysctl.h>
#include <sys/timeet.h>
#include <sys/timetc.h>
#include <sys/vdso.h>

#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>

#include <dev/acpica/acpivar.h>
#include <dev/acpica/acpi_hpet.h>

#ifdef DEV_APIC
#include "pcib_if.h"
#endif

#define HPET_VENDID_AMD		0x4353
#define HPET_VENDID_AMD2	0x1022
#define HPET_VENDID_INTEL	0x8086
#define HPET_VENDID_NVIDIA	0x10de
#define HPET_VENDID_SW		0x1166

ACPI_SERIAL_DECL(hpet, "ACPI HPET support");

static devclass_t hpet_devclass;

/* ACPI CA debugging */
#define _COMPONENT	ACPI_TIMER
ACPI_MODULE_NAME("HPET")

struct hpet_softc {
	device_t		dev;
	int			mem_rid;
	int			intr_rid;
	int			irq;
	int			useirq;
	int			legacy_route;
	int			per_cpu;
	uint32_t		allowed_irqs;
	struct resource		*mem_res;
	struct resource		*intr_res;
	void			*intr_handle;
	ACPI_HANDLE		handle;
	uint32_t		acpi_uid;
	uint64_t		freq;
	uint32_t		caps;
	struct timecounter	tc;
	struct hpet_timer {
		struct eventtimer	et;
		struct hpet_softc	*sc;
		int			num;
		int			mode;
#define	TIMER_STOPPED	0
#define	TIMER_PERIODIC	1
#define	TIMER_ONESHOT	2
		int			intr_rid;
		int			irq;
		int			pcpu_cpu;
		int			pcpu_misrouted;
		int			pcpu_master;
		int			pcpu_slaves[MAXCPU];
		struct resource		*intr_res;
		void			*intr_handle;
		uint32_t		caps;
		uint32_t		vectors;
		uint32_t		div;
		uint32_t		next;
		char			name[8];
	} 			t[32];
	int			num_timers;
	struct cdev		*pdev;
	int			mmap_allow;
	int			mmap_allow_write;
};

static d_open_t hpet_open;
static d_mmap_t hpet_mmap;

static struct cdevsw hpet_cdevsw = {
	.d_version =	D_VERSION,
	.d_name =	"hpet",
	.d_open =	hpet_open,
	.d_mmap =	hpet_mmap,
};

static u_int hpet_get_timecount(struct timecounter *tc);
static void hpet_test(struct hpet_softc *sc);

static char *hpet_ids[] = { "PNP0103", NULL };

/* Knob to disable acpi_hpet device */
bool acpi_hpet_disabled = false;

static u_int
hpet_get_timecount(struct timecounter *tc)
{
	struct hpet_softc *sc;

	sc = tc->tc_priv;
	return (bus_read_4(sc->mem_res, HPET_MAIN_COUNTER));
}

uint32_t
hpet_vdso_timehands(struct vdso_timehands *vdso_th, struct timecounter *tc)
{
	struct hpet_softc *sc;

	sc = tc->tc_priv;
	vdso_th->th_algo = VDSO_TH_ALGO_X86_HPET;
	vdso_th->th_x86_shift = 0;
	vdso_th->th_x86_hpet_idx = device_get_unit(sc->dev);
	bzero(vdso_th->th_res, sizeof(vdso_th->th_res));
	return (sc->mmap_allow != 0);
}

#ifdef COMPAT_FREEBSD32
uint32_t
hpet_vdso_timehands32(struct vdso_timehands32 *vdso_th32,
    struct timecounter *tc)
{
	struct hpet_softc *sc;

	sc = tc->tc_priv;
	vdso_th32->th_algo = VDSO_TH_ALGO_X86_HPET;
	vdso_th32->th_x86_shift = 0;
	vdso_th32->th_x86_hpet_idx = device_get_unit(sc->dev);
	bzero(vdso_th32->th_res, sizeof(vdso_th32->th_res));
	return (sc->mmap_allow != 0);
}
#endif

static void
hpet_enable(struct hpet_softc *sc)
{
	uint32_t val;

	val = bus_read_4(sc->mem_res, HPET_CONFIG);
	if (sc->legacy_route)
		val |= HPET_CNF_LEG_RT;
	else
		val &= ~HPET_CNF_LEG_RT;
	val |= HPET_CNF_ENABLE;
	bus_write_4(sc->mem_res, HPET_CONFIG, val);
}

static void
hpet_disable(struct hpet_softc *sc)
{
	uint32_t val;

	val = bus_read_4(sc->mem_res, HPET_CONFIG);
	val &= ~HPET_CNF_ENABLE;
	bus_write_4(sc->mem_res, HPET_CONFIG, val);
}

static int
hpet_start(struct eventtimer *et, sbintime_t first, sbintime_t period)
{
	struct hpet_timer *mt = (struct hpet_timer *)et->et_priv;
	struct hpet_timer *t;
	struct hpet_softc *sc = mt->sc;
	uint32_t fdiv, now;

	t = (mt->pcpu_master < 0) ? mt : &sc->t[mt->pcpu_slaves[curcpu]];
	if (period != 0) {
		t->mode = TIMER_PERIODIC;
		t->div = (sc->freq * period) >> 32;
	} else {
		t->mode = TIMER_ONESHOT;
		t->div = 0;
	}
	if (first != 0)
		fdiv = (sc->freq * first) >> 32;
	else
		fdiv = t->div;
	if (t->irq < 0)
		bus_write_4(sc->mem_res, HPET_ISR, 1 << t->num);
	t->caps |= HPET_TCNF_INT_ENB;
	now = bus_read_4(sc->mem_res, HPET_MAIN_COUNTER);
restart:
	t->next = now + fdiv;
	if (t->mode == TIMER_PERIODIC && (t->caps & HPET_TCAP_PER_INT)) {
		t->caps |= HPET_TCNF_TYPE;
		bus_write_4(sc->mem_res, HPET_TIMER_CAP_CNF(t->num),
		    t->caps | HPET_TCNF_VAL_SET);
		bus_write_4(sc->mem_res, HPET_TIMER_COMPARATOR(t->num),
		    t->next);
		bus_write_4(sc->mem_res, HPET_TIMER_COMPARATOR(t->num),
		    t->div);
	} else {
		t->caps &= ~HPET_TCNF_TYPE;
		bus_write_4(sc->mem_res, HPET_TIMER_CAP_CNF(t->num),
		    t->caps);
		bus_write_4(sc->mem_res, HPET_TIMER_COMPARATOR(t->num),
		    t->next);
	}
	now = bus_read_4(sc->mem_res, HPET_MAIN_COUNTER);
	if ((int32_t)(now - t->next + HPET_MIN_CYCLES) >= 0) {
		fdiv *= 2;
		goto restart;
	}
	return (0);
}

static int
hpet_stop(struct eventtimer *et)
{
	struct hpet_timer *mt = (struct hpet_timer *)et->et_priv;
	struct hpet_timer *t;
	struct hpet_softc *sc = mt->sc;

	t = (mt->pcpu_master < 0) ? mt : &sc->t[mt->pcpu_slaves[curcpu]];
	t->mode = TIMER_STOPPED;
	t->caps &= ~(HPET_TCNF_INT_ENB | HPET_TCNF_TYPE);
	bus_write_4(sc->mem_res, HPET_TIMER_CAP_CNF(t->num), t->caps);
	return (0);
}

static int
hpet_intr_single(void *arg)
{
	struct hpet_timer *t = (struct hpet_timer *)arg;
	struct hpet_timer *mt;
	struct hpet_softc *sc = t->sc;
	uint32_t now;

	if (t->mode == TIMER_STOPPED)
		return (FILTER_STRAY);
	/* Check that per-CPU timer interrupt reached right CPU. */
	if (t->pcpu_cpu >= 0 && t->pcpu_cpu != curcpu) {
		if ((++t->pcpu_misrouted) % 32 == 0) {
			printf("HPET interrupt routed to the wrong CPU"
			    " (timer %d CPU %d -> %d)!\n",
			    t->num, t->pcpu_cpu, curcpu);
		}

		/*
		 * Reload timer, hoping that next time may be more lucky
		 * (system will manage proper interrupt binding).
		 */
		if ((t->mode == TIMER_PERIODIC &&
		    (t->caps & HPET_TCAP_PER_INT) == 0) ||
		    t->mode == TIMER_ONESHOT) {
			t->next = bus_read_4(sc->mem_res, HPET_MAIN_COUNTER) +
			    sc->freq / 8;
			bus_write_4(sc->mem_res, HPET_TIMER_COMPARATOR(t->num),
			    t->next);
		}
		return (FILTER_HANDLED);
	}
	if (t->mode == TIMER_PERIODIC &&
	    (t->caps & HPET_TCAP_PER_INT) == 0) {
		t->next += t->div;
		now = bus_read_4(sc->mem_res, HPET_MAIN_COUNTER);
		if ((int32_t)((now + t->div / 2) - t->next) > 0)
			t->next = now + t->div / 2;
		bus_write_4(sc->mem_res,
		    HPET_TIMER_COMPARATOR(t->num), t->next);
	} else if (t->mode == TIMER_ONESHOT)
		t->mode = TIMER_STOPPED;
	mt = (t->pcpu_master < 0) ? t : &sc->t[t->pcpu_master];
	if (mt->et.et_active)
		mt->et.et_event_cb(&mt->et, mt->et.et_arg);
	return (FILTER_HANDLED);
}

static int
hpet_intr(void *arg)
{
	struct hpet_softc *sc = (struct hpet_softc *)arg;
	int i;
	uint32_t val;

	val = bus_read_4(sc->mem_res, HPET_ISR);
	if (val) {
		bus_write_4(sc->mem_res, HPET_ISR, val);
		val &= sc->useirq;
		for (i = 0; i < sc->num_timers; i++) {
			if ((val & (1 << i)) == 0)
				continue;
			hpet_intr_single(&sc->t[i]);
		}
		return (FILTER_HANDLED);
	}
	return (FILTER_STRAY);
}

uint32_t
hpet_get_uid(device_t dev)
{
	struct hpet_softc *sc;

	sc = device_get_softc(dev);
	return (sc->acpi_uid);
}

static ACPI_STATUS
hpet_find(ACPI_HANDLE handle, UINT32 level, void *context,
    void **status)
{
	char 		**ids;
	uint32_t	id = (uint32_t)(uintptr_t)context;
	uint32_t	uid = 0;

	for (ids = hpet_ids; *ids != NULL; ids++) {
		if (acpi_MatchHid(handle, *ids))
		        break;
	}
	if (*ids == NULL)
		return (AE_OK);
	if (ACPI_FAILURE(acpi_GetInteger(handle, "_UID", &uid)) ||
	    id == uid)
		*status = acpi_get_device(handle);
	return (AE_OK);
}

/*
 * Find an existing IRQ resource that matches the requested IRQ range
 * and return its RID.  If one is not found, use a new RID.
 */
static int
hpet_find_irq_rid(device_t dev, u_long start, u_long end)
{
	rman_res_t irq;
	int error, rid;

	for (rid = 0;; rid++) {
		error = bus_get_resource(dev, SYS_RES_IRQ, rid, &irq, NULL);
		if (error != 0 || (start <= irq && irq <= end))
			return (rid);
	}
}

static int
hpet_open(struct cdev *cdev, int oflags, int devtype, struct thread *td)
{
	struct hpet_softc *sc;

	sc = cdev->si_drv1;
	if (!sc->mmap_allow)
		return (EPERM);
	else
		return (0);
}

static int
hpet_mmap(struct cdev *cdev, vm_ooffset_t offset, vm_paddr_t *paddr,
    int nprot, vm_memattr_t *memattr)
{
	struct hpet_softc *sc;

	sc = cdev->si_drv1;
	if (offset >= rman_get_size(sc->mem_res))
		return (EINVAL);
	if (!sc->mmap_allow_write && (nprot & PROT_WRITE))
		return (EPERM);
	*paddr = rman_get_start(sc->mem_res) + offset;
	*memattr = VM_MEMATTR_UNCACHEABLE;

	return (0);
}

/* Discover the HPET via the ACPI table of the same name. */
static void
hpet_identify(driver_t *driver, device_t parent)
{
	ACPI_TABLE_HPET *hpet;
	ACPI_STATUS	status;
	device_t	child;
	int		i;

	/* Only one HPET device can be added. */
	if (devclass_get_device(hpet_devclass, 0))
		return;
	for (i = 1; ; i++) {
		/* Search for HPET table. */
		status = AcpiGetTable(ACPI_SIG_HPET, i, (ACPI_TABLE_HEADER **)&hpet);
		if (ACPI_FAILURE(status))
			return;
		/* Search for HPET device with same ID. */
		child = NULL;
		AcpiWalkNamespace(ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT,
		    100, hpet_find, NULL, (void *)(uintptr_t)hpet->Sequence,
		    (void *)&child);
		/* If found - let it be probed in normal way. */
		if (child) {
			if (bus_get_resource(child, SYS_RES_MEMORY, 0,
			    NULL, NULL) != 0)
				bus_set_resource(child, SYS_RES_MEMORY, 0,
				    hpet->Address.Address, HPET_MEM_WIDTH);
			continue;
		}
		/* If not - create it from table info. */
		child = BUS_ADD_CHILD(parent, 2, "hpet", 0);
		if (child == NULL) {
			printf("%s: can't add child\n", __func__);
			continue;
		}
		bus_set_resource(child, SYS_RES_MEMORY, 0, hpet->Address.Address,
		    HPET_MEM_WIDTH);
	}
}

static int
hpet_probe(device_t dev)
{
	int rv;

	ACPI_FUNCTION_TRACE((char *)(uintptr_t) __func__);
	if (acpi_disabled("hpet") || acpi_hpet_disabled)
		return (ENXIO);
	rv = ACPI_ID_PROBE(device_get_parent(dev), dev, hpet_ids, NULL);
	if (rv <= 0)
	   device_set_desc(dev, "High Precision Event Timer");
	return (rv);
}

static int
hpet_attach(device_t dev)
{
	struct hpet_softc *sc;
	struct hpet_timer *t;
	struct make_dev_args mda;
	int i, j, num_msi, num_timers, num_percpu_et, num_percpu_t, cur_cpu;
	int pcpu_master, error;
	static int maxhpetet = 0;
	uint32_t val, val2, cvectors, dvectors;
	uint16_t vendor, rev;

	ACPI_FUNCTION_TRACE((char *)(uintptr_t) __func__);

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->handle = acpi_get_handle(dev);

	sc->mem_rid = 0;
	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &sc->mem_rid,
	    RF_ACTIVE);
	if (sc->mem_res == NULL)
		return (ENOMEM);

	/* Validate that we can access the whole region. */
	if (rman_get_size(sc->mem_res) < HPET_MEM_WIDTH) {
		device_printf(dev, "memory region width %jd too small\n",
		    rman_get_size(sc->mem_res));
		bus_free_resource(dev, SYS_RES_MEMORY, sc->mem_res);
		return (ENXIO);
	}

	/* Be sure timer is enabled. */
	hpet_enable(sc);

	/* Read basic statistics about the timer. */
	val = bus_read_4(sc->mem_res, HPET_PERIOD);
	if (val == 0) {
		device_printf(dev, "invalid period\n");
		hpet_disable(sc);
		bus_free_resource(dev, SYS_RES_MEMORY, sc->mem_res);
		return (ENXIO);
	}

	sc->freq = (1000000000000000LL + val / 2) / val;
	sc->caps = bus_read_4(sc->mem_res, HPET_CAPABILITIES);
	vendor = (sc->caps & HPET_CAP_VENDOR_ID) >> 16;
	rev = sc->caps & HPET_CAP_REV_ID;
	num_timers = 1 + ((sc->caps & HPET_CAP_NUM_TIM) >> 8);
	/*
	 * ATI/AMD violates IA-PC HPET (High Precision Event Timers)
	 * Specification and provides an off by one number
	 * of timers/comparators.
	 * Additionally, they use unregistered value in VENDOR_ID field.
	 */
	if (vendor == HPET_VENDID_AMD && rev < 0x10 && num_timers > 0)
		num_timers--;
	sc->num_timers = num_timers;
	if (bootverbose) {
		device_printf(dev,
		    "vendor 0x%x, rev 0x%x, %jdHz%s, %d timers,%s\n",
		    vendor, rev, sc->freq,
		    (sc->caps & HPET_CAP_COUNT_SIZE) ? " 64bit" : "",
		    num_timers,
		    (sc->caps & HPET_CAP_LEG_RT) ? " legacy route" : "");
	}
	for (i = 0; i < num_timers; i++) {
		t = &sc->t[i];
		t->sc = sc;
		t->num = i;
		t->mode = TIMER_STOPPED;
		t->intr_rid = -1;
		t->irq = -1;
		t->pcpu_cpu = -1;
		t->pcpu_misrouted = 0;
		t->pcpu_master = -1;
		t->caps = bus_read_4(sc->mem_res, HPET_TIMER_CAP_CNF(i));
		t->vectors = bus_read_4(sc->mem_res, HPET_TIMER_CAP_CNF(i) + 4);
		if (bootverbose) {
			device_printf(dev,
			    " t%d: irqs 0x%08x (%d)%s%s%s\n", i,
			    t->vectors, (t->caps & HPET_TCNF_INT_ROUTE) >> 9,
			    (t->caps & HPET_TCAP_FSB_INT_DEL) ? ", MSI" : "",
			    (t->caps & HPET_TCAP_SIZE) ? ", 64bit" : "",
			    (t->caps & HPET_TCAP_PER_INT) ? ", periodic" : "");
		}
	}
	if (testenv("debug.acpi.hpet_test"))
		hpet_test(sc);
	/*
	 * Don't attach if the timer never increments.  Since the spec
	 * requires it to be at least 10 MHz, it has to change in 1 us.
	 */
	val = bus_read_4(sc->mem_res, HPET_MAIN_COUNTER);
	DELAY(1);
	val2 = bus_read_4(sc->mem_res, HPET_MAIN_COUNTER);
	if (val == val2) {
		device_printf(dev, "HPET never increments, disabling\n");
		hpet_disable(sc);
		bus_free_resource(dev, SYS_RES_MEMORY, sc->mem_res);
		return (ENXIO);
	}
	/* Announce first HPET as timecounter. */
	if (device_get_unit(dev) == 0) {
		sc->tc.tc_get_timecount = hpet_get_timecount,
		sc->tc.tc_counter_mask = ~0u,
		sc->tc.tc_name = "HPET",
		sc->tc.tc_quality = 950,
		sc->tc.tc_frequency = sc->freq;
		sc->tc.tc_priv = sc;
		sc->tc.tc_fill_vdso_timehands = hpet_vdso_timehands;
#ifdef COMPAT_FREEBSD32
		sc->tc.tc_fill_vdso_timehands32 = hpet_vdso_timehands32;
#endif
		tc_init(&sc->tc);
	}
	/* If not disabled - setup and announce event timers. */
	if (resource_int_value(device_get_name(dev), device_get_unit(dev),
	     "clock", &i) == 0 && i == 0)
	        return (0);

	/* Check whether we can and want legacy routing. */
	sc->legacy_route = 0;
	resource_int_value(device_get_name(dev), device_get_unit(dev),
	     "legacy_route", &sc->legacy_route);
	if ((sc->caps & HPET_CAP_LEG_RT) == 0)
		sc->legacy_route = 0;
	if (sc->legacy_route) {
		sc->t[0].vectors = 0;
		sc->t[1].vectors = 0;
	}

	/* Check what IRQs we want use. */
	/* By default allow any PCI IRQs. */
	sc->allowed_irqs = 0xffff0000;
	/*
	 * HPETs in AMD chipsets before SB800 have problems with IRQs >= 16
	 * Lower are also not always working for different reasons.
	 * SB800 fixed it, but seems do not implements level triggering
	 * properly, that makes it very unreliable - it freezes after any
	 * interrupt loss. Avoid legacy IRQs for AMD.
	 */
	if (vendor == HPET_VENDID_AMD || vendor == HPET_VENDID_AMD2)
		sc->allowed_irqs = 0x00000000;
	/*
	 * NVidia MCP5x chipsets have number of unexplained interrupt
	 * problems. For some reason, using HPET interrupts breaks HDA sound.
	 */
	if (vendor == HPET_VENDID_NVIDIA && rev <= 0x01)
		sc->allowed_irqs = 0x00000000;
	/*
	 * ServerWorks HT1000 reported to have problems with IRQs >= 16.
	 * Lower IRQs are working, but allowed mask is not set correctly.
	 * Legacy_route mode works fine.
	 */
	if (vendor == HPET_VENDID_SW && rev <= 0x01)
		sc->allowed_irqs = 0x00000000;
	/*
	 * Neither QEMU nor VirtualBox report supported IRQs correctly.
	 * The only way to use HPET there is to specify IRQs manually
	 * and/or use legacy_route. Legacy_route mode works on both.
	 */
	if (vm_guest)
		sc->allowed_irqs = 0x00000000;
	/* Let user override. */
	resource_int_value(device_get_name(dev), device_get_unit(dev),
	     "allowed_irqs", &sc->allowed_irqs);

	/* Get how much per-CPU timers we should try to provide. */
	sc->per_cpu = 1;
	resource_int_value(device_get_name(dev), device_get_unit(dev),
	     "per_cpu", &sc->per_cpu);

	num_msi = 0;
	sc->useirq = 0;
	/* Find IRQ vectors for all timers. */
	cvectors = sc->allowed_irqs & 0xffff0000;
	dvectors = sc->allowed_irqs & 0x0000ffff;
	if (sc->legacy_route)
		dvectors &= 0x0000fefe;
	for (i = 0; i < num_timers; i++) {
		t = &sc->t[i];
		if (sc->legacy_route && i < 2)
			t->irq = (i == 0) ? 0 : 8;
#ifdef DEV_APIC
		else if (t->caps & HPET_TCAP_FSB_INT_DEL) {
			if ((j = PCIB_ALLOC_MSIX(
			    device_get_parent(device_get_parent(dev)), dev,
			    &t->irq))) {
				device_printf(dev,
				    "Can't allocate interrupt for t%d: %d\n",
				    i, j);
			}
		}
#endif
		else if (dvectors & t->vectors) {
			t->irq = ffs(dvectors & t->vectors) - 1;
			dvectors &= ~(1 << t->irq);
		}
		if (t->irq >= 0) {
			t->intr_rid = hpet_find_irq_rid(dev, t->irq, t->irq);
			t->intr_res = bus_alloc_resource(dev, SYS_RES_IRQ,
			    &t->intr_rid, t->irq, t->irq, 1, RF_ACTIVE);
			if (t->intr_res == NULL) {
				t->irq = -1;
				device_printf(dev,
				    "Can't map interrupt for t%d.\n", i);
			} else if (bus_setup_intr(dev, t->intr_res,
			    INTR_TYPE_CLK, hpet_intr_single, NULL, t,
			    &t->intr_handle) != 0) {
				t->irq = -1;
				device_printf(dev,
				    "Can't setup interrupt for t%d.\n", i);
			} else {
				bus_describe_intr(dev, t->intr_res,
				    t->intr_handle, "t%d", i);
				num_msi++;
			}
		}
		if (t->irq < 0 && (cvectors & t->vectors) != 0) {
			cvectors &= t->vectors;
			sc->useirq |= (1 << i);
		}
	}
	if (sc->legacy_route && sc->t[0].irq < 0 && sc->t[1].irq < 0)
		sc->legacy_route = 0;
	if (sc->legacy_route)
		hpet_enable(sc);
	/* Group timers for per-CPU operation. */
	num_percpu_et = min(num_msi / mp_ncpus, sc->per_cpu);
	num_percpu_t = num_percpu_et * mp_ncpus;
	pcpu_master = 0;
	cur_cpu = CPU_FIRST();
	for (i = 0; i < num_timers; i++) {
		t = &sc->t[i];
		if (t->irq >= 0 && num_percpu_t > 0) {
			if (cur_cpu == CPU_FIRST())
				pcpu_master = i;
			t->pcpu_cpu = cur_cpu;
			t->pcpu_master = pcpu_master;
			sc->t[pcpu_master].
			    pcpu_slaves[cur_cpu] = i;
			bus_bind_intr(dev, t->intr_res, cur_cpu);
			cur_cpu = CPU_NEXT(cur_cpu);
			num_percpu_t--;
		} else if (t->irq >= 0)
			bus_bind_intr(dev, t->intr_res, CPU_FIRST());
	}
	bus_write_4(sc->mem_res, HPET_ISR, 0xffffffff);
	sc->irq = -1;
	/* If at least one timer needs legacy IRQ - set it up. */
	if (sc->useirq) {
		j = i = fls(cvectors) - 1;
		while (j > 0 && (cvectors & (1 << (j - 1))) != 0)
			j--;
		sc->intr_rid = hpet_find_irq_rid(dev, j, i);
		sc->intr_res = bus_alloc_resource(dev, SYS_RES_IRQ,
		    &sc->intr_rid, j, i, 1, RF_SHAREABLE | RF_ACTIVE);
		if (sc->intr_res == NULL)
			device_printf(dev, "Can't map interrupt.\n");
		else if (bus_setup_intr(dev, sc->intr_res, INTR_TYPE_CLK,
		    hpet_intr, NULL, sc, &sc->intr_handle) != 0) {
			device_printf(dev, "Can't setup interrupt.\n");
		} else {
			sc->irq = rman_get_start(sc->intr_res);
			/* Bind IRQ to BSP to avoid live migration. */
			bus_bind_intr(dev, sc->intr_res, CPU_FIRST());
		}
	}
	/* Program and announce event timers. */
	for (i = 0; i < num_timers; i++) {
		t = &sc->t[i];
		t->caps &= ~(HPET_TCNF_FSB_EN | HPET_TCNF_INT_ROUTE);
		t->caps &= ~(HPET_TCNF_VAL_SET | HPET_TCNF_INT_ENB);
		t->caps &= ~(HPET_TCNF_INT_TYPE);
		t->caps |= HPET_TCNF_32MODE;
		if (t->irq >= 0 && sc->legacy_route && i < 2) {
			/* Legacy route doesn't need more configuration. */
		} else
#ifdef DEV_APIC
		if ((t->caps & HPET_TCAP_FSB_INT_DEL) && t->irq >= 0) {
			uint64_t addr;
			uint32_t data;

			if (PCIB_MAP_MSI(
			    device_get_parent(device_get_parent(dev)), dev,
			    t->irq, &addr, &data) == 0) {
				bus_write_4(sc->mem_res,
				    HPET_TIMER_FSB_ADDR(i), addr);
				bus_write_4(sc->mem_res,
				    HPET_TIMER_FSB_VAL(i), data);
				t->caps |= HPET_TCNF_FSB_EN;
			} else
				t->irq = -2;
		} else
#endif
		if (t->irq >= 0)
			t->caps |= (t->irq << 9);
		else if (sc->irq >= 0 && (t->vectors & (1 << sc->irq)))
			t->caps |= (sc->irq << 9) | HPET_TCNF_INT_TYPE;
		bus_write_4(sc->mem_res, HPET_TIMER_CAP_CNF(i), t->caps);
		/* Skip event timers without set up IRQ. */
		if (t->irq < 0 &&
		    (sc->irq < 0 || (t->vectors & (1 << sc->irq)) == 0))
			continue;
		/* Announce the reset. */
		if (maxhpetet == 0)
			t->et.et_name = "HPET";
		else {
			sprintf(t->name, "HPET%d", maxhpetet);
			t->et.et_name = t->name;
		}
		t->et.et_flags = ET_FLAGS_PERIODIC | ET_FLAGS_ONESHOT;
		t->et.et_quality = 450;
		if (t->pcpu_master >= 0) {
			t->et.et_flags |= ET_FLAGS_PERCPU;
			t->et.et_quality += 100;
		} else if (mp_ncpus >= 8)
			t->et.et_quality -= 100;
		if ((t->caps & HPET_TCAP_PER_INT) == 0)
			t->et.et_quality -= 10;
		t->et.et_frequency = sc->freq;
		t->et.et_min_period =
		    ((uint64_t)(HPET_MIN_CYCLES * 2) << 32) / sc->freq;
		t->et.et_max_period = (0xfffffffeLLU << 32) / sc->freq;
		t->et.et_start = hpet_start;
		t->et.et_stop = hpet_stop;
		t->et.et_priv = &sc->t[i];
		if (t->pcpu_master < 0 || t->pcpu_master == i) {
			et_register(&t->et);
			maxhpetet++;
		}
	}
	acpi_GetInteger(sc->handle, "_UID", &sc->acpi_uid);

	make_dev_args_init(&mda);
	mda.mda_devsw = &hpet_cdevsw;
	mda.mda_uid = UID_ROOT;
	mda.mda_gid = GID_WHEEL;
	mda.mda_mode = 0644;
	mda.mda_si_drv1 = sc;
	error = make_dev_s(&mda, &sc->pdev, "hpet%d", device_get_unit(dev));
	if (error == 0) {
		sc->mmap_allow = 1;
		TUNABLE_INT_FETCH("hw.acpi.hpet.mmap_allow",
		    &sc->mmap_allow);
		sc->mmap_allow_write = 0;
		TUNABLE_INT_FETCH("hw.acpi.hpet.mmap_allow_write",
		    &sc->mmap_allow_write);
		SYSCTL_ADD_INT(device_get_sysctl_ctx(dev),
		    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
		    OID_AUTO, "mmap_allow",
		    CTLFLAG_RW, &sc->mmap_allow, 0,
		    "Allow userland to memory map HPET");
		SYSCTL_ADD_INT(device_get_sysctl_ctx(dev),
		    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
		    OID_AUTO, "mmap_allow_write",
		    CTLFLAG_RW, &sc->mmap_allow_write, 0,
		    "Allow userland write to the HPET register space");
	} else {
		device_printf(dev, "could not create /dev/hpet%d, error %d\n",
		    device_get_unit(dev), error);
	}

	return (0);
}

static int
hpet_detach(device_t dev)
{
	ACPI_FUNCTION_TRACE((char *)(uintptr_t) __func__);

	/* XXX Without a tc_remove() function, we can't detach. */
	return (EBUSY);
}

static int
hpet_suspend(device_t dev)
{
//	struct hpet_softc *sc;

	/*
	 * Disable the timer during suspend.  The timer will not lose
	 * its state in S1 or S2, but we are required to disable
	 * it.
	 */
//	sc = device_get_softc(dev);
//	hpet_disable(sc);

	return (0);
}

static int
hpet_resume(device_t dev)
{
	struct hpet_softc *sc;
	struct hpet_timer *t;
	int i;

	/* Re-enable the timer after a resume to keep the clock advancing. */
	sc = device_get_softc(dev);
	hpet_enable(sc);
	/* Restart event timers that were running on suspend. */
	for (i = 0; i < sc->num_timers; i++) {
		t = &sc->t[i];
#ifdef DEV_APIC
		if (t->irq >= 0 && (sc->legacy_route == 0 || i >= 2)) {
			uint64_t addr;
			uint32_t data;

			if (PCIB_MAP_MSI(
			    device_get_parent(device_get_parent(dev)), dev,
			    t->irq, &addr, &data) == 0) {
				bus_write_4(sc->mem_res,
				    HPET_TIMER_FSB_ADDR(i), addr);
				bus_write_4(sc->mem_res,
				    HPET_TIMER_FSB_VAL(i), data);
			}
		}
#endif
		if (t->mode == TIMER_STOPPED)
			continue;
		t->next = bus_read_4(sc->mem_res, HPET_MAIN_COUNTER);
		if (t->mode == TIMER_PERIODIC &&
		    (t->caps & HPET_TCAP_PER_INT) != 0) {
			t->caps |= HPET_TCNF_TYPE;
			t->next += t->div;
			bus_write_4(sc->mem_res, HPET_TIMER_CAP_CNF(t->num),
			    t->caps | HPET_TCNF_VAL_SET);
			bus_write_4(sc->mem_res, HPET_TIMER_COMPARATOR(t->num),
			    t->next);
			bus_read_4(sc->mem_res, HPET_TIMER_COMPARATOR(t->num));
			bus_write_4(sc->mem_res, HPET_TIMER_COMPARATOR(t->num),
			    t->div);
		} else {
			t->next += sc->freq / 1024;
			bus_write_4(sc->mem_res, HPET_TIMER_COMPARATOR(t->num),
			    t->next);
		}
		bus_write_4(sc->mem_res, HPET_ISR, 1 << t->num);
		bus_write_4(sc->mem_res, HPET_TIMER_CAP_CNF(t->num), t->caps);
	}
	return (0);
}

/* Print some basic latency/rate information to assist in debugging. */
static void
hpet_test(struct hpet_softc *sc)
{
	int i;
	uint32_t u1, u2;
	struct bintime b0, b1, b2;
	struct timespec ts;

	binuptime(&b0);
	binuptime(&b0);
	binuptime(&b1);
	u1 = bus_read_4(sc->mem_res, HPET_MAIN_COUNTER);
	for (i = 1; i < 1000; i++)
		u2 = bus_read_4(sc->mem_res, HPET_MAIN_COUNTER);
	binuptime(&b2);
	u2 = bus_read_4(sc->mem_res, HPET_MAIN_COUNTER);

	bintime_sub(&b2, &b1);
	bintime_sub(&b1, &b0);
	bintime_sub(&b2, &b1);
	bintime2timespec(&b2, &ts);

	device_printf(sc->dev, "%ld.%09ld: %u ... %u = %u\n",
	    (long)ts.tv_sec, ts.tv_nsec, u1, u2, u2 - u1);

	device_printf(sc->dev, "time per call: %ld ns\n", ts.tv_nsec / 1000);
}

#ifdef DEV_APIC
static int
hpet_remap_intr(device_t dev, device_t child, u_int irq)
{
	struct hpet_softc *sc = device_get_softc(dev);
	struct hpet_timer *t;
	uint64_t addr;
	uint32_t data;
	int error, i;

	for (i = 0; i < sc->num_timers; i++) {
		t = &sc->t[i];
		if (t->irq != irq)
			continue;
		error = PCIB_MAP_MSI(
		    device_get_parent(device_get_parent(dev)), dev,
		    irq, &addr, &data);
		if (error)
			return (error);
		hpet_disable(sc); /* Stop timer to avoid interrupt loss. */
		bus_write_4(sc->mem_res, HPET_TIMER_FSB_ADDR(i), addr);
		bus_write_4(sc->mem_res, HPET_TIMER_FSB_VAL(i), data);
		hpet_enable(sc);
		return (0);
	}
	return (ENOENT);
}
#endif

static device_method_t hpet_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify, hpet_identify),
	DEVMETHOD(device_probe, hpet_probe),
	DEVMETHOD(device_attach, hpet_attach),
	DEVMETHOD(device_detach, hpet_detach),
	DEVMETHOD(device_suspend, hpet_suspend),
	DEVMETHOD(device_resume, hpet_resume),

#ifdef DEV_APIC
	DEVMETHOD(bus_remap_intr, hpet_remap_intr),
#endif

	DEVMETHOD_END
};

static driver_t	hpet_driver = {
	"hpet",
	hpet_methods,
	sizeof(struct hpet_softc),
};

DRIVER_MODULE(hpet, acpi, hpet_driver, hpet_devclass, 0, 0);
MODULE_DEPEND(hpet, acpi, 1, 1, 1);
