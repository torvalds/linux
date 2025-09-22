/*	$OpenBSD: apm.c,v 1.44 2024/05/29 06:39:13 jsg Exp $	*/

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
#include "wsdisplay.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/clockintr.h>
#include <sys/device.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/buf.h>
#include <sys/event.h>
#include <sys/reboot.h>
#include <sys/hibernate.h>

#include <machine/autoconf.h>
#include <machine/conf.h>
#include <machine/cpu.h>
#include <machine/apmvar.h>

#include <dev/pci/pcivar.h>    /* pci_dopm */

#include <dev/wscons/wsdisplayvar.h>

#include <loongson/dev/kb3310var.h>

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
int apm_getdefaultinfo(struct apm_power_info *);

int apm_suspend(int state);

const struct filterops apmread_filtops = {
	.f_flags	= FILTEROP_ISFD,
	.f_attach	= NULL,
	.f_detach	= filt_apmrdetach,
	.f_event	= filt_apmread,
};

int (*get_apminfo)(struct apm_power_info *) = apm_getdefaultinfo;

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
	struct mainbus_attach_args *maa = aux;

	/*
	 * It only makes sense to attach on a 2F system, since 2E do not
	 * feature speed throttling, and we do not support 2E-based
	 * notebooks yet (assuming there are any).
	 */
	if (strcmp(maa->maa_name, apm_cd.cd_name) == 0 && loongson_ver == 0x2f)
		return (1);
	return (0);
}

void
apmattach(struct device *parent, struct device *self, void *aux)
{
	/* Enable PCI Power Management. */
	pci_dopm = 1;

	printf("\n");
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
	struct apm_power_info *power;
	int error = 0;

	/* apm0 only */
	if (!apm_cd.cd_ndevs || APMUNIT(dev) != 0 ||
	    !(sc = apm_cd.cd_devs[APMUNIT(dev)]))
		return ENXIO;

	switch (cmd) {
		/* some ioctl names from linux */
	case APM_IOC_STANDBY:
	case APM_IOC_STANDBY_REQ:
	case APM_IOC_SUSPEND:
	case APM_IOC_SUSPEND_REQ:
		if ((flag & FWRITE) == 0)
			error = EBADF;
		else if (sys_platform->suspend == NULL ||
		    sys_platform->resume == NULL)
			error = EOPNOTSUPP;
		else
			error = apm_suspend(APM_IOC_SUSPEND);
		break;
#ifdef HIBERNATE
	case APM_IOC_HIBERNATE:
		if ((flag & FWRITE) == 0)
			error = EBADF;
		else if (sys_platform->suspend == NULL ||
		    sys_platform->resume == NULL)
			error = EOPNOTSUPP;
		else
			error = apm_suspend(APM_IOC_HIBERNATE);
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
	case APM_IOC_DEV_CTL:
		if ((flag & FWRITE) == 0)
			error = EBADF;
		else
			error = EOPNOTSUPP; /* XXX */
		break;
	case APM_IOC_GETPOWER:
		power = (struct apm_power_info *)data;
		error = (*get_apminfo)(power);
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

int
apm_getdefaultinfo(struct apm_power_info *info)
{
	info->battery_state = APM_BATT_UNKNOWN;
	info->ac_state = APM_AC_UNKNOWN;
	info->battery_life = 0;
	info->minutes_left = -1;
	return (0);
}

void
apm_setinfohook(int (*hook)(struct apm_power_info *))
{
	get_apminfo = hook;
}

int
apm_record_event(u_int event, const char *src, const char *msg)
{
	static int apm_evindex;
	struct apm_softc *sc;

	/* apm0 only */
	if (apm_cd.cd_ndevs == 0 || (sc = apm_cd.cd_devs[0]) == NULL)
		return ENXIO;

	if ((sc->sc_flags & SCFLAG_NOPRINT) == 0)
		printf("%s: %s %s\n", sc->sc_dev.dv_xname, src, msg);

	/* skip if no user waiting */
	if ((sc->sc_flags & SCFLAG_OPEN) == 0)
		return (1);

	apm_evindex++;
	knote_locked(&sc->sc_note, APM_EVENT_COMPOSE(event, apm_evindex));

	return (0);
}

int
apm_suspend(int state)
{
	int rv;
	int s;

#if NWSDISPLAY > 0
	wsdisplay_suspend();
#endif

	stop_periodic_resettodr();
	resettodr();

	config_suspend_all(DVACT_QUIESCE);
	bufq_quiesce();

	s = splhigh();
	(void)disableintr();
	cold = 2;

	rv = config_suspend_all(DVACT_SUSPEND);

	suspend_randomness();

#ifdef HIBERNATE
	if (state == APM_IOC_HIBERNATE) {
		uvm_pmr_zero_everything();
		if (hibernate_suspend()) {
			printf("apm: hibernate_suspend failed");
			uvm_pmr_dirty_everything();
			return (ECANCELED);
		}
	}
#endif

	/* XXX
	 * Flag to disk drivers that they should "power down" the disk
	 * when we get to DVACT_POWERDOWN.
	 */
	boothowto |= RB_POWERDOWN;
	config_suspend_all(DVACT_POWERDOWN);
	boothowto &= ~RB_POWERDOWN;

	if (rv == 0) {
		rv = sys_platform->suspend();
		if (rv == 0)
			rv = sys_platform->resume();
	}

	inittodr(gettime());	/* Move the clock forward */
	clockintr_cpu_init(NULL);
	clockintr_trigger();

	config_suspend_all(DVACT_RESUME);

	cold = 0;
	(void)enableintr();
	splx(s);

	resume_randomness(NULL, 0);	/* force RNG upper level reseed */
	bufq_restart();

	config_suspend_all(DVACT_WAKEUP);

	start_periodic_resettodr();

#if NWSDISPLAY > 0
	wsdisplay_resume();
#endif

	apm_record_event(APM_NORMAL_RESUME, "System", "resumed from sleep");

	return rv;
}
