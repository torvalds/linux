/*	$OpenBSD: nec86.c,v 1.9 2022/11/02 10:41:34 kn Exp $	*/
/*	$NecBSD: nec86.c,v 1.11 1999/07/23 11:04:39 honda Exp $	*/
/*	$NetBSD$	*/

/*
 * [NetBSD for NEC PC-98 series]
 *  Copyright (c) 1996, 1997, 1998
 *	NetBSD/pc98 porting staff. All rights reserved.
 * 
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
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
 */

/*
 * nec86.c
 *
 * NEC PC-9801-86 SoundBoard PCM driver for NetBSD/pc98.
 * Written by NAGAO Tadaaki, Feb 10, 1996.
 *
 * Modified by N. Honda, Mar 7, 1998
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/syslog.h>
#include <sys/device.h>
#include <sys/proc.h>

#include <machine/board.h>	/* PC_BASE */
#include <machine/bus.h>
#include <machine/cpu.h>

#include <sys/audioio.h>
#include <dev/audio_if.h>

#include <luna88k/cbus/nec86reg.h>
#include <luna88k/cbus/nec86hwvar.h>
#include <luna88k/cbus/nec86var.h>

#define NEC_SCR_SIDMASK		0xf0
#define NEC_SCR_MASK		0x0f
#define NEC_SCR_EXT_ENABLE	0x01

/*
 * Define our interface to the higher level audio driver.
 */

const struct audio_hw_if nec86_hw_if = {
	.open		= nec86hw_open,
	.close		= nec86hw_close,
	.set_params	= nec86hw_set_params,
	.round_blocksize	= nec86hw_round_blocksize,
	.commit_settings	= nec86hw_commit_settings,
	.init_output	= nec86hw_pdma_init_output,
	.init_input	= nec86hw_pdma_init_input,
	.start_output	= nec86hw_pdma_output,
	.start_input	= nec86hw_pdma_input,
	.halt_output	= nec86hw_halt_pdma,
	.halt_input	= nec86hw_halt_pdma,
	.set_port	= nec86hw_mixer_set_port,
	.get_port	= nec86hw_mixer_get_port,
	.query_devinfo	= nec86hw_mixer_query_devinfo,
};

/*
 * YAMAHA YM2608(OPNA) register read/write functions
 */
#define YM_INDEX	0
#define YM_DATA		2

void nec86_ym_read(struct nec86_softc *, u_int8_t, u_int8_t *);
void nec86_ym_write(struct nec86_softc *, u_int8_t, u_int8_t);

void
nec86_ym_read(struct nec86_softc *sc, u_int8_t index, u_int8_t *data) {
	bus_space_write_1(sc->sc_ym_iot,
	    sc->sc_ym_iobase + sc->sc_ym_ioh, YM_INDEX, index);
	delay(100);
	*data = bus_space_read_1(sc->sc_ym_iot,
	    sc->sc_ym_iobase + sc->sc_ym_ioh, YM_DATA);
	delay(100);
}

void
nec86_ym_write(struct nec86_softc *sc, u_int8_t index, u_int8_t data) {
	bus_space_write_1(sc->sc_ym_iot,
	    sc->sc_ym_iobase + sc->sc_ym_ioh, YM_INDEX, index);
	delay(100);
	bus_space_write_1(sc->sc_ym_iot,
	    sc->sc_ym_iobase + sc->sc_ym_ioh, YM_DATA, data);
	delay(100);
}

/*
 * Probe for NEC PC-9801-86 SoundBoard hardware.
 */
int
nec86_probesubr(bus_space_tag_t iot, bus_space_handle_t ioh,
    bus_space_handle_t n86ioh)
{
	u_int8_t data;

#ifdef	notyet
	if (nec86hw_probesubr(iot, ioh) != 0)
		return -1;
#endif	/* notyet */

	if (n86ioh == 0)
		return -1;

	data = bus_space_read_1(iot, n86ioh, NEC86_SOUND_ID);

	switch (data & NEC_SCR_SIDMASK) {
#if 0	/* XXX -  PC-9801-73 not yet supported. */
	case 0x20:
	case 0x30:
		break;
#endif
	case 0x40:
	case 0x50:
		break;
	default:	/* No supported board found. */
		return -1;
		/*NOTREACHED*/
	}

	return ((data & NEC_SCR_SIDMASK) >> 4) - 2;
}

/*
 * Attach hardware to driver, attach hardware driver to audio
 * pseudo-device driver.
 */
#define MODEL0_NAME	"PC-9801-73 soundboard"
#define MODEL1_NAME	"PC-9801-86 soundboard"

void
nec86_attachsubr(struct nec86_softc *sc)
{
	struct nec86hw_softc *ysc = &sc->sc_nec86hw;
	bus_space_tag_t iot = sc->sc_n86iot;
	bus_space_handle_t n86ioh = sc->sc_n86ioh;
	char *boardname[] =
	    {MODEL0_NAME, MODEL0_NAME, MODEL1_NAME, MODEL1_NAME};
	u_int8_t data;
	int model;

	if ((model = nec86_probesubr(iot, n86ioh, n86ioh)) < 0) {
		printf("%s: missing hardware\n", ysc->sc_dev.dv_xname);
		return;
	}
	ysc->model = model;

	/* enable YM2608(ONPA) */
	data = bus_space_read_1(iot, n86ioh, NEC86_SOUND_ID);
	data &= ~NEC_SCR_MASK;
	data |= NEC_SCR_EXT_ENABLE;
	bus_space_write_1(iot, n86ioh, NEC86_SOUND_ID, data);

	switch (ysc->model) {
	case 2:	/* base I/O port for YM2608(OPNA) is 0x188 */
		sc->sc_ym_ioh = OPNA_IOBASE1;
		break;
	case 3:	/* base I/O port for YM2608(OPNA) is 0x288 */
		sc->sc_ym_ioh = OPNA_IOBASE2;
		break;
	default:
		/* can not happen; set to factory default */
		sc->sc_ym_ioh = OPNA_IOBASE1;
		break;
	}

	/* YM2608 I/O port set (IOA:input IOB:output) */
	nec86_ym_read(sc, 0x07, &data);
	data &= 0x3f;
	data |= 0x80;
	nec86_ym_write(sc, 0x07, data);

	/* YM2608 register 0x0e has C-bus interrupt level information */
	nec86_ym_read(sc, 0x0e, &data);
	switch (data & 0xc0) {
	case 0x00:
		sc->sc_intlevel = 0;
		break;
	case 0x40:
		sc->sc_intlevel = 6;
		break;
	case 0x80:
		sc->sc_intlevel = 4;
		break;
	case 0xc0:
		sc->sc_intlevel = 5;	/* factory default setting */
		break;
	default:
		/* can not happen; set to factory default */
		sc->sc_intlevel = 5;
		break;
	}

	/* reset YM2608 timer A and B: XXX need this? */
	data = 0x30;
	nec86_ym_write(sc, 0x27, data);

	printf(" int %d", sc->sc_intlevel);

	nec86hw_attach(ysc);

	if (sc->sc_attached == 0) {
		printf(": %s\n", boardname[ysc->model]);
		audio_attach_mi(&nec86_hw_if, ysc, NULL, &ysc->sc_dev);
		sc->sc_attached = 1;
	}
}
