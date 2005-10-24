/*
 *   ALSA driver for VIA VT82xx (South Bridge)
 *
 *   VT82C686A/B/C, VT8233A/C, VT8235
 *
 *	Copyright (c) 2000 Jaroslav Kysela <perex@suse.cz>
 *	                   Tjeerd.Mulder <Tjeerd.Mulder@fujitsu-siemens.com>
 *                    2002 Takashi Iwai <tiwai@suse.de>
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

/*
 * Changes:
 *
 * Dec. 19, 2002	Takashi Iwai <tiwai@suse.de>
 *	- use the DSX channels for the first pcm playback.
 *	  (on VIA8233, 8233C and 8235 only)
 *	  this will allow you play simultaneously up to 4 streams.
 *	  multi-channel playback is assigned to the second device
 *	  on these chips.
 *	- support the secondary capture (on VIA8233/C,8235)
 *	- SPDIF support
 *	  the DSX3 channel can be used for SPDIF output.
 *	  on VIA8233A, this channel is assigned to the second pcm
 *	  playback.
 *	  the card config of alsa-lib will assign the correct
 *	  device for applications.
 *	- clean up the code, separate low-level initialization
 *	  routines for each chipset.
 *
 * Sep. 26, 2005	Karsten Wiese <annabellesgarden@yahoo.de>
 *	- Optimize position calculation for the 823x chips. 
 */

#include <sound/driver.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/gameport.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/info.h>
#include <sound/ac97_codec.h>
#include <sound/mpu401.h>
#include <sound/initval.h>

#if 0
#define POINTER_DEBUG
#endif

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("VIA VT82xx audio");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{VIA,VT82C686A/B/C,pci},{VIA,VT8233A/C,8235}}");

#if defined(CONFIG_GAMEPORT) || (defined(MODULE) && defined(CONFIG_GAMEPORT_MODULE))
#define SUPPORT_JOYSTICK 1
#endif

static int index = SNDRV_DEFAULT_IDX1;	/* Index 0-MAX */
static char *id = SNDRV_DEFAULT_STR1;	/* ID for this card */
static long mpu_port;
#ifdef SUPPORT_JOYSTICK
static int joystick;
#endif
static int ac97_clock = 48000;
static char *ac97_quirk;
static int dxs_support;

module_param(index, int, 0444);
MODULE_PARM_DESC(index, "Index value for VIA 82xx bridge.");
module_param(id, charp, 0444);
MODULE_PARM_DESC(id, "ID string for VIA 82xx bridge.");
module_param(mpu_port, long, 0444);
MODULE_PARM_DESC(mpu_port, "MPU-401 port. (VT82C686x only)");
#ifdef SUPPORT_JOYSTICK
module_param(joystick, bool, 0444);
MODULE_PARM_DESC(joystick, "Enable joystick. (VT82C686x only)");
#endif
module_param(ac97_clock, int, 0444);
MODULE_PARM_DESC(ac97_clock, "AC'97 codec clock (default 48000Hz).");
module_param(ac97_quirk, charp, 0444);
MODULE_PARM_DESC(ac97_quirk, "AC'97 workaround for strange hardware.");
module_param(dxs_support, int, 0444);
MODULE_PARM_DESC(dxs_support, "Support for DXS channels (0 = auto, 1 = enable, 2 = disable, 3 = 48k only, 4 = no VRA, 5 = enable any sample rate)");

/* just for backward compatibility */
static int enable;
module_param(enable, bool, 0444);


/* revision numbers for via686 */
#define VIA_REV_686_A		0x10
#define VIA_REV_686_B		0x11
#define VIA_REV_686_C		0x12
#define VIA_REV_686_D		0x13
#define VIA_REV_686_E		0x14
#define VIA_REV_686_H		0x20

/* revision numbers for via8233 */
#define VIA_REV_PRE_8233	0x10	/* not in market */
#define VIA_REV_8233C		0x20	/* 2 rec, 4 pb, 1 multi-pb */
#define VIA_REV_8233		0x30	/* 2 rec, 4 pb, 1 multi-pb, spdif */
#define VIA_REV_8233A		0x40	/* 1 rec, 1 multi-pb, spdf */
#define VIA_REV_8235		0x50	/* 2 rec, 4 pb, 1 multi-pb, spdif */
#define VIA_REV_8237		0x60

/*
 *  Direct registers
 */

#define VIAREG(via, x) ((via)->port + VIA_REG_##x)
#define VIADEV_REG(viadev, x) ((viadev)->port + VIA_REG_##x)

/* common offsets */
#define VIA_REG_OFFSET_STATUS		0x00	/* byte - channel status */
#define   VIA_REG_STAT_ACTIVE		0x80	/* RO */
#define   VIA8233_SHADOW_STAT_ACTIVE	0x08	/* RO */
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
#define   VIA8233_REG_TYPE_16BIT	0x00200000	/* RW */
#define   VIA8233_REG_TYPE_STEREO	0x00100000	/* RW */
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

/* playback block */
DEFINE_VIA_REGSET(PLAYBACK, 0x00);
DEFINE_VIA_REGSET(CAPTURE, 0x10);
DEFINE_VIA_REGSET(FM, 0x20);

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
/* via686 */
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
/* via8233 */
#define   VIA8233_REG_SGD_STAT_FLAG	(1<<0)
#define   VIA8233_REG_SGD_STAT_EOL	(1<<1)
#define   VIA8233_REG_SGD_STAT_STOP	(1<<2)
#define   VIA8233_REG_SGD_STAT_ACTIVE	(1<<3)
#define VIA8233_INTR_MASK(chan) ((VIA8233_REG_SGD_STAT_FLAG|VIA8233_REG_SGD_STAT_EOL) << ((chan) * 4))
#define   VIA8233_REG_SGD_CHAN_SDX	0
#define   VIA8233_REG_SGD_CHAN_MULTI	4
#define   VIA8233_REG_SGD_CHAN_REC	6
#define   VIA8233_REG_SGD_CHAN_REC1	7

#define VIA_REG_GPI_STATUS		0x88
#define VIA_REG_GPI_INTR		0x8c

/* multi-channel and capture registers for via8233 */
DEFINE_VIA_REGSET(MULTPLAY, 0x40);
DEFINE_VIA_REGSET(CAPTURE_8233, 0x60);

/* via8233-specific registers */
#define VIA_REG_OFS_PLAYBACK_VOLUME_L	0x02	/* byte */
#define VIA_REG_OFS_PLAYBACK_VOLUME_R	0x03	/* byte */
#define VIA_REG_OFS_MULTPLAY_FORMAT	0x02	/* byte - format and channels */
#define   VIA_REG_MULTPLAY_FMT_8BIT	0x00
#define   VIA_REG_MULTPLAY_FMT_16BIT	0x80
#define   VIA_REG_MULTPLAY_FMT_CH_MASK	0x70	/* # channels << 4 (valid = 1,2,4,6) */
#define VIA_REG_OFS_CAPTURE_FIFO	0x02	/* byte - bit 6 = fifo  enable */
#define   VIA_REG_CAPTURE_FIFO_ENABLE	0x40

#define VIA_DXS_MAX_VOLUME		31	/* max. volume (attenuation) of reg 0x32/33 */

#define VIA_REG_CAPTURE_CHANNEL		0x63	/* byte - input select */
#define   VIA_REG_CAPTURE_CHANNEL_MIC	0x4
#define   VIA_REG_CAPTURE_CHANNEL_LINE	0
#define   VIA_REG_CAPTURE_SELECT_CODEC	0x03	/* recording source codec (0 = primary) */

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
				 VIA_ACLINK_CTRL_PCM|\
				 VIA_ACLINK_CTRL_VRA)
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
#define VIA_FM_NMI_CTRL		0x48
#define VIA8233_VOLCHG_CTRL	0x48
#define VIA8233_SPDIF_CTRL	0x49
#define  VIA8233_SPDIF_DX3	0x08
#define  VIA8233_SPDIF_SLOT_MASK	0x03
#define  VIA8233_SPDIF_SLOT_1011	0x00
#define  VIA8233_SPDIF_SLOT_34		0x01
#define  VIA8233_SPDIF_SLOT_78		0x02
#define  VIA8233_SPDIF_SLOT_69		0x03

/*
 */

#define VIA_DXS_AUTO	0
#define VIA_DXS_ENABLE	1
#define VIA_DXS_DISABLE	2
#define VIA_DXS_48K	3
#define VIA_DXS_NO_VRA	4
#define VIA_DXS_SRC	5


/*
 */

typedef struct _snd_via82xx via82xx_t;
typedef struct via_dev viadev_t;

/*
 * pcm stream
 */

struct snd_via_sg_table {
	unsigned int offset;
	unsigned int size;
} ;

#define VIA_TABLE_SIZE	255

struct via_dev {
	unsigned int reg_offset;
	unsigned long port;
	int direction;	/* playback = 0, capture = 1 */
        snd_pcm_substream_t *substream;
	int running;
	unsigned int tbl_entries; /* # descriptors */
	struct snd_dma_buffer table;
	struct snd_via_sg_table *idx_table;
	/* for recovery from the unexpected pointer */
	unsigned int lastpos;
	unsigned int fragsize;
	unsigned int bufsize;
	unsigned int bufsize2;
	int hwptr_done;		/* processed frame position in the buffer */
	int in_interrupt;
	int shadow_shift;
};


enum { TYPE_CARD_VIA686 = 1, TYPE_CARD_VIA8233 };
enum { TYPE_VIA686, TYPE_VIA8233, TYPE_VIA8233A };

#define VIA_MAX_DEVS	7	/* 4 playback, 1 multi, 2 capture */

struct via_rate_lock {
	spinlock_t lock;
	int rate;
	int used;
};

struct _snd_via82xx {
	int irq;

	unsigned long port;
	struct resource *mpu_res;
	int chip_type;
	unsigned char revision;

	unsigned char old_legacy;
	unsigned char old_legacy_cfg;
#ifdef CONFIG_PM
	unsigned char legacy_saved;
	unsigned char legacy_cfg_saved;
	unsigned char spdif_ctrl_saved;
	unsigned char capture_src_saved[2];
	unsigned int mpu_port_saved;
#endif

	unsigned char playback_volume[4][2]; /* for VIA8233/C/8235; default = 0 */
	unsigned char playback_volume_c[2]; /* for VIA8233/C/8235; default = 0 */

	unsigned int intr_mask; /* SGD_SHADOW mask to check interrupts */

	struct pci_dev *pci;
	snd_card_t *card;

	unsigned int num_devs;
	unsigned int playback_devno, multi_devno, capture_devno;
	viadev_t devs[VIA_MAX_DEVS];
	struct via_rate_lock rates[2]; /* playback and capture */
	unsigned int dxs_fixed: 1;	/* DXS channel accepts only 48kHz */
	unsigned int no_vra: 1;		/* no need to set VRA on DXS channels */
	unsigned int dxs_src: 1;	/* use full SRC capabilities of DXS */
	unsigned int spdif_on: 1;	/* only spdif rates work to external DACs */

	snd_pcm_t *pcms[2];
	snd_rawmidi_t *rmidi;

	ac97_bus_t *ac97_bus;
	ac97_t *ac97;
	unsigned int ac97_clock;
	unsigned int ac97_secondary;	/* secondary AC'97 codec is present */

	spinlock_t reg_lock;
	snd_info_entry_t *proc_entry;

#ifdef SUPPORT_JOYSTICK
	struct gameport *gameport;
#endif
};

static struct pci_device_id snd_via82xx_ids[] = {
	/* 0x1106, 0x3058 */
	{ PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_82C686_5, PCI_ANY_ID, PCI_ANY_ID, 0, 0, TYPE_CARD_VIA686, },	/* 686A */
	/* 0x1106, 0x3059 */
	{ PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_8233_5, PCI_ANY_ID, PCI_ANY_ID, 0, 0, TYPE_CARD_VIA8233, },	/* VT8233 */
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, snd_via82xx_ids);

/*
 */

/*
 * allocate and initialize the descriptor buffers
 * periods = number of periods
 * fragsize = period size in bytes
 */
static int build_via_table(viadev_t *dev, snd_pcm_substream_t *substream,
			   struct pci_dev *pci,
			   unsigned int periods, unsigned int fragsize)
{
	unsigned int i, idx, ofs, rest;
	via82xx_t *chip = snd_pcm_substream_chip(substream);
	struct snd_sg_buf *sgbuf = snd_pcm_substream_sgbuf(substream);

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
		dev->idx_table = kmalloc(sizeof(*dev->idx_table) * VIA_TABLE_SIZE, GFP_KERNEL);
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

			if (idx >= VIA_TABLE_SIZE) {
				snd_printk(KERN_ERR "via82xx: too much table size!\n");
				return -EINVAL;
			}
			((u32 *)dev->table.area)[idx << 1] = cpu_to_le32((u32)snd_pcm_sgbuf_get_addr(sgbuf, ofs));
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
			// printk("via: tbl %d: at %d  size %d (rest %d)\n", idx, ofs, r, rest);
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
	dev->fragsize = fragsize;
	return 0;
}


static int clean_via_table(viadev_t *dev, snd_pcm_substream_t *substream,
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

static inline unsigned int snd_via82xx_codec_xread(via82xx_t *chip)
{
	return inl(VIAREG(chip, AC97));
}
 
static inline void snd_via82xx_codec_xwrite(via82xx_t *chip, unsigned int val)
{
	outl(val, VIAREG(chip, AC97));
}
 
static int snd_via82xx_codec_ready(via82xx_t *chip, int secondary)
{
	unsigned int timeout = 1000;	/* 1ms */
	unsigned int val;
	
	while (timeout-- > 0) {
		udelay(1);
		if (!((val = snd_via82xx_codec_xread(chip)) & VIA_REG_AC97_BUSY))
			return val & 0xffff;
	}
	snd_printk(KERN_ERR "codec_ready: codec %i is not ready [0x%x]\n", secondary, snd_via82xx_codec_xread(chip));
	return -EIO;
}
 
static int snd_via82xx_codec_valid(via82xx_t *chip, int secondary)
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
 
static void snd_via82xx_codec_wait(ac97_t *ac97)
{
	via82xx_t *chip = ac97->private_data;
	int err;
	err = snd_via82xx_codec_ready(chip, ac97->num);
	/* here we need to wait fairly for long time.. */
	msleep(500);
}

static void snd_via82xx_codec_write(ac97_t *ac97,
				    unsigned short reg,
				    unsigned short val)
{
	via82xx_t *chip = ac97->private_data;
	unsigned int xval;

	xval = !ac97->num ? VIA_REG_AC97_CODEC_ID_PRIMARY : VIA_REG_AC97_CODEC_ID_SECONDARY;
	xval <<= VIA_REG_AC97_CODEC_ID_SHIFT;
	xval |= reg << VIA_REG_AC97_CMD_SHIFT;
	xval |= val << VIA_REG_AC97_DATA_SHIFT;
	snd_via82xx_codec_xwrite(chip, xval);
	snd_via82xx_codec_ready(chip, ac97->num);
}

static unsigned short snd_via82xx_codec_read(ac97_t *ac97, unsigned short reg)
{
	via82xx_t *chip = ac97->private_data;
	unsigned int xval, val = 0xffff;
	int again = 0;

	xval = ac97->num << VIA_REG_AC97_CODEC_ID_SHIFT;
	xval |= ac97->num ? VIA_REG_AC97_SECONDARY_VALID : VIA_REG_AC97_PRIMARY_VALID;
	xval |= VIA_REG_AC97_READ;
	xval |= (reg & 0x7f) << VIA_REG_AC97_CMD_SHIFT;
      	while (1) {
      		if (again++ > 3) {
			snd_printk(KERN_ERR "codec_read: codec %i is not valid [0x%x]\n", ac97->num, snd_via82xx_codec_xread(chip));
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

static void snd_via82xx_channel_reset(via82xx_t *chip, viadev_t *viadev)
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
	viadev->hwptr_done = 0;
}


/*
 *  Interrupt handler
 *  Used for 686 and 8233A
 */
static irqreturn_t snd_via686_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	via82xx_t *chip = dev_id;
	unsigned int status;
	unsigned int i;

	status = inl(VIAREG(chip, SGD_SHADOW));
	if (! (status & chip->intr_mask)) {
		if (chip->rmidi)
			/* check mpu401 interrupt */
			return snd_mpu401_uart_interrupt(irq, chip->rmidi->private_data, regs);
		return IRQ_NONE;
	}

	/* check status for each stream */
	spin_lock(&chip->reg_lock);
	for (i = 0; i < chip->num_devs; i++) {
		viadev_t *viadev = &chip->devs[i];
		unsigned char c_status = inb(VIADEV_REG(viadev, OFFSET_STATUS));
		if (! (c_status & (VIA_REG_STAT_EOL|VIA_REG_STAT_FLAG|VIA_REG_STAT_STOPPED)))
			continue;
		if (viadev->substream && viadev->running) {
			/*
			 * Update hwptr_done based on 'period elapsed'
			 * interrupts. We'll use it, when the chip returns 0 
			 * for OFFSET_CURR_COUNT.
			 */
			if (c_status & VIA_REG_STAT_EOL)
				viadev->hwptr_done = 0;
			else
				viadev->hwptr_done += viadev->fragsize;
			viadev->in_interrupt = c_status;
			spin_unlock(&chip->reg_lock);
			snd_pcm_period_elapsed(viadev->substream);
			spin_lock(&chip->reg_lock);
			viadev->in_interrupt = 0;
		}
		outb(c_status, VIADEV_REG(viadev, OFFSET_STATUS)); /* ack */
	}
	spin_unlock(&chip->reg_lock);
	return IRQ_HANDLED;
}

/*
 *  Interrupt handler
 */
static irqreturn_t snd_via8233_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	via82xx_t *chip = dev_id;
	unsigned int status;
	unsigned int i;
	int irqreturn = 0;

	/* check status for each stream */
	spin_lock(&chip->reg_lock);
	status = inl(VIAREG(chip, SGD_SHADOW));

	for (i = 0; i < chip->num_devs; i++) {
		viadev_t *viadev = &chip->devs[i];
		snd_pcm_substream_t *substream;
		unsigned char c_status, shadow_status;

		shadow_status = (status >> viadev->shadow_shift) &
			(VIA8233_SHADOW_STAT_ACTIVE|VIA_REG_STAT_EOL|
			 VIA_REG_STAT_FLAG);
		c_status = shadow_status & (VIA_REG_STAT_EOL|VIA_REG_STAT_FLAG);
		if (!c_status)
			continue;

		substream = viadev->substream;
		if (substream && viadev->running) {
			/*
			 * Update hwptr_done based on 'period elapsed'
			 * interrupts. We'll use it, when the chip returns 0 
			 * for OFFSET_CURR_COUNT.
			 */
			if (c_status & VIA_REG_STAT_EOL)
				viadev->hwptr_done = 0;
			else
				viadev->hwptr_done += viadev->fragsize;
			viadev->in_interrupt = c_status;
			if (shadow_status & VIA8233_SHADOW_STAT_ACTIVE)
				viadev->in_interrupt |= VIA_REG_STAT_ACTIVE;
			spin_unlock(&chip->reg_lock);

			snd_pcm_period_elapsed(substream);

			spin_lock(&chip->reg_lock);
			viadev->in_interrupt = 0;
		}
		outb(c_status, VIADEV_REG(viadev, OFFSET_STATUS)); /* ack */
		irqreturn = 1;
	}
	spin_unlock(&chip->reg_lock);
	return IRQ_RETVAL(irqreturn);
}

/*
 *  PCM callbacks
 */

/*
 * trigger callback
 */
static int snd_via82xx_pcm_trigger(snd_pcm_substream_t * substream, int cmd)
{
	via82xx_t *chip = snd_pcm_substream_chip(substream);
	viadev_t *viadev = (viadev_t *)substream->runtime->private_data;
	unsigned char val;

	if (chip->chip_type != TYPE_VIA686)
		val = VIA_REG_CTRL_INT;
	else
		val = 0;
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		val |= VIA_REG_CTRL_START;
		viadev->running = 1;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
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
	((pos) < viadev->lastpos && ((pos) >= viadev->bufsize2 || viadev->lastpos < viadev->bufsize2))

static inline unsigned int calc_linear_pos(viadev_t *viadev, unsigned int idx, unsigned int count)
{
	unsigned int size, base, res;

	size = viadev->idx_table[idx].size;
	base = viadev->idx_table[idx].offset;
	res = base + size - count;
	if (res >= viadev->bufsize)
		res -= viadev->bufsize;

	/* check the validity of the calculated position */
	if (size < count) {
		snd_printd(KERN_ERR "invalid via82xx_cur_ptr (size = %d, count = %d)\n", (int)size, (int)count);
		res = viadev->lastpos;
	} else {
		if (! count) {
			/* Some mobos report count = 0 on the DMA boundary,
			 * i.e. count = size indeed.
			 * Let's check whether this step is above the expected size.
			 */
			int delta = res - viadev->lastpos;
			if (delta < 0)
				delta += viadev->bufsize;
			if ((unsigned int)delta > viadev->fragsize)
				res = base;
		}
		if (check_invalid_pos(viadev, res)) {
#ifdef POINTER_DEBUG
			printk(KERN_DEBUG "fail: idx = %i/%i, lastpos = 0x%x, bufsize2 = 0x%x, offsize = 0x%x, size = 0x%x, count = 0x%x\n", idx, viadev->tbl_entries, viadev->lastpos, viadev->bufsize2, viadev->idx_table[idx].offset, viadev->idx_table[idx].size, count);
#endif
			/* count register returns full size when end of buffer is reached */
			res = base + size;
			if (check_invalid_pos(viadev, res)) {
				snd_printd(KERN_ERR "invalid via82xx_cur_ptr (2), using last valid pointer\n");
				res = viadev->lastpos;
			}
		}
	}
	return res;
}

/*
 * get the current pointer on via686
 */
static snd_pcm_uframes_t snd_via686_pcm_pointer(snd_pcm_substream_t *substream)
{
	via82xx_t *chip = snd_pcm_substream_chip(substream);
	viadev_t *viadev = (viadev_t *)substream->runtime->private_data;
	unsigned int idx, ptr, count, res;

	snd_assert(viadev->tbl_entries, return 0);
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
		idx = ((ptr - (unsigned int)viadev->table.addr) / 8 - 1) % viadev->tbl_entries;
	res = calc_linear_pos(viadev, idx, count);
	viadev->lastpos = res; /* remember the last position */
	spin_unlock(&chip->reg_lock);

	return bytes_to_frames(substream->runtime, res);
}

/*
 * get the current pointer on via823x
 */
static snd_pcm_uframes_t snd_via8233_pcm_pointer(snd_pcm_substream_t *substream)
{
	via82xx_t *chip = snd_pcm_substream_chip(substream);
	viadev_t *viadev = (viadev_t *)substream->runtime->private_data;
	unsigned int idx, count, res;
	int status;
	
	snd_assert(viadev->tbl_entries, return 0);

	spin_lock(&chip->reg_lock);
	count = inl(VIADEV_REG(viadev, OFFSET_CURR_COUNT));
	status = viadev->in_interrupt;
	if (!status)
		status = inb(VIADEV_REG(viadev, OFFSET_STATUS));

	if (!(status & VIA_REG_STAT_ACTIVE)) {
		res = 0;
		goto unlock;
	}
	if (count & 0xffffff) {
		idx = count >> 24;
		if (idx >= viadev->tbl_entries) {
#ifdef POINTER_DEBUG
			printk(KERN_DEBUG "fail: invalid idx = %i/%i\n", idx, viadev->tbl_entries);
#endif
			res = viadev->lastpos;
		} else {
			count &= 0xffffff;
			res = calc_linear_pos(viadev, idx, count);
		}
	} else {
		res = viadev->hwptr_done;
		if (!viadev->in_interrupt) {
			if (status & VIA_REG_STAT_EOL) {
				res = 0;
			} else
				if (status & VIA_REG_STAT_FLAG) {
					res += viadev->fragsize;
				}
		}
	}			    
unlock:
	viadev->lastpos = res;
	spin_unlock(&chip->reg_lock);

	return bytes_to_frames(substream->runtime, res);
}


/*
 * hw_params callback:
 * allocate the buffer and build up the buffer description table
 */
static int snd_via82xx_hw_params(snd_pcm_substream_t * substream,
				 snd_pcm_hw_params_t * hw_params)
{
	via82xx_t *chip = snd_pcm_substream_chip(substream);
	viadev_t *viadev = (viadev_t *)substream->runtime->private_data;
	int err;

	err = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
	if (err < 0)
		return err;
	err = build_via_table(viadev, substream, chip->pci,
			      params_periods(hw_params),
			      params_period_bytes(hw_params));
	if (err < 0)
		return err;

	return 0;
}

/*
 * hw_free callback:
 * clean up the buffer description table and release the buffer
 */
static int snd_via82xx_hw_free(snd_pcm_substream_t * substream)
{
	via82xx_t *chip = snd_pcm_substream_chip(substream);
	viadev_t *viadev = (viadev_t *)substream->runtime->private_data;

	clean_via_table(viadev, substream, chip->pci);
	snd_pcm_lib_free_pages(substream);
	return 0;
}


/*
 * set up the table pointer
 */
static void snd_via82xx_set_table_ptr(via82xx_t *chip, viadev_t *viadev)
{
	snd_via82xx_codec_ready(chip, 0);
	outl((u32)viadev->table.addr, VIADEV_REG(viadev, OFFSET_TABLE_PTR));
	udelay(20);
	snd_via82xx_codec_ready(chip, 0);
}

/*
 * prepare callback for playback and capture on via686
 */
static void via686_setup_format(via82xx_t *chip, viadev_t *viadev, snd_pcm_runtime_t *runtime)
{
	snd_via82xx_channel_reset(chip, viadev);
	/* this must be set after channel_reset */
	snd_via82xx_set_table_ptr(chip, viadev);
	outb(VIA_REG_TYPE_AUTOSTART |
	     (runtime->format == SNDRV_PCM_FORMAT_S16_LE ? VIA_REG_TYPE_16BIT : 0) |
	     (runtime->channels > 1 ? VIA_REG_TYPE_STEREO : 0) |
	     ((viadev->reg_offset & 0x10) == 0 ? VIA_REG_TYPE_INT_LSAMPLE : 0) |
	     VIA_REG_TYPE_INT_EOL |
	     VIA_REG_TYPE_INT_FLAG, VIADEV_REG(viadev, OFFSET_TYPE));
}

static int snd_via686_playback_prepare(snd_pcm_substream_t *substream)
{
	via82xx_t *chip = snd_pcm_substream_chip(substream);
	viadev_t *viadev = (viadev_t *)substream->runtime->private_data;
	snd_pcm_runtime_t *runtime = substream->runtime;

	snd_ac97_set_rate(chip->ac97, AC97_PCM_FRONT_DAC_RATE, runtime->rate);
	snd_ac97_set_rate(chip->ac97, AC97_SPDIF, runtime->rate);
	via686_setup_format(chip, viadev, runtime);
	return 0;
}

static int snd_via686_capture_prepare(snd_pcm_substream_t *substream)
{
	via82xx_t *chip = snd_pcm_substream_chip(substream);
	viadev_t *viadev = (viadev_t *)substream->runtime->private_data;
	snd_pcm_runtime_t *runtime = substream->runtime;

	snd_ac97_set_rate(chip->ac97, AC97_PCM_LR_ADC_RATE, runtime->rate);
	via686_setup_format(chip, viadev, runtime);
	return 0;
}

/*
 * lock the current rate
 */
static int via_lock_rate(struct via_rate_lock *rec, int rate)
{
	int changed = 0;

	spin_lock_irq(&rec->lock);
	if (rec->rate != rate) {
		if (rec->rate && rec->used > 1) /* already set */
			changed = -EINVAL;
		else {
			rec->rate = rate;
			changed = 1;
		}
	}
	spin_unlock_irq(&rec->lock);
	return changed;
}

/*
 * prepare callback for DSX playback on via823x
 */
static int snd_via8233_playback_prepare(snd_pcm_substream_t *substream)
{
	via82xx_t *chip = snd_pcm_substream_chip(substream);
	viadev_t *viadev = (viadev_t *)substream->runtime->private_data;
	snd_pcm_runtime_t *runtime = substream->runtime;
	int ac97_rate = chip->dxs_src ? 48000 : runtime->rate;
	int rate_changed;
	u32 rbits;

	if ((rate_changed = via_lock_rate(&chip->rates[0], ac97_rate)) < 0)
		return rate_changed;
	if (rate_changed)
		snd_ac97_set_rate(chip->ac97, AC97_PCM_FRONT_DAC_RATE,
				  chip->no_vra ? 48000 : runtime->rate);
	if (chip->spdif_on && viadev->reg_offset == 0x30)
		snd_ac97_set_rate(chip->ac97, AC97_SPDIF, runtime->rate);

	if (runtime->rate == 48000)
		rbits = 0xfffff;
	else
		rbits = (0x100000 / 48000) * runtime->rate + ((0x100000 % 48000) * runtime->rate) / 48000;
	snd_assert((rbits & ~0xfffff) == 0, return -EINVAL);
	snd_via82xx_channel_reset(chip, viadev);
	snd_via82xx_set_table_ptr(chip, viadev);
	outb(chip->playback_volume[viadev->reg_offset / 0x10][0], VIADEV_REG(viadev, OFS_PLAYBACK_VOLUME_L));
	outb(chip->playback_volume[viadev->reg_offset / 0x10][1], VIADEV_REG(viadev, OFS_PLAYBACK_VOLUME_R));
	outl((runtime->format == SNDRV_PCM_FORMAT_S16_LE ? VIA8233_REG_TYPE_16BIT : 0) | /* format */
	     (runtime->channels > 1 ? VIA8233_REG_TYPE_STEREO : 0) | /* stereo */
	     rbits | /* rate */
	     0xff000000,    /* STOP index is never reached */
	     VIADEV_REG(viadev, OFFSET_STOP_IDX));
	udelay(20);
	snd_via82xx_codec_ready(chip, 0);
	return 0;
}

/*
 * prepare callback for multi-channel playback on via823x
 */
static int snd_via8233_multi_prepare(snd_pcm_substream_t *substream)
{
	via82xx_t *chip = snd_pcm_substream_chip(substream);
	viadev_t *viadev = (viadev_t *)substream->runtime->private_data;
	snd_pcm_runtime_t *runtime = substream->runtime;
	unsigned int slots;
	int fmt;

	if (via_lock_rate(&chip->rates[0], runtime->rate) < 0)
		return -EINVAL;
	snd_ac97_set_rate(chip->ac97, AC97_PCM_FRONT_DAC_RATE, runtime->rate);
	snd_ac97_set_rate(chip->ac97, AC97_PCM_SURR_DAC_RATE, runtime->rate);
	snd_ac97_set_rate(chip->ac97, AC97_PCM_LFE_DAC_RATE, runtime->rate);
	snd_ac97_set_rate(chip->ac97, AC97_SPDIF, runtime->rate);
	snd_via82xx_channel_reset(chip, viadev);
	snd_via82xx_set_table_ptr(chip, viadev);

	fmt = (runtime->format == SNDRV_PCM_FORMAT_S16_LE) ? VIA_REG_MULTPLAY_FMT_16BIT : VIA_REG_MULTPLAY_FMT_8BIT;
	fmt |= runtime->channels << 4;
	outb(fmt, VIADEV_REG(viadev, OFS_MULTPLAY_FORMAT));
#if 0
	if (chip->revision == VIA_REV_8233A)
		slots = 0;
	else
#endif
	{
		/* set sample number to slot 3, 4, 7, 8, 6, 9 (for VIA8233/C,8235) */
		/* corresponding to FL, FR, RL, RR, C, LFE ?? */
		switch (runtime->channels) {
		case 1: slots = (1<<0) | (1<<4); break;
		case 2: slots = (1<<0) | (2<<4); break;
		case 3: slots = (1<<0) | (2<<4) | (5<<8); break;
		case 4: slots = (1<<0) | (2<<4) | (3<<8) | (4<<12); break;
		case 5: slots = (1<<0) | (2<<4) | (3<<8) | (4<<12) | (5<<16); break;
		case 6: slots = (1<<0) | (2<<4) | (3<<8) | (4<<12) | (5<<16) | (6<<20); break;
		default: slots = 0; break;
		}
	}
	/* STOP index is never reached */
	outl(0xff000000 | slots, VIADEV_REG(viadev, OFFSET_STOP_IDX));
	udelay(20);
	snd_via82xx_codec_ready(chip, 0);
	return 0;
}

/*
 * prepare callback for capture on via823x
 */
static int snd_via8233_capture_prepare(snd_pcm_substream_t *substream)
{
	via82xx_t *chip = snd_pcm_substream_chip(substream);
	viadev_t *viadev = (viadev_t *)substream->runtime->private_data;
	snd_pcm_runtime_t *runtime = substream->runtime;

	if (via_lock_rate(&chip->rates[1], runtime->rate) < 0)
		return -EINVAL;
	snd_ac97_set_rate(chip->ac97, AC97_PCM_LR_ADC_RATE, runtime->rate);
	snd_via82xx_channel_reset(chip, viadev);
	snd_via82xx_set_table_ptr(chip, viadev);
	outb(VIA_REG_CAPTURE_FIFO_ENABLE, VIADEV_REG(viadev, OFS_CAPTURE_FIFO));
	outl((runtime->format == SNDRV_PCM_FORMAT_S16_LE ? VIA8233_REG_TYPE_16BIT : 0) |
	     (runtime->channels > 1 ? VIA8233_REG_TYPE_STEREO : 0) |
	     0xff000000,    /* STOP index is never reached */
	     VIADEV_REG(viadev, OFFSET_STOP_IDX));
	udelay(20);
	snd_via82xx_codec_ready(chip, 0);
	return 0;
}


/*
 * pcm hardware definition, identical for both playback and capture
 */
static snd_pcm_hardware_t snd_via82xx_hw =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 /* SNDRV_PCM_INFO_RESUME | */
				 SNDRV_PCM_INFO_PAUSE),
	.formats =		SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_48000,
	.rate_min =		48000,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		2,
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
static int snd_via82xx_pcm_open(via82xx_t *chip, viadev_t *viadev, snd_pcm_substream_t * substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	int err;
	struct via_rate_lock *ratep;

	runtime->hw = snd_via82xx_hw;
	
	/* set the hw rate condition */
	ratep = &chip->rates[viadev->direction];
	spin_lock_irq(&ratep->lock);
	ratep->used++;
	if (chip->spdif_on && viadev->reg_offset == 0x30) {
		/* DXS#3 and spdif is on */
		runtime->hw.rates = chip->ac97->rates[AC97_RATES_SPDIF];
		snd_pcm_limit_hw_rates(runtime);
	} else if (chip->dxs_fixed && viadev->reg_offset < 0x40) {
		/* fixed DXS playback rate */
		runtime->hw.rates = SNDRV_PCM_RATE_48000;
		runtime->hw.rate_min = runtime->hw.rate_max = 48000;
	} else if (chip->dxs_src && viadev->reg_offset < 0x40) {
		/* use full SRC capabilities of DXS */
		runtime->hw.rates = (SNDRV_PCM_RATE_CONTINUOUS |
				     SNDRV_PCM_RATE_8000_48000);
		runtime->hw.rate_min = 8000;
		runtime->hw.rate_max = 48000;
	} else if (! ratep->rate) {
		int idx = viadev->direction ? AC97_RATES_ADC : AC97_RATES_FRONT_DAC;
		runtime->hw.rates = chip->ac97->rates[idx];
		snd_pcm_limit_hw_rates(runtime);
	} else {
		/* a fixed rate */
		runtime->hw.rates = SNDRV_PCM_RATE_KNOT;
		runtime->hw.rate_max = runtime->hw.rate_min = ratep->rate;
	}
	spin_unlock_irq(&ratep->lock);

	/* we may remove following constaint when we modify table entries
	   in interrupt */
	if ((err = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS)) < 0)
		return err;

	runtime->private_data = viadev;
	viadev->substream = substream;

	return 0;
}


/*
 * open callback for playback on via686 and via823x DSX
 */
static int snd_via82xx_playback_open(snd_pcm_substream_t * substream)
{
	via82xx_t *chip = snd_pcm_substream_chip(substream);
	viadev_t *viadev = &chip->devs[chip->playback_devno + substream->number];
	int err;

	if ((err = snd_via82xx_pcm_open(chip, viadev, substream)) < 0)
		return err;
	return 0;
}

/*
 * open callback for playback on via823x multi-channel
 */
static int snd_via8233_multi_open(snd_pcm_substream_t * substream)
{
	via82xx_t *chip = snd_pcm_substream_chip(substream);
	viadev_t *viadev = &chip->devs[chip->multi_devno];
	int err;
	/* channels constraint for VIA8233A
	 * 3 and 5 channels are not supported
	 */
	static unsigned int channels[] = {
		1, 2, 4, 6
	};
	static snd_pcm_hw_constraint_list_t hw_constraints_channels = {
		.count = ARRAY_SIZE(channels),
		.list = channels,
		.mask = 0,
	};

	if ((err = snd_via82xx_pcm_open(chip, viadev, substream)) < 0)
		return err;
	substream->runtime->hw.channels_max = 6;
	if (chip->revision == VIA_REV_8233A)
		snd_pcm_hw_constraint_list(substream->runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS, &hw_constraints_channels);
	return 0;
}

/*
 * open callback for capture on via686 and via823x
 */
static int snd_via82xx_capture_open(snd_pcm_substream_t * substream)
{
	via82xx_t *chip = snd_pcm_substream_chip(substream);
	viadev_t *viadev = &chip->devs[chip->capture_devno + substream->pcm->device];

	return snd_via82xx_pcm_open(chip, viadev, substream);
}

/*
 * close callback
 */
static int snd_via82xx_pcm_close(snd_pcm_substream_t * substream)
{
	via82xx_t *chip = snd_pcm_substream_chip(substream);
	viadev_t *viadev = (viadev_t *)substream->runtime->private_data;
	struct via_rate_lock *ratep;

	/* release the rate lock */
	ratep = &chip->rates[viadev->direction];
	spin_lock_irq(&ratep->lock);
	ratep->used--;
	if (! ratep->used)
		ratep->rate = 0;
	spin_unlock_irq(&ratep->lock);

	viadev->substream = NULL;
	return 0;
}


/* via686 playback callbacks */
static snd_pcm_ops_t snd_via686_playback_ops = {
	.open =		snd_via82xx_playback_open,
	.close =	snd_via82xx_pcm_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_via82xx_hw_params,
	.hw_free =	snd_via82xx_hw_free,
	.prepare =	snd_via686_playback_prepare,
	.trigger =	snd_via82xx_pcm_trigger,
	.pointer =	snd_via686_pcm_pointer,
	.page =		snd_pcm_sgbuf_ops_page,
};

/* via686 capture callbacks */
static snd_pcm_ops_t snd_via686_capture_ops = {
	.open =		snd_via82xx_capture_open,
	.close =	snd_via82xx_pcm_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_via82xx_hw_params,
	.hw_free =	snd_via82xx_hw_free,
	.prepare =	snd_via686_capture_prepare,
	.trigger =	snd_via82xx_pcm_trigger,
	.pointer =	snd_via686_pcm_pointer,
	.page =		snd_pcm_sgbuf_ops_page,
};

/* via823x DSX playback callbacks */
static snd_pcm_ops_t snd_via8233_playback_ops = {
	.open =		snd_via82xx_playback_open,
	.close =	snd_via82xx_pcm_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_via82xx_hw_params,
	.hw_free =	snd_via82xx_hw_free,
	.prepare =	snd_via8233_playback_prepare,
	.trigger =	snd_via82xx_pcm_trigger,
	.pointer =	snd_via8233_pcm_pointer,
	.page =		snd_pcm_sgbuf_ops_page,
};

/* via823x multi-channel playback callbacks */
static snd_pcm_ops_t snd_via8233_multi_ops = {
	.open =		snd_via8233_multi_open,
	.close =	snd_via82xx_pcm_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_via82xx_hw_params,
	.hw_free =	snd_via82xx_hw_free,
	.prepare =	snd_via8233_multi_prepare,
	.trigger =	snd_via82xx_pcm_trigger,
	.pointer =	snd_via8233_pcm_pointer,
	.page =		snd_pcm_sgbuf_ops_page,
};

/* via823x capture callbacks */
static snd_pcm_ops_t snd_via8233_capture_ops = {
	.open =		snd_via82xx_capture_open,
	.close =	snd_via82xx_pcm_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_via82xx_hw_params,
	.hw_free =	snd_via82xx_hw_free,
	.prepare =	snd_via8233_capture_prepare,
	.trigger =	snd_via82xx_pcm_trigger,
	.pointer =	snd_via8233_pcm_pointer,
	.page =		snd_pcm_sgbuf_ops_page,
};


static void init_viadev(via82xx_t *chip, int idx, unsigned int reg_offset, int shadow_pos, int direction)
{
	chip->devs[idx].reg_offset = reg_offset;
	chip->devs[idx].shadow_shift = shadow_pos * 4;
	chip->devs[idx].direction = direction;
	chip->devs[idx].port = chip->port + reg_offset;
}

/*
 * create pcm instances for VIA8233, 8233C and 8235 (not 8233A)
 */
static int __devinit snd_via8233_pcm_new(via82xx_t *chip)
{
	snd_pcm_t *pcm;
	int i, err;

	chip->playback_devno = 0;	/* x 4 */
	chip->multi_devno = 4;		/* x 1 */
	chip->capture_devno = 5;	/* x 2 */
	chip->num_devs = 7;
	chip->intr_mask = 0x33033333; /* FLAG|EOL for rec0-1, mc, sdx0-3 */

	/* PCM #0:  4 DSX playbacks and 1 capture */
	err = snd_pcm_new(chip->card, chip->card->shortname, 0, 4, 1, &pcm);
	if (err < 0)
		return err;
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_via8233_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_via8233_capture_ops);
	pcm->private_data = chip;
	strcpy(pcm->name, chip->card->shortname);
	chip->pcms[0] = pcm;
	/* set up playbacks */
	for (i = 0; i < 4; i++)
		init_viadev(chip, i, 0x10 * i, i, 0);
	/* capture */
	init_viadev(chip, chip->capture_devno, VIA_REG_CAPTURE_8233_STATUS, 6, 1);

	if ((err = snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV_SG,
							 snd_dma_pci_data(chip->pci), 64*1024, 128*1024)) < 0)
		return err;

	/* PCM #1:  multi-channel playback and 2nd capture */
	err = snd_pcm_new(chip->card, chip->card->shortname, 1, 1, 1, &pcm);
	if (err < 0)
		return err;
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_via8233_multi_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_via8233_capture_ops);
	pcm->private_data = chip;
	strcpy(pcm->name, chip->card->shortname);
	chip->pcms[1] = pcm;
	/* set up playback */
	init_viadev(chip, chip->multi_devno, VIA_REG_MULTPLAY_STATUS, 4, 0);
	/* set up capture */
	init_viadev(chip, chip->capture_devno + 1, VIA_REG_CAPTURE_8233_STATUS + 0x10, 7, 1);

	if ((err = snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV_SG,
						         snd_dma_pci_data(chip->pci), 64*1024, 128*1024)) < 0)
		return err;

	return 0;
}

/*
 * create pcm instances for VIA8233A
 */
static int __devinit snd_via8233a_pcm_new(via82xx_t *chip)
{
	snd_pcm_t *pcm;
	int err;

	chip->multi_devno = 0;
	chip->playback_devno = 1;
	chip->capture_devno = 2;
	chip->num_devs = 3;
	chip->intr_mask = 0x03033000; /* FLAG|EOL for rec0, mc, sdx3 */

	/* PCM #0:  multi-channel playback and capture */
	err = snd_pcm_new(chip->card, chip->card->shortname, 0, 1, 1, &pcm);
	if (err < 0)
		return err;
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_via8233_multi_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_via8233_capture_ops);
	pcm->private_data = chip;
	strcpy(pcm->name, chip->card->shortname);
	chip->pcms[0] = pcm;
	/* set up playback */
	init_viadev(chip, chip->multi_devno, VIA_REG_MULTPLAY_STATUS, 4, 0);
	/* capture */
	init_viadev(chip, chip->capture_devno, VIA_REG_CAPTURE_8233_STATUS, 6, 1);

	if ((err = snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV_SG,
							 snd_dma_pci_data(chip->pci), 64*1024, 128*1024)) < 0)
		return err;

	/* SPDIF supported? */
	if (! ac97_can_spdif(chip->ac97))
		return 0;

	/* PCM #1:  DXS3 playback (for spdif) */
	err = snd_pcm_new(chip->card, chip->card->shortname, 1, 1, 0, &pcm);
	if (err < 0)
		return err;
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_via8233_playback_ops);
	pcm->private_data = chip;
	strcpy(pcm->name, chip->card->shortname);
	chip->pcms[1] = pcm;
	/* set up playback */
	init_viadev(chip, chip->playback_devno, 0x30, 3, 0);

	if ((err = snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV_SG,
							 snd_dma_pci_data(chip->pci), 64*1024, 128*1024)) < 0)
		return err;

	return 0;
}

/*
 * create a pcm instance for via686a/b
 */
static int __devinit snd_via686_pcm_new(via82xx_t *chip)
{
	snd_pcm_t *pcm;
	int err;

	chip->playback_devno = 0;
	chip->capture_devno = 1;
	chip->num_devs = 2;
	chip->intr_mask = 0x77; /* FLAG | EOL for PB, CP, FM */

	err = snd_pcm_new(chip->card, chip->card->shortname, 0, 1, 1, &pcm);
	if (err < 0)
		return err;
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_via686_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_via686_capture_ops);
	pcm->private_data = chip;
	strcpy(pcm->name, chip->card->shortname);
	chip->pcms[0] = pcm;
	init_viadev(chip, 0, VIA_REG_PLAYBACK_STATUS, 0, 0);
	init_viadev(chip, 1, VIA_REG_CAPTURE_STATUS, 0, 1);

	if ((err = snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV_SG,
							 snd_dma_pci_data(chip->pci), 64*1024, 128*1024)) < 0)
		return err;

	return 0;
}


/*
 *  Mixer part
 */

static int snd_via8233_capture_source_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	/* formerly they were "Line" and "Mic", but it looks like that they
	 * have nothing to do with the actual physical connections...
	 */
	static char *texts[2] = {
		"Input1", "Input2"
	};
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item >= 2)
		uinfo->value.enumerated.item = 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_via8233_capture_source_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	via82xx_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned long port = chip->port + (kcontrol->id.index ? (VIA_REG_CAPTURE_CHANNEL + 0x10) : VIA_REG_CAPTURE_CHANNEL);
	ucontrol->value.enumerated.item[0] = inb(port) & VIA_REG_CAPTURE_CHANNEL_MIC ? 1 : 0;
	return 0;
}

static int snd_via8233_capture_source_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	via82xx_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned long port = chip->port + (kcontrol->id.index ? (VIA_REG_CAPTURE_CHANNEL + 0x10) : VIA_REG_CAPTURE_CHANNEL);
	u8 val, oval;

	spin_lock_irq(&chip->reg_lock);
	oval = inb(port);
	val = oval & ~VIA_REG_CAPTURE_CHANNEL_MIC;
	if (ucontrol->value.enumerated.item[0])
		val |= VIA_REG_CAPTURE_CHANNEL_MIC;
	if (val != oval)
		outb(val, port);
	spin_unlock_irq(&chip->reg_lock);
	return val != oval;
}

static snd_kcontrol_new_t snd_via8233_capture_source __devinitdata = {
	.name = "Input Source Select",
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.info = snd_via8233_capture_source_info,
	.get = snd_via8233_capture_source_get,
	.put = snd_via8233_capture_source_put,
};

static int snd_via8233_dxs3_spdif_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_via8233_dxs3_spdif_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	via82xx_t *chip = snd_kcontrol_chip(kcontrol);
	u8 val;

	pci_read_config_byte(chip->pci, VIA8233_SPDIF_CTRL, &val);
	ucontrol->value.integer.value[0] = (val & VIA8233_SPDIF_DX3) ? 1 : 0;
	return 0;
}

static int snd_via8233_dxs3_spdif_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	via82xx_t *chip = snd_kcontrol_chip(kcontrol);
	u8 val, oval;

	pci_read_config_byte(chip->pci, VIA8233_SPDIF_CTRL, &oval);
	val = oval & ~VIA8233_SPDIF_DX3;
	if (ucontrol->value.integer.value[0])
		val |= VIA8233_SPDIF_DX3;
	/* save the spdif flag for rate filtering */
	chip->spdif_on = ucontrol->value.integer.value[0] ? 1 : 0;
	if (val != oval) {
		pci_write_config_byte(chip->pci, VIA8233_SPDIF_CTRL, val);
		return 1;
	}
	return 0;
}

static snd_kcontrol_new_t snd_via8233_dxs3_spdif_control __devinitdata = {
	.name = SNDRV_CTL_NAME_IEC958("Output ",NONE,SWITCH),
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.info = snd_via8233_dxs3_spdif_info,
	.get = snd_via8233_dxs3_spdif_get,
	.put = snd_via8233_dxs3_spdif_put,
};

static int snd_via8233_dxs_volume_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = VIA_DXS_MAX_VOLUME;
	return 0;
}

static int snd_via8233_dxs_volume_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	via82xx_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned int idx = snd_ctl_get_ioff(kcontrol, &ucontrol->id);

	ucontrol->value.integer.value[0] = VIA_DXS_MAX_VOLUME - chip->playback_volume[idx][0];
	ucontrol->value.integer.value[1] = VIA_DXS_MAX_VOLUME - chip->playback_volume[idx][1];
	return 0;
}

static int snd_via8233_pcmdxs_volume_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	via82xx_t *chip = snd_kcontrol_chip(kcontrol);
	ucontrol->value.integer.value[0] = VIA_DXS_MAX_VOLUME - chip->playback_volume_c[0];
	ucontrol->value.integer.value[1] = VIA_DXS_MAX_VOLUME - chip->playback_volume_c[1];
	return 0;
}

static int snd_via8233_dxs_volume_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	via82xx_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned int idx = snd_ctl_get_ioff(kcontrol, &ucontrol->id);
	unsigned long port = chip->port + 0x10 * idx;
	unsigned char val;
	int i, change = 0;

	for (i = 0; i < 2; i++) {
		val = ucontrol->value.integer.value[i];
		if (val > VIA_DXS_MAX_VOLUME)
			val = VIA_DXS_MAX_VOLUME;
		val = VIA_DXS_MAX_VOLUME - val;
		change |= val != chip->playback_volume[idx][i];
		if (change) {
			chip->playback_volume[idx][i] = val;
			outb(val, port + VIA_REG_OFS_PLAYBACK_VOLUME_L + i);
		}
	}
	return change;
}

static int snd_via8233_pcmdxs_volume_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	via82xx_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned int idx;
	unsigned char val;
	int i, change = 0;

	for (i = 0; i < 2; i++) {
		val = ucontrol->value.integer.value[i];
		if (val > VIA_DXS_MAX_VOLUME)
			val = VIA_DXS_MAX_VOLUME;
		val = VIA_DXS_MAX_VOLUME - val;
		if (val != chip->playback_volume_c[i]) {
			change = 1;
			chip->playback_volume_c[i] = val;
			for (idx = 0; idx < 4; idx++) {
				unsigned long port = chip->port + 0x10 * idx;
				chip->playback_volume[idx][i] = val;
				outb(val, port + VIA_REG_OFS_PLAYBACK_VOLUME_L + i);
			}
		}
	}
	return change;
}

static snd_kcontrol_new_t snd_via8233_pcmdxs_volume_control __devinitdata = {
	.name = "PCM Playback Volume",
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.info = snd_via8233_dxs_volume_info,
	.get = snd_via8233_pcmdxs_volume_get,
	.put = snd_via8233_pcmdxs_volume_put,
};

static snd_kcontrol_new_t snd_via8233_dxs_volume_control __devinitdata = {
	.name = "VIA DXS Playback Volume",
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.count = 4,
	.info = snd_via8233_dxs_volume_info,
	.get = snd_via8233_dxs_volume_get,
	.put = snd_via8233_dxs_volume_put,
};

/*
 */

static void snd_via82xx_mixer_free_ac97_bus(ac97_bus_t *bus)
{
	via82xx_t *chip = bus->private_data;
	chip->ac97_bus = NULL;
}

static void snd_via82xx_mixer_free_ac97(ac97_t *ac97)
{
	via82xx_t *chip = ac97->private_data;
	chip->ac97 = NULL;
}

static struct ac97_quirk ac97_quirks[] = {
	{
		.subvendor = 0x1106,
		.subdevice = 0x4161,
		.codec_id = 0x56494161, /* VT1612A */
		.name = "Soltek SL-75DRV5",
		.type = AC97_TUNE_NONE
	},
	{	/* FIXME: which codec? */
		.subvendor = 0x1106,
		.subdevice = 0x4161,
		.name = "ASRock K7VT2",
		.type = AC97_TUNE_HP_ONLY
	},
	{
		.subvendor = 0x1019,
		.subdevice = 0x0a81,
		.name = "ECS K7VTA3",
		.type = AC97_TUNE_HP_ONLY
	},
	{
		.subvendor = 0x1019,
		.subdevice = 0x0a85,
		.name = "ECS L7VMM2",
		.type = AC97_TUNE_HP_ONLY
	},
	{
		.subvendor = 0x1849,
		.subdevice = 0x3059,
		.name = "ASRock K7VM2",
		.type = AC97_TUNE_HP_ONLY	/* VT1616 */
	},
	{
		.subvendor = 0x14cd,
		.subdevice = 0x7002,
		.name = "Unknown",
		.type = AC97_TUNE_ALC_JACK
	},
	{
		.subvendor = 0x1071,
		.subdevice = 0x8590,
		.name = "Mitac Mobo",
		.type = AC97_TUNE_ALC_JACK
	},
	{
		.subvendor = 0x161f,
		.subdevice = 0x202b,
		.name = "Arima Notebook",
		.type = AC97_TUNE_HP_ONLY,
	},
	{ } /* terminator */
};

static int __devinit snd_via82xx_mixer_new(via82xx_t *chip, const char *quirk_override)
{
	ac97_template_t ac97;
	int err;
	static ac97_bus_ops_t ops = {
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
	ac97.scaps = AC97_SCAP_SKIP_MODEM;
	if ((err = snd_ac97_mixer(chip->ac97_bus, &ac97, &chip->ac97)) < 0)
		return err;

	snd_ac97_tune_hardware(chip->ac97, ac97_quirks, quirk_override);

	if (chip->chip_type != TYPE_VIA686) {
		/* use slot 10/11 */
		snd_ac97_update_bits(chip->ac97, AC97_EXTENDED_STATUS, 0x03 << 4, 0x03 << 4);
	}

	return 0;
}

#ifdef SUPPORT_JOYSTICK
#define JOYSTICK_ADDR	0x200
static int __devinit snd_via686_create_gameport(via82xx_t *chip, unsigned char *legacy)
{
	struct gameport *gp;
	struct resource *r;

	if (!joystick)
		return -ENODEV;

	r = request_region(JOYSTICK_ADDR, 8, "VIA686 gameport");
	if (!r) {
		printk(KERN_WARNING "via82xx: cannot reserve joystick port 0x%#x\n", JOYSTICK_ADDR);
		return -EBUSY;
	}

	chip->gameport = gp = gameport_allocate_port();
	if (!gp) {
		printk(KERN_ERR "via82xx: cannot allocate memory for gameport\n");
		release_and_free_resource(r);
		return -ENOMEM;
	}

	gameport_set_name(gp, "VIA686 Gameport");
	gameport_set_phys(gp, "pci%s/gameport0", pci_name(chip->pci));
	gameport_set_dev_parent(gp, &chip->pci->dev);
	gp->io = JOYSTICK_ADDR;
	gameport_set_port_data(gp, r);

	/* Enable legacy joystick port */
	*legacy |= VIA_FUNC_ENABLE_GAME;
	pci_write_config_byte(chip->pci, VIA_FUNC_ENABLE, *legacy);

	gameport_register_port(chip->gameport);

	return 0;
}

static void snd_via686_free_gameport(via82xx_t *chip)
{
	if (chip->gameport) {
		struct resource *r = gameport_get_port_data(chip->gameport);

		gameport_unregister_port(chip->gameport);
		chip->gameport = NULL;
		release_and_free_resource(r);
	}
}
#else
static inline int snd_via686_create_gameport(via82xx_t *chip, unsigned char *legacy)
{
	return -ENOSYS;
}
static inline void snd_via686_free_gameport(via82xx_t *chip) { }
#endif


/*
 *
 */

static int __devinit snd_via8233_init_misc(via82xx_t *chip)
{
	int i, err, caps;
	unsigned char val;

	caps = chip->chip_type == TYPE_VIA8233A ? 1 : 2;
	for (i = 0; i < caps; i++) {
		snd_via8233_capture_source.index = i;
		err = snd_ctl_add(chip->card, snd_ctl_new1(&snd_via8233_capture_source, chip));
		if (err < 0)
			return err;
	}
	if (ac97_can_spdif(chip->ac97)) {
		err = snd_ctl_add(chip->card, snd_ctl_new1(&snd_via8233_dxs3_spdif_control, chip));
		if (err < 0)
			return err;
	}
	if (chip->chip_type != TYPE_VIA8233A) {
		/* when no h/w PCM volume control is found, use DXS volume control
		 * as the PCM vol control
		 */
		snd_ctl_elem_id_t sid;
		memset(&sid, 0, sizeof(sid));
		strcpy(sid.name, "PCM Playback Volume");
		sid.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
		if (! snd_ctl_find_id(chip->card, &sid)) {
			snd_printd(KERN_INFO "Using DXS as PCM Playback\n");
			err = snd_ctl_add(chip->card, snd_ctl_new1(&snd_via8233_pcmdxs_volume_control, chip));
			if (err < 0)
				return err;
		}
		else /* Using DXS when PCM emulation is enabled is really weird */
		{
			/* Standalone DXS controls */
			err = snd_ctl_add(chip->card, snd_ctl_new1(&snd_via8233_dxs_volume_control, chip));
			if (err < 0)
				return err;
		}
	}
	/* select spdif data slot 10/11 */
	pci_read_config_byte(chip->pci, VIA8233_SPDIF_CTRL, &val);
	val = (val & ~VIA8233_SPDIF_SLOT_MASK) | VIA8233_SPDIF_SLOT_1011;
	val &= ~VIA8233_SPDIF_DX3; /* SPDIF off as default */
	pci_write_config_byte(chip->pci, VIA8233_SPDIF_CTRL, val);

	return 0;
}

static int __devinit snd_via686_init_misc(via82xx_t *chip)
{
	unsigned char legacy, legacy_cfg;
	int rev_h = 0;

	legacy = chip->old_legacy;
	legacy_cfg = chip->old_legacy_cfg;
	legacy |= VIA_FUNC_MIDI_IRQMASK;	/* FIXME: correct? (disable MIDI) */
	legacy &= ~VIA_FUNC_ENABLE_GAME;	/* disable joystick */
	if (chip->revision >= VIA_REV_686_H) {
		rev_h = 1;
		if (mpu_port >= 0x200) {	/* force MIDI */
			mpu_port &= 0xfffc;
			pci_write_config_dword(chip->pci, 0x18, mpu_port | 0x01);
#ifdef CONFIG_PM
			chip->mpu_port_saved = mpu_port;
#endif
		} else {
			mpu_port = pci_resource_start(chip->pci, 2);
		}
	} else {
		switch (mpu_port) {	/* force MIDI */
		case 0x300:
		case 0x310:
		case 0x320:
		case 0x330:
			legacy_cfg &= ~(3 << 2);
			legacy_cfg |= (mpu_port & 0x0030) >> 2;
			break;
		default:			/* no, use BIOS settings */
			if (legacy & VIA_FUNC_ENABLE_MIDI)
				mpu_port = 0x300 + ((legacy_cfg & 0x000c) << 2);
			break;
		}
	}
	if (mpu_port >= 0x200 &&
	    (chip->mpu_res = request_region(mpu_port, 2, "VIA82xx MPU401"))
	    != NULL) {
		if (rev_h)
			legacy |= VIA_FUNC_MIDI_PNP;	/* enable PCI I/O 2 */
		legacy |= VIA_FUNC_ENABLE_MIDI;
	} else {
		if (rev_h)
			legacy &= ~VIA_FUNC_MIDI_PNP;	/* disable PCI I/O 2 */
		legacy &= ~VIA_FUNC_ENABLE_MIDI;
		mpu_port = 0;
	}

	pci_write_config_byte(chip->pci, VIA_FUNC_ENABLE, legacy);
	pci_write_config_byte(chip->pci, VIA_PNP_CONTROL, legacy_cfg);
	if (chip->mpu_res) {
		if (snd_mpu401_uart_new(chip->card, 0, MPU401_HW_VIA686A,
					mpu_port, 1,
					chip->irq, 0, &chip->rmidi) < 0) {
			printk(KERN_WARNING "unable to initialize MPU-401"
			       " at 0x%lx, skipping\n", mpu_port);
			legacy &= ~VIA_FUNC_ENABLE_MIDI;
		} else {
			legacy &= ~VIA_FUNC_MIDI_IRQMASK;	/* enable MIDI interrupt */
		}
		pci_write_config_byte(chip->pci, VIA_FUNC_ENABLE, legacy);
	}

	snd_via686_create_gameport(chip, &legacy);

#ifdef CONFIG_PM
	chip->legacy_saved = legacy;
	chip->legacy_cfg_saved = legacy_cfg;
#endif

	return 0;
}


/*
 * proc interface
 */
static void snd_via82xx_proc_read(snd_info_entry_t *entry, snd_info_buffer_t *buffer)
{
	via82xx_t *chip = entry->private_data;
	int i;
	
	snd_iprintf(buffer, "%s\n\n", chip->card->longname);
	for (i = 0; i < 0xa0; i += 4) {
		snd_iprintf(buffer, "%02x: %08x\n", i, inl(chip->port + i));
	}
}

static void __devinit snd_via82xx_proc_init(via82xx_t *chip)
{
	snd_info_entry_t *entry;

	if (! snd_card_proc_new(chip->card, "via82xx", &entry))
		snd_info_set_text_ops(entry, chip, 1024, snd_via82xx_proc_read);
}

/*
 *
 */

static int snd_via82xx_chip_init(via82xx_t *chip)
{
	unsigned int val;
	unsigned long end_time;
	unsigned char pval;

#if 0 /* broken on K7M? */
	if (chip->chip_type == TYPE_VIA686)
		/* disable all legacy ports */
		pci_write_config_byte(chip->pci, VIA_FUNC_ENABLE, 0);
#endif
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
		/* note - FM data out has trouble with non VRA codecs !! */
		pci_write_config_byte(chip->pci, VIA_ACLINK_CTRL, VIA_ACLINK_CTRL_INIT);
		udelay(100);
	}
	
	/* Make sure VRA is enabled, in case we didn't do a
	 * complete codec reset, above */
	pci_read_config_byte(chip->pci, VIA_ACLINK_CTRL, &pval);
	if ((pval & VIA_ACLINK_CTRL_INIT) != VIA_ACLINK_CTRL_INIT) {
		/* ACLink on, deassert ACLink reset, VSR, SGD data out */
		/* note - FM data out has trouble with non VRA codecs !! */
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
		snd_printk(KERN_ERR "AC'97 codec is not ready [0x%x]\n", val);

#if 0 /* FIXME: we don't support the second codec yet so skip the detection now.. */
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
		schedule_timeout_interruptible(1);
	} while (time_before(jiffies, end_time));
	/* This is ok, the most of motherboards have only one codec */

      __ac97_ok2:
#endif

	if (chip->chip_type == TYPE_VIA686) {
		/* route FM trap to IRQ, disable FM trap */
		pci_write_config_byte(chip->pci, VIA_FM_NMI_CTRL, 0);
		/* disable all GPI interrupts */
		outl(0, VIAREG(chip, GPI_INTR));
	}

	if (chip->chip_type != TYPE_VIA686) {
		/* Workaround for Award BIOS bug:
		 * DXS channels don't work properly with VRA if MC97 is disabled.
		 */
		struct pci_dev *pci;
		pci = pci_get_device(0x1106, 0x3068, NULL); /* MC97 */
		if (pci) {
			unsigned char data;
			pci_read_config_byte(pci, 0x44, &data);
			pci_write_config_byte(pci, 0x44, data | 0x40);
			pci_dev_put(pci);
		}
	}

	if (chip->chip_type != TYPE_VIA8233A) {
		int i, idx;
		for (idx = 0; idx < 4; idx++) {
			unsigned long port = chip->port + 0x10 * idx;
			for (i = 0; i < 2; i++) {
				chip->playback_volume[idx][i]=chip->playback_volume_c[i];
				outb(chip->playback_volume_c[i], port + VIA_REG_OFS_PLAYBACK_VOLUME_L + i);
			}
		}
	}

	return 0;
}

#ifdef CONFIG_PM
/*
 * power management
 */
static int snd_via82xx_suspend(snd_card_t *card, pm_message_t state)
{
	via82xx_t *chip = card->pm_private_data;
	int i;

	for (i = 0; i < 2; i++)
		if (chip->pcms[i])
			snd_pcm_suspend_all(chip->pcms[i]);
	for (i = 0; i < chip->num_devs; i++)
		snd_via82xx_channel_reset(chip, &chip->devs[i]);
	synchronize_irq(chip->irq);
	snd_ac97_suspend(chip->ac97);

	/* save misc values */
	if (chip->chip_type != TYPE_VIA686) {
		pci_read_config_byte(chip->pci, VIA8233_SPDIF_CTRL, &chip->spdif_ctrl_saved);
		chip->capture_src_saved[0] = inb(chip->port + VIA_REG_CAPTURE_CHANNEL);
		chip->capture_src_saved[1] = inb(chip->port + VIA_REG_CAPTURE_CHANNEL + 0x10);
	}

	pci_set_power_state(chip->pci, 3);
	pci_disable_device(chip->pci);
	return 0;
}

static int snd_via82xx_resume(snd_card_t *card)
{
	via82xx_t *chip = card->pm_private_data;
	int i;

	pci_enable_device(chip->pci);
	pci_set_power_state(chip->pci, 0);

	snd_via82xx_chip_init(chip);

	if (chip->chip_type == TYPE_VIA686) {
		if (chip->mpu_port_saved)
			pci_write_config_dword(chip->pci, 0x18, chip->mpu_port_saved | 0x01);
		pci_write_config_byte(chip->pci, VIA_FUNC_ENABLE, chip->legacy_saved);
		pci_write_config_byte(chip->pci, VIA_PNP_CONTROL, chip->legacy_cfg_saved);
	} else {
		pci_write_config_byte(chip->pci, VIA8233_SPDIF_CTRL, chip->spdif_ctrl_saved);
		outb(chip->capture_src_saved[0], chip->port + VIA_REG_CAPTURE_CHANNEL);
		outb(chip->capture_src_saved[1], chip->port + VIA_REG_CAPTURE_CHANNEL + 0x10);
	}

	snd_ac97_resume(chip->ac97);

	for (i = 0; i < chip->num_devs; i++)
		snd_via82xx_channel_reset(chip, &chip->devs[i]);

	return 0;
}
#endif /* CONFIG_PM */

static int snd_via82xx_free(via82xx_t *chip)
{
	unsigned int i;

	if (chip->irq < 0)
		goto __end_hw;
	/* disable interrupts */
	for (i = 0; i < chip->num_devs; i++)
		snd_via82xx_channel_reset(chip, &chip->devs[i]);
	synchronize_irq(chip->irq);
      __end_hw:
	if (chip->irq >= 0)
		free_irq(chip->irq, (void *)chip);
	release_and_free_resource(chip->mpu_res);
	pci_release_regions(chip->pci);

	if (chip->chip_type == TYPE_VIA686) {
		snd_via686_free_gameport(chip);
		pci_write_config_byte(chip->pci, VIA_FUNC_ENABLE, chip->old_legacy);
		pci_write_config_byte(chip->pci, VIA_PNP_CONTROL, chip->old_legacy_cfg);
	}
	pci_disable_device(chip->pci);
	kfree(chip);
	return 0;
}

static int snd_via82xx_dev_free(snd_device_t *device)
{
	via82xx_t *chip = device->device_data;
	return snd_via82xx_free(chip);
}

static int __devinit snd_via82xx_create(snd_card_t * card,
					struct pci_dev *pci,
					int chip_type,
					int revision,
					unsigned int ac97_clock,
					via82xx_t ** r_via)
{
	via82xx_t *chip;
	int err;
        static snd_device_ops_t ops = {
		.dev_free =	snd_via82xx_dev_free,
        };

	if ((err = pci_enable_device(pci)) < 0)
		return err;

	if ((chip = kzalloc(sizeof(*chip), GFP_KERNEL)) == NULL) {
		pci_disable_device(pci);
		return -ENOMEM;
	}

	chip->chip_type = chip_type;
	chip->revision = revision;

	spin_lock_init(&chip->reg_lock);
	spin_lock_init(&chip->rates[0].lock);
	spin_lock_init(&chip->rates[1].lock);
	chip->card = card;
	chip->pci = pci;
	chip->irq = -1;

	pci_read_config_byte(pci, VIA_FUNC_ENABLE, &chip->old_legacy);
	pci_read_config_byte(pci, VIA_PNP_CONTROL, &chip->old_legacy_cfg);
	pci_write_config_byte(chip->pci, VIA_FUNC_ENABLE,
			      chip->old_legacy & ~(VIA_FUNC_ENABLE_SB|VIA_FUNC_ENABLE_FM));

	if ((err = pci_request_regions(pci, card->driver)) < 0) {
		kfree(chip);
		pci_disable_device(pci);
		return err;
	}
	chip->port = pci_resource_start(pci, 0);
	if (request_irq(pci->irq,
			chip_type == TYPE_VIA8233 ?
			snd_via8233_interrupt :	snd_via686_interrupt,
			SA_INTERRUPT|SA_SHIRQ,
			card->driver, (void *)chip)) {
		snd_printk(KERN_ERR "unable to grab IRQ %d\n", pci->irq);
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

	snd_card_set_dev(card, &pci->dev);

	*r_via = chip;
	return 0;
}

struct via823x_info {
	int revision;
	char *name;
	int type;
};
static struct via823x_info via823x_cards[] __devinitdata = {
	{ VIA_REV_PRE_8233, "VIA 8233-Pre", TYPE_VIA8233 },
	{ VIA_REV_8233C, "VIA 8233C", TYPE_VIA8233 },
	{ VIA_REV_8233, "VIA 8233", TYPE_VIA8233 },
	{ VIA_REV_8233A, "VIA 8233A", TYPE_VIA8233A },
	{ VIA_REV_8235, "VIA 8235", TYPE_VIA8233 },
	{ VIA_REV_8237, "VIA 8237", TYPE_VIA8233 },
};

/*
 * auto detection of DXS channel supports.
 */
struct dxs_whitelist {
	unsigned short subvendor;
	unsigned short subdevice; 
	unsigned short mask; 
	short action;	/* new dxs_support value */
};

static int __devinit check_dxs_list(struct pci_dev *pci)
{
	static struct dxs_whitelist whitelist[] = {
		{ .subvendor = 0x1005, .subdevice = 0x4710, .action = VIA_DXS_ENABLE }, /* Avance Logic Mobo */
		{ .subvendor = 0x1019, .subdevice = 0x0996, .action = VIA_DXS_48K },
		{ .subvendor = 0x1019, .subdevice = 0x0a81, .action = VIA_DXS_NO_VRA }, /* ECS K7VTA3 v8.0 */
		{ .subvendor = 0x1019, .subdevice = 0x0a85, .action = VIA_DXS_NO_VRA }, /* ECS L7VMM2 */
		{ .subvendor = 0x1019, .subdevice = 0xa101, .action = VIA_DXS_SRC },
		{ .subvendor = 0x1025, .subdevice = 0x0033, .action = VIA_DXS_NO_VRA }, /* Acer Inspire 1353LM */
		{ .subvendor = 0x1025, .subdevice = 0x0046, .action = VIA_DXS_SRC }, /* Acer Aspire 1524 WLMi */
		{ .subvendor = 0x1043, .subdevice = 0x8095, .action = VIA_DXS_NO_VRA }, /* ASUS A7V8X (FIXME: possibly VIA_DXS_ENABLE?)*/
		{ .subvendor = 0x1043, .subdevice = 0x80a1, .action = VIA_DXS_NO_VRA }, /* ASUS A7V8-X */
		{ .subvendor = 0x1043, .subdevice = 0x80b0, .action = VIA_DXS_NO_VRA }, /* ASUS A7V600 & K8V*/ 
		{ .subvendor = 0x1043, .subdevice = 0x810d, .action = VIA_DXS_SRC }, /* ASUS */
		{ .subvendor = 0x1043, .subdevice = 0x812a, .action = VIA_DXS_SRC    }, /* ASUS A8V Deluxe */ 
		{ .subvendor = 0x1071, .subdevice = 0x8375, .action = VIA_DXS_NO_VRA }, /* Vobis/Yakumo/Mitac notebook */
		{ .subvendor = 0x1071, .subdevice = 0x8399, .action = VIA_DXS_NO_VRA }, /* Umax AB 595T (VIA K8N800A - VT8237) */
		{ .subvendor = 0x10cf, .subdevice = 0x118e, .action = VIA_DXS_ENABLE }, /* FSC laptop */
		{ .subvendor = 0x1106, .subdevice = 0x4161, .action = VIA_DXS_NO_VRA }, /* ASRock K7VT2 */
		{ .subvendor = 0x1106, .subdevice = 0x4552, .action = VIA_DXS_NO_VRA }, /* QDI Kudoz 7X/600-6AL */
		{ .subvendor = 0x1106, .subdevice = 0xaa01, .action = VIA_DXS_NO_VRA }, /* EPIA MII */
		{ .subvendor = 0x1106, .subdevice = 0xc001, .action = VIA_DXS_SRC }, /* Insight P4-ITX */
		{ .subvendor = 0x1297, .subdevice = 0xa232, .action = VIA_DXS_ENABLE }, /* Shuttle ?? */
		{ .subvendor = 0x1297, .subdevice = 0xc160, .action = VIA_DXS_ENABLE }, /* Shuttle SK41G */
		{ .subvendor = 0x1458, .subdevice = 0xa002, .action = VIA_DXS_ENABLE }, /* Gigabyte GA-7VAXP */
		{ .subvendor = 0x1462, .subdevice = 0x0080, .action = VIA_DXS_SRC }, /* MSI K8T Neo-FIS2R */
		{ .subvendor = 0x1462, .subdevice = 0x0430, .action = VIA_DXS_SRC }, /* MSI 7142 (K8MM-V) */
		{ .subvendor = 0x1462, .subdevice = 0x3800, .action = VIA_DXS_ENABLE }, /* MSI KT266 */
		{ .subvendor = 0x1462, .subdevice = 0x5901, .action = VIA_DXS_NO_VRA }, /* MSI KT6 Delta-SR */
		{ .subvendor = 0x1462, .subdevice = 0x7023, .action = VIA_DXS_NO_VRA }, /* MSI K8T Neo2-FI */
		{ .subvendor = 0x1462, .subdevice = 0x7120, .action = VIA_DXS_ENABLE }, /* MSI KT4V */
		{ .subvendor = 0x1462, .subdevice = 0x7142, .action = VIA_DXS_ENABLE }, /* MSI K8MM-V */
		{ .subvendor = 0x147b, .subdevice = 0x1401, .action = VIA_DXS_ENABLE }, /* ABIT KD7(-RAID) */
		{ .subvendor = 0x147b, .subdevice = 0x1411, .action = VIA_DXS_ENABLE }, /* ABIT VA-20 */
		{ .subvendor = 0x147b, .subdevice = 0x1413, .action = VIA_DXS_ENABLE }, /* ABIT KV8 Pro */
		{ .subvendor = 0x147b, .subdevice = 0x1415, .action = VIA_DXS_NO_VRA }, /* Abit AV8 */
		{ .subvendor = 0x14ff, .subdevice = 0x0403, .action = VIA_DXS_ENABLE }, /* Twinhead mobo */
		{ .subvendor = 0x14ff, .subdevice = 0x0408, .action = VIA_DXS_SRC }, /* Twinhead laptop */
		{ .subvendor = 0x1558, .subdevice = 0x4701, .action = VIA_DXS_SRC }, /* Clevo D470 */
		{ .subvendor = 0x1584, .subdevice = 0x8120, .action = VIA_DXS_ENABLE }, /* Gericom/Targa/Vobis/Uniwill laptop */
		{ .subvendor = 0x1584, .subdevice = 0x8123, .action = VIA_DXS_NO_VRA }, /* Uniwill (Targa Visionary XP-210) */
		{ .subvendor = 0x161f, .subdevice = 0x202b, .action = VIA_DXS_NO_VRA }, /* Amira Note book */
		{ .subvendor = 0x161f, .subdevice = 0x2032, .action = VIA_DXS_48K }, /* m680x machines */
		{ .subvendor = 0x1631, .subdevice = 0xe004, .action = VIA_DXS_ENABLE }, /* Easy Note 3174, Packard Bell */
		{ .subvendor = 0x1695, .subdevice = 0x3005, .action = VIA_DXS_ENABLE }, /* EPoX EP-8K9A */
		{ .subvendor = 0x1849, .subdevice = 0x3059, .action = VIA_DXS_NO_VRA }, /* ASRock K7VM2 */
		{ .subvendor = 0x1919, .subdevice = 0x200a, .action = VIA_DXS_NO_VRA }, /* Soltek SL-K8Tpro-939 */
		{ .subvendor = 0x4005, .subdevice = 0x4710, .action = VIA_DXS_SRC },	/* MSI K7T266 Pro2 (MS-6380 V2.0) BIOS 3.7 */
		{ } /* terminator */
	};
	struct dxs_whitelist *w;
	unsigned short subsystem_vendor;
	unsigned short subsystem_device;

	pci_read_config_word(pci, PCI_SUBSYSTEM_VENDOR_ID, &subsystem_vendor);
	pci_read_config_word(pci, PCI_SUBSYSTEM_ID, &subsystem_device);

	for (w = whitelist; w->subvendor; w++) {
		if (w->subvendor != subsystem_vendor)
			continue;
		if (w->mask) {
			if ((w->mask & subsystem_device) == w->subdevice)
				return w->action;
		} else {
			if (subsystem_device == w->subdevice)
				return w->action;
		}
	}

	/*
	 * not detected, try 48k rate only to be sure.
	 */
	printk(KERN_INFO "via82xx: Assuming DXS channels with 48k fixed sample rate.\n");
	printk(KERN_INFO "         Please try dxs_support=5 option\n");
	printk(KERN_INFO "         and report if it works on your machine.\n");
	printk(KERN_INFO "         For more details, read ALSA-Configuration.txt.\n");
	return VIA_DXS_48K;
};

static int __devinit snd_via82xx_probe(struct pci_dev *pci,
				       const struct pci_device_id *pci_id)
{
	snd_card_t *card;
	via82xx_t *chip;
	unsigned char revision;
	int chip_type = 0, card_type;
	unsigned int i;
	int err;

	card = snd_card_new(index, id, THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;

	card_type = pci_id->driver_data;
	pci_read_config_byte(pci, PCI_REVISION_ID, &revision);
	switch (card_type) {
	case TYPE_CARD_VIA686:
		strcpy(card->driver, "VIA686A");
		sprintf(card->shortname, "VIA 82C686A/B rev%x", revision);
		chip_type = TYPE_VIA686;
		break;
	case TYPE_CARD_VIA8233:
		chip_type = TYPE_VIA8233;
		sprintf(card->shortname, "VIA 823x rev%x", revision);
		for (i = 0; i < ARRAY_SIZE(via823x_cards); i++) {
			if (revision == via823x_cards[i].revision) {
				chip_type = via823x_cards[i].type;
				strcpy(card->shortname, via823x_cards[i].name);
				break;
			}
		}
		if (chip_type != TYPE_VIA8233A) {
			if (dxs_support == VIA_DXS_AUTO)
				dxs_support = check_dxs_list(pci);
			/* force to use VIA8233 or 8233A model according to
			 * dxs_support module option
			 */
			if (dxs_support == VIA_DXS_DISABLE)
				chip_type = TYPE_VIA8233A;
			else
				chip_type = TYPE_VIA8233;
		}
		if (chip_type == TYPE_VIA8233A)
			strcpy(card->driver, "VIA8233A");
		else if (revision >= VIA_REV_8237)
			strcpy(card->driver, "VIA8237"); /* no slog assignment */
		else
			strcpy(card->driver, "VIA8233");
		break;
	default:
		snd_printk(KERN_ERR "invalid card type %d\n", card_type);
		err = -EINVAL;
		goto __error;
	}
		
	if ((err = snd_via82xx_create(card, pci, chip_type, revision,
				      ac97_clock, &chip)) < 0)
		goto __error;
	if ((err = snd_via82xx_mixer_new(chip, ac97_quirk)) < 0)
		goto __error;

	if (chip_type == TYPE_VIA686) {
		if ((err = snd_via686_pcm_new(chip)) < 0 ||
		    (err = snd_via686_init_misc(chip)) < 0)
			goto __error;
	} else {
		if (chip_type == TYPE_VIA8233A) {
			if ((err = snd_via8233a_pcm_new(chip)) < 0)
				goto __error;
			// chip->dxs_fixed = 1; /* FIXME: use 48k for DXS #3? */
		} else {
			if ((err = snd_via8233_pcm_new(chip)) < 0)
				goto __error;
			if (dxs_support == VIA_DXS_48K)
				chip->dxs_fixed = 1;
			else if (dxs_support == VIA_DXS_NO_VRA)
				chip->no_vra = 1;
			else if (dxs_support == VIA_DXS_SRC) {
				chip->no_vra = 1;
				chip->dxs_src = 1;
			}
		}
		if ((err = snd_via8233_init_misc(chip)) < 0)
			goto __error;
	}

	snd_card_set_pm_callback(card, snd_via82xx_suspend, snd_via82xx_resume, chip);

	/* disable interrupts */
	for (i = 0; i < chip->num_devs; i++)
		snd_via82xx_channel_reset(chip, &chip->devs[i]);

	snprintf(card->longname, sizeof(card->longname),
		 "%s with %s at %#lx, irq %d", card->shortname,
		 snd_ac97_get_short_name(chip->ac97), chip->port, chip->irq);

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

static void __devexit snd_via82xx_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
	pci_set_drvdata(pci, NULL);
}

static struct pci_driver driver = {
	.name = "VIA 82xx Audio",
	.owner = THIS_MODULE,
	.id_table = snd_via82xx_ids,
	.probe = snd_via82xx_probe,
	.remove = __devexit_p(snd_via82xx_remove),
	SND_PCI_PM_CALLBACKS
};

static int __init alsa_card_via82xx_init(void)
{
	return pci_register_driver(&driver);
}

static void __exit alsa_card_via82xx_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(alsa_card_via82xx_init)
module_exit(alsa_card_via82xx_exit)
