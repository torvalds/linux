/*
 *   ALSA driver for ATI IXP 150/200/250/300 AC97 controllers
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

#include <sound/driver.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/info.h>
#include <sound/ac97_codec.h>
#include <sound/initval.h>

MODULE_AUTHOR("Takashi Iwai <tiwai@suse.de>");
MODULE_DESCRIPTION("ATI IXP AC97 controller");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{ATI,IXP150/200/250/300/400}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;	/* Enable this card */
static int ac97_clock[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 48000};
static char *ac97_quirk[SNDRV_CARDS];
static int spdif_aclink[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 1};

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for ATI IXP controller.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for ATI IXP controller.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable audio part of ATI IXP controller.");
module_param_array(ac97_clock, int, NULL, 0444);
MODULE_PARM_DESC(ac97_clock, "AC'97 codec clock (default 48000Hz).");
module_param_array(ac97_quirk, charp, NULL, 0444);
MODULE_PARM_DESC(ac97_quirk, "AC'97 workaround for strange hardware.");
module_param_array(spdif_aclink, bool, NULL, 0444);
MODULE_PARM_DESC(spdif_aclink, "S/PDIF over AC-link.");


/*
 */

#define ATI_REG_ISR			0x00	/* interrupt source */
#define  ATI_REG_ISR_IN_XRUN		(1U<<0)
#define  ATI_REG_ISR_IN_STATUS		(1U<<1)
#define  ATI_REG_ISR_OUT_XRUN		(1U<<2)
#define  ATI_REG_ISR_OUT_STATUS		(1U<<3)
#define  ATI_REG_ISR_SPDF_XRUN		(1U<<4)
#define  ATI_REG_ISR_SPDF_STATUS	(1U<<5)
#define  ATI_REG_ISR_PHYS_INTR		(1U<<8)
#define  ATI_REG_ISR_PHYS_MISMATCH	(1U<<9)
#define  ATI_REG_ISR_CODEC0_NOT_READY	(1U<<10)
#define  ATI_REG_ISR_CODEC1_NOT_READY	(1U<<11)
#define  ATI_REG_ISR_CODEC2_NOT_READY	(1U<<12)
#define  ATI_REG_ISR_NEW_FRAME		(1U<<13)

#define ATI_REG_IER			0x04	/* interrupt enable */
#define  ATI_REG_IER_IN_XRUN_EN		(1U<<0)
#define  ATI_REG_IER_IO_STATUS_EN	(1U<<1)
#define  ATI_REG_IER_OUT_XRUN_EN	(1U<<2)
#define  ATI_REG_IER_OUT_XRUN_COND	(1U<<3)
#define  ATI_REG_IER_SPDF_XRUN_EN	(1U<<4)
#define  ATI_REG_IER_SPDF_STATUS_EN	(1U<<5)
#define  ATI_REG_IER_PHYS_INTR_EN	(1U<<8)
#define  ATI_REG_IER_PHYS_MISMATCH_EN	(1U<<9)
#define  ATI_REG_IER_CODEC0_INTR_EN	(1U<<10)
#define  ATI_REG_IER_CODEC1_INTR_EN	(1U<<11)
#define  ATI_REG_IER_CODEC2_INTR_EN	(1U<<12)
#define  ATI_REG_IER_NEW_FRAME_EN	(1U<<13)	/* (RO */
#define  ATI_REG_IER_SET_BUS_BUSY	(1U<<14)	/* (WO) audio is running */

#define ATI_REG_CMD			0x08	/* command */
#define  ATI_REG_CMD_POWERDOWN		(1U<<0)
#define  ATI_REG_CMD_RECEIVE_EN		(1U<<1)
#define  ATI_REG_CMD_SEND_EN		(1U<<2)
#define  ATI_REG_CMD_STATUS_MEM		(1U<<3)
#define  ATI_REG_CMD_SPDF_OUT_EN	(1U<<4)
#define  ATI_REG_CMD_SPDF_STATUS_MEM	(1U<<5)
#define  ATI_REG_CMD_SPDF_THRESHOLD	(3U<<6)
#define  ATI_REG_CMD_SPDF_THRESHOLD_SHIFT	6
#define  ATI_REG_CMD_IN_DMA_EN		(1U<<8)
#define  ATI_REG_CMD_OUT_DMA_EN		(1U<<9)
#define  ATI_REG_CMD_SPDF_DMA_EN	(1U<<10)
#define  ATI_REG_CMD_SPDF_OUT_STOPPED	(1U<<11)
#define  ATI_REG_CMD_SPDF_CONFIG_MASK	(7U<<12)
#define   ATI_REG_CMD_SPDF_CONFIG_34	(1U<<12)
#define   ATI_REG_CMD_SPDF_CONFIG_78	(2U<<12)
#define   ATI_REG_CMD_SPDF_CONFIG_69	(3U<<12)
#define   ATI_REG_CMD_SPDF_CONFIG_01	(4U<<12)
#define  ATI_REG_CMD_INTERLEAVE_SPDF	(1U<<16)
#define  ATI_REG_CMD_AUDIO_PRESENT	(1U<<20)
#define  ATI_REG_CMD_INTERLEAVE_IN	(1U<<21)
#define  ATI_REG_CMD_INTERLEAVE_OUT	(1U<<22)
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

#define ATI_REG_IN_DMA_LINKPTR		0x20
#define ATI_REG_IN_DMA_DT_START		0x24	/* RO */
#define ATI_REG_IN_DMA_DT_NEXT		0x28	/* RO */
#define ATI_REG_IN_DMA_DT_CUR		0x2c	/* RO */
#define ATI_REG_IN_DMA_DT_SIZE		0x30

#define ATI_REG_OUT_DMA_SLOT		0x34
#define  ATI_REG_OUT_DMA_SLOT_BIT(x)	(1U << ((x) - 3))
#define  ATI_REG_OUT_DMA_SLOT_MASK	0x1ff
#define  ATI_REG_OUT_DMA_THRESHOLD_MASK	0xf800
#define  ATI_REG_OUT_DMA_THRESHOLD_SHIFT	11

#define ATI_REG_OUT_DMA_LINKPTR		0x38
#define ATI_REG_OUT_DMA_DT_START	0x3c	/* RO */
#define ATI_REG_OUT_DMA_DT_NEXT		0x40	/* RO */
#define ATI_REG_OUT_DMA_DT_CUR		0x44	/* RO */
#define ATI_REG_OUT_DMA_DT_SIZE		0x48

#define ATI_REG_SPDF_CMD		0x4c
#define  ATI_REG_SPDF_CMD_LFSR		(1U<<4)
#define  ATI_REG_SPDF_CMD_SINGLE_CH	(1U<<5)
#define  ATI_REG_SPDF_CMD_LFSR_ACC	(0xff<<8)	/* RO */

#define ATI_REG_SPDF_DMA_LINKPTR	0x50
#define ATI_REG_SPDF_DMA_DT_START	0x54	/* RO */
#define ATI_REG_SPDF_DMA_DT_NEXT	0x58	/* RO */
#define ATI_REG_SPDF_DMA_DT_CUR		0x5c	/* RO */
#define ATI_REG_SPDF_DMA_DT_SIZE	0x60

#define ATI_REG_MODEM_MIRROR		0x7c
#define ATI_REG_AUDIO_MIRROR		0x80

#define ATI_REG_6CH_REORDER		0x84	/* reorder slots for 6ch */
#define  ATI_REG_6CH_REORDER_EN		(1U<<0)	/* 3,4,7,8,6,9 -> 3,4,6,9,7,8 */

#define ATI_REG_FIFO_FLUSH		0x88
#define  ATI_REG_FIFO_OUT_FLUSH		(1U<<0)
#define  ATI_REG_FIFO_IN_FLUSH		(1U<<1)

/* LINKPTR */
#define  ATI_REG_LINKPTR_EN		(1U<<0)

/* [INT|OUT|SPDIF]_DMA_DT_SIZE */
#define  ATI_REG_DMA_DT_SIZE		(0xffffU<<0)
#define  ATI_REG_DMA_FIFO_USED		(0x1fU<<16)
#define  ATI_REG_DMA_FIFO_FREE		(0x1fU<<21)
#define  ATI_REG_DMA_STATE		(7U<<26)


#define ATI_MAX_DESCRIPTORS	256	/* max number of descriptor packets */


/*
 */

typedef struct snd_atiixp atiixp_t;
typedef struct snd_atiixp_dma atiixp_dma_t;
typedef struct snd_atiixp_dma_ops atiixp_dma_ops_t;


/*
 * DMA packate descriptor
 */

typedef struct atiixp_dma_desc {
	u32 addr;	/* DMA buffer address */
	u16 status;	/* status bits */
	u16 size;	/* size of the packet in dwords */
	u32 next;	/* address of the next packet descriptor */
} atiixp_dma_desc_t;

/*
 * stream enum
 */
enum { ATI_DMA_PLAYBACK, ATI_DMA_CAPTURE, ATI_DMA_SPDIF, NUM_ATI_DMAS }; /* DMAs */
enum { ATI_PCM_OUT, ATI_PCM_IN, ATI_PCM_SPDIF, NUM_ATI_PCMS }; /* AC97 pcm slots */
enum { ATI_PCMDEV_ANALOG, ATI_PCMDEV_DIGITAL, NUM_ATI_PCMDEVS }; /* pcm devices */

#define NUM_ATI_CODECS	3


/*
 * constants and callbacks for each DMA type
 */
struct snd_atiixp_dma_ops {
	int type;			/* ATI_DMA_XXX */
	unsigned int llp_offset;	/* LINKPTR offset */
	unsigned int dt_cur;		/* DT_CUR offset */
	void (*enable_dma)(atiixp_t *chip, int on);	/* called from open callback */
	void (*enable_transfer)(atiixp_t *chip, int on); /* called from trigger (START/STOP) */
	void (*flush_dma)(atiixp_t *chip);		/* called from trigger (STOP only) */
};

/*
 * DMA stream
 */
struct snd_atiixp_dma {
	const atiixp_dma_ops_t *ops;
	struct snd_dma_buffer desc_buf;
	snd_pcm_substream_t *substream;	/* assigned PCM substream */
	unsigned int buf_addr, buf_bytes;	/* DMA buffer address, bytes */
	unsigned int period_bytes, periods;
	int opened;
	int running;
	int suspended;
	int pcm_open_flag;
	int ac97_pcm_type;	/* index # of ac97_pcm to access, -1 = not used */
	unsigned int saved_curptr;
};

/*
 * ATI IXP chip
 */
struct snd_atiixp {
	snd_card_t *card;
	struct pci_dev *pci;

	unsigned long addr;
	void __iomem *remap_addr;
	int irq;
	
	ac97_bus_t *ac97_bus;
	ac97_t *ac97[NUM_ATI_CODECS];

	spinlock_t reg_lock;

	atiixp_dma_t dmas[NUM_ATI_DMAS];
	struct ac97_pcm *pcms[NUM_ATI_PCMS];
	snd_pcm_t *pcmdevs[NUM_ATI_PCMDEVS];

	int max_channels;		/* max. channels for PCM out */

	unsigned int codec_not_ready_bits;	/* for codec detection */

	int spdif_over_aclink;		/* passed from the module option */
	struct semaphore open_mutex;	/* playback open mutex */
};


/*
 */
static struct pci_device_id snd_atiixp_ids[] = {
	{ 0x1002, 0x4341, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 }, /* SB200 */
	{ 0x1002, 0x4361, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 }, /* SB300 */
	{ 0x1002, 0x4370, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 }, /* SB400 */
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
static int snd_atiixp_update_bits(atiixp_t *chip, unsigned int reg,
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

/* delay for one tick */
#define do_delay() do { \
	set_current_state(TASK_UNINTERRUPTIBLE); \
	schedule_timeout(1); \
} while (0)


/*
 * handling DMA packets
 *
 * we allocate a linear buffer for the DMA, and split it to  each packet.
 * in a future version, a scatter-gather buffer should be implemented.
 */

#define ATI_DESC_LIST_SIZE \
	PAGE_ALIGN(ATI_MAX_DESCRIPTORS * sizeof(atiixp_dma_desc_t))

/*
 * build packets ring for the given buffer size.
 *
 * IXP handles the buffer descriptors, which are connected as a linked
 * list.  although we can change the list dynamically, in this version,
 * a static RING of buffer descriptors is used.
 *
 * the ring is built in this function, and is set up to the hardware. 
 */
static int atiixp_build_dma_packets(atiixp_t *chip, atiixp_dma_t *dma,
				   snd_pcm_substream_t *substream,
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
		atiixp_dma_desc_t *desc = &((atiixp_dma_desc_t *)dma->desc_buf.area)[i];
		desc->addr = cpu_to_le32(addr);
		desc->status = 0;
		desc->size = period_bytes >> 2; /* in dwords */
		desc_addr += sizeof(atiixp_dma_desc_t);
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
static void atiixp_clear_dma_packets(atiixp_t *chip, atiixp_dma_t *dma, snd_pcm_substream_t *substream)
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
static int snd_atiixp_acquire_codec(atiixp_t *chip)
{
	int timeout = 1000;

	while (atiixp_read(chip, PHYS_OUT_ADDR) & ATI_REG_PHYS_OUT_ADDR_EN) {
		if (! timeout--) {
			snd_printk(KERN_WARNING "atiixp: codec acquire timeout\n");
			return -EBUSY;
		}
		udelay(1);
	}
	return 0;
}

static unsigned short snd_atiixp_codec_read(atiixp_t *chip, unsigned short codec, unsigned short reg)
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
		snd_printk(KERN_WARNING "atiixp: codec read timeout (reg %x)\n", reg);
	return 0xffff;
}


static void snd_atiixp_codec_write(atiixp_t *chip, unsigned short codec, unsigned short reg, unsigned short val)
{
	unsigned int data;
    
	if (snd_atiixp_acquire_codec(chip) < 0)
		return;
	data = ((unsigned int)val << ATI_REG_PHYS_OUT_DATA_SHIFT) |
		((unsigned int)reg << ATI_REG_PHYS_OUT_ADDR_SHIFT) |
		ATI_REG_PHYS_OUT_ADDR_EN | codec;
	atiixp_write(chip, PHYS_OUT_ADDR, data);
}


static unsigned short snd_atiixp_ac97_read(ac97_t *ac97, unsigned short reg)
{
	atiixp_t *chip = ac97->private_data;
	return snd_atiixp_codec_read(chip, ac97->num, reg);
    
}

static void snd_atiixp_ac97_write(ac97_t *ac97, unsigned short reg, unsigned short val)
{
	atiixp_t *chip = ac97->private_data;
	snd_atiixp_codec_write(chip, ac97->num, reg, val);
}

/*
 * reset AC link
 */
static int snd_atiixp_aclink_reset(atiixp_t *chip)
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
		do_delay();
		atiixp_update(chip, CMD, ATI_REG_CMD_AC_RESET, ATI_REG_CMD_AC_RESET);
		if (--timeout) {
			snd_printk(KERN_ERR "atiixp: codec reset timeout\n");
			break;
		}
	}

	/* deassert RESET and assert SYNC to make sure */
	atiixp_update(chip, CMD, ATI_REG_CMD_AC_SYNC|ATI_REG_CMD_AC_RESET,
		      ATI_REG_CMD_AC_SYNC|ATI_REG_CMD_AC_RESET);

	return 0;
}

#ifdef CONFIG_PM
static int snd_atiixp_aclink_down(atiixp_t *chip)
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

static int snd_atiixp_codec_detect(atiixp_t *chip)
{
	int timeout;

	chip->codec_not_ready_bits = 0;
	atiixp_write(chip, IER, CODEC_CHECK_BITS);
	/* wait for the interrupts */
	timeout = HZ / 10;
	while (timeout-- > 0) {
		do_delay();
		if (chip->codec_not_ready_bits)
			break;
	}
	atiixp_write(chip, IER, 0); /* disable irqs */

	if ((chip->codec_not_ready_bits & ALL_CODEC_NOT_READY) == ALL_CODEC_NOT_READY) {
		snd_printk(KERN_ERR "atiixp: no codec detected!\n");
		return -ENXIO;
	}
	return 0;
}


/*
 * enable DMA and irqs
 */
static int snd_atiixp_chip_start(atiixp_t *chip)
{
	unsigned int reg;

	/* set up spdif, enable burst mode */
	reg = atiixp_read(chip, CMD);
	reg |= 0x02 << ATI_REG_CMD_SPDF_THRESHOLD_SHIFT;
	reg |= ATI_REG_CMD_BURST_EN;
	atiixp_write(chip, CMD, reg);

	reg = atiixp_read(chip, SPDF_CMD);
	reg &= ~(ATI_REG_SPDF_CMD_LFSR|ATI_REG_SPDF_CMD_SINGLE_CH);
	atiixp_write(chip, SPDF_CMD, reg);

	/* clear all interrupt source */
	atiixp_write(chip, ISR, 0xffffffff);
	/* enable irqs */
	atiixp_write(chip, IER,
		     ATI_REG_IER_IO_STATUS_EN |
		     ATI_REG_IER_IN_XRUN_EN |
		     ATI_REG_IER_OUT_XRUN_EN |
		     ATI_REG_IER_SPDF_XRUN_EN |
		     ATI_REG_IER_SPDF_STATUS_EN);
	return 0;
}


/*
 * disable DMA and IRQs
 */
static int snd_atiixp_chip_stop(atiixp_t *chip)
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
static snd_pcm_uframes_t snd_atiixp_pcm_pointer(snd_pcm_substream_t *substream)
{
	atiixp_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	atiixp_dma_t *dma = (atiixp_dma_t *)runtime->private_data;
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
	snd_printd("atiixp: invalid DMA pointer read 0x%x (buf=%x)\n",
		   readl(chip->remap_addr + dma->ops->dt_cur), dma->buf_addr);
	return 0;
}

/*
 * XRUN detected, and stop the PCM substream
 */
static void snd_atiixp_xrun_dma(atiixp_t *chip, atiixp_dma_t *dma)
{
	if (! dma->substream || ! dma->running)
		return;
	snd_printdd("atiixp: XRUN detected (DMA %d)\n", dma->ops->type);
	snd_pcm_stop(dma->substream, SNDRV_PCM_STATE_XRUN);
}

/*
 * the period ack.  update the substream.
 */
static void snd_atiixp_update_dma(atiixp_t *chip, atiixp_dma_t *dma)
{
	if (! dma->substream || ! dma->running)
		return;
	snd_pcm_period_elapsed(dma->substream);
}

/* set BUS_BUSY interrupt bit if any DMA is running */
/* call with spinlock held */
static void snd_atiixp_check_bus_busy(atiixp_t *chip)
{
	unsigned int bus_busy;
	if (atiixp_read(chip, CMD) & (ATI_REG_CMD_SEND_EN |
				      ATI_REG_CMD_RECEIVE_EN |
				      ATI_REG_CMD_SPDF_OUT_EN))
		bus_busy = ATI_REG_IER_SET_BUS_BUSY;
	else
		bus_busy = 0;
	atiixp_update(chip, IER, ATI_REG_IER_SET_BUS_BUSY, bus_busy);
}

/* common trigger callback
 * calling the lowlevel callbacks in it
 */
static int snd_atiixp_pcm_trigger(snd_pcm_substream_t *substream, int cmd)
{
	atiixp_t *chip = snd_pcm_substream_chip(substream);
	atiixp_dma_t *dma = (atiixp_dma_t *)substream->runtime->private_data;
	int err = 0;

	snd_assert(dma->ops->enable_transfer && dma->ops->flush_dma, return -EINVAL);

	spin_lock(&chip->reg_lock);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
		dma->ops->enable_transfer(chip, 1);
		dma->running = 1;
		dma->suspended = 0;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		dma->ops->enable_transfer(chip, 0);
		dma->running = 0;
		dma->suspended = cmd == SNDRV_PCM_TRIGGER_SUSPEND;
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
static void atiixp_out_flush_dma(atiixp_t *chip)
{
	atiixp_write(chip, FIFO_FLUSH, ATI_REG_FIFO_OUT_FLUSH);
}

/* enable/disable analog OUT DMA */
static void atiixp_out_enable_dma(atiixp_t *chip, int on)
{
	unsigned int data;
	data = atiixp_read(chip, CMD);
	if (on) {
		if (data & ATI_REG_CMD_OUT_DMA_EN)
			return;
		atiixp_out_flush_dma(chip);
		data |= ATI_REG_CMD_OUT_DMA_EN;
	} else
		data &= ~ATI_REG_CMD_OUT_DMA_EN;
	atiixp_write(chip, CMD, data);
}

/* start/stop transfer over OUT DMA */
static void atiixp_out_enable_transfer(atiixp_t *chip, int on)
{
	atiixp_update(chip, CMD, ATI_REG_CMD_SEND_EN,
		      on ? ATI_REG_CMD_SEND_EN : 0);
}

/* enable/disable analog IN DMA */
static void atiixp_in_enable_dma(atiixp_t *chip, int on)
{
	atiixp_update(chip, CMD, ATI_REG_CMD_IN_DMA_EN,
		      on ? ATI_REG_CMD_IN_DMA_EN : 0);
}

/* start/stop analog IN DMA */
static void atiixp_in_enable_transfer(atiixp_t *chip, int on)
{
	if (on) {
		unsigned int data = atiixp_read(chip, CMD);
		if (! (data & ATI_REG_CMD_RECEIVE_EN)) {
			data |= ATI_REG_CMD_RECEIVE_EN;
#if 0 /* FIXME: this causes the endless loop */
			/* wait until slot 3/4 are finished */
			while ((atiixp_read(chip, COUNTER) &
				ATI_REG_COUNTER_SLOT) != 5)
				;
#endif
			atiixp_write(chip, CMD, data);
		}
	} else
		atiixp_update(chip, CMD, ATI_REG_CMD_RECEIVE_EN, 0);
}

/* flush FIFO of analog IN DMA */
static void atiixp_in_flush_dma(atiixp_t *chip)
{
	atiixp_write(chip, FIFO_FLUSH, ATI_REG_FIFO_IN_FLUSH);
}

/* enable/disable SPDIF OUT DMA */
static void atiixp_spdif_enable_dma(atiixp_t *chip, int on)
{
	atiixp_update(chip, CMD, ATI_REG_CMD_SPDF_DMA_EN,
		      on ? ATI_REG_CMD_SPDF_DMA_EN : 0);
}

/* start/stop SPDIF OUT DMA */
static void atiixp_spdif_enable_transfer(atiixp_t *chip, int on)
{
	unsigned int data;
	data = atiixp_read(chip, CMD);
	if (on)
		data |= ATI_REG_CMD_SPDF_OUT_EN;
	else
		data &= ~ATI_REG_CMD_SPDF_OUT_EN;
	atiixp_write(chip, CMD, data);
}

/* flush FIFO of SPDIF OUT DMA */
static void atiixp_spdif_flush_dma(atiixp_t *chip)
{
	int timeout;

	/* DMA off, transfer on */
	atiixp_spdif_enable_dma(chip, 0);
	atiixp_spdif_enable_transfer(chip, 1);
	
	timeout = 100;
	do {
		if (! (atiixp_read(chip, SPDF_DMA_DT_SIZE) & ATI_REG_DMA_FIFO_USED))
			break;
		udelay(1);
	} while (timeout-- > 0);

	atiixp_spdif_enable_transfer(chip, 0);
}

/* set up slots and formats for SPDIF OUT */
static int snd_atiixp_spdif_prepare(snd_pcm_substream_t *substream)
{
	atiixp_t *chip = snd_pcm_substream_chip(substream);

	spin_lock_irq(&chip->reg_lock);
	if (chip->spdif_over_aclink) {
		unsigned int data;
		/* enable slots 10/11 */
		atiixp_update(chip, CMD, ATI_REG_CMD_SPDF_CONFIG_MASK,
			      ATI_REG_CMD_SPDF_CONFIG_01);
		data = atiixp_read(chip, OUT_DMA_SLOT) & ~ATI_REG_OUT_DMA_SLOT_MASK;
		data |= ATI_REG_OUT_DMA_SLOT_BIT(10) |
			ATI_REG_OUT_DMA_SLOT_BIT(11);
		data |= 0x04 << ATI_REG_OUT_DMA_THRESHOLD_SHIFT;
		atiixp_write(chip, OUT_DMA_SLOT, data);
		atiixp_update(chip, CMD, ATI_REG_CMD_INTERLEAVE_OUT,
			      substream->runtime->format == SNDRV_PCM_FORMAT_S16_LE ?
			      ATI_REG_CMD_INTERLEAVE_OUT : 0);
	} else {
		atiixp_update(chip, CMD, ATI_REG_CMD_SPDF_CONFIG_MASK, 0);
		atiixp_update(chip, CMD, ATI_REG_CMD_INTERLEAVE_SPDF, 0);
	}
	spin_unlock_irq(&chip->reg_lock);
	return 0;
}

/* set up slots and formats for analog OUT */
static int snd_atiixp_playback_prepare(snd_pcm_substream_t *substream)
{
	atiixp_t *chip = snd_pcm_substream_chip(substream);
	unsigned int data;

	spin_lock_irq(&chip->reg_lock);
	data = atiixp_read(chip, OUT_DMA_SLOT) & ~ATI_REG_OUT_DMA_SLOT_MASK;
	switch (substream->runtime->channels) {
	case 8:
		data |= ATI_REG_OUT_DMA_SLOT_BIT(10) |
			ATI_REG_OUT_DMA_SLOT_BIT(11);
		/* fallthru */
	case 6:
		data |= ATI_REG_OUT_DMA_SLOT_BIT(7) |
			ATI_REG_OUT_DMA_SLOT_BIT(8);
		/* fallthru */
	case 4:
		data |= ATI_REG_OUT_DMA_SLOT_BIT(6) |
			ATI_REG_OUT_DMA_SLOT_BIT(9);
		/* fallthru */
	default:
		data |= ATI_REG_OUT_DMA_SLOT_BIT(3) |
			ATI_REG_OUT_DMA_SLOT_BIT(4);
		break;
	}

	/* set output threshold */
	data |= 0x04 << ATI_REG_OUT_DMA_THRESHOLD_SHIFT;
	atiixp_write(chip, OUT_DMA_SLOT, data);

	atiixp_update(chip, CMD, ATI_REG_CMD_INTERLEAVE_OUT,
		      substream->runtime->format == SNDRV_PCM_FORMAT_S16_LE ?
		      ATI_REG_CMD_INTERLEAVE_OUT : 0);

	/*
	 * enable 6 channel re-ordering bit if needed
	 */
	atiixp_update(chip, 6CH_REORDER, ATI_REG_6CH_REORDER_EN,
		      substream->runtime->channels >= 6 ? ATI_REG_6CH_REORDER_EN: 0);
    
	spin_unlock_irq(&chip->reg_lock);
	return 0;
}

/* set up slots and formats for analog IN */
static int snd_atiixp_capture_prepare(snd_pcm_substream_t *substream)
{
	atiixp_t *chip = snd_pcm_substream_chip(substream);

	spin_lock_irq(&chip->reg_lock);
	atiixp_update(chip, CMD, ATI_REG_CMD_INTERLEAVE_IN,
		      substream->runtime->format == SNDRV_PCM_FORMAT_S16_LE ?
		      ATI_REG_CMD_INTERLEAVE_IN : 0);
	spin_unlock_irq(&chip->reg_lock);
	return 0;
}

/*
 * hw_params - allocate the buffer and set up buffer descriptors
 */
static int snd_atiixp_pcm_hw_params(snd_pcm_substream_t *substream,
				   snd_pcm_hw_params_t *hw_params)
{
	atiixp_t *chip = snd_pcm_substream_chip(substream);
	atiixp_dma_t *dma = (atiixp_dma_t *)substream->runtime->private_data;
	int err;

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

	if (dma->ac97_pcm_type >= 0) {
		struct ac97_pcm *pcm = chip->pcms[dma->ac97_pcm_type];
		/* PCM is bound to AC97 codec(s)
		 * set up the AC97 codecs
		 */
		if (dma->pcm_open_flag) {
			snd_ac97_pcm_close(pcm);
			dma->pcm_open_flag = 0;
		}
		err = snd_ac97_pcm_open(pcm, params_rate(hw_params),
					params_channels(hw_params),
					pcm->r[0].slots);
		if (err >= 0)
			dma->pcm_open_flag = 1;
	}

	return err;
}

static int snd_atiixp_pcm_hw_free(snd_pcm_substream_t * substream)
{
	atiixp_t *chip = snd_pcm_substream_chip(substream);
	atiixp_dma_t *dma = (atiixp_dma_t *)substream->runtime->private_data;

	if (dma->pcm_open_flag) {
		struct ac97_pcm *pcm = chip->pcms[dma->ac97_pcm_type];
		snd_ac97_pcm_close(pcm);
		dma->pcm_open_flag = 0;
	}
	atiixp_clear_dma_packets(chip, dma, substream);
	snd_pcm_lib_free_pages(substream);
	return 0;
}


/*
 * pcm hardware definition, identical for all DMA types
 */
static snd_pcm_hardware_t snd_atiixp_pcm_hw =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_PAUSE |
				 SNDRV_PCM_INFO_RESUME |
				 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S32_LE,
	.rates =		SNDRV_PCM_RATE_48000,
	.rate_min =		48000,
	.rate_max =		48000,
	.channels_min =		2,
	.channels_max =		2,
	.buffer_bytes_max =	256 * 1024,
	.period_bytes_min =	32,
	.period_bytes_max =	128 * 1024,
	.periods_min =		2,
	.periods_max =		ATI_MAX_DESCRIPTORS,
};

static int snd_atiixp_pcm_open(snd_pcm_substream_t *substream, atiixp_dma_t *dma, int pcm_type)
{
	atiixp_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	int err;

	snd_assert(dma->ops && dma->ops->enable_dma, return -EINVAL);

	if (dma->opened)
		return -EBUSY;
	dma->substream = substream;
	runtime->hw = snd_atiixp_pcm_hw;
	dma->ac97_pcm_type = pcm_type;
	if (pcm_type >= 0) {
		runtime->hw.rates = chip->pcms[pcm_type]->rates;
		snd_pcm_limit_hw_rates(runtime);
	} else {
		/* direct SPDIF */
		runtime->hw.formats = SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_LE;
	}
	if ((err = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS)) < 0)
		return err;
	runtime->private_data = dma;

	/* enable DMA bits */
	spin_lock_irq(&chip->reg_lock);
	dma->ops->enable_dma(chip, 1);
	spin_unlock_irq(&chip->reg_lock);
	dma->opened = 1;

	return 0;
}

static int snd_atiixp_pcm_close(snd_pcm_substream_t *substream, atiixp_dma_t *dma)
{
	atiixp_t *chip = snd_pcm_substream_chip(substream);
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
static int snd_atiixp_playback_open(snd_pcm_substream_t *substream)
{
	atiixp_t *chip = snd_pcm_substream_chip(substream);
	int err;

	down(&chip->open_mutex);
	err = snd_atiixp_pcm_open(substream, &chip->dmas[ATI_DMA_PLAYBACK], 0);
	up(&chip->open_mutex);
	if (err < 0)
		return err;
	substream->runtime->hw.channels_max = chip->max_channels;
	if (chip->max_channels > 2)
		/* channels must be even */
		snd_pcm_hw_constraint_step(substream->runtime, 0,
					   SNDRV_PCM_HW_PARAM_CHANNELS, 2);
	return 0;
}

static int snd_atiixp_playback_close(snd_pcm_substream_t *substream)
{
	atiixp_t *chip = snd_pcm_substream_chip(substream);
	int err;
	down(&chip->open_mutex);
	err = snd_atiixp_pcm_close(substream, &chip->dmas[ATI_DMA_PLAYBACK]);
	up(&chip->open_mutex);
	return err;
}

static int snd_atiixp_capture_open(snd_pcm_substream_t *substream)
{
	atiixp_t *chip = snd_pcm_substream_chip(substream);
	return snd_atiixp_pcm_open(substream, &chip->dmas[ATI_DMA_CAPTURE], 1);
}

static int snd_atiixp_capture_close(snd_pcm_substream_t *substream)
{
	atiixp_t *chip = snd_pcm_substream_chip(substream);
	return snd_atiixp_pcm_close(substream, &chip->dmas[ATI_DMA_CAPTURE]);
}

static int snd_atiixp_spdif_open(snd_pcm_substream_t *substream)
{
	atiixp_t *chip = snd_pcm_substream_chip(substream);
	int err;
	down(&chip->open_mutex);
	if (chip->spdif_over_aclink) /* share DMA_PLAYBACK */
		err = snd_atiixp_pcm_open(substream, &chip->dmas[ATI_DMA_PLAYBACK], 2);
	else
		err = snd_atiixp_pcm_open(substream, &chip->dmas[ATI_DMA_SPDIF], -1);
	up(&chip->open_mutex);
	return err;
}

static int snd_atiixp_spdif_close(snd_pcm_substream_t *substream)
{
	atiixp_t *chip = snd_pcm_substream_chip(substream);
	int err;
	down(&chip->open_mutex);
	if (chip->spdif_over_aclink)
		err = snd_atiixp_pcm_close(substream, &chip->dmas[ATI_DMA_PLAYBACK]);
	else
		err = snd_atiixp_pcm_close(substream, &chip->dmas[ATI_DMA_SPDIF]);
	up(&chip->open_mutex);
	return err;
}

/* AC97 playback */
static snd_pcm_ops_t snd_atiixp_playback_ops = {
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
static snd_pcm_ops_t snd_atiixp_capture_ops = {
	.open =		snd_atiixp_capture_open,
	.close =	snd_atiixp_capture_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_atiixp_pcm_hw_params,
	.hw_free =	snd_atiixp_pcm_hw_free,
	.prepare =	snd_atiixp_capture_prepare,
	.trigger =	snd_atiixp_pcm_trigger,
	.pointer =	snd_atiixp_pcm_pointer,
};

/* SPDIF playback */
static snd_pcm_ops_t snd_atiixp_spdif_ops = {
	.open =		snd_atiixp_spdif_open,
	.close =	snd_atiixp_spdif_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_atiixp_pcm_hw_params,
	.hw_free =	snd_atiixp_pcm_hw_free,
	.prepare =	snd_atiixp_spdif_prepare,
	.trigger =	snd_atiixp_pcm_trigger,
	.pointer =	snd_atiixp_pcm_pointer,
};

static struct ac97_pcm atiixp_pcm_defs[] __devinitdata = {
	/* front PCM */
	{
		.exclusive = 1,
		.r = {	{
				.slots = (1 << AC97_SLOT_PCM_LEFT) |
					 (1 << AC97_SLOT_PCM_RIGHT) |
					 (1 << AC97_SLOT_PCM_CENTER) |
					 (1 << AC97_SLOT_PCM_SLEFT) |
					 (1 << AC97_SLOT_PCM_SRIGHT) |
					 (1 << AC97_SLOT_LFE)
			}
		}
	},
	/* PCM IN #1 */
	{
		.stream = 1,
		.exclusive = 1,
		.r = {	{
				.slots = (1 << AC97_SLOT_PCM_LEFT) |
					 (1 << AC97_SLOT_PCM_RIGHT)
			}
		}
	},
	/* S/PDIF OUT (optional) */
	{
		.exclusive = 1,
		.spdif = 1,
		.r = {	{
				.slots = (1 << AC97_SLOT_SPDIF_LEFT2) |
					 (1 << AC97_SLOT_SPDIF_RIGHT2)
			}
		}
	},
};

static atiixp_dma_ops_t snd_atiixp_playback_dma_ops = {
	.type = ATI_DMA_PLAYBACK,
	.llp_offset = ATI_REG_OUT_DMA_LINKPTR,
	.dt_cur = ATI_REG_OUT_DMA_DT_CUR,
	.enable_dma = atiixp_out_enable_dma,
	.enable_transfer = atiixp_out_enable_transfer,
	.flush_dma = atiixp_out_flush_dma,
};
	
static atiixp_dma_ops_t snd_atiixp_capture_dma_ops = {
	.type = ATI_DMA_CAPTURE,
	.llp_offset = ATI_REG_IN_DMA_LINKPTR,
	.dt_cur = ATI_REG_IN_DMA_DT_CUR,
	.enable_dma = atiixp_in_enable_dma,
	.enable_transfer = atiixp_in_enable_transfer,
	.flush_dma = atiixp_in_flush_dma,
};
	
static atiixp_dma_ops_t snd_atiixp_spdif_dma_ops = {
	.type = ATI_DMA_SPDIF,
	.llp_offset = ATI_REG_SPDF_DMA_LINKPTR,
	.dt_cur = ATI_REG_SPDF_DMA_DT_CUR,
	.enable_dma = atiixp_spdif_enable_dma,
	.enable_transfer = atiixp_spdif_enable_transfer,
	.flush_dma = atiixp_spdif_flush_dma,
};
	

static int __devinit snd_atiixp_pcm_new(atiixp_t *chip)
{
	snd_pcm_t *pcm;
	ac97_bus_t *pbus = chip->ac97_bus;
	int err, i, num_pcms;

	/* initialize constants */
	chip->dmas[ATI_DMA_PLAYBACK].ops = &snd_atiixp_playback_dma_ops;
	chip->dmas[ATI_DMA_CAPTURE].ops = &snd_atiixp_capture_dma_ops;
	if (! chip->spdif_over_aclink)
		chip->dmas[ATI_DMA_SPDIF].ops = &snd_atiixp_spdif_dma_ops;

	/* assign AC97 pcm */
	if (chip->spdif_over_aclink)
		num_pcms = 3;
	else
		num_pcms = 2;
	err = snd_ac97_pcm_assign(pbus, num_pcms, atiixp_pcm_defs);
	if (err < 0)
		return err;
	for (i = 0; i < num_pcms; i++)
		chip->pcms[i] = &pbus->pcms[i];

	chip->max_channels = 2;
	if (pbus->pcms[ATI_PCM_OUT].r[0].slots & (1 << AC97_SLOT_PCM_SLEFT)) {
		if (pbus->pcms[ATI_PCM_OUT].r[0].slots & (1 << AC97_SLOT_LFE))
			chip->max_channels = 6;
		else
			chip->max_channels = 4;
	}

	/* PCM #0: analog I/O */
	err = snd_pcm_new(chip->card, "ATI IXP AC97", ATI_PCMDEV_ANALOG, 1, 1, &pcm);
	if (err < 0)
		return err;
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_atiixp_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_atiixp_capture_ops);
	pcm->private_data = chip;
	strcpy(pcm->name, "ATI IXP AC97");
	chip->pcmdevs[ATI_PCMDEV_ANALOG] = pcm;

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
					      snd_dma_pci_data(chip->pci), 64*1024, 128*1024);

	/* no SPDIF support on codec? */
	if (chip->pcms[ATI_PCM_SPDIF] && ! chip->pcms[ATI_PCM_SPDIF]->rates)
		return 0;
		
	/* FIXME: non-48k sample rate doesn't work on my test machine with AD1888 */
	if (chip->pcms[ATI_PCM_SPDIF])
		chip->pcms[ATI_PCM_SPDIF]->rates = SNDRV_PCM_RATE_48000;

	/* PCM #1: spdif playback */
	err = snd_pcm_new(chip->card, "ATI IXP IEC958", ATI_PCMDEV_DIGITAL, 1, 0, &pcm);
	if (err < 0)
		return err;
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_atiixp_spdif_ops);
	pcm->private_data = chip;
	if (chip->spdif_over_aclink)
		strcpy(pcm->name, "ATI IXP IEC958 (AC97)");
	else
		strcpy(pcm->name, "ATI IXP IEC958 (Direct)");
	chip->pcmdevs[ATI_PCMDEV_DIGITAL] = pcm;

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
					      snd_dma_pci_data(chip->pci), 64*1024, 128*1024);

	/* pre-select AC97 SPDIF slots 10/11 */
	for (i = 0; i < NUM_ATI_CODECS; i++) {
		if (chip->ac97[i])
			snd_ac97_update_bits(chip->ac97[i], AC97_EXTENDED_STATUS, 0x03 << 4, 0x03 << 4);
	}

	return 0;
}



/*
 * interrupt handler
 */
static irqreturn_t snd_atiixp_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	atiixp_t *chip = dev_id;
	unsigned int status;

	status = atiixp_read(chip, ISR);

	if (! status)
		return IRQ_NONE;

	/* process audio DMA */
	if (status & ATI_REG_ISR_OUT_XRUN)
		snd_atiixp_xrun_dma(chip,  &chip->dmas[ATI_DMA_PLAYBACK]);
	else if (status & ATI_REG_ISR_OUT_STATUS)
		snd_atiixp_update_dma(chip, &chip->dmas[ATI_DMA_PLAYBACK]);
	if (status & ATI_REG_ISR_IN_XRUN)
		snd_atiixp_xrun_dma(chip,  &chip->dmas[ATI_DMA_CAPTURE]);
	else if (status & ATI_REG_ISR_IN_STATUS)
		snd_atiixp_update_dma(chip, &chip->dmas[ATI_DMA_CAPTURE]);
	if (! chip->spdif_over_aclink) {
		if (status & ATI_REG_ISR_SPDF_XRUN)
			snd_atiixp_xrun_dma(chip,  &chip->dmas[ATI_DMA_SPDIF]);
		else if (status & ATI_REG_ISR_SPDF_STATUS)
			snd_atiixp_update_dma(chip, &chip->dmas[ATI_DMA_SPDIF]);
	}

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

static struct ac97_quirk ac97_quirks[] __devinitdata = {
	{
		.subvendor = 0x103c,
		.subdevice = 0x006b,
		.name = "HP Pavilion ZV5030US",
		.type = AC97_TUNE_MUTE_LED
	},
	{ } /* terminator */
};

static int __devinit snd_atiixp_mixer_new(atiixp_t *chip, int clock, const char *quirk_override)
{
	ac97_bus_t *pbus;
	ac97_template_t ac97;
	int i, err;
	int codec_count;
	static ac97_bus_ops_t ops = {
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
	pbus->shared_type = AC97_SHARED_TYPE_ATIIXP;	/* shared with modem driver */
	chip->ac97_bus = pbus;

	codec_count = 0;
	for (i = 0; i < NUM_ATI_CODECS; i++) {
		if (chip->codec_not_ready_bits & codec_skip[i])
			continue;
		memset(&ac97, 0, sizeof(ac97));
		ac97.private_data = chip;
		ac97.pci = chip->pci;
		ac97.num = i;
		ac97.scaps = AC97_SCAP_SKIP_MODEM;
		if (! chip->spdif_over_aclink)
			ac97.scaps |= AC97_SCAP_NO_SPDIF;
		if ((err = snd_ac97_mixer(pbus, &ac97, &chip->ac97[i])) < 0) {
			chip->ac97[i] = NULL; /* to be sure */
			snd_printdd("atiixp: codec %d not available for audio\n", i);
			continue;
		}
		codec_count++;
	}

	if (! codec_count) {
		snd_printk(KERN_ERR "atiixp: no codec available\n");
		return -ENODEV;
	}

	snd_ac97_tune_hardware(chip->ac97[0], ac97_quirks, quirk_override);

	return 0;
}


#ifdef CONFIG_PM
/*
 * power management
 */
static int snd_atiixp_suspend(snd_card_t *card, pm_message_t state)
{
	atiixp_t *chip = card->pm_private_data;
	int i;

	for (i = 0; i < NUM_ATI_PCMDEVS; i++)
		if (chip->pcmdevs[i]) {
			atiixp_dma_t *dma = &chip->dmas[i];
			if (dma->substream && dma->running)
				dma->saved_curptr = readl(chip->remap_addr + dma->ops->dt_cur);
			snd_pcm_suspend_all(chip->pcmdevs[i]);
		}
	for (i = 0; i < NUM_ATI_CODECS; i++)
		if (chip->ac97[i])
			snd_ac97_suspend(chip->ac97[i]);
	snd_atiixp_aclink_down(chip);
	snd_atiixp_chip_stop(chip);

	pci_set_power_state(chip->pci, PCI_D3hot);
	pci_disable_device(chip->pci);
	return 0;
}

static int snd_atiixp_resume(snd_card_t *card)
{
	atiixp_t *chip = card->pm_private_data;
	int i;

	pci_enable_device(chip->pci);
	pci_set_power_state(chip->pci, PCI_D0);
	pci_set_master(chip->pci);

	snd_atiixp_aclink_reset(chip);
	snd_atiixp_chip_start(chip);

	for (i = 0; i < NUM_ATI_CODECS; i++)
		if (chip->ac97[i])
			snd_ac97_resume(chip->ac97[i]);

	for (i = 0; i < NUM_ATI_PCMDEVS; i++)
		if (chip->pcmdevs[i]) {
			atiixp_dma_t *dma = &chip->dmas[i];
			if (dma->substream && dma->suspended) {
				dma->ops->enable_dma(chip, 1);
				dma->substream->ops->prepare(dma->substream);
				writel((u32)dma->desc_buf.addr | ATI_REG_LINKPTR_EN,
				       chip->remap_addr + dma->ops->llp_offset);
				writel(dma->saved_curptr, chip->remap_addr + dma->ops->dt_cur);
			}
		}

	return 0;
}
#endif /* CONFIG_PM */


/*
 * proc interface for register dump
 */

static void snd_atiixp_proc_read(snd_info_entry_t *entry, snd_info_buffer_t *buffer)
{
	atiixp_t *chip = entry->private_data;
	int i;

	for (i = 0; i < 256; i += 4)
		snd_iprintf(buffer, "%02x: %08x\n", i, readl(chip->remap_addr + i));
}

static void __devinit snd_atiixp_proc_init(atiixp_t *chip)
{
	snd_info_entry_t *entry;

	if (! snd_card_proc_new(chip->card, "atiixp", &entry))
		snd_info_set_text_ops(entry, chip, 1024, snd_atiixp_proc_read);
}



/*
 * destructor
 */

static int snd_atiixp_free(atiixp_t *chip)
{
	if (chip->irq < 0)
		goto __hw_end;
	snd_atiixp_chip_stop(chip);
	synchronize_irq(chip->irq);
      __hw_end:
	if (chip->irq >= 0)
		free_irq(chip->irq, (void *)chip);
	if (chip->remap_addr)
		iounmap(chip->remap_addr);
	pci_release_regions(chip->pci);
	pci_disable_device(chip->pci);
	kfree(chip);
	return 0;
}

static int snd_atiixp_dev_free(snd_device_t *device)
{
	atiixp_t *chip = device->device_data;
	return snd_atiixp_free(chip);
}

/*
 * constructor for chip instance
 */
static int __devinit snd_atiixp_create(snd_card_t *card,
				      struct pci_dev *pci,
				      atiixp_t **r_chip)
{
	static snd_device_ops_t ops = {
		.dev_free =	snd_atiixp_dev_free,
	};
	atiixp_t *chip;
	int err;

	if ((err = pci_enable_device(pci)) < 0)
		return err;

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (chip == NULL) {
		pci_disable_device(pci);
		return -ENOMEM;
	}

	spin_lock_init(&chip->reg_lock);
	init_MUTEX(&chip->open_mutex);
	chip->card = card;
	chip->pci = pci;
	chip->irq = -1;
	if ((err = pci_request_regions(pci, "ATI IXP AC97")) < 0) {
		pci_disable_device(pci);
		kfree(chip);
		return err;
	}
	chip->addr = pci_resource_start(pci, 0);
	chip->remap_addr = ioremap_nocache(chip->addr, pci_resource_len(pci, 0));
	if (chip->remap_addr == NULL) {
		snd_printk(KERN_ERR "AC'97 space ioremap problem\n");
		snd_atiixp_free(chip);
		return -EIO;
	}

	if (request_irq(pci->irq, snd_atiixp_interrupt, SA_INTERRUPT|SA_SHIRQ, card->shortname, (void *)chip)) {
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
	static int dev;
	snd_card_t *card;
	atiixp_t *chip;
	unsigned char revision;
	int err;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[dev]) {
		dev++;
		return -ENOENT;
	}

	card = snd_card_new(index[dev], id[dev], THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;

	pci_read_config_byte(pci, PCI_REVISION_ID, &revision);

	strcpy(card->driver, spdif_aclink[dev] ? "ATIIXP" : "ATIIXP-SPDMA");
	strcpy(card->shortname, "ATI IXP");
	if ((err = snd_atiixp_create(card, pci, &chip)) < 0)
		goto __error;

	if ((err = snd_atiixp_aclink_reset(chip)) < 0)
		goto __error;

	chip->spdif_over_aclink = spdif_aclink[dev];

	if ((err = snd_atiixp_mixer_new(chip, ac97_clock[dev], ac97_quirk[dev])) < 0)
		goto __error;

	if ((err = snd_atiixp_pcm_new(chip)) < 0)
		goto __error;
	
	snd_atiixp_proc_init(chip);

	snd_atiixp_chip_start(chip);

	snprintf(card->longname, sizeof(card->longname),
		 "%s rev %x with %s at %#lx, irq %i", card->shortname, revision,
		 chip->ac97[0] ? snd_ac97_get_short_name(chip->ac97[0]) : "?",
		 chip->addr, chip->irq);

	snd_card_set_pm_callback(card, snd_atiixp_suspend, snd_atiixp_resume, chip);

	if ((err = snd_card_register(card)) < 0)
		goto __error;

	pci_set_drvdata(pci, card);
	dev++;
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
	.name = "ATI IXP AC97 controller",
	.owner = THIS_MODULE,
	.id_table = snd_atiixp_ids,
	.probe = snd_atiixp_probe,
	.remove = __devexit_p(snd_atiixp_remove),
	SND_PCI_PM_CALLBACKS
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
