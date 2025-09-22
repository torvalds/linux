/*	$OpenBSD: apm.c,v 1.37 2024/05/29 06:39:13 jsg Exp $	*/

/*-
 * Copyright (c) 2001 Alexander Guy.  All rights reserved.
 * Copyright (c) 1998-2001 Michael Shalayeff. All rights reserved.
 * Copyright (c) 1995 John T. Kohl.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the authors nor the names of contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include "apm.h"

#if NAPM > 1
#error only one APM emulation device may be configured
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/device.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/event.h>

#include <machine/conf.h>
#include <machine/cpu.h>
#include <machine/apmvar.h>
#include <machine/autoconf.h>

#include <macppc/dev/pm_direct.h>

#if defined(APMDEBUG)
#define DPRINTF(x)	printf x
#else
#define	DPRINTF(x)	/**/
#endif

struct apm_softc {
	struct device sc_dev;
	struct klist sc_note;
	int    sc_flags;
};

int apmmatch(struct device *, void *, void *);
void apmattach(struct device *, struct device *, void *);

const struct cfattach apm_ca = {
	sizeof(struct apm_softc), apmmatch, apmattach
};

struct cfdriver apm_cd = {
	NULL, "apm", DV_DULL
};

#define	APMUNIT(dev)	(minor(dev)&0xf0)
#define	APMDEV(dev)	(minor(dev)&0x0f)
#define APMDEV_NORMAL	0
#define APMDEV_CTL	8

void filt_apmrdetach(struct knote *kn);
int filt_apmread(struct knote *kn, long hint);
int apmkqfilter(dev_t dev, struct knote *kn);

const struct filterops apmread_filtops = {
	.f_flags	= FILTEROP_ISFD,
	.f_attach	= NULL,
	.f_detach	= filt_apmrdetach,
	.f_event	= filt_apmread,
};

/*
 * Flags to control kernel display
 *	SCFLAG_NOPRINT:		do not output APM power messages due to
 *				a power change event.
 *
 *	SCFLAG_PCTPRINT:	do not output APM power messages due to
 *				to a power change event unless the battery
 *				percentage changes.
 */

#define SCFLAG_NOPRINT	0x0008000
#define SCFLAG_PCTPRINT	0x0004000
#define SCFLAG_PRINT	(SCFLAG_NOPRINT|SCFLAG_PCTPRINT)

#define	SCFLAG_OREAD 	(1 << 0)
#define	SCFLAG_OWRITE	(1 << 1)
#define	SCFLAG_OPEN	(SCFLAG_OREAD|SCFLAG_OWRITE)


int
apmmatch(struct device *parent, void *match, void *aux)
{
	struct confargs *ca = aux;

	if (strcmp(ca->ca_name, "apm") != 0)
		return (0);

	return (1);
}

void
apmattach(struct device *parent, struct device *self, void *aux)
{
	struct pmu_battery_info info;

	pm_battery_info(0, &info);

	printf(": battery flags 0x%X, ", info.flags);
	printf("%d%% charged\n", ((info.cur_charge * 100) / info.max_charge));

#ifdef SUSPEND
	device_register_wakeup(self);
#endif
}

int
apmopen(dev_t dev, int flag, int mode, struct proc *p)
{
	struct apm_softc *sc;
	int error = 0;

	/* apm0 only */
	if (!apm_cd.cd_ndevs || APMUNIT(dev) != 0 ||
	    !(sc = apm_cd.cd_devs[APMUNIT(dev)]))
		return ENXIO;

	DPRINTF(("apmopen: dev %d pid %d flag %x mode %x\n",
	    APMDEV(dev), p->p_p->ps_pid, flag, mode));

	switch (APMDEV(dev)) {
	case APMDEV_CTL:
		if (!(flag & FWRITE)) {
			error = EINVAL;
			break;
		}
		if (sc->sc_flags & SCFLAG_OWRITE) {
			error = EBUSY;
			break;
		}
		sc->sc_flags |= SCFLAG_OWRITE;
		break;
	case APMDEV_NORMAL:
		if (!(flag & FREAD) || (flag & FWRITE)) {
			error = EINVAL;
			break;
		}
		sc->sc_flags |= SCFLAG_OREAD;
		break;
	default:
		error = ENXIO;
		break;
	}
	return error;
}

int
apmclose(dev_t dev, int flag, int mode, struct proc *p)
{
	struct apm_softc *sc;

	/* apm0 only */
	if (!apm_cd.cd_ndevs || APMUNIT(dev) != 0 ||
	    !(sc = apm_cd.cd_devs[APMUNIT(dev)]))
		return ENXIO;

	DPRINTF(("apmclose: pid %d flag %x mode %x\n",
	    p->p_p->ps_pid, flag, mode));

	switch (APMDEV(dev)) {
	case APMDEV_CTL:
		sc->sc_flags &= ~SCFLAG_OWRITE;
		break;
	case APMDEV_NORMAL:
		sc->sc_flags &= ~SCFLAG_OREAD;
		break;
	}
	return 0;
}

int
apmioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct apm_softc *sc;
	struct pmu_battery_info batt;
	struct apm_power_info *power;
	int error = 0;

	/* apm0 only */
	if (!apm_cd.cd_ndevs || APMUNIT(dev) != 0 ||
	    !(sc = apm_cd.cd_devs[APMUNIT(dev)]))
		return ENXIO;

	switch (cmd) {
#ifdef SUSPEND
	case APM_IOC_STANDBY:
	case APM_IOC_SUSPEND:
		if ((flag & FWRITE) == 0) {
			error = EBADF;
			break;
		}
		sleep_state(sc, SLEEP_SUSPEND);
		break;
#endif
	case APM_IOC_PRN_CTL:
		if ((flag & FWRITE) == 0)
			error = EBADF;
		else {
			int flag = *(int *)data;
			DPRINTF(( "APM_IOC_PRN_CTL: %d\n", flag ));
			switch (flag) {
			case APM_PRINT_ON:	/* enable printing */
				sc->sc_flags &= ~SCFLAG_PRINT;
				break;
			case APM_PRINT_OFF: /* disable printing */
				sc->sc_flags &= ~SCFLAG_PRINT;
				sc->sc_flags |= SCFLAG_NOPRINT;
				break;
			case APM_PRINT_PCT: /* disable some printing */
				sc->sc_flags &= ~SCFLAG_PRINT;
				sc->sc_flags |= SCFLAG_PCTPRINT;
				break;
			default:
				error = EINVAL;
				break;
			}
		}
		break;
	case APM_IOC_GETPOWER:
		power = (struct apm_power_info *)data;

		pm_battery_info(0, &batt);

		power->ac_state = ((batt.flags & PMU_PWR_AC_PRESENT) ?
		    APM_AC_ON : APM_AC_OFF);
		power->battery_life =
		    ((batt.cur_charge * 100) / batt.max_charge);

		/*
		 * If the battery is charging, return the minutes left until
		 * charging is complete. apmd knows this.
		 */

		if (!(batt.flags & PMU_PWR_BATT_PRESENT)) {
			power->battery_state = APM_BATT_UNKNOWN;
			power->minutes_left = 0;
			power->battery_life = 0;
		} else if ((power->ac_state == APM_AC_ON) &&
			   (batt.draw > 0)) {
			power->minutes_left =
			    (((batt.max_charge - batt.cur_charge) * 3600) /
			    batt.draw) / 60;
			power->battery_state = APM_BATT_CHARGING;
		} else {
			power->minutes_left =
			    ((batt.cur_charge * 3600) / (-batt.draw)) / 60;

			if (power->battery_life > 50)
				power->battery_state = APM_BATT_HIGH;
			else if (power->battery_life > 25)
				power->battery_state = APM_BATT_LOW;
			else
				power->battery_state = APM_BATT_CRITICAL;
		}
		break;
	default:
		error = ENOTTY;
	}

	return error;
}

void
filt_apmrdetach(struct knote *kn)
{
	struct apm_softc *sc = (struct apm_softc *)kn->kn_hook;

	klist_remove_locked(&sc->sc_note, kn);
}

int
filt_apmread(struct knote *kn, long hint)
{
	/* XXX weird kqueue_scan() semantics */
	if (hint && !kn->kn_data)
		kn->kn_data = (int)hint;

	return (1);
}

int
apmkqfilter(dev_t dev, struct knote *kn)
{
	struct apm_softc *sc;

	/* apm0 only */
	if (!apm_cd.cd_ndevs || APMUNIT(dev) != 0 ||
	    !(sc = apm_cd.cd_devs[APMUNIT(dev)]))
		return ENXIO;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &apmread_filtops;
		break;
	default:
		return (EINVAL);
	}

	kn->kn_hook = (caddr_t)sc;
	klist_insert_locked(&sc->sc_note, kn);

	return (0);
}

#ifdef SUSPEND

int
request_sleep(int sleepmode)
{
	return EOPNOTSUPP;
}

#ifdef MULTIPROCESSOR

void
sleep_mp(void)
{
}

void
resume_mp(void)
{
}

#endif /* MULTIPROCESSOR */

int
sleep_showstate(void *v, int sleepmode)
{
	switch (sleepmode) {
	case SLEEP_SUSPEND:
		return 0;
	default:
		return EOPNOTSUPP;
	}
}

int
sleep_setstate(void *v)
{
	return 0;
}

int
sleep_resume(void *v)
{
	return 0;
}

int
gosleep(void *v)
{
	return EOPNOTSUPP;
}

int
suspend_finish(void *v)
{
	return 0;
}

#endif /* SUSPEND */
