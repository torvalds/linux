/*
 *  als300.c - driver for Avance Logic ALS300/ALS300+ soundcards.
 *  Copyright (C) 2005 by Ash Willis <ashwillis@programmer.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 *  TODO
 *  4 channel playback for ALS300+
 *  gameport
 *  mpu401
 *  opl3
 *
 *  NOTES
 *  The BLOCK_COUNTER registers for the ALS300(+) return a figure related to
 *  the position in the current period, NOT the whole buffer. It is important
 *  to know which period we are in so we can calculate the correct pointer.
 *  This is why we always use 2 periods. We can then use a flip-flop variable
 *  to keep track of what period we are in.
 */

#include <sound/driver.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/slab.h>

#include <asm/io.h>

#include <sound/core.h>
#include <sound/control.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/ac97_codec.h>
#include <sound/opl3.h>

/* snd_als300_set_irq_flag */
#define IRQ_DISABLE		0
#define IRQ_ENABLE		1

/* I/O port layout */
#define AC97_ACCESS		0x00
#define AC97_READ		0x04
#define AC97_STATUS		0x06
#define   AC97_DATA_AVAIL		(1<<6)
#define   AC97_BUSY			(1<<7)
#define ALS300_IRQ_STATUS	0x07		/* ALS300 Only */
#define   IRQ_PLAYBACK			(1<<3)
#define   IRQ_CAPTURE			(1<<2)
#define GCR_DATA		0x08
#define GCR_INDEX		0x0C
#define ALS300P_DRAM_IRQ_STATUS	0x0D		/* ALS300+ Only */
#define MPU_IRQ_STATUS		0x0E		/* ALS300 Rev. E+, ALS300+ */
#define ALS300P_IRQ_STATUS	0x0F		/* ALS300+ Only */

/* General Control Registers */
#define PLAYBACK_START		0x80
#define PLAYBACK_END		0x81
#define PLAYBACK_CONTROL	0x82
#define   TRANSFER_START		(1<<16)
#define   FIFO_PAUSE			(1<<17)
#define RECORD_START		0x83
#define RECORD_END		0x84
#define RECORD_CONTROL		0x85
#define DRAM_WRITE_CONTROL	0x8B
#define   WRITE_TRANS_START		(1<<16)
#define   DRAM_MODE_2			(1<<17)
#define MISC_CONTROL		0x8C
#define   IRQ_SET_BIT			(1<<15)
#define   VMUTE_NORMAL			(1<<20)
#define   MMUTE_NORMAL			(1<<21)
#define MUS_VOC_VOL		0x8E
#define PLAYBACK_BLOCK_COUNTER	0x9A
#define RECORD_BLOCK_COUNTER	0x9B

#define DEBUG_CALLS	1
#define DEBUG_PLAY_REC	1

#if DEBUG_CALLS
#define snd_als300_dbgcalls(format, args...) printk(format, ##args)
#define snd_als300_dbgcallenter() printk(KERN_ERR "--> %s\n", __FUNCTION__)
#define snd_als300_dbgcallleave() printk(KERN_ERR "<-- %s\n", __FUNCTION__)
#else
#define snd_als300_dbgcalls(format, args...)
#define snd_als300_dbgcallenter()
#define snd_als300_dbgcallleave()
#endif

#if DEBUG_PLAY_REC
#define snd_als300_dbgplay(format, args...) printk(KERN_ERR format, ##args)
#else
#define snd_als300_dbgplay(format, args...)
#endif		

enum {DEVICE_ALS300, DEVICE_ALS300_PLUS};

MODULE_AUTHOR("Ash Willis <ashwillis@programmer.net>");
MODULE_DESCRIPTION("Avance Logic ALS300");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Avance Logic,ALS300},{Avance Logic,ALS300+}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;

struct snd_als300 {
	unsigned long port;
	spinlock_t reg_lock;
	struct snd_card *card;
	struct pci_dev *pci;

	struct snd_pcm *pcm;
	struct snd_pcm_substream *playback_substream;
	struct snd_pcm_substream *capture_substream;

	struct snd_ac97 *ac97;
	struct snd_opl3 *opl3;

	struct resource *res_port;

	int irq;

	int chip_type; /* ALS300 or ALS300+ */

	char revision;	
};

struct snd_als300_substream_data {
	int period_flipflop;
	int control_register;
	int block_counter_register;
};

static struct pci_device_id snd_als300_ids[] = {
	{ 0x4005, 0x0300, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_ALS300 },
	{ 0x4005, 0x0308, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_ALS300_PLUS },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, snd_als300_ids);

static inline u32 snd_als300_gcr_read(unsigned long port, unsigned short reg)
{
	outb(reg, port+GCR_INDEX);
	return inl(port+GCR_DATA);
}

static inline void snd_als300_gcr_write(unsigned long port,
						unsigned short reg, u32 val)
{
	outb(reg, port+GCR_INDEX);
	outl(val, port+GCR_DATA);
}

/* Enable/Disable Interrupts */
static void snd_als300_set_irq_flag(struct snd_als300 *chip, int cmd)
{
	u32 tmp = snd_als300_gcr_read(chip->port, MISC_CONTROL);
	snd_als300_dbgcallenter();

	/* boolean XOR check, since old vs. new hardware have
	   directly reversed bit setting for ENABLE and DISABLE.
	   ALS300+ acts like newer versions of ALS300 */
	if (((chip->revision > 5 || chip->chip_type == DEVICE_ALS300_PLUS) ^
						(cmd == IRQ_ENABLE)) == 0)
		tmp |= IRQ_SET_BIT;
	else
		tmp &= ~IRQ_SET_BIT;
	snd_als300_gcr_write(chip->port, MISC_CONTROL, tmp);
	snd_als300_dbgcallleave();
}

static int snd_als300_free(struct snd_als300 *chip)
{
	snd_als300_dbgcallenter();
	snd_als300_set_irq_flag(chip, IRQ_DISABLE);
	if (chip->irq >= 0)
		free_irq(chip->irq, (void *)chip);
	pci_release_regions(chip->pci);
	pci_disable_device(chip->pci);
	kfree(chip);
	snd_als300_dbgcallleave();
	return 0;
}

static int snd_als300_dev_free(struct snd_device *device)
{
	struct snd_als300 *chip = device->device_data;
	return snd_als300_free(chip);
}

static irqreturn_t snd_als300_interrupt(int irq, void *dev_id)
{
	u8 status;
	struct snd_als300 *chip = dev_id;
	struct snd_als300_substream_data *data;

	status = inb(chip->port+ALS300_IRQ_STATUS);
	if (!status) /* shared IRQ, for different device?? Exit ASAP! */
		return IRQ_NONE;

	/* ACK everything ASAP */
	outb(status, chip->port+ALS300_IRQ_STATUS);
	if (status & IRQ_PLAYBACK) {
		if (chip->pcm && chip->playback_substream) {
			data = chip->playback_substream->runtime->private_data;
			data->period_flipflop ^= 1;
			snd_pcm_period_elapsed(chip->playback_substream);
			snd_als300_dbgplay("IRQ_PLAYBACK\n");
		}
	}
	if (status & IRQ_CAPTURE) {
		if (chip->pcm && chip->capture_substream) {
			data = chip->capture_substream->runtime->private_data;
			data->period_flipflop ^= 1;
			snd_pcm_period_elapsed(chip->capture_substream);
			snd_als300_dbgplay("IRQ_CAPTURE\n");
		}
	}
	return IRQ_HANDLED;
}

static irqreturn_t snd_als300plus_interrupt(int irq, void *dev_id)
{
	u8 general, mpu, dram;
	struct snd_als300 *chip = dev_id;
	struct snd_als300_substream_data *data;
	
	general = inb(chip->port+ALS300P_IRQ_STATUS);
	mpu = inb(chip->port+MPU_IRQ_STATUS);
	dram = inb(chip->port+ALS300P_DRAM_IRQ_STATUS);

	/* shared IRQ, for different device?? Exit ASAP! */
	if ((general == 0) && ((mpu & 0x80) == 0) && ((dram & 0x01) == 0))
		return IRQ_NONE;

	if (general & IRQ_PLAYBACK) {
		if (chip->pcm && chip->playback_substream) {
			outb(IRQ_PLAYBACK, chip->port+ALS300P_IRQ_STATUS);
			data = chip->playback_substream->runtime->private_data;
			data->period_flipflop ^= 1;
			snd_pcm_period_elapsed(chip->playback_substream);
			snd_als300_dbgplay("IRQ_PLAYBACK\n");
		}
	}
	if (general & IRQ_CAPTURE) {
		if (chip->pcm && chip->capture_substream) {
			outb(IRQ_CAPTURE, chip->port+ALS300P_IRQ_STATUS);
			data = chip->capture_substream->runtime->private_data;
			data->period_flipflop ^= 1;
			snd_pcm_period_elapsed(chip->capture_substream);
			snd_als300_dbgplay("IRQ_CAPTURE\n");
		}
	}
	/* FIXME: Ack other interrupt types. Not important right now as
	 * those other devices aren't enabled. */
	return IRQ_HANDLED;
}

static void __devexit snd_als300_remove(struct pci_dev *pci)
{
	snd_als300_dbgcallenter();
	snd_card_free(pci_get_drvdata(pci));
	pci_set_drvdata(pci, NULL);
	snd_als300_dbgcallleave();
}

static unsigned short snd_als300_ac97_read(struct snd_ac97 *ac97,
							unsigned short reg)
{
	int i;
	struct snd_als300 *chip = ac97->private_data;

	for (i = 0; i < 1000; i++) {
		if ((inb(chip->port+AC97_STATUS) & (AC97_BUSY)) == 0)
			break;
		udelay(10);
	}
	outl((reg << 24) | (1 << 31), chip->port+AC97_ACCESS);

	for (i = 0; i < 1000; i++) {
		if ((inb(chip->port+AC97_STATUS) & (AC97_DATA_AVAIL)) != 0)
			break;
		udelay(10);
	}
	return inw(chip->port+AC97_READ);
}

static void snd_als300_ac97_write(struct snd_ac97 *ac97,
				unsigned short reg, unsigned short val)
{
	int i;
	struct snd_als300 *chip = ac97->private_data;

	for (i = 0; i < 1000; i++) {
		if ((inb(chip->port+AC97_STATUS) & (AC97_BUSY)) == 0)
			break;
		udelay(10);
	}
	outl((reg << 24) | val, chip->port+AC97_ACCESS);
}

static int snd_als300_ac97(struct snd_als300 *chip)
{
	struct snd_ac97_bus *bus;
	struct snd_ac97_template ac97;
	int err;
	static struct snd_ac97_bus_ops ops = {
		.write = snd_als300_ac97_write,
		.read = snd_als300_ac97_read,
	};

	snd_als300_dbgcallenter();
	if ((err = snd_ac97_bus(chip->card, 0, &ops, NULL, &bus)) < 0)
		return err;

	memset(&ac97, 0, sizeof(ac97));
	ac97.private_data = chip;

	snd_als300_dbgcallleave();
	return snd_ac97_mixer(bus, &ac97, &chip->ac97);
}

/* hardware definition
 *
 * In AC97 mode, we always use 48k/16bit/stereo.
 * Any request to change data type is ignored by
 * the card when it is running outside of legacy
 * mode.
 */
static struct snd_pcm_hardware snd_als300_playback_hw =
{
	.info =			(SNDRV_PCM_INFO_MMAP |
				SNDRV_PCM_INFO_INTERLEAVED |
				SNDRV_PCM_INFO_PAUSE |
				SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		SNDRV_PCM_FMTBIT_S16,
	.rates =		SNDRV_PCM_RATE_48000,
	.rate_min =		48000,
	.rate_max =		48000,
	.channels_min =		2,
	.channels_max =		2,
	.buffer_bytes_max =	64 * 1024,
	.period_bytes_min =	64,
	.period_bytes_max =	32 * 1024,
	.periods_min =		2,
	.periods_max =		2,
};

static struct snd_pcm_hardware snd_als300_capture_hw =
{
	.info =			(SNDRV_PCM_INFO_MMAP |
				SNDRV_PCM_INFO_INTERLEAVED |
				SNDRV_PCM_INFO_PAUSE |
				SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		SNDRV_PCM_FMTBIT_S16,
	.rates =		SNDRV_PCM_RATE_48000,
	.rate_min =		48000,
	.rate_max =		48000,
	.channels_min =		2,
	.channels_max =		2,
	.buffer_bytes_max =	64 * 1024,
	.period_bytes_min =	64,
	.period_bytes_max =	32 * 1024,
	.periods_min =		2,
	.periods_max =		2,
};

static int snd_als300_playback_open(struct snd_pcm_substream *substream)
{
	struct snd_als300 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_als300_substream_data *data = kzalloc(sizeof(*data),
								GFP_KERNEL);

	snd_als300_dbgcallenter();
	chip->playback_substream = substream;
	runtime->hw = snd_als300_playback_hw;
	runtime->private_data = data;
	data->control_register = PLAYBACK_CONTROL;
	data->block_counter_register = PLAYBACK_BLOCK_COUNTER;
	snd_als300_dbgcallleave();
	return 0;
}

static int snd_als300_playback_close(struct snd_pcm_substream *substream)
{
	struct snd_als300 *chip = snd_pcm_substream_chip(substream);
	struct snd_als300_substream_data *data;

	data = substream->runtime->private_data;
	snd_als300_dbgcallenter();
	kfree(data);
	chip->playback_substream = NULL;
	snd_pcm_lib_free_pages(substream);
	snd_als300_dbgcallleave();
	return 0;
}

static int snd_als300_capture_open(struct snd_pcm_substream *substream)
{
	struct snd_als300 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_als300_substream_data *data = kzalloc(sizeof(*data),
								GFP_KERNEL);

	snd_als300_dbgcallenter();
	chip->capture_substream = substream;
	runtime->hw = snd_als300_capture_hw;
	runtime->private_data = data;
	data->control_register = RECORD_CONTROL;
	data->block_counter_register = RECORD_BLOCK_COUNTER;
	snd_als300_dbgcallleave();
	return 0;
}

static int snd_als300_capture_close(struct snd_pcm_substream *substream)
{
	struct snd_als300 *chip = snd_pcm_substream_chip(substream);
	struct snd_als300_substream_data *data;

	data = substream->runtime->private_data;
	snd_als300_dbgcallenter();
	kfree(data);
	chip->capture_substream = NULL;
	snd_pcm_lib_free_pages(substream);
	snd_als300_dbgcallleave();
	return 0;
}

static int snd_als300_pcm_hw_params(struct snd_pcm_substream *substream,
					snd_pcm_hw_params_t * hw_params)
{
	return snd_pcm_lib_malloc_pages(substream,
					params_buffer_bytes(hw_params));
}

static int snd_als300_pcm_hw_free(struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_pages(substream);
}

static int snd_als300_playback_prepare(struct snd_pcm_substream *substream)
{
	u32 tmp;
	struct snd_als300 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned short period_bytes = snd_pcm_lib_period_bytes(substream);
	unsigned short buffer_bytes = snd_pcm_lib_buffer_bytes(substream);
	
	snd_als300_dbgcallenter();
	spin_lock_irq(&chip->reg_lock);
	tmp = snd_als300_gcr_read(chip->port, PLAYBACK_CONTROL);
	tmp &= ~TRANSFER_START;

	snd_als300_dbgplay("Period bytes: %d Buffer bytes %d\n",
						period_bytes, buffer_bytes);
	
	/* set block size */
	tmp &= 0xffff0000;
	tmp |= period_bytes - 1;
	snd_als300_gcr_write(chip->port, PLAYBACK_CONTROL, tmp);

	/* set dma area */
	snd_als300_gcr_write(chip->port, PLAYBACK_START,
					runtime->dma_addr);
	snd_als300_gcr_write(chip->port, PLAYBACK_END,
					runtime->dma_addr + buffer_bytes - 1);
	spin_unlock_irq(&chip->reg_lock);
	snd_als300_dbgcallleave();
	return 0;
}

static int snd_als300_capture_prepare(struct snd_pcm_substream *substream)
{
	u32 tmp;
	struct snd_als300 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned short period_bytes = snd_pcm_lib_period_bytes(substream);
	unsigned short buffer_bytes = snd_pcm_lib_buffer_bytes(substream);

	snd_als300_dbgcallenter();
	spin_lock_irq(&chip->reg_lock);
	tmp = snd_als300_gcr_read(chip->port, RECORD_CONTROL);
	tmp &= ~TRANSFER_START;

	snd_als300_dbgplay("Period bytes: %d Buffer bytes %d\n", period_bytes,
							buffer_bytes);

	/* set block size */
	tmp &= 0xffff0000;
	tmp |= period_bytes - 1;

	/* set dma area */
	snd_als300_gcr_write(chip->port, RECORD_CONTROL, tmp);
	snd_als300_gcr_write(chip->port, RECORD_START,
					runtime->dma_addr);
	snd_als300_gcr_write(chip->port, RECORD_END,
					runtime->dma_addr + buffer_bytes - 1);
	spin_unlock_irq(&chip->reg_lock);
	snd_als300_dbgcallleave();
	return 0;
}

static int snd_als300_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_als300 *chip = snd_pcm_substream_chip(substream);
	u32 tmp;
	struct snd_als300_substream_data *data;
	unsigned short reg;
	int ret = 0;

	data = substream->runtime->private_data;
	reg = data->control_register;

	snd_als300_dbgcallenter();
	spin_lock(&chip->reg_lock);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		tmp = snd_als300_gcr_read(chip->port, reg);
		data->period_flipflop = 1;
		snd_als300_gcr_write(chip->port, reg, tmp | TRANSFER_START);
		snd_als300_dbgplay("TRIGGER START\n");
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		tmp = snd_als300_gcr_read(chip->port, reg);
		snd_als300_gcr_write(chip->port, reg, tmp & ~TRANSFER_START);
		snd_als300_dbgplay("TRIGGER STOP\n");
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		tmp = snd_als300_gcr_read(chip->port, reg);
		snd_als300_gcr_write(chip->port, reg, tmp | FIFO_PAUSE);
		snd_als300_dbgplay("TRIGGER PAUSE\n");
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		tmp = snd_als300_gcr_read(chip->port, reg);
		snd_als300_gcr_write(chip->port, reg, tmp & ~FIFO_PAUSE);
		snd_als300_dbgplay("TRIGGER RELEASE\n");
		break;
	default:
		snd_als300_dbgplay("TRIGGER INVALID\n");
		ret = -EINVAL;
	}
	spin_unlock(&chip->reg_lock);
	snd_als300_dbgcallleave();
	return ret;
}

static snd_pcm_uframes_t snd_als300_pointer(struct snd_pcm_substream *substream)
{
	u16 current_ptr;
	struct snd_als300 *chip = snd_pcm_substream_chip(substream);
	struct snd_als300_substream_data *data;
	unsigned short period_bytes;

	data = substream->runtime->private_data;
	period_bytes = snd_pcm_lib_period_bytes(substream);
	
	snd_als300_dbgcallenter();
	spin_lock(&chip->reg_lock);
	current_ptr = (u16) snd_als300_gcr_read(chip->port,
					data->block_counter_register) + 4;
	spin_unlock(&chip->reg_lock);
	if (current_ptr > period_bytes)
		current_ptr = 0;
	else
		current_ptr = period_bytes - current_ptr;

	if (data->period_flipflop == 0)
		current_ptr += period_bytes;
	snd_als300_dbgplay("Pointer (bytes): %d\n", current_ptr);
	snd_als300_dbgcallleave();
	return bytes_to_frames(substream->runtime, current_ptr);
}

static struct snd_pcm_ops snd_als300_playback_ops = {
	.open =		snd_als300_playback_open,
	.close =	snd_als300_playback_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_als300_pcm_hw_params,
	.hw_free =	snd_als300_pcm_hw_free,
	.prepare =	snd_als300_playback_prepare,
	.trigger =	snd_als300_trigger,
	.pointer =	snd_als300_pointer,
};

static struct snd_pcm_ops snd_als300_capture_ops = {
	.open =		snd_als300_capture_open,
	.close =	snd_als300_capture_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_als300_pcm_hw_params,
	.hw_free =	snd_als300_pcm_hw_free,
	.prepare =	snd_als300_capture_prepare,
	.trigger =	snd_als300_trigger,
	.pointer =	snd_als300_pointer,
};

static int __devinit snd_als300_new_pcm(struct snd_als300 *chip)
{
	struct snd_pcm *pcm;
	int err;

	snd_als300_dbgcallenter();
	err = snd_pcm_new(chip->card, "ALS300", 0, 1, 1, &pcm);
	if (err < 0)
		return err;
	pcm->private_data = chip;
	strcpy(pcm->name, "ALS300");
	chip->pcm = pcm;

	/* set operators */
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK,
				&snd_als300_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE,
				&snd_als300_capture_ops);

	/* pre-allocation of buffers */
	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
	snd_dma_pci_data(chip->pci), 64*1024, 64*1024);
	snd_als300_dbgcallleave();
	return 0;
}

static void snd_als300_init(struct snd_als300 *chip)
{
	unsigned long flags;
	u32 tmp;
	
	snd_als300_dbgcallenter();
	spin_lock_irqsave(&chip->reg_lock, flags);
	chip->revision = (snd_als300_gcr_read(chip->port, MISC_CONTROL) >> 16)
								& 0x0000000F;
	/* Setup DRAM */
	tmp = snd_als300_gcr_read(chip->port, DRAM_WRITE_CONTROL);
	snd_als300_gcr_write(chip->port, DRAM_WRITE_CONTROL,
						(tmp | DRAM_MODE_2)
						& ~WRITE_TRANS_START);

	/* Enable IRQ output */
	snd_als300_set_irq_flag(chip, IRQ_ENABLE);

	/* Unmute hardware devices so their outputs get routed to
	 * the onboard mixer */
	tmp = snd_als300_gcr_read(chip->port, MISC_CONTROL);
	snd_als300_gcr_write(chip->port, MISC_CONTROL,
			tmp | VMUTE_NORMAL | MMUTE_NORMAL);

	/* Reset volumes */
	snd_als300_gcr_write(chip->port, MUS_VOC_VOL, 0);

	/* Make sure playback transfer is stopped */
	tmp = snd_als300_gcr_read(chip->port, PLAYBACK_CONTROL);
	snd_als300_gcr_write(chip->port, PLAYBACK_CONTROL,
			tmp & ~TRANSFER_START);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	snd_als300_dbgcallleave();
}

static int __devinit snd_als300_create(snd_card_t *card,
				       struct pci_dev *pci, int chip_type,
				       struct snd_als300 **rchip)
{
	struct snd_als300 *chip;
	void *irq_handler;
	int err;

	static snd_device_ops_t ops = {
		.dev_free = snd_als300_dev_free,
	};
	*rchip = NULL;

	snd_als300_dbgcallenter();
	if ((err = pci_enable_device(pci)) < 0)
		return err;

	if (pci_set_dma_mask(pci, DMA_28BIT_MASK) < 0 ||
		pci_set_consistent_dma_mask(pci, DMA_28BIT_MASK) < 0) {
		printk(KERN_ERR "error setting 28bit DMA mask\n");
		pci_disable_device(pci);
		return -ENXIO;
	}
	pci_set_master(pci);

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (chip == NULL) {
		pci_disable_device(pci);
		return -ENOMEM;
	}

	chip->card = card;
	chip->pci = pci;
	chip->irq = -1;
	chip->chip_type = chip_type;
	spin_lock_init(&chip->reg_lock);

	if ((err = pci_request_regions(pci, "ALS300")) < 0) {
		kfree(chip);
		pci_disable_device(pci);
		return err;
	}
	chip->port = pci_resource_start(pci, 0);

	if (chip->chip_type == DEVICE_ALS300_PLUS)
		irq_handler = snd_als300plus_interrupt;
	else
		irq_handler = snd_als300_interrupt;

	if (request_irq(pci->irq, irq_handler, IRQF_DISABLED|IRQF_SHARED,
					card->shortname, (void *)chip)) {
		snd_printk(KERN_ERR "unable to grab IRQ %d\n", pci->irq);
		snd_als300_free(chip);
		return -EBUSY;
	}
	chip->irq = pci->irq;


	snd_als300_init(chip);

	if (snd_als300_ac97(chip) < 0) {
		snd_printk(KERN_WARNING "Could not create ac97\n");
		snd_als300_free(chip);
		return err;
	}

	if ((err = snd_als300_new_pcm(chip)) < 0) {
		snd_printk(KERN_WARNING "Could not create PCM\n");
		snd_als300_free(chip);
		return err;
	}

	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL,
						chip, &ops)) < 0) {
		snd_als300_free(chip);
		return err;
	}

	snd_card_set_dev(card, &pci->dev);

	*rchip = chip;
	snd_als300_dbgcallleave();
	return 0;
}

#ifdef CONFIG_PM
static int snd_als300_suspend(struct pci_dev *pci, pm_message_t state)
{
	struct snd_card *card = pci_get_drvdata(pci);
	struct snd_als300 *chip = card->private_data;

	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
	snd_pcm_suspend_all(chip->pcm);
	snd_ac97_suspend(chip->ac97);

	pci_disable_device(pci);
	pci_save_state(pci);
	pci_set_power_state(pci, pci_choose_state(pci, state));
	return 0;
}

static int snd_als300_resume(struct pci_dev *pci)
{
	struct snd_card *card = pci_get_drvdata(pci);
	struct snd_als300 *chip = card->private_data;

	pci_set_power_state(pci, PCI_D0);
	pci_restore_state(pci);
	if (pci_enable_device(pci) < 0) {
		printk(KERN_ERR "als300: pci_enable_device failed, "
		       "disabling device\n");
		snd_card_disconnect(card);
		return -EIO;
	}
	pci_set_master(pci);

	snd_als300_init(chip);
	snd_ac97_resume(chip->ac97);

	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	return 0;
}
#endif

static int __devinit snd_als300_probe(struct pci_dev *pci,
                             const struct pci_device_id *pci_id)
{
	static int dev;
	struct snd_card *card;
	struct snd_als300 *chip;
	int err, chip_type;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[dev]) {
		dev++;
		return -ENOENT;
	}

	card = snd_card_new(index[dev], id[dev], THIS_MODULE, 0);

	if (card == NULL)
		return -ENOMEM;

	chip_type = pci_id->driver_data;

	if ((err = snd_als300_create(card, pci, chip_type, &chip)) < 0) {
		snd_card_free(card);
		return err;
	}
	card->private_data = chip;

	strcpy(card->driver, "ALS300");
	if (chip->chip_type == DEVICE_ALS300_PLUS)
		/* don't know much about ALS300+ yet
		 * print revision number for now */
		sprintf(card->shortname, "ALS300+ (Rev. %d)", chip->revision);
	else
		sprintf(card->shortname, "ALS300 (Rev. %c)", 'A' +
							chip->revision - 1);
	sprintf(card->longname, "%s at 0x%lx irq %i",
				card->shortname, chip->port, chip->irq);

	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
	pci_set_drvdata(pci, card);
	dev++;
	return 0;
}

static struct pci_driver driver = {
	.name = "ALS300",
	.id_table = snd_als300_ids,
	.probe = snd_als300_probe,
	.remove = __devexit_p(snd_als300_remove),
#ifdef CONFIG_PM
	.suspend = snd_als300_suspend,
	.resume = snd_als300_resume,
#endif
};

static int __init alsa_card_als300_init(void)
{
	return pci_register_driver(&driver);
}

static void __exit alsa_card_als300_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(alsa_card_als300_init)
module_exit(alsa_card_als300_exit)
