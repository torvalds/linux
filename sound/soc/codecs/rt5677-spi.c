// SPDX-License-Identifier: GPL-2.0-only
/*
 * rt5677-spi.c  --  RT5677 ALSA SoC audio codec driver
 *
 * Copyright 2013 Realtek Semiconductor Corp.
 * Author: Oder Chiou <oder_chiou@realtek.com>
 */

#include <linux/module.h>
#include <linux/input.h>
#include <linux/spi/spi.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/regulator/consumer.h>
#include <linux/pm_qos.h>
#include <linux/sysfs.h>
#include <linux/clk.h>
#include <linux/firmware.h>
#include <linux/acpi.h>

#include <sound/soc.h>

#include "rt5677.h"
#include "rt5677-spi.h"

#define DRV_NAME "rt5677spi"

#define RT5677_SPI_BURST_LEN	240
#define RT5677_SPI_HEADER	5
#define RT5677_SPI_FREQ		6000000

/* The AddressPhase and DataPhase of SPI commands are MSB first on the wire.
 * DataPhase word size of 16-bit commands is 2 bytes.
 * DataPhase word size of 32-bit commands is 4 bytes.
 * DataPhase word size of burst commands is 8 bytes.
 * The DSP CPU is little-endian.
 */
#define RT5677_SPI_WRITE_BURST	0x5
#define RT5677_SPI_READ_BURST	0x4
#define RT5677_SPI_WRITE_32	0x3
#define RT5677_SPI_READ_32	0x2
#define RT5677_SPI_WRITE_16	0x1
#define RT5677_SPI_READ_16	0x0

#define RT5677_BUF_BYTES_TOTAL		0x20000
#define RT5677_MIC_BUF_ADDR		0x60030000
#define RT5677_MODEL_ADDR		0x5FFC9800
#define RT5677_MIC_BUF_BYTES		((u32)(RT5677_BUF_BYTES_TOTAL - \
					sizeof(u32)))
#define RT5677_MIC_BUF_FIRST_READ_SIZE	0x10000

static struct spi_device *g_spi;
static DEFINE_MUTEX(spi_mutex);

struct rt5677_dsp {
	struct device *dev;
	struct delayed_work copy_work;
	struct mutex dma_lock;
	struct snd_pcm_substream *substream;
	size_t dma_offset;	/* zero-based offset into runtime->dma_area */
	size_t avail_bytes;	/* number of new bytes since last period */
	u32 mic_read_offset;	/* zero-based offset into DSP's mic buffer */
	bool new_hotword;	/* a new hotword is fired */
};

static const struct snd_pcm_hardware rt5677_spi_pcm_hardware = {
	.info			= SNDRV_PCM_INFO_MMAP |
				  SNDRV_PCM_INFO_MMAP_VALID |
				  SNDRV_PCM_INFO_INTERLEAVED,
	.formats		= SNDRV_PCM_FMTBIT_S16_LE,
	.period_bytes_min	= PAGE_SIZE,
	.period_bytes_max	= RT5677_BUF_BYTES_TOTAL / 8,
	.periods_min		= 8,
	.periods_max		= 8,
	.channels_min		= 1,
	.channels_max		= 1,
	.buffer_bytes_max	= RT5677_BUF_BYTES_TOTAL,
};

static struct snd_soc_dai_driver rt5677_spi_dai = {
	/* The DAI name "rt5677-dsp-cpu-dai" is not used. The actual DAI name
	 * registered with ASoC is the name of the device "spi-RT5677AA:00",
	 * because we only have one DAI. See snd_soc_register_dais().
	 */
	.name = "rt5677-dsp-cpu-dai",
	.id = 0,
	.capture = {
		.stream_name = "DSP Capture",
		.channels_min = 1,
		.channels_max = 1,
		.rates = SNDRV_PCM_RATE_16000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
};

/* PCM for streaming audio from the DSP buffer */
static int rt5677_spi_pcm_open(
		struct snd_soc_component *component,
		struct snd_pcm_substream *substream)
{
	snd_soc_set_runtime_hwparams(substream, &rt5677_spi_pcm_hardware);
	return 0;
}

static int rt5677_spi_pcm_close(
		struct snd_soc_component *component,
		struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_component *codec_component =
			snd_soc_rtdcom_lookup(rtd, "rt5677");
	struct rt5677_priv *rt5677 =
			snd_soc_component_get_drvdata(codec_component);
	struct rt5677_dsp *rt5677_dsp =
			snd_soc_component_get_drvdata(component);

	cancel_delayed_work_sync(&rt5677_dsp->copy_work);
	rt5677->set_dsp_vad(codec_component, false);
	return 0;
}

static int rt5677_spi_hw_params(
		struct snd_soc_component *component,
		struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *hw_params)
{
	struct rt5677_dsp *rt5677_dsp =
			snd_soc_component_get_drvdata(component);

	mutex_lock(&rt5677_dsp->dma_lock);
	rt5677_dsp->substream = substream;
	mutex_unlock(&rt5677_dsp->dma_lock);

	return 0;
}

static int rt5677_spi_hw_free(
		struct snd_soc_component *component,
		struct snd_pcm_substream *substream)
{
	struct rt5677_dsp *rt5677_dsp =
			snd_soc_component_get_drvdata(component);

	mutex_lock(&rt5677_dsp->dma_lock);
	rt5677_dsp->substream = NULL;
	mutex_unlock(&rt5677_dsp->dma_lock);

	return 0;
}

static int rt5677_spi_prepare(
		struct snd_soc_component *component,
		struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_component *rt5677_component =
			snd_soc_rtdcom_lookup(rtd, "rt5677");
	struct rt5677_priv *rt5677 =
			snd_soc_component_get_drvdata(rt5677_component);
	struct rt5677_dsp *rt5677_dsp =
			snd_soc_component_get_drvdata(component);

	rt5677->set_dsp_vad(rt5677_component, true);
	rt5677_dsp->dma_offset = 0;
	rt5677_dsp->avail_bytes = 0;
	return 0;
}

static snd_pcm_uframes_t rt5677_spi_pcm_pointer(
		struct snd_soc_component *component,
		struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct rt5677_dsp *rt5677_dsp =
			snd_soc_component_get_drvdata(component);

	return bytes_to_frames(runtime, rt5677_dsp->dma_offset);
}

static int rt5677_spi_mic_write_offset(u32 *mic_write_offset)
{
	int ret;
	/* Grab the first 4 bytes that hold the write pointer on the
	 * dsp, and check to make sure that it points somewhere inside the
	 * buffer.
	 */
	ret = rt5677_spi_read(RT5677_MIC_BUF_ADDR, mic_write_offset,
			sizeof(u32));
	if (ret)
		return ret;
	/* Adjust the offset so that it's zero-based */
	*mic_write_offset = *mic_write_offset - sizeof(u32);
	return *mic_write_offset < RT5677_MIC_BUF_BYTES ? 0 : -EFAULT;
}

/*
 * Copy one contiguous block of audio samples from the DSP mic buffer to the
 * dma_area of the pcm runtime. The receiving buffer may wrap around.
 * @begin: start offset of the block to copy, in bytes.
 * @end:   offset of the first byte after the block to copy, must be greater
 *         than or equal to begin.
 *
 * Return: Zero if successful, or a negative error code on failure.
 */
static int rt5677_spi_copy_block(struct rt5677_dsp *rt5677_dsp,
		u32 begin, u32 end)
{
	struct snd_pcm_runtime *runtime = rt5677_dsp->substream->runtime;
	size_t bytes_per_frame = frames_to_bytes(runtime, 1);
	size_t first_chunk_len, second_chunk_len;
	int ret;

	if (begin > end || runtime->dma_bytes < 2 * bytes_per_frame) {
		dev_err(rt5677_dsp->dev,
			"Invalid copy from (%u, %u), dma_area size %zu\n",
			begin, end, runtime->dma_bytes);
		return -EINVAL;
	}

	/* The block to copy is empty */
	if (begin == end)
		return 0;

	/* If the incoming chunk is too big for the receiving buffer, only the
	 * last "receiving buffer size - one frame" bytes are copied.
	 */
	if (end - begin > runtime->dma_bytes - bytes_per_frame)
		begin = end - (runtime->dma_bytes - bytes_per_frame);

	/* May need to split to two chunks, calculate the size of each */
	first_chunk_len = end - begin;
	second_chunk_len = 0;
	if (rt5677_dsp->dma_offset + first_chunk_len > runtime->dma_bytes) {
		/* Receiving buffer wrapped around */
		second_chunk_len = first_chunk_len;
		first_chunk_len = runtime->dma_bytes - rt5677_dsp->dma_offset;
		second_chunk_len -= first_chunk_len;
	}

	/* Copy first chunk */
	ret = rt5677_spi_read(RT5677_MIC_BUF_ADDR + sizeof(u32) + begin,
			runtime->dma_area + rt5677_dsp->dma_offset,
			first_chunk_len);
	if (ret)
		return ret;
	rt5677_dsp->dma_offset += first_chunk_len;
	if (rt5677_dsp->dma_offset == runtime->dma_bytes)
		rt5677_dsp->dma_offset = 0;

	/* Copy second chunk */
	if (second_chunk_len) {
		ret = rt5677_spi_read(RT5677_MIC_BUF_ADDR + sizeof(u32) +
				begin + first_chunk_len, runtime->dma_area,
				second_chunk_len);
		if (!ret)
			rt5677_dsp->dma_offset = second_chunk_len;
	}
	return ret;
}

/*
 * Copy a given amount of audio samples from the DSP mic buffer starting at
 * mic_read_offset, to the dma_area of the pcm runtime. The source buffer may
 * wrap around. mic_read_offset is updated after successful copy.
 * @amount: amount of samples to copy, in bytes.
 *
 * Return: Zero if successful, or a negative error code on failure.
 */
static int rt5677_spi_copy(struct rt5677_dsp *rt5677_dsp, u32 amount)
{
	int ret = 0;
	u32 target;

	if (amount == 0)
		return ret;

	target = rt5677_dsp->mic_read_offset + amount;
	/* Copy the first chunk in DSP's mic buffer */
	ret |= rt5677_spi_copy_block(rt5677_dsp, rt5677_dsp->mic_read_offset,
			min(target, RT5677_MIC_BUF_BYTES));

	if (target >= RT5677_MIC_BUF_BYTES) {
		/* Wrap around, copy the second chunk */
		target -= RT5677_MIC_BUF_BYTES;
		ret |= rt5677_spi_copy_block(rt5677_dsp, 0, target);
	}

	if (!ret)
		rt5677_dsp->mic_read_offset = target;
	return ret;
}

/*
 * A delayed work that streams audio samples from the DSP mic buffer to the
 * dma_area of the pcm runtime via SPI.
 */
static void rt5677_spi_copy_work(struct work_struct *work)
{
	struct rt5677_dsp *rt5677_dsp =
		container_of(work, struct rt5677_dsp, copy_work.work);
	struct snd_pcm_runtime *runtime;
	u32 mic_write_offset;
	size_t new_bytes, copy_bytes, period_bytes;
	unsigned int delay;
	int ret = 0;

	/* Ensure runtime->dma_area buffer does not go away while copying. */
	mutex_lock(&rt5677_dsp->dma_lock);
	if (!rt5677_dsp->substream) {
		dev_err(rt5677_dsp->dev, "No pcm substream\n");
		goto done;
	}

	runtime = rt5677_dsp->substream->runtime;

	if (rt5677_spi_mic_write_offset(&mic_write_offset)) {
		dev_err(rt5677_dsp->dev, "No mic_write_offset\n");
		goto done;
	}

	/* If this is the first time that we've asked for streaming data after
	 * a hotword is fired, we should start reading from the previous 2
	 * seconds of audio from wherever the mic_write_offset is currently.
	 */
	if (rt5677_dsp->new_hotword) {
		rt5677_dsp->new_hotword = false;
		/* See if buffer wraparound happens */
		if (mic_write_offset < RT5677_MIC_BUF_FIRST_READ_SIZE)
			rt5677_dsp->mic_read_offset = RT5677_MIC_BUF_BYTES -
					(RT5677_MIC_BUF_FIRST_READ_SIZE -
					mic_write_offset);
		else
			rt5677_dsp->mic_read_offset = mic_write_offset -
					RT5677_MIC_BUF_FIRST_READ_SIZE;
	}

	/* Calculate the amount of new samples in bytes */
	if (rt5677_dsp->mic_read_offset <= mic_write_offset)
		new_bytes = mic_write_offset - rt5677_dsp->mic_read_offset;
	else
		new_bytes = RT5677_MIC_BUF_BYTES + mic_write_offset
				- rt5677_dsp->mic_read_offset;

	/* Copy all new samples from DSP mic buffer, one period at a time */
	period_bytes = snd_pcm_lib_period_bytes(rt5677_dsp->substream);
	while (new_bytes) {
		copy_bytes = min(new_bytes, period_bytes
				- rt5677_dsp->avail_bytes);
		ret = rt5677_spi_copy(rt5677_dsp, copy_bytes);
		if (ret) {
			dev_err(rt5677_dsp->dev, "Copy failed %d\n", ret);
			goto done;
		}
		rt5677_dsp->avail_bytes += copy_bytes;
		if (rt5677_dsp->avail_bytes >= period_bytes) {
			snd_pcm_period_elapsed(rt5677_dsp->substream);
			rt5677_dsp->avail_bytes = 0;
		}
		new_bytes -= copy_bytes;
	}

	delay = bytes_to_frames(runtime, period_bytes) / (runtime->rate / 1000);
	schedule_delayed_work(&rt5677_dsp->copy_work, msecs_to_jiffies(delay));
done:
	mutex_unlock(&rt5677_dsp->dma_lock);
}

static int rt5677_spi_pcm_new(struct snd_soc_component *component,
			      struct snd_soc_pcm_runtime *rtd)
{
	snd_pcm_set_managed_buffer_all(rtd->pcm, SNDRV_DMA_TYPE_VMALLOC,
				       NULL, 0, 0);
	return 0;
}

static int rt5677_spi_pcm_probe(struct snd_soc_component *component)
{
	struct rt5677_dsp *rt5677_dsp;

	rt5677_dsp = devm_kzalloc(component->dev, sizeof(*rt5677_dsp),
			GFP_KERNEL);
	if (!rt5677_dsp)
		return -ENOMEM;
	rt5677_dsp->dev = &g_spi->dev;
	mutex_init(&rt5677_dsp->dma_lock);
	INIT_DELAYED_WORK(&rt5677_dsp->copy_work, rt5677_spi_copy_work);

	snd_soc_component_set_drvdata(component, rt5677_dsp);
	return 0;
}

static const struct snd_soc_component_driver rt5677_spi_dai_component = {
	.name			= DRV_NAME,
	.probe			= rt5677_spi_pcm_probe,
	.open			= rt5677_spi_pcm_open,
	.close			= rt5677_spi_pcm_close,
	.hw_params		= rt5677_spi_hw_params,
	.hw_free		= rt5677_spi_hw_free,
	.prepare		= rt5677_spi_prepare,
	.pointer		= rt5677_spi_pcm_pointer,
	.pcm_construct		= rt5677_spi_pcm_new,
	.legacy_dai_naming	= 1,
};

/* Select a suitable transfer command for the next transfer to ensure
 * the transfer address is always naturally aligned while minimizing
 * the total number of transfers required.
 *
 * 3 transfer commands are available:
 * RT5677_SPI_READ/WRITE_16:	Transfer 2 bytes
 * RT5677_SPI_READ/WRITE_32:	Transfer 4 bytes
 * RT5677_SPI_READ/WRITE_BURST:	Transfer any multiples of 8 bytes
 *
 * Note:
 * 16 Bit writes and reads are restricted to the address range
 * 0x18020000 ~ 0x18021000
 *
 * For example, reading 256 bytes at 0x60030004 uses the following commands:
 * 0x60030004 RT5677_SPI_READ_32	4 bytes
 * 0x60030008 RT5677_SPI_READ_BURST	240 bytes
 * 0x600300F8 RT5677_SPI_READ_BURST	8 bytes
 * 0x60030100 RT5677_SPI_READ_32	4 bytes
 *
 * Input:
 * @read: true for read commands; false for write commands
 * @align: alignment of the next transfer address
 * @remain: number of bytes remaining to transfer
 *
 * Output:
 * @len: number of bytes to transfer with the selected command
 * Returns the selected command
 */
static u8 rt5677_spi_select_cmd(bool read, u32 align, u32 remain, u32 *len)
{
	u8 cmd;

	if (align == 4 || remain <= 4) {
		cmd = RT5677_SPI_READ_32;
		*len = 4;
	} else {
		cmd = RT5677_SPI_READ_BURST;
		*len = (((remain - 1) >> 3) + 1) << 3;
		*len = min_t(u32, *len, RT5677_SPI_BURST_LEN);
	}
	return read ? cmd : cmd + 1;
}

/* Copy dstlen bytes from src to dst, while reversing byte order for each word.
 * If srclen < dstlen, zeros are padded.
 */
static void rt5677_spi_reverse(u8 *dst, u32 dstlen, const u8 *src, u32 srclen)
{
	u32 w, i, si;
	u32 word_size = min_t(u32, dstlen, 8);

	for (w = 0; w < dstlen; w += word_size) {
		for (i = 0; i < word_size && i + w < dstlen; i++) {
			si = w + word_size - i - 1;
			dst[w + i] = si < srclen ? src[si] : 0;
		}
	}
}

/* Read DSP address space using SPI. addr and len have to be 4-byte aligned. */
int rt5677_spi_read(u32 addr, void *rxbuf, size_t len)
{
	u32 offset;
	int status = 0;
	struct spi_transfer t[2];
	struct spi_message m;
	/* +4 bytes is for the DummyPhase following the AddressPhase */
	u8 header[RT5677_SPI_HEADER + 4];
	u8 body[RT5677_SPI_BURST_LEN];
	u8 spi_cmd;
	u8 *cb = rxbuf;

	if (!g_spi)
		return -ENODEV;

	if ((addr & 3) || (len & 3)) {
		dev_err(&g_spi->dev, "Bad read align 0x%x(%zu)\n", addr, len);
		return -EACCES;
	}

	memset(t, 0, sizeof(t));
	t[0].tx_buf = header;
	t[0].len = sizeof(header);
	t[0].speed_hz = RT5677_SPI_FREQ;
	t[1].rx_buf = body;
	t[1].speed_hz = RT5677_SPI_FREQ;
	spi_message_init_with_transfers(&m, t, ARRAY_SIZE(t));

	for (offset = 0; offset < len; offset += t[1].len) {
		spi_cmd = rt5677_spi_select_cmd(true, (addr + offset) & 7,
				len - offset, &t[1].len);

		/* Construct SPI message header */
		header[0] = spi_cmd;
		header[1] = ((addr + offset) & 0xff000000) >> 24;
		header[2] = ((addr + offset) & 0x00ff0000) >> 16;
		header[3] = ((addr + offset) & 0x0000ff00) >> 8;
		header[4] = ((addr + offset) & 0x000000ff) >> 0;

		mutex_lock(&spi_mutex);
		status |= spi_sync(g_spi, &m);
		mutex_unlock(&spi_mutex);


		/* Copy data back to caller buffer */
		rt5677_spi_reverse(cb + offset, len - offset, body, t[1].len);
	}
	return status;
}
EXPORT_SYMBOL_GPL(rt5677_spi_read);

/* Write DSP address space using SPI. addr has to be 4-byte aligned.
 * If len is not 4-byte aligned, then extra zeros are written at the end
 * as padding.
 */
int rt5677_spi_write(u32 addr, const void *txbuf, size_t len)
{
	u32 offset;
	int status = 0;
	struct spi_transfer t;
	struct spi_message m;
	/* +1 byte is for the DummyPhase following the DataPhase */
	u8 buf[RT5677_SPI_HEADER + RT5677_SPI_BURST_LEN + 1];
	u8 *body = buf + RT5677_SPI_HEADER;
	u8 spi_cmd;
	const u8 *cb = txbuf;

	if (!g_spi)
		return -ENODEV;

	if (addr & 3) {
		dev_err(&g_spi->dev, "Bad write align 0x%x(%zu)\n", addr, len);
		return -EACCES;
	}

	memset(&t, 0, sizeof(t));
	t.tx_buf = buf;
	t.speed_hz = RT5677_SPI_FREQ;
	spi_message_init_with_transfers(&m, &t, 1);

	for (offset = 0; offset < len;) {
		spi_cmd = rt5677_spi_select_cmd(false, (addr + offset) & 7,
				len - offset, &t.len);

		/* Construct SPI message header */
		buf[0] = spi_cmd;
		buf[1] = ((addr + offset) & 0xff000000) >> 24;
		buf[2] = ((addr + offset) & 0x00ff0000) >> 16;
		buf[3] = ((addr + offset) & 0x0000ff00) >> 8;
		buf[4] = ((addr + offset) & 0x000000ff) >> 0;

		/* Fetch data from caller buffer */
		rt5677_spi_reverse(body, t.len, cb + offset, len - offset);
		offset += t.len;
		t.len += RT5677_SPI_HEADER + 1;

		mutex_lock(&spi_mutex);
		status |= spi_sync(g_spi, &m);
		mutex_unlock(&spi_mutex);
	}
	return status;
}
EXPORT_SYMBOL_GPL(rt5677_spi_write);

int rt5677_spi_write_firmware(u32 addr, const struct firmware *fw)
{
	return rt5677_spi_write(addr, fw->data, fw->size);
}
EXPORT_SYMBOL_GPL(rt5677_spi_write_firmware);

void rt5677_spi_hotword_detected(void)
{
	struct rt5677_dsp *rt5677_dsp;

	if (!g_spi)
		return;

	rt5677_dsp = dev_get_drvdata(&g_spi->dev);
	if (!rt5677_dsp) {
		dev_err(&g_spi->dev, "Can't get rt5677_dsp\n");
		return;
	}

	mutex_lock(&rt5677_dsp->dma_lock);
	dev_info(rt5677_dsp->dev, "Hotword detected\n");
	rt5677_dsp->new_hotword = true;
	mutex_unlock(&rt5677_dsp->dma_lock);

	schedule_delayed_work(&rt5677_dsp->copy_work, 0);
}
EXPORT_SYMBOL_GPL(rt5677_spi_hotword_detected);

static int rt5677_spi_probe(struct spi_device *spi)
{
	int ret;

	g_spi = spi;

	ret = devm_snd_soc_register_component(&spi->dev,
					      &rt5677_spi_dai_component,
					      &rt5677_spi_dai, 1);
	if (ret < 0)
		dev_err(&spi->dev, "Failed to register component.\n");

	return ret;
}

#ifdef CONFIG_ACPI
static const struct acpi_device_id rt5677_spi_acpi_id[] = {
	{ "10EC5677" },
	{ "RT5677AA" },
	{ }
};
MODULE_DEVICE_TABLE(acpi, rt5677_spi_acpi_id);
#endif

static struct spi_driver rt5677_spi_driver = {
	.driver = {
		.name = DRV_NAME,
		.acpi_match_table = ACPI_PTR(rt5677_spi_acpi_id),
	},
	.probe = rt5677_spi_probe,
};
module_spi_driver(rt5677_spi_driver);

MODULE_DESCRIPTION("ASoC RT5677 SPI driver");
MODULE_AUTHOR("Oder Chiou <oder_chiou@realtek.com>");
MODULE_LICENSE("GPL v2");
