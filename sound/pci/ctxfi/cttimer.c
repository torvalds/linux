/*
 * PCM timer handling on ctxfi
 *
 * This source file is released under GPL v2 license (no other versions).
 * See the COPYING file included in the main directory of this source
 * distribution for the license terms and conditions.
 */

#include <linux/slab.h>
#include <linux/math64.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include "ctatc.h"
#include "cthardware.h"
#include "cttimer.h"

static bool use_system_timer;
MODULE_PARM_DESC(use_system_timer, "Force to use system-timer");
module_param(use_system_timer, bool, 0444);

struct ct_timer_ops {
	void (*init)(struct ct_timer_instance *);
	void (*prepare)(struct ct_timer_instance *);
	void (*start)(struct ct_timer_instance *);
	void (*stop)(struct ct_timer_instance *);
	void (*free_instance)(struct ct_timer_instance *);
	void (*interrupt)(struct ct_timer *);
	void (*free_global)(struct ct_timer *);
};

/* timer instance -- assigned to each PCM stream */
struct ct_timer_instance {
	spinlock_t lock;
	struct ct_timer *timer_base;
	struct ct_atc_pcm *apcm;
	struct snd_pcm_substream *substream;
	struct timer_list timer;
	struct list_head instance_list;
	struct list_head running_list;
	unsigned int position;
	unsigned int frag_count;
	unsigned int running:1;
	unsigned int need_update:1;
};

/* timer instance manager */
struct ct_timer {
	spinlock_t lock;		/* global timer lock (for xfitimer) */
	spinlock_t list_lock;		/* lock for instance list */
	struct ct_atc *atc;
	const struct ct_timer_ops *ops;
	struct list_head instance_head;
	struct list_head running_head;
	unsigned int wc;		/* current wallclock */
	unsigned int irq_handling:1;	/* in IRQ handling */
	unsigned int reprogram:1;	/* need to reprogram the internval */
	unsigned int running:1;		/* global timer running */
};


/*
 * system-timer-based updates
 */

static void ct_systimer_callback(struct timer_list *t)
{
	struct ct_timer_instance *ti = from_timer(ti, t, timer);
	struct snd_pcm_substream *substream = ti->substream;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct ct_atc_pcm *apcm = ti->apcm;
	unsigned int period_size = runtime->period_size;
	unsigned int buffer_size = runtime->buffer_size;
	unsigned long flags;
	unsigned int position, dist, interval;

	position = substream->ops->pointer(substream);
	dist = (position + buffer_size - ti->position) % buffer_size;
	if (dist >= period_size ||
	    position / period_size != ti->position / period_size) {
		apcm->interrupt(apcm);
		ti->position = position;
	}
	/* Add extra HZ*5/1000 to avoid overrun issue when recording
	 * at 8kHz in 8-bit format or at 88kHz in 24-bit format. */
	interval = ((period_size - (position % period_size))
		   * HZ + (runtime->rate - 1)) / runtime->rate + HZ * 5 / 1000;
	spin_lock_irqsave(&ti->lock, flags);
	if (ti->running)
		mod_timer(&ti->timer, jiffies + interval);
	spin_unlock_irqrestore(&ti->lock, flags);
}

static void ct_systimer_init(struct ct_timer_instance *ti)
{
	timer_setup(&ti->timer, ct_systimer_callback, 0);
}

static void ct_systimer_start(struct ct_timer_instance *ti)
{
	struct snd_pcm_runtime *runtime = ti->substream->runtime;
	unsigned long flags;

	spin_lock_irqsave(&ti->lock, flags);
	ti->running = 1;
	mod_timer(&ti->timer,
		  jiffies + (runtime->period_size * HZ +
			     (runtime->rate - 1)) / runtime->rate);
	spin_unlock_irqrestore(&ti->lock, flags);
}

static void ct_systimer_stop(struct ct_timer_instance *ti)
{
	unsigned long flags;

	spin_lock_irqsave(&ti->lock, flags);
	ti->running = 0;
	del_timer(&ti->timer);
	spin_unlock_irqrestore(&ti->lock, flags);
}

static void ct_systimer_prepare(struct ct_timer_instance *ti)
{
	ct_systimer_stop(ti);
	try_to_del_timer_sync(&ti->timer);
}

#define ct_systimer_free	ct_systimer_prepare

static const struct ct_timer_ops ct_systimer_ops = {
	.init = ct_systimer_init,
	.free_instance = ct_systimer_free,
	.prepare = ct_systimer_prepare,
	.start = ct_systimer_start,
	.stop = ct_systimer_stop,
};


/*
 * Handling multiple streams using a global emu20k1 timer irq
 */

#define CT_TIMER_FREQ	48000
#define MIN_TICKS	1
#define MAX_TICKS	((1 << 13) - 1)

static void ct_xfitimer_irq_rearm(struct ct_timer *atimer, int ticks)
{
	struct hw *hw = atimer->atc->hw;
	if (ticks > MAX_TICKS)
		ticks = MAX_TICKS;
	hw->set_timer_tick(hw, ticks);
	if (!atimer->running)
		hw->set_timer_irq(hw, 1);
	atimer->running = 1;
}

static void ct_xfitimer_irq_stop(struct ct_timer *atimer)
{
	if (atimer->running) {
		struct hw *hw = atimer->atc->hw;
		hw->set_timer_irq(hw, 0);
		hw->set_timer_tick(hw, 0);
		atimer->running = 0;
	}
}

static inline unsigned int ct_xfitimer_get_wc(struct ct_timer *atimer)
{
	struct hw *hw = atimer->atc->hw;
	return hw->get_wc(hw);
}

/*
 * reprogram the timer interval;
 * checks the running instance list and determines the next timer interval.
 * also updates the each stream position, returns the number of streams
 * to call snd_pcm_period_elapsed() appropriately
 *
 * call this inside the lock and irq disabled
 */
static int ct_xfitimer_reprogram(struct ct_timer *atimer, int can_update)
{
	struct ct_timer_instance *ti;
	unsigned int min_intr = (unsigned int)-1;
	int updates = 0;
	unsigned int wc, diff;

	if (list_empty(&atimer->running_head)) {
		ct_xfitimer_irq_stop(atimer);
		atimer->reprogram = 0; /* clear flag */
		return 0;
	}

	wc = ct_xfitimer_get_wc(atimer);
	diff = wc - atimer->wc;
	atimer->wc = wc;
	list_for_each_entry(ti, &atimer->running_head, running_list) {
		if (ti->frag_count > diff)
			ti->frag_count -= diff;
		else {
			unsigned int pos;
			unsigned int period_size, rate;

			period_size = ti->substream->runtime->period_size;
			rate = ti->substream->runtime->rate;
			pos = ti->substream->ops->pointer(ti->substream);
			if (pos / period_size != ti->position / period_size) {
				ti->need_update = 1;
				ti->position = pos;
				updates++;
			}
			pos %= period_size;
			pos = period_size - pos;
			ti->frag_count = div_u64((u64)pos * CT_TIMER_FREQ +
						 rate - 1, rate);
		}
		if (ti->need_update && !can_update)
			min_intr = 0; /* pending to the next irq */
		if (ti->frag_count < min_intr)
			min_intr = ti->frag_count;
	}

	if (min_intr < MIN_TICKS)
		min_intr = MIN_TICKS;
	ct_xfitimer_irq_rearm(atimer, min_intr);
	atimer->reprogram = 0; /* clear flag */
	return updates;
}

/* look through the instance list and call period_elapsed if needed */
static void ct_xfitimer_check_period(struct ct_timer *atimer)
{
	struct ct_timer_instance *ti;
	unsigned long flags;

	spin_lock_irqsave(&atimer->list_lock, flags);
	list_for_each_entry(ti, &atimer->instance_head, instance_list) {
		if (ti->running && ti->need_update) {
			ti->need_update = 0;
			ti->apcm->interrupt(ti->apcm);
		}
	}
	spin_unlock_irqrestore(&atimer->list_lock, flags);
}

/* Handle timer-interrupt */
static void ct_xfitimer_callback(struct ct_timer *atimer)
{
	int update;
	unsigned long flags;

	spin_lock_irqsave(&atimer->lock, flags);
	atimer->irq_handling = 1;
	do {
		update = ct_xfitimer_reprogram(atimer, 1);
		spin_unlock(&atimer->lock);
		if (update)
			ct_xfitimer_check_period(atimer);
		spin_lock(&atimer->lock);
	} while (atimer->reprogram);
	atimer->irq_handling = 0;
	spin_unlock_irqrestore(&atimer->lock, flags);
}

static void ct_xfitimer_prepare(struct ct_timer_instance *ti)
{
	ti->frag_count = ti->substream->runtime->period_size;
	ti->running = 0;
	ti->need_update = 0;
}


/* start/stop the timer */
static void ct_xfitimer_update(struct ct_timer *atimer)
{
	unsigned long flags;

	spin_lock_irqsave(&atimer->lock, flags);
	if (atimer->irq_handling) {
		/* reached from IRQ handler; let it handle later */
		atimer->reprogram = 1;
		spin_unlock_irqrestore(&atimer->lock, flags);
		return;
	}

	ct_xfitimer_irq_stop(atimer);
	ct_xfitimer_reprogram(atimer, 0);
	spin_unlock_irqrestore(&atimer->lock, flags);
}

static void ct_xfitimer_start(struct ct_timer_instance *ti)
{
	struct ct_timer *atimer = ti->timer_base;
	unsigned long flags;

	spin_lock_irqsave(&atimer->lock, flags);
	if (list_empty(&ti->running_list))
		atimer->wc = ct_xfitimer_get_wc(atimer);
	ti->running = 1;
	ti->need_update = 0;
	list_add(&ti->running_list, &atimer->running_head);
	spin_unlock_irqrestore(&atimer->lock, flags);
	ct_xfitimer_update(atimer);
}

static void ct_xfitimer_stop(struct ct_timer_instance *ti)
{
	struct ct_timer *atimer = ti->timer_base;
	unsigned long flags;

	spin_lock_irqsave(&atimer->lock, flags);
	list_del_init(&ti->running_list);
	ti->running = 0;
	spin_unlock_irqrestore(&atimer->lock, flags);
	ct_xfitimer_update(atimer);
}

static void ct_xfitimer_free_global(struct ct_timer *atimer)
{
	ct_xfitimer_irq_stop(atimer);
}

static const struct ct_timer_ops ct_xfitimer_ops = {
	.prepare = ct_xfitimer_prepare,
	.start = ct_xfitimer_start,
	.stop = ct_xfitimer_stop,
	.interrupt = ct_xfitimer_callback,
	.free_global = ct_xfitimer_free_global,
};

/*
 * timer instance
 */

struct ct_timer_instance *
ct_timer_instance_new(struct ct_timer *atimer, struct ct_atc_pcm *apcm)
{
	struct ct_timer_instance *ti;

	ti = kzalloc(sizeof(*ti), GFP_KERNEL);
	if (!ti)
		return NULL;
	spin_lock_init(&ti->lock);
	INIT_LIST_HEAD(&ti->instance_list);
	INIT_LIST_HEAD(&ti->running_list);
	ti->timer_base = atimer;
	ti->apcm = apcm;
	ti->substream = apcm->substream;
	if (atimer->ops->init)
		atimer->ops->init(ti);

	spin_lock_irq(&atimer->list_lock);
	list_add(&ti->instance_list, &atimer->instance_head);
	spin_unlock_irq(&atimer->list_lock);

	return ti;
}

void ct_timer_prepare(struct ct_timer_instance *ti)
{
	if (ti->timer_base->ops->prepare)
		ti->timer_base->ops->prepare(ti);
	ti->position = 0;
	ti->running = 0;
}

void ct_timer_start(struct ct_timer_instance *ti)
{
	struct ct_timer *atimer = ti->timer_base;
	atimer->ops->start(ti);
}

void ct_timer_stop(struct ct_timer_instance *ti)
{
	struct ct_timer *atimer = ti->timer_base;
	atimer->ops->stop(ti);
}

void ct_timer_instance_free(struct ct_timer_instance *ti)
{
	struct ct_timer *atimer = ti->timer_base;

	atimer->ops->stop(ti); /* to be sure */
	if (atimer->ops->free_instance)
		atimer->ops->free_instance(ti);

	spin_lock_irq(&atimer->list_lock);
	list_del(&ti->instance_list);
	spin_unlock_irq(&atimer->list_lock);

	kfree(ti);
}

/*
 * timer manager
 */

static void ct_timer_interrupt(void *data, unsigned int status)
{
	struct ct_timer *timer = data;

	/* Interval timer interrupt */
	if ((status & IT_INT) && timer->ops->interrupt)
		timer->ops->interrupt(timer);
}

struct ct_timer *ct_timer_new(struct ct_atc *atc)
{
	struct ct_timer *atimer;
	struct hw *hw;

	atimer = kzalloc(sizeof(*atimer), GFP_KERNEL);
	if (!atimer)
		return NULL;
	spin_lock_init(&atimer->lock);
	spin_lock_init(&atimer->list_lock);
	INIT_LIST_HEAD(&atimer->instance_head);
	INIT_LIST_HEAD(&atimer->running_head);
	atimer->atc = atc;
	hw = atc->hw;
	if (!use_system_timer && hw->set_timer_irq) {
		dev_info(atc->card->dev, "Use xfi-native timer\n");
		atimer->ops = &ct_xfitimer_ops;
		hw->irq_callback_data = atimer;
		hw->irq_callback = ct_timer_interrupt;
	} else {
		dev_info(atc->card->dev, "Use system timer\n");
		atimer->ops = &ct_systimer_ops;
	}
	return atimer;
}

void ct_timer_free(struct ct_timer *atimer)
{
	struct hw *hw = atimer->atc->hw;
	hw->irq_callback = NULL;
	if (atimer->ops->free_global)
		atimer->ops->free_global(atimer);
	kfree(atimer);
}

