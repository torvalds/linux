/*-
 * Copyright (c) 2014-2016 Jared D. McNeill <jmcneill@invisible.ca>
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Allwinner A10/A20 and H3 Audio Codec
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/condvar.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/gpio.h>

#include <machine/bus.h>

#include <dev/sound/pcm/sound.h>
#include <dev/sound/chip.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/gpio/gpiobusvar.h>

#include <dev/extres/clk/clk.h>
#include <dev/extres/hwreset/hwreset.h>

#include "sunxi_dma_if.h"
#include "mixer_if.h"

struct a10codec_info;

struct a10codec_config {
	/* mixer class */
	struct kobj_class *mixer_class;

	/* toggle DAC/ADC mute */
	void		(*mute)(struct a10codec_info *, int, int);

	/* DRQ types */
	u_int		drqtype_codec;
	u_int		drqtype_sdram;

	/* register map */
	bus_size_t	DPC,
			DAC_FIFOC,
			DAC_FIFOS,
			DAC_TXDATA,
			ADC_FIFOC,
			ADC_FIFOS,
			ADC_RXDATA,
			DAC_CNT,
			ADC_CNT;
};

#define	TX_TRIG_LEVEL	0xf
#define	RX_TRIG_LEVEL	0x7
#define	DRQ_CLR_CNT	0x3

#define	AC_DAC_DPC(_sc)		((_sc)->cfg->DPC)	
#define	 DAC_DPC_EN_DA			0x80000000
#define	AC_DAC_FIFOC(_sc)	((_sc)->cfg->DAC_FIFOC)
#define	 DAC_FIFOC_FS_SHIFT		29
#define	 DAC_FIFOC_FS_MASK		(7U << DAC_FIFOC_FS_SHIFT)
#define	  DAC_FS_48KHZ			0
#define	  DAC_FS_32KHZ			1
#define	  DAC_FS_24KHZ			2
#define	  DAC_FS_16KHZ			3
#define	  DAC_FS_12KHZ			4
#define	  DAC_FS_8KHZ			5
#define	  DAC_FS_192KHZ			6
#define	  DAC_FS_96KHZ			7
#define	 DAC_FIFOC_FIFO_MODE_SHIFT	24
#define	 DAC_FIFOC_FIFO_MODE_MASK	(3U << DAC_FIFOC_FIFO_MODE_SHIFT)
#define	  FIFO_MODE_24_31_8		0
#define	  FIFO_MODE_16_31_16		0
#define	  FIFO_MODE_16_15_0		1
#define	 DAC_FIFOC_DRQ_CLR_CNT_SHIFT	21
#define	 DAC_FIFOC_DRQ_CLR_CNT_MASK	(3U << DAC_FIFOC_DRQ_CLR_CNT_SHIFT)
#define	 DAC_FIFOC_TX_TRIG_LEVEL_SHIFT	8
#define	 DAC_FIFOC_TX_TRIG_LEVEL_MASK	(0x7f << DAC_FIFOC_TX_TRIG_LEVEL_SHIFT)
#define	 DAC_FIFOC_MONO_EN		(1U << 6)
#define	 DAC_FIFOC_TX_BITS		(1U << 5)
#define	 DAC_FIFOC_DRQ_EN		(1U << 4)
#define	 DAC_FIFOC_FIFO_FLUSH		(1U << 0)
#define	AC_DAC_FIFOS(_sc)	((_sc)->cfg->DAC_FIFOS)
#define	AC_DAC_TXDATA(_sc)	((_sc)->cfg->DAC_TXDATA)
#define	AC_ADC_FIFOC(_sc)	((_sc)->cfg->ADC_FIFOC)
#define	 ADC_FIFOC_FS_SHIFT		29
#define	 ADC_FIFOC_FS_MASK		(7U << ADC_FIFOC_FS_SHIFT)
#define	  ADC_FS_48KHZ		0
#define	 ADC_FIFOC_EN_AD		(1U << 28)
#define	 ADC_FIFOC_RX_FIFO_MODE		(1U << 24)
#define	 ADC_FIFOC_RX_TRIG_LEVEL_SHIFT	8
#define	 ADC_FIFOC_RX_TRIG_LEVEL_MASK	(0x1f << ADC_FIFOC_RX_TRIG_LEVEL_SHIFT)
#define	 ADC_FIFOC_MONO_EN		(1U << 7)
#define	 ADC_FIFOC_RX_BITS		(1U << 6)
#define	 ADC_FIFOC_DRQ_EN		(1U << 4)
#define	 ADC_FIFOC_FIFO_FLUSH		(1U << 1)
#define	AC_ADC_FIFOS(_sc)	((_sc)->cfg->ADC_FIFOS)
#define	AC_ADC_RXDATA(_sc)	((_sc)->cfg->ADC_RXDATA)
#define	AC_DAC_CNT(_sc)		((_sc)->cfg->DAC_CNT)
#define	AC_ADC_CNT(_sc)		((_sc)->cfg->ADC_CNT)

static uint32_t a10codec_fmt[] = {
	SND_FORMAT(AFMT_S16_LE, 1, 0),
	SND_FORMAT(AFMT_S16_LE, 2, 0),
	0
};

static struct pcmchan_caps a10codec_pcaps = { 8000, 192000, a10codec_fmt, 0 };
static struct pcmchan_caps a10codec_rcaps = { 8000, 48000, a10codec_fmt, 0 };

struct a10codec_info;

struct a10codec_chinfo {
	struct snd_dbuf		*buffer;
	struct pcm_channel	*channel;	
	struct a10codec_info	*parent;
	bus_dmamap_t		dmamap;
	void			*dmaaddr;
	bus_addr_t		physaddr;
	bus_size_t		fifo;
	device_t		dmac;
	void			*dmachan;

	int			dir;
	int			run;
	uint32_t		pos;
	uint32_t		format;
	uint32_t		blocksize;
	uint32_t		speed;
};

struct a10codec_info {
	device_t		dev;
	struct resource		*res[3];
	struct mtx		*lock;
	bus_dma_tag_t		dmat;
	unsigned		dmasize;
	void			*ih;

	struct a10codec_config	*cfg;

	struct a10codec_chinfo	play;
	struct a10codec_chinfo	rec;
};

static struct resource_spec a10codec_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_MEMORY,	1,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },
	{ -1, 0 }
};

#define	CODEC_READ(sc, reg)		bus_read_4((sc)->res[0], (reg))
#define	CODEC_WRITE(sc, reg, val)	bus_write_4((sc)->res[0], (reg), (val))

/*
 * A10/A20 mixer interface
 */

#define	A10_DAC_ACTL	0x10
#define	 A10_DACAREN			(1U << 31)
#define	 A10_DACALEN			(1U << 30)
#define	 A10_MIXEN			(1U << 29)
#define	 A10_DACPAS			(1U << 8)
#define	 A10_PAMUTE			(1U << 6)
#define	 A10_PAVOL_SHIFT		0
#define	 A10_PAVOL_MASK			(0x3f << A10_PAVOL_SHIFT)
#define	A10_ADC_ACTL	0x28
#define	 A10_ADCREN			(1U << 31)
#define	 A10_ADCLEN			(1U << 30)
#define	 A10_PREG1EN			(1U << 29)
#define	 A10_PREG2EN			(1U << 28)
#define	 A10_VMICEN			(1U << 27)
#define	 A10_ADCG_SHIFT			20
#define	 A10_ADCG_MASK			(7U << A10_ADCG_SHIFT)
#define	 A10_ADCIS_SHIFT		17
#define	 A10_ADCIS_MASK			(7U << A10_ADCIS_SHIFT)
#define	  A10_ADC_IS_LINEIN			0
#define	  A10_ADC_IS_FMIN			1
#define	  A10_ADC_IS_MIC1			2
#define	  A10_ADC_IS_MIC2			3
#define	  A10_ADC_IS_MIC1_L_MIC2_R		4
#define	  A10_ADC_IS_MIC1_LR_MIC2_LR		5
#define	  A10_ADC_IS_OMIX			6
#define	  A10_ADC_IS_LINEIN_L_MIC1_R		7
#define	 A10_LNRDF			(1U << 16)
#define	 A10_LNPREG_SHIFT		13
#define	 A10_LNPREG_MASK		(7U << A10_LNPREG_SHIFT)
#define	 A10_PA_EN			(1U << 4)
#define	 A10_DDE			(1U << 3)

static int
a10_mixer_init(struct snd_mixer *m)
{
	struct a10codec_info *sc = mix_getdevinfo(m);
	uint32_t val;

	mix_setdevs(m, SOUND_MASK_VOLUME | SOUND_MASK_LINE | SOUND_MASK_RECLEV);
	mix_setrecdevs(m, SOUND_MASK_LINE | SOUND_MASK_LINE1 | SOUND_MASK_MIC);

	/* Unmute input source to PA */
	val = CODEC_READ(sc, A10_DAC_ACTL);
	val |= A10_PAMUTE;
	CODEC_WRITE(sc, A10_DAC_ACTL, val);

	/* Enable PA */
	val = CODEC_READ(sc, A10_ADC_ACTL);
	val |= A10_PA_EN;
	CODEC_WRITE(sc, A10_ADC_ACTL, val);

	return (0);
}

static const struct a10_mixer {
	unsigned reg;
	unsigned mask;
	unsigned shift;
} a10_mixers[SOUND_MIXER_NRDEVICES] = {
	[SOUND_MIXER_VOLUME]	= { A10_DAC_ACTL, A10_PAVOL_MASK,
				    A10_PAVOL_SHIFT },
	[SOUND_MIXER_LINE]	= { A10_ADC_ACTL, A10_LNPREG_MASK,
				    A10_LNPREG_SHIFT },
	[SOUND_MIXER_RECLEV]	= { A10_ADC_ACTL, A10_ADCG_MASK,
				    A10_ADCG_SHIFT },
}; 

static int
a10_mixer_set(struct snd_mixer *m, unsigned dev, unsigned left,
    unsigned right)
{
	struct a10codec_info *sc = mix_getdevinfo(m);
	uint32_t val;
	unsigned nvol, max;

	max = a10_mixers[dev].mask >> a10_mixers[dev].shift;
	nvol = (left * max) / 100;

	val = CODEC_READ(sc, a10_mixers[dev].reg);
	val &= ~a10_mixers[dev].mask;
	val |= (nvol << a10_mixers[dev].shift);
	CODEC_WRITE(sc, a10_mixers[dev].reg, val);

	left = right = (left * 100) / max;
	return (left | (right << 8));
}

static uint32_t
a10_mixer_setrecsrc(struct snd_mixer *m, uint32_t src)
{
	struct a10codec_info *sc = mix_getdevinfo(m);
	uint32_t val;

	val = CODEC_READ(sc, A10_ADC_ACTL);

	switch (src) {
	case SOUND_MASK_LINE:	/* line-in */
		val &= ~A10_ADCIS_MASK;
		val |= (A10_ADC_IS_LINEIN << A10_ADCIS_SHIFT);
		break;
	case SOUND_MASK_MIC:	/* MIC1 */
		val &= ~A10_ADCIS_MASK;
		val |= (A10_ADC_IS_MIC1 << A10_ADCIS_SHIFT);
		break;
	case SOUND_MASK_LINE1:	/* MIC2 */
		val &= ~A10_ADCIS_MASK;
		val |= (A10_ADC_IS_MIC2 << A10_ADCIS_SHIFT);
		break;
	default:
		break;
	}

	CODEC_WRITE(sc, A10_ADC_ACTL, val);

	switch ((val & A10_ADCIS_MASK) >> A10_ADCIS_SHIFT) {
	case A10_ADC_IS_LINEIN:
		return (SOUND_MASK_LINE);
	case A10_ADC_IS_MIC1:
		return (SOUND_MASK_MIC);
	case A10_ADC_IS_MIC2:
		return (SOUND_MASK_LINE1);
	default:
		return (0);
	}
}

static void
a10_mute(struct a10codec_info *sc, int mute, int dir)
{
	uint32_t val;

	if (dir == PCMDIR_PLAY) {
		val = CODEC_READ(sc, A10_DAC_ACTL);
		if (mute) {
			/* Disable DAC analog l/r channels and output mixer */
			val &= ~A10_DACAREN;
			val &= ~A10_DACALEN;
			val &= ~A10_DACPAS;
		} else {
			/* Enable DAC analog l/r channels and output mixer */
			val |= A10_DACAREN;
			val |= A10_DACALEN;
			val |= A10_DACPAS;
		}
		CODEC_WRITE(sc, A10_DAC_ACTL, val);
	} else {
		val = CODEC_READ(sc, A10_ADC_ACTL);
		if (mute) {
			/* Disable ADC analog l/r channels, MIC1 preamp,
			 * and VMIC pin voltage
			 */
			val &= ~A10_ADCREN;
			val &= ~A10_ADCLEN;
			val &= ~A10_PREG1EN;
			val &= ~A10_VMICEN;
		} else {
			/* Enable ADC analog l/r channels, MIC1 preamp,
			 * and VMIC pin voltage
			 */
			val |= A10_ADCREN;
			val |= A10_ADCLEN;
			val |= A10_PREG1EN;
			val |= A10_VMICEN;
		}
		CODEC_WRITE(sc, A10_ADC_ACTL, val);
	}
}

static kobj_method_t a10_mixer_methods[] = {
	KOBJMETHOD(mixer_init,		a10_mixer_init),
	KOBJMETHOD(mixer_set,		a10_mixer_set),
	KOBJMETHOD(mixer_setrecsrc,	a10_mixer_setrecsrc),
	KOBJMETHOD_END
};
MIXER_DECLARE(a10_mixer);


/*
 * H3 mixer interface
 */

#define	H3_PR_CFG		0x00
#define	 H3_AC_PR_RST		(1 << 18)
#define	 H3_AC_PR_RW		(1 << 24)
#define	 H3_AC_PR_ADDR_SHIFT	16
#define	 H3_AC_PR_ADDR_MASK	(0x1f << H3_AC_PR_ADDR_SHIFT)
#define	 H3_ACDA_PR_WDAT_SHIFT	8
#define	 H3_ACDA_PR_WDAT_MASK	(0xff << H3_ACDA_PR_WDAT_SHIFT)
#define	 H3_ACDA_PR_RDAT_SHIFT	0
#define	 H3_ACDA_PR_RDAT_MASK	(0xff << H3_ACDA_PR_RDAT_SHIFT)

#define	H3_LOMIXSC		0x01
#define	 H3_LOMIXSC_LDAC	(1 << 1)
#define	H3_ROMIXSC		0x02
#define	 H3_ROMIXSC_RDAC	(1 << 1)
#define	H3_DAC_PA_SRC		0x03
#define	 H3_DACAREN		(1 << 7)
#define	 H3_DACALEN		(1 << 6)
#define	 H3_RMIXEN		(1 << 5)
#define	 H3_LMIXEN		(1 << 4)
#define	H3_LINEIN_GCTR		0x05
#define	 H3_LINEING_SHIFT	4
#define	 H3_LINEING_MASK	(0x7 << H3_LINEING_SHIFT)
#define	H3_MIC_GCTR		0x06
#define	 H3_MIC1_GAIN_SHIFT	4
#define	 H3_MIC1_GAIN_MASK	(0x7 << H3_MIC1_GAIN_SHIFT)
#define	 H3_MIC2_GAIN_SHIFT	0
#define	 H3_MIC2_GAIN_MASK	(0x7 << H3_MIC2_GAIN_SHIFT)
#define	H3_PAEN_CTR		0x07
#define	 H3_LINEOUTEN		(1 << 7)
#define	H3_LINEOUT_VOLC		0x09
#define	 H3_LINEOUTVOL_SHIFT	3
#define	 H3_LINEOUTVOL_MASK	(0x1f << H3_LINEOUTVOL_SHIFT)
#define	H3_MIC2G_LINEOUT_CTR	0x0a
#define	 H3_LINEOUT_LSEL	(1 << 3)
#define	 H3_LINEOUT_RSEL	(1 << 2)
#define	H3_LADCMIXSC		0x0c
#define	H3_RADCMIXSC		0x0d
#define	 H3_ADCMIXSC_MIC1	(1 << 6)
#define	 H3_ADCMIXSC_MIC2	(1 << 5)
#define	 H3_ADCMIXSC_LINEIN	(1 << 2)
#define	 H3_ADCMIXSC_OMIXER	(3 << 0)
#define	H3_ADC_AP_EN		0x0f
#define	 H3_ADCREN		(1 << 7)
#define	 H3_ADCLEN		(1 << 6)
#define	 H3_ADCG_SHIFT		0
#define	 H3_ADCG_MASK		(0x7 << H3_ADCG_SHIFT)

static u_int 
h3_pr_read(struct a10codec_info *sc, u_int addr)
{
	uint32_t val;

	/* Read current value */
	val = bus_read_4(sc->res[1], H3_PR_CFG);

	/* De-assert reset */
	val |= H3_AC_PR_RST;
	bus_write_4(sc->res[1], H3_PR_CFG, val);

	/* Read mode */
	val &= ~H3_AC_PR_RW;
	bus_write_4(sc->res[1], H3_PR_CFG, val);

	/* Set address */
	val &= ~H3_AC_PR_ADDR_MASK;
	val |= (addr << H3_AC_PR_ADDR_SHIFT);
	bus_write_4(sc->res[1], H3_PR_CFG, val);

	/* Read data */
	return (bus_read_4(sc->res[1], H3_PR_CFG) & H3_ACDA_PR_RDAT_MASK);
}

static void
h3_pr_write(struct a10codec_info *sc, u_int addr, u_int data)
{
	uint32_t val;

	/* Read current value */
	val = bus_read_4(sc->res[1], H3_PR_CFG);

	/* De-assert reset */
	val |= H3_AC_PR_RST;
	bus_write_4(sc->res[1], H3_PR_CFG, val);

	/* Set address */
	val &= ~H3_AC_PR_ADDR_MASK;
	val |= (addr << H3_AC_PR_ADDR_SHIFT);
	bus_write_4(sc->res[1], H3_PR_CFG, val);

	/* Write data */
	val &= ~H3_ACDA_PR_WDAT_MASK;
	val |= (data << H3_ACDA_PR_WDAT_SHIFT);
	bus_write_4(sc->res[1], H3_PR_CFG, val);

	/* Write mode */
	val |= H3_AC_PR_RW;
	bus_write_4(sc->res[1], H3_PR_CFG, val);
}

static void
h3_pr_set_clear(struct a10codec_info *sc, u_int addr, u_int set, u_int clr)
{
	u_int old, new;

	old = h3_pr_read(sc, addr);
	new = set | (old & ~clr);
	h3_pr_write(sc, addr, new);
}

static int
h3_mixer_init(struct snd_mixer *m)
{
	struct a10codec_info *sc = mix_getdevinfo(m);

	mix_setdevs(m, SOUND_MASK_PCM | SOUND_MASK_VOLUME | SOUND_MASK_RECLEV |
	    SOUND_MASK_MIC | SOUND_MASK_LINE | SOUND_MASK_LINE1);
	mix_setrecdevs(m, SOUND_MASK_MIC | SOUND_MASK_LINE | SOUND_MASK_LINE1 |
	    SOUND_MASK_IMIX);

	pcm_setflags(sc->dev, pcm_getflags(sc->dev) | SD_F_SOFTPCMVOL);

	/* Right & Left LINEOUT enable */
	h3_pr_set_clear(sc, H3_PAEN_CTR, H3_LINEOUTEN, 0);
	h3_pr_set_clear(sc, H3_MIC2G_LINEOUT_CTR,
	    H3_LINEOUT_LSEL | H3_LINEOUT_RSEL, 0);

	return (0);
}

static const struct h3_mixer {
	unsigned reg;
	unsigned mask;
	unsigned shift;
} h3_mixers[SOUND_MIXER_NRDEVICES] = {
	[SOUND_MIXER_VOLUME]	= { H3_LINEOUT_VOLC, H3_LINEOUTVOL_MASK,
				    H3_LINEOUTVOL_SHIFT },
	[SOUND_MIXER_RECLEV]	= { H3_ADC_AP_EN, H3_ADCG_MASK,
				    H3_ADCG_SHIFT },
	[SOUND_MIXER_LINE]	= { H3_LINEIN_GCTR, H3_LINEING_MASK,
				    H3_LINEING_SHIFT },
	[SOUND_MIXER_MIC]	= { H3_MIC_GCTR, H3_MIC1_GAIN_MASK,
				    H3_MIC1_GAIN_SHIFT },
	[SOUND_MIXER_LINE1]	= { H3_MIC_GCTR, H3_MIC2_GAIN_MASK,
				    H3_MIC2_GAIN_SHIFT },
};

static int
h3_mixer_set(struct snd_mixer *m, unsigned dev, unsigned left,
    unsigned right)
{
	struct a10codec_info *sc = mix_getdevinfo(m);
	unsigned nvol, max;

	max = h3_mixers[dev].mask >> h3_mixers[dev].shift;
	nvol = (left * max) / 100;

	h3_pr_set_clear(sc, h3_mixers[dev].reg,
	    nvol << h3_mixers[dev].shift, h3_mixers[dev].mask);

	left = right = (left * 100) / max;
	return (left | (right << 8));
}

static uint32_t
h3_mixer_setrecsrc(struct snd_mixer *m, uint32_t src)
{
	struct a10codec_info *sc = mix_getdevinfo(m);
	uint32_t val;

	val = 0;
	src &= (SOUND_MASK_LINE | SOUND_MASK_MIC |
	    SOUND_MASK_LINE1 | SOUND_MASK_IMIX);

	if ((src & SOUND_MASK_LINE) != 0)	/* line-in */
		val |= H3_ADCMIXSC_LINEIN;
	if ((src & SOUND_MASK_MIC) != 0)	/* MIC1 */
		val |= H3_ADCMIXSC_MIC1;
	if ((src & SOUND_MASK_LINE1) != 0)	/* MIC2 */
		val |= H3_ADCMIXSC_MIC2;
	if ((src & SOUND_MASK_IMIX) != 0)	/* l/r output mixer */
		val |= H3_ADCMIXSC_OMIXER;

	h3_pr_write(sc, H3_LADCMIXSC, val);
	h3_pr_write(sc, H3_RADCMIXSC, val);

	return (src);
}

static void
h3_mute(struct a10codec_info *sc, int mute, int dir)
{
	if (dir == PCMDIR_PLAY) {
		if (mute) {
			/* Mute DAC l/r channels to output mixer */
			h3_pr_set_clear(sc, H3_LOMIXSC, 0, H3_LOMIXSC_LDAC);
			h3_pr_set_clear(sc, H3_ROMIXSC, 0, H3_ROMIXSC_RDAC);
			/* Disable DAC analog l/r channels and output mixer */
			h3_pr_set_clear(sc, H3_DAC_PA_SRC,
			    0, H3_DACAREN | H3_DACALEN | H3_RMIXEN | H3_LMIXEN);
		} else {
			/* Enable DAC analog l/r channels and output mixer */
			h3_pr_set_clear(sc, H3_DAC_PA_SRC,
			    H3_DACAREN | H3_DACALEN | H3_RMIXEN | H3_LMIXEN, 0);
			/* Unmute DAC l/r channels to output mixer */
			h3_pr_set_clear(sc, H3_LOMIXSC, H3_LOMIXSC_LDAC, 0);
			h3_pr_set_clear(sc, H3_ROMIXSC, H3_ROMIXSC_RDAC, 0);
		}
	} else {
		if (mute) {
			/* Disable ADC analog l/r channels */
			h3_pr_set_clear(sc, H3_ADC_AP_EN,
			    0, H3_ADCREN | H3_ADCLEN);
		} else {
			/* Enable ADC analog l/r channels */
			h3_pr_set_clear(sc, H3_ADC_AP_EN,
			    H3_ADCREN | H3_ADCLEN, 0);
		}
	}
}

static kobj_method_t h3_mixer_methods[] = {
	KOBJMETHOD(mixer_init,		h3_mixer_init),
	KOBJMETHOD(mixer_set,		h3_mixer_set),
	KOBJMETHOD(mixer_setrecsrc,	h3_mixer_setrecsrc),
	KOBJMETHOD_END
};
MIXER_DECLARE(h3_mixer);


/*
 * Channel interface
 */

static void
a10codec_dmamap_cb(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	struct a10codec_chinfo *ch = arg;

	if (error != 0)
		return;

	ch->physaddr = segs[0].ds_addr;
}

static void
a10codec_transfer(struct a10codec_chinfo *ch)
{
	bus_addr_t src, dst;
	int error;

	if (ch->dir == PCMDIR_PLAY) {
		src = ch->physaddr + ch->pos;
		dst = ch->fifo;
	} else {
		src = ch->fifo;
		dst = ch->physaddr + ch->pos;
	}

	error = SUNXI_DMA_TRANSFER(ch->dmac, ch->dmachan, src, dst,
	    ch->blocksize);
	if (error) {
		ch->run = 0;
		device_printf(ch->parent->dev, "DMA transfer failed: %d\n",
		    error);
	}
}

static void
a10codec_dmaconfig(struct a10codec_chinfo *ch)
{
	struct a10codec_info *sc = ch->parent;
	struct sunxi_dma_config conf;

	memset(&conf, 0, sizeof(conf));
	conf.src_width = conf.dst_width = 16;
	conf.src_burst_len = conf.dst_burst_len = 4;

	if (ch->dir == PCMDIR_PLAY) {
		conf.dst_noincr = true;
		conf.src_drqtype = sc->cfg->drqtype_sdram;
		conf.dst_drqtype = sc->cfg->drqtype_codec;
	} else {
		conf.src_noincr = true;
		conf.src_drqtype = sc->cfg->drqtype_codec;
		conf.dst_drqtype = sc->cfg->drqtype_sdram;
	}

	SUNXI_DMA_SET_CONFIG(ch->dmac, ch->dmachan, &conf);
}

static void
a10codec_dmaintr(void *priv)
{
	struct a10codec_chinfo *ch = priv;
	unsigned bufsize;

	bufsize = sndbuf_getsize(ch->buffer);

	ch->pos += ch->blocksize;
	if (ch->pos >= bufsize)
		ch->pos -= bufsize;

	if (ch->run) {
		chn_intr(ch->channel);
		a10codec_transfer(ch);
	}
}

static unsigned
a10codec_fs(struct a10codec_chinfo *ch)
{
	switch (ch->speed) {
	case 48000:
		return (DAC_FS_48KHZ);
	case 24000:
		return (DAC_FS_24KHZ);
	case 12000:
		return (DAC_FS_12KHZ);
	case 192000:
		return (DAC_FS_192KHZ);
	case 32000:
		return (DAC_FS_32KHZ);
	case 16000:
		return (DAC_FS_16KHZ);
	case 8000:
		return (DAC_FS_8KHZ);
	case 96000:
		return (DAC_FS_96KHZ);
	default:
		return (DAC_FS_48KHZ);
	}
}

static void
a10codec_start(struct a10codec_chinfo *ch)
{
	struct a10codec_info *sc = ch->parent;
	uint32_t val;

	ch->pos = 0;

	if (ch->dir == PCMDIR_PLAY) {
		/* Flush DAC FIFO */
		CODEC_WRITE(sc, AC_DAC_FIFOC(sc), DAC_FIFOC_FIFO_FLUSH);

		/* Clear DAC FIFO status */
		CODEC_WRITE(sc, AC_DAC_FIFOS(sc),
		    CODEC_READ(sc, AC_DAC_FIFOS(sc)));

		/* Unmute output */
		sc->cfg->mute(sc, 0, ch->dir);

		/* Configure DAC DMA channel */
		a10codec_dmaconfig(ch);

		/* Configure DAC FIFO */
		CODEC_WRITE(sc, AC_DAC_FIFOC(sc),
		    (AFMT_CHANNEL(ch->format) == 1 ? DAC_FIFOC_MONO_EN : 0) |
		    (a10codec_fs(ch) << DAC_FIFOC_FS_SHIFT) |
		    (FIFO_MODE_16_15_0 << DAC_FIFOC_FIFO_MODE_SHIFT) |
		    (DRQ_CLR_CNT << DAC_FIFOC_DRQ_CLR_CNT_SHIFT) |
		    (TX_TRIG_LEVEL << DAC_FIFOC_TX_TRIG_LEVEL_SHIFT));

		/* Enable DAC DRQ */
		val = CODEC_READ(sc, AC_DAC_FIFOC(sc));
		val |= DAC_FIFOC_DRQ_EN;
		CODEC_WRITE(sc, AC_DAC_FIFOC(sc), val);
	} else {
		/* Flush ADC FIFO */
		CODEC_WRITE(sc, AC_ADC_FIFOC(sc), ADC_FIFOC_FIFO_FLUSH);

		/* Clear ADC FIFO status */
		CODEC_WRITE(sc, AC_ADC_FIFOS(sc),
		    CODEC_READ(sc, AC_ADC_FIFOS(sc)));

		/* Unmute input */
		sc->cfg->mute(sc, 0, ch->dir);

		/* Configure ADC DMA channel */
		a10codec_dmaconfig(ch);

		/* Configure ADC FIFO */
		CODEC_WRITE(sc, AC_ADC_FIFOC(sc),
		    ADC_FIFOC_EN_AD |
		    ADC_FIFOC_RX_FIFO_MODE |
		    (AFMT_CHANNEL(ch->format) == 1 ? ADC_FIFOC_MONO_EN : 0) |
		    (a10codec_fs(ch) << ADC_FIFOC_FS_SHIFT) |
		    (RX_TRIG_LEVEL << ADC_FIFOC_RX_TRIG_LEVEL_SHIFT));

		/* Enable ADC DRQ */
		val = CODEC_READ(sc, AC_ADC_FIFOC(sc));
		val |= ADC_FIFOC_DRQ_EN;
		CODEC_WRITE(sc, AC_ADC_FIFOC(sc), val);
	}

	/* Start DMA transfer */
	a10codec_transfer(ch);
}

static void
a10codec_stop(struct a10codec_chinfo *ch)
{
	struct a10codec_info *sc = ch->parent;

	/* Disable DMA channel */
	SUNXI_DMA_HALT(ch->dmac, ch->dmachan);

	sc->cfg->mute(sc, 1, ch->dir);

	if (ch->dir == PCMDIR_PLAY) {
		/* Disable DAC DRQ */
		CODEC_WRITE(sc, AC_DAC_FIFOC(sc), 0);
	} else {
		/* Disable ADC DRQ */
		CODEC_WRITE(sc, AC_ADC_FIFOC(sc), 0);
	}
}

static void *
a10codec_chan_init(kobj_t obj, void *devinfo, struct snd_dbuf *b,
    struct pcm_channel *c, int dir)
{
	struct a10codec_info *sc = devinfo;
	struct a10codec_chinfo *ch = dir == PCMDIR_PLAY ? &sc->play : &sc->rec;
	phandle_t xref;
	pcell_t *cells;
	int ncells, error;

	error = ofw_bus_parse_xref_list_alloc(ofw_bus_get_node(sc->dev),
	    "dmas", "#dma-cells", dir == PCMDIR_PLAY ? 1 : 0,
	    &xref, &ncells, &cells);
	if (error != 0) {
		device_printf(sc->dev, "cannot parse 'dmas' property\n");
		return (NULL);
	}
	OF_prop_free(cells);

	ch->parent = sc;
	ch->channel = c;
	ch->buffer = b;
	ch->dir = dir;
	ch->fifo = rman_get_start(sc->res[0]) +
	    (dir == PCMDIR_REC ? AC_ADC_RXDATA(sc) : AC_DAC_TXDATA(sc));

	ch->dmac = OF_device_from_xref(xref);
	if (ch->dmac == NULL) {
		device_printf(sc->dev, "cannot find DMA controller\n");
		device_printf(sc->dev, "xref = 0x%x\n", (u_int)xref);
		return (NULL);
	}
	ch->dmachan = SUNXI_DMA_ALLOC(ch->dmac, false, a10codec_dmaintr, ch);
	if (ch->dmachan == NULL) {
		device_printf(sc->dev, "cannot allocate DMA channel\n");
		return (NULL);
	}

	error = bus_dmamem_alloc(sc->dmat, &ch->dmaaddr,
	    BUS_DMA_NOWAIT | BUS_DMA_COHERENT, &ch->dmamap);
	if (error != 0) {
		device_printf(sc->dev, "cannot allocate channel buffer\n");
		return (NULL);
	}
	error = bus_dmamap_load(sc->dmat, ch->dmamap, ch->dmaaddr,
	    sc->dmasize, a10codec_dmamap_cb, ch, BUS_DMA_NOWAIT);
	if (error != 0) {
		device_printf(sc->dev, "cannot load DMA map\n");
		return (NULL);
	}
	memset(ch->dmaaddr, 0, sc->dmasize);

	if (sndbuf_setup(ch->buffer, ch->dmaaddr, sc->dmasize) != 0) {
		device_printf(sc->dev, "cannot setup sndbuf\n");
		return (NULL);
	}

	return (ch);
}

static int
a10codec_chan_free(kobj_t obj, void *data)
{
	struct a10codec_chinfo *ch = data;
	struct a10codec_info *sc = ch->parent;

	SUNXI_DMA_FREE(ch->dmac, ch->dmachan);
	bus_dmamap_unload(sc->dmat, ch->dmamap);
	bus_dmamem_free(sc->dmat, ch->dmaaddr, ch->dmamap);

	return (0);
}

static int
a10codec_chan_setformat(kobj_t obj, void *data, uint32_t format)
{
	struct a10codec_chinfo *ch = data;

	ch->format = format;

	return (0);
}

static uint32_t
a10codec_chan_setspeed(kobj_t obj, void *data, uint32_t speed)
{
	struct a10codec_chinfo *ch = data;

	/*
	 * The codec supports full duplex operation but both DAC and ADC
	 * use the same source clock (PLL2). Limit the available speeds to
	 * those supported by a 24576000 Hz input.
	 */
	switch (speed) {
	case 8000:
	case 12000:
	case 16000:
	case 24000:
	case 32000:
	case 48000:
		ch->speed = speed;
		break;
	case 96000:
	case 192000:
		/* 96 KHz / 192 KHz mode only supported for playback */
		if (ch->dir == PCMDIR_PLAY) {
			ch->speed = speed;
		} else {
			ch->speed = 48000;
		}
		break;
	case 44100:
		ch->speed = 48000;
		break;
	case 22050:
		ch->speed = 24000;
		break;
	case 11025:
		ch->speed = 12000;
		break;
	default:
		ch->speed = 48000;
		break;
	}

	return (ch->speed);
}

static uint32_t
a10codec_chan_setblocksize(kobj_t obj, void *data, uint32_t blocksize)
{
	struct a10codec_chinfo *ch = data;

	ch->blocksize = blocksize & ~3;

	return (ch->blocksize);
}

static int
a10codec_chan_trigger(kobj_t obj, void *data, int go)
{
	struct a10codec_chinfo *ch = data;
	struct a10codec_info *sc = ch->parent;

	if (!PCMTRIG_COMMON(go))
		return (0);

	snd_mtxlock(sc->lock);
	switch (go) {
	case PCMTRIG_START:
		ch->run = 1;
		a10codec_start(ch);
		break;
	case PCMTRIG_STOP:
	case PCMTRIG_ABORT:
		ch->run = 0;
		a10codec_stop(ch);
		break;
	default:
		break;
	}
	snd_mtxunlock(sc->lock);

	return (0);
}

static uint32_t
a10codec_chan_getptr(kobj_t obj, void *data)
{
	struct a10codec_chinfo *ch = data;

	return (ch->pos);
}

static struct pcmchan_caps *
a10codec_chan_getcaps(kobj_t obj, void *data)
{
	struct a10codec_chinfo *ch = data;

	if (ch->dir == PCMDIR_PLAY) {
		return (&a10codec_pcaps);
	} else {
		return (&a10codec_rcaps);
	}
}

static kobj_method_t a10codec_chan_methods[] = {
	KOBJMETHOD(channel_init,		a10codec_chan_init),
	KOBJMETHOD(channel_free,		a10codec_chan_free),
	KOBJMETHOD(channel_setformat,		a10codec_chan_setformat),
	KOBJMETHOD(channel_setspeed,		a10codec_chan_setspeed),
	KOBJMETHOD(channel_setblocksize,	a10codec_chan_setblocksize),
	KOBJMETHOD(channel_trigger,		a10codec_chan_trigger),
	KOBJMETHOD(channel_getptr,		a10codec_chan_getptr),
	KOBJMETHOD(channel_getcaps,		a10codec_chan_getcaps),
	KOBJMETHOD_END
};
CHANNEL_DECLARE(a10codec_chan);


/*
 * Device interface
 */

static const struct a10codec_config a10_config = {
	.mixer_class	= &a10_mixer_class,
	.mute		= a10_mute,
	.drqtype_codec	= 19,
	.drqtype_sdram	= 22,
	.DPC		= 0x00,
	.DAC_FIFOC	= 0x04,
	.DAC_FIFOS	= 0x08,
	.DAC_TXDATA	= 0x0c,
	.ADC_FIFOC	= 0x1c,
	.ADC_FIFOS	= 0x20,
	.ADC_RXDATA	= 0x24,
	.DAC_CNT	= 0x30,
	.ADC_CNT	= 0x34,
};

static const struct a10codec_config h3_config = {
	.mixer_class	= &h3_mixer_class,
	.mute		= h3_mute,
	.drqtype_codec	= 15,
	.drqtype_sdram	= 1,
	.DPC		= 0x00,
	.DAC_FIFOC	= 0x04,
	.DAC_FIFOS	= 0x08,
	.DAC_TXDATA	= 0x20,
	.ADC_FIFOC	= 0x10,
	.ADC_FIFOS	= 0x14,
	.ADC_RXDATA	= 0x18,
	.DAC_CNT	= 0x40,
	.ADC_CNT	= 0x44,
};

static struct ofw_compat_data compat_data[] = {
	{ "allwinner,sun4i-a10-codec",	(uintptr_t)&a10_config },
	{ "allwinner,sun7i-a20-codec",	(uintptr_t)&a10_config },
	{ "allwinner,sun8i-h3-codec",	(uintptr_t)&h3_config },
	{ NULL, 0 }
};

static int
a10codec_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "Allwinner Audio Codec");
	return (BUS_PROBE_DEFAULT);
}

static int
a10codec_attach(device_t dev)
{
	struct a10codec_info *sc;
	char status[SND_STATUSLEN];
	struct gpiobus_pin *pa_pin;
	phandle_t node;
	clk_t clk_bus, clk_codec;
	hwreset_t rst;
	uint32_t val;
	int error;

	node = ofw_bus_get_node(dev);

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK | M_ZERO);
	sc->cfg = (void *)ofw_bus_search_compatible(dev, compat_data)->ocd_data;
	sc->dev = dev;
	sc->lock = snd_mtxcreate(device_get_nameunit(dev), "a10codec softc");

	if (bus_alloc_resources(dev, a10codec_spec, sc->res)) {
		device_printf(dev, "cannot allocate resources for device\n");
		error = ENXIO;
		goto fail;
	}

	sc->dmasize = 131072;
	error = bus_dma_tag_create(
	    bus_get_dma_tag(dev),
	    4, sc->dmasize,		/* alignment, boundary */
	    BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    sc->dmasize, 1,		/* maxsize, nsegs */
	    sc->dmasize, 0,		/* maxsegsize, flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->dmat);
	if (error != 0) {
		device_printf(dev, "cannot create DMA tag\n");
		goto fail;
	}

	/* Get clocks */
	if (clk_get_by_ofw_name(dev, 0, "apb", &clk_bus) != 0 &&
	    clk_get_by_ofw_name(dev, 0, "ahb", &clk_bus) != 0) {
		device_printf(dev, "cannot find bus clock\n");
		goto fail;
	}
	if (clk_get_by_ofw_name(dev, 0, "codec", &clk_codec) != 0) {
		device_printf(dev, "cannot find codec clock\n");
		goto fail;
	}

	/* Gating bus clock for codec */
	if (clk_enable(clk_bus) != 0) {
		device_printf(dev, "cannot enable bus clock\n");
		goto fail;
	}
	/* Activate audio codec clock. According to the A10 and A20 user
	 * manuals, Audio_pll can be either 24.576MHz or 22.5792MHz. Most
	 * audio sampling rates require an 24.576MHz input clock with the
	 * exception of 44.1kHz, 22.05kHz, and 11.025kHz. Unfortunately,
	 * both capture and playback use the same clock source so to
	 * safely support independent full duplex operation, we use a fixed
	 * 24.576MHz clock source and don't advertise native support for
	 * the three sampling rates that require a 22.5792MHz input.
	 */
	error = clk_set_freq(clk_codec, 24576000, CLK_SET_ROUND_DOWN);
	if (error != 0) {
		device_printf(dev, "cannot set codec clock frequency\n");
		goto fail;
	}
	/* Enable audio codec clock */
	error = clk_enable(clk_codec);
	if (error != 0) {
		device_printf(dev, "cannot enable codec clock\n");
		goto fail;
	}

	/* De-assert hwreset */
	if (hwreset_get_by_ofw_name(dev, 0, "apb", &rst) == 0 ||
	    hwreset_get_by_ofw_name(dev, 0, "ahb", &rst) == 0) {
		error = hwreset_deassert(rst);
		if (error != 0) {
			device_printf(dev, "cannot de-assert reset\n");
			goto fail;
		}
	}

	/* Enable DAC */
	val = CODEC_READ(sc, AC_DAC_DPC(sc));
	val |= DAC_DPC_EN_DA;
	CODEC_WRITE(sc, AC_DAC_DPC(sc), val);

#ifdef notdef
	error = snd_setup_intr(dev, sc->irq, INTR_MPSAFE, a10codec_intr, sc,
	    &sc->ih);
	if (error != 0) {
		device_printf(dev, "could not setup interrupt handler\n");
		goto fail;
	}
#endif

	if (mixer_init(dev, sc->cfg->mixer_class, sc)) {
		device_printf(dev, "mixer_init failed\n");
		goto fail;
	}

	/* Unmute PA */
	if (gpio_pin_get_by_ofw_property(dev, node, "allwinner,pa-gpios",
	    &pa_pin) == 0) {
		error = gpio_pin_set_active(pa_pin, 1);
		if (error != 0)
			device_printf(dev, "failed to unmute PA\n");
	}

	pcm_setflags(dev, pcm_getflags(dev) | SD_F_MPSAFE);

	if (pcm_register(dev, sc, 1, 1)) {
		device_printf(dev, "pcm_register failed\n");
		goto fail;
	}

	pcm_addchan(dev, PCMDIR_PLAY, &a10codec_chan_class, sc);
	pcm_addchan(dev, PCMDIR_REC, &a10codec_chan_class, sc);

	snprintf(status, SND_STATUSLEN, "at %s", ofw_bus_get_name(dev));
	pcm_setstatus(dev, status);

	return (0);

fail:
	bus_release_resources(dev, a10codec_spec, sc->res);
	snd_mtxfree(sc->lock);
	free(sc, M_DEVBUF);

	return (ENXIO);
}

static device_method_t a10codec_pcm_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		a10codec_probe),
	DEVMETHOD(device_attach,	a10codec_attach),

	DEVMETHOD_END
};

static driver_t a10codec_pcm_driver = {
	"pcm",
	a10codec_pcm_methods,
	PCM_SOFTC_SIZE,
};

DRIVER_MODULE(a10codec, simplebus, a10codec_pcm_driver, pcm_devclass, 0, 0);
MODULE_DEPEND(a10codec, sound, SOUND_MINVER, SOUND_PREFVER, SOUND_MAXVER);
MODULE_VERSION(a10codec, 1);
