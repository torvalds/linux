// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Low-level ALSA driver for the ENSONIQ SoundScape
 *   Copyright (c) by Chris Rankin
 *
 *   This driver was written in part using information obtained from
 *   the OSS/Free SoundScape driver, written by Hannu Savolainen.
 */

#include <linux/init.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/isa.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/pnp.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <asm/dma.h>
#include <sound/core.h>
#include <sound/wss.h>
#include <sound/mpu401.h>
#include <sound/initval.h>


MODULE_AUTHOR("Chris Rankin");
MODULE_DESCRIPTION("ENSONIQ SoundScape driver");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE("sndscape.co0");
MODULE_FIRMWARE("sndscape.co1");
MODULE_FIRMWARE("sndscape.co2");
MODULE_FIRMWARE("sndscape.co3");
MODULE_FIRMWARE("sndscape.co4");
MODULE_FIRMWARE("scope.cod");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
static char* id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;
static long port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;
static long wss_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;
static int irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;
static int mpu_irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;
static int dma[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;
static int dma2[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;
static bool joystick[SNDRV_CARDS];

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index number for SoundScape soundcard");

module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "Description for SoundScape card");

module_param_hw_array(port, long, ioport, NULL, 0444);
MODULE_PARM_DESC(port, "Port # for SoundScape driver.");

module_param_hw_array(wss_port, long, ioport, NULL, 0444);
MODULE_PARM_DESC(wss_port, "WSS Port # for SoundScape driver.");

module_param_hw_array(irq, int, irq, NULL, 0444);
MODULE_PARM_DESC(irq, "IRQ # for SoundScape driver.");

module_param_hw_array(mpu_irq, int, irq, NULL, 0444);
MODULE_PARM_DESC(mpu_irq, "MPU401 IRQ # for SoundScape driver.");

module_param_hw_array(dma, int, dma, NULL, 0444);
MODULE_PARM_DESC(dma, "DMA # for SoundScape driver.");

module_param_hw_array(dma2, int, dma, NULL, 0444);
MODULE_PARM_DESC(dma2, "DMA2 # for SoundScape driver.");

module_param_array(joystick, bool, NULL, 0444);
MODULE_PARM_DESC(joystick, "Enable gameport.");

#ifdef CONFIG_PNP
static int isa_registered;
static int pnp_registered;

static const struct pnp_card_device_id sscape_pnpids[] = {
	{ .id = "ENS3081", .devs = { { "ENS0000" } } }, /* Soundscape PnP */
	{ .id = "ENS4081", .devs = { { "ENS1011" } } },	/* VIVO90 */
	{ .id = "" }	/* end */
};

MODULE_DEVICE_TABLE(pnp_card, sscape_pnpids);
#endif


#define HOST_CTRL_IO(i)  ((i) + 2)
#define HOST_DATA_IO(i)  ((i) + 3)
#define ODIE_ADDR_IO(i)  ((i) + 4)
#define ODIE_DATA_IO(i)  ((i) + 5)
#define CODEC_IO(i)      ((i) + 8)

#define IC_ODIE  1
#define IC_OPUS  2

#define RX_READY 0x01
#define TX_READY 0x02

#define CMD_ACK			0x80
#define CMD_SET_MIDI_VOL	0x84
#define CMD_GET_MIDI_VOL	0x85
#define CMD_XXX_MIDI_VOL	0x86
#define CMD_SET_EXTMIDI		0x8a
#define CMD_GET_EXTMIDI		0x8b
#define CMD_SET_MT32		0x8c
#define CMD_GET_MT32		0x8d

enum GA_REG {
	GA_INTSTAT_REG = 0,
	GA_INTENA_REG,
	GA_DMAA_REG,
	GA_DMAB_REG,
	GA_INTCFG_REG,
	GA_DMACFG_REG,
	GA_CDCFG_REG,
	GA_SMCFGA_REG,
	GA_SMCFGB_REG,
	GA_HMCTL_REG
};

#define DMA_8BIT  0x80


enum card_type {
	MEDIA_FX,	/* Sequoia S-1000 */
	SSCAPE,		/* Sequoia S-2000 */
	SSCAPE_PNP,
	SSCAPE_VIVO,
};

struct soundscape {
	spinlock_t lock;
	unsigned io_base;
	int ic_type;
	enum card_type type;
	struct resource *io_res;
	struct resource *wss_res;
	struct snd_wss *chip;

	unsigned char midi_vol;
	struct device *dev;
};

#define INVALID_IRQ  ((unsigned)-1)


static inline struct soundscape *get_card_soundscape(struct snd_card *c)
{
	return (struct soundscape *) (c->private_data);
}

/*
 * Allocates some kernel memory that we can use for DMA.
 * I think this means that the memory has to map to
 * contiguous pages of physical memory.
 */
static struct snd_dma_buffer *get_dmabuf(struct soundscape *s,
					 struct snd_dma_buffer *buf,
					 unsigned long size)
{
	if (buf) {
		if (snd_dma_alloc_pages_fallback(SNDRV_DMA_TYPE_DEV,
						 s->chip->card->dev,
						 size, buf) < 0) {
			dev_err(s->dev,
				"sscape: Failed to allocate %lu bytes for DMA\n",
				size);
			return NULL;
		}
	}

	return buf;
}

/*
 * Release the DMA-able kernel memory ...
 */
static void free_dmabuf(struct snd_dma_buffer *buf)
{
	if (buf && buf->area)
		snd_dma_free_pages(buf);
}

/*
 * This function writes to the SoundScape's control registers,
 * but doesn't do any locking. It's up to the caller to do that.
 * This is why this function is "unsafe" ...
 */
static inline void sscape_write_unsafe(unsigned io_base, enum GA_REG reg,
				       unsigned char val)
{
	outb(reg, ODIE_ADDR_IO(io_base));
	outb(val, ODIE_DATA_IO(io_base));
}

/*
 * Write to the SoundScape's control registers, and do the
 * necessary locking ...
 */
static void sscape_write(struct soundscape *s, enum GA_REG reg,
			 unsigned char val)
{
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	sscape_write_unsafe(s->io_base, reg, val);
	spin_unlock_irqrestore(&s->lock, flags);
}

/*
 * Read from the SoundScape's control registers, but leave any
 * locking to the caller. This is why the function is "unsafe" ...
 */
static inline unsigned char sscape_read_unsafe(unsigned io_base,
					       enum GA_REG reg)
{
	outb(reg, ODIE_ADDR_IO(io_base));
	return inb(ODIE_DATA_IO(io_base));
}

/*
 * Puts the SoundScape into "host" mode, as compared to "MIDI" mode
 */
static inline void set_host_mode_unsafe(unsigned io_base)
{
	outb(0x0, HOST_CTRL_IO(io_base));
}

/*
 * Puts the SoundScape into "MIDI" mode, as compared to "host" mode
 */
static inline void set_midi_mode_unsafe(unsigned io_base)
{
	outb(0x3, HOST_CTRL_IO(io_base));
}

/*
 * Read the SoundScape's host-mode control register, but leave
 * any locking issues to the caller ...
 */
static inline int host_read_unsafe(unsigned io_base)
{
	int data = -1;
	if ((inb(HOST_CTRL_IO(io_base)) & RX_READY) != 0)
		data = inb(HOST_DATA_IO(io_base));

	return data;
}

/*
 * Read the SoundScape's host-mode control register, performing
 * a limited amount of busy-waiting if the register isn't ready.
 * Also leaves all locking-issues to the caller ...
 */
static int host_read_ctrl_unsafe(unsigned io_base, unsigned timeout)
{
	int data;

	while (((data = host_read_unsafe(io_base)) < 0) && (timeout != 0)) {
		udelay(100);
		--timeout;
	} /* while */

	return data;
}

/*
 * Write to the SoundScape's host-mode control registers, but
 * leave any locking issues to the caller ...
 */
static inline int host_write_unsafe(unsigned io_base, unsigned char data)
{
	if ((inb(HOST_CTRL_IO(io_base)) & TX_READY) != 0) {
		outb(data, HOST_DATA_IO(io_base));
		return 1;
	}

	return 0;
}

/*
 * Write to the SoundScape's host-mode control registers, performing
 * a limited amount of busy-waiting if the register isn't ready.
 * Also leaves all locking-issues to the caller ...
 */
static int host_write_ctrl_unsafe(unsigned io_base, unsigned char data,
				  unsigned timeout)
{
	int err;

	while (!(err = host_write_unsafe(io_base, data)) && (timeout != 0)) {
		udelay(100);
		--timeout;
	} /* while */

	return err;
}


/*
 * Check that the MIDI subsystem is operational. If it isn't,
 * then we will hang the computer if we try to use it ...
 *
 * NOTE: This check is based upon observation, not documentation.
 */
static inline int verify_mpu401(const struct snd_mpu401 *mpu)
{
	return ((inb(MPU401C(mpu)) & 0xc0) == 0x80);
}

/*
 * This is apparently the standard way to initialise an MPU-401
 */
static inline void initialise_mpu401(const struct snd_mpu401 *mpu)
{
	outb(0, MPU401D(mpu));
}

/*
 * Tell the SoundScape to activate the AD1845 chip (I think).
 * The AD1845 detection fails if we *don't* do this, so I
 * think that this is a good idea ...
 */
static void activate_ad1845_unsafe(unsigned io_base)
{
	unsigned char val = sscape_read_unsafe(io_base, GA_HMCTL_REG);
	sscape_write_unsafe(io_base, GA_HMCTL_REG, (val & 0xcf) | 0x10);
	sscape_write_unsafe(io_base, GA_CDCFG_REG, 0x80);
}

/*
 * Tell the SoundScape to begin a DMA transfer using the given channel.
 * All locking issues are left to the caller.
 */
static void sscape_start_dma_unsafe(unsigned io_base, enum GA_REG reg)
{
	sscape_write_unsafe(io_base, reg,
			    sscape_read_unsafe(io_base, reg) | 0x01);
	sscape_write_unsafe(io_base, reg,
			    sscape_read_unsafe(io_base, reg) & 0xfe);
}

/*
 * Wait for a DMA transfer to complete. This is a "limited busy-wait",
 * and all locking issues are left to the caller.
 */
static int sscape_wait_dma_unsafe(unsigned io_base, enum GA_REG reg,
				  unsigned timeout)
{
	while (!(sscape_read_unsafe(io_base, reg) & 0x01) && (timeout != 0)) {
		udelay(100);
		--timeout;
	} /* while */

	return sscape_read_unsafe(io_base, reg) & 0x01;
}

/*
 * Wait for the On-Board Processor to return its start-up
 * acknowledgement sequence. This wait is too long for
 * us to perform "busy-waiting", and so we must sleep.
 * This in turn means that we must not be holding any
 * spinlocks when we call this function.
 */
static int obp_startup_ack(struct soundscape *s, unsigned timeout)
{
	unsigned long end_time = jiffies + msecs_to_jiffies(timeout);

	do {
		unsigned long flags;
		int x;

		spin_lock_irqsave(&s->lock, flags);
		x = host_read_unsafe(s->io_base);
		spin_unlock_irqrestore(&s->lock, flags);
		if (x == 0xfe || x == 0xff)
			return 1;

		msleep(10);
	} while (time_before(jiffies, end_time));

	return 0;
}

/*
 * Wait for the host to return its start-up acknowledgement
 * sequence. This wait is too long for us to perform
 * "busy-waiting", and so we must sleep. This in turn means
 * that we must not be holding any spinlocks when we call
 * this function.
 */
static int host_startup_ack(struct soundscape *s, unsigned timeout)
{
	unsigned long end_time = jiffies + msecs_to_jiffies(timeout);

	do {
		unsigned long flags;
		int x;

		spin_lock_irqsave(&s->lock, flags);
		x = host_read_unsafe(s->io_base);
		spin_unlock_irqrestore(&s->lock, flags);
		if (x == 0xfe)
			return 1;

		msleep(10);
	} while (time_before(jiffies, end_time));

	return 0;
}

/*
 * Upload a byte-stream into the SoundScape using DMA channel A.
 */
static int upload_dma_data(struct soundscape *s, const unsigned char *data,
			   size_t size)
{
	unsigned long flags;
	struct snd_dma_buffer dma;
	int ret;
	unsigned char val;

	if (!get_dmabuf(s, &dma, PAGE_ALIGN(32 * 1024)))
		return -ENOMEM;

	spin_lock_irqsave(&s->lock, flags);

	/*
	 * Reset the board ...
	 */
	val = sscape_read_unsafe(s->io_base, GA_HMCTL_REG);
	sscape_write_unsafe(s->io_base, GA_HMCTL_REG, val & 0x3f);

	/*
	 * Enable the DMA channels and configure them ...
	 */
	val = (s->chip->dma1 << 4) | DMA_8BIT;
	sscape_write_unsafe(s->io_base, GA_DMAA_REG, val);
	sscape_write_unsafe(s->io_base, GA_DMAB_REG, 0x20);

	/*
	 * Take the board out of reset ...
	 */
	val = sscape_read_unsafe(s->io_base, GA_HMCTL_REG);
	sscape_write_unsafe(s->io_base, GA_HMCTL_REG, val | 0x80);

	/*
	 * Upload the firmware to the SoundScape
	 * board through the DMA channel ...
	 */
	while (size != 0) {
		unsigned long len;

		len = min(size, dma.bytes);
		memcpy(dma.area, data, len);
		data += len;
		size -= len;

		snd_dma_program(s->chip->dma1, dma.addr, len, DMA_MODE_WRITE);
		sscape_start_dma_unsafe(s->io_base, GA_DMAA_REG);
		if (!sscape_wait_dma_unsafe(s->io_base, GA_DMAA_REG, 5000)) {
			/*
			 * Don't forget to release this spinlock we're holding
			 */
			spin_unlock_irqrestore(&s->lock, flags);

			dev_err(s->dev, "sscape: DMA upload has timed out\n");
			ret = -EAGAIN;
			goto _release_dma;
		}
	} /* while */

	set_host_mode_unsafe(s->io_base);
	outb(0x0, s->io_base);

	/*
	 * Boot the board ... (I think)
	 */
	val = sscape_read_unsafe(s->io_base, GA_HMCTL_REG);
	sscape_write_unsafe(s->io_base, GA_HMCTL_REG, val | 0x40);
	spin_unlock_irqrestore(&s->lock, flags);

	/*
	 * If all has gone well, then the board should acknowledge
	 * the new upload and tell us that it has rebooted OK. We
	 * give it 5 seconds (max) ...
	 */
	ret = 0;
	if (!obp_startup_ack(s, 5000)) {
		dev_err(s->dev,
			"sscape: No response from on-board processor after upload\n");
		ret = -EAGAIN;
	} else if (!host_startup_ack(s, 5000)) {
		dev_err(s->dev, "sscape: SoundScape failed to initialise\n");
		ret = -EAGAIN;
	}

_release_dma:
	/*
	 * NOTE!!! We are NOT holding any spinlocks at this point !!!
	 */
	sscape_write(s, GA_DMAA_REG, (s->ic_type == IC_OPUS ? 0x40 : 0x70));
	free_dmabuf(&dma);

	return ret;
}

/*
 * Upload the bootblock(?) into the SoundScape. The only
 * purpose of this block of code seems to be to tell
 * us which version of the microcode we should be using.
 */
static int sscape_upload_bootblock(struct snd_card *card)
{
	struct soundscape *sscape = get_card_soundscape(card);
	unsigned long flags;
	const struct firmware *init_fw = NULL;
	int data = 0;
	int ret;

	ret = request_firmware(&init_fw, "scope.cod", card->dev);
	if (ret < 0) {
		dev_err(card->dev, "sscape: Error loading scope.cod");
		return ret;
	}
	ret = upload_dma_data(sscape, init_fw->data, init_fw->size);

	release_firmware(init_fw);

	spin_lock_irqsave(&sscape->lock, flags);
	if (ret == 0)
		data = host_read_ctrl_unsafe(sscape->io_base, 100);

	if (data & 0x10)
		sscape_write_unsafe(sscape->io_base, GA_SMCFGA_REG, 0x2f);

	spin_unlock_irqrestore(&sscape->lock, flags);

	data &= 0xf;
	if (ret == 0 && data > 7) {
		dev_err(card->dev,
			"sscape: timeout reading firmware version\n");
		ret = -EAGAIN;
	}

	return (ret == 0) ? data : ret;
}

/*
 * Upload the microcode into the SoundScape.
 */
static int sscape_upload_microcode(struct snd_card *card, int version)
{
	struct soundscape *sscape = get_card_soundscape(card);
	const struct firmware *init_fw = NULL;
	char name[14];
	int err;

	scnprintf(name, sizeof(name), "sndscape.co%d", version);

	err = request_firmware(&init_fw, name, card->dev);
	if (err < 0) {
		dev_err(card->dev, "sscape: Error loading sndscape.co%d",
			version);
		return err;
	}
	err = upload_dma_data(sscape, init_fw->data, init_fw->size);
	if (err == 0)
		dev_info(card->dev, "sscape: MIDI firmware loaded %zu KBs\n",
			 init_fw->size >> 10);

	release_firmware(init_fw);

	return err;
}

/*
 * Mixer control for the SoundScape's MIDI device.
 */
static int sscape_midi_info(struct snd_kcontrol *ctl,
			    struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 127;
	return 0;
}

static int sscape_midi_get(struct snd_kcontrol *kctl,
			   struct snd_ctl_elem_value *uctl)
{
	struct snd_wss *chip = snd_kcontrol_chip(kctl);
	struct snd_card *card = chip->card;
	register struct soundscape *s = get_card_soundscape(card);
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	uctl->value.integer.value[0] = s->midi_vol;
	spin_unlock_irqrestore(&s->lock, flags);
	return 0;
}

static int sscape_midi_put(struct snd_kcontrol *kctl,
			   struct snd_ctl_elem_value *uctl)
{
	struct snd_wss *chip = snd_kcontrol_chip(kctl);
	struct snd_card *card = chip->card;
	struct soundscape *s = get_card_soundscape(card);
	unsigned long flags;
	int change;
	unsigned char new_val;

	spin_lock_irqsave(&s->lock, flags);

	new_val = uctl->value.integer.value[0] & 127;
	/*
	 * We need to put the board into HOST mode before we
	 * can send any volume-changing HOST commands ...
	 */
	set_host_mode_unsafe(s->io_base);

	/*
	 * To successfully change the MIDI volume setting, you seem to
	 * have to write a volume command, write the new volume value,
	 * and then perform another volume-related command. Perhaps the
	 * first command is an "open" and the second command is a "close"?
	 */
	if (s->midi_vol == new_val) {
		change = 0;
		goto __skip_change;
	}
	change = host_write_ctrl_unsafe(s->io_base, CMD_SET_MIDI_VOL, 100)
		 && host_write_ctrl_unsafe(s->io_base, new_val, 100)
		 && host_write_ctrl_unsafe(s->io_base, CMD_XXX_MIDI_VOL, 100)
		 && host_write_ctrl_unsafe(s->io_base, new_val, 100);
	s->midi_vol = new_val;
__skip_change:

	/*
	 * Take the board out of HOST mode and back into MIDI mode ...
	 */
	set_midi_mode_unsafe(s->io_base);

	spin_unlock_irqrestore(&s->lock, flags);
	return change;
}

static const struct snd_kcontrol_new midi_mixer_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "MIDI",
	.info = sscape_midi_info,
	.get = sscape_midi_get,
	.put = sscape_midi_put
};

/*
 * The SoundScape can use two IRQs from a possible set of four.
 * These IRQs are encoded as bit patterns so that they can be
 * written to the control registers.
 */
static unsigned get_irq_config(int sscape_type, int irq)
{
	static const int valid_irq[] = { 9, 5, 7, 10 };
	static const int old_irq[] = { 9, 7, 5, 15 };
	unsigned cfg;

	if (sscape_type == MEDIA_FX) {
		for (cfg = 0; cfg < ARRAY_SIZE(old_irq); ++cfg)
			if (irq == old_irq[cfg])
				return cfg;
	} else {
		for (cfg = 0; cfg < ARRAY_SIZE(valid_irq); ++cfg)
			if (irq == valid_irq[cfg])
				return cfg;
	}

	return INVALID_IRQ;
}

/*
 * Perform certain arcane port-checks to see whether there
 * is a SoundScape board lurking behind the given ports.
 */
static int detect_sscape(struct soundscape *s, long wss_io)
{
	unsigned long flags;
	unsigned d;
	int retval = 0;

	spin_lock_irqsave(&s->lock, flags);

	/*
	 * The following code is lifted from the original OSS driver,
	 * and as I don't have a datasheet I cannot really comment
	 * on what it is doing...
	 */
	if ((inb(HOST_CTRL_IO(s->io_base)) & 0x78) != 0)
		goto _done;

	d = inb(ODIE_ADDR_IO(s->io_base)) & 0xf0;
	if ((d & 0x80) != 0)
		goto _done;

	if (d == 0)
		s->ic_type = IC_ODIE;
	else if ((d & 0x60) != 0)
		s->ic_type = IC_OPUS;
	else
		goto _done;

	outb(0xfa, ODIE_ADDR_IO(s->io_base));
	if ((inb(ODIE_ADDR_IO(s->io_base)) & 0x9f) != 0x0a)
		goto _done;

	outb(0xfe, ODIE_ADDR_IO(s->io_base));
	if ((inb(ODIE_ADDR_IO(s->io_base)) & 0x9f) != 0x0e)
		goto _done;

	outb(0xfe, ODIE_ADDR_IO(s->io_base));
	d = inb(ODIE_DATA_IO(s->io_base));
	if (s->type != SSCAPE_VIVO && (d & 0x9f) != 0x0e)
		goto _done;

	if (s->ic_type == IC_OPUS)
		activate_ad1845_unsafe(s->io_base);

	if (s->type == SSCAPE_VIVO)
		wss_io += 4;

	d  = sscape_read_unsafe(s->io_base, GA_HMCTL_REG);
	sscape_write_unsafe(s->io_base, GA_HMCTL_REG, d | 0xc0);

	/* wait for WSS codec */
	for (d = 0; d < 500; d++) {
		if ((inb(wss_io) & 0x80) == 0)
			break;
		spin_unlock_irqrestore(&s->lock, flags);
		msleep(1);
		spin_lock_irqsave(&s->lock, flags);
	}

	if ((inb(wss_io) & 0x80) != 0)
		goto _done;

	if (inb(wss_io + 2) == 0xff)
		goto _done;

	d  = sscape_read_unsafe(s->io_base, GA_HMCTL_REG) & 0x3f;
	sscape_write_unsafe(s->io_base, GA_HMCTL_REG, d);

	if ((inb(wss_io) & 0x80) != 0)
		s->type = MEDIA_FX;

	d = sscape_read_unsafe(s->io_base, GA_HMCTL_REG);
	sscape_write_unsafe(s->io_base, GA_HMCTL_REG, d | 0xc0);
	/* wait for WSS codec */
	for (d = 0; d < 500; d++) {
		if ((inb(wss_io) & 0x80) == 0)
			break;
		spin_unlock_irqrestore(&s->lock, flags);
		msleep(1);
		spin_lock_irqsave(&s->lock, flags);
	}

	/*
	 * SoundScape successfully detected!
	 */
	retval = 1;

_done:
	spin_unlock_irqrestore(&s->lock, flags);
	return retval;
}

/*
 * ALSA callback function, called when attempting to open the MIDI device.
 * Check that the MIDI firmware has been loaded, because we don't want
 * to crash the machine. Also check that someone isn't using the hardware
 * IOCTL device.
 */
static int mpu401_open(struct snd_mpu401 *mpu)
{
	if (!verify_mpu401(mpu)) {
		dev_err(mpu->rmidi->card->dev,
			"sscape: MIDI disabled, please load firmware\n");
		return -ENODEV;
	}

	return 0;
}

/*
 * Initialise an MPU-401 subdevice for MIDI support on the SoundScape.
 */
static int create_mpu401(struct snd_card *card, int devnum,
			 unsigned long port, int irq)
{
	struct soundscape *sscape = get_card_soundscape(card);
	struct snd_rawmidi *rawmidi;
	int err;

	err = snd_mpu401_uart_new(card, devnum, MPU401_HW_MPU401, port,
				  MPU401_INFO_INTEGRATED, irq, &rawmidi);
	if (err == 0) {
		struct snd_mpu401 *mpu = rawmidi->private_data;
		mpu->open_input = mpu401_open;
		mpu->open_output = mpu401_open;
		mpu->private_data = sscape;

		initialise_mpu401(mpu);
	}

	return err;
}


/*
 * Create an AD1845 PCM subdevice on the SoundScape. The AD1845
 * is very much like a CS4231, with a few extra bits. We will
 * try to support at least some of the extra bits by overriding
 * some of the CS4231 callback.
 */
static int create_ad1845(struct snd_card *card, unsigned port,
			 int irq, int dma1, int dma2)
{
	register struct soundscape *sscape = get_card_soundscape(card);
	struct snd_wss *chip;
	int err;
	int codec_type = WSS_HW_DETECT;

	switch (sscape->type) {
	case MEDIA_FX:
	case SSCAPE:
		/*
		 * There are some freak examples of early Soundscape cards
		 * with CS4231 instead of AD1848/CS4248. Unfortunately, the
		 * CS4231 works only in CS4248 compatibility mode on
		 * these cards so force it.
		 */
		if (sscape->ic_type != IC_OPUS)
			codec_type = WSS_HW_AD1848;
		break;

	case SSCAPE_VIVO:
		port += 4;
		break;
	default:
		break;
	}

	err = snd_wss_create(card, port, -1, irq, dma1, dma2,
			     codec_type, WSS_HWSHARE_DMA1, &chip);
	if (!err) {
		unsigned long flags;

		if (sscape->type != SSCAPE_VIVO) {
			/*
			 * The input clock frequency on the SoundScape must
			 * be 14.31818 MHz, because we must set this register
			 * to get the playback to sound correct ...
			 */
			snd_wss_mce_up(chip);
			spin_lock_irqsave(&chip->reg_lock, flags);
			snd_wss_out(chip, AD1845_CLOCK, 0x20);
			spin_unlock_irqrestore(&chip->reg_lock, flags);
			snd_wss_mce_down(chip);

		}

		err = snd_wss_pcm(chip, 0);
		if (err < 0) {
			dev_err(card->dev,
				"sscape: No PCM device for AD1845 chip\n");
			goto _error;
		}

		err = snd_wss_mixer(chip);
		if (err < 0) {
			dev_err(card->dev,
				"sscape: No mixer device for AD1845 chip\n");
			goto _error;
		}
		if (chip->hardware != WSS_HW_AD1848) {
			err = snd_wss_timer(chip, 0);
			if (err < 0) {
				dev_err(card->dev,
					"sscape: No timer device for AD1845 chip\n");
				goto _error;
			}
		}

		if (sscape->type != SSCAPE_VIVO) {
			err = snd_ctl_add(card,
					  snd_ctl_new1(&midi_mixer_ctl, chip));
			if (err < 0) {
				dev_err(card->dev,
					"sscape: Could not create MIDI mixer control\n");
				goto _error;
			}
		}

		sscape->chip = chip;
	}

_error:
	return err;
}


/*
 * Create an ALSA soundcard entry for the SoundScape, using
 * the given list of port, IRQ and DMA resources.
 */
static int create_sscape(int dev, struct snd_card *card)
{
	struct soundscape *sscape = get_card_soundscape(card);
	unsigned dma_cfg;
	unsigned irq_cfg;
	unsigned mpu_irq_cfg;
	struct resource *io_res;
	struct resource *wss_res;
	unsigned long flags;
	int err;
	int val;
	const char *name;

	/*
	 * Grab IO ports that we will need to probe so that we
	 * can detect and control this hardware ...
	 */
	io_res = devm_request_region(card->dev, port[dev], 8, "SoundScape");
	if (!io_res) {
		dev_err(card->dev,
			"sscape: can't grab port 0x%lx\n", port[dev]);
		return -EBUSY;
	}
	wss_res = NULL;
	if (sscape->type == SSCAPE_VIVO) {
		wss_res = devm_request_region(card->dev, wss_port[dev], 4,
					      "SoundScape");
		if (!wss_res) {
			dev_err(card->dev, "sscape: can't grab port 0x%lx\n",
				wss_port[dev]);
			return -EBUSY;
		}
	}

	/*
	 * Grab one DMA channel ...
	 */
	err = snd_devm_request_dma(card->dev, dma[dev], "SoundScape");
	if (err < 0) {
		dev_err(card->dev, "sscape: can't grab DMA %d\n", dma[dev]);
		return err;
	}

	spin_lock_init(&sscape->lock);
	sscape->io_res = io_res;
	sscape->wss_res = wss_res;
	sscape->io_base = port[dev];

	if (!detect_sscape(sscape, wss_port[dev])) {
		dev_err(card->dev, "sscape: hardware not detected at 0x%x\n",
			sscape->io_base);
		return -ENODEV;
	}

	switch (sscape->type) {
	case MEDIA_FX:
		name = "MediaFX/SoundFX";
		break;
	case SSCAPE:
		name = "Soundscape";
		break;
	case SSCAPE_PNP:
		name = "Soundscape PnP";
		break;
	case SSCAPE_VIVO:
		name = "Soundscape VIVO";
		break;
	default:
		name = "unknown Soundscape";
		break;
	}

	dev_info(card->dev, "sscape: %s card detected at 0x%x, using IRQ %d, DMA %d\n",
		 name, sscape->io_base, irq[dev], dma[dev]);

	/*
	 * Check that the user didn't pass us garbage data ...
	 */
	irq_cfg = get_irq_config(sscape->type, irq[dev]);
	if (irq_cfg == INVALID_IRQ) {
		dev_err(card->dev, "sscape: Invalid IRQ %d\n", irq[dev]);
		return -ENXIO;
	}

	mpu_irq_cfg = get_irq_config(sscape->type, mpu_irq[dev]);
	if (mpu_irq_cfg == INVALID_IRQ) {
		dev_err(card->dev, "sscape: Invalid IRQ %d\n", mpu_irq[dev]);
		return -ENXIO;
	}

	/*
	 * Tell the on-board devices where their resources are (I think -
	 * I can't be sure without a datasheet ... So many magic values!)
	 */
	spin_lock_irqsave(&sscape->lock, flags);

	sscape_write_unsafe(sscape->io_base, GA_SMCFGA_REG, 0x2e);
	sscape_write_unsafe(sscape->io_base, GA_SMCFGB_REG, 0x00);

	/*
	 * Enable and configure the DMA channels ...
	 */
	sscape_write_unsafe(sscape->io_base, GA_DMACFG_REG, 0x50);
	dma_cfg = (sscape->ic_type == IC_OPUS ? 0x40 : 0x70);
	sscape_write_unsafe(sscape->io_base, GA_DMAA_REG, dma_cfg);
	sscape_write_unsafe(sscape->io_base, GA_DMAB_REG, 0x20);

	mpu_irq_cfg |= mpu_irq_cfg << 2;
	val = sscape_read_unsafe(sscape->io_base, GA_HMCTL_REG) & 0xF7;
	if (joystick[dev])
		val |= 8;
	sscape_write_unsafe(sscape->io_base, GA_HMCTL_REG, val | 0x10);
	sscape_write_unsafe(sscape->io_base, GA_INTCFG_REG, 0xf0 | mpu_irq_cfg);
	sscape_write_unsafe(sscape->io_base,
			    GA_CDCFG_REG, 0x09 | DMA_8BIT
			    | (dma[dev] << 4) | (irq_cfg << 1));
	/*
	 * Enable the master IRQ ...
	 */
	sscape_write_unsafe(sscape->io_base, GA_INTENA_REG, 0x80);

	spin_unlock_irqrestore(&sscape->lock, flags);

	/*
	 * We have now enabled the codec chip, and so we should
	 * detect the AD1845 device ...
	 */
	err = create_ad1845(card, wss_port[dev], irq[dev],
			    dma[dev], dma2[dev]);
	if (err < 0) {
		dev_err(card->dev,
			"sscape: No AD1845 device at 0x%lx, IRQ %d\n",
			wss_port[dev], irq[dev]);
		return err;
	}
	strcpy(card->driver, "SoundScape");
	strcpy(card->shortname, name);
	snprintf(card->longname, sizeof(card->longname),
		 "%s at 0x%lx, IRQ %d, DMA1 %d, DMA2 %d\n",
		 name, sscape->chip->port, sscape->chip->irq,
		 sscape->chip->dma1, sscape->chip->dma2);

#define MIDI_DEVNUM  0
	if (sscape->type != SSCAPE_VIVO) {
		err = sscape_upload_bootblock(card);
		if (err >= 0)
			err = sscape_upload_microcode(card, err);

		if (err == 0) {
			err = create_mpu401(card, MIDI_DEVNUM, port[dev],
					    mpu_irq[dev]);
			if (err < 0) {
				dev_err(card->dev,
					"sscape: Failed to create MPU-401 device at 0x%lx\n",
					port[dev]);
				return err;
			}

			/*
			 * Initialize mixer
			 */
			spin_lock_irqsave(&sscape->lock, flags);
			sscape->midi_vol = 0;
			host_write_ctrl_unsafe(sscape->io_base,
						CMD_SET_MIDI_VOL, 100);
			host_write_ctrl_unsafe(sscape->io_base,
						sscape->midi_vol, 100);
			host_write_ctrl_unsafe(sscape->io_base,
						CMD_XXX_MIDI_VOL, 100);
			host_write_ctrl_unsafe(sscape->io_base,
						sscape->midi_vol, 100);
			host_write_ctrl_unsafe(sscape->io_base,
						CMD_SET_EXTMIDI, 100);
			host_write_ctrl_unsafe(sscape->io_base,
						0, 100);
			host_write_ctrl_unsafe(sscape->io_base, CMD_ACK, 100);

			set_midi_mode_unsafe(sscape->io_base);
			spin_unlock_irqrestore(&sscape->lock, flags);
		}
	}

	return 0;
}


static int snd_sscape_match(struct device *pdev, unsigned int i)
{
	/*
	 * Make sure we were given ALL of the other parameters.
	 */
	if (port[i] == SNDRV_AUTO_PORT)
		return 0;

	if (irq[i] == SNDRV_AUTO_IRQ ||
	    mpu_irq[i] == SNDRV_AUTO_IRQ ||
	    dma[i] == SNDRV_AUTO_DMA) {
		dev_info(pdev,
			 "sscape: insufficient parameters, need IO, IRQ, MPU-IRQ and DMA\n");
		return 0;
	}

	return 1;
}

static int snd_sscape_probe(struct device *pdev, unsigned int dev)
{
	struct snd_card *card;
	struct soundscape *sscape;
	int ret;

	ret = snd_devm_card_new(pdev, index[dev], id[dev], THIS_MODULE,
				sizeof(struct soundscape), &card);
	if (ret < 0)
		return ret;

	sscape = get_card_soundscape(card);
	sscape->dev = pdev;
	sscape->type = SSCAPE;

	dma[dev] &= 0x03;

	ret = create_sscape(dev, card);
	if (ret < 0)
		return ret;

	ret = snd_card_register(card);
	if (ret < 0) {
		dev_err(pdev, "sscape: Failed to register sound card\n");
		return ret;
	}
	dev_set_drvdata(pdev, card);
	return 0;
}

#define DEV_NAME "sscape"

static struct isa_driver snd_sscape_driver = {
	.match		= snd_sscape_match,
	.probe		= snd_sscape_probe,
	/* FIXME: suspend/resume */
	.driver		= {
		.name	= DEV_NAME
	},
};

#ifdef CONFIG_PNP
static inline int get_next_autoindex(int i)
{
	while (i < SNDRV_CARDS && port[i] != SNDRV_AUTO_PORT)
		++i;
	return i;
}


static int sscape_pnp_detect(struct pnp_card_link *pcard,
			     const struct pnp_card_device_id *pid)
{
	static int idx = 0;
	struct pnp_dev *dev;
	struct snd_card *card;
	struct soundscape *sscape;
	int ret;

	/*
	 * Allow this function to fail *quietly* if all the ISA PnP
	 * devices were configured using module parameters instead.
	 */
	idx = get_next_autoindex(idx);
	if (idx >= SNDRV_CARDS)
		return -ENOSPC;

	/*
	 * Check that we still have room for another sound card ...
	 */
	dev = pnp_request_card_device(pcard, pid->devs[0].id, NULL);
	if (!dev)
		return -ENODEV;

	if (!pnp_is_active(dev)) {
		if (pnp_activate_dev(dev) < 0) {
			dev_info(&dev->dev, "sscape: device is inactive\n");
			return -EBUSY;
		}
	}

	/*
	 * Create a new ALSA sound card entry, in anticipation
	 * of detecting our hardware ...
	 */
	ret = snd_devm_card_new(&pcard->card->dev,
				index[idx], id[idx], THIS_MODULE,
				sizeof(struct soundscape), &card);
	if (ret < 0)
		return ret;

	sscape = get_card_soundscape(card);
	sscape->dev = card->dev;

	/*
	 * Identify card model ...
	 */
	if (!strncmp("ENS4081", pid->id, 7))
		sscape->type = SSCAPE_VIVO;
	else
		sscape->type = SSCAPE_PNP;

	/*
	 * Read the correct parameters off the ISA PnP bus ...
	 */
	port[idx] = pnp_port_start(dev, 0);
	irq[idx] = pnp_irq(dev, 0);
	mpu_irq[idx] = pnp_irq(dev, 1);
	dma[idx] = pnp_dma(dev, 0) & 0x03;
	if (sscape->type == SSCAPE_PNP) {
		dma2[idx] = dma[idx];
		wss_port[idx] = CODEC_IO(port[idx]);
	} else {
		wss_port[idx] = pnp_port_start(dev, 1);
		dma2[idx] = pnp_dma(dev, 1);
	}

	ret = create_sscape(idx, card);
	if (ret < 0)
		return ret;

	ret = snd_card_register(card);
	if (ret < 0) {
		dev_err(card->dev, "sscape: Failed to register sound card\n");
		return ret;
	}

	pnp_set_card_drvdata(pcard, card);
	++idx;
	return 0;
}

static struct pnp_card_driver sscape_pnpc_driver = {
	.flags = PNP_DRIVER_RES_DO_NOT_CHANGE,
	.name = "sscape",
	.id_table = sscape_pnpids,
	.probe = sscape_pnp_detect,
};

#endif /* CONFIG_PNP */

static int __init sscape_init(void)
{
	int err;

	err = isa_register_driver(&snd_sscape_driver, SNDRV_CARDS);
#ifdef CONFIG_PNP
	if (!err)
		isa_registered = 1;

	err = pnp_register_card_driver(&sscape_pnpc_driver);
	if (!err)
		pnp_registered = 1;

	if (isa_registered)
		err = 0;
#endif
	return err;
}

static void __exit sscape_exit(void)
{
#ifdef CONFIG_PNP
	if (pnp_registered)
		pnp_unregister_card_driver(&sscape_pnpc_driver);
	if (isa_registered)
#endif
		isa_unregister_driver(&snd_sscape_driver);
}

module_init(sscape_init);
module_exit(sscape_exit);
