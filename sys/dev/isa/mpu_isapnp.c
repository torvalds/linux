/*	$OpenBSD: mpu_isapnp.c,v 1.10 2022/04/06 18:59:28 naddy Exp $	*/

#include "midi.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/syslog.h>
#include <sys/device.h>

#include <machine/bus.h>

#include <sys/audioio.h>
#include <dev/audio_if.h>
#include <dev/midi_if.h>
#include <dev/mulaw.h>

#include <dev/isa/isavar.h>
#include <dev/isa/isadmavar.h>

#include <dev/ic/mpuvar.h>

int	mpu_isapnp_match(struct device *, void *, void *);
void	mpu_isapnp_attach(struct device *, struct device *, void *);

struct mpu_isapnp_softc {
	struct device sc_dev;
	void *sc_ih;

	struct mpu_softc sc_mpu;
};

const struct cfattach mpu_isapnp_ca = {
	sizeof(struct mpu_isapnp_softc), mpu_isapnp_match, mpu_isapnp_attach
};

int
mpu_isapnp_match(struct device *parent, void *match, void *aux)
{
	struct isa_attach_args *ipa = aux;

	if (ipa->ipa_nirq != 1)
		return 0;
	return 1;
}

void
mpu_isapnp_attach(struct device *parent, struct device *self, void *aux)
{
	struct mpu_isapnp_softc *sc = (struct mpu_isapnp_softc *)self;
	struct isa_attach_args *ipa = aux;

	printf("\n");

	sc->sc_mpu.iot = ipa->ia_iot;
	sc->sc_mpu.ioh = ipa->ipa_io[0].h;

	sc->sc_ih = isa_intr_establish(ipa->ia_ic, ipa->ipa_irq[0].num,
	    ipa->ipa_irq[0].type, IPL_AUDIO | IPL_MPSAFE,
	    mpu_intr, &sc->sc_mpu, sc->sc_dev.dv_xname);

	if (!mpu_find(&sc->sc_mpu)) {
		printf("%s: find failed\n", sc->sc_dev.dv_xname);
		return;
	}

	printf("%s: %s %s\n", sc->sc_dev.dv_xname, ipa->ipa_devident,
	       ipa->ipa_devclass);

	midi_attach_mi(&mpu_midi_hw_if, &sc->sc_mpu, &sc->sc_dev);
}
