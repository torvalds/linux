// SPDX-License-Identifier: GPL-2.0-only
/*
 * i2sbus driver -- pcm routines
 *
 * Copyright 2006 Johannes Berg <johannes@sipsolutions.net>
 */

#include <linux/io.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <asm/macio.h>
#include <linux/pci.h>
#include <linux/module.h>
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
	int bus_factor = 0, sysclock_factor = 0;
	int found_this;

	guard(mutex)(&i2sdev->lock);

	get_pcm_info(i2sdev, in, &pi, &other);

	hw = &pi->substream->runtime->hw;
	sdev = &i2sdev->sound;

	if (pi->active) {
		/* alsa messed up */
		return -EBUSY;
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
	if (!masks_inited || !bus_factor || !sysclock_factor)
		return -ENODEV;
	/* bus dependent stuff */
	hw->info = SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
		   SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_RESUME |
		   SNDRV_PCM_INFO_JOINT_DUPLEX;

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
		hw->formats &= pcm_format_to_bits(i2sdev->format);
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
	err = snd_pcm_hw_constraint_integer(pi->substream->runtime,
					    SNDRV_PCM_HW_PARAM_PERIODS);
	if (err < 0)
		return err;
	list_for_each_entry(cii, &sdev->codec_list, list) {
		if (cii->codec->open) {
			err = cii->codec->open(cii, pi->substream);
			if (err) {
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
				return err;
			}
		}
	}

	return 0;
}

#undef CHECK_RATE

static int i2sbus_pcm_close(struct i2sbus_dev *i2sdev, int in)
{
	struct codec_info_item *cii;
	struct pcm_info *pi;
	int err = 0, tmp;

	guard(mutex)(&i2sdev->lock);

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
	return err;
}

static void i2sbus_wait_for_stop(struct i2sbus_dev *i2sdev,
				 struct pcm_info *pi)
{
	unsigned long flags;
	DECLARE_COMPLETION_ONSTACK(done);
	unsigned long time_left;

	spin_lock_irqsave(&i2sdev->low_lock, flags);
	if (pi->dbdma_ring.stopping) {
		pi->stop_completion = &done;
		spin_unlock_irqrestore(&i2sdev->low_lock, flags);
		time_left = wait_for_completion_timeout(&done, HZ);
		spin_lock_irqsave(&i2sdev->low_lock, flags);
		pi->stop_completion = NULL;
		if (time_left == 0) {
			/* timeout expired, stop dbdma forcefully */
			printk(KERN_ERR "i2sbus_wait_for_stop: timed out\n");
			/* make sure RUN, PAUSE and S0 bits are cleared */
			out_le32(&pi->dbdma->control, (RUN | PAUSE | 1) << 16);
			pi->dbdma_ring.stopping = 0;
			time_left = 10;
			while (in_le32(&pi->dbdma->status) & ACTIVE) {
				if (--time_left <= 0)
					break;
				udelay(1);
			}
		}
	}
	spin_unlock_irqrestore(&i2sdev->low_lock, flags);
}

#ifdef CONFIG_PM
void i2sbus_wait_for_stop_both(struct i2sbus_dev *i2sdev)
{
	struct pcm_info *pi;

	get_pcm_info(i2sdev, 0, &pi, NULL);
	i2sbus_wait_for_stop(i2sdev, pi);
	get_pcm_info(i2sdev, 1, &pi, NULL);
	i2sbus_wait_for_stop(i2sdev, pi);
}
#endif

static inline int i2sbus_hw_free(struct snd_pcm_substream *substream, int in)
{
	struct i2sbus_dev *i2sdev = snd_pcm_substream_chip(substream);
	struct pcm_info *pi;

	get_pcm_info(i2sdev, in, &pi, NULL);
	if (pi->dbdma_ring.stopping)
		i2sbus_wait_for_stop(i2sdev, pi);
	return 0;
}

static int i2sbus_playback_hw_free(struct snd_pcm_substream *substream)
{
	return i2sbus_hw_free(substream, 0);
}

static int i2sbus_record_hw_free(struct snd_pcm_substream *substream)
{
	return i2sbus_hw_free(substream, 1);
}

static int i2sbus_pcm_prepare(struct i2sbus_dev *i2sdev, int in)
{
	/* whee. Hard work now. The user has selected a bitrate
	 * and bit format, so now we have to program our
	 * I2S controller appropriately. */
	struct snd_pcm_runtime *runtime;
	struct dbdma_cmd *command;
	int i, periodsize, nperiods;
	dma_addr_t offset;
	struct bus_info bi;
	struct codec_info_item *cii;
	int sfr = 0;		/* serial format register */
	int dws = 0;		/* data word sizes reg */
	int input_16bit;
	struct pcm_info *pi, *other;
	int cnt;
	unsigned int cmd, stopaddr;

	guard(mutex)(&i2sdev->lock);

	get_pcm_info(i2sdev, in, &pi, &other);

	if (pi->dbdma_ring.running)
		return -EBUSY;
	if (pi->dbdma_ring.stopping)
		i2sbus_wait_for_stop(i2sdev, pi);

	if (!pi->substream || !pi->substream->runtime)
		return -EINVAL;

	runtime = pi->substream->runtime;
	pi->active = 1;
	if (other->active &&
	    ((i2sdev->format != runtime->format)
	     || (i2sdev->rate != runtime->rate)))
		return -EINVAL;

	i2sdev->format = runtime->format;
	i2sdev->rate = runtime->rate;

	periodsize = snd_pcm_lib_period_bytes(pi->substream);
	nperiods = pi->substream->runtime->periods;
	pi->current_period = 0;

	/* generate dbdma command ring first */
	command = pi->dbdma_ring.cmds;
	memset(command, 0, (nperiods + 2) * sizeof(struct dbdma_cmd));

	/* commands to DMA to/from the ring */
	/*
	 * For input, we need to do a graceful stop; if we abort
	 * the DMA, we end up with leftover bytes that corrupt
	 * the next recording.  To do this we set the S0 status
	 * bit and wait for the DMA controller to stop.  Each
	 * command has a branch condition to
	 * make it branch to a stop command if S0 is set.
	 * On input we also need to wait for the S7 bit to be
	 * set before turning off the DMA controller.
	 * In fact we do the graceful stop for output as well.
	 */
	offset = runtime->dma_addr;
	cmd = (in? INPUT_MORE: OUTPUT_MORE) | BR_IFSET | INTR_ALWAYS;
	stopaddr = pi->dbdma_ring.bus_cmd_start +
		(nperiods + 1) * sizeof(struct dbdma_cmd);
	for (i = 0; i < nperiods; i++, command++, offset += periodsize) {
		command->command = cpu_to_le16(cmd);
		command->cmd_dep = cpu_to_le32(stopaddr);
		command->phy_addr = cpu_to_le32(offset);
		command->req_count = cpu_to_le16(periodsize);
	}

	/* branch back to beginning of ring */
	command->command = cpu_to_le16(DBDMA_NOP | BR_ALWAYS);
	command->cmd_dep = cpu_to_le32(pi->dbdma_ring.bus_cmd_start);
	command++;

	/* set stop command */
	command->command = cpu_to_le16(DBDMA_STOP);

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
		if (!bi.bus_factor)
			return -ENODEV;
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
		return -EINVAL;
	}
	/* we assume all sysclocks are the same! */
	list_for_each_entry(cii, &i2sdev->sound.codec_list, list) {
		bi.sysclock_factor = cii->codec->sysclock_factor;
		break;
	}

	if (clock_and_divisors(bi.sysclock_factor,
			       bi.bus_factor,
			       runtime->rate,
			       &sfr) < 0)
		return -EINVAL;
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
		if (err)
			return err;
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
		return 0;

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

	return 0;
}

#ifdef CONFIG_PM
void i2sbus_pcm_prepare_both(struct i2sbus_dev *i2sdev)
{
	i2sbus_pcm_prepare(i2sdev, 0);
	i2sbus_pcm_prepare(i2sdev, 1);
}
#endif

static int i2sbus_pcm_trigger(struct i2sbus_dev *i2sdev, int in, int cmd)
{
	struct codec_info_item *cii;
	struct pcm_info *pi;

	guard(spinlock_irqsave)(&i2sdev->low_lock);

	get_pcm_info(i2sdev, in, &pi, NULL);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		if (pi->dbdma_ring.running)
			return -EALREADY;
		list_for_each_entry(cii, &i2sdev->sound.codec_list, list)
			if (cii->codec->start)
				cii->codec->start(cii, pi->substream);
		pi->dbdma_ring.running = 1;

		if (pi->dbdma_ring.stopping) {
			/* Clear the S0 bit, then see if we stopped yet */
			out_le32(&pi->dbdma->control, 1 << 16);
			if (in_le32(&pi->dbdma->status) & ACTIVE) {
				/* possible race here? */
				udelay(10);
				if (in_le32(&pi->dbdma->status) & ACTIVE) {
					pi->dbdma_ring.stopping = 0;
					return 0; /* keep running */
				}
			}
		}

		/* make sure RUN, PAUSE and S0 bits are cleared */
		out_le32(&pi->dbdma->control, (RUN | PAUSE | 1) << 16);

		/* set branch condition select register */
		out_le32(&pi->dbdma->br_sel, (1 << 16) | 1);

		/* write dma command buffer address to the dbdma chip */
		out_le32(&pi->dbdma->cmdptr, pi->dbdma_ring.bus_cmd_start);

		/* initialize the frame count and current period */
		pi->current_period = 0;
		pi->frame_count = in_le32(&i2sdev->intfregs->frame_count);

		/* set the DMA controller running */
		out_le32(&pi->dbdma->control, (RUN << 16) | RUN);

		/* off you go! */
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		if (!pi->dbdma_ring.running)
			return -EALREADY;
		pi->dbdma_ring.running = 0;

		/* Set the S0 bit to make the DMA branch to the stop cmd */
		out_le32(&pi->dbdma->control, (1 << 16) | 1);
		pi->dbdma_ring.stopping = 1;

		list_for_each_entry(cii, &i2sdev->sound.codec_list, list)
			if (cii->codec->stop)
				cii->codec->stop(cii, pi->substream);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static snd_pcm_uframes_t i2sbus_pcm_pointer(struct i2sbus_dev *i2sdev, int in)
{
	struct pcm_info *pi;
	u32 fc;

	get_pcm_info(i2sdev, in, &pi, NULL);

	fc = in_le32(&i2sdev->intfregs->frame_count);
	fc = fc - pi->frame_count;

	if (fc >= pi->substream->runtime->buffer_size)
		fc %= pi->substream->runtime->buffer_size;
	return fc;
}

static inline void handle_interrupt(struct i2sbus_dev *i2sdev, int in)
{
	struct pcm_info *pi;
	u32 fc, nframes;
	u32 status;
	int timeout, i;
	int dma_stopped = 0;
	struct snd_pcm_runtime *runtime;

	scoped_guard(spinlock, &i2sdev->low_lock) {
		get_pcm_info(i2sdev, in, &pi, NULL);
		if (!pi->dbdma_ring.running && !pi->dbdma_ring.stopping)
			return;

		i = pi->current_period;
		runtime = pi->substream->runtime;
		while (pi->dbdma_ring.cmds[i].xfer_status) {
			if (le16_to_cpu(pi->dbdma_ring.cmds[i].xfer_status) & BT)
				/*
				 * BT is the branch taken bit.  If it took a branch
				 * it is because we set the S0 bit to make it
				 * branch to the stop command.
				 */
				dma_stopped = 1;
			pi->dbdma_ring.cmds[i].xfer_status = 0;

			if (++i >= runtime->periods) {
				i = 0;
				pi->frame_count += runtime->buffer_size;
			}
			pi->current_period = i;

			/*
			 * Check the frame count.  The DMA tends to get a bit
			 * ahead of the frame counter, which confuses the core.
			 */
			fc = in_le32(&i2sdev->intfregs->frame_count);
			nframes = i * runtime->period_size;
			if (fc < pi->frame_count + nframes)
				pi->frame_count = fc - nframes;
		}

		if (dma_stopped) {
			timeout = 1000;
			for (;;) {
				status = in_le32(&pi->dbdma->status);
				if (!(status & ACTIVE) && (!in || (status & 0x80)))
					break;
				if (--timeout <= 0) {
					printk(KERN_ERR
					       "i2sbus: timed out waiting for DMA to stop!\n");
					break;
				}
				udelay(1);
			}

			/* Turn off DMA controller, clear S0 bit */
			out_le32(&pi->dbdma->control, (RUN | PAUSE | 1) << 16);

			pi->dbdma_ring.stopping = 0;
			if (pi->stop_completion)
				complete(pi->stop_completion);
		}

		if (!pi->dbdma_ring.running)
			return;
	}

	/* may call _trigger again, hence needs to be unlocked */
	snd_pcm_period_elapsed(pi->substream);
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

static const struct snd_pcm_ops i2sbus_playback_ops = {
	.open =		i2sbus_playback_open,
	.close =	i2sbus_playback_close,
	.hw_free =	i2sbus_playback_hw_free,
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

static const struct snd_pcm_ops i2sbus_record_ops = {
	.open =		i2sbus_record_open,
	.close =	i2sbus_record_close,
	.hw_free =	i2sbus_record_hw_free,
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
	if (!cii)
		return -ENOMEM;

	/* use the private data to point to the codec info */
	cii->sdev = soundbus_dev_get(dev);
	cii->codec = ci;
	cii->codec_data = data;

	if (!cii->sdev) {
		printk(KERN_DEBUG
		       "i2sbus: failed to get soundbus dev reference\n");
		err = -ENODEV;
		goto out_free_cii;
	}

	if (!try_module_get(THIS_MODULE)) {
		printk(KERN_DEBUG "i2sbus: failed to get module reference!\n");
		err = -EBUSY;
		goto out_put_sdev;
	}

	if (!try_module_get(ci->owner)) {
		printk(KERN_DEBUG
		       "i2sbus: failed to get module reference to codec owner!\n");
		err = -EBUSY;
		goto out_put_this_module;
	}

	if (!dev->pcm) {
		err = snd_pcm_new(card, dev->pcmname, dev->pcmid, 0, 0,
				  &dev->pcm);
		if (err) {
			printk(KERN_DEBUG "i2sbus: failed to create pcm\n");
			goto out_put_ci_module;
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
			err = -EINVAL;
			goto out_put_ci_module;
		}
		err = snd_pcm_new_stream(dev->pcm, SNDRV_PCM_STREAM_PLAYBACK, 1);
		if (err)
			goto out_put_ci_module;
		snd_pcm_set_ops(dev->pcm, SNDRV_PCM_STREAM_PLAYBACK,
				&i2sbus_playback_ops);
		dev->pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].dev->parent =
			&dev->ofdev.dev;
		i2sdev->out.created = 1;
	}

	if (!i2sdev->in.created && in) {
		if (dev->pcm->card != card) {
			printk(KERN_ERR
			       "Can't attach same bus to different cards!\n");
			err = -EINVAL;
			goto out_put_ci_module;
		}
		err = snd_pcm_new_stream(dev->pcm, SNDRV_PCM_STREAM_CAPTURE, 1);
		if (err)
			goto out_put_ci_module;
		snd_pcm_set_ops(dev->pcm, SNDRV_PCM_STREAM_CAPTURE,
				&i2sbus_record_ops);
		dev->pcm->streams[SNDRV_PCM_STREAM_CAPTURE].dev->parent =
			&dev->ofdev.dev;
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
		goto out_put_ci_module;
	}
	/* no errors any more, so let's add this to our list */
	list_add(&cii->list, &dev->codec_list);

	dev->pcm->private_data = i2sdev;
	dev->pcm->private_free = i2sbus_private_free;

	/* well, we really should support scatter/gather DMA */
	snd_pcm_set_managed_buffer_all(
		dev->pcm, SNDRV_DMA_TYPE_DEV,
		&macio_get_pci_dev(i2sdev->macio)->dev,
		64 * 1024, 64 * 1024);

	return 0;
 out_put_ci_module:
	module_put(ci->owner);
 out_put_this_module:
	module_put(THIS_MODULE);
 out_put_sdev:
	soundbus_dev_put(dev);
 out_free_cii:
	kfree(cii);
	return err;
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
