/*
 * ALSA timer back-end using hrtimer
 * Copyright (C) 2008 Takashi Iwai
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

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/hrtimer.h>
#include <sound/core.h>
#include <sound/timer.h>

MODULE_AUTHOR("Takashi Iwai <tiwai@suse.de>");
MODULE_DESCRIPTION("ALSA hrtimer backend");
MODULE_LICENSE("GPL");

MODULE_ALIAS("snd-timer-" __stringify(SNDRV_TIMER_GLOBAL_HRTIMER));

#define NANO_SEC	1000000000UL	/* 10^9 in sec */
static unsigned int resolution;

struct snd_hrtimer {
	struct snd_timer *timer;
	struct hrtimer hrt;
	bool in_callback;
};

static enum hrtimer_restart snd_hrtimer_callback(struct hrtimer *hrt)
{
	struct snd_hrtimer *stime = container_of(hrt, struct snd_hrtimer, hrt);
	struct snd_timer *t = stime->timer;
	ktime_t delta;
	unsigned long ticks;
	enum hrtimer_restart ret = HRTIMER_NORESTART;

	spin_lock(&t->lock);
	if (!t->running)
		goto out; /* fast path */
	stime->in_callback = true;
	ticks = t->sticks;
	spin_unlock(&t->lock);

	/* calculate the drift */
	delta = ktime_sub(hrt->base->get_time(), hrtimer_get_expires(hrt));
	if (delta > 0)
		ticks += ktime_divns(delta, ticks * resolution);

	snd_timer_interrupt(stime->timer, ticks);

	spin_lock(&t->lock);
	if (t->running) {
		hrtimer_add_expires_ns(hrt, t->sticks * resolution);
		ret = HRTIMER_RESTART;
	}

	stime->in_callback = false;
 out:
	spin_unlock(&t->lock);
	return ret;
}

static int snd_hrtimer_open(struct snd_timer *t)
{
	struct snd_hrtimer *stime;

	stime = kzalloc(sizeof(*stime), GFP_KERNEL);
	if (!stime)
		return -ENOMEM;
	hrtimer_init(&stime->hrt, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	stime->timer = t;
	stime->hrt.function = snd_hrtimer_callback;
	t->private_data = stime;
	return 0;
}

static int snd_hrtimer_close(struct snd_timer *t)
{
	struct snd_hrtimer *stime = t->private_data;

	if (stime) {
		spin_lock_irq(&t->lock);
		t->running = 0; /* just to be sure */
		stime->in_callback = 1; /* skip start/stop */
		spin_unlock_irq(&t->lock);

		hrtimer_cancel(&stime->hrt);
		kfree(stime);
		t->private_data = NULL;
	}
	return 0;
}

static int snd_hrtimer_start(struct snd_timer *t)
{
	struct snd_hrtimer *stime = t->private_data;

	if (stime->in_callback)
		return 0;
	hrtimer_start(&stime->hrt, ns_to_ktime(t->sticks * resolution),
		      HRTIMER_MODE_REL);
	return 0;
}

static int snd_hrtimer_stop(struct snd_timer *t)
{
	struct snd_hrtimer *stime = t->private_data;

	if (stime->in_callback)
		return 0;
	hrtimer_try_to_cancel(&stime->hrt);
	return 0;
}

static struct snd_timer_hardware hrtimer_hw = {
	.flags =	SNDRV_TIMER_HW_AUTO | SNDRV_TIMER_HW_TASKLET,
	.open =		snd_hrtimer_open,
	.close =	snd_hrtimer_close,
	.start =	snd_hrtimer_start,
	.stop =		snd_hrtimer_stop,
};

/*
 * entry functions
 */

static struct snd_timer *mytimer;

static int __init snd_hrtimer_init(void)
{
	struct snd_timer *timer;
	int err;

	resolution = hrtimer_resolution;

	/* Create a new timer and set up the fields */
	err = snd_timer_global_new("hrtimer", SNDRV_TIMER_GLOBAL_HRTIMER,
				   &timer);
	if (err < 0)
		return err;

	timer->module = THIS_MODULE;
	strcpy(timer->name, "HR timer");
	timer->hw = hrtimer_hw;
	timer->hw.resolution = resolution;
	timer->hw.ticks = NANO_SEC / resolution;
	timer->max_instances = 100; /* lower the limit */

	err = snd_timer_global_register(timer);
	if (err < 0) {
		snd_timer_global_free(timer);
		return err;
	}
	mytimer = timer; /* remember this */

	return 0;
}

static void __exit snd_hrtimer_exit(void)
{
	if (mytimer) {
		snd_timer_global_free(mytimer);
		mytimer = NULL;
	}
}

module_init(snd_hrtimer_init);
module_exit(snd_hrtimer_exit);
