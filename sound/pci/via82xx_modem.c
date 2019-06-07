// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   ALSA modem driver for VIA VT82xx (South Bridge)
 *
 *   VT82C686A/B/C, VT8233A/C, VT8235
 *
 *	Copyright (c) 2000 Jaroslav Kysela <perex@perex.cz>
 *	                   Tjeerd.Mulder <Tjeerd.Mulder@fujitsu-siemens.com>
 *                    2002 Takashi Iwai <tiwai@suse.de>
 */

/*
 * Changes:
 *
 * Sep. 2,  2004  Sasha Khapyorsky <sashak@alsa-project.org>
 *      Modified from original audio driver 'via82xx.c' to support AC97
 *      modems.
 */

#include <linux/io.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/info.h>
#include <sound/ac97_codec.h>
#include <sound/initval.h>

#if 0
#define POINTER_DEBUG
#endif

MODULE_AUTHOR("Jaroslav Kysela <perex@perex.cz>");
MODULE_DESCRIPTION("VIA VT82xx modem");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{VIA,VT82C686A/B/C modem,pci}}");

static int index = -2; /* Exclude the first card */
static char *id = SNDRV_DEFAULT_STR1;	/* ID for this card */
static int ac97_clock = 48000;

module_param(index, int, 0444);
MODULE_PARM_DESC(index, "Index value for VIA 82xx bridge.");
module_param(id, charp, 0444);
MODULE_PARM_DESC(id, "ID string for VIA 82xx bridge.");
module_param(ac97_clock, int, 0444);
MODULE_PARM_DESC(ac97_clock, "AC'97 codec clock (default 48000Hz).");

/* just for backward compatibility */
static bool enable;
module_param(enable, bool, 0444);


/*
 *  Direct registers
 */

#define VIAREG(via, x) ((via)->port + VIA_REG_##x)
#define VIADEV_REG(viadev, x) ((viadev)->port + VIA_REG_##x)

/* common offsets */
#define VIA_REG_OFFSET_STATUS		0x00	/* byte - channel status */
#define   VIA_REG_STAT_ACTIVE		0x80	/* RO */
#define   VIA_REG_STAT_PAUSED		0x40	/* RO */
#define   VIA_REG_STAT_TRIGGER_QUEUED	0x08	/* RO */
#define   VIA_REG_STAT_STOPPED		0x04	/* RWC */
#define   VIA_REG_STAT_EOL		0x02	/* RWC */
#define   VIA_REG_STAT_FLAG		0x01	/* RWC */
#define VIA_REG_OFFSET_CONTROL		0x01	/* byte - channel control */
#define   VIA_REG_CTRL_START		0x80	/* WO */
#define   VIA_REG_CTRL_TERMINATE	0x40	/* WO */
#define   VIA_REG_CTRL_AUTOSTART	0x20
#define   VIA_REG_CTRL_PAUSE		0x08	/* RW */
#define   VIA_REG_CTRL_INT_STOP		0x04		
#define   VIA_REG_CTRL_INT_EOL		0x02
#define   VIA_REG_CTRL_INT_FLAG		0x01
#define   VIA_REG_CTRL_RESET		0x01	/* RW - probably reset? undocumented */
#define   VIA_REG_CTRL_INT (VIA_REG_CTRL_INT_FLAG | VIA_REG_CTRL_INT_EOL | VIA_REG_CTRL_AUTOSTART)
#define VIA_REG_OFFSET_TYPE		0x02	/* byte - channel type (686 only) */
#define   VIA_REG_TYPE_AUTOSTART	0x80	/* RW - autostart at EOL */
#define   VIA_REG_TYPE_16BIT		0x20	/* RW */
#define   VIA_REG_TYPE_STEREO		0x10	/* RW */
#define   VIA_REG_TYPE_INT_LLINE	0x00
#define   VIA_REG_TYPE_INT_LSAMPLE	0x04
#define   VIA_REG_TYPE_INT_LESSONE	0x08
#define   VIA_REG_TYPE_INT_MASK		0x0c
#define   VIA_REG_TYPE_INT_EOL		0x02
#define   VIA_REG_TYPE_INT_FLAG		0x01
#define VIA_REG_OFFSET_TABLE_PTR	0x04	/* dword - channel table pointer */
#define VIA_REG_OFFSET_CURR_PTR		0x04	/* dword - channel current pointer */
#define VIA_REG_OFFSET_STOP_IDX		0x08	/* dword - stop index, channel type, sample rate */
#define VIA_REG_OFFSET_CURR_COUNT	0x0c	/* dword - channel current count (24 bit) */
#define VIA_REG_OFFSET_CURR_INDEX	0x0f	/* byte - channel current index (for via8233 only) */

#define DEFINE_VIA_REGSET(name,val) \
enum {\
	VIA_REG_##name##_STATUS		= (val),\
	VIA_REG_##name##_CONTROL	= (val) + 0x01,\
	VIA_REG_##name##_TYPE		= (val) + 0x02,\
	VIA_REG_##name##_TABLE_PTR	= (val) + 0x04,\
	VIA_REG_##name##_CURR_PTR	= (val) + 0x04,\
	VIA_REG_##name##_STOP_IDX	= (val) + 0x08,\
	VIA_REG_##name##_CURR_COUNT	= (val) + 0x0c,\
}

/* modem block */
DEFINE_VIA_REGSET(MO, 0x40);
DEFINE_VIA_REGSET(MI, 0x50);

/* AC'97 */
#define VIA_REG_AC97			0x80	/* dword */
#define   VIA_REG_AC97_CODEC_ID_MASK	(3<<30)
#define   VIA_REG_AC97_CODEC_ID_SHIFT	30
#define   VIA_REG_AC97_CODEC_ID_PRIMARY	0x00
#define   VIA_REG_AC97_CODEC_ID_SECONDARY 0x01
#define   VIA_REG_AC97_SECONDARY_VALID	(1<<27)
#define   VIA_REG_AC97_PRIMARY_VALID	(1<<25)
#define   VIA_REG_AC97_BUSY		(1<<24)
#define   VIA_REG_AC97_READ		(1<<23)
#define   VIA_REG_AC97_CMD_SHIFT	16
#define   VIA_REG_AC97_CMD_MASK		0x7e
#define   VIA_REG_AC97_DATA_SHIFT	0
#define   VIA_REG_AC97_DATA_MASK	0xffff

#define VIA_REG_SGD_SHADOW		0x84	/* dword */
#define   VIA_REG_SGD_STAT_PB_FLAG	(1<<0)
#define   VIA_REG_SGD_STAT_CP_FLAG	(1<<1)
#define   VIA_REG_SGD_STAT_FM_FLAG	(1<<2)
#define   VIA_REG_SGD_STAT_PB_EOL	(1<<4)
#define   VIA_REG_SGD_STAT_CP_EOL	(1<<5)
#define   VIA_REG_SGD_STAT_FM_EOL	(1<<6)
#define   VIA_REG_SGD_STAT_PB_STOP	(1<<8)
#define   VIA_REG_SGD_STAT_CP_STOP	(1<<9)
#define   VIA_REG_SGD_STAT_FM_STOP	(1<<10)
#define   VIA_REG_SGD_STAT_PB_ACTIVE	(1<<12)
#define   VIA_REG_SGD_STAT_CP_ACTIVE	(1<<13)
#define   VIA_REG_SGD_STAT_FM_ACTIVE	(1<<14)
#define   VIA_REG_SGD_STAT_MR_FLAG      (1<<16)
#define   VIA_REG_SGD_STAT_MW_FLAG      (1<<17)
#define   VIA_REG_SGD_STAT_MR_EOL       (1<<20)
#define   VIA_REG_SGD_STAT_MW_EOL       (1<<21)
#define   VIA_REG_SGD_STAT_MR_STOP      (1<<24)
#define   VIA_REG_SGD_STAT_MW_STOP      (1<<25)
#define   VIA_REG_SGD_STAT_MR_ACTIVE    (1<<28)
#define   VIA_REG_SGD_STAT_MW_ACTIVE    (1<<29)

#define VIA_REG_GPI_STATUS		0x88
#define VIA_REG_GPI_INTR		0x8c

#define VIA_TBL_BIT_FLAG	0x40000000
#define VIA_TBL_BIT_EOL		0x80000000

/* pci space */
#define VIA_ACLINK_STAT		0x40
#define  VIA_ACLINK_C11_READY	0x20
#define  VIA_ACLINK_C10_READY	0x10
#define  VIA_ACLINK_C01_READY	0x04 /* secondary codec ready */
#define  VIA_ACLINK_LOWPOWER	0x02 /* low-power state */
#define  VIA_ACLINK_C00_READY	0x01 /* primary codec ready */
#define VIA_ACLINK_CTRL		0x41
#define  VIA_ACLINK_CTRL_ENABLE	0x80 /* 0: disable, 1: enable */
#define  VIA_ACLINK_CTRL_RESET	0x40 /* 0: assert, 1: de-assert */
#define  VIA_ACLINK_CTRL_SYNC	0x20 /* 0: release SYNC, 1: force SYNC hi */
#define  VIA_ACLINK_CTRL_SDO	0x10 /* 0: release SDO, 1: force SDO hi */
#define  VIA_ACLINK_CTRL_VRA	0x08 /* 0: disable VRA, 1: enable VRA */
#define  VIA_ACLINK_CTRL_PCM	0x04 /* 0: disable PCM, 1: enable PCM */
#define  VIA_ACLINK_CTRL_FM	0x02 /* via686 only */
#define  VIA_ACLINK_CTRL_SB	0x01 /* via686 only */
#define  VIA_ACLINK_CTRL_INIT	(VIA_ACLINK_CTRL_ENABLE|\
				 VIA_ACLINK_CTRL_RESET|\
				 VIA_ACLINK_CTRL_PCM)
#define VIA_FUNC_ENABLE		0x42
#define  VIA_FUNC_MIDI_PNP	0x80 /* FIXME: it's 0x40 in the datasheet! */
#define  VIA_FUNC_MIDI_IRQMASK	0x40 /* FIXME: not documented! */
#define  VIA_FUNC_RX2C_WRITE	0x20
#define  VIA_FUNC_SB_FIFO_EMPTY	0x10
#define  VIA_FUNC_ENABLE_GAME	0x08
#define  VIA_FUNC_ENABLE_FM	0x04
#define  VIA_FUNC_ENABLE_MIDI	0x02
#define  VIA_FUNC_ENABLE_SB	0x01
#define VIA_PNP_CONTROL		0x43
#define VIA_MC97_CTRL		0x44
#define  VIA_MC97_CTRL_ENABLE   0x80
#define  VIA_MC97_CTRL_SECONDARY 0x40
#define  VIA_MC97_CTRL_INIT     (VIA_MC97_CTRL_ENABLE|\
                                 VIA_MC97_CTRL_SECONDARY)


/*
 * pcm stream
 */

struct snd_via_sg_table {
	unsigned int offset;
	unsigned int size;
} ;

#define VIA_TABLE_SIZE	255

struct viadev {
	unsigned int reg_offset;
	unsigned long port;
	int direction;	/* playback = 0, capture = 1 */
        struct snd_pcm_substream *substream;
	int running;
	unsigned int tbl_entries; /* # descriptors */
	struct snd_dma_buffer table;
	struct snd_via_sg_table *idx_table;
	/* for recovery from the unexpected pointer */
	unsigned int lastpos;
	unsigned int bufsize;
	unsigned int bufsize2;
};

enum { TYPE_CARD_VIA82XX_MODEM = 1 };

#define VIA_MAX_MODEM_DEVS	2

struct via82xx_modem {
	int irq;

	unsigned long port;

	unsigned int intr_mask; /* SGD_SHADOW mask to check interrupts */

	struct pci_dev *pci;
	struct snd_card *card;

	unsigned int num_devs;
	unsigned int playback_devno, capture_devno;
	struct viadev devs[VIA_MAX_MODEM_DEVS];

	struct snd_pcm *pcms[2];

	struct snd_ac97_bus *ac97_bus;
	struct snd_ac97 *ac97;
	unsigned int ac97_clock;
	unsigned int ac97_secondary;	/* secondary AC'97 codec is present */

	spinlock_t reg_lock;
	struct snd_info_entry *proc_entry;
};

static const struct pci_device_id snd_via82xx_modem_ids[] = {
	{ PCI_VDEVICE(VIA, 0x3068), TYPE_CARD_VIA82XX_MODEM, },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, snd_via82xx_modem_ids);

/*
 */

/*
 * allocate and initialize the descriptor buffers
 * periods = number of periods
 * fragsize = period size in bytes
 */
static int build_via_table(struct viadev *dev, struct snd_pcm_substream *substream,
			   struct pci_dev *pci,
			   unsigned int periods, unsigned int fragsize)
{
	unsigned int i, idx, ofs, rest;
	struct via82xx_modem *chip = snd_pcm_substream_chip(substream);

	if (dev->table.area == NULL) {
		/* the start of each lists must be aligned to 8 bytes,
		 * but the kernel pages are much bigger, so we don't care
		 */
		if (snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, snd_dma_pci_data(chip->pci),
					PAGE_ALIGN(VIA_TABLE_SIZE * 2 * 8),
					&dev->table) < 0)
			return -ENOMEM;
	}
	if (! dev->idx_table) {
		dev->idx_table = kmalloc_array(VIA_TABLE_SIZE,
					       sizeof(*dev->idx_table),
					       GFP_KERNEL);
		if (! dev->idx_table)
			return -ENOMEM;
	}

	/* fill the entries */
	idx = 0;
	ofs = 0;
	for (i = 0; i < periods; i++) {
		rest = fragsize;
		/* fill descriptors for a period.
		 * a period can be split to several descriptors if it's
		 * over page boundary.
		 */
		do {
			unsigned int r;
			unsigned int flag;
			unsigned int addr;

			if (idx >= VIA_TABLE_SIZE) {
				dev_err(&pci->dev, "too much table size!\n");
				return -EINVAL;
			}
			addr = snd_pcm_sgbuf_get_addr(substream, ofs);
			((u32 *)dev->table.area)[idx << 1] = cpu_to_le32(addr);
			r = PAGE_SIZE - (ofs % PAGE_SIZE);
			if (rest < r)
				r = rest;
			rest -= r;
			if (! rest) {
				if (i == periods - 1)
					flag = VIA_TBL_BIT_EOL; /* buffer boundary */
				else
					flag = VIA_TBL_BIT_FLAG; /* period boundary */
			} else
				flag = 0; /* period continues to the next */
			/*
			dev_dbg(&pci->dev,
				"tbl %d: at %d  size %d (rest %d)\n",
				idx, ofs, r, rest);
			*/
			((u32 *)dev->table.area)[(idx<<1) + 1] = cpu_to_le32(r | flag);
			dev->idx_table[idx].offset = ofs;
			dev->idx_table[idx].size = r;
			ofs += r;
			idx++;
		} while (rest > 0);
	}
	dev->tbl_entries = idx;
	dev->bufsize = periods * fragsize;
	dev->bufsize2 = dev->bufsize / 2;
	return 0;
}


static int clean_via_table(struct viadev *dev, struct snd_pcm_substream *substream,
			   struct pci_dev *pci)
{
	if (dev->table.area) {
		snd_dma_free_pages(&dev->table);
		dev->table.area = NULL;
	}
	kfree(dev->idx_table);
	dev->idx_table = NULL;
	return 0;
}

/*
 *  Basic I/O
 */

static inline unsigned int snd_via82xx_codec_xread(struct via82xx_modem *chip)
{
	return inl(VIAREG(chip, AC97));
}
 
static inline void snd_via82xx_codec_xwrite(struct via82xx_modem *chip, unsigned int val)
{
	outl(val, VIAREG(chip, AC97));
}
 
static int snd_via82xx_codec_ready(struct via82xx_modem *chip, int secondary)
{
	unsigned int timeout = 1000;	/* 1ms */
	unsigned int val;
	
	while (timeout-- > 0) {
		udelay(1);
		if (!((val = snd_via82xx_codec_xread(chip)) & VIA_REG_AC97_BUSY))
			return val & 0xffff;
	}
	dev_err(chip->card->dev, "codec_ready: codec %i is not ready [0x%x]\n",
		   secondary, snd_via82xx_codec_xread(chip));
	return -EIO;
}
 
static int snd_via82xx_codec_valid(struct via82xx_modem *chip, int secondary)
{
	unsigned int timeout = 1000;	/* 1ms */
	unsigned int val, val1;
	unsigned int stat = !secondary ? VIA_REG_AC97_PRIMARY_VALID :
					 VIA_REG_AC97_SECONDARY_VALID;
	
	while (timeout-- > 0) {
		val = snd_via82xx_codec_xread(chip);
		val1 = val & (VIA_REG_AC97_BUSY | stat);
		if (val1 == stat)
			return val & 0xffff;
		udelay(1);
	}
	return -EIO;
}
 
static void snd_via82xx_codec_wait(struct snd_ac97 *ac97)
{
	struct via82xx_modem *chip = ac97->private_data;
	int err;
	err = snd_via82xx_codec_ready(chip, ac97->num);
	/* here we need to wait fairly for long time.. */
	msleep(500);
}

static void snd_via82xx_codec_write(struct snd_ac97 *ac97,
				    unsigned short reg,
				    unsigned short val)
{
	struct via82xx_modem *chip = ac97->private_data;
	unsigned int xval;
	if(reg == AC97_GPIO_STATUS) {
		outl(val, VIAREG(chip, GPI_STATUS));
		return;
	}	
	xval = !ac97->num ? VIA_REG_AC97_CODEC_ID_PRIMARY : VIA_REG_AC97_CODEC_ID_SECONDARY;
	xval <<= VIA_REG_AC97_CODEC_ID_SHIFT;
	xval |= reg << VIA_REG_AC97_CMD_SHIFT;
	xval |= val << VIA_REG_AC97_DATA_SHIFT;
	snd_via82xx_codec_xwrite(chip, xval);
	snd_via82xx_codec_ready(chip, ac97->num);
}

static unsigned short snd_via82xx_codec_read(struct snd_ac97 *ac97, unsigned short reg)
{
	struct via82xx_modem *chip = ac97->private_data;
	unsigned int xval, val = 0xffff;
	int again = 0;

	xval = ac97->num << VIA_REG_AC97_CODEC_ID_SHIFT;
	xval |= ac97->num ? VIA_REG_AC97_SECONDARY_VALID : VIA_REG_AC97_PRIMARY_VALID;
	xval |= VIA_REG_AC97_READ;
	xval |= (reg & 0x7f) << VIA_REG_AC97_CMD_SHIFT;
      	while (1) {
      		if (again++ > 3) {
			dev_err(chip->card->dev,
				"codec_read: codec %i is not valid [0x%x]\n",
				   ac97->num, snd_via82xx_codec_xread(chip));
		      	return 0xffff;
		}
		snd_via82xx_codec_xwrite(chip, xval);
		udelay (20);
		if (snd_via82xx_codec_valid(chip, ac97->num) >= 0) {
			udelay(25);
			val = snd_via82xx_codec_xread(chip);
			break;
		}
	}
	return val & 0xffff;
}

static void snd_via82xx_channel_reset(struct via82xx_modem *chip, struct viadev *viadev)
{
	outb(VIA_REG_CTRL_PAUSE | VIA_REG_CTRL_TERMINATE | VIA_REG_CTRL_RESET,
	     VIADEV_REG(viadev, OFFSET_CONTROL));
	inb(VIADEV_REG(viadev, OFFSET_CONTROL));
	udelay(50);
	/* disable interrupts */
	outb(0x00, VIADEV_REG(viadev, OFFSET_CONTROL));
	/* clear interrupts */
	outb(0x03, VIADEV_REG(viadev, OFFSET_STATUS));
	outb(0x00, VIADEV_REG(viadev, OFFSET_TYPE)); /* for via686 */
	// outl(0, VIADEV_REG(viadev, OFFSET_CURR_PTR));
	viadev->lastpos = 0;
}


/*
 *  Interrupt handler
 */

static irqreturn_t snd_via82xx_interrupt(int irq, void *dev_id)
{
	struct via82xx_modem *chip = dev_id;
	unsigned int status;
	unsigned int i;

	status = inl(VIAREG(chip, SGD_SHADOW));
	if (! (status & chip->intr_mask)) {
		return IRQ_NONE;
	}
// _skip_sgd:

	/* check status for each stream */
	spin_lock(&chip->reg_lock);
	for (i = 0; i < chip->num_devs; i++) {
		struct viadev *viadev = &chip->devs[i];
		unsigned char c_status = inb(VIADEV_REG(viadev, OFFSET_STATUS));
		c_status &= (VIA_REG_STAT_EOL|VIA_REG_STAT_FLAG|VIA_REG_STAT_STOPPED);
		if (! c_status)
			continue;
		if (viadev->substream && viadev->running) {
			spin_unlock(&chip->reg_lock);
			snd_pcm_period_elapsed(viadev->substream);
			spin_lock(&chip->reg_lock);
		}
		outb(c_status, VIADEV_REG(viadev, OFFSET_STATUS)); /* ack */
	}
	spin_unlock(&chip->reg_lock);
	return IRQ_HANDLED;
}

/*
 *  PCM callbacks
 */

/*
 * trigger callback
 */
static int snd_via82xx_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct via82xx_modem *chip = snd_pcm_substream_chip(substream);
	struct viadev *viadev = substream->runtime->private_data;
	unsigned char val = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		val |= VIA_REG_CTRL_START;
		viadev->running = 1;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		val = VIA_REG_CTRL_TERMINATE;
		viadev->running = 0;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		val |= VIA_REG_CTRL_PAUSE;
		viadev->running = 0;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		viadev->running = 1;
		break;
	default:
		return -EINVAL;
	}
	outb(val, VIADEV_REG(viadev, OFFSET_CONTROL));
	if (cmd == SNDRV_PCM_TRIGGER_STOP)
		snd_via82xx_channel_reset(chip, viadev);
	return 0;
}

/*
 * pointer callbacks
 */

/*
 * calculate the linear position at the given sg-buffer index and the rest count
 */

#define check_invalid_pos(viadev,pos) \
	((pos) < viadev->lastpos && ((pos) >= viadev->bufsize2 ||\
				     viadev->lastpos < viadev->bufsize2))

static inline unsigned int calc_linear_pos(struct via82xx_modem *chip,
					   struct viadev *viadev,
					   unsigned int idx,
					   unsigned int count)
{
	unsigned int size, res;

	size = viadev->idx_table[idx].size;
	res = viadev->idx_table[idx].offset + size - count;

	/* check the validity of the calculated position */
	if (size < count) {
		dev_err(chip->card->dev,
			"invalid via82xx_cur_ptr (size = %d, count = %d)\n",
			   (int)size, (int)count);
		res = viadev->lastpos;
	} else if (check_invalid_pos(viadev, res)) {
#ifdef POINTER_DEBUG
		dev_dbg(chip->card->dev,
			"fail: idx = %i/%i, lastpos = 0x%x, bufsize2 = 0x%x, offsize = 0x%x, size = 0x%x, count = 0x%x\n",
			idx, viadev->tbl_entries, viadev->lastpos,
		       viadev->bufsize2, viadev->idx_table[idx].offset,
		       viadev->idx_table[idx].size, count);
#endif
		if (count && size < count) {
			dev_dbg(chip->card->dev,
				"invalid via82xx_cur_ptr, using last valid pointer\n");
			res = viadev->lastpos;
		} else {
			if (! count)
				/* bogus count 0 on the DMA boundary? */
				res = viadev->idx_table[idx].offset;
			else
				/* count register returns full size
				 * when end of buffer is reached
				 */
				res = viadev->idx_table[idx].offset + size;
			if (check_invalid_pos(viadev, res)) {
				dev_dbg(chip->card->dev,
					"invalid via82xx_cur_ptr (2), using last valid pointer\n");
				res = viadev->lastpos;
			}
		}
	}
	viadev->lastpos = res; /* remember the last position */
	if (res >= viadev->bufsize)
		res -= viadev->bufsize;
	return res;
}

/*
 * get the current pointer on via686
 */
static snd_pcm_uframes_t snd_via686_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct via82xx_modem *chip = snd_pcm_substream_chip(substream);
	struct viadev *viadev = substream->runtime->private_data;
	unsigned int idx, ptr, count, res;

	if (snd_BUG_ON(!viadev->tbl_entries))
		return 0;
	if (!(inb(VIADEV_REG(viadev, OFFSET_STATUS)) & VIA_REG_STAT_ACTIVE))
		return 0;

	spin_lock(&chip->reg_lock);
	count = inl(VIADEV_REG(viadev, OFFSET_CURR_COUNT)) & 0xffffff;
	/* The via686a does not have the current index register,
	 * so we need to calculate the index from CURR_PTR.
	 */
	ptr = inl(VIADEV_REG(viadev, OFFSET_CURR_PTR));
	if (ptr <= (unsigned int)viadev->table.addr)
		idx = 0;
	else /* CURR_PTR holds the address + 8 */
		idx = ((ptr - (unsigned int)viadev->table.addr) / 8 - 1) %
			viadev->tbl_entries;
	res = calc_linear_pos(chip, viadev, idx, count);
	spin_unlock(&chip->reg_lock);

	return bytes_to_frames(substream->runtime, res);
}

/*
 * hw_params callback:
 * allocate the buffer and build up the buffer description table
 */
static int snd_via82xx_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *hw_params)
{
	struct via82xx_modem *chip = snd_pcm_substream_chip(substream);
	struct viadev *viadev = substream->runtime->private_data;
	int err;

	err = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
	if (err < 0)
		return err;
	err = build_via_table(viadev, substream, chip->pci,
			      params_periods(hw_params),
			      params_period_bytes(hw_params));
	if (err < 0)
		return err;

	snd_ac97_write(chip->ac97, AC97_LINE1_RATE, params_rate(hw_params));
	snd_ac97_write(chip->ac97, AC97_LINE1_LEVEL, 0);

	return 0;
}

/*
 * hw_free callback:
 * clean up the buffer description table and release the buffer
 */
static int snd_via82xx_hw_free(struct snd_pcm_substream *substream)
{
	struct via82xx_modem *chip = snd_pcm_substream_chip(substream);
	struct viadev *viadev = substream->runtime->private_data;

	clean_via_table(viadev, substream, chip->pci);
	snd_pcm_lib_free_pages(substream);
	return 0;
}


/*
 * set up the table pointer
 */
static void snd_via82xx_set_table_ptr(struct via82xx_modem *chip, struct viadev *viadev)
{
	snd_via82xx_codec_ready(chip, chip->ac97_secondary);
	outl((u32)viadev->table.addr, VIADEV_REG(viadev, OFFSET_TABLE_PTR));
	udelay(20);
	snd_via82xx_codec_ready(chip, chip->ac97_secondary);
}

/*
 * prepare callback for playback and capture
 */
static int snd_via82xx_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct via82xx_modem *chip = snd_pcm_substream_chip(substream);
	struct viadev *viadev = substream->runtime->private_data;

	snd_via82xx_channel_reset(chip, viadev);
	/* this must be set after channel_reset */
	snd_via82xx_set_table_ptr(chip, viadev);
	outb(VIA_REG_TYPE_AUTOSTART|VIA_REG_TYPE_INT_EOL|VIA_REG_TYPE_INT_FLAG,
	     VIADEV_REG(viadev, OFFSET_TYPE));
	return 0;
}

/*
 * pcm hardware definition, identical for both playback and capture
 */
static const struct snd_pcm_hardware snd_via82xx_hw =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 /* SNDRV_PCM_INFO_RESUME | */
				 SNDRV_PCM_INFO_PAUSE),
	.formats =		SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_KNOT,
	.rate_min =		8000,
	.rate_max =		16000,
	.channels_min =		1,
	.channels_max =		1,
	.buffer_bytes_max =	128 * 1024,
	.period_bytes_min =	32,
	.period_bytes_max =	128 * 1024,
	.periods_min =		2,
	.periods_max =		VIA_TABLE_SIZE / 2,
	.fifo_size =		0,
};


/*
 * open callback skeleton
 */
static int snd_via82xx_modem_pcm_open(struct via82xx_modem *chip, struct viadev *viadev,
				      struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;
	static const unsigned int rates[] = { 8000,  9600, 12000, 16000 };
	static const struct snd_pcm_hw_constraint_list hw_constraints_rates = {
                .count = ARRAY_SIZE(rates),
                .list = rates,
                .mask = 0,
        };

	runtime->hw = snd_via82xx_hw;
	
        if ((err = snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
					      &hw_constraints_rates)) < 0)
                return err;

	/* we may remove following constaint when we modify table entries
	   in interrupt */
	if ((err = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS)) < 0)
		return err;

	runtime->private_data = viadev;
	viadev->substream = substream;

	return 0;
}


/*
 * open callback for playback
 */
static int snd_via82xx_playback_open(struct snd_pcm_substream *substream)
{
	struct via82xx_modem *chip = snd_pcm_substream_chip(substream);
	struct viadev *viadev = &chip->devs[chip->playback_devno + substream->number];

	return snd_via82xx_modem_pcm_open(chip, viadev, substream);
}

/*
 * open callback for capture
 */
static int snd_via82xx_capture_open(struct snd_pcm_substream *substream)
{
	struct via82xx_modem *chip = snd_pcm_substream_chip(substream);
	struct viadev *viadev = &chip->devs[chip->capture_devno + substream->pcm->device];

	return snd_via82xx_modem_pcm_open(chip, viadev, substream);
}

/*
 * close callback
 */
static int snd_via82xx_pcm_close(struct snd_pcm_substream *substream)
{
	struct viadev *viadev = substream->runtime->private_data;

	viadev->substream = NULL;
	return 0;
}


/* via686 playback callbacks */
static const struct snd_pcm_ops snd_via686_playback_ops = {
	.open =		snd_via82xx_playback_open,
	.close =	snd_via82xx_pcm_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_via82xx_hw_params,
	.hw_free =	snd_via82xx_hw_free,
	.prepare =	snd_via82xx_pcm_prepare,
	.trigger =	snd_via82xx_pcm_trigger,
	.pointer =	snd_via686_pcm_pointer,
	.page =		snd_pcm_sgbuf_ops_page,
};

/* via686 capture callbacks */
static const struct snd_pcm_ops snd_via686_capture_ops = {
	.open =		snd_via82xx_capture_open,
	.close =	snd_via82xx_pcm_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_via82xx_hw_params,
	.hw_free =	snd_via82xx_hw_free,
	.prepare =	snd_via82xx_pcm_prepare,
	.trigger =	snd_via82xx_pcm_trigger,
	.pointer =	snd_via686_pcm_pointer,
	.page =		snd_pcm_sgbuf_ops_page,
};


static void init_viadev(struct via82xx_modem *chip, int idx, unsigned int reg_offset,
			int direction)
{
	chip->devs[idx].reg_offset = reg_offset;
	chip->devs[idx].direction = direction;
	chip->devs[idx].port = chip->port + reg_offset;
}

/*
 * create a pcm instance for via686a/b
 */
static int snd_via686_pcm_new(struct via82xx_modem *chip)
{
	struct snd_pcm *pcm;
	int err;

	chip->playback_devno = 0;
	chip->capture_devno = 1;
	chip->num_devs = 2;
	chip->intr_mask = 0x330000; /* FLAGS | EOL for MR, MW */

	err = snd_pcm_new(chip->card, chip->card->shortname, 0, 1, 1, &pcm);
	if (err < 0)
		return err;
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_via686_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_via686_capture_ops);
	pcm->dev_class = SNDRV_PCM_CLASS_MODEM;
	pcm->private_data = chip;
	strcpy(pcm->name, chip->card->shortname);
	chip->pcms[0] = pcm;
	init_viadev(chip, 0, VIA_REG_MO_STATUS, 0);
	init_viadev(chip, 1, VIA_REG_MI_STATUS, 1);

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV_SG,
					      snd_dma_pci_data(chip->pci),
					      64*1024, 128*1024);
	return 0;
}


/*
 *  Mixer part
 */


static void snd_via82xx_mixer_free_ac97_bus(struct snd_ac97_bus *bus)
{
	struct via82xx_modem *chip = bus->private_data;
	chip->ac97_bus = NULL;
}

static void snd_via82xx_mixer_free_ac97(struct snd_ac97 *ac97)
{
	struct via82xx_modem *chip = ac97->private_data;
	chip->ac97 = NULL;
}


static int snd_via82xx_mixer_new(struct via82xx_modem *chip)
{
	struct snd_ac97_template ac97;
	int err;
	static struct snd_ac97_bus_ops ops = {
		.write = snd_via82xx_codec_write,
		.read = snd_via82xx_codec_read,
		.wait = snd_via82xx_codec_wait,
	};

	if ((err = snd_ac97_bus(chip->card, 0, &ops, chip, &chip->ac97_bus)) < 0)
		return err;
	chip->ac97_bus->private_free = snd_via82xx_mixer_free_ac97_bus;
	chip->ac97_bus->clock = chip->ac97_clock;

	memset(&ac97, 0, sizeof(ac97));
	ac97.private_data = chip;
	ac97.private_free = snd_via82xx_mixer_free_ac97;
	ac97.pci = chip->pci;
	ac97.scaps = AC97_SCAP_SKIP_AUDIO | AC97_SCAP_POWER_SAVE;
	ac97.num = chip->ac97_secondary;

	if ((err = snd_ac97_mixer(chip->ac97_bus, &ac97, &chip->ac97)) < 0)
		return err;

	return 0;
}


/*
 * proc interface
 */
static void snd_via82xx_proc_read(struct snd_info_entry *entry, struct snd_info_buffer *buffer)
{
	struct via82xx_modem *chip = entry->private_data;
	int i;
	
	snd_iprintf(buffer, "%s\n\n", chip->card->longname);
	for (i = 0; i < 0xa0; i += 4) {
		snd_iprintf(buffer, "%02x: %08x\n", i, inl(chip->port + i));
	}
}

static void snd_via82xx_proc_init(struct via82xx_modem *chip)
{
	snd_card_ro_proc_new(chip->card, "via82xx", chip,
			     snd_via82xx_proc_read);
}

/*
 *
 */

static int snd_via82xx_chip_init(struct via82xx_modem *chip)
{
	unsigned int val;
	unsigned long end_time;
	unsigned char pval;

	pci_read_config_byte(chip->pci, VIA_MC97_CTRL, &pval);
	if((pval & VIA_MC97_CTRL_INIT) != VIA_MC97_CTRL_INIT) {
		pci_write_config_byte(chip->pci, 0x44, pval|VIA_MC97_CTRL_INIT);
		udelay(100);
	}

	pci_read_config_byte(chip->pci, VIA_ACLINK_STAT, &pval);
	if (! (pval & VIA_ACLINK_C00_READY)) { /* codec not ready? */
		/* deassert ACLink reset, force SYNC */
		pci_write_config_byte(chip->pci, VIA_ACLINK_CTRL,
				      VIA_ACLINK_CTRL_ENABLE |
				      VIA_ACLINK_CTRL_RESET |
				      VIA_ACLINK_CTRL_SYNC);
		udelay(100);
#if 1 /* FIXME: should we do full reset here for all chip models? */
		pci_write_config_byte(chip->pci, VIA_ACLINK_CTRL, 0x00);
		udelay(100);
#else
		/* deassert ACLink reset, force SYNC (warm AC'97 reset) */
		pci_write_config_byte(chip->pci, VIA_ACLINK_CTRL,
				      VIA_ACLINK_CTRL_RESET|VIA_ACLINK_CTRL_SYNC);
		udelay(2);
#endif
		/* ACLink on, deassert ACLink reset, VSR, SGD data out */
		pci_write_config_byte(chip->pci, VIA_ACLINK_CTRL, VIA_ACLINK_CTRL_INIT);
		udelay(100);
	}
	
	pci_read_config_byte(chip->pci, VIA_ACLINK_CTRL, &pval);
	if ((pval & VIA_ACLINK_CTRL_INIT) != VIA_ACLINK_CTRL_INIT) {
		/* ACLink on, deassert ACLink reset, VSR, SGD data out */
		pci_write_config_byte(chip->pci, VIA_ACLINK_CTRL, VIA_ACLINK_CTRL_INIT);
		udelay(100);
	}

	/* wait until codec ready */
	end_time = jiffies + msecs_to_jiffies(750);
	do {
		pci_read_config_byte(chip->pci, VIA_ACLINK_STAT, &pval);
		if (pval & VIA_ACLINK_C00_READY) /* primary codec ready */
			break;
		schedule_timeout_uninterruptible(1);
	} while (time_before(jiffies, end_time));

	if ((val = snd_via82xx_codec_xread(chip)) & VIA_REG_AC97_BUSY)
		dev_err(chip->card->dev,
			"AC'97 codec is not ready [0x%x]\n", val);

	snd_via82xx_codec_xwrite(chip, VIA_REG_AC97_READ |
				 VIA_REG_AC97_SECONDARY_VALID |
				 (VIA_REG_AC97_CODEC_ID_SECONDARY << VIA_REG_AC97_CODEC_ID_SHIFT));
	end_time = jiffies + msecs_to_jiffies(750);
	snd_via82xx_codec_xwrite(chip, VIA_REG_AC97_READ |
				 VIA_REG_AC97_SECONDARY_VALID |
				 (VIA_REG_AC97_CODEC_ID_SECONDARY << VIA_REG_AC97_CODEC_ID_SHIFT));
	do {
		if ((val = snd_via82xx_codec_xread(chip)) & VIA_REG_AC97_SECONDARY_VALID) {
			chip->ac97_secondary = 1;
			goto __ac97_ok2;
		}
		schedule_timeout_uninterruptible(1);
	} while (time_before(jiffies, end_time));
	/* This is ok, the most of motherboards have only one codec */

      __ac97_ok2:

	/* route FM trap to IRQ, disable FM trap */
	// pci_write_config_byte(chip->pci, VIA_FM_NMI_CTRL, 0);
	/* disable all GPI interrupts */
	outl(0, VIAREG(chip, GPI_INTR));

	return 0;
}

#ifdef CONFIG_PM_SLEEP
/*
 * power management
 */
static int snd_via82xx_suspend(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct via82xx_modem *chip = card->private_data;
	int i;

	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
	for (i = 0; i < chip->num_devs; i++)
		snd_via82xx_channel_reset(chip, &chip->devs[i]);
	synchronize_irq(chip->irq);
	snd_ac97_suspend(chip->ac97);
	return 0;
}

static int snd_via82xx_resume(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct via82xx_modem *chip = card->private_data;
	int i;

	snd_via82xx_chip_init(chip);

	snd_ac97_resume(chip->ac97);

	for (i = 0; i < chip->num_devs; i++)
		snd_via82xx_channel_reset(chip, &chip->devs[i]);

	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	return 0;
}

static SIMPLE_DEV_PM_OPS(snd_via82xx_pm, snd_via82xx_suspend, snd_via82xx_resume);
#define SND_VIA82XX_PM_OPS	&snd_via82xx_pm
#else
#define SND_VIA82XX_PM_OPS	NULL
#endif /* CONFIG_PM_SLEEP */

static int snd_via82xx_free(struct via82xx_modem *chip)
{
	unsigned int i;

	if (chip->irq < 0)
		goto __end_hw;
	/* disable interrupts */
	for (i = 0; i < chip->num_devs; i++)
		snd_via82xx_channel_reset(chip, &chip->devs[i]);

      __end_hw:
	if (chip->irq >= 0)
		free_irq(chip->irq, chip);
	pci_release_regions(chip->pci);
	pci_disable_device(chip->pci);
	kfree(chip);
	return 0;
}

static int snd_via82xx_dev_free(struct snd_device *device)
{
	struct via82xx_modem *chip = device->device_data;
	return snd_via82xx_free(chip);
}

static int snd_via82xx_create(struct snd_card *card,
			      struct pci_dev *pci,
			      int chip_type,
			      int revision,
			      unsigned int ac97_clock,
			      struct via82xx_modem **r_via)
{
	struct via82xx_modem *chip;
	int err;
        static struct snd_device_ops ops = {
		.dev_free =	snd_via82xx_dev_free,
        };

	if ((err = pci_enable_device(pci)) < 0)
		return err;

	if ((chip = kzalloc(sizeof(*chip), GFP_KERNEL)) == NULL) {
		pci_disable_device(pci);
		return -ENOMEM;
	}

	spin_lock_init(&chip->reg_lock);
	chip->card = card;
	chip->pci = pci;
	chip->irq = -1;

	if ((err = pci_request_regions(pci, card->driver)) < 0) {
		kfree(chip);
		pci_disable_device(pci);
		return err;
	}
	chip->port = pci_resource_start(pci, 0);
	if (request_irq(pci->irq, snd_via82xx_interrupt, IRQF_SHARED,
			KBUILD_MODNAME, chip)) {
		dev_err(card->dev, "unable to grab IRQ %d\n", pci->irq);
		snd_via82xx_free(chip);
		return -EBUSY;
	}
	chip->irq = pci->irq;
	if (ac97_clock >= 8000 && ac97_clock <= 48000)
		chip->ac97_clock = ac97_clock;
	synchronize_irq(chip->irq);

	if ((err = snd_via82xx_chip_init(chip)) < 0) {
		snd_via82xx_free(chip);
		return err;
	}

	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops)) < 0) {
		snd_via82xx_free(chip);
		return err;
	}

	/* The 8233 ac97 controller does not implement the master bit
	 * in the pci command register. IMHO this is a violation of the PCI spec.
	 * We call pci_set_master here because it does not hurt. */
	pci_set_master(pci);

	*r_via = chip;
	return 0;
}


static int snd_via82xx_probe(struct pci_dev *pci,
			     const struct pci_device_id *pci_id)
{
	struct snd_card *card;
	struct via82xx_modem *chip;
	int chip_type = 0, card_type;
	unsigned int i;
	int err;

	err = snd_card_new(&pci->dev, index, id, THIS_MODULE, 0, &card);
	if (err < 0)
		return err;

	card_type = pci_id->driver_data;
	switch (card_type) {
	case TYPE_CARD_VIA82XX_MODEM:
		strcpy(card->driver, "VIA82XX-MODEM");
		sprintf(card->shortname, "VIA 82XX modem");
		break;
	default:
		dev_err(card->dev, "invalid card type %d\n", card_type);
		err = -EINVAL;
		goto __error;
	}
		
	if ((err = snd_via82xx_create(card, pci, chip_type, pci->revision,
				      ac97_clock, &chip)) < 0)
		goto __error;
	card->private_data = chip;
	if ((err = snd_via82xx_mixer_new(chip)) < 0)
		goto __error;

	if ((err = snd_via686_pcm_new(chip)) < 0 )
		goto __error;

	/* disable interrupts */
	for (i = 0; i < chip->num_devs; i++)
		snd_via82xx_channel_reset(chip, &chip->devs[i]);

	sprintf(card->longname, "%s at 0x%lx, irq %d",
		card->shortname, chip->port, chip->irq);

	snd_via82xx_proc_init(chip);

	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
	pci_set_drvdata(pci, card);
	return 0;

 __error:
	snd_card_free(card);
	return err;
}

static void snd_via82xx_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
}

static struct pci_driver via82xx_modem_driver = {
	.name = KBUILD_MODNAME,
	.id_table = snd_via82xx_modem_ids,
	.probe = snd_via82xx_probe,
	.remove = snd_via82xx_remove,
	.driver = {
		.pm = SND_VIA82XX_PM_OPS,
	},
};

module_pci_driver(via82xx_modem_driver);
