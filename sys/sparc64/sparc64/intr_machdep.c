/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1991 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
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
 */
/*-
 * Copyright (c) 2001 Jake Burkholder.
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
 *
 *	from: @(#)isa.c	7.2 (Berkeley) 5/13/91
 *	form: src/sys/i386/isa/intr_machdep.c,v 1.57 2001/07/20
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/errno.h>
#include <sys/interrupt.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/pcpu.h>
#include <sys/proc.h>
#include <sys/smp.h>
#include <sys/sx.h>
#include <sys/vmmeter.h>

#include <machine/frame.h>
#include <machine/intr_machdep.h>

#define	MAX_STRAY_LOG	5

CTASSERT((1 << IV_SHIFT) == sizeof(struct intr_vector));

ih_func_t *intr_handlers[PIL_MAX];
uint16_t pil_countp[PIL_MAX];
static uint16_t pil_stray_count[PIL_MAX];

struct intr_vector intr_vectors[IV_MAX];
uint16_t intr_countp[IV_MAX];
static uint16_t intr_stray_count[IV_MAX];

static const char *const pil_names[] = {
	"stray",
	"low",		/* PIL_LOW */
	"preempt",	/* PIL_PREEMPT */
	"ithrd",	/* PIL_ITHREAD */
	"rndzvs",	/* PIL_RENDEZVOUS */
	"ast",		/* PIL_AST */
	"hardclock",	/* PIL_HARDCLOCK */
	"stray", "stray", "stray", "stray",
	"filter",	/* PIL_FILTER */
	"bridge",	/* PIL_BRIDGE */
	"stop",		/* PIL_STOP */
	"tick",		/* PIL_TICK */
};

/* protect the intr_vectors table */
static struct sx intr_table_lock;
/* protect intrcnt_index */
static struct mtx intrcnt_lock;

#ifdef SMP
static int assign_cpu;

static void intr_assign_next_cpu(struct intr_vector *iv);
static void intr_shuffle_irqs(void *arg __unused);
#endif

static int intr_assign_cpu(void *arg, int cpu);
static void intr_execute_handlers(void *);
static void intr_stray_level(struct trapframe *);
static void intr_stray_vector(void *);
static int intrcnt_setname(const char *, int);
static void intrcnt_updatename(int, const char *, int);
void counter_intr_inc(void);

static void
intrcnt_updatename(int vec, const char *name, int ispil)
{
	static int intrcnt_index, stray_pil_index, stray_vec_index;
	int name_index;

	mtx_lock_spin(&intrcnt_lock);
	if (intrnames[0] == '\0') {
		/* for bitbucket */
		if (bootverbose)
			printf("initalizing intr_countp\n");
		intrcnt_setname("???", intrcnt_index++);

		stray_vec_index = intrcnt_index++;
		intrcnt_setname("stray", stray_vec_index);
		for (name_index = 0; name_index < IV_MAX; name_index++)
			intr_countp[name_index] = stray_vec_index;

		stray_pil_index = intrcnt_index++;
		intrcnt_setname("pil", stray_pil_index);
		for (name_index = 0; name_index < PIL_MAX; name_index++)
			pil_countp[name_index] = stray_pil_index;
	}

	if (name == NULL)
		name = "???";

	if (!ispil && intr_countp[vec] != stray_vec_index)
		name_index = intr_countp[vec];
	else if (ispil && pil_countp[vec] != stray_pil_index)
		name_index = pil_countp[vec];
	else
		name_index = intrcnt_index++;

	if (intrcnt_setname(name, name_index))
		name_index = 0;

	if (!ispil)
		intr_countp[vec] = name_index;
	else
		pil_countp[vec] = name_index;
	mtx_unlock_spin(&intrcnt_lock);
}

static int
intrcnt_setname(const char *name, int index)
{

	if ((MAXCOMLEN + 1) * index >= sintrnames)
		return (E2BIG);
	snprintf(intrnames + (MAXCOMLEN + 1) * index, MAXCOMLEN + 1, "%-*s",
	    MAXCOMLEN, name);
	return (0);
}

void
intr_setup(int pri, ih_func_t *ihf, int vec, iv_func_t *ivf, void *iva)
{
	char pilname[MAXCOMLEN + 1];
	register_t s;

	s = intr_disable();
	if (vec != -1) {
		intr_vectors[vec].iv_func = ivf;
		intr_vectors[vec].iv_arg = iva;
		intr_vectors[vec].iv_pri = pri;
		intr_vectors[vec].iv_vec = vec;
	}
	intr_handlers[pri] = ihf;
	intr_restore(s);
	snprintf(pilname, MAXCOMLEN + 1, "pil%d: %s", pri, pil_names[pri]);
	intrcnt_updatename(pri, pilname, 1);
}

static void
intr_stray_level(struct trapframe *tf)
{
	uint64_t level;

	level = tf->tf_level;
	if (pil_stray_count[level] < MAX_STRAY_LOG) {
		printf("stray level interrupt %ld\n", level);
		pil_stray_count[level]++;
		if (pil_stray_count[level] >= MAX_STRAY_LOG)
			printf("got %d stray level interrupt %ld's: not "
			    "logging anymore\n", MAX_STRAY_LOG, level);
	}
}

static void
intr_stray_vector(void *cookie)
{
	struct intr_vector *iv;
	u_int vec;

	iv = cookie;
	vec = iv->iv_vec;
	if (intr_stray_count[vec] < MAX_STRAY_LOG) {
		printf("stray vector interrupt %d\n", vec);
		intr_stray_count[vec]++;
		if (intr_stray_count[vec] >= MAX_STRAY_LOG)
			printf("got %d stray vector interrupt %d's: not "
			    "logging anymore\n", MAX_STRAY_LOG, vec);
	}
}

void
intr_init1()
{
	int i;

	/* Mark all interrupts as being stray. */
	for (i = 0; i < PIL_MAX; i++)
		intr_handlers[i] = intr_stray_level;
	for (i = 0; i < IV_MAX; i++) {
		intr_vectors[i].iv_func = intr_stray_vector;
		intr_vectors[i].iv_arg = &intr_vectors[i];
		intr_vectors[i].iv_pri = PIL_LOW;
		intr_vectors[i].iv_vec = i;
		intr_vectors[i].iv_refcnt = 0;
	}
	intr_handlers[PIL_LOW] = intr_fast;
}

void
intr_init2()
{

	sx_init(&intr_table_lock, "intr sources");
	mtx_init(&intrcnt_lock, "intrcnt", NULL, MTX_SPIN);
}

static int
intr_assign_cpu(void *arg, int cpu)
{
#ifdef SMP
	struct pcpu *pc;
	struct intr_vector *iv;

	/*
	 * Don't do anything during early boot.  We will pick up the
	 * assignment once the APs are started.
	 */
	if (assign_cpu && cpu != NOCPU) {
		pc = pcpu_find(cpu);
		if (pc == NULL)
			return (EINVAL);
		iv = arg;
		sx_xlock(&intr_table_lock);
		iv->iv_mid = pc->pc_mid;
		iv->iv_ic->ic_assign(iv);
		sx_xunlock(&intr_table_lock);
	}
	return (0);
#else
	return (EOPNOTSUPP);
#endif
}

static void
intr_execute_handlers(void *cookie)
{
	struct intr_vector *iv;

	iv = cookie;
	if (__predict_false(intr_event_handle(iv->iv_event, NULL) != 0))
		intr_stray_vector(iv);
}

int
intr_controller_register(int vec, const struct intr_controller *ic,
    void *icarg)
{
	struct intr_event *ie;
	struct intr_vector *iv;
	int error;

	if (vec < 0 || vec >= IV_MAX)
		return (EINVAL);
	sx_xlock(&intr_table_lock);
	iv = &intr_vectors[vec];
	ie = iv->iv_event;
	sx_xunlock(&intr_table_lock);
	if (ie != NULL)
		return (EEXIST);
	error = intr_event_create(&ie, iv, 0, vec, NULL, ic->ic_clear,
	    ic->ic_clear, intr_assign_cpu, "vec%d:", vec);
	if (error != 0)
		return (error);
	sx_xlock(&intr_table_lock);
	if (iv->iv_event != NULL) {
		sx_xunlock(&intr_table_lock);
		intr_event_destroy(ie);
		return (EEXIST);
	}
	iv->iv_ic = ic;
	iv->iv_icarg = icarg;
	iv->iv_event = ie;
	iv->iv_mid = PCPU_GET(mid);
	sx_xunlock(&intr_table_lock);
	return (0);
}

int
inthand_add(const char *name, int vec, driver_filter_t *filt,
    driver_intr_t *handler, void *arg, int flags, void **cookiep)
{
	const struct intr_controller *ic;
	struct intr_event *ie;
	struct intr_handler *ih;
	struct intr_vector *iv;
	int error, filter;

	if (vec < 0 || vec >= IV_MAX)
		return (EINVAL);
	/*
	 * INTR_BRIDGE filters/handlers are special purpose only, allowing
	 * them to be shared just would complicate things unnecessarily.
	 */
	if ((flags & INTR_BRIDGE) != 0 && (flags & INTR_EXCL) == 0)
		return (EINVAL);
	sx_xlock(&intr_table_lock);
	iv = &intr_vectors[vec];
	ic = iv->iv_ic;
	ie = iv->iv_event;
	sx_xunlock(&intr_table_lock);
	if (ic == NULL || ie == NULL)
		return (EINVAL);
	error = intr_event_add_handler(ie, name, filt, handler, arg,
	    intr_priority(flags), flags, cookiep);
	if (error != 0)
		return (error);
	sx_xlock(&intr_table_lock);
	/* Disable the interrupt while we fiddle with it. */
	ic->ic_disable(iv);
	iv->iv_refcnt++;
	if (iv->iv_refcnt == 1)
		intr_setup((flags & INTR_BRIDGE) != 0 ? PIL_BRIDGE :
		    filt != NULL ? PIL_FILTER : PIL_ITHREAD, intr_fast,
		    vec, intr_execute_handlers, iv);
	else if (filt != NULL) {
		/*
		 * Check if we need to upgrade from PIL_ITHREAD to PIL_FILTER.
		 * Given that apart from the on-board SCCs and UARTs shared
		 * interrupts are rather uncommon on sparc64 this should be
		 * pretty rare in practice.
		 */
		filter = 0;
		CK_SLIST_FOREACH(ih, &ie->ie_handlers, ih_next) {
			if (ih->ih_filter != NULL && ih->ih_filter != filt) {
				filter = 1;
				break;
			}
		}
		if (filter == 0)
			intr_setup(PIL_FILTER, intr_fast, vec,
			    intr_execute_handlers, iv);
	}
	intr_stray_count[vec] = 0;
	intrcnt_updatename(vec, ie->ie_fullname, 0);
#ifdef SMP
	if (assign_cpu)
		intr_assign_next_cpu(iv);
#endif
	ic->ic_enable(iv);
	/* Ensure the interrupt is cleared, it might have triggered before. */
	if (ic->ic_clear != NULL)
		ic->ic_clear(iv);
	sx_xunlock(&intr_table_lock);
	return (0);
}

int
inthand_remove(int vec, void *cookie)
{
	struct intr_vector *iv;
	int error;

	if (vec < 0 || vec >= IV_MAX)
		return (EINVAL);
	error = intr_event_remove_handler(cookie);
	if (error == 0) {
		/*
		 * XXX: maybe this should be done regardless of whether
		 * intr_event_remove_handler() succeeded?
		 */
		sx_xlock(&intr_table_lock);
		iv = &intr_vectors[vec];
		iv->iv_refcnt--;
		if (iv->iv_refcnt == 0) {
			/*
			 * Don't disable the interrupt for now, so that
			 * stray interrupts get detected...
			 */
			intr_setup(PIL_LOW, intr_fast, vec,
			    intr_stray_vector, iv);
		}
		sx_xunlock(&intr_table_lock);
	}
	return (error);
}

/* Add a description to an active interrupt handler. */
int
intr_describe(int vec, void *ih, const char *descr)
{
	struct intr_vector *iv;
	int error;

	if (vec < 0 || vec >= IV_MAX)
		return (EINVAL);
	sx_xlock(&intr_table_lock);
	iv = &intr_vectors[vec];
	if (iv == NULL) {
		sx_xunlock(&intr_table_lock);
		return (EINVAL);
	}
	error = intr_event_describe_handler(iv->iv_event, ih, descr);
	if (error) {
		sx_xunlock(&intr_table_lock);
		return (error);
	}
	intrcnt_updatename(vec, iv->iv_event->ie_fullname, 0);
	sx_xunlock(&intr_table_lock);
	return (error);
}

/*
 * Do VM_CNT_INC(intr), being in the interrupt context already. This is
 * called from assembly.
 * To avoid counter_enter() and appropriate assertion, unwrap VM_CNT_INC()
 * and hardcode the actual increment.
 */
void
counter_intr_inc(void)
{

	*(uint64_t *)zpcpu_get(vm_cnt.v_intr) += 1;
}

#ifdef SMP
/*
 * Support for balancing interrupt sources across CPUs.  For now we just
 * allocate CPUs round-robin.
 */

static cpuset_t intr_cpus = CPUSET_T_INITIALIZER(0x1);
static int current_cpu;

static void
intr_assign_next_cpu(struct intr_vector *iv)
{
	struct pcpu *pc;

	sx_assert(&intr_table_lock, SA_XLOCKED);

	/*
	 * Assign this source to a CPU in a round-robin fashion.
	 */
	pc = pcpu_find(current_cpu);
	if (pc == NULL)
		return;
	iv->iv_mid = pc->pc_mid;
	iv->iv_ic->ic_assign(iv);
	do {
		current_cpu++;
		if (current_cpu > mp_maxid)
			current_cpu = 0;
	} while (!CPU_ISSET(current_cpu, &intr_cpus));
}

/* Attempt to bind the specified IRQ to the specified CPU. */
int
intr_bind(int vec, u_char cpu)
{
	struct intr_vector *iv;
	int error;

	if (vec < 0 || vec >= IV_MAX)
		return (EINVAL);
	sx_xlock(&intr_table_lock);
	iv = &intr_vectors[vec];
	if (iv == NULL) {
		sx_xunlock(&intr_table_lock);
		return (EINVAL);
	}
	error = intr_event_bind(iv->iv_event, cpu);
	sx_xunlock(&intr_table_lock);
	return (error);
}

/*
 * Add a CPU to our mask of valid CPUs that can be destinations of
 * interrupts.
 */
void
intr_add_cpu(u_int cpu)
{

	if (cpu >= MAXCPU)
		panic("%s: Invalid CPU ID", __func__);
	if (bootverbose)
		printf("INTR: Adding CPU %d as a target\n", cpu);

	CPU_SET(cpu, &intr_cpus);
}

/*
 * Distribute all the interrupt sources among the available CPUs once the
 * APs have been launched.
 */
static void
intr_shuffle_irqs(void *arg __unused)
{
	struct pcpu *pc;
	struct intr_vector *iv;
	int i;

	/* Don't bother on UP. */
	if (mp_ncpus == 1)
		return;

	sx_xlock(&intr_table_lock);
	assign_cpu = 1;
	for (i = 0; i < IV_MAX; i++) {
		iv = &intr_vectors[i];
		if (iv != NULL && iv->iv_refcnt > 0) {
			/*
			 * If this event is already bound to a CPU,
			 * then assign the source to that CPU instead
			 * of picking one via round-robin.
			 */
			if (iv->iv_event->ie_cpu != NOCPU &&
			    (pc = pcpu_find(iv->iv_event->ie_cpu)) != NULL) {
				iv->iv_mid = pc->pc_mid;
				iv->iv_ic->ic_assign(iv);
			} else
				intr_assign_next_cpu(iv);
		}
	}
	sx_xunlock(&intr_table_lock);
}
SYSINIT(intr_shuffle_irqs, SI_SUB_SMP, SI_ORDER_SECOND, intr_shuffle_irqs,
    NULL);
#endif
