// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Dummy soundcard
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 */

#include <linux/init.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/hrtimer.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/tlv.h>
#include <sound/pcm.h>
#include <sound/rawmidi.h>
#include <sound/info.h>
#include <sound/initval.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@perex.cz>");
MODULE_DESCRIPTION("Dummy soundcard (/dev/null)");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{ALSA,Dummy soundcard}}");

#define MAX_PCM_DEVICES		4
#define MAX_PCM_SUBSTREAMS	128
#define MAX_MIDI_DEVICES	2

/* defaults */
#define MAX_BUFFER_SIZE		(64*1024)
#define MIN_PERIOD_SIZE		64
#define MAX_PERIOD_SIZE		MAX_BUFFER_SIZE
#define USE_FORMATS 		(SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE)
#define USE_RATE		SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000
#define USE_RATE_MIN		5500
#define USE_RATE_MAX		48000
#define USE_CHANNELS_MIN 	1
#define USE_CHANNELS_MAX 	2
#define USE_PERIODS_MIN 	1
#define USE_PERIODS_MAX 	1024

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static bool enable[SNDRV_CARDS] = {1, [1 ... (SNDRV_CARDS - 1)] = 0};
static char *model[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = NULL};
static int pcm_devs[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 1};
static int pcm_substreams[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 8};
//static int midi_devs[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 2};
#ifdef CONFIG_HIGH_RES_TIMERS
static bool hrtimer = 1;
#endif
static bool fake_buffer = 1;

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for dummy soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for dummy soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable this dummy soundcard.");
module_param_array(model, charp, NULL, 0444);
MODULE_PARM_DESC(model, "Soundcard model.");
module_param_array(pcm_devs, int, NULL, 0444);
MODULE_PARM_DESC(pcm_devs, "PCM devices # (0-4) for dummy driver.");
module_param_array(pcm_substreams, int, NULL, 0444);
MODULE_PARM_DESC(pcm_substreams, "PCM substreams # (1-128) for dummy driver.");
//module_param_array(midi_devs, int, NULL, 0444);
//MODULE_PARM_DESC(midi_devs, "MIDI devices # (0-2) for dummy driver.");
module_param(fake_buffer, bool, 0444);
MODULE_PARM_DESC(fake_buffer, "Fake buffer allocations.");
#ifdef CONFIG_HIGH_RES_TIMERS
module_param(hrtimer, bool, 0644);
MODULE_PARM_DESC(hrtimer, "Use hrtimer as the timer source.");
#endif

static struct platform_device *devices[SNDRV_CARDS];

#define MIXER_ADDR_MASTER	0
#define MIXER_ADDR_LINE		1
#define MIXER_ADDR_MIC		2
#define MIXER_ADDR_SYNTH	3
#define MIXER_ADDR_CD		4
#define MIXER_ADDR_LAST		4

struct dummy_timer_ops {
	int (*create)(struct snd_pcm_substream *);
	void (*free)(struct snd_pcm_substream *);
	int (*prepare)(struct snd_pcm_substream *);
	int (*start)(struct snd_pcm_substream *);
	int (*stop)(struct snd_pcm_substream *);
	snd_pcm_uframes_t (*pointer)(struct snd_pcm_substream *);
};

#define get_dummy_ops(substream) \
	(*(const struct dummy_timer_ops **)(substream)->runtime->private_data)

struct dummy_model {
	const char *name;
	int (*playback_constraints)(struct snd_pcm_runtime *runtime);
	int (*capture_constraints)(struct snd_pcm_runtime *runtime);
	u64 formats;
	size_t buffer_bytes_max;
	size_t period_bytes_min;
	size_t period_bytes_max;
	unsigned int periods_min;
	unsigned int periods_max;
	unsigned int rates;
	unsigned int rate_min;
	unsigned int rate_max;
	unsigned int channels_min;
	unsigned int channels_max;
};

struct snd_dummy {
	struct snd_card *card;
	struct dummy_model *model;
	struct snd_pcm *pcm;
	struct snd_pcm_hardware pcm_hw;
	spinlock_t mixer_lock;
	int mixer_volume[MIXER_ADDR_LAST+1][2];
	int capture_source[MIXER_ADDR_LAST+1][2];
	int iobox;
	struct snd_kcontrol *cd_volume_ctl;
	struct snd_kcontrol *cd_switch_ctl;
};

/*
 * card models
 */

static int emu10k1_playback_constraints(struct snd_pcm_runtime *runtime)
{
	int err;
	err = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	if (err < 0)
		return err;
	err = snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_BYTES, 256, UINT_MAX);
	if (err < 0)
		return err;
	return 0;
}

static struct dummy_model model_emu10k1 = {
	.name = "emu10k1",
	.playback_constraints = emu10k1_playback_constraints,
	.buffer_bytes_max = 128 * 1024,
};

static struct dummy_model model_rme9652 = {
	.name = "rme9652",
	.buffer_bytes_max = 26 * 64 * 1024,
	.formats = SNDRV_PCM_FMTBIT_S32_LE,
	.channels_min = 26,
	.channels_max = 26,
	.periods_min = 2,
	.periods_max = 2,
};

static struct dummy_model model_ice1712 = {
	.name = "ice1712",
	.buffer_bytes_max = 256 * 1024,
	.formats = SNDRV_PCM_FMTBIT_S32_LE,
	.channels_min = 10,
	.channels_max = 10,
	.periods_min = 1,
	.periods_max = 1024,
};

static struct dummy_model model_uda1341 = {
	.name = "uda1341",
	.buffer_bytes_max = 16380,
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.channels_min = 2,
	.channels_max = 2,
	.periods_min = 2,
	.periods_max = 255,
};

static struct dummy_model model_ac97 = {
	.name = "ac97",
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.channels_min = 2,
	.channels_max = 2,
	.rates = SNDRV_PCM_RATE_48000,
	.rate_min = 48000,
	.rate_max = 48000,
};

static struct dummy_model model_ca0106 = {
	.name = "ca0106",
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.buffer_bytes_max = ((65536-64)*8),
	.period_bytes_max = (65536-64),
	.periods_min = 2,
	.periods_max = 8,
	.channels_min = 2,
	.channels_max = 2,
	.rates = SNDRV_PCM_RATE_48000|SNDRV_PCM_RATE_96000|SNDRV_PCM_RATE_192000,
	.rate_min = 48000,
	.rate_max = 192000,
};

static struct dummy_model *dummy_models[] = {
	&model_emu10k1,
	&model_rme9652,
	&model_ice1712,
	&model_uda1341,
	&model_ac97,
	&model_ca0106,
	NULL
};

/*
 * system timer interface
 */

struct dummy_systimer_pcm {
	/* ops must be the first item */
	const struct dummy_timer_ops *timer_ops;
	spinlock_t lock;
	struct timer_list timer;
	unsigned long base_time;
	unsigned int frac_pos;	/* fractional sample position (based HZ) */
	unsigned int frac_period_rest;
	unsigned int frac_buffer_size;	/* buffer_size * HZ */
	unsigned int frac_period_size;	/* period_size * HZ */
	unsigned int rate;
	int elapsed;
	struct snd_pcm_substream *substream;
};

static void dummy_systimer_rearm(struct dummy_systimer_pcm *dpcm)
{
	mod_timer(&dpcm->timer, jiffies +
		(dpcm->frac_period_rest + dpcm->rate - 1) / dpcm->rate);
}

static void dummy_systimer_update(struct dummy_systimer_pcm *dpcm)
{
	unsigned long delta;

	delta = jiffies - dpcm->base_time;
	if (!delta)
		return;
	dpcm->base_time += delta;
	delta *= dpcm->rate;
	dpcm->frac_pos += delta;
	while (dpcm->frac_pos >= dpcm->frac_buffer_size)
		dpcm->frac_pos -= dpcm->frac_buffer_size;
	while (dpcm->frac_period_rest <= delta) {
		dpcm->elapsed++;
		dpcm->frac_period_rest += dpcm->frac_period_size;
	}
	dpcm->frac_period_rest -= delta;
}

static int dummy_systimer_start(struct snd_pcm_substream *substream)
{
	struct dummy_systimer_pcm *dpcm = substream->runtime->private_data;
	spin_lock(&dpcm->lock);
	dpcm->base_time = jiffies;
	dummy_systimer_rearm(dpcm);
	spin_unlock(&dpcm->lock);
	return 0;
}

static int dummy_systimer_stop(struct snd_pcm_substream *substream)
{
	struct dummy_systimer_pcm *dpcm = substream->runtime->private_data;
	spin_lock(&dpcm->lock);
	del_timer(&dpcm->timer);
	spin_unlock(&dpcm->lock);
	return 0;
}

static int dummy_systimer_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct dummy_systimer_pcm *dpcm = runtime->private_data;

	dpcm->frac_pos = 0;
	dpcm->rate = runtime->rate;
	dpcm->frac_buffer_size = runtime->buffer_size * HZ;
	dpcm->frac_period_size = runtime->period_size * HZ;
	dpcm->frac_period_rest = dpcm->frac_period_size;
	dpcm->elapsed = 0;

	return 0;
}

static void dummy_systimer_callback(struct timer_list *t)
{
	struct dummy_systimer_pcm *dpcm = from_timer(dpcm, t, timer);
	unsigned long flags;
	int elapsed = 0;
	
	spin_lock_irqsave(&dpcm->lock, flags);
	dummy_systimer_update(dpcm);
	dummy_systimer_rearm(dpcm);
	elapsed = dpcm->elapsed;
	dpcm->elapsed = 0;
	spin_unlock_irqrestore(&dpcm->lock, flags);
	if (elapsed)
		snd_pcm_period_elapsed(dpcm->substream);
}

static snd_pcm_uframes_t
dummy_systimer_pointer(struct snd_pcm_substream *substream)
{
	struct dummy_systimer_pcm *dpcm = substream->runtime->private_data;
	snd_pcm_uframes_t pos;

	spin_lock(&dpcm->lock);
	dummy_systimer_update(dpcm);
	pos = dpcm->frac_pos / HZ;
	spin_unlock(&dpcm->lock);
	return pos;
}

static int dummy_systimer_create(struct snd_pcm_substream *substream)
{
	struct dummy_systimer_pcm *dpcm;

	dpcm = kzalloc(sizeof(*dpcm), GFP_KERNEL);
	if (!dpcm)
		return -ENOMEM;
	substream->runtime->private_data = dpcm;
	timer_setup(&dpcm->timer, dummy_systimer_callback, 0);
	spin_lock_init(&dpcm->lock);
	dpcm->substream = substream;
	return 0;
}

static void dummy_systimer_free(struct snd_pcm_substream *substream)
{
	kfree(substream->runtime->private_data);
}

static const struct dummy_timer_ops dummy_systimer_ops = {
	.create =	dummy_systimer_create,
	.free =		dummy_systimer_free,
	.prepare =	dummy_systimer_prepare,
	.start =	dummy_systimer_start,
	.stop =		dummy_systimer_stop,
	.pointer =	dummy_systimer_pointer,
};

#ifdef CONFIG_HIGH_RES_TIMERS
/*
 * hrtimer interface
 */

struct dummy_hrtimer_pcm {
	/* ops must be the first item */
	const struct dummy_timer_ops *timer_ops;
	ktime_t base_time;
	ktime_t period_time;
	atomic_t running;
	struct hrtimer timer;
	struct snd_pcm_substream *substream;
};

static enum hrtimer_restart dummy_hrtimer_callback(struct hrtimer *timer)
{
	struct dummy_hrtimer_pcm *dpcm;

	dpcm = container_of(timer, struct dummy_hrtimer_pcm, timer);
	if (!atomic_read(&dpcm->running))
		return HRTIMER_NORESTART;
	/*
	 * In cases of XRUN and draining, this calls .trigger to stop PCM
	 * substream.
	 */
	snd_pcm_period_elapsed(dpcm->substream);
	if (!atomic_read(&dpcm->running))
		return HRTIMER_NORESTART;

	hrtimer_forward_now(timer, dpcm->period_time);
	return HRTIMER_RESTART;
}

static int dummy_hrtimer_start(struct snd_pcm_substream *substream)
{
	struct dummy_hrtimer_pcm *dpcm = substream->runtime->private_data;

	dpcm->base_time = hrtimer_cb_get_time(&dpcm->timer);
	hrtimer_start(&dpcm->timer, dpcm->period_time, HRTIMER_MODE_REL_SOFT);
	atomic_set(&dpcm->running, 1);
	return 0;
}

static int dummy_hrtimer_stop(struct snd_pcm_substream *substream)
{
	struct dummy_hrtimer_pcm *dpcm = substream->runtime->private_data;

	atomic_set(&dpcm->running, 0);
	if (!hrtimer_callback_running(&dpcm->timer))
		hrtimer_cancel(&dpcm->timer);
	return 0;
}

static inline void dummy_hrtimer_sync(struct dummy_hrtimer_pcm *dpcm)
{
	hrtimer_cancel(&dpcm->timer);
}

static snd_pcm_uframes_t
dummy_hrtimer_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct dummy_hrtimer_pcm *dpcm = runtime->private_data;
	u64 delta;
	u32 pos;

	delta = ktime_us_delta(hrtimer_cb_get_time(&dpcm->timer),
			       dpcm->base_time);
	delta = div_u64(delta * runtime->rate + 999999, 1000000);
	div_u64_rem(delta, runtime->buffer_size, &pos);
	return pos;
}

static int dummy_hrtimer_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct dummy_hrtimer_pcm *dpcm = runtime->private_data;
	unsigned int period, rate;
	long sec;
	unsigned long nsecs;

	dummy_hrtimer_sync(dpcm);
	period = runtime->period_size;
	rate = runtime->rate;
	sec = period / rate;
	period %= rate;
	nsecs = div_u64((u64)period * 1000000000UL + rate - 1, rate);
	dpcm->period_time = ktime_set(sec, nsecs);

	return 0;
}

static int dummy_hrtimer_create(struct snd_pcm_substream *substream)
{
	struct dummy_hrtimer_pcm *dpcm;

	dpcm = kzalloc(sizeof(*dpcm), GFP_KERNEL);
	if (!dpcm)
		return -ENOMEM;
	substream->runtime->private_data = dpcm;
	hrtimer_init(&dpcm->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL_SOFT);
	dpcm->timer.function = dummy_hrtimer_callback;
	dpcm->substream = substream;
	atomic_set(&dpcm->running, 0);
	return 0;
}

static void dummy_hrtimer_free(struct snd_pcm_substream *substream)
{
	struct dummy_hrtimer_pcm *dpcm = substream->runtime->private_data;
	dummy_hrtimer_sync(dpcm);
	kfree(dpcm);
}

static const struct dummy_timer_ops dummy_hrtimer_ops = {
	.create =	dummy_hrtimer_create,
	.free =		dummy_hrtimer_free,
	.prepare =	dummy_hrtimer_prepare,
	.start =	dummy_hrtimer_start,
	.stop =		dummy_hrtimer_stop,
	.pointer =	dummy_hrtimer_pointer,
};

#endif /* CONFIG_HIGH_RES_TIMERS */

/*
 * PCM interface
 */

static int dummy_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		return get_dummy_ops(substream)->start(substream);
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		return get_dummy_ops(substream)->stop(substream);
	}
	return -EINVAL;
}

static int dummy_pcm_prepare(struct snd_pcm_substream *substream)
{
	return get_dummy_ops(substream)->prepare(substream);
}

static snd_pcm_uframes_t dummy_pcm_pointer(struct snd_pcm_substream *substream)
{
	return get_dummy_ops(substream)->pointer(substream);
}

static const struct snd_pcm_hardware dummy_pcm_hardware = {
	.info =			(SNDRV_PCM_INFO_MMAP |
				 SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_RESUME |
				 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		USE_FORMATS,
	.rates =		USE_RATE,
	.rate_min =		USE_RATE_MIN,
	.rate_max =		USE_RATE_MAX,
	.channels_min =		USE_CHANNELS_MIN,
	.channels_max =		USE_CHANNELS_MAX,
	.buffer_bytes_max =	MAX_BUFFER_SIZE,
	.period_bytes_min =	MIN_PERIOD_SIZE,
	.period_bytes_max =	MAX_PERIOD_SIZE,
	.periods_min =		USE_PERIODS_MIN,
	.periods_max =		USE_PERIODS_MAX,
	.fifo_size =		0,
};

static int dummy_pcm_hw_params(struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *hw_params)
{
	if (fake_buffer) {
		/* runtime->dma_bytes has to be set manually to allow mmap */
		substream->runtime->dma_bytes = params_buffer_bytes(hw_params);
		return 0;
	}
	return snd_pcm_lib_malloc_pages(substream,
					params_buffer_bytes(hw_params));
}

static int dummy_pcm_hw_free(struct snd_pcm_substream *substream)
{
	if (fake_buffer)
		return 0;
	return snd_pcm_lib_free_pages(substream);
}

static int dummy_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_dummy *dummy = snd_pcm_substream_chip(substream);
	struct dummy_model *model = dummy->model;
	struct snd_pcm_runtime *runtime = substream->runtime;
	const struct dummy_timer_ops *ops;
	int err;

	ops = &dummy_systimer_ops;
#ifdef CONFIG_HIGH_RES_TIMERS
	if (hrtimer)
		ops = &dummy_hrtimer_ops;
#endif

	err = ops->create(substream);
	if (err < 0)
		return err;
	get_dummy_ops(substream) = ops;

	runtime->hw = dummy->pcm_hw;
	if (substream->pcm->device & 1) {
		runtime->hw.info &= ~SNDRV_PCM_INFO_INTERLEAVED;
		runtime->hw.info |= SNDRV_PCM_INFO_NONINTERLEAVED;
	}
	if (substream->pcm->device & 2)
		runtime->hw.info &= ~(SNDRV_PCM_INFO_MMAP |
				      SNDRV_PCM_INFO_MMAP_VALID);

	if (model == NULL)
		return 0;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (model->playback_constraints)
			err = model->playback_constraints(substream->runtime);
	} else {
		if (model->capture_constraints)
			err = model->capture_constraints(substream->runtime);
	}
	if (err < 0) {
		get_dummy_ops(substream)->free(substream);
		return err;
	}
	return 0;
}

static int dummy_pcm_close(struct snd_pcm_substream *substream)
{
	get_dummy_ops(substream)->free(substream);
	return 0;
}

/*
 * dummy buffer handling
 */

static void *dummy_page[2];

static void free_fake_buffer(void)
{
	if (fake_buffer) {
		int i;
		for (i = 0; i < 2; i++)
			if (dummy_page[i]) {
				free_page((unsigned long)dummy_page[i]);
				dummy_page[i] = NULL;
			}
	}
}

static int alloc_fake_buffer(void)
{
	int i;

	if (!fake_buffer)
		return 0;
	for (i = 0; i < 2; i++) {
		dummy_page[i] = (void *)get_zeroed_page(GFP_KERNEL);
		if (!dummy_page[i]) {
			free_fake_buffer();
			return -ENOMEM;
		}
	}
	return 0;
}

static int dummy_pcm_copy(struct snd_pcm_substream *substream,
			  int channel, unsigned long pos,
			  void __user *dst, unsigned long bytes)
{
	return 0; /* do nothing */
}

static int dummy_pcm_copy_kernel(struct snd_pcm_substream *substream,
				 int channel, unsigned long pos,
				 void *dst, unsigned long bytes)
{
	return 0; /* do nothing */
}

static int dummy_pcm_silence(struct snd_pcm_substream *substream,
			     int channel, unsigned long pos,
			     unsigned long bytes)
{
	return 0; /* do nothing */
}

static struct page *dummy_pcm_page(struct snd_pcm_substream *substream,
				   unsigned long offset)
{
	return virt_to_page(dummy_page[substream->stream]); /* the same page */
}

static struct snd_pcm_ops dummy_pcm_ops = {
	.open =		dummy_pcm_open,
	.close =	dummy_pcm_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	dummy_pcm_hw_params,
	.hw_free =	dummy_pcm_hw_free,
	.prepare =	dummy_pcm_prepare,
	.trigger =	dummy_pcm_trigger,
	.pointer =	dummy_pcm_pointer,
};

static struct snd_pcm_ops dummy_pcm_ops_no_buf = {
	.open =		dummy_pcm_open,
	.close =	dummy_pcm_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	dummy_pcm_hw_params,
	.hw_free =	dummy_pcm_hw_free,
	.prepare =	dummy_pcm_prepare,
	.trigger =	dummy_pcm_trigger,
	.pointer =	dummy_pcm_pointer,
	.copy_user =	dummy_pcm_copy,
	.copy_kernel =	dummy_pcm_copy_kernel,
	.fill_silence =	dummy_pcm_silence,
	.page =		dummy_pcm_page,
};

static int snd_card_dummy_pcm(struct snd_dummy *dummy, int device,
			      int substreams)
{
	struct snd_pcm *pcm;
	struct snd_pcm_ops *ops;
	int err;

	err = snd_pcm_new(dummy->card, "Dummy PCM", device,
			       substreams, substreams, &pcm);
	if (err < 0)
		return err;
	dummy->pcm = pcm;
	if (fake_buffer)
		ops = &dummy_pcm_ops_no_buf;
	else
		ops = &dummy_pcm_ops;
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, ops);
	pcm->private_data = dummy;
	pcm->info_flags = 0;
	strcpy(pcm->name, "Dummy PCM");
	if (!fake_buffer) {
		snd_pcm_lib_preallocate_pages_for_all(pcm,
			SNDRV_DMA_TYPE_CONTINUOUS,
			NULL,
			0, 64*1024);
	}
	return 0;
}

/*
 * mixer interface
 */

#define DUMMY_VOLUME(xname, xindex, addr) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .access = SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_TLV_READ, \
  .name = xname, .index = xindex, \
  .info = snd_dummy_volume_info, \
  .get = snd_dummy_volume_get, .put = snd_dummy_volume_put, \
  .private_value = addr, \
  .tlv = { .p = db_scale_dummy } }

static int snd_dummy_volume_info(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = -50;
	uinfo->value.integer.max = 100;
	return 0;
}
 
static int snd_dummy_volume_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_dummy *dummy = snd_kcontrol_chip(kcontrol);
	int addr = kcontrol->private_value;

	spin_lock_irq(&dummy->mixer_lock);
	ucontrol->value.integer.value[0] = dummy->mixer_volume[addr][0];
	ucontrol->value.integer.value[1] = dummy->mixer_volume[addr][1];
	spin_unlock_irq(&dummy->mixer_lock);
	return 0;
}

static int snd_dummy_volume_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_dummy *dummy = snd_kcontrol_chip(kcontrol);
	int change, addr = kcontrol->private_value;
	int left, right;

	left = ucontrol->value.integer.value[0];
	if (left < -50)
		left = -50;
	if (left > 100)
		left = 100;
	right = ucontrol->value.integer.value[1];
	if (right < -50)
		right = -50;
	if (right > 100)
		right = 100;
	spin_lock_irq(&dummy->mixer_lock);
	change = dummy->mixer_volume[addr][0] != left ||
	         dummy->mixer_volume[addr][1] != right;
	dummy->mixer_volume[addr][0] = left;
	dummy->mixer_volume[addr][1] = right;
	spin_unlock_irq(&dummy->mixer_lock);
	return change;
}

static const DECLARE_TLV_DB_SCALE(db_scale_dummy, -4500, 30, 0);

#define DUMMY_CAPSRC(xname, xindex, addr) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .info = snd_dummy_capsrc_info, \
  .get = snd_dummy_capsrc_get, .put = snd_dummy_capsrc_put, \
  .private_value = addr }

#define snd_dummy_capsrc_info	snd_ctl_boolean_stereo_info
 
static int snd_dummy_capsrc_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_dummy *dummy = snd_kcontrol_chip(kcontrol);
	int addr = kcontrol->private_value;

	spin_lock_irq(&dummy->mixer_lock);
	ucontrol->value.integer.value[0] = dummy->capture_source[addr][0];
	ucontrol->value.integer.value[1] = dummy->capture_source[addr][1];
	spin_unlock_irq(&dummy->mixer_lock);
	return 0;
}

static int snd_dummy_capsrc_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_dummy *dummy = snd_kcontrol_chip(kcontrol);
	int change, addr = kcontrol->private_value;
	int left, right;

	left = ucontrol->value.integer.value[0] & 1;
	right = ucontrol->value.integer.value[1] & 1;
	spin_lock_irq(&dummy->mixer_lock);
	change = dummy->capture_source[addr][0] != left &&
	         dummy->capture_source[addr][1] != right;
	dummy->capture_source[addr][0] = left;
	dummy->capture_source[addr][1] = right;
	spin_unlock_irq(&dummy->mixer_lock);
	return change;
}

static int snd_dummy_iobox_info(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *info)
{
	static const char *const names[] = { "None", "CD Player" };

	return snd_ctl_enum_info(info, 1, 2, names);
}

static int snd_dummy_iobox_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *value)
{
	struct snd_dummy *dummy = snd_kcontrol_chip(kcontrol);

	value->value.enumerated.item[0] = dummy->iobox;
	return 0;
}

static int snd_dummy_iobox_put(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *value)
{
	struct snd_dummy *dummy = snd_kcontrol_chip(kcontrol);
	int changed;

	if (value->value.enumerated.item[0] > 1)
		return -EINVAL;

	changed = value->value.enumerated.item[0] != dummy->iobox;
	if (changed) {
		dummy->iobox = value->value.enumerated.item[0];

		if (dummy->iobox) {
			dummy->cd_volume_ctl->vd[0].access &=
				~SNDRV_CTL_ELEM_ACCESS_INACTIVE;
			dummy->cd_switch_ctl->vd[0].access &=
				~SNDRV_CTL_ELEM_ACCESS_INACTIVE;
		} else {
			dummy->cd_volume_ctl->vd[0].access |=
				SNDRV_CTL_ELEM_ACCESS_INACTIVE;
			dummy->cd_switch_ctl->vd[0].access |=
				SNDRV_CTL_ELEM_ACCESS_INACTIVE;
		}

		snd_ctl_notify(dummy->card, SNDRV_CTL_EVENT_MASK_INFO,
			       &dummy->cd_volume_ctl->id);
		snd_ctl_notify(dummy->card, SNDRV_CTL_EVENT_MASK_INFO,
			       &dummy->cd_switch_ctl->id);
	}

	return changed;
}

static struct snd_kcontrol_new snd_dummy_controls[] = {
DUMMY_VOLUME("Master Volume", 0, MIXER_ADDR_MASTER),
DUMMY_CAPSRC("Master Capture Switch", 0, MIXER_ADDR_MASTER),
DUMMY_VOLUME("Synth Volume", 0, MIXER_ADDR_SYNTH),
DUMMY_CAPSRC("Synth Capture Switch", 0, MIXER_ADDR_SYNTH),
DUMMY_VOLUME("Line Volume", 0, MIXER_ADDR_LINE),
DUMMY_CAPSRC("Line Capture Switch", 0, MIXER_ADDR_LINE),
DUMMY_VOLUME("Mic Volume", 0, MIXER_ADDR_MIC),
DUMMY_CAPSRC("Mic Capture Switch", 0, MIXER_ADDR_MIC),
DUMMY_VOLUME("CD Volume", 0, MIXER_ADDR_CD),
DUMMY_CAPSRC("CD Capture Switch", 0, MIXER_ADDR_CD),
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name  = "External I/O Box",
	.info  = snd_dummy_iobox_info,
	.get   = snd_dummy_iobox_get,
	.put   = snd_dummy_iobox_put,
},
};

static int snd_card_dummy_new_mixer(struct snd_dummy *dummy)
{
	struct snd_card *card = dummy->card;
	struct snd_kcontrol *kcontrol;
	unsigned int idx;
	int err;

	spin_lock_init(&dummy->mixer_lock);
	strcpy(card->mixername, "Dummy Mixer");
	dummy->iobox = 1;

	for (idx = 0; idx < ARRAY_SIZE(snd_dummy_controls); idx++) {
		kcontrol = snd_ctl_new1(&snd_dummy_controls[idx], dummy);
		err = snd_ctl_add(card, kcontrol);
		if (err < 0)
			return err;
		if (!strcmp(kcontrol->id.name, "CD Volume"))
			dummy->cd_volume_ctl = kcontrol;
		else if (!strcmp(kcontrol->id.name, "CD Capture Switch"))
			dummy->cd_switch_ctl = kcontrol;

	}
	return 0;
}

#if defined(CONFIG_SND_DEBUG) && defined(CONFIG_SND_PROC_FS)
/*
 * proc interface
 */
static void print_formats(struct snd_dummy *dummy,
			  struct snd_info_buffer *buffer)
{
	int i;

	for (i = 0; i < SNDRV_PCM_FORMAT_LAST; i++) {
		if (dummy->pcm_hw.formats & (1ULL << i))
			snd_iprintf(buffer, " %s", snd_pcm_format_name(i));
	}
}

static void print_rates(struct snd_dummy *dummy,
			struct snd_info_buffer *buffer)
{
	static int rates[] = {
		5512, 8000, 11025, 16000, 22050, 32000, 44100, 48000,
		64000, 88200, 96000, 176400, 192000,
	};
	int i;

	if (dummy->pcm_hw.rates & SNDRV_PCM_RATE_CONTINUOUS)
		snd_iprintf(buffer, " continuous");
	if (dummy->pcm_hw.rates & SNDRV_PCM_RATE_KNOT)
		snd_iprintf(buffer, " knot");
	for (i = 0; i < ARRAY_SIZE(rates); i++)
		if (dummy->pcm_hw.rates & (1 << i))
			snd_iprintf(buffer, " %d", rates[i]);
}

#define get_dummy_int_ptr(dummy, ofs) \
	(unsigned int *)((char *)&((dummy)->pcm_hw) + (ofs))
#define get_dummy_ll_ptr(dummy, ofs) \
	(unsigned long long *)((char *)&((dummy)->pcm_hw) + (ofs))

struct dummy_hw_field {
	const char *name;
	const char *format;
	unsigned int offset;
	unsigned int size;
};
#define FIELD_ENTRY(item, fmt) {		   \
	.name = #item,				   \
	.format = fmt,				   \
	.offset = offsetof(struct snd_pcm_hardware, item), \
	.size = sizeof(dummy_pcm_hardware.item) }

static struct dummy_hw_field fields[] = {
	FIELD_ENTRY(formats, "%#llx"),
	FIELD_ENTRY(rates, "%#x"),
	FIELD_ENTRY(rate_min, "%d"),
	FIELD_ENTRY(rate_max, "%d"),
	FIELD_ENTRY(channels_min, "%d"),
	FIELD_ENTRY(channels_max, "%d"),
	FIELD_ENTRY(buffer_bytes_max, "%ld"),
	FIELD_ENTRY(period_bytes_min, "%ld"),
	FIELD_ENTRY(period_bytes_max, "%ld"),
	FIELD_ENTRY(periods_min, "%d"),
	FIELD_ENTRY(periods_max, "%d"),
};

static void dummy_proc_read(struct snd_info_entry *entry,
			    struct snd_info_buffer *buffer)
{
	struct snd_dummy *dummy = entry->private_data;
	int i;

	for (i = 0; i < ARRAY_SIZE(fields); i++) {
		snd_iprintf(buffer, "%s ", fields[i].name);
		if (fields[i].size == sizeof(int))
			snd_iprintf(buffer, fields[i].format,
				*get_dummy_int_ptr(dummy, fields[i].offset));
		else
			snd_iprintf(buffer, fields[i].format,
				*get_dummy_ll_ptr(dummy, fields[i].offset));
		if (!strcmp(fields[i].name, "formats"))
			print_formats(dummy, buffer);
		else if (!strcmp(fields[i].name, "rates"))
			print_rates(dummy, buffer);
		snd_iprintf(buffer, "\n");
	}
}

static void dummy_proc_write(struct snd_info_entry *entry,
			     struct snd_info_buffer *buffer)
{
	struct snd_dummy *dummy = entry->private_data;
	char line[64];

	while (!snd_info_get_line(buffer, line, sizeof(line))) {
		char item[20];
		const char *ptr;
		unsigned long long val;
		int i;

		ptr = snd_info_get_str(item, line, sizeof(item));
		for (i = 0; i < ARRAY_SIZE(fields); i++) {
			if (!strcmp(item, fields[i].name))
				break;
		}
		if (i >= ARRAY_SIZE(fields))
			continue;
		snd_info_get_str(item, ptr, sizeof(item));
		if (kstrtoull(item, 0, &val))
			continue;
		if (fields[i].size == sizeof(int))
			*get_dummy_int_ptr(dummy, fields[i].offset) = val;
		else
			*get_dummy_ll_ptr(dummy, fields[i].offset) = val;
	}
}

static void dummy_proc_init(struct snd_dummy *chip)
{
	snd_card_rw_proc_new(chip->card, "dummy_pcm", chip,
			     dummy_proc_read, dummy_proc_write);
}
#else
#define dummy_proc_init(x)
#endif /* CONFIG_SND_DEBUG && CONFIG_SND_PROC_FS */

static int snd_dummy_probe(struct platform_device *devptr)
{
	struct snd_card *card;
	struct snd_dummy *dummy;
	struct dummy_model *m = NULL, **mdl;
	int idx, err;
	int dev = devptr->id;

	err = snd_card_new(&devptr->dev, index[dev], id[dev], THIS_MODULE,
			   sizeof(struct snd_dummy), &card);
	if (err < 0)
		return err;
	dummy = card->private_data;
	dummy->card = card;
	for (mdl = dummy_models; *mdl && model[dev]; mdl++) {
		if (strcmp(model[dev], (*mdl)->name) == 0) {
			printk(KERN_INFO
				"snd-dummy: Using model '%s' for card %i\n",
				(*mdl)->name, card->number);
			m = dummy->model = *mdl;
			break;
		}
	}
	for (idx = 0; idx < MAX_PCM_DEVICES && idx < pcm_devs[dev]; idx++) {
		if (pcm_substreams[dev] < 1)
			pcm_substreams[dev] = 1;
		if (pcm_substreams[dev] > MAX_PCM_SUBSTREAMS)
			pcm_substreams[dev] = MAX_PCM_SUBSTREAMS;
		err = snd_card_dummy_pcm(dummy, idx, pcm_substreams[dev]);
		if (err < 0)
			goto __nodev;
	}

	dummy->pcm_hw = dummy_pcm_hardware;
	if (m) {
		if (m->formats)
			dummy->pcm_hw.formats = m->formats;
		if (m->buffer_bytes_max)
			dummy->pcm_hw.buffer_bytes_max = m->buffer_bytes_max;
		if (m->period_bytes_min)
			dummy->pcm_hw.period_bytes_min = m->period_bytes_min;
		if (m->period_bytes_max)
			dummy->pcm_hw.period_bytes_max = m->period_bytes_max;
		if (m->periods_min)
			dummy->pcm_hw.periods_min = m->periods_min;
		if (m->periods_max)
			dummy->pcm_hw.periods_max = m->periods_max;
		if (m->rates)
			dummy->pcm_hw.rates = m->rates;
		if (m->rate_min)
			dummy->pcm_hw.rate_min = m->rate_min;
		if (m->rate_max)
			dummy->pcm_hw.rate_max = m->rate_max;
		if (m->channels_min)
			dummy->pcm_hw.channels_min = m->channels_min;
		if (m->channels_max)
			dummy->pcm_hw.channels_max = m->channels_max;
	}

	err = snd_card_dummy_new_mixer(dummy);
	if (err < 0)
		goto __nodev;
	strcpy(card->driver, "Dummy");
	strcpy(card->shortname, "Dummy");
	sprintf(card->longname, "Dummy %i", dev + 1);

	dummy_proc_init(dummy);

	err = snd_card_register(card);
	if (err == 0) {
		platform_set_drvdata(devptr, card);
		return 0;
	}
      __nodev:
	snd_card_free(card);
	return err;
}

static int snd_dummy_remove(struct platform_device *devptr)
{
	snd_card_free(platform_get_drvdata(devptr));
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int snd_dummy_suspend(struct device *pdev)
{
	struct snd_card *card = dev_get_drvdata(pdev);

	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
	return 0;
}
	
static int snd_dummy_resume(struct device *pdev)
{
	struct snd_card *card = dev_get_drvdata(pdev);

	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	return 0;
}

static SIMPLE_DEV_PM_OPS(snd_dummy_pm, snd_dummy_suspend, snd_dummy_resume);
#define SND_DUMMY_PM_OPS	&snd_dummy_pm
#else
#define SND_DUMMY_PM_OPS	NULL
#endif

#define SND_DUMMY_DRIVER	"snd_dummy"

static struct platform_driver snd_dummy_driver = {
	.probe		= snd_dummy_probe,
	.remove		= snd_dummy_remove,
	.driver		= {
		.name	= SND_DUMMY_DRIVER,
		.pm	= SND_DUMMY_PM_OPS,
	},
};

static void snd_dummy_unregister_all(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(devices); ++i)
		platform_device_unregister(devices[i]);
	platform_driver_unregister(&snd_dummy_driver);
	free_fake_buffer();
}

static int __init alsa_card_dummy_init(void)
{
	int i, cards, err;

	err = platform_driver_register(&snd_dummy_driver);
	if (err < 0)
		return err;

	err = alloc_fake_buffer();
	if (err < 0) {
		platform_driver_unregister(&snd_dummy_driver);
		return err;
	}

	cards = 0;
	for (i = 0; i < SNDRV_CARDS; i++) {
		struct platform_device *device;
		if (! enable[i])
			continue;
		device = platform_device_register_simple(SND_DUMMY_DRIVER,
							 i, NULL, 0);
		if (IS_ERR(device))
			continue;
		if (!platform_get_drvdata(device)) {
			platform_device_unregister(device);
			continue;
		}
		devices[i] = device;
		cards++;
	}
	if (!cards) {
#ifdef MODULE
		printk(KERN_ERR "Dummy soundcard not found or device busy\n");
#endif
		snd_dummy_unregister_all();
		return -ENODEV;
	}
	return 0;
}

static void __exit alsa_card_dummy_exit(void)
{
	snd_dummy_unregister_all();
}

module_init(alsa_card_dummy_init)
module_exit(alsa_card_dummy_exit)
