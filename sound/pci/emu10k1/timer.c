// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) by Lee Revell <rlrevell@joe-job.com>
 *                   Clemens Ladisch <clemens@ladisch.de>
 *  Routines for control of EMU10K1 chips
 *
 *  BUGS:
 *    --
 *
 *  TODO:
 *    --
 */

#include <linux/time.h>
#include <sound/core.h>
#include <sound/emu10k1.h>

static int snd_emu10k1_timer_start(struct snd_timer *timer)
{
	struct snd_emu10k1 *emu;
	unsigned int delay;

	emu = snd_timer_chip(timer);
	delay = timer->sticks - 1;
	if (delay < 5 ) /* minimum time is 5 ticks */
		delay = 5;
	snd_emu10k1_intr_enable(emu, INTE_INTERVALTIMERENB);
	outw(delay & TIMER_RATE_MASK, emu->port + TIMER);
	return 0;
}

static int snd_emu10k1_timer_stop(struct snd_timer *timer)
{
	struct snd_emu10k1 *emu;

	emu = snd_timer_chip(timer);
	snd_emu10k1_intr_disable(emu, INTE_INTERVALTIMERENB);
	return 0;
}

static int snd_emu10k1_timer_precise_resolution(struct snd_timer *timer,
					       unsigned long *num, unsigned long *den)
{
	*num = 1;
	*den = 48000;
	return 0;
}

static const struct snd_timer_hardware snd_emu10k1_timer_hw = {
	.flags = SNDRV_TIMER_HW_AUTO,
	.resolution = 20833, /* 1 sample @ 48KHZ = 20.833...us */
	.ticks = 1024,
	.start = snd_emu10k1_timer_start,
	.stop = snd_emu10k1_timer_stop,
	.precise_resolution = snd_emu10k1_timer_precise_resolution,
};

int snd_emu10k1_timer(struct snd_emu10k1 *emu, int device)
{
	struct snd_timer *timer = NULL;
	struct snd_timer_id tid;
	int err;

	tid.dev_class = SNDRV_TIMER_CLASS_CARD;
	tid.dev_sclass = SNDRV_TIMER_SCLASS_NONE;
	tid.card = emu->card->number;
	tid.device = device;
	tid.subdevice = 0;
	err = snd_timer_new(emu->card, "EMU10K1", &tid, &timer);
	if (err >= 0) {
		strcpy(timer->name, "EMU10K1 timer");
		timer->private_data = emu;
		timer->hw = snd_emu10k1_timer_hw;
	}
	emu->timer = timer;
	return err;
}
