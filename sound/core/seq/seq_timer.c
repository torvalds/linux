// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   ALSA sequencer Timer
 *   Copyright (c) 1998-1999 by Frank van de Pol <fvdpol@coil.demon.nl>
 *                              Jaroslav Kysela <perex@perex.cz>
 */

#include <sound/core.h>
#include <linux/slab.h>
#include "seq_timer.h"
#include "seq_queue.h"
#include "seq_info.h"

/* allowed sequencer timer frequencies, in Hz */
#define MIN_FREQUENCY		10
#define MAX_FREQUENCY		6250
#define DEFAULT_FREQUENCY	1000

#define SKEW_BASE	0x10000	/* 16bit shift */

static void snd_seq_timer_set_tick_resolution(struct snd_seq_timer *tmr)
{
	unsigned int threshold =
		tmr->tempo_base == 1000 ? 1000000 : 10000;

	if (tmr->tempo < threshold)
		tmr->tick.resolution = (tmr->tempo * tmr->tempo_base) / tmr->ppq;
	else {
		/* might overflow.. */
		unsigned int s;
		s = tmr->tempo % tmr->ppq;
		s = (s * tmr->tempo_base) / tmr->ppq;
		tmr->tick.resolution = (tmr->tempo / tmr->ppq) * tmr->tempo_base;
		tmr->tick.resolution += s;
	}
	if (tmr->tick.resolution <= 0)
		tmr->tick.resolution = 1;
	snd_seq_timer_update_tick(&tmr->tick, 0);
}

/* create new timer (constructor) */
struct snd_seq_timer *snd_seq_timer_new(void)
{
	struct snd_seq_timer *tmr;
	
	tmr = kzalloc(sizeof(*tmr), GFP_KERNEL);
	if (!tmr)
		return NULL;
	spin_lock_init(&tmr->lock);

	/* reset setup to defaults */
	snd_seq_timer_defaults(tmr);
	
	/* reset time */
	snd_seq_timer_reset(tmr);
	
	return tmr;
}

/* delete timer (destructor) */
void snd_seq_timer_delete(struct snd_seq_timer **tmr)
{
	struct snd_seq_timer *t = *tmr;
	*tmr = NULL;

	if (t == NULL) {
		pr_debug("ALSA: seq: snd_seq_timer_delete() called with NULL timer\n");
		return;
	}
	t->running = 0;

	/* reset time */
	snd_seq_timer_stop(t);
	snd_seq_timer_reset(t);

	kfree(t);
}

void snd_seq_timer_defaults(struct snd_seq_timer * tmr)
{
	guard(spinlock_irqsave)(&tmr->lock);
	/* setup defaults */
	tmr->ppq = 96;		/* 96 PPQ */
	tmr->tempo = 500000;	/* 120 BPM */
	tmr->tempo_base = 1000;	/* 1us */
	snd_seq_timer_set_tick_resolution(tmr);
	tmr->running = 0;

	tmr->type = SNDRV_SEQ_TIMER_ALSA;
	tmr->alsa_id.dev_class = seq_default_timer_class;
	tmr->alsa_id.dev_sclass = seq_default_timer_sclass;
	tmr->alsa_id.card = seq_default_timer_card;
	tmr->alsa_id.device = seq_default_timer_device;
	tmr->alsa_id.subdevice = seq_default_timer_subdevice;
	tmr->preferred_resolution = seq_default_timer_resolution;

	tmr->skew = tmr->skew_base = SKEW_BASE;
}

static void seq_timer_reset(struct snd_seq_timer *tmr)
{
	/* reset time & songposition */
	tmr->cur_time.tv_sec = 0;
	tmr->cur_time.tv_nsec = 0;

	tmr->tick.cur_tick = 0;
	tmr->tick.fraction = 0;
}

void snd_seq_timer_reset(struct snd_seq_timer *tmr)
{
	guard(spinlock_irqsave)(&tmr->lock);
	seq_timer_reset(tmr);
}


/* called by timer interrupt routine. the period time since previous invocation is passed */
static void snd_seq_timer_interrupt(struct snd_timer_instance *timeri,
				    unsigned long resolution,
				    unsigned long ticks)
{
	struct snd_seq_queue *q = timeri->callback_data;
	struct snd_seq_timer *tmr;

	if (q == NULL)
		return;
	tmr = q->timer;
	if (tmr == NULL)
		return;

	scoped_guard(spinlock_irqsave, &tmr->lock) {
		if (!tmr->running)
			return;

		resolution *= ticks;
		if (tmr->skew != tmr->skew_base) {
			/* FIXME: assuming skew_base = 0x10000 */
			resolution = (resolution >> 16) * tmr->skew +
				(((resolution & 0xffff) * tmr->skew) >> 16);
		}

		/* update timer */
		snd_seq_inc_time_nsec(&tmr->cur_time, resolution);

		/* calculate current tick */
		snd_seq_timer_update_tick(&tmr->tick, resolution);

		/* register actual time of this timer update */
		ktime_get_ts64(&tmr->last_update);
	}

	/* check queues and dispatch events */
	snd_seq_check_queue(q, 1, 0);
}

/* set current tempo */
int snd_seq_timer_set_tempo(struct snd_seq_timer * tmr, int tempo)
{
	if (snd_BUG_ON(!tmr))
		return -EINVAL;
	if (tempo <= 0)
		return -EINVAL;
	guard(spinlock_irqsave)(&tmr->lock);
	if ((unsigned int)tempo != tmr->tempo) {
		tmr->tempo = tempo;
		snd_seq_timer_set_tick_resolution(tmr);
	}
	return 0;
}

/* set current tempo, ppq and base in a shot */
int snd_seq_timer_set_tempo_ppq(struct snd_seq_timer *tmr, int tempo, int ppq,
				unsigned int tempo_base)
{
	int changed;

	if (snd_BUG_ON(!tmr))
		return -EINVAL;
	if (tempo <= 0 || ppq <= 0)
		return -EINVAL;
	/* allow only 10ns or 1us tempo base for now */
	if (tempo_base && tempo_base != 10 && tempo_base != 1000)
		return -EINVAL;
	guard(spinlock_irqsave)(&tmr->lock);
	if (tmr->running && (ppq != tmr->ppq)) {
		/* refuse to change ppq on running timers */
		/* because it will upset the song position (ticks) */
		pr_debug("ALSA: seq: cannot change ppq of a running timer\n");
		return -EBUSY;
	}
	changed = (tempo != tmr->tempo) || (ppq != tmr->ppq);
	tmr->tempo = tempo;
	tmr->ppq = ppq;
	tmr->tempo_base = tempo_base ? tempo_base : 1000;
	if (changed)
		snd_seq_timer_set_tick_resolution(tmr);
	return 0;
}

/* set current tick position */
int snd_seq_timer_set_position_tick(struct snd_seq_timer *tmr,
				    snd_seq_tick_time_t position)
{
	if (snd_BUG_ON(!tmr))
		return -EINVAL;

	guard(spinlock_irqsave)(&tmr->lock);
	tmr->tick.cur_tick = position;
	tmr->tick.fraction = 0;
	return 0;
}

/* set current real-time position */
int snd_seq_timer_set_position_time(struct snd_seq_timer *tmr,
				    snd_seq_real_time_t position)
{
	if (snd_BUG_ON(!tmr))
		return -EINVAL;

	snd_seq_sanity_real_time(&position);
	guard(spinlock_irqsave)(&tmr->lock);
	tmr->cur_time = position;
	return 0;
}

/* set timer skew */
int snd_seq_timer_set_skew(struct snd_seq_timer *tmr, unsigned int skew,
			   unsigned int base)
{
	if (snd_BUG_ON(!tmr))
		return -EINVAL;

	/* FIXME */
	if (base != SKEW_BASE) {
		pr_debug("ALSA: seq: invalid skew base 0x%x\n", base);
		return -EINVAL;
	}
	guard(spinlock_irqsave)(&tmr->lock);
	tmr->skew = skew;
	return 0;
}

int snd_seq_timer_open(struct snd_seq_queue *q)
{
	struct snd_timer_instance *t;
	struct snd_seq_timer *tmr;
	char str[32];
	int err;

	tmr = q->timer;
	if (snd_BUG_ON(!tmr))
		return -EINVAL;
	if (tmr->timeri)
		return -EBUSY;
	sprintf(str, "sequencer queue %i", q->queue);
	if (tmr->type != SNDRV_SEQ_TIMER_ALSA)	/* standard ALSA timer */
		return -EINVAL;
	if (tmr->alsa_id.dev_class != SNDRV_TIMER_CLASS_SLAVE)
		tmr->alsa_id.dev_sclass = SNDRV_TIMER_SCLASS_SEQUENCER;
	t = snd_timer_instance_new(str);
	if (!t)
		return -ENOMEM;
	t->callback = snd_seq_timer_interrupt;
	t->callback_data = q;
	t->flags |= SNDRV_TIMER_IFLG_AUTO;
	err = snd_timer_open(t, &tmr->alsa_id, q->queue);
	if (err < 0 && tmr->alsa_id.dev_class != SNDRV_TIMER_CLASS_SLAVE) {
		if (tmr->alsa_id.dev_class != SNDRV_TIMER_CLASS_GLOBAL ||
		    tmr->alsa_id.device != SNDRV_TIMER_GLOBAL_SYSTEM) {
			struct snd_timer_id tid;
			memset(&tid, 0, sizeof(tid));
			tid.dev_class = SNDRV_TIMER_CLASS_GLOBAL;
			tid.dev_sclass = SNDRV_TIMER_SCLASS_SEQUENCER;
			tid.card = -1;
			tid.device = SNDRV_TIMER_GLOBAL_SYSTEM;
			err = snd_timer_open(t, &tid, q->queue);
		}
	}
	if (err < 0) {
		pr_err("ALSA: seq fatal error: cannot create timer (%i)\n", err);
		snd_timer_instance_free(t);
		return err;
	}
	scoped_guard(spinlock_irq, &tmr->lock) {
		if (tmr->timeri)
			err = -EBUSY;
		else
			tmr->timeri = t;
	}
	if (err < 0) {
		snd_timer_close(t);
		snd_timer_instance_free(t);
		return err;
	}
	return 0;
}

int snd_seq_timer_close(struct snd_seq_queue *q)
{
	struct snd_seq_timer *tmr;
	struct snd_timer_instance *t;
	
	tmr = q->timer;
	if (snd_BUG_ON(!tmr))
		return -EINVAL;
	scoped_guard(spinlock_irq, &tmr->lock) {
		t = tmr->timeri;
		tmr->timeri = NULL;
	}
	if (t) {
		snd_timer_close(t);
		snd_timer_instance_free(t);
	}
	return 0;
}

static int seq_timer_stop(struct snd_seq_timer *tmr)
{
	if (! tmr->timeri)
		return -EINVAL;
	if (!tmr->running)
		return 0;
	tmr->running = 0;
	snd_timer_pause(tmr->timeri);
	return 0;
}

int snd_seq_timer_stop(struct snd_seq_timer *tmr)
{
	guard(spinlock_irqsave)(&tmr->lock);
	return seq_timer_stop(tmr);
}

static int initialize_timer(struct snd_seq_timer *tmr)
{
	struct snd_timer *t;
	unsigned long freq;

	t = tmr->timeri->timer;
	if (!t)
		return -EINVAL;

	freq = tmr->preferred_resolution;
	if (!freq)
		freq = DEFAULT_FREQUENCY;
	else if (freq < MIN_FREQUENCY)
		freq = MIN_FREQUENCY;
	else if (freq > MAX_FREQUENCY)
		freq = MAX_FREQUENCY;

	tmr->ticks = 1;
	if (!(t->hw.flags & SNDRV_TIMER_HW_SLAVE)) {
		unsigned long r = snd_timer_resolution(tmr->timeri);
		if (r) {
			tmr->ticks = (unsigned int)(1000000000uL / (r * freq));
			if (! tmr->ticks)
				tmr->ticks = 1;
		}
	}
	tmr->initialized = 1;
	return 0;
}

static int seq_timer_start(struct snd_seq_timer *tmr)
{
	if (! tmr->timeri)
		return -EINVAL;
	if (tmr->running)
		seq_timer_stop(tmr);
	seq_timer_reset(tmr);
	if (initialize_timer(tmr) < 0)
		return -EINVAL;
	snd_timer_start(tmr->timeri, tmr->ticks);
	tmr->running = 1;
	ktime_get_ts64(&tmr->last_update);
	return 0;
}

int snd_seq_timer_start(struct snd_seq_timer *tmr)
{
	guard(spinlock_irqsave)(&tmr->lock);
	return seq_timer_start(tmr);
}

static int seq_timer_continue(struct snd_seq_timer *tmr)
{
	if (! tmr->timeri)
		return -EINVAL;
	if (tmr->running)
		return -EBUSY;
	if (! tmr->initialized) {
		seq_timer_reset(tmr);
		if (initialize_timer(tmr) < 0)
			return -EINVAL;
	}
	snd_timer_start(tmr->timeri, tmr->ticks);
	tmr->running = 1;
	ktime_get_ts64(&tmr->last_update);
	return 0;
}

int snd_seq_timer_continue(struct snd_seq_timer *tmr)
{
	guard(spinlock_irqsave)(&tmr->lock);
	return seq_timer_continue(tmr);
}

/* return current 'real' time. use timeofday() to get better granularity. */
snd_seq_real_time_t snd_seq_timer_get_cur_time(struct snd_seq_timer *tmr,
					       bool adjust_ktime)
{
	snd_seq_real_time_t cur_time;

	guard(spinlock_irqsave)(&tmr->lock);
	cur_time = tmr->cur_time;
	if (adjust_ktime && tmr->running) {
		struct timespec64 tm;

		ktime_get_ts64(&tm);
		tm = timespec64_sub(tm, tmr->last_update);
		cur_time.tv_nsec += tm.tv_nsec;
		cur_time.tv_sec += tm.tv_sec;
		snd_seq_sanity_real_time(&cur_time);
	}
	return cur_time;	
}

/* TODO: use interpolation on tick queue (will only be useful for very
 high PPQ values) */
snd_seq_tick_time_t snd_seq_timer_get_cur_tick(struct snd_seq_timer *tmr)
{
	guard(spinlock_irqsave)(&tmr->lock);
	return tmr->tick.cur_tick;
}


#ifdef CONFIG_SND_PROC_FS
/* exported to seq_info.c */
void snd_seq_info_timer_read(struct snd_info_entry *entry,
			     struct snd_info_buffer *buffer)
{
	int idx;
	struct snd_seq_timer *tmr;
	struct snd_timer_instance *ti;
	unsigned long resolution;
	
	for (idx = 0; idx < SNDRV_SEQ_MAX_QUEUES; idx++) {
		struct snd_seq_queue *q __free(snd_seq_queue) = queueptr(idx);

		if (q == NULL)
			continue;
		scoped_guard(mutex, &q->timer_mutex) {
			tmr = q->timer;
			if (!tmr)
				break;
			ti = tmr->timeri;
			if (!ti)
				break;
			snd_iprintf(buffer, "Timer for queue %i : %s\n", q->queue, ti->timer->name);
			resolution = snd_timer_resolution(ti) * tmr->ticks;
			snd_iprintf(buffer, "  Period time : %lu.%09lu\n", resolution / 1000000000, resolution % 1000000000);
			snd_iprintf(buffer, "  Skew : %u / %u\n", tmr->skew, tmr->skew_base);
		}
 	}
}
#endif /* CONFIG_SND_PROC_FS */

