/*
 *
 *  hda_intel.c - Implementation of primary alsa driver code base for Intel HD Audio.
 *
 *  Copyright(c) 2004 Intel Corporation. All rights reserved.
 *
 *  Copyright (c) 2004 Takashi Iwai <tiwai@suse.de>
 *                     PeiSen Hou <pshou@realtek.com.tw>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, write to the Free Software Foundation, Inc., 59
 *  Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 *  CONTACTS:
 *
 *  Matt Jared		matt.jared@intel.com
 *  Andy Kopp		andy.kopp@intel.com
 *  Dan Kogan		dan.d.kogan@intel.com
 *
 *  CHANGES:
 *
 *  2004.12.01	Major rewrite by tiwai, merged the work of pshou
 * 
 */

#include <sound/driver.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <sound/core.h>
#include <sound/initval.h>
#include "hda_codec.h"


static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;
static char *model[SNDRV_CARDS];
static int position_fix[SNDRV_CARDS];

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for Intel HD audio interface.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for Intel HD audio interface.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable Intel HD audio interface.");
module_param_array(model, charp, NULL, 0444);
MODULE_PARM_DESC(model, "Use the given board model.");
module_param_array(position_fix, int, NULL, 0444);
MODULE_PARM_DESC(position_fix, "Fix DMA pointer (0 = auto, 1 = none, 2 = POSBUF, 3 = FIFO size).");

MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Intel, ICH6},"
			 "{Intel, ICH6M},"
			 "{Intel, ICH7},"
			 "{Intel, ESB2},"
			 "{ATI, SB450},"
			 "{VIA, VT8251},"
			 "{VIA, VT8237A},"
			 "{SiS, SIS966},"
			 "{ULI, M5461}}");
MODULE_DESCRIPTION("Intel HDA driver");

#define SFX	"hda-intel: "

/*
 * registers
 */
#define ICH6_REG_GCAP			0x00
#define ICH6_REG_VMIN			0x02
#define ICH6_REG_VMAJ			0x03
#define ICH6_REG_OUTPAY			0x04
#define ICH6_REG_INPAY			0x06
#define ICH6_REG_GCTL			0x08
#define ICH6_REG_WAKEEN			0x0c
#define ICH6_REG_STATESTS		0x0e
#define ICH6_REG_GSTS			0x10
#define ICH6_REG_INTCTL			0x20
#define ICH6_REG_INTSTS			0x24
#define ICH6_REG_WALCLK			0x30
#define ICH6_REG_SYNC			0x34	
#define ICH6_REG_CORBLBASE		0x40
#define ICH6_REG_CORBUBASE		0x44
#define ICH6_REG_CORBWP			0x48
#define ICH6_REG_CORBRP			0x4A
#define ICH6_REG_CORBCTL		0x4c
#define ICH6_REG_CORBSTS		0x4d
#define ICH6_REG_CORBSIZE		0x4e

#define ICH6_REG_RIRBLBASE		0x50
#define ICH6_REG_RIRBUBASE		0x54
#define ICH6_REG_RIRBWP			0x58
#define ICH6_REG_RINTCNT		0x5a
#define ICH6_REG_RIRBCTL		0x5c
#define ICH6_REG_RIRBSTS		0x5d
#define ICH6_REG_RIRBSIZE		0x5e

#define ICH6_REG_IC			0x60
#define ICH6_REG_IR			0x64
#define ICH6_REG_IRS			0x68
#define   ICH6_IRS_VALID	(1<<1)
#define   ICH6_IRS_BUSY		(1<<0)

#define ICH6_REG_DPLBASE		0x70
#define ICH6_REG_DPUBASE		0x74
#define   ICH6_DPLBASE_ENABLE	0x1	/* Enable position buffer */

/* SD offset: SDI0=0x80, SDI1=0xa0, ... SDO3=0x160 */
enum { SDI0, SDI1, SDI2, SDI3, SDO0, SDO1, SDO2, SDO3 };

/* stream register offsets from stream base */
#define ICH6_REG_SD_CTL			0x00
#define ICH6_REG_SD_STS			0x03
#define ICH6_REG_SD_LPIB		0x04
#define ICH6_REG_SD_CBL			0x08
#define ICH6_REG_SD_LVI			0x0c
#define ICH6_REG_SD_FIFOW		0x0e
#define ICH6_REG_SD_FIFOSIZE		0x10
#define ICH6_REG_SD_FORMAT		0x12
#define ICH6_REG_SD_BDLPL		0x18
#define ICH6_REG_SD_BDLPU		0x1c

/* PCI space */
#define ICH6_PCIREG_TCSEL	0x44

/*
 * other constants
 */

/* max number of SDs */
/* ICH, ATI and VIA have 4 playback and 4 capture */
#define ICH6_CAPTURE_INDEX	0
#define ICH6_NUM_CAPTURE	4
#define ICH6_PLAYBACK_INDEX	4
#define ICH6_NUM_PLAYBACK	4

/* ULI has 6 playback and 5 capture */
#define ULI_CAPTURE_INDEX	0
#define ULI_NUM_CAPTURE		5
#define ULI_PLAYBACK_INDEX	5
#define ULI_NUM_PLAYBACK	6

/* this number is statically defined for simplicity */
#define MAX_AZX_DEV		16

/* max number of fragments - we may use more if allocating more pages for BDL */
#define BDL_SIZE		PAGE_ALIGN(8192)
#define AZX_MAX_FRAG		(BDL_SIZE / (MAX_AZX_DEV * 16))
/* max buffer size - no h/w limit, you can increase as you like */
#define AZX_MAX_BUF_SIZE	(1024*1024*1024)
/* max number of PCM devics per card */
#define AZX_MAX_AUDIO_PCMS	6
#define AZX_MAX_MODEM_PCMS	2
#define AZX_MAX_PCMS		(AZX_MAX_AUDIO_PCMS + AZX_MAX_MODEM_PCMS)

/* RIRB int mask: overrun[2], response[0] */
#define RIRB_INT_RESPONSE	0x01
#define RIRB_INT_OVERRUN	0x04
#define RIRB_INT_MASK		0x05

/* STATESTS int mask: SD2,SD1,SD0 */
#define STATESTS_INT_MASK	0x07
#define AZX_MAX_CODECS		4

/* SD_CTL bits */
#define SD_CTL_STREAM_RESET	0x01	/* stream reset bit */
#define SD_CTL_DMA_START	0x02	/* stream DMA start bit */
#define SD_CTL_STREAM_TAG_MASK	(0xf << 20)
#define SD_CTL_STREAM_TAG_SHIFT	20

/* SD_CTL and SD_STS */
#define SD_INT_DESC_ERR		0x10	/* descriptor error interrupt */
#define SD_INT_FIFO_ERR		0x08	/* FIFO error interrupt */
#define SD_INT_COMPLETE		0x04	/* completion interrupt */
#define SD_INT_MASK		(SD_INT_DESC_ERR|SD_INT_FIFO_ERR|SD_INT_COMPLETE)

/* SD_STS */
#define SD_STS_FIFO_READY	0x20	/* FIFO ready */

/* INTCTL and INTSTS */
#define ICH6_INT_ALL_STREAM	0xff		/* all stream interrupts */
#define ICH6_INT_CTRL_EN	0x40000000	/* controller interrupt enable bit */
#define ICH6_INT_GLOBAL_EN	0x80000000	/* global interrupt enable bit */

/* GCTL unsolicited response enable bit */
#define ICH6_GCTL_UREN		(1<<8)

/* GCTL reset bit */
#define ICH6_GCTL_RESET		(1<<0)

/* CORB/RIRB control, read/write pointer */
#define ICH6_RBCTL_DMA_EN	0x02	/* enable DMA */
#define ICH6_RBCTL_IRQ_EN	0x01	/* enable IRQ */
#define ICH6_RBRWP_CLR		0x8000	/* read/write pointer clear */
/* below are so far hardcoded - should read registers in future */
#define ICH6_MAX_CORB_ENTRIES	256
#define ICH6_MAX_RIRB_ENTRIES	256

/* position fix mode */
enum {
	POS_FIX_AUTO,
	POS_FIX_NONE,
	POS_FIX_POSBUF,
	POS_FIX_FIFO,
};

/* Defines for ATI HD Audio support in SB450 south bridge */
#define ATI_SB450_HDAUDIO_MISC_CNTR2_ADDR   0x42
#define ATI_SB450_HDAUDIO_ENABLE_SNOOP      0x02


/*
 * Use CORB/RIRB for communication from/to codecs.
 * This is the way recommended by Intel (see below).
 */
#define USE_CORB_RIRB

/*
 */

typedef struct snd_azx azx_t;
typedef struct snd_azx_rb azx_rb_t;
typedef struct snd_azx_dev azx_dev_t;

struct snd_azx_dev {
	u32 *bdl;			/* virtual address of the BDL */
	dma_addr_t bdl_addr;		/* physical address of the BDL */
	volatile u32 *posbuf;			/* position buffer pointer */

	unsigned int bufsize;		/* size of the play buffer in bytes */
	unsigned int fragsize;		/* size of each period in bytes */
	unsigned int frags;		/* number for period in the play buffer */
	unsigned int fifo_size;		/* FIFO size */
	unsigned int last_pos;		/* last updated period position */

	void __iomem *sd_addr;		/* stream descriptor pointer */

	u32 sd_int_sta_mask;		/* stream int status mask */

	/* pcm support */
	snd_pcm_substream_t *substream;	/* assigned substream, set in PCM open */
	unsigned int format_val;	/* format value to be set in the controller and the codec */
	unsigned char stream_tag;	/* assigned stream */
	unsigned char index;		/* stream index */

	unsigned int opened: 1;
	unsigned int running: 1;
	unsigned int period_updating: 1;
};

/* CORB/RIRB */
struct snd_azx_rb {
	u32 *buf;		/* CORB/RIRB buffer
				 * Each CORB entry is 4byte, RIRB is 8byte
				 */
	dma_addr_t addr;	/* physical address of CORB/RIRB buffer */
	/* for RIRB */
	unsigned short rp, wp;	/* read/write pointers */
	int cmds;		/* number of pending requests */
	u32 res;		/* last read value */
};

struct snd_azx {
	snd_card_t *card;
	struct pci_dev *pci;

	/* chip type specific */
	int driver_type;
	int playback_streams;
	int playback_index_offset;
	int capture_streams;
	int capture_index_offset;
	int num_streams;

	/* pci resources */
	unsigned long addr;
	void __iomem *remap_addr;
	int irq;

	/* locks */
	spinlock_t reg_lock;
	struct semaphore open_mutex;

	/* streams (x num_streams) */
	azx_dev_t *azx_dev;

	/* PCM */
	unsigned int pcm_devs;
	snd_pcm_t *pcm[AZX_MAX_PCMS];

	/* HD codec */
	unsigned short codec_mask;
	struct hda_bus *bus;

	/* CORB/RIRB */
	azx_rb_t corb;
	azx_rb_t rirb;

	/* BDL, CORB/RIRB and position buffers */
	struct snd_dma_buffer bdl;
	struct snd_dma_buffer rb;
	struct snd_dma_buffer posbuf;

	/* flags */
	int position_fix;
	unsigned int initialized: 1;
};

/* driver types */
enum {
	AZX_DRIVER_ICH,
	AZX_DRIVER_ATI,
	AZX_DRIVER_VIA,
	AZX_DRIVER_SIS,
	AZX_DRIVER_ULI,
};

static char *driver_short_names[] __devinitdata = {
	[AZX_DRIVER_ICH] = "HDA Intel",
	[AZX_DRIVER_ATI] = "HDA ATI SB",
	[AZX_DRIVER_VIA] = "HDA VIA VT82xx",
	[AZX_DRIVER_SIS] = "HDA SIS966",
	[AZX_DRIVER_ULI] = "HDA ULI M5461"
};

/*
 * macros for easy use
 */
#define azx_writel(chip,reg,value) \
	writel(value, (chip)->remap_addr + ICH6_REG_##reg)
#define azx_readl(chip,reg) \
	readl((chip)->remap_addr + ICH6_REG_##reg)
#define azx_writew(chip,reg,value) \
	writew(value, (chip)->remap_addr + ICH6_REG_##reg)
#define azx_readw(chip,reg) \
	readw((chip)->remap_addr + ICH6_REG_##reg)
#define azx_writeb(chip,reg,value) \
	writeb(value, (chip)->remap_addr + ICH6_REG_##reg)
#define azx_readb(chip,reg) \
	readb((chip)->remap_addr + ICH6_REG_##reg)

#define azx_sd_writel(dev,reg,value) \
	writel(value, (dev)->sd_addr + ICH6_REG_##reg)
#define azx_sd_readl(dev,reg) \
	readl((dev)->sd_addr + ICH6_REG_##reg)
#define azx_sd_writew(dev,reg,value) \
	writew(value, (dev)->sd_addr + ICH6_REG_##reg)
#define azx_sd_readw(dev,reg) \
	readw((dev)->sd_addr + ICH6_REG_##reg)
#define azx_sd_writeb(dev,reg,value) \
	writeb(value, (dev)->sd_addr + ICH6_REG_##reg)
#define azx_sd_readb(dev,reg) \
	readb((dev)->sd_addr + ICH6_REG_##reg)

/* for pcm support */
#define get_azx_dev(substream) (azx_dev_t*)(substream->runtime->private_data)

/* Get the upper 32bit of the given dma_addr_t
 * Compiler should optimize and eliminate the code if dma_addr_t is 32bit
 */
#define upper_32bit(addr) (sizeof(addr) > 4 ? (u32)((addr) >> 32) : (u32)0)


/*
 * Interface for HD codec
 */

#ifdef USE_CORB_RIRB
/*
 * CORB / RIRB interface
 */
static int azx_alloc_cmd_io(azx_t *chip)
{
	int err;

	/* single page (at least 4096 bytes) must suffice for both ringbuffes */
	err = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, snd_dma_pci_data(chip->pci),
				  PAGE_SIZE, &chip->rb);
	if (err < 0) {
		snd_printk(KERN_ERR SFX "cannot allocate CORB/RIRB\n");
		return err;
	}
	return 0;
}

static void azx_init_cmd_io(azx_t *chip)
{
	/* CORB set up */
	chip->corb.addr = chip->rb.addr;
	chip->corb.buf = (u32 *)chip->rb.area;
	azx_writel(chip, CORBLBASE, (u32)chip->corb.addr);
	azx_writel(chip, CORBUBASE, upper_32bit(chip->corb.addr));

	/* set the corb size to 256 entries (ULI requires explicitly) */
	azx_writeb(chip, CORBSIZE, 0x02);
	/* set the corb write pointer to 0 */
	azx_writew(chip, CORBWP, 0);
	/* reset the corb hw read pointer */
	azx_writew(chip, CORBRP, ICH6_RBRWP_CLR);
	/* enable corb dma */
	azx_writeb(chip, CORBCTL, ICH6_RBCTL_DMA_EN);

	/* RIRB set up */
	chip->rirb.addr = chip->rb.addr + 2048;
	chip->rirb.buf = (u32 *)(chip->rb.area + 2048);
	azx_writel(chip, RIRBLBASE, (u32)chip->rirb.addr);
	azx_writel(chip, RIRBUBASE, upper_32bit(chip->rirb.addr));

	/* set the rirb size to 256 entries (ULI requires explicitly) */
	azx_writeb(chip, RIRBSIZE, 0x02);
	/* reset the rirb hw write pointer */
	azx_writew(chip, RIRBWP, ICH6_RBRWP_CLR);
	/* set N=1, get RIRB response interrupt for new entry */
	azx_writew(chip, RINTCNT, 1);
	/* enable rirb dma and response irq */
#ifdef USE_CORB_RIRB
	azx_writeb(chip, RIRBCTL, ICH6_RBCTL_DMA_EN | ICH6_RBCTL_IRQ_EN);
#else
	azx_writeb(chip, RIRBCTL, ICH6_RBCTL_DMA_EN);
#endif
	chip->rirb.rp = chip->rirb.cmds = 0;
}

static void azx_free_cmd_io(azx_t *chip)
{
	/* disable ringbuffer DMAs */
	azx_writeb(chip, RIRBCTL, 0);
	azx_writeb(chip, CORBCTL, 0);
}

/* send a command */
static int azx_send_cmd(struct hda_codec *codec, hda_nid_t nid, int direct,
			unsigned int verb, unsigned int para)
{
	azx_t *chip = codec->bus->private_data;
	unsigned int wp;
	u32 val;

	val = (u32)(codec->addr & 0x0f) << 28;
	val |= (u32)direct << 27;
	val |= (u32)nid << 20;
	val |= verb << 8;
	val |= para;

	/* add command to corb */
	wp = azx_readb(chip, CORBWP);
	wp++;
	wp %= ICH6_MAX_CORB_ENTRIES;

	spin_lock_irq(&chip->reg_lock);
	chip->rirb.cmds++;
	chip->corb.buf[wp] = cpu_to_le32(val);
	azx_writel(chip, CORBWP, wp);
	spin_unlock_irq(&chip->reg_lock);

	return 0;
}

#define ICH6_RIRB_EX_UNSOL_EV	(1<<4)

/* retrieve RIRB entry - called from interrupt handler */
static void azx_update_rirb(azx_t *chip)
{
	unsigned int rp, wp;
	u32 res, res_ex;

	wp = azx_readb(chip, RIRBWP);
	if (wp == chip->rirb.wp)
		return;
	chip->rirb.wp = wp;
		
	while (chip->rirb.rp != wp) {
		chip->rirb.rp++;
		chip->rirb.rp %= ICH6_MAX_RIRB_ENTRIES;

		rp = chip->rirb.rp << 1; /* an RIRB entry is 8-bytes */
		res_ex = le32_to_cpu(chip->rirb.buf[rp + 1]);
		res = le32_to_cpu(chip->rirb.buf[rp]);
		if (res_ex & ICH6_RIRB_EX_UNSOL_EV)
			snd_hda_queue_unsol_event(chip->bus, res, res_ex);
		else if (chip->rirb.cmds) {
			chip->rirb.cmds--;
			chip->rirb.res = res;
		}
	}
}

/* receive a response */
static unsigned int azx_get_response(struct hda_codec *codec)
{
	azx_t *chip = codec->bus->private_data;
	int timeout = 50;

	while (chip->rirb.cmds) {
		if (! --timeout) {
			snd_printk(KERN_ERR "azx_get_response timeout\n");
			chip->rirb.rp = azx_readb(chip, RIRBWP);
			chip->rirb.cmds = 0;
			return -1;
		}
		msleep(1);
	}
	return chip->rirb.res; /* the last value */
}

#else
/*
 * Use the single immediate command instead of CORB/RIRB for simplicity
 *
 * Note: according to Intel, this is not preferred use.  The command was
 *       intended for the BIOS only, and may get confused with unsolicited
 *       responses.  So, we shouldn't use it for normal operation from the
 *       driver.
 *       I left the codes, however, for debugging/testing purposes.
 */

#define azx_alloc_cmd_io(chip)	0
#define azx_init_cmd_io(chip)
#define azx_free_cmd_io(chip)

/* send a command */
static int azx_send_cmd(struct hda_codec *codec, hda_nid_t nid, int direct,
			unsigned int verb, unsigned int para)
{
	azx_t *chip = codec->bus->private_data;
	u32 val;
	int timeout = 50;

	val = (u32)(codec->addr & 0x0f) << 28;
	val |= (u32)direct << 27;
	val |= (u32)nid << 20;
	val |= verb << 8;
	val |= para;

	while (timeout--) {
		/* check ICB busy bit */
		if (! (azx_readw(chip, IRS) & ICH6_IRS_BUSY)) {
			/* Clear IRV valid bit */
			azx_writew(chip, IRS, azx_readw(chip, IRS) | ICH6_IRS_VALID);
			azx_writel(chip, IC, val);
			azx_writew(chip, IRS, azx_readw(chip, IRS) | ICH6_IRS_BUSY);
			return 0;
		}
		udelay(1);
	}
	snd_printd(SFX "send_cmd timeout: IRS=0x%x, val=0x%x\n", azx_readw(chip, IRS), val);
	return -EIO;
}

/* receive a response */
static unsigned int azx_get_response(struct hda_codec *codec)
{
	azx_t *chip = codec->bus->private_data;
	int timeout = 50;

	while (timeout--) {
		/* check IRV busy bit */
		if (azx_readw(chip, IRS) & ICH6_IRS_VALID)
			return azx_readl(chip, IR);
		udelay(1);
	}
	snd_printd(SFX "get_response timeout: IRS=0x%x\n", azx_readw(chip, IRS));
	return (unsigned int)-1;
}

#define azx_update_rirb(chip)

#endif /* USE_CORB_RIRB */

/* reset codec link */
static int azx_reset(azx_t *chip)
{
	int count;

	/* reset controller */
	azx_writel(chip, GCTL, azx_readl(chip, GCTL) & ~ICH6_GCTL_RESET);

	count = 50;
	while (azx_readb(chip, GCTL) && --count)
		msleep(1);

	/* delay for >= 100us for codec PLL to settle per spec
	 * Rev 0.9 section 5.5.1
	 */
	msleep(1);

	/* Bring controller out of reset */
	azx_writeb(chip, GCTL, azx_readb(chip, GCTL) | ICH6_GCTL_RESET);

	count = 50;
	while (! azx_readb(chip, GCTL) && --count)
		msleep(1);

	/* Brent Chartrand said to wait >= 540us for codecs to intialize */
	msleep(1);

	/* check to see if controller is ready */
	if (! azx_readb(chip, GCTL)) {
		snd_printd("azx_reset: controller not ready!\n");
		return -EBUSY;
	}

	/* Accept unsolicited responses */
	azx_writel(chip, GCTL, azx_readl(chip, GCTL) | ICH6_GCTL_UREN);

	/* detect codecs */
	if (! chip->codec_mask) {
		chip->codec_mask = azx_readw(chip, STATESTS);
		snd_printdd("codec_mask = 0x%x\n", chip->codec_mask);
	}

	return 0;
}


/*
 * Lowlevel interface
 */  

/* enable interrupts */
static void azx_int_enable(azx_t *chip)
{
	/* enable controller CIE and GIE */
	azx_writel(chip, INTCTL, azx_readl(chip, INTCTL) |
		   ICH6_INT_CTRL_EN | ICH6_INT_GLOBAL_EN);
}

/* disable interrupts */
static void azx_int_disable(azx_t *chip)
{
	int i;

	/* disable interrupts in stream descriptor */
	for (i = 0; i < chip->num_streams; i++) {
		azx_dev_t *azx_dev = &chip->azx_dev[i];
		azx_sd_writeb(azx_dev, SD_CTL,
			      azx_sd_readb(azx_dev, SD_CTL) & ~SD_INT_MASK);
	}

	/* disable SIE for all streams */
	azx_writeb(chip, INTCTL, 0);

	/* disable controller CIE and GIE */
	azx_writel(chip, INTCTL, azx_readl(chip, INTCTL) &
		   ~(ICH6_INT_CTRL_EN | ICH6_INT_GLOBAL_EN));
}

/* clear interrupts */
static void azx_int_clear(azx_t *chip)
{
	int i;

	/* clear stream status */
	for (i = 0; i < chip->num_streams; i++) {
		azx_dev_t *azx_dev = &chip->azx_dev[i];
		azx_sd_writeb(azx_dev, SD_STS, SD_INT_MASK);
	}

	/* clear STATESTS */
	azx_writeb(chip, STATESTS, STATESTS_INT_MASK);

	/* clear rirb status */
	azx_writeb(chip, RIRBSTS, RIRB_INT_MASK);

	/* clear int status */
	azx_writel(chip, INTSTS, ICH6_INT_CTRL_EN | ICH6_INT_ALL_STREAM);
}

/* start a stream */
static void azx_stream_start(azx_t *chip, azx_dev_t *azx_dev)
{
	/* enable SIE */
	azx_writeb(chip, INTCTL,
		   azx_readb(chip, INTCTL) | (1 << azx_dev->index));
	/* set DMA start and interrupt mask */
	azx_sd_writeb(azx_dev, SD_CTL, azx_sd_readb(azx_dev, SD_CTL) |
		      SD_CTL_DMA_START | SD_INT_MASK);
}

/* stop a stream */
static void azx_stream_stop(azx_t *chip, azx_dev_t *azx_dev)
{
	/* stop DMA */
	azx_sd_writeb(azx_dev, SD_CTL, azx_sd_readb(azx_dev, SD_CTL) &
		      ~(SD_CTL_DMA_START | SD_INT_MASK));
	azx_sd_writeb(azx_dev, SD_STS, SD_INT_MASK); /* to be sure */
	/* disable SIE */
	azx_writeb(chip, INTCTL,
		   azx_readb(chip, INTCTL) & ~(1 << azx_dev->index));
}


/*
 * initialize the chip
 */
static void azx_init_chip(azx_t *chip)
{
	unsigned char tcsel_reg, ati_misc_cntl2;

	/* Clear bits 0-2 of PCI register TCSEL (at offset 0x44)
	 * TCSEL == Traffic Class Select Register, which sets PCI express QOS
	 * Ensuring these bits are 0 clears playback static on some HD Audio codecs
	 */
	pci_read_config_byte (chip->pci, ICH6_PCIREG_TCSEL, &tcsel_reg);
	pci_write_config_byte(chip->pci, ICH6_PCIREG_TCSEL, tcsel_reg & 0xf8);

	/* reset controller */
	azx_reset(chip);

	/* initialize interrupts */
	azx_int_clear(chip);
	azx_int_enable(chip);

	/* initialize the codec command I/O */
	azx_init_cmd_io(chip);

	/* program the position buffer */
	azx_writel(chip, DPLBASE, (u32)chip->posbuf.addr);
	azx_writel(chip, DPUBASE, upper_32bit(chip->posbuf.addr));

	/* For ATI SB450 azalia HD audio, we need to enable snoop */
	if (chip->driver_type == AZX_DRIVER_ATI) {
		pci_read_config_byte(chip->pci, ATI_SB450_HDAUDIO_MISC_CNTR2_ADDR, 
				     &ati_misc_cntl2);
		pci_write_config_byte(chip->pci, ATI_SB450_HDAUDIO_MISC_CNTR2_ADDR, 
				      (ati_misc_cntl2 & 0xf8) | ATI_SB450_HDAUDIO_ENABLE_SNOOP);
	}
}


/*
 * interrupt handler
 */
static irqreturn_t azx_interrupt(int irq, void* dev_id, struct pt_regs *regs)
{
	azx_t *chip = dev_id;
	azx_dev_t *azx_dev;
	u32 status;
	int i;

	spin_lock(&chip->reg_lock);

	status = azx_readl(chip, INTSTS);
	if (status == 0) {
		spin_unlock(&chip->reg_lock);
		return IRQ_NONE;
	}
	
	for (i = 0; i < chip->num_streams; i++) {
		azx_dev = &chip->azx_dev[i];
		if (status & azx_dev->sd_int_sta_mask) {
			azx_sd_writeb(azx_dev, SD_STS, SD_INT_MASK);
			if (azx_dev->substream && azx_dev->running) {
				azx_dev->period_updating = 1;
				spin_unlock(&chip->reg_lock);
				snd_pcm_period_elapsed(azx_dev->substream);
				spin_lock(&chip->reg_lock);
				azx_dev->period_updating = 0;
			}
		}
	}

	/* clear rirb int */
	status = azx_readb(chip, RIRBSTS);
	if (status & RIRB_INT_MASK) {
		if (status & RIRB_INT_RESPONSE)
			azx_update_rirb(chip);
		azx_writeb(chip, RIRBSTS, RIRB_INT_MASK);
	}

#if 0
	/* clear state status int */
	if (azx_readb(chip, STATESTS) & 0x04)
		azx_writeb(chip, STATESTS, 0x04);
#endif
	spin_unlock(&chip->reg_lock);
	
	return IRQ_HANDLED;
}


/*
 * set up BDL entries
 */
static void azx_setup_periods(azx_dev_t *azx_dev)
{
	u32 *bdl = azx_dev->bdl;
	dma_addr_t dma_addr = azx_dev->substream->runtime->dma_addr;
	int idx;

	/* reset BDL address */
	azx_sd_writel(azx_dev, SD_BDLPL, 0);
	azx_sd_writel(azx_dev, SD_BDLPU, 0);

	/* program the initial BDL entries */
	for (idx = 0; idx < azx_dev->frags; idx++) {
		unsigned int off = idx << 2; /* 4 dword step */
		dma_addr_t addr = dma_addr + idx * azx_dev->fragsize;
		/* program the address field of the BDL entry */
		bdl[off] = cpu_to_le32((u32)addr);
		bdl[off+1] = cpu_to_le32(upper_32bit(addr));

		/* program the size field of the BDL entry */
		bdl[off+2] = cpu_to_le32(azx_dev->fragsize);

		/* program the IOC to enable interrupt when buffer completes */
		bdl[off+3] = cpu_to_le32(0x01);
	}
}

/*
 * set up the SD for streaming
 */
static int azx_setup_controller(azx_t *chip, azx_dev_t *azx_dev)
{
	unsigned char val;
	int timeout;

	/* make sure the run bit is zero for SD */
	azx_sd_writeb(azx_dev, SD_CTL, azx_sd_readb(azx_dev, SD_CTL) & ~SD_CTL_DMA_START);
	/* reset stream */
	azx_sd_writeb(azx_dev, SD_CTL, azx_sd_readb(azx_dev, SD_CTL) | SD_CTL_STREAM_RESET);
	udelay(3);
	timeout = 300;
	while (!((val = azx_sd_readb(azx_dev, SD_CTL)) & SD_CTL_STREAM_RESET) &&
	       --timeout)
		;
	val &= ~SD_CTL_STREAM_RESET;
	azx_sd_writeb(azx_dev, SD_CTL, val);
	udelay(3);

	timeout = 300;
	/* waiting for hardware to report that the stream is out of reset */
	while (((val = azx_sd_readb(azx_dev, SD_CTL)) & SD_CTL_STREAM_RESET) &&
	       --timeout)
		;

	/* program the stream_tag */
	azx_sd_writel(azx_dev, SD_CTL,
		      (azx_sd_readl(azx_dev, SD_CTL) & ~SD_CTL_STREAM_TAG_MASK) |
		      (azx_dev->stream_tag << SD_CTL_STREAM_TAG_SHIFT));

	/* program the length of samples in cyclic buffer */
	azx_sd_writel(azx_dev, SD_CBL, azx_dev->bufsize);

	/* program the stream format */
	/* this value needs to be the same as the one programmed */
	azx_sd_writew(azx_dev, SD_FORMAT, azx_dev->format_val);

	/* program the stream LVI (last valid index) of the BDL */
	azx_sd_writew(azx_dev, SD_LVI, azx_dev->frags - 1);

	/* program the BDL address */
	/* lower BDL address */
	azx_sd_writel(azx_dev, SD_BDLPL, (u32)azx_dev->bdl_addr);
	/* upper BDL address */
	azx_sd_writel(azx_dev, SD_BDLPU, upper_32bit(azx_dev->bdl_addr));

	/* enable the position buffer */
	if (! (azx_readl(chip, DPLBASE) & ICH6_DPLBASE_ENABLE))
		azx_writel(chip, DPLBASE, (u32)chip->posbuf.addr | ICH6_DPLBASE_ENABLE);

	/* set the interrupt enable bits in the descriptor control register */
	azx_sd_writel(azx_dev, SD_CTL, azx_sd_readl(azx_dev, SD_CTL) | SD_INT_MASK);

	return 0;
}


/*
 * Codec initialization
 */

static int __devinit azx_codec_create(azx_t *chip, const char *model)
{
	struct hda_bus_template bus_temp;
	int c, codecs, err;

	memset(&bus_temp, 0, sizeof(bus_temp));
	bus_temp.private_data = chip;
	bus_temp.modelname = model;
	bus_temp.pci = chip->pci;
	bus_temp.ops.command = azx_send_cmd;
	bus_temp.ops.get_response = azx_get_response;

	if ((err = snd_hda_bus_new(chip->card, &bus_temp, &chip->bus)) < 0)
		return err;

	codecs = 0;
	for (c = 0; c < AZX_MAX_CODECS; c++) {
		if (chip->codec_mask & (1 << c)) {
			err = snd_hda_codec_new(chip->bus, c, NULL);
			if (err < 0)
				continue;
			codecs++;
		}
	}
	if (! codecs) {
		snd_printk(KERN_ERR SFX "no codecs initialized\n");
		return -ENXIO;
	}

	return 0;
}


/*
 * PCM support
 */

/* assign a stream for the PCM */
static inline azx_dev_t *azx_assign_device(azx_t *chip, int stream)
{
	int dev, i, nums;
	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		dev = chip->playback_index_offset;
		nums = chip->playback_streams;
	} else {
		dev = chip->capture_index_offset;
		nums = chip->capture_streams;
	}
	for (i = 0; i < nums; i++, dev++)
		if (! chip->azx_dev[dev].opened) {
			chip->azx_dev[dev].opened = 1;
			return &chip->azx_dev[dev];
		}
	return NULL;
}

/* release the assigned stream */
static inline void azx_release_device(azx_dev_t *azx_dev)
{
	azx_dev->opened = 0;
}

static snd_pcm_hardware_t azx_pcm_hw = {
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_PAUSE /*|*/
				 /*SNDRV_PCM_INFO_RESUME*/),
	.formats =		SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_48000,
	.rate_min =		48000,
	.rate_max =		48000,
	.channels_min =		2,
	.channels_max =		2,
	.buffer_bytes_max =	AZX_MAX_BUF_SIZE,
	.period_bytes_min =	128,
	.period_bytes_max =	AZX_MAX_BUF_SIZE / 2,
	.periods_min =		2,
	.periods_max =		AZX_MAX_FRAG,
	.fifo_size =		0,
};

struct azx_pcm {
	azx_t *chip;
	struct hda_codec *codec;
	struct hda_pcm_stream *hinfo[2];
};

static int azx_pcm_open(snd_pcm_substream_t *substream)
{
	struct azx_pcm *apcm = snd_pcm_substream_chip(substream);
	struct hda_pcm_stream *hinfo = apcm->hinfo[substream->stream];
	azx_t *chip = apcm->chip;
	azx_dev_t *azx_dev;
	snd_pcm_runtime_t *runtime = substream->runtime;
	unsigned long flags;
	int err;

	down(&chip->open_mutex);
	azx_dev = azx_assign_device(chip, substream->stream);
	if (azx_dev == NULL) {
		up(&chip->open_mutex);
		return -EBUSY;
	}
	runtime->hw = azx_pcm_hw;
	runtime->hw.channels_min = hinfo->channels_min;
	runtime->hw.channels_max = hinfo->channels_max;
	runtime->hw.formats = hinfo->formats;
	runtime->hw.rates = hinfo->rates;
	snd_pcm_limit_hw_rates(runtime);
	snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	if ((err = hinfo->ops.open(hinfo, apcm->codec, substream)) < 0) {
		azx_release_device(azx_dev);
		up(&chip->open_mutex);
		return err;
	}
	spin_lock_irqsave(&chip->reg_lock, flags);
	azx_dev->substream = substream;
	azx_dev->running = 0;
	spin_unlock_irqrestore(&chip->reg_lock, flags);

	runtime->private_data = azx_dev;
	up(&chip->open_mutex);
	return 0;
}

static int azx_pcm_close(snd_pcm_substream_t *substream)
{
	struct azx_pcm *apcm = snd_pcm_substream_chip(substream);
	struct hda_pcm_stream *hinfo = apcm->hinfo[substream->stream];
	azx_t *chip = apcm->chip;
	azx_dev_t *azx_dev = get_azx_dev(substream);
	unsigned long flags;

	down(&chip->open_mutex);
	spin_lock_irqsave(&chip->reg_lock, flags);
	azx_dev->substream = NULL;
	azx_dev->running = 0;
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	azx_release_device(azx_dev);
	hinfo->ops.close(hinfo, apcm->codec, substream);
	up(&chip->open_mutex);
	return 0;
}

static int azx_pcm_hw_params(snd_pcm_substream_t *substream, snd_pcm_hw_params_t *hw_params)
{
	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
}

static int azx_pcm_hw_free(snd_pcm_substream_t *substream)
{
	struct azx_pcm *apcm = snd_pcm_substream_chip(substream);
	azx_dev_t *azx_dev = get_azx_dev(substream);
	struct hda_pcm_stream *hinfo = apcm->hinfo[substream->stream];

	/* reset BDL address */
	azx_sd_writel(azx_dev, SD_BDLPL, 0);
	azx_sd_writel(azx_dev, SD_BDLPU, 0);
	azx_sd_writel(azx_dev, SD_CTL, 0);

	hinfo->ops.cleanup(hinfo, apcm->codec, substream);

	return snd_pcm_lib_free_pages(substream);
}

static int azx_pcm_prepare(snd_pcm_substream_t *substream)
{
	struct azx_pcm *apcm = snd_pcm_substream_chip(substream);
	azx_t *chip = apcm->chip;
	azx_dev_t *azx_dev = get_azx_dev(substream);
	struct hda_pcm_stream *hinfo = apcm->hinfo[substream->stream];
	snd_pcm_runtime_t *runtime = substream->runtime;

	azx_dev->bufsize = snd_pcm_lib_buffer_bytes(substream);
	azx_dev->fragsize = snd_pcm_lib_period_bytes(substream);
	azx_dev->frags = azx_dev->bufsize / azx_dev->fragsize;
	azx_dev->format_val = snd_hda_calc_stream_format(runtime->rate,
							 runtime->channels,
							 runtime->format,
							 hinfo->maxbps);
	if (! azx_dev->format_val) {
		snd_printk(KERN_ERR SFX "invalid format_val, rate=%d, ch=%d, format=%d\n",
			   runtime->rate, runtime->channels, runtime->format);
		return -EINVAL;
	}

	snd_printdd("azx_pcm_prepare: bufsize=0x%x, fragsize=0x%x, format=0x%x\n",
		    azx_dev->bufsize, azx_dev->fragsize, azx_dev->format_val);
	azx_setup_periods(azx_dev);
	azx_setup_controller(chip, azx_dev);
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		azx_dev->fifo_size = azx_sd_readw(azx_dev, SD_FIFOSIZE) + 1;
	else
		azx_dev->fifo_size = 0;
	azx_dev->last_pos = 0;

	return hinfo->ops.prepare(hinfo, apcm->codec, azx_dev->stream_tag,
				  azx_dev->format_val, substream);
}

static int azx_pcm_trigger(snd_pcm_substream_t *substream, int cmd)
{
	struct azx_pcm *apcm = snd_pcm_substream_chip(substream);
	azx_dev_t *azx_dev = get_azx_dev(substream);
	azx_t *chip = apcm->chip;
	int err = 0;

	spin_lock(&chip->reg_lock);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_START:
		azx_stream_start(chip, azx_dev);
		azx_dev->running = 1;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
		azx_stream_stop(chip, azx_dev);
		azx_dev->running = 0;
		break;
	default:
		err = -EINVAL;
	}
	spin_unlock(&chip->reg_lock);
	if (cmd == SNDRV_PCM_TRIGGER_PAUSE_PUSH ||
	    cmd == SNDRV_PCM_TRIGGER_SUSPEND ||
	    cmd == SNDRV_PCM_TRIGGER_STOP) {
		int timeout = 5000;
		while (azx_sd_readb(azx_dev, SD_CTL) & SD_CTL_DMA_START && --timeout)
			;
	}
	return err;
}

static snd_pcm_uframes_t azx_pcm_pointer(snd_pcm_substream_t *substream)
{
	struct azx_pcm *apcm = snd_pcm_substream_chip(substream);
	azx_t *chip = apcm->chip;
	azx_dev_t *azx_dev = get_azx_dev(substream);
	unsigned int pos;

	if (chip->position_fix == POS_FIX_POSBUF) {
		/* use the position buffer */
		pos = *azx_dev->posbuf;
	} else {
		/* read LPIB */
		pos = azx_sd_readl(azx_dev, SD_LPIB);
		if (chip->position_fix == POS_FIX_FIFO)
			pos += azx_dev->fifo_size;
#if 0 /* disabled temprarily, auto-correction doesn't work well... */
		else if (chip->position_fix == POS_FIX_AUTO && azx_dev->period_updating) {
			/* check the validity of DMA position */
			unsigned int diff = 0;
			azx_dev->last_pos += azx_dev->fragsize;
			if (azx_dev->last_pos > pos)
				diff = azx_dev->last_pos - pos;
			if (azx_dev->last_pos >= azx_dev->bufsize) {
				if (pos < azx_dev->fragsize)
					diff = 0;
				azx_dev->last_pos = 0;
			}
			if (diff > 0 && diff <= azx_dev->fifo_size)
				pos += azx_dev->fifo_size;
			else {
				snd_printdd(KERN_INFO "hda_intel: DMA position fix %d, switching to posbuf\n", diff);
				chip->position_fix = POS_FIX_POSBUF;
				pos = *azx_dev->posbuf;
			}
			azx_dev->period_updating = 0;
		}
#else
		else if (chip->position_fix == POS_FIX_AUTO)
			pos += azx_dev->fifo_size;
#endif
	}
	if (pos >= azx_dev->bufsize)
		pos = 0;
	return bytes_to_frames(substream->runtime, pos);
}

static snd_pcm_ops_t azx_pcm_ops = {
	.open = azx_pcm_open,
	.close = azx_pcm_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = azx_pcm_hw_params,
	.hw_free = azx_pcm_hw_free,
	.prepare = azx_pcm_prepare,
	.trigger = azx_pcm_trigger,
	.pointer = azx_pcm_pointer,
};

static void azx_pcm_free(snd_pcm_t *pcm)
{
	kfree(pcm->private_data);
}

static int __devinit create_codec_pcm(azx_t *chip, struct hda_codec *codec,
				      struct hda_pcm *cpcm, int pcm_dev)
{
	int err;
	snd_pcm_t *pcm;
	struct azx_pcm *apcm;

	snd_assert(cpcm->stream[0].substreams || cpcm->stream[1].substreams, return -EINVAL);
	snd_assert(cpcm->name, return -EINVAL);

	err = snd_pcm_new(chip->card, cpcm->name, pcm_dev,
			  cpcm->stream[0].substreams, cpcm->stream[1].substreams,
			  &pcm);
	if (err < 0)
		return err;
	strcpy(pcm->name, cpcm->name);
	apcm = kmalloc(sizeof(*apcm), GFP_KERNEL);
	if (apcm == NULL)
		return -ENOMEM;
	apcm->chip = chip;
	apcm->codec = codec;
	apcm->hinfo[0] = &cpcm->stream[0];
	apcm->hinfo[1] = &cpcm->stream[1];
	pcm->private_data = apcm;
	pcm->private_free = azx_pcm_free;
	if (cpcm->stream[0].substreams)
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &azx_pcm_ops);
	if (cpcm->stream[1].substreams)
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &azx_pcm_ops);
	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
					      snd_dma_pci_data(chip->pci),
					      1024 * 64, 1024 * 128);
	chip->pcm[pcm_dev] = pcm;
	chip->pcm_devs = pcm_dev + 1;

	return 0;
}

static int __devinit azx_pcm_create(azx_t *chip)
{
	struct list_head *p;
	struct hda_codec *codec;
	int c, err;
	int pcm_dev;

	if ((err = snd_hda_build_pcms(chip->bus)) < 0)
		return err;

	/* create audio PCMs */
	pcm_dev = 0;
	list_for_each(p, &chip->bus->codec_list) {
		codec = list_entry(p, struct hda_codec, list);
		for (c = 0; c < codec->num_pcms; c++) {
			if (codec->pcm_info[c].is_modem)
				continue; /* create later */
			if (pcm_dev >= AZX_MAX_AUDIO_PCMS) {
				snd_printk(KERN_ERR SFX "Too many audio PCMs\n");
				return -EINVAL;
			}
			err = create_codec_pcm(chip, codec, &codec->pcm_info[c], pcm_dev);
			if (err < 0)
				return err;
			pcm_dev++;
		}
	}

	/* create modem PCMs */
	pcm_dev = AZX_MAX_AUDIO_PCMS;
	list_for_each(p, &chip->bus->codec_list) {
		codec = list_entry(p, struct hda_codec, list);
		for (c = 0; c < codec->num_pcms; c++) {
			if (! codec->pcm_info[c].is_modem)
				continue; /* already created */
			if (pcm_dev >= AZX_MAX_PCMS) {
				snd_printk(KERN_ERR SFX "Too many modem PCMs\n");
				return -EINVAL;
			}
			err = create_codec_pcm(chip, codec, &codec->pcm_info[c], pcm_dev);
			if (err < 0)
				return err;
			pcm_dev++;
		}
	}
	return 0;
}

/*
 * mixer creation - all stuff is implemented in hda module
 */
static int __devinit azx_mixer_create(azx_t *chip)
{
	return snd_hda_build_controls(chip->bus);
}


/*
 * initialize SD streams
 */
static int __devinit azx_init_stream(azx_t *chip)
{
	int i;

	/* initialize each stream (aka device)
	 * assign the starting bdl address to each stream (device) and initialize
	 */
	for (i = 0; i < chip->num_streams; i++) {
		unsigned int off = sizeof(u32) * (i * AZX_MAX_FRAG * 4);
		azx_dev_t *azx_dev = &chip->azx_dev[i];
		azx_dev->bdl = (u32 *)(chip->bdl.area + off);
		azx_dev->bdl_addr = chip->bdl.addr + off;
		azx_dev->posbuf = (volatile u32 *)(chip->posbuf.area + i * 8);
		/* offset: SDI0=0x80, SDI1=0xa0, ... SDO3=0x160 */
		azx_dev->sd_addr = chip->remap_addr + (0x20 * i + 0x80);
		/* int mask: SDI0=0x01, SDI1=0x02, ... SDO3=0x80 */
		azx_dev->sd_int_sta_mask = 1 << i;
		/* stream tag: must be non-zero and unique */
		azx_dev->index = i;
		azx_dev->stream_tag = i + 1;
	}

	return 0;
}


#ifdef CONFIG_PM
/*
 * power management
 */
static int azx_suspend(snd_card_t *card, pm_message_t state)
{
	azx_t *chip = card->pm_private_data;
	int i;

	for (i = 0; i < chip->pcm_devs; i++)
		if (chip->pcm[i])
			snd_pcm_suspend_all(chip->pcm[i]);
	snd_hda_suspend(chip->bus, state);
	azx_free_cmd_io(chip);
	pci_disable_device(chip->pci);
	return 0;
}

static int azx_resume(snd_card_t *card)
{
	azx_t *chip = card->pm_private_data;

	pci_enable_device(chip->pci);
	pci_set_master(chip->pci);
	azx_init_chip(chip);
	snd_hda_resume(chip->bus);
	return 0;
}
#endif /* CONFIG_PM */


/*
 * destructor
 */
static int azx_free(azx_t *chip)
{
	if (chip->initialized) {
		int i;

		for (i = 0; i < chip->num_streams; i++)
			azx_stream_stop(chip, &chip->azx_dev[i]);

		/* disable interrupts */
		azx_int_disable(chip);
		azx_int_clear(chip);

		/* disable CORB/RIRB */
		azx_free_cmd_io(chip);

		/* disable position buffer */
		azx_writel(chip, DPLBASE, 0);
		azx_writel(chip, DPUBASE, 0);

		/* wait a little for interrupts to finish */
		msleep(1);
	}

	if (chip->remap_addr)
		iounmap(chip->remap_addr);
	if (chip->irq >= 0)
		free_irq(chip->irq, (void*)chip);

	if (chip->bdl.area)
		snd_dma_free_pages(&chip->bdl);
	if (chip->rb.area)
		snd_dma_free_pages(&chip->rb);
	if (chip->posbuf.area)
		snd_dma_free_pages(&chip->posbuf);
	pci_release_regions(chip->pci);
	pci_disable_device(chip->pci);
	kfree(chip->azx_dev);
	kfree(chip);

	return 0;
}

static int azx_dev_free(snd_device_t *device)
{
	return azx_free(device->device_data);
}

/*
 * constructor
 */
static int __devinit azx_create(snd_card_t *card, struct pci_dev *pci,
				int posfix, int driver_type,
				azx_t **rchip)
{
	azx_t *chip;
	int err = 0;
	static snd_device_ops_t ops = {
		.dev_free = azx_dev_free,
	};

	*rchip = NULL;
	
	if ((err = pci_enable_device(pci)) < 0)
		return err;

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	
	if (NULL == chip) {
		snd_printk(KERN_ERR SFX "cannot allocate chip\n");
		pci_disable_device(pci);
		return -ENOMEM;
	}

	spin_lock_init(&chip->reg_lock);
	init_MUTEX(&chip->open_mutex);
	chip->card = card;
	chip->pci = pci;
	chip->irq = -1;
	chip->driver_type = driver_type;

	chip->position_fix = posfix;

#if BITS_PER_LONG != 64
	/* Fix up base address on ULI M5461 */
	if (chip->driver_type == AZX_DRIVER_ULI) {
		u16 tmp3;
		pci_read_config_word(pci, 0x40, &tmp3);
		pci_write_config_word(pci, 0x40, tmp3 | 0x10);
		pci_write_config_dword(pci, PCI_BASE_ADDRESS_1, 0);
	}
#endif

	if ((err = pci_request_regions(pci, "ICH HD audio")) < 0) {
		kfree(chip);
		pci_disable_device(pci);
		return err;
	}

	chip->addr = pci_resource_start(pci,0);
	chip->remap_addr = ioremap_nocache(chip->addr, pci_resource_len(pci,0));
	if (chip->remap_addr == NULL) {
		snd_printk(KERN_ERR SFX "ioremap error\n");
		err = -ENXIO;
		goto errout;
	}

	if (request_irq(pci->irq, azx_interrupt, SA_INTERRUPT|SA_SHIRQ,
			"HDA Intel", (void*)chip)) {
		snd_printk(KERN_ERR SFX "unable to grab IRQ %d\n", pci->irq);
		err = -EBUSY;
		goto errout;
	}
	chip->irq = pci->irq;

	pci_set_master(pci);
	synchronize_irq(chip->irq);

	switch (chip->driver_type) {
	case AZX_DRIVER_ULI:
		chip->playback_streams = ULI_NUM_PLAYBACK;
		chip->capture_streams = ULI_NUM_CAPTURE;
		chip->playback_index_offset = ULI_PLAYBACK_INDEX;
		chip->capture_index_offset = ULI_CAPTURE_INDEX;
		break;
	default:
		chip->playback_streams = ICH6_NUM_PLAYBACK;
		chip->capture_streams = ICH6_NUM_CAPTURE;
		chip->playback_index_offset = ICH6_PLAYBACK_INDEX;
		chip->capture_index_offset = ICH6_CAPTURE_INDEX;
		break;
	}
	chip->num_streams = chip->playback_streams + chip->capture_streams;
	chip->azx_dev = kcalloc(chip->num_streams, sizeof(*chip->azx_dev), GFP_KERNEL);
	if (! chip->azx_dev) {
		snd_printk(KERN_ERR "cannot malloc azx_dev\n");
		goto errout;
	}

	/* allocate memory for the BDL for each stream */
	if ((err = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, snd_dma_pci_data(chip->pci),
				       BDL_SIZE, &chip->bdl)) < 0) {
		snd_printk(KERN_ERR SFX "cannot allocate BDL\n");
		goto errout;
	}
	/* allocate memory for the position buffer */
	if ((err = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, snd_dma_pci_data(chip->pci),
				       chip->num_streams * 8, &chip->posbuf)) < 0) {
		snd_printk(KERN_ERR SFX "cannot allocate posbuf\n");
		goto errout;
	}
	/* allocate CORB/RIRB */
	if ((err = azx_alloc_cmd_io(chip)) < 0)
		goto errout;

	/* initialize streams */
	azx_init_stream(chip);

	/* initialize chip */
	azx_init_chip(chip);

	chip->initialized = 1;

	/* codec detection */
	if (! chip->codec_mask) {
		snd_printk(KERN_ERR SFX "no codecs found!\n");
		err = -ENODEV;
		goto errout;
	}

	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops)) <0) {
		snd_printk(KERN_ERR SFX "Error creating device [card]!\n");
		goto errout;
	}

	strcpy(card->driver, "HDA-Intel");
	strcpy(card->shortname, driver_short_names[chip->driver_type]);
	sprintf(card->longname, "%s at 0x%lx irq %i", card->shortname, chip->addr, chip->irq);

	*rchip = chip;
	return 0;

 errout:
	azx_free(chip);
	return err;
}

static int __devinit azx_probe(struct pci_dev *pci, const struct pci_device_id *pci_id)
{
	static int dev;
	snd_card_t *card;
	azx_t *chip;
	int err = 0;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (! enable[dev]) {
		dev++;
		return -ENOENT;
	}

	card = snd_card_new(index[dev], id[dev], THIS_MODULE, 0);
	if (NULL == card) {
		snd_printk(KERN_ERR SFX "Error creating card!\n");
		return -ENOMEM;
	}

	if ((err = azx_create(card, pci, position_fix[dev], pci_id->driver_data,
			      &chip)) < 0) {
		snd_card_free(card);
		return err;
	}

	/* create codec instances */
	if ((err = azx_codec_create(chip, model[dev])) < 0) {
		snd_card_free(card);
		return err;
	}

	/* create PCM streams */
	if ((err = azx_pcm_create(chip)) < 0) {
		snd_card_free(card);
		return err;
	}

	/* create mixer controls */
	if ((err = azx_mixer_create(chip)) < 0) {
		snd_card_free(card);
		return err;
	}

	snd_card_set_pm_callback(card, azx_suspend, azx_resume, chip);
	snd_card_set_dev(card, &pci->dev);

	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}

	pci_set_drvdata(pci, card);
	dev++;

	return err;
}

static void __devexit azx_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
	pci_set_drvdata(pci, NULL);
}

/* PCI IDs */
static struct pci_device_id azx_ids[] = {
	{ 0x8086, 0x2668, PCI_ANY_ID, PCI_ANY_ID, 0, 0, AZX_DRIVER_ICH }, /* ICH6 */
	{ 0x8086, 0x27d8, PCI_ANY_ID, PCI_ANY_ID, 0, 0, AZX_DRIVER_ICH }, /* ICH7 */
	{ 0x8086, 0x269a, PCI_ANY_ID, PCI_ANY_ID, 0, 0, AZX_DRIVER_ICH }, /* ESB2 */
	{ 0x1002, 0x437b, PCI_ANY_ID, PCI_ANY_ID, 0, 0, AZX_DRIVER_ATI }, /* ATI SB450 */
	{ 0x1106, 0x3288, PCI_ANY_ID, PCI_ANY_ID, 0, 0, AZX_DRIVER_VIA }, /* VIA VT8251/VT8237A */
	{ 0x1039, 0x7502, PCI_ANY_ID, PCI_ANY_ID, 0, 0, AZX_DRIVER_SIS }, /* SIS966 */
	{ 0x10b9, 0x5461, PCI_ANY_ID, PCI_ANY_ID, 0, 0, AZX_DRIVER_ULI }, /* ULI M5461 */
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, azx_ids);

/* pci_driver definition */
static struct pci_driver driver = {
	.name = "HDA Intel",
	.owner = THIS_MODULE,
	.id_table = azx_ids,
	.probe = azx_probe,
	.remove = __devexit_p(azx_remove),
	SND_PCI_PM_CALLBACKS
};

static int __init alsa_card_azx_init(void)
{
	return pci_register_driver(&driver);
}

static void __exit alsa_card_azx_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(alsa_card_azx_init)
module_exit(alsa_card_azx_exit)
