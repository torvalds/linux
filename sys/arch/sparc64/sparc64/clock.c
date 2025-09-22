/*	$OpenBSD: clock.c,v 1.88 2025/06/28 11:34:21 miod Exp $	*/
/*	$NetBSD: clock.c,v 1.41 2001/07/24 19:29:25 eeh Exp $ */

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1994 Gordon W. Ross
 * Copyright (c) 1993 Adam Glass
 * Copyright (c) 1996 Paul Kranenburg
 * Copyright (c) 1996
 * 	The President and Fellows of Harvard College. All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by Harvard University.
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 *	This product includes software developed by Paul Kranenburg.
 *	This product includes software developed by Harvard University.
 * 4. Neither the name of the University nor the names of its contributors
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
 *	@(#)clock.c	8.1 (Berkeley) 6/11/93
 *
 */

/*
 * Clock driver.  This is the id prom and eeprom driver as well
 * and includes the timer register functions too.
 */

/* Define this for a 1/4s clock to ease debugging */
/* #define INTR_DEBUG */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/clockintr.h>
#include <sys/sched.h>
#include <sys/stdint.h>
#include <sys/timetc.h>
#include <sys/atomic.h>

#include <machine/bus.h>
#include <machine/autoconf.h>
#include <machine/cpu.h>
#include <machine/idprom.h>

#include <dev/clock_subr.h>
#include <dev/ic/mk48txxreg.h>

#include <sparc64/dev/sbusreg.h>
#include <dev/sbus/sbusvar.h>
#include <sparc64/dev/ebusreg.h>
#include <sparc64/dev/ebusvar.h>
#include <sparc64/dev/fhcvar.h>

extern u_int64_t cpu_clockrate;

struct clock_wenable_info {
	bus_space_tag_t		cwi_bt;
	bus_space_handle_t	cwi_bh;
	bus_size_t		cwi_size;
};

struct cfdriver clock_cd = {
	NULL, "clock", DV_DULL
};

u_int tick_get_timecount(struct timecounter *);

struct timecounter tick_timecounter = {
	.tc_get_timecount = tick_get_timecount,
	.tc_counter_mask = ~0u,
	.tc_frequency = 0,
	.tc_name = "tick",
	.tc_quality = 0,
	.tc_priv = NULL,
	.tc_user = TC_TICK,
};

u_int sys_tick_get_timecount(struct timecounter *);

struct timecounter sys_tick_timecounter = {
	.tc_get_timecount = sys_tick_get_timecount,
	.tc_counter_mask = ~0u,
	.tc_frequency = 0,
	.tc_name = "sys_tick",
	.tc_quality = 1000,
	.tc_priv = NULL,
	.tc_user = TC_SYS_TICK,
};

void	tick_start(void);
void	sys_tick_start(void);
void	stick_start(void);

int	tickintr(void *);
int	sys_tickintr(void *);
int	stickintr(void *);

/* %TICK is at most a 63-bit counter. */
#define TICK_COUNT_MASK 0x7fffffffffffffff

uint64_t tick_nsec_cycle_ratio;
uint64_t tick_nsec_max;

void tick_rearm(void *, uint64_t);
void tick_trigger(void *);

const struct intrclock tick_intrclock = {
	.ic_rearm = tick_rearm,
	.ic_trigger = tick_trigger
};

/* %STICK is at most a 63-bit counter. */
#define STICK_COUNT_MASK 0x7fffffffffffffff

uint64_t sys_tick_nsec_cycle_ratio;
uint64_t sys_tick_nsec_max;

void sys_tick_rearm(void *, uint64_t);
void sys_tick_trigger(void *);

const struct intrclock sys_tick_intrclock = {
	.ic_rearm = sys_tick_rearm,
	.ic_trigger = sys_tick_trigger
};

void stick_rearm(void *, uint64_t);
void stick_trigger(void *);

const struct intrclock stick_intrclock = {
	.ic_rearm = stick_rearm,
	.ic_trigger = stick_trigger
};

void sparc64_raise_clockintr(void);

static struct intrhand level10 = {
	.ih_fun = tickintr,
	.ih_number = 1,
	.ih_pil = 10,
	.ih_name = "clock"
};

/*
 * clock (eeprom) attaches at the sbus or the ebus (PCI)
 */
static int	clockmatch_sbus(struct device *, void *, void *);
static void	clockattach_sbus(struct device *, struct device *, void *);
static int	clockmatch_ebus(struct device *, void *, void *);
static void	clockattach_ebus(struct device *, struct device *, void *);
static int	clockmatch_fhc(struct device *, void *, void *);
static void	clockattach_fhc(struct device *, struct device *, void *);
static void	clockattach(int, bus_space_tag_t, bus_space_handle_t);

const struct cfattach clock_sbus_ca = {
	sizeof(struct device), clockmatch_sbus, clockattach_sbus
};

const struct cfattach clock_ebus_ca = {
	sizeof(struct device), clockmatch_ebus, clockattach_ebus
};

const struct cfattach clock_fhc_ca = {
	sizeof(struct device), clockmatch_fhc, clockattach_fhc
};

/* Global TOD clock handle & idprom pointer */
extern todr_chip_handle_t todr_handle;
static struct idprom *idprom;

int clock_bus_wenable(struct todr_chip_handle *, int);
void myetheraddr(u_char *);
struct idprom *getidprom(void);

/*
 * The OPENPROM calls the clock the "eeprom", so we have to have our
 * own special match function to call it the "clock".
 */
static int
clockmatch_sbus(struct device *parent, void *cf, void *aux)
{
	struct sbus_attach_args *sa = aux;

	return (strcmp("eeprom", sa->sa_name) == 0);
}

static int
clockmatch_ebus(struct device *parent, void *cf, void *aux)
{
	struct ebus_attach_args *ea = aux;

	return (strcmp("eeprom", ea->ea_name) == 0);
}

static int
clockmatch_fhc(struct device *parent, void *cf, void *aux)
{
	struct fhc_attach_args *fa = aux;
        
	return (strcmp("eeprom", fa->fa_name) == 0);
}

/*
 * Attach a clock (really `eeprom') to the sbus or ebus.
 *
 * We ignore any existing virtual address as we need to map
 * this read-only and make it read-write only temporarily,
 * whenever we read or write the clock chip.  The clock also
 * contains the ID ``PROM'', and I have already had the pleasure
 * of reloading the cpu type, Ethernet address, etc, by hand from
 * the console FORTH interpreter.  I intend not to enjoy it again.
 *
 * the MK48T02 is 2K.  the MK48T08 is 8K, and the MK48T59 is
 * supposed to be identical to it.
 *
 * This is *UGLY*!  We probably have multiple mappings.  But I do
 * know that this all fits inside an 8K page, so I'll just map in
 * once.
 *
 * What we really need is some way to record the bus attach args
 * so we can call *_bus_map() later with BUS_SPACE_MAP_READONLY
 * or not to write enable/disable the device registers.  This is
 * a non-trivial operation.  
 */

static void
clockattach_sbus(struct device *parent, struct device *self, void *aux)
{
	struct sbus_attach_args *sa = aux;
	bus_space_tag_t bt = sa->sa_bustag;
	int sz;
	static struct clock_wenable_info cwi;

	/* use sa->sa_regs[0].size? */
	sz = 8192;

	if (sbus_bus_map(bt,
			 sa->sa_slot,
			 (sa->sa_offset & ~NBPG),
			 sz,
			 BUS_SPACE_MAP_LINEAR | BUS_SPACE_MAP_READONLY,
			 0, &cwi.cwi_bh) != 0) {
		printf("%s: can't map register\n", self->dv_xname);
		return;
	}
	clockattach(sa->sa_node, bt, cwi.cwi_bh);

	/* Save info for the clock wenable call. */
	cwi.cwi_bt = bt;
	cwi.cwi_size = sz;
	todr_handle->bus_cookie = &cwi;
	todr_handle->todr_setwen = clock_bus_wenable;
}

/*
 * Write en/dis-able clock registers.  We coordinate so that several
 * writers can run simultaneously.
 * XXX There is still a race here.  The page change and the "writers"
 * change are not atomic.
 */
int
clock_bus_wenable(struct todr_chip_handle *handle, int onoff)
{
	int s, err = 0;
	int prot; /* nonzero => change prot */
	volatile static int writers;
	struct clock_wenable_info *cwi = handle->bus_cookie;

	s = splhigh();
	if (onoff)
		prot = writers++ == 0 ? 1 : 0;
	else
		prot = --writers == 0 ? 1 : 0;
	splx(s);

	if (prot) {
		err = bus_space_protect(cwi->cwi_bt, cwi->cwi_bh, cwi->cwi_size,
		    onoff ? 0 : BUS_SPACE_MAP_READONLY);
		if (err)
			printf("clock_wenable_info: WARNING -- cannot %s "
			    "page protection\n", onoff ? "disable" : "enable");
	}
	return (err);
}

static void
clockattach_ebus(struct device *parent, struct device *self, void *aux)
{
	struct ebus_attach_args *ea = aux;
	bus_space_tag_t bt;
	int sz;
	static struct clock_wenable_info cwi;

	/* hard code to 8K? */
	sz = ea->ea_regs[0].size;

	if (ebus_bus_map(ea->ea_iotag, 0,
	    EBUS_PADDR_FROM_REG(&ea->ea_regs[0]), sz, 0, 0, &cwi.cwi_bh) == 0) {
		bt = ea->ea_iotag;
	} else if (ebus_bus_map(ea->ea_memtag, 0,
	    EBUS_PADDR_FROM_REG(&ea->ea_regs[0]), sz,
	    BUS_SPACE_MAP_LINEAR | BUS_SPACE_MAP_READONLY,
	    0, &cwi.cwi_bh) == 0) {
		bt = ea->ea_memtag;
	} else {
		printf("%s: can't map register\n", self->dv_xname);
		return;
	}

	clockattach(ea->ea_node, bt, cwi.cwi_bh);

	/* Save info for the clock wenable call. */
	cwi.cwi_bt = bt;
	cwi.cwi_size = sz;
	todr_handle->bus_cookie = &cwi;
	todr_handle->todr_setwen = (ea->ea_memtag == bt) ? 
	    clock_bus_wenable : NULL;
}

static void
clockattach_fhc(struct device *parent, struct device *self, void *aux)
{
	struct fhc_attach_args *fa = aux;
	bus_space_tag_t bt = fa->fa_bustag;
	int sz;
	static struct clock_wenable_info cwi;

	/* use sa->sa_regs[0].size? */
	sz = 8192;

	if (fhc_bus_map(bt, fa->fa_reg[0].fbr_slot,
	    (fa->fa_reg[0].fbr_offset & ~NBPG), fa->fa_reg[0].fbr_size,
	    BUS_SPACE_MAP_LINEAR | BUS_SPACE_MAP_READONLY, &cwi.cwi_bh) != 0) {
		printf("%s: can't map register\n", self->dv_xname);
		return;
	}

	clockattach(fa->fa_node, bt, cwi.cwi_bh);

	/* Save info for the clock wenable call. */
	cwi.cwi_bt = bt;
	cwi.cwi_size = sz;
	todr_handle->bus_cookie = &cwi;
	todr_handle->todr_setwen = clock_bus_wenable;
}

static void
clockattach(int node, bus_space_tag_t bt, bus_space_handle_t bh)
{
	char *model;
	struct idprom *idp;
	int h;

	model = getpropstring(node, "model");

#ifdef DIAGNOSTIC
	if (model == NULL)
		panic("clockattach: no model property");
#endif

	/* Our TOD clock year 0 is 1968 */
	if ((todr_handle = mk48txx_attach(bt, bh, model, 1968)) == NULL)
		panic("Can't attach %s tod clock", model);

#define IDPROM_OFFSET (8*1024 - 40)	/* XXX - get nvram sz from driver */
	if (idprom == NULL) {
		idp = getidprom();
		if (idp == NULL)
			idp = (struct idprom *)(bus_space_vaddr(bt, bh) +
			    IDPROM_OFFSET);
		idprom = idp;
	} else
		idp = idprom;
	h = idp->id_machine << 24;
	h |= idp->id_hostid[0] << 16;
	h |= idp->id_hostid[1] << 8;
	h |= idp->id_hostid[2];
	hostid = h;
	printf("\n");
}

struct idprom *
getidprom(void)
{
	struct idprom *idp = NULL;
	int node, n;

	node = findroot();
	if (getprop(node, "idprom", sizeof(*idp), &n, (void **)&idp) != 0)
		return (NULL);
	if (n != 1) {
		free(idp, M_DEVBUF, 0);
		return (NULL);
	}
	return (idp);
}

/*
 * XXX this belongs elsewhere
 */
void
myetheraddr(u_char *cp)
{
	struct idprom *idp;

	if ((idp = idprom) == NULL) {
		int node, n;

		node = findroot();
		if (getprop(node, "idprom", sizeof *idp, &n, (void **)&idp) ||
		    n != 1) {
			printf("\nmyetheraddr: clock not setup yet, "
			       "and no idprom property in /\n");
			return;
		}
	}

	cp[0] = idp->id_ether[0];
	cp[1] = idp->id_ether[1];
	cp[2] = idp->id_ether[2];
	cp[3] = idp->id_ether[3];
	cp[4] = idp->id_ether[4];
	cp[5] = idp->id_ether[5];
	if (idprom == NULL)
		free(idp, M_DEVBUF, 0);
}

/*
 * Set up the real-time and statistics clocks.
 *
 * The frequencies of these clocks must be an even number of microseconds.
 */
void
cpu_initclocks(void)
{
	u_int sys_tick_rate;
	int impl = 0;

	if (1000000 % hz) {
		printf("cannot get %d Hz clock; using 100 Hz\n", hz);
		hz = 100;
		tick = 1000000 / hz;
		tick_nsec = 1000000000 / hz;
	}

	stathz = hz;
	profhz = stathz * 10;
	statclock_is_randomized = 1;

	/* Make sure we have a sane cpu_clockrate -- we'll need it */
	if (!cpu_clockrate) 
		/* Default to 200MHz clock XXXXX */
		cpu_clockrate = 200000000;

	tick_timecounter.tc_frequency = cpu_clockrate;
	tc_init(&tick_timecounter);

	/*
	 * UltraSPARC IIe processors do have a STICK register, but it
	 * lives on the PCI host bridge and isn't accessible through
	 * ASR24.
	 */
	if (CPU_ISSUN4U || CPU_ISSUN4US)
		impl = (getver() & VER_IMPL) >> VER_IMPL_SHIFT;

	sys_tick_rate = getpropint(findroot(), "stick-frequency", 0);
	if (sys_tick_rate > 0 && impl != IMPL_HUMMINGBIRD) {
		sys_tick_timecounter.tc_frequency = sys_tick_rate;
		tc_init(&sys_tick_timecounter);
	}

	struct cpu_info *ci;

	/* 
	 * Establish a level 10 interrupt handler 
	 *
	 * We will have a conflict with the softint handler,
	 * so we set the ih_number to 1.
	 */
	intr_establish(&level10);
	evcount_percpu(&level10.ih_count);

	if (sys_tick_rate > 0) {
		sys_tick_nsec_cycle_ratio =
		    sys_tick_rate * (1ULL << 32) / 1000000000;
		sys_tick_nsec_max = UINT64_MAX / sys_tick_nsec_cycle_ratio;
		if (impl == IMPL_HUMMINGBIRD) {
			level10.ih_fun = stickintr;
			cpu_start_clock = stick_start;
		} else {
			level10.ih_fun = sys_tickintr;
			cpu_start_clock = sys_tick_start;
		}
	} else {
		tick_nsec_cycle_ratio =
		    cpu_clockrate * (1ULL << 32) / 1000000000;
		tick_nsec_max = UINT64_MAX / tick_nsec_cycle_ratio;
		level10.ih_fun = tickintr;
		cpu_start_clock = tick_start;
	}

	for (ci = cpus; ci != NULL; ci = ci->ci_next)
		memcpy(&ci->ci_tickintr, &level10, sizeof(level10));
}

void
cpu_startclock(void)
{
	cpu_start_clock();
}

void
setstatclockrate(int newhz)
{
}

/*
 * Level 10 (clock) interrupts.  If we are using the FORTH PROM for
 * console input, we need to check for that here as well, and generate
 * a software interrupt to read it.
 *
 * %tick is really a level-14 interrupt.  We need to remap this in 
 * locore.s to a level 10.
 */
int
tickintr(void *cap)
{
	clockintr_dispatch(cap);
	evcount_inc(&level10.ih_count);
	return (1);
}

int
sys_tickintr(void *cap)
{
	clockintr_dispatch(cap);
	evcount_inc(&level10.ih_count);
	return (1);
}

int
stickintr(void *cap)
{
	clockintr_dispatch(cap);
	evcount_inc(&level10.ih_count);
	return (1);
}

void
tick_start(void)
{
	tick_enable();

	clockintr_cpu_init(&tick_intrclock);
	clockintr_trigger();
}

void
tick_rearm(void *unused, uint64_t nsecs)
{
	uint64_t s, t0;
	uint32_t cycles;

	if (nsecs > tick_nsec_max)
		nsecs = tick_nsec_max;
	cycles = (nsecs * tick_nsec_cycle_ratio) >> 32;

	s = intr_disable();
	t0 = tick();
	tickcmpr_set((t0 + cycles) & TICK_COUNT_MASK);
	if (cycles <= ((tick() - t0) & TICK_COUNT_MASK))
		sparc64_raise_clockintr();
	intr_restore(s);
}

void
tick_trigger(void *unused)
{
	sparc64_raise_clockintr();
}

void
sys_tick_start(void)
{
	if (CPU_ISSUN4U || CPU_ISSUN4US) {
		tick_enable();
		sys_tick_enable();
	}

	clockintr_cpu_init(&sys_tick_intrclock);
	clockintr_trigger();
}

void
sys_tick_rearm(void *unused, uint64_t nsecs)
{
	uint64_t s, t0;
	uint32_t cycles;

	if (nsecs > sys_tick_nsec_max)
		nsecs = sys_tick_nsec_max;
	cycles = (nsecs * sys_tick_nsec_cycle_ratio) >> 32;

	s = intr_disable();
	t0 = sys_tick();
	sys_tickcmpr_set((t0 + cycles) & STICK_COUNT_MASK);
	if (cycles <= ((sys_tick() - t0) & STICK_COUNT_MASK))
		sparc64_raise_clockintr();
	intr_restore(s);
}

void
sys_tick_trigger(void *unused)
{
	sparc64_raise_clockintr();
}

void
stick_start(void)
{
	tick_enable();

	clockintr_cpu_init(&stick_intrclock);
	clockintr_trigger();
}

void
stick_rearm(void *unused, uint64_t nsecs)
{
	uint64_t s, t0;
	uint32_t cycles;

	if (nsecs > sys_tick_nsec_max)
		nsecs = sys_tick_nsec_max;
	cycles = (nsecs * sys_tick_nsec_cycle_ratio) >> 32;

	s = intr_disable();
	t0 = stick();
	stickcmpr_set((t0 + cycles) & STICK_COUNT_MASK);
	if (cycles <= ((stick() - t0) & STICK_COUNT_MASK))
		sparc64_raise_clockintr();
	intr_restore(s);
}

void
stick_trigger(void *unused)
{
	sparc64_raise_clockintr();
}

u_int
tick_get_timecount(struct timecounter *tc)
{
	u_int64_t tick;

	__asm volatile("rd %%tick, %0" : "=r" (tick));

	return (tick & ~0u);
}

u_int
sys_tick_get_timecount(struct timecounter *tc)
{
	u_int64_t tick;

	__asm volatile("rd %%sys_tick, %0" : "=r" (tick));

	return (tick & ~0u);
}

void
sparc64_raise_clockintr(void)
{
	send_softint(PIL_CLOCK, &curcpu()->ci_tickintr);
}
