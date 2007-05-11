/*
 *  ALSA driver for Echoaudio soundcards.
 *  Copyright (C) 2003-2004 Giuliano Pochini <pochini@shiny.it>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

MODULE_AUTHOR("Giuliano Pochini <pochini@shiny.it>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Echoaudio " ECHOCARD_NAME " soundcards driver");
MODULE_SUPPORTED_DEVICE("{{Echoaudio," ECHOCARD_NAME "}}");
MODULE_DEVICE_TABLE(pci, snd_echo_ids);

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for " ECHOCARD_NAME " soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for " ECHOCARD_NAME " soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable " ECHOCARD_NAME " soundcard.");

static unsigned int channels_list[10] = {1, 2, 4, 6, 8, 10, 12, 14, 16, 999999};
static const DECLARE_TLV_DB_SCALE(db_scale_output_gain, -12800, 100, 1);

static int get_firmware(const struct firmware **fw_entry,
			const struct firmware *frm, struct echoaudio *chip)
{
	int err;
	char name[30];
	DE_ACT(("firmware requested: %s\n", frm->data));
	snprintf(name, sizeof(name), "ea/%s", frm->data);
	if ((err = request_firmware(fw_entry, name, pci_device(chip))) < 0)
		snd_printk(KERN_ERR "get_firmware(): Firmware not available (%d)\n", err);
	return err;
}

static void free_firmware(const struct firmware *fw_entry)
{
	release_firmware(fw_entry);
	DE_ACT(("firmware released\n"));
}



/******************************************************************************
	PCM interface
******************************************************************************/

static void audiopipe_free(struct snd_pcm_runtime *runtime)
{
	struct audiopipe *pipe = runtime->private_data;

	if (pipe->sgpage.area)
		snd_dma_free_pages(&pipe->sgpage);
	kfree(pipe);
}



static int hw_rule_capture_format_by_channels(struct snd_pcm_hw_params *params,
					      struct snd_pcm_hw_rule *rule)
{
	struct snd_interval *c = hw_param_interval(params,
						   SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_mask *f = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);
	struct snd_mask fmt;

	snd_mask_any(&fmt);

#ifndef ECHOCARD_HAS_STEREO_BIG_ENDIAN32
	/* >=2 channels cannot be S32_BE */
	if (c->min == 2) {
		fmt.bits[0] &= ~SNDRV_PCM_FMTBIT_S32_BE;
		return snd_mask_refine(f, &fmt);
	}
#endif
	/* > 2 channels cannot be U8 and S32_BE */
	if (c->min > 2) {
		fmt.bits[0] &= ~(SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S32_BE);
		return snd_mask_refine(f, &fmt);
	}
	/* Mono is ok with any format */
	return 0;
}



static int hw_rule_capture_channels_by_format(struct snd_pcm_hw_params *params,
					      struct snd_pcm_hw_rule *rule)
{
	struct snd_interval *c = hw_param_interval(params,
						   SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_mask *f = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);
	struct snd_interval ch;

	snd_interval_any(&ch);

	/* S32_BE is mono (and stereo) only */
	if (f->bits[0] == SNDRV_PCM_FMTBIT_S32_BE) {
		ch.min = 1;
#ifdef ECHOCARD_HAS_STEREO_BIG_ENDIAN32
		ch.max = 2;
#else
		ch.max = 1;
#endif
		ch.integer = 1;
		return snd_interval_refine(c, &ch);
	}
	/* U8 can be only mono or stereo */
	if (f->bits[0] == SNDRV_PCM_FMTBIT_U8) {
		ch.min = 1;
		ch.max = 2;
		ch.integer = 1;
		return snd_interval_refine(c, &ch);
	}
	/* S16_LE, S24_3LE and S32_LE support any number of channels. */
	return 0;
}



static int hw_rule_playback_format_by_channels(struct snd_pcm_hw_params *params,
					       struct snd_pcm_hw_rule *rule)
{
	struct snd_interval *c = hw_param_interval(params,
						   SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_mask *f = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);
	struct snd_mask fmt;
	u64 fmask;
	snd_mask_any(&fmt);

	fmask = fmt.bits[0] + ((u64)fmt.bits[1] << 32);

	/* >2 channels must be S16_LE, S24_3LE or S32_LE */
	if (c->min > 2) {
		fmask &= SNDRV_PCM_FMTBIT_S16_LE |
			 SNDRV_PCM_FMTBIT_S24_3LE |
			 SNDRV_PCM_FMTBIT_S32_LE;
	/* 1 channel must be S32_BE or S32_LE */
	} else if (c->max == 1)
		fmask &= SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S32_BE;
#ifndef ECHOCARD_HAS_STEREO_BIG_ENDIAN32
	/* 2 channels cannot be S32_BE */
	else if (c->min == 2 && c->max == 2)
		fmask &= ~SNDRV_PCM_FMTBIT_S32_BE;
#endif
	else
		return 0;

	fmt.bits[0] &= (u32)fmask;
	fmt.bits[1] &= (u32)(fmask >> 32);
	return snd_mask_refine(f, &fmt);
}



static int hw_rule_playback_channels_by_format(struct snd_pcm_hw_params *params,
					       struct snd_pcm_hw_rule *rule)
{
	struct snd_interval *c = hw_param_interval(params,
						   SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_mask *f = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);
	struct snd_interval ch;
	u64 fmask;

	snd_interval_any(&ch);
	ch.integer = 1;
	fmask = f->bits[0] + ((u64)f->bits[1] << 32);

	/* S32_BE is mono (and stereo) only */
	if (fmask == SNDRV_PCM_FMTBIT_S32_BE) {
		ch.min = 1;
#ifdef ECHOCARD_HAS_STEREO_BIG_ENDIAN32
		ch.max = 2;
#else
		ch.max = 1;
#endif
	/* U8 is stereo only */
	} else if (fmask == SNDRV_PCM_FMTBIT_U8)
		ch.min = ch.max = 2;
	/* S16_LE and S24_3LE must be at least stereo */
	else if (!(fmask & ~(SNDRV_PCM_FMTBIT_S16_LE |
			       SNDRV_PCM_FMTBIT_S24_3LE)))
		ch.min = 2;
	else
		return 0;

	return snd_interval_refine(c, &ch);
}



/* Since the sample rate is a global setting, do allow the user to change the
sample rate only if there is only one pcm device open. */
static int hw_rule_sample_rate(struct snd_pcm_hw_params *params,
			       struct snd_pcm_hw_rule *rule)
{
	struct snd_interval *rate = hw_param_interval(params,
						      SNDRV_PCM_HW_PARAM_RATE);
	struct echoaudio *chip = rule->private;
	struct snd_interval fixed;

	if (!chip->can_set_rate) {
		snd_interval_any(&fixed);
		fixed.min = fixed.max = chip->sample_rate;
		return snd_interval_refine(rate, &fixed);
	}
	return 0;
}


static int pcm_open(struct snd_pcm_substream *substream,
		    signed char max_channels)
{
	struct echoaudio *chip;
	struct snd_pcm_runtime *runtime;
	struct audiopipe *pipe;
	int err, i;

	if (max_channels <= 0)
		return -EAGAIN;

	chip = snd_pcm_substream_chip(substream);
	runtime = substream->runtime;

	pipe = kzalloc(sizeof(struct audiopipe), GFP_KERNEL);
	if (!pipe)
		return -ENOMEM;
	pipe->index = -1;		/* Not configured yet */

	/* Set up hw capabilities and contraints */
	memcpy(&pipe->hw, &pcm_hardware_skel, sizeof(struct snd_pcm_hardware));
	DE_HWP(("max_channels=%d\n", max_channels));
	pipe->constr.list = channels_list;
	pipe->constr.mask = 0;
	for (i = 0; channels_list[i] <= max_channels; i++);
	pipe->constr.count = i;
	if (pipe->hw.channels_max > max_channels)
		pipe->hw.channels_max = max_channels;
	if (chip->digital_mode == DIGITAL_MODE_ADAT) {
		pipe->hw.rate_max = 48000;
		pipe->hw.rates &= SNDRV_PCM_RATE_8000_48000;
	}

	runtime->hw = pipe->hw;
	runtime->private_data = pipe;
	runtime->private_free = audiopipe_free;
	snd_pcm_set_sync(substream);

	/* Only mono and any even number of channels are allowed */
	if ((err = snd_pcm_hw_constraint_list(runtime, 0,
					      SNDRV_PCM_HW_PARAM_CHANNELS,
					      &pipe->constr)) < 0)
		return err;

	/* All periods should have the same size */
	if ((err = snd_pcm_hw_constraint_integer(runtime,
						 SNDRV_PCM_HW_PARAM_PERIODS)) < 0)
		return err;

	/* The hw accesses memory in chunks 32 frames long and they should be
	32-bytes-aligned. It's not a requirement, but it seems that IRQs are
	generated with a resolution of 32 frames. Thus we need the following */
	if ((err = snd_pcm_hw_constraint_step(runtime, 0,
					      SNDRV_PCM_HW_PARAM_PERIOD_SIZE,
					      32)) < 0)
		return err;
	if ((err = snd_pcm_hw_constraint_step(runtime, 0,
					      SNDRV_PCM_HW_PARAM_BUFFER_SIZE,
					      32)) < 0)
		return err;

	if ((err = snd_pcm_hw_rule_add(substream->runtime, 0,
				       SNDRV_PCM_HW_PARAM_RATE,
					hw_rule_sample_rate, chip,
				       SNDRV_PCM_HW_PARAM_RATE, -1)) < 0)
		return err;

	/* Finally allocate a page for the scatter-gather list */
	if ((err = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV,
				       snd_dma_pci_data(chip->pci),
				       PAGE_SIZE, &pipe->sgpage)) < 0) {
		DE_HWP(("s-g list allocation failed\n"));
		return err;
	}

	return 0;
}



static int pcm_analog_in_open(struct snd_pcm_substream *substream)
{
	struct echoaudio *chip = snd_pcm_substream_chip(substream);
	int err;

	DE_ACT(("pcm_analog_in_open\n"));
	if ((err = pcm_open(substream, num_analog_busses_in(chip) -
			    substream->number)) < 0)
		return err;
	if ((err = snd_pcm_hw_rule_add(substream->runtime, 0,
				       SNDRV_PCM_HW_PARAM_CHANNELS,
				       hw_rule_capture_channels_by_format, NULL,
				       SNDRV_PCM_HW_PARAM_FORMAT, -1)) < 0)
		return err;
	if ((err = snd_pcm_hw_rule_add(substream->runtime, 0,
				       SNDRV_PCM_HW_PARAM_FORMAT,
				       hw_rule_capture_format_by_channels, NULL,
				       SNDRV_PCM_HW_PARAM_CHANNELS, -1)) < 0)
		return err;
	atomic_inc(&chip->opencount);
	if (atomic_read(&chip->opencount) > 1 && chip->rate_set)
		chip->can_set_rate=0;
	DE_HWP(("pcm_analog_in_open  cs=%d  oc=%d  r=%d\n",
		chip->can_set_rate, atomic_read(&chip->opencount),
		chip->sample_rate));
	return 0;
}



static int pcm_analog_out_open(struct snd_pcm_substream *substream)
{
	struct echoaudio *chip = snd_pcm_substream_chip(substream);
	int max_channels, err;

#ifdef ECHOCARD_HAS_VMIXER
	max_channels = num_pipes_out(chip);
#else
	max_channels = num_analog_busses_out(chip);
#endif
	DE_ACT(("pcm_analog_out_open\n"));
	if ((err = pcm_open(substream, max_channels - substream->number)) < 0)
		return err;
	if ((err = snd_pcm_hw_rule_add(substream->runtime, 0,
				       SNDRV_PCM_HW_PARAM_CHANNELS,
				       hw_rule_playback_channels_by_format,
				       NULL,
				       SNDRV_PCM_HW_PARAM_FORMAT, -1)) < 0)
		return err;
	if ((err = snd_pcm_hw_rule_add(substream->runtime, 0,
				       SNDRV_PCM_HW_PARAM_FORMAT,
				       hw_rule_playback_format_by_channels,
				       NULL,
				       SNDRV_PCM_HW_PARAM_CHANNELS, -1)) < 0)
		return err;
	atomic_inc(&chip->opencount);
	if (atomic_read(&chip->opencount) > 1 && chip->rate_set)
		chip->can_set_rate=0;
	DE_HWP(("pcm_analog_out_open  cs=%d  oc=%d  r=%d\n",
		chip->can_set_rate, atomic_read(&chip->opencount),
		chip->sample_rate));
	return 0;
}



#ifdef ECHOCARD_HAS_DIGITAL_IO

static int pcm_digital_in_open(struct snd_pcm_substream *substream)
{
	struct echoaudio *chip = snd_pcm_substream_chip(substream);
	int err, max_channels;

	DE_ACT(("pcm_digital_in_open\n"));
	max_channels = num_digital_busses_in(chip) - substream->number;
	down(&chip->mode_mutex);
	if (chip->digital_mode == DIGITAL_MODE_ADAT)
		err = pcm_open(substream, max_channels);
	else	/* If the card has ADAT, subtract the 6 channels
		 * that S/PDIF doesn't have
		 */
		err = pcm_open(substream, max_channels - ECHOCARD_HAS_ADAT);

	if (err < 0)
		goto din_exit;

	if ((err = snd_pcm_hw_rule_add(substream->runtime, 0,
				       SNDRV_PCM_HW_PARAM_CHANNELS,
				       hw_rule_capture_channels_by_format, NULL,
				       SNDRV_PCM_HW_PARAM_FORMAT, -1)) < 0)
		goto din_exit;
	if ((err = snd_pcm_hw_rule_add(substream->runtime, 0,
				       SNDRV_PCM_HW_PARAM_FORMAT,
				       hw_rule_capture_format_by_channels, NULL,
				       SNDRV_PCM_HW_PARAM_CHANNELS, -1)) < 0)
		goto din_exit;

	atomic_inc(&chip->opencount);
	if (atomic_read(&chip->opencount) > 1 && chip->rate_set)
		chip->can_set_rate=0;

din_exit:
	up(&chip->mode_mutex);
	return err;
}



#ifndef ECHOCARD_HAS_VMIXER	/* See the note in snd_echo_new_pcm() */

static int pcm_digital_out_open(struct snd_pcm_substream *substream)
{
	struct echoaudio *chip = snd_pcm_substream_chip(substream);
	int err, max_channels;

	DE_ACT(("pcm_digital_out_open\n"));
	max_channels = num_digital_busses_out(chip) - substream->number;
	down(&chip->mode_mutex);
	if (chip->digital_mode == DIGITAL_MODE_ADAT)
		err = pcm_open(substream, max_channels);
	else	/* If the card has ADAT, subtract the 6 channels
		 * that S/PDIF doesn't have
		 */
		err = pcm_open(substream, max_channels - ECHOCARD_HAS_ADAT);

	if (err < 0)
		goto dout_exit;

	if ((err = snd_pcm_hw_rule_add(substream->runtime, 0,
				       SNDRV_PCM_HW_PARAM_CHANNELS,
				       hw_rule_playback_channels_by_format,
				       NULL, SNDRV_PCM_HW_PARAM_FORMAT,
				       -1)) < 0)
		goto dout_exit;
	if ((err = snd_pcm_hw_rule_add(substream->runtime, 0,
				       SNDRV_PCM_HW_PARAM_FORMAT,
				       hw_rule_playback_format_by_channels,
				       NULL, SNDRV_PCM_HW_PARAM_CHANNELS,
				       -1)) < 0)
		goto dout_exit;
	atomic_inc(&chip->opencount);
	if (atomic_read(&chip->opencount) > 1 && chip->rate_set)
		chip->can_set_rate=0;
dout_exit:
	up(&chip->mode_mutex);
	return err;
}

#endif /* !ECHOCARD_HAS_VMIXER */

#endif /* ECHOCARD_HAS_DIGITAL_IO */



static int pcm_close(struct snd_pcm_substream *substream)
{
	struct echoaudio *chip = snd_pcm_substream_chip(substream);
	int oc;

	/* Nothing to do here. Audio is already off and pipe will be
	 * freed by its callback
	 */
	DE_ACT(("pcm_close\n"));

	atomic_dec(&chip->opencount);
	oc = atomic_read(&chip->opencount);
	DE_ACT(("pcm_close  oc=%d  cs=%d  rs=%d\n", oc,
		chip->can_set_rate, chip->rate_set));
	if (oc < 2)
		chip->can_set_rate = 1;
	if (oc == 0)
		chip->rate_set = 0;
	DE_ACT(("pcm_close2 oc=%d  cs=%d  rs=%d\n", oc,
		chip->can_set_rate,chip->rate_set));

	return 0;
}



/* Channel allocation and scatter-gather list setup */
static int init_engine(struct snd_pcm_substream *substream,
		       struct snd_pcm_hw_params *hw_params,
		       int pipe_index, int interleave)
{
	struct echoaudio *chip;
	int err, per, rest, page, edge, offs;
	struct snd_sg_buf *sgbuf;
	struct audiopipe *pipe;

	chip = snd_pcm_substream_chip(substream);
	pipe = (struct audiopipe *) substream->runtime->private_data;

	/* Sets up che hardware. If it's already initialized, reset and
	 * redo with the new parameters
	 */
	spin_lock_irq(&chip->lock);
	if (pipe->index >= 0) {
		DE_HWP(("hwp_ie free(%d)\n", pipe->index));
		err = free_pipes(chip, pipe);
		snd_assert(!err);
		chip->substream[pipe->index] = NULL;
	}

	err = allocate_pipes(chip, pipe, pipe_index, interleave);
	if (err < 0) {
		spin_unlock_irq(&chip->lock);
		DE_ACT((KERN_NOTICE "allocate_pipes(%d) err=%d\n",
			pipe_index, err));
		return err;
	}
	spin_unlock_irq(&chip->lock);
	DE_ACT((KERN_NOTICE "allocate_pipes()=%d\n", pipe_index));

	DE_HWP(("pcm_hw_params (bufsize=%dB periods=%d persize=%dB)\n",
		params_buffer_bytes(hw_params), params_periods(hw_params),
		params_period_bytes(hw_params)));
	err = snd_pcm_lib_malloc_pages(substream,
				       params_buffer_bytes(hw_params));
	if (err < 0) {
		snd_printk(KERN_ERR "malloc_pages err=%d\n", err);
		spin_lock_irq(&chip->lock);
		free_pipes(chip, pipe);
		spin_unlock_irq(&chip->lock);
		pipe->index = -1;
		return err;
	}

	sgbuf = snd_pcm_substream_sgbuf(substream);

	DE_HWP(("pcm_hw_params table size=%d pages=%d\n",
		sgbuf->size, sgbuf->pages));
	sglist_init(chip, pipe);
	edge = PAGE_SIZE;
	for (offs = page = per = 0; offs < params_buffer_bytes(hw_params);
	     per++) {
		rest = params_period_bytes(hw_params);
		if (offs + rest > params_buffer_bytes(hw_params))
			rest = params_buffer_bytes(hw_params) - offs;
		while (rest) {
			if (rest <= edge - offs) {
				sglist_add_mapping(chip, pipe,
						   snd_sgbuf_get_addr(sgbuf, offs),
						   rest);
				sglist_add_irq(chip, pipe);
				offs += rest;
				rest = 0;
			} else {
				sglist_add_mapping(chip, pipe,
						   snd_sgbuf_get_addr(sgbuf, offs),
						   edge - offs);
				rest -= edge - offs;
				offs = edge;
			}
			if (offs == edge) {
				edge += PAGE_SIZE;
				page++;
			}
		}
	}

	/* Close the ring buffer */
	sglist_wrap(chip, pipe);

	/* This stuff is used by the irq handler, so it must be
	 * initialized before chip->substream
	 */
	chip->last_period[pipe_index] = 0;
	pipe->last_counter = 0;
	pipe->position = 0;
	smp_wmb();
	chip->substream[pipe_index] = substream;
	chip->rate_set = 1;
	spin_lock_irq(&chip->lock);
	set_sample_rate(chip, hw_params->rate_num / hw_params->rate_den);
	spin_unlock_irq(&chip->lock);
	DE_HWP(("pcm_hw_params ok\n"));
	return 0;
}



static int pcm_analog_in_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *hw_params)
{
	struct echoaudio *chip = snd_pcm_substream_chip(substream);

	return init_engine(substream, hw_params, px_analog_in(chip) +
			substream->number, params_channels(hw_params));
}



static int pcm_analog_out_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *hw_params)
{
	return init_engine(substream, hw_params, substream->number,
			   params_channels(hw_params));
}



#ifdef ECHOCARD_HAS_DIGITAL_IO

static int pcm_digital_in_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *hw_params)
{
	struct echoaudio *chip = snd_pcm_substream_chip(substream);

	return init_engine(substream, hw_params, px_digital_in(chip) +
			substream->number, params_channels(hw_params));
}



#ifndef ECHOCARD_HAS_VMIXER	/* See the note in snd_echo_new_pcm() */
static int pcm_digital_out_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *hw_params)
{
	struct echoaudio *chip = snd_pcm_substream_chip(substream);

	return init_engine(substream, hw_params, px_digital_out(chip) +
			substream->number, params_channels(hw_params));
}
#endif /* !ECHOCARD_HAS_VMIXER */

#endif /* ECHOCARD_HAS_DIGITAL_IO */



static int pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct echoaudio *chip;
	struct audiopipe *pipe;

	chip = snd_pcm_substream_chip(substream);
	pipe = (struct audiopipe *) substream->runtime->private_data;

	spin_lock_irq(&chip->lock);
	if (pipe->index >= 0) {
		DE_HWP(("pcm_hw_free(%d)\n", pipe->index));
		free_pipes(chip, pipe);
		chip->substream[pipe->index] = NULL;
		pipe->index = -1;
	}
	spin_unlock_irq(&chip->lock);

	DE_HWP(("pcm_hw_freed\n"));
	snd_pcm_lib_free_pages(substream);
	return 0;
}



static int pcm_prepare(struct snd_pcm_substream *substream)
{
	struct echoaudio *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct audioformat format;
	int pipe_index = ((struct audiopipe *)runtime->private_data)->index;

	DE_HWP(("Prepare rate=%d format=%d channels=%d\n",
		runtime->rate, runtime->format, runtime->channels));
	format.interleave = runtime->channels;
	format.data_are_bigendian = 0;
	format.mono_to_stereo = 0;
	switch (runtime->format) {
	case SNDRV_PCM_FORMAT_U8:
		format.bits_per_sample = 8;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		format.bits_per_sample = 16;
		break;
	case SNDRV_PCM_FORMAT_S24_3LE:
		format.bits_per_sample = 24;
		break;
	case SNDRV_PCM_FORMAT_S32_BE:
		format.data_are_bigendian = 1;
	case SNDRV_PCM_FORMAT_S32_LE:
		format.bits_per_sample = 32;
		break;
	default:
		DE_HWP(("Prepare error: unsupported format %d\n",
			runtime->format));
		return -EINVAL;
	}

	snd_assert(pipe_index < px_num(chip), return -EINVAL);
	snd_assert(is_pipe_allocated(chip, pipe_index), return -EINVAL);
	set_audio_format(chip, pipe_index, &format);
	return 0;
}



static int pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct echoaudio *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct audiopipe *pipe = runtime->private_data;
	int i, err;
	u32 channelmask = 0;
	struct snd_pcm_substream *s;

	snd_pcm_group_for_each_entry(s, substream) {
		for (i = 0; i < DSP_MAXPIPES; i++) {
			if (s == chip->substream[i]) {
				channelmask |= 1 << i;
				snd_pcm_trigger_done(s, substream);
			}
		}
	}

	spin_lock(&chip->lock);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		DE_ACT(("pcm_trigger start\n"));
		for (i = 0; i < DSP_MAXPIPES; i++) {
			if (channelmask & (1 << i)) {
				pipe = chip->substream[i]->runtime->private_data;
				switch (pipe->state) {
				case PIPE_STATE_STOPPED:
					chip->last_period[i] = 0;
					pipe->last_counter = 0;
					pipe->position = 0;
					*pipe->dma_counter = 0;
				case PIPE_STATE_PAUSED:
					pipe->state = PIPE_STATE_STARTED;
					break;
				case PIPE_STATE_STARTED:
					break;
				}
			}
		}
		err = start_transport(chip, channelmask,
				      chip->pipe_cyclic_mask);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		DE_ACT(("pcm_trigger stop\n"));
		for (i = 0; i < DSP_MAXPIPES; i++) {
			if (channelmask & (1 << i)) {
				pipe = chip->substream[i]->runtime->private_data;
				pipe->state = PIPE_STATE_STOPPED;
			}
		}
		err = stop_transport(chip, channelmask);
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		DE_ACT(("pcm_trigger pause\n"));
		for (i = 0; i < DSP_MAXPIPES; i++) {
			if (channelmask & (1 << i)) {
				pipe = chip->substream[i]->runtime->private_data;
				pipe->state = PIPE_STATE_PAUSED;
			}
		}
		err = pause_transport(chip, channelmask);
		break;
	default:
		err = -EINVAL;
	}
	spin_unlock(&chip->lock);
	return err;
}



static snd_pcm_uframes_t pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct audiopipe *pipe = runtime->private_data;
	size_t cnt, bufsize, pos;

	cnt = le32_to_cpu(*pipe->dma_counter);
	pipe->position += cnt - pipe->last_counter;
	pipe->last_counter = cnt;
	bufsize = substream->runtime->buffer_size;
	pos = bytes_to_frames(substream->runtime, pipe->position);

	while (pos >= bufsize) {
		pipe->position -= frames_to_bytes(substream->runtime, bufsize);
		pos -= bufsize;
	}
	return pos;
}



/* pcm *_ops structures */
static struct snd_pcm_ops analog_playback_ops = {
	.open = pcm_analog_out_open,
	.close = pcm_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = pcm_analog_out_hw_params,
	.hw_free = pcm_hw_free,
	.prepare = pcm_prepare,
	.trigger = pcm_trigger,
	.pointer = pcm_pointer,
	.page = snd_pcm_sgbuf_ops_page,
};
static struct snd_pcm_ops analog_capture_ops = {
	.open = pcm_analog_in_open,
	.close = pcm_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = pcm_analog_in_hw_params,
	.hw_free = pcm_hw_free,
	.prepare = pcm_prepare,
	.trigger = pcm_trigger,
	.pointer = pcm_pointer,
	.page = snd_pcm_sgbuf_ops_page,
};
#ifdef ECHOCARD_HAS_DIGITAL_IO
#ifndef ECHOCARD_HAS_VMIXER
static struct snd_pcm_ops digital_playback_ops = {
	.open = pcm_digital_out_open,
	.close = pcm_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = pcm_digital_out_hw_params,
	.hw_free = pcm_hw_free,
	.prepare = pcm_prepare,
	.trigger = pcm_trigger,
	.pointer = pcm_pointer,
	.page = snd_pcm_sgbuf_ops_page,
};
#endif /* !ECHOCARD_HAS_VMIXER */
static struct snd_pcm_ops digital_capture_ops = {
	.open = pcm_digital_in_open,
	.close = pcm_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = pcm_digital_in_hw_params,
	.hw_free = pcm_hw_free,
	.prepare = pcm_prepare,
	.trigger = pcm_trigger,
	.pointer = pcm_pointer,
	.page = snd_pcm_sgbuf_ops_page,
};
#endif /* ECHOCARD_HAS_DIGITAL_IO */



/* Preallocate memory only for the first substream because it's the most
 * used one
 */
static int snd_echo_preallocate_pages(struct snd_pcm *pcm, struct device *dev)
{
	struct snd_pcm_substream *ss;
	int stream, err;

	for (stream = 0; stream < 2; stream++)
		for (ss = pcm->streams[stream].substream; ss; ss = ss->next) {
			err = snd_pcm_lib_preallocate_pages(ss, SNDRV_DMA_TYPE_DEV_SG,
							    dev,
							    ss->number ? 0 : 128<<10,
							    256<<10);
			if (err < 0)
				return err;
		}
	return 0;
}



/*<--snd_echo_probe() */
static int __devinit snd_echo_new_pcm(struct echoaudio *chip)
{
	struct snd_pcm *pcm;
	int err;

#ifdef ECHOCARD_HAS_VMIXER
	/* This card has a Vmixer, that is there is no direct mapping from PCM
	streams to physical outputs. The user can mix the streams as he wishes
	via control interface and it's possible to send any stream to any
	output, thus it makes no sense to keep analog and digital outputs
	separated */

	/* PCM#0 Virtual outputs and analog inputs */
	if ((err = snd_pcm_new(chip->card, "PCM", 0, num_pipes_out(chip),
				num_analog_busses_in(chip), &pcm)) < 0)
		return err;
	pcm->private_data = chip;
	chip->analog_pcm = pcm;
	strcpy(pcm->name, chip->card->shortname);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &analog_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &analog_capture_ops);
	if ((err = snd_echo_preallocate_pages(pcm, snd_dma_pci_data(chip->pci))) < 0)
		return err;
	DE_INIT(("Analog PCM ok\n"));

#ifdef ECHOCARD_HAS_DIGITAL_IO
	/* PCM#1 Digital inputs, no outputs */
	if ((err = snd_pcm_new(chip->card, "Digital PCM", 1, 0,
			       num_digital_busses_in(chip), &pcm)) < 0)
		return err;
	pcm->private_data = chip;
	chip->digital_pcm = pcm;
	strcpy(pcm->name, chip->card->shortname);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &digital_capture_ops);
	if ((err = snd_echo_preallocate_pages(pcm, snd_dma_pci_data(chip->pci))) < 0)
		return err;
	DE_INIT(("Digital PCM ok\n"));
#endif /* ECHOCARD_HAS_DIGITAL_IO */

#else /* ECHOCARD_HAS_VMIXER */

	/* The card can manage substreams formed by analog and digital channels
	at the same time, but I prefer to keep analog and digital channels
	separated, because that mixed thing is confusing and useless. So we
	register two PCM devices: */

	/* PCM#0 Analog i/o */
	if ((err = snd_pcm_new(chip->card, "Analog PCM", 0,
			       num_analog_busses_out(chip),
			       num_analog_busses_in(chip), &pcm)) < 0)
		return err;
	pcm->private_data = chip;
	chip->analog_pcm = pcm;
	strcpy(pcm->name, chip->card->shortname);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &analog_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &analog_capture_ops);
	if ((err = snd_echo_preallocate_pages(pcm, snd_dma_pci_data(chip->pci))) < 0)
		return err;
	DE_INIT(("Analog PCM ok\n"));

#ifdef ECHOCARD_HAS_DIGITAL_IO
	/* PCM#1 Digital i/o */
	if ((err = snd_pcm_new(chip->card, "Digital PCM", 1,
			       num_digital_busses_out(chip),
			       num_digital_busses_in(chip), &pcm)) < 0)
		return err;
	pcm->private_data = chip;
	chip->digital_pcm = pcm;
	strcpy(pcm->name, chip->card->shortname);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &digital_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &digital_capture_ops);
	if ((err = snd_echo_preallocate_pages(pcm, snd_dma_pci_data(chip->pci))) < 0)
		return err;
	DE_INIT(("Digital PCM ok\n"));
#endif /* ECHOCARD_HAS_DIGITAL_IO */

#endif /* ECHOCARD_HAS_VMIXER */

	return 0;
}




/******************************************************************************
	Control interface
******************************************************************************/

/******************* PCM output volume *******************/
static int snd_echo_output_gain_info(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_info *uinfo)
{
	struct echoaudio *chip;

	chip = snd_kcontrol_chip(kcontrol);
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = num_busses_out(chip);
	uinfo->value.integer.min = ECHOGAIN_MINOUT;
	uinfo->value.integer.max = ECHOGAIN_MAXOUT;
	return 0;
}

static int snd_echo_output_gain_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct echoaudio *chip;
	int c;

	chip = snd_kcontrol_chip(kcontrol);
	for (c = 0; c < num_busses_out(chip); c++)
		ucontrol->value.integer.value[c] = chip->output_gain[c];
	return 0;
}

static int snd_echo_output_gain_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct echoaudio *chip;
	int c, changed, gain;

	changed = 0;
	chip = snd_kcontrol_chip(kcontrol);
	spin_lock_irq(&chip->lock);
	for (c = 0; c < num_busses_out(chip); c++) {
		gain = ucontrol->value.integer.value[c];
		/* Ignore out of range values */
		if (gain < ECHOGAIN_MINOUT || gain > ECHOGAIN_MAXOUT)
			continue;
		if (chip->output_gain[c] != gain) {
			set_output_gain(chip, c, gain);
			changed = 1;
		}
	}
	if (changed)
		update_output_line_level(chip);
	spin_unlock_irq(&chip->lock);
	return changed;
}

#ifdef ECHOCARD_HAS_VMIXER
/* On Vmixer cards this one controls the line-out volume */
static struct snd_kcontrol_new snd_echo_line_output_gain __devinitdata = {
	.name = "Line Playback Volume",
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_TLV_READ,
	.info = snd_echo_output_gain_info,
	.get = snd_echo_output_gain_get,
	.put = snd_echo_output_gain_put,
	.tlv = {.p = db_scale_output_gain},
};
#else
static struct snd_kcontrol_new snd_echo_pcm_output_gain __devinitdata = {
	.name = "PCM Playback Volume",
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_TLV_READ,
	.info = snd_echo_output_gain_info,
	.get = snd_echo_output_gain_get,
	.put = snd_echo_output_gain_put,
	.tlv = {.p = db_scale_output_gain},
};
#endif



#ifdef ECHOCARD_HAS_INPUT_GAIN

/******************* Analog input volume *******************/
static int snd_echo_input_gain_info(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_info *uinfo)
{
	struct echoaudio *chip;

	chip = snd_kcontrol_chip(kcontrol);
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = num_analog_busses_in(chip);
	uinfo->value.integer.min = ECHOGAIN_MININP;
	uinfo->value.integer.max = ECHOGAIN_MAXINP;
	return 0;
}

static int snd_echo_input_gain_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct echoaudio *chip;
	int c;

	chip = snd_kcontrol_chip(kcontrol);
	for (c = 0; c < num_analog_busses_in(chip); c++)
		ucontrol->value.integer.value[c] = chip->input_gain[c];
	return 0;
}

static int snd_echo_input_gain_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct echoaudio *chip;
	int c, gain, changed;

	changed = 0;
	chip = snd_kcontrol_chip(kcontrol);
	spin_lock_irq(&chip->lock);
	for (c = 0; c < num_analog_busses_in(chip); c++) {
		gain = ucontrol->value.integer.value[c];
		/* Ignore out of range values */
		if (gain < ECHOGAIN_MININP || gain > ECHOGAIN_MAXINP)
			continue;
		if (chip->input_gain[c] != gain) {
			set_input_gain(chip, c, gain);
			changed = 1;
		}
	}
	if (changed)
		update_input_line_level(chip);
	spin_unlock_irq(&chip->lock);
	return changed;
}

static const DECLARE_TLV_DB_SCALE(db_scale_input_gain, -2500, 50, 0);

static struct snd_kcontrol_new snd_echo_line_input_gain __devinitdata = {
	.name = "Line Capture Volume",
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_TLV_READ,
	.info = snd_echo_input_gain_info,
	.get = snd_echo_input_gain_get,
	.put = snd_echo_input_gain_put,
	.tlv = {.p = db_scale_input_gain},
};

#endif /* ECHOCARD_HAS_INPUT_GAIN */



#ifdef ECHOCARD_HAS_OUTPUT_NOMINAL_LEVEL

/************ Analog output nominal level (+4dBu / -10dBV) ***************/
static int snd_echo_output_nominal_info (struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_info *uinfo)
{
	struct echoaudio *chip;

	chip = snd_kcontrol_chip(kcontrol);
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = num_analog_busses_out(chip);
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_echo_output_nominal_get(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct echoaudio *chip;
	int c;

	chip = snd_kcontrol_chip(kcontrol);
	for (c = 0; c < num_analog_busses_out(chip); c++)
		ucontrol->value.integer.value[c] = chip->nominal_level[c];
	return 0;
}

static int snd_echo_output_nominal_put(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct echoaudio *chip;
	int c, changed;

	changed = 0;
	chip = snd_kcontrol_chip(kcontrol);
	spin_lock_irq(&chip->lock);
	for (c = 0; c < num_analog_busses_out(chip); c++) {
		if (chip->nominal_level[c] != ucontrol->value.integer.value[c]) {
			set_nominal_level(chip, c,
					  ucontrol->value.integer.value[c]);
			changed = 1;
		}
	}
	if (changed)
		update_output_line_level(chip);
	spin_unlock_irq(&chip->lock);
	return changed;
}

static struct snd_kcontrol_new snd_echo_output_nominal_level __devinitdata = {
	.name = "Line Playback Switch (-10dBV)",
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.info = snd_echo_output_nominal_info,
	.get = snd_echo_output_nominal_get,
	.put = snd_echo_output_nominal_put,
};

#endif /* ECHOCARD_HAS_OUTPUT_NOMINAL_LEVEL */



#ifdef ECHOCARD_HAS_INPUT_NOMINAL_LEVEL

/*************** Analog input nominal level (+4dBu / -10dBV) ***************/
static int snd_echo_input_nominal_info(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_info *uinfo)
{
	struct echoaudio *chip;

	chip = snd_kcontrol_chip(kcontrol);
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = num_analog_busses_in(chip);
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_echo_input_nominal_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct echoaudio *chip;
	int c;

	chip = snd_kcontrol_chip(kcontrol);
	for (c = 0; c < num_analog_busses_in(chip); c++)
		ucontrol->value.integer.value[c] =
			chip->nominal_level[bx_analog_in(chip) + c];
	return 0;
}

static int snd_echo_input_nominal_put(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct echoaudio *chip;
	int c, changed;

	changed = 0;
	chip = snd_kcontrol_chip(kcontrol);
	spin_lock_irq(&chip->lock);
	for (c = 0; c < num_analog_busses_in(chip); c++) {
		if (chip->nominal_level[bx_analog_in(chip) + c] !=
		    ucontrol->value.integer.value[c]) {
			set_nominal_level(chip, bx_analog_in(chip) + c,
					  ucontrol->value.integer.value[c]);
			changed = 1;
		}
	}
	if (changed)
		update_output_line_level(chip);	/* "Output" is not a mistake
						 * here.
						 */
	spin_unlock_irq(&chip->lock);
	return changed;
}

static struct snd_kcontrol_new snd_echo_intput_nominal_level __devinitdata = {
	.name = "Line Capture Switch (-10dBV)",
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.info = snd_echo_input_nominal_info,
	.get = snd_echo_input_nominal_get,
	.put = snd_echo_input_nominal_put,
};

#endif /* ECHOCARD_HAS_INPUT_NOMINAL_LEVEL */



#ifdef ECHOCARD_HAS_MONITOR

/******************* Monitor mixer *******************/
static int snd_echo_mixer_info(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_info *uinfo)
{
	struct echoaudio *chip;

	chip = snd_kcontrol_chip(kcontrol);
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = ECHOGAIN_MINOUT;
	uinfo->value.integer.max = ECHOGAIN_MAXOUT;
	uinfo->dimen.d[0] = num_busses_out(chip);
	uinfo->dimen.d[1] = num_busses_in(chip);
	return 0;
}

static int snd_echo_mixer_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct echoaudio *chip;

	chip = snd_kcontrol_chip(kcontrol);
	ucontrol->value.integer.value[0] =
		chip->monitor_gain[ucontrol->id.index / num_busses_in(chip)]
			[ucontrol->id.index % num_busses_in(chip)];
	return 0;
}

static int snd_echo_mixer_put(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct echoaudio *chip;
	int changed,  gain;
	short out, in;

	changed = 0;
	chip = snd_kcontrol_chip(kcontrol);
	out = ucontrol->id.index / num_busses_in(chip);
	in = ucontrol->id.index % num_busses_in(chip);
	gain = ucontrol->value.integer.value[0];
	if (gain < ECHOGAIN_MINOUT || gain > ECHOGAIN_MAXOUT)
		return -EINVAL;
	if (chip->monitor_gain[out][in] != gain) {
		spin_lock_irq(&chip->lock);
		set_monitor_gain(chip, out, in, gain);
		update_output_line_level(chip);
		spin_unlock_irq(&chip->lock);
		changed = 1;
	}
	return changed;
}

static struct snd_kcontrol_new snd_echo_monitor_mixer __devinitdata = {
	.name = "Monitor Mixer Volume",
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_TLV_READ,
	.info = snd_echo_mixer_info,
	.get = snd_echo_mixer_get,
	.put = snd_echo_mixer_put,
	.tlv = {.p = db_scale_output_gain},
};

#endif /* ECHOCARD_HAS_MONITOR */



#ifdef ECHOCARD_HAS_VMIXER

/******************* Vmixer *******************/
static int snd_echo_vmixer_info(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *uinfo)
{
	struct echoaudio *chip;

	chip = snd_kcontrol_chip(kcontrol);
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = ECHOGAIN_MINOUT;
	uinfo->value.integer.max = ECHOGAIN_MAXOUT;
	uinfo->dimen.d[0] = num_busses_out(chip);
	uinfo->dimen.d[1] = num_pipes_out(chip);
	return 0;
}

static int snd_echo_vmixer_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct echoaudio *chip;

	chip = snd_kcontrol_chip(kcontrol);
	ucontrol->value.integer.value[0] =
		chip->vmixer_gain[ucontrol->id.index / num_pipes_out(chip)]
			[ucontrol->id.index % num_pipes_out(chip)];
	return 0;
}

static int snd_echo_vmixer_put(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct echoaudio *chip;
	int gain, changed;
	short vch, out;

	changed = 0;
	chip = snd_kcontrol_chip(kcontrol);
	out = ucontrol->id.index / num_pipes_out(chip);
	vch = ucontrol->id.index % num_pipes_out(chip);
	gain = ucontrol->value.integer.value[0];
	if (gain < ECHOGAIN_MINOUT || gain > ECHOGAIN_MAXOUT)
		return -EINVAL;
	if (chip->vmixer_gain[out][vch] != ucontrol->value.integer.value[0]) {
		spin_lock_irq(&chip->lock);
		set_vmixer_gain(chip, out, vch, ucontrol->value.integer.value[0]);
		update_vmixer_level(chip);
		spin_unlock_irq(&chip->lock);
		changed = 1;
	}
	return changed;
}

static struct snd_kcontrol_new snd_echo_vmixer __devinitdata = {
	.name = "VMixer Volume",
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_TLV_READ,
	.info = snd_echo_vmixer_info,
	.get = snd_echo_vmixer_get,
	.put = snd_echo_vmixer_put,
	.tlv = {.p = db_scale_output_gain},
};

#endif /* ECHOCARD_HAS_VMIXER */



#ifdef ECHOCARD_HAS_DIGITAL_MODE_SWITCH

/******************* Digital mode switch *******************/
static int snd_echo_digital_mode_info(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_info *uinfo)
{
	static char *names[4] = {
		"S/PDIF Coaxial", "S/PDIF Optical", "ADAT Optical",
		"S/PDIF Cdrom"
	};
	struct echoaudio *chip;

	chip = snd_kcontrol_chip(kcontrol);
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->value.enumerated.items = chip->num_digital_modes;
	uinfo->count = 1;
	if (uinfo->value.enumerated.item >= chip->num_digital_modes)
		uinfo->value.enumerated.item = chip->num_digital_modes - 1;
	strcpy(uinfo->value.enumerated.name, names[
			chip->digital_mode_list[uinfo->value.enumerated.item]]);
	return 0;
}

static int snd_echo_digital_mode_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct echoaudio *chip;
	int i, mode;

	chip = snd_kcontrol_chip(kcontrol);
	mode = chip->digital_mode;
	for (i = chip->num_digital_modes - 1; i >= 0; i--)
		if (mode == chip->digital_mode_list[i]) {
			ucontrol->value.enumerated.item[0] = i;
			break;
		}
	return 0;
}

static int snd_echo_digital_mode_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct echoaudio *chip;
	int changed;
	unsigned short emode, dmode;

	changed = 0;
	chip = snd_kcontrol_chip(kcontrol);

	emode = ucontrol->value.enumerated.item[0];
	if (emode >= chip->num_digital_modes)
		return -EINVAL;
	dmode = chip->digital_mode_list[emode];

	if (dmode != chip->digital_mode) {
		/* mode_mutex is required to make this operation atomic wrt
		pcm_digital_*_open() and set_input_clock() functions. */
		down(&chip->mode_mutex);

		/* Do not allow the user to change the digital mode when a pcm
		device is open because it also changes the number of channels
		and the allowed sample rates */
		if (atomic_read(&chip->opencount)) {
			changed = -EAGAIN;
		} else {
			changed = set_digital_mode(chip, dmode);
			/* If we had to change the clock source, report it */
			if (changed > 0 && chip->clock_src_ctl) {
				snd_ctl_notify(chip->card,
					       SNDRV_CTL_EVENT_MASK_VALUE,
					       &chip->clock_src_ctl->id);
				DE_ACT(("SDM() =%d\n", changed));
			}
			if (changed >= 0)
				changed = 1;	/* No errors */
		}
		up(&chip->mode_mutex);
	}
	return changed;
}

static struct snd_kcontrol_new snd_echo_digital_mode_switch __devinitdata = {
	.name = "Digital mode Switch",
	.iface = SNDRV_CTL_ELEM_IFACE_CARD,
	.info = snd_echo_digital_mode_info,
	.get = snd_echo_digital_mode_get,
	.put = snd_echo_digital_mode_put,
};

#endif /* ECHOCARD_HAS_DIGITAL_MODE_SWITCH */



#ifdef ECHOCARD_HAS_DIGITAL_IO

/******************* S/PDIF mode switch *******************/
static int snd_echo_spdif_mode_info(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_info *uinfo)
{
	static char *names[2] = {"Consumer", "Professional"};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->value.enumerated.items = 2;
	uinfo->count = 1;
	if (uinfo->value.enumerated.item)
		uinfo->value.enumerated.item = 1;
	strcpy(uinfo->value.enumerated.name,
	       names[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_echo_spdif_mode_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct echoaudio *chip;

	chip = snd_kcontrol_chip(kcontrol);
	ucontrol->value.enumerated.item[0] = !!chip->professional_spdif;
	return 0;
}

static int snd_echo_spdif_mode_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct echoaudio *chip;
	int mode;

	chip = snd_kcontrol_chip(kcontrol);
	mode = !!ucontrol->value.enumerated.item[0];
	if (mode != chip->professional_spdif) {
		spin_lock_irq(&chip->lock);
		set_professional_spdif(chip, mode);
		spin_unlock_irq(&chip->lock);
		return 1;
	}
	return 0;
}

static struct snd_kcontrol_new snd_echo_spdif_mode_switch __devinitdata = {
	.name = "S/PDIF mode Switch",
	.iface = SNDRV_CTL_ELEM_IFACE_CARD,
	.info = snd_echo_spdif_mode_info,
	.get = snd_echo_spdif_mode_get,
	.put = snd_echo_spdif_mode_put,
};

#endif /* ECHOCARD_HAS_DIGITAL_IO */



#ifdef ECHOCARD_HAS_EXTERNAL_CLOCK

/******************* Select input clock source *******************/
static int snd_echo_clock_source_info(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_info *uinfo)
{
	static char *names[8] = {
		"Internal", "Word", "Super", "S/PDIF", "ADAT", "ESync",
		"ESync96", "MTC"
	};
	struct echoaudio *chip;

	chip = snd_kcontrol_chip(kcontrol);
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->value.enumerated.items = chip->num_clock_sources;
	uinfo->count = 1;
	if (uinfo->value.enumerated.item >= chip->num_clock_sources)
		uinfo->value.enumerated.item = chip->num_clock_sources - 1;
	strcpy(uinfo->value.enumerated.name, names[
			chip->clock_source_list[uinfo->value.enumerated.item]]);
	return 0;
}

static int snd_echo_clock_source_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct echoaudio *chip;
	int i, clock;

	chip = snd_kcontrol_chip(kcontrol);
	clock = chip->input_clock;

	for (i = 0; i < chip->num_clock_sources; i++)
		if (clock == chip->clock_source_list[i])
			ucontrol->value.enumerated.item[0] = i;

	return 0;
}

static int snd_echo_clock_source_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct echoaudio *chip;
	int changed;
	unsigned int eclock, dclock;

	changed = 0;
	chip = snd_kcontrol_chip(kcontrol);
	eclock = ucontrol->value.enumerated.item[0];
	if (eclock >= chip->input_clock_types)
		return -EINVAL;
	dclock = chip->clock_source_list[eclock];
	if (chip->input_clock != dclock) {
		down(&chip->mode_mutex);
		spin_lock_irq(&chip->lock);
		if ((changed = set_input_clock(chip, dclock)) == 0)
			changed = 1;	/* no errors */
		spin_unlock_irq(&chip->lock);
		up(&chip->mode_mutex);
	}

	if (changed < 0)
		DE_ACT(("seticlk val%d err 0x%x\n", dclock, changed));

	return changed;
}

static struct snd_kcontrol_new snd_echo_clock_source_switch __devinitdata = {
	.name = "Sample Clock Source",
	.iface = SNDRV_CTL_ELEM_IFACE_PCM,
	.info = snd_echo_clock_source_info,
	.get = snd_echo_clock_source_get,
	.put = snd_echo_clock_source_put,
};

#endif /* ECHOCARD_HAS_EXTERNAL_CLOCK */



#ifdef ECHOCARD_HAS_PHANTOM_POWER

/******************* Phantom power switch *******************/
static int snd_echo_phantom_power_info(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_echo_phantom_power_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct echoaudio *chip = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = chip->phantom_power;
	return 0;
}

static int snd_echo_phantom_power_put(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct echoaudio *chip = snd_kcontrol_chip(kcontrol);
	int power, changed = 0;

	power = !!ucontrol->value.integer.value[0];
	if (chip->phantom_power != power) {
		spin_lock_irq(&chip->lock);
		changed = set_phantom_power(chip, power);
		spin_unlock_irq(&chip->lock);
		if (changed == 0)
			changed = 1;	/* no errors */
	}
	return changed;
}

static struct snd_kcontrol_new snd_echo_phantom_power_switch __devinitdata = {
	.name = "Phantom power Switch",
	.iface = SNDRV_CTL_ELEM_IFACE_CARD,
	.info = snd_echo_phantom_power_info,
	.get = snd_echo_phantom_power_get,
	.put = snd_echo_phantom_power_put,
};

#endif /* ECHOCARD_HAS_PHANTOM_POWER */



#ifdef ECHOCARD_HAS_DIGITAL_IN_AUTOMUTE

/******************* Digital input automute switch *******************/
static int snd_echo_automute_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_echo_automute_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct echoaudio *chip = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = chip->digital_in_automute;
	return 0;
}

static int snd_echo_automute_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct echoaudio *chip = snd_kcontrol_chip(kcontrol);
	int automute, changed = 0;

	automute = !!ucontrol->value.integer.value[0];
	if (chip->digital_in_automute != automute) {
		spin_lock_irq(&chip->lock);
		changed = set_input_auto_mute(chip, automute);
		spin_unlock_irq(&chip->lock);
		if (changed == 0)
			changed = 1;	/* no errors */
	}
	return changed;
}

static struct snd_kcontrol_new snd_echo_automute_switch __devinitdata = {
	.name = "Digital Capture Switch (automute)",
	.iface = SNDRV_CTL_ELEM_IFACE_CARD,
	.info = snd_echo_automute_info,
	.get = snd_echo_automute_get,
	.put = snd_echo_automute_put,
};

#endif /* ECHOCARD_HAS_DIGITAL_IN_AUTOMUTE */



/******************* VU-meters switch *******************/
static int snd_echo_vumeters_switch_info(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_info *uinfo)
{
	struct echoaudio *chip;

	chip = snd_kcontrol_chip(kcontrol);
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_echo_vumeters_switch_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct echoaudio *chip;

	chip = snd_kcontrol_chip(kcontrol);
	spin_lock_irq(&chip->lock);
	set_meters_on(chip, ucontrol->value.integer.value[0]);
	spin_unlock_irq(&chip->lock);
	return 1;
}

static struct snd_kcontrol_new snd_echo_vumeters_switch __devinitdata = {
	.name = "VU-meters Switch",
	.iface = SNDRV_CTL_ELEM_IFACE_CARD,
	.access = SNDRV_CTL_ELEM_ACCESS_WRITE,
	.info = snd_echo_vumeters_switch_info,
	.put = snd_echo_vumeters_switch_put,
};



/***** Read VU-meters (input, output, analog and digital together) *****/
static int snd_echo_vumeters_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	struct echoaudio *chip;

	chip = snd_kcontrol_chip(kcontrol);
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 96;
	uinfo->value.integer.min = ECHOGAIN_MINOUT;
	uinfo->value.integer.max = 0;
#ifdef ECHOCARD_HAS_VMIXER
	uinfo->dimen.d[0] = 3;	/* Out, In, Virt */
#else
	uinfo->dimen.d[0] = 2;	/* Out, In */
#endif
	uinfo->dimen.d[1] = 16;	/* 16 channels */
	uinfo->dimen.d[2] = 2;	/* 0=level, 1=peak */
	return 0;
}

static int snd_echo_vumeters_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct echoaudio *chip;

	chip = snd_kcontrol_chip(kcontrol);
	get_audio_meters(chip, ucontrol->value.integer.value);
	return 0;
}

static struct snd_kcontrol_new snd_echo_vumeters __devinitdata = {
	.name = "VU-meters",
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READ |
		  SNDRV_CTL_ELEM_ACCESS_VOLATILE |
		  SNDRV_CTL_ELEM_ACCESS_TLV_READ,
	.info = snd_echo_vumeters_info,
	.get = snd_echo_vumeters_get,
	.tlv = {.p = db_scale_output_gain},
};



/*** Channels info - it exports informations about the number of channels ***/
static int snd_echo_channels_info_info(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_info *uinfo)
{
	struct echoaudio *chip;

	chip = snd_kcontrol_chip(kcontrol);
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 6;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1 << ECHO_CLOCK_NUMBER;
	return 0;
}

static int snd_echo_channels_info_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct echoaudio *chip;
	int detected, clocks, bit, src;

	chip = snd_kcontrol_chip(kcontrol);
	ucontrol->value.integer.value[0] = num_busses_in(chip);
	ucontrol->value.integer.value[1] = num_analog_busses_in(chip);
	ucontrol->value.integer.value[2] = num_busses_out(chip);
	ucontrol->value.integer.value[3] = num_analog_busses_out(chip);
	ucontrol->value.integer.value[4] = num_pipes_out(chip);

	/* Compute the bitmask of the currently valid input clocks */
	detected = detect_input_clocks(chip);
	clocks = 0;
	src = chip->num_clock_sources - 1;
	for (bit = ECHO_CLOCK_NUMBER - 1; bit >= 0; bit--)
		if (detected & (1 << bit))
			for (; src >= 0; src--)
				if (bit == chip->clock_source_list[src]) {
					clocks |= 1 << src;
					break;
				}
	ucontrol->value.integer.value[5] = clocks;

	return 0;
}

static struct snd_kcontrol_new snd_echo_channels_info __devinitdata = {
	.name = "Channels info",
	.iface = SNDRV_CTL_ELEM_IFACE_HWDEP,
	.access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
	.info = snd_echo_channels_info_info,
	.get = snd_echo_channels_info_get,
};




/******************************************************************************
	IRQ Handler
******************************************************************************/

static irqreturn_t snd_echo_interrupt(int irq, void *dev_id)
{
	struct echoaudio *chip = dev_id;
	struct snd_pcm_substream *substream;
	int period, ss, st;

	spin_lock(&chip->lock);
	st = service_irq(chip);
	if (st < 0) {
		spin_unlock(&chip->lock);
		return IRQ_NONE;
	}
	/* The hardware doesn't tell us which substream caused the irq,
	thus we have to check all running substreams. */
	for (ss = 0; ss < DSP_MAXPIPES; ss++) {
		if ((substream = chip->substream[ss])) {
			period = pcm_pointer(substream) /
				substream->runtime->period_size;
			if (period != chip->last_period[ss]) {
				chip->last_period[ss] = period;
				spin_unlock(&chip->lock);
				snd_pcm_period_elapsed(substream);
				spin_lock(&chip->lock);
			}
		}
	}
	spin_unlock(&chip->lock);

#ifdef ECHOCARD_HAS_MIDI
	if (st > 0 && chip->midi_in) {
		snd_rawmidi_receive(chip->midi_in, chip->midi_buffer, st);
		DE_MID(("rawmidi_iread=%d\n", st));
	}
#endif
	return IRQ_HANDLED;
}




/******************************************************************************
	Module construction / destruction
******************************************************************************/

static int snd_echo_free(struct echoaudio *chip)
{
	DE_INIT(("Stop DSP...\n"));
	if (chip->comm_page) {
		rest_in_peace(chip);
		snd_dma_free_pages(&chip->commpage_dma_buf);
	}
	DE_INIT(("Stopped.\n"));

	if (chip->irq >= 0)
		free_irq(chip->irq, chip);

	if (chip->dsp_registers)
		iounmap(chip->dsp_registers);

	if (chip->iores)
		release_and_free_resource(chip->iores);

	DE_INIT(("MMIO freed.\n"));

	pci_disable_device(chip->pci);

	/* release chip data */
	kfree(chip);
	DE_INIT(("Chip freed.\n"));
	return 0;
}



static int snd_echo_dev_free(struct snd_device *device)
{
	struct echoaudio *chip = device->device_data;

	DE_INIT(("snd_echo_dev_free()...\n"));
	return snd_echo_free(chip);
}



/* <--snd_echo_probe() */
static __devinit int snd_echo_create(struct snd_card *card,
				     struct pci_dev *pci,
				     struct echoaudio **rchip)
{
	struct echoaudio *chip;
	int err;
	size_t sz;
	static struct snd_device_ops ops = {
		.dev_free = snd_echo_dev_free,
	};

	*rchip = NULL;

	pci_write_config_byte(pci, PCI_LATENCY_TIMER, 0xC0);

	if ((err = pci_enable_device(pci)) < 0)
		return err;
	pci_set_master(pci);

	/* allocate a chip-specific data */
	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (!chip) {
		pci_disable_device(pci);
		return -ENOMEM;
	}
	DE_INIT(("chip=%p\n", chip));

	spin_lock_init(&chip->lock);
	chip->card = card;
	chip->pci = pci;
	chip->irq = -1;

	/* PCI resource allocation */
	chip->dsp_registers_phys = pci_resource_start(pci, 0);
	sz = pci_resource_len(pci, 0);
	if (sz > PAGE_SIZE)
		sz = PAGE_SIZE;		/* We map only the required part */

	if ((chip->iores = request_mem_region(chip->dsp_registers_phys, sz,
					      ECHOCARD_NAME)) == NULL) {
		snd_echo_free(chip);
		snd_printk(KERN_ERR "cannot get memory region\n");
		return -EBUSY;
	}
	chip->dsp_registers = (volatile u32 __iomem *)
		ioremap_nocache(chip->dsp_registers_phys, sz);

	if (request_irq(pci->irq, snd_echo_interrupt, IRQF_SHARED,
			ECHOCARD_NAME, chip)) {
		snd_echo_free(chip);
		snd_printk(KERN_ERR "cannot grab irq\n");
		return -EBUSY;
	}
	chip->irq = pci->irq;
	DE_INIT(("pci=%p irq=%d subdev=%04x Init hardware...\n",
		 chip->pci, chip->irq, chip->pci->subsystem_device));

	/* Create the DSP comm page - this is the area of memory used for most
	of the communication with the DSP, which accesses it via bus mastering */
	if (snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, snd_dma_pci_data(chip->pci),
				sizeof(struct comm_page),
				&chip->commpage_dma_buf) < 0) {
		snd_echo_free(chip);
		snd_printk(KERN_ERR "cannot allocate the comm page\n");
		return -ENOMEM;
	}
	chip->comm_page_phys = chip->commpage_dma_buf.addr;
	chip->comm_page = (struct comm_page *)chip->commpage_dma_buf.area;

	err = init_hw(chip, chip->pci->device, chip->pci->subsystem_device);
	if (err) {
		DE_INIT(("init_hw err=%d\n", err));
		snd_echo_free(chip);
		return err;
	}
	DE_INIT(("Card init OK\n"));

	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops)) < 0) {
		snd_echo_free(chip);
		return err;
	}
	atomic_set(&chip->opencount, 0);
	init_MUTEX(&chip->mode_mutex);
	chip->can_set_rate = 1;
	*rchip = chip;
	/* Init done ! */
	return 0;
}



/* constructor */
static int __devinit snd_echo_probe(struct pci_dev *pci,
				    const struct pci_device_id *pci_id)
{
	static int dev;
	struct snd_card *card;
	struct echoaudio *chip;
	char *dsp;
	int i, err;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[dev]) {
		dev++;
		return -ENOENT;
	}

	DE_INIT(("Echoaudio driver starting...\n"));
	i = 0;
	card = snd_card_new(index[dev], id[dev], THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;

	snd_card_set_dev(card, &pci->dev);

	if ((err = snd_echo_create(card, pci, &chip)) < 0) {
		snd_card_free(card);
		return err;
	}

	strcpy(card->driver, "Echo_" ECHOCARD_NAME);
	strcpy(card->shortname, chip->card_name);

	dsp = "56301";
	if (pci_id->device == 0x3410)
		dsp = "56361";

	sprintf(card->longname, "%s rev.%d (DSP%s) at 0x%lx irq %i",
		card->shortname, pci_id->subdevice & 0x000f, dsp,
		chip->dsp_registers_phys, chip->irq);

	if ((err = snd_echo_new_pcm(chip)) < 0) {
		snd_printk(KERN_ERR "new pcm error %d\n", err);
		snd_card_free(card);
		return err;
	}

#ifdef ECHOCARD_HAS_MIDI
	if (chip->has_midi) {	/* Some Mia's do not have midi */
		if ((err = snd_echo_midi_create(card, chip)) < 0) {
			snd_printk(KERN_ERR "new midi error %d\n", err);
			snd_card_free(card);
			return err;
		}
	}
#endif

#ifdef ECHOCARD_HAS_VMIXER
	snd_echo_vmixer.count = num_pipes_out(chip) * num_busses_out(chip);
	if ((err = snd_ctl_add(chip->card, snd_ctl_new1(&snd_echo_line_output_gain, chip))) < 0)
		goto ctl_error;
	if ((err = snd_ctl_add(chip->card, snd_ctl_new1(&snd_echo_vmixer, chip))) < 0)
		goto ctl_error;
#else
	if ((err = snd_ctl_add(chip->card, snd_ctl_new1(&snd_echo_pcm_output_gain, chip))) < 0)
		goto ctl_error;
#endif

#ifdef ECHOCARD_HAS_INPUT_GAIN
	if ((err = snd_ctl_add(chip->card, snd_ctl_new1(&snd_echo_line_input_gain, chip))) < 0)
		goto ctl_error;
#endif

#ifdef ECHOCARD_HAS_INPUT_NOMINAL_LEVEL
	if (!chip->hasnt_input_nominal_level)
		if ((err = snd_ctl_add(chip->card, snd_ctl_new1(&snd_echo_intput_nominal_level, chip))) < 0)
			goto ctl_error;
#endif

#ifdef ECHOCARD_HAS_OUTPUT_NOMINAL_LEVEL
	if ((err = snd_ctl_add(chip->card, snd_ctl_new1(&snd_echo_output_nominal_level, chip))) < 0)
		goto ctl_error;
#endif

	if ((err = snd_ctl_add(chip->card, snd_ctl_new1(&snd_echo_vumeters_switch, chip))) < 0)
		goto ctl_error;

	if ((err = snd_ctl_add(chip->card, snd_ctl_new1(&snd_echo_vumeters, chip))) < 0)
		goto ctl_error;

#ifdef ECHOCARD_HAS_MONITOR
	snd_echo_monitor_mixer.count = num_busses_in(chip) * num_busses_out(chip);
	if ((err = snd_ctl_add(chip->card, snd_ctl_new1(&snd_echo_monitor_mixer, chip))) < 0)
		goto ctl_error;
#endif

#ifdef ECHOCARD_HAS_DIGITAL_IN_AUTOMUTE
	if ((err = snd_ctl_add(chip->card, snd_ctl_new1(&snd_echo_automute_switch, chip))) < 0)
		goto ctl_error;
#endif

	if ((err = snd_ctl_add(chip->card, snd_ctl_new1(&snd_echo_channels_info, chip))) < 0)
		goto ctl_error;

#ifdef ECHOCARD_HAS_DIGITAL_MODE_SWITCH
	/* Creates a list of available digital modes */
	chip->num_digital_modes = 0;
	for (i = 0; i < 6; i++)
		if (chip->digital_modes & (1 << i))
			chip->digital_mode_list[chip->num_digital_modes++] = i;

	if ((err = snd_ctl_add(chip->card, snd_ctl_new1(&snd_echo_digital_mode_switch, chip))) < 0)
		goto ctl_error;
#endif /* ECHOCARD_HAS_DIGITAL_MODE_SWITCH */

#ifdef ECHOCARD_HAS_EXTERNAL_CLOCK
	/* Creates a list of available clock sources */
	chip->num_clock_sources = 0;
	for (i = 0; i < 10; i++)
		if (chip->input_clock_types & (1 << i))
			chip->clock_source_list[chip->num_clock_sources++] = i;

	if (chip->num_clock_sources > 1) {
		chip->clock_src_ctl = snd_ctl_new1(&snd_echo_clock_source_switch, chip);
		if ((err = snd_ctl_add(chip->card, chip->clock_src_ctl)) < 0)
			goto ctl_error;
	}
#endif /* ECHOCARD_HAS_EXTERNAL_CLOCK */

#ifdef ECHOCARD_HAS_DIGITAL_IO
	if ((err = snd_ctl_add(chip->card, snd_ctl_new1(&snd_echo_spdif_mode_switch, chip))) < 0)
		goto ctl_error;
#endif

#ifdef ECHOCARD_HAS_PHANTOM_POWER
	if (chip->has_phantom_power)
		if ((err = snd_ctl_add(chip->card, snd_ctl_new1(&snd_echo_phantom_power_switch, chip))) < 0)
			goto ctl_error;
#endif

	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		goto ctl_error;
	}
	snd_printk(KERN_INFO "Card registered: %s\n", card->longname);

	pci_set_drvdata(pci, chip);
	dev++;
	return 0;

ctl_error:
	snd_printk(KERN_ERR "new control error %d\n", err);
	snd_card_free(card);
	return err;
}



static void __devexit snd_echo_remove(struct pci_dev *pci)
{
	struct echoaudio *chip;

	chip = pci_get_drvdata(pci);
	if (chip)
		snd_card_free(chip->card);
	pci_set_drvdata(pci, NULL);
}



/******************************************************************************
	Everything starts and ends here
******************************************************************************/

/* pci_driver definition */
static struct pci_driver driver = {
	.name = "Echoaudio " ECHOCARD_NAME,
	.id_table = snd_echo_ids,
	.probe = snd_echo_probe,
	.remove = __devexit_p(snd_echo_remove),
};



/* initialization of the module */
static int __init alsa_card_echo_init(void)
{
	return pci_register_driver(&driver);
}



/* clean up the module */
static void __exit alsa_card_echo_exit(void)
{
	pci_unregister_driver(&driver);
}


module_init(alsa_card_echo_init)
module_exit(alsa_card_echo_exit)
