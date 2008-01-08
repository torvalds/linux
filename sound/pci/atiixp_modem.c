/*
 *   ALSA driver for ATI IXP 150/200/250 AC97 modem controllers
 *
 *	Copyright (c) 2004 Takashi Iwai <tiwai@suse.de>
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
#include <linux/mutex.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/info.h>
#include <sound/ac97_codec.h>
#include <sound/initval.h>

MODULE_AUTHOR("Takashi Iwai <tiwai@suse.de>");
MODULE_DESCRIPTION("ATI IXP MC97 controller");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{ATI,IXP150/200/250}}");

static int index = -2; /* Exclude the first card */
static char *id = SNDRV_DEFAULT_STR1;	/* ID for this card */
static int ac97_clock = 48000;

module_param(index, int, 0444);
MODULE_PARM_DESC(index, "Index value for ATI IXP controller.");
module_param(id, charp, 0444);
MODULE_PARM_DESC(id, "ID string for ATI IXP controller.");
module_param(ac97_clock, int, 0444);
MODULE_PARM_DESC(ac97_clock, "AC'97 codec clock (default 48000Hz).");

/* just for backward compatibility */
static int enable;
module_param(enable, bool, 0444);


/*
 */

#define ATI_REG_ISR			0x00	/* interrupt source */
#define  ATI_REG_ISR_MODEM_IN_XRUN	(1U<<0)
#define  ATI_REG_ISR_MODEM_IN_STATUS	(1U<<1)
#define  ATI_REG_ISR_MODEM_OUT1_XRUN	(1U<<2)
#define  ATI_REG_ISR_MODEM_OUT1_STATUS	(1U<<3)
#define  ATI_REG_ISR_MODEM_OUT2_XRUN	(1U<<4)
#define  ATI_REG_ISR_MODEM_OUT2_STATUS	(1U<<5)
#define  ATI_REG_ISR_MODEM_OUT3_XRUN	(1U<<6)
#define  ATI_REG_ISR_MODEM_OUT3_STATUS	(1U<<7)
#define  ATI_REG_ISR_PHYS_INTR		(1U<<8)
#define  ATI_REG_ISR_PHYS_MISMATCH	(1U<<9)
#define  ATI_REG_ISR_CODEC0_NOT_READY	(1U<<10)
#define  ATI_REG_ISR_CODEC1_NOT_READY	(1U<<11)
#define  ATI_REG_ISR_CODEC2_NOT_READY	(1U<<12)
#define  ATI_REG_ISR_NEW_FRAME		(1U<<13)
#define  ATI_REG_ISR_MODEM_GPIO_DATA	(1U<<14)

#define ATI_REG_IER			0x04	/* interrupt enable */
#define  ATI_REG_IER_MODEM_IN_XRUN_EN	(1U<<0)
#define  ATI_REG_IER_MODEM_STATUS_EN	(1U<<1)
#define  ATI_REG_IER_MODEM_OUT1_XRUN_EN	(1U<<2)
#define  ATI_REG_IER_MODEM_OUT2_XRUN_EN	(1U<<4)
#define  ATI_REG_IER_MODEM_OUT3_XRUN_EN	(1U<<6)
#define  ATI_REG_IER_PHYS_INTR_EN	(1U<<8)
#define  ATI_REG_IER_PHYS_MISMATCH_EN	(1U<<9)
#define  ATI_REG_IER_CODEC0_INTR_EN	(1U<<10)
#define  ATI_REG_IER_CODEC1_INTR_EN	(1U<<11)
#define  ATI_REG_IER_CODEC2_INTR_EN	(1U<<12)
#define  ATI_REG_IER_NEW_FRAME_EN	(1U<<13)	/* (RO */
#define  ATI_REG_IER_MODEM_GPIO_DATA_EN	(1U<<14)	/* (WO) modem is running */
#define  ATI_REG_IER_MODEM_SET_BUS_BUSY	(1U<<15)

#define ATI_REG_CMD			0x08	/* command */
#define  ATI_REG_CMD_POWERDOWN	(1U<<0)
#define  ATI_REG_CMD_MODEM_RECEIVE_EN	(1U<<1)	/* modem only */
#define  ATI_REG_CMD_MODEM_SEND1_EN	(1U<<2)	/* modem only */
#define  ATI_REG_CMD_MODEM_SEND2_EN	(1U<<3)	/* modem only */
#define  ATI_REG_CMD_MODEM_SEND3_EN	(1U<<4)	/* modem only */
#define  ATI_REG_CMD_MODEM_STATUS_MEM	(1U<<5)	/* modem only */
#define  ATI_REG_CMD_MODEM_IN_DMA_EN	(1U<<8)	/* modem only */
#define  ATI_REG_CMD_MODEM_OUT_DMA1_EN	(1U<<9)	/* modem only */
#define  ATI_REG_CMD_MODEM_OUT_DMA2_EN	(1U<<10)	/* modem only */
#define  ATI_REG_CMD_MODEM_OUT_DMA3_EN	(1U<<11)	/* modem only */
#define  ATI_REG_CMD_AUDIO_PRESENT	(1U<<20)
#define  ATI_REG_CMD_MODEM_GPIO_THRU_DMA	(1U<<22)	/* modem only */
#define  ATI_REG_CMD_LOOPBACK_EN	(1U<<23)
#define  ATI_REG_CMD_PACKED_DIS		(1U<<24)
#define  ATI_REG_CMD_BURST_EN		(1U<<25)
#define  ATI_REG_CMD_PANIC_EN		(1U<<26)
#define  ATI_REG_CMD_MODEM_PRESENT	(1U<<27)
#define  ATI_REG_CMD_ACLINK_ACTIVE	(1U<<28)
#define  ATI_REG_CMD_AC_SOFT_RESET	(1U<<29)
#define  ATI_REG_CMD_AC_SYNC		(1U<<30)
#define  ATI_REG_CMD_AC_RESET		(1U<<31)

#define ATI_REG_PHYS_OUT_ADDR		0x0c
#define  ATI_REG_PHYS_OUT_CODEC_MASK	(3U<<0)
#define  ATI_REG_PHYS_OUT_RW		(1U<<2)
#define  ATI_REG_PHYS_OUT_ADDR_EN	(1U<<8)
#define  ATI_REG_PHYS_OUT_ADDR_SHIFT	9
#define  ATI_REG_PHYS_OUT_DATA_SHIFT	16

#define ATI_REG_PHYS_IN_ADDR		0x10
#define  ATI_REG_PHYS_IN_READ_FLAG	(1U<<8)
#define  ATI_REG_PHYS_IN_ADDR_SHIFT	9
#define  ATI_REG_PHYS_IN_DATA_SHIFT	16

#define ATI_REG_SLOTREQ			0x14

#define ATI_REG_COUNTER			0x18
#define  ATI_REG_COUNTER_SLOT		(3U<<0)	/* slot # */
#define  ATI_REG_COUNTER_BITCLOCK	(31U<<8)

#define ATI_REG_IN_FIFO_THRESHOLD	0x1c

#define ATI_REG_MODEM_IN_DMA_LINKPTR	0x20
#define ATI_REG_MODEM_IN_DMA_DT_START	0x24	/* RO */
#define ATI_REG_MODEM_IN_DMA_DT_NEXT	0x28	/* RO */
#define ATI_REG_MODEM_IN_DMA_DT_CUR	0x2c	/* RO */
#define ATI_REG_MODEM_IN_DMA_DT_SIZE	0x30
#define ATI_REG_MODEM_OUT_FIFO		0x34	/* output threshold */
#define  ATI_REG_MODEM_OUT1_DMA_THRESHOLD_MASK	(0xf<<16)
#define  ATI_REG_MODEM_OUT1_DMA_THRESHOLD_SHIFT	16
#define ATI_REG_MODEM_OUT_DMA1_LINKPTR	0x38
#define ATI_REG_MODEM_OUT_DMA2_LINKPTR	0x3c
#define ATI_REG_MODEM_OUT_DMA3_LINKPTR	0x40
#define ATI_REG_MODEM_OUT_DMA1_DT_START	0x44
#define ATI_REG_MODEM_OUT_DMA1_DT_NEXT	0x48
#define ATI_REG_MODEM_OUT_DMA1_DT_CUR	0x4c
#define ATI_REG_MODEM_OUT_DMA2_DT_START	0x50
#define ATI_REG_MODEM_OUT_DMA2_DT_NEXT	0x54
#define ATI_REG_MODEM_OUT_DMA2_DT_CUR	0x58
#define ATI_REG_MODEM_OUT_DMA3_DT_START	0x5c
#define ATI_REG_MODEM_OUT_DMA3_DT_NEXT	0x60
#define ATI_REG_MODEM_OUT_DMA3_DT_CUR	0x64
#define ATI_REG_MODEM_OUT_DMA12_DT_SIZE	0x68
#define ATI_REG_MODEM_OUT_DMA3_DT_SIZE	0x6c
#define ATI_REG_MODEM_OUT_FIFO_USED     0x70
#define ATI_REG_MODEM_OUT_GPIO		0x74
#define  ATI_REG_MODEM_OUT_GPIO_EN	   1
#define  ATI_REG_MODEM_OUT_GPIO_DATA_SHIFT 5
#define ATI_REG_MODEM_IN_GPIO		0x78

#define ATI_REG_MODEM_MIRROR		0x7c
#define ATI_REG_AUDIO_MIRROR		0x80

#define ATI_REG_MODEM_FIFO_FLUSH	0x88
#define  ATI_REG_MODEM_FIFO_OUT1_FLUSH	(1U<<0)
#define  ATI_REG_MODEM_FIFO_OUT2_FLUSH	(1U<<1)
#define  ATI_REG_MODEM_FIFO_OUT3_FLUSH	(1U<<2)
#define  ATI_REG_MODEM_FIFO_IN_FLUSH	(1U<<3)

/* LINKPTR */
#define  ATI_REG_LINKPTR_EN		(1U<<0)

#define ATI_MAX_DESCRIPTORS	256	/* max number of descriptor packets */


struct atiixp_modem;

/*
 * DMA packate descriptor
 */

struct atiixp_dma_desc {
	u32 addr;	/* DMA buffer address */
	u16 status;	/* status bits */
	u16 size;	/* size of the packet in dwords */
	u32 next;	/* address of the next packet descriptor */
};

/*
 * stream enum
 */
enum { ATI_DMA_PLAYBACK, ATI_DMA_CAPTURE, NUM_ATI_DMAS }; /* DMAs */
enum { ATI_PCM_OUT, ATI_PCM_IN, NUM_ATI_PCMS }; /* AC97 pcm slots */
enum { ATI_PCMDEV_ANALOG, NUM_ATI_PCMDEVS }; /* pcm devices */

#define NUM_ATI_CODECS	3


/*
 * constants and callbacks for each DMA type
 */
struct atiixp_dma_ops {
	int type;			/* ATI_DMA_XXX */
	unsigned int llp_offset;	/* LINKPTR offset */
	unsigned int dt_cur;		/* DT_CUR offset */
	/* called from open callback */
	void (*enable_dma)(struct atiixp_modem *chip, int on);
	/* called from trigger (START/STOP) */
	void (*enable_transfer)(struct atiixp_modem *chip, int on);
 	/* called from trigger (STOP only) */
	void (*flush_dma)(struct atiixp_modem *chip);
};

/*
 * DMA stream
 */
struct atiixp_dma {
	const struct atiixp_dma_ops *ops;
	struct snd_dma_buffer desc_buf;
	struct snd_pcm_substream *substream;	/* assigned PCM substream */
	unsigned int buf_addr, buf_bytes;	/* DMA buffer address, bytes */
	unsigned int period_bytes, periods;
	int opened;
	int running;
	int pcm_open_flag;
	int ac97_pcm_type;	/* index # of ac97_pcm to access, -1 = not used */
};

/*
 * ATI IXP chip
 */
struct atiixp_modem {
	struct snd_card *card;
	struct pci_dev *pci;

	struct resource *res;		/* memory i/o */
	unsigned long addr;
	void __iomem *remap_addr;
	int irq;
	
	struct snd_ac97_bus *ac97_bus;
	struct snd_ac97 *ac97[NUM_ATI_CODECS];

	spinlock_t reg_lock;

	struct atiixp_dma dmas[NUM_ATI_DMAS];
	struct ac97_pcm *pcms[NUM_ATI_PCMS];
	struct snd_pcm *pcmdevs[NUM_ATI_PCMDEVS];

	int max_channels;		/* max. channels for PCM out */

	unsigned int codec_not_ready_bits;	/* for codec detection */

	int spdif_over_aclink;		/* passed from the module option */
	struct mutex open_mutex;	/* playback open mutex */
};


/*
 */
static struct pci_device_id snd_atiixp_ids[] = {
	{ 0x1002, 0x434d, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 }, /* SB200 */
	{ 0x1002, 0x4378, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 }, /* SB400 */
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, snd_atiixp_ids);


/*
 * lowlevel functions
 */

/*
 * update the bits of the given register.
 * return 1 if the bits changed.
 */
static int snd_atiixp_update_bits(struct atiixp_modem *chip, unsigned int reg,
				  unsigned int mask, unsigned int value)
{
	void __iomem *addr = chip->remap_addr + reg;
	unsigned int data, old_data;
	old_data = data = readl(addr);
	data &= ~mask;
	data |= value;
	if (old_data == data)
		return 0;
	writel(data, addr);
	return 1;
}

/*
 * macros for easy use
 */
#define atiixp_write(chip,reg,value) \
	writel(value, chip->remap_addr + ATI_REG_##reg)
#define atiixp_read(chip,reg) \
	readl(chip->remap_addr + ATI_REG_##reg)
#define atiixp_update(chip,reg,mask,val) \
	snd_atiixp_update_bits(chip, ATI_REG_##reg, mask, val)

/*
 * handling DMA packets
 *
 * we allocate a linear buffer for the DMA, and split it to  each packet.
 * in a future version, a scatter-gather buffer should be implemented.
 */

#define ATI_DESC_LIST_SIZE \
	PAGE_ALIGN(ATI_MAX_DESCRIPTORS * sizeof(struct atiixp_dma_desc))

/*
 * build packets ring for the given buffer size.
 *
 * IXP handles the buffer descriptors, which are connected as a linked
 * list.  although we can change the list dynamically, in this version,
 * a static RING of buffer descriptors is used.
 *
 * the ring is built in this function, and is set up to the hardware. 
 */
static int atiixp_build_dma_packets(struct atiixp_modem *chip,
				    struct atiixp_dma *dma,
				    struct snd_pcm_substream *substream,
				    unsigned int periods,
				    unsigned int period_bytes)
{
	unsigned int i;
	u32 addr, desc_addr;
	unsigned long flags;

	if (periods > ATI_MAX_DESCRIPTORS)
		return -ENOMEM;

	if (dma->desc_buf.area == NULL) {
		if (snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, snd_dma_pci_data(chip->pci),
					ATI_DESC_LIST_SIZE, &dma->desc_buf) < 0)
			return -ENOMEM;
		dma->period_bytes = dma->periods = 0; /* clear */
	}

	if (dma->periods == periods && dma->period_bytes == period_bytes)
		return 0;

	/* reset DMA before changing the descriptor table */
	spin_lock_irqsave(&chip->reg_lock, flags);
	writel(0, chip->remap_addr + dma->ops->llp_offset);
	dma->ops->enable_dma(chip, 0);
	dma->ops->enable_dma(chip, 1);
	spin_unlock_irqrestore(&chip->reg_lock, flags);

	/* fill the entries */
	addr = (u32)substream->runtime->dma_addr;
	desc_addr = (u32)dma->desc_buf.addr;
	for (i = 0; i < periods; i++) {
		struct atiixp_dma_desc *desc;
		desc = &((struct atiixp_dma_desc *)dma->desc_buf.area)[i];
		desc->addr = cpu_to_le32(addr);
		desc->status = 0;
		desc->size = period_bytes >> 2; /* in dwords */
		desc_addr += sizeof(struct atiixp_dma_desc);
		if (i == periods - 1)
			desc->next = cpu_to_le32((u32)dma->desc_buf.addr);
		else
			desc->next = cpu_to_le32(desc_addr);
		addr += period_bytes;
	}

	writel((u32)dma->desc_buf.addr | ATI_REG_LINKPTR_EN,
	       chip->remap_addr + dma->ops->llp_offset);

	dma->period_bytes = period_bytes;
	dma->periods = periods;

	return 0;
}

/*
 * remove the ring buffer and release it if assigned
 */
static void atiixp_clear_dma_packets(struct atiixp_modem *chip,
				     struct atiixp_dma *dma,
				     struct snd_pcm_substream *substream)
{
	if (dma->desc_buf.area) {
		writel(0, chip->remap_addr + dma->ops->llp_offset);
		snd_dma_free_pages(&dma->desc_buf);
		dma->desc_buf.area = NULL;
	}
}

/*
 * AC97 interface
 */
static int snd_atiixp_acquire_codec(struct atiixp_modem *chip)
{
	int timeout = 1000;

	while (atiixp_read(chip, PHYS_OUT_ADDR) & ATI_REG_PHYS_OUT_ADDR_EN) {
		if (! timeout--) {
			snd_printk(KERN_WARNING "atiixp-modem: codec acquire timeout\n");
			return -EBUSY;
		}
		udelay(1);
	}
	return 0;
}

static unsigned short snd_atiixp_codec_read(struct atiixp_modem *chip,
					    unsigned short codec,
					    unsigned short reg)
{
	unsigned int data;
	int timeout;

	if (snd_atiixp_acquire_codec(chip) < 0)
		return 0xffff;
	data = (reg << ATI_REG_PHYS_OUT_ADDR_SHIFT) |
		ATI_REG_PHYS_OUT_ADDR_EN |
		ATI_REG_PHYS_OUT_RW |
		codec;
	atiixp_write(chip, PHYS_OUT_ADDR, data);
	if (snd_atiixp_acquire_codec(chip) < 0)
		return 0xffff;
	timeout = 1000;
	do {
		data = atiixp_read(chip, PHYS_IN_ADDR);
		if (data & ATI_REG_PHYS_IN_READ_FLAG)
			return data >> ATI_REG_PHYS_IN_DATA_SHIFT;
		udelay(1);
	} while (--timeout);
	/* time out may happen during reset */
	if (reg < 0x7c)
		snd_printk(KERN_WARNING "atiixp-modem: codec read timeout (reg %x)\n", reg);
	return 0xffff;
}


static void snd_atiixp_codec_write(struct atiixp_modem *chip,
				   unsigned short codec,
				   unsigned short reg, unsigned short val)
{
	unsigned int data;
    
	if (snd_atiixp_acquire_codec(chip) < 0)
		return;
	data = ((unsigned int)val << ATI_REG_PHYS_OUT_DATA_SHIFT) |
		((unsigned int)reg << ATI_REG_PHYS_OUT_ADDR_SHIFT) |
		ATI_REG_PHYS_OUT_ADDR_EN | codec;
	atiixp_write(chip, PHYS_OUT_ADDR, data);
}


static unsigned short snd_atiixp_ac97_read(struct snd_ac97 *ac97,
					   unsigned short reg)
{
	struct atiixp_modem *chip = ac97->private_data;
	return snd_atiixp_codec_read(chip, ac97->num, reg);
    
}

static void snd_atiixp_ac97_write(struct snd_ac97 *ac97, unsigned short reg,
				  unsigned short val)
{
	struct atiixp_modem *chip = ac97->private_data;
	if (reg == AC97_GPIO_STATUS) {
		atiixp_write(chip, MODEM_OUT_GPIO,
			(val << ATI_REG_MODEM_OUT_GPIO_DATA_SHIFT) | ATI_REG_MODEM_OUT_GPIO_EN);
		return;
	}
	snd_atiixp_codec_write(chip, ac97->num, reg, val);
}

/*
 * reset AC link
 */
static int snd_atiixp_aclink_reset(struct atiixp_modem *chip)
{
	int timeout;

	/* reset powerdoewn */
	if (atiixp_update(chip, CMD, ATI_REG_CMD_POWERDOWN, 0))
		udelay(10);

	/* perform a software reset */
	atiixp_update(chip, CMD, ATI_REG_CMD_AC_SOFT_RESET, ATI_REG_CMD_AC_SOFT_RESET);
	atiixp_read(chip, CMD);
	udelay(10);
	atiixp_update(chip, CMD, ATI_REG_CMD_AC_SOFT_RESET, 0);
    
	timeout = 10;
	while (! (atiixp_read(chip, CMD) & ATI_REG_CMD_ACLINK_ACTIVE)) {
		/* do a hard reset */
		atiixp_update(chip, CMD, ATI_REG_CMD_AC_SYNC|ATI_REG_CMD_AC_RESET,
			      ATI_REG_CMD_AC_SYNC);
		atiixp_read(chip, CMD);
		msleep(1);
		atiixp_update(chip, CMD, ATI_REG_CMD_AC_RESET, ATI_REG_CMD_AC_RESET);
		if (--timeout) {
			snd_printk(KERN_ERR "atiixp-modem: codec reset timeout\n");
			break;
		}
	}

	/* deassert RESET and assert SYNC to make sure */
	atiixp_update(chip, CMD, ATI_REG_CMD_AC_SYNC|ATI_REG_CMD_AC_RESET,
		      ATI_REG_CMD_AC_SYNC|ATI_REG_CMD_AC_RESET);

	return 0;
}

#ifdef CONFIG_PM
static int snd_atiixp_aclink_down(struct atiixp_modem *chip)
{
	// if (atiixp_read(chip, MODEM_MIRROR) & 0x1) /* modem running, too? */
	//	return -EBUSY;
	atiixp_update(chip, CMD,
		     ATI_REG_CMD_POWERDOWN | ATI_REG_CMD_AC_RESET,
		     ATI_REG_CMD_POWERDOWN);
	return 0;
}
#endif

/*
 * auto-detection of codecs
 *
 * the IXP chip can generate interrupts for the non-existing codecs.
 * NEW_FRAME interrupt is used to make sure that the interrupt is generated
 * even if all three codecs are connected.
 */

#define ALL_CODEC_NOT_READY \
	    (ATI_REG_ISR_CODEC0_NOT_READY |\
	     ATI_REG_ISR_CODEC1_NOT_READY |\
	     ATI_REG_ISR_CODEC2_NOT_READY)
#define CODEC_CHECK_BITS (ALL_CODEC_NOT_READY|ATI_REG_ISR_NEW_FRAME)

static int snd_atiixp_codec_detect(struct atiixp_modem *chip)
{
	int timeout;

	chip->codec_not_ready_bits = 0;
	atiixp_write(chip, IER, CODEC_CHECK_BITS);
	/* wait for the interrupts */
	timeout = 50;
	while (timeout-- > 0) {
		msleep(1);
		if (chip->codec_not_ready_bits)
			break;
	}
	atiixp_write(chip, IER, 0); /* disable irqs */

	if ((chip->codec_not_ready_bits & ALL_CODEC_NOT_READY) == ALL_CODEC_NOT_READY) {
		snd_printk(KERN_ERR "atiixp-modem: no codec detected!\n");
		return -ENXIO;
	}
	return 0;
}


/*
 * enable DMA and irqs
 */
static int snd_atiixp_chip_start(struct atiixp_modem *chip)
{
	unsigned int reg;

	/* set up spdif, enable burst mode */
	reg = atiixp_read(chip, CMD);
	reg |= ATI_REG_CMD_BURST_EN;
	if(!(reg & ATI_REG_CMD_MODEM_PRESENT))
		reg |= ATI_REG_CMD_MODEM_PRESENT;
	atiixp_write(chip, CMD, reg);

	/* clear all interrupt source */
	atiixp_write(chip, ISR, 0xffffffff);
	/* enable irqs */
	atiixp_write(chip, IER,
		     ATI_REG_IER_MODEM_STATUS_EN |
		     ATI_REG_IER_MODEM_IN_XRUN_EN |
		     ATI_REG_IER_MODEM_OUT1_XRUN_EN);
	return 0;
}


/*
 * disable DMA and IRQs
 */
static int snd_atiixp_chip_stop(struct atiixp_modem *chip)
{
	/* clear interrupt source */
	atiixp_write(chip, ISR, atiixp_read(chip, ISR));
	/* disable irqs */
	atiixp_write(chip, IER, 0);
	return 0;
}


/*
 * PCM section
 */

/*
 * pointer callback simplly reads XXX_DMA_DT_CUR register as the current
 * position.  when SG-buffer is implemented, the offset must be calculated
 * correctly...
 */
static snd_pcm_uframes_t snd_atiixp_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct atiixp_modem *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct atiixp_dma *dma = runtime->private_data;
	unsigned int curptr;
	int timeout = 1000;

	while (timeout--) {
		curptr = readl(chip->remap_addr + dma->ops->dt_cur);
		if (curptr < dma->buf_addr)
			continue;
		curptr -= dma->buf_addr;
		if (curptr >= dma->buf_bytes)
			continue;
		return bytes_to_frames(runtime, curptr);
	}
	snd_printd("atiixp-modem: invalid DMA pointer read 0x%x (buf=%x)\n",
		   readl(chip->remap_addr + dma->ops->dt_cur), dma->buf_addr);
	return 0;
}

/*
 * XRUN detected, and stop the PCM substream
 */
static void snd_atiixp_xrun_dma(struct atiixp_modem *chip,
				struct atiixp_dma *dma)
{
	if (! dma->substream || ! dma->running)
		return;
	snd_printdd("atiixp-modem: XRUN detected (DMA %d)\n", dma->ops->type);
	snd_pcm_stop(dma->substream, SNDRV_PCM_STATE_XRUN);
}

/*
 * the period ack.  update the substream.
 */
static void snd_atiixp_update_dma(struct atiixp_modem *chip,
				  struct atiixp_dma *dma)
{
	if (! dma->substream || ! dma->running)
		return;
	snd_pcm_period_elapsed(dma->substream);
}

/* set BUS_BUSY interrupt bit if any DMA is running */
/* call with spinlock held */
static void snd_atiixp_check_bus_busy(struct atiixp_modem *chip)
{
	unsigned int bus_busy;
	if (atiixp_read(chip, CMD) & (ATI_REG_CMD_MODEM_SEND1_EN |
				      ATI_REG_CMD_MODEM_RECEIVE_EN))
		bus_busy = ATI_REG_IER_MODEM_SET_BUS_BUSY;
	else
		bus_busy = 0;
	atiixp_update(chip, IER, ATI_REG_IER_MODEM_SET_BUS_BUSY, bus_busy);
}

/* common trigger callback
 * calling the lowlevel callbacks in it
 */
static int snd_atiixp_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct atiixp_modem *chip = snd_pcm_substream_chip(substream);
	struct atiixp_dma *dma = substream->runtime->private_data;
	int err = 0;

	snd_assert(dma->ops->enable_transfer && dma->ops->flush_dma, return -EINVAL);

	spin_lock(&chip->reg_lock);
	switch(cmd) {
	case SNDRV_PCM_TRIGGER_START:
		dma->ops->enable_transfer(chip, 1);
		dma->running = 1;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		dma->ops->enable_transfer(chip, 0);
		dma->running = 0;
		break;
	default:
		err = -EINVAL;
		break;
	}
	if (! err) {
	snd_atiixp_check_bus_busy(chip);
	if (cmd == SNDRV_PCM_TRIGGER_STOP) {
		dma->ops->flush_dma(chip);
		snd_atiixp_check_bus_busy(chip);
	}
	}
	spin_unlock(&chip->reg_lock);
	return err;
}


/*
 * lowlevel callbacks for each DMA type
 *
 * every callback is supposed to be called in chip->reg_lock spinlock
 */

/* flush FIFO of analog OUT DMA */
static void atiixp_out_flush_dma(struct atiixp_modem *chip)
{
	atiixp_write(chip, MODEM_FIFO_FLUSH, ATI_REG_MODEM_FIFO_OUT1_FLUSH);
}

/* enable/disable analog OUT DMA */
static void atiixp_out_enable_dma(struct atiixp_modem *chip, int on)
{
	unsigned int data;
	data = atiixp_read(chip, CMD);
	if (on) {
		if (data & ATI_REG_CMD_MODEM_OUT_DMA1_EN)
			return;
		atiixp_out_flush_dma(chip);
		data |= ATI_REG_CMD_MODEM_OUT_DMA1_EN;
	} else
		data &= ~ATI_REG_CMD_MODEM_OUT_DMA1_EN;
	atiixp_write(chip, CMD, data);
}

/* start/stop transfer over OUT DMA */
static void atiixp_out_enable_transfer(struct atiixp_modem *chip, int on)
{
	atiixp_update(chip, CMD, ATI_REG_CMD_MODEM_SEND1_EN,
		      on ? ATI_REG_CMD_MODEM_SEND1_EN : 0);
}

/* enable/disable analog IN DMA */
static void atiixp_in_enable_dma(struct atiixp_modem *chip, int on)
{
	atiixp_update(chip, CMD, ATI_REG_CMD_MODEM_IN_DMA_EN,
		      on ? ATI_REG_CMD_MODEM_IN_DMA_EN : 0);
}

/* start/stop analog IN DMA */
static void atiixp_in_enable_transfer(struct atiixp_modem *chip, int on)
{
	if (on) {
		unsigned int data = atiixp_read(chip, CMD);
		if (! (data & ATI_REG_CMD_MODEM_RECEIVE_EN)) {
			data |= ATI_REG_CMD_MODEM_RECEIVE_EN;
			atiixp_write(chip, CMD, data);
		}
	} else
		atiixp_update(chip, CMD, ATI_REG_CMD_MODEM_RECEIVE_EN, 0);
}

/* flush FIFO of analog IN DMA */
static void atiixp_in_flush_dma(struct atiixp_modem *chip)
{
	atiixp_write(chip, MODEM_FIFO_FLUSH, ATI_REG_MODEM_FIFO_IN_FLUSH);
}

/* set up slots and formats for analog OUT */
static int snd_atiixp_playback_prepare(struct snd_pcm_substream *substream)
{
	struct atiixp_modem *chip = snd_pcm_substream_chip(substream);
	unsigned int data;

	spin_lock_irq(&chip->reg_lock);
	/* set output threshold */
	data = atiixp_read(chip, MODEM_OUT_FIFO);
	data &= ~ATI_REG_MODEM_OUT1_DMA_THRESHOLD_MASK;
	data |= 0x04 << ATI_REG_MODEM_OUT1_DMA_THRESHOLD_SHIFT;
	atiixp_write(chip, MODEM_OUT_FIFO, data);
	spin_unlock_irq(&chip->reg_lock);
	return 0;
}

/* set up slots and formats for analog IN */
static int snd_atiixp_capture_prepare(struct snd_pcm_substream *substream)
{
	return 0;
}

/*
 * hw_params - allocate the buffer and set up buffer descriptors
 */
static int snd_atiixp_pcm_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *hw_params)
{
	struct atiixp_modem *chip = snd_pcm_substream_chip(substream);
	struct atiixp_dma *dma = substream->runtime->private_data;
	int err;
	int i;

	err = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
	if (err < 0)
		return err;
	dma->buf_addr = substream->runtime->dma_addr;
	dma->buf_bytes = params_buffer_bytes(hw_params);

	err = atiixp_build_dma_packets(chip, dma, substream,
				       params_periods(hw_params),
				       params_period_bytes(hw_params));
	if (err < 0)
		return err;

	/* set up modem rate */
	for (i = 0; i < NUM_ATI_CODECS; i++) {
		if (! chip->ac97[i])
			continue;
		snd_ac97_write(chip->ac97[i], AC97_LINE1_RATE, params_rate(hw_params));
		snd_ac97_write(chip->ac97[i], AC97_LINE1_LEVEL, 0);
	}

	return err;
}

static int snd_atiixp_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct atiixp_modem *chip = snd_pcm_substream_chip(substream);
	struct atiixp_dma *dma = substream->runtime->private_data;

	atiixp_clear_dma_packets(chip, dma, substream);
	snd_pcm_lib_free_pages(substream);
	return 0;
}


/*
 * pcm hardware definition, identical for all DMA types
 */
static struct snd_pcm_hardware snd_atiixp_pcm_hw =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		(SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000 |
				 SNDRV_PCM_RATE_KNOT),
	.rate_min =		8000,
	.rate_max =		16000,
	.channels_min =		2,
	.channels_max =		2,
	.buffer_bytes_max =	256 * 1024,
	.period_bytes_min =	32,
	.period_bytes_max =	128 * 1024,
	.periods_min =		2,
	.periods_max =		ATI_MAX_DESCRIPTORS,
};

static int snd_atiixp_pcm_open(struct snd_pcm_substream *substream,
			       struct atiixp_dma *dma, int pcm_type)
{
	struct atiixp_modem *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;
	static unsigned int rates[] = { 8000,  9600, 12000, 16000 };
	static struct snd_pcm_hw_constraint_list hw_constraints_rates = {
		.count = ARRAY_SIZE(rates),
		.list = rates,
		.mask = 0,
	};

	snd_assert(dma->ops && dma->ops->enable_dma, return -EINVAL);

	if (dma->opened)
		return -EBUSY;
	dma->substream = substream;
	runtime->hw = snd_atiixp_pcm_hw;
	dma->ac97_pcm_type = pcm_type;
	if ((err = snd_pcm_hw_constraint_list(runtime, 0,
					      SNDRV_PCM_HW_PARAM_RATE,
					      &hw_constraints_rates)) < 0)
		return err;
	if ((err = snd_pcm_hw_constraint_integer(runtime,
						 SNDRV_PCM_HW_PARAM_PERIODS)) < 0)
		return err;
	runtime->private_data = dma;

	/* enable DMA bits */
	spin_lock_irq(&chip->reg_lock);
	dma->ops->enable_dma(chip, 1);
	spin_unlock_irq(&chip->reg_lock);
	dma->opened = 1;

	return 0;
}

static int snd_atiixp_pcm_close(struct snd_pcm_substream *substream,
				struct atiixp_dma *dma)
{
	struct atiixp_modem *chip = snd_pcm_substream_chip(substream);
	/* disable DMA bits */
	snd_assert(dma->ops && dma->ops->enable_dma, return -EINVAL);
	spin_lock_irq(&chip->reg_lock);
	dma->ops->enable_dma(chip, 0);
	spin_unlock_irq(&chip->reg_lock);
	dma->substream = NULL;
	dma->opened = 0;
	return 0;
}

/*
 */
static int snd_atiixp_playback_open(struct snd_pcm_substream *substream)
{
	struct atiixp_modem *chip = snd_pcm_substream_chip(substream);
	int err;

	mutex_lock(&chip->open_mutex);
	err = snd_atiixp_pcm_open(substream, &chip->dmas[ATI_DMA_PLAYBACK], 0);
	mutex_unlock(&chip->open_mutex);
	if (err < 0)
		return err;
	return 0;
}

static int snd_atiixp_playback_close(struct snd_pcm_substream *substream)
{
	struct atiixp_modem *chip = snd_pcm_substream_chip(substream);
	int err;
	mutex_lock(&chip->open_mutex);
	err = snd_atiixp_pcm_close(substream, &chip->dmas[ATI_DMA_PLAYBACK]);
	mutex_unlock(&chip->open_mutex);
	return err;
}

static int snd_atiixp_capture_open(struct snd_pcm_substream *substream)
{
	struct atiixp_modem *chip = snd_pcm_substream_chip(substream);
	return snd_atiixp_pcm_open(substream, &chip->dmas[ATI_DMA_CAPTURE], 1);
}

static int snd_atiixp_capture_close(struct snd_pcm_substream *substream)
{
	struct atiixp_modem *chip = snd_pcm_substream_chip(substream);
	return snd_atiixp_pcm_close(substream, &chip->dmas[ATI_DMA_CAPTURE]);
}


/* AC97 playback */
static struct snd_pcm_ops snd_atiixp_playback_ops = {
	.open =		snd_atiixp_playback_open,
	.close =	snd_atiixp_playback_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_atiixp_pcm_hw_params,
	.hw_free =	snd_atiixp_pcm_hw_free,
	.prepare =	snd_atiixp_playback_prepare,
	.trigger =	snd_atiixp_pcm_trigger,
	.pointer =	snd_atiixp_pcm_pointer,
};

/* AC97 capture */
static struct snd_pcm_ops snd_atiixp_capture_ops = {
	.open =		snd_atiixp_capture_open,
	.close =	snd_atiixp_capture_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_atiixp_pcm_hw_params,
	.hw_free =	snd_atiixp_pcm_hw_free,
	.prepare =	snd_atiixp_capture_prepare,
	.trigger =	snd_atiixp_pcm_trigger,
	.pointer =	snd_atiixp_pcm_pointer,
};

static struct atiixp_dma_ops snd_atiixp_playback_dma_ops = {
	.type = ATI_DMA_PLAYBACK,
	.llp_offset = ATI_REG_MODEM_OUT_DMA1_LINKPTR,
	.dt_cur = ATI_REG_MODEM_OUT_DMA1_DT_CUR,
	.enable_dma = atiixp_out_enable_dma,
	.enable_transfer = atiixp_out_enable_transfer,
	.flush_dma = atiixp_out_flush_dma,
};
	
static struct atiixp_dma_ops snd_atiixp_capture_dma_ops = {
	.type = ATI_DMA_CAPTURE,
	.llp_offset = ATI_REG_MODEM_IN_DMA_LINKPTR,
	.dt_cur = ATI_REG_MODEM_IN_DMA_DT_CUR,
	.enable_dma = atiixp_in_enable_dma,
	.enable_transfer = atiixp_in_enable_transfer,
	.flush_dma = atiixp_in_flush_dma,
};

static int __devinit snd_atiixp_pcm_new(struct atiixp_modem *chip)
{
	struct snd_pcm *pcm;
	int err;

	/* initialize constants */
	chip->dmas[ATI_DMA_PLAYBACK].ops = &snd_atiixp_playback_dma_ops;
	chip->dmas[ATI_DMA_CAPTURE].ops = &snd_atiixp_capture_dma_ops;

	/* PCM #0: analog I/O */
	err = snd_pcm_new(chip->card, "ATI IXP MC97", ATI_PCMDEV_ANALOG, 1, 1, &pcm);
	if (err < 0)
		return err;
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_atiixp_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_atiixp_capture_ops);
	pcm->dev_class = SNDRV_PCM_CLASS_MODEM;
	pcm->private_data = chip;
	strcpy(pcm->name, "ATI IXP MC97");
	chip->pcmdevs[ATI_PCMDEV_ANALOG] = pcm;

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
					      snd_dma_pci_data(chip->pci),
					      64*1024, 128*1024);

	return 0;
}



/*
 * interrupt handler
 */
static irqreturn_t snd_atiixp_interrupt(int irq, void *dev_id)
{
	struct atiixp_modem *chip = dev_id;
	unsigned int status;

	status = atiixp_read(chip, ISR);

	if (! status)
		return IRQ_NONE;

	/* process audio DMA */
	if (status & ATI_REG_ISR_MODEM_OUT1_XRUN)
		snd_atiixp_xrun_dma(chip,  &chip->dmas[ATI_DMA_PLAYBACK]);
	else if (status & ATI_REG_ISR_MODEM_OUT1_STATUS)
		snd_atiixp_update_dma(chip, &chip->dmas[ATI_DMA_PLAYBACK]);
	if (status & ATI_REG_ISR_MODEM_IN_XRUN)
		snd_atiixp_xrun_dma(chip,  &chip->dmas[ATI_DMA_CAPTURE]);
	else if (status & ATI_REG_ISR_MODEM_IN_STATUS)
		snd_atiixp_update_dma(chip, &chip->dmas[ATI_DMA_CAPTURE]);

	/* for codec detection */
	if (status & CODEC_CHECK_BITS) {
		unsigned int detected;
		detected = status & CODEC_CHECK_BITS;
		spin_lock(&chip->reg_lock);
		chip->codec_not_ready_bits |= detected;
		atiixp_update(chip, IER, detected, 0); /* disable the detected irqs */
		spin_unlock(&chip->reg_lock);
	}

	/* ack */
	atiixp_write(chip, ISR, status);

	return IRQ_HANDLED;
}


/*
 * ac97 mixer section
 */

static int __devinit snd_atiixp_mixer_new(struct atiixp_modem *chip, int clock)
{
	struct snd_ac97_bus *pbus;
	struct snd_ac97_template ac97;
	int i, err;
	int codec_count;
	static struct snd_ac97_bus_ops ops = {
		.write = snd_atiixp_ac97_write,
		.read = snd_atiixp_ac97_read,
	};
	static unsigned int codec_skip[NUM_ATI_CODECS] = {
		ATI_REG_ISR_CODEC0_NOT_READY,
		ATI_REG_ISR_CODEC1_NOT_READY,
		ATI_REG_ISR_CODEC2_NOT_READY,
	};

	if (snd_atiixp_codec_detect(chip) < 0)
		return -ENXIO;

	if ((err = snd_ac97_bus(chip->card, 0, &ops, chip, &pbus)) < 0)
		return err;
	pbus->clock = clock;
	chip->ac97_bus = pbus;

	codec_count = 0;
	for (i = 0; i < NUM_ATI_CODECS; i++) {
		if (chip->codec_not_ready_bits & codec_skip[i])
			continue;
		memset(&ac97, 0, sizeof(ac97));
		ac97.private_data = chip;
		ac97.pci = chip->pci;
		ac97.num = i;
		ac97.scaps = AC97_SCAP_SKIP_AUDIO | AC97_SCAP_POWER_SAVE;
		if ((err = snd_ac97_mixer(pbus, &ac97, &chip->ac97[i])) < 0) {
			chip->ac97[i] = NULL; /* to be sure */
			snd_printdd("atiixp-modem: codec %d not available for modem\n", i);
			continue;
		}
		codec_count++;
	}

	if (! codec_count) {
		snd_printk(KERN_ERR "atiixp-modem: no codec available\n");
		return -ENODEV;
	}

	/* snd_ac97_tune_hardware(chip->ac97, ac97_quirks); */

	return 0;
}


#ifdef CONFIG_PM
/*
 * power management
 */
static int snd_atiixp_suspend(struct pci_dev *pci, pm_message_t state)
{
	struct snd_card *card = pci_get_drvdata(pci);
	struct atiixp_modem *chip = card->private_data;
	int i;

	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
	for (i = 0; i < NUM_ATI_PCMDEVS; i++)
		snd_pcm_suspend_all(chip->pcmdevs[i]);
	for (i = 0; i < NUM_ATI_CODECS; i++)
		snd_ac97_suspend(chip->ac97[i]);
	snd_atiixp_aclink_down(chip);
	snd_atiixp_chip_stop(chip);

	pci_disable_device(pci);
	pci_save_state(pci);
	pci_set_power_state(pci, pci_choose_state(pci, state));
	return 0;
}

static int snd_atiixp_resume(struct pci_dev *pci)
{
	struct snd_card *card = pci_get_drvdata(pci);
	struct atiixp_modem *chip = card->private_data;
	int i;

	pci_set_power_state(pci, PCI_D0);
	pci_restore_state(pci);
	if (pci_enable_device(pci) < 0) {
		printk(KERN_ERR "atiixp-modem: pci_enable_device failed, "
		       "disabling device\n");
		snd_card_disconnect(card);
		return -EIO;
	}
	pci_set_master(pci);

	snd_atiixp_aclink_reset(chip);
	snd_atiixp_chip_start(chip);

	for (i = 0; i < NUM_ATI_CODECS; i++)
		snd_ac97_resume(chip->ac97[i]);

	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	return 0;
}
#endif /* CONFIG_PM */


#ifdef CONFIG_PROC_FS
/*
 * proc interface for register dump
 */

static void snd_atiixp_proc_read(struct snd_info_entry *entry,
				 struct snd_info_buffer *buffer)
{
	struct atiixp_modem *chip = entry->private_data;
	int i;

	for (i = 0; i < 256; i += 4)
		snd_iprintf(buffer, "%02x: %08x\n", i, readl(chip->remap_addr + i));
}

static void __devinit snd_atiixp_proc_init(struct atiixp_modem *chip)
{
	struct snd_info_entry *entry;

	if (! snd_card_proc_new(chip->card, "atiixp-modem", &entry))
		snd_info_set_text_ops(entry, chip, snd_atiixp_proc_read);
}
#else
#define snd_atiixp_proc_init(chip)
#endif


/*
 * destructor
 */

static int snd_atiixp_free(struct atiixp_modem *chip)
{
	if (chip->irq < 0)
		goto __hw_end;
	snd_atiixp_chip_stop(chip);
	synchronize_irq(chip->irq);
      __hw_end:
	if (chip->irq >= 0)
		free_irq(chip->irq, chip);
	if (chip->remap_addr)
		iounmap(chip->remap_addr);
	pci_release_regions(chip->pci);
	pci_disable_device(chip->pci);
	kfree(chip);
	return 0;
}

static int snd_atiixp_dev_free(struct snd_device *device)
{
	struct atiixp_modem *chip = device->device_data;
	return snd_atiixp_free(chip);
}

/*
 * constructor for chip instance
 */
static int __devinit snd_atiixp_create(struct snd_card *card,
				       struct pci_dev *pci,
				       struct atiixp_modem **r_chip)
{
	static struct snd_device_ops ops = {
		.dev_free =	snd_atiixp_dev_free,
	};
	struct atiixp_modem *chip;
	int err;

	if ((err = pci_enable_device(pci)) < 0)
		return err;

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (chip == NULL) {
		pci_disable_device(pci);
		return -ENOMEM;
	}

	spin_lock_init(&chip->reg_lock);
	mutex_init(&chip->open_mutex);
	chip->card = card;
	chip->pci = pci;
	chip->irq = -1;
	if ((err = pci_request_regions(pci, "ATI IXP MC97")) < 0) {
		kfree(chip);
		pci_disable_device(pci);
		return err;
	}
	chip->addr = pci_resource_start(pci, 0);
	chip->remap_addr = ioremap_nocache(chip->addr, pci_resource_len(pci, 0));
	if (chip->remap_addr == NULL) {
		snd_printk(KERN_ERR "AC'97 space ioremap problem\n");
		snd_atiixp_free(chip);
		return -EIO;
	}

	if (request_irq(pci->irq, snd_atiixp_interrupt, IRQF_SHARED,
			card->shortname, chip)) {
		snd_printk(KERN_ERR "unable to grab IRQ %d\n", pci->irq);
		snd_atiixp_free(chip);
		return -EBUSY;
	}
	chip->irq = pci->irq;
	pci_set_master(pci);
	synchronize_irq(chip->irq);

	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops)) < 0) {
		snd_atiixp_free(chip);
		return err;
	}

	snd_card_set_dev(card, &pci->dev);

	*r_chip = chip;
	return 0;
}


static int __devinit snd_atiixp_probe(struct pci_dev *pci,
				      const struct pci_device_id *pci_id)
{
	struct snd_card *card;
	struct atiixp_modem *chip;
	int err;

	card = snd_card_new(index, id, THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;

	strcpy(card->driver, "ATIIXP-MODEM");
	strcpy(card->shortname, "ATI IXP Modem");
	if ((err = snd_atiixp_create(card, pci, &chip)) < 0)
		goto __error;
	card->private_data = chip;

	if ((err = snd_atiixp_aclink_reset(chip)) < 0)
		goto __error;

	if ((err = snd_atiixp_mixer_new(chip, ac97_clock)) < 0)
		goto __error;

	if ((err = snd_atiixp_pcm_new(chip)) < 0)
		goto __error;
	
	snd_atiixp_proc_init(chip);

	snd_atiixp_chip_start(chip);

	sprintf(card->longname, "%s rev %x at 0x%lx, irq %i",
		card->shortname, pci->revision, chip->addr, chip->irq);

	if ((err = snd_card_register(card)) < 0)
		goto __error;

	pci_set_drvdata(pci, card);
	return 0;

 __error:
	snd_card_free(card);
	return err;
}

static void __devexit snd_atiixp_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
	pci_set_drvdata(pci, NULL);
}

static struct pci_driver driver = {
	.name = "ATI IXP MC97 controller",
	.id_table = snd_atiixp_ids,
	.probe = snd_atiixp_probe,
	.remove = __devexit_p(snd_atiixp_remove),
#ifdef CONFIG_PM
	.suspend = snd_atiixp_suspend,
	.resume = snd_atiixp_resume,
#endif
};


static int __init alsa_card_atiixp_init(void)
{
	return pci_register_driver(&driver);
}

static void __exit alsa_card_atiixp_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(alsa_card_atiixp_init)
module_exit(alsa_card_atiixp_exit)
