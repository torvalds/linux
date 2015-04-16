/*
 *   ALSA driver for VT1724 ICEnsemble ICE1724 / VIA VT1724 (Envy24HT)
 *                   VIA VT1720 (Envy24PT)
 *
 *	Copyright (c) 2000 Jaroslav Kysela <perex@perex.cz>
 *                    2002 James Stafford <jstafford@ampltd.com>
 *                    2003 Takashi Iwai <tiwai@suse.de>
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

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <sound/core.h>
#include <sound/info.h>
#include <sound/rawmidi.h>
#include <sound/initval.h>

#include <sound/asoundef.h>

#include "ice1712.h"
#include "envy24ht.h"

/* lowlevel routines */
#include "amp.h"
#include "revo.h"
#include "aureon.h"
#include "vt1720_mobo.h"
#include "pontis.h"
#include "prodigy192.h"
#include "prodigy_hifi.h"
#include "juli.h"
#include "maya44.h"
#include "phase.h"
#include "wtm.h"
#include "se.h"
#include "quartet.h"
#include "psc724.h"

MODULE_AUTHOR("Jaroslav Kysela <perex@perex.cz>");
MODULE_DESCRIPTION("VIA ICEnsemble ICE1724/1720 (Envy24HT/PT)");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{"
	       REVO_DEVICE_DESC
	       AMP_AUDIO2000_DEVICE_DESC
	       AUREON_DEVICE_DESC
	       VT1720_MOBO_DEVICE_DESC
	       PONTIS_DEVICE_DESC
	       PRODIGY192_DEVICE_DESC
	       PRODIGY_HIFI_DEVICE_DESC
	       JULI_DEVICE_DESC
	       MAYA44_DEVICE_DESC
	       PHASE_DEVICE_DESC
	       WTM_DEVICE_DESC
	       SE_DEVICE_DESC
	       QTET_DEVICE_DESC
		"{VIA,VT1720},"
		"{VIA,VT1724},"
		"{ICEnsemble,Generic ICE1724},"
		"{ICEnsemble,Generic Envy24HT}"
		"{ICEnsemble,Generic Envy24PT}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;		/* Enable this card */
static char *model[SNDRV_CARDS];

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for ICE1724 soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for ICE1724 soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable ICE1724 soundcard.");
module_param_array(model, charp, NULL, 0444);
MODULE_PARM_DESC(model, "Use the given board model.");


/* Both VT1720 and VT1724 have the same PCI IDs */
static const struct pci_device_id snd_vt1724_ids[] = {
	{ PCI_VDEVICE(ICE, PCI_DEVICE_ID_VT1724), 0 },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, snd_vt1724_ids);


static int PRO_RATE_LOCKED;
static int PRO_RATE_RESET = 1;
static unsigned int PRO_RATE_DEFAULT = 44100;

static const char * const ext_clock_names[1] = { "IEC958 In" };

/*
 *  Basic I/O
 */

/*
 *  default rates, default clock routines
 */

/* check whether the clock mode is spdif-in */
static inline int stdclock_is_spdif_master(struct snd_ice1712 *ice)
{
	return (inb(ICEMT1724(ice, RATE)) & VT1724_SPDIF_MASTER) ? 1 : 0;
}

/*
 * locking rate makes sense only for internal clock mode
 */
static inline int is_pro_rate_locked(struct snd_ice1712 *ice)
{
	return (!ice->is_spdif_master(ice)) && PRO_RATE_LOCKED;
}

/*
 * ac97 section
 */

static unsigned char snd_vt1724_ac97_ready(struct snd_ice1712 *ice)
{
	unsigned char old_cmd;
	int tm;
	for (tm = 0; tm < 0x10000; tm++) {
		old_cmd = inb(ICEMT1724(ice, AC97_CMD));
		if (old_cmd & (VT1724_AC97_WRITE | VT1724_AC97_READ))
			continue;
		if (!(old_cmd & VT1724_AC97_READY))
			continue;
		return old_cmd;
	}
	dev_dbg(ice->card->dev, "snd_vt1724_ac97_ready: timeout\n");
	return old_cmd;
}

static int snd_vt1724_ac97_wait_bit(struct snd_ice1712 *ice, unsigned char bit)
{
	int tm;
	for (tm = 0; tm < 0x10000; tm++)
		if ((inb(ICEMT1724(ice, AC97_CMD)) & bit) == 0)
			return 0;
	dev_dbg(ice->card->dev, "snd_vt1724_ac97_wait_bit: timeout\n");
	return -EIO;
}

static void snd_vt1724_ac97_write(struct snd_ac97 *ac97,
				  unsigned short reg,
				  unsigned short val)
{
	struct snd_ice1712 *ice = ac97->private_data;
	unsigned char old_cmd;

	old_cmd = snd_vt1724_ac97_ready(ice);
	old_cmd &= ~VT1724_AC97_ID_MASK;
	old_cmd |= ac97->num;
	outb(reg, ICEMT1724(ice, AC97_INDEX));
	outw(val, ICEMT1724(ice, AC97_DATA));
	outb(old_cmd | VT1724_AC97_WRITE, ICEMT1724(ice, AC97_CMD));
	snd_vt1724_ac97_wait_bit(ice, VT1724_AC97_WRITE);
}

static unsigned short snd_vt1724_ac97_read(struct snd_ac97 *ac97, unsigned short reg)
{
	struct snd_ice1712 *ice = ac97->private_data;
	unsigned char old_cmd;

	old_cmd = snd_vt1724_ac97_ready(ice);
	old_cmd &= ~VT1724_AC97_ID_MASK;
	old_cmd |= ac97->num;
	outb(reg, ICEMT1724(ice, AC97_INDEX));
	outb(old_cmd | VT1724_AC97_READ, ICEMT1724(ice, AC97_CMD));
	if (snd_vt1724_ac97_wait_bit(ice, VT1724_AC97_READ) < 0)
		return ~0;
	return inw(ICEMT1724(ice, AC97_DATA));
}


/*
 * GPIO operations
 */

/* set gpio direction 0 = read, 1 = write */
static void snd_vt1724_set_gpio_dir(struct snd_ice1712 *ice, unsigned int data)
{
	outl(data, ICEREG1724(ice, GPIO_DIRECTION));
	inw(ICEREG1724(ice, GPIO_DIRECTION)); /* dummy read for pci-posting */
}

/* get gpio direction 0 = read, 1 = write */
static unsigned int snd_vt1724_get_gpio_dir(struct snd_ice1712 *ice)
{
	return inl(ICEREG1724(ice, GPIO_DIRECTION));
}

/* set the gpio mask (0 = writable) */
static void snd_vt1724_set_gpio_mask(struct snd_ice1712 *ice, unsigned int data)
{
	outw(data, ICEREG1724(ice, GPIO_WRITE_MASK));
	if (!ice->vt1720) /* VT1720 supports only 16 GPIO bits */
		outb((data >> 16) & 0xff, ICEREG1724(ice, GPIO_WRITE_MASK_22));
	inw(ICEREG1724(ice, GPIO_WRITE_MASK)); /* dummy read for pci-posting */
}

static unsigned int snd_vt1724_get_gpio_mask(struct snd_ice1712 *ice)
{
	unsigned int mask;
	if (!ice->vt1720)
		mask = (unsigned int)inb(ICEREG1724(ice, GPIO_WRITE_MASK_22));
	else
		mask = 0;
	mask = (mask << 16) | inw(ICEREG1724(ice, GPIO_WRITE_MASK));
	return mask;
}

static void snd_vt1724_set_gpio_data(struct snd_ice1712 *ice, unsigned int data)
{
	outw(data, ICEREG1724(ice, GPIO_DATA));
	if (!ice->vt1720)
		outb(data >> 16, ICEREG1724(ice, GPIO_DATA_22));
	inw(ICEREG1724(ice, GPIO_DATA)); /* dummy read for pci-posting */
}

static unsigned int snd_vt1724_get_gpio_data(struct snd_ice1712 *ice)
{
	unsigned int data;
	if (!ice->vt1720)
		data = (unsigned int)inb(ICEREG1724(ice, GPIO_DATA_22));
	else
		data = 0;
	data = (data << 16) | inw(ICEREG1724(ice, GPIO_DATA));
	return data;
}

/*
 * MIDI
 */

static void vt1724_midi_clear_rx(struct snd_ice1712 *ice)
{
	unsigned int count;

	for (count = inb(ICEREG1724(ice, MPU_RXFIFO)); count > 0; --count)
		inb(ICEREG1724(ice, MPU_DATA));
}

static inline struct snd_rawmidi_substream *
get_rawmidi_substream(struct snd_ice1712 *ice, unsigned int stream)
{
	return list_first_entry(&ice->rmidi[0]->streams[stream].substreams,
				struct snd_rawmidi_substream, list);
}

static void enable_midi_irq(struct snd_ice1712 *ice, u8 flag, int enable);

static void vt1724_midi_write(struct snd_ice1712 *ice)
{
	struct snd_rawmidi_substream *s;
	int count, i;
	u8 buffer[32];

	s = get_rawmidi_substream(ice, SNDRV_RAWMIDI_STREAM_OUTPUT);
	count = 31 - inb(ICEREG1724(ice, MPU_TXFIFO));
	if (count > 0) {
		count = snd_rawmidi_transmit(s, buffer, count);
		for (i = 0; i < count; ++i)
			outb(buffer[i], ICEREG1724(ice, MPU_DATA));
	}
	/* mask irq when all bytes have been transmitted.
	 * enabled again in output_trigger when the new data comes in.
	 */
	enable_midi_irq(ice, VT1724_IRQ_MPU_TX,
			!snd_rawmidi_transmit_empty(s));
}

static void vt1724_midi_read(struct snd_ice1712 *ice)
{
	struct snd_rawmidi_substream *s;
	int count, i;
	u8 buffer[32];

	s = get_rawmidi_substream(ice, SNDRV_RAWMIDI_STREAM_INPUT);
	count = inb(ICEREG1724(ice, MPU_RXFIFO));
	if (count > 0) {
		count = min(count, 32);
		for (i = 0; i < count; ++i)
			buffer[i] = inb(ICEREG1724(ice, MPU_DATA));
		snd_rawmidi_receive(s, buffer, count);
	}
}

/* call with ice->reg_lock */
static void enable_midi_irq(struct snd_ice1712 *ice, u8 flag, int enable)
{
	u8 mask = inb(ICEREG1724(ice, IRQMASK));
	if (enable)
		mask &= ~flag;
	else
		mask |= flag;
	outb(mask, ICEREG1724(ice, IRQMASK));
}

static void vt1724_enable_midi_irq(struct snd_rawmidi_substream *substream,
				   u8 flag, int enable)
{
	struct snd_ice1712 *ice = substream->rmidi->private_data;

	spin_lock_irq(&ice->reg_lock);
	enable_midi_irq(ice, flag, enable);
	spin_unlock_irq(&ice->reg_lock);
}

static int vt1724_midi_output_open(struct snd_rawmidi_substream *s)
{
	return 0;
}

static int vt1724_midi_output_close(struct snd_rawmidi_substream *s)
{
	return 0;
}

static void vt1724_midi_output_trigger(struct snd_rawmidi_substream *s, int up)
{
	struct snd_ice1712 *ice = s->rmidi->private_data;
	unsigned long flags;

	spin_lock_irqsave(&ice->reg_lock, flags);
	if (up) {
		ice->midi_output = 1;
		vt1724_midi_write(ice);
	} else {
		ice->midi_output = 0;
		enable_midi_irq(ice, VT1724_IRQ_MPU_TX, 0);
	}
	spin_unlock_irqrestore(&ice->reg_lock, flags);
}

static void vt1724_midi_output_drain(struct snd_rawmidi_substream *s)
{
	struct snd_ice1712 *ice = s->rmidi->private_data;
	unsigned long timeout;

	vt1724_enable_midi_irq(s, VT1724_IRQ_MPU_TX, 0);
	/* 32 bytes should be transmitted in less than about 12 ms */
	timeout = jiffies + msecs_to_jiffies(15);
	do {
		if (inb(ICEREG1724(ice, MPU_CTRL)) & VT1724_MPU_TX_EMPTY)
			break;
		schedule_timeout_uninterruptible(1);
	} while (time_after(timeout, jiffies));
}

static struct snd_rawmidi_ops vt1724_midi_output_ops = {
	.open = vt1724_midi_output_open,
	.close = vt1724_midi_output_close,
	.trigger = vt1724_midi_output_trigger,
	.drain = vt1724_midi_output_drain,
};

static int vt1724_midi_input_open(struct snd_rawmidi_substream *s)
{
	vt1724_midi_clear_rx(s->rmidi->private_data);
	vt1724_enable_midi_irq(s, VT1724_IRQ_MPU_RX, 1);
	return 0;
}

static int vt1724_midi_input_close(struct snd_rawmidi_substream *s)
{
	vt1724_enable_midi_irq(s, VT1724_IRQ_MPU_RX, 0);
	return 0;
}

static void vt1724_midi_input_trigger(struct snd_rawmidi_substream *s, int up)
{
	struct snd_ice1712 *ice = s->rmidi->private_data;
	unsigned long flags;

	spin_lock_irqsave(&ice->reg_lock, flags);
	if (up) {
		ice->midi_input = 1;
		vt1724_midi_read(ice);
	} else {
		ice->midi_input = 0;
	}
	spin_unlock_irqrestore(&ice->reg_lock, flags);
}

static struct snd_rawmidi_ops vt1724_midi_input_ops = {
	.open = vt1724_midi_input_open,
	.close = vt1724_midi_input_close,
	.trigger = vt1724_midi_input_trigger,
};


/*
 *  Interrupt handler
 */

static irqreturn_t snd_vt1724_interrupt(int irq, void *dev_id)
{
	struct snd_ice1712 *ice = dev_id;
	unsigned char status;
	unsigned char status_mask =
		VT1724_IRQ_MPU_RX | VT1724_IRQ_MPU_TX | VT1724_IRQ_MTPCM;
	int handled = 0;
	int timeout = 0;

	while (1) {
		status = inb(ICEREG1724(ice, IRQSTAT));
		status &= status_mask;
		if (status == 0)
			break;
		spin_lock(&ice->reg_lock);
		if (++timeout > 10) {
			status = inb(ICEREG1724(ice, IRQSTAT));
			dev_err(ice->card->dev,
				"Too long irq loop, status = 0x%x\n", status);
			if (status & VT1724_IRQ_MPU_TX) {
				dev_err(ice->card->dev, "Disabling MPU_TX\n");
				enable_midi_irq(ice, VT1724_IRQ_MPU_TX, 0);
			}
			spin_unlock(&ice->reg_lock);
			break;
		}
		handled = 1;
		if (status & VT1724_IRQ_MPU_TX) {
			if (ice->midi_output)
				vt1724_midi_write(ice);
			else
				enable_midi_irq(ice, VT1724_IRQ_MPU_TX, 0);
			/* Due to mysterical reasons, MPU_TX is always
			 * generated (and can't be cleared) when a PCM
			 * playback is going.  So let's ignore at the
			 * next loop.
			 */
			status_mask &= ~VT1724_IRQ_MPU_TX;
		}
		if (status & VT1724_IRQ_MPU_RX) {
			if (ice->midi_input)
				vt1724_midi_read(ice);
			else
				vt1724_midi_clear_rx(ice);
		}
		/* ack MPU irq */
		outb(status, ICEREG1724(ice, IRQSTAT));
		spin_unlock(&ice->reg_lock);
		if (status & VT1724_IRQ_MTPCM) {
			/*
			 * Multi-track PCM
			 * PCM assignment are:
			 * Playback DMA0 (M/C) = playback_pro_substream
			 * Playback DMA1 = playback_con_substream_ds[0]
			 * Playback DMA2 = playback_con_substream_ds[1]
			 * Playback DMA3 = playback_con_substream_ds[2]
			 * Playback DMA4 (SPDIF) = playback_con_substream
			 * Record DMA0 = capture_pro_substream
			 * Record DMA1 = capture_con_substream
			 */
			unsigned char mtstat = inb(ICEMT1724(ice, IRQ));
			if (mtstat & VT1724_MULTI_PDMA0) {
				if (ice->playback_pro_substream)
					snd_pcm_period_elapsed(ice->playback_pro_substream);
			}
			if (mtstat & VT1724_MULTI_RDMA0) {
				if (ice->capture_pro_substream)
					snd_pcm_period_elapsed(ice->capture_pro_substream);
			}
			if (mtstat & VT1724_MULTI_PDMA1) {
				if (ice->playback_con_substream_ds[0])
					snd_pcm_period_elapsed(ice->playback_con_substream_ds[0]);
			}
			if (mtstat & VT1724_MULTI_PDMA2) {
				if (ice->playback_con_substream_ds[1])
					snd_pcm_period_elapsed(ice->playback_con_substream_ds[1]);
			}
			if (mtstat & VT1724_MULTI_PDMA3) {
				if (ice->playback_con_substream_ds[2])
					snd_pcm_period_elapsed(ice->playback_con_substream_ds[2]);
			}
			if (mtstat & VT1724_MULTI_PDMA4) {
				if (ice->playback_con_substream)
					snd_pcm_period_elapsed(ice->playback_con_substream);
			}
			if (mtstat & VT1724_MULTI_RDMA1) {
				if (ice->capture_con_substream)
					snd_pcm_period_elapsed(ice->capture_con_substream);
			}
			/* ack anyway to avoid freeze */
			outb(mtstat, ICEMT1724(ice, IRQ));
			/* ought to really handle this properly */
			if (mtstat & VT1724_MULTI_FIFO_ERR) {
				unsigned char fstat = inb(ICEMT1724(ice, DMA_FIFO_ERR));
				outb(fstat, ICEMT1724(ice, DMA_FIFO_ERR));
				outb(VT1724_MULTI_FIFO_ERR | inb(ICEMT1724(ice, DMA_INT_MASK)), ICEMT1724(ice, DMA_INT_MASK));
				/* If I don't do this, I get machine lockup due to continual interrupts */
			}

		}
	}
	return IRQ_RETVAL(handled);
}

/*
 *  PCM code - professional part (multitrack)
 */

static unsigned int rates[] = {
	8000, 9600, 11025, 12000, 16000, 22050, 24000,
	32000, 44100, 48000, 64000, 88200, 96000,
	176400, 192000,
};

static struct snd_pcm_hw_constraint_list hw_constraints_rates_96 = {
	.count = ARRAY_SIZE(rates) - 2, /* up to 96000 */
	.list = rates,
	.mask = 0,
};

static struct snd_pcm_hw_constraint_list hw_constraints_rates_48 = {
	.count = ARRAY_SIZE(rates) - 5, /* up to 48000 */
	.list = rates,
	.mask = 0,
};

static struct snd_pcm_hw_constraint_list hw_constraints_rates_192 = {
	.count = ARRAY_SIZE(rates),
	.list = rates,
	.mask = 0,
};

struct vt1724_pcm_reg {
	unsigned int addr;	/* ADDR register offset */
	unsigned int size;	/* SIZE register offset */
	unsigned int count;	/* COUNT register offset */
	unsigned int start;	/* start & pause bit */
};

static int snd_vt1724_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);
	unsigned char what;
	unsigned char old;
	struct snd_pcm_substream *s;

	what = 0;
	snd_pcm_group_for_each_entry(s, substream) {
		if (snd_pcm_substream_chip(s) == ice) {
			const struct vt1724_pcm_reg *reg;
			reg = s->runtime->private_data;
			what |= reg->start;
			snd_pcm_trigger_done(s, substream);
		}
	}

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		spin_lock(&ice->reg_lock);
		old = inb(ICEMT1724(ice, DMA_PAUSE));
		if (cmd == SNDRV_PCM_TRIGGER_PAUSE_PUSH)
			old |= what;
		else
			old &= ~what;
		outb(old, ICEMT1724(ice, DMA_PAUSE));
		spin_unlock(&ice->reg_lock);
		break;

	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		spin_lock(&ice->reg_lock);
		old = inb(ICEMT1724(ice, DMA_CONTROL));
		if (cmd == SNDRV_PCM_TRIGGER_START)
			old |= what;
		else
			old &= ~what;
		outb(old, ICEMT1724(ice, DMA_CONTROL));
		spin_unlock(&ice->reg_lock);
		break;

	case SNDRV_PCM_TRIGGER_RESUME:
		/* apps will have to restart stream */
		break;

	default:
		return -EINVAL;
	}
	return 0;
}

/*
 */

#define DMA_STARTS	(VT1724_RDMA0_START|VT1724_PDMA0_START|VT1724_RDMA1_START|\
	VT1724_PDMA1_START|VT1724_PDMA2_START|VT1724_PDMA3_START|VT1724_PDMA4_START)
#define DMA_PAUSES	(VT1724_RDMA0_PAUSE|VT1724_PDMA0_PAUSE|VT1724_RDMA1_PAUSE|\
	VT1724_PDMA1_PAUSE|VT1724_PDMA2_PAUSE|VT1724_PDMA3_PAUSE|VT1724_PDMA4_PAUSE)

static const unsigned int stdclock_rate_list[16] = {
	48000, 24000, 12000, 9600, 32000, 16000, 8000, 96000, 44100,
	22050, 11025, 88200, 176400, 0, 192000, 64000
};

static unsigned int stdclock_get_rate(struct snd_ice1712 *ice)
{
	unsigned int rate;
	rate = stdclock_rate_list[inb(ICEMT1724(ice, RATE)) & 15];
	return rate;
}

static void stdclock_set_rate(struct snd_ice1712 *ice, unsigned int rate)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(stdclock_rate_list); i++) {
		if (stdclock_rate_list[i] == rate) {
			outb(i, ICEMT1724(ice, RATE));
			return;
		}
	}
}

static unsigned char stdclock_set_mclk(struct snd_ice1712 *ice,
				       unsigned int rate)
{
	unsigned char val, old;
	/* check MT02 */
	if (ice->eeprom.data[ICE_EEP2_ACLINK] & VT1724_CFG_PRO_I2S) {
		val = old = inb(ICEMT1724(ice, I2S_FORMAT));
		if (rate > 96000)
			val |= VT1724_MT_I2S_MCLK_128X; /* 128x MCLK */
		else
			val &= ~VT1724_MT_I2S_MCLK_128X; /* 256x MCLK */
		if (val != old) {
			outb(val, ICEMT1724(ice, I2S_FORMAT));
			/* master clock changed */
			return 1;
		}
	}
	/* no change in master clock */
	return 0;
}

static int snd_vt1724_set_pro_rate(struct snd_ice1712 *ice, unsigned int rate,
				    int force)
{
	unsigned long flags;
	unsigned char mclk_change;
	unsigned int i, old_rate;

	if (rate > ice->hw_rates->list[ice->hw_rates->count - 1])
		return -EINVAL;

	spin_lock_irqsave(&ice->reg_lock, flags);
	if ((inb(ICEMT1724(ice, DMA_CONTROL)) & DMA_STARTS) ||
	    (inb(ICEMT1724(ice, DMA_PAUSE)) & DMA_PAUSES)) {
		/* running? we cannot change the rate now... */
		spin_unlock_irqrestore(&ice->reg_lock, flags);
		return ((rate == ice->cur_rate) && !force) ? 0 : -EBUSY;
	}
	if (!force && is_pro_rate_locked(ice)) {
		/* comparing required and current rate - makes sense for
		 * internal clock only */
		spin_unlock_irqrestore(&ice->reg_lock, flags);
		return (rate == ice->cur_rate) ? 0 : -EBUSY;
	}

	if (force || !ice->is_spdif_master(ice)) {
		/* force means the rate was switched by ucontrol, otherwise
		 * setting clock rate for internal clock mode */
		old_rate = ice->get_rate(ice);
		if (force || (old_rate != rate))
			ice->set_rate(ice, rate);
		else if (rate == ice->cur_rate) {
			spin_unlock_irqrestore(&ice->reg_lock, flags);
			return 0;
		}
	}

	ice->cur_rate = rate;

	/* setting master clock */
	mclk_change = ice->set_mclk(ice, rate);

	spin_unlock_irqrestore(&ice->reg_lock, flags);

	if (mclk_change && ice->gpio.i2s_mclk_changed)
		ice->gpio.i2s_mclk_changed(ice);
	if (ice->gpio.set_pro_rate)
		ice->gpio.set_pro_rate(ice, rate);

	/* set up codecs */
	for (i = 0; i < ice->akm_codecs; i++) {
		if (ice->akm[i].ops.set_rate_val)
			ice->akm[i].ops.set_rate_val(&ice->akm[i], rate);
	}
	if (ice->spdif.ops.setup_rate)
		ice->spdif.ops.setup_rate(ice, rate);

	return 0;
}

static int snd_vt1724_pcm_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *hw_params)
{
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);
	int i, chs, err;

	chs = params_channels(hw_params);
	mutex_lock(&ice->open_mutex);
	/* mark surround channels */
	if (substream == ice->playback_pro_substream) {
		/* PDMA0 can be multi-channel up to 8 */
		chs = chs / 2 - 1;
		for (i = 0; i < chs; i++) {
			if (ice->pcm_reserved[i] &&
			    ice->pcm_reserved[i] != substream) {
				mutex_unlock(&ice->open_mutex);
				return -EBUSY;
			}
			ice->pcm_reserved[i] = substream;
		}
		for (; i < 3; i++) {
			if (ice->pcm_reserved[i] == substream)
				ice->pcm_reserved[i] = NULL;
		}
	} else {
		for (i = 0; i < 3; i++) {
			/* check individual playback stream */
			if (ice->playback_con_substream_ds[i] == substream) {
				if (ice->pcm_reserved[i] &&
				    ice->pcm_reserved[i] != substream) {
					mutex_unlock(&ice->open_mutex);
					return -EBUSY;
				}
				ice->pcm_reserved[i] = substream;
				break;
			}
		}
	}
	mutex_unlock(&ice->open_mutex);

	err = snd_vt1724_set_pro_rate(ice, params_rate(hw_params), 0);
	if (err < 0)
		return err;

	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
}

static int snd_vt1724_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);
	int i;

	mutex_lock(&ice->open_mutex);
	/* unmark surround channels */
	for (i = 0; i < 3; i++)
		if (ice->pcm_reserved[i] == substream)
			ice->pcm_reserved[i] = NULL;
	mutex_unlock(&ice->open_mutex);
	return snd_pcm_lib_free_pages(substream);
}

static int snd_vt1724_playback_pro_prepare(struct snd_pcm_substream *substream)
{
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);
	unsigned char val;
	unsigned int size;

	spin_lock_irq(&ice->reg_lock);
	val = (8 - substream->runtime->channels) >> 1;
	outb(val, ICEMT1724(ice, BURST));

	outl(substream->runtime->dma_addr, ICEMT1724(ice, PLAYBACK_ADDR));

	size = (snd_pcm_lib_buffer_bytes(substream) >> 2) - 1;
	/* outl(size, ICEMT1724(ice, PLAYBACK_SIZE)); */
	outw(size, ICEMT1724(ice, PLAYBACK_SIZE));
	outb(size >> 16, ICEMT1724(ice, PLAYBACK_SIZE) + 2);
	size = (snd_pcm_lib_period_bytes(substream) >> 2) - 1;
	/* outl(size, ICEMT1724(ice, PLAYBACK_COUNT)); */
	outw(size, ICEMT1724(ice, PLAYBACK_COUNT));
	outb(size >> 16, ICEMT1724(ice, PLAYBACK_COUNT) + 2);

	spin_unlock_irq(&ice->reg_lock);

	/*
	dev_dbg(ice->card->dev, "pro prepare: ch = %d, addr = 0x%x, "
	       "buffer = 0x%x, period = 0x%x\n",
	       substream->runtime->channels,
	       (unsigned int)substream->runtime->dma_addr,
	       snd_pcm_lib_buffer_bytes(substream),
	       snd_pcm_lib_period_bytes(substream));
	*/
	return 0;
}

static snd_pcm_uframes_t snd_vt1724_playback_pro_pointer(struct snd_pcm_substream *substream)
{
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);
	size_t ptr;

	if (!(inl(ICEMT1724(ice, DMA_CONTROL)) & VT1724_PDMA0_START))
		return 0;
#if 0 /* read PLAYBACK_ADDR */
	ptr = inl(ICEMT1724(ice, PLAYBACK_ADDR));
	if (ptr < substream->runtime->dma_addr) {
		dev_dbg(ice->card->dev, "invalid negative ptr\n");
		return 0;
	}
	ptr -= substream->runtime->dma_addr;
	ptr = bytes_to_frames(substream->runtime, ptr);
	if (ptr >= substream->runtime->buffer_size) {
		dev_dbg(ice->card->dev, "invalid ptr %d (size=%d)\n",
			   (int)ptr, (int)substream->runtime->period_size);
		return 0;
	}
#else /* read PLAYBACK_SIZE */
	ptr = inl(ICEMT1724(ice, PLAYBACK_SIZE)) & 0xffffff;
	ptr = (ptr + 1) << 2;
	ptr = bytes_to_frames(substream->runtime, ptr);
	if (!ptr)
		;
	else if (ptr <= substream->runtime->buffer_size)
		ptr = substream->runtime->buffer_size - ptr;
	else {
		dev_dbg(ice->card->dev, "invalid ptr %d (size=%d)\n",
			   (int)ptr, (int)substream->runtime->buffer_size);
		ptr = 0;
	}
#endif
	return ptr;
}

static int snd_vt1724_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);
	const struct vt1724_pcm_reg *reg = substream->runtime->private_data;

	spin_lock_irq(&ice->reg_lock);
	outl(substream->runtime->dma_addr, ice->profi_port + reg->addr);
	outw((snd_pcm_lib_buffer_bytes(substream) >> 2) - 1,
	     ice->profi_port + reg->size);
	outw((snd_pcm_lib_period_bytes(substream) >> 2) - 1,
	     ice->profi_port + reg->count);
	spin_unlock_irq(&ice->reg_lock);
	return 0;
}

static snd_pcm_uframes_t snd_vt1724_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);
	const struct vt1724_pcm_reg *reg = substream->runtime->private_data;
	size_t ptr;

	if (!(inl(ICEMT1724(ice, DMA_CONTROL)) & reg->start))
		return 0;
#if 0 /* use ADDR register */
	ptr = inl(ice->profi_port + reg->addr);
	ptr -= substream->runtime->dma_addr;
	return bytes_to_frames(substream->runtime, ptr);
#else /* use SIZE register */
	ptr = inw(ice->profi_port + reg->size);
	ptr = (ptr + 1) << 2;
	ptr = bytes_to_frames(substream->runtime, ptr);
	if (!ptr)
		;
	else if (ptr <= substream->runtime->buffer_size)
		ptr = substream->runtime->buffer_size - ptr;
	else {
		dev_dbg(ice->card->dev, "invalid ptr %d (size=%d)\n",
			   (int)ptr, (int)substream->runtime->buffer_size);
		ptr = 0;
	}
	return ptr;
#endif
}

static const struct vt1724_pcm_reg vt1724_pdma0_reg = {
	.addr = VT1724_MT_PLAYBACK_ADDR,
	.size = VT1724_MT_PLAYBACK_SIZE,
	.count = VT1724_MT_PLAYBACK_COUNT,
	.start = VT1724_PDMA0_START,
};

static const struct vt1724_pcm_reg vt1724_pdma4_reg = {
	.addr = VT1724_MT_PDMA4_ADDR,
	.size = VT1724_MT_PDMA4_SIZE,
	.count = VT1724_MT_PDMA4_COUNT,
	.start = VT1724_PDMA4_START,
};

static const struct vt1724_pcm_reg vt1724_rdma0_reg = {
	.addr = VT1724_MT_CAPTURE_ADDR,
	.size = VT1724_MT_CAPTURE_SIZE,
	.count = VT1724_MT_CAPTURE_COUNT,
	.start = VT1724_RDMA0_START,
};

static const struct vt1724_pcm_reg vt1724_rdma1_reg = {
	.addr = VT1724_MT_RDMA1_ADDR,
	.size = VT1724_MT_RDMA1_SIZE,
	.count = VT1724_MT_RDMA1_COUNT,
	.start = VT1724_RDMA1_START,
};

#define vt1724_playback_pro_reg vt1724_pdma0_reg
#define vt1724_playback_spdif_reg vt1724_pdma4_reg
#define vt1724_capture_pro_reg vt1724_rdma0_reg
#define vt1724_capture_spdif_reg vt1724_rdma1_reg

static const struct snd_pcm_hardware snd_vt1724_playback_pro = {
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_SYNC_START),
	.formats =		SNDRV_PCM_FMTBIT_S32_LE,
	.rates =		SNDRV_PCM_RATE_KNOT | SNDRV_PCM_RATE_8000_192000,
	.rate_min =		8000,
	.rate_max =		192000,
	.channels_min =		2,
	.channels_max =		8,
	.buffer_bytes_max =	(1UL << 21),	/* 19bits dword */
	.period_bytes_min =	8 * 4 * 2,	/* FIXME: constraints needed */
	.period_bytes_max =	(1UL << 21),
	.periods_min =		2,
	.periods_max =		1024,
};

static const struct snd_pcm_hardware snd_vt1724_spdif = {
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_SYNC_START),
	.formats =		SNDRV_PCM_FMTBIT_S32_LE,
	.rates =	        (SNDRV_PCM_RATE_32000|SNDRV_PCM_RATE_44100|
				 SNDRV_PCM_RATE_48000|SNDRV_PCM_RATE_88200|
				 SNDRV_PCM_RATE_96000|SNDRV_PCM_RATE_176400|
				 SNDRV_PCM_RATE_192000),
	.rate_min =		32000,
	.rate_max =		192000,
	.channels_min =		2,
	.channels_max =		2,
	.buffer_bytes_max =	(1UL << 18),	/* 16bits dword */
	.period_bytes_min =	2 * 4 * 2,
	.period_bytes_max =	(1UL << 18),
	.periods_min =		2,
	.periods_max =		1024,
};

static const struct snd_pcm_hardware snd_vt1724_2ch_stereo = {
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_SYNC_START),
	.formats =		SNDRV_PCM_FMTBIT_S32_LE,
	.rates =		SNDRV_PCM_RATE_KNOT | SNDRV_PCM_RATE_8000_192000,
	.rate_min =		8000,
	.rate_max =		192000,
	.channels_min =		2,
	.channels_max =		2,
	.buffer_bytes_max =	(1UL << 18),	/* 16bits dword */
	.period_bytes_min =	2 * 4 * 2,
	.period_bytes_max =	(1UL << 18),
	.periods_min =		2,
	.periods_max =		1024,
};

/*
 * set rate constraints
 */
static void set_std_hw_rates(struct snd_ice1712 *ice)
{
	if (ice->eeprom.data[ICE_EEP2_ACLINK] & VT1724_CFG_PRO_I2S) {
		/* I2S */
		/* VT1720 doesn't support more than 96kHz */
		if ((ice->eeprom.data[ICE_EEP2_I2S] & 0x08) && !ice->vt1720)
			ice->hw_rates = &hw_constraints_rates_192;
		else
			ice->hw_rates = &hw_constraints_rates_96;
	} else {
		/* ACLINK */
		ice->hw_rates = &hw_constraints_rates_48;
	}
}

static int set_rate_constraints(struct snd_ice1712 *ice,
				struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	runtime->hw.rate_min = ice->hw_rates->list[0];
	runtime->hw.rate_max = ice->hw_rates->list[ice->hw_rates->count - 1];
	runtime->hw.rates = SNDRV_PCM_RATE_KNOT;
	return snd_pcm_hw_constraint_list(runtime, 0,
					  SNDRV_PCM_HW_PARAM_RATE,
					  ice->hw_rates);
}

/* if the card has the internal rate locked (is_pro_locked), limit runtime
   hw rates to the current internal rate only.
*/
static void constrain_rate_if_locked(struct snd_pcm_substream *substream)
{
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned int rate;
	if (is_pro_rate_locked(ice)) {
		rate = ice->get_rate(ice);
		if (rate >= runtime->hw.rate_min
		    && rate <= runtime->hw.rate_max) {
			runtime->hw.rate_min = rate;
			runtime->hw.rate_max = rate;
		}
	}
}


/* multi-channel playback needs alignment 8x32bit regardless of the channels
 * actually used
 */
#define VT1724_BUFFER_ALIGN	0x20

static int snd_vt1724_playback_pro_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);
	int chs, num_indeps;

	runtime->private_data = (void *)&vt1724_playback_pro_reg;
	ice->playback_pro_substream = substream;
	runtime->hw = snd_vt1724_playback_pro;
	snd_pcm_set_sync(substream);
	snd_pcm_hw_constraint_msbits(runtime, 0, 32, 24);
	set_rate_constraints(ice, substream);
	mutex_lock(&ice->open_mutex);
	/* calculate the currently available channels */
	num_indeps = ice->num_total_dacs / 2 - 1;
	for (chs = 0; chs < num_indeps; chs++) {
		if (ice->pcm_reserved[chs])
			break;
	}
	chs = (chs + 1) * 2;
	runtime->hw.channels_max = chs;
	if (chs > 2) /* channels must be even */
		snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS, 2);
	mutex_unlock(&ice->open_mutex);
	snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES,
				   VT1724_BUFFER_ALIGN);
	snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
				   VT1724_BUFFER_ALIGN);
	constrain_rate_if_locked(substream);
	if (ice->pro_open)
		ice->pro_open(ice, substream);
	return 0;
}

static int snd_vt1724_capture_pro_open(struct snd_pcm_substream *substream)
{
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	runtime->private_data = (void *)&vt1724_capture_pro_reg;
	ice->capture_pro_substream = substream;
	runtime->hw = snd_vt1724_2ch_stereo;
	snd_pcm_set_sync(substream);
	snd_pcm_hw_constraint_msbits(runtime, 0, 32, 24);
	set_rate_constraints(ice, substream);
	snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES,
				   VT1724_BUFFER_ALIGN);
	snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
				   VT1724_BUFFER_ALIGN);
	constrain_rate_if_locked(substream);
	if (ice->pro_open)
		ice->pro_open(ice, substream);
	return 0;
}

static int snd_vt1724_playback_pro_close(struct snd_pcm_substream *substream)
{
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);

	if (PRO_RATE_RESET)
		snd_vt1724_set_pro_rate(ice, ice->pro_rate_default, 0);
	ice->playback_pro_substream = NULL;

	return 0;
}

static int snd_vt1724_capture_pro_close(struct snd_pcm_substream *substream)
{
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);

	if (PRO_RATE_RESET)
		snd_vt1724_set_pro_rate(ice, ice->pro_rate_default, 0);
	ice->capture_pro_substream = NULL;
	return 0;
}

static struct snd_pcm_ops snd_vt1724_playback_pro_ops = {
	.open =		snd_vt1724_playback_pro_open,
	.close =	snd_vt1724_playback_pro_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_vt1724_pcm_hw_params,
	.hw_free =	snd_vt1724_pcm_hw_free,
	.prepare =	snd_vt1724_playback_pro_prepare,
	.trigger =	snd_vt1724_pcm_trigger,
	.pointer =	snd_vt1724_playback_pro_pointer,
};

static struct snd_pcm_ops snd_vt1724_capture_pro_ops = {
	.open =		snd_vt1724_capture_pro_open,
	.close =	snd_vt1724_capture_pro_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_vt1724_pcm_hw_params,
	.hw_free =	snd_vt1724_pcm_hw_free,
	.prepare =	snd_vt1724_pcm_prepare,
	.trigger =	snd_vt1724_pcm_trigger,
	.pointer =	snd_vt1724_pcm_pointer,
};

static int snd_vt1724_pcm_profi(struct snd_ice1712 *ice, int device)
{
	struct snd_pcm *pcm;
	int capt, err;

	if ((ice->eeprom.data[ICE_EEP2_SYSCONF] & VT1724_CFG_ADC_MASK) ==
	    VT1724_CFG_ADC_NONE)
		capt = 0;
	else
		capt = 1;
	err = snd_pcm_new(ice->card, "ICE1724", device, 1, capt, &pcm);
	if (err < 0)
		return err;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_vt1724_playback_pro_ops);
	if (capt)
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE,
			&snd_vt1724_capture_pro_ops);

	pcm->private_data = ice;
	pcm->info_flags = 0;
	strcpy(pcm->name, "ICE1724");

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
					      snd_dma_pci_data(ice->pci),
					      256*1024, 256*1024);

	ice->pcm_pro = pcm;

	return 0;
}


/*
 * SPDIF PCM
 */

/* update spdif control bits; call with reg_lock */
static void update_spdif_bits(struct snd_ice1712 *ice, unsigned int val)
{
	unsigned char cbit, disabled;

	cbit = inb(ICEREG1724(ice, SPDIF_CFG));
	disabled = cbit & ~VT1724_CFG_SPDIF_OUT_EN;
	if (cbit != disabled)
		outb(disabled, ICEREG1724(ice, SPDIF_CFG));
	outw(val, ICEMT1724(ice, SPDIF_CTRL));
	if (cbit != disabled)
		outb(cbit, ICEREG1724(ice, SPDIF_CFG));
	outw(val, ICEMT1724(ice, SPDIF_CTRL));
}

/* update SPDIF control bits according to the given rate */
static void update_spdif_rate(struct snd_ice1712 *ice, unsigned int rate)
{
	unsigned int val, nval;
	unsigned long flags;

	spin_lock_irqsave(&ice->reg_lock, flags);
	nval = val = inw(ICEMT1724(ice, SPDIF_CTRL));
	nval &= ~(7 << 12);
	switch (rate) {
	case 44100: break;
	case 48000: nval |= 2 << 12; break;
	case 32000: nval |= 3 << 12; break;
	case 88200: nval |= 4 << 12; break;
	case 96000: nval |= 5 << 12; break;
	case 192000: nval |= 6 << 12; break;
	case 176400: nval |= 7 << 12; break;
	}
	if (val != nval)
		update_spdif_bits(ice, nval);
	spin_unlock_irqrestore(&ice->reg_lock, flags);
}

static int snd_vt1724_playback_spdif_prepare(struct snd_pcm_substream *substream)
{
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);
	if (!ice->force_pdma4)
		update_spdif_rate(ice, substream->runtime->rate);
	return snd_vt1724_pcm_prepare(substream);
}

static int snd_vt1724_playback_spdif_open(struct snd_pcm_substream *substream)
{
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	runtime->private_data = (void *)&vt1724_playback_spdif_reg;
	ice->playback_con_substream = substream;
	if (ice->force_pdma4) {
		runtime->hw = snd_vt1724_2ch_stereo;
		set_rate_constraints(ice, substream);
	} else
		runtime->hw = snd_vt1724_spdif;
	snd_pcm_set_sync(substream);
	snd_pcm_hw_constraint_msbits(runtime, 0, 32, 24);
	snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES,
				   VT1724_BUFFER_ALIGN);
	snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
				   VT1724_BUFFER_ALIGN);
	constrain_rate_if_locked(substream);
	if (ice->spdif.ops.open)
		ice->spdif.ops.open(ice, substream);
	return 0;
}

static int snd_vt1724_playback_spdif_close(struct snd_pcm_substream *substream)
{
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);

	if (PRO_RATE_RESET)
		snd_vt1724_set_pro_rate(ice, ice->pro_rate_default, 0);
	ice->playback_con_substream = NULL;
	if (ice->spdif.ops.close)
		ice->spdif.ops.close(ice, substream);

	return 0;
}

static int snd_vt1724_capture_spdif_open(struct snd_pcm_substream *substream)
{
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	runtime->private_data = (void *)&vt1724_capture_spdif_reg;
	ice->capture_con_substream = substream;
	if (ice->force_rdma1) {
		runtime->hw = snd_vt1724_2ch_stereo;
		set_rate_constraints(ice, substream);
	} else
		runtime->hw = snd_vt1724_spdif;
	snd_pcm_set_sync(substream);
	snd_pcm_hw_constraint_msbits(runtime, 0, 32, 24);
	snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES,
				   VT1724_BUFFER_ALIGN);
	snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
				   VT1724_BUFFER_ALIGN);
	constrain_rate_if_locked(substream);
	if (ice->spdif.ops.open)
		ice->spdif.ops.open(ice, substream);
	return 0;
}

static int snd_vt1724_capture_spdif_close(struct snd_pcm_substream *substream)
{
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);

	if (PRO_RATE_RESET)
		snd_vt1724_set_pro_rate(ice, ice->pro_rate_default, 0);
	ice->capture_con_substream = NULL;
	if (ice->spdif.ops.close)
		ice->spdif.ops.close(ice, substream);

	return 0;
}

static struct snd_pcm_ops snd_vt1724_playback_spdif_ops = {
	.open =		snd_vt1724_playback_spdif_open,
	.close =	snd_vt1724_playback_spdif_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_vt1724_pcm_hw_params,
	.hw_free =	snd_vt1724_pcm_hw_free,
	.prepare =	snd_vt1724_playback_spdif_prepare,
	.trigger =	snd_vt1724_pcm_trigger,
	.pointer =	snd_vt1724_pcm_pointer,
};

static struct snd_pcm_ops snd_vt1724_capture_spdif_ops = {
	.open =		snd_vt1724_capture_spdif_open,
	.close =	snd_vt1724_capture_spdif_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_vt1724_pcm_hw_params,
	.hw_free =	snd_vt1724_pcm_hw_free,
	.prepare =	snd_vt1724_pcm_prepare,
	.trigger =	snd_vt1724_pcm_trigger,
	.pointer =	snd_vt1724_pcm_pointer,
};


static int snd_vt1724_pcm_spdif(struct snd_ice1712 *ice, int device)
{
	char *name;
	struct snd_pcm *pcm;
	int play, capt;
	int err;

	if (ice->force_pdma4 ||
	    (ice->eeprom.data[ICE_EEP2_SPDIF] & VT1724_CFG_SPDIF_OUT_INT)) {
		play = 1;
		ice->has_spdif = 1;
	} else
		play = 0;
	if (ice->force_rdma1 ||
	    (ice->eeprom.data[ICE_EEP2_SPDIF] & VT1724_CFG_SPDIF_IN)) {
		capt = 1;
		ice->has_spdif = 1;
	} else
		capt = 0;
	if (!play && !capt)
		return 0; /* no spdif device */

	if (ice->force_pdma4 || ice->force_rdma1)
		name = "ICE1724 Secondary";
	else
		name = "ICE1724 IEC958";
	err = snd_pcm_new(ice->card, name, device, play, capt, &pcm);
	if (err < 0)
		return err;

	if (play)
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK,
				&snd_vt1724_playback_spdif_ops);
	if (capt)
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE,
				&snd_vt1724_capture_spdif_ops);

	pcm->private_data = ice;
	pcm->info_flags = 0;
	strcpy(pcm->name, name);

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
					      snd_dma_pci_data(ice->pci),
					      256*1024, 256*1024);

	ice->pcm = pcm;

	return 0;
}


/*
 * independent surround PCMs
 */

static const struct vt1724_pcm_reg vt1724_playback_dma_regs[3] = {
	{
		.addr = VT1724_MT_PDMA1_ADDR,
		.size = VT1724_MT_PDMA1_SIZE,
		.count = VT1724_MT_PDMA1_COUNT,
		.start = VT1724_PDMA1_START,
	},
	{
		.addr = VT1724_MT_PDMA2_ADDR,
		.size = VT1724_MT_PDMA2_SIZE,
		.count = VT1724_MT_PDMA2_COUNT,
		.start = VT1724_PDMA2_START,
	},
	{
		.addr = VT1724_MT_PDMA3_ADDR,
		.size = VT1724_MT_PDMA3_SIZE,
		.count = VT1724_MT_PDMA3_COUNT,
		.start = VT1724_PDMA3_START,
	},
};

static int snd_vt1724_playback_indep_prepare(struct snd_pcm_substream *substream)
{
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);
	unsigned char val;

	spin_lock_irq(&ice->reg_lock);
	val = 3 - substream->number;
	if (inb(ICEMT1724(ice, BURST)) < val)
		outb(val, ICEMT1724(ice, BURST));
	spin_unlock_irq(&ice->reg_lock);
	return snd_vt1724_pcm_prepare(substream);
}

static int snd_vt1724_playback_indep_open(struct snd_pcm_substream *substream)
{
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	mutex_lock(&ice->open_mutex);
	/* already used by PDMA0? */
	if (ice->pcm_reserved[substream->number]) {
		mutex_unlock(&ice->open_mutex);
		return -EBUSY; /* FIXME: should handle blocking mode properly */
	}
	mutex_unlock(&ice->open_mutex);
	runtime->private_data = (void *)&vt1724_playback_dma_regs[substream->number];
	ice->playback_con_substream_ds[substream->number] = substream;
	runtime->hw = snd_vt1724_2ch_stereo;
	snd_pcm_set_sync(substream);
	snd_pcm_hw_constraint_msbits(runtime, 0, 32, 24);
	set_rate_constraints(ice, substream);
	return 0;
}

static int snd_vt1724_playback_indep_close(struct snd_pcm_substream *substream)
{
	struct snd_ice1712 *ice = snd_pcm_substream_chip(substream);

	if (PRO_RATE_RESET)
		snd_vt1724_set_pro_rate(ice, ice->pro_rate_default, 0);
	ice->playback_con_substream_ds[substream->number] = NULL;
	ice->pcm_reserved[substream->number] = NULL;

	return 0;
}

static struct snd_pcm_ops snd_vt1724_playback_indep_ops = {
	.open =		snd_vt1724_playback_indep_open,
	.close =	snd_vt1724_playback_indep_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_vt1724_pcm_hw_params,
	.hw_free =	snd_vt1724_pcm_hw_free,
	.prepare =	snd_vt1724_playback_indep_prepare,
	.trigger =	snd_vt1724_pcm_trigger,
	.pointer =	snd_vt1724_pcm_pointer,
};


static int snd_vt1724_pcm_indep(struct snd_ice1712 *ice, int device)
{
	struct snd_pcm *pcm;
	int play;
	int err;

	play = ice->num_total_dacs / 2 - 1;
	if (play <= 0)
		return 0;

	err = snd_pcm_new(ice->card, "ICE1724 Surrounds", device, play, 0, &pcm);
	if (err < 0)
		return err;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK,
			&snd_vt1724_playback_indep_ops);

	pcm->private_data = ice;
	pcm->info_flags = 0;
	strcpy(pcm->name, "ICE1724 Surround PCM");

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
					      snd_dma_pci_data(ice->pci),
					      256*1024, 256*1024);

	ice->pcm_ds = pcm;

	return 0;
}


/*
 *  Mixer section
 */

static int snd_vt1724_ac97_mixer(struct snd_ice1712 *ice)
{
	int err;

	if (!(ice->eeprom.data[ICE_EEP2_ACLINK] & VT1724_CFG_PRO_I2S)) {
		struct snd_ac97_bus *pbus;
		struct snd_ac97_template ac97;
		static struct snd_ac97_bus_ops ops = {
			.write = snd_vt1724_ac97_write,
			.read = snd_vt1724_ac97_read,
		};

		/* cold reset */
		outb(inb(ICEMT1724(ice, AC97_CMD)) | 0x80, ICEMT1724(ice, AC97_CMD));
		mdelay(5); /* FIXME */
		outb(inb(ICEMT1724(ice, AC97_CMD)) & ~0x80, ICEMT1724(ice, AC97_CMD));

		err = snd_ac97_bus(ice->card, 0, &ops, NULL, &pbus);
		if (err < 0)
			return err;
		memset(&ac97, 0, sizeof(ac97));
		ac97.private_data = ice;
		err = snd_ac97_mixer(pbus, &ac97, &ice->ac97);
		if (err < 0)
			dev_warn(ice->card->dev,
				 "cannot initialize pro ac97, skipped\n");
		else
			return 0;
	}
	/* I2S mixer only */
	strcat(ice->card->mixername, "ICE1724 - multitrack");
	return 0;
}

/*
 *
 */

static inline unsigned int eeprom_triple(struct snd_ice1712 *ice, int idx)
{
	return (unsigned int)ice->eeprom.data[idx] | \
		((unsigned int)ice->eeprom.data[idx + 1] << 8) | \
		((unsigned int)ice->eeprom.data[idx + 2] << 16);
}

static void snd_vt1724_proc_read(struct snd_info_entry *entry,
				 struct snd_info_buffer *buffer)
{
	struct snd_ice1712 *ice = entry->private_data;
	unsigned int idx;

	snd_iprintf(buffer, "%s\n\n", ice->card->longname);
	snd_iprintf(buffer, "EEPROM:\n");

	snd_iprintf(buffer, "  Subvendor        : 0x%x\n", ice->eeprom.subvendor);
	snd_iprintf(buffer, "  Size             : %i bytes\n", ice->eeprom.size);
	snd_iprintf(buffer, "  Version          : %i\n", ice->eeprom.version);
	snd_iprintf(buffer, "  System Config    : 0x%x\n",
		    ice->eeprom.data[ICE_EEP2_SYSCONF]);
	snd_iprintf(buffer, "  ACLink           : 0x%x\n",
		    ice->eeprom.data[ICE_EEP2_ACLINK]);
	snd_iprintf(buffer, "  I2S              : 0x%x\n",
		    ice->eeprom.data[ICE_EEP2_I2S]);
	snd_iprintf(buffer, "  S/PDIF           : 0x%x\n",
		    ice->eeprom.data[ICE_EEP2_SPDIF]);
	snd_iprintf(buffer, "  GPIO direction   : 0x%x\n",
		    ice->eeprom.gpiodir);
	snd_iprintf(buffer, "  GPIO mask        : 0x%x\n",
		    ice->eeprom.gpiomask);
	snd_iprintf(buffer, "  GPIO state       : 0x%x\n",
		    ice->eeprom.gpiostate);
	for (idx = 0x12; idx < ice->eeprom.size; idx++)
		snd_iprintf(buffer, "  Extra #%02i        : 0x%x\n",
			    idx, ice->eeprom.data[idx]);

	snd_iprintf(buffer, "\nRegisters:\n");

	snd_iprintf(buffer, "  PSDOUT03 : 0x%08x\n",
		    (unsigned)inl(ICEMT1724(ice, ROUTE_PLAYBACK)));
	for (idx = 0x0; idx < 0x20 ; idx++)
		snd_iprintf(buffer, "  CCS%02x    : 0x%02x\n",
			    idx, inb(ice->port+idx));
	for (idx = 0x0; idx < 0x30 ; idx++)
		snd_iprintf(buffer, "  MT%02x     : 0x%02x\n",
			    idx, inb(ice->profi_port+idx));
}

static void snd_vt1724_proc_init(struct snd_ice1712 *ice)
{
	struct snd_info_entry *entry;

	if (!snd_card_proc_new(ice->card, "ice1724", &entry))
		snd_info_set_text_ops(entry, ice, snd_vt1724_proc_read);
}

/*
 *
 */

static int snd_vt1724_eeprom_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BYTES;
	uinfo->count = sizeof(struct snd_ice1712_eeprom);
	return 0;
}

static int snd_vt1724_eeprom_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);

	memcpy(ucontrol->value.bytes.data, &ice->eeprom, sizeof(ice->eeprom));
	return 0;
}

static struct snd_kcontrol_new snd_vt1724_eeprom = {
	.iface = SNDRV_CTL_ELEM_IFACE_CARD,
	.name = "ICE1724 EEPROM",
	.access = SNDRV_CTL_ELEM_ACCESS_READ,
	.info = snd_vt1724_eeprom_info,
	.get = snd_vt1724_eeprom_get
};

/*
 */
static int snd_vt1724_spdif_info(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static unsigned int encode_spdif_bits(struct snd_aes_iec958 *diga)
{
	unsigned int val, rbits;

	val = diga->status[0] & 0x03; /* professional, non-audio */
	if (val & 0x01) {
		/* professional */
		if ((diga->status[0] & IEC958_AES0_PRO_EMPHASIS) ==
		    IEC958_AES0_PRO_EMPHASIS_5015)
			val |= 1U << 3;
		rbits = (diga->status[4] >> 3) & 0x0f;
		if (rbits) {
			switch (rbits) {
			case 2: val |= 5 << 12; break; /* 96k */
			case 3: val |= 6 << 12; break; /* 192k */
			case 10: val |= 4 << 12; break; /* 88.2k */
			case 11: val |= 7 << 12; break; /* 176.4k */
			}
		} else {
			switch (diga->status[0] & IEC958_AES0_PRO_FS) {
			case IEC958_AES0_PRO_FS_44100:
				break;
			case IEC958_AES0_PRO_FS_32000:
				val |= 3U << 12;
				break;
			default:
				val |= 2U << 12;
				break;
			}
		}
	} else {
		/* consumer */
		val |= diga->status[1] & 0x04; /* copyright */
		if ((diga->status[0] & IEC958_AES0_CON_EMPHASIS) ==
		    IEC958_AES0_CON_EMPHASIS_5015)
			val |= 1U << 3;
		val |= (unsigned int)(diga->status[1] & 0x3f) << 4; /* category */
		val |= (unsigned int)(diga->status[3] & IEC958_AES3_CON_FS) << 12; /* fs */
	}
	return val;
}

static void decode_spdif_bits(struct snd_aes_iec958 *diga, unsigned int val)
{
	memset(diga->status, 0, sizeof(diga->status));
	diga->status[0] = val & 0x03; /* professional, non-audio */
	if (val & 0x01) {
		/* professional */
		if (val & (1U << 3))
			diga->status[0] |= IEC958_AES0_PRO_EMPHASIS_5015;
		switch ((val >> 12) & 0x7) {
		case 0:
			break;
		case 2:
			diga->status[0] |= IEC958_AES0_PRO_FS_32000;
			break;
		default:
			diga->status[0] |= IEC958_AES0_PRO_FS_48000;
			break;
		}
	} else {
		/* consumer */
		diga->status[0] |= val & (1U << 2); /* copyright */
		if (val & (1U << 3))
			diga->status[0] |= IEC958_AES0_CON_EMPHASIS_5015;
		diga->status[1] |= (val >> 4) & 0x3f; /* category */
		diga->status[3] |= (val >> 12) & 0x07; /* fs */
	}
}

static int snd_vt1724_spdif_default_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned int val;
	val = inw(ICEMT1724(ice, SPDIF_CTRL));
	decode_spdif_bits(&ucontrol->value.iec958, val);
	return 0;
}

static int snd_vt1724_spdif_default_put(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned int val, old;

	val = encode_spdif_bits(&ucontrol->value.iec958);
	spin_lock_irq(&ice->reg_lock);
	old = inw(ICEMT1724(ice, SPDIF_CTRL));
	if (val != old)
		update_spdif_bits(ice, val);
	spin_unlock_irq(&ice->reg_lock);
	return val != old;
}

static struct snd_kcontrol_new snd_vt1724_spdif_default =
{
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =         SNDRV_CTL_NAME_IEC958("", PLAYBACK, DEFAULT),
	.info =		snd_vt1724_spdif_info,
	.get =		snd_vt1724_spdif_default_get,
	.put =		snd_vt1724_spdif_default_put
};

static int snd_vt1724_spdif_maskc_get(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.iec958.status[0] = IEC958_AES0_NONAUDIO |
						     IEC958_AES0_PROFESSIONAL |
						     IEC958_AES0_CON_NOT_COPYRIGHT |
						     IEC958_AES0_CON_EMPHASIS;
	ucontrol->value.iec958.status[1] = IEC958_AES1_CON_ORIGINAL |
						     IEC958_AES1_CON_CATEGORY;
	ucontrol->value.iec958.status[3] = IEC958_AES3_CON_FS;
	return 0;
}

static int snd_vt1724_spdif_maskp_get(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.iec958.status[0] = IEC958_AES0_NONAUDIO |
						     IEC958_AES0_PROFESSIONAL |
						     IEC958_AES0_PRO_FS |
						     IEC958_AES0_PRO_EMPHASIS;
	return 0;
}

static struct snd_kcontrol_new snd_vt1724_spdif_maskc =
{
	.access =	SNDRV_CTL_ELEM_ACCESS_READ,
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =         SNDRV_CTL_NAME_IEC958("", PLAYBACK, CON_MASK),
	.info =		snd_vt1724_spdif_info,
	.get =		snd_vt1724_spdif_maskc_get,
};

static struct snd_kcontrol_new snd_vt1724_spdif_maskp =
{
	.access =	SNDRV_CTL_ELEM_ACCESS_READ,
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =         SNDRV_CTL_NAME_IEC958("", PLAYBACK, PRO_MASK),
	.info =		snd_vt1724_spdif_info,
	.get =		snd_vt1724_spdif_maskp_get,
};

#define snd_vt1724_spdif_sw_info		snd_ctl_boolean_mono_info

static int snd_vt1724_spdif_sw_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	ucontrol->value.integer.value[0] = inb(ICEREG1724(ice, SPDIF_CFG)) &
		VT1724_CFG_SPDIF_OUT_EN ? 1 : 0;
	return 0;
}

static int snd_vt1724_spdif_sw_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned char old, val;

	spin_lock_irq(&ice->reg_lock);
	old = val = inb(ICEREG1724(ice, SPDIF_CFG));
	val &= ~VT1724_CFG_SPDIF_OUT_EN;
	if (ucontrol->value.integer.value[0])
		val |= VT1724_CFG_SPDIF_OUT_EN;
	if (old != val)
		outb(val, ICEREG1724(ice, SPDIF_CFG));
	spin_unlock_irq(&ice->reg_lock);
	return old != val;
}

static struct snd_kcontrol_new snd_vt1724_spdif_switch =
{
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	/* FIXME: the following conflict with IEC958 Playback Route */
	/* .name =         SNDRV_CTL_NAME_IEC958("", PLAYBACK, SWITCH), */
	.name =         SNDRV_CTL_NAME_IEC958("Output ", NONE, SWITCH),
	.info =		snd_vt1724_spdif_sw_info,
	.get =		snd_vt1724_spdif_sw_get,
	.put =		snd_vt1724_spdif_sw_put
};


#if 0 /* NOT USED YET */
/*
 * GPIO access from extern
 */

#define snd_vt1724_gpio_info		snd_ctl_boolean_mono_info

int snd_vt1724_gpio_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	int shift = kcontrol->private_value & 0xff;
	int invert = (kcontrol->private_value & (1<<24)) ? 1 : 0;

	snd_ice1712_save_gpio_status(ice);
	ucontrol->value.integer.value[0] =
		(snd_ice1712_gpio_read(ice) & (1 << shift) ? 1 : 0) ^ invert;
	snd_ice1712_restore_gpio_status(ice);
	return 0;
}

int snd_ice1712_gpio_put(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	int shift = kcontrol->private_value & 0xff;
	int invert = (kcontrol->private_value & (1<<24)) ? mask : 0;
	unsigned int val, nval;

	if (kcontrol->private_value & (1 << 31))
		return -EPERM;
	nval = (ucontrol->value.integer.value[0] ? (1 << shift) : 0) ^ invert;
	snd_ice1712_save_gpio_status(ice);
	val = snd_ice1712_gpio_read(ice);
	nval |= val & ~(1 << shift);
	if (val != nval)
		snd_ice1712_gpio_write(ice, nval);
	snd_ice1712_restore_gpio_status(ice);
	return val != nval;
}
#endif /* NOT USED YET */

/*
 *  rate
 */
static int snd_vt1724_pro_internal_clock_info(struct snd_kcontrol *kcontrol,
					      struct snd_ctl_elem_info *uinfo)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	int hw_rates_count = ice->hw_rates->count;
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;

	/* internal clocks */
	uinfo->value.enumerated.items = hw_rates_count;
	/* external clocks */
	if (ice->force_rdma1 ||
	    (ice->eeprom.data[ICE_EEP2_SPDIF] & VT1724_CFG_SPDIF_IN))
		uinfo->value.enumerated.items += ice->ext_clock_count;
	/* upper limit - keep at top */
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	if (uinfo->value.enumerated.item >= hw_rates_count)
		/* ext_clock items */
		strcpy(uinfo->value.enumerated.name,
				ice->ext_clock_names[
				uinfo->value.enumerated.item - hw_rates_count]);
	else
		/* int clock items */
		sprintf(uinfo->value.enumerated.name, "%d",
			ice->hw_rates->list[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_vt1724_pro_internal_clock_get(struct snd_kcontrol *kcontrol,
					     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned int i, rate;

	spin_lock_irq(&ice->reg_lock);
	if (ice->is_spdif_master(ice)) {
		ucontrol->value.enumerated.item[0] = ice->hw_rates->count +
			ice->get_spdif_master_type(ice);
	} else {
		rate = ice->get_rate(ice);
		ucontrol->value.enumerated.item[0] = 0;
		for (i = 0; i < ice->hw_rates->count; i++) {
			if (ice->hw_rates->list[i] == rate) {
				ucontrol->value.enumerated.item[0] = i;
				break;
			}
		}
	}
	spin_unlock_irq(&ice->reg_lock);
	return 0;
}

static int stdclock_get_spdif_master_type(struct snd_ice1712 *ice)
{
	/* standard external clock - only single type - SPDIF IN */
	return 0;
}

/* setting clock to external - SPDIF */
static int stdclock_set_spdif_clock(struct snd_ice1712 *ice, int type)
{
	unsigned char oval;
	unsigned char i2s_oval;
	oval = inb(ICEMT1724(ice, RATE));
	outb(oval | VT1724_SPDIF_MASTER, ICEMT1724(ice, RATE));
	/* setting 256fs */
	i2s_oval = inb(ICEMT1724(ice, I2S_FORMAT));
	outb(i2s_oval & ~VT1724_MT_I2S_MCLK_128X, ICEMT1724(ice, I2S_FORMAT));
	return 0;
}


static int snd_vt1724_pro_internal_clock_put(struct snd_kcontrol *kcontrol,
					     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned int old_rate, new_rate;
	unsigned int item = ucontrol->value.enumerated.item[0];
	unsigned int first_ext_clock = ice->hw_rates->count;

	if (item >  first_ext_clock + ice->ext_clock_count - 1)
		return -EINVAL;

	/* if rate = 0 => external clock */
	spin_lock_irq(&ice->reg_lock);
	if (ice->is_spdif_master(ice))
		old_rate = 0;
	else
		old_rate = ice->get_rate(ice);
	if (item >= first_ext_clock) {
		/* switching to external clock */
		ice->set_spdif_clock(ice, item - first_ext_clock);
		new_rate = 0;
	} else {
		/* internal on-card clock */
		new_rate = ice->hw_rates->list[item];
		ice->pro_rate_default = new_rate;
		spin_unlock_irq(&ice->reg_lock);
		snd_vt1724_set_pro_rate(ice, ice->pro_rate_default, 1);
		spin_lock_irq(&ice->reg_lock);
	}
	spin_unlock_irq(&ice->reg_lock);

	/* the first switch to the ext. clock mode? */
	if (old_rate != new_rate && !new_rate) {
		/* notify akm chips as well */
		unsigned int i;
		if (ice->gpio.set_pro_rate)
			ice->gpio.set_pro_rate(ice, 0);
		for (i = 0; i < ice->akm_codecs; i++) {
			if (ice->akm[i].ops.set_rate_val)
				ice->akm[i].ops.set_rate_val(&ice->akm[i], 0);
		}
	}
	return old_rate != new_rate;
}

static struct snd_kcontrol_new snd_vt1724_pro_internal_clock = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Multi Track Internal Clock",
	.info = snd_vt1724_pro_internal_clock_info,
	.get = snd_vt1724_pro_internal_clock_get,
	.put = snd_vt1724_pro_internal_clock_put
};

#define snd_vt1724_pro_rate_locking_info	snd_ctl_boolean_mono_info

static int snd_vt1724_pro_rate_locking_get(struct snd_kcontrol *kcontrol,
					   struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = PRO_RATE_LOCKED;
	return 0;
}

static int snd_vt1724_pro_rate_locking_put(struct snd_kcontrol *kcontrol,
					   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	int change = 0, nval;

	nval = ucontrol->value.integer.value[0] ? 1 : 0;
	spin_lock_irq(&ice->reg_lock);
	change = PRO_RATE_LOCKED != nval;
	PRO_RATE_LOCKED = nval;
	spin_unlock_irq(&ice->reg_lock);
	return change;
}

static struct snd_kcontrol_new snd_vt1724_pro_rate_locking = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Multi Track Rate Locking",
	.info = snd_vt1724_pro_rate_locking_info,
	.get = snd_vt1724_pro_rate_locking_get,
	.put = snd_vt1724_pro_rate_locking_put
};

#define snd_vt1724_pro_rate_reset_info		snd_ctl_boolean_mono_info

static int snd_vt1724_pro_rate_reset_get(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = PRO_RATE_RESET ? 1 : 0;
	return 0;
}

static int snd_vt1724_pro_rate_reset_put(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	int change = 0, nval;

	nval = ucontrol->value.integer.value[0] ? 1 : 0;
	spin_lock_irq(&ice->reg_lock);
	change = PRO_RATE_RESET != nval;
	PRO_RATE_RESET = nval;
	spin_unlock_irq(&ice->reg_lock);
	return change;
}

static struct snd_kcontrol_new snd_vt1724_pro_rate_reset = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Multi Track Rate Reset",
	.info = snd_vt1724_pro_rate_reset_info,
	.get = snd_vt1724_pro_rate_reset_get,
	.put = snd_vt1724_pro_rate_reset_put
};


/*
 * routing
 */
static int snd_vt1724_pro_route_info(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts[] = {
		"PCM Out", /* 0 */
		"H/W In 0", "H/W In 1", /* 1-2 */
		"IEC958 In L", "IEC958 In R", /* 3-4 */
	};

	return snd_ctl_enum_info(uinfo, 1, 5, texts);
}

static inline int analog_route_shift(int idx)
{
	return (idx % 2) * 12 + ((idx / 2) * 3) + 8;
}

static inline int digital_route_shift(int idx)
{
	return idx * 3;
}

int snd_ice1724_get_route_val(struct snd_ice1712 *ice, int shift)
{
	unsigned long val;
	unsigned char eitem;
	static const unsigned char xlate[8] = {
		0, 255, 1, 2, 255, 255, 3, 4,
	};

	val = inl(ICEMT1724(ice, ROUTE_PLAYBACK));
	val >>= shift;
	val &= 7; /* we now have 3 bits per output */
	eitem = xlate[val];
	if (eitem == 255) {
		snd_BUG();
		return 0;
	}
	return eitem;
}

int snd_ice1724_put_route_val(struct snd_ice1712 *ice, unsigned int val,
								int shift)
{
	unsigned int old_val, nval;
	int change;
	static const unsigned char xroute[8] = {
		0, /* PCM */
		2, /* PSDIN0 Left */
		3, /* PSDIN0 Right */
		6, /* SPDIN Left */
		7, /* SPDIN Right */
	};

	nval = xroute[val % 5];
	val = old_val = inl(ICEMT1724(ice, ROUTE_PLAYBACK));
	val &= ~(0x07 << shift);
	val |= nval << shift;
	change = val != old_val;
	if (change)
		outl(val, ICEMT1724(ice, ROUTE_PLAYBACK));
	return change;
}

static int snd_vt1724_pro_route_analog_get(struct snd_kcontrol *kcontrol,
					   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	int idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	ucontrol->value.enumerated.item[0] =
		snd_ice1724_get_route_val(ice, analog_route_shift(idx));
	return 0;
}

static int snd_vt1724_pro_route_analog_put(struct snd_kcontrol *kcontrol,
					   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	int idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	return snd_ice1724_put_route_val(ice,
					 ucontrol->value.enumerated.item[0],
					 analog_route_shift(idx));
}

static int snd_vt1724_pro_route_spdif_get(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	int idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	ucontrol->value.enumerated.item[0] =
		snd_ice1724_get_route_val(ice, digital_route_shift(idx));
	return 0;
}

static int snd_vt1724_pro_route_spdif_put(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	int idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	return snd_ice1724_put_route_val(ice,
					 ucontrol->value.enumerated.item[0],
					 digital_route_shift(idx));
}

static struct snd_kcontrol_new snd_vt1724_mixer_pro_analog_route =
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "H/W Playback Route",
	.info = snd_vt1724_pro_route_info,
	.get = snd_vt1724_pro_route_analog_get,
	.put = snd_vt1724_pro_route_analog_put,
};

static struct snd_kcontrol_new snd_vt1724_mixer_pro_spdif_route = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = SNDRV_CTL_NAME_IEC958("", PLAYBACK, NONE) "Route",
	.info = snd_vt1724_pro_route_info,
	.get = snd_vt1724_pro_route_spdif_get,
	.put = snd_vt1724_pro_route_spdif_put,
	.count = 2,
};


static int snd_vt1724_pro_peak_info(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 22; /* FIXME: for compatibility with ice1712... */
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 255;
	return 0;
}

static int snd_vt1724_pro_peak_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	int idx;

	spin_lock_irq(&ice->reg_lock);
	for (idx = 0; idx < 22; idx++) {
		outb(idx, ICEMT1724(ice, MONITOR_PEAKINDEX));
		ucontrol->value.integer.value[idx] =
			inb(ICEMT1724(ice, MONITOR_PEAKDATA));
	}
	spin_unlock_irq(&ice->reg_lock);
	return 0;
}

static struct snd_kcontrol_new snd_vt1724_mixer_pro_peak = {
	.iface = SNDRV_CTL_ELEM_IFACE_PCM,
	.name = "Multi Track Peak",
	.access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
	.info = snd_vt1724_pro_peak_info,
	.get = snd_vt1724_pro_peak_get
};

/*
 *
 */

static struct snd_ice1712_card_info no_matched;


/*
  ooAoo cards with no controls
*/
static unsigned char ooaoo_sq210_eeprom[] = {
	[ICE_EEP2_SYSCONF]     = 0x4c,	/* 49MHz crystal, no mpu401, no ADC,
					   1xDACs */
	[ICE_EEP2_ACLINK]      = 0x80,	/* I2S */
	[ICE_EEP2_I2S]         = 0x78,	/* no volume, 96k, 24bit, 192k */
	[ICE_EEP2_SPDIF]       = 0xc1,	/* out-en, out-int, out-ext */
	[ICE_EEP2_GPIO_DIR]    = 0x00,	/* no GPIOs are used */
	[ICE_EEP2_GPIO_DIR1]   = 0x00,
	[ICE_EEP2_GPIO_DIR2]   = 0x00,
	[ICE_EEP2_GPIO_MASK]   = 0xff,
	[ICE_EEP2_GPIO_MASK1]  = 0xff,
	[ICE_EEP2_GPIO_MASK2]  = 0xff,

	[ICE_EEP2_GPIO_STATE]  = 0x00, /* inputs */
	[ICE_EEP2_GPIO_STATE1] = 0x00, /* all 1, but GPIO_CPLD_RW
					  and GPIO15 always zero */
	[ICE_EEP2_GPIO_STATE2] = 0x00, /* inputs */
};


static struct snd_ice1712_card_info snd_vt1724_ooaoo_cards[] = {
	{
		.name = "ooAoo SQ210a",
		.model = "sq210a",
		.eeprom_size = sizeof(ooaoo_sq210_eeprom),
		.eeprom_data = ooaoo_sq210_eeprom,
	},
	{ } /* terminator */
};

static struct snd_ice1712_card_info *card_tables[] = {
	snd_vt1724_revo_cards,
	snd_vt1724_amp_cards,
	snd_vt1724_aureon_cards,
	snd_vt1720_mobo_cards,
	snd_vt1720_pontis_cards,
	snd_vt1724_prodigy_hifi_cards,
	snd_vt1724_prodigy192_cards,
	snd_vt1724_juli_cards,
	snd_vt1724_maya44_cards,
	snd_vt1724_phase_cards,
	snd_vt1724_wtm_cards,
	snd_vt1724_se_cards,
	snd_vt1724_qtet_cards,
	snd_vt1724_ooaoo_cards,
	snd_vt1724_psc724_cards,
	NULL,
};


/*
 */

static void wait_i2c_busy(struct snd_ice1712 *ice)
{
	int t = 0x10000;
	while ((inb(ICEREG1724(ice, I2C_CTRL)) & VT1724_I2C_BUSY) && t--)
		;
	if (t == -1)
		dev_err(ice->card->dev, "i2c busy timeout\n");
}

unsigned char snd_vt1724_read_i2c(struct snd_ice1712 *ice,
				  unsigned char dev, unsigned char addr)
{
	unsigned char val;

	mutex_lock(&ice->i2c_mutex);
	wait_i2c_busy(ice);
	outb(addr, ICEREG1724(ice, I2C_BYTE_ADDR));
	outb(dev & ~VT1724_I2C_WRITE, ICEREG1724(ice, I2C_DEV_ADDR));
	wait_i2c_busy(ice);
	val = inb(ICEREG1724(ice, I2C_DATA));
	mutex_unlock(&ice->i2c_mutex);
	/*
	dev_dbg(ice->card->dev, "i2c_read: [0x%x,0x%x] = 0x%x\n", dev, addr, val);
	*/
	return val;
}

void snd_vt1724_write_i2c(struct snd_ice1712 *ice,
			  unsigned char dev, unsigned char addr, unsigned char data)
{
	mutex_lock(&ice->i2c_mutex);
	wait_i2c_busy(ice);
	/*
	dev_dbg(ice->card->dev, "i2c_write: [0x%x,0x%x] = 0x%x\n", dev, addr, data);
	*/
	outb(addr, ICEREG1724(ice, I2C_BYTE_ADDR));
	outb(data, ICEREG1724(ice, I2C_DATA));
	outb(dev | VT1724_I2C_WRITE, ICEREG1724(ice, I2C_DEV_ADDR));
	wait_i2c_busy(ice);
	mutex_unlock(&ice->i2c_mutex);
}

static int snd_vt1724_read_eeprom(struct snd_ice1712 *ice,
				  const char *modelname)
{
	const int dev = 0xa0;		/* EEPROM device address */
	unsigned int i, size;
	struct snd_ice1712_card_info * const *tbl, *c;

	if (!modelname || !*modelname) {
		ice->eeprom.subvendor = 0;
		if ((inb(ICEREG1724(ice, I2C_CTRL)) & VT1724_I2C_EEPROM) != 0)
			ice->eeprom.subvendor =
				(snd_vt1724_read_i2c(ice, dev, 0x00) << 0) |
				(snd_vt1724_read_i2c(ice, dev, 0x01) << 8) |
				(snd_vt1724_read_i2c(ice, dev, 0x02) << 16) |
				(snd_vt1724_read_i2c(ice, dev, 0x03) << 24);
		if (ice->eeprom.subvendor == 0 ||
		    ice->eeprom.subvendor == (unsigned int)-1) {
			/* invalid subvendor from EEPROM, try the PCI
			 * subststem ID instead
			 */
			u16 vendor, device;
			pci_read_config_word(ice->pci, PCI_SUBSYSTEM_VENDOR_ID,
					     &vendor);
			pci_read_config_word(ice->pci, PCI_SUBSYSTEM_ID, &device);
			ice->eeprom.subvendor =
				((unsigned int)swab16(vendor) << 16) | swab16(device);
			if (ice->eeprom.subvendor == 0 ||
			    ice->eeprom.subvendor == (unsigned int)-1) {
				dev_err(ice->card->dev,
					"No valid ID is found\n");
				return -ENXIO;
			}
		}
	}
	for (tbl = card_tables; *tbl; tbl++) {
		for (c = *tbl; c->name; c++) {
			if (modelname && c->model &&
			    !strcmp(modelname, c->model)) {
				dev_info(ice->card->dev,
					 "Using board model %s\n",
				       c->name);
				ice->eeprom.subvendor = c->subvendor;
			} else if (c->subvendor != ice->eeprom.subvendor)
				continue;
			ice->card_info = c;
			if (!c->eeprom_size || !c->eeprom_data)
				goto found;
			/* if the EEPROM is given by the driver, use it */
			dev_dbg(ice->card->dev, "using the defined eeprom..\n");
			ice->eeprom.version = 2;
			ice->eeprom.size = c->eeprom_size + 6;
			memcpy(ice->eeprom.data, c->eeprom_data, c->eeprom_size);
			goto read_skipped;
		}
	}
	dev_warn(ice->card->dev, "No matching model found for ID 0x%x\n",
	       ice->eeprom.subvendor);
#ifdef CONFIG_PM_SLEEP
	/* assume AC97-only card which can suspend without additional code */
	ice->pm_suspend_enabled = 1;
#endif

 found:
	ice->eeprom.size = snd_vt1724_read_i2c(ice, dev, 0x04);
	if (ice->eeprom.size < 6)
		ice->eeprom.size = 32;
	else if (ice->eeprom.size > 32) {
		dev_err(ice->card->dev, "Invalid EEPROM (size = %i)\n",
		       ice->eeprom.size);
		return -EIO;
	}
	ice->eeprom.version = snd_vt1724_read_i2c(ice, dev, 0x05);
	if (ice->eeprom.version != 1 && ice->eeprom.version != 2)
		dev_warn(ice->card->dev, "Invalid EEPROM version %i\n",
		       ice->eeprom.version);
	size = ice->eeprom.size - 6;
	for (i = 0; i < size; i++)
		ice->eeprom.data[i] = snd_vt1724_read_i2c(ice, dev, i + 6);

 read_skipped:
	ice->eeprom.gpiomask = eeprom_triple(ice, ICE_EEP2_GPIO_MASK);
	ice->eeprom.gpiostate = eeprom_triple(ice, ICE_EEP2_GPIO_STATE);
	ice->eeprom.gpiodir = eeprom_triple(ice, ICE_EEP2_GPIO_DIR);

	return 0;
}



static void snd_vt1724_chip_reset(struct snd_ice1712 *ice)
{
	outb(VT1724_RESET , ICEREG1724(ice, CONTROL));
	inb(ICEREG1724(ice, CONTROL)); /* pci posting flush */
	msleep(10);
	outb(0, ICEREG1724(ice, CONTROL));
	inb(ICEREG1724(ice, CONTROL)); /* pci posting flush */
	msleep(10);
}

static int snd_vt1724_chip_init(struct snd_ice1712 *ice)
{
	outb(ice->eeprom.data[ICE_EEP2_SYSCONF], ICEREG1724(ice, SYS_CFG));
	outb(ice->eeprom.data[ICE_EEP2_ACLINK], ICEREG1724(ice, AC97_CFG));
	outb(ice->eeprom.data[ICE_EEP2_I2S], ICEREG1724(ice, I2S_FEATURES));
	outb(ice->eeprom.data[ICE_EEP2_SPDIF], ICEREG1724(ice, SPDIF_CFG));

	ice->gpio.write_mask = ice->eeprom.gpiomask;
	ice->gpio.direction = ice->eeprom.gpiodir;
	snd_vt1724_set_gpio_mask(ice, ice->eeprom.gpiomask);
	snd_vt1724_set_gpio_dir(ice, ice->eeprom.gpiodir);
	snd_vt1724_set_gpio_data(ice, ice->eeprom.gpiostate);

	outb(0, ICEREG1724(ice, POWERDOWN));

	/* MPU_RX and TX irq masks are cleared later dynamically */
	outb(VT1724_IRQ_MPU_RX | VT1724_IRQ_MPU_TX , ICEREG1724(ice, IRQMASK));

	/* don't handle FIFO overrun/underruns (just yet),
	 * since they cause machine lockups
	 */
	outb(VT1724_MULTI_FIFO_ERR, ICEMT1724(ice, DMA_INT_MASK));

	return 0;
}

static int snd_vt1724_spdif_build_controls(struct snd_ice1712 *ice)
{
	int err;
	struct snd_kcontrol *kctl;

	if (snd_BUG_ON(!ice->pcm))
		return -EIO;

	if (!ice->own_routing) {
		err = snd_ctl_add(ice->card,
			snd_ctl_new1(&snd_vt1724_mixer_pro_spdif_route, ice));
		if (err < 0)
			return err;
	}

	err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_vt1724_spdif_switch, ice));
	if (err < 0)
		return err;

	err = snd_ctl_add(ice->card, kctl = snd_ctl_new1(&snd_vt1724_spdif_default, ice));
	if (err < 0)
		return err;
	kctl->id.device = ice->pcm->device;
	err = snd_ctl_add(ice->card, kctl = snd_ctl_new1(&snd_vt1724_spdif_maskc, ice));
	if (err < 0)
		return err;
	kctl->id.device = ice->pcm->device;
	err = snd_ctl_add(ice->card, kctl = snd_ctl_new1(&snd_vt1724_spdif_maskp, ice));
	if (err < 0)
		return err;
	kctl->id.device = ice->pcm->device;
#if 0 /* use default only */
	err = snd_ctl_add(ice->card, kctl = snd_ctl_new1(&snd_vt1724_spdif_stream, ice));
	if (err < 0)
		return err;
	kctl->id.device = ice->pcm->device;
	ice->spdif.stream_ctl = kctl;
#endif
	return 0;
}


static int snd_vt1724_build_controls(struct snd_ice1712 *ice)
{
	int err;

	err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_vt1724_eeprom, ice));
	if (err < 0)
		return err;
	err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_vt1724_pro_internal_clock, ice));
	if (err < 0)
		return err;

	err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_vt1724_pro_rate_locking, ice));
	if (err < 0)
		return err;
	err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_vt1724_pro_rate_reset, ice));
	if (err < 0)
		return err;

	if (!ice->own_routing && ice->num_total_dacs > 0) {
		struct snd_kcontrol_new tmp = snd_vt1724_mixer_pro_analog_route;
		tmp.count = ice->num_total_dacs;
		if (ice->vt1720 && tmp.count > 2)
			tmp.count = 2;
		err = snd_ctl_add(ice->card, snd_ctl_new1(&tmp, ice));
		if (err < 0)
			return err;
	}

	return snd_ctl_add(ice->card,
			   snd_ctl_new1(&snd_vt1724_mixer_pro_peak, ice));
}

static int snd_vt1724_free(struct snd_ice1712 *ice)
{
	if (!ice->port)
		goto __hw_end;
	/* mask all interrupts */
	outb(0xff, ICEMT1724(ice, DMA_INT_MASK));
	outb(0xff, ICEREG1724(ice, IRQMASK));
	/* --- */
__hw_end:
	if (ice->irq >= 0)
		free_irq(ice->irq, ice);
	pci_release_regions(ice->pci);
	snd_ice1712_akm4xxx_free(ice);
	pci_disable_device(ice->pci);
	kfree(ice->spec);
	kfree(ice);
	return 0;
}

static int snd_vt1724_dev_free(struct snd_device *device)
{
	struct snd_ice1712 *ice = device->device_data;
	return snd_vt1724_free(ice);
}

static int snd_vt1724_create(struct snd_card *card,
			     struct pci_dev *pci,
			     const char *modelname,
			     struct snd_ice1712 **r_ice1712)
{
	struct snd_ice1712 *ice;
	int err;
	static struct snd_device_ops ops = {
		.dev_free =	snd_vt1724_dev_free,
	};

	*r_ice1712 = NULL;

	/* enable PCI device */
	err = pci_enable_device(pci);
	if (err < 0)
		return err;

	ice = kzalloc(sizeof(*ice), GFP_KERNEL);
	if (ice == NULL) {
		pci_disable_device(pci);
		return -ENOMEM;
	}
	ice->vt1724 = 1;
	spin_lock_init(&ice->reg_lock);
	mutex_init(&ice->gpio_mutex);
	mutex_init(&ice->open_mutex);
	mutex_init(&ice->i2c_mutex);
	ice->gpio.set_mask = snd_vt1724_set_gpio_mask;
	ice->gpio.get_mask = snd_vt1724_get_gpio_mask;
	ice->gpio.set_dir = snd_vt1724_set_gpio_dir;
	ice->gpio.get_dir = snd_vt1724_get_gpio_dir;
	ice->gpio.set_data = snd_vt1724_set_gpio_data;
	ice->gpio.get_data = snd_vt1724_get_gpio_data;
	ice->card = card;
	ice->pci = pci;
	ice->irq = -1;
	pci_set_master(pci);
	snd_vt1724_proc_init(ice);
	synchronize_irq(pci->irq);

	card->private_data = ice;

	err = pci_request_regions(pci, "ICE1724");
	if (err < 0) {
		kfree(ice);
		pci_disable_device(pci);
		return err;
	}
	ice->port = pci_resource_start(pci, 0);
	ice->profi_port = pci_resource_start(pci, 1);

	if (request_irq(pci->irq, snd_vt1724_interrupt,
			IRQF_SHARED, KBUILD_MODNAME, ice)) {
		dev_err(card->dev, "unable to grab IRQ %d\n", pci->irq);
		snd_vt1724_free(ice);
		return -EIO;
	}

	ice->irq = pci->irq;

	snd_vt1724_chip_reset(ice);
	if (snd_vt1724_read_eeprom(ice, modelname) < 0) {
		snd_vt1724_free(ice);
		return -EIO;
	}
	if (snd_vt1724_chip_init(ice) < 0) {
		snd_vt1724_free(ice);
		return -EIO;
	}

	err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, ice, &ops);
	if (err < 0) {
		snd_vt1724_free(ice);
		return err;
	}

	*r_ice1712 = ice;
	return 0;
}


/*
 *
 * Registration
 *
 */

static int snd_vt1724_probe(struct pci_dev *pci,
			    const struct pci_device_id *pci_id)
{
	static int dev;
	struct snd_card *card;
	struct snd_ice1712 *ice;
	int pcm_dev = 0, err;
	struct snd_ice1712_card_info * const *tbl, *c;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[dev]) {
		dev++;
		return -ENOENT;
	}

	err = snd_card_new(&pci->dev, index[dev], id[dev], THIS_MODULE,
			   0, &card);
	if (err < 0)
		return err;

	strcpy(card->driver, "ICE1724");
	strcpy(card->shortname, "ICEnsemble ICE1724");

	err = snd_vt1724_create(card, pci, model[dev], &ice);
	if (err < 0) {
		snd_card_free(card);
		return err;
	}

	/* field init before calling chip_init */
	ice->ext_clock_count = 0;

	for (tbl = card_tables; *tbl; tbl++) {
		for (c = *tbl; c->name; c++) {
			if ((model[dev] && c->model &&
			     !strcmp(model[dev], c->model)) ||
			    (c->subvendor == ice->eeprom.subvendor)) {
				strcpy(card->shortname, c->name);
				if (c->driver) /* specific driver? */
					strcpy(card->driver, c->driver);
				if (c->chip_init) {
					err = c->chip_init(ice);
					if (err < 0) {
						snd_card_free(card);
						return err;
					}
				}
				goto __found;
			}
		}
	}
	c = &no_matched;
__found:
	/*
	* VT1724 has separate DMAs for the analog and the SPDIF streams while
	* ICE1712 has only one for both (mixed up).
	*
	* Confusingly the analog PCM is named "professional" here because it
	* was called so in ice1712 driver, and vt1724 driver is derived from
	* ice1712 driver.
	*/
	ice->pro_rate_default = PRO_RATE_DEFAULT;
	if (!ice->is_spdif_master)
		ice->is_spdif_master = stdclock_is_spdif_master;
	if (!ice->get_rate)
		ice->get_rate = stdclock_get_rate;
	if (!ice->set_rate)
		ice->set_rate = stdclock_set_rate;
	if (!ice->set_mclk)
		ice->set_mclk = stdclock_set_mclk;
	if (!ice->set_spdif_clock)
		ice->set_spdif_clock = stdclock_set_spdif_clock;
	if (!ice->get_spdif_master_type)
		ice->get_spdif_master_type = stdclock_get_spdif_master_type;
	if (!ice->ext_clock_names)
		ice->ext_clock_names = ext_clock_names;
	if (!ice->ext_clock_count)
		ice->ext_clock_count = ARRAY_SIZE(ext_clock_names);

	if (!ice->hw_rates)
		set_std_hw_rates(ice);

	err = snd_vt1724_pcm_profi(ice, pcm_dev++);
	if (err < 0) {
		snd_card_free(card);
		return err;
	}

	err = snd_vt1724_pcm_spdif(ice, pcm_dev++);
	if (err < 0) {
		snd_card_free(card);
		return err;
	}

	err = snd_vt1724_pcm_indep(ice, pcm_dev++);
	if (err < 0) {
		snd_card_free(card);
		return err;
	}

	err = snd_vt1724_ac97_mixer(ice);
	if (err < 0) {
		snd_card_free(card);
		return err;
	}

	err = snd_vt1724_build_controls(ice);
	if (err < 0) {
		snd_card_free(card);
		return err;
	}

	if (ice->pcm && ice->has_spdif) { /* has SPDIF I/O */
		err = snd_vt1724_spdif_build_controls(ice);
		if (err < 0) {
			snd_card_free(card);
			return err;
		}
	}

	if (c->build_controls) {
		err = c->build_controls(ice);
		if (err < 0) {
			snd_card_free(card);
			return err;
		}
	}

	if (!c->no_mpu401) {
		if (ice->eeprom.data[ICE_EEP2_SYSCONF] & VT1724_CFG_MPU401) {
			struct snd_rawmidi *rmidi;

			err = snd_rawmidi_new(card, "MIDI", 0, 1, 1, &rmidi);
			if (err < 0) {
				snd_card_free(card);
				return err;
			}
			ice->rmidi[0] = rmidi;
			rmidi->private_data = ice;
			strcpy(rmidi->name, "ICE1724 MIDI");
			rmidi->info_flags = SNDRV_RAWMIDI_INFO_OUTPUT |
					    SNDRV_RAWMIDI_INFO_INPUT |
					    SNDRV_RAWMIDI_INFO_DUPLEX;
			snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT,
					    &vt1724_midi_output_ops);
			snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT,
					    &vt1724_midi_input_ops);

			/* set watermarks */
			outb(VT1724_MPU_RX_FIFO | 0x1,
			     ICEREG1724(ice, MPU_FIFO_WM));
			outb(0x1, ICEREG1724(ice, MPU_FIFO_WM));
			/* set UART mode */
			outb(VT1724_MPU_UART, ICEREG1724(ice, MPU_CTRL));
		}
	}

	sprintf(card->longname, "%s at 0x%lx, irq %i",
		card->shortname, ice->port, ice->irq);

	err = snd_card_register(card);
	if (err < 0) {
		snd_card_free(card);
		return err;
	}
	pci_set_drvdata(pci, card);
	dev++;
	return 0;
}

static void snd_vt1724_remove(struct pci_dev *pci)
{
	struct snd_card *card = pci_get_drvdata(pci);
	struct snd_ice1712 *ice = card->private_data;

	if (ice->card_info && ice->card_info->chip_exit)
		ice->card_info->chip_exit(ice);
	snd_card_free(card);
}

#ifdef CONFIG_PM_SLEEP
static int snd_vt1724_suspend(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct snd_ice1712 *ice = card->private_data;

	if (!ice->pm_suspend_enabled)
		return 0;

	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);

	snd_pcm_suspend_all(ice->pcm);
	snd_pcm_suspend_all(ice->pcm_pro);
	snd_pcm_suspend_all(ice->pcm_ds);
	snd_ac97_suspend(ice->ac97);

	spin_lock_irq(&ice->reg_lock);
	ice->pm_saved_is_spdif_master = ice->is_spdif_master(ice);
	ice->pm_saved_spdif_ctrl = inw(ICEMT1724(ice, SPDIF_CTRL));
	ice->pm_saved_spdif_cfg = inb(ICEREG1724(ice, SPDIF_CFG));
	ice->pm_saved_route = inl(ICEMT1724(ice, ROUTE_PLAYBACK));
	spin_unlock_irq(&ice->reg_lock);

	if (ice->pm_suspend)
		ice->pm_suspend(ice);
	return 0;
}

static int snd_vt1724_resume(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct snd_ice1712 *ice = card->private_data;

	if (!ice->pm_suspend_enabled)
		return 0;

	snd_vt1724_chip_reset(ice);

	if (snd_vt1724_chip_init(ice) < 0) {
		snd_card_disconnect(card);
		return -EIO;
	}

	if (ice->pm_resume)
		ice->pm_resume(ice);

	if (ice->pm_saved_is_spdif_master) {
		/* switching to external clock via SPDIF */
		ice->set_spdif_clock(ice, 0);
	} else {
		/* internal on-card clock */
		int rate;
		if (ice->cur_rate)
			rate = ice->cur_rate;
		else
			rate = ice->pro_rate_default;
		snd_vt1724_set_pro_rate(ice, rate, 1);
	}

	update_spdif_bits(ice, ice->pm_saved_spdif_ctrl);

	outb(ice->pm_saved_spdif_cfg, ICEREG1724(ice, SPDIF_CFG));
	outl(ice->pm_saved_route, ICEMT1724(ice, ROUTE_PLAYBACK));

	snd_ac97_resume(ice->ac97);

	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	return 0;
}

static SIMPLE_DEV_PM_OPS(snd_vt1724_pm, snd_vt1724_suspend, snd_vt1724_resume);
#define SND_VT1724_PM_OPS	&snd_vt1724_pm
#else
#define SND_VT1724_PM_OPS	NULL
#endif /* CONFIG_PM_SLEEP */

static struct pci_driver vt1724_driver = {
	.name = KBUILD_MODNAME,
	.id_table = snd_vt1724_ids,
	.probe = snd_vt1724_probe,
	.remove = snd_vt1724_remove,
	.driver = {
		.pm = SND_VT1724_PM_OPS,
	},
};

module_pci_driver(vt1724_driver);
