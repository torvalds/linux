// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 Google, Inc.
 *
 * ChromeOS Embedded Controller codec driver.
 *
 * This driver uses the cros-ec interface to communicate with the ChromeOS
 * EC for audio function.
 */

#include <crypto/sha.h>
#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/platform_device.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

struct cros_ec_codec_priv {
	struct device *dev;
	struct cros_ec_device *ec_device;

	/* common */
	uint32_t ec_capabilities;

	uint64_t ec_shm_addr;
	uint32_t ec_shm_len;

	uint64_t ap_shm_phys_addr;
	uint32_t ap_shm_len;
	uint64_t ap_shm_addr;
	uint64_t ap_shm_last_alloc;

	/* DMIC */
	atomic_t dmic_probed;

	/* I2S_RX */
	uint32_t i2s_rx_bclk_ratio;

	/* WoV */
	bool wov_enabled;
	uint8_t *wov_audio_shm_p;
	uint32_t wov_audio_shm_len;
	uint8_t wov_audio_shm_type;
	uint8_t *wov_lang_shm_p;
	uint32_t wov_lang_shm_len;
	uint8_t wov_lang_shm_type;

	struct mutex wov_dma_lock;
	uint8_t wov_buf[64000];
	uint32_t wov_rp, wov_wp;
	size_t wov_dma_offset;
	bool wov_burst_read;
	struct snd_pcm_substream *wov_substream;
	struct delayed_work wov_copy_work;
	struct notifier_block wov_notifier;
};

static int ec_codec_capable(struct cros_ec_codec_priv *priv, uint8_t cap)
{
	return priv->ec_capabilities & BIT(cap);
}

static int send_ec_host_command(struct cros_ec_device *ec_dev, uint32_t cmd,
				uint8_t *out, size_t outsize,
				uint8_t *in, size_t insize)
{
	int ret;
	struct cros_ec_command *msg;

	msg = kmalloc(sizeof(*msg) + max(outsize, insize), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	msg->version = 0;
	msg->command = cmd;
	msg->outsize = outsize;
	msg->insize = insize;

	if (outsize)
		memcpy(msg->data, out, outsize);

	ret = cros_ec_cmd_xfer_status(ec_dev, msg);
	if (ret < 0)
		goto error;

	if (insize)
		memcpy(in, msg->data, insize);

	ret = 0;
error:
	kfree(msg);
	return ret;
}

static int dmic_get_gain(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct cros_ec_codec_priv *priv =
		snd_soc_component_get_drvdata(component);
	struct ec_param_ec_codec_dmic p;
	struct ec_response_ec_codec_dmic_get_gain_idx r;
	int ret;

	p.cmd = EC_CODEC_DMIC_GET_GAIN_IDX;
	p.get_gain_idx_param.channel = EC_CODEC_DMIC_CHANNEL_0;
	ret = send_ec_host_command(priv->ec_device, EC_CMD_EC_CODEC_DMIC,
				   (uint8_t *)&p, sizeof(p),
				   (uint8_t *)&r, sizeof(r));
	if (ret < 0)
		return ret;
	ucontrol->value.integer.value[0] = r.gain;

	p.cmd = EC_CODEC_DMIC_GET_GAIN_IDX;
	p.get_gain_idx_param.channel = EC_CODEC_DMIC_CHANNEL_1;
	ret = send_ec_host_command(priv->ec_device, EC_CMD_EC_CODEC_DMIC,
				   (uint8_t *)&p, sizeof(p),
				   (uint8_t *)&r, sizeof(r));
	if (ret < 0)
		return ret;
	ucontrol->value.integer.value[1] = r.gain;

	return 0;
}

static int dmic_put_gain(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct cros_ec_codec_priv *priv =
		snd_soc_component_get_drvdata(component);
	struct soc_mixer_control *control =
		(struct soc_mixer_control *)kcontrol->private_value;
	int max_dmic_gain = control->max;
	int left = ucontrol->value.integer.value[0];
	int right = ucontrol->value.integer.value[1];
	struct ec_param_ec_codec_dmic p;
	int ret;

	if (left > max_dmic_gain || right > max_dmic_gain)
		return -EINVAL;

	dev_dbg(component->dev, "set mic gain to %u, %u\n", left, right);

	p.cmd = EC_CODEC_DMIC_SET_GAIN_IDX;
	p.set_gain_idx_param.channel = EC_CODEC_DMIC_CHANNEL_0;
	p.set_gain_idx_param.gain = left;
	ret = send_ec_host_command(priv->ec_device, EC_CMD_EC_CODEC_DMIC,
				   (uint8_t *)&p, sizeof(p), NULL, 0);
	if (ret < 0)
		return ret;

	p.cmd = EC_CODEC_DMIC_SET_GAIN_IDX;
	p.set_gain_idx_param.channel = EC_CODEC_DMIC_CHANNEL_1;
	p.set_gain_idx_param.gain = right;
	return send_ec_host_command(priv->ec_device, EC_CMD_EC_CODEC_DMIC,
				    (uint8_t *)&p, sizeof(p), NULL, 0);
}

static const DECLARE_TLV_DB_SCALE(dmic_gain_tlv, 0, 100, 0);

enum {
	DMIC_CTL_GAIN = 0,
};

static struct snd_kcontrol_new dmic_controls[] = {
	[DMIC_CTL_GAIN] =
		SOC_DOUBLE_EXT_TLV("EC Mic Gain", SND_SOC_NOPM, SND_SOC_NOPM,
				   0, 0, 0, dmic_get_gain, dmic_put_gain,
				   dmic_gain_tlv),
};

static int dmic_probe(struct snd_soc_component *component)
{
	struct cros_ec_codec_priv *priv =
		snd_soc_component_get_drvdata(component);
	struct device *dev = priv->dev;
	struct soc_mixer_control *control;
	struct ec_param_ec_codec_dmic p;
	struct ec_response_ec_codec_dmic_get_max_gain r;
	int ret;

	if (!atomic_add_unless(&priv->dmic_probed, 1, 1))
		return 0;

	p.cmd = EC_CODEC_DMIC_GET_MAX_GAIN;

	ret = send_ec_host_command(priv->ec_device, EC_CMD_EC_CODEC_DMIC,
				   (uint8_t *)&p, sizeof(p),
				   (uint8_t *)&r, sizeof(r));
	if (ret < 0) {
		dev_warn(dev, "get_max_gain() unsupported\n");
		return 0;
	}

	dev_dbg(dev, "max gain = %d\n", r.max_gain);

	control = (struct soc_mixer_control *)
		dmic_controls[DMIC_CTL_GAIN].private_value;
	control->max = r.max_gain;
	control->platform_max = r.max_gain;

	return snd_soc_add_component_controls(component,
			&dmic_controls[DMIC_CTL_GAIN], 1);
}

static int i2s_rx_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct cros_ec_codec_priv *priv =
		snd_soc_component_get_drvdata(component);
	struct ec_param_ec_codec_i2s_rx p;
	enum ec_codec_i2s_rx_sample_depth depth;
	uint32_t bclk;
	int ret;

	if (params_rate(params) != 48000)
		return -EINVAL;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		depth = EC_CODEC_I2S_RX_SAMPLE_DEPTH_16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		depth = EC_CODEC_I2S_RX_SAMPLE_DEPTH_24;
		break;
	default:
		return -EINVAL;
	}

	dev_dbg(component->dev, "set depth to %u\n", depth);

	p.cmd = EC_CODEC_I2S_RX_SET_SAMPLE_DEPTH;
	p.set_sample_depth_param.depth = depth;
	ret = send_ec_host_command(priv->ec_device, EC_CMD_EC_CODEC_I2S_RX,
				   (uint8_t *)&p, sizeof(p), NULL, 0);
	if (ret < 0)
		return ret;

	if (priv->i2s_rx_bclk_ratio)
		bclk = params_rate(params) * priv->i2s_rx_bclk_ratio;
	else
		bclk = snd_soc_params_to_bclk(params);

	dev_dbg(component->dev, "set bclk to %u\n", bclk);

	p.cmd = EC_CODEC_I2S_RX_SET_BCLK;
	p.set_bclk_param.bclk = bclk;
	return send_ec_host_command(priv->ec_device, EC_CMD_EC_CODEC_I2S_RX,
				    (uint8_t *)&p, sizeof(p), NULL, 0);
}

static int i2s_rx_set_bclk_ratio(struct snd_soc_dai *dai, unsigned int ratio)
{
	struct snd_soc_component *component = dai->component;
	struct cros_ec_codec_priv *priv =
		snd_soc_component_get_drvdata(component);

	priv->i2s_rx_bclk_ratio = ratio;
	return 0;
}

static int i2s_rx_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	struct cros_ec_codec_priv *priv =
		snd_soc_component_get_drvdata(component);
	struct ec_param_ec_codec_i2s_rx p;
	enum ec_codec_i2s_rx_daifmt daifmt;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		daifmt = EC_CODEC_I2S_RX_DAIFMT_I2S;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		daifmt = EC_CODEC_I2S_RX_DAIFMT_RIGHT_J;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		daifmt = EC_CODEC_I2S_RX_DAIFMT_LEFT_J;
		break;
	default:
		return -EINVAL;
	}

	dev_dbg(component->dev, "set format to %u\n", daifmt);

	p.cmd = EC_CODEC_I2S_RX_SET_DAIFMT;
	p.set_daifmt_param.daifmt = daifmt;
	return send_ec_host_command(priv->ec_device, EC_CMD_EC_CODEC_I2S_RX,
				    (uint8_t *)&p, sizeof(p), NULL, 0);
}

static const struct snd_soc_dai_ops i2s_rx_dai_ops = {
	.hw_params = i2s_rx_hw_params,
	.set_fmt = i2s_rx_set_fmt,
	.set_bclk_ratio = i2s_rx_set_bclk_ratio,
};

static int i2s_rx_event(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct cros_ec_codec_priv *priv =
		snd_soc_component_get_drvdata(component);
	struct ec_param_ec_codec_i2s_rx p = {};

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		dev_dbg(component->dev, "enable I2S RX\n");
		p.cmd = EC_CODEC_I2S_RX_ENABLE;
		break;
	case SND_SOC_DAPM_PRE_PMD:
		dev_dbg(component->dev, "disable I2S RX\n");
		p.cmd = EC_CODEC_I2S_RX_DISABLE;
		break;
	default:
		return 0;
	}

	return send_ec_host_command(priv->ec_device, EC_CMD_EC_CODEC_I2S_RX,
				    (uint8_t *)&p, sizeof(p), NULL, 0);
}

static struct snd_soc_dapm_widget i2s_rx_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("DMIC"),
	SND_SOC_DAPM_SUPPLY("I2S RX Enable", SND_SOC_NOPM, 0, 0, i2s_rx_event,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_AIF_OUT("I2S RX", "I2S Capture", 0, SND_SOC_NOPM, 0, 0),
};

static struct snd_soc_dapm_route i2s_rx_dapm_routes[] = {
	{"I2S RX", NULL, "DMIC"},
	{"I2S RX", NULL, "I2S RX Enable"},
};

static struct snd_soc_dai_driver i2s_rx_dai_driver = {
	.name = "EC Codec I2S RX",
	.capture = {
		.stream_name = "I2S Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE |
			SNDRV_PCM_FMTBIT_S24_LE,
	},
	.ops = &i2s_rx_dai_ops,
};

static int i2s_rx_probe(struct snd_soc_component *component)
{
	return dmic_probe(component);
}

static const struct snd_soc_component_driver i2s_rx_component_driver = {
	.probe			= i2s_rx_probe,
	.dapm_widgets		= i2s_rx_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(i2s_rx_dapm_widgets),
	.dapm_routes		= i2s_rx_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(i2s_rx_dapm_routes),
};

static void *wov_map_shm(struct cros_ec_codec_priv *priv,
			 uint8_t shm_id, uint32_t *len, uint8_t *type)
{
	struct ec_param_ec_codec p;
	struct ec_response_ec_codec_get_shm_addr r;
	uint32_t req, offset;

	p.cmd = EC_CODEC_GET_SHM_ADDR;
	p.get_shm_addr_param.shm_id = shm_id;
	if (send_ec_host_command(priv->ec_device, EC_CMD_EC_CODEC,
				 (uint8_t *)&p, sizeof(p),
				 (uint8_t *)&r, sizeof(r)) < 0) {
		dev_err(priv->dev, "failed to EC_CODEC_GET_SHM_ADDR\n");
		return NULL;
	}

	dev_dbg(priv->dev, "phys_addr=%#llx, len=%#x\n", r.phys_addr, r.len);

	*len = r.len;
	*type = r.type;

	switch (r.type) {
	case EC_CODEC_SHM_TYPE_EC_RAM:
		return (void __force *)devm_ioremap_wc(priv->dev,
				r.phys_addr + priv->ec_shm_addr, r.len);
	case EC_CODEC_SHM_TYPE_SYSTEM_RAM:
		if (r.phys_addr) {
			dev_err(priv->dev, "unknown status\n");
			return NULL;
		}

		req = round_up(r.len, PAGE_SIZE);
		dev_dbg(priv->dev, "round up from %u to %u\n", r.len, req);

		if (priv->ap_shm_last_alloc + req >
		    priv->ap_shm_phys_addr + priv->ap_shm_len) {
			dev_err(priv->dev, "insufficient space for AP SHM\n");
			return NULL;
		}

		dev_dbg(priv->dev, "alloc AP SHM addr=%#llx, len=%#x\n",
			priv->ap_shm_last_alloc, req);

		p.cmd = EC_CODEC_SET_SHM_ADDR;
		p.set_shm_addr_param.phys_addr = priv->ap_shm_last_alloc;
		p.set_shm_addr_param.len = req;
		p.set_shm_addr_param.shm_id = shm_id;
		if (send_ec_host_command(priv->ec_device, EC_CMD_EC_CODEC,
					 (uint8_t *)&p, sizeof(p),
					 NULL, 0) < 0) {
			dev_err(priv->dev, "failed to EC_CODEC_SET_SHM_ADDR\n");
			return NULL;
		}

		/*
		 * Note: EC codec only requests for `r.len' but we allocate
		 * round up PAGE_SIZE `req'.
		 */
		offset = priv->ap_shm_last_alloc - priv->ap_shm_phys_addr;
		priv->ap_shm_last_alloc += req;

		return (void *)(uintptr_t)(priv->ap_shm_addr + offset);
	default:
		return NULL;
	}
}

static bool wov_queue_full(struct cros_ec_codec_priv *priv)
{
	return ((priv->wov_wp + 1) % sizeof(priv->wov_buf)) == priv->wov_rp;
}

static size_t wov_queue_size(struct cros_ec_codec_priv *priv)
{
	if (priv->wov_wp >= priv->wov_rp)
		return priv->wov_wp - priv->wov_rp;
	else
		return sizeof(priv->wov_buf) - priv->wov_rp + priv->wov_wp;
}

static void wov_queue_dequeue(struct cros_ec_codec_priv *priv, size_t len)
{
	struct snd_pcm_runtime *runtime = priv->wov_substream->runtime;
	size_t req;

	while (len) {
		req = min(len, runtime->dma_bytes - priv->wov_dma_offset);
		if (priv->wov_wp >= priv->wov_rp)
			req = min(req, (size_t)priv->wov_wp - priv->wov_rp);
		else
			req = min(req, sizeof(priv->wov_buf) - priv->wov_rp);

		memcpy(runtime->dma_area + priv->wov_dma_offset,
		       priv->wov_buf + priv->wov_rp, req);

		priv->wov_dma_offset += req;
		if (priv->wov_dma_offset == runtime->dma_bytes)
			priv->wov_dma_offset = 0;

		priv->wov_rp += req;
		if (priv->wov_rp == sizeof(priv->wov_buf))
			priv->wov_rp = 0;

		len -= req;
	}

	snd_pcm_period_elapsed(priv->wov_substream);
}

static void wov_queue_try_dequeue(struct cros_ec_codec_priv *priv)
{
	size_t period_bytes = snd_pcm_lib_period_bytes(priv->wov_substream);

	while (period_bytes && wov_queue_size(priv) >= period_bytes) {
		wov_queue_dequeue(priv, period_bytes);
		period_bytes = snd_pcm_lib_period_bytes(priv->wov_substream);
	}
}

static void wov_queue_enqueue(struct cros_ec_codec_priv *priv,
			      uint8_t *addr, size_t len, bool iomem)
{
	size_t req;

	while (len) {
		if (wov_queue_full(priv)) {
			wov_queue_try_dequeue(priv);

			if (wov_queue_full(priv)) {
				dev_err(priv->dev, "overrun detected\n");
				return;
			}
		}

		if (priv->wov_wp >= priv->wov_rp)
			req = sizeof(priv->wov_buf) - priv->wov_wp;
		else
			/* Note: waste 1-byte to differentiate full and empty */
			req = priv->wov_rp - priv->wov_wp - 1;
		req = min(req, len);

		if (iomem)
			memcpy_fromio(priv->wov_buf + priv->wov_wp,
				      (void __force __iomem *)addr, req);
		else
			memcpy(priv->wov_buf + priv->wov_wp, addr, req);

		priv->wov_wp += req;
		if (priv->wov_wp == sizeof(priv->wov_buf))
			priv->wov_wp = 0;

		addr += req;
		len -= req;
	}

	wov_queue_try_dequeue(priv);
}

static int wov_read_audio_shm(struct cros_ec_codec_priv *priv)
{
	struct ec_param_ec_codec_wov p;
	struct ec_response_ec_codec_wov_read_audio_shm r;
	int ret;

	p.cmd = EC_CODEC_WOV_READ_AUDIO_SHM;
	ret = send_ec_host_command(priv->ec_device, EC_CMD_EC_CODEC_WOV,
				   (uint8_t *)&p, sizeof(p),
				   (uint8_t *)&r, sizeof(r));
	if (ret) {
		dev_err(priv->dev, "failed to EC_CODEC_WOV_READ_AUDIO_SHM\n");
		return ret;
	}

	if (!r.len)
		dev_dbg(priv->dev, "no data, sleep\n");
	else
		wov_queue_enqueue(priv, priv->wov_audio_shm_p + r.offset, r.len,
			priv->wov_audio_shm_type == EC_CODEC_SHM_TYPE_EC_RAM);
	return -EAGAIN;
}

static int wov_read_audio(struct cros_ec_codec_priv *priv)
{
	struct ec_param_ec_codec_wov p;
	struct ec_response_ec_codec_wov_read_audio r;
	int remain = priv->wov_burst_read ? 16000 : 320;
	int ret;

	while (remain >= 0) {
		p.cmd = EC_CODEC_WOV_READ_AUDIO;
		ret = send_ec_host_command(priv->ec_device, EC_CMD_EC_CODEC_WOV,
					   (uint8_t *)&p, sizeof(p),
					   (uint8_t *)&r, sizeof(r));
		if (ret) {
			dev_err(priv->dev,
				"failed to EC_CODEC_WOV_READ_AUDIO\n");
			return ret;
		}

		if (!r.len) {
			dev_dbg(priv->dev, "no data, sleep\n");
			priv->wov_burst_read = false;
			break;
		}

		wov_queue_enqueue(priv, r.buf, r.len, false);
		remain -= r.len;
	}

	return -EAGAIN;
}

static void wov_copy_work(struct work_struct *w)
{
	struct cros_ec_codec_priv *priv =
		container_of(w, struct cros_ec_codec_priv, wov_copy_work.work);
	int ret;

	mutex_lock(&priv->wov_dma_lock);
	if (!priv->wov_substream) {
		dev_warn(priv->dev, "no pcm substream\n");
		goto leave;
	}

	if (ec_codec_capable(priv, EC_CODEC_CAP_WOV_AUDIO_SHM))
		ret = wov_read_audio_shm(priv);
	else
		ret = wov_read_audio(priv);

	if (ret == -EAGAIN)
		schedule_delayed_work(&priv->wov_copy_work,
				      msecs_to_jiffies(10));
	else if (ret)
		dev_err(priv->dev, "failed to read audio data\n");
leave:
	mutex_unlock(&priv->wov_dma_lock);
}

static int wov_enable_get(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *c = snd_soc_kcontrol_component(kcontrol);
	struct cros_ec_codec_priv *priv = snd_soc_component_get_drvdata(c);

	ucontrol->value.integer.value[0] = priv->wov_enabled;
	return 0;
}

static int wov_enable_put(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *c = snd_soc_kcontrol_component(kcontrol);
	struct cros_ec_codec_priv *priv = snd_soc_component_get_drvdata(c);
	int enabled = ucontrol->value.integer.value[0];
	struct ec_param_ec_codec_wov p;
	int ret;

	if (priv->wov_enabled != enabled) {
		if (enabled)
			p.cmd = EC_CODEC_WOV_ENABLE;
		else
			p.cmd = EC_CODEC_WOV_DISABLE;

		ret = send_ec_host_command(priv->ec_device, EC_CMD_EC_CODEC_WOV,
					   (uint8_t *)&p, sizeof(p), NULL, 0);
		if (ret) {
			dev_err(priv->dev, "failed to %s wov\n",
				enabled ? "enable" : "disable");
			return ret;
		}

		priv->wov_enabled = enabled;
	}

	return 0;
}

static int wov_set_lang_shm(struct cros_ec_codec_priv *priv,
			    uint8_t *buf, size_t size, uint8_t *digest)
{
	struct ec_param_ec_codec_wov p;
	struct ec_param_ec_codec_wov_set_lang_shm *pp = &p.set_lang_shm_param;
	int ret;

	if (size > priv->wov_lang_shm_len) {
		dev_err(priv->dev, "no enough SHM size: %d\n",
			priv->wov_lang_shm_len);
		return -EIO;
	}

	switch (priv->wov_lang_shm_type) {
	case EC_CODEC_SHM_TYPE_EC_RAM:
		memcpy_toio((void __force __iomem *)priv->wov_lang_shm_p,
			    buf, size);
		memset_io((void __force __iomem *)priv->wov_lang_shm_p + size,
			  0, priv->wov_lang_shm_len - size);
		break;
	case EC_CODEC_SHM_TYPE_SYSTEM_RAM:
		memcpy(priv->wov_lang_shm_p, buf, size);
		memset(priv->wov_lang_shm_p + size, 0,
		       priv->wov_lang_shm_len - size);

		/* make sure write to memory before calling host command */
		wmb();
		break;
	}

	p.cmd = EC_CODEC_WOV_SET_LANG_SHM;
	memcpy(pp->hash, digest, SHA256_DIGEST_SIZE);
	pp->total_len = size;
	ret = send_ec_host_command(priv->ec_device, EC_CMD_EC_CODEC_WOV,
				   (uint8_t *)&p, sizeof(p), NULL, 0);
	if (ret) {
		dev_err(priv->dev, "failed to EC_CODEC_WOV_SET_LANG_SHM\n");
		return ret;
	}

	return 0;
}

static int wov_set_lang(struct cros_ec_codec_priv *priv,
			uint8_t *buf, size_t size, uint8_t *digest)
{
	struct ec_param_ec_codec_wov p;
	struct ec_param_ec_codec_wov_set_lang *pp = &p.set_lang_param;
	size_t i, req;
	int ret;

	for (i = 0; i < size; i += req) {
		req = min(size - i, ARRAY_SIZE(pp->buf));

		p.cmd = EC_CODEC_WOV_SET_LANG;
		memcpy(pp->hash, digest, SHA256_DIGEST_SIZE);
		pp->total_len = size;
		pp->offset = i;
		memcpy(pp->buf, buf + i, req);
		pp->len = req;
		ret = send_ec_host_command(priv->ec_device, EC_CMD_EC_CODEC_WOV,
					   (uint8_t *)&p, sizeof(p), NULL, 0);
		if (ret) {
			dev_err(priv->dev, "failed to EC_CODEC_WOV_SET_LANG\n");
			return ret;
		}
	}

	return 0;
}

static int wov_hotword_model_put(struct snd_kcontrol *kcontrol,
				 const unsigned int __user *bytes,
				 unsigned int size)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct cros_ec_codec_priv *priv =
		snd_soc_component_get_drvdata(component);
	struct ec_param_ec_codec_wov p;
	struct ec_response_ec_codec_wov_get_lang r;
	uint8_t digest[SHA256_DIGEST_SIZE];
	uint8_t *buf;
	int ret;

	/* Skips the TLV header. */
	bytes += 2;
	size -= 8;

	dev_dbg(priv->dev, "%s: size=%d\n", __func__, size);

	buf = memdup_user(bytes, size);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	sha256(buf, size, digest);
	dev_dbg(priv->dev, "hash=%*phN\n", SHA256_DIGEST_SIZE, digest);

	p.cmd = EC_CODEC_WOV_GET_LANG;
	ret = send_ec_host_command(priv->ec_device, EC_CMD_EC_CODEC_WOV,
				   (uint8_t *)&p, sizeof(p),
				   (uint8_t *)&r, sizeof(r));
	if (ret)
		goto leave;

	if (memcmp(digest, r.hash, SHA256_DIGEST_SIZE) == 0) {
		dev_dbg(priv->dev, "not updated");
		goto leave;
	}

	if (ec_codec_capable(priv, EC_CODEC_CAP_WOV_LANG_SHM))
		ret = wov_set_lang_shm(priv, buf, size, digest);
	else
		ret = wov_set_lang(priv, buf, size, digest);

leave:
	kfree(buf);
	return ret;
}

static struct snd_kcontrol_new wov_controls[] = {
	SOC_SINGLE_BOOL_EXT("Wake-on-Voice Switch", 0,
			    wov_enable_get, wov_enable_put),
	SND_SOC_BYTES_TLV("Hotword Model", 0x11000, NULL,
			  wov_hotword_model_put),
};

static struct snd_soc_dai_driver wov_dai_driver = {
	.name = "Wake on Voice",
	.capture = {
		.stream_name = "WoV Capture",
		.channels_min = 1,
		.channels_max = 1,
		.rates = SNDRV_PCM_RATE_16000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
};

static int wov_host_event(struct notifier_block *nb,
			  unsigned long queued_during_suspend, void *notify)
{
	struct cros_ec_codec_priv *priv =
		container_of(nb, struct cros_ec_codec_priv, wov_notifier);
	u32 host_event;

	dev_dbg(priv->dev, "%s\n", __func__);

	host_event = cros_ec_get_host_event(priv->ec_device);
	if (host_event & EC_HOST_EVENT_MASK(EC_HOST_EVENT_WOV)) {
		schedule_delayed_work(&priv->wov_copy_work, 0);
		return NOTIFY_OK;
	} else {
		return NOTIFY_DONE;
	}
}

static int wov_probe(struct snd_soc_component *component)
{
	struct cros_ec_codec_priv *priv =
		snd_soc_component_get_drvdata(component);
	int ret;

	mutex_init(&priv->wov_dma_lock);
	INIT_DELAYED_WORK(&priv->wov_copy_work, wov_copy_work);

	priv->wov_notifier.notifier_call = wov_host_event;
	ret = blocking_notifier_chain_register(
			&priv->ec_device->event_notifier, &priv->wov_notifier);
	if (ret)
		return ret;

	if (ec_codec_capable(priv, EC_CODEC_CAP_WOV_LANG_SHM)) {
		priv->wov_lang_shm_p = wov_map_shm(priv,
				EC_CODEC_SHM_ID_WOV_LANG,
				&priv->wov_lang_shm_len,
				&priv->wov_lang_shm_type);
		if (!priv->wov_lang_shm_p)
			return -EFAULT;
	}

	if (ec_codec_capable(priv, EC_CODEC_CAP_WOV_AUDIO_SHM)) {
		priv->wov_audio_shm_p = wov_map_shm(priv,
				EC_CODEC_SHM_ID_WOV_AUDIO,
				&priv->wov_audio_shm_len,
				&priv->wov_audio_shm_type);
		if (!priv->wov_audio_shm_p)
			return -EFAULT;
	}

	return dmic_probe(component);
}

static void wov_remove(struct snd_soc_component *component)
{
	struct cros_ec_codec_priv *priv =
		snd_soc_component_get_drvdata(component);

	blocking_notifier_chain_unregister(
			&priv->ec_device->event_notifier, &priv->wov_notifier);
}

static int wov_pcm_open(struct snd_soc_component *component,
			struct snd_pcm_substream *substream)
{
	static const struct snd_pcm_hardware hw_param = {
		.info = SNDRV_PCM_INFO_MMAP |
			SNDRV_PCM_INFO_INTERLEAVED |
			SNDRV_PCM_INFO_MMAP_VALID,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
		.rates = SNDRV_PCM_RATE_16000,
		.channels_min = 1,
		.channels_max = 1,
		.period_bytes_min = PAGE_SIZE,
		.period_bytes_max = 0x20000 / 8,
		.periods_min = 8,
		.periods_max = 8,
		.buffer_bytes_max = 0x20000,
	};

	return snd_soc_set_runtime_hwparams(substream, &hw_param);
}

static int wov_pcm_hw_params(struct snd_soc_component *component,
			     struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *hw_params)
{
	struct cros_ec_codec_priv *priv =
		snd_soc_component_get_drvdata(component);

	mutex_lock(&priv->wov_dma_lock);
	priv->wov_substream = substream;
	priv->wov_rp = priv->wov_wp = 0;
	priv->wov_dma_offset = 0;
	priv->wov_burst_read = true;
	mutex_unlock(&priv->wov_dma_lock);

	return 0;
}

static int wov_pcm_hw_free(struct snd_soc_component *component,
			   struct snd_pcm_substream *substream)
{
	struct cros_ec_codec_priv *priv =
		snd_soc_component_get_drvdata(component);

	mutex_lock(&priv->wov_dma_lock);
	wov_queue_dequeue(priv, wov_queue_size(priv));
	priv->wov_substream = NULL;
	mutex_unlock(&priv->wov_dma_lock);

	cancel_delayed_work_sync(&priv->wov_copy_work);

	return 0;
}

static snd_pcm_uframes_t wov_pcm_pointer(struct snd_soc_component *component,
					 struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct cros_ec_codec_priv *priv =
		snd_soc_component_get_drvdata(component);

	return bytes_to_frames(runtime, priv->wov_dma_offset);
}

static int wov_pcm_new(struct snd_soc_component *component,
		       struct snd_soc_pcm_runtime *rtd)
{
	snd_pcm_set_managed_buffer_all(rtd->pcm, SNDRV_DMA_TYPE_VMALLOC,
				       NULL, 0, 0);
	return 0;
}

static const struct snd_soc_component_driver wov_component_driver = {
	.probe		= wov_probe,
	.remove		= wov_remove,
	.controls	= wov_controls,
	.num_controls	= ARRAY_SIZE(wov_controls),
	.open		= wov_pcm_open,
	.hw_params	= wov_pcm_hw_params,
	.hw_free	= wov_pcm_hw_free,
	.pointer	= wov_pcm_pointer,
	.pcm_construct	= wov_pcm_new,
};

static int cros_ec_codec_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cros_ec_device *ec_device = dev_get_drvdata(pdev->dev.parent);
	struct cros_ec_codec_priv *priv;
	struct ec_param_ec_codec p;
	struct ec_response_ec_codec_get_capabilities r;
	int ret;
#ifdef CONFIG_OF
	struct device_node *node;
	struct resource res;
	u64 ec_shm_size;
	const __be32 *regaddr_p;
#endif

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

#ifdef CONFIG_OF
	regaddr_p = of_get_address(dev->of_node, 0, &ec_shm_size, NULL);
	if (regaddr_p) {
		priv->ec_shm_addr = of_read_number(regaddr_p, 2);
		priv->ec_shm_len = ec_shm_size;

		dev_dbg(dev, "ec_shm_addr=%#llx len=%#x\n",
			priv->ec_shm_addr, priv->ec_shm_len);
	}

	node = of_parse_phandle(dev->of_node, "memory-region", 0);
	if (node) {
		ret = of_address_to_resource(node, 0, &res);
		if (!ret) {
			priv->ap_shm_phys_addr = res.start;
			priv->ap_shm_len = resource_size(&res);
			priv->ap_shm_addr =
				(uint64_t)(uintptr_t)devm_ioremap_wc(
					dev, priv->ap_shm_phys_addr,
					priv->ap_shm_len);
			priv->ap_shm_last_alloc = priv->ap_shm_phys_addr;

			dev_dbg(dev, "ap_shm_phys_addr=%#llx len=%#x\n",
				priv->ap_shm_phys_addr, priv->ap_shm_len);
		}
		of_node_put(node);
	}
#endif

	priv->dev = dev;
	priv->ec_device = ec_device;
	atomic_set(&priv->dmic_probed, 0);

	p.cmd = EC_CODEC_GET_CAPABILITIES;
	ret = send_ec_host_command(priv->ec_device, EC_CMD_EC_CODEC,
				   (uint8_t *)&p, sizeof(p),
				   (uint8_t *)&r, sizeof(r));
	if (ret) {
		dev_err(dev, "failed to EC_CODEC_GET_CAPABILITIES\n");
		return ret;
	}
	priv->ec_capabilities = r.capabilities;

	platform_set_drvdata(pdev, priv);

	ret = devm_snd_soc_register_component(dev, &i2s_rx_component_driver,
					      &i2s_rx_dai_driver, 1);
	if (ret)
		return ret;

	return devm_snd_soc_register_component(dev, &wov_component_driver,
					       &wov_dai_driver, 1);
}

#ifdef CONFIG_OF
static const struct of_device_id cros_ec_codec_of_match[] = {
	{ .compatible = "google,cros-ec-codec" },
	{},
};
MODULE_DEVICE_TABLE(of, cros_ec_codec_of_match);
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id cros_ec_codec_acpi_id[] = {
	{ "GOOG0013", 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, cros_ec_codec_acpi_id);
#endif

static struct platform_driver cros_ec_codec_platform_driver = {
	.driver = {
		.name = "cros-ec-codec",
		.of_match_table = of_match_ptr(cros_ec_codec_of_match),
		.acpi_match_table = ACPI_PTR(cros_ec_codec_acpi_id),
	},
	.probe = cros_ec_codec_platform_probe,
};

module_platform_driver(cros_ec_codec_platform_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ChromeOS EC codec driver");
MODULE_AUTHOR("Cheng-Yi Chiang <cychiang@chromium.org>");
MODULE_ALIAS("platform:cros-ec-codec");
