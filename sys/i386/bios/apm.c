/*-
 * APM (Advanced Power Management) BIOS Device Driver
 *
 * Copyright (c) 1994 UKAI, Fumitoshi.
 * Copyright (c) 1994-1995 by HOSOKAWA, Tatsumi <hosokawa@jp.FreeBSD.org>
 * Copyright (c) 1996 Nate Williams <nate@FreeBSD.org>
 * Copyright (c) 1997 Poul-Henning Kamp <phk@FreeBSD.org>
 *
 * This software may be used, modified, copied, and distributed, in
 * both source and binary form provided that the above copyright and
 * these terms are retained. Under no circumstances is the author
 * responsible for the proper functioning of this software, nor does
 * the author assume any responsibility for damages incurred with its
 * use.
 *
 * Sep, 1994	Implemented on FreeBSD 1.1.5.1R (Toshiba AVS001WD)
 */

/*-
 * Copyright (c) 2000 Mitsuru IWASAKI <iwasaki@FreeBSD.org>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/condvar.h>
#include <sys/eventhandler.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/poll.h>
#include <sys/power.h>
#include <sys/reboot.h>
#include <sys/selinfo.h>
#include <sys/signalvar.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/timetc.h>
#include <sys/time.h>
#include <sys/uio.h>

#include <machine/apm_bios.h>
#include <machine/clock.h>
#include <machine/endian.h>
#include <machine/pc/bios.h>
#include <machine/cpufunc.h>
#include <machine/segments.h>
#include <machine/stdarg.h>
#include <machine/vm86.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_param.h>

#include <i386/bios/apm.h>
#include <isa/rtc.h>

/* Used by the apm_saver screen saver module */
int apm_display(int newstate);
struct apm_softc apm_softc;

static void apm_resume(void);
static int apm_bioscall(void);
static int apm_check_function_supported(u_int version, u_int func);

static int apm_pm_func(u_long, void*, ...);

static u_long	apm_version;

int	apm_evindex;

#define	SCFLAG_ONORMAL	0x0000001
#define	SCFLAG_OCTL	0x0000002
#define	SCFLAG_OPEN	(SCFLAG_ONORMAL|SCFLAG_OCTL)

#define APMDEV_NORMAL	0
#define APMDEV_CTL	1

static struct apmhook	*hook[NAPM_HOOK];		/* XXX */

#define is_enabled(foo) ((foo) ? "enabled" : "disabled")

/* Map version number to integer (keeps ordering of version numbers) */
#define INTVERSION(major, minor)	((major)*100 + (minor))

static d_open_t apmopen;
static d_close_t apmclose;
static d_write_t apmwrite;
static d_ioctl_t apmioctl;
static d_poll_t apmpoll;

static struct cdevsw apm_cdevsw = {
	.d_version =	D_VERSION,
	.d_flags =	D_NEEDGIANT,
	.d_open =	apmopen,
	.d_close =	apmclose,
	.d_write =	apmwrite,
	.d_ioctl =	apmioctl,
	.d_poll =	apmpoll,
	.d_name =	"apm",
};

static int apm_suspend_delay = 1;
static int apm_standby_delay = 1;
static int apm_swab_batt_minutes = 0;
static int apm_debug = 0;

#define APM_DPRINT(args...) do	{					\
	if (apm_debug) {						\
		printf(args);						\
	}								\
} while (0)

SYSCTL_INT(_machdep, OID_AUTO, apm_suspend_delay, CTLFLAG_RW, &apm_suspend_delay, 1, "");
SYSCTL_INT(_machdep, OID_AUTO, apm_standby_delay, CTLFLAG_RW, &apm_standby_delay, 1, "");
SYSCTL_INT(_debug, OID_AUTO, apm_debug, CTLFLAG_RW, &apm_debug, 0, "");

SYSCTL_INT(_machdep, OID_AUTO, apm_swab_batt_minutes, CTLFLAG_RWTUN,
	   &apm_swab_batt_minutes, 0, "Byte swap battery time value.");

/*
 * return  0 if the function successfull,
 * return  1 if the function unsuccessfull,
 * return -1 if the function unsupported.
 */
static int
apm_bioscall(void)
{
	struct apm_softc *sc = &apm_softc;
	int errno = 0;
	u_int apm_func = sc->bios.r.eax & 0xff;

	if (!apm_check_function_supported(sc->intversion, apm_func)) {
		APM_DPRINT("apm_bioscall: function 0x%x is not supported in v%d.%d\n",
		    apm_func, sc->majorversion, sc->minorversion);
		return (-1);
	}

	sc->bios_busy = 1;
	if (sc->connectmode == APM_PROT32CONNECT) {
		set_bios_selectors(&sc->bios.seg,
				   BIOSCODE_FLAG | BIOSDATA_FLAG);
		errno = bios32(&sc->bios.r,
			       sc->bios.entry, GSEL(GBIOSCODE32_SEL, SEL_KPL));
	} else {
		errno = bios16(&sc->bios, NULL);
	}
	sc->bios_busy = 0;
	return (errno);
}

/* check whether APM function is supported (1)  or not (0). */
static int
apm_check_function_supported(u_int version, u_int func)
{
	/* except driver version */
	if (func == APM_DRVVERSION) {
		return (1);
	}

	switch (version) {
	case INTVERSION(1, 0):
		if (func > APM_GETPMEVENT) {
			return (0); /* not supported */
		}
		break;
	case INTVERSION(1, 1):
		if (func > APM_ENGAGEDISENGAGEPM &&
		    func < APM_OEMFUNC) {
			return (0); /* not supported */
		}
		break;
	case INTVERSION(1, 2):
		break;
	}

	return (1); /* supported */
}

/* enable/disable power management */
static int
apm_enable_disable_pm(int enable)
{
	struct apm_softc *sc = &apm_softc;

	sc->bios.r.eax = (APM_BIOS << 8) | APM_ENABLEDISABLEPM;

	if (sc->intversion >= INTVERSION(1, 1))
		sc->bios.r.ebx  = PMDV_ALLDEV;
	else
		sc->bios.r.ebx  = 0xffff;	/* APM version 1.0 only */
	sc->bios.r.ecx  = enable;
	sc->bios.r.edx = 0;
	return (apm_bioscall());
}

/* register driver version (APM 1.1 or later) */
static int
apm_driver_version(int version)
{
	struct apm_softc *sc = &apm_softc;

	sc->bios.r.eax = (APM_BIOS << 8) | APM_DRVVERSION;
	sc->bios.r.ebx  = 0x0;
	sc->bios.r.ecx  = version;
	sc->bios.r.edx = 0;

	if (apm_bioscall() == 0 && sc->bios.r.eax == version)
		return (0);

	/* Some old BIOSes don't return the connection version in %ax. */
	if (sc->bios.r.eax == ((APM_BIOS << 8) | APM_DRVVERSION))
		return (0);

	return (1);
}

/* engage/disengage power management (APM 1.1 or later) */
static int
apm_engage_disengage_pm(int engage)
{
	struct apm_softc *sc = &apm_softc;

	sc->bios.r.eax = (APM_BIOS << 8) | APM_ENGAGEDISENGAGEPM;
	sc->bios.r.ebx = PMDV_ALLDEV;
	sc->bios.r.ecx = engage;
	sc->bios.r.edx = 0;
	return (apm_bioscall());
}

/* get PM event */
static u_int
apm_getevent(void)
{
	struct apm_softc *sc = &apm_softc;

	sc->bios.r.eax = (APM_BIOS << 8) | APM_GETPMEVENT;

	sc->bios.r.ebx = 0;
	sc->bios.r.ecx = 0;
	sc->bios.r.edx = 0;
	if (apm_bioscall())
		return (PMEV_NOEVENT);
	return (sc->bios.r.ebx & 0xffff);
}

/* suspend entire system */
static int
apm_suspend_system(int state)
{
	struct apm_softc *sc = &apm_softc;

	sc->bios.r.eax = (APM_BIOS << 8) | APM_SETPWSTATE;
	sc->bios.r.ebx = PMDV_ALLDEV;
	sc->bios.r.ecx = state;
	sc->bios.r.edx = 0;

	if (apm_bioscall()) {
 		printf("Entire system suspend failure: errcode = %d\n",
		       0xff & (sc->bios.r.eax >> 8));
 		return 1;
 	}

 	return 0;
}

/* Display control */
/*
 * Experimental implementation: My laptop machine can't handle this function
 * If your laptop can control the display via APM, please inform me.
 *                            HOSOKAWA, Tatsumi <hosokawa@jp.FreeBSD.org>
 */
int
apm_display(int newstate)
{
	struct apm_softc *sc = &apm_softc;

	sc->bios.r.eax = (APM_BIOS << 8) | APM_SETPWSTATE;
	sc->bios.r.ebx = PMDV_DISP0;
	sc->bios.r.ecx = newstate ? PMST_APMENABLED:PMST_SUSPEND;
	sc->bios.r.edx = 0;
	if (apm_bioscall() == 0) {
		return 0;
 	}

	/* If failed, then try to blank all display devices instead. */
	sc->bios.r.eax = (APM_BIOS << 8) | APM_SETPWSTATE;
	sc->bios.r.ebx = PMDV_DISPALL;	/* all display devices */
	sc->bios.r.ecx = newstate ? PMST_APMENABLED:PMST_SUSPEND;
	sc->bios.r.edx = 0;
	if (apm_bioscall() == 0) {
		return 0;
 	}
 	printf("Display off failure: errcode = %d\n",
	       0xff & (sc->bios.r.eax >> 8));
 	return 1;
}

/*
 * Turn off the entire system.
 */
static void
apm_power_off(void *junk, int howto)
{
	struct apm_softc *sc = &apm_softc;

	/* Not halting powering off, or not active */
	if (!(howto & RB_POWEROFF) || !apm_softc.active)
		return;
	sc->bios.r.eax = (APM_BIOS << 8) | APM_SETPWSTATE;
	sc->bios.r.ebx = PMDV_ALLDEV;
	sc->bios.r.ecx = PMST_OFF;
	sc->bios.r.edx = 0;
	(void) apm_bioscall();
}

/* APM Battery low handler */
static void
apm_battery_low(void)
{
	printf("\007\007 * * * BATTERY IS LOW * * * \007\007");
}

/* APM hook manager */
static struct apmhook *
apm_add_hook(struct apmhook **list, struct apmhook *ah)
{
	struct apmhook *p, *prev;

	APM_DPRINT("Add hook \"%s\"\n", ah->ah_name);

	if (ah == NULL)
		panic("illegal apm_hook!");
	prev = NULL;
	for (p = *list; p != NULL; prev = p, p = p->ah_next)
		if (p->ah_order > ah->ah_order)
			break;

	if (prev == NULL) {
		ah->ah_next = *list;
		*list = ah;
	} else {
		ah->ah_next = prev->ah_next;
		prev->ah_next = ah;
	}
	return ah;
}

static void
apm_del_hook(struct apmhook **list, struct apmhook *ah)
{
	struct apmhook *p, *prev;

	prev = NULL;
	for (p = *list; p != NULL; prev = p, p = p->ah_next)
		if (p == ah)
			goto deleteit;
	panic("Tried to delete unregistered apm_hook.");
	return;
deleteit:
	if (prev != NULL)
		prev->ah_next = p->ah_next;
	else
		*list = p->ah_next;
}


/* APM driver calls some functions automatically */
static void
apm_execute_hook(struct apmhook *list)
{
	struct apmhook *p;

	for (p = list; p != NULL; p = p->ah_next) {
		APM_DPRINT("Execute APM hook \"%s.\"\n", p->ah_name);
		if ((*(p->ah_fun))(p->ah_arg))
			printf("Warning: APM hook \"%s\" failed", p->ah_name);
	}
}


/* establish an apm hook */
struct apmhook *
apm_hook_establish(int apmh, struct apmhook *ah)
{
	if (apmh < 0 || apmh >= NAPM_HOOK)
		return NULL;

	return apm_add_hook(&hook[apmh], ah);
}

/* disestablish an apm hook */
void
apm_hook_disestablish(int apmh, struct apmhook *ah)
{
	if (apmh < 0 || apmh >= NAPM_HOOK)
		return;

	apm_del_hook(&hook[apmh], ah);
}

static int apm_record_event(struct apm_softc *, u_int);
static void apm_processevent(void);

static u_int apm_op_inprog = 0;

static void
apm_do_suspend(void)
{
	struct apm_softc *sc = &apm_softc;
	int error;

	if (sc == NULL || sc->initialized == 0)
		return;

	apm_op_inprog = 0;
	sc->suspends = sc->suspend_countdown = 0;

	EVENTHANDLER_INVOKE(power_suspend);

	/*
	 * Be sure to hold Giant across DEVICE_SUSPEND/RESUME since
	 * non-MPSAFE drivers need this.
	 */
	mtx_lock(&Giant);
	error = DEVICE_SUSPEND(root_bus);
	if (error)
		goto backout;

	apm_execute_hook(hook[APM_HOOK_SUSPEND]);
	if (apm_suspend_system(PMST_SUSPEND) == 0) {
		sc->suspending = 1;
		apm_processevent();
		mtx_unlock(&Giant);
		return;
	}

	/* Failure, 'resume' the system again */
	apm_execute_hook(hook[APM_HOOK_RESUME]);
	DEVICE_RESUME(root_bus);
backout:
	mtx_unlock(&Giant);
	EVENTHANDLER_INVOKE(power_resume);
}

static void
apm_do_standby(void)
{
	struct apm_softc *sc = &apm_softc;

	if (sc == NULL || sc->initialized == 0)
		return;

	apm_op_inprog = 0;
	sc->standbys = sc->standby_countdown = 0;

	/*
	 * As far as standby, we don't need to execute
	 * all of suspend hooks.
	 */
	if (apm_suspend_system(PMST_STANDBY) == 0)
		apm_processevent();
	return;
}

static void
apm_lastreq_notify(void)
{
	struct apm_softc *sc = &apm_softc;

	sc->bios.r.eax = (APM_BIOS << 8) | APM_SETPWSTATE;
	sc->bios.r.ebx = PMDV_ALLDEV;
	sc->bios.r.ecx = PMST_LASTREQNOTIFY;
	sc->bios.r.edx = 0;
	apm_bioscall();
}

static int
apm_lastreq_rejected(void)
{
	struct apm_softc *sc = &apm_softc;

	if (apm_op_inprog == 0) {
		return 1;	/* no operation in progress */
	}

	sc->bios.r.eax = (APM_BIOS << 8) | APM_SETPWSTATE;
	sc->bios.r.ebx = PMDV_ALLDEV;
	sc->bios.r.ecx = PMST_LASTREQREJECT;
	sc->bios.r.edx = 0;

	if (apm_bioscall()) {
		APM_DPRINT("apm_lastreq_rejected: failed\n");
		return 1;
	}
	apm_op_inprog = 0;
	return 0;
}

/*
 * Public interface to the suspend/resume:
 *
 * Execute suspend and resume hook before and after sleep, respectively.
 *
 */

void
apm_suspend(int state)
{
	struct apm_softc *sc = &apm_softc;

	if (sc == NULL || sc->initialized == 0)
		return;

	switch (state) {
	case PMST_SUSPEND:
		if (sc->suspends)
			return;
		sc->suspends++;
		sc->suspend_countdown = apm_suspend_delay;
		break;
	case PMST_STANDBY:
		if (sc->standbys)
			return;
		sc->standbys++;
		sc->standby_countdown = apm_standby_delay;
		break;
	default:
		printf("apm_suspend: Unknown Suspend state 0x%x\n", state);
		return;
	}

	apm_op_inprog++;
	apm_lastreq_notify();
}

static void
apm_resume(void)
{
	struct apm_softc *sc = &apm_softc;

	if (sc == NULL || sc->initialized == 0 || sc->suspending == 0)
		return;

	sc->suspending = 0;
	apm_execute_hook(hook[APM_HOOK_RESUME]);
	mtx_lock(&Giant);
	DEVICE_RESUME(root_bus);
	mtx_unlock(&Giant);
	EVENTHANDLER_INVOKE(power_resume);
}


/* get power status per battery */
static int
apm_get_pwstatus(apm_pwstatus_t app)
{
	struct apm_softc *sc = &apm_softc;

	if (app->ap_device != PMDV_ALLDEV &&
	    (app->ap_device < PMDV_BATT0 || app->ap_device > PMDV_BATT_ALL))
		return 1;

	sc->bios.r.eax = (APM_BIOS << 8) | APM_GETPWSTATUS;
	sc->bios.r.ebx = app->ap_device;
	sc->bios.r.ecx = 0;
	sc->bios.r.edx = 0xffff;	/* default to unknown battery time */

	if (apm_bioscall())
		return 1;

	app->ap_acline    = (sc->bios.r.ebx >> 8) & 0xff;
	app->ap_batt_stat = sc->bios.r.ebx & 0xff;
	app->ap_batt_flag = (sc->bios.r.ecx >> 8) & 0xff;
	app->ap_batt_life = sc->bios.r.ecx & 0xff;
	sc->bios.r.edx &= 0xffff;
	if (apm_swab_batt_minutes)
		sc->bios.r.edx = __bswap16(sc->bios.r.edx) | 0x8000;
	if (sc->bios.r.edx == 0xffff)	/* Time is unknown */
		app->ap_batt_time = -1;
	else if (sc->bios.r.edx & 0x8000)	/* Time is in minutes */
		app->ap_batt_time = (sc->bios.r.edx & 0x7fff) * 60;
	else				/* Time is in seconds */
		app->ap_batt_time = sc->bios.r.edx;

	return 0;
}


/* get APM information */
static int
apm_get_info(apm_info_t aip)
{
	struct apm_softc *sc = &apm_softc;
	struct apm_pwstatus aps;

	bzero(&aps, sizeof(aps));
	aps.ap_device = PMDV_ALLDEV;
	if (apm_get_pwstatus(&aps))
		return 1;

	aip->ai_infoversion = 1;
	aip->ai_acline      = aps.ap_acline;
	aip->ai_batt_stat   = aps.ap_batt_stat;
	aip->ai_batt_life   = aps.ap_batt_life;
	aip->ai_batt_time   = aps.ap_batt_time;
	aip->ai_major       = (u_int)sc->majorversion;
	aip->ai_minor       = (u_int)sc->minorversion;
	aip->ai_status      = (u_int)sc->active;

	sc->bios.r.eax = (APM_BIOS << 8) | APM_GETCAPABILITIES;
	sc->bios.r.ebx = 0;
	sc->bios.r.ecx = 0;
	sc->bios.r.edx = 0;
	if (apm_bioscall()) {
		aip->ai_batteries = 0xffffffff;	/* Unknown */
		aip->ai_capabilities = 0xff00; /* Unknown, with no bits set */
	} else {
		aip->ai_batteries = sc->bios.r.ebx & 0xff;
		aip->ai_capabilities = sc->bios.r.ecx & 0xff;
	}

	bzero(aip->ai_spare, sizeof aip->ai_spare);

	return 0;
}


/* inform APM BIOS that CPU is idle */
void
apm_cpu_idle(void)
{
	struct apm_softc *sc = &apm_softc;

	if (sc->active) {

		sc->bios.r.eax = (APM_BIOS <<8) | APM_CPUIDLE;
		sc->bios.r.edx = sc->bios.r.ecx = sc->bios.r.ebx = 0;
		(void) apm_bioscall();
	}
	/*
	 * Some APM implementation halts CPU in BIOS, whenever
	 * "CPU-idle" function are invoked, but swtch() of
	 * FreeBSD halts CPU, therefore, CPU is halted twice
	 * in the sched loop. It makes the interrupt latency
	 * terribly long and be able to cause a serious problem
	 * in interrupt processing. We prevent it by removing
	 * "hlt" operation from swtch() and managed it under
	 * APM driver.
	 */
	if (!sc->active || sc->always_halt_cpu)
		halt();	/* wait for interrupt */
}

/* inform APM BIOS that CPU is busy */
void
apm_cpu_busy(void)
{
	struct apm_softc *sc = &apm_softc;

	/*
	 * The APM specification says this is only necessary if your BIOS
	 * slows down the processor in the idle task, otherwise it's not
	 * necessary.
	 */
	if (sc->slow_idle_cpu && sc->active) {

		sc->bios.r.eax = (APM_BIOS <<8) | APM_CPUBUSY;
		sc->bios.r.edx = sc->bios.r.ecx = sc->bios.r.ebx = 0;
		apm_bioscall();
	}
}


/*
 * APM thread loop.
 *
 * This routine wakes up from time to time to deal with delaying the
 * suspend of the system, or other events.
 */
static void
apm_event_thread(void *arg)
{
	struct apm_softc *sc = &apm_softc;

	sc->running = 1;
	while (sc->active) {
		if (apm_op_inprog)
			apm_lastreq_notify();
		if (sc->standbys && sc->standby_countdown-- <= 0)
			apm_do_standby();
		if (sc->suspends && sc->suspend_countdown-- <= 0)
			apm_do_suspend();
		if (!sc->bios_busy)
			apm_processevent();
		mtx_lock(&sc->mtx);
		cv_timedwait(&sc->cv, &sc->mtx, 10 * hz / 9);
		mtx_unlock(&sc->mtx);
	}
	sc->running = 0;
	kproc_exit(0);
}

/* enable APM BIOS */
static void
apm_event_enable(void)
{
	struct apm_softc *sc = &apm_softc;

	APM_DPRINT("called apm_event_enable()\n");

	if (sc == NULL || sc->initialized == 0)
		return;

	/* Start the thread */
	sc->active = 1;
	if (kproc_create(apm_event_thread, sc, &sc->event_thread, 0, 0,
	    "apm worker"))
		panic("Cannot create apm worker thread");

	return;
}

/* disable APM BIOS */
static void
apm_event_disable(void)
{
	struct apm_softc *sc = &apm_softc;

	APM_DPRINT("called apm_event_disable()\n");

	if (sc == NULL || sc->initialized == 0)
		return;

	mtx_lock(&sc->mtx);
	sc->active = 0;
	while (sc->running) {
		cv_broadcast(&sc->cv);
		msleep(sc->event_thread, &sc->mtx, PWAIT, "apmdie", 0);
	}
	mtx_unlock(&sc->mtx);
	sc->event_thread = NULL;
	return;
}

/* halt CPU in scheduling loop */
static void
apm_halt_cpu(void)
{
	struct apm_softc *sc = &apm_softc;

	if (sc == NULL || sc->initialized == 0)
		return;

	sc->always_halt_cpu = 1;

	return;
}

/* don't halt CPU in scheduling loop */
static void
apm_not_halt_cpu(void)
{
	struct apm_softc *sc = &apm_softc;

	if (sc == NULL || sc->initialized == 0)
		return;

	sc->always_halt_cpu = 0;

	return;
}

/* device driver definitions */

/*
 * Module event
 */

static int
apm_modevent(struct module *mod, int event, void *junk)
{

	switch (event) {
	case MOD_LOAD:
		if (!cold)
			return (EPERM);
		break;
	case MOD_UNLOAD:
		if (!cold && power_pm_get_type() == POWER_PM_TYPE_APM)
			return (EBUSY);
		break;
	default:
		break;
	}

	return (0);
}

/*
 * Create "connection point"
 */
static void
apm_identify(driver_t *driver, device_t parent)
{
	device_t child;

	if (!cold) {
		printf("Don't load this driver from userland!!\n");
		return;
	}

	if (resource_disabled("apm", 0))
		return;

	child = BUS_ADD_CHILD(parent, 0, "apm", 0);
	if (child == NULL)
		panic("apm_identify");
}

/*
 * probe for APM BIOS
 */
static int
apm_probe(device_t dev)
{
#define APM_KERNBASE	KERNBASE
	struct vm86frame	vmf;
	struct apm_softc	*sc = &apm_softc;

	device_set_desc(dev, "APM BIOS");
	if (device_get_unit(dev) > 0) {
		printf("apm: Only one APM driver supported.\n");
		return ENXIO;
	}

	if (power_pm_get_type() != POWER_PM_TYPE_NONE &&
	    power_pm_get_type() != POWER_PM_TYPE_APM) {
		printf("apm: Other PM system enabled.\n");
		return ENXIO;
	}

	bzero(&vmf, sizeof(struct vm86frame));		/* safety */
	bzero(&apm_softc, sizeof(apm_softc));
	vmf.vmf_ah = APM_BIOS;
	vmf.vmf_al = APM_INSTCHECK;
	vmf.vmf_bx = 0;
	if (vm86_intcall(APM_INT, &vmf))
		return ENXIO;			/* APM not found */
	if (vmf.vmf_bx != 0x504d) {
		printf("apm: incorrect signature (0x%x)\n", vmf.vmf_bx);
		return ENXIO;
	}
	if ((vmf.vmf_cx & (APM_32BIT_SUPPORT | APM_16BIT_SUPPORT)) == 0) {
		printf("apm: protected mode connections are not supported\n");
		return ENXIO;
	}

	apm_version = vmf.vmf_ax;
	sc->slow_idle_cpu = ((vmf.vmf_cx & APM_CPUIDLE_SLOW) != 0);
	sc->disabled = ((vmf.vmf_cx & APM_DISABLED) != 0);
	sc->disengaged = ((vmf.vmf_cx & APM_DISENGAGED) != 0);

	vmf.vmf_ah = APM_BIOS;
	vmf.vmf_al = APM_DISCONNECT;
	vmf.vmf_bx = 0;
        vm86_intcall(APM_INT, &vmf);		/* disconnect, just in case */

	if ((vmf.vmf_cx & APM_32BIT_SUPPORT) != 0) {
		vmf.vmf_ah = APM_BIOS;
		vmf.vmf_al = APM_PROT32CONNECT;
		vmf.vmf_bx = 0;
		if (vm86_intcall(APM_INT, &vmf)) {
			printf("apm: 32-bit connection error.\n");
			return (ENXIO);
 		}
		sc->bios.seg.code32.base = (vmf.vmf_ax << 4) + APM_KERNBASE;
		sc->bios.seg.code32.limit = 0xffff;
		sc->bios.seg.code16.base = (vmf.vmf_cx << 4) + APM_KERNBASE;
		sc->bios.seg.code16.limit = 0xffff;
		sc->bios.seg.data.base = (vmf.vmf_dx << 4) + APM_KERNBASE;
		sc->bios.seg.data.limit = 0xffff;
		sc->bios.entry = vmf.vmf_ebx;
		sc->connectmode = APM_PROT32CONNECT;
 	} else {
		/* use 16-bit connection */
		vmf.vmf_ah = APM_BIOS;
		vmf.vmf_al = APM_PROT16CONNECT;
		vmf.vmf_bx = 0;
		if (vm86_intcall(APM_INT, &vmf)) {
			printf("apm: 16-bit connection error.\n");
			return (ENXIO);
		}
		sc->bios.seg.code16.base = (vmf.vmf_ax << 4) + APM_KERNBASE;
		sc->bios.seg.code16.limit = 0xffff;
		sc->bios.seg.data.base = (vmf.vmf_cx << 4) + APM_KERNBASE;
		sc->bios.seg.data.limit = 0xffff;
		sc->bios.entry = vmf.vmf_bx;
		sc->connectmode = APM_PROT16CONNECT;
	}

	return(0);
}


/*
 * return 0 if the user will notice and handle the event,
 * return 1 if the kernel driver should do so.
 */
static int
apm_record_event(struct apm_softc *sc, u_int event_type)
{
	struct apm_event_info *evp;

	if ((sc->sc_flags & SCFLAG_OPEN) == 0)
		return 1;		/* no user waiting */
	if (sc->event_count == APM_NEVENTS)
		return 1;			/* overflow */
	if (sc->event_filter[event_type] == 0)
		return 1;		/* not registered */
	evp = &sc->event_list[sc->event_ptr];
	sc->event_count++;
	sc->event_ptr++;
	sc->event_ptr %= APM_NEVENTS;
	evp->type = event_type;
	evp->index = ++apm_evindex;
	selwakeuppri(&sc->sc_rsel, PZERO);
	return (sc->sc_flags & SCFLAG_OCTL) ? 0 : 1; /* user may handle */
}

/* Power profile */
static void
apm_power_profile(struct apm_softc *sc)
{
	int state;
	struct apm_info info;
	static int apm_acline = 0;

	if (apm_get_info(&info))
		return;

	if (apm_acline != info.ai_acline) {
		apm_acline = info.ai_acline;
		state = apm_acline ? POWER_PROFILE_PERFORMANCE : POWER_PROFILE_ECONOMY;
		power_profile_set_state(state);
	}
}

/* Process APM event */
static void
apm_processevent(void)
{
	int apm_event;
	struct apm_softc *sc = &apm_softc;

#define OPMEV_DEBUGMESSAGE(symbol) case symbol:				\
	APM_DPRINT("Received APM Event: " #symbol "\n");

	do {
		apm_event = apm_getevent();
		switch (apm_event) {
		    OPMEV_DEBUGMESSAGE(PMEV_STANDBYREQ);
			if (apm_op_inprog == 0) {
			    apm_op_inprog++;
			    if (apm_record_event(sc, apm_event)) {
				apm_suspend(PMST_STANDBY);
			    }
			}
			break;
		    OPMEV_DEBUGMESSAGE(PMEV_USERSTANDBYREQ);
			if (apm_op_inprog == 0) {
			    apm_op_inprog++;
			    if (apm_record_event(sc, apm_event)) {
				apm_suspend(PMST_STANDBY);
			    }
			}
			break;
		    OPMEV_DEBUGMESSAGE(PMEV_SUSPENDREQ);
 			apm_lastreq_notify();
			if (apm_op_inprog == 0) {
			    apm_op_inprog++;
			    if (apm_record_event(sc, apm_event)) {
				apm_do_suspend();
			    }
			}
			return; /* XXX skip the rest */
		    OPMEV_DEBUGMESSAGE(PMEV_USERSUSPENDREQ);
 			apm_lastreq_notify();
			if (apm_op_inprog == 0) {
			    apm_op_inprog++;
			    if (apm_record_event(sc, apm_event)) {
				apm_do_suspend();
			    }
			}
			return; /* XXX skip the rest */
		    OPMEV_DEBUGMESSAGE(PMEV_CRITSUSPEND);
			apm_do_suspend();
			break;
		    OPMEV_DEBUGMESSAGE(PMEV_NORMRESUME);
			apm_record_event(sc, apm_event);
			apm_resume();
			break;
		    OPMEV_DEBUGMESSAGE(PMEV_CRITRESUME);
			apm_record_event(sc, apm_event);
			apm_resume();
			break;
		    OPMEV_DEBUGMESSAGE(PMEV_STANDBYRESUME);
			apm_record_event(sc, apm_event);
			break;
		    OPMEV_DEBUGMESSAGE(PMEV_BATTERYLOW);
			if (apm_record_event(sc, apm_event)) {
			    apm_battery_low();
			    apm_suspend(PMST_SUSPEND);
			}
			break;
		    OPMEV_DEBUGMESSAGE(PMEV_POWERSTATECHANGE);
			apm_record_event(sc, apm_event);
			apm_power_profile(sc);
			break;
		    OPMEV_DEBUGMESSAGE(PMEV_UPDATETIME);
			apm_record_event(sc, apm_event);
			inittodr(0);	/* adjust time to RTC */
			break;
		    OPMEV_DEBUGMESSAGE(PMEV_CAPABILITIESCHANGE);
			apm_record_event(sc, apm_event);
			apm_power_profile(sc);
			break;
		    case PMEV_NOEVENT:
			break;
		    default:
			printf("Unknown Original APM Event 0x%x\n", apm_event);
			    break;
		}
	} while (apm_event != PMEV_NOEVENT);
}

static struct timeval suspend_time;
static struct timeval diff_time;

static int
apm_rtc_suspend(void *arg __unused)
{

	microtime(&diff_time);
	inittodr(0);
	microtime(&suspend_time);
	timevalsub(&diff_time, &suspend_time);
	return (0);
}

static int
apm_rtc_resume(void *arg __unused)
{
	u_int second, minute, hour;
	struct timeval resume_time, tmp_time;

	/* modified for adjkerntz */
	timer_restore();		/* restore the all timers */
	inittodr(0);			/* adjust time to RTC */
	microtime(&resume_time);
	getmicrotime(&tmp_time);
	timevaladd(&tmp_time, &diff_time);
	/* Calculate the delta time suspended */
	timevalsub(&resume_time, &suspend_time);

#ifdef PMTIMER_FIXUP_CALLTODO
	/* Fixup the calltodo list with the delta time. */
	adjust_timeout_calltodo(&resume_time);
#endif /* PMTIMER_FIXUP_CALLTODO */
	second = resume_time.tv_sec;
	hour = second / 3600;
	second %= 3600;
	minute = second / 60;
	second %= 60;
	log(LOG_NOTICE, "wakeup from sleeping state (slept %02d:%02d:%02d)\n",
		hour, minute, second);
	return (0);
}

/*
 * Attach APM:
 *
 * Initialize APM driver
 */

static int
apm_attach(device_t dev)
{
	struct apm_softc	*sc = &apm_softc;
	int			drv_version;

	mtx_init(&sc->mtx, device_get_nameunit(dev), "apm", MTX_DEF);
	cv_init(&sc->cv, "cbb cv");

	if (device_get_flags(dev) & 0x20)
		atrtcclock_disable = 1;

	sc->initialized = 0;

	/* Must be externally enabled */
	sc->active = 0;

	/* Always call HLT in idle loop */
	sc->always_halt_cpu = 1;

	getenv_int("debug.apm_debug", &apm_debug);

	/* print bootstrap messages */
	APM_DPRINT("apm: APM BIOS version %04lx\n",  apm_version);
	APM_DPRINT("apm: Code16 0x%08x, Data 0x%08x\n",
	    sc->bios.seg.code16.base, sc->bios.seg.data.base);
	APM_DPRINT("apm: Code entry 0x%08x, Idling CPU %s, Management %s\n",
	    sc->bios.entry, is_enabled(sc->slow_idle_cpu),
	    is_enabled(!sc->disabled));
	APM_DPRINT("apm: CS_limit=0x%x, DS_limit=0x%x\n",
	    sc->bios.seg.code16.limit, sc->bios.seg.data.limit);

	/*
         * In one test, apm bios version was 1.02; an attempt to register
         * a 1.04 driver resulted in a 1.00 connection!  Registering a
         * 1.02 driver resulted in a 1.02 connection.
         */
	drv_version = apm_version > 0x102 ? 0x102 : apm_version;
	for (; drv_version > 0x100; drv_version--)
		if (apm_driver_version(drv_version) == 0)
			break;
	sc->minorversion = ((drv_version & 0x00f0) >>  4) * 10 +
		((drv_version & 0x000f) >> 0);
	sc->majorversion = ((drv_version & 0xf000) >> 12) * 10 +
		((apm_version & 0x0f00) >> 8);

	sc->intversion = INTVERSION(sc->majorversion, sc->minorversion);

	if (sc->intversion >= INTVERSION(1, 1))
		APM_DPRINT("apm: Engaged control %s\n", is_enabled(!sc->disengaged));
	device_printf(dev, "found APM BIOS v%ld.%ld, connected at v%d.%d\n",
	       ((apm_version & 0xf000) >> 12) * 10 + ((apm_version & 0x0f00) >> 8),
	       ((apm_version & 0x00f0) >> 4) * 10 + ((apm_version & 0x000f) >> 0),
	       sc->majorversion, sc->minorversion);


	APM_DPRINT("apm: Slow Idling CPU %s\n", is_enabled(sc->slow_idle_cpu));
	/* enable power management */
	if (sc->disabled) {
		if (apm_enable_disable_pm(1)) {
			APM_DPRINT("apm: *Warning* enable function failed! [%x]\n",
			    (sc->bios.r.eax >> 8) & 0xff);
		}
	}

	/* engage power managment (APM 1.1 or later) */
	if (sc->intversion >= INTVERSION(1, 1) && sc->disengaged) {
		if (apm_engage_disengage_pm(1)) {
			APM_DPRINT("apm: *Warning* engage function failed err=[%x]",
			    (sc->bios.r.eax >> 8) & 0xff);
			APM_DPRINT(" (Docked or using external power?).\n");
		}
	}

	/* Power the system off using APM */
	EVENTHANDLER_REGISTER(shutdown_final, apm_power_off, NULL,
			      SHUTDOWN_PRI_LAST);

	/* Register APM again to pass the correct argument of pm_func. */
	power_pm_register(POWER_PM_TYPE_APM, apm_pm_func, sc);

	sc->initialized = 1;
	sc->suspending = 0;
	sc->running = 0;

	make_dev(&apm_cdevsw, APMDEV_NORMAL,
	    UID_ROOT, GID_OPERATOR, 0664, "apm");
	make_dev(&apm_cdevsw, APMDEV_CTL,
	    UID_ROOT, GID_OPERATOR, 0660, "apmctl");

	sc->sc_suspend.ah_fun = apm_rtc_suspend;
	sc->sc_suspend.ah_arg = sc;
	apm_hook_establish(APM_HOOK_SUSPEND, &sc->sc_suspend);

	sc->sc_resume.ah_fun = apm_rtc_resume;
	sc->sc_resume.ah_arg = sc;
	apm_hook_establish(APM_HOOK_RESUME, &sc->sc_resume);

	return 0;
}

static int
apmopen(struct cdev *dev, int flag, int fmt, struct thread *td)
{
	struct apm_softc *sc = &apm_softc;

	if (sc == NULL || sc->initialized == 0)
		return (ENXIO);

	switch (dev2unit(dev)) {
	case APMDEV_CTL:
		if (!(flag & FWRITE))
			return EINVAL;
		if (sc->sc_flags & SCFLAG_OCTL)
			return EBUSY;
		sc->sc_flags |= SCFLAG_OCTL;
		bzero(sc->event_filter, sizeof sc->event_filter);
		break;
	case APMDEV_NORMAL:
		sc->sc_flags |= SCFLAG_ONORMAL;
		break;
	}
	return 0;
}

static int
apmclose(struct cdev *dev, int flag, int fmt, struct thread *td)
{
	struct apm_softc *sc = &apm_softc;

	switch (dev2unit(dev)) {
	case APMDEV_CTL:
		apm_lastreq_rejected();
		sc->sc_flags &= ~SCFLAG_OCTL;
		bzero(sc->event_filter, sizeof sc->event_filter);
		break;
	case APMDEV_NORMAL:
		sc->sc_flags &= ~SCFLAG_ONORMAL;
		break;
	}
	if ((sc->sc_flags & SCFLAG_OPEN) == 0) {
		sc->event_count = 0;
		sc->event_ptr = 0;
	}
	return 0;
}

static int
apmioctl(struct cdev *dev, u_long cmd, caddr_t addr, int flag, struct thread *td)
{
	struct apm_softc *sc = &apm_softc;
	struct apm_bios_arg *args;
	int error = 0;
	int ret;
	int newstate;

	if (sc == NULL || sc->initialized == 0)
		return (ENXIO);

	APM_DPRINT("APM ioctl: cmd = 0x%lx\n", cmd);
	switch (cmd) {
	case APMIO_SUSPEND:
		if (!(flag & FWRITE))
			return (EPERM);
		if (sc->active)
			apm_suspend(PMST_SUSPEND);
		else
			error = EINVAL;
		break;

	case APMIO_STANDBY:
		if (!(flag & FWRITE))
			return (EPERM);
		if (sc->active)
			apm_suspend(PMST_STANDBY);
		else
			error = EINVAL;
		break;

	case APMIO_GETINFO_OLD:
		{
			struct apm_info info;
			apm_info_old_t aiop;

			if (apm_get_info(&info))
				error = ENXIO;
			aiop = (apm_info_old_t)addr;
			aiop->ai_major = info.ai_major;
			aiop->ai_minor = info.ai_minor;
			aiop->ai_acline = info.ai_acline;
			aiop->ai_batt_stat = info.ai_batt_stat;
			aiop->ai_batt_life = info.ai_batt_life;
			aiop->ai_status = info.ai_status;
		}
		break;
	case APMIO_GETINFO:
		if (apm_get_info((apm_info_t)addr))
			error = ENXIO;
		break;
	case APMIO_GETPWSTATUS:
		if (apm_get_pwstatus((apm_pwstatus_t)addr))
			error = ENXIO;
		break;
	case APMIO_ENABLE:
		if (!(flag & FWRITE))
			return (EPERM);
		apm_event_enable();
		break;
	case APMIO_DISABLE:
		if (!(flag & FWRITE))
			return (EPERM);
		apm_event_disable();
		break;
	case APMIO_HALTCPU:
		if (!(flag & FWRITE))
			return (EPERM);
		apm_halt_cpu();
		break;
	case APMIO_NOTHALTCPU:
		if (!(flag & FWRITE))
			return (EPERM);
		apm_not_halt_cpu();
		break;
	case APMIO_DISPLAY:
		if (!(flag & FWRITE))
			return (EPERM);
		newstate = *(int *)addr;
		if (apm_display(newstate))
			error = ENXIO;
		break;
	case APMIO_BIOS:
		if (!(flag & FWRITE))
			return (EPERM);
		/* XXX compatibility with the old interface */
		args = (struct apm_bios_arg *)addr;
		sc->bios.r.eax = args->eax;
		sc->bios.r.ebx = args->ebx;
		sc->bios.r.ecx = args->ecx;
		sc->bios.r.edx = args->edx;
		sc->bios.r.esi = args->esi;
		sc->bios.r.edi = args->edi;
		if ((ret = apm_bioscall())) {
			/*
			 * Return code 1 means bios call was unsuccessful.
			 * Error code is stored in %ah.
			 * Return code -1 means bios call was unsupported
			 * in the APM BIOS version.
			 */
			if (ret == -1) {
				error = EINVAL;
			}
		} else {
			/*
			 * Return code 0 means bios call was successful.
			 * We need only %al and can discard %ah.
			 */
			sc->bios.r.eax &= 0xff;
		}
		args->eax = sc->bios.r.eax;
		args->ebx = sc->bios.r.ebx;
		args->ecx = sc->bios.r.ecx;
		args->edx = sc->bios.r.edx;
		args->esi = sc->bios.r.esi;
		args->edi = sc->bios.r.edi;
		break;
	default:
		error = EINVAL;
		break;
	}

	/* for /dev/apmctl */
	if (dev2unit(dev) == APMDEV_CTL) {
		struct apm_event_info *evp;
		int i;

		error = 0;
		switch (cmd) {
		case APMIO_NEXTEVENT:
			if (!sc->event_count) {
				error = EAGAIN;
			} else {
				evp = (struct apm_event_info *)addr;
				i = sc->event_ptr + APM_NEVENTS - sc->event_count;
				i %= APM_NEVENTS;
				*evp = sc->event_list[i];
				sc->event_count--;
			}
			break;
		case APMIO_REJECTLASTREQ:
			if (apm_lastreq_rejected()) {
				error = EINVAL;
			}
			break;
		default:
			error = EINVAL;
			break;
		}
	}

	return error;
}

static int
apmwrite(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct apm_softc *sc = &apm_softc;
	u_int event_type;
	int error;
	u_char enabled;

	if (dev2unit(dev) != APMDEV_CTL)
		return(ENODEV);
	if (uio->uio_resid != sizeof(u_int))
		return(E2BIG);

	if ((error = uiomove((caddr_t)&event_type, sizeof(u_int), uio)))
		return(error);

	if (event_type >= APM_NPMEV)
		return(EINVAL);

	if (sc->event_filter[event_type] == 0) {
		enabled = 1;
	} else {
		enabled = 0;
	}
	sc->event_filter[event_type] = enabled;
	APM_DPRINT("apmwrite: event 0x%x %s\n", event_type, is_enabled(enabled));

	return uio->uio_resid;
}

static int
apmpoll(struct cdev *dev, int events, struct thread *td)
{
	struct apm_softc *sc = &apm_softc;
	int revents = 0;

	if (events & (POLLIN | POLLRDNORM)) {
		if (sc->event_count) {
			revents |= events & (POLLIN | POLLRDNORM);
		} else {
			selrecord(td, &sc->sc_rsel);
		}
	}

	return (revents);
}

static device_method_t apm_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	apm_identify),
	DEVMETHOD(device_probe,		apm_probe),
	DEVMETHOD(device_attach,	apm_attach),

	{ 0, 0 }
};

static driver_t apm_driver = {
	"apm",
	apm_methods,
	1,			/* no softc (XXX) */
};

static devclass_t apm_devclass;

DRIVER_MODULE(apm, legacy, apm_driver, apm_devclass, apm_modevent, 0);
MODULE_VERSION(apm, 1);

static int
apm_pm_func(u_long cmd, void *arg, ...)
{
	int	state, apm_state;
	int	error;
	va_list	ap;

	error = 0;
	switch (cmd) {
	case POWER_CMD_SUSPEND:
		va_start(ap, arg);
		state = va_arg(ap, int);
		va_end(ap);	

		switch (state) {
		case POWER_SLEEP_STATE_STANDBY:
			apm_state = PMST_STANDBY;
			break;
		case POWER_SLEEP_STATE_SUSPEND:
		case POWER_SLEEP_STATE_HIBERNATE:
			apm_state = PMST_SUSPEND;
			break;
		default:
			error = EINVAL;
			goto out;
		}

		apm_suspend(apm_state);
		break;

	default:
		error = EINVAL;
		goto out;
	}

out:
	return (error);
}

static void
apm_pm_register(void *arg)
{

	if (!resource_disabled("apm", 0))
		power_pm_register(POWER_PM_TYPE_APM, apm_pm_func, NULL);
}

SYSINIT(power, SI_SUB_KLD, SI_ORDER_ANY, apm_pm_register, NULL);
