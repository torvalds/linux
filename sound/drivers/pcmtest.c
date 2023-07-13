// SPDX-License-Identifier: GPL-2.0
/*
 * Virtual ALSA driver for PCM testing/fuzzing
 *
 * Copyright 2023 Ivan Orlov <ivan.orlov0322@gmail.com>
 *
 * This is a simple virtual ALSA driver, which can be used for audio applications/PCM middle layer
 * testing or fuzzing.
 * It can:
 *	- Simulate 'playback' and 'capture' actions
 *	- Generate random or pattern-based capture data
 *	- Check playback buffer for containing looped template, and notify about the results
 *	through the debugfs entry
 *	- Inject delays into the playback and capturing processes. See 'inject_delay' parameter.
 *	- Inject errors during the PCM callbacks.
 *	- Register custom RESET ioctl and notify when it is called through the debugfs entry
 *	- Work in interleaved and non-interleaved modes
 *	- Support up to 8 substreams
 *	- Support up to 4 channels
 *	- Support framerates from 8 kHz to 48 kHz
 *
 * When driver works in the capture mode with multiple channels, it duplicates the looped
 * pattern to each separate channel. For example, if we have 2 channels, format = U8, interleaved
 * access mode and pattern 'abacaba', the DMA buffer will look like aabbccaabbaaaa..., so buffer for
 * each channel will contain abacabaabacaba... Same for the non-interleaved mode.
 *
 * However, it may break the capturing on the higher framerates with small period size, so it is
 * better to choose larger period sizes.
 *
 * You can find the corresponding selftest in the 'alsa' selftests folder.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <sound/pcm.h>
#include <sound/core.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/timer.h>
#include <linux/random.h>
#include <linux/debugfs.h>
#include <linux/delay.h>

#define DEVNAME "pcmtestd"
#define CARD_NAME "pcm-test-card"
#define TIMER_PER_SEC 5
#define TIMER_INTERVAL (HZ / TIMER_PER_SEC)
#define DELAY_JIFFIES HZ
#define PLAYBACK_SUBSTREAM_CNT	8
#define CAPTURE_SUBSTREAM_CNT	8
#define MAX_CHANNELS_NUM	4

#define DEFAULT_PATTERN		"abacaba"
#define DEFAULT_PATTERN_LEN	7

#define FILL_MODE_RAND	0
#define FILL_MODE_PAT	1

#define MAX_PATTERN_LEN 4096

static int index = -1;
static char *id = "pcmtest";
static bool enable = true;
static int inject_delay;
static bool inject_hwpars_err;
static bool inject_prepare_err;
static bool inject_trigger_err;
static bool inject_open_err;

static short fill_mode = FILL_MODE_PAT;

static u8 playback_capture_test;
static u8 ioctl_reset_test;
static struct dentry *driver_debug_dir;

module_param(index, int, 0444);
MODULE_PARM_DESC(index, "Index value for " CARD_NAME " soundcard");
module_param(id, charp, 0444);
MODULE_PARM_DESC(id, "ID string for " CARD_NAME " soundcard");
module_param(enable, bool, 0444);
MODULE_PARM_DESC(enable, "Enable " CARD_NAME " soundcard.");
module_param(fill_mode, short, 0600);
MODULE_PARM_DESC(fill_mode, "Buffer fill mode: rand(0) or pattern(1)");
module_param(inject_delay, int, 0600);
MODULE_PARM_DESC(inject_delay, "Inject delays during playback/capture (in jiffies)");
module_param(inject_hwpars_err, bool, 0600);
MODULE_PARM_DESC(inject_hwpars_err, "Inject EBUSY error in the 'hw_params' callback");
module_param(inject_prepare_err, bool, 0600);
MODULE_PARM_DESC(inject_prepare_err, "Inject EINVAL error in the 'prepare' callback");
module_param(inject_trigger_err, bool, 0600);
MODULE_PARM_DESC(inject_trigger_err, "Inject EINVAL error in the 'trigger' callback");
module_param(inject_open_err, bool, 0600);
MODULE_PARM_DESC(inject_open_err, "Inject EBUSY error in the 'open' callback");


struct pcmtst {
	struct snd_pcm *pcm;
	struct snd_card *card;
	struct platform_device *pdev;
};

struct pcmtst_buf_iter {
	size_t buf_pos;				// position in the DMA buffer
	size_t period_pos;			// period-relative position
	size_t b_rw;				// Bytes to write on every timer tick
	size_t s_rw_ch;				// Samples to write to one channel on every tick
	unsigned int sample_bytes;		// sample_bits / 8
	bool is_buf_corrupted;			// playback test result indicator
	size_t period_bytes;			// bytes in a one period
	bool interleaved;			// Interleaved/Non-interleaved mode
	size_t total_bytes;			// Total bytes read/written
	size_t chan_block;			// Bytes in one channel buffer when non-interleaved
	struct snd_pcm_substream *substream;
	struct timer_list timer_instance;
};

static struct snd_pcm_hardware snd_pcmtst_hw = {
	.info = (SNDRV_PCM_INFO_INTERLEAVED |
		 SNDRV_PCM_INFO_BLOCK_TRANSFER |
		 SNDRV_PCM_INFO_NONINTERLEAVED |
		 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_8000_48000,
	.rate_min =		8000,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		MAX_CHANNELS_NUM,
	.buffer_bytes_max =	128 * 1024,
	.period_bytes_min =	4096,
	.period_bytes_max =	32768,
	.periods_min =		1,
	.periods_max =		1024,
};

struct pattern_buf {
	char *buf;
	u32 len;
};

static int buf_allocated;
static struct pattern_buf patt_bufs[MAX_CHANNELS_NUM];

static inline void inc_buf_pos(struct pcmtst_buf_iter *v_iter, size_t by, size_t bytes)
{
	v_iter->total_bytes += by;
	v_iter->buf_pos += by;
	if (v_iter->buf_pos >= bytes)
		v_iter->buf_pos %= bytes;
}

/*
 * Position in the DMA buffer when we are in the non-interleaved mode. We increment buf_pos
 * every time we write a byte to any channel, so the position in the current channel buffer is
 * (position in the DMA buffer) / count_of_channels + size_of_channel_buf * current_channel
 */
static inline size_t buf_pos_n(struct pcmtst_buf_iter *v_iter, unsigned int channels,
			       unsigned int chan_num)
{
	return v_iter->buf_pos / channels + v_iter->chan_block * chan_num;
}

/*
 * Get the count of bytes written for the current channel in the interleaved mode.
 * This is (count of samples written for the current channel) * bytes_in_sample +
 * (relative position in the current sample)
 */
static inline size_t ch_pos_i(size_t b_total, unsigned int channels, unsigned int b_sample)
{
	return b_total / channels / b_sample * b_sample + (b_total % b_sample);
}

static void check_buf_block_i(struct pcmtst_buf_iter *v_iter, struct snd_pcm_runtime *runtime)
{
	size_t i;
	short ch_num;
	u8 current_byte;

	for (i = 0; i < v_iter->b_rw; i++) {
		current_byte = runtime->dma_area[v_iter->buf_pos];
		if (!current_byte)
			break;
		ch_num = (v_iter->total_bytes / v_iter->sample_bytes) % runtime->channels;
		if (current_byte != patt_bufs[ch_num].buf[ch_pos_i(v_iter->total_bytes,
								   runtime->channels,
								   v_iter->sample_bytes)
							  % patt_bufs[ch_num].len]) {
			v_iter->is_buf_corrupted = true;
			break;
		}
		inc_buf_pos(v_iter, 1, runtime->dma_bytes);
	}
	// If we broke during the loop, add remaining bytes to the buffer position.
	inc_buf_pos(v_iter, v_iter->b_rw - i, runtime->dma_bytes);
}

static void check_buf_block_ni(struct pcmtst_buf_iter *v_iter, struct snd_pcm_runtime *runtime)
{
	unsigned int channels = runtime->channels;
	size_t i;
	short ch_num;
	u8 current_byte;

	for (i = 0; i < v_iter->b_rw; i++) {
		ch_num = i % channels;
		current_byte = runtime->dma_area[buf_pos_n(v_iter, channels, ch_num)];
		if (!current_byte)
			break;
		if (current_byte != patt_bufs[ch_num].buf[(v_iter->total_bytes / channels)
							  % patt_bufs[ch_num].len]) {
			v_iter->is_buf_corrupted = true;
			break;
		}
		inc_buf_pos(v_iter, 1, runtime->dma_bytes);
	}
	inc_buf_pos(v_iter, v_iter->b_rw - i, runtime->dma_bytes);
}

/*
 * Check one block of the buffer. Here we iterate the buffer until we find '0'. This condition is
 * necessary because we need to detect when the reading/writing ends, so we assume that the pattern
 * doesn't contain zeros.
 */
static void check_buf_block(struct pcmtst_buf_iter *v_iter, struct snd_pcm_runtime *runtime)
{
	if (v_iter->interleaved)
		check_buf_block_i(v_iter, runtime);
	else
		check_buf_block_ni(v_iter, runtime);
}

/*
 * Fill buffer in the non-interleaved mode. The order of samples is C0, ..., C0, C1, ..., C1, C2...
 * The channel buffers lay in the DMA buffer continuously (see default copy_user and copy_kernel
 * handlers in the pcm_lib.c file).
 *
 * Here we increment the DMA buffer position every time we write a byte to any channel 'buffer'.
 * We need this to simulate the correct hardware pointer moving.
 */
static void fill_block_pattern_n(struct pcmtst_buf_iter *v_iter, struct snd_pcm_runtime *runtime)
{
	size_t i;
	unsigned int channels = runtime->channels;
	short ch_num;

	for (i = 0; i < v_iter->b_rw; i++) {
		ch_num = i % channels;
		runtime->dma_area[buf_pos_n(v_iter, channels, ch_num)] =
			patt_bufs[ch_num].buf[(v_iter->total_bytes / channels)
					      % patt_bufs[ch_num].len];
		inc_buf_pos(v_iter, 1, runtime->dma_bytes);
	}
}

// Fill buffer in the interleaved mode. The order of samples is C0, C1, C2, C0, C1, C2, ...
static void fill_block_pattern_i(struct pcmtst_buf_iter *v_iter, struct snd_pcm_runtime *runtime)
{
	size_t sample;
	size_t pos_in_ch, pos_pattern;
	short ch, pos_sample;

	pos_in_ch = ch_pos_i(v_iter->total_bytes, runtime->channels, v_iter->sample_bytes);

	for (sample = 0; sample < v_iter->s_rw_ch; sample++) {
		for (ch = 0; ch < runtime->channels; ch++) {
			for (pos_sample = 0; pos_sample < v_iter->sample_bytes; pos_sample++) {
				pos_pattern = (pos_in_ch + sample * v_iter->sample_bytes
					      + pos_sample) % patt_bufs[ch].len;
				runtime->dma_area[v_iter->buf_pos] = patt_bufs[ch].buf[pos_pattern];
				inc_buf_pos(v_iter, 1, runtime->dma_bytes);
			}
		}
	}
}

static void fill_block_pattern(struct pcmtst_buf_iter *v_iter, struct snd_pcm_runtime *runtime)
{
	if (v_iter->interleaved)
		fill_block_pattern_i(v_iter, runtime);
	else
		fill_block_pattern_n(v_iter, runtime);
}

static void fill_block_rand_n(struct pcmtst_buf_iter *v_iter, struct snd_pcm_runtime *runtime)
{
	unsigned int channels = runtime->channels;
	// Remaining space in all channel buffers
	size_t bytes_remain = runtime->dma_bytes - v_iter->buf_pos;
	unsigned int i;

	for (i = 0; i < channels; i++) {
		if (v_iter->b_rw <= bytes_remain) {
			//b_rw - count of bytes must be written for all channels at each timer tick
			get_random_bytes(runtime->dma_area + buf_pos_n(v_iter, channels, i),
					 v_iter->b_rw / channels);
		} else {
			// Write to the end of buffer and start from the beginning of it
			get_random_bytes(runtime->dma_area + buf_pos_n(v_iter, channels, i),
					 bytes_remain / channels);
			get_random_bytes(runtime->dma_area + v_iter->chan_block * i,
					 (v_iter->b_rw - bytes_remain) / channels);
		}
	}
	inc_buf_pos(v_iter, v_iter->b_rw, runtime->dma_bytes);
}

static void fill_block_rand_i(struct pcmtst_buf_iter *v_iter, struct snd_pcm_runtime *runtime)
{
	size_t in_cur_block = runtime->dma_bytes - v_iter->buf_pos;

	if (v_iter->b_rw <= in_cur_block) {
		get_random_bytes(&runtime->dma_area[v_iter->buf_pos], v_iter->b_rw);
	} else {
		get_random_bytes(&runtime->dma_area[v_iter->buf_pos], in_cur_block);
		get_random_bytes(runtime->dma_area, v_iter->b_rw - in_cur_block);
	}
	inc_buf_pos(v_iter, v_iter->b_rw, runtime->dma_bytes);
}

static void fill_block_random(struct pcmtst_buf_iter *v_iter, struct snd_pcm_runtime *runtime)
{
	if (v_iter->interleaved)
		fill_block_rand_i(v_iter, runtime);
	else
		fill_block_rand_n(v_iter, runtime);
}

static void fill_block(struct pcmtst_buf_iter *v_iter, struct snd_pcm_runtime *runtime)
{
	switch (fill_mode) {
	case FILL_MODE_RAND:
		fill_block_random(v_iter, runtime);
		break;
	case FILL_MODE_PAT:
		fill_block_pattern(v_iter, runtime);
		break;
	}
}

/*
 * Here we iterate through the buffer by (buffer_size / iterates_per_second) bytes.
 * The driver uses timer to simulate the hardware pointer moving, and notify the PCM middle layer
 * about period elapsed.
 */
static void timer_timeout(struct timer_list *data)
{
	struct pcmtst_buf_iter *v_iter;
	struct snd_pcm_substream *substream;

	v_iter = from_timer(v_iter, data, timer_instance);
	substream = v_iter->substream;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK && !v_iter->is_buf_corrupted)
		check_buf_block(v_iter, substream->runtime);
	else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		fill_block(v_iter, substream->runtime);
	else
		inc_buf_pos(v_iter, v_iter->b_rw, substream->runtime->dma_bytes);

	v_iter->period_pos += v_iter->b_rw;
	if (v_iter->period_pos >= v_iter->period_bytes) {
		v_iter->period_pos %= v_iter->period_bytes;
		snd_pcm_period_elapsed(substream);
	}
	mod_timer(&v_iter->timer_instance, jiffies + TIMER_INTERVAL + inject_delay);
}

static int snd_pcmtst_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct pcmtst_buf_iter *v_iter;

	if (inject_open_err)
		return -EBUSY;

	v_iter = kzalloc(sizeof(*v_iter), GFP_KERNEL);
	if (!v_iter)
		return -ENOMEM;

	runtime->hw = snd_pcmtst_hw;
	runtime->private_data = v_iter;
	v_iter->substream = substream;
	v_iter->buf_pos = 0;
	v_iter->is_buf_corrupted = false;
	v_iter->period_pos = 0;
	v_iter->total_bytes = 0;

	playback_capture_test = 0;
	ioctl_reset_test = 0;

	timer_setup(&v_iter->timer_instance, timer_timeout, 0);
	mod_timer(&v_iter->timer_instance, jiffies + TIMER_INTERVAL);
	return 0;
}

static int snd_pcmtst_pcm_close(struct snd_pcm_substream *substream)
{
	struct pcmtst_buf_iter *v_iter = substream->runtime->private_data;

	timer_shutdown_sync(&v_iter->timer_instance);
	v_iter->substream = NULL;
	playback_capture_test = !v_iter->is_buf_corrupted;
	kfree(v_iter);
	return 0;
}

static int snd_pcmtst_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct pcmtst_buf_iter *v_iter = runtime->private_data;

	if (inject_trigger_err)
		return -EINVAL;

	v_iter->sample_bytes = runtime->sample_bits / 8;
	v_iter->period_bytes = frames_to_bytes(runtime, runtime->period_size);
	if (runtime->access == SNDRV_PCM_ACCESS_RW_NONINTERLEAVED ||
	    runtime->access == SNDRV_PCM_ACCESS_MMAP_NONINTERLEAVED) {
		v_iter->chan_block = runtime->dma_bytes / runtime->channels;
		v_iter->interleaved = false;
	} else {
		v_iter->interleaved = true;
	}
	// We want to record RATE * ch_cnt samples per sec, it is rate * sample_bytes * ch_cnt bytes
	v_iter->s_rw_ch = runtime->rate / TIMER_PER_SEC;
	v_iter->b_rw = v_iter->s_rw_ch * v_iter->sample_bytes * runtime->channels;

	return 0;
}

static snd_pcm_uframes_t snd_pcmtst_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct pcmtst_buf_iter *v_iter = substream->runtime->private_data;

	return bytes_to_frames(substream->runtime, v_iter->buf_pos);
}

static int snd_pcmtst_free(struct pcmtst *pcmtst)
{
	if (!pcmtst)
		return 0;
	kfree(pcmtst);
	return 0;
}

// These callbacks are required, but empty - all freeing occurs in pdev_remove
static int snd_pcmtst_dev_free(struct snd_device *device)
{
	return 0;
}

static void pcmtst_pdev_release(struct device *dev)
{
}

static int snd_pcmtst_pcm_prepare(struct snd_pcm_substream *substream)
{
	if (inject_prepare_err)
		return -EINVAL;
	return 0;
}

static int snd_pcmtst_pcm_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params)
{
	if (inject_hwpars_err)
		return -EBUSY;
	return 0;
}

static int snd_pcmtst_pcm_hw_free(struct snd_pcm_substream *substream)
{
	return 0;
}

static int snd_pcmtst_ioctl(struct snd_pcm_substream *substream, unsigned int cmd, void *arg)
{
	switch (cmd) {
	case SNDRV_PCM_IOCTL1_RESET:
		ioctl_reset_test = 1;
		break;
	}
	return snd_pcm_lib_ioctl(substream, cmd, arg);
}

static const struct snd_pcm_ops snd_pcmtst_playback_ops = {
	.open =		snd_pcmtst_pcm_open,
	.close =	snd_pcmtst_pcm_close,
	.trigger =	snd_pcmtst_pcm_trigger,
	.hw_params =	snd_pcmtst_pcm_hw_params,
	.ioctl =	snd_pcmtst_ioctl,
	.hw_free =	snd_pcmtst_pcm_hw_free,
	.prepare =	snd_pcmtst_pcm_prepare,
	.pointer =	snd_pcmtst_pcm_pointer,
};

static const struct snd_pcm_ops snd_pcmtst_capture_ops = {
	.open =		snd_pcmtst_pcm_open,
	.close =	snd_pcmtst_pcm_close,
	.trigger =	snd_pcmtst_pcm_trigger,
	.hw_params =	snd_pcmtst_pcm_hw_params,
	.hw_free =	snd_pcmtst_pcm_hw_free,
	.ioctl =	snd_pcmtst_ioctl,
	.prepare =	snd_pcmtst_pcm_prepare,
	.pointer =	snd_pcmtst_pcm_pointer,
};

static int snd_pcmtst_new_pcm(struct pcmtst *pcmtst)
{
	struct snd_pcm *pcm;
	int err;

	err = snd_pcm_new(pcmtst->card, "PCMTest", 0, PLAYBACK_SUBSTREAM_CNT,
			  CAPTURE_SUBSTREAM_CNT, &pcm);
	if (err < 0)
		return err;
	pcm->private_data = pcmtst;
	strcpy(pcm->name, "PCMTest");
	pcmtst->pcm = pcm;
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_pcmtst_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_pcmtst_capture_ops);

	err = snd_pcm_set_managed_buffer_all(pcm, SNDRV_DMA_TYPE_DEV, &pcmtst->pdev->dev,
					     0, 128 * 1024);
	return err;
}

static int snd_pcmtst_create(struct snd_card *card, struct platform_device *pdev,
			     struct pcmtst **r_pcmtst)
{
	struct pcmtst *pcmtst;
	int err;
	static const struct snd_device_ops ops = {
		.dev_free = snd_pcmtst_dev_free,
	};

	pcmtst = kzalloc(sizeof(*pcmtst), GFP_KERNEL);
	if (!pcmtst)
		return -ENOMEM;
	pcmtst->card = card;
	pcmtst->pdev = pdev;

	err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, pcmtst, &ops);
	if (err < 0)
		goto _err_free_chip;

	err = snd_pcmtst_new_pcm(pcmtst);
	if (err < 0)
		goto _err_free_chip;

	*r_pcmtst = pcmtst;
	return 0;

_err_free_chip:
	snd_pcmtst_free(pcmtst);
	return err;
}

static int pcmtst_probe(struct platform_device *pdev)
{
	struct snd_card *card;
	struct pcmtst *pcmtst;
	int err;

	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (err)
		return err;

	err = snd_devm_card_new(&pdev->dev, index, id, THIS_MODULE, 0, &card);
	if (err < 0)
		return err;
	err = snd_pcmtst_create(card, pdev, &pcmtst);
	if (err < 0)
		return err;

	strcpy(card->driver, "PCM-TEST Driver");
	strcpy(card->shortname, "PCM-Test");
	strcpy(card->longname, "PCM-Test virtual driver");

	err = snd_card_register(card);
	if (err < 0)
		return err;

	platform_set_drvdata(pdev, pcmtst);

	return 0;
}

static void pdev_remove(struct platform_device *pdev)
{
	struct pcmtst *pcmtst = platform_get_drvdata(pdev);

	snd_pcmtst_free(pcmtst);
}

static struct platform_device pcmtst_pdev = {
	.name =		"pcmtest",
	.dev.release =	pcmtst_pdev_release,
};

static struct platform_driver pcmtst_pdrv = {
	.probe =	pcmtst_probe,
	.remove_new =	pdev_remove,
	.driver =	{
		.name = "pcmtest",
	},
};

static ssize_t pattern_write(struct file *file, const char __user *u_buff, size_t len, loff_t *off)
{
	struct pattern_buf *patt_buf = file->f_inode->i_private;
	ssize_t to_write = len;

	if (*off + to_write > MAX_PATTERN_LEN)
		to_write = MAX_PATTERN_LEN - *off;

	// Crop silently everything over the buffer
	if (to_write <= 0)
		return len;

	if (copy_from_user(patt_buf->buf + *off, u_buff, to_write))
		return -EFAULT;

	patt_buf->len = *off + to_write;
	*off += to_write;

	return to_write;
}

static ssize_t pattern_read(struct file *file, char __user *u_buff, size_t len, loff_t *off)
{
	struct pattern_buf *patt_buf = file->f_inode->i_private;
	ssize_t to_read = len;

	if (*off + to_read >= MAX_PATTERN_LEN)
		to_read = MAX_PATTERN_LEN - *off;
	if (to_read <= 0)
		return 0;

	if (copy_to_user(u_buff, patt_buf->buf + *off, to_read))
		to_read = 0;
	else
		*off += to_read;

	return to_read;
}

static const struct file_operations fill_pattern_fops = {
	.read = pattern_read,
	.write = pattern_write,
};

static int setup_patt_bufs(void)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(patt_bufs); i++) {
		patt_bufs[i].buf = kzalloc(MAX_PATTERN_LEN, GFP_KERNEL);
		if (!patt_bufs[i].buf)
			break;
		strcpy(patt_bufs[i].buf, DEFAULT_PATTERN);
		patt_bufs[i].len = DEFAULT_PATTERN_LEN;
	}

	return i;
}

static const char * const pattern_files[] = { "fill_pattern0", "fill_pattern1",
					      "fill_pattern2", "fill_pattern3"};
static int init_debug_files(int buf_count)
{
	size_t i;
	char len_file_name[32];

	driver_debug_dir = debugfs_create_dir("pcmtest", NULL);
	if (IS_ERR(driver_debug_dir))
		return PTR_ERR(driver_debug_dir);
	debugfs_create_u8("pc_test", 0444, driver_debug_dir, &playback_capture_test);
	debugfs_create_u8("ioctl_test", 0444, driver_debug_dir, &ioctl_reset_test);

	for (i = 0; i < buf_count; i++) {
		debugfs_create_file(pattern_files[i], 0600, driver_debug_dir,
				    &patt_bufs[i], &fill_pattern_fops);
		snprintf(len_file_name, sizeof(len_file_name), "%s_len", pattern_files[i]);
		debugfs_create_u32(len_file_name, 0444, driver_debug_dir, &patt_bufs[i].len);
	}

	return 0;
}

static void free_pattern_buffers(void)
{
	int i;

	for (i = 0; i < buf_allocated; i++)
		kfree(patt_bufs[i].buf);
}

static void clear_debug_files(void)
{
	debugfs_remove_recursive(driver_debug_dir);
}

static int __init mod_init(void)
{
	int err = 0;

	buf_allocated = setup_patt_bufs();
	if (!buf_allocated)
		return -ENOMEM;

	snd_pcmtst_hw.channels_max = buf_allocated;

	err = init_debug_files(buf_allocated);
	if (err)
		return err;
	err = platform_device_register(&pcmtst_pdev);
	if (err)
		return err;
	err = platform_driver_register(&pcmtst_pdrv);
	if (err)
		platform_device_unregister(&pcmtst_pdev);
	return err;
}

static void __exit mod_exit(void)
{
	clear_debug_files();
	free_pattern_buffers();

	platform_driver_unregister(&pcmtst_pdrv);
	platform_device_unregister(&pcmtst_pdev);
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ivan Orlov");
module_init(mod_init);
module_exit(mod_exit);
