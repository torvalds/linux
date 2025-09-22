/* $OpenBSD: acpi_apm.c,v 1.4 2024/10/30 06:16:27 jsg Exp $ */
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
#include <sys/fcntl.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/dsdt.h>

#include <machine/conf.h>
#include <machine/cpufunc.h>

#ifdef HIBERNATE
#include <sys/hibernate.h>
#endif

#include <machine/apmvar.h>
#define APMUNIT(dev)	(minor(dev)&0xf0)
#define APMDEV(dev)	(minor(dev)&0x0f)
#define APMDEV_NORMAL	0
#define APMDEV_CTL	8

#ifndef SMALL_KERNEL

int
acpiopen(dev_t dev, int flag, int mode, struct proc *p)
{
	int error = 0;
	struct acpi_softc *sc = acpi_softc;
	int s;

	if (sc == NULL)
		return (ENXIO);

	s = splbio();
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
	splx(s);
	return (error);
}

int
acpiclose(dev_t dev, int flag, int mode, struct proc *p)
{
	int error = 0;
	struct acpi_softc *sc = acpi_softc;
	int s;

	if (sc == NULL)
		return (ENXIO);

	s = splbio();
	switch (APMDEV(dev)) {
	case APMDEV_CTL:
		sc->sc_flags &= ~SCFLAG_OWRITE;
		break;
	case APMDEV_NORMAL:
		sc->sc_flags &= ~SCFLAG_OREAD;
		break;
	default:
		error = ENXIO;
		break;
	}
	splx(s);
	return (error);
}

int
acpiioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	int error = 0;
	struct acpi_softc *sc = acpi_softc;
	struct apm_power_info *pi = (struct apm_power_info *)data;
	int s;

	if (sc == NULL)
		return (ENXIO);

	s = splbio();
	/* fake APM */
	switch (cmd) {
#ifdef SUSPEND
	case APM_IOC_SUSPEND:
	case APM_IOC_STANDBY:
		if ((flag & FWRITE) == 0) {
			error = EBADF;
			break;
		}
		error = request_sleep(SLEEP_SUSPEND);
		if (error)
			break;
		acpi_wakeup(sc);
		break;
#ifdef HIBERNATE
	case APM_IOC_HIBERNATE:
		if ((error = suser(p)) != 0)
			break;
		if ((flag & FWRITE) == 0) {
			error = EBADF;
			break;
		}
		if (get_hibernate_io_function(swdevt[0]) == NULL) {
			error = EOPNOTSUPP;
			break;
		}
		error = request_sleep(SLEEP_HIBERNATE);
		if (error)
			break;
		acpi_wakeup(sc);
		break;
#endif
#endif
	case APM_IOC_GETPOWER:
		error = acpi_apminfo(pi);
		break;

	default:
		error = ENOTTY;
	}

	splx(s);
	return (error);
}

void	acpi_filtdetach(struct knote *);
int	acpi_filtread(struct knote *, long);

const struct filterops acpiread_filtops = {
	.f_flags	= FILTEROP_ISFD,
	.f_attach	= NULL,
	.f_detach	= acpi_filtdetach,
	.f_event	= acpi_filtread,
};

int
acpikqfilter(dev_t dev, struct knote *kn)
{
	struct acpi_softc *sc = acpi_softc;
	int s;

	if (sc == NULL)
		return (ENXIO);

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &acpiread_filtops;
		break;
	default:
		return (EINVAL);
	}

	kn->kn_hook = sc;

	s = splbio();
	klist_insert_locked(&sc->sc_note, kn);
	splx(s);

	return (0);
}

void
acpi_filtdetach(struct knote *kn)
{
	struct acpi_softc *sc = kn->kn_hook;
	int s;

	s = splbio();
	klist_remove_locked(&sc->sc_note, kn);
	splx(s);
}

int
acpi_filtread(struct knote *kn, long hint)
{
	/* XXX weird kqueue_scan() semantics */
	if (hint && !kn->kn_data)
		kn->kn_data = hint;
	return (1);
}

#ifdef SUSPEND
int
request_sleep(int sleepmode)
{
	struct acpi_softc *sc = acpi_softc;

#ifdef HIBERNATE
	if (sleepmode == SLEEP_HIBERNATE) {
		if (get_hibernate_io_function(swdevt[0]) == NULL)
			return EOPNOTSUPP;
	}
#endif
	acpi_addtask(sc, acpi_sleep_task, sc, sleepmode);
	return 0;
}
#endif /* SUSPEND */

#else /* SMALL_KERNEL */

int
acpiopen(dev_t dev, int flag, int mode, struct proc *p)
{
	return (ENXIO);
}

int
acpiclose(dev_t dev, int flag, int mode, struct proc *p)
{
	return (ENXIO);
}

int
acpiioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	return (ENXIO);
}

int
acpikqfilter(dev_t dev, struct knote *kn)
{
	return (EOPNOTSUPP);
}

#endif /* SMALL_KERNEL */
