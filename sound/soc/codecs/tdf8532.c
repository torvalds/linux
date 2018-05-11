/*
 * Codec driver for NXP Semiconductors - TDF8532
 * Copyright (c) 2017, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/jiffies.h>
#include <linux/time.h>
#include <linux/acpi.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <sound/pcm_params.h>
#include "tdf8532.h"

static int __tdf8532_build_pkt(struct tdf8532_priv *dev_data,
				va_list valist,	u8 *payload)
{
	int param;
	u8 len;
	u8 *cmd_payload;
	const u8 cmd_offset = 3;

	payload[HEADER_TYPE] = MSG_TYPE_STX;
	payload[HEADER_PKTID] = dev_data->pkt_id;

	cmd_payload = &(payload[cmd_offset]);

	param = va_arg(valist, int);
	len = 0;

	while (param != END) {
		cmd_payload[len] = param;

		len++;

		param = va_arg(valist, int);
	}

	payload[HEADER_LEN] = len;

	return len + cmd_offset;
}

static int __tdf8532_single_write(struct tdf8532_priv *dev_data,
						int dummy, ...)
{
	va_list valist;
	int ret;
	u8 len;
	u8 payload[255];

	va_start(valist, dummy);

	len = __tdf8532_build_pkt(dev_data, valist, payload);

	va_end(valist);

	print_hex_dump_debug("tdf8532-codec: Tx:", DUMP_PREFIX_NONE, 32, 1,
			payload, len, false);
	ret = i2c_master_send(dev_data->i2c, payload, len);

	dev_data->pkt_id++;

	if (ret < 0) {
		dev_err(&(dev_data->i2c->dev),
				"i2c send packet returned: %d\n", ret);

		return ret;
	}

	return 0;
}


static uint8_t tdf8532_read_wait_ack(struct tdf8532_priv *dev_data,
						unsigned long timeout)
{
	uint8_t ack_repl[HEADER_SIZE] = {0, 0, 0};
	unsigned long timeout_point = jiffies + timeout;
	int ret;

	usleep_range(10000,20000);
	do {
		ret = i2c_master_recv(dev_data->i2c, ack_repl, HEADER_SIZE);
		if (ret < 0)
			goto out;
	} while (time_before(jiffies, timeout_point) &&
			ack_repl[0] != MSG_TYPE_ACK);

	if (ack_repl[0] != MSG_TYPE_ACK)
		return -ETIME;
	else
		return ack_repl[2];

out:
	return ret;
}

static uint8_t tdf8532_single_read(struct tdf8532_priv *dev_data,
						char **repl_buff)
{
	int ret;
	uint8_t len;

	struct device *dev = &(dev_data->i2c->dev);

	ret = tdf8532_read_wait_ack(dev_data, msecs_to_jiffies(ACK_TIMEOUT));

	if (ret < 0) {
		dev_err(dev,
				"Error waiting for ACK reply: %d\n", ret);
		goto out;
	}

	len = ret + HEADER_SIZE;

	*repl_buff = kzalloc(len, GFP_KERNEL);

	ret = i2c_master_recv(dev_data->i2c, *repl_buff, len);

	print_hex_dump_debug("tdf8532-codec: Rx:", DUMP_PREFIX_NONE, 32, 1,
			*repl_buff, len, false);

	if (ret < 0 || ret != len) {
		dev_err(dev,
				"i2c recv packet returned: %d (expected: %d)\n",
				ret, len);
		goto out_free;
	}

	return len;

out_free:
	kfree(*repl_buff);
	repl_buff = NULL;
out:
	return ret;
}

static int tdf8532_get_state(struct tdf8532_priv *dev_data,
		struct get_dev_status_repl **status_repl)
{
	int ret = 0;
	char *repl_buff = NULL;

	ret = tdf8532_amp_write(dev_data, GET_DEV_STATUS);
	if (ret < 0)
		goto out;

	ret = tdf8532_single_read(dev_data, &repl_buff);
	if (ret < 0)
		goto out;

	*status_repl = (struct get_dev_status_repl *) repl_buff;

out:
	return ret;
}

static int tdf8532_wait_state(struct tdf8532_priv *dev_data, u8 req_state,
							unsigned long timeout)
{
	unsigned long timeout_point = jiffies + msecs_to_jiffies(timeout);
	int ret;
	struct get_dev_status_repl *status_repl = NULL;
	u8 cur_state = STATE_NONE;
	struct device *dev = &(dev_data->i2c->dev);

	do {
		ret = tdf8532_get_state(dev_data, &status_repl);
		if (ret < 0)
			goto out;
		cur_state = status_repl->state;
		print_hex_dump_debug("tdf8532-codec: wait_state: ",
				DUMP_PREFIX_NONE, 32, 1, status_repl,
				6, false);

		kfree(status_repl);
		status_repl = NULL;
	} while (time_before(jiffies, timeout_point)
			&& cur_state != req_state);

	if (cur_state == req_state)
		return 0;

out:
	ret = -ETIME;

	dev_err(dev, "tdf8532-codec: state: %u, req_state: %u, ret: %d\n",
			cur_state, req_state, ret);
	return ret;
}

static int tdf8532_start_play(struct tdf8532_priv *tdf8532)
{
	int ret;

	ret = tdf8532_amp_write(tdf8532, SET_CLK_STATE, CLK_CONNECT);
	if (ret < 0)
		return ret;

	ret = tdf8532_amp_write(tdf8532, SET_CHNL_ENABLE,
			CHNL_MASK(tdf8532->channels));

	if (ret >= 0)
		ret = tdf8532_wait_state(tdf8532, STATE_PLAY, ACK_TIMEOUT);

	return ret;
}


static int tdf8532_stop_play(struct tdf8532_priv *tdf8532)
{
	int ret;

	ret = tdf8532_amp_write(tdf8532, SET_CHNL_DISABLE,
			CHNL_MASK(tdf8532->channels));
	if (ret < 0)
		goto out;

	ret = tdf8532_wait_state(tdf8532, STATE_STBY, ACK_TIMEOUT);
	if (ret < 0)
		goto out;

	ret = tdf8532_amp_write(tdf8532, SET_CLK_STATE, CLK_DISCONNECT);
	if (ret < 0)
		goto out;

	ret = tdf8532_wait_state(tdf8532, STATE_IDLE, ACK_TIMEOUT);

out:
	return ret;
}


static int tdf8532_dai_trigger(struct snd_pcm_substream *substream, int cmd,
		struct snd_soc_dai *dai)
{
	int ret = 0;
	struct snd_soc_codec *codec = dai->codec;
	struct tdf8532_priv *tdf8532 = snd_soc_codec_get_drvdata(codec);

	dev_dbg(codec->dev, "%s: cmd = %d\n", __func__, cmd);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
		ret = tdf8532_start_play(tdf8532);
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
		ret = tdf8532_stop_play(tdf8532);
		break;
	}

	return ret;
}

static int tdf8532_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	struct tdf8532_priv *tdf8532 = snd_soc_codec_get_drvdata(dai->codec);

	dev_dbg(codec->dev, "%s\n", __func__);

	if (mute)
		return tdf8532_amp_write(tdf8532, SET_CHNL_MUTE,
				CHNL_MASK(CHNL_MAX));
	else
		return tdf8532_amp_write(tdf8532, SET_CHNL_UNMUTE,
				CHNL_MASK(CHNL_MAX));
}

static const struct snd_soc_dai_ops tdf8532_dai_ops = {
	.trigger  = tdf8532_dai_trigger,
	.digital_mute = tdf8532_mute,
};

static struct snd_soc_codec_driver soc_codec_tdf8532;

static struct snd_soc_dai_driver tdf8532_dai[] = {
	{
		.name = "tdf8532-hifi",
		.playback = {
			.stream_name = "Playback",
			.channels_min = 4,
			.channels_max = 4,
			.rates = SNDRV_PCM_RATE_48000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.ops = &tdf8532_dai_ops,
	}
};

static int tdf8532_i2c_probe(struct i2c_client *i2c,
		const struct i2c_device_id *id)
{
	int ret;
	struct tdf8532_priv *dev_data;
	struct device *dev = &(i2c->dev);

	dev_dbg(&i2c->dev, "%s\n", __func__);

	dev_data = devm_kzalloc(dev, sizeof(struct tdf8532_priv), GFP_KERNEL);

	if (!dev_data) {
		ret = -ENOMEM;
		goto out;
	}

	dev_data->i2c = i2c;
	dev_data->pkt_id = 0;
	dev_data->channels = 4;

	i2c_set_clientdata(i2c, dev_data);

	ret = snd_soc_register_codec(&i2c->dev, &soc_codec_tdf8532,
			tdf8532_dai, ARRAY_SIZE(tdf8532_dai));
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to register codec: %d\n", ret);
		goto out;
	}

out:
	return ret;
}

static int tdf8532_i2c_remove(struct i2c_client *i2c)
{
	snd_soc_unregister_codec(&i2c->dev);

	return 0;
}

static const struct i2c_device_id tdf8532_i2c_id[] = {
	{ "tdf8532", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, tdf8532_i2c_id);

#if CONFIG_ACPI
static const struct acpi_device_id tdf8532_acpi_match[] = {
	{"INT34C3", 0},
	{},
};

MODULE_DEVICE_TABLE(acpi, tdf8532_acpi_match);
#endif

static struct i2c_driver tdf8532_i2c_driver = {
	.driver = {
		.name = "tdf8532-codec",
		.owner = THIS_MODULE,
		.acpi_match_table = ACPI_PTR(tdf8532_acpi_match),
	},
	.probe =    tdf8532_i2c_probe,
	.remove =   tdf8532_i2c_remove,
	.id_table = tdf8532_i2c_id,
};

module_i2c_driver(tdf8532_i2c_driver);

MODULE_DESCRIPTION("ASoC NXP Semiconductors TDF8532 driver");
MODULE_AUTHOR("Steffen Wagner <steffen.wagner@intel.com>");
MODULE_LICENSE("GPL v2");
