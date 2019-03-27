/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD

 * Copyright (c) 2015-2018 Tai-hwa Liang <avatar@FreeBSD.org>
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
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/bus.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <sys/systm.h>
#include <sys/kobj.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/sound/chip.h>
#include <dev/sound/pcm/sound.h>

#include <dev/sound/midi/midi.h>
#include <dev/sound/midi/mpu401.h>

#include <dev/sound/pci/csareg.h>
#include <dev/sound/pci/csavar.h>

#include "mpufoi_if.h"

SND_DECLARE_FILE("$FreeBSD$");

/* pulled from mpu401.c */
#define	MPU_DATAPORT	0
#define	MPU_CMDPORT	1
#define	MPU_STATPORT	1
#define	MPU_RESET	0xff
#define	MPU_UART	0x3f
#define	MPU_ACK		0xfe
#define	MPU_STATMASK	0xc0
#define	MPU_OUTPUTBUSY	0x40
#define	MPU_INPUTBUSY	0x80

/* device private data */
struct csa_midi_softc {
	/* hardware resources */
	int		io_rid;		/* io rid */
	struct resource	*io;		/* io */

	struct mtx	mtx;
	device_t	dev;
	struct mpu401	*mpu;
	mpu401_intr_t	*mpu_intr;
	int		mflags;		/* MIDI flags */
};

static struct kobj_class csamidi_mpu_class;
static devclass_t midicsa_devclass;

static u_int32_t
csamidi_readio(struct csa_midi_softc *scp, u_long offset)
{
	if (offset < BA0_AC97_RESET)
		return bus_space_read_4(rman_get_bustag(scp->io), rman_get_bushandle(scp->io), offset) & 0xffffffff;
	else
		return (0);
}

static void
csamidi_writeio(struct csa_midi_softc *scp, u_long offset, u_int32_t data)
{
	if (offset < BA0_AC97_RESET)
		bus_space_write_4(rman_get_bustag(scp->io), rman_get_bushandle(scp->io), offset, data);
}

static void
csamidi_midi_intr(void *arg)
{
	struct csa_midi_softc *scp = (struct csa_midi_softc *)arg;

	if (scp->mpu_intr)
		(scp->mpu_intr)(scp->mpu);
}

static unsigned char
csamidi_mread(struct mpu401 *arg __unused, void *cookie, int reg)
{
	struct csa_midi_softc *scp = cookie;
	unsigned int rc;
	unsigned int uart_stat;

	rc = 0;
	/* hacks to convert hardware status to MPU compatible ones */
	switch (reg) {
	case MPU_STATPORT:
		uart_stat = csamidi_readio(scp, BA0_MIDSR);
		if (uart_stat & MIDSR_TBF)
			rc |= MPU_OUTPUTBUSY;	/* Tx buffer full */
		if (uart_stat & MIDSR_RBE)
			rc |= MPU_INPUTBUSY;
		break;
	case MPU_DATAPORT:
		rc = csamidi_readio(scp, BA0_MIDRP);
		break;
	default:
		printf("csamidi_mread: unknown register %d\n", reg);
		break;
	}
	return (rc);
}

static void
csamidi_mwrite(struct mpu401 *arg __unused, void *cookie, int reg, unsigned char b)
{
	struct csa_midi_softc *scp = cookie;
	unsigned int val;

	switch (reg) {
	case MPU_CMDPORT:
		switch (b)
		{
		case MPU_RESET:
			/* preserve current operation mode */
			val = csamidi_readio(scp, BA0_MIDCR);
			/* reset the MIDI port */
			csamidi_writeio(scp, BA0_MIDCR, MIDCR_MRST);
			csamidi_writeio(scp, BA0_MIDCR, MIDCR_MLB);
			csamidi_writeio(scp, BA0_MIDCR, 0x00);
			/* restore previous operation mode */
			csamidi_writeio(scp, BA0_MIDCR, val);
			break;
		case MPU_UART:
			/* switch to UART mode, no-op */
		default:
			break;
		}
		break;
	case MPU_DATAPORT:
		/* put the MIDI databyte in the write port */
		csamidi_writeio(scp, BA0_MIDWP, b);
		break;
	default:
		printf("csamidi_mwrite: unknown register %d\n", reg);
		break;
	}
}

static int
csamidi_muninit(struct mpu401 *arg __unused, void *cookie)
{
	struct csa_midi_softc *scp = cookie;

	mtx_lock(&scp->mtx);
	scp->mpu_intr = NULL;
	mtx_unlock(&scp->mtx);

	return (0);
}

static int
midicsa_probe(device_t dev)
{
	struct sndcard_func *func;

	/* The parent device has already been probed. */

	func = device_get_ivars(dev);
	if (func == NULL || func->func != SCF_MIDI)
		return (ENXIO);

	device_set_desc(dev, "CS461x MIDI");
	return (0);
}

static int
midicsa_attach(device_t dev)
{
	struct csa_midi_softc *scp;
	struct sndcard_func *func;
	int rc = ENXIO;

	scp = device_get_softc(dev);
	func = device_get_ivars(dev);

	bzero(scp, sizeof(struct csa_midi_softc));
	scp->dev = dev;

	/* allocate the required resources */
	scp->io_rid = PCIR_BAR(0);
	scp->io = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &scp->io_rid, RF_ACTIVE);
	if (scp->io == NULL)
		goto err0;

	/* init the fake MPU401 interface. */
	scp->mpu = mpu401_init(&csamidi_mpu_class, scp, csamidi_midi_intr,
	    &scp->mpu_intr);
	if (scp->mpu == NULL) {
		rc = ENOMEM;
		goto err1;
	}

	mtx_init(&scp->mtx, device_get_nameunit(dev), "csamidi softc",
	    MTX_DEF);

	/* reset the MIDI port */
	csamidi_writeio(scp, BA0_MIDCR, MIDCR_MRST);
	/* MIDI transmit enable, no interrupt */
	csamidi_writeio(scp, BA0_MIDCR, MIDCR_TXE | MIDCR_RXE);
	csamidi_writeio(scp, BA0_HICR, HICR_IEV | HICR_CHGM);

	return (0);
err1:
	bus_release_resource(dev, SYS_RES_MEMORY, scp->io_rid, scp->io);
	scp->io = NULL;
err0:
	return (rc);
}

static int
midicsa_detach(device_t dev)
{
	struct csa_midi_softc *scp;
	int rc = 0;

	scp = device_get_softc(dev);
	rc = mpu401_uninit(scp->mpu);
	if (rc)
		return (rc);
	if (scp->io != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, scp->io_rid,
		    scp->io);
		scp->io = NULL;
	}
	mtx_destroy(&scp->mtx);
	return (rc);
}

static kobj_method_t csamidi_mpu_methods[] = {
	KOBJMETHOD(mpufoi_read, csamidi_mread),
	KOBJMETHOD(mpufoi_write, csamidi_mwrite),
	KOBJMETHOD(mpufoi_uninit, csamidi_muninit),
	KOBJMETHOD_END
};

static DEFINE_CLASS(csamidi_mpu, csamidi_mpu_methods, 0);

static device_method_t midicsa_methods[] = {
	DEVMETHOD(device_probe, midicsa_probe),
	DEVMETHOD(device_attach, midicsa_attach),
	DEVMETHOD(device_detach, midicsa_detach),

	DEVMETHOD_END
};

static driver_t midicsa_driver = {
	"midi",
	midicsa_methods,
	sizeof(struct csa_midi_softc),
};
DRIVER_MODULE(snd_csa_midi, csa, midicsa_driver, midicsa_devclass, 0, 0);
MODULE_DEPEND(snd_csa_midi, snd_csa, 1, 1, 1);
MODULE_DEPEND(snd_csa_midi, sound, SOUND_MINVER, SOUND_PREFVER, SOUND_MAXVER);
MODULE_VERSION(snd_csa_midi, 1);
