/*
 * wm0010.c  --  WM0010 DSP Driver
 *
 * Copyright 2012 Wolfson Microelectronics PLC.
 *
 * Authors: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *          Dimitris Papastamos <dp@opensource.wolfsonmicro.com>
 *          Scott Ling <sl@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/irqreturn.h>
#include <linux/init.h>
#include <linux/spi/spi.h>
#include <linux/firmware.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>

#include <sound/soc.h>
#include <sound/wm0010.h>

#define DEVICE_ID_WM0010	10

/* We only support v1 of the .dfw INFO record */
#define INFO_VERSION		1

enum dfw_cmd {
	DFW_CMD_FUSE = 0x01,
	DFW_CMD_CODE_HDR,
	DFW_CMD_CODE_DATA,
	DFW_CMD_PLL,
	DFW_CMD_INFO = 0xff
};

struct dfw_binrec {
	u8 command;
	u32 length:24;
	u32 address;
	uint8_t data[0];
} __packed;

struct dfw_inforec {
	u8 info_version;
	u8 tool_major_version;
	u8 tool_minor_version;
	u8 dsp_target;
};

struct dfw_pllrec {
	u8 command;
	u32 length:24;
	u32 address;
	u32 clkctrl1;
	u32 clkctrl2;
	u32 clkctrl3;
	u32 ldetctrl;
	u32 uart_div;
	u32 spi_div;
} __packed;

static struct pll_clock_map {
	int max_sysclk;
	int max_pll_spi_speed;
	u32 pll_clkctrl1;
} pll_clock_map[] = {			   /* Dividers */
	{ 22000000, 26000000, 0x00201f11 }, /* 2,32,2  */
	{ 18000000, 26000000, 0x00203f21 }, /* 2,64,4  */
	{ 14000000, 26000000, 0x00202620 }, /* 1,39,4  */
	{ 10000000, 22000000, 0x00203120 }, /* 1,50,4  */
	{  6500000, 22000000, 0x00204520 }, /* 1,70,4  */
	{  5500000, 22000000, 0x00103f10 }, /* 1,64,2  */
};

enum wm0010_state {
	WM0010_POWER_OFF,
	WM0010_OUT_OF_RESET,
	WM0010_BOOTROM,
	WM0010_STAGE2,
	WM0010_FIRMWARE,
};

struct wm0010_priv {
	struct snd_soc_codec *codec;

	struct mutex lock;
	struct device *dev;

	struct wm0010_pdata pdata;

	int gpio_reset;
	int gpio_reset_value;

	struct regulator_bulk_data core_supplies[2];
	struct regulator *dbvdd;

	int sysclk;

	enum wm0010_state state;
	bool boot_failed;
	bool ready;
	bool pll_running;
	int max_spi_freq;
	int board_max_spi_speed;
	u32 pll_clkctrl1;

	spinlock_t irq_lock;
	int irq;

	struct completion boot_completion;
};

struct wm0010_spi_msg {
	struct spi_message m;
	struct spi_transfer t;
	u8 *tx_buf;
	u8 *rx_buf;
	size_t len;
};

static const struct snd_soc_dapm_widget wm0010_dapm_widgets[] = {
SND_SOC_DAPM_SUPPLY("CLKIN",  SND_SOC_NOPM, 0, 0, NULL, 0),
};

static const struct snd_soc_dapm_route wm0010_dapm_routes[] = {
	{ "SDI2 Capture", NULL, "SDI1 Playback" },
	{ "SDI1 Capture", NULL, "SDI2 Playback" },

	{ "SDI1 Capture", NULL, "CLKIN" },
	{ "SDI2 Capture", NULL, "CLKIN" },
	{ "SDI1 Playback", NULL, "CLKIN" },
	{ "SDI2 Playback", NULL, "CLKIN" },
};

static const char *wm0010_state_to_str(enum wm0010_state state)
{
	const char *state_to_str[] = {
		"Power off",
		"Out of reset",
		"Boot ROM",
		"Stage2",
		"Firmware"
	};

	if (state < 0 || state >= ARRAY_SIZE(state_to_str))
		return "null";
	return state_to_str[state];
}

/* Called with wm0010->lock held */
static void wm0010_halt(struct snd_soc_codec *codec)
{
	struct wm0010_priv *wm0010 = snd_soc_codec_get_drvdata(codec);
	unsigned long flags;
	enum wm0010_state state;

	/* Fetch the wm0010 state */
	spin_lock_irqsave(&wm0010->irq_lock, flags);
	state = wm0010->state;
	spin_unlock_irqrestore(&wm0010->irq_lock, flags);

	switch (state) {
	case WM0010_POWER_OFF:
		/* If there's nothing to do, bail out */
		return;
	case WM0010_OUT_OF_RESET:
	case WM0010_BOOTROM:
	case WM0010_STAGE2:
	case WM0010_FIRMWARE:
		/* Remember to put chip back into reset */
		gpio_set_value_cansleep(wm0010->gpio_reset,
					wm0010->gpio_reset_value);
		/* Disable the regulators */
		regulator_disable(wm0010->dbvdd);
		regulator_bulk_disable(ARRAY_SIZE(wm0010->core_supplies),
				       wm0010->core_supplies);
		break;
	}

	spin_lock_irqsave(&wm0010->irq_lock, flags);
	wm0010->state = WM0010_POWER_OFF;
	spin_unlock_irqrestore(&wm0010->irq_lock, flags);
}

struct wm0010_boot_xfer {
	struct list_head list;
	struct snd_soc_codec *codec;
	struct completion *done;
	struct spi_message m;
	struct spi_transfer t;
};

/* Called with wm0010->lock held */
static void wm0010_mark_boot_failure(struct wm0010_priv *wm0010)
{
	enum wm0010_state state;
	unsigned long flags;

	spin_lock_irqsave(&wm0010->irq_lock, flags);
	state = wm0010->state;
	spin_unlock_irqrestore(&wm0010->irq_lock, flags);

	dev_err(wm0010->dev, "Failed to transition from `%s' state to `%s' state\n",
		wm0010_state_to_str(state), wm0010_state_to_str(state + 1));

	wm0010->boot_failed = true;
}

static void wm0010_boot_xfer_complete(void *data)
{
	struct wm0010_boot_xfer *xfer = data;
	struct snd_soc_codec *codec = xfer->codec;
	struct wm0010_priv *wm0010 = snd_soc_codec_get_drvdata(codec);
	u32 *out32 = xfer->t.rx_buf;
	int i;

	if (xfer->m.status != 0) {
		dev_err(codec->dev, "SPI transfer failed: %d\n",
			xfer->m.status);
		wm0010_mark_boot_failure(wm0010);
		if (xfer->done)
			complete(xfer->done);
		return;
	}

	for (i = 0; i < xfer->t.len / 4; i++) {
		dev_dbg(codec->dev, "%d: %04x\n", i, out32[i]);

		switch (be32_to_cpu(out32[i])) {
		case 0xe0e0e0e0:
			dev_err(codec->dev,
				"%d: ROM error reported in stage 2\n", i);
			wm0010_mark_boot_failure(wm0010);
			break;

		case 0x55555555:
			if (wm0010->state < WM0010_STAGE2)
				break;
			dev_err(codec->dev,
				"%d: ROM bootloader running in stage 2\n", i);
			wm0010_mark_boot_failure(wm0010);
			break;

		case 0x0fed0000:
			dev_dbg(codec->dev, "Stage2 loader running\n");
			break;

		case 0x0fed0007:
			dev_dbg(codec->dev, "CODE_HDR packet received\n");
			break;

		case 0x0fed0008:
			dev_dbg(codec->dev, "CODE_DATA packet received\n");
			break;

		case 0x0fed0009:
			dev_dbg(codec->dev, "Download complete\n");
			break;

		case 0x0fed000c:
			dev_dbg(codec->dev, "Application start\n");
			break;

		case 0x0fed000e:
			dev_dbg(codec->dev, "PLL packet received\n");
			wm0010->pll_running = true;
			break;

		case 0x0fed0025:
			dev_err(codec->dev, "Device reports image too long\n");
			wm0010_mark_boot_failure(wm0010);
			break;

		case 0x0fed002c:
			dev_err(codec->dev, "Device reports bad SPI packet\n");
			wm0010_mark_boot_failure(wm0010);
			break;

		case 0x0fed0031:
			dev_err(codec->dev, "Device reports SPI read overflow\n");
			wm0010_mark_boot_failure(wm0010);
			break;

		case 0x0fed0032:
			dev_err(codec->dev, "Device reports SPI underclock\n");
			wm0010_mark_boot_failure(wm0010);
			break;

		case 0x0fed0033:
			dev_err(codec->dev, "Device reports bad header packet\n");
			wm0010_mark_boot_failure(wm0010);
			break;

		case 0x0fed0034:
			dev_err(codec->dev, "Device reports invalid packet type\n");
			wm0010_mark_boot_failure(wm0010);
			break;

		case 0x0fed0035:
			dev_err(codec->dev, "Device reports data before header error\n");
			wm0010_mark_boot_failure(wm0010);
			break;

		case 0x0fed0038:
			dev_err(codec->dev, "Device reports invalid PLL packet\n");
			break;

		case 0x0fed003a:
			dev_err(codec->dev, "Device reports packet alignment error\n");
			wm0010_mark_boot_failure(wm0010);
			break;

		default:
			dev_err(codec->dev, "Unrecognised return 0x%x\n",
			    be32_to_cpu(out32[i]));
			wm0010_mark_boot_failure(wm0010);
			break;
		}

		if (wm0010->boot_failed)
			break;
	}

	if (xfer->done)
		complete(xfer->done);
}

static void byte_swap_64(u64 *data_in, u64 *data_out, u32 len)
{
	int i;

	for (i = 0; i < len / 8; i++)
		data_out[i] = cpu_to_be64(le64_to_cpu(data_in[i]));
}

static int wm0010_firmware_load(const char *name, struct snd_soc_codec *codec)
{
	struct spi_device *spi = to_spi_device(codec->dev);
	struct wm0010_priv *wm0010 = snd_soc_codec_get_drvdata(codec);
	struct list_head xfer_list;
	struct wm0010_boot_xfer *xfer;
	int ret;
	struct completion done;
	const struct firmware *fw;
	const struct dfw_binrec *rec;
	const struct dfw_inforec *inforec;
	u64 *img;
	u8 *out, dsp;
	u32 len, offset;

	INIT_LIST_HEAD(&xfer_list);

	ret = request_firmware(&fw, name, codec->dev);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to request application(%s): %d\n",
			name, ret);
		return ret;
	}

	rec = (const struct dfw_binrec *)fw->data;
	inforec = (const struct dfw_inforec *)rec->data;
	offset = 0;
	dsp = inforec->dsp_target;
	wm0010->boot_failed = false;
	BUG_ON(!list_empty(&xfer_list));
	init_completion(&done);

	/* First record should be INFO */
	if (rec->command != DFW_CMD_INFO) {
		dev_err(codec->dev, "First record not INFO\r\n");
		ret = -EINVAL;
		goto abort;
	}

	if (inforec->info_version != INFO_VERSION) {
		dev_err(codec->dev,
			"Unsupported version (%02d) of INFO record\r\n",
			inforec->info_version);
		ret = -EINVAL;
		goto abort;
	}

	dev_dbg(codec->dev, "Version v%02d INFO record found\r\n",
		inforec->info_version);

	/* Check it's a DSP file */
	if (dsp != DEVICE_ID_WM0010) {
		dev_err(codec->dev, "Not a WM0010 firmware file.\r\n");
		ret = -EINVAL;
		goto abort;
	}

	/* Skip the info record as we don't need to send it */
	offset += ((rec->length) + 8);
	rec = (void *)&rec->data[rec->length];

	while (offset < fw->size) {
		dev_dbg(codec->dev,
			"Packet: command %d, data length = 0x%x\r\n",
			rec->command, rec->length);
		len = rec->length + 8;

		out = kzalloc(len, GFP_KERNEL);
		if (!out) {
			dev_err(codec->dev,
				"Failed to allocate RX buffer\n");
			ret = -ENOMEM;
			goto abort1;
		}

		img = kzalloc(len, GFP_KERNEL);
		if (!img) {
			dev_err(codec->dev,
				"Failed to allocate image buffer\n");
			ret = -ENOMEM;
			goto abort1;
		}

		byte_swap_64((u64 *)&rec->command, img, len);

		xfer = kzalloc(sizeof(*xfer), GFP_KERNEL);
		if (!xfer) {
			dev_err(codec->dev, "Failed to allocate xfer\n");
			ret = -ENOMEM;
			goto abort1;
		}

		xfer->codec = codec;
		list_add_tail(&xfer->list, &xfer_list);

		spi_message_init(&xfer->m);
		xfer->m.complete = wm0010_boot_xfer_complete;
		xfer->m.context = xfer;
		xfer->t.tx_buf = img;
		xfer->t.rx_buf = out;
		xfer->t.len = len;
		xfer->t.bits_per_word = 8;

		if (!wm0010->pll_running) {
			xfer->t.speed_hz = wm0010->sysclk / 6;
		} else {
			xfer->t.speed_hz = wm0010->max_spi_freq;

			if (wm0010->board_max_spi_speed &&
			   (wm0010->board_max_spi_speed < wm0010->max_spi_freq))
					xfer->t.speed_hz = wm0010->board_max_spi_speed;
		}

		/* Store max usable spi frequency for later use */
		wm0010->max_spi_freq = xfer->t.speed_hz;

		spi_message_add_tail(&xfer->t, &xfer->m);

		offset += ((rec->length) + 8);
		rec = (void *)&rec->data[rec->length];

		if (offset >= fw->size) {
			dev_dbg(codec->dev, "All transfers scheduled\n");
			xfer->done = &done;
		}

		ret = spi_async(spi, &xfer->m);
		if (ret != 0) {
			dev_err(codec->dev, "Write failed: %d\n", ret);
			goto abort1;
		}

		if (wm0010->boot_failed) {
			dev_dbg(codec->dev, "Boot fail!\n");
			ret = -EINVAL;
			goto abort1;
		}
	}

	wait_for_completion(&done);

	ret = 0;

abort1:
	while (!list_empty(&xfer_list)) {
		xfer = list_first_entry(&xfer_list, struct wm0010_boot_xfer,
					list);
		kfree(xfer->t.rx_buf);
		kfree(xfer->t.tx_buf);
		list_del(&xfer->list);
		kfree(xfer);
	}

abort:
	release_firmware(fw);
	return ret;
}

static int wm0010_stage2_load(struct snd_soc_codec *codec)
{
	struct spi_device *spi = to_spi_device(codec->dev);
	struct wm0010_priv *wm0010 = snd_soc_codec_get_drvdata(codec);
	const struct firmware *fw;
	struct spi_message m;
	struct spi_transfer t;
	u32 *img;
	u8 *out;
	int i;
	int ret = 0;

	ret = request_firmware(&fw, "wm0010_stage2.bin", codec->dev);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to request stage2 loader: %d\n",
			ret);
		return ret;
	}

	dev_dbg(codec->dev, "Downloading %zu byte stage 2 loader\n", fw->size);

	/* Copy to local buffer first as vmalloc causes problems for dma */
	img = kzalloc(fw->size, GFP_KERNEL);
	if (!img) {
		dev_err(codec->dev, "Failed to allocate image buffer\n");
		ret = -ENOMEM;
		goto abort2;
	}

	out = kzalloc(fw->size, GFP_KERNEL);
	if (!out) {
		dev_err(codec->dev, "Failed to allocate output buffer\n");
		ret = -ENOMEM;
		goto abort1;
	}

	memcpy(img, &fw->data[0], fw->size);

	spi_message_init(&m);
	memset(&t, 0, sizeof(t));
	t.rx_buf = out;
	t.tx_buf = img;
	t.len = fw->size;
	t.bits_per_word = 8;
	t.speed_hz = wm0010->sysclk / 10;
	spi_message_add_tail(&t, &m);

	dev_dbg(codec->dev, "Starting initial download at %dHz\n",
		t.speed_hz);

	ret = spi_sync(spi, &m);
	if (ret != 0) {
		dev_err(codec->dev, "Initial download failed: %d\n", ret);
		goto abort;
	}

	/* Look for errors from the boot ROM */
	for (i = 0; i < fw->size; i++) {
		if (out[i] != 0x55) {
			dev_err(codec->dev, "Boot ROM error: %x in %d\n",
				out[i], i);
			wm0010_mark_boot_failure(wm0010);
			ret = -EBUSY;
			goto abort;
		}
	}
abort:
	kfree(out);
abort1:
	kfree(img);
abort2:
	release_firmware(fw);

	return ret;
}

static int wm0010_boot(struct snd_soc_codec *codec)
{
	struct spi_device *spi = to_spi_device(codec->dev);
	struct wm0010_priv *wm0010 = snd_soc_codec_get_drvdata(codec);
	unsigned long flags;
	int ret;
	const struct firmware *fw;
	struct spi_message m;
	struct spi_transfer t;
	struct dfw_pllrec pll_rec;
	u32 *p, len;
	u64 *img_swap;
	u8 *out;
	int i;

	spin_lock_irqsave(&wm0010->irq_lock, flags);
	if (wm0010->state != WM0010_POWER_OFF)
		dev_warn(wm0010->dev, "DSP already powered up!\n");
	spin_unlock_irqrestore(&wm0010->irq_lock, flags);

	if (wm0010->sysclk > 26000000) {
		dev_err(codec->dev, "Max DSP clock frequency is 26MHz\n");
		ret = -ECANCELED;
		goto err;
	}

	mutex_lock(&wm0010->lock);
	wm0010->pll_running = false;

	dev_dbg(codec->dev, "max_spi_freq: %d\n", wm0010->max_spi_freq);

	ret = regulator_bulk_enable(ARRAY_SIZE(wm0010->core_supplies),
				    wm0010->core_supplies);
	if (ret != 0) {
		dev_err(&spi->dev, "Failed to enable core supplies: %d\n",
			ret);
		mutex_unlock(&wm0010->lock);
		goto err;
	}

	ret = regulator_enable(wm0010->dbvdd);
	if (ret != 0) {
		dev_err(&spi->dev, "Failed to enable DBVDD: %d\n", ret);
		goto err_core;
	}

	/* Release reset */
	gpio_set_value_cansleep(wm0010->gpio_reset, !wm0010->gpio_reset_value);
	spin_lock_irqsave(&wm0010->irq_lock, flags);
	wm0010->state = WM0010_OUT_OF_RESET;
	spin_unlock_irqrestore(&wm0010->irq_lock, flags);

	/* First the bootloader */
	ret = request_firmware(&fw, "wm0010_stage2.bin", codec->dev);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to request stage2 loader: %d\n",
			ret);
		goto abort;
	}

	if (!wait_for_completion_timeout(&wm0010->boot_completion,
					 msecs_to_jiffies(20)))
		dev_err(codec->dev, "Failed to get interrupt from DSP\n");

	spin_lock_irqsave(&wm0010->irq_lock, flags);
	wm0010->state = WM0010_BOOTROM;
	spin_unlock_irqrestore(&wm0010->irq_lock, flags);

	ret = wm0010_stage2_load(codec);
	if (ret)
		goto abort;

	if (!wait_for_completion_timeout(&wm0010->boot_completion,
					 msecs_to_jiffies(20)))
		dev_err(codec->dev, "Failed to get interrupt from DSP loader.\n");

	spin_lock_irqsave(&wm0010->irq_lock, flags);
	wm0010->state = WM0010_STAGE2;
	spin_unlock_irqrestore(&wm0010->irq_lock, flags);

	/* Only initialise PLL if max_spi_freq initialised */
	if (wm0010->max_spi_freq) {

		/* Initialise a PLL record */
		memset(&pll_rec, 0, sizeof(pll_rec));
		pll_rec.command = DFW_CMD_PLL;
		pll_rec.length = (sizeof(pll_rec) - 8);

		/* On wm0010 only the CLKCTRL1 value is used */
		pll_rec.clkctrl1 = wm0010->pll_clkctrl1;

		len = pll_rec.length + 8;
		out = kzalloc(len, GFP_KERNEL);
		if (!out) {
			dev_err(codec->dev,
				"Failed to allocate RX buffer\n");
			goto abort;
		}

		img_swap = kzalloc(len, GFP_KERNEL);
		if (!img_swap) {
			dev_err(codec->dev,
				"Failed to allocate image buffer\n");
			goto abort;
		}

		/* We need to re-order for 0010 */
		byte_swap_64((u64 *)&pll_rec, img_swap, len);

		spi_message_init(&m);
		memset(&t, 0, sizeof(t));
		t.rx_buf = out;
		t.tx_buf = img_swap;
		t.len = len;
		t.bits_per_word = 8;
		t.speed_hz = wm0010->sysclk / 6;
		spi_message_add_tail(&t, &m);

		ret = spi_sync(spi, &m);
		if (ret != 0) {
			dev_err(codec->dev, "First PLL write failed: %d\n", ret);
			goto abort;
		}

		/* Use a second send of the message to get the return status */
		ret = spi_sync(spi, &m);
		if (ret != 0) {
			dev_err(codec->dev, "Second PLL write failed: %d\n", ret);
			goto abort;
		}

		p = (u32 *)out;

		/* Look for PLL active code from the DSP */
		for (i = 0; i < len / 4; i++) {
			if (*p == 0x0e00ed0f) {
				dev_dbg(codec->dev, "PLL packet received\n");
				wm0010->pll_running = true;
				break;
			}
			p++;
		}

		kfree(img_swap);
		kfree(out);
	} else
		dev_dbg(codec->dev, "Not enabling DSP PLL.");

	ret = wm0010_firmware_load("wm0010.dfw", codec);

	if (ret != 0)
		goto abort;

	spin_lock_irqsave(&wm0010->irq_lock, flags);
	wm0010->state = WM0010_FIRMWARE;
	spin_unlock_irqrestore(&wm0010->irq_lock, flags);

	mutex_unlock(&wm0010->lock);

	return 0;

abort:
	/* Put the chip back into reset */
	wm0010_halt(codec);
	mutex_unlock(&wm0010->lock);
	return ret;

err_core:
	mutex_unlock(&wm0010->lock);
	regulator_bulk_disable(ARRAY_SIZE(wm0010->core_supplies),
			       wm0010->core_supplies);
err:
	return ret;
}

static int wm0010_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	struct wm0010_priv *wm0010 = snd_soc_codec_get_drvdata(codec);

	switch (level) {
	case SND_SOC_BIAS_ON:
		if (codec->dapm.bias_level == SND_SOC_BIAS_PREPARE)
			wm0010_boot(codec);
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		if (codec->dapm.bias_level == SND_SOC_BIAS_PREPARE) {
			mutex_lock(&wm0010->lock);
			wm0010_halt(codec);
			mutex_unlock(&wm0010->lock);
		}
		break;
	case SND_SOC_BIAS_OFF:
		break;
	}

	codec->dapm.bias_level = level;

	return 0;
}

static int wm0010_set_sysclk(struct snd_soc_codec *codec, int source,
			     int clk_id, unsigned int freq, int dir)
{
	struct wm0010_priv *wm0010 = snd_soc_codec_get_drvdata(codec);
	unsigned int i;

	wm0010->sysclk = freq;

	if (freq < pll_clock_map[ARRAY_SIZE(pll_clock_map)-1].max_sysclk) {
		wm0010->max_spi_freq = 0;
	} else {
		for (i = 0; i < ARRAY_SIZE(pll_clock_map); i++)
			if (freq >= pll_clock_map[i].max_sysclk)
				break;

		wm0010->max_spi_freq = pll_clock_map[i].max_pll_spi_speed;
		wm0010->pll_clkctrl1 = pll_clock_map[i].pll_clkctrl1;
	}

	return 0;
}

static int wm0010_probe(struct snd_soc_codec *codec);

static struct snd_soc_codec_driver soc_codec_dev_wm0010 = {
	.probe = wm0010_probe,
	.set_bias_level = wm0010_set_bias_level,
	.set_sysclk = wm0010_set_sysclk,
	.idle_bias_off = true,

	.dapm_widgets = wm0010_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(wm0010_dapm_widgets),
	.dapm_routes = wm0010_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(wm0010_dapm_routes),
};

#define WM0010_RATES (SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000)
#define WM0010_FORMATS (SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_S16_LE |\
			SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S24_LE |\
			SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver wm0010_dai[] = {
	{
		.name = "wm0010-sdi1",
		.playback = {
			.stream_name = "SDI1 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = WM0010_RATES,
			.formats = WM0010_FORMATS,
		},
		.capture = {
			 .stream_name = "SDI1 Capture",
			 .channels_min = 1,
			 .channels_max = 2,
			 .rates = WM0010_RATES,
			 .formats = WM0010_FORMATS,
		 },
	},
	{
		.name = "wm0010-sdi2",
		.playback = {
			.stream_name = "SDI2 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = WM0010_RATES,
			.formats = WM0010_FORMATS,
		},
		.capture = {
			 .stream_name = "SDI2 Capture",
			 .channels_min = 1,
			 .channels_max = 2,
			 .rates = WM0010_RATES,
			 .formats = WM0010_FORMATS,
		 },
	},
};

static irqreturn_t wm0010_irq(int irq, void *data)
{
	struct wm0010_priv *wm0010 = data;

	switch (wm0010->state) {
	case WM0010_OUT_OF_RESET:
	case WM0010_BOOTROM:
	case WM0010_STAGE2:
		spin_lock(&wm0010->irq_lock);
		complete(&wm0010->boot_completion);
		spin_unlock(&wm0010->irq_lock);
		return IRQ_HANDLED;
	default:
		return IRQ_NONE;
	}

	return IRQ_NONE;
}

static int wm0010_probe(struct snd_soc_codec *codec)
{
	struct wm0010_priv *wm0010 = snd_soc_codec_get_drvdata(codec);

	wm0010->codec = codec;

	return 0;
}

static int wm0010_spi_probe(struct spi_device *spi)
{
	unsigned long gpio_flags;
	int ret;
	int trigger;
	int irq;
	struct wm0010_priv *wm0010;

	wm0010 = devm_kzalloc(&spi->dev, sizeof(*wm0010),
			      GFP_KERNEL);
	if (!wm0010)
		return -ENOMEM;

	mutex_init(&wm0010->lock);
	spin_lock_init(&wm0010->irq_lock);

	spi_set_drvdata(spi, wm0010);
	wm0010->dev = &spi->dev;

	if (dev_get_platdata(&spi->dev))
		memcpy(&wm0010->pdata, dev_get_platdata(&spi->dev),
		       sizeof(wm0010->pdata));

	init_completion(&wm0010->boot_completion);

	wm0010->core_supplies[0].supply = "AVDD";
	wm0010->core_supplies[1].supply = "DCVDD";
	ret = devm_regulator_bulk_get(wm0010->dev, ARRAY_SIZE(wm0010->core_supplies),
				      wm0010->core_supplies);
	if (ret != 0) {
		dev_err(wm0010->dev, "Failed to obtain core supplies: %d\n",
			ret);
		return ret;
	}

	wm0010->dbvdd = devm_regulator_get(wm0010->dev, "DBVDD");
	if (IS_ERR(wm0010->dbvdd)) {
		ret = PTR_ERR(wm0010->dbvdd);
		dev_err(wm0010->dev, "Failed to obtain DBVDD: %d\n", ret);
		return ret;
	}

	if (wm0010->pdata.gpio_reset) {
		wm0010->gpio_reset = wm0010->pdata.gpio_reset;

		if (wm0010->pdata.reset_active_high)
			wm0010->gpio_reset_value = 1;
		else
			wm0010->gpio_reset_value = 0;

		if (wm0010->gpio_reset_value)
			gpio_flags = GPIOF_OUT_INIT_HIGH;
		else
			gpio_flags = GPIOF_OUT_INIT_LOW;

		ret = devm_gpio_request_one(wm0010->dev, wm0010->gpio_reset,
					    gpio_flags, "wm0010 reset");
		if (ret < 0) {
			dev_err(wm0010->dev,
				"Failed to request GPIO for DSP reset: %d\n",
				ret);
			return ret;
		}
	} else {
		dev_err(wm0010->dev, "No reset GPIO configured\n");
		return -EINVAL;
	}

	wm0010->state = WM0010_POWER_OFF;

	irq = spi->irq;
	if (wm0010->pdata.irq_flags)
		trigger = wm0010->pdata.irq_flags;
	else
		trigger = IRQF_TRIGGER_FALLING;
	trigger |= IRQF_ONESHOT;

	ret = request_threaded_irq(irq, NULL, wm0010_irq, trigger | IRQF_ONESHOT,
				   "wm0010", wm0010);
	if (ret) {
		dev_err(wm0010->dev, "Failed to request IRQ %d: %d\n",
			irq, ret);
		return ret;
	}
	wm0010->irq = irq;

	if (spi->max_speed_hz)
		wm0010->board_max_spi_speed = spi->max_speed_hz;
	else
		wm0010->board_max_spi_speed = 0;

	ret = snd_soc_register_codec(&spi->dev,
				     &soc_codec_dev_wm0010, wm0010_dai,
				     ARRAY_SIZE(wm0010_dai));
	if (ret < 0)
		return ret;

	return 0;
}

static int wm0010_spi_remove(struct spi_device *spi)
{
	struct wm0010_priv *wm0010 = spi_get_drvdata(spi);

	snd_soc_unregister_codec(&spi->dev);

	gpio_set_value_cansleep(wm0010->gpio_reset,
				wm0010->gpio_reset_value);

	if (wm0010->irq)
		free_irq(wm0010->irq, wm0010);

	return 0;
}

static struct spi_driver wm0010_spi_driver = {
	.driver = {
		.name	= "wm0010",
		.bus 	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe		= wm0010_spi_probe,
	.remove		= wm0010_spi_remove,
};

module_spi_driver(wm0010_spi_driver);

MODULE_DESCRIPTION("ASoC WM0010 driver");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
