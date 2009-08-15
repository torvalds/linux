/*
 * Serial Sound Interface (I2S) support for SH7760/SH7780
 *
 * Copyright (c) 2007 Manuel Lauss <mano@roarinelk.homelinux.net>
 *
 *  licensed under the terms outlined in the file COPYING at the root
 *  of the linux kernel sources.
 *
 * dont forget to set IPSEL/OMSEL register bits (in your board code) to
 * enable SSI output pins!
 */

/*
 * LIMITATIONS:
 *	The SSI unit has only one physical data line, so full duplex is
 *	impossible.  This can be remedied  on the  SH7760 by  using the
 *	other SSI unit for recording; however the SH7780 has only 1 SSI
 *	unit, and its pins are shared with the AC97 unit,  among others.
 *
 * FEATURES:
 *	The SSI features "compressed mode": in this mode it continuously
 *	streams PCM data over the I2S lines and uses LRCK as a handshake
 *	signal.  Can be used to send compressed data (AC3/DTS) to a DSP.
 *	The number of bits sent over the wire in a frame can be adjusted
 *	and can be independent from the actual sample bit depth. This is
 *	useful to support TDM mode codecs like the AD1939 which have a
 *	fixed TDM slot size, regardless of sample resolution.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <asm/io.h>

#define SSICR	0x00
#define SSISR	0x04

#define CR_DMAEN	(1 << 28)
#define CR_CHNL_SHIFT	22
#define CR_CHNL_MASK	(3 << CR_CHNL_SHIFT)
#define CR_DWL_SHIFT	19
#define CR_DWL_MASK	(7 << CR_DWL_SHIFT)
#define CR_SWL_SHIFT	16
#define CR_SWL_MASK	(7 << CR_SWL_SHIFT)
#define CR_SCK_MASTER	(1 << 15)	/* bitclock master bit */
#define CR_SWS_MASTER	(1 << 14)	/* wordselect master bit */
#define CR_SCKP		(1 << 13)	/* I2Sclock polarity */
#define CR_SWSP		(1 << 12)	/* LRCK polarity */
#define CR_SPDP		(1 << 11)
#define CR_SDTA		(1 << 10)	/* i2s alignment (msb/lsb) */
#define CR_PDTA		(1 << 9)	/* fifo data alignment */
#define CR_DEL		(1 << 8)	/* delay data by 1 i2sclk */
#define CR_BREN		(1 << 7)	/* clock gating in burst mode */
#define CR_CKDIV_SHIFT	4
#define CR_CKDIV_MASK	(7 << CR_CKDIV_SHIFT)	/* bitclock divider */
#define CR_MUTE		(1 << 3)	/* SSI mute */
#define CR_CPEN		(1 << 2)	/* compressed mode */
#define CR_TRMD		(1 << 1)	/* transmit/receive select */
#define CR_EN		(1 << 0)	/* enable SSI */

#define SSIREG(reg)	(*(unsigned long *)(ssi->mmio + (reg)))

struct ssi_priv {
	unsigned long mmio;
	unsigned long sysclk;
	int inuse;
} ssi_cpu_data[] = {
#if defined(CONFIG_CPU_SUBTYPE_SH7760)
	{
		.mmio	= 0xFE680000,
	},
	{
		.mmio	= 0xFE690000,
	},
#elif defined(CONFIG_CPU_SUBTYPE_SH7780)
	{
		.mmio	= 0xFFE70000,
	},
#else
#error "Unsupported SuperH SoC"
#endif
};

/*
 * track usage of the SSI; it is simplex-only so prevent attempts of
 * concurrent playback + capture. FIXME: any locking required?
 */
static int ssi_startup(struct snd_pcm_substream *substream,
		       struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct ssi_priv *ssi = &ssi_cpu_data[rtd->dai->cpu_dai->id];
	if (ssi->inuse) {
		pr_debug("ssi: already in use!\n");
		return -EBUSY;
	} else
		ssi->inuse = 1;
	return 0;
}

static void ssi_shutdown(struct snd_pcm_substream *substream,
			 struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct ssi_priv *ssi = &ssi_cpu_data[rtd->dai->cpu_dai->id];

	ssi->inuse = 0;
}

static int ssi_trigger(struct snd_pcm_substream *substream, int cmd,
		       struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct ssi_priv *ssi = &ssi_cpu_data[rtd->dai->cpu_dai->id];

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		SSIREG(SSICR) |= CR_DMAEN | CR_EN;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		SSIREG(SSICR) &= ~(CR_DMAEN | CR_EN);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int ssi_hw_params(struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *params,
			 struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct ssi_priv *ssi = &ssi_cpu_data[rtd->dai->cpu_dai->id];
	unsigned long ssicr = SSIREG(SSICR);
	unsigned int bits, channels, swl, recv, i;

	channels = params_channels(params);
	bits = params->msbits;
	recv = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ? 0 : 1;

	pr_debug("ssi_hw_params() enter\nssicr was    %08lx\n", ssicr);
	pr_debug("bits: %u channels: %u\n", bits, channels);

	ssicr &= ~(CR_TRMD | CR_CHNL_MASK | CR_DWL_MASK | CR_PDTA |
		   CR_SWL_MASK);

	/* direction (send/receive) */
	if (!recv)
		ssicr |= CR_TRMD;	/* transmit */

	/* channels */
	if ((channels < 2) || (channels > 8) || (channels & 1)) {
		pr_debug("ssi: invalid number of channels\n");
		return -EINVAL;
	}
	ssicr |= ((channels >> 1) - 1) << CR_CHNL_SHIFT;

	/* DATA WORD LENGTH (DWL): databits in audio sample */
	i = 0;
	switch (bits) {
	case 32: ++i;
	case 24: ++i;
	case 22: ++i;
	case 20: ++i;
	case 18: ++i;
	case 16: ++i;
		 ssicr |= i << CR_DWL_SHIFT;
	case 8:	 break;
	default:
		pr_debug("ssi: invalid sample width\n");
		return -EINVAL;
	}

	/*
	 * SYSTEM WORD LENGTH: size in bits of half a frame over the I2S
	 * wires. This is usually bits_per_sample x channels/2;  i.e. in
	 * Stereo mode  the SWL equals DWL.  SWL can  be bigger than the
	 * product of (channels_per_slot x samplebits), e.g.  for codecs
	 * like the AD1939 which  only accept 32bit wide TDM slots.  For
	 * "standard" I2S operation we set SWL = chans / 2 * DWL here.
	 * Waiting for ASoC to get TDM support ;-)
	 */
	if ((bits > 16) && (bits <= 24)) {
		bits = 24;	/* these are padded by the SSI */
		/*ssicr |= CR_PDTA;*/ /* cpu/data endianness ? */
	}
	i = 0;
	swl = (bits * channels) / 2;
	switch (swl) {
	case 256: ++i;
	case 128: ++i;
	case 64:  ++i;
	case 48:  ++i;
	case 32:  ++i;
	case 16:  ++i;
		  ssicr |= i << CR_SWL_SHIFT;
	case 8:   break;
	default:
		pr_debug("ssi: invalid system word length computed\n");
		return -EINVAL;
	}

	SSIREG(SSICR) = ssicr;

	pr_debug("ssi_hw_params() leave\nssicr is now %08lx\n", ssicr);
	return 0;
}

static int ssi_set_sysclk(struct snd_soc_dai *cpu_dai, int clk_id,
			  unsigned int freq, int dir)
{
	struct ssi_priv *ssi = &ssi_cpu_data[cpu_dai->id];

	ssi->sysclk = freq;

	return 0;
}

/*
 * This divider is used to generate the SSI_SCK (I2S bitclock) from the
 * clock at the HAC_BIT_CLK ("oversampling clock") pin.
 */
static int ssi_set_clkdiv(struct snd_soc_dai *dai, int did, int div)
{
	struct ssi_priv *ssi = &ssi_cpu_data[dai->id];
	unsigned long ssicr;
	int i;

	i = 0;
	ssicr = SSIREG(SSICR) & ~CR_CKDIV_MASK;
	switch (div) {
	case 16: ++i;
	case 8:  ++i;
	case 4:  ++i;
	case 2:  ++i;
		 SSIREG(SSICR) = ssicr | (i << CR_CKDIV_SHIFT);
	case 1:  break;
	default:
		pr_debug("ssi: invalid sck divider %d\n", div);
		return -EINVAL;
	}

	return 0;
}

static int ssi_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct ssi_priv *ssi = &ssi_cpu_data[dai->id];
	unsigned long ssicr = SSIREG(SSICR);

	pr_debug("ssi_set_fmt()\nssicr was    0x%08lx\n", ssicr);

	ssicr &= ~(CR_DEL | CR_PDTA | CR_BREN | CR_SWSP | CR_SCKP |
		   CR_SWS_MASTER | CR_SCK_MASTER);

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		ssicr |= CR_DEL | CR_PDTA;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		ssicr |= CR_DEL;
		break;
	default:
		pr_debug("ssi: unsupported format\n");
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_CLOCK_MASK) {
	case SND_SOC_DAIFMT_CONT:
		break;
	case SND_SOC_DAIFMT_GATED:
		ssicr |= CR_BREN;
		break;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		ssicr |= CR_SCKP;	/* sample data at low clkedge */
		break;
	case SND_SOC_DAIFMT_NB_IF:
		ssicr |= CR_SCKP | CR_SWSP;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		break;
	case SND_SOC_DAIFMT_IB_IF:
		ssicr |= CR_SWSP;	/* word select starts low */
		break;
	default:
		pr_debug("ssi: invalid inversion\n");
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		break;
	case SND_SOC_DAIFMT_CBS_CFM:
		ssicr |= CR_SCK_MASTER;
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
		ssicr |= CR_SWS_MASTER;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		ssicr |= CR_SWS_MASTER | CR_SCK_MASTER;
		break;
	default:
		pr_debug("ssi: invalid master/slave configuration\n");
		return -EINVAL;
	}

	SSIREG(SSICR) = ssicr;
	pr_debug("ssi_set_fmt() leave\nssicr is now 0x%08lx\n", ssicr);

	return 0;
}

/* the SSI depends on an external clocksource (at HAC_BIT_CLK) even in
 * Master mode,  so really this is board specific;  the SSI can do any
 * rate with the right bitclk and divider settings.
 */
#define SSI_RATES	\
	SNDRV_PCM_RATE_8000_192000

/* the SSI can do 8-32 bit samples, with 8 possible channels */
#define SSI_FMTS	\
	(SNDRV_PCM_FMTBIT_S8      | SNDRV_PCM_FMTBIT_U8      |	\
	 SNDRV_PCM_FMTBIT_S16_LE  | SNDRV_PCM_FMTBIT_U16_LE  |	\
	 SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_U20_3LE |	\
	 SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_U24_3LE |	\
	 SNDRV_PCM_FMTBIT_S32_LE  | SNDRV_PCM_FMTBIT_U32_LE)

static struct snd_soc_dai_ops ssi_dai_ops = {
	.startup	= ssi_startup,
	.shutdown	= ssi_shutdown,
	.trigger	= ssi_trigger,
	.hw_params	= ssi_hw_params,
	.set_sysclk	= ssi_set_sysclk,
	.set_clkdiv	= ssi_set_clkdiv,
	.set_fmt	= ssi_set_fmt,
};

struct snd_soc_dai sh4_ssi_dai[] = {
{
	.name			= "SSI0",
	.id			= 0,
	.playback = {
		.rates		= SSI_RATES,
		.formats	= SSI_FMTS,
		.channels_min	= 2,
		.channels_max	= 8,
	},
	.capture = {
		.rates		= SSI_RATES,
		.formats	= SSI_FMTS,
		.channels_min	= 2,
		.channels_max	= 8,
	},
	.ops = &ssi_dai_ops,
},
#ifdef CONFIG_CPU_SUBTYPE_SH7760
{
	.name			= "SSI1",
	.id			= 1,
	.playback = {
		.rates		= SSI_RATES,
		.formats	= SSI_FMTS,
		.channels_min	= 2,
		.channels_max	= 8,
	},
	.capture = {
		.rates		= SSI_RATES,
		.formats	= SSI_FMTS,
		.channels_min	= 2,
		.channels_max	= 8,
	},
	.ops = &ssi_dai_ops,
},
#endif
};
EXPORT_SYMBOL_GPL(sh4_ssi_dai);

static int __init sh4_ssi_init(void)
{
	return snd_soc_register_dais(sh4_ssi_dai, ARRAY_SIZE(sh4_ssi_dai));
}
module_init(sh4_ssi_init);

static void __exit sh4_ssi_exit(void)
{
	snd_soc_unregister_dais(sh4_ssi_dai, ARRAY_SIZE(sh4_ssi_dai));
}
module_exit(sh4_ssi_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SuperH onchip SSI (I2S) audio driver");
MODULE_AUTHOR("Manuel Lauss <mano@roarinelk.homelinux.net>");
