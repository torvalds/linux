/*
 *  Digital Audio (PCM) abstract layer
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
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

#include <linux/time.h>
#include <linux/gcd.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/timer.h>

/*
 *  Timer functions
 */

void snd_pcm_timer_resolution_change(struct snd_pcm_substream *substream)
{
	unsigned long rate, mult, fsize, l, post;
	struct snd_pcm_runtime *runtime = substream->runtime;
	
        mult = 1000000000;
	rate = runtime->rate;
	if (snd_BUG_ON(!rate))
		return;
	l = gcd(mult, rate);
	mult /= l;
	rate /= l;
	fsize = runtime->period_size;
	if (snd_BUG_ON(!fsize))
		return;
	l = gcd(rate, fsize);
	rate /= l;
	fsize /= l;
	post = 1;
	while ((mult * fsize) / fsize != mult) {
		mult /= 2;
		post *= 2;
	}
	if (rate == 0) {
		snd_printk(KERN_ERR "pcm timer resolution out of range (rate = %u, period_size = %lu)\n", runtime->rate, runtime->period_size);
		runtime->timer_resolution = -1;
		return;
	}
	runtime->timer_resolution = (mult * fsize / rate) * post;
}

static unsigned long snd_pcm_timer_resolution(struct snd_timer * timer)
{
	struct snd_pcm_substream *substream;
	
	substream = timer->private_data;
	return substream->runtime ? substream->runtime->timer_resolution : 0;
}

static int snd_pcm_timer_start(struct snd_timer * timer)
{
	struct snd_pcm_substream *substream;
	
	substream = snd_timer_chip(timer);
	substream->timer_running = 1;
	return 0;
}

static int snd_pcm_timer_stop(struct snd_timer * timer)
{
	struct snd_pcm_substream *substream;
	
	substream = snd_timer_chip(timer);
	substream->timer_running = 0;
	return 0;
}

static struct snd_timer_hardware snd_pcm_timer =
{
	.flags =	SNDRV_TIMER_HW_AUTO | SNDRV_TIMER_HW_SLAVE,
	.resolution =	0,
	.ticks =	1,
	.c_resolution =	snd_pcm_timer_resolution,
	.start =	snd_pcm_timer_start,
	.stop =		snd_pcm_timer_stop,
};

/*
 *  Init functions
 */

static void snd_pcm_timer_free(struct snd_timer *timer)
{
	struct snd_pcm_substream *substream = timer->private_data;
	substream->timer = NULL;
}

void snd_pcm_timer_init(struct snd_pcm_substream *substream)
{
	struct snd_timer_id tid;
	struct snd_timer *timer;
	
	tid.dev_sclass = SNDRV_TIMER_SCLASS_NONE;
	tid.dev_class = SNDRV_TIMER_CLASS_PCM;
	tid.card = substream->pcm->card->number;
	tid.device = substream->pcm->device;
	tid.subdevice = (substream->number << 1) | (substream->stream & 1);
	if (snd_timer_new(substream->pcm->card, "PCM", &tid, &timer) < 0)
		return;
	sprintf(timer->name, "PCM %s %i-%i-%i",
			substream->stream == SNDRV_PCM_STREAM_CAPTURE ?
				"capture" : "playback",
			tid.card, tid.device, tid.subdevice);
	timer->hw = snd_pcm_timer;
	if (snd_device_register(timer->card, timer) < 0) {
		snd_device_free(timer->card, timer);
		return;
	}
	timer->private_data = substream;
	timer->private_free = snd_pcm_timer_free;
	substream->timer = timer;
}

void snd_pcm_timer_done(struct snd_pcm_substream *substream)
{
	if (substream->timer) {
		snd_device_free(substream->pcm->card, substream->timer);
		substream->timer = NULL;
	}
}
