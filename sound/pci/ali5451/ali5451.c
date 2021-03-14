// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Matt Wu <Matt_Wu@acersoftech.com.cn>
 *  Apr 26, 2001
 *  Routines for control of ALi pci audio M5451
 *
 *  BUGS:
 *    --
 *
 *  TODO:
 *    --
 */

#include <linux/io.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/info.h>
#include <sound/ac97_codec.h>
#include <sound/mpu401.h>
#include <sound/initval.h>

MODULE_AUTHOR("Matt Wu <Matt_Wu@acersoftech.com.cn>");
MODULE_DESCRIPTION("ALI M5451");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{ALI,M5451,pci},{ALI,M5451}}");

static int index = SNDRV_DEFAULT_IDX1;	/* Index */
static char *id = SNDRV_DEFAULT_STR1;	/* ID for this card */
static int pcm_channels = 32;
static bool spdif;

module_param(index, int, 0444);
MODULE_PARM_DESC(index, "Index value for ALI M5451 PCI Audio.");
module_param(id, charp, 0444);
MODULE_PARM_DESC(id, "ID string for ALI M5451 PCI Audio.");
module_param(pcm_channels, int, 0444);
MODULE_PARM_DESC(pcm_channels, "PCM Channels");
module_param(spdif, bool, 0444);
MODULE_PARM_DESC(spdif, "Support SPDIF I/O");

/* just for backward compatibility */
static bool enable;
module_param(enable, bool, 0444);


/*
 *  Constants definition
 */

#define DEVICE_ID_ALI5451	((PCI_VENDOR_ID_AL<<16)|PCI_DEVICE_ID_AL_M5451)


#define ALI_CHANNELS		32

#define ALI_PCM_IN_CHANNEL	31
#define ALI_SPDIF_IN_CHANNEL	19
#define ALI_SPDIF_OUT_CHANNEL	15
#define ALI_CENTER_CHANNEL	24
#define ALI_LEF_CHANNEL		23
#define ALI_SURR_LEFT_CHANNEL	26
#define ALI_SURR_RIGHT_CHANNEL	25
#define ALI_MODEM_IN_CHANNEL    21
#define ALI_MODEM_OUT_CHANNEL   20

#define	SNDRV_ALI_VOICE_TYPE_PCM	01
#define SNDRV_ALI_VOICE_TYPE_OTH	02

#define	ALI_5451_V02		0x02

/*
 *  Direct Registers
 */

#define ALI_LEGACY_DMAR0        0x00  /* ADR0 */
#define ALI_LEGACY_DMAR4        0x04  /* CNT0 */
#define ALI_LEGACY_DMAR11       0x0b  /* MOD  */
#define ALI_LEGACY_DMAR15       0x0f  /* MMR  */
#define ALI_MPUR0		0x20
#define ALI_MPUR1		0x21
#define ALI_MPUR2		0x22
#define ALI_MPUR3		0x23

#define	ALI_AC97_WRITE		0x40
#define ALI_AC97_READ		0x44

#define ALI_SCTRL		0x48
#define   ALI_SPDIF_OUT_ENABLE		0x20
#define   ALI_SCTRL_LINE_IN2		(1 << 9)
#define   ALI_SCTRL_GPIO_IN2		(1 << 13)
#define   ALI_SCTRL_LINE_OUT_EN 	(1 << 20)
#define   ALI_SCTRL_GPIO_OUT_EN 	(1 << 23)
#define   ALI_SCTRL_CODEC1_READY	(1 << 24)
#define   ALI_SCTRL_CODEC2_READY	(1 << 25)
#define ALI_AC97_GPIO		0x4c
#define   ALI_AC97_GPIO_ENABLE		0x8000
#define   ALI_AC97_GPIO_DATA_SHIFT	16
#define ALI_SPDIF_CS		0x70
#define ALI_SPDIF_CTRL		0x74
#define   ALI_SPDIF_IN_FUNC_ENABLE	0x02
#define   ALI_SPDIF_IN_CH_STATUS	0x40
#define   ALI_SPDIF_OUT_CH_STATUS	0xbf
#define ALI_START		0x80
#define ALI_STOP		0x84
#define ALI_CSPF		0x90
#define ALI_AINT		0x98
#define ALI_GC_CIR		0xa0
	#define ENDLP_IE		0x00001000
	#define MIDLP_IE		0x00002000
#define ALI_AINTEN		0xa4
#define ALI_VOLUME		0xa8
#define ALI_SBDELTA_DELTA_R     0xac
#define ALI_MISCINT		0xb0
	#define ADDRESS_IRQ		0x00000020
	#define TARGET_REACHED		0x00008000
	#define MIXER_OVERFLOW		0x00000800
	#define MIXER_UNDERFLOW		0x00000400
	#define GPIO_IRQ		0x01000000
#define ALI_SBBL_SBCL           0xc0
#define ALI_SBCTRL_SBE2R_SBDD   0xc4
#define ALI_STIMER		0xc8
#define ALI_GLOBAL_CONTROL	0xd4
#define   ALI_SPDIF_OUT_SEL_PCM		0x00000400 /* bit 10 */
#define   ALI_SPDIF_IN_SUPPORT		0x00000800 /* bit 11 */
#define   ALI_SPDIF_OUT_CH_ENABLE	0x00008000 /* bit 15 */
#define   ALI_SPDIF_IN_CH_ENABLE	0x00080000 /* bit 19 */
#define   ALI_PCM_IN_ENABLE		0x80000000 /* bit 31 */

#define ALI_CSO_ALPHA_FMS	0xe0
#define ALI_LBA			0xe4
#define ALI_ESO_DELTA		0xe8
#define ALI_GVSEL_PAN_VOC_CTRL_EC	0xf0
#define ALI_EBUF1		0xf4
#define ALI_EBUF2		0xf8

#define ALI_REG(codec, x) ((codec)->port + x)

#define MAX_CODECS 2


struct snd_ali;
struct snd_ali_voice;

struct snd_ali_channel_control {
	/* register data */
	struct REGDATA {
		unsigned int start;
		unsigned int stop;
		unsigned int aint;
		unsigned int ainten;
	} data;
		
	/* register addresses */
	struct REGS {
		unsigned int start;
		unsigned int stop;
		unsigned int aint;
		unsigned int ainten;
		unsigned int ac97read;
		unsigned int ac97write;
	} regs;

};

struct snd_ali_voice {
	unsigned int number;
	unsigned int use :1,
		pcm :1,
		midi :1,
		mode :1,
		synth :1,
		running :1;

	/* PCM data */
	struct snd_ali *codec;
	struct snd_pcm_substream *substream;
	struct snd_ali_voice *extra;
	
	int eso;                /* final ESO value for channel */
	int count;              /* runtime->period_size */

	/* --- */

	void *private_data;
	void (*private_free)(void *private_data);
};


struct snd_alidev {

	struct snd_ali_voice voices[ALI_CHANNELS];	

	unsigned int	chcnt;			/* num of opened channels */
	unsigned int	chmap;			/* bitmap for opened channels */
	unsigned int synthcount;

};


#define ALI_GLOBAL_REGS		56
#define ALI_CHANNEL_REGS	8
struct snd_ali_image {
	u32 regs[ALI_GLOBAL_REGS];
	u32 channel_regs[ALI_CHANNELS][ALI_CHANNEL_REGS];
};


struct snd_ali {
	int		irq;
	unsigned long	port;
	unsigned char	revision;

	unsigned int hw_initialized :1;
	unsigned int spdif_support :1;

	struct pci_dev	*pci;
	struct pci_dev	*pci_m1533;
	struct pci_dev	*pci_m7101;

	struct snd_card	*card;
	struct snd_pcm	*pcm[MAX_CODECS];
	struct snd_alidev	synth;
	struct snd_ali_channel_control chregs;

	/* S/PDIF Mask */
	unsigned int	spdif_mask;

	unsigned int spurious_irq_count;
	unsigned int spurious_irq_max_delta;

	unsigned int num_of_codecs;

	struct snd_ac97_bus *ac97_bus;
	struct snd_ac97 *ac97[MAX_CODECS];
	unsigned short	ac97_ext_id;
	unsigned short	ac97_ext_status;

	spinlock_t	reg_lock;
	spinlock_t	voice_alloc;

#ifdef CONFIG_PM_SLEEP
	struct snd_ali_image *image;
#endif
};

static const struct pci_device_id snd_ali_ids[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_AL, PCI_DEVICE_ID_AL_M5451), 0, 0, 0},
	{0, }
};
MODULE_DEVICE_TABLE(pci, snd_ali_ids);

static void snd_ali_clear_voices(struct snd_ali *, unsigned int, unsigned int);
static unsigned short snd_ali_codec_peek(struct snd_ali *, int, unsigned short);
static void snd_ali_codec_poke(struct snd_ali *, int, unsigned short,
			       unsigned short);

/*
 *  AC97 ACCESS
 */

static inline unsigned int snd_ali_5451_peek(struct snd_ali *codec,
					     unsigned int port)
{
	return (unsigned int)inl(ALI_REG(codec, port)); 
}

static inline void snd_ali_5451_poke(struct snd_ali *codec,
				     unsigned int port,
				     unsigned int val)
{
	outl((unsigned int)val, ALI_REG(codec, port));
}

static int snd_ali_codec_ready(struct snd_ali *codec,
			       unsigned int port)
{
	unsigned long end_time;
	unsigned int res;
	
	end_time = jiffies + msecs_to_jiffies(250);

	for (;;) {
		res = snd_ali_5451_peek(codec,port);
		if (!(res & 0x8000))
			return 0;
		if (!time_after_eq(end_time, jiffies))
			break;
		schedule_timeout_uninterruptible(1);
	}

	snd_ali_5451_poke(codec, port, res & ~0x8000);
	dev_dbg(codec->card->dev, "ali_codec_ready: codec is not ready.\n ");
	return -EIO;
}

static int snd_ali_stimer_ready(struct snd_ali *codec)
{
	unsigned long end_time;
	unsigned long dwChk1,dwChk2;
	
	dwChk1 = snd_ali_5451_peek(codec, ALI_STIMER);
	end_time = jiffies + msecs_to_jiffies(250);

	for (;;) {
		dwChk2 = snd_ali_5451_peek(codec, ALI_STIMER);
		if (dwChk2 != dwChk1)
			return 0;
		if (!time_after_eq(end_time, jiffies))
			break;
		schedule_timeout_uninterruptible(1);
	}

	dev_err(codec->card->dev, "ali_stimer_read: stimer is not ready.\n");
	return -EIO;
}

static void snd_ali_codec_poke(struct snd_ali *codec,int secondary,
			       unsigned short reg,
			       unsigned short val)
{
	unsigned int dwVal;
	unsigned int port;

	if (reg >= 0x80) {
		dev_err(codec->card->dev,
			"ali_codec_poke: reg(%xh) invalid.\n", reg);
		return;
	}

	port = codec->chregs.regs.ac97write;

	if (snd_ali_codec_ready(codec, port) < 0)
		return;
	if (snd_ali_stimer_ready(codec) < 0)
		return;

	dwVal  = (unsigned int) (reg & 0xff);
	dwVal |= 0x8000 | (val << 16);
	if (secondary)
		dwVal |= 0x0080;
	if (codec->revision == ALI_5451_V02)
		dwVal |= 0x0100;

	snd_ali_5451_poke(codec, port, dwVal);

	return ;
}

static unsigned short snd_ali_codec_peek(struct snd_ali *codec,
					 int secondary,
					 unsigned short reg)
{
	unsigned int dwVal;
	unsigned int port;

	if (reg >= 0x80) {
		dev_err(codec->card->dev,
			"ali_codec_peek: reg(%xh) invalid.\n", reg);
		return ~0;
	}

	port = codec->chregs.regs.ac97read;

	if (snd_ali_codec_ready(codec, port) < 0)
		return ~0;
	if (snd_ali_stimer_ready(codec) < 0)
		return ~0;

	dwVal  = (unsigned int) (reg & 0xff);
	dwVal |= 0x8000;				/* bit 15*/
	if (secondary)
		dwVal |= 0x0080;

	snd_ali_5451_poke(codec, port, dwVal);

	if (snd_ali_stimer_ready(codec) < 0)
		return ~0;
	if (snd_ali_codec_ready(codec, port) < 0)
		return ~0;
	
	return (snd_ali_5451_peek(codec, port) & 0xffff0000) >> 16;
}

static void snd_ali_codec_write(struct snd_ac97 *ac97,
				unsigned short reg,
				unsigned short val )
{
	struct snd_ali *codec = ac97->private_data;

	dev_dbg(codec->card->dev, "codec_write: reg=%xh data=%xh.\n", reg, val);
	if (reg == AC97_GPIO_STATUS) {
		outl((val << ALI_AC97_GPIO_DATA_SHIFT) | ALI_AC97_GPIO_ENABLE,
		     ALI_REG(codec, ALI_AC97_GPIO));
		return;
	}
	snd_ali_codec_poke(codec, ac97->num, reg, val);
	return ;
}


static unsigned short snd_ali_codec_read(struct snd_ac97 *ac97,
					 unsigned short reg)
{
	struct snd_ali *codec = ac97->private_data;

	dev_dbg(codec->card->dev, "codec_read reg=%xh.\n", reg);
	return snd_ali_codec_peek(codec, ac97->num, reg);
}

/*
 *	AC97 Reset
 */

static int snd_ali_reset_5451(struct snd_ali *codec)
{
	struct pci_dev *pci_dev;
	unsigned short wCount, wReg;
	unsigned int   dwVal;
	
	pci_dev = codec->pci_m1533;
	if (pci_dev) {
		pci_read_config_dword(pci_dev, 0x7c, &dwVal);
		pci_write_config_dword(pci_dev, 0x7c, dwVal | 0x08000000);
		mdelay(5);
		pci_read_config_dword(pci_dev, 0x7c, &dwVal);
		pci_write_config_dword(pci_dev, 0x7c, dwVal & 0xf7ffffff);
		mdelay(5);
	}
	
	pci_dev = codec->pci;
	pci_read_config_dword(pci_dev, 0x44, &dwVal);
	pci_write_config_dword(pci_dev, 0x44, dwVal | 0x000c0000);
	udelay(500);
	pci_read_config_dword(pci_dev, 0x44, &dwVal);
	pci_write_config_dword(pci_dev, 0x44, dwVal & 0xfffbffff);
	mdelay(5);
	
	wCount = 200;
	while(wCount--) {
		wReg = snd_ali_codec_peek(codec, 0, AC97_POWERDOWN);
		if ((wReg & 0x000f) == 0x000f)
			return 0;
		mdelay(5);
	}

	/* non-fatal if you have a non PM capable codec */
	/* dev_warn(codec->card->dev, "ali5451: reset time out\n"); */
	return 0;
}

/*
 *  ALI 5451 Controller
 */

static void snd_ali_enable_special_channel(struct snd_ali *codec,
					   unsigned int channel)
{
	unsigned long dwVal;

	dwVal  = inl(ALI_REG(codec, ALI_GLOBAL_CONTROL));
	dwVal |= 1 << (channel & 0x0000001f);
	outl(dwVal, ALI_REG(codec, ALI_GLOBAL_CONTROL));
}

static void snd_ali_disable_special_channel(struct snd_ali *codec,
					    unsigned int channel)
{
	unsigned long dwVal;

	dwVal  = inl(ALI_REG(codec, ALI_GLOBAL_CONTROL));
	dwVal &= ~(1 << (channel & 0x0000001f));
	outl(dwVal, ALI_REG(codec, ALI_GLOBAL_CONTROL));
}

static void snd_ali_enable_address_interrupt(struct snd_ali *codec)
{
	unsigned int gc;

	gc  = inl(ALI_REG(codec, ALI_GC_CIR));
	gc |= ENDLP_IE;
	gc |= MIDLP_IE;
	outl( gc, ALI_REG(codec, ALI_GC_CIR));
}

static void snd_ali_disable_address_interrupt(struct snd_ali *codec)
{
	unsigned int gc;

	gc  = inl(ALI_REG(codec, ALI_GC_CIR));
	gc &= ~ENDLP_IE;
	gc &= ~MIDLP_IE;
	outl(gc, ALI_REG(codec, ALI_GC_CIR));
}

static void snd_ali_disable_voice_irq(struct snd_ali *codec,
				      unsigned int channel)
{
	unsigned int mask;
	struct snd_ali_channel_control *pchregs = &(codec->chregs);

	dev_dbg(codec->card->dev, "disable_voice_irq channel=%d\n", channel);

	mask = 1 << (channel & 0x1f);
	pchregs->data.ainten  = inl(ALI_REG(codec, pchregs->regs.ainten));
	pchregs->data.ainten &= ~mask;
	outl(pchregs->data.ainten, ALI_REG(codec, pchregs->regs.ainten));
}

static int snd_ali_alloc_pcm_channel(struct snd_ali *codec, int channel)
{
	unsigned int idx =  channel & 0x1f;

	if (codec->synth.chcnt >= ALI_CHANNELS){
		dev_err(codec->card->dev,
			   "ali_alloc_pcm_channel: no free channels.\n");
		return -1;
	}

	if (!(codec->synth.chmap & (1 << idx))) {
		codec->synth.chmap |= 1 << idx;
		codec->synth.chcnt++;
		dev_dbg(codec->card->dev, "alloc_pcm_channel no. %d.\n", idx);
		return idx;
	}
	return -1;
}

static int snd_ali_find_free_channel(struct snd_ali * codec, int rec)
{
	int idx;
	int result = -1;

	dev_dbg(codec->card->dev,
		"find_free_channel: for %s\n", rec ? "rec" : "pcm");

	/* recording */
	if (rec) {
		if (codec->spdif_support &&
		    (inl(ALI_REG(codec, ALI_GLOBAL_CONTROL)) &
		     ALI_SPDIF_IN_SUPPORT))
			idx = ALI_SPDIF_IN_CHANNEL;
		else
			idx = ALI_PCM_IN_CHANNEL;

		result = snd_ali_alloc_pcm_channel(codec, idx);
		if (result >= 0)
			return result;
		else {
			dev_err(codec->card->dev,
				"ali_find_free_channel: record channel is busy now.\n");
			return -1;
		}
	}

	/* playback... */
	if (codec->spdif_support &&
	    (inl(ALI_REG(codec, ALI_GLOBAL_CONTROL)) &
	     ALI_SPDIF_OUT_CH_ENABLE)) {
		idx = ALI_SPDIF_OUT_CHANNEL;
		result = snd_ali_alloc_pcm_channel(codec, idx);
		if (result >= 0)
			return result;
		else
			dev_err(codec->card->dev,
				"ali_find_free_channel: S/PDIF out channel is in busy now.\n");
	}

	for (idx = 0; idx < ALI_CHANNELS; idx++) {
		result = snd_ali_alloc_pcm_channel(codec, idx);
		if (result >= 0)
			return result;
	}
	dev_err(codec->card->dev, "ali_find_free_channel: no free channels.\n");
	return -1;
}

static void snd_ali_free_channel_pcm(struct snd_ali *codec, int channel)
{
	unsigned int idx = channel & 0x0000001f;

	dev_dbg(codec->card->dev, "free_channel_pcm channel=%d\n", channel);

	if (channel < 0 || channel >= ALI_CHANNELS)
		return;

	if (!(codec->synth.chmap & (1 << idx))) {
		dev_err(codec->card->dev,
			"ali_free_channel_pcm: channel %d is not in use.\n",
			channel);
		return;
	} else {
		codec->synth.chmap &= ~(1 << idx);
		codec->synth.chcnt--;
	}
}

static void snd_ali_stop_voice(struct snd_ali *codec, unsigned int channel)
{
	unsigned int mask = 1 << (channel & 0x1f);

	dev_dbg(codec->card->dev, "stop_voice: channel=%d\n", channel);
	outl(mask, ALI_REG(codec, codec->chregs.regs.stop));
}

/*
 *    S/PDIF Part
 */

static void snd_ali_delay(struct snd_ali *codec,int interval)
{
	unsigned long  begintimer,currenttimer;

	begintimer   = inl(ALI_REG(codec, ALI_STIMER));
	currenttimer = inl(ALI_REG(codec, ALI_STIMER));

	while (currenttimer < begintimer + interval) {
		if (snd_ali_stimer_ready(codec) < 0)
			break;
		currenttimer = inl(ALI_REG(codec,  ALI_STIMER));
		cpu_relax();
	}
}

static void snd_ali_detect_spdif_rate(struct snd_ali *codec)
{
	u16 wval;
	u16 count = 0;
	u8  bval, R1 = 0, R2;

	bval  = inb(ALI_REG(codec, ALI_SPDIF_CTRL + 1));
	bval |= 0x1F;
	outb(bval, ALI_REG(codec, ALI_SPDIF_CTRL + 1));

	while ((R1 < 0x0b || R1 > 0x0e) && R1 != 0x12 && count <= 50000) {
		count ++;
		snd_ali_delay(codec, 6);
		bval = inb(ALI_REG(codec, ALI_SPDIF_CTRL + 1));
		R1 = bval & 0x1F;
	}

	if (count > 50000) {
		dev_err(codec->card->dev, "ali_detect_spdif_rate: timeout!\n");
		return;
	}

	for (count = 0; count <= 50000; count++) {
		snd_ali_delay(codec, 6);
		bval = inb(ALI_REG(codec,ALI_SPDIF_CTRL + 1));
		R2 = bval & 0x1F;
		if (R2 != R1)
			R1 = R2;
		else
			break;
	}

	if (count > 50000) {
		dev_err(codec->card->dev, "ali_detect_spdif_rate: timeout!\n");
		return;
	}

	if (R2 >= 0x0b && R2 <= 0x0e) {
		wval  = inw(ALI_REG(codec, ALI_SPDIF_CTRL + 2));
		wval &= 0xe0f0;
		wval |= (0x09 << 8) | 0x05;
		outw(wval, ALI_REG(codec, ALI_SPDIF_CTRL + 2));

		bval  = inb(ALI_REG(codec, ALI_SPDIF_CS + 3)) & 0xf0;
		outb(bval | 0x02, ALI_REG(codec, ALI_SPDIF_CS + 3));
	} else if (R2 == 0x12) {
		wval  = inw(ALI_REG(codec, ALI_SPDIF_CTRL + 2));
		wval &= 0xe0f0;
		wval |= (0x0e << 8) | 0x08;
		outw(wval, ALI_REG(codec, ALI_SPDIF_CTRL + 2));

		bval  = inb(ALI_REG(codec,ALI_SPDIF_CS + 3)) & 0xf0;
		outb(bval | 0x03, ALI_REG(codec, ALI_SPDIF_CS + 3));
	}
}

static unsigned int snd_ali_get_spdif_in_rate(struct snd_ali *codec)
{
	u32	dwRate;
	u8	bval;

	bval  = inb(ALI_REG(codec, ALI_SPDIF_CTRL));
	bval &= 0x7f;
	bval |= 0x40;
	outb(bval, ALI_REG(codec, ALI_SPDIF_CTRL));

	snd_ali_detect_spdif_rate(codec);

	bval  = inb(ALI_REG(codec, ALI_SPDIF_CS + 3));
	bval &= 0x0f;

	switch (bval) {
	case 0: dwRate = 44100; break;
	case 1: dwRate = 48000; break;
	case 2: dwRate = 32000; break;
	default: dwRate = 0; break;
	}

	return dwRate;
}

static void snd_ali_enable_spdif_in(struct snd_ali *codec)
{	
	unsigned int dwVal;

	dwVal = inl(ALI_REG(codec, ALI_GLOBAL_CONTROL));
	dwVal |= ALI_SPDIF_IN_SUPPORT;
	outl(dwVal, ALI_REG(codec, ALI_GLOBAL_CONTROL));

	dwVal = inb(ALI_REG(codec, ALI_SPDIF_CTRL));
	dwVal |= 0x02;
	outb(dwVal, ALI_REG(codec, ALI_SPDIF_CTRL));

	snd_ali_enable_special_channel(codec, ALI_SPDIF_IN_CHANNEL);
}

static void snd_ali_disable_spdif_in(struct snd_ali *codec)
{
	unsigned int dwVal;
	
	dwVal = inl(ALI_REG(codec, ALI_GLOBAL_CONTROL));
	dwVal &= ~ALI_SPDIF_IN_SUPPORT;
	outl(dwVal, ALI_REG(codec, ALI_GLOBAL_CONTROL));
	
	snd_ali_disable_special_channel(codec, ALI_SPDIF_IN_CHANNEL);	
}


static void snd_ali_set_spdif_out_rate(struct snd_ali *codec, unsigned int rate)
{
	unsigned char  bVal;
	unsigned int  dwRate;
	
	switch (rate) {
	case 32000: dwRate = 0x300; break;
	case 48000: dwRate = 0x200; break;
	default: dwRate = 0; break;
	}
	
	bVal  = inb(ALI_REG(codec, ALI_SPDIF_CTRL));
	bVal &= (unsigned char)(~(1<<6));
	
	bVal |= 0x80;		/* select right */
	outb(bVal, ALI_REG(codec, ALI_SPDIF_CTRL));
	outb(dwRate | 0x20, ALI_REG(codec, ALI_SPDIF_CS + 2));
	
	bVal &= ~0x80;	/* select left */
	outb(bVal, ALI_REG(codec, ALI_SPDIF_CTRL));
	outw(rate | 0x10, ALI_REG(codec, ALI_SPDIF_CS + 2));
}

static void snd_ali_enable_spdif_out(struct snd_ali *codec)
{
	unsigned short wVal;
	unsigned char bVal;
        struct pci_dev *pci_dev;

        pci_dev = codec->pci_m1533;
        if (pci_dev == NULL)
                return;
        pci_read_config_byte(pci_dev, 0x61, &bVal);
        bVal |= 0x40;
        pci_write_config_byte(pci_dev, 0x61, bVal);
        pci_read_config_byte(pci_dev, 0x7d, &bVal);
        bVal |= 0x01;
        pci_write_config_byte(pci_dev, 0x7d, bVal);

        pci_read_config_byte(pci_dev, 0x7e, &bVal);
        bVal &= (~0x20);
        bVal |= 0x10;
        pci_write_config_byte(pci_dev, 0x7e, bVal);

	bVal = inb(ALI_REG(codec, ALI_SCTRL));
	outb(bVal | ALI_SPDIF_OUT_ENABLE, ALI_REG(codec, ALI_SCTRL));

	bVal = inb(ALI_REG(codec, ALI_SPDIF_CTRL));
	outb(bVal & ALI_SPDIF_OUT_CH_STATUS, ALI_REG(codec, ALI_SPDIF_CTRL));
   
	wVal = inw(ALI_REG(codec, ALI_GLOBAL_CONTROL));
	wVal |= ALI_SPDIF_OUT_SEL_PCM;
	outw(wVal, ALI_REG(codec, ALI_GLOBAL_CONTROL));
	snd_ali_disable_special_channel(codec, ALI_SPDIF_OUT_CHANNEL);
}

static void snd_ali_enable_spdif_chnout(struct snd_ali *codec)
{
	unsigned short wVal;

	wVal  = inw(ALI_REG(codec, ALI_GLOBAL_CONTROL));
   	wVal &= ~ALI_SPDIF_OUT_SEL_PCM;
   	outw(wVal, ALI_REG(codec, ALI_GLOBAL_CONTROL));
/*
	wVal = inw(ALI_REG(codec, ALI_SPDIF_CS));
	if (flag & ALI_SPDIF_OUT_NON_PCM)
   		wVal |= 0x0002;
	else	
		wVal &= (~0x0002);
   	outw(wVal, ALI_REG(codec, ALI_SPDIF_CS));
*/
	snd_ali_enable_special_channel(codec, ALI_SPDIF_OUT_CHANNEL);
}

static void snd_ali_disable_spdif_chnout(struct snd_ali *codec)
{
	unsigned short wVal;

  	wVal  = inw(ALI_REG(codec, ALI_GLOBAL_CONTROL));
   	wVal |= ALI_SPDIF_OUT_SEL_PCM;
   	outw(wVal, ALI_REG(codec, ALI_GLOBAL_CONTROL));

	snd_ali_enable_special_channel(codec, ALI_SPDIF_OUT_CHANNEL);
}

static void snd_ali_disable_spdif_out(struct snd_ali *codec)
{
	unsigned char  bVal;

	bVal = inb(ALI_REG(codec, ALI_SCTRL));
	outb(bVal & ~ALI_SPDIF_OUT_ENABLE, ALI_REG(codec, ALI_SCTRL));

	snd_ali_disable_spdif_chnout(codec);
}

static void snd_ali_update_ptr(struct snd_ali *codec, int channel)
{
	struct snd_ali_voice *pvoice;
	struct snd_ali_channel_control *pchregs;
	unsigned int old, mask;

	pchregs = &(codec->chregs);

	/* check if interrupt occurred for channel */
	old  = pchregs->data.aint;
	mask = 1U << (channel & 0x1f);

	if (!(old & mask))
		return;

	pvoice = &codec->synth.voices[channel];

	udelay(100);
	spin_lock(&codec->reg_lock);

	if (pvoice->pcm && pvoice->substream) {
		/* pcm interrupt */
		if (pvoice->running) {
			dev_dbg(codec->card->dev,
				"update_ptr: cso=%4.4x cspf=%d.\n",
				inw(ALI_REG(codec, ALI_CSO_ALPHA_FMS + 2)),
				(inl(ALI_REG(codec, ALI_CSPF)) & mask) == mask);
			spin_unlock(&codec->reg_lock);
			snd_pcm_period_elapsed(pvoice->substream);
			spin_lock(&codec->reg_lock);
		} else {
			snd_ali_stop_voice(codec, channel);
			snd_ali_disable_voice_irq(codec, channel);
		}	
	} else if (codec->synth.voices[channel].synth) {
		/* synth interrupt */
	} else if (codec->synth.voices[channel].midi) {
		/* midi interrupt */
	} else {
		/* unknown interrupt */
		snd_ali_stop_voice(codec, channel);
		snd_ali_disable_voice_irq(codec, channel);
	}
	spin_unlock(&codec->reg_lock);
	outl(mask,ALI_REG(codec,pchregs->regs.aint));
	pchregs->data.aint = old & (~mask);
}

static irqreturn_t snd_ali_card_interrupt(int irq, void *dev_id)
{
	struct snd_ali 	*codec = dev_id;
	int channel;
	unsigned int audio_int;
	struct snd_ali_channel_control *pchregs;

	if (codec == NULL || !codec->hw_initialized)
		return IRQ_NONE;

	audio_int = inl(ALI_REG(codec, ALI_MISCINT));
	if (!audio_int)
		return IRQ_NONE;

	pchregs = &(codec->chregs);
	if (audio_int & ADDRESS_IRQ) {
		/* get interrupt status for all channels */
		pchregs->data.aint = inl(ALI_REG(codec, pchregs->regs.aint));
		for (channel = 0; channel < ALI_CHANNELS; channel++)
			snd_ali_update_ptr(codec, channel);
	}
	outl((TARGET_REACHED | MIXER_OVERFLOW | MIXER_UNDERFLOW),
		ALI_REG(codec, ALI_MISCINT));

	return IRQ_HANDLED;
}


static struct snd_ali_voice *snd_ali_alloc_voice(struct snd_ali * codec,
						 int type, int rec, int channel)
{
	struct snd_ali_voice *pvoice;
	int idx;

	dev_dbg(codec->card->dev, "alloc_voice: type=%d rec=%d\n", type, rec);

	spin_lock_irq(&codec->voice_alloc);
	if (type == SNDRV_ALI_VOICE_TYPE_PCM) {
		idx = channel > 0 ? snd_ali_alloc_pcm_channel(codec, channel) :
			snd_ali_find_free_channel(codec,rec);
		if (idx < 0) {
			dev_err(codec->card->dev, "ali_alloc_voice: err.\n");
			spin_unlock_irq(&codec->voice_alloc);
			return NULL;
		}
		pvoice = &(codec->synth.voices[idx]);
		pvoice->codec = codec;
		pvoice->use = 1;
		pvoice->pcm = 1;
		pvoice->mode = rec;
		spin_unlock_irq(&codec->voice_alloc);
		return pvoice;
	}
	spin_unlock_irq(&codec->voice_alloc);
	return NULL;
}


static void snd_ali_free_voice(struct snd_ali * codec,
			       struct snd_ali_voice *pvoice)
{
	void (*private_free)(void *);
	void *private_data;

	dev_dbg(codec->card->dev, "free_voice: channel=%d\n", pvoice->number);
	if (!pvoice->use)
		return;
	snd_ali_clear_voices(codec, pvoice->number, pvoice->number);
	spin_lock_irq(&codec->voice_alloc);
	private_free = pvoice->private_free;
	private_data = pvoice->private_data;
	pvoice->private_free = NULL;
	pvoice->private_data = NULL;
	if (pvoice->pcm)
		snd_ali_free_channel_pcm(codec, pvoice->number);
	pvoice->use = pvoice->pcm = pvoice->synth = 0;
	pvoice->substream = NULL;
	spin_unlock_irq(&codec->voice_alloc);
	if (private_free)
		private_free(private_data);
}


static void snd_ali_clear_voices(struct snd_ali *codec,
				 unsigned int v_min,
				 unsigned int v_max)
{
	unsigned int i;

	for (i = v_min; i <= v_max; i++) {
		snd_ali_stop_voice(codec, i);
		snd_ali_disable_voice_irq(codec, i);
	}
}

static void snd_ali_write_voice_regs(struct snd_ali *codec,
			 unsigned int Channel,
			 unsigned int LBA,
			 unsigned int CSO,
			 unsigned int ESO,
			 unsigned int DELTA,
			 unsigned int ALPHA_FMS,
			 unsigned int GVSEL,
			 unsigned int PAN,
			 unsigned int VOL,
			 unsigned int CTRL,
			 unsigned int EC)
{
	unsigned int ctlcmds[4];
	
	outb((unsigned char)(Channel & 0x001f), ALI_REG(codec, ALI_GC_CIR));

	ctlcmds[0] =  (CSO << 16) | (ALPHA_FMS & 0x0000ffff);
	ctlcmds[1] =  LBA;
	ctlcmds[2] =  (ESO << 16) | (DELTA & 0x0ffff);
	ctlcmds[3] =  (GVSEL << 31) |
		      ((PAN & 0x0000007f) << 24) |
		      ((VOL & 0x000000ff) << 16) |
		      ((CTRL & 0x0000000f) << 12) |
		      (EC & 0x00000fff);

	outb(Channel, ALI_REG(codec, ALI_GC_CIR));

	outl(ctlcmds[0], ALI_REG(codec, ALI_CSO_ALPHA_FMS));
	outl(ctlcmds[1], ALI_REG(codec, ALI_LBA));
	outl(ctlcmds[2], ALI_REG(codec, ALI_ESO_DELTA));
	outl(ctlcmds[3], ALI_REG(codec, ALI_GVSEL_PAN_VOC_CTRL_EC));

	outl(0x30000000, ALI_REG(codec, ALI_EBUF1));	/* Still Mode */
	outl(0x30000000, ALI_REG(codec, ALI_EBUF2));	/* Still Mode */
}

static unsigned int snd_ali_convert_rate(unsigned int rate, int rec)
{
	unsigned int delta;

	if (rate < 4000)
		rate = 4000;
	if (rate > 48000)
		rate = 48000;

	if (rec) {
		if (rate == 44100)
			delta = 0x116a;
		else if (rate == 8000)
			delta = 0x6000;
		else if (rate == 48000)
			delta = 0x1000;
		else
			delta = ((48000 << 12) / rate) & 0x0000ffff;
	} else {
		if (rate == 44100)
			delta = 0xeb3;
		else if (rate == 8000)
			delta = 0x2ab;
		else if (rate == 48000)
			delta = 0x1000;
		else 
			delta = (((rate << 12) + rate) / 48000) & 0x0000ffff;
	}

	return delta;
}

static unsigned int snd_ali_control_mode(struct snd_pcm_substream *substream)
{
	unsigned int CTRL;
	struct snd_pcm_runtime *runtime = substream->runtime;

	/* set ctrl mode
	   CTRL default: 8-bit (unsigned) mono, loop mode enabled
	 */
	CTRL = 0x00000001;
	if (snd_pcm_format_width(runtime->format) == 16)
		CTRL |= 0x00000008;	/* 16-bit data */
	if (!snd_pcm_format_unsigned(runtime->format))
		CTRL |= 0x00000002;	/* signed data */
	if (runtime->channels > 1)
		CTRL |= 0x00000004;	/* stereo data */
	return CTRL;
}

/*
 *  PCM part
 */

static int snd_ali_trigger(struct snd_pcm_substream *substream,
			       int cmd)
				    
{
	struct snd_ali *codec = snd_pcm_substream_chip(substream);
	struct snd_pcm_substream *s;
	unsigned int what, whati;
	struct snd_ali_voice *pvoice, *evoice;
	unsigned int val;
	int do_start;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		do_start = 1;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		do_start = 0;
		break;
	default:
		return -EINVAL;
	}

	what = whati = 0;
	snd_pcm_group_for_each_entry(s, substream) {
		if ((struct snd_ali *) snd_pcm_substream_chip(s) == codec) {
			pvoice = s->runtime->private_data;
			evoice = pvoice->extra;
			what |= 1 << (pvoice->number & 0x1f);
			if (evoice == NULL)
				whati |= 1 << (pvoice->number & 0x1f);
			else {
				whati |= 1 << (evoice->number & 0x1f);
				what |= 1 << (evoice->number & 0x1f);
			}
			if (do_start) {
				pvoice->running = 1;
				if (evoice != NULL)
					evoice->running = 1;
			} else {
				pvoice->running = 0;
				if (evoice != NULL)
					evoice->running = 0;
			}
			snd_pcm_trigger_done(s, substream);
		}
	}
	spin_lock(&codec->reg_lock);
	if (!do_start)
		outl(what, ALI_REG(codec, ALI_STOP));
	val = inl(ALI_REG(codec, ALI_AINTEN));
	if (do_start)
		val |= whati;
	else
		val &= ~whati;
	outl(val, ALI_REG(codec, ALI_AINTEN));
	if (do_start)
		outl(what, ALI_REG(codec, ALI_START));
	dev_dbg(codec->card->dev, "trigger: what=%xh whati=%xh\n", what, whati);
	spin_unlock(&codec->reg_lock);

	return 0;
}

static int snd_ali_playback_hw_params(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *hw_params)
{
	struct snd_ali *codec = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_ali_voice *pvoice = runtime->private_data;
	struct snd_ali_voice *evoice = pvoice->extra;

	/* voice management */

	if (params_buffer_size(hw_params) / 2 !=
	    params_period_size(hw_params)) {
		if (!evoice) {
			evoice = snd_ali_alloc_voice(codec,
						     SNDRV_ALI_VOICE_TYPE_PCM,
						     0, -1);
			if (!evoice)
				return -ENOMEM;
			pvoice->extra = evoice;
			evoice->substream = substream;
		}
	} else {
		if (evoice) {
			snd_ali_free_voice(codec, evoice);
			pvoice->extra = evoice = NULL;
		}
	}

	return 0;
}

static int snd_ali_playback_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_ali *codec = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_ali_voice *pvoice = runtime->private_data;
	struct snd_ali_voice *evoice = pvoice ? pvoice->extra : NULL;

	if (evoice) {
		snd_ali_free_voice(codec, evoice);
		pvoice->extra = NULL;
	}
	return 0;
}

static int snd_ali_playback_prepare(struct snd_pcm_substream *substream)
{
	struct snd_ali *codec = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_ali_voice *pvoice = runtime->private_data;
	struct snd_ali_voice *evoice = pvoice->extra;

	unsigned int LBA;
	unsigned int Delta;
	unsigned int ESO;
	unsigned int CTRL;
	unsigned int GVSEL;
	unsigned int PAN;
	unsigned int VOL;
	unsigned int EC;
	
	dev_dbg(codec->card->dev, "playback_prepare ...\n");

	spin_lock_irq(&codec->reg_lock);	
	
	/* set Delta (rate) value */
	Delta = snd_ali_convert_rate(runtime->rate, 0);

	if (pvoice->number == ALI_SPDIF_IN_CHANNEL || 
	    pvoice->number == ALI_PCM_IN_CHANNEL)
		snd_ali_disable_special_channel(codec, pvoice->number);
	else if (codec->spdif_support &&
		 (inl(ALI_REG(codec, ALI_GLOBAL_CONTROL)) &
		  ALI_SPDIF_OUT_CH_ENABLE)
		 && pvoice->number == ALI_SPDIF_OUT_CHANNEL) {
		snd_ali_set_spdif_out_rate(codec, runtime->rate);
		Delta = 0x1000;
	}
	
	/* set Loop Back Address */
	LBA = runtime->dma_addr;

	/* set interrupt count size */
	pvoice->count = runtime->period_size;

	/* set target ESO for channel */
	pvoice->eso = runtime->buffer_size; 

	dev_dbg(codec->card->dev, "playback_prepare: eso=%xh count=%xh\n",
		       pvoice->eso, pvoice->count);

	/* set ESO to capture first MIDLP interrupt */
	ESO = pvoice->eso -1;
	/* set ctrl mode */
	CTRL = snd_ali_control_mode(substream);

	GVSEL = 1;
	PAN = 0;
	VOL = 0;
	EC = 0;
	dev_dbg(codec->card->dev, "playback_prepare:\n");
	dev_dbg(codec->card->dev,
		"ch=%d, Rate=%d Delta=%xh,GVSEL=%xh,PAN=%xh,CTRL=%xh\n",
		       pvoice->number,runtime->rate,Delta,GVSEL,PAN,CTRL);
	snd_ali_write_voice_regs(codec,
				 pvoice->number,
				 LBA,
				 0,	/* cso */
				 ESO,
				 Delta,
				 0,	/* alpha */
				 GVSEL,
				 PAN,
				 VOL,
				 CTRL,
				 EC);
	if (evoice) {
		evoice->count = pvoice->count;
		evoice->eso = pvoice->count << 1;
		ESO = evoice->eso - 1;
		snd_ali_write_voice_regs(codec,
					 evoice->number,
					 LBA,
					 0,	/* cso */
					 ESO,
					 Delta,
					 0,	/* alpha */
					 GVSEL,
					 0x7f,
					 0x3ff,
					 CTRL,
					 EC);
	}
	spin_unlock_irq(&codec->reg_lock);
	return 0;
}


static int snd_ali_prepare(struct snd_pcm_substream *substream)
{
	struct snd_ali *codec = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_ali_voice *pvoice = runtime->private_data;
	unsigned int LBA;
	unsigned int Delta;
	unsigned int ESO;
	unsigned int CTRL;
	unsigned int GVSEL;
	unsigned int PAN;
	unsigned int VOL;
	unsigned int EC;
	u8	 bValue;

	spin_lock_irq(&codec->reg_lock);

	dev_dbg(codec->card->dev, "ali_prepare...\n");

	snd_ali_enable_special_channel(codec,pvoice->number);

	Delta = (pvoice->number == ALI_MODEM_IN_CHANNEL ||
		 pvoice->number == ALI_MODEM_OUT_CHANNEL) ? 
		0x1000 : snd_ali_convert_rate(runtime->rate, pvoice->mode);

	/* Prepare capture intr channel */
	if (pvoice->number == ALI_SPDIF_IN_CHANNEL) {

		unsigned int rate;
		
		spin_unlock_irq(&codec->reg_lock);
		if (codec->revision != ALI_5451_V02)
			return -1;

		rate = snd_ali_get_spdif_in_rate(codec);
		if (rate == 0) {
			dev_warn(codec->card->dev,
				 "ali_capture_prepare: spdif rate detect err!\n");
			rate = 48000;
		}
		spin_lock_irq(&codec->reg_lock);
		bValue = inb(ALI_REG(codec,ALI_SPDIF_CTRL));
		if (bValue & 0x10) {
			outb(bValue,ALI_REG(codec,ALI_SPDIF_CTRL));
			dev_warn(codec->card->dev,
				 "clear SPDIF parity error flag.\n");
		}

		if (rate != 48000)
			Delta = ((rate << 12) / runtime->rate) & 0x00ffff;
	}

	/* set target ESO for channel  */
	pvoice->eso = runtime->buffer_size; 

	/* set interrupt count size  */
	pvoice->count = runtime->period_size;

	/* set Loop Back Address  */
	LBA = runtime->dma_addr;

	/* set ESO to capture first MIDLP interrupt  */
	ESO = pvoice->eso - 1;
	CTRL = snd_ali_control_mode(substream);
	GVSEL = 0;
	PAN = 0x00;
	VOL = 0x00;
	EC = 0;

	snd_ali_write_voice_regs(    codec,
				     pvoice->number,
				     LBA,
				     0,	/* cso */
				     ESO,
				     Delta,
				     0,	/* alpha */
				     GVSEL,
				     PAN,
				     VOL,
				     CTRL,
				     EC);

	spin_unlock_irq(&codec->reg_lock);

	return 0;
}


static snd_pcm_uframes_t
snd_ali_playback_pointer(struct snd_pcm_substream *substream)
{
	struct snd_ali *codec = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_ali_voice *pvoice = runtime->private_data;
	unsigned int cso;

	spin_lock(&codec->reg_lock);
	if (!pvoice->running) {
		spin_unlock(&codec->reg_lock);
		return 0;
	}
	outb(pvoice->number, ALI_REG(codec, ALI_GC_CIR));
	cso = inw(ALI_REG(codec, ALI_CSO_ALPHA_FMS + 2));
	spin_unlock(&codec->reg_lock);
	dev_dbg(codec->card->dev, "playback pointer returned cso=%xh.\n", cso);

	cso %= runtime->buffer_size;
	return cso;
}


static snd_pcm_uframes_t snd_ali_pointer(struct snd_pcm_substream *substream)
{
	struct snd_ali *codec = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_ali_voice *pvoice = runtime->private_data;
	unsigned int cso;

	spin_lock(&codec->reg_lock);
	if (!pvoice->running) {
		spin_unlock(&codec->reg_lock);
		return 0;
	}
	outb(pvoice->number, ALI_REG(codec, ALI_GC_CIR));
	cso = inw(ALI_REG(codec, ALI_CSO_ALPHA_FMS + 2));
	spin_unlock(&codec->reg_lock);

	cso %= runtime->buffer_size;
	return cso;
}

static const struct snd_pcm_hardware snd_ali_playback =
{
	.info =		(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
			 SNDRV_PCM_INFO_BLOCK_TRANSFER |
			 SNDRV_PCM_INFO_MMAP_VALID |
			 SNDRV_PCM_INFO_RESUME |
			 SNDRV_PCM_INFO_SYNC_START),
	.formats =	(SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE |
			 SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_U16_LE),
	.rates =	SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	.rate_min =		4000,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	(256*1024),
	.period_bytes_min =	64,
	.period_bytes_max =	(256*1024),
	.periods_min =		1,
	.periods_max =		1024,
	.fifo_size =		0,
};

/*
 *  Capture support device description
 */

static const struct snd_pcm_hardware snd_ali_capture =
{
	.info =		(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
			 SNDRV_PCM_INFO_BLOCK_TRANSFER |
			 SNDRV_PCM_INFO_MMAP_VALID |
			 SNDRV_PCM_INFO_RESUME |
			 SNDRV_PCM_INFO_SYNC_START),
	.formats =	(SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE |
			 SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_U16_LE),
	.rates =	SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	.rate_min =		4000,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	(128*1024),
	.period_bytes_min =	64,
	.period_bytes_max =	(128*1024),
	.periods_min =		1,
	.periods_max =		1024,
	.fifo_size =		0,
};

static void snd_ali_pcm_free_substream(struct snd_pcm_runtime *runtime)
{
	struct snd_ali_voice *pvoice = runtime->private_data;

	if (pvoice)
		snd_ali_free_voice(pvoice->codec, pvoice);
}

static int snd_ali_open(struct snd_pcm_substream *substream, int rec,
			int channel, const struct snd_pcm_hardware *phw)
{
	struct snd_ali *codec = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_ali_voice *pvoice;

	pvoice = snd_ali_alloc_voice(codec, SNDRV_ALI_VOICE_TYPE_PCM, rec,
				     channel);
	if (!pvoice)
		return -EAGAIN;

	pvoice->substream = substream;
	runtime->private_data = pvoice;
	runtime->private_free = snd_ali_pcm_free_substream;

	runtime->hw = *phw;
	snd_pcm_set_sync(substream);
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_SIZE,
				     0, 64*1024);
	return 0;
}

static int snd_ali_playback_open(struct snd_pcm_substream *substream)
{
	return snd_ali_open(substream, 0, -1, &snd_ali_playback);
}

static int snd_ali_capture_open(struct snd_pcm_substream *substream)
{
	return snd_ali_open(substream, 1, -1, &snd_ali_capture);
}

static int snd_ali_playback_close(struct snd_pcm_substream *substream)
{
	return 0;
}

static int snd_ali_close(struct snd_pcm_substream *substream)
{
	struct snd_ali *codec = snd_pcm_substream_chip(substream);
	struct snd_ali_voice *pvoice = substream->runtime->private_data;

	snd_ali_disable_special_channel(codec,pvoice->number);

	return 0;
}

static const struct snd_pcm_ops snd_ali_playback_ops = {
	.open =		snd_ali_playback_open,
	.close =	snd_ali_playback_close,
	.hw_params =	snd_ali_playback_hw_params,
	.hw_free =	snd_ali_playback_hw_free,
	.prepare =	snd_ali_playback_prepare,
	.trigger =	snd_ali_trigger,
	.pointer =	snd_ali_playback_pointer,
};

static const struct snd_pcm_ops snd_ali_capture_ops = {
	.open =		snd_ali_capture_open,
	.close =	snd_ali_close,
	.prepare =	snd_ali_prepare,
	.trigger =	snd_ali_trigger,
	.pointer =	snd_ali_pointer,
};

/*
 * Modem PCM
 */

static int snd_ali_modem_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *hw_params)
{
	struct snd_ali *chip = snd_pcm_substream_chip(substream);
	unsigned int modem_num = chip->num_of_codecs - 1;
	snd_ac97_write(chip->ac97[modem_num], AC97_LINE1_RATE,
		       params_rate(hw_params));
	snd_ac97_write(chip->ac97[modem_num], AC97_LINE1_LEVEL, 0);
	return 0;
}

static const struct snd_pcm_hardware snd_ali_modem =
{
	.info =		(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
			 SNDRV_PCM_INFO_BLOCK_TRANSFER |
			 SNDRV_PCM_INFO_MMAP_VALID |
			 SNDRV_PCM_INFO_RESUME |
			 SNDRV_PCM_INFO_SYNC_START),
	.formats =	SNDRV_PCM_FMTBIT_S16_LE,
	.rates =	(SNDRV_PCM_RATE_KNOT | SNDRV_PCM_RATE_8000 |
			 SNDRV_PCM_RATE_16000),
	.rate_min =		8000,
	.rate_max =		16000,
	.channels_min =		1,
	.channels_max =		1,
	.buffer_bytes_max =	(256*1024),
	.period_bytes_min =	64,
	.period_bytes_max =	(256*1024),
	.periods_min =		1,
	.periods_max =		1024,
	.fifo_size =		0,
};

static int snd_ali_modem_open(struct snd_pcm_substream *substream, int rec,
			      int channel)
{
	static const unsigned int rates[] = {8000, 9600, 12000, 16000};
	static const struct snd_pcm_hw_constraint_list hw_constraint_rates = {
		.count = ARRAY_SIZE(rates),
		.list = rates,
		.mask = 0,
	};
	int err = snd_ali_open(substream, rec, channel, &snd_ali_modem);

	if (err)
		return err;
	return snd_pcm_hw_constraint_list(substream->runtime, 0,
			SNDRV_PCM_HW_PARAM_RATE, &hw_constraint_rates);
}

static int snd_ali_modem_playback_open(struct snd_pcm_substream *substream)
{
	return snd_ali_modem_open(substream, 0, ALI_MODEM_OUT_CHANNEL);
}

static int snd_ali_modem_capture_open(struct snd_pcm_substream *substream)
{
	return snd_ali_modem_open(substream, 1, ALI_MODEM_IN_CHANNEL);
}

static const struct snd_pcm_ops snd_ali_modem_playback_ops = {
	.open =		snd_ali_modem_playback_open,
	.close =	snd_ali_close,
	.hw_params =	snd_ali_modem_hw_params,
	.prepare =	snd_ali_prepare,
	.trigger =	snd_ali_trigger,
	.pointer =	snd_ali_pointer,
};

static const struct snd_pcm_ops snd_ali_modem_capture_ops = {
	.open =		snd_ali_modem_capture_open,
	.close =	snd_ali_close,
	.hw_params =	snd_ali_modem_hw_params,
	.prepare =	snd_ali_prepare,
	.trigger =	snd_ali_trigger,
	.pointer =	snd_ali_pointer,
};


struct ali_pcm_description {
	char *name;
	unsigned int playback_num;
	unsigned int capture_num;
	const struct snd_pcm_ops *playback_ops;
	const struct snd_pcm_ops *capture_ops;
	unsigned short class;
};


static void snd_ali_pcm_free(struct snd_pcm *pcm)
{
	struct snd_ali *codec = pcm->private_data;
	codec->pcm[pcm->device] = NULL;
}


static int snd_ali_pcm(struct snd_ali *codec, int device,
		       struct ali_pcm_description *desc)
{
	struct snd_pcm *pcm;
	int err;

	err = snd_pcm_new(codec->card, desc->name, device,
			  desc->playback_num, desc->capture_num, &pcm);
	if (err < 0) {
		dev_err(codec->card->dev,
			"snd_ali_pcm: err called snd_pcm_new.\n");
		return err;
	}
	pcm->private_data = codec;
	pcm->private_free = snd_ali_pcm_free;
	if (desc->playback_ops)
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK,
				desc->playback_ops);
	if (desc->capture_ops)
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE,
				desc->capture_ops);

	snd_pcm_set_managed_buffer_all(pcm, SNDRV_DMA_TYPE_DEV,
				       &codec->pci->dev, 64*1024, 128*1024);

	pcm->info_flags = 0;
	pcm->dev_class = desc->class;
	pcm->dev_subclass = SNDRV_PCM_SUBCLASS_GENERIC_MIX;
	strcpy(pcm->name, desc->name);
	codec->pcm[0] = pcm;
	return 0;
}

static struct ali_pcm_description ali_pcms[] = {
	{ .name = "ALI 5451",
	  .playback_num = ALI_CHANNELS,
	  .capture_num = 1,
	  .playback_ops = &snd_ali_playback_ops,
	  .capture_ops = &snd_ali_capture_ops
	},
	{ .name = "ALI 5451 modem",
	  .playback_num = 1,
	  .capture_num = 1,
	  .playback_ops = &snd_ali_modem_playback_ops,
	  .capture_ops = &snd_ali_modem_capture_ops,
	  .class = SNDRV_PCM_CLASS_MODEM
	}
};

static int snd_ali_build_pcms(struct snd_ali *codec)
{
	int i, err;
	for (i = 0; i < codec->num_of_codecs && i < ARRAY_SIZE(ali_pcms); i++) {
		err = snd_ali_pcm(codec, i, &ali_pcms[i]);
		if (err < 0)
			return err;
	}
	return 0;
}


#define ALI5451_SPDIF(xname, xindex, value) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex,\
.info = snd_ali5451_spdif_info, .get = snd_ali5451_spdif_get, \
.put = snd_ali5451_spdif_put, .private_value = value}

#define snd_ali5451_spdif_info		snd_ctl_boolean_mono_info

static int snd_ali5451_spdif_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ali *codec = kcontrol->private_data;
	unsigned int spdif_enable;

	spdif_enable = ucontrol->value.integer.value[0] ? 1 : 0;

	spin_lock_irq(&codec->reg_lock);
	switch (kcontrol->private_value) {
	case 0:
		spdif_enable = (codec->spdif_mask & 0x02) ? 1 : 0;
		break;
	case 1:
		spdif_enable = ((codec->spdif_mask & 0x02) &&
			  (codec->spdif_mask & 0x04)) ? 1 : 0;
		break;
	case 2:
		spdif_enable = (codec->spdif_mask & 0x01) ? 1 : 0;
		break;
	default:
		break;
	}
	ucontrol->value.integer.value[0] = spdif_enable;
	spin_unlock_irq(&codec->reg_lock);
	return 0;
}

static int snd_ali5451_spdif_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ali *codec = kcontrol->private_data;
	unsigned int change = 0, spdif_enable = 0;

	spdif_enable = ucontrol->value.integer.value[0] ? 1 : 0;

	spin_lock_irq(&codec->reg_lock);
	switch (kcontrol->private_value) {
	case 0:
		change = (codec->spdif_mask & 0x02) ? 1 : 0;
		change = change ^ spdif_enable;
		if (change) {
			if (spdif_enable) {
				codec->spdif_mask |= 0x02;
				snd_ali_enable_spdif_out(codec);
			} else {
				codec->spdif_mask &= ~(0x02);
				codec->spdif_mask &= ~(0x04);
				snd_ali_disable_spdif_out(codec);
			}
		}
		break;
	case 1: 
		change = (codec->spdif_mask & 0x04) ? 1 : 0;
		change = change ^ spdif_enable;
		if (change && (codec->spdif_mask & 0x02)) {
			if (spdif_enable) {
				codec->spdif_mask |= 0x04;
				snd_ali_enable_spdif_chnout(codec);
			} else {
				codec->spdif_mask &= ~(0x04);
				snd_ali_disable_spdif_chnout(codec);
			}
		}
		break;
	case 2:
		change = (codec->spdif_mask & 0x01) ? 1 : 0;
		change = change ^ spdif_enable;
		if (change) {
			if (spdif_enable) {
				codec->spdif_mask |= 0x01;
				snd_ali_enable_spdif_in(codec);
			} else {
				codec->spdif_mask &= ~(0x01);
				snd_ali_disable_spdif_in(codec);
			}
		}
		break;
	default:
		break;
	}
	spin_unlock_irq(&codec->reg_lock);
	
	return change;
}

static const struct snd_kcontrol_new snd_ali5451_mixer_spdif[] = {
	/* spdif aplayback switch */
	/* FIXME: "IEC958 Playback Switch" may conflict with one on ac97_codec */
	ALI5451_SPDIF(SNDRV_CTL_NAME_IEC958("Output ",NONE,SWITCH), 0, 0),
	/* spdif out to spdif channel */
	ALI5451_SPDIF(SNDRV_CTL_NAME_IEC958("Channel Output ",NONE,SWITCH), 0, 1),
	/* spdif in from spdif channel */
	ALI5451_SPDIF(SNDRV_CTL_NAME_IEC958("",CAPTURE,SWITCH), 0, 2)
};

static int snd_ali_mixer(struct snd_ali *codec)
{
	struct snd_ac97_template ac97;
	unsigned int idx;
	int i, err;
	static const struct snd_ac97_bus_ops ops = {
		.write = snd_ali_codec_write,
		.read = snd_ali_codec_read,
	};

	err = snd_ac97_bus(codec->card, 0, &ops, codec, &codec->ac97_bus);
	if (err < 0)
		return err;

	memset(&ac97, 0, sizeof(ac97));
	ac97.private_data = codec;

	for (i = 0; i < codec->num_of_codecs; i++) {
		ac97.num = i;
		err = snd_ac97_mixer(codec->ac97_bus, &ac97, &codec->ac97[i]);
		if (err < 0) {
			dev_err(codec->card->dev,
				   "ali mixer %d creating error.\n", i);
			if (i == 0)
				return err;
			codec->num_of_codecs = 1;
			break;
		}
	}

	if (codec->spdif_support) {
		for (idx = 0; idx < ARRAY_SIZE(snd_ali5451_mixer_spdif); idx++) {
			err = snd_ctl_add(codec->card,
					  snd_ctl_new1(&snd_ali5451_mixer_spdif[idx], codec));
			if (err < 0)
				return err;
		}
	}
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int ali_suspend(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct snd_ali *chip = card->private_data;
	struct snd_ali_image *im;
	int i, j;

	im = chip->image;
	if (!im)
		return 0;

	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
	for (i = 0; i < chip->num_of_codecs; i++)
		snd_ac97_suspend(chip->ac97[i]);

	spin_lock_irq(&chip->reg_lock);
	
	im->regs[ALI_MISCINT >> 2] = inl(ALI_REG(chip, ALI_MISCINT));
	/* im->regs[ALI_START >> 2] = inl(ALI_REG(chip, ALI_START)); */
	im->regs[ALI_STOP >> 2] = inl(ALI_REG(chip, ALI_STOP));
	
	/* disable all IRQ bits */
	outl(0, ALI_REG(chip, ALI_MISCINT));
	
	for (i = 0; i < ALI_GLOBAL_REGS; i++) {	
		if ((i*4 == ALI_MISCINT) || (i*4 == ALI_STOP))
			continue;
		im->regs[i] = inl(ALI_REG(chip, i*4));
	}
	
	for (i = 0; i < ALI_CHANNELS; i++) {
		outb(i, ALI_REG(chip, ALI_GC_CIR));
		for (j = 0; j < ALI_CHANNEL_REGS; j++) 
			im->channel_regs[i][j] = inl(ALI_REG(chip, j*4 + 0xe0));
	}

	/* stop all HW channel */
	outl(0xffffffff, ALI_REG(chip, ALI_STOP));

	spin_unlock_irq(&chip->reg_lock);
	return 0;
}

static int ali_resume(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct snd_ali *chip = card->private_data;
	struct snd_ali_image *im;
	int i, j;

	im = chip->image;
	if (!im)
		return 0;

	spin_lock_irq(&chip->reg_lock);
	
	for (i = 0; i < ALI_CHANNELS; i++) {
		outb(i, ALI_REG(chip, ALI_GC_CIR));
		for (j = 0; j < ALI_CHANNEL_REGS; j++) 
			outl(im->channel_regs[i][j], ALI_REG(chip, j*4 + 0xe0));
	}
	
	for (i = 0; i < ALI_GLOBAL_REGS; i++) {	
		if ((i*4 == ALI_MISCINT) || (i*4 == ALI_STOP) ||
		    (i*4 == ALI_START))
			continue;
		outl(im->regs[i], ALI_REG(chip, i*4));
	}
	
	/* start HW channel */
	outl(im->regs[ALI_START >> 2], ALI_REG(chip, ALI_START));
	/* restore IRQ enable bits */
	outl(im->regs[ALI_MISCINT >> 2], ALI_REG(chip, ALI_MISCINT));
	
	spin_unlock_irq(&chip->reg_lock);

	for (i = 0 ; i < chip->num_of_codecs; i++)
		snd_ac97_resume(chip->ac97[i]);
	
	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	return 0;
}

static SIMPLE_DEV_PM_OPS(ali_pm, ali_suspend, ali_resume);
#define ALI_PM_OPS	&ali_pm
#else
#define ALI_PM_OPS	NULL
#endif /* CONFIG_PM_SLEEP */

static int snd_ali_free(struct snd_ali * codec)
{
	if (codec->hw_initialized)
		snd_ali_disable_address_interrupt(codec);
	if (codec->irq >= 0)
		free_irq(codec->irq, codec);
	if (codec->port)
		pci_release_regions(codec->pci);
	pci_disable_device(codec->pci);
#ifdef CONFIG_PM_SLEEP
	kfree(codec->image);
#endif
	pci_dev_put(codec->pci_m1533);
	pci_dev_put(codec->pci_m7101);
	kfree(codec);
	return 0;
}

static int snd_ali_chip_init(struct snd_ali *codec)
{
	unsigned int legacy;
	unsigned char temp;
	struct pci_dev *pci_dev;

	dev_dbg(codec->card->dev, "chip initializing ...\n");

	if (snd_ali_reset_5451(codec)) {
		dev_err(codec->card->dev, "ali_chip_init: reset 5451 error.\n");
		return -1;
	}

	if (codec->revision == ALI_5451_V02) {
        	pci_dev = codec->pci_m1533;
		pci_read_config_byte(pci_dev, 0x59, &temp);
		temp |= 0x80;
		pci_write_config_byte(pci_dev, 0x59, temp);
	
		pci_dev = codec->pci_m7101;
		pci_read_config_byte(pci_dev, 0xb8, &temp);
		temp |= 0x20;
		pci_write_config_byte(pci_dev, 0xB8, temp);
	}

	pci_read_config_dword(codec->pci, 0x44, &legacy);
	legacy &= 0xff00ff00;
	legacy |= 0x000800aa;
	pci_write_config_dword(codec->pci, 0x44, legacy);

	outl(0x80000001, ALI_REG(codec, ALI_GLOBAL_CONTROL));
	outl(0x00000000, ALI_REG(codec, ALI_AINTEN));
	outl(0xffffffff, ALI_REG(codec, ALI_AINT));
	outl(0x00000000, ALI_REG(codec, ALI_VOLUME));
	outb(0x10, 	 ALI_REG(codec, ALI_MPUR2));

	codec->ac97_ext_id = snd_ali_codec_peek(codec, 0, AC97_EXTENDED_ID);
	codec->ac97_ext_status = snd_ali_codec_peek(codec, 0,
						    AC97_EXTENDED_STATUS);
	if (codec->spdif_support) {
		snd_ali_enable_spdif_out(codec);
		codec->spdif_mask = 0x00000002;
	}

	codec->num_of_codecs = 1;

	/* secondary codec - modem */
	if (inl(ALI_REG(codec, ALI_SCTRL)) & ALI_SCTRL_CODEC2_READY) {
		codec->num_of_codecs++;
		outl(inl(ALI_REG(codec, ALI_SCTRL)) |
		     (ALI_SCTRL_LINE_IN2 | ALI_SCTRL_GPIO_IN2 |
		      ALI_SCTRL_LINE_OUT_EN),
		     ALI_REG(codec, ALI_SCTRL));
	}

	dev_dbg(codec->card->dev, "chip initialize succeed.\n");
	return 0;

}

/* proc for register dump */
static void snd_ali_proc_read(struct snd_info_entry *entry,
			      struct snd_info_buffer *buf)
{
	struct snd_ali *codec = entry->private_data;
	int i;
	for (i = 0; i < 256 ; i+= 4)
		snd_iprintf(buf, "%02x: %08x\n", i, inl(ALI_REG(codec, i)));
}

static void snd_ali_proc_init(struct snd_ali *codec)
{
	snd_card_ro_proc_new(codec->card, "ali5451", codec, snd_ali_proc_read);
}

static int snd_ali_resources(struct snd_ali *codec)
{
	int err;

	dev_dbg(codec->card->dev, "resources allocation ...\n");
	err = pci_request_regions(codec->pci, "ALI 5451");
	if (err < 0)
		return err;
	codec->port = pci_resource_start(codec->pci, 0);

	if (request_irq(codec->pci->irq, snd_ali_card_interrupt,
			IRQF_SHARED, KBUILD_MODNAME, codec)) {
		dev_err(codec->card->dev, "Unable to request irq.\n");
		return -EBUSY;
	}
	codec->irq = codec->pci->irq;
	codec->card->sync_irq = codec->irq;
	dev_dbg(codec->card->dev, "resources allocated.\n");
	return 0;
}
static int snd_ali_dev_free(struct snd_device *device)
{
	struct snd_ali *codec = device->device_data;
	snd_ali_free(codec);
	return 0;
}

static int snd_ali_create(struct snd_card *card,
			  struct pci_dev *pci,
			  int pcm_streams,
			  int spdif_support,
			  struct snd_ali **r_ali)
{
	struct snd_ali *codec;
	int i, err;
	unsigned short cmdw;
	static const struct snd_device_ops ops = {
		.dev_free = snd_ali_dev_free,
        };

	*r_ali = NULL;

	dev_dbg(card->dev, "creating ...\n");

	/* enable PCI device */
	err = pci_enable_device(pci);
	if (err < 0)
		return err;
	/* check, if we can restrict PCI DMA transfers to 31 bits */
	if (dma_set_mask_and_coherent(&pci->dev, DMA_BIT_MASK(31))) {
		dev_err(card->dev,
			"architecture does not support 31bit PCI busmaster DMA\n");
		pci_disable_device(pci);
		return -ENXIO;
	}

	codec = kzalloc(sizeof(*codec), GFP_KERNEL);
	if (!codec) {
		pci_disable_device(pci);
		return -ENOMEM;
	}

	spin_lock_init(&codec->reg_lock);
	spin_lock_init(&codec->voice_alloc);

	codec->card = card;
	codec->pci = pci;
	codec->irq = -1;
	codec->revision = pci->revision;
	codec->spdif_support = spdif_support;

	if (pcm_streams < 1)
		pcm_streams = 1;
	if (pcm_streams > 32)
		pcm_streams = 32;
	
	pci_set_master(pci);
	pci_read_config_word(pci, PCI_COMMAND, &cmdw);
	if ((cmdw & PCI_COMMAND_IO) != PCI_COMMAND_IO) {
		cmdw |= PCI_COMMAND_IO;
		pci_write_config_word(pci, PCI_COMMAND, cmdw);
	}
	pci_set_master(pci);
	
	if (snd_ali_resources(codec)) {
		snd_ali_free(codec);
		return -EBUSY;
	}

	codec->synth.chmap = 0;
	codec->synth.chcnt = 0;
	codec->spdif_mask = 0;
	codec->synth.synthcount = 0;

	if (codec->revision == ALI_5451_V02)
		codec->chregs.regs.ac97read = ALI_AC97_WRITE;
	else
		codec->chregs.regs.ac97read = ALI_AC97_READ;
	codec->chregs.regs.ac97write = ALI_AC97_WRITE;

	codec->chregs.regs.start  = ALI_START;
	codec->chregs.regs.stop   = ALI_STOP;
	codec->chregs.regs.aint   = ALI_AINT;
	codec->chregs.regs.ainten = ALI_AINTEN;

	codec->chregs.data.start  = 0x00;
	codec->chregs.data.stop   = 0x00;
	codec->chregs.data.aint   = 0x00;
	codec->chregs.data.ainten = 0x00;

	/* M1533: southbridge */
	codec->pci_m1533 = pci_get_device(0x10b9, 0x1533, NULL);
	if (!codec->pci_m1533) {
		dev_err(card->dev, "cannot find ALi 1533 chip.\n");
		snd_ali_free(codec);
		return -ENODEV;
	}
	/* M7101: power management */
	codec->pci_m7101 = pci_get_device(0x10b9, 0x7101, NULL);
	if (!codec->pci_m7101 && codec->revision == ALI_5451_V02) {
		dev_err(card->dev, "cannot find ALi 7101 chip.\n");
		snd_ali_free(codec);
		return -ENODEV;
	}

	dev_dbg(card->dev, "snd_device_new is called.\n");
	err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, codec, &ops);
	if (err < 0) {
		snd_ali_free(codec);
		return err;
	}

	/* initialise synth voices*/
	for (i = 0; i < ALI_CHANNELS; i++)
		codec->synth.voices[i].number = i;

	err = snd_ali_chip_init(codec);
	if (err < 0) {
		dev_err(card->dev, "ali create: chip init error.\n");
		return err;
	}

#ifdef CONFIG_PM_SLEEP
	codec->image = kmalloc(sizeof(*codec->image), GFP_KERNEL);
	if (!codec->image)
		dev_warn(card->dev, "can't allocate apm buffer\n");
#endif

	snd_ali_enable_address_interrupt(codec);
	codec->hw_initialized = 1;

	*r_ali = codec;
	dev_dbg(card->dev, "created.\n");
	return 0;
}

static int snd_ali_probe(struct pci_dev *pci,
			 const struct pci_device_id *pci_id)
{
	struct snd_card *card;
	struct snd_ali *codec;
	int err;

	dev_dbg(&pci->dev, "probe ...\n");

	err = snd_card_new(&pci->dev, index, id, THIS_MODULE, 0, &card);
	if (err < 0)
		return err;

	err = snd_ali_create(card, pci, pcm_channels, spdif, &codec);
	if (err < 0)
		goto error;
	card->private_data = codec;

	dev_dbg(&pci->dev, "mixer building ...\n");
	err = snd_ali_mixer(codec);
	if (err < 0)
		goto error;
	
	dev_dbg(&pci->dev, "pcm building ...\n");
	err = snd_ali_build_pcms(codec);
	if (err < 0)
		goto error;

	snd_ali_proc_init(codec);

	strcpy(card->driver, "ALI5451");
	strcpy(card->shortname, "ALI 5451");
	
	sprintf(card->longname, "%s at 0x%lx, irq %i",
		card->shortname, codec->port, codec->irq);

	dev_dbg(&pci->dev, "register card.\n");
	err = snd_card_register(card);
	if (err < 0)
		goto error;

	pci_set_drvdata(pci, card);
	return 0;

 error:
	snd_card_free(card);
	return err;
}

static void snd_ali_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
}

static struct pci_driver ali5451_driver = {
	.name = KBUILD_MODNAME,
	.id_table = snd_ali_ids,
	.probe = snd_ali_probe,
	.remove = snd_ali_remove,
	.driver = {
		.pm = ALI_PM_OPS,
	},
};                                

module_pci_driver(ali5451_driver);
