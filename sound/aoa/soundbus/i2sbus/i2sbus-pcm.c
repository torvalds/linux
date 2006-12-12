/*
 * i2sbus driver -- pcm routines
 *
 * Copyright 2006 Johannes Berg <johannes@sipsolutions.net>
 *
 * GPL v2, can be found in COPYING.
 */

#include <asm/io.h>
#include <linux/delay.h>
/* So apparently there's a reason for requiring driver.h
 * to be included first, even if I don't know it... */
#include <sound/driver.h>
#include <sound/core.h>
#include <asm/macio.h>
#include <linux/pci.h>
#include "../soundbus.h"
#include "i2sbus.h"

static inline void get_pcm_info(struct i2sbus_dev *i2sdev, int in,
				struct pcm_info **pi, struct pcm_info **other)
{
	if (in) {
		if (pi)
			*pi = &i2sdev->in;
		if (other)
			*other = &i2sdev->out;
	} else {
		if (pi)
			*pi = &i2sdev->out;
		if (other)
			*other = &i2sdev->in;
	}
}

static int clock_and_divisors(int mclk, int sclk, int rate, int *out)
{
	/* sclk must be derived from mclk! */
	if (mclk % sclk)
		return -1;
	/* derive sclk register value */
	if (i2s_sf_sclkdiv(mclk / sclk, out))
		return -1;

	if (I2S_CLOCK_SPEED_18MHz % (rate * mclk) == 0) {
		if (!i2s_sf_mclkdiv(I2S_CLOCK_SPEED_18MHz / (rate * mclk), out)) {
			*out |= I2S_SF_CLOCK_SOURCE_18MHz;
			return 0;
		}
	}
	if (I2S_CLOCK_SPEED_45MHz % (rate * mclk) == 0) {
		if (!i2s_sf_mclkdiv(I2S_CLOCK_SPEED_45MHz / (rate * mclk), out)) {
			*out |= I2S_SF_CLOCK_SOURCE_45MHz;
			return 0;
		}
	}
	if (I2S_CLOCK_SPEED_49MHz % (rate * mclk) == 0) {
		if (!i2s_sf_mclkdiv(I2S_CLOCK_SPEED_49MHz / (rate * mclk), out)) {
			*out |= I2S_SF_CLOCK_SOURCE_49MHz;
			return 0;
		}
	}
	return -1;
}

#define CHECK_RATE(rate)						\
	do { if (rates & SNDRV_PCM_RATE_ ##rate) {			\
		int dummy;						\
		if (clock_and_divisors(sysclock_factor,			\
				       bus_factor, rate, &dummy))	\
			rates &= ~SNDRV_PCM_RATE_ ##rate;		\
	} } while (0)

static int i2sbus_pcm_open(struct i2sbus_dev *i2sdev, int in)
{
	struct pcm_info *pi, *other;
	struct soundbus_dev *sdev;
	int masks_inited = 0, err;
	struct codec_info_item *cii, *rev;
	struct snd_pcm_hardware *hw;
	u64 formats = 0;
	unsigned int rates = 0;
	struct transfer_info v;
	int result = 0;
	int bus_factor = 0, sysclock_factor = 0;
	int found_this;

	mutex_lock(&i2sdev->lock);

	get_pcm_info(i2sdev, in, &pi, &other);

	hw = &pi->substream->runtime->hw;
	sdev = &i2sdev->sound;

	if (pi->active) {
		/* alsa messed up */
		result = -EBUSY;
		goto out_unlock;
	}

	/* we now need to assign the hw */
	list_for_each_entry(cii, &sdev->codec_list, list) {
		struct transfer_info *ti = cii->codec->transfers;
		bus_factor = cii->codec->bus_factor;
		sysclock_factor = cii->codec->sysclock_factor;
		while (ti->formats && ti->rates) {
			v = *ti;
			if (ti->transfer_in == in
			    && cii->codec->usable(cii, ti, &v)) {
				if (masks_inited) {
					formats &= v.formats;
					rates &= v.rates;
				} else {
					formats = v.formats;
					rates = v.rates;
					masks_inited = 1;
				}
			}
			ti++;
		}
	}
	if (!masks_inited || !bus_factor || !sysclock_factor) {
		result = -ENODEV;
		goto out_unlock;
	}
	/* bus dependent stuff */
	hw->info = SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
		   SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_RESUME;

	CHECK_RATE(5512);
	CHECK_RATE(8000);
	CHECK_RATE(11025);
	CHECK_RATE(16000);
	CHECK_RATE(22050);
	CHECK_RATE(32000);
	CHECK_RATE(44100);
	CHECK_RATE(48000);
	CHECK_RATE(64000);
	CHECK_RATE(88200);
	CHECK_RATE(96000);
	CHECK_RATE(176400);
	CHECK_RATE(192000);
	hw->rates = rates;

	/* well. the codec might want 24 bits only, and we'll
	 * ever only transfer 24 bits, but they are top-aligned!
	 * So for alsa, we claim that we're doing full 32 bit
	 * while in reality we'll ignore the lower 8 bits of
	 * that when doing playback (they're transferred as 0
	 * as far as I know, no codecs we have are 32-bit capable
	 * so I can't really test) and when doing recording we'll
	 * always have those lower 8 bits recorded as 0 */
	if (formats & SNDRV_PCM_FMTBIT_S24_BE)
		formats |= SNDRV_PCM_FMTBIT_S32_BE;
	if (formats & SNDRV_PCM_FMTBIT_U24_BE)
		formats |= SNDRV_PCM_FMTBIT_U32_BE;
	/* now mask off what we can support. I suppose we could
	 * also support S24_3LE and some similar formats, but I
	 * doubt there's a codec that would be able to use that,
	 * so we don't support it here. */
	hw->formats = formats & (SNDRV_PCM_FMTBIT_S16_BE |
				 SNDRV_PCM_FMTBIT_U16_BE |
				 SNDRV_PCM_FMTBIT_S32_BE |
				 SNDRV_PCM_FMTBIT_U32_BE);

	/* we need to set the highest and lowest rate possible.
	 * These are the highest and lowest rates alsa can
	 * support properly in its bitfield.
	 * Below, we'll use that to restrict to the rate
	 * currently in use (if any). */
	hw->rate_min = 5512;
	hw->rate_max = 192000;
	/* if the other stream is active, then we can only
	 * support what it is currently using.
	 * FIXME: I lied. This comment is wrong. We can support
	 * anything that works with the same serial format, ie.
	 * when recording 24 bit sound we can well play 16 bit
	 * sound at the same time iff using the same transfer mode.
	 */
	if (other->active) {
		/* FIXME: is this guaranteed by the alsa api? */
		hw->formats &= (1ULL << i2sdev->format);
		/* see above, restrict rates to the one we already have */
		hw->rate_min = i2sdev->rate;
		hw->rate_max = i2sdev->rate;
	}

	hw->channels_min = 2;
	hw->channels_max = 2;
	/* these are somewhat arbitrary */
	hw->buffer_bytes_max = 131072;
	hw->period_bytes_min = 256;
	hw->period_bytes_max = 16384;
	hw->periods_min = 3;
	hw->periods_max = MAX_DBDMA_COMMANDS;
	list_for_each_entry(cii, &sdev->codec_list, list) {
		if (cii->codec->open) {
			err = cii->codec->open(cii, pi->substream);
			if (err) {
				result = err;
				/* unwind */
				found_this = 0;
				list_for_each_entry_reverse(rev,
				    &sdev->codec_list, list) {
					if (found_this && rev->codec->close) {
						rev->codec->close(rev,
								pi->substream);
					}
					if (rev == cii)
						found_this = 1;
				}
				goto out_unlock;
			}
		}
	}

 out_unlock:
	mutex_unlock(&i2sdev->lock);
	return result;
}

#undef CHECK_RATE

static int i2sbus_pcm_close(struct i2sbus_dev *i2sdev, int in)
{
	struct codec_info_item *cii;
	struct pcm_info *pi;
	int err = 0, tmp;

	mutex_lock(&i2sdev->lock);

	get_pcm_info(i2sdev, in, &pi, NULL);

	list_for_each_entry(cii, &i2sdev->sound.codec_list, list) {
		if (cii->codec->close) {
			tmp = cii->codec->close(cii, pi->substream);
			if (tmp)
				err = tmp;
		}
	}

	pi->substream = NULL;
	pi->active = 0;
	mutex_unlock(&i2sdev->lock);
	return err;
}

static int i2sbus_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params)
{
	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(params));
}

static int i2sbus_hw_free(struct snd_pcm_substream *substream)
{
	snd_pcm_lib_free_pages(substream);
	return 0;
}

static int i2sbus_pcm_prepare(struct i2sbus_dev *i2sdev, int in)
{
	/* whee. Hard work now. The user has selected a bitrate
	 * and bit format, so now we have to program our
	 * I2S controller appropriately. */
	struct snd_pcm_runtime *runtime;
	struct dbdma_cmd *command;
	int i, periodsize;
	dma_addr_t offset;
	struct bus_info bi;
	struct codec_info_item *cii;
	int sfr = 0;		/* serial format register */
	int dws = 0;		/* data word sizes reg */
	int input_16bit;
	struct pcm_info *pi, *other;
	int cnt;
	int result = 0;

	mutex_lock(&i2sdev->lock);

	get_pcm_info(i2sdev, in, &pi, &other);

	if (pi->dbdma_ring.running) {
		result = -EBUSY;
		goto out_unlock;
	}

	runtime = pi->substream->runtime;
	pi->active = 1;
	if (other->active &&
	    ((i2sdev->format != runtime->format)
	     || (i2sdev->rate != runtime->rate))) {
		result = -EINVAL;
		goto out_unlock;
	}

	i2sdev->format = runtime->format;
	i2sdev->rate = runtime->rate;

	periodsize = snd_pcm_lib_period_bytes(pi->substream);
	pi->current_period = 0;

	/* generate dbdma command ring first */
	command = pi->dbdma_ring.cmds;
	offset = runtime->dma_addr;
	for (i = 0; i < pi->substream->runtime->periods;
	     i++, command++, offset += periodsize) {
		memset(command, 0, sizeof(struct dbdma_cmd));
		command->command =
		    cpu_to_le16((in ? INPUT_MORE : OUTPUT_MORE) | INTR_ALWAYS);
		command->phy_addr = cpu_to_le32(offset);
		command->req_count = cpu_to_le16(periodsize);
		command->xfer_status = cpu_to_le16(0);
	}
	/* last one branches back to first */
	command--;
	command->command |= cpu_to_le16(BR_ALWAYS);
	command->cmd_dep = cpu_to_le32(pi->dbdma_ring.bus_cmd_start);

	/* ok, let's set the serial format and stuff */
	switch (runtime->format) {
	/* 16 bit formats */
	case SNDRV_PCM_FORMAT_S16_BE:
	case SNDRV_PCM_FORMAT_U16_BE:
		/* FIXME: if we add different bus factors we need to
		 * do more here!! */
		bi.bus_factor = 0;
		list_for_each_entry(cii, &i2sdev->sound.codec_list, list) {
			bi.bus_factor = cii->codec->bus_factor;
			break;
		}
		if (!bi.bus_factor) {
			result = -ENODEV;
			goto out_unlock;
		}
		input_16bit = 1;
		break;
	case SNDRV_PCM_FORMAT_S32_BE:
	case SNDRV_PCM_FORMAT_U32_BE:
		/* force 64x bus speed, otherwise the data cannot be
		 * transferred quickly enough! */
		bi.bus_factor = 64;
		input_16bit = 0;
		break;
	default:
		result = -EINVAL;
		goto out_unlock;
	}
	/* we assume all sysclocks are the same! */
	list_for_each_entry(cii, &i2sdev->sound.codec_list, list) {
		bi.sysclock_factor = cii->codec->sysclock_factor;
		break;
	}

	if (clock_and_divisors(bi.sysclock_factor,
			       bi.bus_factor,
			       runtime->rate,
			       &sfr) < 0) {
		result = -EINVAL;
		goto out_unlock;
	}
	switch (bi.bus_factor) {
	case 32:
		sfr |= I2S_SF_SERIAL_FORMAT_I2S_32X;
		break;
	case 64:
		sfr |= I2S_SF_SERIAL_FORMAT_I2S_64X;
		break;
	}
	/* FIXME: THIS ASSUMES MASTER ALL THE TIME */
	sfr |= I2S_SF_SCLK_MASTER;

	list_for_each_entry(cii, &i2sdev->sound.codec_list, list) {
		int err = 0;
		if (cii->codec->prepare)
			err = cii->codec->prepare(cii, &bi, pi->substream);
		if (err) {
			result = err;
			goto out_unlock;
		}
	}
	/* codecs are fine with it, so set our clocks */
	if (input_16bit)
		dws =	(2 << I2S_DWS_NUM_CHANNELS_IN_SHIFT) |
			(2 << I2S_DWS_NUM_CHANNELS_OUT_SHIFT) |
			I2S_DWS_DATA_IN_16BIT | I2S_DWS_DATA_OUT_16BIT;
	else
		dws =	(2 << I2S_DWS_NUM_CHANNELS_IN_SHIFT) |
			(2 << I2S_DWS_NUM_CHANNELS_OUT_SHIFT) |
			I2S_DWS_DATA_IN_24BIT | I2S_DWS_DATA_OUT_24BIT;

	/* early exit if already programmed correctly */
	/* not locking these is fine since we touch them only in this function */
	if (in_le32(&i2sdev->intfregs->serial_format) == sfr
	 && in_le32(&i2sdev->intfregs->data_word_sizes) == dws)
		goto out_unlock;

	/* let's notify the codecs about clocks going away.
	 * For now we only do mastering on the i2s cell... */
	list_for_each_entry(cii, &i2sdev->sound.codec_list, list)
		if (cii->codec->switch_clock)
			cii->codec->switch_clock(cii, CLOCK_SWITCH_PREPARE_SLAVE);

	i2sbus_control_enable(i2sdev->control, i2sdev);
	i2sbus_control_cell(i2sdev->control, i2sdev, 1);

	out_le32(&i2sdev->intfregs->intr_ctl, I2S_PENDING_CLOCKS_STOPPED);

	i2sbus_control_clock(i2sdev->control, i2sdev, 0);

	msleep(1);

	/* wait for clock stopped. This can apparently take a while... */
	cnt = 100;
	while (cnt-- &&
	    !(in_le32(&i2sdev->intfregs->intr_ctl) & I2S_PENDING_CLOCKS_STOPPED)) {
		msleep(5);
	}
	out_le32(&i2sdev->intfregs->intr_ctl, I2S_PENDING_CLOCKS_STOPPED);

	/* not locking these is fine since we touch them only in this function */
	out_le32(&i2sdev->intfregs->serial_format, sfr);
	out_le32(&i2sdev->intfregs->data_word_sizes, dws);

        i2sbus_control_enable(i2sdev->control, i2sdev);
        i2sbus_control_cell(i2sdev->control, i2sdev, 1);
        i2sbus_control_clock(i2sdev->control, i2sdev, 1);
	msleep(1);

	list_for_each_entry(cii, &i2sdev->sound.codec_list, list)
		if (cii->codec->switch_clock)
			cii->codec->switch_clock(cii, CLOCK_SWITCH_SLAVE);

 out_unlock:
	mutex_unlock(&i2sdev->lock);
	return result;
}

static struct dbdma_cmd STOP_CMD = {
	.command = __constant_cpu_to_le16(DBDMA_STOP),
};

static int i2sbus_pcm_trigger(struct i2sbus_dev *i2sdev, int in, int cmd)
{
	struct codec_info_item *cii;
	struct pcm_info *pi;
	int timeout;
	struct dbdma_cmd tmp;
	int result = 0;
	unsigned long flags;

	spin_lock_irqsave(&i2sdev->low_lock, flags);

	get_pcm_info(i2sdev, in, &pi, NULL);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		if (pi->dbdma_ring.running) {
			result = -EALREADY;
			goto out_unlock;
		}
		list_for_each_entry(cii, &i2sdev->sound.codec_list, list)
			if (cii->codec->start)
				cii->codec->start(cii, pi->substream);
		pi->dbdma_ring.running = 1;

		/* reset dma engine */
		out_le32(&pi->dbdma->control,
			 0 | (RUN | PAUSE | FLUSH | WAKE) << 16);
		timeout = 100;
		while (in_le32(&pi->dbdma->status) & RUN && timeout--)
			udelay(1);
		if (timeout <= 0) {
			printk(KERN_ERR
			       "i2sbus: error waiting for dma reset\n");
			result = -ENXIO;
			goto out_unlock;
		}

		/* write dma command buffer address to the dbdma chip */
		out_le32(&pi->dbdma->cmdptr, pi->dbdma_ring.bus_cmd_start);
		/* post PCI write */
		mb();
		(void)in_le32(&pi->dbdma->status);

		/* change first command to STOP */
		tmp = *pi->dbdma_ring.cmds;
		*pi->dbdma_ring.cmds = STOP_CMD;

		/* set running state, remember that the first command is STOP */
		out_le32(&pi->dbdma->control, RUN | (RUN << 16));
		timeout = 100;
		/* wait for STOP to be executed */
		while (in_le32(&pi->dbdma->status) & ACTIVE && timeout--)
			udelay(1);
		if (timeout <= 0) {
			printk(KERN_ERR "i2sbus: error waiting for dma stop\n");
			result = -ENXIO;
			goto out_unlock;
		}
		/* again, write dma command buffer address to the dbdma chip,
		 * this time of the first real command */
		*pi->dbdma_ring.cmds = tmp;
		out_le32(&pi->dbdma->cmdptr, pi->dbdma_ring.bus_cmd_start);
		/* post write */
		mb();
		(void)in_le32(&pi->dbdma->status);

		/* reset dma engine again */
		out_le32(&pi->dbdma->control,
			 0 | (RUN | PAUSE | FLUSH | WAKE) << 16);
		timeout = 100;
		while (in_le32(&pi->dbdma->status) & RUN && timeout--)
			udelay(1);
		if (timeout <= 0) {
			printk(KERN_ERR
			       "i2sbus: error waiting for dma reset\n");
			result = -ENXIO;
			goto out_unlock;
		}

		/* wake up the chip with the next descriptor */
		out_le32(&pi->dbdma->control,
			 (RUN | WAKE) | ((RUN | WAKE) << 16));
		/* get the frame count  */
		pi->frame_count = in_le32(&i2sdev->intfregs->frame_count);

		/* off you go! */
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		if (!pi->dbdma_ring.running) {
			result = -EALREADY;
			goto out_unlock;
		}

		/* turn off all relevant bits */
		out_le32(&pi->dbdma->control,
			 (RUN | WAKE | FLUSH | PAUSE) << 16);
		{
			/* FIXME: move to own function */
			int timeout = 5000;
			while ((in_le32(&pi->dbdma->status) & RUN)
			       && --timeout > 0)
				udelay(1);
			if (!timeout)
				printk(KERN_ERR
				       "i2sbus: timed out turning "
				       "off dbdma engine!\n");
		}

		pi->dbdma_ring.running = 0;
		list_for_each_entry(cii, &i2sdev->sound.codec_list, list)
			if (cii->codec->stop)
				cii->codec->stop(cii, pi->substream);
		break;
	default:
		result = -EINVAL;
		goto out_unlock;
	}

 out_unlock:
	spin_unlock_irqrestore(&i2sdev->low_lock, flags);
	return result;
}

static snd_pcm_uframes_t i2sbus_pcm_pointer(struct i2sbus_dev *i2sdev, int in)
{
	struct pcm_info *pi;
	u32 fc;

	get_pcm_info(i2sdev, in, &pi, NULL);

	fc = in_le32(&i2sdev->intfregs->frame_count);
	fc = fc - pi->frame_count;

	return (bytes_to_frames(pi->substream->runtime,
			pi->current_period *
			snd_pcm_lib_period_bytes(pi->substream))
		+ fc) % pi->substream->runtime->buffer_size;
}

static inline void handle_interrupt(struct i2sbus_dev *i2sdev, int in)
{
	struct pcm_info *pi;
	u32 fc;
	u32 delta;

	spin_lock(&i2sdev->low_lock);
	get_pcm_info(i2sdev, in, &pi, NULL);

	if (!pi->dbdma_ring.running) {
		/* there was still an interrupt pending
		 * while we stopped. or maybe another
		 * processor (not the one that was stopping
		 * the DMA engine) was spinning above
		 * waiting for the lock. */
		goto out_unlock;
	}

	fc = in_le32(&i2sdev->intfregs->frame_count);
	/* a counter overflow does not change the calculation. */
	delta = fc - pi->frame_count;

	/* update current_period */
	while (delta >= pi->substream->runtime->period_size) {
		pi->current_period++;
		delta = delta - pi->substream->runtime->period_size;
	}

	if (unlikely(delta)) {
		/* Some interrupt came late, so check the dbdma.
		 * This special case exists to syncronize the frame_count with
		 * the dbdma transfer, but is hit every once in a while. */
		int period;

		period = (in_le32(&pi->dbdma->cmdptr)
		        - pi->dbdma_ring.bus_cmd_start)
				/ sizeof(struct dbdma_cmd);
		pi->current_period = pi->current_period
					% pi->substream->runtime->periods;

		while (pi->current_period != period) {
			pi->current_period++;
			pi->current_period %= pi->substream->runtime->periods;
			/* Set delta to zero, as the frame_count value is too
			 * high (otherwise the code path will not be executed).
			 * This corrects the fact that the frame_count is too
			 * low at the beginning due to buffering. */
			delta = 0;
		}
	}

	pi->frame_count = fc - delta;
	pi->current_period %= pi->substream->runtime->periods;

	spin_unlock(&i2sdev->low_lock);
	/* may call _trigger again, hence needs to be unlocked */
	snd_pcm_period_elapsed(pi->substream);
	return;
 out_unlock:
	spin_unlock(&i2sdev->low_lock);
}

irqreturn_t i2sbus_tx_intr(int irq, void *devid)
{
	handle_interrupt((struct i2sbus_dev *)devid, 0);
	return IRQ_HANDLED;
}

irqreturn_t i2sbus_rx_intr(int irq, void *devid)
{
	handle_interrupt((struct i2sbus_dev *)devid, 1);
	return IRQ_HANDLED;
}

static int i2sbus_playback_open(struct snd_pcm_substream *substream)
{
	struct i2sbus_dev *i2sdev = snd_pcm_substream_chip(substream);

	if (!i2sdev)
		return -EINVAL;
	i2sdev->out.substream = substream;
	return i2sbus_pcm_open(i2sdev, 0);
}

static int i2sbus_playback_close(struct snd_pcm_substream *substream)
{
	struct i2sbus_dev *i2sdev = snd_pcm_substream_chip(substream);
	int err;

	if (!i2sdev)
		return -EINVAL;
	if (i2sdev->out.substream != substream)
		return -EINVAL;
	err = i2sbus_pcm_close(i2sdev, 0);
	if (!err)
		i2sdev->out.substream = NULL;
	return err;
}

static int i2sbus_playback_prepare(struct snd_pcm_substream *substream)
{
	struct i2sbus_dev *i2sdev = snd_pcm_substream_chip(substream);

	if (!i2sdev)
		return -EINVAL;
	if (i2sdev->out.substream != substream)
		return -EINVAL;
	return i2sbus_pcm_prepare(i2sdev, 0);
}

static int i2sbus_playback_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct i2sbus_dev *i2sdev = snd_pcm_substream_chip(substream);

	if (!i2sdev)
		return -EINVAL;
	if (i2sdev->out.substream != substream)
		return -EINVAL;
	return i2sbus_pcm_trigger(i2sdev, 0, cmd);
}

static snd_pcm_uframes_t i2sbus_playback_pointer(struct snd_pcm_substream
						 *substream)
{
	struct i2sbus_dev *i2sdev = snd_pcm_substream_chip(substream);

	if (!i2sdev)
		return -EINVAL;
	if (i2sdev->out.substream != substream)
		return 0;
	return i2sbus_pcm_pointer(i2sdev, 0);
}

static struct snd_pcm_ops i2sbus_playback_ops = {
	.open =		i2sbus_playback_open,
	.close =	i2sbus_playback_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	i2sbus_hw_params,
	.hw_free =	i2sbus_hw_free,
	.prepare =	i2sbus_playback_prepare,
	.trigger =	i2sbus_playback_trigger,
	.pointer =	i2sbus_playback_pointer,
};

static int i2sbus_record_open(struct snd_pcm_substream *substream)
{
	struct i2sbus_dev *i2sdev = snd_pcm_substream_chip(substream);

	if (!i2sdev)
		return -EINVAL;
	i2sdev->in.substream = substream;
	return i2sbus_pcm_open(i2sdev, 1);
}

static int i2sbus_record_close(struct snd_pcm_substream *substream)
{
	struct i2sbus_dev *i2sdev = snd_pcm_substream_chip(substream);
	int err;

	if (!i2sdev)
		return -EINVAL;
	if (i2sdev->in.substream != substream)
		return -EINVAL;
	err = i2sbus_pcm_close(i2sdev, 1);
	if (!err)
		i2sdev->in.substream = NULL;
	return err;
}

static int i2sbus_record_prepare(struct snd_pcm_substream *substream)
{
	struct i2sbus_dev *i2sdev = snd_pcm_substream_chip(substream);

	if (!i2sdev)
		return -EINVAL;
	if (i2sdev->in.substream != substream)
		return -EINVAL;
	return i2sbus_pcm_prepare(i2sdev, 1);
}

static int i2sbus_record_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct i2sbus_dev *i2sdev = snd_pcm_substream_chip(substream);

	if (!i2sdev)
		return -EINVAL;
	if (i2sdev->in.substream != substream)
		return -EINVAL;
	return i2sbus_pcm_trigger(i2sdev, 1, cmd);
}

static snd_pcm_uframes_t i2sbus_record_pointer(struct snd_pcm_substream
					       *substream)
{
	struct i2sbus_dev *i2sdev = snd_pcm_substream_chip(substream);

	if (!i2sdev)
		return -EINVAL;
	if (i2sdev->in.substream != substream)
		return 0;
	return i2sbus_pcm_pointer(i2sdev, 1);
}

static struct snd_pcm_ops i2sbus_record_ops = {
	.open =		i2sbus_record_open,
	.close =	i2sbus_record_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	i2sbus_hw_params,
	.hw_free =	i2sbus_hw_free,
	.prepare =	i2sbus_record_prepare,
	.trigger =	i2sbus_record_trigger,
	.pointer =	i2sbus_record_pointer,
};

static void i2sbus_private_free(struct snd_pcm *pcm)
{
	struct i2sbus_dev *i2sdev = snd_pcm_chip(pcm);
	struct codec_info_item *p, *tmp;

	i2sdev->sound.pcm = NULL;
	i2sdev->out.created = 0;
	i2sdev->in.created = 0;
	list_for_each_entry_safe(p, tmp, &i2sdev->sound.codec_list, list) {
		printk(KERN_ERR "i2sbus: a codec didn't unregister!\n");
		list_del(&p->list);
		module_put(p->codec->owner);
		kfree(p);
	}
	soundbus_dev_put(&i2sdev->sound);
	module_put(THIS_MODULE);
}

/* FIXME: this function needs an error handling strategy with labels */
int
i2sbus_attach_codec(struct soundbus_dev *dev, struct snd_card *card,
		    struct codec_info *ci, void *data)
{
	int err, in = 0, out = 0;
	struct transfer_info *tmp;
	struct i2sbus_dev *i2sdev = soundbus_dev_to_i2sbus_dev(dev);
	struct codec_info_item *cii;

	if (!dev->pcmname || dev->pcmid == -1) {
		printk(KERN_ERR "i2sbus: pcm name and id must be set!\n");
		return -EINVAL;
	}

	list_for_each_entry(cii, &dev->codec_list, list) {
		if (cii->codec_data == data)
			return -EALREADY;
	}

	if (!ci->transfers || !ci->transfers->formats
	    || !ci->transfers->rates || !ci->usable)
		return -EINVAL;

	/* we currently code the i2s transfer on the clock, and support only
	 * 32 and 64 */
	if (ci->bus_factor != 32 && ci->bus_factor != 64)
		return -EINVAL;

	/* If you want to fix this, you need to keep track of what transport infos
	 * are to be used, which codecs they belong to, and then fix all the
	 * sysclock/busclock stuff above to depend on which is usable */
	list_for_each_entry(cii, &dev->codec_list, list) {
		if (cii->codec->sysclock_factor != ci->sysclock_factor) {
			printk(KERN_DEBUG
			       "cannot yet handle multiple different sysclocks!\n");
			return -EINVAL;
		}
		if (cii->codec->bus_factor != ci->bus_factor) {
			printk(KERN_DEBUG
			       "cannot yet handle multiple different bus clocks!\n");
			return -EINVAL;
		}
	}

	tmp = ci->transfers;
	while (tmp->formats && tmp->rates) {
		if (tmp->transfer_in)
			in = 1;
		else
			out = 1;
		tmp++;
	}

	cii = kzalloc(sizeof(struct codec_info_item), GFP_KERNEL);
	if (!cii) {
		printk(KERN_DEBUG "i2sbus: failed to allocate cii\n");
		return -ENOMEM;
	}

	/* use the private data to point to the codec info */
	cii->sdev = soundbus_dev_get(dev);
	cii->codec = ci;
	cii->codec_data = data;

	if (!cii->sdev) {
		printk(KERN_DEBUG
		       "i2sbus: failed to get soundbus dev reference\n");
		kfree(cii);
		return -ENODEV;
	}

	if (!try_module_get(THIS_MODULE)) {
		printk(KERN_DEBUG "i2sbus: failed to get module reference!\n");
		soundbus_dev_put(dev);
		kfree(cii);
		return -EBUSY;
	}

	if (!try_module_get(ci->owner)) {
		printk(KERN_DEBUG
		       "i2sbus: failed to get module reference to codec owner!\n");
		module_put(THIS_MODULE);
		soundbus_dev_put(dev);
		kfree(cii);
		return -EBUSY;
	}

	if (!dev->pcm) {
		err = snd_pcm_new(card,
				  dev->pcmname,
				  dev->pcmid,
				  0,
				  0,
				  &dev->pcm);
		if (err) {
			printk(KERN_DEBUG "i2sbus: failed to create pcm\n");
			kfree(cii);
			module_put(ci->owner);
			soundbus_dev_put(dev);
			module_put(THIS_MODULE);
			return err;
		}
	}

	/* ALSA yet again sucks.
	 * If it is ever fixed, remove this line. See below. */
	out = in = 1;

	if (!i2sdev->out.created && out) {
		if (dev->pcm->card != card) {
			/* eh? */
			printk(KERN_ERR
			       "Can't attach same bus to different cards!\n");
			module_put(ci->owner);
			kfree(cii);
			soundbus_dev_put(dev);
			module_put(THIS_MODULE);
			return -EINVAL;
		}
		if ((err =
		     snd_pcm_new_stream(dev->pcm, SNDRV_PCM_STREAM_PLAYBACK, 1))) {
			module_put(ci->owner);
			kfree(cii);
			soundbus_dev_put(dev);
			module_put(THIS_MODULE);
			return err;
		}
		snd_pcm_set_ops(dev->pcm, SNDRV_PCM_STREAM_PLAYBACK,
				&i2sbus_playback_ops);
		i2sdev->out.created = 1;
	}

	if (!i2sdev->in.created && in) {
		if (dev->pcm->card != card) {
			printk(KERN_ERR
			       "Can't attach same bus to different cards!\n");
			module_put(ci->owner);
			kfree(cii);
			soundbus_dev_put(dev);
			module_put(THIS_MODULE);
			return -EINVAL;
		}
		if ((err =
		     snd_pcm_new_stream(dev->pcm, SNDRV_PCM_STREAM_CAPTURE, 1))) {
			module_put(ci->owner);
			kfree(cii);
			soundbus_dev_put(dev);
			module_put(THIS_MODULE);
			return err;
		}
		snd_pcm_set_ops(dev->pcm, SNDRV_PCM_STREAM_CAPTURE,
				&i2sbus_record_ops);
		i2sdev->in.created = 1;
	}

	/* so we have to register the pcm after adding any substream
	 * to it because alsa doesn't create the devices for the
	 * substreams when we add them later.
	 * Therefore, force in and out on both busses (above) and
	 * register the pcm now instead of just after creating it.
	 */
	err = snd_device_register(card, dev->pcm);
	if (err) {
		printk(KERN_ERR "i2sbus: error registering new pcm\n");
		module_put(ci->owner);
		kfree(cii);
		soundbus_dev_put(dev);
		module_put(THIS_MODULE);
		return err;
	}
	/* no errors any more, so let's add this to our list */
	list_add(&cii->list, &dev->codec_list);

	dev->pcm->private_data = i2sdev;
	dev->pcm->private_free = i2sbus_private_free;

	/* well, we really should support scatter/gather DMA */
	snd_pcm_lib_preallocate_pages_for_all(
		dev->pcm, SNDRV_DMA_TYPE_DEV,
		snd_dma_pci_data(macio_get_pci_dev(i2sdev->macio)),
		64 * 1024, 64 * 1024);

	return 0;
}

void i2sbus_detach_codec(struct soundbus_dev *dev, void *data)
{
	struct codec_info_item *cii = NULL, *i;

	list_for_each_entry(i, &dev->codec_list, list) {
		if (i->codec_data == data) {
			cii = i;
			break;
		}
	}
	if (cii) {
		list_del(&cii->list);
		module_put(cii->codec->owner);
		kfree(cii);
	}
	/* no more codecs, but still a pcm? */
	if (list_empty(&dev->codec_list) && dev->pcm) {
		/* the actual cleanup is done by the callback above! */
		snd_device_free(dev->pcm->card, dev->pcm);
	}
}
