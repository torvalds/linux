/*
 *  Digital Audio (PCM) abstract layer
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *                   Abramo Bagnara <abramo@alsa-project.org>
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

#include <linux/slab.h>
#include <linux/time.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/info.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/timer.h>

/*
 * fill ring buffer with silence
 * runtime->silence_start: starting pointer to silence area
 * runtime->silence_filled: size filled with silence
 * runtime->silence_threshold: threshold from application
 * runtime->silence_size: maximal size from application
 *
 * when runtime->silence_size >= runtime->boundary - fill processed area with silence immediately
 */
void snd_pcm_playback_silence(struct snd_pcm_substream *substream, snd_pcm_uframes_t new_hw_ptr)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	snd_pcm_uframes_t frames, ofs, transfer;

	if (runtime->silence_size < runtime->boundary) {
		snd_pcm_sframes_t noise_dist, n;
		if (runtime->silence_start != runtime->control->appl_ptr) {
			n = runtime->control->appl_ptr - runtime->silence_start;
			if (n < 0)
				n += runtime->boundary;
			if ((snd_pcm_uframes_t)n < runtime->silence_filled)
				runtime->silence_filled -= n;
			else
				runtime->silence_filled = 0;
			runtime->silence_start = runtime->control->appl_ptr;
		}
		if (runtime->silence_filled >= runtime->buffer_size)
			return;
		noise_dist = snd_pcm_playback_hw_avail(runtime) + runtime->silence_filled;
		if (noise_dist >= (snd_pcm_sframes_t) runtime->silence_threshold)
			return;
		frames = runtime->silence_threshold - noise_dist;
		if (frames > runtime->silence_size)
			frames = runtime->silence_size;
	} else {
		if (new_hw_ptr == ULONG_MAX) {	/* initialization */
			snd_pcm_sframes_t avail = snd_pcm_playback_hw_avail(runtime);
			runtime->silence_filled = avail > 0 ? avail : 0;
			runtime->silence_start = (runtime->status->hw_ptr +
						  runtime->silence_filled) %
						 runtime->boundary;
		} else {
			ofs = runtime->status->hw_ptr;
			frames = new_hw_ptr - ofs;
			if ((snd_pcm_sframes_t)frames < 0)
				frames += runtime->boundary;
			runtime->silence_filled -= frames;
			if ((snd_pcm_sframes_t)runtime->silence_filled < 0) {
				runtime->silence_filled = 0;
				runtime->silence_start = new_hw_ptr;
			} else {
				runtime->silence_start = ofs;
			}
		}
		frames = runtime->buffer_size - runtime->silence_filled;
	}
	if (snd_BUG_ON(frames > runtime->buffer_size))
		return;
	if (frames == 0)
		return;
	ofs = runtime->silence_start % runtime->buffer_size;
	while (frames > 0) {
		transfer = ofs + frames > runtime->buffer_size ? runtime->buffer_size - ofs : frames;
		if (runtime->access == SNDRV_PCM_ACCESS_RW_INTERLEAVED ||
		    runtime->access == SNDRV_PCM_ACCESS_MMAP_INTERLEAVED) {
			if (substream->ops->silence) {
				int err;
				err = substream->ops->silence(substream, -1, ofs, transfer);
				snd_BUG_ON(err < 0);
			} else {
				char *hwbuf = runtime->dma_area + frames_to_bytes(runtime, ofs);
				snd_pcm_format_set_silence(runtime->format, hwbuf, transfer * runtime->channels);
			}
		} else {
			unsigned int c;
			unsigned int channels = runtime->channels;
			if (substream->ops->silence) {
				for (c = 0; c < channels; ++c) {
					int err;
					err = substream->ops->silence(substream, c, ofs, transfer);
					snd_BUG_ON(err < 0);
				}
			} else {
				size_t dma_csize = runtime->dma_bytes / channels;
				for (c = 0; c < channels; ++c) {
					char *hwbuf = runtime->dma_area + (c * dma_csize) + samples_to_bytes(runtime, ofs);
					snd_pcm_format_set_silence(runtime->format, hwbuf, transfer);
				}
			}
		}
		runtime->silence_filled += transfer;
		frames -= transfer;
		ofs = 0;
	}
}

#ifdef CONFIG_SND_PCM_XRUN_DEBUG
#define xrun_debug(substream)	((substream)->pstr->xrun_debug)
#else
#define xrun_debug(substream)	0
#endif

#define dump_stack_on_xrun(substream) do {	\
		if (xrun_debug(substream) > 1)	\
			dump_stack();		\
	} while (0)

static void xrun(struct snd_pcm_substream *substream)
{
	snd_pcm_stop(substream, SNDRV_PCM_STATE_XRUN);
	if (xrun_debug(substream)) {
		snd_printd(KERN_DEBUG "XRUN: pcmC%dD%d%c\n",
			   substream->pcm->card->number,
			   substream->pcm->device,
			   substream->stream ? 'c' : 'p');
		dump_stack_on_xrun(substream);
	}
}

static snd_pcm_uframes_t
snd_pcm_update_hw_ptr_pos(struct snd_pcm_substream *substream,
			  struct snd_pcm_runtime *runtime)
{
	snd_pcm_uframes_t pos;

	if (runtime->tstamp_mode == SNDRV_PCM_TSTAMP_ENABLE)
		snd_pcm_gettime(runtime, (struct timespec *)&runtime->status->tstamp);
	pos = substream->ops->pointer(substream);
	if (pos == SNDRV_PCM_POS_XRUN)
		return pos; /* XRUN */
	if (pos >= runtime->buffer_size) {
		if (printk_ratelimit()) {
			snd_printd(KERN_ERR  "BUG: stream = %i, pos = 0x%lx, "
				   "buffer size = 0x%lx, period size = 0x%lx\n",
				   substream->stream, pos, runtime->buffer_size,
				   runtime->period_size);
		}
		pos = 0;
	}
	pos -= pos % runtime->min_align;
	return pos;
}

static int snd_pcm_update_hw_ptr_post(struct snd_pcm_substream *substream,
				      struct snd_pcm_runtime *runtime)
{
	snd_pcm_uframes_t avail;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		avail = snd_pcm_playback_avail(runtime);
	else
		avail = snd_pcm_capture_avail(runtime);
	if (avail > runtime->avail_max)
		runtime->avail_max = avail;
	if (avail >= runtime->stop_threshold) {
		if (substream->runtime->status->state == SNDRV_PCM_STATE_DRAINING)
			snd_pcm_drain_done(substream);
		else
			xrun(substream);
		return -EPIPE;
	}
	if (avail >= runtime->control->avail_min)
		wake_up(&runtime->sleep);
	return 0;
}

#define hw_ptr_error(substream, fmt, args...)				\
	do {								\
		if (xrun_debug(substream)) {				\
			if (printk_ratelimit()) {			\
				snd_printd("PCM: " fmt, ##args);	\
			}						\
			dump_stack_on_xrun(substream);			\
		}							\
	} while (0)

static int snd_pcm_update_hw_ptr_interrupt(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	snd_pcm_uframes_t pos;
	snd_pcm_uframes_t old_hw_ptr, new_hw_ptr, hw_ptr_interrupt, hw_base;
	snd_pcm_sframes_t hdelta, delta;
	unsigned long jdelta;

	old_hw_ptr = runtime->status->hw_ptr;
	pos = snd_pcm_update_hw_ptr_pos(substream, runtime);
	if (pos == SNDRV_PCM_POS_XRUN) {
		xrun(substream);
		return -EPIPE;
	}
	hw_base = runtime->hw_ptr_base;
	new_hw_ptr = hw_base + pos;
	hw_ptr_interrupt = runtime->hw_ptr_interrupt + runtime->period_size;
	delta = new_hw_ptr - hw_ptr_interrupt;
	if (hw_ptr_interrupt >= runtime->boundary) {
		hw_ptr_interrupt -= runtime->boundary;
		if (hw_base < runtime->boundary / 2)
			/* hw_base was already lapped; recalc delta */
			delta = new_hw_ptr - hw_ptr_interrupt;
	}
	if (delta < 0) {
		delta += runtime->buffer_size;
		if (delta < 0) {
			hw_ptr_error(substream, 
				     "Unexpected hw_pointer value "
				     "(stream=%i, pos=%ld, intr_ptr=%ld)\n",
				     substream->stream, (long)pos,
				     (long)hw_ptr_interrupt);
			/* rebase to interrupt position */
			hw_base = new_hw_ptr = hw_ptr_interrupt;
			/* align hw_base to buffer_size */
			hw_base -= hw_base % runtime->buffer_size;
			delta = 0;
		} else {
			hw_base += runtime->buffer_size;
			if (hw_base >= runtime->boundary)
				hw_base = 0;
			new_hw_ptr = hw_base + pos;
		}
	}
	hdelta = new_hw_ptr - old_hw_ptr;
	jdelta = jiffies - runtime->hw_ptr_jiffies;
	if (((hdelta * HZ) / runtime->rate) > jdelta + HZ/100) {
		delta = jdelta /
			(((runtime->period_size * HZ) / runtime->rate)
								+ HZ/100);
		hw_ptr_error(substream,
			     "hw_ptr skipping! [Q] "
			     "(pos=%ld, delta=%ld, period=%ld, "
			     "jdelta=%lu/%lu/%lu)\n",
			     (long)pos, (long)hdelta,
			     (long)runtime->period_size, jdelta,
			     ((hdelta * HZ) / runtime->rate), delta);
		hw_ptr_interrupt = runtime->hw_ptr_interrupt +
				   runtime->period_size * delta;
		if (hw_ptr_interrupt >= runtime->boundary)
			hw_ptr_interrupt -= runtime->boundary;
		/* rebase to interrupt position */
		hw_base = new_hw_ptr = hw_ptr_interrupt;
		/* align hw_base to buffer_size */
		hw_base -= hw_base % runtime->buffer_size;
		delta = 0;
	}
	if (delta > runtime->period_size + runtime->period_size / 2) {
		hw_ptr_error(substream,
			     "Lost interrupts? "
			     "(stream=%i, delta=%ld, intr_ptr=%ld)\n",
			     substream->stream, (long)delta,
			     (long)hw_ptr_interrupt);
		/* rebase hw_ptr_interrupt */
		hw_ptr_interrupt =
			new_hw_ptr - new_hw_ptr % runtime->period_size;
	}
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK &&
	    runtime->silence_size > 0)
		snd_pcm_playback_silence(substream, new_hw_ptr);

	runtime->hw_ptr_base = hw_base;
	runtime->status->hw_ptr = new_hw_ptr;
	runtime->hw_ptr_jiffies = jiffies;
	runtime->hw_ptr_interrupt = hw_ptr_interrupt;

	return snd_pcm_update_hw_ptr_post(substream, runtime);
}

/* CAUTION: call it with irq disabled */
int snd_pcm_update_hw_ptr(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	snd_pcm_uframes_t pos;
	snd_pcm_uframes_t old_hw_ptr, new_hw_ptr, hw_base;
	snd_pcm_sframes_t delta;
	unsigned long jdelta;

	old_hw_ptr = runtime->status->hw_ptr;
	pos = snd_pcm_update_hw_ptr_pos(substream, runtime);
	if (pos == SNDRV_PCM_POS_XRUN) {
		xrun(substream);
		return -EPIPE;
	}
	hw_base = runtime->hw_ptr_base;
	new_hw_ptr = hw_base + pos;

	delta = new_hw_ptr - old_hw_ptr;
	jdelta = jiffies - runtime->hw_ptr_jiffies;
	if (delta < 0) {
		delta += runtime->buffer_size;
		if (delta < 0) {
			hw_ptr_error(substream, 
				     "Unexpected hw_pointer value [2] "
				     "(stream=%i, pos=%ld, old_ptr=%ld, jdelta=%li)\n",
				     substream->stream, (long)pos,
				     (long)old_hw_ptr, jdelta);
			return 0;
		}
		hw_base += runtime->buffer_size;
		if (hw_base >= runtime->boundary)
			hw_base = 0;
		new_hw_ptr = hw_base + pos;
	}
	if (((delta * HZ) / runtime->rate) > jdelta + HZ/100) {
		hw_ptr_error(substream,
			     "hw_ptr skipping! "
			     "(pos=%ld, delta=%ld, period=%ld, jdelta=%lu/%lu)\n",
			     (long)pos, (long)delta,
			     (long)runtime->period_size, jdelta,
			     ((delta * HZ) / runtime->rate));
		return 0;
	}
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK &&
	    runtime->silence_size > 0)
		snd_pcm_playback_silence(substream, new_hw_ptr);

	runtime->hw_ptr_base = hw_base;
	runtime->status->hw_ptr = new_hw_ptr;
	runtime->hw_ptr_jiffies = jiffies;

	return snd_pcm_update_hw_ptr_post(substream, runtime);
}

/**
 * snd_pcm_set_ops - set the PCM operators
 * @pcm: the pcm instance
 * @direction: stream direction, SNDRV_PCM_STREAM_XXX
 * @ops: the operator table
 *
 * Sets the given PCM operators to the pcm instance.
 */
void snd_pcm_set_ops(struct snd_pcm *pcm, int direction, struct snd_pcm_ops *ops)
{
	struct snd_pcm_str *stream = &pcm->streams[direction];
	struct snd_pcm_substream *substream;
	
	for (substream = stream->substream; substream != NULL; substream = substream->next)
		substream->ops = ops;
}

EXPORT_SYMBOL(snd_pcm_set_ops);

/**
 * snd_pcm_sync - set the PCM sync id
 * @substream: the pcm substream
 *
 * Sets the PCM sync identifier for the card.
 */
void snd_pcm_set_sync(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	
	runtime->sync.id32[0] = substream->pcm->card->number;
	runtime->sync.id32[1] = -1;
	runtime->sync.id32[2] = -1;
	runtime->sync.id32[3] = -1;
}

EXPORT_SYMBOL(snd_pcm_set_sync);

/*
 *  Standard ioctl routine
 */

static inline unsigned int div32(unsigned int a, unsigned int b, 
				 unsigned int *r)
{
	if (b == 0) {
		*r = 0;
		return UINT_MAX;
	}
	*r = a % b;
	return a / b;
}

static inline unsigned int div_down(unsigned int a, unsigned int b)
{
	if (b == 0)
		return UINT_MAX;
	return a / b;
}

static inline unsigned int div_up(unsigned int a, unsigned int b)
{
	unsigned int r;
	unsigned int q;
	if (b == 0)
		return UINT_MAX;
	q = div32(a, b, &r);
	if (r)
		++q;
	return q;
}

static inline unsigned int mul(unsigned int a, unsigned int b)
{
	if (a == 0)
		return 0;
	if (div_down(UINT_MAX, a) < b)
		return UINT_MAX;
	return a * b;
}

static inline unsigned int muldiv32(unsigned int a, unsigned int b,
				    unsigned int c, unsigned int *r)
{
	u_int64_t n = (u_int64_t) a * b;
	if (c == 0) {
		snd_BUG_ON(!n);
		*r = 0;
		return UINT_MAX;
	}
	div64_32(&n, c, r);
	if (n >= UINT_MAX) {
		*r = 0;
		return UINT_MAX;
	}
	return n;
}

/**
 * snd_interval_refine - refine the interval value of configurator
 * @i: the interval value to refine
 * @v: the interval value to refer to
 *
 * Refines the interval value with the reference value.
 * The interval is changed to the range satisfying both intervals.
 * The interval status (min, max, integer, etc.) are evaluated.
 *
 * Returns non-zero if the value is changed, zero if not changed.
 */
int snd_interval_refine(struct snd_interval *i, const struct snd_interval *v)
{
	int changed = 0;
	if (snd_BUG_ON(snd_interval_empty(i)))
		return -EINVAL;
	if (i->min < v->min) {
		i->min = v->min;
		i->openmin = v->openmin;
		changed = 1;
	} else if (i->min == v->min && !i->openmin && v->openmin) {
		i->openmin = 1;
		changed = 1;
	}
	if (i->max > v->max) {
		i->max = v->max;
		i->openmax = v->openmax;
		changed = 1;
	} else if (i->max == v->max && !i->openmax && v->openmax) {
		i->openmax = 1;
		changed = 1;
	}
	if (!i->integer && v->integer) {
		i->integer = 1;
		changed = 1;
	}
	if (i->integer) {
		if (i->openmin) {
			i->min++;
			i->openmin = 0;
		}
		if (i->openmax) {
			i->max--;
			i->openmax = 0;
		}
	} else if (!i->openmin && !i->openmax && i->min == i->max)
		i->integer = 1;
	if (snd_interval_checkempty(i)) {
		snd_interval_none(i);
		return -EINVAL;
	}
	return changed;
}

EXPORT_SYMBOL(snd_interval_refine);

static int snd_interval_refine_first(struct snd_interval *i)
{
	if (snd_BUG_ON(snd_interval_empty(i)))
		return -EINVAL;
	if (snd_interval_single(i))
		return 0;
	i->max = i->min;
	i->openmax = i->openmin;
	if (i->openmax)
		i->max++;
	return 1;
}

static int snd_interval_refine_last(struct snd_interval *i)
{
	if (snd_BUG_ON(snd_interval_empty(i)))
		return -EINVAL;
	if (snd_interval_single(i))
		return 0;
	i->min = i->max;
	i->openmin = i->openmax;
	if (i->openmin)
		i->min--;
	return 1;
}

void snd_interval_mul(const struct snd_interval *a, const struct snd_interval *b, struct snd_interval *c)
{
	if (a->empty || b->empty) {
		snd_interval_none(c);
		return;
	}
	c->empty = 0;
	c->min = mul(a->min, b->min);
	c->openmin = (a->openmin || b->openmin);
	c->max = mul(a->max,  b->max);
	c->openmax = (a->openmax || b->openmax);
	c->integer = (a->integer && b->integer);
}

/**
 * snd_interval_div - refine the interval value with division
 * @a: dividend
 * @b: divisor
 * @c: quotient
 *
 * c = a / b
 *
 * Returns non-zero if the value is changed, zero if not changed.
 */
void snd_interval_div(const struct snd_interval *a, const struct snd_interval *b, struct snd_interval *c)
{
	unsigned int r;
	if (a->empty || b->empty) {
		snd_interval_none(c);
		return;
	}
	c->empty = 0;
	c->min = div32(a->min, b->max, &r);
	c->openmin = (r || a->openmin || b->openmax);
	if (b->min > 0) {
		c->max = div32(a->max, b->min, &r);
		if (r) {
			c->max++;
			c->openmax = 1;
		} else
			c->openmax = (a->openmax || b->openmin);
	} else {
		c->max = UINT_MAX;
		c->openmax = 0;
	}
	c->integer = 0;
}

/**
 * snd_interval_muldivk - refine the interval value
 * @a: dividend 1
 * @b: dividend 2
 * @k: divisor (as integer)
 * @c: result
  *
 * c = a * b / k
 *
 * Returns non-zero if the value is changed, zero if not changed.
 */
void snd_interval_muldivk(const struct snd_interval *a, const struct snd_interval *b,
		      unsigned int k, struct snd_interval *c)
{
	unsigned int r;
	if (a->empty || b->empty) {
		snd_interval_none(c);
		return;
	}
	c->empty = 0;
	c->min = muldiv32(a->min, b->min, k, &r);
	c->openmin = (r || a->openmin || b->openmin);
	c->max = muldiv32(a->max, b->max, k, &r);
	if (r) {
		c->max++;
		c->openmax = 1;
	} else
		c->openmax = (a->openmax || b->openmax);
	c->integer = 0;
}

/**
 * snd_interval_mulkdiv - refine the interval value
 * @a: dividend 1
 * @k: dividend 2 (as integer)
 * @b: divisor
 * @c: result
 *
 * c = a * k / b
 *
 * Returns non-zero if the value is changed, zero if not changed.
 */
void snd_interval_mulkdiv(const struct snd_interval *a, unsigned int k,
		      const struct snd_interval *b, struct snd_interval *c)
{
	unsigned int r;
	if (a->empty || b->empty) {
		snd_interval_none(c);
		return;
	}
	c->empty = 0;
	c->min = muldiv32(a->min, k, b->max, &r);
	c->openmin = (r || a->openmin || b->openmax);
	if (b->min > 0) {
		c->max = muldiv32(a->max, k, b->min, &r);
		if (r) {
			c->max++;
			c->openmax = 1;
		} else
			c->openmax = (a->openmax || b->openmin);
	} else {
		c->max = UINT_MAX;
		c->openmax = 0;
	}
	c->integer = 0;
}

/* ---- */


/**
 * snd_interval_ratnum - refine the interval value
 * @i: interval to refine
 * @rats_count: number of ratnum_t 
 * @rats: ratnum_t array
 * @nump: pointer to store the resultant numerator
 * @denp: pointer to store the resultant denominator
 *
 * Returns non-zero if the value is changed, zero if not changed.
 */
int snd_interval_ratnum(struct snd_interval *i,
			unsigned int rats_count, struct snd_ratnum *rats,
			unsigned int *nump, unsigned int *denp)
{
	unsigned int best_num, best_diff, best_den;
	unsigned int k;
	struct snd_interval t;
	int err;

	best_num = best_den = best_diff = 0;
	for (k = 0; k < rats_count; ++k) {
		unsigned int num = rats[k].num;
		unsigned int den;
		unsigned int q = i->min;
		int diff;
		if (q == 0)
			q = 1;
		den = div_down(num, q);
		if (den < rats[k].den_min)
			continue;
		if (den > rats[k].den_max)
			den = rats[k].den_max;
		else {
			unsigned int r;
			r = (den - rats[k].den_min) % rats[k].den_step;
			if (r != 0)
				den -= r;
		}
		diff = num - q * den;
		if (best_num == 0 ||
		    diff * best_den < best_diff * den) {
			best_diff = diff;
			best_den = den;
			best_num = num;
		}
	}
	if (best_den == 0) {
		i->empty = 1;
		return -EINVAL;
	}
	t.min = div_down(best_num, best_den);
	t.openmin = !!(best_num % best_den);
	
	best_num = best_den = best_diff = 0;
	for (k = 0; k < rats_count; ++k) {
		unsigned int num = rats[k].num;
		unsigned int den;
		unsigned int q = i->max;
		int diff;
		if (q == 0) {
			i->empty = 1;
			return -EINVAL;
		}
		den = div_up(num, q);
		if (den > rats[k].den_max)
			continue;
		if (den < rats[k].den_min)
			den = rats[k].den_min;
		else {
			unsigned int r;
			r = (den - rats[k].den_min) % rats[k].den_step;
			if (r != 0)
				den += rats[k].den_step - r;
		}
		diff = q * den - num;
		if (best_num == 0 ||
		    diff * best_den < best_diff * den) {
			best_diff = diff;
			best_den = den;
			best_num = num;
		}
	}
	if (best_den == 0) {
		i->empty = 1;
		return -EINVAL;
	}
	t.max = div_up(best_num, best_den);
	t.openmax = !!(best_num % best_den);
	t.integer = 0;
	err = snd_interval_refine(i, &t);
	if (err < 0)
		return err;

	if (snd_interval_single(i)) {
		if (nump)
			*nump = best_num;
		if (denp)
			*denp = best_den;
	}
	return err;
}

EXPORT_SYMBOL(snd_interval_ratnum);

/**
 * snd_interval_ratden - refine the interval value
 * @i: interval to refine
 * @rats_count: number of struct ratden
 * @rats: struct ratden array
 * @nump: pointer to store the resultant numerator
 * @denp: pointer to store the resultant denominator
 *
 * Returns non-zero if the value is changed, zero if not changed.
 */
static int snd_interval_ratden(struct snd_interval *i,
			       unsigned int rats_count, struct snd_ratden *rats,
			       unsigned int *nump, unsigned int *denp)
{
	unsigned int best_num, best_diff, best_den;
	unsigned int k;
	struct snd_interval t;
	int err;

	best_num = best_den = best_diff = 0;
	for (k = 0; k < rats_count; ++k) {
		unsigned int num;
		unsigned int den = rats[k].den;
		unsigned int q = i->min;
		int diff;
		num = mul(q, den);
		if (num > rats[k].num_max)
			continue;
		if (num < rats[k].num_min)
			num = rats[k].num_max;
		else {
			unsigned int r;
			r = (num - rats[k].num_min) % rats[k].num_step;
			if (r != 0)
				num += rats[k].num_step - r;
		}
		diff = num - q * den;
		if (best_num == 0 ||
		    diff * best_den < best_diff * den) {
			best_diff = diff;
			best_den = den;
			best_num = num;
		}
	}
	if (best_den == 0) {
		i->empty = 1;
		return -EINVAL;
	}
	t.min = div_down(best_num, best_den);
	t.openmin = !!(best_num % best_den);
	
	best_num = best_den = best_diff = 0;
	for (k = 0; k < rats_count; ++k) {
		unsigned int num;
		unsigned int den = rats[k].den;
		unsigned int q = i->max;
		int diff;
		num = mul(q, den);
		if (num < rats[k].num_min)
			continue;
		if (num > rats[k].num_max)
			num = rats[k].num_max;
		else {
			unsigned int r;
			r = (num - rats[k].num_min) % rats[k].num_step;
			if (r != 0)
				num -= r;
		}
		diff = q * den - num;
		if (best_num == 0 ||
		    diff * best_den < best_diff * den) {
			best_diff = diff;
			best_den = den;
			best_num = num;
		}
	}
	if (best_den == 0) {
		i->empty = 1;
		return -EINVAL;
	}
	t.max = div_up(best_num, best_den);
	t.openmax = !!(best_num % best_den);
	t.integer = 0;
	err = snd_interval_refine(i, &t);
	if (err < 0)
		return err;

	if (snd_interval_single(i)) {
		if (nump)
			*nump = best_num;
		if (denp)
			*denp = best_den;
	}
	return err;
}

/**
 * snd_interval_list - refine the interval value from the list
 * @i: the interval value to refine
 * @count: the number of elements in the list
 * @list: the value list
 * @mask: the bit-mask to evaluate
 *
 * Refines the interval value from the list.
 * When mask is non-zero, only the elements corresponding to bit 1 are
 * evaluated.
 *
 * Returns non-zero if the value is changed, zero if not changed.
 */
int snd_interval_list(struct snd_interval *i, unsigned int count, unsigned int *list, unsigned int mask)
{
        unsigned int k;
	int changed = 0;

	if (!count) {
		i->empty = 1;
		return -EINVAL;
	}
        for (k = 0; k < count; k++) {
		if (mask && !(mask & (1 << k)))
			continue;
                if (i->min == list[k] && !i->openmin)
                        goto _l1;
                if (i->min < list[k]) {
                        i->min = list[k];
			i->openmin = 0;
			changed = 1;
                        goto _l1;
                }
        }
        i->empty = 1;
        return -EINVAL;
 _l1:
        for (k = count; k-- > 0;) {
		if (mask && !(mask & (1 << k)))
			continue;
                if (i->max == list[k] && !i->openmax)
                        goto _l2;
                if (i->max > list[k]) {
                        i->max = list[k];
			i->openmax = 0;
			changed = 1;
                        goto _l2;
                }
        }
        i->empty = 1;
        return -EINVAL;
 _l2:
	if (snd_interval_checkempty(i)) {
		i->empty = 1;
		return -EINVAL;
	}
        return changed;
}

EXPORT_SYMBOL(snd_interval_list);

static int snd_interval_step(struct snd_interval *i, unsigned int min, unsigned int step)
{
	unsigned int n;
	int changed = 0;
	n = (i->min - min) % step;
	if (n != 0 || i->openmin) {
		i->min += step - n;
		changed = 1;
	}
	n = (i->max - min) % step;
	if (n != 0 || i->openmax) {
		i->max -= n;
		changed = 1;
	}
	if (snd_interval_checkempty(i)) {
		i->empty = 1;
		return -EINVAL;
	}
	return changed;
}

/* Info constraints helpers */

/**
 * snd_pcm_hw_rule_add - add the hw-constraint rule
 * @runtime: the pcm runtime instance
 * @cond: condition bits
 * @var: the variable to evaluate
 * @func: the evaluation function
 * @private: the private data pointer passed to function
 * @dep: the dependent variables
 *
 * Returns zero if successful, or a negative error code on failure.
 */
int snd_pcm_hw_rule_add(struct snd_pcm_runtime *runtime, unsigned int cond,
			int var,
			snd_pcm_hw_rule_func_t func, void *private,
			int dep, ...)
{
	struct snd_pcm_hw_constraints *constrs = &runtime->hw_constraints;
	struct snd_pcm_hw_rule *c;
	unsigned int k;
	va_list args;
	va_start(args, dep);
	if (constrs->rules_num >= constrs->rules_all) {
		struct snd_pcm_hw_rule *new;
		unsigned int new_rules = constrs->rules_all + 16;
		new = kcalloc(new_rules, sizeof(*c), GFP_KERNEL);
		if (!new)
			return -ENOMEM;
		if (constrs->rules) {
			memcpy(new, constrs->rules,
			       constrs->rules_num * sizeof(*c));
			kfree(constrs->rules);
		}
		constrs->rules = new;
		constrs->rules_all = new_rules;
	}
	c = &constrs->rules[constrs->rules_num];
	c->cond = cond;
	c->func = func;
	c->var = var;
	c->private = private;
	k = 0;
	while (1) {
		if (snd_BUG_ON(k >= ARRAY_SIZE(c->deps)))
			return -EINVAL;
		c->deps[k++] = dep;
		if (dep < 0)
			break;
		dep = va_arg(args, int);
	}
	constrs->rules_num++;
	va_end(args);
	return 0;
}				    

EXPORT_SYMBOL(snd_pcm_hw_rule_add);

/**
 * snd_pcm_hw_constraint_mask - apply the given bitmap mask constraint
 * @runtime: PCM runtime instance
 * @var: hw_params variable to apply the mask
 * @mask: the bitmap mask
 *
 * Apply the constraint of the given bitmap mask to a 32-bit mask parameter.
 */
int snd_pcm_hw_constraint_mask(struct snd_pcm_runtime *runtime, snd_pcm_hw_param_t var,
			       u_int32_t mask)
{
	struct snd_pcm_hw_constraints *constrs = &runtime->hw_constraints;
	struct snd_mask *maskp = constrs_mask(constrs, var);
	*maskp->bits &= mask;
	memset(maskp->bits + 1, 0, (SNDRV_MASK_MAX-32) / 8); /* clear rest */
	if (*maskp->bits == 0)
		return -EINVAL;
	return 0;
}

/**
 * snd_pcm_hw_constraint_mask64 - apply the given bitmap mask constraint
 * @runtime: PCM runtime instance
 * @var: hw_params variable to apply the mask
 * @mask: the 64bit bitmap mask
 *
 * Apply the constraint of the given bitmap mask to a 64-bit mask parameter.
 */
int snd_pcm_hw_constraint_mask64(struct snd_pcm_runtime *runtime, snd_pcm_hw_param_t var,
				 u_int64_t mask)
{
	struct snd_pcm_hw_constraints *constrs = &runtime->hw_constraints;
	struct snd_mask *maskp = constrs_mask(constrs, var);
	maskp->bits[0] &= (u_int32_t)mask;
	maskp->bits[1] &= (u_int32_t)(mask >> 32);
	memset(maskp->bits + 2, 0, (SNDRV_MASK_MAX-64) / 8); /* clear rest */
	if (! maskp->bits[0] && ! maskp->bits[1])
		return -EINVAL;
	return 0;
}

/**
 * snd_pcm_hw_constraint_integer - apply an integer constraint to an interval
 * @runtime: PCM runtime instance
 * @var: hw_params variable to apply the integer constraint
 *
 * Apply the constraint of integer to an interval parameter.
 */
int snd_pcm_hw_constraint_integer(struct snd_pcm_runtime *runtime, snd_pcm_hw_param_t var)
{
	struct snd_pcm_hw_constraints *constrs = &runtime->hw_constraints;
	return snd_interval_setinteger(constrs_interval(constrs, var));
}

EXPORT_SYMBOL(snd_pcm_hw_constraint_integer);

/**
 * snd_pcm_hw_constraint_minmax - apply a min/max range constraint to an interval
 * @runtime: PCM runtime instance
 * @var: hw_params variable to apply the range
 * @min: the minimal value
 * @max: the maximal value
 * 
 * Apply the min/max range constraint to an interval parameter.
 */
int snd_pcm_hw_constraint_minmax(struct snd_pcm_runtime *runtime, snd_pcm_hw_param_t var,
				 unsigned int min, unsigned int max)
{
	struct snd_pcm_hw_constraints *constrs = &runtime->hw_constraints;
	struct snd_interval t;
	t.min = min;
	t.max = max;
	t.openmin = t.openmax = 0;
	t.integer = 0;
	return snd_interval_refine(constrs_interval(constrs, var), &t);
}

EXPORT_SYMBOL(snd_pcm_hw_constraint_minmax);

static int snd_pcm_hw_rule_list(struct snd_pcm_hw_params *params,
				struct snd_pcm_hw_rule *rule)
{
	struct snd_pcm_hw_constraint_list *list = rule->private;
	return snd_interval_list(hw_param_interval(params, rule->var), list->count, list->list, list->mask);
}		


/**
 * snd_pcm_hw_constraint_list - apply a list of constraints to a parameter
 * @runtime: PCM runtime instance
 * @cond: condition bits
 * @var: hw_params variable to apply the list constraint
 * @l: list
 * 
 * Apply the list of constraints to an interval parameter.
 */
int snd_pcm_hw_constraint_list(struct snd_pcm_runtime *runtime,
			       unsigned int cond,
			       snd_pcm_hw_param_t var,
			       struct snd_pcm_hw_constraint_list *l)
{
	return snd_pcm_hw_rule_add(runtime, cond, var,
				   snd_pcm_hw_rule_list, l,
				   var, -1);
}

EXPORT_SYMBOL(snd_pcm_hw_constraint_list);

static int snd_pcm_hw_rule_ratnums(struct snd_pcm_hw_params *params,
				   struct snd_pcm_hw_rule *rule)
{
	struct snd_pcm_hw_constraint_ratnums *r = rule->private;
	unsigned int num = 0, den = 0;
	int err;
	err = snd_interval_ratnum(hw_param_interval(params, rule->var),
				  r->nrats, r->rats, &num, &den);
	if (err >= 0 && den && rule->var == SNDRV_PCM_HW_PARAM_RATE) {
		params->rate_num = num;
		params->rate_den = den;
	}
	return err;
}

/**
 * snd_pcm_hw_constraint_ratnums - apply ratnums constraint to a parameter
 * @runtime: PCM runtime instance
 * @cond: condition bits
 * @var: hw_params variable to apply the ratnums constraint
 * @r: struct snd_ratnums constriants
 */
int snd_pcm_hw_constraint_ratnums(struct snd_pcm_runtime *runtime, 
				  unsigned int cond,
				  snd_pcm_hw_param_t var,
				  struct snd_pcm_hw_constraint_ratnums *r)
{
	return snd_pcm_hw_rule_add(runtime, cond, var,
				   snd_pcm_hw_rule_ratnums, r,
				   var, -1);
}

EXPORT_SYMBOL(snd_pcm_hw_constraint_ratnums);

static int snd_pcm_hw_rule_ratdens(struct snd_pcm_hw_params *params,
				   struct snd_pcm_hw_rule *rule)
{
	struct snd_pcm_hw_constraint_ratdens *r = rule->private;
	unsigned int num = 0, den = 0;
	int err = snd_interval_ratden(hw_param_interval(params, rule->var),
				  r->nrats, r->rats, &num, &den);
	if (err >= 0 && den && rule->var == SNDRV_PCM_HW_PARAM_RATE) {
		params->rate_num = num;
		params->rate_den = den;
	}
	return err;
}

/**
 * snd_pcm_hw_constraint_ratdens - apply ratdens constraint to a parameter
 * @runtime: PCM runtime instance
 * @cond: condition bits
 * @var: hw_params variable to apply the ratdens constraint
 * @r: struct snd_ratdens constriants
 */
int snd_pcm_hw_constraint_ratdens(struct snd_pcm_runtime *runtime, 
				  unsigned int cond,
				  snd_pcm_hw_param_t var,
				  struct snd_pcm_hw_constraint_ratdens *r)
{
	return snd_pcm_hw_rule_add(runtime, cond, var,
				   snd_pcm_hw_rule_ratdens, r,
				   var, -1);
}

EXPORT_SYMBOL(snd_pcm_hw_constraint_ratdens);

static int snd_pcm_hw_rule_msbits(struct snd_pcm_hw_params *params,
				  struct snd_pcm_hw_rule *rule)
{
	unsigned int l = (unsigned long) rule->private;
	int width = l & 0xffff;
	unsigned int msbits = l >> 16;
	struct snd_interval *i = hw_param_interval(params, SNDRV_PCM_HW_PARAM_SAMPLE_BITS);
	if (snd_interval_single(i) && snd_interval_value(i) == width)
		params->msbits = msbits;
	return 0;
}

/**
 * snd_pcm_hw_constraint_msbits - add a hw constraint msbits rule
 * @runtime: PCM runtime instance
 * @cond: condition bits
 * @width: sample bits width
 * @msbits: msbits width
 */
int snd_pcm_hw_constraint_msbits(struct snd_pcm_runtime *runtime, 
				 unsigned int cond,
				 unsigned int width,
				 unsigned int msbits)
{
	unsigned long l = (msbits << 16) | width;
	return snd_pcm_hw_rule_add(runtime, cond, -1,
				    snd_pcm_hw_rule_msbits,
				    (void*) l,
				    SNDRV_PCM_HW_PARAM_SAMPLE_BITS, -1);
}

EXPORT_SYMBOL(snd_pcm_hw_constraint_msbits);

static int snd_pcm_hw_rule_step(struct snd_pcm_hw_params *params,
				struct snd_pcm_hw_rule *rule)
{
	unsigned long step = (unsigned long) rule->private;
	return snd_interval_step(hw_param_interval(params, rule->var), 0, step);
}

/**
 * snd_pcm_hw_constraint_step - add a hw constraint step rule
 * @runtime: PCM runtime instance
 * @cond: condition bits
 * @var: hw_params variable to apply the step constraint
 * @step: step size
 */
int snd_pcm_hw_constraint_step(struct snd_pcm_runtime *runtime,
			       unsigned int cond,
			       snd_pcm_hw_param_t var,
			       unsigned long step)
{
	return snd_pcm_hw_rule_add(runtime, cond, var, 
				   snd_pcm_hw_rule_step, (void *) step,
				   var, -1);
}

EXPORT_SYMBOL(snd_pcm_hw_constraint_step);

static int snd_pcm_hw_rule_pow2(struct snd_pcm_hw_params *params, struct snd_pcm_hw_rule *rule)
{
	static unsigned int pow2_sizes[] = {
		1<<0, 1<<1, 1<<2, 1<<3, 1<<4, 1<<5, 1<<6, 1<<7,
		1<<8, 1<<9, 1<<10, 1<<11, 1<<12, 1<<13, 1<<14, 1<<15,
		1<<16, 1<<17, 1<<18, 1<<19, 1<<20, 1<<21, 1<<22, 1<<23,
		1<<24, 1<<25, 1<<26, 1<<27, 1<<28, 1<<29, 1<<30
	};
	return snd_interval_list(hw_param_interval(params, rule->var),
				 ARRAY_SIZE(pow2_sizes), pow2_sizes, 0);
}		

/**
 * snd_pcm_hw_constraint_pow2 - add a hw constraint power-of-2 rule
 * @runtime: PCM runtime instance
 * @cond: condition bits
 * @var: hw_params variable to apply the power-of-2 constraint
 */
int snd_pcm_hw_constraint_pow2(struct snd_pcm_runtime *runtime,
			       unsigned int cond,
			       snd_pcm_hw_param_t var)
{
	return snd_pcm_hw_rule_add(runtime, cond, var, 
				   snd_pcm_hw_rule_pow2, NULL,
				   var, -1);
}

EXPORT_SYMBOL(snd_pcm_hw_constraint_pow2);

static void _snd_pcm_hw_param_any(struct snd_pcm_hw_params *params,
				  snd_pcm_hw_param_t var)
{
	if (hw_is_mask(var)) {
		snd_mask_any(hw_param_mask(params, var));
		params->cmask |= 1 << var;
		params->rmask |= 1 << var;
		return;
	}
	if (hw_is_interval(var)) {
		snd_interval_any(hw_param_interval(params, var));
		params->cmask |= 1 << var;
		params->rmask |= 1 << var;
		return;
	}
	snd_BUG();
}

void _snd_pcm_hw_params_any(struct snd_pcm_hw_params *params)
{
	unsigned int k;
	memset(params, 0, sizeof(*params));
	for (k = SNDRV_PCM_HW_PARAM_FIRST_MASK; k <= SNDRV_PCM_HW_PARAM_LAST_MASK; k++)
		_snd_pcm_hw_param_any(params, k);
	for (k = SNDRV_PCM_HW_PARAM_FIRST_INTERVAL; k <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL; k++)
		_snd_pcm_hw_param_any(params, k);
	params->info = ~0U;
}

EXPORT_SYMBOL(_snd_pcm_hw_params_any);

/**
 * snd_pcm_hw_param_value - return @params field @var value
 * @params: the hw_params instance
 * @var: parameter to retrieve
 * @dir: pointer to the direction (-1,0,1) or %NULL
 *
 * Return the value for field @var if it's fixed in configuration space
 * defined by @params. Return -%EINVAL otherwise.
 */
int snd_pcm_hw_param_value(const struct snd_pcm_hw_params *params,
			   snd_pcm_hw_param_t var, int *dir)
{
	if (hw_is_mask(var)) {
		const struct snd_mask *mask = hw_param_mask_c(params, var);
		if (!snd_mask_single(mask))
			return -EINVAL;
		if (dir)
			*dir = 0;
		return snd_mask_value(mask);
	}
	if (hw_is_interval(var)) {
		const struct snd_interval *i = hw_param_interval_c(params, var);
		if (!snd_interval_single(i))
			return -EINVAL;
		if (dir)
			*dir = i->openmin;
		return snd_interval_value(i);
	}
	return -EINVAL;
}

EXPORT_SYMBOL(snd_pcm_hw_param_value);

void _snd_pcm_hw_param_setempty(struct snd_pcm_hw_params *params,
				snd_pcm_hw_param_t var)
{
	if (hw_is_mask(var)) {
		snd_mask_none(hw_param_mask(params, var));
		params->cmask |= 1 << var;
		params->rmask |= 1 << var;
	} else if (hw_is_interval(var)) {
		snd_interval_none(hw_param_interval(params, var));
		params->cmask |= 1 << var;
		params->rmask |= 1 << var;
	} else {
		snd_BUG();
	}
}

EXPORT_SYMBOL(_snd_pcm_hw_param_setempty);

static int _snd_pcm_hw_param_first(struct snd_pcm_hw_params *params,
				   snd_pcm_hw_param_t var)
{
	int changed;
	if (hw_is_mask(var))
		changed = snd_mask_refine_first(hw_param_mask(params, var));
	else if (hw_is_interval(var))
		changed = snd_interval_refine_first(hw_param_interval(params, var));
	else
		return -EINVAL;
	if (changed) {
		params->cmask |= 1 << var;
		params->rmask |= 1 << var;
	}
	return changed;
}


/**
 * snd_pcm_hw_param_first - refine config space and return minimum value
 * @pcm: PCM instance
 * @params: the hw_params instance
 * @var: parameter to retrieve
 * @dir: pointer to the direction (-1,0,1) or %NULL
 *
 * Inside configuration space defined by @params remove from @var all
 * values > minimum. Reduce configuration space accordingly.
 * Return the minimum.
 */
int snd_pcm_hw_param_first(struct snd_pcm_substream *pcm, 
			   struct snd_pcm_hw_params *params, 
			   snd_pcm_hw_param_t var, int *dir)
{
	int changed = _snd_pcm_hw_param_first(params, var);
	if (changed < 0)
		return changed;
	if (params->rmask) {
		int err = snd_pcm_hw_refine(pcm, params);
		if (snd_BUG_ON(err < 0))
			return err;
	}
	return snd_pcm_hw_param_value(params, var, dir);
}

EXPORT_SYMBOL(snd_pcm_hw_param_first);

static int _snd_pcm_hw_param_last(struct snd_pcm_hw_params *params,
				  snd_pcm_hw_param_t var)
{
	int changed;
	if (hw_is_mask(var))
		changed = snd_mask_refine_last(hw_param_mask(params, var));
	else if (hw_is_interval(var))
		changed = snd_interval_refine_last(hw_param_interval(params, var));
	else
		return -EINVAL;
	if (changed) {
		params->cmask |= 1 << var;
		params->rmask |= 1 << var;
	}
	return changed;
}


/**
 * snd_pcm_hw_param_last - refine config space and return maximum value
 * @pcm: PCM instance
 * @params: the hw_params instance
 * @var: parameter to retrieve
 * @dir: pointer to the direction (-1,0,1) or %NULL
 *
 * Inside configuration space defined by @params remove from @var all
 * values < maximum. Reduce configuration space accordingly.
 * Return the maximum.
 */
int snd_pcm_hw_param_last(struct snd_pcm_substream *pcm, 
			  struct snd_pcm_hw_params *params,
			  snd_pcm_hw_param_t var, int *dir)
{
	int changed = _snd_pcm_hw_param_last(params, var);
	if (changed < 0)
		return changed;
	if (params->rmask) {
		int err = snd_pcm_hw_refine(pcm, params);
		if (snd_BUG_ON(err < 0))
			return err;
	}
	return snd_pcm_hw_param_value(params, var, dir);
}

EXPORT_SYMBOL(snd_pcm_hw_param_last);

/**
 * snd_pcm_hw_param_choose - choose a configuration defined by @params
 * @pcm: PCM instance
 * @params: the hw_params instance
 *
 * Choose one configuration from configuration space defined by @params.
 * The configuration chosen is that obtained fixing in this order:
 * first access, first format, first subformat, min channels,
 * min rate, min period time, max buffer size, min tick time
 */
int snd_pcm_hw_params_choose(struct snd_pcm_substream *pcm,
			     struct snd_pcm_hw_params *params)
{
	static int vars[] = {
		SNDRV_PCM_HW_PARAM_ACCESS,
		SNDRV_PCM_HW_PARAM_FORMAT,
		SNDRV_PCM_HW_PARAM_SUBFORMAT,
		SNDRV_PCM_HW_PARAM_CHANNELS,
		SNDRV_PCM_HW_PARAM_RATE,
		SNDRV_PCM_HW_PARAM_PERIOD_TIME,
		SNDRV_PCM_HW_PARAM_BUFFER_SIZE,
		SNDRV_PCM_HW_PARAM_TICK_TIME,
		-1
	};
	int err, *v;

	for (v = vars; *v != -1; v++) {
		if (*v != SNDRV_PCM_HW_PARAM_BUFFER_SIZE)
			err = snd_pcm_hw_param_first(pcm, params, *v, NULL);
		else
			err = snd_pcm_hw_param_last(pcm, params, *v, NULL);
		if (snd_BUG_ON(err < 0))
			return err;
	}
	return 0;
}

static int snd_pcm_lib_ioctl_reset(struct snd_pcm_substream *substream,
				   void *arg)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned long flags;
	snd_pcm_stream_lock_irqsave(substream, flags);
	if (snd_pcm_running(substream) &&
	    snd_pcm_update_hw_ptr(substream) >= 0)
		runtime->status->hw_ptr %= runtime->buffer_size;
	else
		runtime->status->hw_ptr = 0;
	runtime->hw_ptr_jiffies = jiffies;
	snd_pcm_stream_unlock_irqrestore(substream, flags);
	return 0;
}

static int snd_pcm_lib_ioctl_channel_info(struct snd_pcm_substream *substream,
					  void *arg)
{
	struct snd_pcm_channel_info *info = arg;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int width;
	if (!(runtime->info & SNDRV_PCM_INFO_MMAP)) {
		info->offset = -1;
		return 0;
	}
	width = snd_pcm_format_physical_width(runtime->format);
	if (width < 0)
		return width;
	info->offset = 0;
	switch (runtime->access) {
	case SNDRV_PCM_ACCESS_MMAP_INTERLEAVED:
	case SNDRV_PCM_ACCESS_RW_INTERLEAVED:
		info->first = info->channel * width;
		info->step = runtime->channels * width;
		break;
	case SNDRV_PCM_ACCESS_MMAP_NONINTERLEAVED:
	case SNDRV_PCM_ACCESS_RW_NONINTERLEAVED:
	{
		size_t size = runtime->dma_bytes / runtime->channels;
		info->first = info->channel * size * 8;
		info->step = width;
		break;
	}
	default:
		snd_BUG();
		break;
	}
	return 0;
}

/**
 * snd_pcm_lib_ioctl - a generic PCM ioctl callback
 * @substream: the pcm substream instance
 * @cmd: ioctl command
 * @arg: ioctl argument
 *
 * Processes the generic ioctl commands for PCM.
 * Can be passed as the ioctl callback for PCM ops.
 *
 * Returns zero if successful, or a negative error code on failure.
 */
int snd_pcm_lib_ioctl(struct snd_pcm_substream *substream,
		      unsigned int cmd, void *arg)
{
	switch (cmd) {
	case SNDRV_PCM_IOCTL1_INFO:
		return 0;
	case SNDRV_PCM_IOCTL1_RESET:
		return snd_pcm_lib_ioctl_reset(substream, arg);
	case SNDRV_PCM_IOCTL1_CHANNEL_INFO:
		return snd_pcm_lib_ioctl_channel_info(substream, arg);
	}
	return -ENXIO;
}

EXPORT_SYMBOL(snd_pcm_lib_ioctl);

/**
 * snd_pcm_period_elapsed - update the pcm status for the next period
 * @substream: the pcm substream instance
 *
 * This function is called from the interrupt handler when the
 * PCM has processed the period size.  It will update the current
 * pointer, wake up sleepers, etc.
 *
 * Even if more than one periods have elapsed since the last call, you
 * have to call this only once.
 */
void snd_pcm_period_elapsed(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime;
	unsigned long flags;

	if (PCM_RUNTIME_CHECK(substream))
		return;
	runtime = substream->runtime;

	if (runtime->transfer_ack_begin)
		runtime->transfer_ack_begin(substream);

	snd_pcm_stream_lock_irqsave(substream, flags);
	if (!snd_pcm_running(substream) ||
	    snd_pcm_update_hw_ptr_interrupt(substream) < 0)
		goto _end;

	if (substream->timer_running)
		snd_timer_interrupt(substream->timer, 1);
 _end:
	snd_pcm_stream_unlock_irqrestore(substream, flags);
	if (runtime->transfer_ack_end)
		runtime->transfer_ack_end(substream);
	kill_fasync(&runtime->fasync, SIGIO, POLL_IN);
}

EXPORT_SYMBOL(snd_pcm_period_elapsed);

/*
 * Wait until avail_min data becomes available
 * Returns a negative error code if any error occurs during operation.
 * The available space is stored on availp.  When err = 0 and avail = 0
 * on the capture stream, it indicates the stream is in DRAINING state.
 */
static int wait_for_avail_min(struct snd_pcm_substream *substream,
			      snd_pcm_uframes_t *availp)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	int is_playback = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	wait_queue_t wait;
	int err = 0;
	snd_pcm_uframes_t avail = 0;
	long tout;

	init_waitqueue_entry(&wait, current);
	add_wait_queue(&runtime->sleep, &wait);
	for (;;) {
		if (signal_pending(current)) {
			err = -ERESTARTSYS;
			break;
		}
		set_current_state(TASK_INTERRUPTIBLE);
		snd_pcm_stream_unlock_irq(substream);
		tout = schedule_timeout(msecs_to_jiffies(10000));
		snd_pcm_stream_lock_irq(substream);
		switch (runtime->status->state) {
		case SNDRV_PCM_STATE_SUSPENDED:
			err = -ESTRPIPE;
			goto _endloop;
		case SNDRV_PCM_STATE_XRUN:
			err = -EPIPE;
			goto _endloop;
		case SNDRV_PCM_STATE_DRAINING:
			if (is_playback)
				err = -EPIPE;
			else 
				avail = 0; /* indicate draining */
			goto _endloop;
		case SNDRV_PCM_STATE_OPEN:
		case SNDRV_PCM_STATE_SETUP:
		case SNDRV_PCM_STATE_DISCONNECTED:
			err = -EBADFD;
			goto _endloop;
		}
		if (!tout) {
			snd_printd("%s write error (DMA or IRQ trouble?)\n",
				   is_playback ? "playback" : "capture");
			err = -EIO;
			break;
		}
		if (is_playback)
			avail = snd_pcm_playback_avail(runtime);
		else
			avail = snd_pcm_capture_avail(runtime);
		if (avail >= runtime->control->avail_min)
			break;
	}
 _endloop:
	remove_wait_queue(&runtime->sleep, &wait);
	*availp = avail;
	return err;
}
	
static int snd_pcm_lib_write_transfer(struct snd_pcm_substream *substream,
				      unsigned int hwoff,
				      unsigned long data, unsigned int off,
				      snd_pcm_uframes_t frames)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;
	char __user *buf = (char __user *) data + frames_to_bytes(runtime, off);
	if (substream->ops->copy) {
		if ((err = substream->ops->copy(substream, -1, hwoff, buf, frames)) < 0)
			return err;
	} else {
		char *hwbuf = runtime->dma_area + frames_to_bytes(runtime, hwoff);
		if (copy_from_user(hwbuf, buf, frames_to_bytes(runtime, frames)))
			return -EFAULT;
	}
	return 0;
}
 
typedef int (*transfer_f)(struct snd_pcm_substream *substream, unsigned int hwoff,
			  unsigned long data, unsigned int off,
			  snd_pcm_uframes_t size);

static snd_pcm_sframes_t snd_pcm_lib_write1(struct snd_pcm_substream *substream, 
					    unsigned long data,
					    snd_pcm_uframes_t size,
					    int nonblock,
					    transfer_f transfer)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	snd_pcm_uframes_t xfer = 0;
	snd_pcm_uframes_t offset = 0;
	int err = 0;

	if (size == 0)
		return 0;

	snd_pcm_stream_lock_irq(substream);
	switch (runtime->status->state) {
	case SNDRV_PCM_STATE_PREPARED:
	case SNDRV_PCM_STATE_RUNNING:
	case SNDRV_PCM_STATE_PAUSED:
		break;
	case SNDRV_PCM_STATE_XRUN:
		err = -EPIPE;
		goto _end_unlock;
	case SNDRV_PCM_STATE_SUSPENDED:
		err = -ESTRPIPE;
		goto _end_unlock;
	default:
		err = -EBADFD;
		goto _end_unlock;
	}

	while (size > 0) {
		snd_pcm_uframes_t frames, appl_ptr, appl_ofs;
		snd_pcm_uframes_t avail;
		snd_pcm_uframes_t cont;
		if (runtime->status->state == SNDRV_PCM_STATE_RUNNING)
			snd_pcm_update_hw_ptr(substream);
		avail = snd_pcm_playback_avail(runtime);
		if (!avail) {
			if (nonblock) {
				err = -EAGAIN;
				goto _end_unlock;
			}
			err = wait_for_avail_min(substream, &avail);
			if (err < 0)
				goto _end_unlock;
		}
		frames = size > avail ? avail : size;
		cont = runtime->buffer_size - runtime->control->appl_ptr % runtime->buffer_size;
		if (frames > cont)
			frames = cont;
		if (snd_BUG_ON(!frames)) {
			snd_pcm_stream_unlock_irq(substream);
			return -EINVAL;
		}
		appl_ptr = runtime->control->appl_ptr;
		appl_ofs = appl_ptr % runtime->buffer_size;
		snd_pcm_stream_unlock_irq(substream);
		if ((err = transfer(substream, appl_ofs, data, offset, frames)) < 0)
			goto _end;
		snd_pcm_stream_lock_irq(substream);
		switch (runtime->status->state) {
		case SNDRV_PCM_STATE_XRUN:
			err = -EPIPE;
			goto _end_unlock;
		case SNDRV_PCM_STATE_SUSPENDED:
			err = -ESTRPIPE;
			goto _end_unlock;
		default:
			break;
		}
		appl_ptr += frames;
		if (appl_ptr >= runtime->boundary)
			appl_ptr -= runtime->boundary;
		runtime->control->appl_ptr = appl_ptr;
		if (substream->ops->ack)
			substream->ops->ack(substream);

		offset += frames;
		size -= frames;
		xfer += frames;
		if (runtime->status->state == SNDRV_PCM_STATE_PREPARED &&
		    snd_pcm_playback_hw_avail(runtime) >= (snd_pcm_sframes_t)runtime->start_threshold) {
			err = snd_pcm_start(substream);
			if (err < 0)
				goto _end_unlock;
		}
	}
 _end_unlock:
	snd_pcm_stream_unlock_irq(substream);
 _end:
	return xfer > 0 ? (snd_pcm_sframes_t)xfer : err;
}

/* sanity-check for read/write methods */
static int pcm_sanity_check(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime;
	if (PCM_RUNTIME_CHECK(substream))
		return -ENXIO;
	runtime = substream->runtime;
	if (snd_BUG_ON(!substream->ops->copy && !runtime->dma_area))
		return -EINVAL;
	if (runtime->status->state == SNDRV_PCM_STATE_OPEN)
		return -EBADFD;
	return 0;
}

snd_pcm_sframes_t snd_pcm_lib_write(struct snd_pcm_substream *substream, const void __user *buf, snd_pcm_uframes_t size)
{
	struct snd_pcm_runtime *runtime;
	int nonblock;
	int err;

	err = pcm_sanity_check(substream);
	if (err < 0)
		return err;
	runtime = substream->runtime;
	nonblock = !!(substream->f_flags & O_NONBLOCK);

	if (runtime->access != SNDRV_PCM_ACCESS_RW_INTERLEAVED &&
	    runtime->channels > 1)
		return -EINVAL;
	return snd_pcm_lib_write1(substream, (unsigned long)buf, size, nonblock,
				  snd_pcm_lib_write_transfer);
}

EXPORT_SYMBOL(snd_pcm_lib_write);

static int snd_pcm_lib_writev_transfer(struct snd_pcm_substream *substream,
				       unsigned int hwoff,
				       unsigned long data, unsigned int off,
				       snd_pcm_uframes_t frames)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;
	void __user **bufs = (void __user **)data;
	int channels = runtime->channels;
	int c;
	if (substream->ops->copy) {
		if (snd_BUG_ON(!substream->ops->silence))
			return -EINVAL;
		for (c = 0; c < channels; ++c, ++bufs) {
			if (*bufs == NULL) {
				if ((err = substream->ops->silence(substream, c, hwoff, frames)) < 0)
					return err;
			} else {
				char __user *buf = *bufs + samples_to_bytes(runtime, off);
				if ((err = substream->ops->copy(substream, c, hwoff, buf, frames)) < 0)
					return err;
			}
		}
	} else {
		/* default transfer behaviour */
		size_t dma_csize = runtime->dma_bytes / channels;
		for (c = 0; c < channels; ++c, ++bufs) {
			char *hwbuf = runtime->dma_area + (c * dma_csize) + samples_to_bytes(runtime, hwoff);
			if (*bufs == NULL) {
				snd_pcm_format_set_silence(runtime->format, hwbuf, frames);
			} else {
				char __user *buf = *bufs + samples_to_bytes(runtime, off);
				if (copy_from_user(hwbuf, buf, samples_to_bytes(runtime, frames)))
					return -EFAULT;
			}
		}
	}
	return 0;
}
 
snd_pcm_sframes_t snd_pcm_lib_writev(struct snd_pcm_substream *substream,
				     void __user **bufs,
				     snd_pcm_uframes_t frames)
{
	struct snd_pcm_runtime *runtime;
	int nonblock;
	int err;

	err = pcm_sanity_check(substream);
	if (err < 0)
		return err;
	runtime = substream->runtime;
	nonblock = !!(substream->f_flags & O_NONBLOCK);

	if (runtime->access != SNDRV_PCM_ACCESS_RW_NONINTERLEAVED)
		return -EINVAL;
	return snd_pcm_lib_write1(substream, (unsigned long)bufs, frames,
				  nonblock, snd_pcm_lib_writev_transfer);
}

EXPORT_SYMBOL(snd_pcm_lib_writev);

static int snd_pcm_lib_read_transfer(struct snd_pcm_substream *substream, 
				     unsigned int hwoff,
				     unsigned long data, unsigned int off,
				     snd_pcm_uframes_t frames)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;
	char __user *buf = (char __user *) data + frames_to_bytes(runtime, off);
	if (substream->ops->copy) {
		if ((err = substream->ops->copy(substream, -1, hwoff, buf, frames)) < 0)
			return err;
	} else {
		char *hwbuf = runtime->dma_area + frames_to_bytes(runtime, hwoff);
		if (copy_to_user(buf, hwbuf, frames_to_bytes(runtime, frames)))
			return -EFAULT;
	}
	return 0;
}

static snd_pcm_sframes_t snd_pcm_lib_read1(struct snd_pcm_substream *substream,
					   unsigned long data,
					   snd_pcm_uframes_t size,
					   int nonblock,
					   transfer_f transfer)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	snd_pcm_uframes_t xfer = 0;
	snd_pcm_uframes_t offset = 0;
	int err = 0;

	if (size == 0)
		return 0;

	snd_pcm_stream_lock_irq(substream);
	switch (runtime->status->state) {
	case SNDRV_PCM_STATE_PREPARED:
		if (size >= runtime->start_threshold) {
			err = snd_pcm_start(substream);
			if (err < 0)
				goto _end_unlock;
		}
		break;
	case SNDRV_PCM_STATE_DRAINING:
	case SNDRV_PCM_STATE_RUNNING:
	case SNDRV_PCM_STATE_PAUSED:
		break;
	case SNDRV_PCM_STATE_XRUN:
		err = -EPIPE;
		goto _end_unlock;
	case SNDRV_PCM_STATE_SUSPENDED:
		err = -ESTRPIPE;
		goto _end_unlock;
	default:
		err = -EBADFD;
		goto _end_unlock;
	}

	while (size > 0) {
		snd_pcm_uframes_t frames, appl_ptr, appl_ofs;
		snd_pcm_uframes_t avail;
		snd_pcm_uframes_t cont;
		if (runtime->status->state == SNDRV_PCM_STATE_RUNNING)
			snd_pcm_update_hw_ptr(substream);
		avail = snd_pcm_capture_avail(runtime);
		if (!avail) {
			if (runtime->status->state ==
			    SNDRV_PCM_STATE_DRAINING) {
				snd_pcm_stop(substream, SNDRV_PCM_STATE_SETUP);
				goto _end_unlock;
			}
			if (nonblock) {
				err = -EAGAIN;
				goto _end_unlock;
			}
			err = wait_for_avail_min(substream, &avail);
			if (err < 0)
				goto _end_unlock;
			if (!avail)
				continue; /* draining */
		}
		frames = size > avail ? avail : size;
		cont = runtime->buffer_size - runtime->control->appl_ptr % runtime->buffer_size;
		if (frames > cont)
			frames = cont;
		if (snd_BUG_ON(!frames)) {
			snd_pcm_stream_unlock_irq(substream);
			return -EINVAL;
		}
		appl_ptr = runtime->control->appl_ptr;
		appl_ofs = appl_ptr % runtime->buffer_size;
		snd_pcm_stream_unlock_irq(substream);
		if ((err = transfer(substream, appl_ofs, data, offset, frames)) < 0)
			goto _end;
		snd_pcm_stream_lock_irq(substream);
		switch (runtime->status->state) {
		case SNDRV_PCM_STATE_XRUN:
			err = -EPIPE;
			goto _end_unlock;
		case SNDRV_PCM_STATE_SUSPENDED:
			err = -ESTRPIPE;
			goto _end_unlock;
		default:
			break;
		}
		appl_ptr += frames;
		if (appl_ptr >= runtime->boundary)
			appl_ptr -= runtime->boundary;
		runtime->control->appl_ptr = appl_ptr;
		if (substream->ops->ack)
			substream->ops->ack(substream);

		offset += frames;
		size -= frames;
		xfer += frames;
	}
 _end_unlock:
	snd_pcm_stream_unlock_irq(substream);
 _end:
	return xfer > 0 ? (snd_pcm_sframes_t)xfer : err;
}

snd_pcm_sframes_t snd_pcm_lib_read(struct snd_pcm_substream *substream, void __user *buf, snd_pcm_uframes_t size)
{
	struct snd_pcm_runtime *runtime;
	int nonblock;
	int err;
	
	err = pcm_sanity_check(substream);
	if (err < 0)
		return err;
	runtime = substream->runtime;
	nonblock = !!(substream->f_flags & O_NONBLOCK);
	if (runtime->access != SNDRV_PCM_ACCESS_RW_INTERLEAVED)
		return -EINVAL;
	return snd_pcm_lib_read1(substream, (unsigned long)buf, size, nonblock, snd_pcm_lib_read_transfer);
}

EXPORT_SYMBOL(snd_pcm_lib_read);

static int snd_pcm_lib_readv_transfer(struct snd_pcm_substream *substream,
				      unsigned int hwoff,
				      unsigned long data, unsigned int off,
				      snd_pcm_uframes_t frames)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;
	void __user **bufs = (void __user **)data;
	int channels = runtime->channels;
	int c;
	if (substream->ops->copy) {
		for (c = 0; c < channels; ++c, ++bufs) {
			char __user *buf;
			if (*bufs == NULL)
				continue;
			buf = *bufs + samples_to_bytes(runtime, off);
			if ((err = substream->ops->copy(substream, c, hwoff, buf, frames)) < 0)
				return err;
		}
	} else {
		snd_pcm_uframes_t dma_csize = runtime->dma_bytes / channels;
		for (c = 0; c < channels; ++c, ++bufs) {
			char *hwbuf;
			char __user *buf;
			if (*bufs == NULL)
				continue;

			hwbuf = runtime->dma_area + (c * dma_csize) + samples_to_bytes(runtime, hwoff);
			buf = *bufs + samples_to_bytes(runtime, off);
			if (copy_to_user(buf, hwbuf, samples_to_bytes(runtime, frames)))
				return -EFAULT;
		}
	}
	return 0;
}
 
snd_pcm_sframes_t snd_pcm_lib_readv(struct snd_pcm_substream *substream,
				    void __user **bufs,
				    snd_pcm_uframes_t frames)
{
	struct snd_pcm_runtime *runtime;
	int nonblock;
	int err;

	err = pcm_sanity_check(substream);
	if (err < 0)
		return err;
	runtime = substream->runtime;
	if (runtime->status->state == SNDRV_PCM_STATE_OPEN)
		return -EBADFD;

	nonblock = !!(substream->f_flags & O_NONBLOCK);
	if (runtime->access != SNDRV_PCM_ACCESS_RW_NONINTERLEAVED)
		return -EINVAL;
	return snd_pcm_lib_read1(substream, (unsigned long)bufs, frames, nonblock, snd_pcm_lib_readv_transfer);
}

EXPORT_SYMBOL(snd_pcm_lib_readv);
