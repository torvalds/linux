/*
 *   Low-level ALSA driver for the ENSONIQ SoundScape PnP
 *   Copyright (c) by Chris Rankin
 *
 *   This driver was written in part using information obtained from
 *   the OSS/Free SoundScape driver, written by Hannu Savolainen.
 *
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
 */

#include <sound/driver.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/pnp.h>
#include <linux/spinlock.h>
#include <linux/moduleparam.h>
#include <asm/dma.h>
#include <sound/core.h>
#include <sound/hwdep.h>
#include <sound/cs4231.h>
#include <sound/mpu401.h>
#include <sound/initval.h>

#include <sound/sscape_ioctl.h>


MODULE_AUTHOR("Chris Rankin");
MODULE_DESCRIPTION("ENSONIQ SoundScape PnP driver");
MODULE_LICENSE("GPL");

static int index[SNDRV_CARDS] __devinitdata = SNDRV_DEFAULT_IDX;
static char* id[SNDRV_CARDS] __devinitdata = SNDRV_DEFAULT_STR;
static long port[SNDRV_CARDS] __devinitdata = { [0 ... (SNDRV_CARDS-1)] = SNDRV_AUTO_PORT };
static int irq[SNDRV_CARDS] __devinitdata = SNDRV_DEFAULT_IRQ;
static int mpu_irq[SNDRV_CARDS] __devinitdata = SNDRV_DEFAULT_IRQ;
static int dma[SNDRV_CARDS] __devinitdata = SNDRV_DEFAULT_DMA;

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index number for SoundScape soundcard");

module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "Description for SoundScape card");

module_param_array(port, long, NULL, 0444);
MODULE_PARM_DESC(port, "Port # for SoundScape driver.");

module_param_array(irq, int, NULL, 0444);
MODULE_PARM_DESC(irq, "IRQ # for SoundScape driver.");

module_param_array(mpu_irq, int, NULL, 0444);
MODULE_PARM_DESC(mpu_irq, "MPU401 IRQ # for SoundScape driver.");

module_param_array(dma, int, NULL, 0444);
MODULE_PARM_DESC(dma, "DMA # for SoundScape driver.");

static struct platform_device *platform_devices[SNDRV_CARDS];
static int pnp_registered;
  
#ifdef CONFIG_PNP
static struct pnp_card_device_id sscape_pnpids[] = {
	{ .id = "ENS3081", .devs = { { "ENS0000" } } },
	{ .id = "" }	/* end */
};

MODULE_DEVICE_TABLE(pnp_card, sscape_pnpids);
#endif


#define MPU401_IO(i)     ((i) + 0)
#define MIDI_DATA_IO(i)  ((i) + 0)
#define MIDI_CTRL_IO(i)  ((i) + 1)
#define HOST_CTRL_IO(i)  ((i) + 2)
#define HOST_DATA_IO(i)  ((i) + 3)
#define ODIE_ADDR_IO(i)  ((i) + 4)
#define ODIE_DATA_IO(i)  ((i) + 5)
#define CODEC_IO(i)      ((i) + 8)

#define IC_ODIE  1
#define IC_OPUS  2

#define RX_READY 0x01
#define TX_READY 0x02

#define CMD_ACK           0x80
#define CMD_SET_MIDI_VOL  0x84
#define CMD_GET_MIDI_VOL  0x85
#define CMD_XXX_MIDI_VOL  0x86
#define CMD_SET_EXTMIDI   0x8a
#define CMD_GET_EXTMIDI   0x8b
#define CMD_SET_MT32      0x8c
#define CMD_GET_MT32      0x8d

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


#define AD1845_FREQ_SEL_MSB    0x16
#define AD1845_FREQ_SEL_LSB    0x17

struct soundscape {
	spinlock_t lock;
	unsigned io_base;
	int codec_type;
	int ic_type;
	struct resource *io_res;
	struct snd_cs4231 *chip;
	struct snd_mpu401 *mpu;
	struct snd_hwdep *hw;

	/*
	 * The MIDI device won't work until we've loaded
	 * its firmware via a hardware-dependent device IOCTL
	 */
	spinlock_t fwlock;
	int hw_in_use;
	unsigned long midi_usage;
	unsigned char midi_vol;
};

#define INVALID_IRQ  ((unsigned)-1)


static inline struct soundscape *get_card_soundscape(struct snd_card *c)
{
	return (struct soundscape *) (c->private_data);
}

static inline struct soundscape *get_mpu401_soundscape(struct snd_mpu401 * mpu)
{
	return (struct soundscape *) (mpu->private_data);
}

static inline struct soundscape *get_hwdep_soundscape(struct snd_hwdep * hw)
{
	return (struct soundscape *) (hw->private_data);
}


/*
 * Allocates some kernel memory that we can use for DMA.
 * I think this means that the memory has to map to
 * contiguous pages of physical memory.
 */
static struct snd_dma_buffer *get_dmabuf(struct snd_dma_buffer *buf, unsigned long size)
{
	if (buf) {
		if (snd_dma_alloc_pages_fallback(SNDRV_DMA_TYPE_DEV, snd_dma_isa_data(),
						 size, buf) < 0) {
			snd_printk(KERN_ERR "sscape: Failed to allocate %lu bytes for DMA\n", size);
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
static inline void sscape_write_unsafe(unsigned io_base, enum GA_REG reg, unsigned char val)
{
	outb(reg, ODIE_ADDR_IO(io_base));
	outb(val, ODIE_DATA_IO(io_base));
}

/*
 * Write to the SoundScape's control registers, and do the
 * necessary locking ...
 */
static void sscape_write(struct soundscape *s, enum GA_REG reg, unsigned char val)
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
static inline unsigned char sscape_read_unsafe(unsigned io_base, enum GA_REG reg)
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
	if ((inb(HOST_CTRL_IO(io_base)) & RX_READY) != 0) {
		data = inb(HOST_DATA_IO(io_base));
	}

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
static inline int verify_mpu401(const struct snd_mpu401 * mpu)
{
	return ((inb(MIDI_CTRL_IO(mpu->port)) & 0xc0) == 0x80);
}

/*
 * This is apparently the standard way to initailise an MPU-401
 */
static inline void initialise_mpu401(const struct snd_mpu401 * mpu)
{
	outb(0, MIDI_DATA_IO(mpu->port));
}

/*
 * Tell the SoundScape to activate the AD1845 chip (I think).
 * The AD1845 detection fails if we *don't* do this, so I
 * think that this is a good idea ...
 */
static inline void activate_ad1845_unsafe(unsigned io_base)
{
	sscape_write_unsafe(io_base, GA_HMCTL_REG, (sscape_read_unsafe(io_base, GA_HMCTL_REG) & 0xcf) | 0x10);
	sscape_write_unsafe(io_base, GA_CDCFG_REG, 0x80);
}

/*
 * Do the necessary ALSA-level cleanup to deallocate our driver ...
 */
static void soundscape_free(struct snd_card *c)
{
	register struct soundscape *sscape = get_card_soundscape(c);
	release_and_free_resource(sscape->io_res);
	free_dma(sscape->chip->dma1);
}

/*
 * Tell the SoundScape to begin a DMA tranfer using the given channel.
 * All locking issues are left to the caller.
 */
static inline void sscape_start_dma_unsafe(unsigned io_base, enum GA_REG reg)
{
	sscape_write_unsafe(io_base, reg, sscape_read_unsafe(io_base, reg) | 0x01);
	sscape_write_unsafe(io_base, reg, sscape_read_unsafe(io_base, reg) & 0xfe);
}

/*
 * Wait for a DMA transfer to complete. This is a "limited busy-wait",
 * and all locking issues are left to the caller.
 */
static int sscape_wait_dma_unsafe(unsigned io_base, enum GA_REG reg, unsigned timeout)
{
	while (!(sscape_read_unsafe(io_base, reg) & 0x01) && (timeout != 0)) {
		udelay(100);
		--timeout;
	} /* while */

	return (sscape_read_unsafe(io_base, reg) & 0x01);
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
	while (timeout != 0) {
		unsigned long flags;
		unsigned char x;

		schedule_timeout_interruptible(1);

		spin_lock_irqsave(&s->lock, flags);
		x = inb(HOST_DATA_IO(s->io_base));
		spin_unlock_irqrestore(&s->lock, flags);
		if ((x & 0xfe) == 0xfe)
			return 1;

		--timeout;
	} /* while */

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
	while (timeout != 0) {
		unsigned long flags;
		unsigned char x;

		schedule_timeout_interruptible(1);

		spin_lock_irqsave(&s->lock, flags);
		x = inb(HOST_DATA_IO(s->io_base));
		spin_unlock_irqrestore(&s->lock, flags);
		if (x == 0xfe)
			return 1;

		--timeout;
	} /* while */

	return 0;
}

/*
 * Upload a byte-stream into the SoundScape using DMA channel A.
 */
static int upload_dma_data(struct soundscape *s,
                           const unsigned char __user *data,
                           size_t size)
{
	unsigned long flags;
	struct snd_dma_buffer dma;
	int ret;

	if (!get_dmabuf(&dma, PAGE_ALIGN(size)))
		return -ENOMEM;

	spin_lock_irqsave(&s->lock, flags);

	/*
	 * Reset the board ...
	 */
	sscape_write_unsafe(s->io_base, GA_HMCTL_REG, sscape_read_unsafe(s->io_base, GA_HMCTL_REG) & 0x3f);

	/*
	 * Enable the DMA channels and configure them ...
	 */
	sscape_write_unsafe(s->io_base, GA_DMACFG_REG, 0x50);
	sscape_write_unsafe(s->io_base, GA_DMAA_REG, (s->chip->dma1 << 4) | DMA_8BIT);
	sscape_write_unsafe(s->io_base, GA_DMAB_REG, 0x20);

	/*
	 * Take the board out of reset ...
	 */
	sscape_write_unsafe(s->io_base, GA_HMCTL_REG, sscape_read_unsafe(s->io_base, GA_HMCTL_REG) | 0x80);

	/*
	 * Upload the user's data (firmware?) to the SoundScape
	 * board through the DMA channel ...
	 */
	while (size != 0) {
		unsigned long len;

		/*
		 * Apparently, copying to/from userspace can sleep.
		 * We are therefore forbidden from holding any
		 * spinlocks while we copy ...
		 */
		spin_unlock_irqrestore(&s->lock, flags);

		/*
		 * Remember that the data that we want to DMA
		 * comes from USERSPACE. We have already verified
		 * the userspace pointer ...
		 */
		len = min(size, dma.bytes);
		len -= __copy_from_user(dma.area, data, len);
		data += len;
		size -= len;

		/*
		 * Grab that spinlock again, now that we've
		 * finished copying!
		 */
		spin_lock_irqsave(&s->lock, flags);

		snd_dma_program(s->chip->dma1, dma.addr, len, DMA_MODE_WRITE);
		sscape_start_dma_unsafe(s->io_base, GA_DMAA_REG);
		if (!sscape_wait_dma_unsafe(s->io_base, GA_DMAA_REG, 5000)) {
			/*
			 * Don't forget to release this spinlock we're holding ...
			 */
			spin_unlock_irqrestore(&s->lock, flags);

			snd_printk(KERN_ERR "sscape: DMA upload has timed out\n");
			ret = -EAGAIN;
			goto _release_dma;
		}
	} /* while */

	set_host_mode_unsafe(s->io_base);

	/*
	 * Boot the board ... (I think)
	 */
	sscape_write_unsafe(s->io_base, GA_HMCTL_REG, sscape_read_unsafe(s->io_base, GA_HMCTL_REG) | 0x40);
	spin_unlock_irqrestore(&s->lock, flags);

	/*
	 * If all has gone well, then the board should acknowledge
	 * the new upload and tell us that it has rebooted OK. We
	 * give it 5 seconds (max) ...
	 */
	ret = 0;
	if (!obp_startup_ack(s, 5)) {
		snd_printk(KERN_ERR "sscape: No response from on-board processor after upload\n");
		ret = -EAGAIN;
	} else if (!host_startup_ack(s, 5)) {
		snd_printk(KERN_ERR "sscape: SoundScape failed to initialise\n");
		ret = -EAGAIN;
	}

	_release_dma:
	/*
	 * NOTE!!! We are NOT holding any spinlocks at this point !!!
	 */
	sscape_write(s, GA_DMAA_REG, (s->ic_type == IC_ODIE ? 0x70 : 0x40));
	free_dmabuf(&dma);

	return ret;
}

/*
 * Upload the bootblock(?) into the SoundScape. The only
 * purpose of this block of code seems to be to tell
 * us which version of the microcode we should be using.
 *
 * NOTE: The boot-block data resides in USER-SPACE!!!
 *       However, we have already verified its memory
 *       addresses by the time we get here.
 */
static int sscape_upload_bootblock(struct soundscape *sscape, struct sscape_bootblock __user *bb)
{
	unsigned long flags;
	int data = 0;
	int ret;

	ret = upload_dma_data(sscape, bb->code, sizeof(bb->code));

	spin_lock_irqsave(&sscape->lock, flags);
	if (ret == 0) {
		data = host_read_ctrl_unsafe(sscape->io_base, 100);
	}
	set_midi_mode_unsafe(sscape->io_base);
	spin_unlock_irqrestore(&sscape->lock, flags);

	if (ret == 0) {
		if (data < 0) {
			snd_printk(KERN_ERR "sscape: timeout reading firmware version\n");
			ret = -EAGAIN;
		}
		else if (__copy_to_user(&bb->version, &data, sizeof(bb->version))) {
			ret = -EFAULT;
		}
	}

	return ret;
}

/*
 * Upload the microcode into the SoundScape. The
 * microcode is 64K of data, and if we try to copy
 * it into a local variable then we will SMASH THE
 * KERNEL'S STACK! We therefore leave it in USER
 * SPACE, and save ourselves from copying it at all.
 */
static int sscape_upload_microcode(struct soundscape *sscape,
                                   const struct sscape_microcode __user *mc)
{
	unsigned long flags;
	char __user *code;
	int err;

	/*
	 * We are going to have to copy this data into a special
	 * DMA-able buffer before we can upload it. We shall therefore
	 * just check that the data pointer is valid for now.
	 *
	 * NOTE: This buffer is 64K long! That's WAY too big to
	 *       copy into a stack-temporary anyway.
	 */
	if ( get_user(code, &mc->code) ||
	     !access_ok(VERIFY_READ, code, SSCAPE_MICROCODE_SIZE) )
		return -EFAULT;

	if ((err = upload_dma_data(sscape, code, SSCAPE_MICROCODE_SIZE)) == 0) {
		snd_printk(KERN_INFO "sscape: MIDI firmware loaded\n");
	}

	spin_lock_irqsave(&sscape->lock, flags);
	set_midi_mode_unsafe(sscape->io_base);
	spin_unlock_irqrestore(&sscape->lock, flags);

	initialise_mpu401(sscape->mpu);

	return err;
}

/*
 * Hardware-specific device functions, to implement special
 * IOCTLs for the SoundScape card. This is how we upload
 * the microcode into the card, for example, and so we
 * must ensure that no two processes can open this device
 * simultaneously, and that we can't open it at all if
 * someone is using the MIDI device.
 */
static int sscape_hw_open(struct snd_hwdep * hw, struct file *file)
{
	register struct soundscape *sscape = get_hwdep_soundscape(hw);
	unsigned long flags;
	int err;

	spin_lock_irqsave(&sscape->fwlock, flags);

	if ((sscape->midi_usage != 0) || sscape->hw_in_use) {
		err = -EBUSY;
	} else {
		sscape->hw_in_use = 1;
		err = 0;
	}

	spin_unlock_irqrestore(&sscape->fwlock, flags);
	return err;
}

static int sscape_hw_release(struct snd_hwdep * hw, struct file *file)
{
	register struct soundscape *sscape = get_hwdep_soundscape(hw);
	unsigned long flags;

	spin_lock_irqsave(&sscape->fwlock, flags);
	sscape->hw_in_use = 0;
	spin_unlock_irqrestore(&sscape->fwlock, flags);
	return 0;
}

static int sscape_hw_ioctl(struct snd_hwdep * hw, struct file *file,
                           unsigned int cmd, unsigned long arg)
{
	struct soundscape *sscape = get_hwdep_soundscape(hw);
	int err = -EBUSY;

	switch (cmd) {
	case SND_SSCAPE_LOAD_BOOTB:
		{
			register struct sscape_bootblock __user *bb = (struct sscape_bootblock __user *) arg;

			/*
			 * We are going to have to copy this data into a special
			 * DMA-able buffer before we can upload it. We shall therefore
			 * just check that the data pointer is valid for now ...
			 */
			if ( !access_ok(VERIFY_READ, bb->code, sizeof(bb->code)) )
				return -EFAULT;

			/*
			 * Now check that we can write the firmware version number too...
			 */
			if ( !access_ok(VERIFY_WRITE, &bb->version, sizeof(bb->version)) )
				return -EFAULT;

			err = sscape_upload_bootblock(sscape, bb);
		}
		break;

	case SND_SSCAPE_LOAD_MCODE:
		{
			register const struct sscape_microcode __user *mc = (const struct sscape_microcode __user *) arg;

			err = sscape_upload_microcode(sscape, mc);
		}
		break;

	default:
		err = -EINVAL;
		break;
	} /* switch */

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
	struct snd_cs4231 *chip = snd_kcontrol_chip(kctl);
	struct snd_card *card = chip->card;
	register struct soundscape *s = get_card_soundscape(card);
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	set_host_mode_unsafe(s->io_base);

	if (host_write_ctrl_unsafe(s->io_base, CMD_GET_MIDI_VOL, 100)) {
		uctl->value.integer.value[0] = host_read_ctrl_unsafe(s->io_base, 100);
	}

	set_midi_mode_unsafe(s->io_base);
	spin_unlock_irqrestore(&s->lock, flags);
	return 0;
}

static int sscape_midi_put(struct snd_kcontrol *kctl,
                           struct snd_ctl_elem_value *uctl)
{
	struct snd_cs4231 *chip = snd_kcontrol_chip(kctl);
	struct snd_card *card = chip->card;
	register struct soundscape *s = get_card_soundscape(card);
	unsigned long flags;
	int change;

	spin_lock_irqsave(&s->lock, flags);

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
	if (s->midi_vol == ((unsigned char) uctl->value.integer. value[0] & 127)) {
		change = 0;
		goto __skip_change;
	}
	change = (host_write_ctrl_unsafe(s->io_base, CMD_SET_MIDI_VOL, 100)
	          && host_write_ctrl_unsafe(s->io_base, ((unsigned char) uctl->value.integer. value[0]) & 127, 100)
	          && host_write_ctrl_unsafe(s->io_base, CMD_XXX_MIDI_VOL, 100));
      __skip_change:

	/*
	 * Take the board out of HOST mode and back into MIDI mode ...
	 */
	set_midi_mode_unsafe(s->io_base);

	spin_unlock_irqrestore(&s->lock, flags);
	return change;
}

static struct snd_kcontrol_new midi_mixer_ctl = {
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
static unsigned __devinit get_irq_config(int irq)
{
	static const int valid_irq[] = { 9, 5, 7, 10 };
	unsigned cfg;

	for (cfg = 0; cfg < ARRAY_SIZE(valid_irq); ++cfg) {
		if (irq == valid_irq[cfg])
			return cfg;
	} /* for */

	return INVALID_IRQ;
}


/*
 * Perform certain arcane port-checks to see whether there
 * is a SoundScape board lurking behind the given ports.
 */
static int __devinit detect_sscape(struct soundscape *s)
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

	if (d == 0) {
		s->codec_type = 1;
		s->ic_type = IC_ODIE;
	} else if ((d & 0x60) != 0) {
		s->codec_type = 2;
		s->ic_type = IC_OPUS;
	} else
		goto _done;

	outb(0xfa, ODIE_ADDR_IO(s->io_base));
	if ((inb(ODIE_ADDR_IO(s->io_base)) & 0x9f) != 0x0a)
		goto _done;

	outb(0xfe, ODIE_ADDR_IO(s->io_base));
	if ((inb(ODIE_ADDR_IO(s->io_base)) & 0x9f) != 0x0e)
		goto _done;
	if ((inb(ODIE_DATA_IO(s->io_base)) & 0x9f) != 0x0e)
		goto _done;

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
static int mpu401_open(struct snd_mpu401 * mpu)
{
	int err;

	if (!verify_mpu401(mpu)) {
		snd_printk(KERN_ERR "sscape: MIDI disabled, please load firmware\n");
		err = -ENODEV;
	} else {
		register struct soundscape *sscape = get_mpu401_soundscape(mpu);
		unsigned long flags;

		spin_lock_irqsave(&sscape->fwlock, flags);

		if (sscape->hw_in_use || (sscape->midi_usage == ULONG_MAX)) {
			err = -EBUSY;
		} else {
			++(sscape->midi_usage);
			err = 0;
		}

		spin_unlock_irqrestore(&sscape->fwlock, flags);
	}

	return err;
}

static void mpu401_close(struct snd_mpu401 * mpu)
{
	register struct soundscape *sscape = get_mpu401_soundscape(mpu);
	unsigned long flags;

	spin_lock_irqsave(&sscape->fwlock, flags);
	--(sscape->midi_usage);
	spin_unlock_irqrestore(&sscape->fwlock, flags);
}

/*
 * Initialse an MPU-401 subdevice for MIDI support on the SoundScape.
 */
static int __devinit create_mpu401(struct snd_card *card, int devnum, unsigned long port, int irq)
{
	struct soundscape *sscape = get_card_soundscape(card);
	struct snd_rawmidi *rawmidi;
	int err;

#define MPU401_SHARE_HARDWARE  1
	if ((err = snd_mpu401_uart_new(card, devnum,
	                               MPU401_HW_MPU401,
	                               port, MPU401_SHARE_HARDWARE,
	                               irq, SA_INTERRUPT,
	                               &rawmidi)) == 0) {
		struct snd_mpu401 *mpu = (struct snd_mpu401 *) rawmidi->private_data;
		mpu->open_input = mpu401_open;
		mpu->open_output = mpu401_open;
		mpu->close_input = mpu401_close;
		mpu->close_output = mpu401_close;
		mpu->private_data = sscape;
		sscape->mpu = mpu;

		initialise_mpu401(mpu);
	}

	return err;
}


/*
 * Override for the CS4231 playback format function.
 * The AD1845 has much simpler format and rate selection.
 */
static void ad1845_playback_format(struct snd_cs4231 * chip, struct snd_pcm_hw_params *params, unsigned char format)
{
	unsigned long flags;
	unsigned rate = params_rate(params);

	/*
	 * The AD1845 can't handle sample frequencies
	 * outside of 4 kHZ to 50 kHZ
	 */
	if (rate > 50000)
		rate = 50000;
	else if (rate < 4000)
		rate = 4000;

	spin_lock_irqsave(&chip->reg_lock, flags);

	/*
	 * Program the AD1845 correctly for the playback stream.
	 * Note that we do NOT need to toggle the MCE bit because
	 * the PLAYBACK_ENABLE bit of the Interface Configuration
	 * register is set.
	 * 
	 * NOTE: We seem to need to write to the MSB before the LSB
	 *       to get the correct sample frequency.
	 */
	snd_cs4231_out(chip, CS4231_PLAYBK_FORMAT, (format & 0xf0));
	snd_cs4231_out(chip, AD1845_FREQ_SEL_MSB, (unsigned char) (rate >> 8));
	snd_cs4231_out(chip, AD1845_FREQ_SEL_LSB, (unsigned char) rate);

	spin_unlock_irqrestore(&chip->reg_lock, flags);
}

/*
 * Override for the CS4231 capture format function. 
 * The AD1845 has much simpler format and rate selection.
 */
static void ad1845_capture_format(struct snd_cs4231 * chip, struct snd_pcm_hw_params *params, unsigned char format)
{
	unsigned long flags;
	unsigned rate = params_rate(params);

	/*
	 * The AD1845 can't handle sample frequencies 
	 * outside of 4 kHZ to 50 kHZ
	 */
	if (rate > 50000)
		rate = 50000;
	else if (rate < 4000)
		rate = 4000;

	spin_lock_irqsave(&chip->reg_lock, flags);

	/*
	 * Program the AD1845 correctly for the playback stream.
	 * Note that we do NOT need to toggle the MCE bit because
	 * the CAPTURE_ENABLE bit of the Interface Configuration
	 * register is set.
	 *
	 * NOTE: We seem to need to write to the MSB before the LSB
	 *       to get the correct sample frequency.
	 */
	snd_cs4231_out(chip, CS4231_REC_FORMAT, (format & 0xf0));
	snd_cs4231_out(chip, AD1845_FREQ_SEL_MSB, (unsigned char) (rate >> 8));
	snd_cs4231_out(chip, AD1845_FREQ_SEL_LSB, (unsigned char) rate);

	spin_unlock_irqrestore(&chip->reg_lock, flags);
}

/*
 * Create an AD1845 PCM subdevice on the SoundScape. The AD1845
 * is very much like a CS4231, with a few extra bits. We will
 * try to support at least some of the extra bits by overriding
 * some of the CS4231 callback.
 */
static int __devinit create_ad1845(struct snd_card *card, unsigned port, int irq, int dma1)
{
	register struct soundscape *sscape = get_card_soundscape(card);
	struct snd_cs4231 *chip;
	int err;

#define CS4231_SHARE_HARDWARE  (CS4231_HWSHARE_DMA1 | CS4231_HWSHARE_DMA2)
	/*
	 * The AD1845 PCM device is only half-duplex, and so
	 * we only give it one DMA channel ...
	 */
	if ((err = snd_cs4231_create(card,
				     port, -1, irq, dma1, dma1,
				     CS4231_HW_DETECT,
				     CS4231_HWSHARE_DMA1, &chip)) == 0) {
		unsigned long flags;
		struct snd_pcm *pcm;

#define AD1845_FREQ_SEL_ENABLE  0x08

#define AD1845_PWR_DOWN_CTRL   0x1b
#define AD1845_CRYS_CLOCK_SEL  0x1d

/*
 * It turns out that the PLAYBACK_ENABLE bit is set
 * by the lowlevel driver ...
 *
#define AD1845_IFACE_CONFIG  \
           (CS4231_AUTOCALIB | CS4231_RECORD_ENABLE | CS4231_PLAYBACK_ENABLE)
    snd_cs4231_mce_up(chip);
    spin_lock_irqsave(&chip->reg_lock, flags);
    snd_cs4231_out(chip, CS4231_IFACE_CTRL, AD1845_IFACE_CONFIG);
    spin_unlock_irqrestore(&chip->reg_lock, flags);
    snd_cs4231_mce_down(chip);
 */

		/*
		 * The input clock frequency on the SoundScape must
		 * be 14.31818 MHz, because we must set this register
		 * to get the playback to sound correct ...
		 */
		snd_cs4231_mce_up(chip);
		spin_lock_irqsave(&chip->reg_lock, flags);
		snd_cs4231_out(chip, AD1845_CRYS_CLOCK_SEL, 0x20);
		spin_unlock_irqrestore(&chip->reg_lock, flags);
		snd_cs4231_mce_down(chip);

		/*
		 * More custom configuration:
		 * a) select "mode 2", and provide a current drive of 8 mA
		 * b) enable frequency selection (for capture/playback)
		 */
		spin_lock_irqsave(&chip->reg_lock, flags);
		snd_cs4231_out(chip, CS4231_MISC_INFO, (CS4231_MODE2 | 0x10));
		snd_cs4231_out(chip, AD1845_PWR_DOWN_CTRL, snd_cs4231_in(chip, AD1845_PWR_DOWN_CTRL) | AD1845_FREQ_SEL_ENABLE);
		spin_unlock_irqrestore(&chip->reg_lock, flags);

		if ((err = snd_cs4231_pcm(chip, 0, &pcm)) < 0) {
			snd_printk(KERN_ERR "sscape: No PCM device for AD1845 chip\n");
			goto _error;
		}

		if ((err = snd_cs4231_mixer(chip)) < 0) {
			snd_printk(KERN_ERR "sscape: No mixer device for AD1845 chip\n");
			goto _error;
		}

		if ((err = snd_ctl_add(card, snd_ctl_new1(&midi_mixer_ctl, chip))) < 0) {
			snd_printk(KERN_ERR "sscape: Could not create MIDI mixer control\n");
			goto _error;
		}

		strcpy(card->driver, "SoundScape");
		strcpy(card->shortname, pcm->name);
		snprintf(card->longname, sizeof(card->longname),
		         "%s at 0x%lx, IRQ %d, DMA %d\n",
		         pcm->name, chip->port, chip->irq, chip->dma1);
		chip->set_playback_format = ad1845_playback_format;
		chip->set_capture_format = ad1845_capture_format;
		sscape->chip = chip;
	}

	_error:
	return err;
}


/*
 * Create an ALSA soundcard entry for the SoundScape, using
 * the given list of port, IRQ and DMA resources.
 */
static int __devinit create_sscape(int dev, struct snd_card **rcardp)
{
	struct snd_card *card;
	register struct soundscape *sscape;
	register unsigned dma_cfg;
	unsigned irq_cfg;
	unsigned mpu_irq_cfg;
	unsigned xport;
	struct resource *io_res;
	unsigned long flags;
	int err;

	/*
	 * Check that the user didn't pass us garbage data ...
	 */
	irq_cfg = get_irq_config(irq[dev]);
	if (irq_cfg == INVALID_IRQ) {
		snd_printk(KERN_ERR "sscape: Invalid IRQ %d\n", irq[dev]);
		return -ENXIO;
	}

	mpu_irq_cfg = get_irq_config(mpu_irq[dev]);
	if (mpu_irq_cfg == INVALID_IRQ) {
		printk(KERN_ERR "sscape: Invalid IRQ %d\n", mpu_irq[dev]);
		return -ENXIO;
	}
	xport = port[dev];

	/*
	 * Grab IO ports that we will need to probe so that we
	 * can detect and control this hardware ...
	 */
	if ((io_res = request_region(xport, 8, "SoundScape")) == NULL) {
		snd_printk(KERN_ERR "sscape: can't grab port 0x%x\n", xport);
		return -EBUSY;
	}

	/*
	 * Grab both DMA channels (OK, only one for now) ...
	 */
	if ((err = request_dma(dma[dev], "SoundScape")) < 0) {
		snd_printk(KERN_ERR "sscape: can't grab DMA %d\n", dma[dev]);
		goto _release_region;
	}

	/*
	 * Create a new ALSA sound card entry, in anticipation
	 * of detecting our hardware ...
	 */
	if ((card = snd_card_new(index[dev], id[dev], THIS_MODULE,
				 sizeof(struct soundscape))) == NULL) {
		err = -ENOMEM;
		goto _release_dma;
	}

	sscape = get_card_soundscape(card);
	spin_lock_init(&sscape->lock);
	spin_lock_init(&sscape->fwlock);
	sscape->io_res = io_res;
	sscape->io_base = xport;

	if (!detect_sscape(sscape)) {
		printk(KERN_ERR "sscape: hardware not detected at 0x%x\n", sscape->io_base);
		err = -ENODEV;
		goto _release_card;
	}

	printk(KERN_INFO "sscape: hardware detected at 0x%x, using IRQ %d, DMA %d\n",
	                 sscape->io_base, irq[dev], dma[dev]);

	/*
	 * Now create the hardware-specific device so that we can
	 * load the microcode into the on-board processor.
	 * We cannot use the MPU-401 MIDI system until this firmware
	 * has been loaded into the card.
	 */
	if ((err = snd_hwdep_new(card, "MC68EC000", 0, &(sscape->hw))) < 0) {
		printk(KERN_ERR "sscape: Failed to create firmware device\n");
		goto _release_card;
	}
	strlcpy(sscape->hw->name, "SoundScape M68K", sizeof(sscape->hw->name));
	sscape->hw->name[sizeof(sscape->hw->name) - 1] = '\0';
	sscape->hw->iface = SNDRV_HWDEP_IFACE_SSCAPE;
	sscape->hw->ops.open = sscape_hw_open;
	sscape->hw->ops.release = sscape_hw_release;
	sscape->hw->ops.ioctl = sscape_hw_ioctl;
	sscape->hw->private_data = sscape;

	/*
	 * Tell the on-board devices where their resources are (I think -
	 * I can't be sure without a datasheet ... So many magic values!)
	 */
	spin_lock_irqsave(&sscape->lock, flags);

	activate_ad1845_unsafe(sscape->io_base);

	sscape_write_unsafe(sscape->io_base, GA_INTENA_REG, 0x00); /* disable */
	sscape_write_unsafe(sscape->io_base, GA_SMCFGA_REG, 0x2e);
	sscape_write_unsafe(sscape->io_base, GA_SMCFGB_REG, 0x00);

	/*
	 * Enable and configure the DMA channels ...
	 */
	sscape_write_unsafe(sscape->io_base, GA_DMACFG_REG, 0x50);
	dma_cfg = (sscape->ic_type == IC_ODIE ? 0x70 : 0x40);
	sscape_write_unsafe(sscape->io_base, GA_DMAA_REG, dma_cfg);
	sscape_write_unsafe(sscape->io_base, GA_DMAB_REG, 0x20);

	sscape_write_unsafe(sscape->io_base,
	                    GA_INTCFG_REG, 0xf0 | (mpu_irq_cfg << 2) | mpu_irq_cfg);
	sscape_write_unsafe(sscape->io_base,
	                    GA_CDCFG_REG, 0x09 | DMA_8BIT | (dma[dev] << 4) | (irq_cfg << 1));

	spin_unlock_irqrestore(&sscape->lock, flags);

	/*
	 * We have now enabled the codec chip, and so we should
	 * detect the AD1845 device ...
	 */
	if ((err = create_ad1845(card, CODEC_IO(xport), irq[dev], dma[dev])) < 0) {
		printk(KERN_ERR "sscape: No AD1845 device at 0x%x, IRQ %d\n",
		                CODEC_IO(xport), irq[dev]);
		goto _release_card;
	}
#define MIDI_DEVNUM  0
	if ((err = create_mpu401(card, MIDI_DEVNUM, MPU401_IO(xport), mpu_irq[dev])) < 0) {
		printk(KERN_ERR "sscape: Failed to create MPU-401 device at 0x%x\n",
		                MPU401_IO(xport));
		goto _release_card;
	}

	/*
	 * Enable the master IRQ ...
	 */
	sscape_write(sscape, GA_INTENA_REG, 0x80);

	/*
	 * Initialize mixer
	 */
	sscape->midi_vol = 0;
	host_write_ctrl_unsafe(sscape->io_base, CMD_SET_MIDI_VOL, 100);
	host_write_ctrl_unsafe(sscape->io_base, 0, 100);
	host_write_ctrl_unsafe(sscape->io_base, CMD_XXX_MIDI_VOL, 100);

	/*
	 * Now that we have successfully created this sound card,
	 * it is safe to store the pointer.
	 * NOTE: we only register the sound card's "destructor"
	 *       function now that our "constructor" has completed.
	 */
	card->private_free = soundscape_free;
	*rcardp = card;

	return 0;

	_release_card:
	snd_card_free(card);

	_release_dma:
	free_dma(dma[dev]);

	_release_region:
	release_and_free_resource(io_res);

	return err;
}


static int __init snd_sscape_probe(struct platform_device *pdev)
{
	int dev = pdev->id;
	struct snd_card *card;
	int ret;

	dma[dev] &= 0x03;
	ret = create_sscape(dev, &card);
	if (ret < 0)
		return ret;
	snd_card_set_dev(card, &pdev->dev);
	if ((ret = snd_card_register(card)) < 0) {
		printk(KERN_ERR "sscape: Failed to register sound card\n");
		return ret;
	}
	platform_set_drvdata(pdev, card);
	return 0;
}

static int __devexit snd_sscape_remove(struct platform_device *devptr)
{
	snd_card_free(platform_get_drvdata(devptr));
	platform_set_drvdata(devptr, NULL);
	return 0;
}

#define SSCAPE_DRIVER	"snd_sscape"

static struct platform_driver snd_sscape_driver = {
	.probe		= snd_sscape_probe,
	.remove		= __devexit_p(snd_sscape_remove),
	/* FIXME: suspend/resume */
	.driver		= {
		.name	= SSCAPE_DRIVER
	},
};

#ifdef CONFIG_PNP
static inline int __devinit get_next_autoindex(int i)
{
	while (i < SNDRV_CARDS && port[i] != SNDRV_AUTO_PORT)
		++i;
	return i;
}


static int __devinit sscape_pnp_detect(struct pnp_card_link *pcard,
				       const struct pnp_card_device_id *pid)
{
	static int idx = 0;
	struct pnp_dev *dev;
	struct snd_card *card;
	int ret;

	/*
	 * Allow this function to fail *quietly* if all the ISA PnP
	 * devices were configured using module parameters instead.
	 */
	if ((idx = get_next_autoindex(idx)) >= SNDRV_CARDS)
		return -ENOSPC;

	/*
	 * We have found a candidate ISA PnP card. Now we
	 * have to check that it has the devices that we
	 * expect it to have.
	 *
	 * We will NOT try and autoconfigure all of the resources
	 * needed and then activate the card as we are assuming that
	 * has already been done at boot-time using /proc/isapnp.
	 * We shall simply try to give each active card the resources
	 * that it wants. This is a sensible strategy for a modular
	 * system where unused modules are unloaded regularly.
	 *
	 * This strategy is utterly useless if we compile the driver
	 * into the kernel, of course.
	 */
	// printk(KERN_INFO "sscape: %s\n", card->name);

	/*
	 * Check that we still have room for another sound card ...
	 */
	dev = pnp_request_card_device(pcard, pid->devs[0].id, NULL);
	if (! dev)
		return -ENODEV;

	if (!pnp_is_active(dev)) {
		if (pnp_activate_dev(dev) < 0) {
			printk(KERN_INFO "sscape: device is inactive\n");
			return -EBUSY;
		}
	}

	/*
	 * Read the correct parameters off the ISA PnP bus ...
	 */
	port[idx] = pnp_port_start(dev, 0);
	irq[idx] = pnp_irq(dev, 0);
	mpu_irq[idx] = pnp_irq(dev, 1);
	dma[idx] = pnp_dma(dev, 0) & 0x03;

	ret = create_sscape(idx, &card);
	if (ret < 0)
		return ret;
	snd_card_set_dev(card, &pcard->card->dev);
	if ((ret = snd_card_register(card)) < 0) {
		printk(KERN_ERR "sscape: Failed to register sound card\n");
		snd_card_free(card);
		return ret;
	}

	pnp_set_card_drvdata(pcard, card);
	++idx;

	return ret;
}

static void __devexit sscape_pnp_remove(struct pnp_card_link * pcard)
{
	snd_card_free(pnp_get_card_drvdata(pcard));
	pnp_set_card_drvdata(pcard, NULL);
}

static struct pnp_card_driver sscape_pnpc_driver = {
	.flags = PNP_DRIVER_RES_DO_NOT_CHANGE,
	.name = "sscape",
	.id_table = sscape_pnpids,
	.probe = sscape_pnp_detect,
	.remove = __devexit_p(sscape_pnp_remove),
};

#endif /* CONFIG_PNP */

static void __init_or_module sscape_unregister_all(void)
{
	int i;

	if (pnp_registered)
		pnp_unregister_card_driver(&sscape_pnpc_driver);
	for (i = 0; i < ARRAY_SIZE(platform_devices); ++i)
		platform_device_unregister(platform_devices[i]);
	platform_driver_unregister(&snd_sscape_driver);
}

static int __init sscape_manual_probe(void)
{
	struct platform_device *device;
	int i, ret;

	ret = platform_driver_register(&snd_sscape_driver);
	if (ret < 0)
		return ret;

	for (i = 0; i < SNDRV_CARDS; ++i) {
		/*
		 * We do NOT probe for ports.
		 * If we're not given a port number for this
		 * card then we completely ignore this line
		 * of parameters.
		 */
		if (port[i] == SNDRV_AUTO_PORT)
			continue;

		/*
		 * Make sure we were given ALL of the other parameters.
		 */
		if (irq[i] == SNDRV_AUTO_IRQ ||
		    mpu_irq[i] == SNDRV_AUTO_IRQ ||
		    dma[i] == SNDRV_AUTO_DMA) {
			printk(KERN_INFO
			       "sscape: insufficient parameters, need IO, IRQ, MPU-IRQ and DMA\n");
			ret = -ENXIO;
			goto errout;
		}

		/*
		 * This cards looks OK ...
		 */
		device = platform_device_register_simple(SSCAPE_DRIVER,
							 i, NULL, 0);
		if (IS_ERR(device)) {
			ret = PTR_ERR(device);
			goto errout;
		}
		platform_devices[i] = device;
	}
	return 0;

 errout:
	sscape_unregister_all();
	return ret;
}

static void sscape_exit(void)
{
	sscape_unregister_all();
}


static int __init sscape_init(void)
{
	int ret;

	/*
	 * First check whether we were passed any parameters.
	 * These MUST take precedence over ANY automatic way
	 * of allocating cards, because the operator is
	 * S-P-E-L-L-I-N-G it out for us...
	 */
	ret = sscape_manual_probe();
	if (ret < 0)
		return ret;
	if (pnp_register_card_driver(&sscape_pnpc_driver) >= 0)
		pnp_registered = 1;
	return 0;
}

module_init(sscape_init);
module_exit(sscape_exit);
