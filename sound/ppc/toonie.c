/*
 * Mac Mini "toonie" mixer control
 *
 * Copyright (c) 2005 by Benjamin Herrenschmidt <benh@kernel.crashing.org>
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
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <linux/kmod.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <sound/core.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/machdep.h>
#include <asm/pmac_feature.h>
#include "pmac.h"

#undef DEBUG

#ifdef DEBUG
#define DBG(fmt...) printk(fmt)
#else
#define DBG(fmt...)
#endif

struct pmac_gpio {
	unsigned int addr;
	u8 active_val;
	u8 inactive_val;
	u8 active_state;
};

struct pmac_toonie
{
	struct pmac_gpio	hp_detect_gpio;
	struct pmac_gpio	hp_mute_gpio;
	struct pmac_gpio	amp_mute_gpio;
	int			hp_detect_irq;
	int			auto_mute_notify;
	struct work_struct	detect_work;
};


/*
 * gpio access
 */
#define do_gpio_write(gp, val) \
	pmac_call_feature(PMAC_FTR_WRITE_GPIO, NULL, (gp)->addr, val)
#define do_gpio_read(gp) \
	pmac_call_feature(PMAC_FTR_READ_GPIO, NULL, (gp)->addr, 0)
#define tumbler_gpio_free(gp) /* NOP */

static void write_audio_gpio(struct pmac_gpio *gp, int active)
{
	if (! gp->addr)
		return;
	active = active ? gp->active_val : gp->inactive_val;
	do_gpio_write(gp, active);
	DBG("(I) gpio %x write %d\n", gp->addr, active);
}

static int check_audio_gpio(struct pmac_gpio *gp)
{
	int ret;

	if (! gp->addr)
		return 0;

	ret = do_gpio_read(gp);

	return (ret & 0xd) == (gp->active_val & 0xd);
}

static int read_audio_gpio(struct pmac_gpio *gp)
{
	int ret;
	if (! gp->addr)
		return 0;
	ret = ((do_gpio_read(gp) & 0x02) !=0);
	return ret == gp->active_state;
}


enum { TOONIE_MUTE_HP, TOONIE_MUTE_AMP };

static int toonie_get_mute_switch(snd_kcontrol_t *kcontrol,
				  snd_ctl_elem_value_t *ucontrol)
{
	pmac_t *chip = snd_kcontrol_chip(kcontrol);
	struct pmac_toonie *mix = chip->mixer_data;
	struct pmac_gpio *gp;

	if (mix == NULL)
		return -ENODEV;
	switch(kcontrol->private_value) {
	case TOONIE_MUTE_HP:
		gp = &mix->hp_mute_gpio;
		break;
	case TOONIE_MUTE_AMP:
		gp = &mix->amp_mute_gpio;
		break;
	default:
		return -EINVAL;;
	}
	ucontrol->value.integer.value[0] = !check_audio_gpio(gp);
	return 0;
}

static int toonie_put_mute_switch(snd_kcontrol_t *kcontrol,
				   snd_ctl_elem_value_t *ucontrol)
{
	pmac_t *chip = snd_kcontrol_chip(kcontrol);
	struct pmac_toonie *mix = chip->mixer_data;
	struct pmac_gpio *gp;
	int val;

	if (chip->update_automute && chip->auto_mute)
		return 0; /* don't touch in the auto-mute mode */

	if (mix == NULL)
		return -ENODEV;

	switch(kcontrol->private_value) {
	case TOONIE_MUTE_HP:
		gp = &mix->hp_mute_gpio;
		break;
	case TOONIE_MUTE_AMP:
		gp = &mix->amp_mute_gpio;
		break;
	default:
		return -EINVAL;;
	}
	val = ! check_audio_gpio(gp);
	if (val != ucontrol->value.integer.value[0]) {
		write_audio_gpio(gp, ! ucontrol->value.integer.value[0]);
		return 1;
	}
	return 0;
}

static snd_kcontrol_new_t toonie_hp_sw __initdata = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Headphone Playback Switch",
	.info = snd_pmac_boolean_mono_info,
	.get = toonie_get_mute_switch,
	.put = toonie_put_mute_switch,
	.private_value = TOONIE_MUTE_HP,
};
static snd_kcontrol_new_t toonie_speaker_sw __initdata = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "PC Speaker Playback Switch",
	.info = snd_pmac_boolean_mono_info,
	.get = toonie_get_mute_switch,
	.put = toonie_put_mute_switch,
	.private_value = TOONIE_MUTE_AMP,
};

/*
 * auto-mute stuffs
 */
static int toonie_detect_headphone(pmac_t *chip)
{
	struct pmac_toonie *mix = chip->mixer_data;
	int detect = 0;

	if (mix->hp_detect_gpio.addr)
		detect |= read_audio_gpio(&mix->hp_detect_gpio);
	return detect;
}

static void toonie_check_mute(pmac_t *chip, struct pmac_gpio *gp, int val,
			      int do_notify, snd_kcontrol_t *sw)
{
	if (check_audio_gpio(gp) != val) {
		write_audio_gpio(gp, val);
		if (do_notify)
			snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_VALUE,
				       &sw->id);
	}
}

static void toonie_detect_handler(void *self)
{
	pmac_t *chip = (pmac_t*) self;
	struct pmac_toonie *mix;
	int headphone;

	if (!chip)
		return;

	mix = chip->mixer_data;
	snd_assert(mix, return);

	headphone = toonie_detect_headphone(chip);

	DBG("headphone: %d, lineout: %d\n", headphone, lineout);

	if (headphone) {
		/* unmute headphone/lineout & mute speaker */
		toonie_check_mute(chip, &mix->hp_mute_gpio, 0,
				  mix->auto_mute_notify, chip->master_sw_ctl);
		toonie_check_mute(chip, &mix->amp_mute_gpio, 1,
				  mix->auto_mute_notify, chip->speaker_sw_ctl);
	} else {
		/* unmute speaker, mute others */
		toonie_check_mute(chip, &mix->amp_mute_gpio, 0,
				  mix->auto_mute_notify, chip->speaker_sw_ctl);
		toonie_check_mute(chip, &mix->hp_mute_gpio, 1,
				  mix->auto_mute_notify, chip->master_sw_ctl);
	}
	if (mix->auto_mute_notify) {
		snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_VALUE,
				       &chip->hp_detect_ctl->id);
	}
}

static void toonie_update_automute(pmac_t *chip, int do_notify)
{
	if (chip->auto_mute) {
		struct pmac_toonie *mix;
		mix = chip->mixer_data;
		snd_assert(mix, return);
		mix->auto_mute_notify = do_notify;
		schedule_work(&mix->detect_work);
	}
}

/* interrupt - headphone plug changed */
static irqreturn_t toonie_hp_intr(int irq, void *devid, struct pt_regs *regs)
{
	pmac_t *chip = devid;

	if (chip->update_automute && chip->initialized) {
		chip->update_automute(chip, 1);
		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}

/* look for audio gpio device */
static int find_audio_gpio(const char *name, const char *platform,
			   struct pmac_gpio *gp)
{
	struct device_node *np;
  	u32 *base, addr;

	if (! (np = find_devices("gpio")))
		return -ENODEV;

	for (np = np->child; np; np = np->sibling) {
		char *property = get_property(np, "audio-gpio", NULL);
		if (property && strcmp(property, name) == 0)
			break;
		if (device_is_compatible(np, name))
			break;
	}
	if (np == NULL)
		return -ENODEV;

	base = (u32 *)get_property(np, "AAPL,address", NULL);
	if (! base) {
		base = (u32 *)get_property(np, "reg", NULL);
		if (!base) {
			DBG("(E) cannot find address for device %s !\n", name);
			return -ENODEV;
		}
		addr = *base;
		if (addr < 0x50)
			addr += 0x50;
	} else
		addr = *base;

	gp->addr = addr & 0x0000ffff;

	/* Try to find the active state, default to 0 ! */
	base = (u32 *)get_property(np, "audio-gpio-active-state", NULL);
	if (base) {
		gp->active_state = *base;
		gp->active_val = (*base) ? 0x5 : 0x4;
		gp->inactive_val = (*base) ? 0x4 : 0x5;
	} else {
		u32 *prop = NULL;
		gp->active_state = 0;
		gp->active_val = 0x4;
		gp->inactive_val = 0x5;
		/* Here are some crude hacks to extract the GPIO polarity and
		 * open collector informations out of the do-platform script
		 * as we don't yet have an interpreter for these things
		 */
		if (platform)
			prop = (u32 *)get_property(np, platform, NULL);
		if (prop) {
			if (prop[3] == 0x9 && prop[4] == 0x9) {
				gp->active_val = 0xd;
				gp->inactive_val = 0xc;
			}
			if (prop[3] == 0x1 && prop[4] == 0x1) {
				gp->active_val = 0x5;
				gp->inactive_val = 0x4;
			}
		}
	}

	DBG("(I) GPIO device %s found, offset: %x, active state: %d !\n",
	    name, gp->addr, gp->active_state);

	return (np->n_intrs > 0) ? np->intrs[0].line : 0;
}

static void toonie_cleanup(pmac_t *chip)
{
	struct pmac_toonie *mix = chip->mixer_data;
	if (! mix)
		return;
	if (mix->hp_detect_irq >= 0)
		free_irq(mix->hp_detect_irq, chip);
	kfree(mix);
	chip->mixer_data = NULL;
}

int snd_pmac_toonie_init(pmac_t *chip)
{
	struct pmac_toonie *mix;

	mix = kmalloc(sizeof(*mix), GFP_KERNEL);
	if (! mix)
		return -ENOMEM;

	chip->mixer_data = mix;
	chip->mixer_free = toonie_cleanup;

	find_audio_gpio("headphone-mute", NULL, &mix->hp_mute_gpio);
	find_audio_gpio("amp-mute", NULL, &mix->amp_mute_gpio);
	mix->hp_detect_irq = find_audio_gpio("headphone-detect",
					     NULL, &mix->hp_detect_gpio);

	strcpy(chip->card->mixername, "PowerMac Toonie");

	chip->master_sw_ctl = snd_ctl_new1(&toonie_hp_sw, chip);
	snd_ctl_add(chip->card, chip->master_sw_ctl);

	chip->speaker_sw_ctl = snd_ctl_new1(&toonie_speaker_sw, chip);
	snd_ctl_add(chip->card, chip->speaker_sw_ctl);

	INIT_WORK(&mix->detect_work, toonie_detect_handler, (void *)chip);

	if (mix->hp_detect_irq >= 0) {
		snd_pmac_add_automute(chip);

		chip->detect_headphone = toonie_detect_headphone;
		chip->update_automute = toonie_update_automute;
		toonie_update_automute(chip, 0);

		if (request_irq(mix->hp_detect_irq, toonie_hp_intr, 0,
				"Sound Headphone Detection", chip) < 0)
			mix->hp_detect_irq = -1;
	}

	return 0;
}

