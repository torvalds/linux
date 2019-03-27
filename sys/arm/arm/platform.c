/*-
 * Copyright (c) 2005 Peter Grehan
 * Copyright (c) 2009 Nathan Whitehorn
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Dispatch platform calls to the appropriate platform implementation
 * through a previously registered kernel object.
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/ktr.h>
#include <sys/mutex.h>
#include <sys/rman.h>
#include <sys/systm.h>
#include <sys/smp.h>
#include <sys/sysctl.h>
#include <sys/types.h>

#include <vm/vm.h>
#include <vm/vm_page.h>

#include <machine/bus_dma.h>
#include <machine/cpu.h>
#include <machine/intr.h>
#include <machine/machdep.h>
#include <machine/md_var.h>
#include <machine/platform.h>
#include <machine/platformvar.h>
#include <machine/smp.h>

#include "platform_if.h"

static platform_def_t	*plat_def_impl;
static platform_t	plat_obj;
static struct kobj_ops	plat_kernel_kops;
static struct platform_kobj	plat_kernel_obj;

static char plat_name[64];
SYSCTL_STRING(_hw, OID_AUTO, platform, CTLFLAG_RDTUN | CTLFLAG_NOFETCH, plat_name, 0,
    "Platform currently in use");

/*
 * Platform install routines. Highest priority wins, using the same
 * algorithm as bus attachment.
 */
SET_DECLARE(platform_set, platform_def_t);

static delay_func platform_delay;

platform_t
platform_obj(void)
{

	return (plat_obj);
}

void
platform_probe_and_attach(void)
{
	platform_def_t	**platpp, *platp;
	int		prio, best_prio;

	plat_obj = &plat_kernel_obj;
	best_prio = 0;

	/*
	 * We are unable to use TUNABLE_STR as the read will happen
	 * well after this function has returned.
	 */
	TUNABLE_STR_FETCH("hw.platform", plat_name, sizeof(plat_name));

	/*
	 * Try to locate the best platform kobj
	 */
	SET_FOREACH(platpp, platform_set) {
		platp = *platpp;

		/*
		 * Take care of compiling the selected class, and
		 * then statically initialise the MMU object
		 */
		kobj_class_compile_static((kobj_class_t)platp,
		    &plat_kernel_kops);
		kobj_init_static((kobj_t)plat_obj, (kobj_class_t)platp);

		plat_obj->cls = platp;

		prio = PLATFORM_PROBE(plat_obj);

		/* Check for errors */
		if (prio > 0)
			continue;

		/*
		 * Check if this module was specifically requested through
		 * the loader tunable we provide.
		 */
		if (strcmp(platp->name,plat_name) == 0) {
			plat_def_impl = platp;
			break;
		}

		/* Otherwise, see if it is better than our current best */
		if (plat_def_impl == NULL || prio > best_prio) {
			best_prio = prio;
			plat_def_impl = platp;
		}

		/*
		 * We can't free the KOBJ, since it is static. Reset the ops
		 * member of this class so that we can come back later.
		 */
		platp->ops = NULL;
	}

	if (plat_def_impl == NULL)
		panic("No platform module found!");

	/*
	 * Recompile to make sure we ended with the
	 * correct one, and then attach.
	 */

	kobj_class_compile_static((kobj_class_t)plat_def_impl,
	    &plat_kernel_kops);
	kobj_init_static((kobj_t)plat_obj, (kobj_class_t)plat_def_impl);

	strlcpy(plat_name, plat_def_impl->name, sizeof(plat_name));

	/* Set a default delay function */
	arm_set_delay(platform_delay, NULL);

	PLATFORM_ATTACH(plat_obj);
}

int
platform_devmap_init(void)
{

	return PLATFORM_DEVMAP_INIT(plat_obj);
}

vm_offset_t
platform_lastaddr(void)
{

	return PLATFORM_LASTADDR(plat_obj);
}

void
platform_gpio_init(void)
{

	PLATFORM_GPIO_INIT(plat_obj);
}

void
platform_late_init(void)
{

	PLATFORM_LATE_INIT(plat_obj);
}

void
cpu_reset(void)
{

	PLATFORM_CPU_RESET(plat_obj);

	printf("cpu_reset failed");

	intr_disable();
	while(1) {
		cpu_sleep(0);
	}
}

static void
platform_delay(int usec, void *arg __unused)
{
	int counts;

	for (; usec > 0; usec--)
		for (counts = plat_obj->cls->delay_count; counts > 0; counts--)
			/*
			 * Prevent the compiler from optimizing
			 * out the loop
			 */
			cpufunc_nullop();
}

#if defined(SMP)
void
platform_mp_setmaxid(void)
{
	int ncpu;

	PLATFORM_MP_SETMAXID(plat_obj);

	if (TUNABLE_INT_FETCH("hw.ncpu", &ncpu)) {
		if (ncpu >= 1 && ncpu <= mp_ncpus) {
			mp_ncpus = ncpu;
			mp_maxid = ncpu - 1;
		}
	}
}

void
platform_mp_start_ap(void)
{

	PLATFORM_MP_START_AP(plat_obj);
}
#endif
