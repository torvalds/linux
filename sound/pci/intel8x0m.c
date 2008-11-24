/*
 *   ALSA modem driver for Intel ICH (i8x0) chipsets
 *
 *	Copyright (c) 2000 Jaroslav Kysela <perex@perex.cz>
 *
 *   This is modified (by Sasha Khapyorsky <sashak@alsa-project.org>) version
 *   of ALSA ICH sound driver intel8x0.c .
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */      

#include <asm/io.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/ac97_codec.h>
#include <sound/info.h>
#include <sound/initval.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@perex.cz>");
MODULE_DESCRIPTION("Intel 82801AA,82901AB,i810,i820,i830,i840,i845,MX440; "
		   "SiS 7013; NVidia MCP/2/2S/3 modems");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Intel,82801AA-ICH},"
		"{Intel,82901AB-ICH0},"
		"{Intel,82801BA-ICH2},"
		"{Intel,82801CA-ICH3},"
		"{Intel,82801DB-ICH4},"
		"{Intel,ICH5},"
		"{Intel,ICH6},"
		"{Intel,ICH7},"
	        "{Intel,MX440},"
		"{SiS,7013},"
		"{NVidia,NForce Modem},"
		"{NVidia,NForce2 Modem},"
		"{NVidia,NForce2s Modem},"
		"{NVidia,NForce3 Modem},"
		"{AMD,AMD768}}");

static int index = -2; /* Exclude the first card */
static char *id = SNDRV_DEFAULT_STR1;	/* ID for this card */
static int ac97_clock;

module_param(index, int, 0444);
MODULE_PARM_DESC(index, "Index value for Intel i8x0 modemcard.");
module_param(id, charp, 0444);
MODULE_PARM_DESC(id, "ID string for Intel i8x0 modemcard.");
module_param(ac97_clock, int, 0444);
MODULE_PARM_DESC(ac97_clock, "AC'97 codec clock (0 = auto-detect).");

/* just for backward compatibility */
static int enable;
module_param(enable, bool, 0444);

/*
 *  Direct registers
 */
enum { DEVICE_INTEL, DEVICE_SIS, DEVICE_ALI, DEVICE_NFORCE };

#define ICHREG(x) ICH_REG_##x

#define DEFINE_REGSET(name,base) \
enum { \
	ICH_REG_##name##_BDBAR	= base + 0x0,	/* dword - buffer descriptor list base address */ \
	ICH_REG_##name##_CIV	= base + 0x04,	/* byte - current index value */ \
	ICH_REG_##name##_LVI	= base + 0x05,	/* byte - last valid index */ \
	ICH_REG_##name##_SR	= base + 0x06,	/* byte - status register */ \
	ICH_REG_##name##_PICB	= base + 0x08,	/* word - position in current buffer */ \
	ICH_REG_##name##_PIV	= base + 0x0a,	/* byte - prefetched index value */ \
	ICH_REG_##name##_CR	= base + 0x0b,	/* byte - control register */ \
};

/* busmaster blocks */
DEFINE_REGSET(OFF, 0);		/* offset */

/* values for each busmaster block */

/* LVI */
#define ICH_REG_LVI_MASK		0x1f

/* SR */
#define ICH_FIFOE			0x10	/* FIFO error */
#define ICH_BCIS			0x08	/* buffer completion interrupt status */
#define ICH_LVBCI			0x04	/* last valid buffer completion interrupt */
#define ICH_CELV			0x02	/* current equals last valid */
#define ICH_DCH				0x01	/* DMA controller halted */

/* PIV */
#define ICH_REG_PIV_MASK		0x1f	/* mask */

/* CR */
#define ICH_IOCE			0x10	/* interrupt on completion enable */
#define ICH_FEIE			0x08	/* fifo error interrupt enable */
#define ICH_LVBIE			0x04	/* last valid buffer interrupt enable */
#define ICH_RESETREGS			0x02	/* reset busmaster registers */
#define ICH_STARTBM			0x01	/* start busmaster operation */


/* global block */
#define ICH_REG_GLOB_CNT		0x3c	/* dword - global control */
#define   ICH_TRIE		0x00000040	/* tertiary resume interrupt enable */
#define   ICH_SRIE		0x00000020	/* secondary resume interrupt enable */
#define   ICH_PRIE		0x00000010	/* primary resume interrupt enable */
#define   ICH_ACLINK		0x00000008	/* AClink shut off */
#define   ICH_AC97WARM		0x00000004	/* AC'97 warm reset */
#define   ICH_AC97COLD		0x00000002	/* AC'97 cold reset */
#define   ICH_GIE		0x00000001	/* GPI interrupt enable */
#define ICH_REG_GLOB_STA		0x40	/* dword - global status */
#define   ICH_TRI		0x20000000	/* ICH4: tertiary (AC_SDIN2) resume interrupt */
#define   ICH_TCR		0x10000000	/* ICH4: tertiary (AC_SDIN2) codec ready */
#define   ICH_BCS		0x08000000	/* ICH4: bit clock stopped */
#define   ICH_SPINT		0x04000000	/* ICH4: S/PDIF interrupt */
#define   ICH_P2INT		0x02000000	/* ICH4: PCM2-In interrupt */
#define   ICH_M2INT		0x01000000	/* ICH4: Mic2-In interrupt */
#define   ICH_SAMPLE_CAP	0x00c00000	/* ICH4: sample capability bits (RO) */
#define   ICH_MULTICHAN_CAP	0x00300000	/* ICH4: multi-channel capability bits (RO) */
#define   ICH_MD3		0x00020000	/* modem power down semaphore */
#define   ICH_AD3		0x00010000	/* audio power down semaphore */
#define   ICH_RCS		0x00008000	/* read completion status */
#define   ICH_BIT3		0x00004000	/* bit 3 slot 12 */
#define   ICH_BIT2		0x00002000	/* bit 2 slot 12 */
#define   ICH_BIT1		0x00001000	/* bit 1 slot 12 */
#define   ICH_SRI		0x00000800	/* secondary (AC_SDIN1) resume interrupt */
#define   ICH_PRI		0x00000400	/* primary (AC_SDIN0) resume interrupt */
#define   ICH_SCR		0x00000200	/* secondary (AC_SDIN1) codec ready */
#define   ICH_PCR		0x00000100	/* primary (AC_SDIN0) codec ready */
#define   ICH_MCINT		0x00000080	/* MIC capture interrupt */
#define   ICH_POINT		0x00000040	/* playback interrupt */
#define   ICH_PIINT		0x00000020	/* capture interrupt */
#define   ICH_NVSPINT		0x00000010	/* nforce spdif interrupt */
#define   ICH_MOINT		0x00000004	/* modem playback interrupt */
#define   ICH_MIINT		0x00000002	/* modem capture interrupt */
#define   ICH_GSCI		0x00000001	/* GPI status change interrupt */
#define ICH_REG_ACC_SEMA		0x44	/* byte - codec write semaphore */
#define   ICH_CAS		0x01		/* codec access semaphore */

#define ICH_MAX_FRAGS		32		/* max hw frags */


/*
 *  
 */

enum { ICHD_MDMIN, ICHD_MDMOUT, ICHD_MDMLAST = ICHD_MDMOUT };
enum { ALID_MDMIN, ALID_MDMOUT, ALID_MDMLAST = ALID_MDMOUT };

#define get_ichdev(substream) (substream->runtime->private_data)

struct ichdev {
	unsigned int ichd;			/* ich device number */
	unsigned long reg_offset;		/* offset to bmaddr */
	u32 *bdbar;				/* CPU address (32bit) */
	unsigned int bdbar_addr;		/* PCI bus address (32bit) */
	struct snd_pcm_substream *substream;
	unsigned int physbuf;			/* physical address (32bit) */
        unsigned int size;
        unsigned int fragsize;
        unsigned int fragsize1;
        unsigned int position;
        int frags;
        int lvi;
        int lvi_frag;
	int civ;
	int ack;
	int ack_reload;
	unsigned int ack_bit;
	unsigned int roff_sr;
	unsigned int roff_picb;
	unsigned int int_sta_mask;		/* interrupt status mask */
	unsigned int ali_slot;			/* ALI DMA slot */
	struct snd_ac97 *ac97;
};

struct intel8x0m {
	unsigned int device_type;

	int irq;

	void __iomem *addr;
	void __iomem *bmaddr;

	struct pci_dev *pci;
	struct snd_card *card;

	int pcm_devs;
	struct snd_pcm *pcm[2];
	struct ichdev ichd[2];

	unsigned int in_ac97_init: 1;

	struct snd_ac97_bus *ac97_bus;
	struct snd_ac97 *ac97;

	spinlock_t reg_lock;
	
	struct snd_dma_buffer bdbars;
	u32 bdbars_count;
	u32 int_sta_reg;		/* interrupt status register */
	u32 int_sta_mask;		/* interrupt status mask */
	unsigned int pcm_pos_shift;
};

static struct pci_device_id snd_intel8x0m_ids[] = {
	{ 0x8086, 0x2416, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_INTEL },	/* 82801AA */
	{ 0x8086, 0x2426, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_INTEL },	/* 82901AB */
	{ 0x8086, 0x2446, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_INTEL },	/* 82801BA */
	{ 0x8086, 0x2486, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_INTEL },	/* ICH3 */
	{ 0x8086, 0x24c6, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_INTEL }, /* ICH4 */
	{ 0x8086, 0x24d6, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_INTEL }, /* ICH5 */
	{ 0x8086, 0x266d, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_INTEL },	/* ICH6 */
	{ 0x8086, 0x27dd, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_INTEL },	/* ICH7 */
	{ 0x8086, 0x7196, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_INTEL },	/* 440MX */
	{ 0x1022, 0x7446, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_INTEL },	/* AMD768 */
	{ 0x1039, 0x7013, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_SIS },	/* SI7013 */
	{ 0x10de, 0x01c1, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_NFORCE }, /* NFORCE */
	{ 0x10de, 0x0069, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_NFORCE }, /* NFORCE2 */
	{ 0x10de, 0x0089, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_NFORCE }, /* NFORCE2s */
	{ 0x10de, 0x00d9, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_NFORCE }, /* NFORCE3 */
#if 0
	{ 0x1022, 0x746d, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_INTEL },	/* AMD8111 */
	{ 0x10b9, 0x5455, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_ALI },   /* Ali5455 */
#endif
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, snd_intel8x0m_ids);

/*
 *  Lowlevel I/O - busmaster
 */

static inline u8 igetbyte(struct intel8x0m *chip, u32 offset)
{
	return ioread8(chip->bmaddr + offset);
}

static inline u16 igetword(struct intel8x0m *chip, u32 offset)
{
	return ioread16(chip->bmaddr + offset);
}

static inline u32 igetdword(struct intel8x0m *chip, u32 offset)
{
	return ioread32(chip->bmaddr + offset);
}

static inline void iputbyte(struct intel8x0m *chip, u32 offset, u8 val)
{
	iowrite8(val, chip->bmaddr + offset);
}

static inline void iputword(struct intel8x0m *chip, u32 offset, u16 val)
{
	iowrite16(val, chip->bmaddr + offset);
}

static inline void iputdword(struct intel8x0m *chip, u32 offset, u32 val)
{
	iowrite32(val, chip->bmaddr + offset);
}

/*
 *  Lowlevel I/O - AC'97 registers
 */

static inline u16 iagetword(struct intel8x0m *chip, u32 offset)
{
	return ioread16(chip->addr + offset);
}

static inline void iaputword(struct intel8x0m *chip, u32 offset, u16 val)
{
	iowrite16(val, chip->addr + offset);
}

/*
 *  Basic I/O
 */

/*
 * access to AC97 codec via normal i/o (for ICH and SIS7013)
 */

/* return the GLOB_STA bit for the corresponding codec */
static unsigned int get_ich_codec_bit(struct intel8x0m *chip, unsigned int codec)
{
	static unsigned int codec_bit[3] = {
		ICH_PCR, ICH_SCR, ICH_TCR
	};
	if (snd_BUG_ON(codec >= 3))
		return ICH_PCR;
	return codec_bit[codec];
}

static int snd_intel8x0m_codec_semaphore(struct intel8x0m *chip, unsigned int codec)
{
	int time;
	
	if (codec > 1)
		return -EIO;
	codec = get_ich_codec_bit(chip, codec);

	/* codec ready ? */
	if ((igetdword(chip, ICHREG(GLOB_STA)) & codec) == 0)
		return -EIO;

	/* Anyone holding a semaphore for 1 msec should be shot... */
	time = 100;
      	do {
      		if (!(igetbyte(chip, ICHREG(ACC_SEMA)) & ICH_CAS))
      			return 0;
		udelay(10);
	} while (time--);

	/* access to some forbidden (non existant) ac97 registers will not
	 * reset the semaphore. So even if you don't get the semaphore, still
	 * continue the access. We don't need the semaphore anyway. */
	snd_printk(KERN_ERR "codec_semaphore: semaphore is not ready [0x%x][0x%x]\n",
			igetbyte(chip, ICHREG(ACC_SEMA)), igetdword(chip, ICHREG(GLOB_STA)));
	iagetword(chip, 0);	/* clear semaphore flag */
	/* I don't care about the semaphore */
	return -EBUSY;
}
 
static void snd_intel8x0_codec_write(struct snd_ac97 *ac97,
				     unsigned short reg,
				     unsigned short val)
{
	struct intel8x0m *chip = ac97->private_data;
	
	if (snd_intel8x0m_codec_semaphore(chip, ac97->num) < 0) {
		if (! chip->in_ac97_init)
			snd_printk(KERN_ERR "codec_write %d: semaphore is not ready for register 0x%x\n", ac97->num, reg);
	}
	iaputword(chip, reg + ac97->num * 0x80, val);
}

static unsigned short snd_intel8x0_codec_read(struct snd_ac97 *ac97,
					      unsigned short reg)
{
	struct intel8x0m *chip = ac97->private_data;
	unsigned short res;
	unsigned int tmp;

	if (snd_intel8x0m_codec_semaphore(chip, ac97->num) < 0) {
		if (! chip->in_ac97_init)
			snd_printk(KERN_ERR "codec_read %d: semaphore is not ready for register 0x%x\n", ac97->num, reg);
		res = 0xffff;
	} else {
		res = iagetword(chip, reg + ac97->num * 0x80);
		if ((tmp = igetdword(chip, ICHREG(GLOB_STA))) & ICH_RCS) {
			/* reset RCS and preserve other R/WC bits */
			iputdword(chip, ICHREG(GLOB_STA),
				  tmp & ~(ICH_SRI|ICH_PRI|ICH_TRI|ICH_GSCI));
			if (! chip->in_ac97_init)
				snd_printk(KERN_ERR "codec_read %d: read timeout for register 0x%x\n", ac97->num, reg);
			res = 0xffff;
		}
	}
	if (reg == AC97_GPIO_STATUS)
		iagetword(chip, 0); /* clear semaphore */
	return res;
}


/*
 * DMA I/O
 */
static void snd_intel8x0_setup_periods(struct intel8x0m *chip, struct ichdev *ichdev) 
{
	int idx;
	u32 *bdbar = ichdev->bdbar;
	unsigned long port = ichdev->reg_offset;

	iputdword(chip, port + ICH_REG_OFF_BDBAR, ichdev->bdbar_addr);
	if (ichdev->size == ichdev->fragsize) {
		ichdev->ack_reload = ichdev->ack = 2;
		ichdev->fragsize1 = ichdev->fragsize >> 1;
		for (idx = 0; idx < (ICH_REG_LVI_MASK + 1) * 2; idx += 4) {
			bdbar[idx + 0] = cpu_to_le32(ichdev->physbuf);
			bdbar[idx + 1] = cpu_to_le32(0x80000000 | /* interrupt on completion */
						     ichdev->fragsize1 >> chip->pcm_pos_shift);
			bdbar[idx + 2] = cpu_to_le32(ichdev->physbuf + (ichdev->size >> 1));
			bdbar[idx + 3] = cpu_to_le32(0x80000000 | /* interrupt on completion */
						     ichdev->fragsize1 >> chip->pcm_pos_shift);
		}
		ichdev->frags = 2;
	} else {
		ichdev->ack_reload = ichdev->ack = 1;
		ichdev->fragsize1 = ichdev->fragsize;
		for (idx = 0; idx < (ICH_REG_LVI_MASK + 1) * 2; idx += 2) {
			bdbar[idx + 0] = cpu_to_le32(ichdev->physbuf + (((idx >> 1) * ichdev->fragsize) % ichdev->size));
			bdbar[idx + 1] = cpu_to_le32(0x80000000 | /* interrupt on completion */
						     ichdev->fragsize >> chip->pcm_pos_shift);
			// printk("bdbar[%i] = 0x%x [0x%x]\n", idx + 0, bdbar[idx + 0], bdbar[idx + 1]);
		}
		ichdev->frags = ichdev->size / ichdev->fragsize;
	}
	iputbyte(chip, port + ICH_REG_OFF_LVI, ichdev->lvi = ICH_REG_LVI_MASK);
	ichdev->civ = 0;
	iputbyte(chip, port + ICH_REG_OFF_CIV, 0);
	ichdev->lvi_frag = ICH_REG_LVI_MASK % ichdev->frags;
	ichdev->position = 0;
#if 0
	printk("lvi_frag = %i, frags = %i, period_size = 0x%x, period_size1 = 0x%x\n",
			ichdev->lvi_frag, ichdev->frags, ichdev->fragsize, ichdev->fragsize1);
#endif
	/* clear interrupts */
	iputbyte(chip, port + ichdev->roff_sr, ICH_FIFOE | ICH_BCIS | ICH_LVBCI);
}

/*
 *  Interrupt handler
 */

static inline void snd_intel8x0_update(struct intel8x0m *chip, struct ichdev *ichdev)
{
	unsigned long port = ichdev->reg_offset;
	int civ, i, step;
	int ack = 0;

	civ = igetbyte(chip, port + ICH_REG_OFF_CIV);
	if (civ == ichdev->civ) {
		// snd_printd("civ same %d\n", civ);
		step = 1;
		ichdev->civ++;
		ichdev->civ &= ICH_REG_LVI_MASK;
	} else {
		step = civ - ichdev->civ;
		if (step < 0)
			step += ICH_REG_LVI_MASK + 1;
		// if (step != 1)
		//	snd_printd("step = %d, %d -> %d\n", step, ichdev->civ, civ);
		ichdev->civ = civ;
	}

	ichdev->position += step * ichdev->fragsize1;
	ichdev->position %= ichdev->size;
	ichdev->lvi += step;
	ichdev->lvi &= ICH_REG_LVI_MASK;
	iputbyte(chip, port + ICH_REG_OFF_LVI, ichdev->lvi);
	for (i = 0; i < step; i++) {
		ichdev->lvi_frag++;
		ichdev->lvi_frag %= ichdev->frags;
		ichdev->bdbar[ichdev->lvi * 2] = cpu_to_le32(ichdev->physbuf +
							     ichdev->lvi_frag *
							     ichdev->fragsize1);
#if 0
		printk("new: bdbar[%i] = 0x%x [0x%x], prefetch = %i, all = 0x%x, 0x%x\n",
		       ichdev->lvi * 2, ichdev->bdbar[ichdev->lvi * 2],
		       ichdev->bdbar[ichdev->lvi * 2 + 1], inb(ICH_REG_OFF_PIV + port),
		       inl(port + 4), inb(port + ICH_REG_OFF_CR));
#endif
		if (--ichdev->ack == 0) {
			ichdev->ack = ichdev->ack_reload;
			ack = 1;
		}
	}
	if (ack && ichdev->substream) {
		spin_unlock(&chip->reg_lock);
		snd_pcm_period_elapsed(ichdev->substream);
		spin_lock(&chip->reg_lock);
	}
	iputbyte(chip, port + ichdev->roff_sr, ICH_FIFOE | ICH_BCIS | ICH_LVBCI);
}

static irqreturn_t snd_intel8x0_interrupt(int irq, void *dev_id)
{
	struct intel8x0m *chip = dev_id;
	struct ichdev *ichdev;
	unsigned int status;
	unsigned int i;

	spin_lock(&chip->reg_lock);
	status = igetdword(chip, chip->int_sta_reg);
	if (status == 0xffffffff) { /* we are not yet resumed */
		spin_unlock(&chip->reg_lock);
		return IRQ_NONE;
	}
	if ((status & chip->int_sta_mask) == 0) {
		if (status)
			iputdword(chip, chip->int_sta_reg, status);
		spin_unlock(&chip->reg_lock);
		return IRQ_NONE;
	}

	for (i = 0; i < chip->bdbars_count; i++) {
		ichdev = &chip->ichd[i];
		if (status & ichdev->int_sta_mask)
			snd_intel8x0_update(chip, ichdev);
	}

	/* ack them */
	iputdword(chip, chip->int_sta_reg, status & chip->int_sta_mask);
	spin_unlock(&chip->reg_lock);
	
	return IRQ_HANDLED;
}

/*
 *  PCM part
 */

static int snd_intel8x0_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct intel8x0m *chip = snd_pcm_substream_chip(substream);
	struct ichdev *ichdev = get_ichdev(substream);
	unsigned char val = 0;
	unsigned long port = ichdev->reg_offset;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		val = ICH_IOCE | ICH_STARTBM;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		val = 0;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		val = ICH_IOCE;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		val = ICH_IOCE | ICH_STARTBM;
		break;
	default:
		return -EINVAL;
	}
	iputbyte(chip, port + ICH_REG_OFF_CR, val);
	if (cmd == SNDRV_PCM_TRIGGER_STOP) {
		/* wait until DMA stopped */
		while (!(igetbyte(chip, port + ichdev->roff_sr) & ICH_DCH)) ;
		/* reset whole DMA things */
		iputbyte(chip, port + ICH_REG_OFF_CR, ICH_RESETREGS);
	}
	return 0;
}

static int snd_intel8x0_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *hw_params)
{
	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
}

static int snd_intel8x0_hw_free(struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_pages(substream);
}

static snd_pcm_uframes_t snd_intel8x0_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct intel8x0m *chip = snd_pcm_substream_chip(substream);
	struct ichdev *ichdev = get_ichdev(substream);
	size_t ptr1, ptr;

	ptr1 = igetword(chip, ichdev->reg_offset + ichdev->roff_picb) << chip->pcm_pos_shift;
	if (ptr1 != 0)
		ptr = ichdev->fragsize1 - ptr1;
	else
		ptr = 0;
	ptr += ichdev->position;
	if (ptr >= ichdev->size)
		return 0;
	return bytes_to_frames(substream->runtime, ptr);
}

static int snd_intel8x0m_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct intel8x0m *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct ichdev *ichdev = get_ichdev(substream);

	ichdev->physbuf = runtime->dma_addr;
	ichdev->size = snd_pcm_lib_buffer_bytes(substream);
	ichdev->fragsize = snd_pcm_lib_period_bytes(substream);
	snd_ac97_write(ichdev->ac97, AC97_LINE1_RATE, runtime->rate);
	snd_ac97_write(ichdev->ac97, AC97_LINE1_LEVEL, 0);
	snd_intel8x0_setup_periods(chip, ichdev);
	return 0;
}

static struct snd_pcm_hardware snd_intel8x0m_stream =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_PAUSE |
				 SNDRV_PCM_INFO_RESUME),
	.formats =		SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_KNOT,
	.rate_min =		8000,
	.rate_max =		16000,
	.channels_min =		1,
	.channels_max =		1,
	.buffer_bytes_max =	64 * 1024,
	.period_bytes_min =	32,
	.period_bytes_max =	64 * 1024,
	.periods_min =		1,
	.periods_max =		1024,
	.fifo_size =		0,
};


static int snd_intel8x0m_pcm_open(struct snd_pcm_substream *substream, struct ichdev *ichdev)
{
	static unsigned int rates[] = { 8000,  9600, 12000, 16000 };
	static struct snd_pcm_hw_constraint_list hw_constraints_rates = {
		.count = ARRAY_SIZE(rates),
		.list = rates,
		.mask = 0,
	};
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;

	ichdev->substream = substream;
	runtime->hw = snd_intel8x0m_stream;
	err = snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
					 &hw_constraints_rates);
	if ( err < 0 )
		return err;
	runtime->private_data = ichdev;
	return 0;
}

static int snd_intel8x0m_playback_open(struct snd_pcm_substream *substream)
{
	struct intel8x0m *chip = snd_pcm_substream_chip(substream);

	return snd_intel8x0m_pcm_open(substream, &chip->ichd[ICHD_MDMOUT]);
}

static int snd_intel8x0m_playback_close(struct snd_pcm_substream *substream)
{
	struct intel8x0m *chip = snd_pcm_substream_chip(substream);

	chip->ichd[ICHD_MDMOUT].substream = NULL;
	return 0;
}

static int snd_intel8x0m_capture_open(struct snd_pcm_substream *substream)
{
	struct intel8x0m *chip = snd_pcm_substream_chip(substream);

	return snd_intel8x0m_pcm_open(substream, &chip->ichd[ICHD_MDMIN]);
}

static int snd_intel8x0m_capture_close(struct snd_pcm_substream *substream)
{
	struct intel8x0m *chip = snd_pcm_substream_chip(substream);

	chip->ichd[ICHD_MDMIN].substream = NULL;
	return 0;
}


static struct snd_pcm_ops snd_intel8x0m_playback_ops = {
	.open =		snd_intel8x0m_playback_open,
	.close =	snd_intel8x0m_playback_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_intel8x0_hw_params,
	.hw_free =	snd_intel8x0_hw_free,
	.prepare =	snd_intel8x0m_pcm_prepare,
	.trigger =	snd_intel8x0_pcm_trigger,
	.pointer =	snd_intel8x0_pcm_pointer,
};

static struct snd_pcm_ops snd_intel8x0m_capture_ops = {
	.open =		snd_intel8x0m_capture_open,
	.close =	snd_intel8x0m_capture_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_intel8x0_hw_params,
	.hw_free =	snd_intel8x0_hw_free,
	.prepare =	snd_intel8x0m_pcm_prepare,
	.trigger =	snd_intel8x0_pcm_trigger,
	.pointer =	snd_intel8x0_pcm_pointer,
};


struct ich_pcm_table {
	char *suffix;
	struct snd_pcm_ops *playback_ops;
	struct snd_pcm_ops *capture_ops;
	size_t prealloc_size;
	size_t prealloc_max_size;
	int ac97_idx;
};

static int __devinit snd_intel8x0_pcm1(struct intel8x0m *chip, int device,
				       struct ich_pcm_table *rec)
{
	struct snd_pcm *pcm;
	int err;
	char name[32];

	if (rec->suffix)
		sprintf(name, "Intel ICH - %s", rec->suffix);
	else
		strcpy(name, "Intel ICH");
	err = snd_pcm_new(chip->card, name, device,
			  rec->playback_ops ? 1 : 0,
			  rec->capture_ops ? 1 : 0, &pcm);
	if (err < 0)
		return err;

	if (rec->playback_ops)
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, rec->playback_ops);
	if (rec->capture_ops)
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, rec->capture_ops);

	pcm->private_data = chip;
	pcm->info_flags = 0;
	pcm->dev_class = SNDRV_PCM_CLASS_MODEM;
	if (rec->suffix)
		sprintf(pcm->name, "%s - %s", chip->card->shortname, rec->suffix);
	else
		strcpy(pcm->name, chip->card->shortname);
	chip->pcm[device] = pcm;

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
					      snd_dma_pci_data(chip->pci),
					      rec->prealloc_size,
					      rec->prealloc_max_size);

	return 0;
}

static struct ich_pcm_table intel_pcms[] __devinitdata = {
	{
		.suffix = "Modem",
		.playback_ops = &snd_intel8x0m_playback_ops,
		.capture_ops = &snd_intel8x0m_capture_ops,
		.prealloc_size = 32 * 1024,
		.prealloc_max_size = 64 * 1024,
	},
};

static int __devinit snd_intel8x0_pcm(struct intel8x0m *chip)
{
	int i, tblsize, device, err;
	struct ich_pcm_table *tbl, *rec;

#if 1
	tbl = intel_pcms;
	tblsize = 1;
#else
	switch (chip->device_type) {
	case DEVICE_NFORCE:
		tbl = nforce_pcms;
		tblsize = ARRAY_SIZE(nforce_pcms);
		break;
	case DEVICE_ALI:
		tbl = ali_pcms;
		tblsize = ARRAY_SIZE(ali_pcms);
		break;
	default:
		tbl = intel_pcms;
		tblsize = 2;
		break;
	}
#endif
	device = 0;
	for (i = 0; i < tblsize; i++) {
		rec = tbl + i;
		if (i > 0 && rec->ac97_idx) {
			/* activate PCM only when associated AC'97 codec */
			if (! chip->ichd[rec->ac97_idx].ac97)
				continue;
		}
		err = snd_intel8x0_pcm1(chip, device, rec);
		if (err < 0)
			return err;
		device++;
	}

	chip->pcm_devs = device;
	return 0;
}
	

/*
 *  Mixer part
 */

static void snd_intel8x0_mixer_free_ac97_bus(struct snd_ac97_bus *bus)
{
	struct intel8x0m *chip = bus->private_data;
	chip->ac97_bus = NULL;
}

static void snd_intel8x0_mixer_free_ac97(struct snd_ac97 *ac97)
{
	struct intel8x0m *chip = ac97->private_data;
	chip->ac97 = NULL;
}


static int __devinit snd_intel8x0_mixer(struct intel8x0m *chip, int ac97_clock)
{
	struct snd_ac97_bus *pbus;
	struct snd_ac97_template ac97;
	struct snd_ac97 *x97;
	int err;
	unsigned int glob_sta = 0;
	static struct snd_ac97_bus_ops ops = {
		.write = snd_intel8x0_codec_write,
		.read = snd_intel8x0_codec_read,
	};

	chip->in_ac97_init = 1;
	
	memset(&ac97, 0, sizeof(ac97));
	ac97.private_data = chip;
	ac97.private_free = snd_intel8x0_mixer_free_ac97;
	ac97.scaps = AC97_SCAP_SKIP_AUDIO | AC97_SCAP_POWER_SAVE;

	glob_sta = igetdword(chip, ICHREG(GLOB_STA));

	if ((err = snd_ac97_bus(chip->card, 0, &ops, chip, &pbus)) < 0)
		goto __err;
	pbus->private_free = snd_intel8x0_mixer_free_ac97_bus;
	if (ac97_clock >= 8000 && ac97_clock <= 48000)
		pbus->clock = ac97_clock;
	chip->ac97_bus = pbus;

	ac97.pci = chip->pci;
	ac97.num = glob_sta & ICH_SCR ? 1 : 0;
	if ((err = snd_ac97_mixer(pbus, &ac97, &x97)) < 0) {
		snd_printk(KERN_ERR "Unable to initialize codec #%d\n", ac97.num);
		if (ac97.num == 0)
			goto __err;
		return err;
	}
	chip->ac97 = x97;
	if(ac97_is_modem(x97) && !chip->ichd[ICHD_MDMIN].ac97) {
		chip->ichd[ICHD_MDMIN].ac97 = x97;
		chip->ichd[ICHD_MDMOUT].ac97 = x97;
	}

	chip->in_ac97_init = 0;
	return 0;

 __err:
	/* clear the cold-reset bit for the next chance */
	if (chip->device_type != DEVICE_ALI)
		iputdword(chip, ICHREG(GLOB_CNT),
			  igetdword(chip, ICHREG(GLOB_CNT)) & ~ICH_AC97COLD);
	return err;
}


/*
 *
 */

static int snd_intel8x0m_ich_chip_init(struct intel8x0m *chip, int probing)
{
	unsigned long end_time;
	unsigned int cnt, status, nstatus;
	
	/* put logic to right state */
	/* first clear status bits */
	status = ICH_RCS | ICH_MIINT | ICH_MOINT;
	cnt = igetdword(chip, ICHREG(GLOB_STA));
	iputdword(chip, ICHREG(GLOB_STA), cnt & status);

	/* ACLink on, 2 channels */
	cnt = igetdword(chip, ICHREG(GLOB_CNT));
	cnt &= ~(ICH_ACLINK);
	/* finish cold or do warm reset */
	cnt |= (cnt & ICH_AC97COLD) == 0 ? ICH_AC97COLD : ICH_AC97WARM;
	iputdword(chip, ICHREG(GLOB_CNT), cnt);
	end_time = (jiffies + (HZ / 4)) + 1;
	do {
		if ((igetdword(chip, ICHREG(GLOB_CNT)) & ICH_AC97WARM) == 0)
			goto __ok;
		schedule_timeout_uninterruptible(1);
	} while (time_after_eq(end_time, jiffies));
	snd_printk(KERN_ERR "AC'97 warm reset still in progress? [0x%x]\n",
		   igetdword(chip, ICHREG(GLOB_CNT)));
	return -EIO;

      __ok:
	if (probing) {
		/* wait for any codec ready status.
		 * Once it becomes ready it should remain ready
		 * as long as we do not disable the ac97 link.
		 */
		end_time = jiffies + HZ;
		do {
			status = igetdword(chip, ICHREG(GLOB_STA)) &
				(ICH_PCR | ICH_SCR | ICH_TCR);
			if (status)
				break;
			schedule_timeout_uninterruptible(1);
		} while (time_after_eq(end_time, jiffies));
		if (! status) {
			/* no codec is found */
			snd_printk(KERN_ERR "codec_ready: codec is not ready [0x%x]\n",
				   igetdword(chip, ICHREG(GLOB_STA)));
			return -EIO;
		}

		/* up to two codecs (modem cannot be tertiary with ICH4) */
		nstatus = ICH_PCR | ICH_SCR;

		/* wait for other codecs ready status. */
		end_time = jiffies + HZ / 4;
		while (status != nstatus && time_after_eq(end_time, jiffies)) {
			schedule_timeout_uninterruptible(1);
			status |= igetdword(chip, ICHREG(GLOB_STA)) & nstatus;
		}

	} else {
		/* resume phase */
		status = 0;
		if (chip->ac97)
			status |= get_ich_codec_bit(chip, chip->ac97->num);
		/* wait until all the probed codecs are ready */
		end_time = jiffies + HZ;
		do {
			nstatus = igetdword(chip, ICHREG(GLOB_STA)) &
				(ICH_PCR | ICH_SCR | ICH_TCR);
			if (status == nstatus)
				break;
			schedule_timeout_uninterruptible(1);
		} while (time_after_eq(end_time, jiffies));
	}

	if (chip->device_type == DEVICE_SIS) {
		/* unmute the output on SIS7012 */
		iputword(chip, 0x4c, igetword(chip, 0x4c) | 1);
	}

      	return 0;
}

static int snd_intel8x0_chip_init(struct intel8x0m *chip, int probing)
{
	unsigned int i;
	int err;
	
	if ((err = snd_intel8x0m_ich_chip_init(chip, probing)) < 0)
		return err;
	iagetword(chip, 0);	/* clear semaphore flag */

	/* disable interrupts */
	for (i = 0; i < chip->bdbars_count; i++)
		iputbyte(chip, ICH_REG_OFF_CR + chip->ichd[i].reg_offset, 0x00);
	/* reset channels */
	for (i = 0; i < chip->bdbars_count; i++)
		iputbyte(chip, ICH_REG_OFF_CR + chip->ichd[i].reg_offset, ICH_RESETREGS);
	/* initialize Buffer Descriptor Lists */
	for (i = 0; i < chip->bdbars_count; i++)
		iputdword(chip, ICH_REG_OFF_BDBAR + chip->ichd[i].reg_offset, chip->ichd[i].bdbar_addr);
	return 0;
}

static int snd_intel8x0_free(struct intel8x0m *chip)
{
	unsigned int i;

	if (chip->irq < 0)
		goto __hw_end;
	/* disable interrupts */
	for (i = 0; i < chip->bdbars_count; i++)
		iputbyte(chip, ICH_REG_OFF_CR + chip->ichd[i].reg_offset, 0x00);
	/* reset channels */
	for (i = 0; i < chip->bdbars_count; i++)
		iputbyte(chip, ICH_REG_OFF_CR + chip->ichd[i].reg_offset, ICH_RESETREGS);
 __hw_end:
	if (chip->irq >= 0)
		free_irq(chip->irq, chip);
	if (chip->bdbars.area)
		snd_dma_free_pages(&chip->bdbars);
	if (chip->addr)
		pci_iounmap(chip->pci, chip->addr);
	if (chip->bmaddr)
		pci_iounmap(chip->pci, chip->bmaddr);
	pci_release_regions(chip->pci);
	pci_disable_device(chip->pci);
	kfree(chip);
	return 0;
}

#ifdef CONFIG_PM
/*
 * power management
 */
static int intel8x0m_suspend(struct pci_dev *pci, pm_message_t state)
{
	struct snd_card *card = pci_get_drvdata(pci);
	struct intel8x0m *chip = card->private_data;
	int i;

	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
	for (i = 0; i < chip->pcm_devs; i++)
		snd_pcm_suspend_all(chip->pcm[i]);
	snd_ac97_suspend(chip->ac97);
	if (chip->irq >= 0) {
		free_irq(chip->irq, chip);
		chip->irq = -1;
	}
	pci_disable_device(pci);
	pci_save_state(pci);
	pci_set_power_state(pci, pci_choose_state(pci, state));
	return 0;
}

static int intel8x0m_resume(struct pci_dev *pci)
{
	struct snd_card *card = pci_get_drvdata(pci);
	struct intel8x0m *chip = card->private_data;

	pci_set_power_state(pci, PCI_D0);
	pci_restore_state(pci);
	if (pci_enable_device(pci) < 0) {
		printk(KERN_ERR "intel8x0m: pci_enable_device failed, "
		       "disabling device\n");
		snd_card_disconnect(card);
		return -EIO;
	}
	pci_set_master(pci);
	if (request_irq(pci->irq, snd_intel8x0_interrupt,
			IRQF_SHARED, card->shortname, chip)) {
		printk(KERN_ERR "intel8x0m: unable to grab IRQ %d, "
		       "disabling device\n", pci->irq);
		snd_card_disconnect(card);
		return -EIO;
	}
	chip->irq = pci->irq;
	snd_intel8x0_chip_init(chip, 0);
	snd_ac97_resume(chip->ac97);

	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	return 0;
}
#endif /* CONFIG_PM */

#ifdef CONFIG_PROC_FS
static void snd_intel8x0m_proc_read(struct snd_info_entry * entry,
				   struct snd_info_buffer *buffer)
{
	struct intel8x0m *chip = entry->private_data;
	unsigned int tmp;

	snd_iprintf(buffer, "Intel8x0m\n\n");
	if (chip->device_type == DEVICE_ALI)
		return;
	tmp = igetdword(chip, ICHREG(GLOB_STA));
	snd_iprintf(buffer, "Global control        : 0x%08x\n",
		    igetdword(chip, ICHREG(GLOB_CNT)));
	snd_iprintf(buffer, "Global status         : 0x%08x\n", tmp);
	snd_iprintf(buffer, "AC'97 codecs ready    :%s%s%s%s\n",
			tmp & ICH_PCR ? " primary" : "",
			tmp & ICH_SCR ? " secondary" : "",
			tmp & ICH_TCR ? " tertiary" : "",
			(tmp & (ICH_PCR | ICH_SCR | ICH_TCR)) == 0 ? " none" : "");
}

static void __devinit snd_intel8x0m_proc_init(struct intel8x0m * chip)
{
	struct snd_info_entry *entry;

	if (! snd_card_proc_new(chip->card, "intel8x0m", &entry))
		snd_info_set_text_ops(entry, chip, snd_intel8x0m_proc_read);
}
#else /* !CONFIG_PROC_FS */
#define snd_intel8x0m_proc_init(chip)
#endif /* CONFIG_PROC_FS */


static int snd_intel8x0_dev_free(struct snd_device *device)
{
	struct intel8x0m *chip = device->device_data;
	return snd_intel8x0_free(chip);
}

struct ich_reg_info {
	unsigned int int_sta_mask;
	unsigned int offset;
};

static int __devinit snd_intel8x0m_create(struct snd_card *card,
					 struct pci_dev *pci,
					 unsigned long device_type,
					 struct intel8x0m ** r_intel8x0)
{
	struct intel8x0m *chip;
	int err;
	unsigned int i;
	unsigned int int_sta_masks;
	struct ichdev *ichdev;
	static struct snd_device_ops ops = {
		.dev_free =	snd_intel8x0_dev_free,
	};
	static struct ich_reg_info intel_regs[2] = {
		{ ICH_MIINT, 0 },
		{ ICH_MOINT, 0x10 },
	};
	struct ich_reg_info *tbl;

	*r_intel8x0 = NULL;

	if ((err = pci_enable_device(pci)) < 0)
		return err;

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (chip == NULL) {
		pci_disable_device(pci);
		return -ENOMEM;
	}
	spin_lock_init(&chip->reg_lock);
	chip->device_type = device_type;
	chip->card = card;
	chip->pci = pci;
	chip->irq = -1;

	if ((err = pci_request_regions(pci, card->shortname)) < 0) {
		kfree(chip);
		pci_disable_device(pci);
		return err;
	}

	if (device_type == DEVICE_ALI) {
		/* ALI5455 has no ac97 region */
		chip->bmaddr = pci_iomap(pci, 0, 0);
		goto port_inited;
	}

	if (pci_resource_flags(pci, 2) & IORESOURCE_MEM) /* ICH4 and Nforce */
		chip->addr = pci_iomap(pci, 2, 0);
	else
		chip->addr = pci_iomap(pci, 0, 0);
	if (!chip->addr) {
		snd_printk(KERN_ERR "AC'97 space ioremap problem\n");
		snd_intel8x0_free(chip);
		return -EIO;
	}
	if (pci_resource_flags(pci, 3) & IORESOURCE_MEM) /* ICH4 */
		chip->bmaddr = pci_iomap(pci, 3, 0);
	else
		chip->bmaddr = pci_iomap(pci, 1, 0);
	if (!chip->bmaddr) {
		snd_printk(KERN_ERR "Controller space ioremap problem\n");
		snd_intel8x0_free(chip);
		return -EIO;
	}

 port_inited:
	if (request_irq(pci->irq, snd_intel8x0_interrupt, IRQF_SHARED,
			card->shortname, chip)) {
		snd_printk(KERN_ERR "unable to grab IRQ %d\n", pci->irq);
		snd_intel8x0_free(chip);
		return -EBUSY;
	}
	chip->irq = pci->irq;
	pci_set_master(pci);
	synchronize_irq(chip->irq);

	/* initialize offsets */
	chip->bdbars_count = 2;
	tbl = intel_regs;

	for (i = 0; i < chip->bdbars_count; i++) {
		ichdev = &chip->ichd[i];
		ichdev->ichd = i;
		ichdev->reg_offset = tbl[i].offset;
		ichdev->int_sta_mask = tbl[i].int_sta_mask;
		if (device_type == DEVICE_SIS) {
			/* SiS 7013 swaps the registers */
			ichdev->roff_sr = ICH_REG_OFF_PICB;
			ichdev->roff_picb = ICH_REG_OFF_SR;
		} else {
			ichdev->roff_sr = ICH_REG_OFF_SR;
			ichdev->roff_picb = ICH_REG_OFF_PICB;
		}
		if (device_type == DEVICE_ALI)
			ichdev->ali_slot = (ichdev->reg_offset - 0x40) / 0x10;
	}
	/* SIS7013 handles the pcm data in bytes, others are in words */
	chip->pcm_pos_shift = (device_type == DEVICE_SIS) ? 0 : 1;

	/* allocate buffer descriptor lists */
	/* the start of each lists must be aligned to 8 bytes */
	if (snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, snd_dma_pci_data(pci),
				chip->bdbars_count * sizeof(u32) * ICH_MAX_FRAGS * 2,
				&chip->bdbars) < 0) {
		snd_intel8x0_free(chip);
		return -ENOMEM;
	}
	/* tables must be aligned to 8 bytes here, but the kernel pages
	   are much bigger, so we don't care (on i386) */
	int_sta_masks = 0;
	for (i = 0; i < chip->bdbars_count; i++) {
		ichdev = &chip->ichd[i];
		ichdev->bdbar = ((u32 *)chip->bdbars.area) + (i * ICH_MAX_FRAGS * 2);
		ichdev->bdbar_addr = chip->bdbars.addr + (i * sizeof(u32) * ICH_MAX_FRAGS * 2);
		int_sta_masks |= ichdev->int_sta_mask;
	}
	chip->int_sta_reg = ICH_REG_GLOB_STA;
	chip->int_sta_mask = int_sta_masks;

	if ((err = snd_intel8x0_chip_init(chip, 1)) < 0) {
		snd_intel8x0_free(chip);
		return err;
	}

	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops)) < 0) {
		snd_intel8x0_free(chip);
		return err;
	}

	snd_card_set_dev(card, &pci->dev);

	*r_intel8x0 = chip;
	return 0;
}

static struct shortname_table {
	unsigned int id;
	const char *s;
} shortnames[] __devinitdata = {
	{ PCI_DEVICE_ID_INTEL_82801AA_6, "Intel 82801AA-ICH" },
	{ PCI_DEVICE_ID_INTEL_82801AB_6, "Intel 82901AB-ICH0" },
	{ PCI_DEVICE_ID_INTEL_82801BA_6, "Intel 82801BA-ICH2" },
	{ PCI_DEVICE_ID_INTEL_440MX_6, "Intel 440MX" },
	{ PCI_DEVICE_ID_INTEL_82801CA_6, "Intel 82801CA-ICH3" },
	{ PCI_DEVICE_ID_INTEL_82801DB_6, "Intel 82801DB-ICH4" },
	{ PCI_DEVICE_ID_INTEL_82801EB_6, "Intel ICH5" },
	{ PCI_DEVICE_ID_INTEL_ICH6_17, "Intel ICH6" },
	{ PCI_DEVICE_ID_INTEL_ICH7_19, "Intel ICH7" },
	{ 0x7446, "AMD AMD768" },
	{ PCI_DEVICE_ID_SI_7013, "SiS SI7013" },
	{ PCI_DEVICE_ID_NVIDIA_MCP1_MODEM, "NVidia nForce" },
	{ PCI_DEVICE_ID_NVIDIA_MCP2_MODEM, "NVidia nForce2" },
	{ PCI_DEVICE_ID_NVIDIA_MCP2S_MODEM, "NVidia nForce2s" },
	{ PCI_DEVICE_ID_NVIDIA_MCP3_MODEM, "NVidia nForce3" },
#if 0
	{ 0x5455, "ALi M5455" },
	{ 0x746d, "AMD AMD8111" },
#endif
	{ 0 },
};

static int __devinit snd_intel8x0m_probe(struct pci_dev *pci,
					const struct pci_device_id *pci_id)
{
	struct snd_card *card;
	struct intel8x0m *chip;
	int err;
	struct shortname_table *name;

	card = snd_card_new(index, id, THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;

	strcpy(card->driver, "ICH-MODEM");
	strcpy(card->shortname, "Intel ICH");
	for (name = shortnames; name->id; name++) {
		if (pci->device == name->id) {
			strcpy(card->shortname, name->s);
			break;
		}
	}
	strcat(card->shortname," Modem");

	if ((err = snd_intel8x0m_create(card, pci, pci_id->driver_data, &chip)) < 0) {
		snd_card_free(card);
		return err;
	}
	card->private_data = chip;

	if ((err = snd_intel8x0_mixer(chip, ac97_clock)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_intel8x0_pcm(chip)) < 0) {
		snd_card_free(card);
		return err;
	}
	
	snd_intel8x0m_proc_init(chip);

	sprintf(card->longname, "%s at irq %i",
		card->shortname, chip->irq);

	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
	pci_set_drvdata(pci, card);
	return 0;
}

static void __devexit snd_intel8x0m_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
	pci_set_drvdata(pci, NULL);
}

static struct pci_driver driver = {
	.name = "Intel ICH Modem",
	.id_table = snd_intel8x0m_ids,
	.probe = snd_intel8x0m_probe,
	.remove = __devexit_p(snd_intel8x0m_remove),
#ifdef CONFIG_PM
	.suspend = intel8x0m_suspend,
	.resume = intel8x0m_resume,
#endif
};


static int __init alsa_card_intel8x0m_init(void)
{
	return pci_register_driver(&driver);
}

static void __exit alsa_card_intel8x0m_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(alsa_card_intel8x0m_init)
module_exit(alsa_card_intel8x0m_exit)
