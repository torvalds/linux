/*	$OpenBSD: hilid.c,v 1.5 2022/04/06 18:59:28 naddy Exp $	*/
/*
 * Copyright (c) 2003, Miodrag Vallat.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/ioctl.h>

#include <machine/autoconf.h>
#include <machine/bus.h>
#include <machine/cpu.h>

#include <dev/hil/hilreg.h>
#include <dev/hil/hilvar.h>
#include <dev/hil/hildevs.h>

struct hilid_softc {
	struct hildev_softc sc_hildev;

	u_int8_t	sc_id[16];
};

int	hilidprobe(struct device *, void *, void *);
void	hilidattach(struct device *, struct device *, void *);
int	hiliddetach(struct device *, int);

struct cfdriver hilid_cd = {
	NULL, "hilid", DV_DULL
};

const struct cfattach hilid_ca = {
	sizeof(struct hilid_softc), hilidprobe, hilidattach, hiliddetach,
};

int
hilidprobe(struct device *parent, void *match, void *aux)
{
	struct hil_attach_args *ha = aux;

	if (ha->ha_type != HIL_DEVICE_IDMODULE)
		return (0);

	return (1);
}

void
hilidattach(struct device *parent, struct device *self, void *aux)
{
	struct hilid_softc *sc = (void *)self;
	struct hil_attach_args *ha = aux;
	u_int i, len;

	sc->hd_code = ha->ha_code;
	sc->hd_type = ha->ha_type;
	sc->hd_infolen = ha->ha_infolen;
	bcopy(ha->ha_info, sc->hd_info, ha->ha_infolen);
	sc->hd_fn = NULL;

	printf("\n");

	bzero(sc->sc_id, sizeof(sc->sc_id));
	len = sizeof(sc->sc_id);
	printf("%s: security code", self->dv_xname);

	if (send_hildev_cmd((struct hildev_softc *)sc,
	    HIL_SECURITY, sc->sc_id, &len) == 0) {
		for (i = 0; i < sizeof(sc->sc_id); i++)
			printf(" %02x", sc->sc_id[i]);
		printf("\n");
	} else
		printf(" unavailable\n");
}

int
hiliddetach(struct device *self, int flags)
{
	return (0);
}
