/*
    card-opti92x-ad1848.c - driver for OPTi 82c92x based soundcards.
    Copyright (C) 1998-2000 by Massimo Piccioni <dafastidio@libero.it>

    Part of this code was developed at the Italian Ministry of Air Defence,
    Sixth Division (oh, che pace ...), Rome.

    Thanks to Maria Grazia Pollarini, Salvatore Vassallo.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
*/


#include <sound/driver.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/pnp.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#ifdef CS4231
#include <sound/cs4231.h>
#else
#ifndef OPTi93X
#include <sound/ad1848.h>
#else
#include <sound/control.h>
#include <sound/pcm.h>
#endif	/* OPTi93X */
#endif	/* CS4231 */
#include <sound/mpu401.h>
#include <sound/opl3.h>
#ifndef OPTi93X
#include <sound/opl4.h>
#endif
#define SNDRV_LEGACY_FIND_FREE_IRQ
#define SNDRV_LEGACY_FIND_FREE_DMA
#include <sound/initval.h>

MODULE_AUTHOR("Massimo Piccioni <dafastidio@libero.it>");
MODULE_LICENSE("GPL");
#ifdef OPTi93X
MODULE_DESCRIPTION("OPTi93X");
MODULE_SUPPORTED_DEVICE("{{OPTi,82C931/3}}");
#else	/* OPTi93X */
#ifdef CS4231
MODULE_DESCRIPTION("OPTi92X - CS4231");
MODULE_SUPPORTED_DEVICE("{{OPTi,82C924 (CS4231)},"
		"{OPTi,82C925 (CS4231)}}");
#else	/* CS4231 */
MODULE_DESCRIPTION("OPTi92X - AD1848");
MODULE_SUPPORTED_DEVICE("{{OPTi,82C924 (AD1848)},"
		"{OPTi,82C925 (AD1848)},"
	        "{OAK,Mozart}}");
#endif	/* CS4231 */
#endif	/* OPTi93X */

static int index = SNDRV_DEFAULT_IDX1;	/* Index 0-MAX */
static char *id = SNDRV_DEFAULT_STR1;		/* ID for this card */
//static int enable = SNDRV_DEFAULT_ENABLE1;	/* Enable this card */
static int isapnp = 1;			/* Enable ISA PnP detection */
static long port = SNDRV_DEFAULT_PORT1; 	/* 0x530,0xe80,0xf40,0x604 */
static long mpu_port = SNDRV_DEFAULT_PORT1;	/* 0x300,0x310,0x320,0x330 */
static long fm_port = SNDRV_DEFAULT_PORT1;	/* 0x388 */
static int irq = SNDRV_DEFAULT_IRQ1;		/* 5,7,9,10,11 */
static int mpu_irq = SNDRV_DEFAULT_IRQ1;	/* 5,7,9,10 */
static int dma1 = SNDRV_DEFAULT_DMA1;		/* 0,1,3 */
#if defined(CS4231) || defined(OPTi93X)
static int dma2 = SNDRV_DEFAULT_DMA1;		/* 0,1,3 */
#endif	/* CS4231 || OPTi93X */

module_param(index, int, 0444);
MODULE_PARM_DESC(index, "Index value for opti9xx based soundcard.");
module_param(id, charp, 0444);
MODULE_PARM_DESC(id, "ID string for opti9xx based soundcard.");
//module_param(enable, bool, 0444);
//MODULE_PARM_DESC(enable, "Enable opti9xx soundcard.");
module_param(isapnp, bool, 0444);
MODULE_PARM_DESC(isapnp, "Enable ISA PnP detection for specified soundcard.");
module_param(port, long, 0444);
MODULE_PARM_DESC(port, "WSS port # for opti9xx driver.");
module_param(mpu_port, long, 0444);
MODULE_PARM_DESC(mpu_port, "MPU-401 port # for opti9xx driver.");
module_param(fm_port, long, 0444);
MODULE_PARM_DESC(fm_port, "FM port # for opti9xx driver.");
module_param(irq, int, 0444);
MODULE_PARM_DESC(irq, "WSS irq # for opti9xx driver.");
module_param(mpu_irq, int, 0444);
MODULE_PARM_DESC(mpu_irq, "MPU-401 irq # for opti9xx driver.");
module_param(dma1, int, 0444);
MODULE_PARM_DESC(dma1, "1st dma # for opti9xx driver.");
#if defined(CS4231) || defined(OPTi93X)
module_param(dma2, int, 0444);
MODULE_PARM_DESC(dma2, "2nd dma # for opti9xx driver.");
#endif	/* CS4231 || OPTi93X */

#define OPTi9XX_HW_DETECT	0
#define OPTi9XX_HW_82C928	1
#define OPTi9XX_HW_82C929	2
#define OPTi9XX_HW_82C924	3
#define OPTi9XX_HW_82C925	4
#define OPTi9XX_HW_82C930	5
#define OPTi9XX_HW_82C931	6
#define OPTi9XX_HW_82C933	7
#define OPTi9XX_HW_LAST		OPTi9XX_HW_82C933

#define OPTi9XX_MC_REG(n)	n

typedef struct _snd_opti9xx opti9xx_t;

#ifdef OPTi93X

#define OPTi93X_INDEX			0x00
#define OPTi93X_DATA			0x01
#define OPTi93X_STATUS			0x02
#define OPTi93X_DDATA			0x03
#define OPTi93X_PORT(chip, r)		((chip)->port + OPTi93X_##r)

#define OPTi93X_MIXOUT_LEFT		0x00
#define OPTi93X_MIXOUT_RIGHT		0x01
#define OPTi93X_CD_LEFT_INPUT		0x02
#define OPTi93X_CD_RIGHT_INPUT		0x03
#define OPTi930_AUX_LEFT_INPUT		0x04
#define OPTi930_AUX_RIGHT_INPUT		0x05
#define OPTi931_FM_LEFT_INPUT		0x04
#define OPTi931_FM_RIGHT_INPUT		0x05
#define OPTi93X_DAC_LEFT		0x06
#define OPTi93X_DAC_RIGHT		0x07
#define OPTi93X_PLAY_FORMAT		0x08
#define OPTi93X_IFACE_CONF		0x09
#define OPTi93X_PIN_CTRL		0x0a
#define OPTi93X_ERR_INIT		0x0b
#define OPTi93X_ID			0x0c
#define OPTi93X_PLAY_UPR_CNT		0x0e
#define OPTi93X_PLAY_LWR_CNT		0x0f
#define OPTi931_AUX_LEFT_INPUT		0x10
#define OPTi931_AUX_RIGHT_INPUT		0x11
#define OPTi93X_LINE_LEFT_INPUT		0x12
#define OPTi93X_LINE_RIGHT_INPUT	0x13
#define OPTi93X_MIC_LEFT_INPUT		0x14
#define OPTi93X_MIC_RIGHT_INPUT		0x15
#define OPTi93X_OUT_LEFT		0x16
#define OPTi93X_OUT_RIGHT		0x17
#define OPTi93X_CAPT_FORMAT		0x1c
#define OPTi93X_CAPT_UPR_CNT		0x1e
#define OPTi93X_CAPT_LWR_CNT		0x1f

#define OPTi93X_TRD			0x20
#define OPTi93X_MCE			0x40
#define OPTi93X_INIT			0x80

#define OPTi93X_MIXOUT_MIC_GAIN		0x20
#define OPTi93X_MIXOUT_LINE		0x00
#define OPTi93X_MIXOUT_CD		0x40
#define OPTi93X_MIXOUT_MIC		0x80
#define OPTi93X_MIXOUT_MIXER		0xc0

#define OPTi93X_STEREO			0x10
#define OPTi93X_LINEAR_8		0x00
#define OPTi93X_ULAW_8			0x20
#define OPTi93X_LINEAR_16_LIT		0x40
#define OPTi93X_ALAW_8			0x60
#define OPTi93X_ADPCM_16		0xa0
#define OPTi93X_LINEAR_16_BIG		0xc0

#define OPTi93X_CAPTURE_PIO		0x80
#define OPTi93X_PLAYBACK_PIO		0x40
#define OPTi93X_AUTOCALIB		0x08
#define OPTi93X_SINGLE_DMA		0x04
#define OPTi93X_CAPTURE_ENABLE		0x02
#define OPTi93X_PLAYBACK_ENABLE		0x01

#define OPTi93X_IRQ_ENABLE		0x02

#define OPTi93X_DMA_REQUEST		0x10
#define OPTi93X_CALIB_IN_PROGRESS	0x20

#define OPTi93X_IRQ_PLAYBACK		0x04
#define OPTi93X_IRQ_CAPTURE		0x08


typedef struct _snd_opti93x opti93x_t;

struct _snd_opti93x {
	unsigned long port;
	struct resource *res_port;
	int irq;
	int dma1;
	int dma2;

	opti9xx_t *chip;
	unsigned short hardware;
	unsigned char image[32];

	unsigned char mce_bit;
	unsigned short mode;
	int mute;

	spinlock_t lock;

	snd_card_t *card;
	snd_pcm_t *pcm;
	snd_pcm_substream_t *playback_substream;
	snd_pcm_substream_t *capture_substream;
	unsigned int p_dma_size;
	unsigned int c_dma_size;
};

#define OPTi93X_MODE_NONE	0x00
#define OPTi93X_MODE_PLAY	0x01
#define OPTi93X_MODE_CAPTURE	0x02
#define OPTi93X_MODE_OPEN	(OPTi93X_MODE_PLAY | OPTi93X_MODE_CAPTURE)

#endif /* OPTi93X */

struct _snd_opti9xx {
	unsigned short hardware;
	unsigned char password;
	char name[7];

	unsigned long mc_base;
	struct resource *res_mc_base;
	unsigned long mc_base_size;
#ifdef OPTi93X
	unsigned long mc_indir_index;
#endif	/* OPTi93X */
	unsigned long pwd_reg;

	spinlock_t lock;

	long wss_base;
	int irq;
	int dma1;
#if defined(CS4231) || defined(OPTi93X)
	int dma2;
#endif	/* CS4231 || OPTi93X */

	long fm_port;

	long mpu_port;
	int mpu_irq;

#ifdef CONFIG_PNP
	struct pnp_dev *dev;
	struct pnp_dev *devmpu;
#endif	/* CONFIG_PNP */
};

static int snd_opti9xx_first_hit = 1;
static snd_card_t *snd_opti9xx_legacy = SNDRV_DEFAULT_PTR1;

#ifdef CONFIG_PNP

static struct pnp_card_device_id snd_opti9xx_pnpids[] = {
#ifndef OPTi93X
	/* OPTi 82C924 */
	{ .id = "OPT0924", .devs = { { "OPT0000" }, { "OPT0002" } }, .driver_data = 0x0924 },
	/* OPTi 82C925 */
	{ .id = "OPT0925", .devs = { { "OPT9250" }, { "OPT0002" } }, .driver_data = 0x0925 },
#else
	/* OPTi 82C931/3 */
	{ .id = "OPT0931", .devs = { { "OPT9310" }, { "OPT0002" } }, .driver_data = 0x0931 },
#endif	/* OPTi93X */
	{ .id = "" }
};

MODULE_DEVICE_TABLE(pnp_card, snd_opti9xx_pnpids);

#endif	/* CONFIG_PNP */

#ifdef OPTi93X
#define DRIVER_NAME	"snd-card-opti93x"
#else
#define DRIVER_NAME	"snd-card-opti92x"
#endif	/* OPTi93X */

static char * snd_opti9xx_names[] = {
	"unkown",
	"82C928",	"82C929",
	"82C924",	"82C925",
	"82C930",	"82C931",	"82C933"
};


static long snd_legacy_find_free_ioport(long *port_table, long size)
{
	while (*port_table != -1) {
		if (request_region(*port_table, size, "ALSA test")) {
			release_region(*port_table, size);
			return *port_table;
		}
		port_table++;
	}
	return -1;
}

static int __devinit snd_opti9xx_init(opti9xx_t *chip, unsigned short hardware)
{
	static int opti9xx_mc_size[] = {7, 7, 10, 10, 2, 2, 2};

	chip->hardware = hardware;
	strcpy(chip->name, snd_opti9xx_names[hardware]);

	chip->mc_base_size = opti9xx_mc_size[hardware];  

	spin_lock_init(&chip->lock);

	chip->wss_base = -1;
	chip->irq = -1;
	chip->dma1 = -1;
#if defined(CS4231) || defined (OPTi93X)
	chip->dma2 = -1;
#endif 	/* CS4231 || OPTi93X */
	chip->fm_port = -1;
	chip->mpu_port = -1;
	chip->mpu_irq = -1;

	switch (hardware) {
#ifndef OPTi93X
	case OPTi9XX_HW_82C928:
	case OPTi9XX_HW_82C929:
		chip->mc_base = 0xf8c;
		chip->password = (hardware == OPTi9XX_HW_82C928) ? 0xe2 : 0xe3;
		chip->pwd_reg = 3;
		break;

	case OPTi9XX_HW_82C924:
	case OPTi9XX_HW_82C925:
		chip->mc_base = 0xf8c;
		chip->password = 0xe5;
		chip->pwd_reg = 3;
		break;
#else	/* OPTi93X */

	case OPTi9XX_HW_82C930:
	case OPTi9XX_HW_82C931:
	case OPTi9XX_HW_82C933:
		chip->mc_base = (hardware == OPTi9XX_HW_82C930) ? 0xf8f : 0xf8d;
		chip->mc_indir_index = 0xe0e;
		chip->password = 0xe4;
		chip->pwd_reg = 0;
		break;
#endif	/* OPTi93X */

	default:
		snd_printk("chip %d not supported\n", hardware);
		return -ENODEV;
	}
	return 0;
}

static unsigned char snd_opti9xx_read(opti9xx_t *chip,
				      unsigned char reg)
{
	unsigned long flags;
	unsigned char retval = 0xff;

	spin_lock_irqsave(&chip->lock, flags);
	outb(chip->password, chip->mc_base + chip->pwd_reg);

	switch (chip->hardware) {
#ifndef OPTi93X
	case OPTi9XX_HW_82C924:
	case OPTi9XX_HW_82C925:
		if (reg > 7) {
			outb(reg, chip->mc_base + 8);
			outb(chip->password, chip->mc_base + chip->pwd_reg);
			retval = inb(chip->mc_base + 9);
			break;
		}

	case OPTi9XX_HW_82C928:
	case OPTi9XX_HW_82C929:
		retval = inb(chip->mc_base + reg);
		break;
#else	/* OPTi93X */

	case OPTi9XX_HW_82C930:
	case OPTi9XX_HW_82C931:
	case OPTi9XX_HW_82C933:
		outb(reg, chip->mc_indir_index);
		outb(chip->password, chip->mc_base + chip->pwd_reg);
		retval = inb(chip->mc_indir_index + 1);
		break;
#endif	/* OPTi93X */

	default:
		snd_printk("chip %d not supported\n", chip->hardware);
	}

	spin_unlock_irqrestore(&chip->lock, flags);
	return retval;
}
	
static void snd_opti9xx_write(opti9xx_t *chip, unsigned char reg,
			      unsigned char value)
{
	unsigned long flags;

	spin_lock_irqsave(&chip->lock, flags);
	outb(chip->password, chip->mc_base + chip->pwd_reg);

	switch (chip->hardware) {
#ifndef OPTi93X
	case OPTi9XX_HW_82C924:
	case OPTi9XX_HW_82C925:
		if (reg > 7) {
			outb(reg, chip->mc_base + 8);
			outb(chip->password, chip->mc_base + chip->pwd_reg);
			outb(value, chip->mc_base + 9);
			break;
		}

	case OPTi9XX_HW_82C928:
	case OPTi9XX_HW_82C929:
		outb(value, chip->mc_base + reg);
		break;
#else	/* OPTi93X */

	case OPTi9XX_HW_82C930:
	case OPTi9XX_HW_82C931:
	case OPTi9XX_HW_82C933:
		outb(reg, chip->mc_indir_index);
		outb(chip->password, chip->mc_base + chip->pwd_reg);
		outb(value, chip->mc_indir_index + 1);
		break;
#endif	/* OPTi93X */

	default:
		snd_printk("chip %d not supported\n", chip->hardware);
	}

	spin_unlock_irqrestore(&chip->lock, flags);
}


#define snd_opti9xx_write_mask(chip, reg, value, mask)	\
	snd_opti9xx_write(chip, reg,			\
		(snd_opti9xx_read(chip, reg) & ~(mask)) | ((value) & (mask)))


static int __devinit snd_opti9xx_configure(opti9xx_t *chip)
{
	unsigned char wss_base_bits;
	unsigned char irq_bits;
	unsigned char dma_bits;
	unsigned char mpu_port_bits = 0;
	unsigned char mpu_irq_bits;

	switch (chip->hardware) {
#ifndef OPTi93X
	case OPTi9XX_HW_82C924:
		snd_opti9xx_write_mask(chip, OPTi9XX_MC_REG(4), 0xf0, 0xfc);
		snd_opti9xx_write_mask(chip, OPTi9XX_MC_REG(6), 0x02, 0x02);

	case OPTi9XX_HW_82C925:
		snd_opti9xx_write_mask(chip, OPTi9XX_MC_REG(1), 0x80, 0x80);
		snd_opti9xx_write_mask(chip, OPTi9XX_MC_REG(2), 0x00, 0x20);
		snd_opti9xx_write_mask(chip, OPTi9XX_MC_REG(3), 0xf0, 0xff);
#ifdef CS4231
		snd_opti9xx_write_mask(chip, OPTi9XX_MC_REG(5), 0x02, 0x02);
#else
		snd_opti9xx_write_mask(chip, OPTi9XX_MC_REG(5), 0x00, 0x02);
#endif	/* CS4231 */
		break;

	case OPTi9XX_HW_82C928:
	case OPTi9XX_HW_82C929:
		snd_opti9xx_write_mask(chip, OPTi9XX_MC_REG(1), 0x80, 0x80);
		snd_opti9xx_write_mask(chip, OPTi9XX_MC_REG(2), 0x00, 0x20);
		/*
		snd_opti9xx_write_mask(chip, OPTi9XX_MC_REG(3), 0xa2, 0xae);
		*/
		snd_opti9xx_write_mask(chip, OPTi9XX_MC_REG(4), 0x00, 0x0c);
#ifdef CS4231
		snd_opti9xx_write_mask(chip, OPTi9XX_MC_REG(5), 0x02, 0x02);
#else
		snd_opti9xx_write_mask(chip, OPTi9XX_MC_REG(5), 0x00, 0x02);
#endif	/* CS4231 */
		break;

#else	/* OPTi93X */
	case OPTi9XX_HW_82C930:
	case OPTi9XX_HW_82C931:
	case OPTi9XX_HW_82C933:
		snd_opti9xx_write_mask(chip, OPTi9XX_MC_REG(6), 0x02, 0x03);
		snd_opti9xx_write_mask(chip, OPTi9XX_MC_REG(3), 0x00, 0xff);
		snd_opti9xx_write_mask(chip, OPTi9XX_MC_REG(4), 0x10 |
			(chip->hardware == OPTi9XX_HW_82C930 ? 0x00 : 0x04),
			0x34);
		snd_opti9xx_write_mask(chip, OPTi9XX_MC_REG(5), 0x20, 0xbf);
		break;
#endif	/* OPTi93X */

	default:
		snd_printk("chip %d not supported\n", chip->hardware);
		return -EINVAL;
	}

	switch (chip->wss_base) {
	case 0x530:
		wss_base_bits = 0x00;
		break;
	case 0x604:
		wss_base_bits = 0x03;
		break;
	case 0xe80:
		wss_base_bits = 0x01;
		break;
	case 0xf40:
		wss_base_bits = 0x02;
		break;
	default:
		snd_printk("WSS port 0x%lx not valid\n", chip->wss_base);
		goto __skip_base;
	}
	snd_opti9xx_write_mask(chip, OPTi9XX_MC_REG(1), wss_base_bits << 4, 0x30);

__skip_base:
	switch (chip->irq) {
//#ifdef OPTi93X
	case 5:
		irq_bits = 0x05;
		break;
//#endif	/* OPTi93X */
	case 7:
		irq_bits = 0x01;
		break;
	case 9:
		irq_bits = 0x02;
		break;
	case 10:
		irq_bits = 0x03;
		break;
	case 11:
		irq_bits = 0x04;
		break;
	default:
		snd_printk("WSS irq # %d not valid\n", chip->irq);
		goto __skip_resources;
	}

	switch (chip->dma1) {
	case 0:
		dma_bits = 0x01;
		break;
	case 1:
		dma_bits = 0x02;
		break;
	case 3:
		dma_bits = 0x03;
		break;
	default:
		snd_printk("WSS dma1 # %d not valid\n", chip->dma1);
		goto __skip_resources;
	}

#if defined(CS4231) || defined(OPTi93X)
	if (chip->dma1 == chip->dma2) {
		snd_printk("don't want to share dmas\n");
		return -EBUSY;
	}

	switch (chip->dma2) {
	case 0:
	case 1:
		break;
	default:
		snd_printk("WSS dma2 # %d not valid\n", chip->dma2);
		goto __skip_resources;
	}
	dma_bits |= 0x04;
#endif	/* CS4231 || OPTi93X */

#ifndef OPTi93X
	 outb(irq_bits << 3 | dma_bits, chip->wss_base);
#else /* OPTi93X */
	snd_opti9xx_write(chip, OPTi9XX_MC_REG(3), (irq_bits << 3 | dma_bits));
#endif /* OPTi93X */

__skip_resources:
	if (chip->hardware > OPTi9XX_HW_82C928) {
		switch (chip->mpu_port) {
		case 0:
		case -1:
			break;
		case 0x300:
			mpu_port_bits = 0x03;
			break;
		case 0x310:
			mpu_port_bits = 0x02;
			break;
		case 0x320:
			mpu_port_bits = 0x01;
			break;
		case 0x330:
			mpu_port_bits = 0x00;
			break;
		default:
			snd_printk("MPU-401 port 0x%lx not valid\n",
				chip->mpu_port);
			goto __skip_mpu;
		}

		switch (chip->mpu_irq) {
		case 5:
			mpu_irq_bits = 0x02;
			break;
		case 7:
			mpu_irq_bits = 0x03;
			break;
		case 9:
			mpu_irq_bits = 0x00;
			break;
		case 10:
			mpu_irq_bits = 0x01;
			break;
		default:
			snd_printk("MPU-401 irq # %d not valid\n",
				chip->mpu_irq);
			goto __skip_mpu;
		}

		snd_opti9xx_write_mask(chip, OPTi9XX_MC_REG(6),
			(chip->mpu_port <= 0) ? 0x00 :
				0x80 | mpu_port_bits << 5 | mpu_irq_bits << 3,
			0xf8);
	}
__skip_mpu:

	return 0;
}

#ifdef OPTi93X

static unsigned char snd_opti93x_default_image[32] =
{
	0x00,		/* 00/00 - l_mixout_outctrl */
	0x00,		/* 01/01 - r_mixout_outctrl */
	0x88,		/* 02/02 - l_cd_inctrl */
	0x88,		/* 03/03 - r_cd_inctrl */
	0x88,		/* 04/04 - l_a1/fm_inctrl */
	0x88,		/* 05/05 - r_a1/fm_inctrl */
	0x80,		/* 06/06 - l_dac_inctrl */
	0x80,		/* 07/07 - r_dac_inctrl */
	0x00,		/* 08/08 - ply_dataform_reg */
	0x00,		/* 09/09 - if_conf */
	0x00,		/* 0a/10 - pin_ctrl */
	0x00,		/* 0b/11 - err_init_reg */
	0x0a,		/* 0c/12 - id_reg */
	0x00,		/* 0d/13 - reserved */
	0x00,		/* 0e/14 - ply_upcount_reg */
	0x00,		/* 0f/15 - ply_lowcount_reg */
	0x88,		/* 10/16 - reserved/l_a1_inctrl */
	0x88,		/* 11/17 - reserved/r_a1_inctrl */
	0x88,		/* 12/18 - l_line_inctrl */
	0x88,		/* 13/19 - r_line_inctrl */
	0x88,		/* 14/20 - l_mic_inctrl */
	0x88,		/* 15/21 - r_mic_inctrl */
	0x80,		/* 16/22 - l_out_outctrl */
	0x80,		/* 17/23 - r_out_outctrl */
	0x00,		/* 18/24 - reserved */
	0x00,		/* 19/25 - reserved */
	0x00,		/* 1a/26 - reserved */
	0x00,		/* 1b/27 - reserved */
	0x00,		/* 1c/28 - cap_dataform_reg */
	0x00,		/* 1d/29 - reserved */
	0x00,		/* 1e/30 - cap_upcount_reg */
	0x00		/* 1f/31 - cap_lowcount_reg */
};


static int snd_opti93x_busy_wait(opti93x_t *chip)
{
	int timeout;

	for (timeout = 250; timeout-- > 0; udelay(10))
		if (!(inb(OPTi93X_PORT(chip, INDEX)) & OPTi93X_INIT))
			return 0;

	snd_printk("chip still busy.\n");
	return -EBUSY;
}

static unsigned char snd_opti93x_in(opti93x_t *chip, unsigned char reg)
{
	snd_opti93x_busy_wait(chip);
	outb(chip->mce_bit | (reg & 0x1f), OPTi93X_PORT(chip, INDEX));
	return inb(OPTi93X_PORT(chip, DATA));
}

static void snd_opti93x_out(opti93x_t *chip, unsigned char reg,
			    unsigned char value)
{
	snd_opti93x_busy_wait(chip);
	outb(chip->mce_bit | (reg & 0x1f), OPTi93X_PORT(chip, INDEX));
	outb(value, OPTi93X_PORT(chip, DATA));
}

static void snd_opti93x_out_image(opti93x_t *chip, unsigned char reg,
				  unsigned char value)
{
	snd_opti93x_out(chip, reg, chip->image[reg] = value);
}

static void snd_opti93x_out_mask(opti93x_t *chip, unsigned char reg,
				 unsigned char mask, unsigned char value)
{
	snd_opti93x_out_image(chip, reg,
		(chip->image[reg] & ~mask) | (value & mask));
}


static void snd_opti93x_mce_up(opti93x_t *chip)
{
	snd_opti93x_busy_wait(chip);

	chip->mce_bit = OPTi93X_MCE;
	if (!(inb(OPTi93X_PORT(chip, INDEX)) & OPTi93X_MCE))
		outb(chip->mce_bit, OPTi93X_PORT(chip, INDEX));
}

static void snd_opti93x_mce_down(opti93x_t *chip)
{
	snd_opti93x_busy_wait(chip);

	chip->mce_bit = 0;
	if (inb(OPTi93X_PORT(chip, INDEX)) & OPTi93X_MCE)
		outb(chip->mce_bit, OPTi93X_PORT(chip, INDEX));
}

#define snd_opti93x_mute_reg(chip, reg, mute)	\
	snd_opti93x_out(chip, reg, mute ? 0x80 : chip->image[reg]);

static void snd_opti93x_mute(opti93x_t *chip, int mute)
{
	mute = mute ? 1 : 0;
	if (chip->mute == mute)
		return;

	chip->mute = mute;

	snd_opti93x_mute_reg(chip, OPTi93X_CD_LEFT_INPUT, mute);
	snd_opti93x_mute_reg(chip, OPTi93X_CD_RIGHT_INPUT, mute);
	switch (chip->hardware) {
	case OPTi9XX_HW_82C930:
		snd_opti93x_mute_reg(chip, OPTi930_AUX_LEFT_INPUT, mute);
		snd_opti93x_mute_reg(chip, OPTi930_AUX_RIGHT_INPUT, mute);
		break;
	case OPTi9XX_HW_82C931:
	case OPTi9XX_HW_82C933:
		snd_opti93x_mute_reg(chip, OPTi931_FM_LEFT_INPUT, mute);
		snd_opti93x_mute_reg(chip, OPTi931_FM_RIGHT_INPUT, mute);
		snd_opti93x_mute_reg(chip, OPTi931_AUX_LEFT_INPUT, mute);
		snd_opti93x_mute_reg(chip, OPTi931_AUX_RIGHT_INPUT, mute);
	}
	snd_opti93x_mute_reg(chip, OPTi93X_DAC_LEFT, mute);
	snd_opti93x_mute_reg(chip, OPTi93X_DAC_RIGHT, mute);
	snd_opti93x_mute_reg(chip, OPTi93X_LINE_LEFT_INPUT, mute);
	snd_opti93x_mute_reg(chip, OPTi93X_LINE_RIGHT_INPUT, mute);
	snd_opti93x_mute_reg(chip, OPTi93X_MIC_LEFT_INPUT, mute);
	snd_opti93x_mute_reg(chip, OPTi93X_MIC_RIGHT_INPUT, mute);
	snd_opti93x_mute_reg(chip, OPTi93X_OUT_LEFT, mute);
	snd_opti93x_mute_reg(chip, OPTi93X_OUT_RIGHT, mute);
}


static unsigned int snd_opti93x_get_count(unsigned char format,
					  unsigned int size)
{
	switch (format & 0xe0) {
	case OPTi93X_LINEAR_16_LIT:
	case OPTi93X_LINEAR_16_BIG:
		size >>= 1;
		break;
	case OPTi93X_ADPCM_16:
		return size >> 2;
	}
	return (format & OPTi93X_STEREO) ? (size >> 1) : size;
}

static unsigned int rates[] = {  5512,  6615,  8000,  9600, 11025, 16000, 
				18900, 22050, 27428, 32000, 33075, 37800,
				44100, 48000 };
#define RATES ARRAY_SIZE(rates)

static snd_pcm_hw_constraint_list_t hw_constraints_rates = {
	.count = RATES,
	.list = rates,
	.mask = 0,
};

static unsigned char bits[] = {  0x01,  0x0f,  0x00,  0x0e,  0x03,  0x02,
				 0x05,  0x07,  0x04,  0x06,  0x0d,  0x09,
				 0x0b,  0x0c};

static unsigned char snd_opti93x_get_freq(unsigned int rate)
{
	unsigned int i;

	for (i = 0; i < RATES; i++) {
		if (rate == rates[i])
			return bits[i];
	}
	snd_BUG();
	return bits[RATES-1];
}

static unsigned char snd_opti93x_get_format(opti93x_t *chip,
					    unsigned int format, int channels)
{
	unsigned char retval = OPTi93X_LINEAR_8;

	switch (format) {
	case SNDRV_PCM_FORMAT_MU_LAW:
		retval = OPTi93X_ULAW_8;
		break;
	case SNDRV_PCM_FORMAT_A_LAW:
		retval = OPTi93X_ALAW_8;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		retval = OPTi93X_LINEAR_16_LIT;
		break;
	case SNDRV_PCM_FORMAT_S16_BE:
		retval = OPTi93X_LINEAR_16_BIG;
		break;
	case SNDRV_PCM_FORMAT_IMA_ADPCM:
		retval = OPTi93X_ADPCM_16;
	}
	return (channels > 1) ? (retval | OPTi93X_STEREO) : retval;
}


static void snd_opti93x_playback_format(opti93x_t *chip, unsigned char fmt)
{
	unsigned char mask;

	snd_opti93x_mute(chip, 1);

	snd_opti93x_mce_up(chip);
	mask = (chip->mode & OPTi93X_MODE_CAPTURE) ? 0xf0 : 0xff;
	snd_opti93x_out_mask(chip, OPTi93X_PLAY_FORMAT, mask, fmt);
	snd_opti93x_mce_down(chip);

	snd_opti93x_mute(chip, 0);
}

static void snd_opti93x_capture_format(opti93x_t *chip, unsigned char fmt)
{
	snd_opti93x_mute(chip, 1);

	snd_opti93x_mce_up(chip);
	if (!(chip->mode & OPTi93X_MODE_PLAY))
		snd_opti93x_out_mask(chip, OPTi93X_PLAY_FORMAT, 0x0f, fmt);
	else
		fmt = chip->image[OPTi93X_PLAY_FORMAT] & 0xf0;
	snd_opti93x_out_image(chip, OPTi93X_CAPT_FORMAT, fmt);
	snd_opti93x_mce_down(chip);

	snd_opti93x_mute(chip, 0);
}


static int snd_opti93x_open(opti93x_t *chip, unsigned int mode)
{
	unsigned long flags;

	spin_lock_irqsave(&chip->lock, flags);

	if (chip->mode & mode) {
		spin_unlock_irqrestore(&chip->lock, flags);
		return -EAGAIN;
	}

	if (!(chip->mode & OPTi93X_MODE_OPEN)) {
		outb(0x00, OPTi93X_PORT(chip, STATUS));
		snd_opti93x_out_mask(chip, OPTi93X_PIN_CTRL,
			OPTi93X_IRQ_ENABLE, OPTi93X_IRQ_ENABLE);
		chip->mode = mode;
	}
	else
		chip->mode |= mode;

	spin_unlock_irqrestore(&chip->lock, flags);
	return 0;
}

static void snd_opti93x_close(opti93x_t *chip, unsigned int mode)
{
	unsigned long flags;

	spin_lock_irqsave(&chip->lock, flags);

	chip->mode &= ~mode;
	if (chip->mode & OPTi93X_MODE_OPEN) {
		spin_unlock_irqrestore(&chip->lock, flags);
		return;
	}

	snd_opti93x_mute(chip, 1);

	outb(0, OPTi93X_PORT(chip, STATUS));
	snd_opti93x_out_mask(chip, OPTi93X_PIN_CTRL, OPTi93X_IRQ_ENABLE,
		~OPTi93X_IRQ_ENABLE);

	snd_opti93x_mce_up(chip);
	snd_opti93x_out_image(chip, OPTi93X_IFACE_CONF, 0x00);
	snd_opti93x_mce_down(chip);
	chip->mode = 0;

	snd_opti93x_mute(chip, 0);
	spin_unlock_irqrestore(&chip->lock, flags);
}

static int snd_opti93x_trigger(snd_pcm_substream_t *substream, 
			       unsigned char what, int cmd)
{
	opti93x_t *chip = snd_pcm_substream_chip(substream);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_STOP:
	{
		unsigned int what = 0;
		struct list_head *pos;
		snd_pcm_substream_t *s;
		snd_pcm_group_for_each(pos, substream) {
			s = snd_pcm_group_substream_entry(pos);
			if (s == chip->playback_substream) {
				what |= OPTi93X_PLAYBACK_ENABLE;
				snd_pcm_trigger_done(s, substream);
			} else if (s == chip->capture_substream) {
				what |= OPTi93X_CAPTURE_ENABLE;
				snd_pcm_trigger_done(s, substream);
			}
		}
		spin_lock(&chip->lock);
		if (cmd == SNDRV_PCM_TRIGGER_START) {
			snd_opti93x_out_mask(chip, OPTi93X_IFACE_CONF, what, what);
			if (what & OPTi93X_CAPTURE_ENABLE)
				udelay(50);
		} else
			snd_opti93x_out_mask(chip, OPTi93X_IFACE_CONF, what, 0x00);
		spin_unlock(&chip->lock);
		break;
	}
	default:
		return -EINVAL;
	}
	return 0;
}

static int snd_opti93x_playback_trigger(snd_pcm_substream_t *substream, int cmd)
{
	return snd_opti93x_trigger(substream,
				   OPTi93X_PLAYBACK_ENABLE, cmd);
}

static int snd_opti93x_capture_trigger(snd_pcm_substream_t * substream, int cmd)
{
	return snd_opti93x_trigger(substream,
				   OPTi93X_CAPTURE_ENABLE, cmd);
}

static int snd_opti93x_hw_params(snd_pcm_substream_t * substream,
				 snd_pcm_hw_params_t * hw_params)
{
	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
}


static int snd_opti93x_hw_free(snd_pcm_substream_t * substream)
{
	snd_pcm_lib_free_pages(substream);
	return 0;
}


static int snd_opti93x_playback_prepare(snd_pcm_substream_t * substream)
{
	opti93x_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	unsigned long flags;
	unsigned char format;
	unsigned int count = snd_pcm_lib_period_bytes(substream);
	unsigned int size = snd_pcm_lib_buffer_bytes(substream);

	spin_lock_irqsave(&chip->lock, flags);

	chip->p_dma_size = size;
	snd_opti93x_out_mask(chip, OPTi93X_IFACE_CONF,
		OPTi93X_PLAYBACK_ENABLE | OPTi93X_PLAYBACK_PIO,
		~(OPTi93X_PLAYBACK_ENABLE | OPTi93X_PLAYBACK_PIO));

	snd_dma_program(chip->dma1, runtime->dma_addr, size,
		DMA_MODE_WRITE | DMA_AUTOINIT);

	format = snd_opti93x_get_freq(runtime->rate);
	format |= snd_opti93x_get_format(chip, runtime->format,
		runtime->channels);
	snd_opti93x_playback_format(chip, format);
	format = chip->image[OPTi93X_PLAY_FORMAT];

	count = snd_opti93x_get_count(format, count) - 1;
	snd_opti93x_out_image(chip, OPTi93X_PLAY_LWR_CNT, count);
	snd_opti93x_out_image(chip, OPTi93X_PLAY_UPR_CNT, count >> 8);

	spin_unlock_irqrestore(&chip->lock, flags);
	return 0;
}

static int snd_opti93x_capture_prepare(snd_pcm_substream_t *substream)
{
	opti93x_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	unsigned long flags;
	unsigned char format;
	unsigned int count = snd_pcm_lib_period_bytes(substream);
	unsigned int size = snd_pcm_lib_buffer_bytes(substream);

	spin_lock_irqsave(&chip->lock, flags);

	chip->c_dma_size = size;
	snd_opti93x_out_mask(chip, OPTi93X_IFACE_CONF,
		OPTi93X_CAPTURE_ENABLE | OPTi93X_CAPTURE_PIO, 0);

	snd_dma_program(chip->dma2, runtime->dma_addr, size,
		DMA_MODE_READ | DMA_AUTOINIT);

	format = snd_opti93x_get_freq(runtime->rate);
	format |= snd_opti93x_get_format(chip, runtime->format,
		runtime->channels);
	snd_opti93x_capture_format(chip, format);
	format = chip->image[OPTi93X_CAPT_FORMAT];

	count = snd_opti93x_get_count(format, count) - 1;
	snd_opti93x_out_image(chip, OPTi93X_CAPT_LWR_CNT, count);
	snd_opti93x_out_image(chip, OPTi93X_CAPT_UPR_CNT, count >> 8);

	spin_unlock_irqrestore(&chip->lock, flags);
	return 0;
}

static snd_pcm_uframes_t snd_opti93x_playback_pointer(snd_pcm_substream_t *substream)
{
	opti93x_t *chip = snd_pcm_substream_chip(substream);
	size_t ptr;

	if (!(chip->image[OPTi93X_IFACE_CONF] & OPTi93X_PLAYBACK_ENABLE))
		return 0;

	ptr = snd_dma_pointer(chip->dma1, chip->p_dma_size);
	return bytes_to_frames(substream->runtime, ptr);
}

static snd_pcm_uframes_t snd_opti93x_capture_pointer(snd_pcm_substream_t *substream)
{
	opti93x_t *chip = snd_pcm_substream_chip(substream);
	size_t ptr;
	
	if (!(chip->image[OPTi93X_IFACE_CONF] & OPTi93X_CAPTURE_ENABLE))
		return 0;

	ptr = snd_dma_pointer(chip->dma2, chip->c_dma_size);
	return bytes_to_frames(substream->runtime, ptr);
}


static void snd_opti93x_overrange(opti93x_t *chip)
{
	unsigned long flags;

	spin_lock_irqsave(&chip->lock, flags);

	if (snd_opti93x_in(chip, OPTi93X_ERR_INIT) & (0x08 | 0x02))
		chip->capture_substream->runtime->overrange++;

	spin_unlock_irqrestore(&chip->lock, flags);
}

static irqreturn_t snd_opti93x_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	opti93x_t *codec = dev_id;
	unsigned char status;

	status = snd_opti9xx_read(codec->chip, OPTi9XX_MC_REG(11));
	if ((status & OPTi93X_IRQ_PLAYBACK) && codec->playback_substream)
		snd_pcm_period_elapsed(codec->playback_substream);
	if ((status & OPTi93X_IRQ_CAPTURE) && codec->capture_substream) {
		snd_opti93x_overrange(codec);
		snd_pcm_period_elapsed(codec->capture_substream);
	}
	outb(0x00, OPTi93X_PORT(codec, STATUS));
	return IRQ_HANDLED;
}


static snd_pcm_hardware_t snd_opti93x_playback = {
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_SYNC_START),
	.formats =		(SNDRV_PCM_FMTBIT_MU_LAW | SNDRV_PCM_FMTBIT_A_LAW | SNDRV_PCM_FMTBIT_IMA_ADPCM |
				 SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S16_BE),
	.rates =		SNDRV_PCM_RATE_KNOT | SNDRV_PCM_RATE_8000_48000,
	.rate_min =		5512,
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

static snd_pcm_hardware_t snd_opti93x_capture = {
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_SYNC_START),
	.formats =		(SNDRV_PCM_FMTBIT_MU_LAW | SNDRV_PCM_FMTBIT_A_LAW | SNDRV_PCM_FMTBIT_IMA_ADPCM |
				 SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S16_BE),
	.rates =		SNDRV_PCM_RATE_8000_48000,
	.rate_min =		5512,
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

static int snd_opti93x_playback_open(snd_pcm_substream_t *substream)
{
	int error;
	opti93x_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;

	if ((error = snd_opti93x_open(chip, OPTi93X_MODE_PLAY)) < 0)
		return error;
	snd_pcm_set_sync(substream);
	chip->playback_substream = substream;
	runtime->hw = snd_opti93x_playback;
	snd_pcm_limit_isa_dma_size(chip->dma1, &runtime->hw.buffer_bytes_max);
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE, &hw_constraints_rates);
	return error;
}

static int snd_opti93x_capture_open(snd_pcm_substream_t *substream)
{
	int error;
	opti93x_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;

	if ((error = snd_opti93x_open(chip, OPTi93X_MODE_CAPTURE)) < 0)
		return error;
	runtime->hw = snd_opti93x_capture;
	snd_pcm_set_sync(substream);
	chip->capture_substream = substream;
	snd_pcm_limit_isa_dma_size(chip->dma2, &runtime->hw.buffer_bytes_max);
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE, &hw_constraints_rates);
	return error;
}

static int snd_opti93x_playback_close(snd_pcm_substream_t *substream)
{
	opti93x_t *chip = snd_pcm_substream_chip(substream);

	chip->playback_substream = NULL;
	snd_opti93x_close(chip, OPTi93X_MODE_PLAY);
	return 0;
}

static int snd_opti93x_capture_close(snd_pcm_substream_t *substream)
{
	opti93x_t *chip = snd_pcm_substream_chip(substream);

	chip->capture_substream = NULL;
	snd_opti93x_close(chip, OPTi93X_MODE_CAPTURE);
	return 0;
}


static void snd_opti93x_init(opti93x_t *chip)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&chip->lock, flags);
	snd_opti93x_mce_up(chip);

	for (i = 0; i < 32; i++)
		snd_opti93x_out_image(chip, i, snd_opti93x_default_image[i]);

	snd_opti93x_mce_down(chip);
	spin_unlock_irqrestore(&chip->lock, flags);
}

static int snd_opti93x_probe(opti93x_t *chip)
{
	unsigned long flags;
	unsigned char val;

	spin_lock_irqsave(&chip->lock, flags);
	val = snd_opti93x_in(chip, OPTi93X_ID) & 0x0f;
	spin_unlock_irqrestore(&chip->lock, flags);

	return (val == 0x0a) ? 0 : -ENODEV;
}

static int snd_opti93x_free(opti93x_t *chip)
{
	release_and_free_resource(chip->res_port);
	if (chip->dma1 >= 0) {
		disable_dma(chip->dma1);
		free_dma(chip->dma1);
	}
	if (chip->dma2 >= 0) {
		disable_dma(chip->dma2);
		free_dma(chip->dma2);
	}
	if (chip->irq >= 0) {
	  free_irq(chip->irq, chip);
	}
	kfree(chip);
	return 0;
}

static int snd_opti93x_dev_free(snd_device_t *device)
{
	opti93x_t *chip = device->device_data;
	return snd_opti93x_free(chip);
}

static const char *snd_opti93x_chip_id(opti93x_t *codec)
{
	switch (codec->hardware) {
	case OPTi9XX_HW_82C930: return "82C930";
	case OPTi9XX_HW_82C931: return "82C931";
	case OPTi9XX_HW_82C933: return "82C933";
	default:		return "???";
	}
}

static int snd_opti93x_create(snd_card_t *card, opti9xx_t *chip,
			      int dma1, int dma2,
			      opti93x_t **rcodec)
{
	static snd_device_ops_t ops = {
		.dev_free =	snd_opti93x_dev_free,
	};
	int error;
	opti93x_t *codec;

	*rcodec = NULL;
	codec = kzalloc(sizeof(*codec), GFP_KERNEL);
	if (codec == NULL)
		return -ENOMEM;
	codec->irq = -1;
	codec->dma1 = -1;
	codec->dma2 = -1;

	if ((codec->res_port = request_region(chip->wss_base + 4, 4, "OPTI93x CODEC")) == NULL) {
		snd_printk(KERN_ERR "opti9xx: can't grab port 0x%lx\n", chip->wss_base + 4);
		snd_opti93x_free(codec);
		return -EBUSY;
	}
	if (request_dma(dma1, "OPTI93x - 1")) {
		snd_printk(KERN_ERR "opti9xx: can't grab DMA1 %d\n", dma1);
		snd_opti93x_free(codec);
		return -EBUSY;
	}
	codec->dma1 = chip->dma1;
	if (request_dma(dma2, "OPTI93x - 2")) {
		snd_printk(KERN_ERR "opti9xx: can't grab DMA2 %d\n", dma2);
		snd_opti93x_free(codec);
		return -EBUSY;
	}
	codec->dma2 = chip->dma2;

	if (request_irq(chip->irq, snd_opti93x_interrupt, SA_INTERRUPT, DRIVER_NAME" - WSS", codec)) {
		snd_printk(KERN_ERR "opti9xx: can't grab IRQ %d\n", chip->irq);
		snd_opti93x_free(codec);
		return -EBUSY;
	}

	codec->card = card;
	codec->port = chip->wss_base + 4;
	codec->irq = chip->irq;

	spin_lock_init(&codec->lock);
	codec->hardware = chip->hardware;
	codec->chip = chip;

	if ((error = snd_opti93x_probe(codec))) {
		snd_opti93x_free(codec);
		return error;
	}

	snd_opti93x_init(codec);

	/* Register device */
	if ((error = snd_device_new(card, SNDRV_DEV_LOWLEVEL, codec, &ops)) < 0) {
		snd_opti93x_free(codec);
		return error;
	}

	*rcodec = codec;
	return 0;
}

static snd_pcm_ops_t snd_opti93x_playback_ops = {
	.open =		snd_opti93x_playback_open,
	.close =	snd_opti93x_playback_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_opti93x_hw_params,
	.hw_free =	snd_opti93x_hw_free,
	.prepare =	snd_opti93x_playback_prepare,
	.trigger =	snd_opti93x_playback_trigger,
	.pointer =	snd_opti93x_playback_pointer,
};

static snd_pcm_ops_t snd_opti93x_capture_ops = {
	.open =		snd_opti93x_capture_open,
	.close =	snd_opti93x_capture_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_opti93x_hw_params,
	.hw_free =	snd_opti93x_hw_free,
	.prepare =	snd_opti93x_capture_prepare,
	.trigger =	snd_opti93x_capture_trigger,
	.pointer =	snd_opti93x_capture_pointer,
};

static void snd_opti93x_pcm_free(snd_pcm_t *pcm)
{
	opti93x_t *codec = pcm->private_data;
	codec->pcm = NULL;
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

static int snd_opti93x_pcm(opti93x_t *codec, int device, snd_pcm_t **rpcm)
{
	int error;
	snd_pcm_t *pcm;

	if ((error = snd_pcm_new(codec->card, "OPTi 82C93X", device, 1, 1, &pcm)))
		return error;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_opti93x_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_opti93x_capture_ops);

	pcm->private_data = codec;
	pcm->private_free = snd_opti93x_pcm_free;
	pcm->info_flags = SNDRV_PCM_INFO_JOINT_DUPLEX;

	strcpy(pcm->name, snd_opti93x_chip_id(codec));

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
					      snd_dma_isa_data(),
					      64*1024, codec->dma1 > 3 || codec->dma2 > 3 ? 128*1024 : 64*1024);

	codec->pcm = pcm;
	if (rpcm)
		*rpcm = pcm;
	return 0;
}

/*
 *  MIXER part
 */

static int snd_opti93x_info_mux(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	static char *texts[4] = {
		"Line1", "Aux", "Mic", "Mix"
	};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 2;
	uinfo->value.enumerated.items = 4;
	if (uinfo->value.enumerated.item > 3)
		uinfo->value.enumerated.item = 3;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_opti93x_get_mux(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	opti93x_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	
	spin_lock_irqsave(&chip->lock, flags);
	ucontrol->value.enumerated.item[0] = (chip->image[OPTi93X_MIXOUT_LEFT] & OPTi93X_MIXOUT_MIXER) >> 6;
	ucontrol->value.enumerated.item[1] = (chip->image[OPTi93X_MIXOUT_RIGHT] & OPTi93X_MIXOUT_MIXER) >> 6;
	spin_unlock_irqrestore(&chip->lock, flags);
	return 0;
}

static int snd_opti93x_put_mux(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	opti93x_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	unsigned short left, right;
	int change;
	
	if (ucontrol->value.enumerated.item[0] > 3 ||
	    ucontrol->value.enumerated.item[1] > 3)
		return -EINVAL;
	left = ucontrol->value.enumerated.item[0] << 6;
	right = ucontrol->value.enumerated.item[1] << 6;
	spin_lock_irqsave(&chip->lock, flags);
	left = (chip->image[OPTi93X_MIXOUT_LEFT] & ~OPTi93X_MIXOUT_MIXER) | left;
	right = (chip->image[OPTi93X_MIXOUT_RIGHT] & ~OPTi93X_MIXOUT_MIXER) | right;
	change = left != chip->image[OPTi93X_MIXOUT_LEFT] ||
	         right != chip->image[OPTi93X_MIXOUT_RIGHT];
	snd_opti93x_out_image(chip, OPTi93X_MIXOUT_LEFT, left);
	snd_opti93x_out_image(chip, OPTi93X_MIXOUT_RIGHT, right);
	spin_unlock_irqrestore(&chip->lock, flags);
	return change;
}

#if 0

#define OPTi93X_SINGLE(xname, xindex, reg, shift, mask, invert) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .info = snd_opti93x_info_single, \
  .get = snd_opti93x_get_single, .put = snd_opti93x_put_single, \
  .private_value = reg | (shift << 8) | (mask << 16) | (invert << 24) }

static int snd_opti93x_info_single(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	int mask = (kcontrol->private_value >> 16) & 0xff;

	uinfo->type = mask == 1 ? SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mask;
	return 0;
}

static int snd_opti93x_get_single(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	opti93x_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0xff;
	int mask = (kcontrol->private_value >> 16) & 0xff;
	int invert = (kcontrol->private_value >> 24) & 0xff;
	
	spin_lock_irqsave(&chip->lock, flags);
	ucontrol->value.integer.value[0] = (chip->image[reg] >> shift) & mask;
	spin_unlock_irqrestore(&chip->lock, flags);
	if (invert)
		ucontrol->value.integer.value[0] = mask - ucontrol->value.integer.value[0];
	return 0;
}

static int snd_opti93x_put_single(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	opti93x_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0xff;
	int mask = (kcontrol->private_value >> 16) & 0xff;
	int invert = (kcontrol->private_value >> 24) & 0xff;
	int change;
	unsigned short val;
	
	val = (ucontrol->value.integer.value[0] & mask);
	if (invert)
		val = mask - val;
	val <<= shift;
	spin_lock_irqsave(&chip->lock, flags);
	val = (chip->image[reg] & ~(mask << shift)) | val;
	change = val != chip->image[reg];
	snd_opti93x_out(chip, reg, val);
	spin_unlock_irqrestore(&chip->lock, flags);
	return change;
}

#endif /* single */

#define OPTi93X_DOUBLE(xname, xindex, left_reg, right_reg, shift_left, shift_right, mask, invert) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .info = snd_opti93x_info_double, \
  .get = snd_opti93x_get_double, .put = snd_opti93x_put_double, \
  .private_value = left_reg | (right_reg << 8) | (shift_left << 16) | (shift_right << 19) | (mask << 24) | (invert << 22) }

#define OPTi93X_DOUBLE_INVERT_INVERT(xctl) \
	do { xctl.private_value ^= 22; } while (0)
#define OPTi93X_DOUBLE_CHANGE_REGS(xctl, left_reg, right_reg) \
	do { xctl.private_value &= ~0x0000ffff; \
	     xctl.private_value |= left_reg | (right_reg << 8); } while (0)

static int snd_opti93x_info_double(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	int mask = (kcontrol->private_value >> 24) & 0xff;

	uinfo->type = mask == 1 ? SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mask;
	return 0;
}

static int snd_opti93x_get_double(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	opti93x_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int left_reg = kcontrol->private_value & 0xff;
	int right_reg = (kcontrol->private_value >> 8) & 0xff;
	int shift_left = (kcontrol->private_value >> 16) & 0x07;
	int shift_right = (kcontrol->private_value >> 19) & 0x07;
	int mask = (kcontrol->private_value >> 24) & 0xff;
	int invert = (kcontrol->private_value >> 22) & 1;
	
	spin_lock_irqsave(&chip->lock, flags);
	ucontrol->value.integer.value[0] = (chip->image[left_reg] >> shift_left) & mask;
	ucontrol->value.integer.value[1] = (chip->image[right_reg] >> shift_right) & mask;
	spin_unlock_irqrestore(&chip->lock, flags);
	if (invert) {
		ucontrol->value.integer.value[0] = mask - ucontrol->value.integer.value[0];
		ucontrol->value.integer.value[1] = mask - ucontrol->value.integer.value[1];
	}
	return 0;
}

static int snd_opti93x_put_double(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	opti93x_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int left_reg = kcontrol->private_value & 0xff;
	int right_reg = (kcontrol->private_value >> 8) & 0xff;
	int shift_left = (kcontrol->private_value >> 16) & 0x07;
	int shift_right = (kcontrol->private_value >> 19) & 0x07;
	int mask = (kcontrol->private_value >> 24) & 0xff;
	int invert = (kcontrol->private_value >> 22) & 1;
	int change;
	unsigned short val1, val2;
	
	val1 = ucontrol->value.integer.value[0] & mask;
	val2 = ucontrol->value.integer.value[1] & mask;
	if (invert) {
		val1 = mask - val1;
		val2 = mask - val2;
	}
	val1 <<= shift_left;
	val2 <<= shift_right;
	spin_lock_irqsave(&chip->lock, flags);
	val1 = (chip->image[left_reg] & ~(mask << shift_left)) | val1;
	val2 = (chip->image[right_reg] & ~(mask << shift_right)) | val2;
	change = val1 != chip->image[left_reg] || val2 != chip->image[right_reg];
	snd_opti93x_out_image(chip, left_reg, val1);
	snd_opti93x_out_image(chip, right_reg, val2);
	spin_unlock_irqrestore(&chip->lock, flags);
	return change;
}

static snd_kcontrol_new_t snd_opti93x_controls[] = {
OPTi93X_DOUBLE("Master Playback Switch", 0, OPTi93X_OUT_LEFT, OPTi93X_OUT_RIGHT, 7, 7, 1, 1),
OPTi93X_DOUBLE("Master Playback Volume", 0, OPTi93X_OUT_LEFT, OPTi93X_OUT_RIGHT, 1, 1, 31, 1), 
OPTi93X_DOUBLE("PCM Playback Switch", 0, OPTi93X_DAC_LEFT, OPTi93X_DAC_RIGHT, 7, 7, 1, 1),
OPTi93X_DOUBLE("PCM Playback Volume", 0, OPTi93X_DAC_LEFT, OPTi93X_DAC_RIGHT, 0, 0, 31, 1),
OPTi93X_DOUBLE("FM Playback Switch", 0, OPTi931_FM_LEFT_INPUT, OPTi931_FM_RIGHT_INPUT, 7, 7, 1, 1),
OPTi93X_DOUBLE("FM Playback Volume", 0, OPTi931_FM_LEFT_INPUT, OPTi931_FM_RIGHT_INPUT, 1, 1, 15, 1),
OPTi93X_DOUBLE("Line Playback Switch", 0, OPTi93X_LINE_LEFT_INPUT, OPTi93X_LINE_RIGHT_INPUT, 7, 7, 1, 1),
OPTi93X_DOUBLE("Line Playback Volume", 0, OPTi93X_LINE_LEFT_INPUT, OPTi93X_LINE_RIGHT_INPUT, 1, 1, 15, 1), 
OPTi93X_DOUBLE("Mic Playback Switch", 0, OPTi93X_MIC_LEFT_INPUT, OPTi93X_MIC_RIGHT_INPUT, 7, 7, 1, 1),
OPTi93X_DOUBLE("Mic Playback Volume", 0, OPTi93X_MIC_LEFT_INPUT, OPTi93X_MIC_RIGHT_INPUT, 1, 1, 15, 1), 
OPTi93X_DOUBLE("Mic Boost", 0, OPTi93X_MIXOUT_LEFT, OPTi93X_MIXOUT_RIGHT, 5, 5, 1, 1),
OPTi93X_DOUBLE("CD Playback Switch", 0, OPTi93X_CD_LEFT_INPUT, OPTi93X_CD_RIGHT_INPUT, 7, 7, 1, 1),
OPTi93X_DOUBLE("CD Playback Volume", 0, OPTi93X_CD_LEFT_INPUT, OPTi93X_CD_RIGHT_INPUT, 1, 1, 15, 1),
OPTi93X_DOUBLE("Aux Playback Switch", 0, OPTi931_AUX_LEFT_INPUT, OPTi931_AUX_RIGHT_INPUT, 7, 7, 1, 1),
OPTi93X_DOUBLE("Aux Playback Volume", 0, OPTi931_AUX_LEFT_INPUT, OPTi931_AUX_RIGHT_INPUT, 1, 1, 15, 1), 
OPTi93X_DOUBLE("Capture Volume", 0, OPTi93X_MIXOUT_LEFT, OPTi93X_MIXOUT_RIGHT, 0, 0, 15, 0),
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Capture Source",
	.info = snd_opti93x_info_mux,
	.get = snd_opti93x_get_mux,
	.put = snd_opti93x_put_mux,
}
};
                                        
static int snd_opti93x_mixer(opti93x_t *chip)
{
	snd_card_t *card;
	snd_kcontrol_new_t knew;
	int err;
	unsigned int idx;

	snd_assert(chip != NULL && chip->card != NULL, return -EINVAL);

	card = chip->card;

	strcpy(card->mixername, snd_opti93x_chip_id(chip));

	for (idx = 0; idx < ARRAY_SIZE(snd_opti93x_controls); idx++) {
		knew = snd_opti93x_controls[idx];
		if (chip->hardware == OPTi9XX_HW_82C930) {
			if (strstr(knew.name, "FM"))	/* skip FM controls */
				continue;
			else if (strcmp(knew.name, "Mic Playback Volume"))
				OPTi93X_DOUBLE_INVERT_INVERT(knew);
			else if (strstr(knew.name, "Aux"))
				OPTi93X_DOUBLE_CHANGE_REGS(knew, OPTi930_AUX_LEFT_INPUT, OPTi930_AUX_RIGHT_INPUT);
			else if (strcmp(knew.name, "PCM Playback Volume"))
				OPTi93X_DOUBLE_INVERT_INVERT(knew);
			else if (strcmp(knew.name, "Master Playback Volume"))
				OPTi93X_DOUBLE_INVERT_INVERT(knew);
		}
		if ((err = snd_ctl_add(card, snd_ctl_new1(&snd_opti93x_controls[idx], chip))) < 0)
			return err;
	}
	return 0;
}

#endif /* OPTi93X */

static int __devinit snd_card_opti9xx_detect(snd_card_t *card, opti9xx_t *chip)
{
	int i, err;

#ifndef OPTi93X
	for (i = OPTi9XX_HW_82C928; i < OPTi9XX_HW_82C930; i++) {
		unsigned char value;

		if ((err = snd_opti9xx_init(chip, i)) < 0)
			return err;

		if ((chip->res_mc_base = request_region(chip->mc_base, chip->mc_base_size, "OPTi9xx MC")) == NULL)
			continue;

		value = snd_opti9xx_read(chip, OPTi9XX_MC_REG(1));
		if ((value != 0xff) && (value != inb(chip->mc_base + 1)))
			if (value == snd_opti9xx_read(chip, OPTi9XX_MC_REG(1)))
				return 1;

		release_and_free_resource(chip->res_mc_base);
		chip->res_mc_base = NULL;

	}
#else	/* OPTi93X */
	for (i = OPTi9XX_HW_82C931; i >= OPTi9XX_HW_82C930; i--) {
		unsigned long flags;
		unsigned char value;

		if ((err = snd_opti9xx_init(chip, i)) < 0)
			return err;

		if ((chip->res_mc_base = request_region(chip->mc_base, chip->mc_base_size, "OPTi9xx MC")) == NULL)
			continue;

		spin_lock_irqsave(&chip->lock, flags);
		outb(chip->password, chip->mc_base + chip->pwd_reg);
		outb(((chip->mc_indir_index & (1 << 8)) >> 4) |
			((chip->mc_indir_index & 0xf0) >> 4), chip->mc_base);
		spin_unlock_irqrestore(&chip->lock, flags);

		value = snd_opti9xx_read(chip, OPTi9XX_MC_REG(7));
		snd_opti9xx_write(chip, OPTi9XX_MC_REG(7), 0xff - value);
		if (snd_opti9xx_read(chip, OPTi9XX_MC_REG(7)) == 0xff - value)
			return 1;

		release_and_free_resource(chip->res_mc_base);
		chip->res_mc_base = NULL;
	}
#endif	/* OPTi93X */

	return -ENODEV;
}

#ifdef CONFIG_PNP
static int __devinit snd_card_opti9xx_pnp(opti9xx_t *chip, struct pnp_card_link *card,
					  const struct pnp_card_device_id *pid)
{
	struct pnp_dev *pdev;
	struct pnp_resource_table *cfg = kmalloc(sizeof(*cfg), GFP_KERNEL);
	int err;

	chip->dev = pnp_request_card_device(card, pid->devs[0].id, NULL);
	if (chip->dev == NULL) {
		kfree(cfg);
		return -EBUSY;
	}
	chip->devmpu = pnp_request_card_device(card, pid->devs[1].id, NULL);

	pdev = chip->dev;
	pnp_init_resource_table(cfg);

#ifdef OPTi93X
	if (port != SNDRV_AUTO_PORT)
		pnp_resource_change(&cfg->port_resource[0], port + 4, 4);
#else
	if (pid->driver_data != 0x0924 && port != SNDRV_AUTO_PORT)
		pnp_resource_change(&cfg->port_resource[1], port, 4);
#endif	/* OPTi93X */
	if (irq != SNDRV_AUTO_IRQ)
		pnp_resource_change(&cfg->irq_resource[0], irq, 1);
	if (dma1 != SNDRV_AUTO_DMA)
		pnp_resource_change(&cfg->dma_resource[0], dma1, 1);
#if defined(CS4231) || defined(OPTi93X)
	if (dma2 != SNDRV_AUTO_DMA)
		pnp_resource_change(&cfg->dma_resource[1], dma2, 1);
#else
#ifdef snd_opti9xx_fixup_dma2
	snd_opti9xx_fixup_dma2(pdev);
#endif
#endif	/* CS4231 || OPTi93X */
#ifdef OPTi93X
	if (fm_port > 0 && fm_port != SNDRV_AUTO_PORT)
		pnp_resource_change(&cfg->port_resource[1], fm_port, 4);
#else
	if (fm_port > 0 && fm_port != SNDRV_AUTO_PORT)
		pnp_resource_change(&cfg->port_resource[2], fm_port, 4);
#endif
	if (pnp_manual_config_dev(pdev, cfg, 0) < 0)
		snd_printk(KERN_ERR "AUDIO the requested resources are invalid, using auto config\n");
	err = pnp_activate_dev(pdev);
	if (err < 0) {
		snd_printk(KERN_ERR "AUDIO pnp configure failure: %d\n", err);
		kfree(cfg);
		return err;
	}

#ifdef OPTi93X
	port = pnp_port_start(pdev, 0) - 4;
	fm_port = pnp_port_start(pdev, 1);
#else
	if (pid->driver_data != 0x0924)
		port = pnp_port_start(pdev, 1);
	fm_port = pnp_port_start(pdev, 2);
#endif	/* OPTi93X */
	irq = pnp_irq(pdev, 0);
	dma1 = pnp_dma(pdev, 0);
#if defined(CS4231) || defined(OPTi93X)
	dma2 = pnp_dma(pdev, 1);
#endif	/* CS4231 || OPTi93X */

	pdev = chip->devmpu;
	if (pdev && mpu_port > 0) {
		pnp_init_resource_table(cfg);

		if (mpu_port != SNDRV_AUTO_PORT)
			pnp_resource_change(&cfg->port_resource[0], mpu_port, 2);
		if (mpu_irq != SNDRV_AUTO_IRQ)
			pnp_resource_change(&cfg->irq_resource[0], mpu_irq, 1);

		if (pnp_manual_config_dev(pdev, cfg, 0) < 0)
			snd_printk(KERN_ERR "AUDIO the requested resources are invalid, using auto config\n");
		err = pnp_activate_dev(pdev);
		if (err < 0) {
			snd_printk(KERN_ERR "AUDIO pnp configure failure\n");
			mpu_port = -1;
			chip->devmpu = NULL;
		} else {
			mpu_port = pnp_port_start(pdev, 0);
			mpu_irq = pnp_irq(pdev, 0);
		}
	}
	kfree(cfg);
	return pid->driver_data;
}
#endif	/* CONFIG_PNP */

#if 0
static int __devinit snd_card_opti9xx_resources(struct snd_card_opti9xx *chip,
						snd_card_t *card)
{
	int error, i, pnp = 0;

#ifdef CONFIG_PNP
	pnp = chip->dev != NULL;
#endif	/* CONFIG_PNP */

#ifndef OPTi93X
	if (chip->chip->hardware == OPTi9XX_HW_82C928)
		mpu_port = -1;
#endif	/* OPTi93X */
	error = 0;
	if (!pnp && (mpu_port == SNDRV_DEFAULT_PORT1)) {
		for (i = 0; possible_mpu_ports[i] != -1; i++)
			if (!snd_register_ioport(card, possible_mpu_ports[i], 2,
					DRIVER_NAME" - MPU-401", NULL)) {
				mpu_port = possible_mpu_ports[i];
				break;
			}
		if (mpu_port == SNDRV_DEFAULT_PORT1)
			error = -EBUSY;
	}
	else
		error = (mpu_port == -1) ? -ENODEV :
			snd_register_ioport(card, mpu_port, 2,
			DRIVER_NAME" - MPU-401", NULL);
	if (error)
		chip->chip->mpu_port = -1;
	else if (pnp && (irq == mpu_irq))
		chip->chip->mpu_irq = mpu_irq;
	else if (!snd_register_interrupt(card,
			DRIVER_NAME" - MPU-401",
			mpu_irq, SNDRV_IRQ_TYPE_ISA,
			snd_card_opti9xx_mpu_interrupt, chip,
			pnp ? no_alternatives : possible_mpu_irqs,
			&chip->mpuirqptr)) {
		chip->chip->mpu_port = mpu_port;
		chip->chip->mpu_irq = chip->mpuirqptr->irq;
	}
	else
		chip->chip->mpu_port = -1;

	if (!pnp && (port == SNDRV_DEFAULT_PORT1)) {
		for (i = 0; possible_ports[i] != -1; i++)
			if (!snd_register_ioport(card, possible_ports[i], 8,
					DRIVER_NAME" - WSS", NULL)) {
				port = possible_ports[i];
				break;
			}
		if (port == SNDRV_DEFAULT_PORT1)
			return -EBUSY;
	}
	else if ((error = snd_register_ioport(card, port, 8,
			DRIVER_NAME" - WSS", NULL)) < 0)
		return error;
	chip->chip->wss_base = port;
	if ((error = snd_register_interrupt(card, DRIVER_NAME" - WSS",
			irq, SNDRV_IRQ_TYPE_ISA,
			snd_card_opti9xx_interrupt, chip,
			pnp ? no_alternatives : possible_irqs,
			&chip->irqptr)) < 0)
		return error;
	chip->chip->irq = chip->irqptr->irq;
	if ((error = snd_register_dma_channel(card,
#if defined(CS4231) || defined(OPTi93X)
			DRIVER_NAME" - WSS playback",
#else
			DRIVER_NAME" - WSS",
#endif	/* CS4231 || OPTi93X */
			dma1, SNDRV_DMA_TYPE_ISA, dma1_size,
			pnp ? no_alternatives : possible_dma1s,
			&chip->dma1ptr)) < 0)
		return error;
	chip->chip->dma1 = chip->dma1ptr->dma;
#if defined(CS4231) || defined(OPTi93X)
	if ((error = snd_register_dma_channel(card, DRIVER_NAME" - WSS capture",
			dma2, SNDRV_DMA_TYPE_ISA, dma2_size,
			pnp ? no_alternatives :
				possible_dma2s[chip->dma1ptr->dma],
			&chip->dma2ptr)) < 0)
		return error;
	chip->chip->dma2 = chip->dma2ptr->dma;
#endif	/* CS4231 || OPTi93X */

	if (snd_register_ioport(card,
			pnp ? fm_port : fm_port = 0x388, 4,
			DRIVER_NAME" - OPL", NULL) < 0)
		fm_port = -1;
	chip->chip->fm_port = fm_port;

	return 0;
}
#endif

static void snd_card_opti9xx_free(snd_card_t *card)
{
	opti9xx_t *chip = (opti9xx_t *)card->private_data;
        
	if (chip)
		release_and_free_resource(chip->res_mc_base);
}

static int snd_card_opti9xx_probe(struct pnp_card_link *pcard,
				  const struct pnp_card_device_id *pid)
{
	static long possible_ports[] = {0x530, 0xe80, 0xf40, 0x604, -1};
	static long possible_mpu_ports[] = {0x300, 0x310, 0x320, 0x330, -1};
#ifdef OPTi93X
	static int possible_irqs[] = {5, 9, 10, 11, 7, -1};
#else
	static int possible_irqs[] = {9, 10, 11, 7, -1};
#endif	/* OPTi93X */
	static int possible_mpu_irqs[] = {5, 9, 10, 7, -1};
	static int possible_dma1s[] = {3, 1, 0, -1};
#if defined(CS4231) || defined(OPTi93X)
	static int possible_dma2s[][2] = {{1,-1}, {0,-1}, {-1,-1}, {0,-1}};
#endif	/* CS4231 || OPTi93X */
	int error;
	opti9xx_t *chip;
#if defined(OPTi93X)
	opti93x_t *codec;
#elif defined(CS4231)
	cs4231_t *codec;
	snd_timer_t *timer;
#else
	ad1848_t *codec;
#endif
	snd_card_t *card;
	snd_pcm_t *pcm;
	snd_rawmidi_t *rmidi;
	snd_hwdep_t *synth;
#ifdef CONFIG_PNP
	int hw;
#endif	/* CONFIG_PNP */

	if (pcard && !snd_opti9xx_first_hit)
		return -EBUSY;
	if (!(card = snd_card_new(index, id, THIS_MODULE,
				  sizeof(opti9xx_t))))
		return -ENOMEM;
	card->private_free = snd_card_opti9xx_free;
	chip = (opti9xx_t *)card->private_data;

#ifdef CONFIG_PNP
	if (isapnp && pcard && (hw = snd_card_opti9xx_pnp(chip, pcard, pid)) > 0) {
		switch (hw) {
		case 0x0924:
			hw = OPTi9XX_HW_82C924;
			break;
		case 0x0925:
			hw = OPTi9XX_HW_82C925;
			break;
		case 0x0931:
			hw = OPTi9XX_HW_82C931;
			break;
		default:
			snd_card_free(card);
			return -ENODEV;
		}

		if ((error = snd_opti9xx_init(chip, hw))) {
			snd_card_free(card);
			return error;
		}
		if (hw <= OPTi9XX_HW_82C930)
			chip->mc_base -= 0x80;
		snd_card_set_dev(card, &pcard->card->dev);
	} else {
#endif	/* CONFIG_PNP */
		if ((error = snd_card_opti9xx_detect(card, chip)) < 0) {
			snd_card_free(card);
			return error;
		}
		if ((error = snd_card_set_generic_dev(card)) < 0) {
			snd_card_free(card);
			return error;
		}
#ifdef CONFIG_PNP
	}
#endif	/* CONFIG_PNP */

	if (! chip->res_mc_base &&
	    (chip->res_mc_base = request_region(chip->mc_base, chip->mc_base_size, "OPTi9xx MC")) == NULL) {
		snd_card_free(card);
		return -ENOMEM;
	}

	chip->wss_base = port;
	chip->fm_port = fm_port;
	chip->mpu_port = mpu_port;
	chip->irq = irq;
	chip->mpu_irq = mpu_irq;
	chip->dma1 = dma1;
#if defined(CS4231) || defined(OPTi93X)
	chip->dma2 = dma2;
#endif

	if (chip->wss_base == SNDRV_AUTO_PORT) {
		if ((chip->wss_base = snd_legacy_find_free_ioport(possible_ports, 4)) < 0) {
			snd_card_free(card);
			snd_printk("unable to find a free WSS port\n");
			return -EBUSY;
		}
	}
#ifdef CONFIG_PNP
	if (!isapnp) {
#endif
	if (chip->mpu_port == SNDRV_AUTO_PORT) {
		if ((chip->mpu_port = snd_legacy_find_free_ioport(possible_mpu_ports, 2)) < 0) {
			snd_card_free(card);
			snd_printk("unable to find a free MPU401 port\n");
			return -EBUSY;
		}
	}
	if (chip->irq == SNDRV_AUTO_IRQ) {
		if ((chip->irq = snd_legacy_find_free_irq(possible_irqs)) < 0) {
			snd_card_free(card);
			snd_printk("unable to find a free IRQ\n");
			return -EBUSY;
		}
	}
	if (chip->mpu_irq == SNDRV_AUTO_IRQ) {
		if ((chip->mpu_irq = snd_legacy_find_free_irq(possible_mpu_irqs)) < 0) {
			snd_card_free(card);
			snd_printk("unable to find a free MPU401 IRQ\n");
			return -EBUSY;
		}
	}
	if (chip->dma1 == SNDRV_AUTO_DMA) {
                if ((chip->dma1 = snd_legacy_find_free_dma(possible_dma1s)) < 0) {
                        snd_card_free(card);
			snd_printk("unable to find a free DMA1\n");
			return -EBUSY;
		}
        }
#if defined(CS4231) || defined(OPTi93X)
	if (chip->dma2 == SNDRV_AUTO_DMA) {
                if ((chip->dma2 = snd_legacy_find_free_dma(possible_dma2s[chip->dma1 % 4])) < 0) {
                        snd_card_free(card);
			snd_printk("unable to find a free DMA2\n");
			return -EBUSY;
		}
        }
#endif

#ifdef CONFIG_PNP
	}
#endif

	if ((error = snd_opti9xx_configure(chip))) {
		snd_card_free(card);
		return error;
	}

#if defined(OPTi93X)
	if ((error = snd_opti93x_create(card, chip, chip->dma1, chip->dma2, &codec))) {
		snd_card_free(card);
		return error;
	}
	if ((error = snd_opti93x_pcm(codec, 0, &pcm)) < 0) {
		snd_card_free(card);
		return error;
	}
	if ((error = snd_opti93x_mixer(codec)) < 0) {
		snd_card_free(card);
		return error;
	}
#elif defined(CS4231)
	if ((error = snd_cs4231_create(card, chip->wss_base + 4, -1,
				       chip->irq, chip->dma1, chip->dma2,
				       CS4231_HW_DETECT,
				       0,
				       &codec)) < 0) {
		snd_card_free(card);
		return error;
	}
	if ((error = snd_cs4231_pcm(codec, 0, &pcm)) < 0) {
		snd_card_free(card);
		return error;
	}
	if ((error = snd_cs4231_mixer(codec)) < 0) {
		snd_card_free(card);
		return error;
	}
	if ((error = snd_cs4231_timer(codec, 0, &timer)) < 0) {
		snd_card_free(card);
		return error;
	}
#else
	if ((error = snd_ad1848_create(card, chip->wss_base + 4,
				       chip->irq, chip->dma1,
				       AD1848_HW_DETECT, &codec)) < 0) {
		snd_card_free(card);
		return error;
	}
	if ((error = snd_ad1848_pcm(codec, 0, &pcm)) < 0) {
		snd_card_free(card);
		return error;
	}
	if ((error = snd_ad1848_mixer(codec)) < 0) {
		snd_card_free(card);
		return error;
	}
#endif
	strcpy(card->driver, chip->name);
	sprintf(card->shortname, "OPTi %s", card->driver);
#if defined(CS4231) || defined(OPTi93X)
	sprintf(card->longname, "%s, %s at 0x%lx, irq %d, dma %d&%d",
		card->shortname, pcm->name, chip->wss_base + 4,
		chip->irq, chip->dma1, chip->dma2);
#else
	sprintf(card->longname, "%s, %s at 0x%lx, irq %d, dma %d",
		card->shortname, pcm->name, chip->wss_base + 4,
		chip->irq, chip->dma1);
#endif	/* CS4231 || OPTi93X */

	if (chip->mpu_port <= 0 || chip->mpu_port == SNDRV_AUTO_PORT)
		rmidi = NULL;
	else
		if ((error = snd_mpu401_uart_new(card, 0, MPU401_HW_MPU401,
				chip->mpu_port, 0, chip->mpu_irq, SA_INTERRUPT,
				&rmidi)))
			snd_printk("no MPU-401 device at 0x%lx?\n", chip->mpu_port);

	if (chip->fm_port > 0 && chip->fm_port != SNDRV_AUTO_PORT) {
		opl3_t *opl3 = NULL;
#ifndef OPTi93X
		if (chip->hardware == OPTi9XX_HW_82C928 ||
		    chip->hardware == OPTi9XX_HW_82C929 ||
		    chip->hardware == OPTi9XX_HW_82C924) {
			opl4_t *opl4;
			/* assume we have an OPL4 */
			snd_opti9xx_write_mask(chip, OPTi9XX_MC_REG(2),
					       0x20, 0x20);
			if (snd_opl4_create(card,
					    chip->fm_port,
					    chip->fm_port - 8,
					    2, &opl3, &opl4) < 0) {
				/* no luck, use OPL3 instead */
				snd_opti9xx_write_mask(chip, OPTi9XX_MC_REG(2),
						       0x00, 0x20);
			}
		}
#endif	/* !OPTi93X */
		if (!opl3 && snd_opl3_create(card,
					     chip->fm_port,
					     chip->fm_port + 2,
					     OPL3_HW_AUTO, 0, &opl3) < 0) {
			snd_printk("no OPL device at 0x%lx-0x%lx\n",
				   chip->fm_port, chip->fm_port + 4 - 1);
		}
		if (opl3) {
			if ((error = snd_opl3_timer_new(opl3,
#ifdef CS4231
							1, 2)) < 0) {
#else
							0, 1)) < 0) {
#endif	/* CS4231 */
				snd_card_free(card);
				return error;
			}
			if ((error = snd_opl3_hwdep_new(opl3, 0, 1, &synth)) < 0) {
				snd_card_free(card);
				return error;
			}
		}
	}

	if ((error = snd_card_register(card))) {
		snd_card_free(card);
		return error;
	}
	snd_opti9xx_first_hit = 0;
	if (pcard)
		pnp_set_card_drvdata(pcard, card);
	else
		snd_opti9xx_legacy = card;
	return 0;
}

#ifdef CONFIG_PNP
static void __devexit snd_opti9xx_pnp_remove(struct pnp_card_link * pcard)
{
	snd_card_t *card = (snd_card_t *) pnp_get_card_drvdata(pcard);

	snd_card_disconnect(card);
	snd_card_free_in_thread(card);
	snd_opti9xx_first_hit = 0;
}

static struct pnp_card_driver opti9xx_pnpc_driver = {
	.flags		= PNP_DRIVER_RES_DISABLE,
	.name		= "opti9xx",
	.id_table	= snd_opti9xx_pnpids,
	.probe		= snd_card_opti9xx_probe,
	.remove		= __devexit_p(snd_opti9xx_pnp_remove),
};
#endif

static int __init alsa_card_opti9xx_init(void)
{
	int cards, error;

#ifdef CONFIG_PNP
	cards = pnp_register_card_driver(&opti9xx_pnpc_driver);
#else
	cards = 0;
#endif
	if (cards == 0 && (error = snd_card_opti9xx_probe(NULL, NULL)) < 0) {
#ifdef CONFIG_PNP
		pnp_unregister_card_driver(&opti9xx_pnpc_driver);
#endif
#ifdef MODULE
#ifdef OPTi93X
		printk(KERN_ERR "no OPTi 82C93x soundcard found\n");
#else
		printk(KERN_ERR "no OPTi 82C92x soundcard found\n");
#endif	/* OPTi93X */
#endif
		return error;
	}
	return 0;
}

static void __exit alsa_card_opti9xx_exit(void)
{
#ifdef CONFIG_PNP
	pnp_unregister_card_driver(&opti9xx_pnpc_driver);
#endif
	if (snd_opti9xx_legacy)
		snd_card_free(snd_opti9xx_legacy);
}

module_init(alsa_card_opti9xx_init)
module_exit(alsa_card_opti9xx_exit)
