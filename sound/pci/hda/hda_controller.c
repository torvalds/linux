// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 *  Implementation of primary alsa driver code base for Intel HD Audio.
 *
 *  Copyright(c) 2004 Intel Corporation. All rights reserved.
 *
 *  Copyright (c) 2004 Takashi Iwai <tiwai@suse.de>
 *                     PeiSen Hou <pshou@realtek.com.tw>
 */

#include <linux/clocksource.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

#ifdef CONFIG_X86
/* for art-tsc conversion */
#include <asm/tsc.h>
#endif

#include <sound/core.h>
#include <sound/initval.h>
#include "hda_controller.h"
#include "hda_local.h"

#define CREATE_TRACE_POINTS
#include "hda_controller_trace.h"

/* DSP lock helpers */
#define dsp_lock(dev)		snd_hdac_dsp_lock(azx_stream(dev))
#define dsp_unlock(dev)		snd_hdac_dsp_unlock(azx_stream(dev))
#define dsp_is_locked(dev)	snd_hdac_stream_is_locked(azx_stream(dev))

/* assign a stream for the PCM */
static inline struct azx_dev *
azx_assign_device(struct azx *chip, struct snd_pcm_substream *substream)
{
	struct hdac_stream *s;

	s = snd_hdac_stream_assign(azx_bus(chip), substream);
	if (!s)
		return NULL;
	return stream_to_azx_dev(s);
}

/* release the assigned stream */
static inline void azx_release_device(struct azx_dev *azx_dev)
{
	snd_hdac_stream_release(azx_stream(azx_dev));
}

static inline struct hda_pcm_stream *
to_hda_pcm_stream(struct snd_pcm_substream *substream)
{
	struct azx_pcm *apcm = snd_pcm_substream_chip(substream);
	return &apcm->info->stream[substream->stream];
}

static u64 azx_adjust_codec_delay(struct snd_pcm_substream *substream,
				u64 nsec)
{
	struct azx_pcm *apcm = snd_pcm_substream_chip(substream);
	struct hda_pcm_stream *hinfo = to_hda_pcm_stream(substream);
	u64 codec_frames, codec_nsecs;

	if (!hinfo->ops.get_delay)
		return nsec;

	codec_frames = hinfo->ops.get_delay(hinfo, apcm->codec, substream);
	codec_nsecs = div_u64(codec_frames * 1000000000LL,
			      substream->runtime->rate);

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		return nsec + codec_nsecs;

	return (nsec > codec_nsecs) ? nsec - codec_nsecs : 0;
}

/*
 * PCM ops
 */

static int azx_pcm_close(struct snd_pcm_substream *substream)
{
	struct azx_pcm *apcm = snd_pcm_substream_chip(substream);
	struct hda_pcm_stream *hinfo = to_hda_pcm_stream(substream);
	struct azx *chip = apcm->chip;
	struct azx_dev *azx_dev = get_azx_dev(substream);

	trace_azx_pcm_close(chip, azx_dev);
	mutex_lock(&chip->open_mutex);
	azx_release_device(azx_dev);
	if (hinfo->ops.close)
		hinfo->ops.close(hinfo, apcm->codec, substream);
	snd_hda_power_down(apcm->codec);
	mutex_unlock(&chip->open_mutex);
	snd_hda_codec_pcm_put(apcm->info);
	return 0;
}

static int azx_pcm_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *hw_params)
{
	struct azx_pcm *apcm = snd_pcm_substream_chip(substream);
	struct azx *chip = apcm->chip;
	struct azx_dev *azx_dev = get_azx_dev(substream);
	int ret = 0;

	trace_azx_pcm_hw_params(chip, azx_dev);
	dsp_lock(azx_dev);
	if (dsp_is_locked(azx_dev)) {
		ret = -EBUSY;
		goto unlock;
	}

	azx_dev->core.bufsize = 0;
	azx_dev->core.period_bytes = 0;
	azx_dev->core.format_val = 0;

unlock:
	dsp_unlock(azx_dev);
	return ret;
}

static int azx_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct azx_pcm *apcm = snd_pcm_substream_chip(substream);
	struct azx_dev *azx_dev = get_azx_dev(substream);
	struct hda_pcm_stream *hinfo = to_hda_pcm_stream(substream);

	/* reset BDL address */
	dsp_lock(azx_dev);
	if (!dsp_is_locked(azx_dev))
		snd_hdac_stream_cleanup(azx_stream(azx_dev));

	snd_hda_codec_cleanup(apcm->codec, hinfo, substream);

	azx_stream(azx_dev)->prepared = 0;
	dsp_unlock(azx_dev);
	return 0;
}

static int azx_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct azx_pcm *apcm = snd_pcm_substream_chip(substream);
	struct azx *chip = apcm->chip;
	struct azx_dev *azx_dev = get_azx_dev(substream);
	struct hda_pcm_stream *hinfo = to_hda_pcm_stream(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned int format_val, stream_tag;
	int err;
	struct hda_spdif_out *spdif =
		snd_hda_spdif_out_of_nid(apcm->codec, hinfo->nid);
	unsigned short ctls = spdif ? spdif->ctls : 0;

	trace_azx_pcm_prepare(chip, azx_dev);
	dsp_lock(azx_dev);
	if (dsp_is_locked(azx_dev)) {
		err = -EBUSY;
		goto unlock;
	}

	snd_hdac_stream_reset(azx_stream(azx_dev));
	format_val = snd_hdac_calc_stream_format(runtime->rate,
						runtime->channels,
						runtime->format,
						hinfo->maxbps,
						ctls);
	if (!format_val) {
		dev_err(chip->card->dev,
			"invalid format_val, rate=%d, ch=%d, format=%d\n",
			runtime->rate, runtime->channels, runtime->format);
		err = -EINVAL;
		goto unlock;
	}

	err = snd_hdac_stream_set_params(azx_stream(azx_dev), format_val);
	if (err < 0)
		goto unlock;

	snd_hdac_stream_setup(azx_stream(azx_dev));

	stream_tag = azx_dev->core.stream_tag;
	/* CA-IBG chips need the playback stream starting from 1 */
	if ((chip->driver_caps & AZX_DCAPS_CTX_WORKAROUND) &&
	    stream_tag > chip->capture_streams)
		stream_tag -= chip->capture_streams;
	err = snd_hda_codec_prepare(apcm->codec, hinfo, stream_tag,
				     azx_dev->core.format_val, substream);

 unlock:
	if (!err)
		azx_stream(azx_dev)->prepared = 1;
	dsp_unlock(azx_dev);
	return err;
}

static int azx_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct azx_pcm *apcm = snd_pcm_substream_chip(substream);
	struct azx *chip = apcm->chip;
	struct hdac_bus *bus = azx_bus(chip);
	struct azx_dev *azx_dev;
	struct snd_pcm_substream *s;
	struct hdac_stream *hstr;
	bool start;
	int sbits = 0;
	int sync_reg;

	azx_dev = get_azx_dev(substream);
	trace_azx_pcm_trigger(chip, azx_dev, cmd);

	hstr = azx_stream(azx_dev);
	if (chip->driver_caps & AZX_DCAPS_OLD_SSYNC)
		sync_reg = AZX_REG_OLD_SSYNC;
	else
		sync_reg = AZX_REG_SSYNC;

	if (dsp_is_locked(azx_dev) || !hstr->prepared)
		return -EPIPE;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
		start = true;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
		start = false;
		break;
	default:
		return -EINVAL;
	}

	snd_pcm_group_for_each_entry(s, substream) {
		if (s->pcm->card != substream->pcm->card)
			continue;
		azx_dev = get_azx_dev(s);
		sbits |= 1 << azx_dev->core.index;
		snd_pcm_trigger_done(s, substream);
	}

	spin_lock(&bus->reg_lock);

	/* first, set SYNC bits of corresponding streams */
	snd_hdac_stream_sync_trigger(hstr, true, sbits, sync_reg);

	snd_pcm_group_for_each_entry(s, substream) {
		if (s->pcm->card != substream->pcm->card)
			continue;
		azx_dev = get_azx_dev(s);
		if (start) {
			azx_dev->insufficient = 1;
			snd_hdac_stream_start(azx_stream(azx_dev), true);
		} else {
			snd_hdac_stream_stop(azx_stream(azx_dev));
		}
	}
	spin_unlock(&bus->reg_lock);

	snd_hdac_stream_sync(hstr, start, sbits);

	spin_lock(&bus->reg_lock);
	/* reset SYNC bits */
	snd_hdac_stream_sync_trigger(hstr, false, sbits, sync_reg);
	if (start)
		snd_hdac_stream_timecounter_init(hstr, sbits);
	spin_unlock(&bus->reg_lock);
	return 0;
}

unsigned int azx_get_pos_lpib(struct azx *chip, struct azx_dev *azx_dev)
{
	return snd_hdac_stream_get_pos_lpib(azx_stream(azx_dev));
}
EXPORT_SYMBOL_GPL(azx_get_pos_lpib);

unsigned int azx_get_pos_posbuf(struct azx *chip, struct azx_dev *azx_dev)
{
	return snd_hdac_stream_get_pos_posbuf(azx_stream(azx_dev));
}
EXPORT_SYMBOL_GPL(azx_get_pos_posbuf);

unsigned int azx_get_position(struct azx *chip,
			      struct azx_dev *azx_dev)
{
	struct snd_pcm_substream *substream = azx_dev->core.substream;
	unsigned int pos;
	int stream = substream->stream;
	int delay = 0;

	if (chip->get_position[stream])
		pos = chip->get_position[stream](chip, azx_dev);
	else /* use the position buffer as default */
		pos = azx_get_pos_posbuf(chip, azx_dev);

	if (pos >= azx_dev->core.bufsize)
		pos = 0;

	if (substream->runtime) {
		struct azx_pcm *apcm = snd_pcm_substream_chip(substream);
		struct hda_pcm_stream *hinfo = to_hda_pcm_stream(substream);

		if (chip->get_delay[stream])
			delay += chip->get_delay[stream](chip, azx_dev, pos);
		if (hinfo->ops.get_delay)
			delay += hinfo->ops.get_delay(hinfo, apcm->codec,
						      substream);
		substream->runtime->delay = delay;
	}

	trace_azx_get_position(chip, azx_dev, pos, delay);
	return pos;
}
EXPORT_SYMBOL_GPL(azx_get_position);

static snd_pcm_uframes_t azx_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct azx_pcm *apcm = snd_pcm_substream_chip(substream);
	struct azx *chip = apcm->chip;
	struct azx_dev *azx_dev = get_azx_dev(substream);
	return bytes_to_frames(substream->runtime,
			       azx_get_position(chip, azx_dev));
}

/*
 * azx_scale64: Scale base by mult/div while not overflowing sanely
 *
 * Derived from scale64_check_overflow in kernel/time/timekeeping.c
 *
 * The tmestamps for a 48Khz stream can overflow after (2^64/10^9)/48K which
 * is about 384307 ie ~4.5 days.
 *
 * This scales the calculation so that overflow will happen but after 2^64 /
 * 48000 secs, which is pretty large!
 *
 * In caln below:
 *	base may overflow, but since there isn’t any additional division
 *	performed on base it’s OK
 *	rem can’t overflow because both are 32-bit values
 */

#ifdef CONFIG_X86
static u64 azx_scale64(u64 base, u32 num, u32 den)
{
	u64 rem;

	rem = do_div(base, den);

	base *= num;
	rem *= num;

	do_div(rem, den);

	return base + rem;
}

static int azx_get_sync_time(ktime_t *device,
		struct system_counterval_t *system, void *ctx)
{
	struct snd_pcm_substream *substream = ctx;
	struct azx_dev *azx_dev = get_azx_dev(substream);
	struct azx_pcm *apcm = snd_pcm_substream_chip(substream);
	struct azx *chip = apcm->chip;
	struct snd_pcm_runtime *runtime;
	u64 ll_counter, ll_counter_l, ll_counter_h;
	u64 tsc_counter, tsc_counter_l, tsc_counter_h;
	u32 wallclk_ctr, wallclk_cycles;
	bool direction;
	u32 dma_select;
	u32 timeout;
	u32 retry_count = 0;

	runtime = substream->runtime;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		direction = 1;
	else
		direction = 0;

	/* 0th stream tag is not used, so DMA ch 0 is for 1st stream tag */
	do {
		timeout = 100;
		dma_select = (direction << GTSCC_CDMAS_DMA_DIR_SHIFT) |
					(azx_dev->core.stream_tag - 1);
		snd_hdac_chip_writel(azx_bus(chip), GTSCC, dma_select);

		/* Enable the capture */
		snd_hdac_chip_updatel(azx_bus(chip), GTSCC, 0, GTSCC_TSCCI_MASK);

		while (timeout) {
			if (snd_hdac_chip_readl(azx_bus(chip), GTSCC) &
						GTSCC_TSCCD_MASK)
				break;

			timeout--;
		}

		if (!timeout) {
			dev_err(chip->card->dev, "GTSCC capture Timedout!\n");
			return -EIO;
		}

		/* Read wall clock counter */
		wallclk_ctr = snd_hdac_chip_readl(azx_bus(chip), WALFCC);

		/* Read TSC counter */
		tsc_counter_l = snd_hdac_chip_readl(azx_bus(chip), TSCCL);
		tsc_counter_h = snd_hdac_chip_readl(azx_bus(chip), TSCCU);

		/* Read Link counter */
		ll_counter_l = snd_hdac_chip_readl(azx_bus(chip), LLPCL);
		ll_counter_h = snd_hdac_chip_readl(azx_bus(chip), LLPCU);

		/* Ack: registers read done */
		snd_hdac_chip_writel(azx_bus(chip), GTSCC, GTSCC_TSCCD_SHIFT);

		tsc_counter = (tsc_counter_h << TSCCU_CCU_SHIFT) |
						tsc_counter_l;

		ll_counter = (ll_counter_h << LLPC_CCU_SHIFT) |	ll_counter_l;
		wallclk_cycles = wallclk_ctr & WALFCC_CIF_MASK;

		/*
		 * An error occurs near frame "rollover". The clocks in
		 * frame value indicates whether this error may have
		 * occurred. Here we use the value of 10 i.e.,
		 * HDA_MAX_CYCLE_OFFSET
		 */
		if (wallclk_cycles < HDA_MAX_CYCLE_VALUE - HDA_MAX_CYCLE_OFFSET
					&& wallclk_cycles > HDA_MAX_CYCLE_OFFSET)
			break;

		/*
		 * Sleep before we read again, else we may again get
		 * value near to MAX_CYCLE. Try to sleep for different
		 * amount of time so we dont hit the same number again
		 */
		udelay(retry_count++);

	} while (retry_count != HDA_MAX_CYCLE_READ_RETRY);

	if (retry_count == HDA_MAX_CYCLE_READ_RETRY) {
		dev_err_ratelimited(chip->card->dev,
			"Error in WALFCC cycle count\n");
		return -EIO;
	}

	*device = ns_to_ktime(azx_scale64(ll_counter,
				NSEC_PER_SEC, runtime->rate));
	*device = ktime_add_ns(*device, (wallclk_cycles * NSEC_PER_SEC) /
			       ((HDA_MAX_CYCLE_VALUE + 1) * runtime->rate));

	*system = convert_art_to_tsc(tsc_counter);

	return 0;
}

#else
static int azx_get_sync_time(ktime_t *device,
		struct system_counterval_t *system, void *ctx)
{
	return -ENXIO;
}
#endif

static int azx_get_crosststamp(struct snd_pcm_substream *substream,
			      struct system_device_crosststamp *xtstamp)
{
	return get_device_system_crosststamp(azx_get_sync_time,
					substream, NULL, xtstamp);
}

static inline bool is_link_time_supported(struct snd_pcm_runtime *runtime,
				struct snd_pcm_audio_tstamp_config *ts)
{
	if (runtime->hw.info & SNDRV_PCM_INFO_HAS_LINK_SYNCHRONIZED_ATIME)
		if (ts->type_requested == SNDRV_PCM_AUDIO_TSTAMP_TYPE_LINK_SYNCHRONIZED)
			return true;

	return false;
}

static int azx_get_time_info(struct snd_pcm_substream *substream,
			struct timespec64 *system_ts, struct timespec64 *audio_ts,
			struct snd_pcm_audio_tstamp_config *audio_tstamp_config,
			struct snd_pcm_audio_tstamp_report *audio_tstamp_report)
{
	struct azx_dev *azx_dev = get_azx_dev(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct system_device_crosststamp xtstamp;
	int ret;
	u64 nsec;

	if ((substream->runtime->hw.info & SNDRV_PCM_INFO_HAS_LINK_ATIME) &&
		(audio_tstamp_config->type_requested == SNDRV_PCM_AUDIO_TSTAMP_TYPE_LINK)) {

		snd_pcm_gettime(substream->runtime, system_ts);

		nsec = timecounter_read(&azx_dev->core.tc);
		if (audio_tstamp_config->report_delay)
			nsec = azx_adjust_codec_delay(substream, nsec);

		*audio_ts = ns_to_timespec64(nsec);

		audio_tstamp_report->actual_type = SNDRV_PCM_AUDIO_TSTAMP_TYPE_LINK;
		audio_tstamp_report->accuracy_report = 1; /* rest of structure is valid */
		audio_tstamp_report->accuracy = 42; /* 24 MHz WallClock == 42ns resolution */

	} else if (is_link_time_supported(runtime, audio_tstamp_config)) {

		ret = azx_get_crosststamp(substream, &xtstamp);
		if (ret)
			return ret;

		switch (runtime->tstamp_type) {
		case SNDRV_PCM_TSTAMP_TYPE_MONOTONIC:
			return -EINVAL;

		case SNDRV_PCM_TSTAMP_TYPE_MONOTONIC_RAW:
			*system_ts = ktime_to_timespec64(xtstamp.sys_monoraw);
			break;

		default:
			*system_ts = ktime_to_timespec64(xtstamp.sys_realtime);
			break;

		}

		*audio_ts = ktime_to_timespec64(xtstamp.device);

		audio_tstamp_report->actual_type =
			SNDRV_PCM_AUDIO_TSTAMP_TYPE_LINK_SYNCHRONIZED;
		audio_tstamp_report->accuracy_report = 1;
		/* 24 MHz WallClock == 42ns resolution */
		audio_tstamp_report->accuracy = 42;

	} else {
		audio_tstamp_report->actual_type = SNDRV_PCM_AUDIO_TSTAMP_TYPE_DEFAULT;
	}

	return 0;
}

static const struct snd_pcm_hardware azx_pcm_hw = {
	.info =			(SNDRV_PCM_INFO_MMAP |
				 SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 /* No full-resume yet implemented */
				 /* SNDRV_PCM_INFO_RESUME |*/
				 SNDRV_PCM_INFO_PAUSE |
				 SNDRV_PCM_INFO_SYNC_START |
				 SNDRV_PCM_INFO_HAS_WALL_CLOCK | /* legacy */
				 SNDRV_PCM_INFO_HAS_LINK_ATIME |
				 SNDRV_PCM_INFO_NO_PERIOD_WAKEUP),
	.formats =		SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_48000,
	.rate_min =		48000,
	.rate_max =		48000,
	.channels_min =		2,
	.channels_max =		2,
	.buffer_bytes_max =	AZX_MAX_BUF_SIZE,
	.period_bytes_min =	128,
	.period_bytes_max =	AZX_MAX_BUF_SIZE / 2,
	.periods_min =		2,
	.periods_max =		AZX_MAX_FRAG,
	.fifo_size =		0,
};

static int azx_pcm_open(struct snd_pcm_substream *substream)
{
	struct azx_pcm *apcm = snd_pcm_substream_chip(substream);
	struct hda_pcm_stream *hinfo = to_hda_pcm_stream(substream);
	struct azx *chip = apcm->chip;
	struct azx_dev *azx_dev;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;
	int buff_step;

	snd_hda_codec_pcm_get(apcm->info);
	mutex_lock(&chip->open_mutex);
	azx_dev = azx_assign_device(chip, substream);
	trace_azx_pcm_open(chip, azx_dev);
	if (azx_dev == NULL) {
		err = -EBUSY;
		goto unlock;
	}
	runtime->private_data = azx_dev;

	runtime->hw = azx_pcm_hw;
	if (chip->gts_present)
		runtime->hw.info |= SNDRV_PCM_INFO_HAS_LINK_SYNCHRONIZED_ATIME;
	runtime->hw.channels_min = hinfo->channels_min;
	runtime->hw.channels_max = hinfo->channels_max;
	runtime->hw.formats = hinfo->formats;
	runtime->hw.rates = hinfo->rates;
	snd_pcm_limit_hw_rates(runtime);
	snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);

	/* avoid wrap-around with wall-clock */
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_TIME,
				     20,
				     178000000);

	if (chip->align_buffer_size)
		/* constrain buffer sizes to be multiple of 128
		   bytes. This is more efficient in terms of memory
		   access but isn't required by the HDA spec and
		   prevents users from specifying exact period/buffer
		   sizes. For example for 44.1kHz, a period size set
		   to 20ms will be rounded to 19.59ms. */
		buff_step = 128;
	else
		/* Don't enforce steps on buffer sizes, still need to
		   be multiple of 4 bytes (HDA spec). Tested on Intel
		   HDA controllers, may not work on all devices where
		   option needs to be disabled */
		buff_step = 4;

	snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
				   buff_step);
	snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES,
				   buff_step);
	snd_hda_power_up(apcm->codec);
	if (hinfo->ops.open)
		err = hinfo->ops.open(hinfo, apcm->codec, substream);
	else
		err = -ENODEV;
	if (err < 0) {
		azx_release_device(azx_dev);
		goto powerdown;
	}
	snd_pcm_limit_hw_rates(runtime);
	/* sanity check */
	if (snd_BUG_ON(!runtime->hw.channels_min) ||
	    snd_BUG_ON(!runtime->hw.channels_max) ||
	    snd_BUG_ON(!runtime->hw.formats) ||
	    snd_BUG_ON(!runtime->hw.rates)) {
		azx_release_device(azx_dev);
		if (hinfo->ops.close)
			hinfo->ops.close(hinfo, apcm->codec, substream);
		err = -EINVAL;
		goto powerdown;
	}

	/* disable LINK_ATIME timestamps for capture streams
	   until we figure out how to handle digital inputs */
	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		runtime->hw.info &= ~SNDRV_PCM_INFO_HAS_WALL_CLOCK; /* legacy */
		runtime->hw.info &= ~SNDRV_PCM_INFO_HAS_LINK_ATIME;
	}

	snd_pcm_set_sync(substream);
	mutex_unlock(&chip->open_mutex);
	return 0;

 powerdown:
	snd_hda_power_down(apcm->codec);
 unlock:
	mutex_unlock(&chip->open_mutex);
	snd_hda_codec_pcm_put(apcm->info);
	return err;
}

static const struct snd_pcm_ops azx_pcm_ops = {
	.open = azx_pcm_open,
	.close = azx_pcm_close,
	.hw_params = azx_pcm_hw_params,
	.hw_free = azx_pcm_hw_free,
	.prepare = azx_pcm_prepare,
	.trigger = azx_pcm_trigger,
	.pointer = azx_pcm_pointer,
	.get_time_info =  azx_get_time_info,
};

static void azx_pcm_free(struct snd_pcm *pcm)
{
	struct azx_pcm *apcm = pcm->private_data;
	if (apcm) {
		list_del(&apcm->list);
		apcm->info->pcm = NULL;
		kfree(apcm);
	}
}

#define MAX_PREALLOC_SIZE	(32 * 1024 * 1024)

int snd_hda_attach_pcm_stream(struct hda_bus *_bus, struct hda_codec *codec,
			      struct hda_pcm *cpcm)
{
	struct hdac_bus *bus = &_bus->core;
	struct azx *chip = bus_to_azx(bus);
	struct snd_pcm *pcm;
	struct azx_pcm *apcm;
	int pcm_dev = cpcm->device;
	unsigned int size;
	int s, err;
	int type = SNDRV_DMA_TYPE_DEV_SG;

	list_for_each_entry(apcm, &chip->pcm_list, list) {
		if (apcm->pcm->device == pcm_dev) {
			dev_err(chip->card->dev, "PCM %d already exists\n",
				pcm_dev);
			return -EBUSY;
		}
	}
	err = snd_pcm_new(chip->card, cpcm->name, pcm_dev,
			  cpcm->stream[SNDRV_PCM_STREAM_PLAYBACK].substreams,
			  cpcm->stream[SNDRV_PCM_STREAM_CAPTURE].substreams,
			  &pcm);
	if (err < 0)
		return err;
	strscpy(pcm->name, cpcm->name, sizeof(pcm->name));
	apcm = kzalloc(sizeof(*apcm), GFP_KERNEL);
	if (apcm == NULL) {
		snd_device_free(chip->card, pcm);
		return -ENOMEM;
	}
	apcm->chip = chip;
	apcm->pcm = pcm;
	apcm->codec = codec;
	apcm->info = cpcm;
	pcm->private_data = apcm;
	pcm->private_free = azx_pcm_free;
	if (cpcm->pcm_type == HDA_PCM_TYPE_MODEM)
		pcm->dev_class = SNDRV_PCM_CLASS_MODEM;
	list_add_tail(&apcm->list, &chip->pcm_list);
	cpcm->pcm = pcm;
	for (s = 0; s < 2; s++) {
		if (cpcm->stream[s].substreams)
			snd_pcm_set_ops(pcm, s, &azx_pcm_ops);
	}
	/* buffer pre-allocation */
	size = CONFIG_SND_HDA_PREALLOC_SIZE * 1024;
	if (size > MAX_PREALLOC_SIZE)
		size = MAX_PREALLOC_SIZE;
	if (chip->uc_buffer)
		type = SNDRV_DMA_TYPE_DEV_WC_SG;
	snd_pcm_set_managed_buffer_all(pcm, type, chip->card->dev,
				       size, MAX_PREALLOC_SIZE);
	return 0;
}

static unsigned int azx_command_addr(u32 cmd)
{
	unsigned int addr = cmd >> 28;

	if (addr >= AZX_MAX_CODECS) {
		snd_BUG();
		addr = 0;
	}

	return addr;
}

/* receive a response */
static int azx_rirb_get_response(struct hdac_bus *bus, unsigned int addr,
				 unsigned int *res)
{
	struct azx *chip = bus_to_azx(bus);
	struct hda_bus *hbus = &chip->bus;
	int err;

 again:
	err = snd_hdac_bus_get_response(bus, addr, res);
	if (!err)
		return 0;

	if (hbus->no_response_fallback)
		return -EIO;

	if (!bus->polling_mode) {
		dev_warn(chip->card->dev,
			 "azx_get_response timeout, switching to polling mode: last cmd=0x%08x\n",
			 bus->last_cmd[addr]);
		bus->polling_mode = 1;
		goto again;
	}

	if (chip->msi) {
		dev_warn(chip->card->dev,
			 "No response from codec, disabling MSI: last cmd=0x%08x\n",
			 bus->last_cmd[addr]);
		if (chip->ops->disable_msi_reset_irq &&
		    chip->ops->disable_msi_reset_irq(chip) < 0)
			return -EIO;
		goto again;
	}

	if (chip->probing) {
		/* If this critical timeout happens during the codec probing
		 * phase, this is likely an access to a non-existing codec
		 * slot.  Better to return an error and reset the system.
		 */
		return -EIO;
	}

	/* no fallback mechanism? */
	if (!chip->fallback_to_single_cmd)
		return -EIO;

	/* a fatal communication error; need either to reset or to fallback
	 * to the single_cmd mode
	 */
	if (hbus->allow_bus_reset && !hbus->response_reset && !hbus->in_reset) {
		hbus->response_reset = 1;
		dev_err(chip->card->dev,
			"No response from codec, resetting bus: last cmd=0x%08x\n",
			bus->last_cmd[addr]);
		return -EAGAIN; /* give a chance to retry */
	}

	dev_err(chip->card->dev,
		"azx_get_response timeout, switching to single_cmd mode: last cmd=0x%08x\n",
		bus->last_cmd[addr]);
	chip->single_cmd = 1;
	hbus->response_reset = 0;
	snd_hdac_bus_stop_cmd_io(bus);
	return -EIO;
}

/*
 * Use the single immediate command instead of CORB/RIRB for simplicity
 *
 * Note: according to Intel, this is not preferred use.  The command was
 *       intended for the BIOS only, and may get confused with unsolicited
 *       responses.  So, we shouldn't use it for normal operation from the
 *       driver.
 *       I left the codes, however, for debugging/testing purposes.
 */

/* receive a response */
static int azx_single_wait_for_response(struct azx *chip, unsigned int addr)
{
	int timeout = 50;

	while (timeout--) {
		/* check IRV busy bit */
		if (azx_readw(chip, IRS) & AZX_IRS_VALID) {
			/* reuse rirb.res as the response return value */
			azx_bus(chip)->rirb.res[addr] = azx_readl(chip, IR);
			return 0;
		}
		udelay(1);
	}
	if (printk_ratelimit())
		dev_dbg(chip->card->dev, "get_response timeout: IRS=0x%x\n",
			azx_readw(chip, IRS));
	azx_bus(chip)->rirb.res[addr] = -1;
	return -EIO;
}

/* send a command */
static int azx_single_send_cmd(struct hdac_bus *bus, u32 val)
{
	struct azx *chip = bus_to_azx(bus);
	unsigned int addr = azx_command_addr(val);
	int timeout = 50;

	bus->last_cmd[azx_command_addr(val)] = val;
	while (timeout--) {
		/* check ICB busy bit */
		if (!((azx_readw(chip, IRS) & AZX_IRS_BUSY))) {
			/* Clear IRV valid bit */
			azx_writew(chip, IRS, azx_readw(chip, IRS) |
				   AZX_IRS_VALID);
			azx_writel(chip, IC, val);
			azx_writew(chip, IRS, azx_readw(chip, IRS) |
				   AZX_IRS_BUSY);
			return azx_single_wait_for_response(chip, addr);
		}
		udelay(1);
	}
	if (printk_ratelimit())
		dev_dbg(chip->card->dev,
			"send_cmd timeout: IRS=0x%x, val=0x%x\n",
			azx_readw(chip, IRS), val);
	return -EIO;
}

/* receive a response */
static int azx_single_get_response(struct hdac_bus *bus, unsigned int addr,
				   unsigned int *res)
{
	if (res)
		*res = bus->rirb.res[addr];
	return 0;
}

/*
 * The below are the main callbacks from hda_codec.
 *
 * They are just the skeleton to call sub-callbacks according to the
 * current setting of chip->single_cmd.
 */

/* send a command */
static int azx_send_cmd(struct hdac_bus *bus, unsigned int val)
{
	struct azx *chip = bus_to_azx(bus);

	if (chip->disabled)
		return 0;
	if (chip->single_cmd)
		return azx_single_send_cmd(bus, val);
	else
		return snd_hdac_bus_send_cmd(bus, val);
}

/* get a response */
static int azx_get_response(struct hdac_bus *bus, unsigned int addr,
			    unsigned int *res)
{
	struct azx *chip = bus_to_azx(bus);

	if (chip->disabled)
		return 0;
	if (chip->single_cmd)
		return azx_single_get_response(bus, addr, res);
	else
		return azx_rirb_get_response(bus, addr, res);
}

static const struct hdac_bus_ops bus_core_ops = {
	.command = azx_send_cmd,
	.get_response = azx_get_response,
};

#ifdef CONFIG_SND_HDA_DSP_LOADER
/*
 * DSP loading code (e.g. for CA0132)
 */

/* use the first stream for loading DSP */
static struct azx_dev *
azx_get_dsp_loader_dev(struct azx *chip)
{
	struct hdac_bus *bus = azx_bus(chip);
	struct hdac_stream *s;

	list_for_each_entry(s, &bus->stream_list, list)
		if (s->index == chip->playback_index_offset)
			return stream_to_azx_dev(s);

	return NULL;
}

int snd_hda_codec_load_dsp_prepare(struct hda_codec *codec, unsigned int format,
				   unsigned int byte_size,
				   struct snd_dma_buffer *bufp)
{
	struct hdac_bus *bus = &codec->bus->core;
	struct azx *chip = bus_to_azx(bus);
	struct azx_dev *azx_dev;
	struct hdac_stream *hstr;
	bool saved = false;
	int err;

	azx_dev = azx_get_dsp_loader_dev(chip);
	hstr = azx_stream(azx_dev);
	spin_lock_irq(&bus->reg_lock);
	if (hstr->opened) {
		chip->saved_azx_dev = *azx_dev;
		saved = true;
	}
	spin_unlock_irq(&bus->reg_lock);

	err = snd_hdac_dsp_prepare(hstr, format, byte_size, bufp);
	if (err < 0) {
		spin_lock_irq(&bus->reg_lock);
		if (saved)
			*azx_dev = chip->saved_azx_dev;
		spin_unlock_irq(&bus->reg_lock);
		return err;
	}

	hstr->prepared = 0;
	return err;
}
EXPORT_SYMBOL_GPL(snd_hda_codec_load_dsp_prepare);

void snd_hda_codec_load_dsp_trigger(struct hda_codec *codec, bool start)
{
	struct hdac_bus *bus = &codec->bus->core;
	struct azx *chip = bus_to_azx(bus);
	struct azx_dev *azx_dev = azx_get_dsp_loader_dev(chip);

	snd_hdac_dsp_trigger(azx_stream(azx_dev), start);
}
EXPORT_SYMBOL_GPL(snd_hda_codec_load_dsp_trigger);

void snd_hda_codec_load_dsp_cleanup(struct hda_codec *codec,
				    struct snd_dma_buffer *dmab)
{
	struct hdac_bus *bus = &codec->bus->core;
	struct azx *chip = bus_to_azx(bus);
	struct azx_dev *azx_dev = azx_get_dsp_loader_dev(chip);
	struct hdac_stream *hstr = azx_stream(azx_dev);

	if (!dmab->area || !hstr->locked)
		return;

	snd_hdac_dsp_cleanup(hstr, dmab);
	spin_lock_irq(&bus->reg_lock);
	if (hstr->opened)
		*azx_dev = chip->saved_azx_dev;
	hstr->locked = false;
	spin_unlock_irq(&bus->reg_lock);
}
EXPORT_SYMBOL_GPL(snd_hda_codec_load_dsp_cleanup);
#endif /* CONFIG_SND_HDA_DSP_LOADER */

/*
 * reset and start the controller registers
 */
void azx_init_chip(struct azx *chip, bool full_reset)
{
	if (snd_hdac_bus_init_chip(azx_bus(chip), full_reset)) {
		/* correct RINTCNT for CXT */
		if (chip->driver_caps & AZX_DCAPS_CTX_WORKAROUND)
			azx_writew(chip, RINTCNT, 0xc0);
	}
}
EXPORT_SYMBOL_GPL(azx_init_chip);

void azx_stop_all_streams(struct azx *chip)
{
	struct hdac_bus *bus = azx_bus(chip);

	snd_hdac_stop_streams(bus);
}
EXPORT_SYMBOL_GPL(azx_stop_all_streams);

void azx_stop_chip(struct azx *chip)
{
	snd_hdac_bus_stop_chip(azx_bus(chip));
}
EXPORT_SYMBOL_GPL(azx_stop_chip);

/*
 * interrupt handler
 */
static void stream_update(struct hdac_bus *bus, struct hdac_stream *s)
{
	struct azx *chip = bus_to_azx(bus);
	struct azx_dev *azx_dev = stream_to_azx_dev(s);

	/* check whether this IRQ is really acceptable */
	if (!chip->ops->position_check ||
	    chip->ops->position_check(chip, azx_dev)) {
		spin_unlock(&bus->reg_lock);
		snd_pcm_period_elapsed(azx_stream(azx_dev)->substream);
		spin_lock(&bus->reg_lock);
	}
}

irqreturn_t azx_interrupt(int irq, void *dev_id)
{
	struct azx *chip = dev_id;
	struct hdac_bus *bus = azx_bus(chip);
	u32 status;
	bool active, handled = false;
	int repeat = 0; /* count for avoiding endless loop */

#ifdef CONFIG_PM
	if (azx_has_pm_runtime(chip))
		if (!pm_runtime_active(chip->card->dev))
			return IRQ_NONE;
#endif

	spin_lock(&bus->reg_lock);

	if (chip->disabled)
		goto unlock;

	do {
		status = azx_readl(chip, INTSTS);
		if (status == 0 || status == 0xffffffff)
			break;

		handled = true;
		active = false;
		if (snd_hdac_bus_handle_stream_irq(bus, status, stream_update))
			active = true;

		status = azx_readb(chip, RIRBSTS);
		if (status & RIRB_INT_MASK) {
			/*
			 * Clearing the interrupt status here ensures that no
			 * interrupt gets masked after the RIRB wp is read in
			 * snd_hdac_bus_update_rirb. This avoids a possible
			 * race condition where codec response in RIRB may
			 * remain unserviced by IRQ, eventually falling back
			 * to polling mode in azx_rirb_get_response.
			 */
			azx_writeb(chip, RIRBSTS, RIRB_INT_MASK);
			active = true;
			if (status & RIRB_INT_RESPONSE) {
				if (chip->driver_caps & AZX_DCAPS_CTX_WORKAROUND)
					udelay(80);
				snd_hdac_bus_update_rirb(bus);
			}
		}
	} while (active && ++repeat < 10);

 unlock:
	spin_unlock(&bus->reg_lock);

	return IRQ_RETVAL(handled);
}
EXPORT_SYMBOL_GPL(azx_interrupt);

/*
 * Codec initerface
 */

/*
 * Probe the given codec address
 */
static int probe_codec(struct azx *chip, int addr)
{
	unsigned int cmd = (addr << 28) | (AC_NODE_ROOT << 20) |
		(AC_VERB_PARAMETERS << 8) | AC_PAR_VENDOR_ID;
	struct hdac_bus *bus = azx_bus(chip);
	int err;
	unsigned int res = -1;

	mutex_lock(&bus->cmd_mutex);
	chip->probing = 1;
	azx_send_cmd(bus, cmd);
	err = azx_get_response(bus, addr, &res);
	chip->probing = 0;
	mutex_unlock(&bus->cmd_mutex);
	if (err < 0 || res == -1)
		return -EIO;
	dev_dbg(chip->card->dev, "codec #%d probed OK\n", addr);
	return 0;
}

void snd_hda_bus_reset(struct hda_bus *bus)
{
	struct azx *chip = bus_to_azx(&bus->core);

	bus->in_reset = 1;
	azx_stop_chip(chip);
	azx_init_chip(chip, true);
	if (bus->core.chip_init)
		snd_hda_bus_reset_codecs(bus);
	bus->in_reset = 0;
}

/* HD-audio bus initialization */
int azx_bus_init(struct azx *chip, const char *model)
{
	struct hda_bus *bus = &chip->bus;
	int err;

	err = snd_hdac_bus_init(&bus->core, chip->card->dev, &bus_core_ops);
	if (err < 0)
		return err;

	bus->card = chip->card;
	mutex_init(&bus->prepare_mutex);
	bus->pci = chip->pci;
	bus->modelname = model;
	bus->mixer_assigned = -1;
	bus->core.snoop = azx_snoop(chip);
	if (chip->get_position[0] != azx_get_pos_lpib ||
	    chip->get_position[1] != azx_get_pos_lpib)
		bus->core.use_posbuf = true;
	bus->core.bdl_pos_adj = chip->bdl_pos_adj;
	if (chip->driver_caps & AZX_DCAPS_CORBRP_SELF_CLEAR)
		bus->core.corbrp_self_clear = true;

	if (chip->driver_caps & AZX_DCAPS_4K_BDLE_BOUNDARY)
		bus->core.align_bdle_4k = true;

	/* enable sync_write flag for stable communication as default */
	bus->core.sync_write = 1;

	return 0;
}
EXPORT_SYMBOL_GPL(azx_bus_init);

/* Probe codecs */
int azx_probe_codecs(struct azx *chip, unsigned int max_slots)
{
	struct hdac_bus *bus = azx_bus(chip);
	int c, codecs, err;

	codecs = 0;
	if (!max_slots)
		max_slots = AZX_DEFAULT_CODECS;

	/* First try to probe all given codec slots */
	for (c = 0; c < max_slots; c++) {
		if ((bus->codec_mask & (1 << c)) & chip->codec_probe_mask) {
			if (probe_codec(chip, c) < 0) {
				/* Some BIOSen give you wrong codec addresses
				 * that don't exist
				 */
				dev_warn(chip->card->dev,
					 "Codec #%d probe error; disabling it...\n", c);
				bus->codec_mask &= ~(1 << c);
				/* More badly, accessing to a non-existing
				 * codec often screws up the controller chip,
				 * and disturbs the further communications.
				 * Thus if an error occurs during probing,
				 * better to reset the controller chip to
				 * get back to the sanity state.
				 */
				azx_stop_chip(chip);
				azx_init_chip(chip, true);
			}
		}
	}

	/* Then create codec instances */
	for (c = 0; c < max_slots; c++) {
		if ((bus->codec_mask & (1 << c)) & chip->codec_probe_mask) {
			struct hda_codec *codec;
			err = snd_hda_codec_new(&chip->bus, chip->card, c, &codec);
			if (err < 0)
				continue;
			codec->jackpoll_interval = chip->jackpoll_interval;
			codec->beep_mode = chip->beep_mode;
			codec->ctl_dev_id = chip->ctl_dev_id;
			codecs++;
		}
	}
	if (!codecs) {
		dev_err(chip->card->dev, "no codecs initialized\n");
		return -ENXIO;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(azx_probe_codecs);

/* configure each codec instance */
int azx_codec_configure(struct azx *chip)
{
	struct hda_codec *codec, *next;
	int success = 0;

	list_for_each_codec(codec, &chip->bus) {
		if (!snd_hda_codec_configure(codec))
			success++;
	}

	if (success) {
		/* unregister failed codecs if any codec has been probed */
		list_for_each_codec_safe(codec, next, &chip->bus) {
			if (!codec->configured) {
				codec_err(codec, "Unable to configure, disabling\n");
				snd_hdac_device_unregister(&codec->core);
			}
		}
	}

	return success ? 0 : -ENODEV;
}
EXPORT_SYMBOL_GPL(azx_codec_configure);

static int stream_direction(struct azx *chip, unsigned char index)
{
	if (index >= chip->capture_index_offset &&
	    index < chip->capture_index_offset + chip->capture_streams)
		return SNDRV_PCM_STREAM_CAPTURE;
	return SNDRV_PCM_STREAM_PLAYBACK;
}

/* initialize SD streams */
int azx_init_streams(struct azx *chip)
{
	int i;
	int stream_tags[2] = { 0, 0 };

	/* initialize each stream (aka device)
	 * assign the starting bdl address to each stream (device)
	 * and initialize
	 */
	for (i = 0; i < chip->num_streams; i++) {
		struct azx_dev *azx_dev = kzalloc(sizeof(*azx_dev), GFP_KERNEL);
		int dir, tag;

		if (!azx_dev)
			return -ENOMEM;

		dir = stream_direction(chip, i);
		/* stream tag must be unique throughout
		 * the stream direction group,
		 * valid values 1...15
		 * use separate stream tag if the flag
		 * AZX_DCAPS_SEPARATE_STREAM_TAG is used
		 */
		if (chip->driver_caps & AZX_DCAPS_SEPARATE_STREAM_TAG)
			tag = ++stream_tags[dir];
		else
			tag = i + 1;
		snd_hdac_stream_init(azx_bus(chip), azx_stream(azx_dev),
				     i, dir, tag);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(azx_init_streams);

void azx_free_streams(struct azx *chip)
{
	struct hdac_bus *bus = azx_bus(chip);
	struct hdac_stream *s;

	while (!list_empty(&bus->stream_list)) {
		s = list_first_entry(&bus->stream_list, struct hdac_stream, list);
		list_del(&s->list);
		kfree(stream_to_azx_dev(s));
	}
}
EXPORT_SYMBOL_GPL(azx_free_streams);
