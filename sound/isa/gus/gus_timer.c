/*
 *  Routines for Gravis UltraSound soundcards - Timers
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *
 *  GUS have similar timers as AdLib (OPL2/OPL3 chips).
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
 *
 */

#include <sound/driver.h>
#include <linux/time.h>
#include <sound/core.h>
#include <sound/gus.h>

/*
 *  Timer 1 - 80us
 */

static int snd_gf1_timer1_start(snd_timer_t * timer)
{
	unsigned long flags;
	unsigned char tmp;
	unsigned int ticks;
	snd_gus_card_t *gus;

	gus = snd_timer_chip(timer);
	spin_lock_irqsave(&gus->reg_lock, flags);
	ticks = timer->sticks;
	tmp = (gus->gf1.timer_enabled |= 4);
	snd_gf1_write8(gus, SNDRV_GF1_GB_ADLIB_TIMER_1, 256 - ticks);	/* timer 1 count */
	snd_gf1_write8(gus, SNDRV_GF1_GB_SOUND_BLASTER_CONTROL, tmp);	/* enable timer 1 IRQ */
	snd_gf1_adlib_write(gus, 0x04, tmp >> 2);	/* timer 2 start */
	spin_unlock_irqrestore(&gus->reg_lock, flags);
	return 0;
}

static int snd_gf1_timer1_stop(snd_timer_t * timer)
{
	unsigned long flags;
	unsigned char tmp;
	snd_gus_card_t *gus;

	gus = snd_timer_chip(timer);
	spin_lock_irqsave(&gus->reg_lock, flags);
	tmp = (gus->gf1.timer_enabled &= ~4);
	snd_gf1_write8(gus, SNDRV_GF1_GB_SOUND_BLASTER_CONTROL, tmp);	/* disable timer #1 */
	spin_unlock_irqrestore(&gus->reg_lock, flags);
	return 0;
}

/*
 *  Timer 2 - 320us
 */

static int snd_gf1_timer2_start(snd_timer_t * timer)
{
	unsigned long flags;
	unsigned char tmp;
	unsigned int ticks;
	snd_gus_card_t *gus;

	gus = snd_timer_chip(timer);
	spin_lock_irqsave(&gus->reg_lock, flags);
	ticks = timer->sticks;
	tmp = (gus->gf1.timer_enabled |= 8);
	snd_gf1_write8(gus, SNDRV_GF1_GB_ADLIB_TIMER_2, 256 - ticks);	/* timer 2 count */
	snd_gf1_write8(gus, SNDRV_GF1_GB_SOUND_BLASTER_CONTROL, tmp);	/* enable timer 2 IRQ */
	snd_gf1_adlib_write(gus, 0x04, tmp >> 2);	/* timer 2 start */
	spin_unlock_irqrestore(&gus->reg_lock, flags);
	return 0;
}

static int snd_gf1_timer2_stop(snd_timer_t * timer)
{
	unsigned long flags;
	unsigned char tmp;
	snd_gus_card_t *gus;

	gus = snd_timer_chip(timer);
	spin_lock_irqsave(&gus->reg_lock, flags);
	tmp = (gus->gf1.timer_enabled &= ~8);
	snd_gf1_write8(gus, SNDRV_GF1_GB_SOUND_BLASTER_CONTROL, tmp);	/* disable timer #1 */
	spin_unlock_irqrestore(&gus->reg_lock, flags);
	return 0;
}

/*

 */

static void snd_gf1_interrupt_timer1(snd_gus_card_t * gus)
{
	snd_timer_t *timer = gus->gf1.timer1;

	if (timer == NULL)
		return;
	snd_timer_interrupt(timer, timer->sticks);
}

static void snd_gf1_interrupt_timer2(snd_gus_card_t * gus)
{
	snd_timer_t *timer = gus->gf1.timer2;

	if (timer == NULL)
		return;
	snd_timer_interrupt(timer, timer->sticks);
}

/*

 */

static struct _snd_timer_hardware snd_gf1_timer1 =
{
	.flags =	SNDRV_TIMER_HW_STOP,
	.resolution =	80000,
	.ticks =	256,
	.start =	snd_gf1_timer1_start,
	.stop =		snd_gf1_timer1_stop,
};

static struct _snd_timer_hardware snd_gf1_timer2 =
{
	.flags =	SNDRV_TIMER_HW_STOP,
	.resolution =	320000,
	.ticks =	256,
	.start =	snd_gf1_timer2_start,
	.stop =		snd_gf1_timer2_stop,
};

static void snd_gf1_timer1_free(snd_timer_t *timer)
{
	snd_gus_card_t *gus = timer->private_data;
	gus->gf1.timer1 = NULL;
}

static void snd_gf1_timer2_free(snd_timer_t *timer)
{
	snd_gus_card_t *gus = timer->private_data;
	gus->gf1.timer2 = NULL;
}

void snd_gf1_timers_init(snd_gus_card_t * gus)
{
	snd_timer_t *timer;
	snd_timer_id_t tid;

	if (gus->gf1.timer1 != NULL || gus->gf1.timer2 != NULL)
		return;

	gus->gf1.interrupt_handler_timer1 = snd_gf1_interrupt_timer1;
	gus->gf1.interrupt_handler_timer2 = snd_gf1_interrupt_timer2;

	tid.dev_class = SNDRV_TIMER_CLASS_CARD;
	tid.dev_sclass = SNDRV_TIMER_SCLASS_NONE;
	tid.card = gus->card->number;
	tid.device = gus->timer_dev;
	tid.subdevice = 0;

	if (snd_timer_new(gus->card, "GF1 timer", &tid, &timer) >= 0) {
		strcpy(timer->name, "GF1 timer #1");
		timer->private_data = gus;
		timer->private_free = snd_gf1_timer1_free;
		timer->hw = snd_gf1_timer1;
	}
	gus->gf1.timer1 = timer;

	tid.device++;

	if (snd_timer_new(gus->card, "GF1 timer", &tid, &timer) >= 0) {
		strcpy(timer->name, "GF1 timer #2");
		timer->private_data = gus;
		timer->private_free = snd_gf1_timer2_free;
		timer->hw = snd_gf1_timer2;
	}
	gus->gf1.timer2 = timer;
}

void snd_gf1_timers_done(snd_gus_card_t * gus)
{
	snd_gf1_set_default_handlers(gus, SNDRV_GF1_HANDLER_TIMER1 | SNDRV_GF1_HANDLER_TIMER2);
	if (gus->gf1.timer1) {
		snd_device_free(gus->card, gus->gf1.timer1);
		gus->gf1.timer1 = NULL;
	}
	if (gus->gf1.timer2) {
		snd_device_free(gus->card, gus->gf1.timer2);
		gus->gf1.timer2 = NULL;
	}
}
