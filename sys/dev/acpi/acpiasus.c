/* $OpenBSD: acpiasus.c,v 1.20 2022/04/06 18:59:27 naddy Exp $ */
/* $NetBSD: asus_acpi.c,v 1.2.2.2 2008/04/03 12:42:37 mjf Exp $ */
/*
 * Copyright (c) 2007, 2008 Jared D. McNeill <jmcneill@invisible.ca>
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * ASUS ACPI hotkeys driver.
 */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/systm.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

#include "audio.h"
#include "wskbd.h"

struct acpiasus_softc {
	struct device		sc_dev;

	struct acpi_softc	*sc_acpi;
	struct aml_node		*sc_devnode;
};

#define ASUS_NOTIFY_WIRELESSON		0x10
#define ASUS_NOTIFY_WIRELESSOFF		0x11
#define ASUS_NOTIFY_TASKSWITCH		0x12
#define ASUS_NOTIFY_VOLUMEMUTE		0x13
#define ASUS_NOTIFY_VOLUMEDOWN		0x14
#define ASUS_NOTIFY_VOLUMEUP		0x15
#define ASUS_NOTIFY_LCDSWITCHOFF0	0x16
#define ASUS_NOTIFY_LCDSWITCHOFF1	0x1a
#define ASUS_NOTIFY_LCDCHANGERES	0x1b
#define ASUS_NOTIFY_USERDEF0		0x1c
#define ASUS_NOTIFY_USERDEF1		0x1d
#define ASUS_NOTIFY_BRIGHTNESSLOW	0x20
#define ASUS_NOTIFY_BRIGHTNESSHIGH	0x2f
#define ASUS_NOTIFY_DISPLAYCYCLEDOWN	0x30
#define ASUS_NOTIFY_DISPLAYCYCLEUP	0x32

#define ASUS_NOTIFY_POWERCONNECT	0x50
#define ASUS_NOTIFY_POWERDISCONNECT	0x51

#define	ASUS_SDSP_LCD			0x01
#define	ASUS_SDSP_CRT			0x02
#define	ASUS_SDSP_TV			0x04
#define	ASUS_SDSP_DVI			0x08
#define	ASUS_SDSP_ALL \
	(ASUS_SDSP_LCD | ASUS_SDSP_CRT | ASUS_SDSP_TV | ASUS_SDSP_DVI)

int	acpiasus_match(struct device *, void *, void *);
void	acpiasus_attach(struct device *, struct device *, void *);
void	acpiasus_init(struct device *);
int	acpiasus_notify(struct aml_node *, int, void *);
int	acpiasus_activate(struct device *, int);

#if NAUDIO > 0 && NWSKBD > 0
extern int wskbd_set_mixervolume(long, long);
#endif

const struct cfattach acpiasus_ca = {
	sizeof(struct acpiasus_softc), acpiasus_match, acpiasus_attach,
	NULL, acpiasus_activate
};

struct cfdriver acpiasus_cd = {
	NULL, "acpiasus", DV_DULL
};

const char *acpiasus_hids[] = {
	"ASUS010",
	NULL
};

int
acpiasus_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aa = aux;
	struct cfdata *cf = match;

	return (acpi_matchhids(aa, acpiasus_hids, cf->cf_driver->cd_name));
}

void
acpiasus_attach(struct device *parent, struct device *self, void *aux)
{
	struct acpiasus_softc *sc = (struct acpiasus_softc *)self;
	struct acpi_attach_args *aa = aux;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_devnode = aa->aaa_node;

	printf("\n");

	acpiasus_init(self);

	aml_register_notify(sc->sc_devnode, aa->aaa_dev,
	    acpiasus_notify, sc, ACPIDEV_NOPOLL);
}

void
acpiasus_init(struct device *self)
{
	struct acpiasus_softc *sc = (struct acpiasus_softc *)self;
	struct aml_value cmd;
	struct aml_value ret;

	bzero(&cmd, sizeof(cmd));
	cmd.type = AML_OBJTYPE_INTEGER;
	cmd.v_integer = 0x40;		/* Disable ASL display switching. */

	if (aml_evalname(sc->sc_acpi, sc->sc_devnode, "INIT", 1, &cmd, &ret))
		printf("%s: no INIT\n", DEVNAME(sc));
	else
		aml_freevalue(&ret);
}

int
acpiasus_notify(struct aml_node *node, int notify, void *arg)
{
	struct acpiasus_softc *sc = arg;

	if (notify >= ASUS_NOTIFY_BRIGHTNESSLOW &&
	    notify <= ASUS_NOTIFY_BRIGHTNESSHIGH) {
#ifdef ACPIASUS_DEBUG
		printf("%s: brightness %d percent\n", DEVNAME(sc),
		    (notify & 0xf) * 100 / 0xf);
#endif
		return 0;
	}

	switch (notify) {
	case ASUS_NOTIFY_WIRELESSON:	/* Handled by AML. */
	case ASUS_NOTIFY_WIRELESSOFF:	/* Handled by AML. */
		break;
	case ASUS_NOTIFY_TASKSWITCH:
		break;
	case ASUS_NOTIFY_DISPLAYCYCLEDOWN:
	case ASUS_NOTIFY_DISPLAYCYCLEUP:
		break;
#if NAUDIO > 0 && NWSKBD > 0
	case ASUS_NOTIFY_VOLUMEMUTE:
		wskbd_set_mixervolume(0, 1);
		break;
	case ASUS_NOTIFY_VOLUMEDOWN:
		wskbd_set_mixervolume(-1, 1);
		break;
	case ASUS_NOTIFY_VOLUMEUP:
		wskbd_set_mixervolume(1, 1);
		break;
#else
	case ASUS_NOTIFY_VOLUMEMUTE:
	case ASUS_NOTIFY_VOLUMEDOWN:
	case ASUS_NOTIFY_VOLUMEUP:
		break;
#endif
	case ASUS_NOTIFY_POWERCONNECT:
	case ASUS_NOTIFY_POWERDISCONNECT:
		break;

	case ASUS_NOTIFY_LCDSWITCHOFF0:
	case ASUS_NOTIFY_LCDSWITCHOFF1:
		break;

	case ASUS_NOTIFY_LCDCHANGERES:
		break;

	case ASUS_NOTIFY_USERDEF0:
	case ASUS_NOTIFY_USERDEF1:
		break;

	default:
		printf("%s: unknown event 0x%02x\n", DEVNAME(sc), notify);
		break;
	}

	return 0;
}

int
acpiasus_activate(struct device *self, int act)
{
	struct acpiasus_softc *sc = (struct acpiasus_softc *)self;
	struct aml_value cmd;
	struct aml_value ret;

	switch (act) {
	case DVACT_WAKEUP:
		acpiasus_init(self);

		bzero(&cmd, sizeof(cmd));
		cmd.type = AML_OBJTYPE_INTEGER;
		cmd.v_integer = ASUS_SDSP_LCD;

		if (aml_evalname(sc->sc_acpi, sc->sc_devnode, "SDSP", 1,
		    &cmd, &ret))
			printf("%s: no SDSP\n", DEVNAME(sc));
		else
			aml_freevalue(&ret);
		break;
	}
	return (0);
}
