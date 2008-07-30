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


#include <linux/init.h>
#include <linux/err.h>
#include <linux/isa.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/pnp.h>
#include <linux/moduleparam.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <sound/core.h>
#if defined(CS4231) || defined(OPTi93X)
#include <sound/cs4231.h>
#else
#include <sound/ad1848.h>
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
#ifdef CONFIG_PNP
static int isapnp = 1;			/* Enable ISA PnP detection */
#endif
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
#ifdef CONFIG_PNP
module_param(isapnp, bool, 0444);
MODULE_PARM_DESC(isapnp, "Enable ISA PnP detection for specified soundcard.");
#endif
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

#define OPTi9XX_HW_82C928	1
#define OPTi9XX_HW_82C929	2
#define OPTi9XX_HW_82C924	3
#define OPTi9XX_HW_82C925	4
#define OPTi9XX_HW_82C930	5
#define OPTi9XX_HW_82C931	6
#define OPTi9XX_HW_82C933	7
#define OPTi9XX_HW_LAST		OPTi9XX_HW_82C933

#define OPTi9XX_MC_REG(n)	n

#ifdef OPTi93X

#define OPTi93X_STATUS			0x02
#define OPTi93X_PORT(chip, r)		((chip)->port + OPTi93X_##r)

#define OPTi93X_IRQ_PLAYBACK		0x04
#define OPTi93X_IRQ_CAPTURE		0x08

#endif /* OPTi93X */

struct snd_opti9xx {
	unsigned short hardware;
	unsigned char password;
	char name[7];

	unsigned long mc_base;
	struct resource *res_mc_base;
	unsigned long mc_base_size;
#ifdef OPTi93X
	unsigned long mc_indir_index;
	struct snd_cs4231 *codec;
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

static int snd_opti9xx_pnp_is_probed;

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
#define DEV_NAME "opti93x"
#else
#define DEV_NAME "opti92x"
#endif

static char * snd_opti9xx_names[] = {
	"unkown",
	"82C928",	"82C929",
	"82C924",	"82C925",
	"82C930",	"82C931",	"82C933"
};


static long __devinit snd_legacy_find_free_ioport(long *port_table, long size)
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

static int __devinit snd_opti9xx_init(struct snd_opti9xx *chip,
				      unsigned short hardware)
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

static unsigned char snd_opti9xx_read(struct snd_opti9xx *chip,
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
	
static void snd_opti9xx_write(struct snd_opti9xx *chip, unsigned char reg,
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


static int __devinit snd_opti9xx_configure(struct snd_opti9xx *chip)
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
	case OPTi9XX_HW_82C931:
	case OPTi9XX_HW_82C933:
		/*
		 * The BTC 1817DW has QS1000 wavetable which is connected
		 * to the serial digital input of the OPTI931.
		 */
		snd_opti9xx_write_mask(chip, OPTi9XX_MC_REG(21), 0x82, 0xff);
		/* 
		 * This bit sets OPTI931 to automaticaly select FM
		 * or digital input signal.
		 */
		snd_opti9xx_write_mask(chip, OPTi9XX_MC_REG(26), 0x01, 0x01);
	case OPTi9XX_HW_82C930: /* FALL THROUGH */
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

static irqreturn_t snd_opti93x_interrupt(int irq, void *dev_id)
{
	struct snd_cs4231 *codec = dev_id;
	struct snd_opti9xx *chip = codec->card->private_data;
	unsigned char status;

	status = snd_opti9xx_read(chip, OPTi9XX_MC_REG(11));
	if ((status & OPTi93X_IRQ_PLAYBACK) && codec->playback_substream)
		snd_pcm_period_elapsed(codec->playback_substream);
	if ((status & OPTi93X_IRQ_CAPTURE) && codec->capture_substream) {
		snd_cs4231_overrange(codec);
		snd_pcm_period_elapsed(codec->capture_substream);
	}
	outb(0x00, OPTi93X_PORT(codec, STATUS));
	return IRQ_HANDLED;
}

#endif /* OPTi93X */

static int __devinit snd_card_opti9xx_detect(struct snd_card *card,
					     struct snd_opti9xx *chip)
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
static int __devinit snd_card_opti9xx_pnp(struct snd_opti9xx *chip,
					  struct pnp_card_link *card,
					  const struct pnp_card_device_id *pid)
{
	struct pnp_dev *pdev;
	int err;

	chip->dev = pnp_request_card_device(card, pid->devs[0].id, NULL);
	if (chip->dev == NULL)
		return -EBUSY;

	chip->devmpu = pnp_request_card_device(card, pid->devs[1].id, NULL);

	pdev = chip->dev;

	err = pnp_activate_dev(pdev);
	if (err < 0) {
		snd_printk(KERN_ERR "AUDIO pnp configure failure: %d\n", err);
		return err;
	}

#ifdef OPTi93X
	port = pnp_port_start(pdev, 0) - 4;
	fm_port = pnp_port_start(pdev, 1) + 8;
#else
	if (pid->driver_data != 0x0924)
		port = pnp_port_start(pdev, 1);
	fm_port = pnp_port_start(pdev, 2) + 8;
#endif	/* OPTi93X */
	irq = pnp_irq(pdev, 0);
	dma1 = pnp_dma(pdev, 0);
#if defined(CS4231) || defined(OPTi93X)
	dma2 = pnp_dma(pdev, 1);
#endif	/* CS4231 || OPTi93X */

	pdev = chip->devmpu;
	if (pdev && mpu_port > 0) {
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
	return pid->driver_data;
}
#endif	/* CONFIG_PNP */

static void snd_card_opti9xx_free(struct snd_card *card)
{
	struct snd_opti9xx *chip = card->private_data;
        
	if (chip) {
#ifdef OPTi93X
		struct snd_cs4231 *codec = chip->codec;
		if (codec && codec->irq > 0) {
			disable_irq(codec->irq);
			free_irq(codec->irq, codec);
		}
#endif
		release_and_free_resource(chip->res_mc_base);
	}
}

static int __devinit snd_opti9xx_probe(struct snd_card *card)
{
	static long possible_ports[] = {0x530, 0xe80, 0xf40, 0x604, -1};
	int error;
	struct snd_opti9xx *chip = card->private_data;
#if defined(CS4231) || defined(OPTi93X)
	struct snd_cs4231 *codec;
#ifdef CS4231
	struct snd_timer *timer;
#endif
#else
	struct snd_ad1848 *codec;
#endif
	struct snd_pcm *pcm;
	struct snd_rawmidi *rmidi;
	struct snd_hwdep *synth;

	if (! chip->res_mc_base &&
	    (chip->res_mc_base = request_region(chip->mc_base, chip->mc_base_size,
						"OPTi9xx MC")) == NULL)
		return -ENOMEM;

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
			snd_printk("unable to find a free WSS port\n");
			return -EBUSY;
		}
	}
	if ((error = snd_opti9xx_configure(chip)))
		return error;

#if defined(CS4231) || defined(OPTi93X)
	if ((error = snd_cs4231_create(card, chip->wss_base + 4, -1,
				       chip->irq, chip->dma1, chip->dma2,
#ifdef CS4231
				       CS4231_HW_DETECT, 0,
#else /* OPTi93x */
				       CS4231_HW_OPTI93X, CS4231_HWSHARE_IRQ,
#endif
				       &codec)) < 0)
		return error;
#ifdef OPTi93X
	chip->codec = codec;
#endif
	if ((error = snd_cs4231_pcm(codec, 0, &pcm)) < 0)
		return error;
	if ((error = snd_cs4231_mixer(codec)) < 0)
		return error;
#ifdef CS4231
	if ((error = snd_cs4231_timer(codec, 0, &timer)) < 0)
		return error;
#else /* OPTI93X */
	error = request_irq(chip->irq, snd_opti93x_interrupt,
			    IRQF_DISABLED, DEV_NAME" - WSS", codec);
	if (error < 0) {
		snd_printk(KERN_ERR "opti9xx: can't grab IRQ %d\n", chip->irq);
		return error;
	}
#endif
#else
	if ((error = snd_ad1848_create(card, chip->wss_base + 4,
				       chip->irq, chip->dma1,
				       AD1848_HW_DETECT, &codec)) < 0)
		return error;
	if ((error = snd_ad1848_pcm(codec, 0, &pcm)) < 0)
		return error;
	if ((error = snd_ad1848_mixer(codec)) < 0)
		return error;
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
				chip->mpu_port, 0, chip->mpu_irq, IRQF_DISABLED,
				&rmidi)))
			snd_printk(KERN_WARNING "no MPU-401 device at 0x%lx?\n",
				   chip->mpu_port);

	if (chip->fm_port > 0 && chip->fm_port != SNDRV_AUTO_PORT) {
		struct snd_opl3 *opl3 = NULL;
#ifndef OPTi93X
		if (chip->hardware == OPTi9XX_HW_82C928 ||
		    chip->hardware == OPTi9XX_HW_82C929 ||
		    chip->hardware == OPTi9XX_HW_82C924) {
			struct snd_opl4 *opl4;
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
			snd_printk(KERN_WARNING "no OPL device at 0x%lx-0x%lx\n",
				   chip->fm_port, chip->fm_port + 4 - 1);
		}
		if (opl3) {
#ifdef CS4231
			const int t1dev = 1;
#else
			const int t1dev = 0;
#endif
			if ((error = snd_opl3_timer_new(opl3, t1dev, t1dev+1)) < 0)
				return error;
			if ((error = snd_opl3_hwdep_new(opl3, 0, 1, &synth)) < 0)
				return error;
		}
	}

	return snd_card_register(card);
}

static struct snd_card *snd_opti9xx_card_new(void)
{
	struct snd_card *card;

	card = snd_card_new(index, id, THIS_MODULE, sizeof(struct snd_opti9xx));
	if (! card)
		return NULL;
	card->private_free = snd_card_opti9xx_free;
	return card;
}

static int __devinit snd_opti9xx_isa_match(struct device *devptr,
					   unsigned int dev)
{
#ifdef CONFIG_PNP
	if (snd_opti9xx_pnp_is_probed)
		return 0;
	if (isapnp)
		return 0;
#endif
	return 1;
}

static int __devinit snd_opti9xx_isa_probe(struct device *devptr,
					   unsigned int dev)
{
	struct snd_card *card;
	int error;
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

	if (mpu_port == SNDRV_AUTO_PORT) {
		if ((mpu_port = snd_legacy_find_free_ioport(possible_mpu_ports, 2)) < 0) {
			snd_printk(KERN_ERR "unable to find a free MPU401 port\n");
			return -EBUSY;
		}
	}
	if (irq == SNDRV_AUTO_IRQ) {
		if ((irq = snd_legacy_find_free_irq(possible_irqs)) < 0) {
			snd_printk(KERN_ERR "unable to find a free IRQ\n");
			return -EBUSY;
		}
	}
	if (mpu_irq == SNDRV_AUTO_IRQ) {
		if ((mpu_irq = snd_legacy_find_free_irq(possible_mpu_irqs)) < 0) {
			snd_printk(KERN_ERR "unable to find a free MPU401 IRQ\n");
			return -EBUSY;
		}
	}
	if (dma1 == SNDRV_AUTO_DMA) {
		if ((dma1 = snd_legacy_find_free_dma(possible_dma1s)) < 0) {
			snd_printk(KERN_ERR "unable to find a free DMA1\n");
			return -EBUSY;
		}
	}
#if defined(CS4231) || defined(OPTi93X)
	if (dma2 == SNDRV_AUTO_DMA) {
		if ((dma2 = snd_legacy_find_free_dma(possible_dma2s[dma1 % 4])) < 0) {
			snd_printk("unable to find a free DMA2\n");
			return -EBUSY;
		}
	}
#endif

	card = snd_opti9xx_card_new();
	if (! card)
		return -ENOMEM;

	if ((error = snd_card_opti9xx_detect(card, card->private_data)) < 0) {
		snd_card_free(card);
		return error;
	}
	snd_card_set_dev(card, devptr);
	if ((error = snd_opti9xx_probe(card)) < 0) {
		snd_card_free(card);
		return error;
	}
	dev_set_drvdata(devptr, card);
	return 0;
}

static int __devexit snd_opti9xx_isa_remove(struct device *devptr,
					    unsigned int dev)
{
	snd_card_free(dev_get_drvdata(devptr));
	dev_set_drvdata(devptr, NULL);
	return 0;
}

static struct isa_driver snd_opti9xx_driver = {
	.match		= snd_opti9xx_isa_match,
	.probe		= snd_opti9xx_isa_probe,
	.remove		= __devexit_p(snd_opti9xx_isa_remove),
	/* FIXME: suspend/resume */
	.driver		= {
		.name	= DEV_NAME
	},
};

#ifdef CONFIG_PNP
static int __devinit snd_opti9xx_pnp_probe(struct pnp_card_link *pcard,
					   const struct pnp_card_device_id *pid)
{
	struct snd_card *card;
	int error, hw;
	struct snd_opti9xx *chip;

	if (snd_opti9xx_pnp_is_probed)
		return -EBUSY;
	if (! isapnp)
		return -ENODEV;
	card = snd_opti9xx_card_new();
	if (! card)
		return -ENOMEM;
	chip = card->private_data;

	hw = snd_card_opti9xx_pnp(chip, pcard, pid);
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
	if ((error = snd_opti9xx_probe(card)) < 0) {
		snd_card_free(card);
		return error;
	}
	pnp_set_card_drvdata(pcard, card);
	snd_opti9xx_pnp_is_probed = 1;
	return 0;
}

static void __devexit snd_opti9xx_pnp_remove(struct pnp_card_link * pcard)
{
	snd_card_free(pnp_get_card_drvdata(pcard));
	pnp_set_card_drvdata(pcard, NULL);
	snd_opti9xx_pnp_is_probed = 0;
}

static struct pnp_card_driver opti9xx_pnpc_driver = {
	.flags		= PNP_DRIVER_RES_DISABLE,
	.name		= "opti9xx",
	.id_table	= snd_opti9xx_pnpids,
	.probe		= snd_opti9xx_pnp_probe,
	.remove		= __devexit_p(snd_opti9xx_pnp_remove),
};
#endif

#ifdef OPTi93X
#define CHIP_NAME	"82C93x"
#else
#define CHIP_NAME	"82C92x"
#endif

static int __init alsa_card_opti9xx_init(void)
{
#ifdef CONFIG_PNP
	pnp_register_card_driver(&opti9xx_pnpc_driver);
	if (snd_opti9xx_pnp_is_probed)
		return 0;
	pnp_unregister_card_driver(&opti9xx_pnpc_driver);
#endif
	return isa_register_driver(&snd_opti9xx_driver, 1);
}

static void __exit alsa_card_opti9xx_exit(void)
{
	if (!snd_opti9xx_pnp_is_probed) {
		isa_unregister_driver(&snd_opti9xx_driver);
		return;
	}
#ifdef CONFIG_PNP
	pnp_unregister_card_driver(&opti9xx_pnpc_driver);
#endif
}

module_init(alsa_card_opti9xx_init)
module_exit(alsa_card_opti9xx_exit)
