/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000-2004 Taku YAMAMOTO <taku@tackymt.homeip.net>
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
 *	maestro.c,v 1.23.2.1 2003/10/03 18:21:38 taku Exp
 */

/*
 * Credits:
 *
 * Part of this code (especially in many magic numbers) was heavily inspired
 * by the Linux driver originally written by
 * Alan Cox <alan.cox@linux.org>, modified heavily by
 * Zach Brown <zab@zabbo.net>.
 *
 * busdma()-ize and buffer size reduction were suggested by
 * Cameron Grant <cg@freebsd.org>.
 * Also he showed me the way to use busdma() suite.
 *
 * Internal speaker problems on NEC VersaPro's and Dell Inspiron 7500
 * were looked at by
 * Munehiro Matsuda <haro@tk.kubota.co.jp>,
 * who brought patches based on the Linux driver with some simplification.
 *
 * Hardware volume controller was implemented by
 * John Baldwin <jhb@freebsd.org>.
 */

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/pcm/sound.h>
#include <dev/sound/pcm/ac97.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/sound/pci/maestro_reg.h>

SND_DECLARE_FILE("$FreeBSD$");

/*
 * PCI IDs of supported chips:
 *
 * MAESTRO-1	0x01001285
 * MAESTRO-2	0x1968125d
 * MAESTRO-2E	0x1978125d
 */

#define MAESTRO_1_PCI_ID	0x01001285
#define MAESTRO_2_PCI_ID	0x1968125d
#define MAESTRO_2E_PCI_ID	0x1978125d

#define NEC_SUBID1	0x80581033	/* Taken from Linux driver */
#define NEC_SUBID2	0x803c1033	/* NEC VersaProNX VA26D    */

#ifdef AGG_MAXPLAYCH
# if AGG_MAXPLAYCH > 4
#  undef AGG_MAXPLAYCH
#  define AGG_MAXPLAYCH 4
# endif
#else
# define AGG_MAXPLAYCH	4
#endif

#define AGG_DEFAULT_BUFSZ	0x4000 /* 0x1000, but gets underflows */


#ifndef PCIR_BAR
#define PCIR_BAR(x)	(PCIR_MAPS + (x) * 4)
#endif


/* -----------------------------
 * Data structures.
 */
struct agg_chinfo {
	/* parent softc */
	struct agg_info		*parent;

	/* FreeBSD newpcm related */
	struct pcm_channel	*channel;
	struct snd_dbuf		*buffer;

	/* OS independent */
	bus_dmamap_t		map;
	bus_addr_t		phys;	/* channel buffer physical address */
	bus_addr_t		base;	/* channel buffer segment base */
	u_int32_t		blklen;	/* DMA block length in WORDs */
	u_int32_t		buflen;	/* channel buffer length in WORDs */
	u_int32_t		speed;
	unsigned		num	: 3;
	unsigned		stereo	: 1;
	unsigned		qs16	: 1;	/* quantum size is 16bit */
	unsigned		us	: 1;	/* in unsigned format */
};

struct agg_rchinfo {
	/* parent softc */
	struct agg_info		*parent;

	/* FreeBSD newpcm related */
	struct pcm_channel	*channel;
	struct snd_dbuf		*buffer;

	/* OS independent */
	bus_dmamap_t		map;
	bus_addr_t		phys;	/* channel buffer physical address */
	bus_addr_t		base;	/* channel buffer segment base */
	u_int32_t		blklen;	/* DMA block length in WORDs */
	u_int32_t		buflen;	/* channel buffer length in WORDs */
	u_int32_t		speed;
	unsigned			: 3;
	unsigned		stereo	: 1;
	bus_addr_t		srcphys;
	int16_t			*src;	/* stereo peer buffer */
	int16_t			*sink;	/* channel buffer pointer */
	volatile u_int32_t	hwptr;	/* ready point in 16bit sample */
};

struct agg_info {
	/* FreeBSD newbus related */
	device_t		dev;

	/* I wonder whether bus_space_* are in common in *BSD... */
	struct resource		*reg;
	int			regid;
	bus_space_tag_t		st;
	bus_space_handle_t	sh;

	struct resource		*irq;
	int			irqid;
	void			*ih;

	bus_dma_tag_t		buf_dmat;
	bus_dma_tag_t		stat_dmat;

	/* FreeBSD SMPng related */
	struct mtx		lock;	/* mutual exclusion */
	/* FreeBSD newpcm related */
	struct ac97_info	*codec;

	/* OS independent */
	bus_dmamap_t		stat_map;
	u_int8_t		*stat;	/* status buffer pointer */
	bus_addr_t		phys;	/* status buffer physical address */
	unsigned int		bufsz;	/* channel buffer size in bytes */
	u_int			playchns;
	volatile u_int		active;
	struct agg_chinfo	pch[AGG_MAXPLAYCH];
	struct agg_rchinfo	rch;
	volatile u_int8_t	curpwr;	/* current power status: D[0-3] */
};


/* -----------------------------
 * Sysctls for debug.
 */
static unsigned int powerstate_active = PCI_POWERSTATE_D1;
#ifdef MAESTRO_AGGRESSIVE_POWERSAVE
static unsigned int powerstate_idle   = PCI_POWERSTATE_D2;
#else
static unsigned int powerstate_idle   = PCI_POWERSTATE_D1;
#endif
static unsigned int powerstate_init   = PCI_POWERSTATE_D2;

/* XXX: this should move to a device specific sysctl dev.pcm.X.debug.Y via
   device_get_sysctl_*() as discussed on multimedia@ in msg-id
   <861wujij2q.fsf@xps.des.no> */
static SYSCTL_NODE(_debug, OID_AUTO, maestro, CTLFLAG_RD, 0, "");
SYSCTL_UINT(_debug_maestro, OID_AUTO, powerstate_active, CTLFLAG_RW,
	    &powerstate_active, 0, "The Dx power state when active (0-1)");
SYSCTL_UINT(_debug_maestro, OID_AUTO, powerstate_idle, CTLFLAG_RW,
	    &powerstate_idle, 0, "The Dx power state when idle (0-2)");
SYSCTL_UINT(_debug_maestro, OID_AUTO, powerstate_init, CTLFLAG_RW,
	    &powerstate_init, 0,
	    "The Dx power state prior to the first use (0-2)");


/* -----------------------------
 * Prototypes
 */

static void	agg_sleep(struct agg_info*, const char *wmesg, int msec);

#if 0
static __inline u_int32_t	agg_rd(struct agg_info*, int, int size);
static __inline void		agg_wr(struct agg_info*, int, u_int32_t data,
								int size);
#endif
static int	agg_rdcodec(struct agg_info*, int);
static int	agg_wrcodec(struct agg_info*, int, u_int32_t);

static void	ringbus_setdest(struct agg_info*, int, int);

static u_int16_t	wp_rdreg(struct agg_info*, u_int16_t);
static void		wp_wrreg(struct agg_info*, u_int16_t, u_int16_t);
static u_int16_t	wp_rdapu(struct agg_info*, unsigned, u_int16_t);
static void	wp_wrapu(struct agg_info*, unsigned, u_int16_t, u_int16_t);
static void	wp_settimer(struct agg_info*, u_int);
static void	wp_starttimer(struct agg_info*);
static void	wp_stoptimer(struct agg_info*);

#if 0
static u_int16_t	wc_rdreg(struct agg_info*, u_int16_t);
#endif
static void		wc_wrreg(struct agg_info*, u_int16_t, u_int16_t);
#if 0
static u_int16_t	wc_rdchctl(struct agg_info*, int);
#endif
static void		wc_wrchctl(struct agg_info*, int, u_int16_t);

static void	agg_stopclock(struct agg_info*, int part, int st);

static void	agg_initcodec(struct agg_info*);
static void	agg_init(struct agg_info*);
static void	agg_power(struct agg_info*, int);

static void	aggch_start_dac(struct agg_chinfo*);
static void	aggch_stop_dac(struct agg_chinfo*);
static void	aggch_start_adc(struct agg_rchinfo*);
static void	aggch_stop_adc(struct agg_rchinfo*);
static void	aggch_feed_adc_stereo(struct agg_rchinfo*);
static void	aggch_feed_adc_mono(struct agg_rchinfo*);

#ifdef AGG_JITTER_CORRECTION
static void	suppress_jitter(struct agg_chinfo*);
static void	suppress_rec_jitter(struct agg_rchinfo*);
#endif

static void	set_timer(struct agg_info*);

static void	agg_intr(void *);
static int	agg_probe(device_t);
static int	agg_attach(device_t);
static int	agg_detach(device_t);
static int	agg_suspend(device_t);
static int	agg_resume(device_t);
static int	agg_shutdown(device_t);

static void	*dma_malloc(bus_dma_tag_t, u_int32_t, bus_addr_t*,
		    bus_dmamap_t *);
static void	dma_free(bus_dma_tag_t, void *, bus_dmamap_t);


/* -----------------------------
 * Subsystems.
 */

/* locking */
#define agg_lock(sc)	snd_mtxlock(&((sc)->lock))
#define agg_unlock(sc)	snd_mtxunlock(&((sc)->lock))

static void
agg_sleep(struct agg_info *sc, const char *wmesg, int msec)
{
	int timo;

	timo = msec * hz / 1000;
	if (timo == 0)
		timo = 1;
	msleep(sc, &sc->lock, PWAIT, wmesg, timo);
}


/* I/O port */

#if 0
static __inline u_int32_t
agg_rd(struct agg_info *sc, int regno, int size)
{
	switch (size) {
	case 1:
		return bus_space_read_1(sc->st, sc->sh, regno);
	case 2:
		return bus_space_read_2(sc->st, sc->sh, regno);
	case 4:
		return bus_space_read_4(sc->st, sc->sh, regno);
	default:
		return ~(u_int32_t)0;
	}
}
#endif

#define AGG_RD(sc, regno, size)           \
	bus_space_read_##size(            \
	    ((struct agg_info*)(sc))->st, \
	    ((struct agg_info*)(sc))->sh, (regno))

#if 0
static __inline void
agg_wr(struct agg_info *sc, int regno, u_int32_t data, int size)
{
	switch (size) {
	case 1:
		bus_space_write_1(sc->st, sc->sh, regno, data);
		break;
	case 2:
		bus_space_write_2(sc->st, sc->sh, regno, data);
		break;
	case 4:
		bus_space_write_4(sc->st, sc->sh, regno, data);
		break;
	}
}
#endif

#define AGG_WR(sc, regno, data, size)     \
	bus_space_write_##size(           \
	    ((struct agg_info*)(sc))->st, \
	    ((struct agg_info*)(sc))->sh, (regno), (data))

/* -------------------------------------------------------------------- */

/* Codec/Ringbus */

static int
agg_codec_wait4idle(struct agg_info *ess)
{
	unsigned t = 26;

	while (AGG_RD(ess, PORT_CODEC_STAT, 1) & CODEC_STAT_MASK) {
		if (--t == 0)
			return EBUSY;
		DELAY(2);	/* 20.8us / 13 */
	}
	return 0;
}


static int
agg_rdcodec(struct agg_info *ess, int regno)
{
	int ret;

	/* We have to wait for a SAFE time to write addr/data */
	if (agg_codec_wait4idle(ess)) {
		/* Timed out. No read performed. */
		device_printf(ess->dev, "agg_rdcodec() PROGLESS timed out.\n");
		return -1;
	}

	AGG_WR(ess, PORT_CODEC_CMD, CODEC_CMD_READ | regno, 1);
	/*DELAY(21);	* AC97 cycle = 20.8usec */

	/* Wait for data retrieve */
	if (!agg_codec_wait4idle(ess)) {
		ret = AGG_RD(ess, PORT_CODEC_REG, 2);
	} else {
		/* Timed out. No read performed. */
		device_printf(ess->dev, "agg_rdcodec() RW_DONE timed out.\n");
		ret = -1;
	}

	return ret;
}

static int
agg_wrcodec(struct agg_info *ess, int regno, u_int32_t data)
{
	/* We have to wait for a SAFE time to write addr/data */
	if (agg_codec_wait4idle(ess)) {
		/* Timed out. Abort writing. */
		device_printf(ess->dev, "agg_wrcodec() PROGLESS timed out.\n");
		return -1;
	}

	AGG_WR(ess, PORT_CODEC_REG, data, 2);
	AGG_WR(ess, PORT_CODEC_CMD, CODEC_CMD_WRITE | regno, 1);

	/* Wait for write completion */
	if (agg_codec_wait4idle(ess)) {
		/* Timed out. */
		device_printf(ess->dev, "agg_wrcodec() RW_DONE timed out.\n");
		return -1;
	}

	return 0;
}

static void
ringbus_setdest(struct agg_info *ess, int src, int dest)
{
	u_int32_t	data;

	data = AGG_RD(ess, PORT_RINGBUS_CTRL, 4);
	data &= ~(0xfU << src);
	data |= (0xfU & dest) << src;
	AGG_WR(ess, PORT_RINGBUS_CTRL, data, 4);
}

/* -------------------------------------------------------------------- */

/* Wave Processor */

static u_int16_t
wp_rdreg(struct agg_info *ess, u_int16_t reg)
{
	AGG_WR(ess, PORT_DSP_INDEX, reg, 2);
	return AGG_RD(ess, PORT_DSP_DATA, 2);
}

static void
wp_wrreg(struct agg_info *ess, u_int16_t reg, u_int16_t data)
{
	AGG_WR(ess, PORT_DSP_INDEX, reg, 2);
	AGG_WR(ess, PORT_DSP_DATA, data, 2);
}

static int
wp_wait_data(struct agg_info *ess, u_int16_t data)
{
	unsigned t = 0;

	while (AGG_RD(ess, PORT_DSP_DATA, 2) != data) {
		if (++t == 1000) {
			return EAGAIN;
		}
		AGG_WR(ess, PORT_DSP_DATA, data, 2);
	}

	return 0;
}

static u_int16_t
wp_rdapu(struct agg_info *ess, unsigned ch, u_int16_t reg)
{
	wp_wrreg(ess, WPREG_CRAM_PTR, reg | (ch << 4));
	if (wp_wait_data(ess, reg | (ch << 4)) != 0)
		device_printf(ess->dev, "wp_rdapu() indexing timed out.\n");
	return wp_rdreg(ess, WPREG_DATA_PORT);
}

static void
wp_wrapu(struct agg_info *ess, unsigned ch, u_int16_t reg, u_int16_t data)
{
	wp_wrreg(ess, WPREG_CRAM_PTR, reg | (ch << 4));
	if (wp_wait_data(ess, reg | (ch << 4)) == 0) {
		wp_wrreg(ess, WPREG_DATA_PORT, data);
		if (wp_wait_data(ess, data) != 0)
			device_printf(ess->dev,
			    "wp_wrapu() write timed out.\n");
	} else {
		device_printf(ess->dev, "wp_wrapu() indexing timed out.\n");
	}
}

static void
apu_setparam(struct agg_info *ess, int apuch,
    u_int32_t wpwa, u_int16_t size, int16_t pan, u_int dv)
{
	wp_wrapu(ess, apuch, APUREG_WAVESPACE, (wpwa >> 8) & APU_64KPAGE_MASK);
	wp_wrapu(ess, apuch, APUREG_CURPTR, wpwa);
	wp_wrapu(ess, apuch, APUREG_ENDPTR, wpwa + size);
	wp_wrapu(ess, apuch, APUREG_LOOPLEN, size);
	wp_wrapu(ess, apuch, APUREG_ROUTING, 0);
	wp_wrapu(ess, apuch, APUREG_AMPLITUDE, 0xf000);
	wp_wrapu(ess, apuch, APUREG_POSITION, 0x8f00
	    | (APU_RADIUS_MASK & (RADIUS_CENTERCIRCLE << APU_RADIUS_SHIFT))
	    | (APU_PAN_MASK & ((pan + PAN_FRONT) << APU_PAN_SHIFT)));
	wp_wrapu(ess, apuch, APUREG_FREQ_LOBYTE,
	    APU_plus6dB | ((dv & 0xff) << APU_FREQ_LOBYTE_SHIFT));
	wp_wrapu(ess, apuch, APUREG_FREQ_HIWORD, dv >> 8);
}

static void
wp_settimer(struct agg_info *ess, u_int divide)
{
	u_int prescale = 0;

	RANGE(divide, 2, 32 << 7);

	for (; divide > 32; divide >>= 1) {
		prescale++;
		divide++;
	}

	for (; prescale < 7 && divide > 2 && !(divide & 1); divide >>= 1)
		prescale++;

	wp_wrreg(ess, WPREG_TIMER_ENABLE, 0);
	wp_wrreg(ess, WPREG_TIMER_FREQ, 0x9000 |
	    (prescale << WP_TIMER_FREQ_PRESCALE_SHIFT) | (divide - 1));
	wp_wrreg(ess, WPREG_TIMER_ENABLE, 1);
}

static void
wp_starttimer(struct agg_info *ess)
{
	AGG_WR(ess, PORT_INT_STAT, 1, 2);
	AGG_WR(ess, PORT_HOSTINT_CTRL, HOSTINT_CTRL_DSOUND_INT_ENABLED
	       | AGG_RD(ess, PORT_HOSTINT_CTRL, 2), 2);
	wp_wrreg(ess, WPREG_TIMER_START, 1);
}

static void
wp_stoptimer(struct agg_info *ess)
{
	AGG_WR(ess, PORT_HOSTINT_CTRL, ~HOSTINT_CTRL_DSOUND_INT_ENABLED
	       & AGG_RD(ess, PORT_HOSTINT_CTRL, 2), 2);
	AGG_WR(ess, PORT_INT_STAT, 1, 2);
	wp_wrreg(ess, WPREG_TIMER_START, 0);
}

/* -------------------------------------------------------------------- */

/* WaveCache */

#if 0
static u_int16_t
wc_rdreg(struct agg_info *ess, u_int16_t reg)
{
	AGG_WR(ess, PORT_WAVCACHE_INDEX, reg, 2);
	return AGG_RD(ess, PORT_WAVCACHE_DATA, 2);
}
#endif

static void
wc_wrreg(struct agg_info *ess, u_int16_t reg, u_int16_t data)
{
	AGG_WR(ess, PORT_WAVCACHE_INDEX, reg, 2);
	AGG_WR(ess, PORT_WAVCACHE_DATA, data, 2);
}

#if 0
static u_int16_t
wc_rdchctl(struct agg_info *ess, int ch)
{
	return wc_rdreg(ess, ch << 3);
}
#endif

static void
wc_wrchctl(struct agg_info *ess, int ch, u_int16_t data)
{
	wc_wrreg(ess, ch << 3, data);
}

/* -------------------------------------------------------------------- */

/* Power management */
static void
agg_stopclock(struct agg_info *ess, int part, int st)
{
	u_int32_t data;

	data = pci_read_config(ess->dev, CONF_ACPI_STOPCLOCK, 4);
	if (part < 16) {
		if (st == PCI_POWERSTATE_D1)
			data &= ~(1 << part);
		else
			data |= (1 << part);
		if (st == PCI_POWERSTATE_D1 || st == PCI_POWERSTATE_D2)
			data |= (0x10000 << part);
		else
			data &= ~(0x10000 << part);
		pci_write_config(ess->dev, CONF_ACPI_STOPCLOCK, data, 4);
	}
}


/* -----------------------------
 * Controller.
 */

static void
agg_initcodec(struct agg_info* ess)
{
	u_int16_t data;

	if (AGG_RD(ess, PORT_RINGBUS_CTRL, 4) & RINGBUS_CTRL_ACLINK_ENABLED) {
		AGG_WR(ess, PORT_RINGBUS_CTRL, 0, 4);
		DELAY(104);	/* 20.8us * (4 + 1) */
	}
	/* XXX - 2nd codec should be looked at. */
	AGG_WR(ess, PORT_RINGBUS_CTRL, RINGBUS_CTRL_AC97_SWRESET, 4);
	DELAY(2);
	AGG_WR(ess, PORT_RINGBUS_CTRL, RINGBUS_CTRL_ACLINK_ENABLED, 4);
	DELAY(50);

	if (agg_rdcodec(ess, 0) < 0) {
		AGG_WR(ess, PORT_RINGBUS_CTRL, 0, 4);
		DELAY(21);

		/* Try cold reset. */
		device_printf(ess->dev, "will perform cold reset.\n");
		data = AGG_RD(ess, PORT_GPIO_DIR, 2);
		if (pci_read_config(ess->dev, 0x58, 2) & 1)
			data |= 0x10;
		data |= 0x009 & ~AGG_RD(ess, PORT_GPIO_DATA, 2);
		AGG_WR(ess, PORT_GPIO_MASK, 0xff6, 2);
		AGG_WR(ess, PORT_GPIO_DIR, data | 0x009, 2);
		AGG_WR(ess, PORT_GPIO_DATA, 0x000, 2);
		DELAY(2);
		AGG_WR(ess, PORT_GPIO_DATA, 0x001, 2);
		DELAY(1);
		AGG_WR(ess, PORT_GPIO_DATA, 0x009, 2);
		agg_sleep(ess, "agginicd", 500);
		AGG_WR(ess, PORT_GPIO_DIR, data, 2);
		DELAY(84);	/* 20.8us * 4 */
		AGG_WR(ess, PORT_RINGBUS_CTRL, RINGBUS_CTRL_ACLINK_ENABLED, 4);
		DELAY(50);
	}
}

static void
agg_init(struct agg_info* ess)
{
	u_int32_t data;

	/* Setup PCI config registers. */

	/* Disable all legacy emulations. */
	data = pci_read_config(ess->dev, CONF_LEGACY, 2);
	data |= LEGACY_DISABLED;
	pci_write_config(ess->dev, CONF_LEGACY, data, 2);

	/* Disconnect from CHI. (Makes Dell inspiron 7500 work?)
	 * Enable posted write.
	 * Prefer PCI timing rather than that of ISA.
	 * Don't swap L/R. */
	data = pci_read_config(ess->dev, CONF_MAESTRO, 4);
	data |= MAESTRO_PMC;
	data |= MAESTRO_CHIBUS | MAESTRO_POSTEDWRITE | MAESTRO_DMA_PCITIMING;
	data &= ~MAESTRO_SWAP_LR;
	pci_write_config(ess->dev, CONF_MAESTRO, data, 4);

	/* Turn off unused parts if necessary. */
	/* consult CONF_MAESTRO. */
	if (data & MAESTRO_SPDIF)
		agg_stopclock(ess, ACPI_PART_SPDIF,	PCI_POWERSTATE_D2);
	else
		agg_stopclock(ess, ACPI_PART_SPDIF,	PCI_POWERSTATE_D1);
	if (data & MAESTRO_HWVOL)
		agg_stopclock(ess, ACPI_PART_HW_VOL,	PCI_POWERSTATE_D3);
	else
		agg_stopclock(ess, ACPI_PART_HW_VOL,	PCI_POWERSTATE_D1);

	/* parts that never be used */
	agg_stopclock(ess, ACPI_PART_978,	PCI_POWERSTATE_D1);
	agg_stopclock(ess, ACPI_PART_DAA,	PCI_POWERSTATE_D1);
	agg_stopclock(ess, ACPI_PART_GPIO,	PCI_POWERSTATE_D1);
	agg_stopclock(ess, ACPI_PART_SB,	PCI_POWERSTATE_D1);
	agg_stopclock(ess, ACPI_PART_FM,	PCI_POWERSTATE_D1);
	agg_stopclock(ess, ACPI_PART_MIDI,	PCI_POWERSTATE_D1);
	agg_stopclock(ess, ACPI_PART_GAME_PORT,	PCI_POWERSTATE_D1);

	/* parts that will be used only when play/recording */
	agg_stopclock(ess, ACPI_PART_WP,	PCI_POWERSTATE_D2);

	/* parts that should always be turned on */
	agg_stopclock(ess, ACPI_PART_CODEC_CLOCK, PCI_POWERSTATE_D3);
	agg_stopclock(ess, ACPI_PART_GLUE,	PCI_POWERSTATE_D3);
	agg_stopclock(ess, ACPI_PART_PCI_IF,	PCI_POWERSTATE_D3);
	agg_stopclock(ess, ACPI_PART_RINGBUS,	PCI_POWERSTATE_D3);

	/* Reset direct sound. */
	AGG_WR(ess, PORT_HOSTINT_CTRL, HOSTINT_CTRL_SOFT_RESET, 2);
	DELAY(100);
	AGG_WR(ess, PORT_HOSTINT_CTRL, 0, 2);
	DELAY(100);
	AGG_WR(ess, PORT_HOSTINT_CTRL, HOSTINT_CTRL_DSOUND_RESET, 2);
	DELAY(100);
	AGG_WR(ess, PORT_HOSTINT_CTRL, 0, 2);
	DELAY(100);

	/* Enable hardware volume control interruption. */
	if (data & MAESTRO_HWVOL)	/* XXX - why not use device flags? */
		AGG_WR(ess, PORT_HOSTINT_CTRL,HOSTINT_CTRL_HWVOL_ENABLED, 2);

	/* Setup Wave Processor. */

	/* Enable WaveCache, set DMA base address. */
	wp_wrreg(ess, WPREG_WAVE_ROMRAM,
	    WP_WAVE_VIRTUAL_ENABLED | WP_WAVE_DRAM_ENABLED);
	wp_wrreg(ess, WPREG_CRAM_DATA, 0);

	AGG_WR(ess, PORT_WAVCACHE_CTRL,
	       WAVCACHE_ENABLED | WAVCACHE_WTSIZE_2MB | WAVCACHE_SGC_32_47, 2);

	for (data = WAVCACHE_PCMBAR; data < WAVCACHE_PCMBAR + 4; data++)
		wc_wrreg(ess, data, ess->phys >> WAVCACHE_BASEADDR_SHIFT);

	/* Setup Codec/Ringbus. */
	agg_initcodec(ess);
	AGG_WR(ess, PORT_RINGBUS_CTRL,
	       RINGBUS_CTRL_RINGBUS_ENABLED | RINGBUS_CTRL_ACLINK_ENABLED, 4);

	wp_wrreg(ess, 0x08, 0xB004);
	wp_wrreg(ess, 0x09, 0x001B);
	wp_wrreg(ess, 0x0A, 0x8000);
	wp_wrreg(ess, 0x0B, 0x3F37);
	wp_wrreg(ess, WPREG_BASE, 0x8598);	/* Parallel I/O */
	wp_wrreg(ess, WPREG_BASE + 1, 0x7632);
	ringbus_setdest(ess, RINGBUS_SRC_ADC,
	    RINGBUS_DEST_STEREO | RINGBUS_DEST_DSOUND_IN);
	ringbus_setdest(ess, RINGBUS_SRC_DSOUND,
	    RINGBUS_DEST_STEREO | RINGBUS_DEST_DAC);

	/* Enable S/PDIF if necessary. */
	if (pci_read_config(ess->dev, CONF_MAESTRO, 4) & MAESTRO_SPDIF)
		/* XXX - why not use device flags? */
		AGG_WR(ess, PORT_RINGBUS_CTRL_B, RINGBUS_CTRL_SPDIF |
		       AGG_RD(ess, PORT_RINGBUS_CTRL_B, 1), 1);

	/* Setup ASSP. Needed for Dell Inspiron 7500? */
	AGG_WR(ess, PORT_ASSP_CTRL_B, 0x00, 1);
	AGG_WR(ess, PORT_ASSP_CTRL_A, 0x03, 1);
	AGG_WR(ess, PORT_ASSP_CTRL_C, 0x00, 1);

	/*
	 * Setup GPIO.
	 * There seems to be speciality with NEC systems.
	 */
	switch (pci_get_subvendor(ess->dev)
	    | (pci_get_subdevice(ess->dev) << 16)) {
	case NEC_SUBID1:
	case NEC_SUBID2:
		/* Matthew Braithwaite <matt@braithwaite.net> reported that
		 * NEC Versa LX doesn't need GPIO operation. */
		AGG_WR(ess, PORT_GPIO_MASK, 0x9ff, 2);
		AGG_WR(ess, PORT_GPIO_DIR,
		       AGG_RD(ess, PORT_GPIO_DIR, 2) | 0x600, 2);
		AGG_WR(ess, PORT_GPIO_DATA, 0x200, 2);
		break;
	}
}

/* Deals power state transition. Must be called with softc->lock held. */
static void
agg_power(struct agg_info *ess, int status)
{
	u_int8_t lastpwr;

	lastpwr = ess->curpwr;
	if (lastpwr == status)
		return;

	switch (status) {
	case PCI_POWERSTATE_D0:
	case PCI_POWERSTATE_D1:
		switch (lastpwr) {
		case PCI_POWERSTATE_D2:
			pci_set_powerstate(ess->dev, status);
			/* Turn on PCM-related parts. */
			agg_wrcodec(ess, AC97_REG_POWER, 0);
			DELAY(100);
#if 0
			if ((agg_rdcodec(ess, AC97_REG_POWER) & 3) != 3)
				device_printf(ess->dev,
				    "warning: codec not ready.\n");
#endif
			AGG_WR(ess, PORT_RINGBUS_CTRL,
			       (AGG_RD(ess, PORT_RINGBUS_CTRL, 4)
				& ~RINGBUS_CTRL_ACLINK_ENABLED)
			       | RINGBUS_CTRL_RINGBUS_ENABLED, 4);
			DELAY(50);
			AGG_WR(ess, PORT_RINGBUS_CTRL,
			       AGG_RD(ess, PORT_RINGBUS_CTRL, 4)
			       | RINGBUS_CTRL_ACLINK_ENABLED, 4);
			break;
		case PCI_POWERSTATE_D3:
			/* Initialize. */
			pci_set_powerstate(ess->dev, PCI_POWERSTATE_D0);
			DELAY(100);
			agg_init(ess);
			/* FALLTHROUGH */
		case PCI_POWERSTATE_D0:
		case PCI_POWERSTATE_D1:
			pci_set_powerstate(ess->dev, status);
			break;
		}
		break;
	case PCI_POWERSTATE_D2:
		switch (lastpwr) {
		case PCI_POWERSTATE_D3:
			/* Initialize. */
			pci_set_powerstate(ess->dev, PCI_POWERSTATE_D0);
			DELAY(100);
			agg_init(ess);
			/* FALLTHROUGH */
		case PCI_POWERSTATE_D0:
		case PCI_POWERSTATE_D1:
			/* Turn off PCM-related parts. */
			AGG_WR(ess, PORT_RINGBUS_CTRL,
			       AGG_RD(ess, PORT_RINGBUS_CTRL, 4)
			       & ~RINGBUS_CTRL_RINGBUS_ENABLED, 4);
			DELAY(100);
			agg_wrcodec(ess, AC97_REG_POWER, 0x300);
			DELAY(100);
			break;
		}
		pci_set_powerstate(ess->dev, status);
		break;
	case PCI_POWERSTATE_D3:
		/* Entirely power down. */
		agg_wrcodec(ess, AC97_REG_POWER, 0xdf00);
		DELAY(100);
		AGG_WR(ess, PORT_RINGBUS_CTRL, 0, 4);
		/*DELAY(1);*/
		if (lastpwr != PCI_POWERSTATE_D2)
			wp_stoptimer(ess);
		AGG_WR(ess, PORT_HOSTINT_CTRL, 0, 2);
		AGG_WR(ess, PORT_HOSTINT_STAT, 0xff, 1);
		pci_set_powerstate(ess->dev, status);
		break;
	default:
		/* Invalid power state; let it ignored. */
		status = lastpwr;
		break;
	}

	ess->curpwr = status;
}

/* -------------------------------------------------------------------- */

/* Channel controller. */

static void
aggch_start_dac(struct agg_chinfo *ch)
{
	bus_addr_t	wpwa;
	u_int32_t	speed;
	u_int16_t	size, apuch, wtbar, wcreg, aputype;
	u_int		dv;
	int		pan;

	speed = ch->speed;
	wpwa = (ch->phys - ch->base) >> 1;
	wtbar = 0xc & (wpwa >> WPWA_WTBAR_SHIFT(2));
	wcreg = (ch->phys - 16) & WAVCACHE_CHCTL_ADDRTAG_MASK;
	size  = ch->buflen;
	apuch = (ch->num << 1) | 32;
	pan = PAN_RIGHT - PAN_FRONT;

	if (ch->stereo) {
		wcreg |= WAVCACHE_CHCTL_STEREO;
		if (ch->qs16) {
			aputype = APUTYPE_16BITSTEREO;
			wpwa >>= 1;
			size >>= 1;
			pan = -pan;
		} else
			aputype = APUTYPE_8BITSTEREO;
	} else {
		pan = 0;
		if (ch->qs16)
			aputype = APUTYPE_16BITLINEAR;
		else {
			aputype = APUTYPE_8BITLINEAR;
			speed >>= 1;
		}
	}
	if (ch->us)
		wcreg |= WAVCACHE_CHCTL_U8;

	if (wtbar > 8)
		wtbar = (wtbar >> 1) + 4;

	dv = (((speed % 48000) << 16) + 24000) / 48000
	    + ((speed / 48000) << 16);

	agg_lock(ch->parent);
	agg_power(ch->parent, powerstate_active);

	wc_wrreg(ch->parent, WAVCACHE_WTBAR + wtbar,
	    ch->base >> WAVCACHE_BASEADDR_SHIFT);
	wc_wrreg(ch->parent, WAVCACHE_WTBAR + wtbar + 1,
	    ch->base >> WAVCACHE_BASEADDR_SHIFT);
	if (wtbar < 8) {
		wc_wrreg(ch->parent, WAVCACHE_WTBAR + wtbar + 2,
		    ch->base >> WAVCACHE_BASEADDR_SHIFT);
		wc_wrreg(ch->parent, WAVCACHE_WTBAR + wtbar + 3,
		    ch->base >> WAVCACHE_BASEADDR_SHIFT);
	}
	wc_wrchctl(ch->parent, apuch, wcreg);
	wc_wrchctl(ch->parent, apuch + 1, wcreg);

	apu_setparam(ch->parent, apuch, wpwa, size, pan, dv);
	if (ch->stereo) {
		if (ch->qs16)
			wpwa |= (WPWA_STEREO >> 1);
		apu_setparam(ch->parent, apuch + 1, wpwa, size, -pan, dv);

		critical_enter();
		wp_wrapu(ch->parent, apuch, APUREG_APUTYPE,
		    (aputype << APU_APUTYPE_SHIFT) | APU_DMA_ENABLED | 0xf);
		wp_wrapu(ch->parent, apuch + 1, APUREG_APUTYPE,
		    (aputype << APU_APUTYPE_SHIFT) | APU_DMA_ENABLED | 0xf);
		critical_exit();
	} else {
		wp_wrapu(ch->parent, apuch, APUREG_APUTYPE,
		    (aputype << APU_APUTYPE_SHIFT) | APU_DMA_ENABLED | 0xf);
	}

	/* to mark that this channel is ready for intr. */
	ch->parent->active |= (1 << ch->num);

	set_timer(ch->parent);
	wp_starttimer(ch->parent);
	agg_unlock(ch->parent);
}

static void
aggch_stop_dac(struct agg_chinfo *ch)
{
	agg_lock(ch->parent);

	/* to mark that this channel no longer needs further intrs. */
	ch->parent->active &= ~(1 << ch->num);

	wp_wrapu(ch->parent, (ch->num << 1) | 32, APUREG_APUTYPE,
	    APUTYPE_INACTIVE << APU_APUTYPE_SHIFT);
	wp_wrapu(ch->parent, (ch->num << 1) | 33, APUREG_APUTYPE,
	    APUTYPE_INACTIVE << APU_APUTYPE_SHIFT);

	if (ch->parent->active) {
		set_timer(ch->parent);
		wp_starttimer(ch->parent);
	} else {
		wp_stoptimer(ch->parent);
		agg_power(ch->parent, powerstate_idle);
	}
	agg_unlock(ch->parent);
}

static void
aggch_start_adc(struct agg_rchinfo *ch)
{
	bus_addr_t	wpwa, wpwa2;
	u_int16_t	wcreg, wcreg2;
	u_int	dv;
	int	pan;

	/* speed > 48000 not cared */
	dv = ((ch->speed << 16) + 24000) / 48000;

	/* RATECONV doesn't seem to like dv == 0x10000. */
	if (dv == 0x10000)
		dv--;

	if (ch->stereo) {
		wpwa = (ch->srcphys - ch->base) >> 1;
		wpwa2 = (ch->srcphys + ch->parent->bufsz/2 - ch->base) >> 1;
		wcreg = (ch->srcphys - 16) & WAVCACHE_CHCTL_ADDRTAG_MASK;
		wcreg2 = (ch->base - 16) & WAVCACHE_CHCTL_ADDRTAG_MASK;
		pan = PAN_LEFT - PAN_FRONT;
	} else {
		wpwa = (ch->phys - ch->base) >> 1;
		wpwa2 = (ch->srcphys - ch->base) >> 1;
		wcreg = (ch->phys - 16) & WAVCACHE_CHCTL_ADDRTAG_MASK;
		wcreg2 = (ch->base - 16) & WAVCACHE_CHCTL_ADDRTAG_MASK;
		pan = 0;
	}

	agg_lock(ch->parent);

	ch->hwptr = 0;
	agg_power(ch->parent, powerstate_active);

	/* Invalidate WaveCache. */
	wc_wrchctl(ch->parent, 0, wcreg | WAVCACHE_CHCTL_STEREO);
	wc_wrchctl(ch->parent, 1, wcreg | WAVCACHE_CHCTL_STEREO);
	wc_wrchctl(ch->parent, 2, wcreg2 | WAVCACHE_CHCTL_STEREO);
	wc_wrchctl(ch->parent, 3, wcreg2 | WAVCACHE_CHCTL_STEREO);

	/* Load APU registers. */
	/* APU #0 : Sample rate converter for left/center. */
	apu_setparam(ch->parent, 0, WPWA_USE_SYSMEM | wpwa,
		     ch->buflen >> ch->stereo, 0, dv);
	wp_wrapu(ch->parent, 0, APUREG_AMPLITUDE, 0);
	wp_wrapu(ch->parent, 0, APUREG_ROUTING, 2 << APU_DATASRC_A_SHIFT);

	/* APU #1 : Sample rate converter for right. */
	apu_setparam(ch->parent, 1, WPWA_USE_SYSMEM | wpwa2,
		     ch->buflen >> ch->stereo, 0, dv);
	wp_wrapu(ch->parent, 1, APUREG_AMPLITUDE, 0);
	wp_wrapu(ch->parent, 1, APUREG_ROUTING, 3 << APU_DATASRC_A_SHIFT);

	/* APU #2 : Input mixer for left. */
	apu_setparam(ch->parent, 2, WPWA_USE_SYSMEM | 0,
		     ch->parent->bufsz >> 2, pan, 0x10000);
	wp_wrapu(ch->parent, 2, APUREG_AMPLITUDE, 0);
	wp_wrapu(ch->parent, 2, APUREG_EFFECT_GAIN, 0xf0);
	wp_wrapu(ch->parent, 2, APUREG_ROUTING, 0x15 << APU_DATASRC_A_SHIFT);

	/* APU #3 : Input mixer for right. */
	apu_setparam(ch->parent, 3, WPWA_USE_SYSMEM | (ch->parent->bufsz >> 2),
		     ch->parent->bufsz >> 2, -pan, 0x10000);
	wp_wrapu(ch->parent, 3, APUREG_AMPLITUDE, 0);
	wp_wrapu(ch->parent, 3, APUREG_EFFECT_GAIN, 0xf0);
	wp_wrapu(ch->parent, 3, APUREG_ROUTING, 0x14 << APU_DATASRC_A_SHIFT);

	/* to mark this channel ready for intr. */
	ch->parent->active |= (1 << ch->parent->playchns);

	/* start adc */
	critical_enter();
	wp_wrapu(ch->parent, 0, APUREG_APUTYPE,
	    (APUTYPE_RATECONV << APU_APUTYPE_SHIFT) | APU_DMA_ENABLED | 0xf);
	wp_wrapu(ch->parent, 1, APUREG_APUTYPE,
	    (APUTYPE_RATECONV << APU_APUTYPE_SHIFT) | APU_DMA_ENABLED | 0xf);
	wp_wrapu(ch->parent, 2, APUREG_APUTYPE,
	    (APUTYPE_INPUTMIXER << APU_APUTYPE_SHIFT) | 0xf);
	wp_wrapu(ch->parent, 3, APUREG_APUTYPE,
	    (APUTYPE_INPUTMIXER << APU_APUTYPE_SHIFT) | 0xf);
	critical_exit();

	set_timer(ch->parent);
	wp_starttimer(ch->parent);
	agg_unlock(ch->parent);
}

static void
aggch_stop_adc(struct agg_rchinfo *ch)
{
	int apuch;

	agg_lock(ch->parent);

	/* to mark that this channel no longer needs further intrs. */
	ch->parent->active &= ~(1 << ch->parent->playchns);

	for (apuch = 0; apuch < 4; apuch++)
		wp_wrapu(ch->parent, apuch, APUREG_APUTYPE,
		    APUTYPE_INACTIVE << APU_APUTYPE_SHIFT);

	if (ch->parent->active) {
		set_timer(ch->parent);
		wp_starttimer(ch->parent);
	} else {
		wp_stoptimer(ch->parent);
		agg_power(ch->parent, powerstate_idle);
	}
	agg_unlock(ch->parent);
}

/*
 * Feed from L/R channel of ADC to destination with stereo interleaving.
 * This function expects n not overwrapping the buffer boundary.
 * Note that n is measured in sample unit.
 *
 * XXX - this function works in 16bit stereo format only.
 */
static void
interleave(int16_t *l, int16_t *r, int16_t *p, unsigned n)
{
	int16_t *end;

	for (end = l + n; l < end; ) {
		*p++ = *l++;
		*p++ = *r++;
	}
}

static void
aggch_feed_adc_stereo(struct agg_rchinfo *ch)
{
	unsigned cur, last;
	int16_t *src2;

	agg_lock(ch->parent);
	cur = wp_rdapu(ch->parent, 0, APUREG_CURPTR);
	agg_unlock(ch->parent);
	cur -= 0xffff & ((ch->srcphys - ch->base) >> 1);
	last = ch->hwptr;
	src2 = ch->src + ch->parent->bufsz/4;

	if (cur < last) {
		interleave(ch->src + last, src2 + last,
			   ch->sink + 2*last, ch->buflen/2 - last);
		interleave(ch->src, src2,
			   ch->sink, cur);
	} else if (cur > last)
		interleave(ch->src + last, src2 + last,
			   ch->sink + 2*last, cur - last);
	ch->hwptr = cur;
}

/*
 * Feed from R channel of ADC and mixdown to destination L/center.
 * This function expects n not overwrapping the buffer boundary.
 * Note that n is measured in sample unit.
 *
 * XXX - this function works in 16bit monoral format only.
 */
static void
mixdown(int16_t *src, int16_t *dest, unsigned n)
{
	int16_t *end;

	for (end = dest + n; dest < end; dest++)
		*dest = (int16_t)(((int)*dest - (int)*src++) / 2);
}

static void
aggch_feed_adc_mono(struct agg_rchinfo *ch)
{
	unsigned cur, last;

	agg_lock(ch->parent);
	cur = wp_rdapu(ch->parent, 0, APUREG_CURPTR);
	agg_unlock(ch->parent);
	cur -= 0xffff & ((ch->phys - ch->base) >> 1);
	last = ch->hwptr;

	if (cur < last) {
		mixdown(ch->src + last, ch->sink + last, ch->buflen - last);
		mixdown(ch->src, ch->sink, cur);
	} else if (cur > last)
		mixdown(ch->src + last, ch->sink + last, cur - last);
	ch->hwptr = cur;
}

#ifdef AGG_JITTER_CORRECTION
/*
 * Stereo jitter suppressor.
 * Sometimes playback pointers differ in stereo-paired channels.
 * Calling this routine within intr fixes the problem.
 */
static void
suppress_jitter(struct agg_chinfo *ch)
{
	if (ch->stereo) {
		int cp1, cp2, diff /*, halfsize*/ ;

		/*halfsize = (ch->qs16? ch->buflen >> 2 : ch->buflen >> 1);*/
		cp1 = wp_rdapu(ch->parent, (ch->num << 1) | 32, APUREG_CURPTR);
		cp2 = wp_rdapu(ch->parent, (ch->num << 1) | 33, APUREG_CURPTR);
		if (cp1 != cp2) {
			diff = (cp1 > cp2 ? cp1 - cp2 : cp2 - cp1);
			if (diff > 1 /* && diff < halfsize*/ )
				AGG_WR(ch->parent, PORT_DSP_DATA, cp1, 2);
		}
	}
}

static void
suppress_rec_jitter(struct agg_rchinfo *ch)
{
	int cp1, cp2, diff /*, halfsize*/ ;

	/*halfsize = (ch->stereo? ch->buflen >> 2 : ch->buflen >> 1);*/
	cp1 = (ch->stereo? ch->parent->bufsz >> 2 : ch->parent->bufsz >> 1)
		+ wp_rdapu(ch->parent, 0, APUREG_CURPTR);
	cp2 = wp_rdapu(ch->parent, 1, APUREG_CURPTR);
	if (cp1 != cp2) {
		diff = (cp1 > cp2 ? cp1 - cp2 : cp2 - cp1);
		if (diff > 1 /* && diff < halfsize*/ )
			AGG_WR(ch->parent, PORT_DSP_DATA, cp1, 2);
	}
}
#endif

static u_int
calc_timer_div(struct agg_chinfo *ch)
{
	u_int speed;

	speed = ch->speed;
#ifdef INVARIANTS
	if (speed == 0) {
		printf("snd_maestro: pch[%d].speed == 0, which shouldn't\n",
		       ch->num);
		speed = 1;
	}
#endif
	return (48000 * (ch->blklen << (!ch->qs16 + !ch->stereo))
		+ speed - 1) / speed;
}

static u_int
calc_timer_div_rch(struct agg_rchinfo *ch)
{
	u_int speed;

	speed = ch->speed;
#ifdef INVARIANTS
	if (speed == 0) {
		printf("snd_maestro: rch.speed == 0, which shouldn't\n");
		speed = 1;
	}
#endif
	return (48000 * (ch->blklen << (!ch->stereo))
		+ speed - 1) / speed;
}

static void
set_timer(struct agg_info *ess)
{
	int i;
	u_int	dv = 32 << 7, newdv;

	for (i = 0; i < ess->playchns; i++)
		if ((ess->active & (1 << i)) &&
		    (dv > (newdv = calc_timer_div(ess->pch + i))))
			dv = newdv;
	if ((ess->active & (1 << i)) &&
	    (dv > (newdv = calc_timer_div_rch(&ess->rch))))
		dv = newdv;

	wp_settimer(ess, dv);
}


/* -----------------------------
 * Newpcm glue.
 */

/* AC97 mixer interface. */

static u_int32_t
agg_ac97_init(kobj_t obj, void *sc)
{
	struct agg_info *ess = sc;

	return (AGG_RD(ess, PORT_CODEC_STAT, 1) & CODEC_STAT_MASK)? 0 : 1;
}

static int
agg_ac97_read(kobj_t obj, void *sc, int regno)
{
	struct agg_info *ess = sc;
	int ret;

	/* XXX sound locking violation: agg_lock(ess); */
	ret = agg_rdcodec(ess, regno);
	/* agg_unlock(ess); */
	return ret;
}

static int
agg_ac97_write(kobj_t obj, void *sc, int regno, u_int32_t data)
{
	struct agg_info *ess = sc;
	int ret;

	/* XXX sound locking violation: agg_lock(ess); */
	ret = agg_wrcodec(ess, regno, data);
	/* agg_unlock(ess); */
	return ret;
}


static kobj_method_t agg_ac97_methods[] = {
    	KOBJMETHOD(ac97_init,		agg_ac97_init),
    	KOBJMETHOD(ac97_read,		agg_ac97_read),
    	KOBJMETHOD(ac97_write,		agg_ac97_write),
	KOBJMETHOD_END
};
AC97_DECLARE(agg_ac97);


/* -------------------------------------------------------------------- */

/* Playback channel. */

static void *
aggpch_init(kobj_t obj, void *devinfo, struct snd_dbuf *b,
						struct pcm_channel *c, int dir)
{
	struct agg_info *ess = devinfo;
	struct agg_chinfo *ch;
	bus_addr_t physaddr;
	void *p;

	KASSERT((dir == PCMDIR_PLAY),
	    ("aggpch_init() called for RECORDING channel!"));
	ch = ess->pch + ess->playchns;

	ch->parent = ess;
	ch->channel = c;
	ch->buffer = b;
	ch->num = ess->playchns;

	p = dma_malloc(ess->buf_dmat, ess->bufsz, &physaddr, &ch->map);
	if (p == NULL)
		return NULL;
	ch->phys = physaddr;
	ch->base = physaddr & ((~(bus_addr_t)0) << WAVCACHE_BASEADDR_SHIFT);

	sndbuf_setup(b, p, ess->bufsz);
	ch->blklen = sndbuf_getblksz(b) / 2;
	ch->buflen = sndbuf_getsize(b) / 2;
	ess->playchns++;

	return ch;
}

static void
adjust_pchbase(struct agg_chinfo *chans, u_int n, u_int size)
{
	struct agg_chinfo *pchs[AGG_MAXPLAYCH];
	u_int i, j, k;
	bus_addr_t base;

	/* sort pchs by phys address */
	for (i = 0; i < n; i++) {
		for (j = 0; j < i; j++)
			if (chans[i].phys < pchs[j]->phys) {
				for (k = i; k > j; k--)
					pchs[k] = pchs[k - 1];
				break;
			}
		pchs[j] = chans + i;
	}

	/* use new base register if next buffer can not be addressed
	   via current base. */
#define BASE_SHIFT (WPWA_WTBAR_SHIFT(2) + 2 + 1)
	base = pchs[0]->base;
	for (k = 1, i = 1; i < n; i++) {
		if (pchs[i]->phys + size - base >= 1 << BASE_SHIFT)
			/* not addressable: assign new base */
			base = (pchs[i]->base -= k++ << BASE_SHIFT);
		else
			pchs[i]->base = base;
	}
#undef BASE_SHIFT

	if (bootverbose) {
		printf("Total of %d bases are assigned.\n", k);
		for (i = 0; i < n; i++) {
			printf("ch.%d: phys 0x%llx, wpwa 0x%llx\n",
			       i, (long long)chans[i].phys,
			       (long long)(chans[i].phys -
					   chans[i].base) >> 1);
		}
	}
}

static int
aggpch_free(kobj_t obj, void *data)
{
	struct agg_chinfo *ch = data;
	struct agg_info *ess = ch->parent;

	/* free up buffer - called after channel stopped */
	dma_free(ess->buf_dmat, sndbuf_getbuf(ch->buffer), ch->map);

	/* return 0 if ok */
	return 0;
}

static int
aggpch_setformat(kobj_t obj, void *data, u_int32_t format)
{
	struct agg_chinfo *ch = data;

	if (format & AFMT_BIGENDIAN || format & AFMT_U16_LE)
		return EINVAL;
	ch->stereo = ch->qs16 = ch->us = 0;
	if (AFMT_CHANNEL(format) > 1)
		ch->stereo = 1;

	if (format & AFMT_U8 || format & AFMT_S8) {
		if (format & AFMT_U8)
			ch->us = 1;
	} else
		ch->qs16 = 1;
	return 0;
}

static u_int32_t
aggpch_setspeed(kobj_t obj, void *data, u_int32_t speed)
{

	((struct agg_chinfo*)data)->speed = speed;

	return (speed);
}

static u_int32_t
aggpch_setblocksize(kobj_t obj, void *data, u_int32_t blocksize)
{
	struct agg_chinfo *ch = data;
	int blkcnt;

	/* try to keep at least 20msec DMA space */
	blkcnt = (ch->speed << (ch->stereo + ch->qs16)) / (50 * blocksize);
	RANGE(blkcnt, 2, ch->parent->bufsz / blocksize);

	if (sndbuf_getsize(ch->buffer) != blkcnt * blocksize) {
		sndbuf_resize(ch->buffer, blkcnt, blocksize);
		blkcnt = sndbuf_getblkcnt(ch->buffer);
		blocksize = sndbuf_getblksz(ch->buffer);
	} else {
		sndbuf_setblkcnt(ch->buffer, blkcnt);
		sndbuf_setblksz(ch->buffer, blocksize);
	}

	ch->blklen = blocksize / 2;
	ch->buflen = blkcnt * blocksize / 2;
	return blocksize;
}

static int
aggpch_trigger(kobj_t obj, void *data, int go)
{
	struct agg_chinfo *ch = data;

	switch (go) {
	case PCMTRIG_EMLDMAWR:
		break;
	case PCMTRIG_START:
		aggch_start_dac(ch);
		break;
	case PCMTRIG_ABORT:
	case PCMTRIG_STOP:
		aggch_stop_dac(ch);
		break;
	}
	return 0;
}

static u_int32_t
aggpch_getptr(kobj_t obj, void *data)
{
	struct agg_chinfo *ch = data;
	u_int32_t cp;

	agg_lock(ch->parent);
	cp = wp_rdapu(ch->parent, (ch->num << 1) | 32, APUREG_CURPTR);
	agg_unlock(ch->parent);

	return ch->qs16 && ch->stereo
		? (cp << 2) - ((0xffff << 2) & (ch->phys - ch->base))
		: (cp << 1) - ((0xffff << 1) & (ch->phys - ch->base));
}

static struct pcmchan_caps *
aggpch_getcaps(kobj_t obj, void *data)
{
	static u_int32_t playfmt[] = {
		SND_FORMAT(AFMT_U8, 1, 0),
		SND_FORMAT(AFMT_U8, 2, 0),
		SND_FORMAT(AFMT_S8, 1, 0),
		SND_FORMAT(AFMT_S8, 2, 0),
		SND_FORMAT(AFMT_S16_LE, 1, 0),
		SND_FORMAT(AFMT_S16_LE, 2, 0),
		0
	};
	static struct pcmchan_caps playcaps = {8000, 48000, playfmt, 0};

	return &playcaps;
}


static kobj_method_t aggpch_methods[] = {
    	KOBJMETHOD(channel_init,		aggpch_init),
    	KOBJMETHOD(channel_free,		aggpch_free),
    	KOBJMETHOD(channel_setformat,		aggpch_setformat),
    	KOBJMETHOD(channel_setspeed,		aggpch_setspeed),
    	KOBJMETHOD(channel_setblocksize,	aggpch_setblocksize),
    	KOBJMETHOD(channel_trigger,		aggpch_trigger),
    	KOBJMETHOD(channel_getptr,		aggpch_getptr),
    	KOBJMETHOD(channel_getcaps,		aggpch_getcaps),
	KOBJMETHOD_END
};
CHANNEL_DECLARE(aggpch);


/* -------------------------------------------------------------------- */

/* Recording channel. */

static void *
aggrch_init(kobj_t obj, void *devinfo, struct snd_dbuf *b,
						struct pcm_channel *c, int dir)
{
	struct agg_info *ess = devinfo;
	struct agg_rchinfo *ch;
	u_int8_t *p;

	KASSERT((dir == PCMDIR_REC),
	    ("aggrch_init() called for PLAYBACK channel!"));
	ch = &ess->rch;

	ch->parent = ess;
	ch->channel = c;
	ch->buffer = b;

	/* Uses the bottom-half of the status buffer. */
	p        = ess->stat + ess->bufsz;
	ch->phys = ess->phys + ess->bufsz;
	ch->base = ess->phys;
	ch->src  = (int16_t *)(p + ess->bufsz);
	ch->srcphys = ch->phys + ess->bufsz;
	ch->sink = (int16_t *)p;

	sndbuf_setup(b, p, ess->bufsz);
	ch->blklen = sndbuf_getblksz(b) / 2;
	ch->buflen = sndbuf_getsize(b) / 2;

	return ch;
}

static int
aggrch_setformat(kobj_t obj, void *data, u_int32_t format)
{
	struct agg_rchinfo *ch = data;

	if (!(format & AFMT_S16_LE))
		return EINVAL;
	if (AFMT_CHANNEL(format) > 1)
		ch->stereo = 1;
	else
		ch->stereo = 0;
	return 0;
}

static u_int32_t
aggrch_setspeed(kobj_t obj, void *data, u_int32_t speed)
{

	((struct agg_rchinfo*)data)->speed = speed;

	return (speed);
}

static u_int32_t
aggrch_setblocksize(kobj_t obj, void *data, u_int32_t blocksize)
{
	struct agg_rchinfo *ch = data;
	int blkcnt;

	/* try to keep at least 20msec DMA space */
	blkcnt = (ch->speed << ch->stereo) / (25 * blocksize);
	RANGE(blkcnt, 2, ch->parent->bufsz / blocksize);

	if (sndbuf_getsize(ch->buffer) != blkcnt * blocksize) {
		sndbuf_resize(ch->buffer, blkcnt, blocksize);
		blkcnt = sndbuf_getblkcnt(ch->buffer);
		blocksize = sndbuf_getblksz(ch->buffer);
	} else {
		sndbuf_setblkcnt(ch->buffer, blkcnt);
		sndbuf_setblksz(ch->buffer, blocksize);
	}

	ch->blklen = blocksize / 2;
	ch->buflen = blkcnt * blocksize / 2;
	return blocksize;
}

static int
aggrch_trigger(kobj_t obj, void *sc, int go)
{
	struct agg_rchinfo *ch = sc;

	switch (go) {
	case PCMTRIG_EMLDMARD:
		if (ch->stereo)
			aggch_feed_adc_stereo(ch);
		else
			aggch_feed_adc_mono(ch);
		break;
	case PCMTRIG_START:
		aggch_start_adc(ch);
		break;
	case PCMTRIG_ABORT:
	case PCMTRIG_STOP:
		aggch_stop_adc(ch);
		break;
	}
	return 0;
}

static u_int32_t
aggrch_getptr(kobj_t obj, void *sc)
{
	struct agg_rchinfo *ch = sc;

	return ch->stereo? ch->hwptr << 2 : ch->hwptr << 1;
}

static struct pcmchan_caps *
aggrch_getcaps(kobj_t obj, void *sc)
{
	static u_int32_t recfmt[] = {
		SND_FORMAT(AFMT_S16_LE, 1, 0),
		SND_FORMAT(AFMT_S16_LE, 2, 0),
		0
	};
	static struct pcmchan_caps reccaps = {8000, 48000, recfmt, 0};

	return &reccaps;
}

static kobj_method_t aggrch_methods[] = {
	KOBJMETHOD(channel_init,		aggrch_init),
	/* channel_free: no-op */
	KOBJMETHOD(channel_setformat,		aggrch_setformat),
	KOBJMETHOD(channel_setspeed,		aggrch_setspeed),
	KOBJMETHOD(channel_setblocksize,	aggrch_setblocksize),
	KOBJMETHOD(channel_trigger,		aggrch_trigger),
	KOBJMETHOD(channel_getptr,		aggrch_getptr),
	KOBJMETHOD(channel_getcaps,		aggrch_getcaps),
	KOBJMETHOD_END
};
CHANNEL_DECLARE(aggrch);


/* -----------------------------
 * Bus space.
 */

static void
agg_intr(void *sc)
{
	struct agg_info* ess = sc;
	register u_int8_t status;
	int i;
	u_int m;

	status = AGG_RD(ess, PORT_HOSTINT_STAT, 1);
	if (!status)
		return;

	/* Acknowledge intr. */
	AGG_WR(ess, PORT_HOSTINT_STAT, status, 1);

	if (status & HOSTINT_STAT_DSOUND) {
#ifdef AGG_JITTER_CORRECTION
		agg_lock(ess);
#endif
		if (ess->curpwr <= PCI_POWERSTATE_D1) {
			AGG_WR(ess, PORT_INT_STAT, 1, 2);
#ifdef AGG_JITTER_CORRECTION
			for (i = 0, m = 1; i < ess->playchns; i++, m <<= 1) {
				if (ess->active & m)
					suppress_jitter(ess->pch + i);
			}
			if (ess->active & m)
				suppress_rec_jitter(&ess->rch);
			agg_unlock(ess);
#endif
			for (i = 0, m = 1; i < ess->playchns; i++, m <<= 1) {
				if (ess->active & m) {
					if (ess->curpwr <= PCI_POWERSTATE_D1)
						chn_intr(ess->pch[i].channel);
					else {
						m = 0;
						break;
					}
				}
			}
			if ((ess->active & m)
			    && ess->curpwr <= PCI_POWERSTATE_D1)
				chn_intr(ess->rch.channel);
		}
#ifdef AGG_JITTER_CORRECTION
		else
			agg_unlock(ess);
#endif
	}

	if (status & HOSTINT_STAT_HWVOL) {
		register u_int8_t event;

		agg_lock(ess);
		event = AGG_RD(ess, PORT_HWVOL_MASTER, 1);
		AGG_WR(ess, PORT_HWVOL_MASTER, HWVOL_NOP, 1);
		agg_unlock(ess);

		switch (event) {
		case HWVOL_UP:
			mixer_hwvol_step(ess->dev, 1, 1);
			break;
		case HWVOL_DOWN:
			mixer_hwvol_step(ess->dev, -1, -1);
			break;
		case HWVOL_NOP:
			break;
		default:
			if (event & HWVOL_MUTE) {
				mixer_hwvol_mute(ess->dev);
				break;
			}
			device_printf(ess->dev,
				      "%s: unknown HWVOL event 0x%x\n",
				      device_get_nameunit(ess->dev), event);
		}
	}
}

static void
setmap(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	bus_addr_t *phys = arg;

	*phys = error? 0 : segs->ds_addr;

	if (bootverbose) {
		printf("setmap (%lx, %lx), nseg=%d, error=%d\n",
		    (unsigned long)segs->ds_addr, (unsigned long)segs->ds_len,
		    nseg, error);
	}
}

static void *
dma_malloc(bus_dma_tag_t dmat, u_int32_t sz, bus_addr_t *phys,
    bus_dmamap_t *map)
{
	void *buf;

	if (bus_dmamem_alloc(dmat, &buf, BUS_DMA_NOWAIT, map))
		return NULL;
	if (bus_dmamap_load(dmat, *map, buf, sz, setmap, phys, 0) != 0 ||
	    *phys == 0) {
		bus_dmamem_free(dmat, buf, *map);
		return NULL;
	}
	return buf;
}

static void
dma_free(bus_dma_tag_t dmat, void *buf, bus_dmamap_t map)
{
	bus_dmamap_unload(dmat, map);
	bus_dmamem_free(dmat, buf, map);
}

static int
agg_probe(device_t dev)
{
	char *s = NULL;

	switch (pci_get_devid(dev)) {
	case MAESTRO_1_PCI_ID:
		s = "ESS Technology Maestro-1";
		break;

	case MAESTRO_2_PCI_ID:
		s = "ESS Technology Maestro-2";
		break;

	case MAESTRO_2E_PCI_ID:
		s = "ESS Technology Maestro-2E";
		break;
	}

	if (s != NULL && pci_get_class(dev) == PCIC_MULTIMEDIA) {
		device_set_desc(dev, s);
		return BUS_PROBE_DEFAULT;
	}
	return ENXIO;
}

static int
agg_attach(device_t dev)
{
	struct agg_info	*ess = NULL;
	u_int32_t	data;
	int	regid = PCIR_BAR(0);
	struct resource	*reg = NULL;
	struct ac97_info	*codec = NULL;
	int	irqid = 0;
	struct resource	*irq = NULL;
	void	*ih = NULL;
	char	status[SND_STATUSLEN];
	int	dacn, ret = 0;

	ess = malloc(sizeof(*ess), M_DEVBUF, M_WAITOK | M_ZERO);
	ess->dev = dev;

	mtx_init(&ess->lock, device_get_desc(dev), "snd_maestro softc",
		 MTX_DEF | MTX_RECURSE);
	if (!mtx_initialized(&ess->lock)) {
		device_printf(dev, "failed to create a mutex.\n");
		ret = ENOMEM;
		goto bad;
	}

	if (resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "dac", &dacn) == 0) {
	    	if (dacn < 1)
			dacn = 1;
		else if (dacn > AGG_MAXPLAYCH)
			dacn = AGG_MAXPLAYCH;
	} else
		dacn = AGG_MAXPLAYCH;

	ess->bufsz = pcm_getbuffersize(dev, 4096, AGG_DEFAULT_BUFSZ, 65536);
	if (bus_dma_tag_create(/*parent*/ bus_get_dma_tag(dev),
			       /*align */ 4, 1 << (16+1),
			       /*limit */ MAESTRO_MAXADDR, BUS_SPACE_MAXADDR,
			       /*filter*/ NULL, NULL,
			       /*size  */ ess->bufsz, 1, 0x3ffff,
			       /*flags */ 0,
			       /*lock  */ busdma_lock_mutex, &Giant,
			       &ess->buf_dmat) != 0) {
		device_printf(dev, "unable to create dma tag\n");
		ret = ENOMEM;
		goto bad;
	}

	if (bus_dma_tag_create(/*parent*/ bus_get_dma_tag(dev),
			       /*align */ 1 << WAVCACHE_BASEADDR_SHIFT,
			                  1 << (16+1),
			       /*limit */ MAESTRO_MAXADDR, BUS_SPACE_MAXADDR,
			       /*filter*/ NULL, NULL,
			       /*size  */ 3*ess->bufsz, 1, 0x3ffff,
			       /*flags */ 0,
			       /*lock  */ busdma_lock_mutex, &Giant,
			       &ess->stat_dmat) != 0) {
		device_printf(dev, "unable to create dma tag\n");
		ret = ENOMEM;
		goto bad;
	}

	/* Allocate the room for brain-damaging status buffer. */
	ess->stat = dma_malloc(ess->stat_dmat, 3*ess->bufsz, &ess->phys,
	    &ess->stat_map);
	if (ess->stat == NULL) {
		device_printf(dev, "cannot allocate status buffer\n");
		ret = ENOMEM;
		goto bad;
	}
	if (bootverbose)
		device_printf(dev, "Maestro status/record buffer: %#llx\n",
		    (long long)ess->phys);

	/* State D0-uninitialized. */
	ess->curpwr = PCI_POWERSTATE_D3;
	pci_set_powerstate(dev, PCI_POWERSTATE_D0);

	pci_enable_busmaster(dev);

	/* Allocate resources. */
	reg = bus_alloc_resource_any(dev, SYS_RES_IOPORT, &regid, RF_ACTIVE);
	if (reg != NULL) {
		ess->reg = reg;
		ess->regid = regid;
		ess->st = rman_get_bustag(reg);
		ess->sh = rman_get_bushandle(reg);
	} else {
		device_printf(dev, "unable to map register space\n");
		ret = ENXIO;
		goto bad;
	}
	irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &irqid,
	    RF_ACTIVE | RF_SHAREABLE);
	if (irq != NULL) {
		ess->irq = irq;
		ess->irqid = irqid;
	} else {
		device_printf(dev, "unable to map interrupt\n");
		ret = ENXIO;
		goto bad;
	}

	/* Setup resources. */
	if (snd_setup_intr(dev, irq, INTR_MPSAFE, agg_intr, ess, &ih)) {
		device_printf(dev, "unable to setup interrupt\n");
		ret = ENXIO;
		goto bad;
	} else
		ess->ih = ih;

	/* Transition from D0-uninitialized to D0. */
	agg_lock(ess);
	agg_power(ess, PCI_POWERSTATE_D0);
	if (agg_rdcodec(ess, 0) == 0x80) {
		/* XXX - TODO: PT101 */
		agg_unlock(ess);
		device_printf(dev, "PT101 codec detected!\n");
		ret = ENXIO;
		goto bad;
	}
	agg_unlock(ess);
	codec = AC97_CREATE(dev, ess, agg_ac97);
	if (codec == NULL) {
		device_printf(dev, "failed to create AC97 codec softc!\n");
		ret = ENOMEM;
		goto bad;
	}
	if (mixer_init(dev, ac97_getmixerclass(), codec) == -1) {
		device_printf(dev, "mixer initialization failed!\n");
		ret = ENXIO;
		goto bad;
	}
	ess->codec = codec;

	ret = pcm_register(dev, ess, dacn, 1);
	if (ret)
		goto bad;

	mixer_hwvol_init(dev);
	agg_lock(ess);
	agg_power(ess, powerstate_init);
	agg_unlock(ess);
	for (data = 0; data < dacn; data++)
		pcm_addchan(dev, PCMDIR_PLAY, &aggpch_class, ess);
	pcm_addchan(dev, PCMDIR_REC, &aggrch_class, ess);
	adjust_pchbase(ess->pch, ess->playchns, ess->bufsz);

	snprintf(status, SND_STATUSLEN,
	    "port 0x%jx-0x%jx irq %jd at device %d.%d on pci%d",
	    rman_get_start(reg), rman_get_end(reg), rman_get_start(irq),
	    pci_get_slot(dev), pci_get_function(dev), pci_get_bus(dev));
	pcm_setstatus(dev, status);

	return 0;

 bad:
	if (codec != NULL)
		ac97_destroy(codec);
	if (ih != NULL)
		bus_teardown_intr(dev, irq, ih);
	if (irq != NULL)
		bus_release_resource(dev, SYS_RES_IRQ, irqid, irq);
	if (reg != NULL)
		bus_release_resource(dev, SYS_RES_IOPORT, regid, reg);
	if (ess != NULL) {
		if (ess->stat != NULL)
			dma_free(ess->stat_dmat, ess->stat, ess->stat_map);
		if (ess->stat_dmat != NULL)
			bus_dma_tag_destroy(ess->stat_dmat);
		if (ess->buf_dmat != NULL)
			bus_dma_tag_destroy(ess->buf_dmat);
		if (mtx_initialized(&ess->lock))
			mtx_destroy(&ess->lock);
		free(ess, M_DEVBUF);
	}

	return ret;
}

static int
agg_detach(device_t dev)
{
	struct agg_info	*ess = pcm_getdevinfo(dev);
	int r;
	u_int16_t icr;

	icr = AGG_RD(ess, PORT_HOSTINT_CTRL, 2);
	AGG_WR(ess, PORT_HOSTINT_CTRL, 0, 2);

	agg_lock(ess);
	if (ess->active) {
		AGG_WR(ess, PORT_HOSTINT_CTRL, icr, 2);
		agg_unlock(ess);
		return EBUSY;
	}
	agg_unlock(ess);

	r = pcm_unregister(dev);
	if (r) {
		AGG_WR(ess, PORT_HOSTINT_CTRL, icr, 2);
		return r;
	}

	agg_lock(ess);
	agg_power(ess, PCI_POWERSTATE_D3);
	agg_unlock(ess);

	bus_teardown_intr(dev, ess->irq, ess->ih);
	bus_release_resource(dev, SYS_RES_IRQ, ess->irqid, ess->irq);
	bus_release_resource(dev, SYS_RES_IOPORT, ess->regid, ess->reg);
	dma_free(ess->stat_dmat, ess->stat, ess->stat_map);
	bus_dma_tag_destroy(ess->stat_dmat);
	bus_dma_tag_destroy(ess->buf_dmat);
	mtx_destroy(&ess->lock);
	free(ess, M_DEVBUF);
	return 0;
}

static int
agg_suspend(device_t dev)
{
	struct agg_info *ess = pcm_getdevinfo(dev);

	AGG_WR(ess, PORT_HOSTINT_CTRL, 0, 2);
	agg_lock(ess);
	agg_power(ess, PCI_POWERSTATE_D3);
	agg_unlock(ess);

	return 0;
}

static int
agg_resume(device_t dev)
{
	int i;
	struct agg_info *ess = pcm_getdevinfo(dev);

	for (i = 0; i < ess->playchns; i++)
		if (ess->active & (1 << i))
			aggch_start_dac(ess->pch + i);
	if (ess->active & (1 << i))
		aggch_start_adc(&ess->rch);

	agg_lock(ess);
	if (!ess->active)
		agg_power(ess, powerstate_init);
	agg_unlock(ess);

	if (mixer_reinit(dev)) {
		device_printf(dev, "unable to reinitialize the mixer\n");
		return ENXIO;
	}

	return 0;
}

static int
agg_shutdown(device_t dev)
{
	struct agg_info *ess = pcm_getdevinfo(dev);

	agg_lock(ess);
	agg_power(ess, PCI_POWERSTATE_D3);
	agg_unlock(ess);

	return 0;
}


static device_method_t agg_methods[] = {
    DEVMETHOD(device_probe,	agg_probe),
    DEVMETHOD(device_attach,	agg_attach),
    DEVMETHOD(device_detach,	agg_detach),
    DEVMETHOD(device_suspend,	agg_suspend),
    DEVMETHOD(device_resume,	agg_resume),
    DEVMETHOD(device_shutdown,	agg_shutdown),

    { 0, 0 }
};

static driver_t agg_driver = {
    "pcm",
    agg_methods,
    PCM_SOFTC_SIZE,
};

/*static devclass_t pcm_devclass;*/

DRIVER_MODULE(snd_maestro, pci, agg_driver, pcm_devclass, 0, 0);
MODULE_DEPEND(snd_maestro, sound, SOUND_MINVER, SOUND_PREFVER, SOUND_MAXVER);
MODULE_VERSION(snd_maestro, 1);
