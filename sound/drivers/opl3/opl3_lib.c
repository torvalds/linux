/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>,
 *                   Hannu Savolainen 1993-1996,
 *                   Rob Hooft
 *                   
 *  Routines for control of AdLib FM cards (OPL2/OPL3/OPL4 chips)
 *
 *  Most if code is ported from OSS/Lite.
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

#include <sound/opl3.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <sound/minors.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@perex.cz>, Hannu Savolainen 1993-1996, Rob Hooft");
MODULE_DESCRIPTION("Routines for control of AdLib FM cards (OPL2/OPL3/OPL4 chips)");
MODULE_LICENSE("GPL");

extern char snd_opl3_regmap[MAX_OPL2_VOICES][4];

static void snd_opl2_command(struct snd_opl3 * opl3, unsigned short cmd, unsigned char val)
{
	unsigned long flags;
	unsigned long port;

	/*
	 * The original 2-OP synth requires a quite long delay
	 * after writing to a register.
	 */

	port = (cmd & OPL3_RIGHT) ? opl3->r_port : opl3->l_port;

	spin_lock_irqsave(&opl3->reg_lock, flags);

	outb((unsigned char) cmd, port);
	udelay(10);

	outb((unsigned char) val, port + 1);
	udelay(30);

	spin_unlock_irqrestore(&opl3->reg_lock, flags);
}

static void snd_opl3_command(struct snd_opl3 * opl3, unsigned short cmd, unsigned char val)
{
	unsigned long flags;
	unsigned long port;

	/*
	 * The OPL-3 survives with just two INBs
	 * after writing to a register.
	 */

	port = (cmd & OPL3_RIGHT) ? opl3->r_port : opl3->l_port;

	spin_lock_irqsave(&opl3->reg_lock, flags);

	outb((unsigned char) cmd, port);
	inb(opl3->l_port);
	inb(opl3->l_port);

	outb((unsigned char) val, port + 1);
	inb(opl3->l_port);
	inb(opl3->l_port);

	spin_unlock_irqrestore(&opl3->reg_lock, flags);
}

static int snd_opl3_detect(struct snd_opl3 * opl3)
{
	/*
	 * This function returns 1 if the FM chip is present at the given I/O port
	 * The detection algorithm plays with the timer built in the FM chip and
	 * looks for a change in the status register.
	 *
	 * Note! The timers of the FM chip are not connected to AdLib (and compatible)
	 * boards.
	 *
	 * Note2! The chip is initialized if detected.
	 */

	unsigned char stat1, stat2, signature;

	/* Reset timers 1 and 2 */
	opl3->command(opl3, OPL3_LEFT | OPL3_REG_TIMER_CONTROL, OPL3_TIMER1_MASK | OPL3_TIMER2_MASK);
	/* Reset the IRQ of the FM chip */
	opl3->command(opl3, OPL3_LEFT | OPL3_REG_TIMER_CONTROL, OPL3_IRQ_RESET);
	signature = stat1 = inb(opl3->l_port);	/* Status register */
	if ((stat1 & 0xe0) != 0x00) {	/* Should be 0x00 */
		snd_printd("OPL3: stat1 = 0x%x\n", stat1);
		return -ENODEV;
	}
	/* Set timer1 to 0xff */
	opl3->command(opl3, OPL3_LEFT | OPL3_REG_TIMER1, 0xff);
	/* Unmask and start timer 1 */
	opl3->command(opl3, OPL3_LEFT | OPL3_REG_TIMER_CONTROL, OPL3_TIMER2_MASK | OPL3_TIMER1_START);
	/* Now we have to delay at least 80us */
	udelay(200);
	/* Read status after timers have expired */
	stat2 = inb(opl3->l_port);
	/* Stop the timers */
	opl3->command(opl3, OPL3_LEFT | OPL3_REG_TIMER_CONTROL, OPL3_TIMER1_MASK | OPL3_TIMER2_MASK);
	/* Reset the IRQ of the FM chip */
	opl3->command(opl3, OPL3_LEFT | OPL3_REG_TIMER_CONTROL, OPL3_IRQ_RESET);
	if ((stat2 & 0xe0) != 0xc0) {	/* There is no YM3812 */
		snd_printd("OPL3: stat2 = 0x%x\n", stat2);
		return -ENODEV;
	}

	/* If the toplevel code knows exactly the type of chip, don't try
	   to detect it. */
	if (opl3->hardware != OPL3_HW_AUTO)
		return 0;

	/* There is a FM chip on this address. Detect the type (OPL2 to OPL4) */
	if (signature == 0x06) {	/* OPL2 */
		opl3->hardware = OPL3_HW_OPL2;
	} else {
		/*
		 * If we had an OPL4 chip, opl3->hardware would have been set
		 * by the OPL4 driver; so we can assume OPL3 here.
		 */
		if (snd_BUG_ON(!opl3->r_port))
			return -ENODEV;
		opl3->hardware = OPL3_HW_OPL3;
	}
	return 0;
}

/*
 *  AdLib timers
 */

/*
 *  Timer 1 - 80us
 */

static int snd_opl3_timer1_start(struct snd_timer * timer)
{
	unsigned long flags;
	unsigned char tmp;
	unsigned int ticks;
	struct snd_opl3 *opl3;

	opl3 = snd_timer_chip(timer);
	spin_lock_irqsave(&opl3->timer_lock, flags);
	ticks = timer->sticks;
	tmp = (opl3->timer_enable | OPL3_TIMER1_START) & ~OPL3_TIMER1_MASK;
	opl3->timer_enable = tmp;
	opl3->command(opl3, OPL3_LEFT | OPL3_REG_TIMER1, 256 - ticks);	/* timer 1 count */
	opl3->command(opl3, OPL3_LEFT | OPL3_REG_TIMER_CONTROL, tmp);	/* enable timer 1 IRQ */
	spin_unlock_irqrestore(&opl3->timer_lock, flags);
	return 0;
}

static int snd_opl3_timer1_stop(struct snd_timer * timer)
{
	unsigned long flags;
	unsigned char tmp;
	struct snd_opl3 *opl3;

	opl3 = snd_timer_chip(timer);
	spin_lock_irqsave(&opl3->timer_lock, flags);
	tmp = (opl3->timer_enable | OPL3_TIMER1_MASK) & ~OPL3_TIMER1_START;
	opl3->timer_enable = tmp;
	opl3->command(opl3, OPL3_LEFT | OPL3_REG_TIMER_CONTROL, tmp);	/* disable timer #1 */
	spin_unlock_irqrestore(&opl3->timer_lock, flags);
	return 0;
}

/*
 *  Timer 2 - 320us
 */

static int snd_opl3_timer2_start(struct snd_timer * timer)
{
	unsigned long flags;
	unsigned char tmp;
	unsigned int ticks;
	struct snd_opl3 *opl3;

	opl3 = snd_timer_chip(timer);
	spin_lock_irqsave(&opl3->timer_lock, flags);
	ticks = timer->sticks;
	tmp = (opl3->timer_enable | OPL3_TIMER2_START) & ~OPL3_TIMER2_MASK;
	opl3->timer_enable = tmp;
	opl3->command(opl3, OPL3_LEFT | OPL3_REG_TIMER2, 256 - ticks);	/* timer 1 count */
	opl3->command(opl3, OPL3_LEFT | OPL3_REG_TIMER_CONTROL, tmp);	/* enable timer 1 IRQ */
	spin_unlock_irqrestore(&opl3->timer_lock, flags);
	return 0;
}

static int snd_opl3_timer2_stop(struct snd_timer * timer)
{
	unsigned long flags;
	unsigned char tmp;
	struct snd_opl3 *opl3;

	opl3 = snd_timer_chip(timer);
	spin_lock_irqsave(&opl3->timer_lock, flags);
	tmp = (opl3->timer_enable | OPL3_TIMER2_MASK) & ~OPL3_TIMER2_START;
	opl3->timer_enable = tmp;
	opl3->command(opl3, OPL3_LEFT | OPL3_REG_TIMER_CONTROL, tmp);	/* disable timer #1 */
	spin_unlock_irqrestore(&opl3->timer_lock, flags);
	return 0;
}

/*

 */

static struct snd_timer_hardware snd_opl3_timer1 =
{
	.flags =	SNDRV_TIMER_HW_STOP,
	.resolution =	80000,
	.ticks =	256,
	.start =	snd_opl3_timer1_start,
	.stop =		snd_opl3_timer1_stop,
};

static struct snd_timer_hardware snd_opl3_timer2 =
{
	.flags =	SNDRV_TIMER_HW_STOP,
	.resolution =	320000,
	.ticks =	256,
	.start =	snd_opl3_timer2_start,
	.stop =		snd_opl3_timer2_stop,
};

static int snd_opl3_timer1_init(struct snd_opl3 * opl3, int timer_no)
{
	struct snd_timer *timer = NULL;
	struct snd_timer_id tid;
	int err;

	tid.dev_class = SNDRV_TIMER_CLASS_CARD;
	tid.dev_sclass = SNDRV_TIMER_SCLASS_NONE;
	tid.card = opl3->card->number;
	tid.device = timer_no;
	tid.subdevice = 0;
	if ((err = snd_timer_new(opl3->card, "AdLib timer #1", &tid, &timer)) >= 0) {
		strcpy(timer->name, "AdLib timer #1");
		timer->private_data = opl3;
		timer->hw = snd_opl3_timer1;
	}
	opl3->timer1 = timer;
	return err;
}

static int snd_opl3_timer2_init(struct snd_opl3 * opl3, int timer_no)
{
	struct snd_timer *timer = NULL;
	struct snd_timer_id tid;
	int err;

	tid.dev_class = SNDRV_TIMER_CLASS_CARD;
	tid.dev_sclass = SNDRV_TIMER_SCLASS_NONE;
	tid.card = opl3->card->number;
	tid.device = timer_no;
	tid.subdevice = 0;
	if ((err = snd_timer_new(opl3->card, "AdLib timer #2", &tid, &timer)) >= 0) {
		strcpy(timer->name, "AdLib timer #2");
		timer->private_data = opl3;
		timer->hw = snd_opl3_timer2;
	}
	opl3->timer2 = timer;
	return err;
}

/*

 */

void snd_opl3_interrupt(struct snd_hwdep * hw)
{
	unsigned char status;
	struct snd_opl3 *opl3;
	struct snd_timer *timer;

	if (hw == NULL)
		return;

	opl3 = hw->private_data;
	status = inb(opl3->l_port);
#if 0
	snd_printk("AdLib IRQ status = 0x%x\n", status);
#endif
	if (!(status & 0x80))
		return;

	if (status & 0x40) {
		timer = opl3->timer1;
		snd_timer_interrupt(timer, timer->sticks);
	}
	if (status & 0x20) {
		timer = opl3->timer2;
		snd_timer_interrupt(timer, timer->sticks);
	}
}

EXPORT_SYMBOL(snd_opl3_interrupt);

/*

 */

static int snd_opl3_free(struct snd_opl3 *opl3)
{
	if (snd_BUG_ON(!opl3))
		return -ENXIO;
	if (opl3->private_free)
		opl3->private_free(opl3);
	snd_opl3_clear_patches(opl3);
	release_and_free_resource(opl3->res_l_port);
	release_and_free_resource(opl3->res_r_port);
	kfree(opl3);
	return 0;
}

static int snd_opl3_dev_free(struct snd_device *device)
{
	struct snd_opl3 *opl3 = device->device_data;
	return snd_opl3_free(opl3);
}

int snd_opl3_new(struct snd_card *card,
		 unsigned short hardware,
		 struct snd_opl3 **ropl3)
{
	static struct snd_device_ops ops = {
		.dev_free = snd_opl3_dev_free,
	};
	struct snd_opl3 *opl3;
	int err;

	*ropl3 = NULL;
	opl3 = kzalloc(sizeof(*opl3), GFP_KERNEL);
	if (opl3 == NULL) {
		snd_printk(KERN_ERR "opl3: cannot allocate\n");
		return -ENOMEM;
	}

	opl3->card = card;
	opl3->hardware = hardware;
	spin_lock_init(&opl3->reg_lock);
	spin_lock_init(&opl3->timer_lock);

	if ((err = snd_device_new(card, SNDRV_DEV_CODEC, opl3, &ops)) < 0) {
		snd_opl3_free(opl3);
		return err;
	}

	*ropl3 = opl3;
	return 0;
}

EXPORT_SYMBOL(snd_opl3_new);

int snd_opl3_init(struct snd_opl3 *opl3)
{
	if (! opl3->command) {
		printk(KERN_ERR "snd_opl3_init: command not defined!\n");
		return -EINVAL;
	}

	opl3->command(opl3, OPL3_LEFT | OPL3_REG_TEST, OPL3_ENABLE_WAVE_SELECT);
	/* Melodic mode */
	opl3->command(opl3, OPL3_LEFT | OPL3_REG_PERCUSSION, 0x00);

	switch (opl3->hardware & OPL3_HW_MASK) {
	case OPL3_HW_OPL2:
		opl3->max_voices = MAX_OPL2_VOICES;
		break;
	case OPL3_HW_OPL3:
	case OPL3_HW_OPL4:
		opl3->max_voices = MAX_OPL3_VOICES;
		/* Enter OPL3 mode */
		opl3->command(opl3, OPL3_RIGHT | OPL3_REG_MODE, OPL3_OPL3_ENABLE);
	}
	return 0;
}

EXPORT_SYMBOL(snd_opl3_init);

int snd_opl3_create(struct snd_card *card,
		    unsigned long l_port,
		    unsigned long r_port,
		    unsigned short hardware,
		    int integrated,
		    struct snd_opl3 ** ropl3)
{
	struct snd_opl3 *opl3;
	int err;

	*ropl3 = NULL;
	if ((err = snd_opl3_new(card, hardware, &opl3)) < 0)
		return err;
	if (! integrated) {
		if ((opl3->res_l_port = request_region(l_port, 2, "OPL2/3 (left)")) == NULL) {
			snd_printk(KERN_ERR "opl3: can't grab left port 0x%lx\n", l_port);
			snd_device_free(card, opl3);
			return -EBUSY;
		}
		if (r_port != 0 &&
		    (opl3->res_r_port = request_region(r_port, 2, "OPL2/3 (right)")) == NULL) {
			snd_printk(KERN_ERR "opl3: can't grab right port 0x%lx\n", r_port);
			snd_device_free(card, opl3);
			return -EBUSY;
		}
	}
	opl3->l_port = l_port;
	opl3->r_port = r_port;

	switch (opl3->hardware) {
	/* some hardware doesn't support timers */
	case OPL3_HW_OPL3_SV:
	case OPL3_HW_OPL3_CS:
	case OPL3_HW_OPL3_FM801:
		opl3->command = &snd_opl3_command;
		break;
	default:
		opl3->command = &snd_opl2_command;
		if ((err = snd_opl3_detect(opl3)) < 0) {
			snd_printd("OPL2/3 chip not detected at 0x%lx/0x%lx\n",
				   opl3->l_port, opl3->r_port);
			snd_device_free(card, opl3);
			return err;
		}
		/* detect routine returns correct hardware type */
		switch (opl3->hardware & OPL3_HW_MASK) {
		case OPL3_HW_OPL3:
		case OPL3_HW_OPL4:
			opl3->command = &snd_opl3_command;
		}
	}

	snd_opl3_init(opl3);

	*ropl3 = opl3;
	return 0;
}

EXPORT_SYMBOL(snd_opl3_create);

int snd_opl3_timer_new(struct snd_opl3 * opl3, int timer1_dev, int timer2_dev)
{
	int err;

	if (timer1_dev >= 0)
		if ((err = snd_opl3_timer1_init(opl3, timer1_dev)) < 0)
			return err;
	if (timer2_dev >= 0) {
		if ((err = snd_opl3_timer2_init(opl3, timer2_dev)) < 0) {
			snd_device_free(opl3->card, opl3->timer1);
			opl3->timer1 = NULL;
			return err;
		}
	}
	return 0;
}

EXPORT_SYMBOL(snd_opl3_timer_new);

int snd_opl3_hwdep_new(struct snd_opl3 * opl3,
		       int device, int seq_device,
		       struct snd_hwdep ** rhwdep)
{
	struct snd_hwdep *hw;
	struct snd_card *card = opl3->card;
	int err;

	if (rhwdep)
		*rhwdep = NULL;

	/* create hardware dependent device (direct FM) */

	if ((err = snd_hwdep_new(card, "OPL2/OPL3", device, &hw)) < 0) {
		snd_device_free(card, opl3);
		return err;
	}
	hw->private_data = opl3;
	hw->exclusive = 1;
#ifdef CONFIG_SND_OSSEMUL
	if (device == 0) {
		hw->oss_type = SNDRV_OSS_DEVICE_TYPE_DMFM;
		sprintf(hw->oss_dev, "dmfm%i", card->number);
	}
#endif
	strcpy(hw->name, hw->id);
	switch (opl3->hardware & OPL3_HW_MASK) {
	case OPL3_HW_OPL2:
		strcpy(hw->name, "OPL2 FM");
		hw->iface = SNDRV_HWDEP_IFACE_OPL2;
		break;
	case OPL3_HW_OPL3:
		strcpy(hw->name, "OPL3 FM");
		hw->iface = SNDRV_HWDEP_IFACE_OPL3;
		break;
	case OPL3_HW_OPL4:
		strcpy(hw->name, "OPL4 FM");
		hw->iface = SNDRV_HWDEP_IFACE_OPL4;
		break;
	}

	/* operators - only ioctl */
	hw->ops.open = snd_opl3_open;
	hw->ops.ioctl = snd_opl3_ioctl;
	hw->ops.write = snd_opl3_write;
	hw->ops.release = snd_opl3_release;

	opl3->hwdep = hw;
	opl3->seq_dev_num = seq_device;
#if defined(CONFIG_SND_SEQUENCER) || (defined(MODULE) && defined(CONFIG_SND_SEQUENCER_MODULE))
	if (snd_seq_device_new(card, seq_device, SNDRV_SEQ_DEV_ID_OPL3,
			       sizeof(struct snd_opl3 *), &opl3->seq_dev) >= 0) {
		strcpy(opl3->seq_dev->name, hw->name);
		*(struct snd_opl3 **)SNDRV_SEQ_DEVICE_ARGPTR(opl3->seq_dev) = opl3;
	}
#endif
	if (rhwdep)
		*rhwdep = hw;
	return 0;
}

EXPORT_SYMBOL(snd_opl3_hwdep_new);

/*
 *  INIT part
 */

static int __init alsa_opl3_init(void)
{
	return 0;
}

static void __exit alsa_opl3_exit(void)
{
}

module_init(alsa_opl3_init)
module_exit(alsa_opl3_exit)
