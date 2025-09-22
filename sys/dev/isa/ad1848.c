/*	$OpenBSD: ad1848.c,v 1.50 2024/05/28 09:27:08 jsg Exp $	*/
/*	$NetBSD: ad1848.c,v 1.45 1998/01/30 02:02:38 augustss Exp $	*/

/*
 * Copyright (c) 1994 John Brezak
 * Copyright (c) 1991-1993 Regents of the University of California.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the Computer Systems
 *	Engineering Group at Lawrence Berkeley Laboratory.
 * 4. Neither the name of the University nor of the Laboratory may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/*
 * Copyright by Hannu Savolainen 1994
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer. 2.
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */
/*
 * Portions of this code are from the VOXware support for the ad1848
 * by Hannu Savolainen <hannu@voxware.pp.fi>
 * 
 * Portions also supplied from the SoundBlaster driver for NetBSD.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/syslog.h>
#include <sys/device.h>
#include <sys/buf.h>
#include <sys/fcntl.h>

#include <machine/cpu.h>
#include <machine/bus.h>

#include <sys/audioio.h>

#include <dev/audio_if.h>

#include <dev/isa/isavar.h>
#include <dev/isa/isadmavar.h>

#include <dev/ic/ad1848reg.h>
#include <dev/ic/cs4231reg.h>
#include <dev/isa/ad1848var.h>

#ifdef AUDIO_DEBUG
#define DPRINTF(x)	do { if (ad1848debug) printf x; } while (0);
int	ad1848debug = 0;
#else
#define DPRINTF(x)
#endif

/*
 * Initial values for the indirect registers of CS4248/AD1848.
 */
static int ad1848_init_values[] = {
	GAIN_12 | INPUT_MIC_GAIN_ENABLE,	/* Left Input Control */
	GAIN_12 | INPUT_MIC_GAIN_ENABLE,	/* Right Input Control */
	ATTEN_12,			/* Left Aux #1 Input Control */
	ATTEN_12,			/* Right Aux #1 Input Control */
	ATTEN_12,			/* Left Aux #2 Input Control */
	ATTEN_12,			/* Right Aux #2 Input Control */
	/* bits 5-0 are attenuation select */
	ATTEN_12,			/* Left DAC output Control */
	ATTEN_12,			/* Right DAC output Control */
	CLOCK_XTAL1 | FMT_PCM8,		/* Clock and Data Format */
	SINGLE_DMA | AUTO_CAL_ENABLE,	/* Interface Config */
	INTERRUPT_ENABLE,		/* Pin control */
	0x00,				/* Test and Init */
	MODE2,				/* Misc control */
	ATTEN_0 << 2,			/* Digital Mix Control */
	0,				/* Upper base Count */
	0,				/* Lower base Count */

	/* These are for CS4231 &c. only (additional registers): */
	0,				/* Alt feature 1 */
	0,				/* Alt feature 2 */
	ATTEN_12,			/* Left line in */
	ATTEN_12,			/* Right line in */
	0,				/* Timer low */
	0,				/* Timer high */
	0,				/* unused */
	0,				/* unused */
	0,				/* IRQ status */
	0,				/* unused */

	/* Mono input (a.k.a speaker) (mic) Control */
	MONO_INPUT_MUTE|ATTEN_6,	/* mute speaker by default */
	0,				/* unused */
	0,				/* record format */
	0,				/* Crystal Clock Select */
	0,				/* upper record count */
	0				/* lower record count */
};

static struct audio_params ad1848_audio_default =
	{48000, AUDIO_ENCODING_SLINEAR_LE, 16, 2, 1, 2};

void	ad1848_reset(struct ad1848_softc *);
int	ad1848_set_speed(struct ad1848_softc *, u_long *);
void	ad1848_mute_monitor(void *, int);

/* indirect register access */
static int ad_read(struct ad1848_softc *, int);
static void ad_write(struct ad1848_softc *, int, int);
static void ad_set_MCE(struct ad1848_softc *, int);
static void wait_for_calibration(struct ad1848_softc *);

/* direct register (AD1848_{IADDR,IDATA,STATUS} only) access */
#define ADREAD(sc, addr) bus_space_read_1((sc)->sc_iot, (sc)->sc_ioh, (sc)->sc_iooffs+(addr))
#define ADWRITE(sc, addr, data) bus_space_write_1((sc)->sc_iot, (sc)->sc_ioh, (sc)->sc_iooffs+(addr), (data))

static int
ad_read(struct ad1848_softc *sc, int reg)
{
	int x;

	ADWRITE(sc, AD1848_IADDR, (reg & 0xff) | sc->MCE_bit);
	x = ADREAD(sc, AD1848_IDATA);
	/*  printf("(%02x<-%02x) ", reg|sc->MCE_bit, x); */

	return x;
}

static void
ad_write(struct ad1848_softc *sc, int reg, int data)
{
	ADWRITE(sc, AD1848_IADDR, (reg & 0xff) | sc->MCE_bit);
	ADWRITE(sc, AD1848_IDATA, data & 0xff);
	/* printf("(%02x->%02x) ", reg|sc->MCE_bit, data); */
}

static void
ad_set_MCE(struct ad1848_softc *sc, int state)
{
	if (state)
		sc->MCE_bit = MODE_CHANGE_ENABLE;
	else
		sc->MCE_bit = 0;

	ADWRITE(sc, AD1848_IADDR, sc->MCE_bit);
}

static void
wait_for_calibration(struct ad1848_softc *sc)
{
	int timeout;

	DPRINTF(("ad1848: Auto calibration started.\n"));
	/*
	 * Wait until the auto calibration process has finished.
	 *
	 * 1) Wait until the chip becomes ready (reads don't return SP_IN_INIT).
	 * 2) Wait until the ACI bit of I11 goes hi and then lo.
	 *   a) With AD1848 alike, ACI goes hi within 5 sample cycles
	 *	  and remains hi for ~384 sample periods.
	 *   b) With CS4231 alike, ACI goes hi immediately and remains
	 *	  hi for at least 168 sample periods.
	 */
	timeout = AD1848_TIMO;
	while (timeout > 0 && ADREAD(sc, AD1848_IADDR) == SP_IN_INIT)
		timeout--;

	if (ADREAD(sc, AD1848_IADDR) == SP_IN_INIT)
		DPRINTF(("ad1848: Auto calibration timed out(1).\n"));

	if (!(sc->sc_flags & AD1848_FLAG_32REGS)) {
		timeout = AD1848_TIMO;
		while (timeout > 0 &&
	 	    !(ad_read(sc, SP_TEST_AND_INIT) & AUTO_CAL_IN_PROG))
			timeout--;

		if (!(ad_read(sc, SP_TEST_AND_INIT) & AUTO_CAL_IN_PROG)) {
			DPRINTF(("ad1848: Auto calibration timed out(2).\n"));
		}
	}

	timeout = AD1848_TIMO;
	while (timeout > 0 && ad_read(sc, SP_TEST_AND_INIT) & AUTO_CAL_IN_PROG)
		timeout--;
	if (ad_read(sc, SP_TEST_AND_INIT) & AUTO_CAL_IN_PROG)
		DPRINTF(("ad1848: Auto calibration timed out(3).\n"));
}

#ifdef AUDIO_DEBUG
void ad1848_dump_regs(struct ad1848_softc *);

void
ad1848_dump_regs(struct ad1848_softc *sc)
{
	int i;
	u_char r;
	
	printf("ad1848 status=%02x", ADREAD(sc, AD1848_STATUS));
	printf(" regs: ");
	for (i = 0; i < 16; i++) {
		r = ad_read(sc, i);
		printf("%02x ", r);
	}
	if (sc->mode == 2) {
		for (i = 16; i < 32; i++) {
			r = ad_read(sc, i);
			printf("%02x ", r);
		}
	}
	printf("\n");
}
#endif

/*
 * Map and probe for the ad1848 chip
 */
int
ad1848_mapprobe(struct ad1848_softc *sc, int iobase)
{
	if (!AD1848_BASE_VALID(iobase)) {
#ifdef AUDIO_DEBUG
		printf("ad1848: configured iobase %04x invalid\n", iobase);
#endif
		return 0;
	}

	sc->sc_iooffs = 0;
	/* Map the AD1848 ports */
	if (bus_space_map(sc->sc_iot, iobase, AD1848_NPORT, 0, &sc->sc_ioh))
		return 0;

	if (!ad1848_probe(sc)) {
		bus_space_unmap(sc->sc_iot, sc->sc_ioh, AD1848_NPORT);
		return 0;
	} else
		return 1;
}

/*
 * Probe for the ad1848 chip
 */
int
ad1848_probe(struct ad1848_softc *sc)
{
	u_char tmp, tmp1 = 0xff, tmp2 = 0xff;
#if 0
	int i;
#endif

	/* Is there an ad1848 chip ? */
	sc->MCE_bit = MODE_CHANGE_ENABLE;
	sc->mode = 1;	/* MODE 1 = original ad1848/ad1846/cs4248 */
	sc->sc_flags = 0;

	/*
	 * Check that the I/O address is in use.
	 *
	 * The SP_IN_INIT bit of the base I/O port is known to be 0 after the
	 * chip has performed its power-on initialization. Just assume
	 * this has happened before the OS is starting.
	 *
	 * If the I/O address is unused, inb() typically returns 0xff.
	 */
	tmp = ADREAD(sc, AD1848_IADDR);
	if (tmp & SP_IN_INIT) { /* Not a AD1848 */
#if 0
		DPRINTF(("ad_detect_A %x\n", tmp));
#endif
		goto bad;
	}

	/*
	 * Test if it's possible to change contents of the indirect registers.
	 * Registers 0 and 1 are ADC volume registers. The bit 0x10 is read
	 * only so try to avoid using it.
	 */
	ad_write(sc, 0, 0xaa);
	ad_write(sc, 1, 0x45);	/* 0x55 with bit 0x10 clear */

	if ((tmp1 = ad_read(sc, 0)) != 0xaa ||
	    (tmp2 = ad_read(sc, 1)) != 0x45) {
		DPRINTF(("ad_detect_B (%x/%x)\n", tmp1, tmp2));
		goto bad;
	}

	ad_write(sc, 0, 0x45);
	ad_write(sc, 1, 0xaa);

	if ((tmp1 = ad_read(sc, 0)) != 0x45 ||
	    (tmp2 = ad_read(sc, 1)) != 0xaa) {
		DPRINTF(("ad_detect_C (%x/%x)\n", tmp1, tmp2));
		goto bad;
	}

	/*
	 * The indirect register I12 has some read only bits. Lets
	 * try to change them.
	 */
	tmp = ad_read(sc, SP_MISC_INFO);
	ad_write(sc, SP_MISC_INFO, (~tmp) & 0x0f);

	if ((tmp & 0x0f) != ((tmp1 = ad_read(sc, SP_MISC_INFO)) & 0x0f)) {
		DPRINTF(("ad_detect_D (%x)\n", tmp1));
		goto bad;
	}

	/*
	 * MSB and 4 LSBs of the reg I12 tell the chip revision.
	 *
	 * A preliminary version of the AD1846 data sheet stated that it
	 * used an ID field of 0x0B.  The current version, however,
	 * states that the AD1846 uses ID 0x0A, just like the AD1848K.
	 *
	 * this switch statement will need updating as newer clones arrive....
	 */
	switch (tmp1 & 0x8f) {
	case 0x09:
		sc->chip_name = "AD1848J";
		break;
	case 0x0A:
		sc->chip_name = "AD1848K";
		break;
#if 0	/* See above */
	case 0x0B:
		sc->chip_name = "AD1846";
		break;
#endif
	case 0x81:
		sc->chip_name = "CS4248revB"; /* or CS4231 rev B; see below */
		break;
	case 0x89:
		sc->chip_name = "CS4248";
		break;
	case 0x8A:
		sc->chip_name = "broken"; /* CS4231/AD1845; see below */
		break;
	default:
		sc->chip_name = "unknown";
		DPRINTF(("ad1848: unknown codec version %#02X\n", (tmp1 & 0x8f)));
	}	

#if 0
	/*
	 * XXX I don't know why, but this probe fails on an otherwise
	 * well-working AW35/pro card, so I'll just take it out for now.
	 * [niklas@openbsd.org]
	 */

	/*
	 * The original AD1848/CS4248 has just 16 indirect registers. This
	 * means that I0 and I16 should return the same value (etc.).
	 * Ensure that the Mode2 enable bit of I12 is 0. Otherwise this test
	 * fails with CS4231, AD1845, etc.
	 */
	ad_write(sc, SP_MISC_INFO, 0);	/* Mode2 = disabled */

	for (i = 0; i < 16; i++) {
		if ((tmp1 = ad_read(sc, i)) != (tmp2 = ad_read(sc, i + 16))) {
			if (i != SP_TEST_AND_INIT) {
				DPRINTF(("ad_detect_F(%d/%x/%x)\n", i, tmp1, tmp2));
				goto bad;
			}
		}
	}
#endif

	/*
	 * Try to switch the chip to mode2 (CS4231) by setting the MODE2 bit
	 * The bit 0x80 is always 1 in CS4248, CS4231, and AD1845.
	 */
	ad_write(sc, SP_MISC_INFO, MODE2);	/* Set mode2, clear 0x80 */

	tmp1 = ad_read(sc, SP_MISC_INFO);
	if ((tmp1 & 0xc0) == (0x80 | MODE2)) {
		/*
		 *	CS4231 or AD1845 detected - is it?
		 *
		 *	Verify that setting I2 doesn't change I18.
		 */
		ad_write(sc, 18, 0x88); /* Set I18 to known value */

		ad_write(sc, 2, 0x45);
		if ((tmp2 = ad_read(sc, 18)) != 0x45) {
			/* No change -> CS4231? */
			ad_write(sc, 2, 0xaa);
			if ((tmp2 = ad_read(sc, 18)) == 0xaa) {
				/* Rotten bits? */
				DPRINTF(("ad_detect_H(%x)\n", tmp2));
				goto bad;
			}

			/*
			 *  It's a CS4231, or another clone with 32 registers.
			 *  Let's find out which by checking I25.
			 */
			if ((tmp1 & 0x8f) == 0x8a) {
				tmp1 = ad_read(sc, CS_VERSION_ID);
				switch (tmp1 & 0xe7) {
				case 0xA0:
					sc->chip_name = "CS4231A";
					break;
				case 0x80:
					/* I25 no good, AD1845 same as CS4231 */
					sc->chip_name = "CS4231 or AD1845";
					break;
				case 0x82:
					sc->chip_name = "CS4232";
					break;
				case 0xa2:
					sc->chip_name = "CS4232C";
					break;
				case 0x03:
					sc->chip_name = "CS4236/CS4236B";
					break;
				}
			}
			sc->mode = 2;
			sc->sc_flags |= AD1848_FLAG_32REGS;
		}
	}

	/* Wait for 1848 to init */
	while(ADREAD(sc, AD1848_IADDR) & SP_IN_INIT)
		;

	/* Wait for 1848 to autocal */
	ADWRITE(sc, AD1848_IADDR, SP_TEST_AND_INIT);
	while(ADREAD(sc, AD1848_IDATA) & AUTO_CAL_IN_PROG)
		;

	return 1;
bad:
	return 0;
}

/* Unmap the I/O ports */
void
ad1848_unmap(struct ad1848_softc *sc)
{
	bus_space_unmap(sc->sc_iot, sc->sc_ioh, AD1848_NPORT);
}

/*
 * Attach hardware to driver, attach hardware driver to audio
 * pseudo-device driver .
 */
void
ad1848_attach(struct ad1848_softc *sc)
{
	int i;
	struct ad1848_volume vol_mid = {220, 220};
	struct ad1848_volume vol_0   = {0, 0};
	struct audio_params pparams, rparams;
	int timeout;

	sc->sc_playrun = 0;
	sc->sc_recrun = 0;

	if (sc->sc_drq != -1) {
		if (isa_dmamap_create(sc->sc_isa, sc->sc_drq, MAX_ISADMA,
		    BUS_DMA_NOWAIT|BUS_DMA_ALLOCNOW)) {
			printf("ad1848_attach: can't create map for drq %d\n",
			    sc->sc_drq);
			return;
		}
	}
	if (sc->sc_recdrq != -1 && sc->sc_recdrq != sc->sc_drq) {
		if (isa_dmamap_create(sc->sc_isa, sc->sc_recdrq, MAX_ISADMA,
		    BUS_DMA_NOWAIT|BUS_DMA_ALLOCNOW)) {
			printf("ad1848_attach: can't create map for second drq %d\n",
			    sc->sc_recdrq);
			return;
		}
	}

	/* Initialize the ad1848... */
	for (i = 0; i < 0x10; i++) {
		ad_write(sc, i, ad1848_init_values[i]);
		timeout = AD1848_TIMO;
		while (timeout > 0 && ADREAD(sc, AD1848_IADDR) & SP_IN_INIT)
			timeout--;
	}
	/* need 2 separate drqs for mode 2 */
	if ((sc->mode == 2) &&
	    ((sc->sc_recdrq == -1) || (sc->sc_recdrq == sc->sc_drq))) {
		ad_write(sc, SP_MISC_INFO, ad_read(sc, SP_MISC_INFO) & ~MODE2);
		if (!(ad_read(sc, SP_MISC_INFO) & MODE2))
			sc->mode = 1;
	}
	/* ...and additional CS4231 stuff too */
	if (sc->mode == 2) {
		ad_write(sc, SP_INTERFACE_CONFIG, 0); /* disable SINGLE_DMA */
		for (i = 0x10; i < 0x20; i++) {
			if (ad1848_init_values[i] != 0) {
				ad_write(sc, i, ad1848_init_values[i]);
				timeout = AD1848_TIMO;
				while (timeout > 0 && 
				    ADREAD(sc, AD1848_IADDR) & SP_IN_INIT)
					timeout--;
			}
		}
	}
	ad1848_reset(sc);

	pparams = ad1848_audio_default;
	rparams = ad1848_audio_default;
	(void) ad1848_set_params(sc, AUMODE_RECORD|AUMODE_PLAY, 0,
	    &pparams, &rparams);

	/* Set default gains */
	(void) ad1848_set_rec_gain(sc, &vol_mid);
	(void) ad1848_set_channel_gain(sc, AD1848_DAC_CHANNEL, &vol_mid);
	(void) ad1848_set_channel_gain(sc, AD1848_MONITOR_CHANNEL, &vol_0);
	/* CD volume */
	(void) ad1848_set_channel_gain(sc, AD1848_AUX1_CHANNEL, &vol_mid);
	if (sc->mode == 2) {
		 /* CD volume */
		(void) ad1848_set_channel_gain(sc, AD1848_AUX2_CHANNEL, &vol_mid);
		(void) ad1848_set_channel_gain(sc, AD1848_LINE_CHANNEL, &vol_mid);
		(void) ad1848_set_channel_gain(sc, AD1848_MONO_CHANNEL, &vol_0);
		sc->mute[AD1848_MONO_CHANNEL] = MUTE_ALL;
	} else
		(void) ad1848_set_channel_gain(sc, AD1848_AUX2_CHANNEL, &vol_0);

	/* Set default port */
	(void) ad1848_set_rec_port(sc, MIC_IN_PORT);

	if (sc->chip_name)
		printf(": %s", sc->chip_name);
}

/*
 * Various routines to interface to higher level audio driver
 */
struct ad1848_mixerinfo {
	int left_reg;
	int right_reg;
	int atten_bits;
	int atten_mask;
} mixer_channel_info[] = {
	{ SP_LEFT_AUX2_CONTROL, SP_RIGHT_AUX2_CONTROL, AUX_INPUT_ATTEN_BITS,
		AUX_INPUT_ATTEN_MASK },
	{ SP_LEFT_AUX1_CONTROL, SP_RIGHT_AUX1_CONTROL, AUX_INPUT_ATTEN_BITS,
		AUX_INPUT_ATTEN_MASK },
	{ SP_LEFT_OUTPUT_CONTROL, SP_RIGHT_OUTPUT_CONTROL, OUTPUT_ATTEN_BITS,
		OUTPUT_ATTEN_MASK }, 
	{ CS_LEFT_LINE_CONTROL, CS_RIGHT_LINE_CONTROL, LINE_INPUT_ATTEN_BITS,
		LINE_INPUT_ATTEN_MASK },
	{ CS_MONO_IO_CONTROL, 0, MONO_INPUT_ATTEN_BITS, MONO_INPUT_ATTEN_MASK },
	{ SP_DIGITAL_MIX, 0, OUTPUT_ATTEN_BITS, MIX_ATTEN_MASK }
};

/*
 *  This function doesn't set the mute flags but does use them.
 *  The mute flags reflect the mutes that have been applied by the user.
 *  However, the driver occasionally wants to mute devices (e.g. when changing
 *  sampling rate). These operations should not affect the mute flags.
 */
void 
ad1848_mute_channel(struct ad1848_softc *sc, int device, int mute)
{
	u_char reg;

	reg = ad_read(sc, mixer_channel_info[device].left_reg);

	if (mute & MUTE_LEFT) {
		if (device == AD1848_MONITOR_CHANNEL) {
			ad_write(sc, mixer_channel_info[device].left_reg,
			    reg & 0xFE);
		} else {
			ad_write(sc, mixer_channel_info[device].left_reg,
			    reg | 0x80);
		}
	} else if (!(sc->mute[device] & MUTE_LEFT)) {
		if (device == AD1848_MONITOR_CHANNEL) {
			ad_write(sc, mixer_channel_info[device].left_reg,
			    reg | 0x01);
		} else {
			ad_write(sc, mixer_channel_info[device].left_reg,
			    reg & ~0x80);
		}
	}

	if (!mixer_channel_info[device].right_reg) {
		return;
  	}

	reg = ad_read(sc, mixer_channel_info[device].right_reg);

	if (mute & MUTE_RIGHT) {
		ad_write(sc, mixer_channel_info[device].right_reg, reg | 0x80);
	} else if (!(sc->mute[device] & MUTE_RIGHT)) {
		ad_write(sc, mixer_channel_info[device].right_reg, reg & ~0x80);
	}
}

int
ad1848_set_channel_gain(struct ad1848_softc *sc, int device,
    struct ad1848_volume *gp)
{
	struct ad1848_mixerinfo *info = &mixer_channel_info[device];
	u_char reg;
	u_int atten;

	sc->gains[device] = *gp;

	atten = ((AUDIO_MAX_GAIN - gp->left) * info->atten_bits) /
	    AUDIO_MAX_GAIN;

	reg = ad_read(sc, info->left_reg) & (info->atten_mask);
	if (device == AD1848_MONITOR_CHANNEL)
		reg |= ((atten & info->atten_bits) << 2);
	else
		reg |= ((atten & info->atten_bits));

	ad_write(sc, info->left_reg, reg);

	if (!info->right_reg)
		return 0;

	atten = ((AUDIO_MAX_GAIN - gp->right) * info->atten_bits) /
	    AUDIO_MAX_GAIN;
	reg = ad_read(sc, info->right_reg);
	reg &= (info->atten_mask);
	ad_write(sc, info->right_reg, (atten & info->atten_bits) | reg);

	return 0;
}

int
ad1848_get_device_gain(struct ad1848_softc *sc, int device,
    struct ad1848_volume *gp)
{
	*gp = sc->gains[device];
	return 0;
}

int
ad1848_get_rec_gain(struct ad1848_softc *sc, struct ad1848_volume *gp)
{
	*gp = sc->rec_gain;
	return 0;
}

int
ad1848_set_rec_gain(struct ad1848_softc *sc, struct ad1848_volume *gp)
{
	u_char reg, gain;
	
	DPRINTF(("ad1848_set_rec_gain: %d:%d\n", gp->left, gp->right));

	sc->rec_gain = *gp;

	gain = (gp->left * GAIN_22_5) / AUDIO_MAX_GAIN;
	reg = ad_read(sc, SP_LEFT_INPUT_CONTROL);
	reg &= INPUT_GAIN_MASK;
	ad_write(sc, SP_LEFT_INPUT_CONTROL, (gain & 0x0f) | reg);

	gain = (gp->right * GAIN_22_5) / AUDIO_MAX_GAIN;
	reg = ad_read(sc, SP_RIGHT_INPUT_CONTROL);
	reg &= INPUT_GAIN_MASK;
	ad_write(sc, SP_RIGHT_INPUT_CONTROL, (gain & 0x0f) | reg);

	return 0;
}

void
ad1848_mute_monitor(void *addr, int mute)
{
	struct ad1848_softc *sc = addr;

	DPRINTF(("ad1848_mute_monitor: %smuting\n", mute ? "" : "un"));
	if (sc->mode == 2) {
		ad1848_mute_channel(sc, AD1848_DAC_CHANNEL,
		    mute ? MUTE_ALL : 0);
		ad1848_mute_channel(sc, AD1848_MONO_CHANNEL,
		    mute ? MUTE_MONO : 0);
		ad1848_mute_channel(sc, AD1848_LINE_CHANNEL,
		    mute ? MUTE_ALL : 0);
	}

	ad1848_mute_channel(sc, AD1848_AUX2_CHANNEL, mute ? MUTE_ALL : 0);
	ad1848_mute_channel(sc, AD1848_AUX1_CHANNEL, mute ? MUTE_ALL : 0);
}

int
ad1848_set_mic_gain(struct ad1848_softc *sc, struct ad1848_volume *gp)
{
	u_char reg;

	DPRINTF(("cs4231_set_mic_gain: %d\n", gp->left));

	if (gp->left > AUDIO_MAX_GAIN / 2) {
		sc->mic_gain_on = 1;
		reg = ad_read(sc, SP_LEFT_INPUT_CONTROL);
		ad_write(sc, SP_LEFT_INPUT_CONTROL,
		    reg | INPUT_MIC_GAIN_ENABLE);
	} else {
		sc->mic_gain_on = 0;
		reg = ad_read(sc, SP_LEFT_INPUT_CONTROL);
		ad_write(sc, SP_LEFT_INPUT_CONTROL,
		    reg & ~INPUT_MIC_GAIN_ENABLE);
	}

	return 0;
}

int
ad1848_get_mic_gain(struct ad1848_softc *sc, struct ad1848_volume *gp)
{
	if (sc->mic_gain_on)
		gp->left = gp->right = AUDIO_MAX_GAIN;
	else
		gp->left = gp->right = AUDIO_MIN_GAIN;

	return 0;
}


static ad1848_devmap_t *ad1848_mixer_find_dev(ad1848_devmap_t *, int, mixer_ctrl_t *);

static ad1848_devmap_t *
ad1848_mixer_find_dev(ad1848_devmap_t *map, int cnt, mixer_ctrl_t *cp)
{
	int idx;

	for (idx = 0; idx < cnt; idx++) {
		if (map[idx].id == cp->dev) {
			return &map[idx];
		}
  	}
	return NULL;
}

int
ad1848_mixer_get_port(struct ad1848_softc *ac, struct ad1848_devmap *map,
    int cnt, mixer_ctrl_t *cp)
{
	ad1848_devmap_t *entry;
	struct ad1848_volume vol;
	int error = EINVAL;
	int dev;

	if (!(entry = ad1848_mixer_find_dev(map, cnt, cp)))
		return (ENXIO);

	dev = entry->dev;
	mtx_enter(&audio_lock);
	switch (entry->kind) {
	case AD1848_KIND_LVL:
		if (cp->type != AUDIO_MIXER_VALUE)
			break;
		if (dev < AD1848_AUX2_CHANNEL ||
		    dev > AD1848_MONITOR_CHANNEL)
	  		break;
		if (cp->un.value.num_channels != 1 &&
		    mixer_channel_info[dev].right_reg == 0) 
	  		break;
		error = ad1848_get_device_gain(ac, dev, &vol);
		if (!error)
			ad1848_from_vol(cp, &vol);
		break;

	case AD1848_KIND_MUTE:
		if (cp->type != AUDIO_MIXER_ENUM)
			break;
		cp->un.ord = ac->mute[dev] ? 1 : 0;
		error = 0;
		break;

	case AD1848_KIND_RECORDGAIN:
		if (cp->type != AUDIO_MIXER_VALUE)
			break;
		error = ad1848_get_rec_gain(ac, &vol);
		if (!error)
			ad1848_from_vol(cp, &vol);
		break;

	case AD1848_KIND_MICGAIN:
		if (cp->type != AUDIO_MIXER_VALUE)
			break;
		error = ad1848_get_mic_gain(ac, &vol);
		if (!error)
			ad1848_from_vol(cp, &vol);
		break;

	case AD1848_KIND_RECORDSOURCE:
		if (cp->type != AUDIO_MIXER_ENUM)
			break;
		cp->un.ord = ad1848_get_rec_port(ac);
		error = 0;
		break;

	default:
		printf("Invalid kind\n");
		break;
	}
	mtx_leave(&audio_lock);
	return error;
}

int	 
ad1848_mixer_set_port(struct ad1848_softc *ac, struct ad1848_devmap *map,
    int cnt, mixer_ctrl_t *cp)
{
	ad1848_devmap_t *entry;
	struct ad1848_volume vol;
	int error = EINVAL;
	int dev;

	if (!(entry = ad1848_mixer_find_dev(map, cnt, cp)))
		return (ENXIO);

  	dev = entry->dev;
	mtx_enter(&audio_lock);
	switch (entry->kind) {
	case AD1848_KIND_LVL:
		if (cp->type != AUDIO_MIXER_VALUE)
			break;
		if (dev < AD1848_AUX2_CHANNEL ||
		    dev > AD1848_MONITOR_CHANNEL)
			break;
		if (cp->un.value.num_channels != 1 &&
		    mixer_channel_info[dev].right_reg == 0) 
			break;
		ad1848_to_vol(cp, &vol);
		error = ad1848_set_channel_gain(ac, dev, &vol);
		break;

	case AD1848_KIND_MUTE:
		if (cp->type != AUDIO_MIXER_ENUM)
			break;
		ac->mute[dev] = (cp->un.ord ? MUTE_ALL : 0);
		ad1848_mute_channel(ac, dev, ac->mute[dev]);
		error = 0;
		break;

	case AD1848_KIND_RECORDGAIN:
		if (cp->type != AUDIO_MIXER_VALUE)
			break;
		ad1848_to_vol(cp, &vol);
		error = ad1848_set_rec_gain(ac, &vol);
		break;

	case AD1848_KIND_MICGAIN:
		if (cp->type != AUDIO_MIXER_VALUE)
			break;
		ad1848_to_vol(cp, &vol);
		error = ad1848_set_mic_gain(ac, &vol);
		break;

	case AD1848_KIND_RECORDSOURCE:
		if (cp->type != AUDIO_MIXER_ENUM)
			break;
		error = ad1848_set_rec_port(ac,  cp->un.ord);
		break;

	default:
		printf("Invalid kind\n");
		break;
	}
	mtx_leave(&audio_lock);
	return (error);
}

int
ad1848_set_params(void *addr, int setmode, int usemode, struct audio_params *p,
    struct audio_params *r)
{
	struct ad1848_softc *sc = addr;
	int error, bits, enc;

	DPRINTF(("ad1848_set_params: %d %d %d %ld\n", 
	     p->encoding, p->precision, p->channels, p->sample_rate));

	enc = p->encoding;
	switch (enc) {
	case AUDIO_ENCODING_SLINEAR_LE:
		if (p->precision == 8)
			return EINVAL;
		break;
	case AUDIO_ENCODING_SLINEAR_BE:
		if (p->precision == 16)
			return EINVAL;
		break;
	case AUDIO_ENCODING_ULINEAR_LE:
		if (p->precision == 16)
			return EINVAL;
		break;
	case AUDIO_ENCODING_ULINEAR_BE:
		if (p->precision == 16)
			return EINVAL;
		break;
	}
	switch (enc) {
	case AUDIO_ENCODING_ULAW:
		p->precision = 8;
		bits = FMT_ULAW;
		break;
	case AUDIO_ENCODING_ALAW:
		p->precision = 8;
		bits = FMT_ALAW;
		break;
	case AUDIO_ENCODING_SLINEAR_LE:
		if (p->precision == 16)
			bits = FMT_TWOS_COMP;
		else
			return EINVAL;
		break;
	case AUDIO_ENCODING_SLINEAR_BE:
		if (p->precision == 16)
			bits = FMT_TWOS_COMP_BE;
		else
			return EINVAL;
		break;
	case AUDIO_ENCODING_ULINEAR_LE:
		if (p->precision == 8)
			bits = FMT_PCM8;
		else
			return EINVAL;
		break;
	default:
		return EINVAL;
	}

	if (p->channels < 1 || p->channels > 2)
		return EINVAL;

	error = ad1848_set_speed(sc, &p->sample_rate);
	if (error)
		return error;

	p->bps = AUDIO_BPS(p->precision);
	r->bps = AUDIO_BPS(r->precision);
	p->msb = 1;
	r->msb = 1;

	sc->format_bits = bits;
	sc->channels = p->channels;
	sc->precision = p->precision;
	sc->need_commit = 1;

	DPRINTF(("ad1848_set_params succeeded, bits=%x\n", bits));
	return (0);
}

int
ad1848_set_rec_port(struct ad1848_softc *sc, int port)
{
	u_char inp, reg;

	DPRINTF(("ad1848_set_rec_port: 0x%x\n", port));

	if (port == MIC_IN_PORT) {
		inp = MIC_INPUT;
	} else if (port == LINE_IN_PORT) {
		inp = LINE_INPUT;
	} else if (port == DAC_IN_PORT) {
		inp = MIXED_DAC_INPUT;
	} else if (sc->mode == 2 && port == AUX1_IN_PORT) {
		inp = AUX_INPUT;
	} else
		return EINVAL;

	reg = ad_read(sc, SP_LEFT_INPUT_CONTROL);
	reg &= INPUT_SOURCE_MASK;
	ad_write(sc, SP_LEFT_INPUT_CONTROL, (inp | reg));

	reg = ad_read(sc, SP_RIGHT_INPUT_CONTROL);
	reg &= INPUT_SOURCE_MASK;
	ad_write(sc, SP_RIGHT_INPUT_CONTROL, (inp | reg));

	sc->rec_port = port;

	return 0;
}

int
ad1848_get_rec_port(struct ad1848_softc *sc)
{
	return sc->rec_port;
}

int
ad1848_round_blocksize(void *addr, int blk)
{
	/* Round to a multiple of the biggest sample size. */
	blk = (blk + 3) & -4;

	return blk;
}

int
ad1848_open(void *addr, int flags)
{
	struct ad1848_softc *sc = addr;

	DPRINTF(("ad1848_open: sc=%p\n", sc));

	if ((flags & (FWRITE | FREAD)) == (FWRITE | FREAD) && sc->mode != 2)
		return ENXIO;

	sc->sc_pintr = sc->sc_parg = NULL;
	sc->sc_rintr = sc->sc_rarg = NULL;

	/* Enable interrupts */
	DPRINTF(("ad1848_open: enable intrs\n"));
	ad_write(sc, SP_PIN_CONTROL,
	    INTERRUPT_ENABLE | ad_read(sc, SP_PIN_CONTROL));

#ifdef AUDIO_DEBUG
	if (ad1848debug > 2)
		ad1848_dump_regs(sc);
#endif

	return 0;
}

/*
 * Close function is called at splaudio().
 */
void
ad1848_close(void *addr)
{
	struct ad1848_softc *sc = addr;
	u_char r;

	ad1848_halt_output(sc);
	ad1848_halt_input(sc);

	sc->sc_pintr = NULL;
	sc->sc_rintr = NULL;

	DPRINTF(("ad1848_close: stop DMA\n"));

	ad_write(sc, SP_LOWER_BASE_COUNT, (u_char)0);
	ad_write(sc, SP_UPPER_BASE_COUNT, (u_char)0);

	/* Disable interrupts */
	DPRINTF(("ad1848_close: disable intrs\n"));
	ad_write(sc, SP_PIN_CONTROL, 
	    ad_read(sc, SP_PIN_CONTROL) & ~INTERRUPT_ENABLE);

	DPRINTF(("ad1848_close: disable capture and playback\n"));
	r = ad_read(sc, SP_INTERFACE_CONFIG);
	r &= ~(CAPTURE_ENABLE | PLAYBACK_ENABLE);
	ad_write(sc, SP_INTERFACE_CONFIG, r);

#ifdef AUDIO_DEBUG
	if (ad1848debug > 2)
		ad1848_dump_regs(sc);
#endif
}

/*
 * Lower-level routines
 */
int
ad1848_commit_settings(void *addr)
{
	struct ad1848_softc *sc = addr;
	int timeout;
	u_char fs;

	if (!sc->need_commit)
		return 0;

	mtx_enter(&audio_lock);

	ad1848_mute_monitor(sc, 1);

	/* Enables changes to the format select reg */
	ad_set_MCE(sc, 1);

	fs = sc->speed_bits | sc->format_bits;

	if (sc->channels == 2)
		fs |= FMT_STEREO;

	ad_write(sc, SP_CLOCK_DATA_FORMAT, fs);

	/*
	 * If mode == 2 (CS4231), set I28 also. It's the capture format
	 * register.
	 */
	if (sc->mode == 2) {
		/* Gravis Ultrasound MAX SDK sources says something about
		 * errata sheets, with the implication that these inb()s
		 * are necessary.
		 */
		(void)ADREAD(sc, AD1848_IDATA);
		(void)ADREAD(sc, AD1848_IDATA);

		/*
		 * Write to I8 starts resynchronization. Wait until it
		 * completes.
		 */
		timeout = AD1848_TIMO;
		while (timeout > 0 && ADREAD(sc, AD1848_IADDR) == SP_IN_INIT)
			timeout--;

		ad_write(sc, CS_REC_FORMAT, fs);
		/* Gravis Ultrasound MAX SDK sources says something about
		 * errata sheets, with the implication that these inb()s
		 * are necessary.
		 */
		(void)ADREAD(sc, AD1848_IDATA);
		(void)ADREAD(sc, AD1848_IDATA);
		/* Now wait for resync for capture side of the house */
	}
	/*
	 * Write to I8 starts resynchronization. Wait until it completes.
	 */
	timeout = AD1848_TIMO;
	while (timeout > 0 && ADREAD(sc, AD1848_IADDR) == SP_IN_INIT)
		timeout--;

	if (ADREAD(sc, AD1848_IADDR) == SP_IN_INIT)
		printf("ad1848_commit: Auto calibration timed out\n");

	/*
	 * Starts the calibration process and enters playback mode after it.
	 */
	ad_set_MCE(sc, 0);
	wait_for_calibration(sc);

	ad1848_mute_monitor(sc, 0);

	mtx_leave(&audio_lock);
	
	sc->need_commit = 0;

	return 0;
}

void
ad1848_reset(struct ad1848_softc *sc)
{
	u_char r;

	DPRINTF(("ad1848_reset\n"));

	/* Clear the PEN and CEN bits */
	r = ad_read(sc, SP_INTERFACE_CONFIG);
	r &= ~(CAPTURE_ENABLE | PLAYBACK_ENABLE);
	ad_write(sc, SP_INTERFACE_CONFIG, r);

	/* Clear interrupt status */
	if (sc->mode == 2)
		ad_write(sc, CS_IRQ_STATUS, 0);
	ADWRITE(sc, AD1848_STATUS, 0);

#ifdef AUDIO_DEBUG
	if (ad1848debug > 2)
		ad1848_dump_regs(sc);
#endif
}

int
ad1848_set_speed(struct ad1848_softc *sc, u_long *argp)
{
	/*
	 * The sampling speed is encoded in the least significant nible of I8.
	 * The LSB selects the clock source (0=24.576 MHz, 1=16.9344 MHz) and
	 * other three bits select the divisor (indirectly):
	 *
	 * The available speeds are in the following table. Keep the speeds in
	 * the increasing order.
	 */
	typedef struct {
		int	speed;
		u_char	bits;
	} speed_struct;
	u_long arg = *argp;

	static speed_struct speed_table[] =  {
		{5510, (0 << 1) | 1},
		{5510, (0 << 1) | 1},
		{6620, (7 << 1) | 1},
		{8000, (0 << 1) | 0},
		{9600, (7 << 1) | 0},
		{11025, (1 << 1) | 1},
		{16000, (1 << 1) | 0},
		{18900, (2 << 1) | 1},
		{22050, (3 << 1) | 1},
		{27420, (2 << 1) | 0},
		{32000, (3 << 1) | 0},
		{33075, (6 << 1) | 1},
		{37800, (4 << 1) | 1},
		{44100, (5 << 1) | 1},
		{48000, (6 << 1) | 0}
	};

	int i, n, selected = -1;

	n = sizeof(speed_table) / sizeof(speed_struct);

	if (arg < speed_table[0].speed)
		selected = 0;
	if (arg > speed_table[n - 1].speed)
		selected = n - 1;

	for (i = 1 /*really*/ ; selected == -1 && i < n; i++) {
		if (speed_table[i].speed == arg)
			selected = i;
		else if (speed_table[i].speed > arg) {
			int diff1, diff2;

			diff1 = arg - speed_table[i - 1].speed;
			diff2 = speed_table[i].speed - arg;

			if (diff1 < diff2)
				selected = i - 1;
			else
				selected = i;
		}
	}

	if (selected == -1) {
		printf("ad1848: Can't find speed???\n");
		selected = 3;
	}

	sc->speed_bits = speed_table[selected].bits;
	sc->need_commit = 1;
	*argp = speed_table[selected].speed;

	return 0;
}

/*
 * Halt a DMA in progress.
 */
int
ad1848_halt_output(void *addr)
{
	struct ad1848_softc *sc = addr;
	u_char reg;

	DPRINTF(("ad1848: ad1848_halt_output\n"));
	mtx_enter(&audio_lock);
	reg = ad_read(sc, SP_INTERFACE_CONFIG);
	ad_write(sc, SP_INTERFACE_CONFIG, (reg & ~PLAYBACK_ENABLE));

	if (sc->sc_playrun == 1) {
		isa_dmaabort(sc->sc_isa, sc->sc_drq);
		sc->sc_playrun = 0;
	}
	mtx_leave(&audio_lock);
	return 0;
}

int
ad1848_halt_input(void *addr)
{
	struct ad1848_softc *sc = addr;
	u_char reg;

	DPRINTF(("ad1848: ad1848_halt_input\n"));
	mtx_enter(&audio_lock);
	reg = ad_read(sc, SP_INTERFACE_CONFIG);
	ad_write(sc, SP_INTERFACE_CONFIG, (reg & ~CAPTURE_ENABLE));

	if (sc->sc_recrun == 1) {
		isa_dmaabort(sc->sc_isa, sc->sc_recdrq);
		sc->sc_recrun = 0;
	}
	mtx_leave(&audio_lock);
	return 0;
}

int
ad1848_trigger_input(void *addr, void *start, void *end, int blksize,
    void (*intr)(void *), void *arg, struct audio_params *param)
{
	struct ad1848_softc *sc = addr;
	u_char reg;

	if (sc->sc_recdrq == -1) {
		DPRINTF(("ad1848_trigger_input: invalid recording drq\n"));
		return ENXIO;
	}
	mtx_enter(&audio_lock);
	isa_dmastart(sc->sc_isa, sc->sc_recdrq, start,
	    (char *)end - (char *)start, NULL, DMAMODE_READ | DMAMODE_LOOP,
	    BUS_DMA_NOWAIT);

	sc->sc_recrun = 1;
	sc->sc_rintr = intr;
	sc->sc_rarg = arg;

	blksize = (blksize * NBBY) / (param->precision * param->channels) - 1;

	if (sc->mode == 2) {
		ad_write(sc, CS_LOWER_REC_CNT, (blksize & 0xff));
		ad_write(sc, CS_UPPER_REC_CNT, ((blksize >> 8) & 0xff));
	} else {
		ad_write(sc, SP_LOWER_BASE_COUNT, blksize & 0xff);
		ad_write(sc, SP_UPPER_BASE_COUNT, (blksize >> 8) & 0xff);
	}

	reg = ad_read(sc, SP_INTERFACE_CONFIG);
	ad_write(sc, SP_INTERFACE_CONFIG, (CAPTURE_ENABLE | reg));

#ifdef AUDIO_DEBUG
	if (ad1848debug > 1)
		printf("ad1848_trigger_input: started capture\n");
#endif
	mtx_leave(&audio_lock);
	return 0;
}

int
ad1848_trigger_output(void *addr, void *start, void *end, int blksize,
    void (*intr)(void *), void *arg, struct audio_params *param)
{
	struct ad1848_softc *sc = addr;
	u_char reg;

	mtx_enter(&audio_lock);
	isa_dmastart(sc->sc_isa, sc->sc_drq, start,
	    (char *)end - (char *)start, NULL,
	    DMAMODE_WRITE | DMAMODE_LOOP, BUS_DMA_NOWAIT);

	sc->sc_playrun = 1;
	sc->sc_pintr = intr;
	sc->sc_parg = arg;

	blksize = (blksize * NBBY) / (param->precision * param->channels) - 1;

	ad_write(sc, SP_LOWER_BASE_COUNT, blksize & 0xff);
	ad_write(sc, SP_UPPER_BASE_COUNT, (blksize >> 8) & 0xff);

	reg = ad_read(sc, SP_INTERFACE_CONFIG);
	ad_write(sc, SP_INTERFACE_CONFIG, (PLAYBACK_ENABLE | reg));

#ifdef AUDIO_DEBUG
	if (ad1848debug > 1)
		printf("ad1848_trigger_output: started playback\n");
#endif
	mtx_leave(&audio_lock);
	return 0;
}

int
ad1848_intr(void *arg)
{
	struct ad1848_softc *sc = arg;
	int retval = 0;
	u_char status;

	mtx_enter(&audio_lock);
	/* Get intr status */
	status = ADREAD(sc, AD1848_STATUS);
	
#ifdef AUDIO_DEBUG
	if (ad1848debug > 1)
		printf("ad1848_intr: mode=%d pintr=%p prun=%d rintr=%p rrun=%d status=0x%x\n",
		    sc->mode, sc->sc_pintr, sc->sc_playrun, sc->sc_rintr, sc->sc_recrun, status);
#endif

	/* Handle interrupt */
	if ((status & INTERRUPT_STATUS) != 0) {
		if (sc->mode == 2) {
			status = ad_read(sc, CS_IRQ_STATUS);
#ifdef AUDIO_DEBUG
			if (ad1848debug > 2)
				printf("ad1848_intr: cs_irq_status=0x%x (play=0x%x rec0x%x)\n",
				    status, CS_IRQ_PI, CS_IRQ_CI);
#endif
			if ((status & CS_IRQ_PI) && sc->sc_playrun) {
				(*sc->sc_pintr)(sc->sc_parg);
				retval = 1;
			}
			if ((status & CS_IRQ_CI) && sc->sc_recrun) {
				(*sc->sc_rintr)(sc->sc_rarg);
				retval = 1;
			}
		} else {
			if (sc->sc_playrun) {
				(*sc->sc_pintr)(sc->sc_parg);
				retval = 1;
			} else if (sc->sc_recrun) {
				(*sc->sc_rintr)(sc->sc_rarg);
				retval = 1;
			}
		}
		/* clear interrupt */
		ADWRITE(sc, AD1848_STATUS, 0);
	}
	mtx_leave(&audio_lock);
	return(retval);
}

void *
ad1848_malloc(void *addr, int direction, size_t size, int pool, int flags)
{
	struct ad1848_softc *sc = addr;
	int drq;

	if (direction == AUMODE_PLAY)
		drq = sc->sc_drq;
	else
		drq = sc->sc_recdrq;

	return isa_malloc(sc->sc_isa, drq, size, pool, flags);
}

void
ad1848_free(void *addr, void *ptr, int pool)
{
	isa_free(ptr, pool);
}

size_t
ad1848_round(void *addr, int direction, size_t size)
{
	if (size > MAX_ISADMA)
		size = MAX_ISADMA;
	return size;
}
