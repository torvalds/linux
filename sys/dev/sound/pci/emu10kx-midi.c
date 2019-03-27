/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Seigo Tanimura
 * Copyright (c) 2003 Mathew Kanner
 * Copyright (c) 2003-2006 Yuriy Tsibizov <yuriy.tsibizov@gfk.ru>
 * All rights reserved
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
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/bus.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <sys/systm.h>
#include <sys/sbuf.h>
#include <sys/queue.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/chip.h>
#include <dev/sound/pcm/sound.h>

#include <dev/sound/midi/midi.h>
#include <dev/sound/midi/mpu401.h>
#include "mpufoi_if.h"

#include <dev/sound/pci/emuxkireg.h>
#include <dev/sound/pci/emu10kx.h>

struct emu_midi_softc {
	struct mtx	mtx;
	device_t	dev;
	struct mpu401	*mpu;
	mpu401_intr_t	*mpu_intr;
	struct emu_sc_info *card;
	int		port;			/* I/O port or I/O ptr reg */
	int		is_emu10k1;
	int		fflags;			/* File flags */
	int		ihandle;		/* interrupt manager handle */
};

static uint32_t	emu_midi_card_intr(void *p, uint32_t arg);
static devclass_t emu_midi_devclass;

static unsigned char
emu_mread(struct mpu401 *arg __unused, void *cookie, int reg)
{
	struct emu_midi_softc *sc = cookie;
	unsigned int d;

	d = 0;
	if (sc->is_emu10k1)
		d = emu_rd(sc->card, 0x18 + reg, 1);
	else
		d = emu_rdptr(sc->card, 0, sc->port + reg);

	return (d);
}

static void
emu_mwrite(struct mpu401 *arg __unused, void *cookie, int reg, unsigned char b)
{
	struct emu_midi_softc *sc = cookie;

	if (sc->is_emu10k1)
		emu_wr(sc->card, 0x18 + reg, b, 1);
	else
		emu_wrptr(sc->card, 0, sc->port + reg, b);
}

static int
emu_muninit(struct mpu401 *arg __unused, void *cookie)
{
	struct emu_midi_softc *sc = cookie;

	mtx_lock(&sc->mtx);
	sc->mpu_intr = NULL;
	mtx_unlock(&sc->mtx);

	return (0);
}

static kobj_method_t emu_mpu_methods[] = {
	KOBJMETHOD(mpufoi_read, emu_mread),
	KOBJMETHOD(mpufoi_write, emu_mwrite),
	KOBJMETHOD(mpufoi_uninit, emu_muninit),
	KOBJMETHOD_END
};
static DEFINE_CLASS(emu_mpu, emu_mpu_methods, 0);

static uint32_t
emu_midi_card_intr(void *p, uint32_t intr_status)
{
	struct emu_midi_softc *sc = (struct emu_midi_softc *)p;
	if (sc->mpu_intr)
		(sc->mpu_intr) (sc->mpu);
	if (sc->mpu_intr == NULL) {
		/* We should read MIDI event to unlock card after
		 * interrupt. XXX - check, why this happens.  */
		if (bootverbose)
			device_printf(sc->dev, "midi interrupt %08x without interrupt handler, force mread!\n", intr_status);
		(void)emu_mread((void *)(NULL), sc, 0);
	}
	return (intr_status); /* Acknowledge everything */
}

static void
emu_midi_intr(void *p)
{
	(void)emu_midi_card_intr(p, 0);
}

static int
emu_midi_probe(device_t dev)
{
	struct emu_midi_softc *scp;
	uintptr_t func, r, is_emu10k1;

	r = BUS_READ_IVAR(device_get_parent(dev), dev, 0, &func);
	if (func != SCF_MIDI)
		return (ENXIO);

	scp = device_get_softc(dev);
	bzero(scp, sizeof(*scp));
	r = BUS_READ_IVAR(device_get_parent(dev), dev, EMU_VAR_ISEMU10K1, &is_emu10k1);
	scp->is_emu10k1 = is_emu10k1 ? 1 : 0;

	device_set_desc(dev, "EMU10Kx MIDI Interface");
	return (0);
}

static int
emu_midi_attach(device_t dev)
{
	struct emu_midi_softc * scp;
	struct sndcard_func *func;
	struct emu_midiinfo *midiinfo;
	uint32_t inte_val, ipr_val;

	scp = device_get_softc(dev);
	func = device_get_ivars(dev);

	scp->dev = dev;
	midiinfo = (struct emu_midiinfo *)func->varinfo;
	scp->port = midiinfo->port;
	scp->card = midiinfo->card;

	mtx_init(&scp->mtx, device_get_nameunit(dev), "midi softc", MTX_DEF);

	if (scp->is_emu10k1) {
		/* SB Live! - only one MIDI device here */
		inte_val = 0;
		/* inte_val |= EMU_INTE_MIDITXENABLE;*/
		inte_val |= EMU_INTE_MIDIRXENABLE;
		ipr_val = EMU_IPR_MIDITRANSBUFE;
		ipr_val |= EMU_IPR_MIDIRECVBUFE;
	} else {
		if (scp->port == EMU_A_MUDATA1) {
			/* EXTERNAL MIDI (AudigyDrive) */
			inte_val = 0;
			/* inte_val |= A_EMU_INTE_MIDITXENABLE1;*/
			inte_val |= EMU_INTE_MIDIRXENABLE;
			ipr_val = EMU_IPR_MIDITRANSBUFE;
			ipr_val |= EMU_IPR_MIDIRECVBUFE;
		} else {
			/* MIDI hw config port 2 */
			inte_val = 0;
			/* inte_val |= A_EMU_INTE_MIDITXENABLE2;*/
			inte_val |= EMU_INTE_A_MIDIRXENABLE2;
			ipr_val = EMU_IPR_A_MIDITRANSBUFE2;
			ipr_val |= EMU_IPR_A_MIDIRECBUFE2;
		}
	}

	scp->ihandle = emu_intr_register(scp->card, inte_val, ipr_val, &emu_midi_card_intr, scp);
	/* Init the interface. */
	scp->mpu = mpu401_init(&emu_mpu_class, scp, emu_midi_intr, &scp->mpu_intr);
	if (scp->mpu == NULL) {
		emu_intr_unregister(scp->card, scp->ihandle);
		mtx_destroy(&scp->mtx);
		return (ENOMEM);
	}
	/*
	 * XXX I don't know how to check for Live!Drive / AudigyDrive
	 * presence. Let's hope that IR enabling code will not harm if
	 * it is not present.
	 */
	if (scp->is_emu10k1)
		emu_enable_ir(scp->card);
	else {
		if (scp->port == EMU_A_MUDATA1)
			emu_enable_ir(scp->card);
	}

	return (0);
}


static int
emu_midi_detach(device_t dev)
{
	struct emu_midi_softc *scp;

	scp = device_get_softc(dev);
	mpu401_uninit(scp->mpu);
	emu_intr_unregister(scp->card, scp->ihandle);
	mtx_destroy(&scp->mtx);
	return (0);
}

static device_method_t emu_midi_methods[] = {
	DEVMETHOD(device_probe, emu_midi_probe),
	DEVMETHOD(device_attach, emu_midi_attach),
	DEVMETHOD(device_detach, emu_midi_detach),

	DEVMETHOD_END
};

static driver_t emu_midi_driver = {
	"midi",
	emu_midi_methods,
	sizeof(struct emu_midi_softc),
};
DRIVER_MODULE(snd_emu10kx_midi, emu10kx, emu_midi_driver, emu_midi_devclass, 0, 0);
MODULE_DEPEND(snd_emu10kx_midi, snd_emu10kx, SND_EMU10KX_MINVER, SND_EMU10KX_PREFVER, SND_EMU10KX_MAXVER);
MODULE_DEPEND(snd_emu10kx_midi, sound, SOUND_MINVER, SOUND_PREFVER, SOUND_MAXVER);
MODULE_VERSION(snd_emu10kx_midi, SND_EMU10KX_PREFVER);
