/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Katsurajima Naoto <raven@katsurajima.seya.yokohama.jp>
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
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHERIN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/pcm/sound.h>
#include <dev/sound/pcm/ac97.h>
#include <dev/sound/pci/spicds.h>
#include <dev/sound/pci/envy24.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include "mixer_if.h"

SND_DECLARE_FILE("$FreeBSD$");

static MALLOC_DEFINE(M_ENVY24, "envy24", "envy24 audio");

/* -------------------------------------------------------------------- */

struct sc_info;

#define ENVY24_PLAY_CHNUM 10
#define ENVY24_REC_CHNUM 12
#define ENVY24_PLAY_BUFUNIT (4 /* byte/sample */ * 10 /* channel */)
#define ENVY24_REC_BUFUNIT  (4 /* byte/sample */ * 12 /* channel */)
#define ENVY24_SAMPLE_NUM   4096

#define ENVY24_TIMEOUT 1000

#define ENVY24_DEFAULT_FORMAT	SND_FORMAT(AFMT_S16_LE, 2, 0)

#define ENVY24_NAMELEN 32

#define SDA_GPIO 0x10
#define SCL_GPIO 0x20

struct envy24_sample {
        volatile u_int32_t buffer;
};

typedef struct envy24_sample sample32_t;

/* channel registers */
struct sc_chinfo {
	struct snd_dbuf		*buffer;
	struct pcm_channel	*channel;
	struct sc_info		*parent;
	int			dir;
	unsigned		num; /* hw channel number */

	/* channel information */
	u_int32_t		format;
	u_int32_t		speed;
	u_int32_t		blk; /* hw block size(dword) */

	/* format conversion structure */
	u_int8_t		*data;
	unsigned int		size; /* data buffer size(byte) */
	int			unit; /* sample size(byte) */
	unsigned int		offset; /* samples number offset */
	void			(*emldma)(struct sc_chinfo *);

	/* flags */
	int			run;
};

/* codec interface entrys */
struct codec_entry {
	void *(*create)(device_t dev, void *devinfo, int dir, int num);
	void (*destroy)(void *codec);
	void (*init)(void *codec);
	void (*reinit)(void *codec);
	void (*setvolume)(void *codec, int dir, unsigned int left, unsigned int right);
	void (*setrate)(void *codec, int which, int rate);
};

/* system configuration information */
struct cfg_info {
	char *name;
	u_int16_t subvendor, subdevice;
	u_int8_t scfg, acl, i2s, spdif;
	u_int8_t gpiomask, gpiostate, gpiodir;
	u_int8_t cdti, cclk, cs, cif, type;
	u_int8_t free;
	struct codec_entry *codec;
};

/* device private data */
struct sc_info {
	device_t	dev;
	struct mtx	*lock;

	/* Control/Status registor */
	struct resource *cs;
	int		csid;
	bus_space_tag_t cst;
	bus_space_handle_t csh;
	/* DDMA registor */
	struct resource *ddma;
	int		ddmaid;
	bus_space_tag_t ddmat;
	bus_space_handle_t ddmah;
	/* Consumer Section DMA Channel Registers */
	struct resource *ds;
	int		dsid;
	bus_space_tag_t dst;
	bus_space_handle_t dsh;
	/* MultiTrack registor */
	struct resource *mt;
	int		mtid;
	bus_space_tag_t mtt;
	bus_space_handle_t mth;
	/* DMA tag */
	bus_dma_tag_t dmat;
	/* IRQ resource */
	struct resource *irq;
	int		irqid;
	void		*ih;

	/* system configuration data */
	struct cfg_info *cfg;

	/* ADC/DAC number and info */
	int		adcn, dacn;
	void		*adc[4], *dac[4];

	/* mixer control data */
	u_int32_t	src;
	u_int8_t	left[ENVY24_CHAN_NUM];
	u_int8_t	right[ENVY24_CHAN_NUM];

	/* Play/Record DMA fifo */
	sample32_t	*pbuf;
	sample32_t	*rbuf;
	u_int32_t	psize, rsize; /* DMA buffer size(byte) */
	u_int16_t	blk[2]; /* transfer check blocksize(dword) */
	bus_dmamap_t	pmap, rmap;
	bus_addr_t	paddr, raddr;

	/* current status */
	u_int32_t	speed;
	int		run[2];
	u_int16_t	intr[2];
	struct pcmchan_caps	caps[2];

	/* channel info table */
	unsigned	chnum;
	struct sc_chinfo chan[11];
};

/* -------------------------------------------------------------------- */

/*
 * prototypes
 */

/* DMA emulator */
static void envy24_p8u(struct sc_chinfo *);
static void envy24_p16sl(struct sc_chinfo *);
static void envy24_p32sl(struct sc_chinfo *);
static void envy24_r16sl(struct sc_chinfo *);
static void envy24_r32sl(struct sc_chinfo *);

/* channel interface */
static void *envy24chan_init(kobj_t, void *, struct snd_dbuf *, struct pcm_channel *, int);
static int envy24chan_setformat(kobj_t, void *, u_int32_t);
static u_int32_t envy24chan_setspeed(kobj_t, void *, u_int32_t);
static u_int32_t envy24chan_setblocksize(kobj_t, void *, u_int32_t);
static int envy24chan_trigger(kobj_t, void *, int);
static u_int32_t envy24chan_getptr(kobj_t, void *);
static struct pcmchan_caps *envy24chan_getcaps(kobj_t, void *);

/* mixer interface */
static int envy24mixer_init(struct snd_mixer *);
static int envy24mixer_reinit(struct snd_mixer *);
static int envy24mixer_uninit(struct snd_mixer *);
static int envy24mixer_set(struct snd_mixer *, unsigned, unsigned, unsigned);
static u_int32_t envy24mixer_setrecsrc(struct snd_mixer *, u_int32_t);

/* M-Audio Delta series AK4524 access interface */
static void *envy24_delta_ak4524_create(device_t, void *, int, int);
static void envy24_delta_ak4524_destroy(void *);
static void envy24_delta_ak4524_init(void *);
static void envy24_delta_ak4524_reinit(void *);
static void envy24_delta_ak4524_setvolume(void *, int, unsigned int, unsigned int);

/* -------------------------------------------------------------------- */

/*
  system constant tables
*/

/* API -> hardware channel map */
static unsigned envy24_chanmap[ENVY24_CHAN_NUM] = {
	ENVY24_CHAN_PLAY_SPDIF, /* 0 */
	ENVY24_CHAN_PLAY_DAC1,  /* 1 */
	ENVY24_CHAN_PLAY_DAC2,  /* 2 */
	ENVY24_CHAN_PLAY_DAC3,  /* 3 */
	ENVY24_CHAN_PLAY_DAC4,  /* 4 */
	ENVY24_CHAN_REC_MIX,    /* 5 */
	ENVY24_CHAN_REC_SPDIF,  /* 6 */
	ENVY24_CHAN_REC_ADC1,   /* 7 */
	ENVY24_CHAN_REC_ADC2,   /* 8 */
	ENVY24_CHAN_REC_ADC3,   /* 9 */
	ENVY24_CHAN_REC_ADC4,   /* 10 */
};

/* mixer -> API channel map. see above */
static int envy24_mixmap[] = {
	-1, /* Master output level. It is depend on codec support */
	-1, /* Treble level of all output channels */
	-1, /* Bass level of all output channels */
	-1, /* Volume of synthesier input */
	0,  /* Output level for the audio device */
	-1, /* Output level for the PC speaker */
	7,  /* line in jack */
	-1, /* microphone jack */
	-1, /* CD audio input */
	-1, /* Recording monitor */
	1,  /* alternative codec */
	-1, /* global recording level */
	-1, /* Input gain */
	-1, /* Output gain */
	8,  /* Input source 1 */
	9,  /* Input source 2 */
	10, /* Input source 3 */
	6,  /* Digital (input) 1 */
	-1, /* Digital (input) 2 */
	-1, /* Digital (input) 3 */
	-1, /* Phone input */
	-1, /* Phone output */
	-1, /* Video/TV (audio) in */
	-1, /* Radio in */
	-1, /* Monitor volume */
};

/* variable rate audio */
static u_int32_t envy24_speed[] = {
    96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000,
    12000, 11025, 9600, 8000, 0
};

/* known boards configuration */
static struct codec_entry delta_codec = {
	envy24_delta_ak4524_create,
	envy24_delta_ak4524_destroy,
	envy24_delta_ak4524_init,
	envy24_delta_ak4524_reinit,
	envy24_delta_ak4524_setvolume,
	NULL, /* setrate */
};

static struct cfg_info cfg_table[] = {
	{
		"Envy24 audio (M Audio Delta Dio 2496)",
		0x1412, 0xd631,
		0x10, 0x80, 0xf0, 0x03,
		0x02, 0xc0, 0xfd,
		0x10, 0x20, 0x40, 0x00, 0x00,
		0x00,
		&delta_codec,
	},
	{
		"Envy24 audio (Terratec DMX 6fire)",
		0x153b, 0x1138,
		0x2f, 0x80, 0xf0, 0x03,
		0xc0, 0xff, 0x7f,
		0x10, 0x20, 0x01, 0x01, 0x00,
		0x00,
 		&delta_codec,
 	},
	{
		"Envy24 audio (M Audio Audiophile 2496)",
		0x1412, 0xd634,
		0x10, 0x80, 0x72, 0x03,
		0x04, 0xfe, 0xfb,
		0x08, 0x02, 0x20, 0x00, 0x01,
		0x00,
 		&delta_codec,
 	},
        {
                "Envy24 audio (M Audio Delta 66)",
                0x1412, 0xd632,
                0x15, 0x80, 0xf0, 0x03,
                0x02, 0xc0, 0xfd,
                0x10, 0x20, 0x40, 0x00, 0x00,
                0x00,
                &delta_codec,
        },
        {
                "Envy24 audio (M Audio Delta 44)",
                0x1412, 0xd633,
                0x15, 0x80, 0xf0, 0x00,
                0x02, 0xc0, 0xfd,
                0x10, 0x20, 0x40, 0x00, 0x00,
                0x00,
                &delta_codec,
        },
        {
                "Envy24 audio (M Audio Delta 1010)",
                0x1412, 0xd630,
                0x1f, 0x80, 0xf0, 0x03,
                0x22, 0xd0, 0xdd,
                0x10, 0x20, 0x40, 0x00, 0x00,
                0x00,
                &delta_codec,
        },
        {
                "Envy24 audio (M Audio Delta 1010LT)",
                0x1412, 0xd63b,
                0x1f, 0x80, 0x72, 0x03,
                0x04, 0x7e, 0xfb,
                0x08, 0x02, 0x70, 0x00, 0x00,
                0x00,
                &delta_codec,
        },
        {
                "Envy24 audio (Terratec EWX 2496)",
                0x153b, 0x1130,
                0x10, 0x80, 0xf0, 0x03,
                0xc0, 0x3f, 0x3f,
                0x10, 0x20, 0x01, 0x01, 0x00,
                0x00,
                &delta_codec,
        },
	{
		"Envy24 audio (Generic)",
		0, 0,
		0x0f, 0x00, 0x01, 0x03,
		0xff, 0x00, 0x00,
		0x10, 0x20, 0x40, 0x00, 0x00,
		0x00,
		&delta_codec, /* default codec routines */
	}
};

static u_int32_t envy24_recfmt[] = {
	SND_FORMAT(AFMT_S16_LE, 2, 0),
	SND_FORMAT(AFMT_S32_LE, 2, 0),
	0
};
static struct pcmchan_caps envy24_reccaps = {8000, 96000, envy24_recfmt, 0};

static u_int32_t envy24_playfmt[] = {
	SND_FORMAT(AFMT_U8, 2, 0),
	SND_FORMAT(AFMT_S16_LE, 2, 0),
	SND_FORMAT(AFMT_S32_LE, 2, 0),
	0
};

static struct pcmchan_caps envy24_playcaps = {8000, 96000, envy24_playfmt, 0};

struct envy24_emldma {
	u_int32_t	format;
	void		(*emldma)(struct sc_chinfo *);
	int		unit;
};

static struct envy24_emldma envy24_pemltab[] = {
	{SND_FORMAT(AFMT_U8, 2, 0), envy24_p8u, 2},
	{SND_FORMAT(AFMT_S16_LE, 2, 0), envy24_p16sl, 4},
	{SND_FORMAT(AFMT_S32_LE, 2, 0), envy24_p32sl, 8},
	{0, NULL, 0}
};

static struct envy24_emldma envy24_remltab[] = {
	{SND_FORMAT(AFMT_S16_LE, 2, 0), envy24_r16sl, 4},
	{SND_FORMAT(AFMT_S32_LE, 2, 0), envy24_r32sl, 8},
	{0, NULL, 0}
};

/* -------------------------------------------------------------------- */

/* common routines */
static u_int32_t
envy24_rdcs(struct sc_info *sc, int regno, int size)
{
	switch (size) {
	case 1:
		return bus_space_read_1(sc->cst, sc->csh, regno);
	case 2:
		return bus_space_read_2(sc->cst, sc->csh, regno);
	case 4:
		return bus_space_read_4(sc->cst, sc->csh, regno);
	default:
		return 0xffffffff;
	}
}

static void
envy24_wrcs(struct sc_info *sc, int regno, u_int32_t data, int size)
{
	switch (size) {
	case 1:
		bus_space_write_1(sc->cst, sc->csh, regno, data);
		break;
	case 2:
		bus_space_write_2(sc->cst, sc->csh, regno, data);
		break;
	case 4:
		bus_space_write_4(sc->cst, sc->csh, regno, data);
		break;
	}
}

static u_int32_t
envy24_rdmt(struct sc_info *sc, int regno, int size)
{
	switch (size) {
	case 1:
		return bus_space_read_1(sc->mtt, sc->mth, regno);
	case 2:
		return bus_space_read_2(sc->mtt, sc->mth, regno);
	case 4:
		return bus_space_read_4(sc->mtt, sc->mth, regno);
	default:
		return 0xffffffff;
	}
}

static void
envy24_wrmt(struct sc_info *sc, int regno, u_int32_t data, int size)
{
	switch (size) {
	case 1:
		bus_space_write_1(sc->mtt, sc->mth, regno, data);
		break;
	case 2:
		bus_space_write_2(sc->mtt, sc->mth, regno, data);
		break;
	case 4:
		bus_space_write_4(sc->mtt, sc->mth, regno, data);
		break;
	}
}

static u_int32_t
envy24_rdci(struct sc_info *sc, int regno)
{
	envy24_wrcs(sc, ENVY24_CCS_INDEX, regno, 1);
	return envy24_rdcs(sc, ENVY24_CCS_DATA, 1);
}

static void
envy24_wrci(struct sc_info *sc, int regno, u_int32_t data)
{
	envy24_wrcs(sc, ENVY24_CCS_INDEX, regno, 1);
	envy24_wrcs(sc, ENVY24_CCS_DATA, data, 1);
}

/* -------------------------------------------------------------------- */

/* I2C port/E2PROM access routines */

static int
envy24_rdi2c(struct sc_info *sc, u_int32_t dev, u_int32_t addr)
{
	u_int32_t data;
	int i;

#if(0)
	device_printf(sc->dev, "envy24_rdi2c(sc, 0x%02x, 0x%02x)\n", dev, addr);
#endif
	for (i = 0; i < ENVY24_TIMEOUT; i++) {
		data = envy24_rdcs(sc, ENVY24_CCS_I2CSTAT, 1);
		if ((data & ENVY24_CCS_I2CSTAT_BSY) == 0)
			break;
		DELAY(32); /* 31.25kHz */
	}
	if (i == ENVY24_TIMEOUT) {
		return -1;
	}
	envy24_wrcs(sc, ENVY24_CCS_I2CADDR, addr, 1);
	envy24_wrcs(sc, ENVY24_CCS_I2CDEV,
	    (dev & ENVY24_CCS_I2CDEV_ADDR) | ENVY24_CCS_I2CDEV_RD, 1);
	for (i = 0; i < ENVY24_TIMEOUT; i++) {
		data = envy24_rdcs(sc, ENVY24_CCS_I2CSTAT, 1);
		if ((data & ENVY24_CCS_I2CSTAT_BSY) == 0)
			break;
		DELAY(32); /* 31.25kHz */
	}
	if (i == ENVY24_TIMEOUT) {
		return -1;
	}
	data = envy24_rdcs(sc, ENVY24_CCS_I2CDATA, 1);

#if(0)
	device_printf(sc->dev, "envy24_rdi2c(): return 0x%x\n", data);
#endif
	return (int)data;
}

#if 0
static int
envy24_wri2c(struct sc_info *sc, u_int32_t dev, u_int32_t addr, u_int32_t data)
{
	u_int32_t tmp;
	int i;

#if(0)
	device_printf(sc->dev, "envy24_rdi2c(sc, 0x%02x, 0x%02x)\n", dev, addr);
#endif
	for (i = 0; i < ENVY24_TIMEOUT; i++) {
		tmp = envy24_rdcs(sc, ENVY24_CCS_I2CSTAT, 1);
		if ((tmp & ENVY24_CCS_I2CSTAT_BSY) == 0)
			break;
		DELAY(32); /* 31.25kHz */
	}
	if (i == ENVY24_TIMEOUT) {
		return -1;
	}
	envy24_wrcs(sc, ENVY24_CCS_I2CADDR, addr, 1);
	envy24_wrcs(sc, ENVY24_CCS_I2CDATA, data, 1);
	envy24_wrcs(sc, ENVY24_CCS_I2CDEV,
	    (dev & ENVY24_CCS_I2CDEV_ADDR) | ENVY24_CCS_I2CDEV_WR, 1);
	for (i = 0; i < ENVY24_TIMEOUT; i++) {
		data = envy24_rdcs(sc, ENVY24_CCS_I2CSTAT, 1);
		if ((data & ENVY24_CCS_I2CSTAT_BSY) == 0)
			break;
		DELAY(32); /* 31.25kHz */
	}
	if (i == ENVY24_TIMEOUT) {
		return -1;
	}

	return 0;
}
#endif

static int
envy24_rdrom(struct sc_info *sc, u_int32_t addr)
{
	u_int32_t data;

#if(0)
	device_printf(sc->dev, "envy24_rdrom(sc, 0x%02x)\n", addr);
#endif
	data = envy24_rdcs(sc, ENVY24_CCS_I2CSTAT, 1);
	if ((data & ENVY24_CCS_I2CSTAT_ROM) == 0) {
#if(0)
		device_printf(sc->dev, "envy24_rdrom(): E2PROM not presented\n");
#endif
		return -1;
	}

	return envy24_rdi2c(sc, ENVY24_CCS_I2CDEV_ROM, addr);
}

static struct cfg_info *
envy24_rom2cfg(struct sc_info *sc)
{
	struct cfg_info *buff;
	int size;
	int i;

#if(0)
	device_printf(sc->dev, "envy24_rom2cfg(sc)\n");
#endif
	size = envy24_rdrom(sc, ENVY24_E2PROM_SIZE);
	if (size < ENVY24_E2PROM_GPIODIR + 1) {
#if(0)
		device_printf(sc->dev, "envy24_rom2cfg(): ENVY24_E2PROM_SIZE-->%d\n", size);
#endif
		return NULL;
	}
	buff = malloc(sizeof(*buff), M_ENVY24, M_NOWAIT);
	if (buff == NULL) {
#if(0)
		device_printf(sc->dev, "envy24_rom2cfg(): malloc()\n");
#endif
		return NULL;
	}
	buff->free = 1;

	buff->subvendor = envy24_rdrom(sc, ENVY24_E2PROM_SUBVENDOR) << 8;
	buff->subvendor += envy24_rdrom(sc, ENVY24_E2PROM_SUBVENDOR + 1);
	buff->subdevice = envy24_rdrom(sc, ENVY24_E2PROM_SUBDEVICE) << 8;
	buff->subdevice += envy24_rdrom(sc, ENVY24_E2PROM_SUBDEVICE + 1);
	buff->scfg = envy24_rdrom(sc, ENVY24_E2PROM_SCFG);
	buff->acl = envy24_rdrom(sc, ENVY24_E2PROM_ACL);
	buff->i2s = envy24_rdrom(sc, ENVY24_E2PROM_I2S);
	buff->spdif = envy24_rdrom(sc, ENVY24_E2PROM_SPDIF);
	buff->gpiomask = envy24_rdrom(sc, ENVY24_E2PROM_GPIOMASK);
	buff->gpiostate = envy24_rdrom(sc, ENVY24_E2PROM_GPIOSTATE);
	buff->gpiodir = envy24_rdrom(sc, ENVY24_E2PROM_GPIODIR);

	for (i = 0; cfg_table[i].subvendor != 0 || cfg_table[i].subdevice != 0; i++)
		if (cfg_table[i].subvendor == buff->subvendor &&
		    cfg_table[i].subdevice == buff->subdevice)
			break;
	buff->name = cfg_table[i].name;
	buff->codec = cfg_table[i].codec;

	return buff;
}

static void
envy24_cfgfree(struct cfg_info *cfg) {
	if (cfg == NULL)
		return;
	if (cfg->free)
		free(cfg, M_ENVY24);
	return;
}

/* -------------------------------------------------------------------- */

/* AC'97 codec access routines */

#if 0
static int
envy24_coldcd(struct sc_info *sc)
{
	u_int32_t data;
	int i;

#if(0)
	device_printf(sc->dev, "envy24_coldcd()\n");
#endif
	envy24_wrmt(sc, ENVY24_MT_AC97CMD, ENVY24_MT_AC97CMD_CLD, 1);
	DELAY(10);
	envy24_wrmt(sc, ENVY24_MT_AC97CMD, 0, 1);
	DELAY(1000);
	for (i = 0; i < ENVY24_TIMEOUT; i++) {
		data = envy24_rdmt(sc, ENVY24_MT_AC97CMD, 1);
		if (data & ENVY24_MT_AC97CMD_RDY) {
			return 0;
		}
	}

	return -1;
}
#endif

static int
envy24_slavecd(struct sc_info *sc)
{
	u_int32_t data;
	int i;

#if(0)
	device_printf(sc->dev, "envy24_slavecd()\n");
#endif
	envy24_wrmt(sc, ENVY24_MT_AC97CMD,
	    ENVY24_MT_AC97CMD_CLD | ENVY24_MT_AC97CMD_WRM, 1);
	DELAY(10);
	envy24_wrmt(sc, ENVY24_MT_AC97CMD, 0, 1);
	DELAY(1000);
	for (i = 0; i < ENVY24_TIMEOUT; i++) {
		data = envy24_rdmt(sc, ENVY24_MT_AC97CMD, 1);
		if (data & ENVY24_MT_AC97CMD_RDY) {
			return 0;
		}
	}

	return -1;
}

#if 0
static int
envy24_rdcd(kobj_t obj, void *devinfo, int regno)
{
	struct sc_info *sc = (struct sc_info *)devinfo;
	u_int32_t data;
	int i;

#if(0)
	device_printf(sc->dev, "envy24_rdcd(obj, sc, 0x%02x)\n", regno);
#endif
	envy24_wrmt(sc, ENVY24_MT_AC97IDX, (u_int32_t)regno, 1);
	envy24_wrmt(sc, ENVY24_MT_AC97CMD, ENVY24_MT_AC97CMD_RD, 1);
	for (i = 0; i < ENVY24_TIMEOUT; i++) {
		data = envy24_rdmt(sc, ENVY24_MT_AC97CMD, 1);
		if ((data & ENVY24_MT_AC97CMD_RD) == 0)
			break;
	}
	data = envy24_rdmt(sc, ENVY24_MT_AC97DLO, 2);

#if(0)
	device_printf(sc->dev, "envy24_rdcd(): return 0x%x\n", data);
#endif
	return (int)data;
}

static int
envy24_wrcd(kobj_t obj, void *devinfo, int regno, u_int16_t data)
{
	struct sc_info *sc = (struct sc_info *)devinfo;
	u_int32_t cmd;
	int i;

#if(0)
	device_printf(sc->dev, "envy24_wrcd(obj, sc, 0x%02x, 0x%04x)\n", regno, data);
#endif
	envy24_wrmt(sc, ENVY24_MT_AC97IDX, (u_int32_t)regno, 1);
	envy24_wrmt(sc, ENVY24_MT_AC97DLO, (u_int32_t)data, 2);
	envy24_wrmt(sc, ENVY24_MT_AC97CMD, ENVY24_MT_AC97CMD_WR, 1);
	for (i = 0; i < ENVY24_TIMEOUT; i++) {
		cmd = envy24_rdmt(sc, ENVY24_MT_AC97CMD, 1);
		if ((cmd & ENVY24_MT_AC97CMD_WR) == 0)
			break;
	}

	return 0;
}

static kobj_method_t envy24_ac97_methods[] = {
	KOBJMETHOD(ac97_read,	envy24_rdcd),
	KOBJMETHOD(ac97_write,	envy24_wrcd),
	KOBJMETHOD_END
};
AC97_DECLARE(envy24_ac97);
#endif

/* -------------------------------------------------------------------- */

/* GPIO access routines */

static u_int32_t
envy24_gpiord(struct sc_info *sc)
{
	return envy24_rdci(sc, ENVY24_CCI_GPIODAT);
}

static void
envy24_gpiowr(struct sc_info *sc, u_int32_t data)
{
#if(0)
	device_printf(sc->dev, "envy24_gpiowr(sc, 0x%02x)\n", data & 0xff);
	return;
#endif
	envy24_wrci(sc, ENVY24_CCI_GPIODAT, data);
	return;
}

#if 0
static u_int32_t
envy24_gpiogetmask(struct sc_info *sc)
{
	return envy24_rdci(sc, ENVY24_CCI_GPIOMASK);
}
#endif

static void
envy24_gpiosetmask(struct sc_info *sc, u_int32_t mask)
{
	envy24_wrci(sc, ENVY24_CCI_GPIOMASK, mask);
	return;
}

#if 0
static u_int32_t
envy24_gpiogetdir(struct sc_info *sc)
{
	return envy24_rdci(sc, ENVY24_CCI_GPIOCTL);
}
#endif

static void
envy24_gpiosetdir(struct sc_info *sc, u_int32_t dir)
{
	envy24_wrci(sc, ENVY24_CCI_GPIOCTL, dir);
	return;
}

/* -------------------------------------------------------------------- */

/* Envy24 I2C through GPIO bit-banging */

struct envy24_delta_ak4524_codec {
	struct spicds_info *info;
	struct sc_info *parent;
	int dir;
	int num;
	int cs, cclk, cdti;
};

static void
envy24_gpio_i2c_ctl(void *codec, unsigned int scl, unsigned int sda)
{
        u_int32_t data = 0;
        struct envy24_delta_ak4524_codec *ptr = codec;
#if(0)
        device_printf(ptr->parent->dev, "--> %d, %d\n", scl, sda);
#endif
        data = envy24_gpiord(ptr->parent);
        data &= ~(SDA_GPIO | SCL_GPIO);
        if (scl) data += SCL_GPIO;
        if (sda) data += SDA_GPIO;
        envy24_gpiowr(ptr->parent, data);
        return;
}

static void
i2c_wrbit(void *codec, void (*ctrl)(void*, unsigned int, unsigned int), int bit)
{
        struct envy24_delta_ak4524_codec *ptr = codec;
        unsigned int sda;

        if (bit)
                sda = 1;
        else
                sda = 0;

        ctrl(ptr, 0, sda);
        DELAY(I2C_DELAY);
        ctrl(ptr, 1, sda);
        DELAY(I2C_DELAY);
        ctrl(ptr, 0, sda);
        DELAY(I2C_DELAY);
}

static void
i2c_start(void *codec, void (*ctrl)(void*, unsigned int, unsigned int))
{
        struct envy24_delta_ak4524_codec *ptr = codec;

        ctrl(ptr, 1, 1);
        DELAY(I2C_DELAY);
        ctrl(ptr, 1, 0);
        DELAY(I2C_DELAY);
        ctrl(ptr, 0, 0);
        DELAY(I2C_DELAY);
}

static void
i2c_stop(void *codec, void (*ctrl)(void*, unsigned int, unsigned int))
{
        struct envy24_delta_ak4524_codec *ptr = codec;

        ctrl(ptr, 0, 0);
        DELAY(I2C_DELAY);
        ctrl(ptr, 1, 0);
        DELAY(I2C_DELAY);
        ctrl(ptr, 1, 1);
        DELAY(I2C_DELAY);
}

static void
i2c_ack(void *codec, void (*ctrl)(void*, unsigned int, unsigned int))
{
        struct envy24_delta_ak4524_codec *ptr = codec;

        ctrl(ptr, 0, 1);
        DELAY(I2C_DELAY);
        ctrl(ptr, 1, 1);
        DELAY(I2C_DELAY);
        /* dummy, need routine to change gpio direction */
        ctrl(ptr, 0, 1);
        DELAY(I2C_DELAY);
}

static void
i2c_wr(void *codec,  void (*ctrl)(void*, unsigned int, unsigned int), u_int32_t dev, int reg, u_int8_t val)
{
        struct envy24_delta_ak4524_codec *ptr = codec;
        int mask;

        i2c_start(ptr, ctrl);

        for (mask = 0x80; mask != 0; mask >>= 1)
                i2c_wrbit(ptr, ctrl, dev & mask);
        i2c_ack(ptr, ctrl);

        if (reg != 0xff) {
        for (mask = 0x80; mask != 0; mask >>= 1)
                i2c_wrbit(ptr, ctrl, reg & mask);
        i2c_ack(ptr, ctrl);
        }

        for (mask = 0x80; mask != 0; mask >>= 1)
                i2c_wrbit(ptr, ctrl, val & mask);
        i2c_ack(ptr, ctrl);

        i2c_stop(ptr, ctrl);
}

/* -------------------------------------------------------------------- */

/* M-Audio Delta series AK4524 access interface routine */

static void
envy24_delta_ak4524_ctl(void *codec, unsigned int cs, unsigned int cclk, unsigned int cdti)
{
	u_int32_t data = 0;
	struct envy24_delta_ak4524_codec *ptr = codec;

#if(0)
	device_printf(ptr->parent->dev, "--> %d, %d, %d\n", cs, cclk, cdti);
#endif
	data = envy24_gpiord(ptr->parent);
	data &= ~(ptr->cs | ptr->cclk | ptr->cdti);
	if (cs) data += ptr->cs;
	if (cclk) data += ptr->cclk;
	if (cdti) data += ptr->cdti;
	envy24_gpiowr(ptr->parent, data);
	return;
}

static void *
envy24_delta_ak4524_create(device_t dev, void *info, int dir, int num)
{
	struct sc_info *sc = info;
	struct envy24_delta_ak4524_codec *buff = NULL;

#if(0)
	device_printf(sc->dev, "envy24_delta_ak4524_create(dev, sc, %d, %d)\n", dir, num);
#endif
	
	buff = malloc(sizeof(*buff), M_ENVY24, M_NOWAIT);
	if (buff == NULL)
		return NULL;

	if (dir == PCMDIR_REC && sc->adc[num] != NULL)
		buff->info = ((struct envy24_delta_ak4524_codec *)sc->adc[num])->info;
	else if (dir == PCMDIR_PLAY && sc->dac[num] != NULL)
		buff->info = ((struct envy24_delta_ak4524_codec *)sc->dac[num])->info;
	else
		buff->info = spicds_create(dev, buff, num, envy24_delta_ak4524_ctl);
	if (buff->info == NULL) {
		free(buff, M_ENVY24);
		return NULL;
	}

	buff->parent = sc;
	buff->dir = dir;
	buff->num = num;

	return (void *)buff;
}

static void
envy24_delta_ak4524_destroy(void *codec)
{
	struct envy24_delta_ak4524_codec *ptr = codec;
	if (ptr == NULL)
		return;
#if(0)
	device_printf(ptr->parent->dev, "envy24_delta_ak4524_destroy()\n");
#endif

	if (ptr->dir == PCMDIR_PLAY) {
		if (ptr->parent->dac[ptr->num] != NULL)
			spicds_destroy(ptr->info);
	}
	else {
		if (ptr->parent->adc[ptr->num] != NULL)
			spicds_destroy(ptr->info);
	}

	free(codec, M_ENVY24);
}

static void
envy24_delta_ak4524_init(void *codec)
{
#if 0
	u_int32_t gpiomask, gpiodir;
#endif
	struct envy24_delta_ak4524_codec *ptr = codec;
	if (ptr == NULL)
		return;
#if(0)
	device_printf(ptr->parent->dev, "envy24_delta_ak4524_init()\n");
#endif

	/*
	gpiomask = envy24_gpiogetmask(ptr->parent);
	gpiomask &= ~(ENVY24_GPIO_AK4524_CDTI | ENVY24_GPIO_AK4524_CCLK | ENVY24_GPIO_AK4524_CS0 | ENVY24_GPIO_AK4524_CS1);
	envy24_gpiosetmask(ptr->parent, gpiomask);
	gpiodir = envy24_gpiogetdir(ptr->parent);
	gpiodir |= ENVY24_GPIO_AK4524_CDTI | ENVY24_GPIO_AK4524_CCLK | ENVY24_GPIO_AK4524_CS0 | ENVY24_GPIO_AK4524_CS1;
	envy24_gpiosetdir(ptr->parent, gpiodir);
	*/
	ptr->cs = ptr->parent->cfg->cs;
#if 0
	envy24_gpiosetmask(ptr->parent, ENVY24_GPIO_CS8414_STATUS);
	envy24_gpiosetdir(ptr->parent, ~ENVY24_GPIO_CS8414_STATUS);
	if (ptr->num == 0)
		ptr->cs = ENVY24_GPIO_AK4524_CS0;
	else
		ptr->cs = ENVY24_GPIO_AK4524_CS1;
	ptr->cclk = ENVY24_GPIO_AK4524_CCLK;
#endif
	ptr->cclk = ptr->parent->cfg->cclk;
	ptr->cdti = ptr->parent->cfg->cdti;
	spicds_settype(ptr->info,  ptr->parent->cfg->type);
	spicds_setcif(ptr->info, ptr->parent->cfg->cif);
	spicds_setformat(ptr->info,
	    AK452X_FORMAT_I2S | AK452X_FORMAT_256FSN | AK452X_FORMAT_1X);
	spicds_setdvc(ptr->info, AK452X_DVC_DEMOFF);
	/* for the time being, init only first codec */
	if (ptr->num == 0)
		spicds_init(ptr->info);

        /* 6fire rear input init test, set ptr->num to 1 for test */
        if (ptr->parent->cfg->subvendor == 0x153b && \
                ptr->parent->cfg->subdevice == 0x1138 && ptr->num == 100) {
                ptr->cs = 0x02;  
                spicds_init(ptr->info);
                device_printf(ptr->parent->dev, "6fire rear input init\n");
                i2c_wr(ptr, envy24_gpio_i2c_ctl, \
                        PCA9554_I2CDEV, PCA9554_DIR, 0x80);
                i2c_wr(ptr, envy24_gpio_i2c_ctl, \
                        PCA9554_I2CDEV, PCA9554_OUT, 0x02);
        }
}

static void
envy24_delta_ak4524_reinit(void *codec)
{
	struct envy24_delta_ak4524_codec *ptr = codec;
	if (ptr == NULL)
		return;
#if(0)
	device_printf(ptr->parent->dev, "envy24_delta_ak4524_reinit()\n");
#endif

	spicds_reinit(ptr->info);
}

static void
envy24_delta_ak4524_setvolume(void *codec, int dir, unsigned int left, unsigned int right)
{
	struct envy24_delta_ak4524_codec *ptr = codec;
	if (ptr == NULL)
		return;
#if(0)
	device_printf(ptr->parent->dev, "envy24_delta_ak4524_set()\n");
#endif

	spicds_set(ptr->info, dir, left, right);
}

/*
  There is no need for AK452[48] codec to set sample rate
  static void
  envy24_delta_ak4524_setrate(struct envy24_delta_ak4524_codec *codec, int which, int rate)
  {
  }
*/

/* -------------------------------------------------------------------- */

/* hardware access routeines */

static struct {
	u_int32_t speed;
	u_int32_t code;
} envy24_speedtab[] = {
	{48000, ENVY24_MT_RATE_48000},
	{24000, ENVY24_MT_RATE_24000},
	{12000, ENVY24_MT_RATE_12000},
	{9600, ENVY24_MT_RATE_9600},
	{32000, ENVY24_MT_RATE_32000},
	{16000, ENVY24_MT_RATE_16000},
	{8000, ENVY24_MT_RATE_8000},
	{96000, ENVY24_MT_RATE_96000},
	{64000, ENVY24_MT_RATE_64000},
	{44100, ENVY24_MT_RATE_44100},
	{22050, ENVY24_MT_RATE_22050},
	{11025, ENVY24_MT_RATE_11025},
	{88200, ENVY24_MT_RATE_88200},
	{0, 0x10}
};

static u_int32_t
envy24_setspeed(struct sc_info *sc, u_int32_t speed) {
	u_int32_t code;
	int i = 0;

#if(0)
	device_printf(sc->dev, "envy24_setspeed(sc, %d)\n", speed);
#endif
	if (speed == 0) {
		code = ENVY24_MT_RATE_SPDIF; /* external master clock */
		envy24_slavecd(sc);
	}
	else {
		for (i = 0; envy24_speedtab[i].speed != 0; i++) {
			if (envy24_speedtab[i].speed == speed)
				break;
		}
		code = envy24_speedtab[i].code;
	}
#if(0)
	device_printf(sc->dev, "envy24_setspeed(): speed %d/code 0x%04x\n", envy24_speedtab[i].speed, code);
#endif
	if (code < 0x10) {
		envy24_wrmt(sc, ENVY24_MT_RATE, code, 1);
		code = envy24_rdmt(sc, ENVY24_MT_RATE, 1);
		code &= ENVY24_MT_RATE_MASK;
		for (i = 0; envy24_speedtab[i].code < 0x10; i++) {
			if (envy24_speedtab[i].code == code)
				break;
		}
		speed = envy24_speedtab[i].speed;
	}
	else
		speed = 0;

#if(0)
	device_printf(sc->dev, "envy24_setspeed(): return %d\n", speed);
#endif
	return speed;
}

static void
envy24_setvolume(struct sc_info *sc, unsigned ch)
{
#if(0)
	device_printf(sc->dev, "envy24_setvolume(sc, %d)\n", ch);
#endif
if (sc->cfg->subvendor==0x153b  && sc->cfg->subdevice==0x1138 ) {
        envy24_wrmt(sc, ENVY24_MT_VOLIDX, 16, 1);
        envy24_wrmt(sc, ENVY24_MT_VOLUME, 0x7f7f, 2);
        envy24_wrmt(sc, ENVY24_MT_VOLIDX, 17, 1);
        envy24_wrmt(sc, ENVY24_MT_VOLUME, 0x7f7f, 2);
	}

	envy24_wrmt(sc, ENVY24_MT_VOLIDX, ch * 2, 1);
	envy24_wrmt(sc, ENVY24_MT_VOLUME, 0x7f00 | sc->left[ch], 2);
	envy24_wrmt(sc, ENVY24_MT_VOLIDX, ch * 2 + 1, 1);
	envy24_wrmt(sc, ENVY24_MT_VOLUME, (sc->right[ch] << 8) | 0x7f, 2);
}

static void
envy24_mutevolume(struct sc_info *sc, unsigned ch)
{
	u_int32_t vol;

#if(0)
	device_printf(sc->dev, "envy24_mutevolume(sc, %d)\n", ch);
#endif
	vol = ENVY24_VOL_MUTE << 8 | ENVY24_VOL_MUTE;
	envy24_wrmt(sc, ENVY24_MT_VOLIDX, ch * 2, 1);
	envy24_wrmt(sc, ENVY24_MT_VOLUME, vol, 2);
	envy24_wrmt(sc, ENVY24_MT_VOLIDX, ch * 2 + 1, 1);
	envy24_wrmt(sc, ENVY24_MT_VOLUME, vol, 2);
}

static u_int32_t
envy24_gethwptr(struct sc_info *sc, int dir)
{
	int unit, regno;
	u_int32_t ptr, rtn;

#if(0)
	device_printf(sc->dev, "envy24_gethwptr(sc, %d)\n", dir);
#endif
	if (dir == PCMDIR_PLAY) {
		rtn = sc->psize / 4;
		unit = ENVY24_PLAY_BUFUNIT / 4;
		regno = ENVY24_MT_PCNT;
	}
	else {
		rtn = sc->rsize / 4;
		unit = ENVY24_REC_BUFUNIT / 4;
		regno = ENVY24_MT_RCNT;
	}

	ptr = envy24_rdmt(sc, regno, 2);
	rtn -= (ptr + 1);
	rtn /= unit;

#if(0)
	device_printf(sc->dev, "envy24_gethwptr(): return %d\n", rtn);
#endif
	return rtn;
}

static void
envy24_updintr(struct sc_info *sc, int dir)
{
	int regptr, regintr;
	u_int32_t mask, intr;
	u_int32_t ptr, size, cnt;
	u_int16_t blk;

#if(0)
	device_printf(sc->dev, "envy24_updintr(sc, %d)\n", dir);
#endif
	if (dir == PCMDIR_PLAY) {
		blk = sc->blk[0];
		size = sc->psize / 4;
		regptr = ENVY24_MT_PCNT;
		regintr = ENVY24_MT_PTERM;
		mask = ~ENVY24_MT_INT_PMASK;
	}
	else {
		blk = sc->blk[1];
		size = sc->rsize / 4;
		regptr = ENVY24_MT_RCNT;
		regintr = ENVY24_MT_RTERM;
		mask = ~ENVY24_MT_INT_RMASK;
	}

	ptr = size - envy24_rdmt(sc, regptr, 2) - 1;
	/*
	cnt = blk - ptr % blk - 1;
	if (cnt == 0)
		cnt = blk - 1;
	*/
	cnt = blk - 1;
#if(0)
	device_printf(sc->dev, "envy24_updintr():ptr = %d, blk = %d, cnt = %d\n", ptr, blk, cnt);
#endif
	envy24_wrmt(sc, regintr, cnt, 2);
	intr = envy24_rdmt(sc, ENVY24_MT_INT, 1);
#if(0)
	device_printf(sc->dev, "envy24_updintr():intr = 0x%02x, mask = 0x%02x\n", intr, mask);
#endif
	envy24_wrmt(sc, ENVY24_MT_INT, intr & mask, 1);
#if(0)
	device_printf(sc->dev, "envy24_updintr():INT-->0x%02x\n",
		      envy24_rdmt(sc, ENVY24_MT_INT, 1));
#endif

	return;
}

#if 0
static void
envy24_maskintr(struct sc_info *sc, int dir)
{
	u_int32_t mask, intr;

#if(0)
	device_printf(sc->dev, "envy24_maskintr(sc, %d)\n", dir);
#endif
	if (dir == PCMDIR_PLAY)
		mask = ENVY24_MT_INT_PMASK;
	else
		mask = ENVY24_MT_INT_RMASK;
	intr = envy24_rdmt(sc, ENVY24_MT_INT, 1);
	envy24_wrmt(sc, ENVY24_MT_INT, intr | mask, 1);

	return;
}
#endif

static int
envy24_checkintr(struct sc_info *sc, int dir)
{
	u_int32_t mask, stat, intr, rtn;

#if(0)
	device_printf(sc->dev, "envy24_checkintr(sc, %d)\n", dir);
#endif
	intr = envy24_rdmt(sc, ENVY24_MT_INT, 1);
	if (dir == PCMDIR_PLAY) {
		if ((rtn = intr & ENVY24_MT_INT_PSTAT) != 0) {
			mask = ~ENVY24_MT_INT_RSTAT;
			stat = ENVY24_MT_INT_PSTAT | ENVY24_MT_INT_PMASK;
			envy24_wrmt(sc, ENVY24_MT_INT, (intr & mask) | stat, 1);
		}
	}
	else {
		if ((rtn = intr & ENVY24_MT_INT_RSTAT) != 0) {
			mask = ~ENVY24_MT_INT_PSTAT;
			stat = ENVY24_MT_INT_RSTAT | ENVY24_MT_INT_RMASK;
			envy24_wrmt(sc, ENVY24_MT_INT, (intr & mask) | stat, 1);
		}
	}

	return rtn;
}

static void
envy24_start(struct sc_info *sc, int dir)
{
	u_int32_t stat, sw;

#if(0)
	device_printf(sc->dev, "envy24_start(sc, %d)\n", dir);
#endif
	if (dir == PCMDIR_PLAY)
		sw = ENVY24_MT_PCTL_PSTART;
	else
		sw = ENVY24_MT_PCTL_RSTART;

	stat = envy24_rdmt(sc, ENVY24_MT_PCTL, 1);
	envy24_wrmt(sc, ENVY24_MT_PCTL, stat | sw, 1);
#if(0)
	DELAY(100);
	device_printf(sc->dev, "PADDR:0x%08x\n", envy24_rdmt(sc, ENVY24_MT_PADDR, 4));
	device_printf(sc->dev, "PCNT:%ld\n", envy24_rdmt(sc, ENVY24_MT_PCNT, 2));
#endif

	return;
}

static void
envy24_stop(struct sc_info *sc, int dir)
{
	u_int32_t stat, sw;

#if(0)
	device_printf(sc->dev, "envy24_stop(sc, %d)\n", dir);
#endif
	if (dir == PCMDIR_PLAY)
		sw = ~ENVY24_MT_PCTL_PSTART;
	else
		sw = ~ENVY24_MT_PCTL_RSTART;

	stat = envy24_rdmt(sc, ENVY24_MT_PCTL, 1);
	envy24_wrmt(sc, ENVY24_MT_PCTL, stat & sw, 1);

	return;
}

static int
envy24_route(struct sc_info *sc, int dac, int class, int adc, int rev)
{
	u_int32_t reg, mask;
	u_int32_t left, right;

#if(0)
	device_printf(sc->dev, "envy24_route(sc, %d, %d, %d, %d)\n",
	    dac, class, adc, rev);
#endif
	/* parameter pattern check */
	if (dac < 0 || ENVY24_ROUTE_DAC_SPDIF < dac)
		return -1;
	if (class == ENVY24_ROUTE_CLASS_MIX &&
	    (dac != ENVY24_ROUTE_DAC_1 && dac != ENVY24_ROUTE_DAC_SPDIF))
		return -1;
	if (rev) {
		left = ENVY24_ROUTE_RIGHT;
		right = ENVY24_ROUTE_LEFT;
	}
	else {
		left = ENVY24_ROUTE_LEFT;
		right = ENVY24_ROUTE_RIGHT;
	}

	if (dac == ENVY24_ROUTE_DAC_SPDIF) {
		reg = class | class << 2 |
			((adc << 1 | left) | left << 3) << 8 |
			((adc << 1 | right) | right << 3) << 12;
#if(0)
		device_printf(sc->dev, "envy24_route(): MT_SPDOUT-->0x%04x\n", reg);
#endif
		envy24_wrmt(sc, ENVY24_MT_SPDOUT, reg, 2);
	}
	else {
		mask = ~(0x0303 << dac * 2);
		reg = envy24_rdmt(sc, ENVY24_MT_PSDOUT, 2);
		reg = (reg & mask) | ((class | class << 8) << dac * 2);
#if(0)
		device_printf(sc->dev, "envy24_route(): MT_PSDOUT-->0x%04x\n", reg);
#endif
		envy24_wrmt(sc, ENVY24_MT_PSDOUT, reg, 2);
		mask = ~(0xff << dac * 8);
		reg = envy24_rdmt(sc, ENVY24_MT_RECORD, 4);
		reg = (reg & mask) |
			(((adc << 1 | left) | left << 3) |
			 ((adc << 1 | right) | right << 3) << 4) << dac * 8;
#if(0)
		device_printf(sc->dev, "envy24_route(): MT_RECORD-->0x%08x\n", reg);
#endif
		envy24_wrmt(sc, ENVY24_MT_RECORD, reg, 4);

		/* 6fire rear input init test */
		envy24_wrmt(sc, ENVY24_MT_RECORD, 0x00, 4);
	}

	return 0;
}

/* -------------------------------------------------------------------- */

/* buffer copy routines */
static void
envy24_p32sl(struct sc_chinfo *ch)
{
	int length;
	sample32_t *dmabuf;
	u_int32_t *data;
	int src, dst, ssize, dsize, slot;
	int i;

	length = sndbuf_getready(ch->buffer) / 8;
	dmabuf = ch->parent->pbuf;
	data = (u_int32_t *)ch->data;
	src = sndbuf_getreadyptr(ch->buffer) / 4;
	dst = src / 2 + ch->offset;
	ssize = ch->size / 4;
	dsize = ch->size / 8;
	slot = ch->num * 2;

	for (i = 0; i < length; i++) {
		dmabuf[dst * ENVY24_PLAY_CHNUM + slot].buffer = data[src];
		dmabuf[dst * ENVY24_PLAY_CHNUM + slot + 1].buffer = data[src + 1];
		dst++;
		dst %= dsize;
		src += 2;
		src %= ssize;
	}
	
	return;
}

static void
envy24_p16sl(struct sc_chinfo *ch)
{
	int length;
	sample32_t *dmabuf;
	u_int16_t *data;
	int src, dst, ssize, dsize, slot;
	int i;

#if(0)
	device_printf(ch->parent->dev, "envy24_p16sl()\n");
#endif
	length = sndbuf_getready(ch->buffer) / 4;
	dmabuf = ch->parent->pbuf;
	data = (u_int16_t *)ch->data;
	src = sndbuf_getreadyptr(ch->buffer) / 2;
	dst = src / 2 + ch->offset;
	ssize = ch->size / 2;
	dsize = ch->size / 4;
	slot = ch->num * 2;
#if(0)
	device_printf(ch->parent->dev, "envy24_p16sl():%lu-->%lu(%lu)\n", src, dst, length);
#endif
	
	for (i = 0; i < length; i++) {
		dmabuf[dst * ENVY24_PLAY_CHNUM + slot].buffer = (u_int32_t)data[src] << 16;
		dmabuf[dst * ENVY24_PLAY_CHNUM + slot + 1].buffer = (u_int32_t)data[src + 1] << 16;
#if(0)
		if (i < 16) {
			printf("%08x", dmabuf[dst * ENVY24_PLAY_CHNUM + slot]);
			printf("%08x", dmabuf[dst * ENVY24_PLAY_CHNUM + slot + 1]);
		}
#endif
		dst++;
		dst %= dsize;
		src += 2;
		src %= ssize;
	}
#if(0)
	printf("\n");
#endif
	
	return;
}

static void
envy24_p8u(struct sc_chinfo *ch)
{
	int length;
	sample32_t *dmabuf;
	u_int8_t *data;
	int src, dst, ssize, dsize, slot;
	int i;

	length = sndbuf_getready(ch->buffer) / 2;
	dmabuf = ch->parent->pbuf;
	data = (u_int8_t *)ch->data;
	src = sndbuf_getreadyptr(ch->buffer);
	dst = src / 2 + ch->offset;
	ssize = ch->size;
	dsize = ch->size / 4;
	slot = ch->num * 2;
	
	for (i = 0; i < length; i++) {
		dmabuf[dst * ENVY24_PLAY_CHNUM + slot].buffer = ((u_int32_t)data[src] ^ 0x80) << 24;
		dmabuf[dst * ENVY24_PLAY_CHNUM + slot + 1].buffer = ((u_int32_t)data[src + 1] ^ 0x80) << 24;
		dst++;
		dst %= dsize;
		src += 2;
		src %= ssize;
	}
	
	return;
}

static void
envy24_r32sl(struct sc_chinfo *ch)
{
	int length;
	sample32_t *dmabuf;
	u_int32_t *data;
	int src, dst, ssize, dsize, slot;
	int i;

	length = sndbuf_getfree(ch->buffer) / 8;
	dmabuf = ch->parent->rbuf;
	data = (u_int32_t *)ch->data;
	dst = sndbuf_getfreeptr(ch->buffer) / 4;
	src = dst / 2 + ch->offset;
	dsize = ch->size / 4;
	ssize = ch->size / 8;
	slot = (ch->num - ENVY24_CHAN_REC_ADC1) * 2;

	for (i = 0; i < length; i++) {
		data[dst] = dmabuf[src * ENVY24_REC_CHNUM + slot].buffer;
		data[dst + 1] = dmabuf[src * ENVY24_REC_CHNUM + slot + 1].buffer;
		dst += 2;
		dst %= dsize;
		src++;
		src %= ssize;
	}
	
	return;
}

static void
envy24_r16sl(struct sc_chinfo *ch)
{
	int length;
	sample32_t *dmabuf;
	u_int16_t *data;
	int src, dst, ssize, dsize, slot;
	int i;

	length = sndbuf_getfree(ch->buffer) / 4;
	dmabuf = ch->parent->rbuf;
	data = (u_int16_t *)ch->data;
	dst = sndbuf_getfreeptr(ch->buffer) / 2;
	src = dst / 2 + ch->offset;
	dsize = ch->size / 2;
	ssize = ch->size / 8;
	slot = (ch->num - ENVY24_CHAN_REC_ADC1) * 2;

	for (i = 0; i < length; i++) {
		data[dst] = dmabuf[src * ENVY24_REC_CHNUM + slot].buffer;
		data[dst + 1] = dmabuf[src * ENVY24_REC_CHNUM + slot + 1].buffer;
		dst += 2;
		dst %= dsize;
		src++;
		src %= ssize;
	}
	
	return;
}

/* -------------------------------------------------------------------- */

/* channel interface */
static void *
envy24chan_init(kobj_t obj, void *devinfo, struct snd_dbuf *b, struct pcm_channel *c, int dir)
{
	struct sc_info	*sc = (struct sc_info *)devinfo;
	struct sc_chinfo *ch;
	unsigned num;

#if(0)
	device_printf(sc->dev, "envy24chan_init(obj, devinfo, b, c, %d)\n", dir);
#endif
	snd_mtxlock(sc->lock);
	if ((sc->chnum > ENVY24_CHAN_PLAY_SPDIF && dir != PCMDIR_REC) ||
	    (sc->chnum < ENVY24_CHAN_REC_ADC1 && dir != PCMDIR_PLAY)) {
		snd_mtxunlock(sc->lock);
		return NULL;
	}
	num = sc->chnum;

	ch = &sc->chan[num];
	ch->size = 8 * ENVY24_SAMPLE_NUM;
	ch->data = malloc(ch->size, M_ENVY24, M_NOWAIT);
	if (ch->data == NULL) {
		ch->size = 0;
		ch = NULL;
	}
	else {
		ch->buffer = b;
		ch->channel = c;
		ch->parent = sc;
		ch->dir = dir;
		/* set channel map */
		ch->num = envy24_chanmap[num];
		snd_mtxunlock(sc->lock);
		sndbuf_setup(ch->buffer, ch->data, ch->size);
		snd_mtxlock(sc->lock);
		/* these 2 values are dummy */
		ch->unit = 4;
		ch->blk = 10240;
	}
	snd_mtxunlock(sc->lock);

	return ch;
}

static int
envy24chan_free(kobj_t obj, void *data)
{
	struct sc_chinfo *ch = data;
	struct sc_info *sc = ch->parent;

#if(0)
	device_printf(sc->dev, "envy24chan_free()\n");
#endif
	snd_mtxlock(sc->lock);
	if (ch->data != NULL) {
		free(ch->data, M_ENVY24);
		ch->data = NULL;
	}
	snd_mtxunlock(sc->lock);

	return 0;
}

static int
envy24chan_setformat(kobj_t obj, void *data, u_int32_t format)
{
	struct sc_chinfo *ch = data;
	struct sc_info *sc = ch->parent;
	struct envy24_emldma *emltab;
	/* unsigned int bcnt, bsize; */
	int i;

#if(0)
	device_printf(sc->dev, "envy24chan_setformat(obj, data, 0x%08x)\n", format);
#endif
	snd_mtxlock(sc->lock);
	/* check and get format related information */
	if (ch->dir == PCMDIR_PLAY)
		emltab = envy24_pemltab;
	else
		emltab = envy24_remltab;
	if (emltab == NULL) {
		snd_mtxunlock(sc->lock);
		return -1;
	}
	for (i = 0; emltab[i].format != 0; i++)
		if (emltab[i].format == format)
			break;
	if (emltab[i].format == 0) {
		snd_mtxunlock(sc->lock);
		return -1;
	}

	/* set format information */
	ch->format = format;
	ch->emldma = emltab[i].emldma;
	if (ch->unit > emltab[i].unit)
		ch->blk *= ch->unit / emltab[i].unit;
	else
		ch->blk /= emltab[i].unit / ch->unit;
	ch->unit = emltab[i].unit;

	/* set channel buffer information */
	ch->size = ch->unit * ENVY24_SAMPLE_NUM;
#if 0
	if (ch->dir == PCMDIR_PLAY)
		bsize = ch->blk * 4 / ENVY24_PLAY_BUFUNIT;
	else
		bsize = ch->blk * 4 / ENVY24_REC_BUFUNIT;
	bsize *= ch->unit;
	bcnt = ch->size / bsize;
	sndbuf_resize(ch->buffer, bcnt, bsize);
#endif
	snd_mtxunlock(sc->lock);

#if(0)
	device_printf(sc->dev, "envy24chan_setformat(): return 0x%08x\n", 0);
#endif
	return 0;
}

/*
  IMPLEMENT NOTICE: In this driver, setspeed function only do setting
  of speed information value. And real hardware speed setting is done
  at start triggered(see envy24chan_trigger()). So, at this function
  is called, any value that ENVY24 can use is able to set. But, at
  start triggerd, some other channel is running, and that channel's
  speed isn't same with, then trigger function will fail.
*/
static u_int32_t
envy24chan_setspeed(kobj_t obj, void *data, u_int32_t speed)
{
	struct sc_chinfo *ch = data;
	u_int32_t val, prev;
	int i;

#if(0)
	device_printf(ch->parent->dev, "envy24chan_setspeed(obj, data, %d)\n", speed);
#endif
	prev = 0x7fffffff;
	for (i = 0; (val = envy24_speed[i]) != 0; i++) {
		if (abs(val - speed) < abs(prev - speed))
			prev = val;
		else
			break;
	}
	ch->speed = prev;
	
#if(0)
	device_printf(ch->parent->dev, "envy24chan_setspeed(): return %d\n", ch->speed);
#endif
	return ch->speed;
}

static u_int32_t
envy24chan_setblocksize(kobj_t obj, void *data, u_int32_t blocksize)
{
	struct sc_chinfo *ch = data;
	/* struct sc_info *sc = ch->parent; */
	u_int32_t size, prev;
        unsigned int bcnt, bsize;

#if(0)
	device_printf(sc->dev, "envy24chan_setblocksize(obj, data, %d)\n", blocksize);
#endif
	prev = 0x7fffffff;
	/* snd_mtxlock(sc->lock); */
	for (size = ch->size / 2; size > 0; size /= 2) {
		if (abs(size - blocksize) < abs(prev - blocksize))
			prev = size;
		else
			break;
	}

	ch->blk = prev / ch->unit;
	if (ch->dir == PCMDIR_PLAY)
		ch->blk *= ENVY24_PLAY_BUFUNIT / 4;
	else
		ch->blk *= ENVY24_REC_BUFUNIT / 4;
	/* set channel buffer information */
	/* ch->size = ch->unit * ENVY24_SAMPLE_NUM; */
        if (ch->dir == PCMDIR_PLAY)
                bsize = ch->blk * 4 / ENVY24_PLAY_BUFUNIT;
        else
                bsize = ch->blk * 4 / ENVY24_REC_BUFUNIT;
        bsize *= ch->unit;
        bcnt = ch->size / bsize;
        sndbuf_resize(ch->buffer, bcnt, bsize);
	/* snd_mtxunlock(sc->lock); */

#if(0)
	device_printf(sc->dev, "envy24chan_setblocksize(): return %d\n", prev);
#endif
	return prev;
}

/* semantic note: must start at beginning of buffer */
static int
envy24chan_trigger(kobj_t obj, void *data, int go)
{
	struct sc_chinfo *ch = data;
	struct sc_info *sc = ch->parent;
	u_int32_t ptr;
	int slot;
	int error = 0;
#if 0
	int i;

	device_printf(sc->dev, "envy24chan_trigger(obj, data, %d)\n", go);
#endif
	snd_mtxlock(sc->lock);
	if (ch->dir == PCMDIR_PLAY)
		slot = 0;
	else
		slot = 1;
	switch (go) {
	case PCMTRIG_START:
#if(0)
		device_printf(sc->dev, "envy24chan_trigger(): start\n");
#endif
		/* check or set channel speed */
		if (sc->run[0] == 0 && sc->run[1] == 0) {
			sc->speed = envy24_setspeed(sc, ch->speed);
			sc->caps[0].minspeed = sc->caps[0].maxspeed = sc->speed;
			sc->caps[1].minspeed = sc->caps[1].maxspeed = sc->speed;
		}
		else if (ch->speed != 0 && ch->speed != sc->speed) {
			error = -1;
			goto fail;
		}
		if (ch->speed == 0)
			ch->channel->speed = sc->speed;
		/* start or enable channel */
		sc->run[slot]++;
		if (sc->run[slot] == 1) {
			/* first channel */
			ch->offset = 0;
			sc->blk[slot] = ch->blk;
		}
		else {
			ptr = envy24_gethwptr(sc, ch->dir);
			ch->offset = ((ptr / ch->blk + 1) * ch->blk %
			    (ch->size / 4)) * 4 / ch->unit;
			if (ch->blk < sc->blk[slot])
				sc->blk[slot] = ch->blk;
		}
		if (ch->dir == PCMDIR_PLAY) {
			ch->emldma(ch);
			envy24_setvolume(sc, ch->num);
		}
		envy24_updintr(sc, ch->dir);
		if (sc->run[slot] == 1)
			envy24_start(sc, ch->dir);
		ch->run = 1;
		break;
	case PCMTRIG_EMLDMAWR:
#if(0)
		device_printf(sc->dev, "envy24chan_trigger(): emldmawr\n");
#endif
		if (ch->run != 1) {
			error = -1;
			goto fail;
		}
		ch->emldma(ch);
		break;
	case PCMTRIG_EMLDMARD:
#if(0)
		device_printf(sc->dev, "envy24chan_trigger(): emldmard\n");
#endif
		if (ch->run != 1) {
			error = -1;
			goto fail;
		}
		ch->emldma(ch);
		break;
	case PCMTRIG_ABORT:
		if (ch->run) {
#if(0)
		device_printf(sc->dev, "envy24chan_trigger(): abort\n");
#endif
		ch->run = 0;
		sc->run[slot]--;
		if (ch->dir == PCMDIR_PLAY)
			envy24_mutevolume(sc, ch->num);
		if (sc->run[slot] == 0) {
			envy24_stop(sc, ch->dir);
			sc->intr[slot] = 0;
		}
#if 0
		else if (ch->blk == sc->blk[slot]) {
			sc->blk[slot] = ENVY24_SAMPLE_NUM / 2;
			for (i = 0; i < ENVY24_CHAN_NUM; i++) {
				if (sc->chan[i].dir == ch->dir &&
				    sc->chan[i].run == 1 &&
				    sc->chan[i].blk < sc->blk[slot])
					sc->blk[slot] = sc->chan[i].blk;
			}
			if (ch->blk != sc->blk[slot])
				envy24_updintr(sc, ch->dir);
		}
#endif
		}
		break;
	}
fail:
	snd_mtxunlock(sc->lock);
	return (error);
}

static u_int32_t
envy24chan_getptr(kobj_t obj, void *data)
{
	struct sc_chinfo *ch = data;
	struct sc_info *sc = ch->parent;
	u_int32_t ptr, rtn;

#if(0)
	device_printf(sc->dev, "envy24chan_getptr()\n");
#endif
	snd_mtxlock(sc->lock);
	ptr = envy24_gethwptr(sc, ch->dir);
	rtn = ptr * ch->unit;
	snd_mtxunlock(sc->lock);

#if(0)
	device_printf(sc->dev, "envy24chan_getptr(): return %d\n",
	    rtn);
#endif
	return rtn;
}

static struct pcmchan_caps *
envy24chan_getcaps(kobj_t obj, void *data)
{
	struct sc_chinfo *ch = data;
	struct sc_info *sc = ch->parent;
	struct pcmchan_caps *rtn;

#if(0)
	device_printf(sc->dev, "envy24chan_getcaps()\n");
#endif
	snd_mtxlock(sc->lock);
	if (ch->dir == PCMDIR_PLAY) {
		if (sc->run[0] == 0)
			rtn = &envy24_playcaps;
		else
			rtn = &sc->caps[0];
	}
	else {
		if (sc->run[1] == 0)
			rtn = &envy24_reccaps;
		else
			rtn = &sc->caps[1];
	}
	snd_mtxunlock(sc->lock);

	return rtn;
}

static kobj_method_t envy24chan_methods[] = {
	KOBJMETHOD(channel_init,		envy24chan_init),
	KOBJMETHOD(channel_free,		envy24chan_free),
	KOBJMETHOD(channel_setformat,		envy24chan_setformat),
	KOBJMETHOD(channel_setspeed,		envy24chan_setspeed),
	KOBJMETHOD(channel_setblocksize,	envy24chan_setblocksize),
	KOBJMETHOD(channel_trigger,		envy24chan_trigger),
	KOBJMETHOD(channel_getptr,		envy24chan_getptr),
	KOBJMETHOD(channel_getcaps,		envy24chan_getcaps),
	KOBJMETHOD_END
};
CHANNEL_DECLARE(envy24chan);

/* -------------------------------------------------------------------- */

/* mixer interface */

static int
envy24mixer_init(struct snd_mixer *m)
{
	struct sc_info *sc = mix_getdevinfo(m);

#if(0)
	device_printf(sc->dev, "envy24mixer_init()\n");
#endif
	if (sc == NULL)
		return -1;

	/* set volume control rate */
	snd_mtxlock(sc->lock);
	envy24_wrmt(sc, ENVY24_MT_VOLRATE, 0x30, 1); /* 0x30 is default value */

	mix_setdevs(m, ENVY24_MIX_MASK);
	mix_setrecdevs(m, ENVY24_MIX_REC_MASK);
	snd_mtxunlock(sc->lock);

	return 0;
}

static int
envy24mixer_reinit(struct snd_mixer *m)
{
	struct sc_info *sc = mix_getdevinfo(m);

	if (sc == NULL)
		return -1;
#if(0)
	device_printf(sc->dev, "envy24mixer_reinit()\n");
#endif

	return 0;
}

static int
envy24mixer_uninit(struct snd_mixer *m)
{
	struct sc_info *sc = mix_getdevinfo(m);

	if (sc == NULL)
		return -1;
#if(0)
	device_printf(sc->dev, "envy24mixer_uninit()\n");
#endif

	return 0;
}

static int
envy24mixer_set(struct snd_mixer *m, unsigned dev, unsigned left, unsigned right)
{
	struct sc_info *sc = mix_getdevinfo(m);
	int ch = envy24_mixmap[dev];
	int hwch;
	int i;

	if (sc == NULL)
		return -1;
	if (dev == 0 && sc->cfg->codec->setvolume == NULL)
		return -1;
	if (dev != 0 && ch == -1)
		return -1;
	hwch = envy24_chanmap[ch];
#if(0)
	device_printf(sc->dev, "envy24mixer_set(m, %d, %d, %d)\n",
	    dev, left, right);
#endif

	snd_mtxlock(sc->lock);
	if (dev == 0) {
		for (i = 0; i < sc->dacn; i++) {
			sc->cfg->codec->setvolume(sc->dac[i], PCMDIR_PLAY, left, right);
		}
	}
	else {
		/* set volume value for hardware */
		if ((sc->left[hwch] = 100 - left) > ENVY24_VOL_MIN)
			sc->left[hwch] = ENVY24_VOL_MUTE;
		if ((sc->right[hwch] = 100 - right) > ENVY24_VOL_MIN)
			sc->right[hwch] = ENVY24_VOL_MUTE;

		/* set volume for record channel and running play channel */
		if (hwch > ENVY24_CHAN_PLAY_SPDIF || sc->chan[ch].run)
			envy24_setvolume(sc, hwch);
	}
	snd_mtxunlock(sc->lock);

	return right << 8 | left;
}

static u_int32_t
envy24mixer_setrecsrc(struct snd_mixer *m, u_int32_t src)
{
	struct sc_info *sc = mix_getdevinfo(m);
	int ch = envy24_mixmap[src];
#if(0)
	device_printf(sc->dev, "envy24mixer_setrecsrc(m, %d)\n", src);
#endif

	if (ch > ENVY24_CHAN_PLAY_SPDIF)
		sc->src = ch;
	return src;
}

static kobj_method_t envy24mixer_methods[] = {
	KOBJMETHOD(mixer_init,		envy24mixer_init),
	KOBJMETHOD(mixer_reinit,	envy24mixer_reinit),
	KOBJMETHOD(mixer_uninit,	envy24mixer_uninit),
	KOBJMETHOD(mixer_set,		envy24mixer_set),
	KOBJMETHOD(mixer_setrecsrc,	envy24mixer_setrecsrc),
	KOBJMETHOD_END
};
MIXER_DECLARE(envy24mixer);

/* -------------------------------------------------------------------- */

/* The interrupt handler */
static void
envy24_intr(void *p)
{
	struct sc_info *sc = (struct sc_info *)p;
	struct sc_chinfo *ch;
	u_int32_t ptr, dsize, feed;
	int i;

#if(0)
	device_printf(sc->dev, "envy24_intr()\n");
#endif
	snd_mtxlock(sc->lock);
	if (envy24_checkintr(sc, PCMDIR_PLAY)) {
#if(0)
		device_printf(sc->dev, "envy24_intr(): play\n");
#endif
		dsize = sc->psize / 4;
		ptr = dsize - envy24_rdmt(sc, ENVY24_MT_PCNT, 2) - 1;
#if(0)
		device_printf(sc->dev, "envy24_intr(): ptr = %d-->", ptr);
#endif
		ptr -= ptr % sc->blk[0];
		feed = (ptr + dsize - sc->intr[0]) % dsize; 
#if(0)
		printf("%d intr = %d feed = %d\n", ptr, sc->intr[0], feed);
#endif
		for (i = ENVY24_CHAN_PLAY_DAC1; i <= ENVY24_CHAN_PLAY_SPDIF; i++) {
			ch = &sc->chan[i];
#if(0)
			if (ch->run)
				device_printf(sc->dev, "envy24_intr(): chan[%d].blk = %d\n", i, ch->blk);
#endif
			if (ch->run && ch->blk <= feed) {
				snd_mtxunlock(sc->lock);
				chn_intr(ch->channel);
				snd_mtxlock(sc->lock);
			}
		}
		sc->intr[0] = ptr;
		envy24_updintr(sc, PCMDIR_PLAY);
	}
	if (envy24_checkintr(sc, PCMDIR_REC)) {
#if(0)
		device_printf(sc->dev, "envy24_intr(): rec\n");
#endif
		dsize = sc->rsize / 4;
		ptr = dsize - envy24_rdmt(sc, ENVY24_MT_RCNT, 2) - 1;
		ptr -= ptr % sc->blk[1];
		feed = (ptr + dsize - sc->intr[1]) % dsize; 
		for (i = ENVY24_CHAN_REC_ADC1; i <= ENVY24_CHAN_REC_SPDIF; i++) {
			ch = &sc->chan[i];
			if (ch->run && ch->blk <= feed) {
				snd_mtxunlock(sc->lock);
				chn_intr(ch->channel);
				snd_mtxlock(sc->lock);
			}
		}
		sc->intr[1] = ptr;
		envy24_updintr(sc, PCMDIR_REC);
	}
	snd_mtxunlock(sc->lock);

	return;
}

/*
 * Probe and attach the card
 */

static int
envy24_pci_probe(device_t dev)
{
	u_int16_t sv, sd;
	int i;

#if(0)
	printf("envy24_pci_probe()\n");
#endif
	if (pci_get_device(dev) == PCID_ENVY24 &&
	    pci_get_vendor(dev) == PCIV_ENVY24) {
		sv = pci_get_subvendor(dev);
		sd = pci_get_subdevice(dev);
		for (i = 0; cfg_table[i].subvendor != 0 || cfg_table[i].subdevice != 0; i++) {
			if (cfg_table[i].subvendor == sv &&
			    cfg_table[i].subdevice == sd) {
				break;
			}
		}
		device_set_desc(dev, cfg_table[i].name);
#if(0)
		printf("envy24_pci_probe(): return 0\n");
#endif
		return 0;
	}
	else {
#if(0)
		printf("envy24_pci_probe(): return ENXIO\n");
#endif
		return ENXIO;
	}
}

static void
envy24_dmapsetmap(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	struct sc_info *sc = (struct sc_info *)arg;

	sc->paddr = segs->ds_addr;
#if(0)
	device_printf(sc->dev, "envy24_dmapsetmap()\n");
	if (bootverbose) {
		printf("envy24(play): setmap %lx, %lx; ",
		    (unsigned long)segs->ds_addr,
		    (unsigned long)segs->ds_len);
		printf("%p -> %lx\n", sc->pmap, sc->paddr);
	}
#endif
}

static void
envy24_dmarsetmap(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	struct sc_info *sc = (struct sc_info *)arg;

	sc->raddr = segs->ds_addr;
#if(0)
	device_printf(sc->dev, "envy24_dmarsetmap()\n");
	if (bootverbose) {
		printf("envy24(record): setmap %lx, %lx; ",
		    (unsigned long)segs->ds_addr,
		    (unsigned long)segs->ds_len);
		printf("%p -> %lx\n", sc->rmap, sc->raddr);
	}
#endif
}

static void
envy24_dmafree(struct sc_info *sc)
{
#if(0)
	device_printf(sc->dev, "envy24_dmafree():");
	printf(" sc->raddr(0x%08x)", (u_int32_t)sc->raddr);
	printf(" sc->paddr(0x%08x)", (u_int32_t)sc->paddr);
	if (sc->rbuf) printf(" sc->rbuf(0x%08x)", (u_int32_t)sc->rbuf);
	else printf(" sc->rbuf(null)");
	if (sc->pbuf) printf(" sc->pbuf(0x%08x)\n", (u_int32_t)sc->pbuf);
	else printf(" sc->pbuf(null)\n");
#endif
#if(0)
	if (sc->raddr)
		bus_dmamap_unload(sc->dmat, sc->rmap);
	if (sc->paddr)
		bus_dmamap_unload(sc->dmat, sc->pmap);
	if (sc->rbuf)
		bus_dmamem_free(sc->dmat, sc->rbuf, sc->rmap);
	if (sc->pbuf)
		bus_dmamem_free(sc->dmat, sc->pbuf, sc->pmap);
#else
	bus_dmamap_unload(sc->dmat, sc->rmap);
	bus_dmamap_unload(sc->dmat, sc->pmap);
	bus_dmamem_free(sc->dmat, sc->rbuf, sc->rmap);
	bus_dmamem_free(sc->dmat, sc->pbuf, sc->pmap);
#endif

	sc->raddr = sc->paddr = 0;
	sc->pbuf = NULL;
	sc->rbuf = NULL;

	return;
}

static int
envy24_dmainit(struct sc_info *sc)
{

#if(0)
	device_printf(sc->dev, "envy24_dmainit()\n");
#endif
	/* init values */
	sc->psize = ENVY24_PLAY_BUFUNIT * ENVY24_SAMPLE_NUM;
	sc->rsize = ENVY24_REC_BUFUNIT * ENVY24_SAMPLE_NUM;
	sc->pbuf = NULL;
	sc->rbuf = NULL;
	sc->paddr = sc->raddr = 0;
	sc->blk[0] = sc->blk[1] = 0;

	/* allocate DMA buffer */
#if(0)
	device_printf(sc->dev, "envy24_dmainit(): bus_dmamem_alloc(): sc->pbuf\n");
#endif
	if (bus_dmamem_alloc(sc->dmat, (void **)&sc->pbuf, BUS_DMA_NOWAIT, &sc->pmap))
		goto bad;
#if(0)
	device_printf(sc->dev, "envy24_dmainit(): bus_dmamem_alloc(): sc->rbuf\n");
#endif
	if (bus_dmamem_alloc(sc->dmat, (void **)&sc->rbuf, BUS_DMA_NOWAIT, &sc->rmap))
		goto bad;
#if(0)
	device_printf(sc->dev, "envy24_dmainit(): bus_dmamem_load(): sc->pmap\n");
#endif
	if (bus_dmamap_load(sc->dmat, sc->pmap, sc->pbuf, sc->psize, envy24_dmapsetmap, sc, 0))
		goto bad;
#if(0)
	device_printf(sc->dev, "envy24_dmainit(): bus_dmamem_load(): sc->rmap\n");
#endif
	if (bus_dmamap_load(sc->dmat, sc->rmap, sc->rbuf, sc->rsize, envy24_dmarsetmap, sc, 0))
		goto bad;
	bzero(sc->pbuf, sc->psize);
	bzero(sc->rbuf, sc->rsize);

	/* set values to register */
#if(0)
	device_printf(sc->dev, "paddr(0x%08x)\n", sc->paddr);
#endif
	envy24_wrmt(sc, ENVY24_MT_PADDR, sc->paddr, 4);
#if(0)
	device_printf(sc->dev, "PADDR-->(0x%08x)\n", envy24_rdmt(sc, ENVY24_MT_PADDR, 4));
	device_printf(sc->dev, "psize(%ld)\n", sc->psize / 4 - 1);
#endif
	envy24_wrmt(sc, ENVY24_MT_PCNT, sc->psize / 4 - 1, 2);
#if(0)
	device_printf(sc->dev, "PCNT-->(%ld)\n", envy24_rdmt(sc, ENVY24_MT_PCNT, 2));
#endif
	envy24_wrmt(sc, ENVY24_MT_RADDR, sc->raddr, 4);
	envy24_wrmt(sc, ENVY24_MT_RCNT, sc->rsize / 4 - 1, 2);

	return 0;
 bad:
	envy24_dmafree(sc);
	return ENOSPC;
}

static void
envy24_putcfg(struct sc_info *sc)
{
	device_printf(sc->dev, "system configuration\n");
	printf("  SubVendorID: 0x%04x, SubDeviceID: 0x%04x\n",
	    sc->cfg->subvendor, sc->cfg->subdevice);
	printf("  XIN2 Clock Source: ");
	switch (sc->cfg->scfg & PCIM_SCFG_XIN2) {
	case 0x00:
		printf("22.5792MHz(44.1kHz*512)\n");
		break;
	case 0x40:
		printf("16.9344MHz(44.1kHz*384)\n");
		break;
	case 0x80:
		printf("from external clock synthesizer chip\n");
		break;
	default:
		printf("illegal system setting\n");
	}
	printf("  MPU-401 UART(s) #: ");
	if (sc->cfg->scfg & PCIM_SCFG_MPU)
		printf("2\n");
	else
		printf("1\n");
	printf("  AC'97 codec: ");
	if (sc->cfg->scfg & PCIM_SCFG_AC97)
		printf("not exist\n");
	else
		printf("exist\n");
	printf("  ADC #: ");
	printf("%d\n", sc->adcn);
	printf("  DAC #: ");
	printf("%d\n", sc->dacn);
	printf("  Multi-track converter type: ");
	if ((sc->cfg->acl & PCIM_ACL_MTC) == 0) {
		printf("AC'97(SDATA_OUT:");
		if (sc->cfg->acl & PCIM_ACL_OMODE)
			printf("packed");
		else
			printf("split");
		printf("|SDATA_IN:");
		if (sc->cfg->acl & PCIM_ACL_IMODE)
			printf("packed");
		else
			printf("split");
		printf(")\n");
	}
	else {
		printf("I2S(");
		if (sc->cfg->i2s & PCIM_I2S_VOL)
			printf("with volume, ");
		if (sc->cfg->i2s & PCIM_I2S_96KHZ)
			printf("96KHz support, ");
		switch (sc->cfg->i2s & PCIM_I2S_RES) {
		case PCIM_I2S_16BIT:
			printf("16bit resolution, ");
			break;
		case PCIM_I2S_18BIT:
			printf("18bit resolution, ");
			break;
		case PCIM_I2S_20BIT:
			printf("20bit resolution, ");
			break;
		case PCIM_I2S_24BIT:
			printf("24bit resolution, ");
			break;
		}
		printf("ID#0x%x)\n", sc->cfg->i2s & PCIM_I2S_ID);
	}
	printf("  S/PDIF(IN/OUT): ");
	if (sc->cfg->spdif & PCIM_SPDIF_IN)
		printf("1/");
	else
		printf("0/");
	if (sc->cfg->spdif & PCIM_SPDIF_OUT)
		printf("1 ");
	else
		printf("0 ");
	if (sc->cfg->spdif & (PCIM_SPDIF_IN | PCIM_SPDIF_OUT))
		printf("ID# 0x%02x\n", (sc->cfg->spdif & PCIM_SPDIF_ID) >> 2);
	printf("  GPIO(mask/dir/state): 0x%02x/0x%02x/0x%02x\n",
	    sc->cfg->gpiomask, sc->cfg->gpiodir, sc->cfg->gpiostate);
}

static int
envy24_init(struct sc_info *sc)
{
	u_int32_t data;
#if(0)
	int rtn;
#endif
	int i;
	u_int32_t sv, sd;


#if(0)
	device_printf(sc->dev, "envy24_init()\n");
#endif

	/* reset chip */
	envy24_wrcs(sc, ENVY24_CCS_CTL, ENVY24_CCS_CTL_RESET | ENVY24_CCS_CTL_NATIVE, 1);
	DELAY(200);
	envy24_wrcs(sc, ENVY24_CCS_CTL, ENVY24_CCS_CTL_NATIVE, 1);
	DELAY(200);

	/* legacy hardware disable */
	data = pci_read_config(sc->dev, PCIR_LAC, 2);
	data |= PCIM_LAC_DISABLE;
	pci_write_config(sc->dev, PCIR_LAC, data, 2);

	/* check system configuration */
	sc->cfg = NULL;
	for (i = 0; cfg_table[i].subvendor != 0 || cfg_table[i].subdevice != 0; i++) {
		/* 1st: search configuration from table */
		sv = pci_get_subvendor(sc->dev);
		sd = pci_get_subdevice(sc->dev);
		if (sv == cfg_table[i].subvendor && sd == cfg_table[i].subdevice) {
#if(0)
			device_printf(sc->dev, "Set configuration from table\n");
#endif
			sc->cfg = &cfg_table[i];
			break;
		}
	}
	if (sc->cfg == NULL) {
		/* 2nd: read configuration from table */
		sc->cfg = envy24_rom2cfg(sc);
	}
	sc->adcn = ((sc->cfg->scfg & PCIM_SCFG_ADC) >> 2) + 1;
	sc->dacn = (sc->cfg->scfg & PCIM_SCFG_DAC) + 1;

	if (1 /* bootverbose */) {
		envy24_putcfg(sc);
	}

	/* set system configuration */
	pci_write_config(sc->dev, PCIR_SCFG, sc->cfg->scfg, 1);
	pci_write_config(sc->dev, PCIR_ACL, sc->cfg->acl, 1);
	pci_write_config(sc->dev, PCIR_I2S, sc->cfg->i2s, 1);
	pci_write_config(sc->dev, PCIR_SPDIF, sc->cfg->spdif, 1);
	envy24_gpiosetmask(sc, sc->cfg->gpiomask);
	envy24_gpiosetdir(sc, sc->cfg->gpiodir);
	envy24_gpiowr(sc, sc->cfg->gpiostate);
	for (i = 0; i < sc->adcn; i++) {
		sc->adc[i] = sc->cfg->codec->create(sc->dev, sc, PCMDIR_REC, i);
		sc->cfg->codec->init(sc->adc[i]);
	}
	for (i = 0; i < sc->dacn; i++) {
		sc->dac[i] = sc->cfg->codec->create(sc->dev, sc, PCMDIR_PLAY, i);
		sc->cfg->codec->init(sc->dac[i]);
	}

	/* initialize DMA buffer */
#if(0)
	device_printf(sc->dev, "envy24_init(): initialize DMA buffer\n");
#endif
	if (envy24_dmainit(sc))
		return ENOSPC;

	/* initialize status */
	sc->run[0] = sc->run[1] = 0;
	sc->intr[0] = sc->intr[1] = 0;
	sc->speed = 0;
	sc->caps[0].fmtlist = envy24_playfmt;
	sc->caps[1].fmtlist = envy24_recfmt;

	/* set channel router */
	envy24_route(sc, ENVY24_ROUTE_DAC_1, ENVY24_ROUTE_CLASS_MIX, 0, 0);
	envy24_route(sc, ENVY24_ROUTE_DAC_SPDIF, ENVY24_ROUTE_CLASS_DMA, 0, 0);
	/* envy24_route(sc, ENVY24_ROUTE_DAC_SPDIF, ENVY24_ROUTE_CLASS_MIX, 0, 0); */

	/* set macro interrupt mask */
	data = envy24_rdcs(sc, ENVY24_CCS_IMASK, 1);
	envy24_wrcs(sc, ENVY24_CCS_IMASK, data & ~ENVY24_CCS_IMASK_PMT, 1);
	data = envy24_rdcs(sc, ENVY24_CCS_IMASK, 1);
#if(0)
	device_printf(sc->dev, "envy24_init(): CCS_IMASK-->0x%02x\n", data);
#endif

	return 0;
}

static int
envy24_alloc_resource(struct sc_info *sc)
{
	/* allocate I/O port resource */
	sc->csid = PCIR_CCS;
	sc->cs = bus_alloc_resource_any(sc->dev, SYS_RES_IOPORT,
	    &sc->csid, RF_ACTIVE);
	sc->ddmaid = PCIR_DDMA;
	sc->ddma = bus_alloc_resource_any(sc->dev, SYS_RES_IOPORT,
	    &sc->ddmaid, RF_ACTIVE);
	sc->dsid = PCIR_DS;
	sc->ds = bus_alloc_resource_any(sc->dev, SYS_RES_IOPORT,
	    &sc->dsid, RF_ACTIVE);
	sc->mtid = PCIR_MT;
	sc->mt = bus_alloc_resource_any(sc->dev, SYS_RES_IOPORT,
	    &sc->mtid, RF_ACTIVE);
	if (!sc->cs || !sc->ddma || !sc->ds || !sc->mt) {
		device_printf(sc->dev, "unable to map IO port space\n");
		return ENXIO;
	}
	sc->cst = rman_get_bustag(sc->cs);
	sc->csh = rman_get_bushandle(sc->cs);
	sc->ddmat = rman_get_bustag(sc->ddma);
	sc->ddmah = rman_get_bushandle(sc->ddma);
	sc->dst = rman_get_bustag(sc->ds);
	sc->dsh = rman_get_bushandle(sc->ds);
	sc->mtt = rman_get_bustag(sc->mt);
	sc->mth = rman_get_bushandle(sc->mt);
#if(0)
	device_printf(sc->dev,
	    "IO port register values\nCCS: 0x%lx\nDDMA: 0x%lx\nDS: 0x%lx\nMT: 0x%lx\n",
	    pci_read_config(sc->dev, PCIR_CCS, 4),
	    pci_read_config(sc->dev, PCIR_DDMA, 4),
	    pci_read_config(sc->dev, PCIR_DS, 4),
	    pci_read_config(sc->dev, PCIR_MT, 4));
#endif

	/* allocate interrupt resource */
	sc->irqid = 0;
	sc->irq = bus_alloc_resource_any(sc->dev, SYS_RES_IRQ, &sc->irqid,
				 RF_ACTIVE | RF_SHAREABLE);
	if (!sc->irq ||
	    snd_setup_intr(sc->dev, sc->irq, INTR_MPSAFE, envy24_intr, sc, &sc->ih)) {
		device_printf(sc->dev, "unable to map interrupt\n");
		return ENXIO;
	}

	/* allocate DMA resource */
	if (bus_dma_tag_create(/*parent*/bus_get_dma_tag(sc->dev),
	    /*alignment*/4,
	    /*boundary*/0,
	    /*lowaddr*/BUS_SPACE_MAXADDR_ENVY24,
	    /*highaddr*/BUS_SPACE_MAXADDR_ENVY24,
	    /*filter*/NULL, /*filterarg*/NULL,
	    /*maxsize*/BUS_SPACE_MAXSIZE_ENVY24,
	    /*nsegments*/1, /*maxsegsz*/0x3ffff,
	    /*flags*/0, /*lockfunc*/busdma_lock_mutex,
	    /*lockarg*/&Giant, &sc->dmat) != 0) {
		device_printf(sc->dev, "unable to create dma tag\n");
		return ENXIO;
	}

	return 0;
}

static int
envy24_pci_attach(device_t dev)
{
	struct sc_info 		*sc;
	char 			status[SND_STATUSLEN];
	int			err = 0;
	int			i;

#if(0)
	device_printf(dev, "envy24_pci_attach()\n");
#endif
	/* get sc_info data area */
	if ((sc = malloc(sizeof(*sc), M_ENVY24, M_NOWAIT)) == NULL) {
		device_printf(dev, "cannot allocate softc\n");
		return ENXIO;
	}

	bzero(sc, sizeof(*sc));
	sc->lock = snd_mtxcreate(device_get_nameunit(dev), "snd_envy24 softc");
	sc->dev = dev;

	/* initialize PCI interface */
	pci_enable_busmaster(dev);

	/* allocate resources */
	err = envy24_alloc_resource(sc);
	if (err) {
		device_printf(dev, "unable to allocate system resources\n");
		goto bad;
	}

	/* initialize card */
	err = envy24_init(sc);
	if (err) {
		device_printf(dev, "unable to initialize the card\n");
		goto bad;
	}

	/* set multi track mixer */
	mixer_init(dev, &envy24mixer_class, sc);

	/* set channel information */
	err = pcm_register(dev, sc, 5, 2 + sc->adcn);
	if (err)
		goto bad;
	sc->chnum = 0;
	for (i = 0; i < 5; i++) {
		pcm_addchan(dev, PCMDIR_PLAY, &envy24chan_class, sc);
		sc->chnum++;
	}
	for (i = 0; i < 2 + sc->adcn; i++) {
		pcm_addchan(dev, PCMDIR_REC, &envy24chan_class, sc);
		sc->chnum++;
	}

	/* set status iformation */
	snprintf(status, SND_STATUSLEN,
	    "at io 0x%jx:%jd,0x%jx:%jd,0x%jx:%jd,0x%jx:%jd irq %jd",
	    rman_get_start(sc->cs),
	    rman_get_end(sc->cs) - rman_get_start(sc->cs) + 1,
	    rman_get_start(sc->ddma),
	    rman_get_end(sc->ddma) - rman_get_start(sc->ddma) + 1,
	    rman_get_start(sc->ds),
	    rman_get_end(sc->ds) - rman_get_start(sc->ds) + 1,
	    rman_get_start(sc->mt),
	    rman_get_end(sc->mt) - rman_get_start(sc->mt) + 1,
	    rman_get_start(sc->irq));
	pcm_setstatus(dev, status);

	return 0;

bad:
	if (sc->ih)
		bus_teardown_intr(dev, sc->irq, sc->ih);
	if (sc->irq)
		bus_release_resource(dev, SYS_RES_IRQ, sc->irqid, sc->irq);
	envy24_dmafree(sc);
	if (sc->dmat)
		bus_dma_tag_destroy(sc->dmat);
	if (sc->cfg->codec->destroy != NULL) {
                for (i = 0; i < sc->adcn; i++)
                        sc->cfg->codec->destroy(sc->adc[i]);
                for (i = 0; i < sc->dacn; i++)
                        sc->cfg->codec->destroy(sc->dac[i]);
        }
	envy24_cfgfree(sc->cfg);
	if (sc->cs)
		bus_release_resource(dev, SYS_RES_IOPORT, sc->csid, sc->cs);
	if (sc->ddma)
		bus_release_resource(dev, SYS_RES_IOPORT, sc->ddmaid, sc->ddma);
	if (sc->ds)
		bus_release_resource(dev, SYS_RES_IOPORT, sc->dsid, sc->ds);
	if (sc->mt)
		bus_release_resource(dev, SYS_RES_IOPORT, sc->mtid, sc->mt);
	if (sc->lock)
		snd_mtxfree(sc->lock);
	free(sc, M_ENVY24);
	return err;
}

static int
envy24_pci_detach(device_t dev)
{
	struct sc_info *sc;
	int r;
	int i;

#if(0)
	device_printf(dev, "envy24_pci_detach()\n");
#endif
	sc = pcm_getdevinfo(dev);
	if (sc == NULL)
		return 0;
	r = pcm_unregister(dev);
	if (r)
		return r;

	envy24_dmafree(sc);
	if (sc->cfg->codec->destroy != NULL) {
		for (i = 0; i < sc->adcn; i++)
			sc->cfg->codec->destroy(sc->adc[i]);
		for (i = 0; i < sc->dacn; i++)
			sc->cfg->codec->destroy(sc->dac[i]);
	}
	envy24_cfgfree(sc->cfg);
	bus_dma_tag_destroy(sc->dmat);
	bus_teardown_intr(dev, sc->irq, sc->ih);
	bus_release_resource(dev, SYS_RES_IRQ, sc->irqid, sc->irq);
	bus_release_resource(dev, SYS_RES_IOPORT, sc->csid, sc->cs);
	bus_release_resource(dev, SYS_RES_IOPORT, sc->ddmaid, sc->ddma);
	bus_release_resource(dev, SYS_RES_IOPORT, sc->dsid, sc->ds);
	bus_release_resource(dev, SYS_RES_IOPORT, sc->mtid, sc->mt);
	snd_mtxfree(sc->lock);
	free(sc, M_ENVY24);
	return 0;
}

static device_method_t envy24_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		envy24_pci_probe),
	DEVMETHOD(device_attach,	envy24_pci_attach),
	DEVMETHOD(device_detach,	envy24_pci_detach),
	{ 0, 0 }
};

static driver_t envy24_driver = {
	"pcm",
	envy24_methods,
	PCM_SOFTC_SIZE,
};

DRIVER_MODULE(snd_envy24, pci, envy24_driver, pcm_devclass, 0, 0);
MODULE_DEPEND(snd_envy24, sound, SOUND_MINVER, SOUND_PREFVER, SOUND_MAXVER);
MODULE_DEPEND(snd_envy24, snd_spicds, 1, 1, 1);
MODULE_VERSION(snd_envy24, 1);
