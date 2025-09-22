/* $OpenBSD: subr_suspend.c,v 1.21 2025/08/01 12:38:21 stsp Exp $ */
/*
 * Copyright (c) 2005 Thorsten Lockert <tholo@sigmasoft.com>
 * Copyright (c) 2005 Jordan Hargrave <jordan@openbsd.org>
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
#include <sys/buf.h>
#include <sys/clockintr.h>
#include <sys/reboot.h>
#include <sys/sensors.h>
#include <sys/sysctl.h>
#include <sys/mount.h>
#include <sys/syscallargs.h>
#include <dev/wscons/wsdisplayvar.h>
#ifdef GPROF
#include <sys/gmon.h>
#endif
#ifdef HIBERNATE
#include <sys/hibernate.h>
#endif

#include "softraid.h"
#include "wsdisplay.h"

/* Number of (active) wakeup devices in the system. */
u_int wakeup_devices;

/* Uptime of last resume. */
time_t resume_time;

void
device_register_wakeup(struct device *dev)
{
	wakeup_devices++;
}

int
sleep_state(void *v, int sleepmode)
{
	int error, s;
	extern int perflevel;
	size_t rndbuflen;
	char *rndbuf;
#ifdef GPROF
	int gmon_state;
#endif
#if NSOFTRAID > 0
	extern void sr_quiesce(void);
#endif

top:
	error = ENXIO;
	rndbuf = NULL;
	rndbuflen = 0;

	if (sleepmode == SLEEP_SUSPEND && wakeup_devices == 0)
		return EOPNOTSUPP;

	if (sleep_showstate(v, sleepmode))
		return EOPNOTSUPP;
#if NWSDISPLAY > 0
	wsdisplay_suspend();
#endif
	stop_periodic_resettodr();

#ifdef HIBERNATE
	if (sleepmode == SLEEP_HIBERNATE) {
		/*
		 * Discard useless memory to reduce fragmentation,
		 * and attempt to create a hibernate work area
		 */
		hibernate_suspend_bufcache();
		uvmpd_hibernate();
		if (hibernate_alloc()) {
			printf("failed to allocate hibernate memory\n");
			error = ENOMEM;
			goto fail_hiballoc;
		}
	}
#endif /* HIBERNATE */

	sensor_quiesce();
	if (config_suspend_all(DVACT_QUIESCE)) {
		error = EIO;
		goto fail_quiesce;
	}

	vfs_stall(curproc, 1);
#if NSOFTRAID > 0
	sr_quiesce();
#endif
	bufq_quiesce();
#ifdef MULTIPROCESSOR
	sched_stop_secondary_cpus();
	KASSERT(CPU_IS_PRIMARY(curcpu()));
#endif
#ifdef GPROF
	gmon_state = gmoninit;
	gmoninit = 0;
#endif
#ifdef MULTIPROCESSOR
	sleep_mp();
#endif

#ifdef HIBERNATE
	if (sleepmode == SLEEP_HIBERNATE) {
		/*
		 * We've just done various forms of syncing to disk
		 * churned lots of memory dirty.  We don't need to
		 * save that dirty memory to hibernate, so release it.
		 */
		hibernate_suspend_bufcache();
		uvmpd_hibernate();
	}
#endif /* HIBERNATE */

	resettodr();

	s = splhigh();
	intr_disable();	/* PSL_I for resume; PIC/APIC broken until repair */
	cold = 2;	/* Force other code to delay() instead of tsleep() */
	intr_enable_wakeup();

	if (config_suspend_all(DVACT_SUSPEND) != 0) {
		error = EDEADLK;
		goto fail_suspend;
	}
	suspend_randomness();
	if (sleep_setstate(v)) {
		error = ENOTBLK;
		goto fail_pts;
	}

	if (sleepmode == SLEEP_SUSPEND) {
		/*
		 * XXX
		 * Flag to disk drivers that they should "power down" the disk
		 * when we get to DVACT_POWERDOWN.
		 */
		boothowto |= RB_POWERDOWN;
		config_suspend_all(DVACT_POWERDOWN);
		boothowto &= ~RB_POWERDOWN;

		if (cpu_setperf != NULL)
			cpu_setperf(0);
	}

	error = gosleep(v);

#ifdef HIBERNATE
	if (sleepmode == SLEEP_HIBERNATE) {
		uvm_pmr_dirty_everything();
		hib_getentropy(&rndbuf, &rndbuflen);
	}
#endif /* HIBERNATE */

fail_pts:
	config_suspend_all(DVACT_RESUME);

fail_suspend:
	intr_disable_wakeup();
	cold = 0;
	intr_enable();
	splx(s);

	inittodr(gettime());
	clockintr_cpu_init(NULL);
	clockintr_trigger();

	resume_time = getuptime();

	sleep_resume(v);
	resume_randomness(rndbuf, rndbuflen);
#ifdef MULTIPROCESSOR
	resume_mp();
#endif
#ifdef GPROF
	gmoninit = gmon_state;
#endif
#ifdef MULTIPROCESSOR
	sched_start_secondary_cpus();
#endif
	vfs_stall(curproc, 0);
	bufq_restart();

fail_quiesce:
	config_suspend_all(DVACT_WAKEUP);
	sensor_restart();

#ifdef HIBERNATE
	if (sleepmode == SLEEP_HIBERNATE) {
		hibernate_free();
fail_hiballoc:
		hibernate_resume_bufcache();
	}
#endif /* HIBERNATE */

	start_periodic_resettodr();
#if NWSDISPLAY > 0
	wsdisplay_resume();
#endif
	sys_sync(curproc, NULL, NULL);
	if (cpu_setperf != NULL)
		cpu_setperf(perflevel);	/* Restore hw.setperf */
	if (suspend_finish(v) == EAGAIN)
		goto top;
	resume_time = getuptime();
	return (error);
}

int
resuming(void)
{
	return (getuptime() < resume_time + 10);
}
