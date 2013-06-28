/*
 * Hitachi Audio Controller (AC97) support for SH7760/SH7780
 *
 * Copyright (c) 2007 Manuel Lauss <mano@roarinelk.homelinux.net>
 *  licensed under the terms outlined in the file COPYING at the root
 *  of the linux kernel sources.
 *
 * dont forget to set IPSEL/OMSEL register bits (in your board code) to
 * enable HAC output pins!
 */

/* BIG FAT FIXME: although the SH7760 has 2 independent AC97 units, only
 * the FIRST can be used since ASoC does not pass any information to the
 * ac97_read/write() functions regarding WHICH unit to use.  You'll have
 * to edit the code a bit to use the other AC97 unit.		--mlau
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/ac97_codec.h>
#include <sound/initval.h>
#include <sound/soc.h>

/* regs and bits */
#define HACCR		0x08
#define HACCSAR		0x20
#define HACCSDR		0x24
#define HACPCML		0x28
#define HACPCMR		0x2C
#define HACTIER		0x50
#define	HACTSR		0x54
#define HACRIER		0x58
#define HACRSR		0x5C
#define HACACR		0x60

#define CR_CR		(1 << 15)	/* "codec-ready" indicator */
#define CR_CDRT		(1 << 11)	/* cold reset */
#define CR_WMRT		(1 << 10)	/* warm reset */
#define CR_B9		(1 << 9)	/* the mysterious "bit 9" */
#define CR_ST		(1 << 5)	/* AC97 link start bit */

#define CSAR_RD		(1 << 19)	/* AC97 data read bit */
#define CSAR_WR		(0)

#define TSR_CMDAMT	(1 << 31)
#define TSR_CMDDMT	(1 << 30)

#define RSR_STARY	(1 << 22)
#define RSR_STDRY	(1 << 21)

#define ACR_DMARX16	(1 << 30)
#define ACR_DMATX16	(1 << 29)
#define ACR_TX12ATOM	(1 << 26)
#define ACR_DMARX20	((1 << 24) | (1 << 22))
#define ACR_DMATX20	((1 << 23) | (1 << 21))

#define CSDR_SHIFT	4
#define CSDR_MASK	(0xffff << CSDR_SHIFT)
#define CSAR_SHIFT	12
#define CSAR_MASK	(0x7f << CSAR_SHIFT)

#define AC97_WRITE_RETRY	1
#define AC97_READ_RETRY		5

/* manual-suggested AC97 codec access timeouts (us) */
#define TMO_E1	500	/* 21 < E1 < 1000 */
#define TMO_E2	13	/* 13 < E2 */
#define TMO_E3	21	/* 21 < E3 */
#define TMO_E4	500	/* 21 < E4 < 1000 */

struct hac_priv {
	unsigned long mmio;	/* HAC base address */
} hac_cpu_data[] = {
#if defined(CONFIG_CPU_SUBTYPE_SH7760)
	{
		.mmio	= 0xFE240000,
	},
	{
		.mmio	= 0xFE250000,
	},
#elif defined(CONFIG_CPU_SUBTYPE_SH7780)
	{
		.mmio	= 0xFFE40000,
	},
#else
#error "Unsupported SuperH SoC"
#endif
};

#define HACREG(reg)	(*(unsigned long *)(hac->mmio + (reg)))

/*
 * AC97 read/write flow as outlined in the SH7760 manual (pages 903-906)
 */
static int hac_get_codec_data(struct hac_priv *hac, unsigned short r,
			      unsigned short *v)
{
	unsigned int to1, to2, i;
	unsigned short adr;

	for (i = AC97_READ_RETRY; i; i--) {
		*v = 0;
		/* wait for HAC to receive something from the codec */
		for (to1 = TMO_E4;
		     to1 && !(HACREG(HACRSR) & RSR_STARY);
		     --to1)
			udelay(1);
		for (to2 = TMO_E4; 
		     to2 && !(HACREG(HACRSR) & RSR_STDRY);
		     --to2)
			udelay(1);

		if (!to1 && !to2)
			return 0;	/* codec comm is down */

		adr = ((HACREG(HACCSAR) & CSAR_MASK) >> CSAR_SHIFT);
		*v  = ((HACREG(HACCSDR) & CSDR_MASK) >> CSDR_SHIFT);

		HACREG(HACRSR) &= ~(RSR_STDRY | RSR_STARY);

		if (r == adr)
			break;

		/* manual says: wait at least 21 usec before retrying */
		udelay(21);
	}
	HACREG(HACRSR) &= ~(RSR_STDRY | RSR_STARY);
	return i;
}

static unsigned short hac_read_codec_aux(struct hac_priv *hac,
					 unsigned short reg)
{
	unsigned short val;
	unsigned int i, to;

	for (i = AC97_READ_RETRY; i; i--) {
		/* send_read_request */
		local_irq_disable();
		HACREG(HACTSR) &= ~(TSR_CMDAMT);
		HACREG(HACCSAR) = (reg << CSAR_SHIFT) | CSAR_RD;
		local_irq_enable();

		for (to = TMO_E3;
		     to && !(HACREG(HACTSR) & TSR_CMDAMT);
		     --to)
			udelay(1);

		HACREG(HACTSR) &= ~TSR_CMDAMT;
		val = 0;
		if (hac_get_codec_data(hac, reg, &val) != 0)
			break;
	}

	return i ? val : ~0;
}

static void hac_ac97_write(struct snd_ac97 *ac97, unsigned short reg,
			   unsigned short val)
{
	int unit_id = 0 /* ac97->private_data */;
	struct hac_priv *hac = &hac_cpu_data[unit_id];
	unsigned int i, to;
	/* write_codec_aux */
	for (i = AC97_WRITE_RETRY; i; i--) {
		/* send_write_request */
		local_irq_disable();
		HACREG(HACTSR) &= ~(TSR_CMDDMT | TSR_CMDAMT);
		HACREG(HACCSDR) = (val << CSDR_SHIFT);
		HACREG(HACCSAR) = (reg << CSAR_SHIFT) & (~CSAR_RD);
		local_irq_enable();

		/* poll-wait for CMDAMT and CMDDMT */
		for (to = TMO_E1;
		     to && !(HACREG(HACTSR) & (TSR_CMDAMT|TSR_CMDDMT));
		     --to)
			udelay(1);

		HACREG(HACTSR) &= ~(TSR_CMDAMT | TSR_CMDDMT);
		if (to)
			break;
		/* timeout, try again */
	}
}

static unsigned short hac_ac97_read(struct snd_ac97 *ac97,
				    unsigned short reg)
{
	int unit_id = 0 /* ac97->private_data */;
	struct hac_priv *hac = &hac_cpu_data[unit_id];
	return hac_read_codec_aux(hac, reg);
}

static void hac_ac97_warmrst(struct snd_ac97 *ac97)
{
	int unit_id = 0 /* ac97->private_data */;
	struct hac_priv *hac = &hac_cpu_data[unit_id];
	unsigned int tmo;

	HACREG(HACCR) = CR_WMRT | CR_ST | CR_B9;
	msleep(10);
	HACREG(HACCR) = CR_ST | CR_B9;
	for (tmo = 1000; (tmo > 0) && !(HACREG(HACCR) & CR_CR); tmo--)
		udelay(1);

	if (!tmo)
		printk(KERN_INFO "hac: reset: AC97 link down!\n");
	/* settings this bit lets us have a conversation with codec */
	HACREG(HACACR) |= ACR_TX12ATOM;
}

static void hac_ac97_coldrst(struct snd_ac97 *ac97)
{
	int unit_id = 0 /* ac97->private_data */;
	struct hac_priv *hac;
	hac = &hac_cpu_data[unit_id];

	HACREG(HACCR) = 0;
	HACREG(HACCR) = CR_CDRT | CR_ST | CR_B9;
	msleep(10);
	hac_ac97_warmrst(ac97);
}

struct snd_ac97_bus_ops soc_ac97_ops = {
	.read	= hac_ac97_read,
	.write	= hac_ac97_write,
	.reset	= hac_ac97_coldrst,
	.warm_reset = hac_ac97_warmrst,
};
EXPORT_SYMBOL_GPL(soc_ac97_ops);

static int hac_hw_params(struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *params,
			 struct snd_soc_dai *dai)
{
	struct hac_priv *hac = &hac_cpu_data[dai->id];
	int d = substream->stream == SNDRV_PCM_STREAM_PLAYBACK ? 0 : 1;

	switch (params->msbits) {
	case 16:
		HACREG(HACACR) |= d ?  ACR_DMARX16 :  ACR_DMATX16;
		HACREG(HACACR) &= d ? ~ACR_DMARX20 : ~ACR_DMATX20;
		break;
	case 20:
		HACREG(HACACR) &= d ? ~ACR_DMARX16 : ~ACR_DMATX16;
		HACREG(HACACR) |= d ?  ACR_DMARX20 :  ACR_DMATX20;
		break;
	default:
		pr_debug("hac: invalid depth %d bit\n", params->msbits);
		return -EINVAL;
		break;
	}

	return 0;
}

#define AC97_RATES	\
	SNDRV_PCM_RATE_8000_192000

#define AC97_FMTS	\
	SNDRV_PCM_FMTBIT_S16_LE

static const struct snd_soc_dai_ops hac_dai_ops = {
	.hw_params	= hac_hw_params,
};

static struct snd_soc_dai_driver sh4_hac_dai[] = {
{
	.name			= "hac-dai.0",
	.ac97_control		= 1,
	.playback = {
		.rates		= AC97_RATES,
		.formats	= AC97_FMTS,
		.channels_min	= 2,
		.channels_max	= 2,
	},
	.capture = {
		.rates		= AC97_RATES,
		.formats	= AC97_FMTS,
		.channels_min	= 2,
		.channels_max	= 2,
	},
	.ops = &hac_dai_ops,
},
#ifdef CONFIG_CPU_SUBTYPE_SH7760
{
	.name			= "hac-dai.1",
	.id			= 1,
	.playback = {
		.rates		= AC97_RATES,
		.formats	= AC97_FMTS,
		.channels_min	= 2,
		.channels_max	= 2,
	},
	.capture = {
		.rates		= AC97_RATES,
		.formats	= AC97_FMTS,
		.channels_min	= 2,
		.channels_max	= 2,
	},
	.ops = &hac_dai_ops,

},
#endif
};

static const struct snd_soc_component_driver sh4_hac_component = {
	.name		= "sh4-hac",
};

static int hac_soc_platform_probe(struct platform_device *pdev)
{
	return snd_soc_register_component(&pdev->dev, &sh4_hac_component,
					  sh4_hac_dai, ARRAY_SIZE(sh4_hac_dai));
}

static int hac_soc_platform_remove(struct platform_device *pdev)
{
	snd_soc_unregister_component(&pdev->dev);
	return 0;
}

static struct platform_driver hac_pcm_driver = {
	.driver = {
			.name = "hac-pcm-audio",
			.owner = THIS_MODULE,
	},

	.probe = hac_soc_platform_probe,
	.remove = hac_soc_platform_remove,
};

module_platform_driver(hac_pcm_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SuperH onchip HAC (AC97) audio driver");
MODULE_AUTHOR("Manuel Lauss <mano@roarinelk.homelinux.net>");
